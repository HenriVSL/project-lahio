/* net.cpp - power and water networks, service coverage, road reach. PORTABLE. Owner: T04. See net.h.

   Cost model (the P2-233 VM is ~100x slower than the dev Mac for this code, measured with
   probe/probe.cpp's own sim kernels): every solver must stay near 3 ms at minimum spec on a dense
   128^2 city, so
   - coverage runs on the 2x2 CELL graph (a cell is passable if it holds an access road); a lot seeds
     from its footprint-centre cell in O(1);
   - road reach (Chebyshev distance to an access road, capped at RR_NONE) is updated incrementally
     for the dirty rect only (full 2-pass distance transform on reset / big changes);
   - power is a tile-exact flood fill (power lines are tile-precise); one solver per net_step call:
     power and water alternate when both are dirty;
   - F1 water (docs/research/WATER.md): tile-exact multi-source BFS over pipes, conducting lots and
     zoned land, nearest-first allocation per source, network spare flows on (solve_water).
   All work arrays are static; BFS uses generation stamps instead of clearing. Deterministic: lots are
   visited in id order, cells/tiles in BFS order. */
#include "common.h"
#include "net.h"
#include "world.h"
#include "budget.h"
#include "util.h"
#include "politics.h"
#include "complex.h"            /* M6: CPX_DEF_USE (complex modules: power/water per stage) */

#define NTILES      (MAP_MAX_W * MAP_MAX_H)
#define NCELLS      (FIELD_MAX_W * FIELD_MAX_H)
#define CELL_IDX(cx, cy) ((cy) * FIELD_MAX_W + (cx))
#define RR_NONE     4           /* road reach cap: "no access road within 3" */
#define MAX_COMP    1024
#define POWER_JUMP_ROADS 1      /* power conducts across one plain road tile */
#define PUMP_PER_WATER_TILE 40
#define COV_NONE    255
#define DIST_NONE   0xFFFF

/* ---- tile scratch (power flood) */
static u16 s_mark[NTILES];
static u16 s_gen;
static u16 s_queue[NTILES];
static u8  s_newbits[NTILES];

/* ---- roads */
static u8  s_roadreach[NTILES];
static u8  s_cell_acc[NCELLS];        /* access road tiles in the cell */
static int s_rr_dirty, s_rr_x0, s_rr_y0, s_rr_x1, s_rr_y1;   /* 0 clean, 1 rect, 2 full */

/* ---- cell scratch (coverage) */
static u16 s_cmark[NCELLS];
static u16 s_cgen;
static u16 s_cq[NCELLS];
static u16 s_cdist[NCELLS], s_csrc[NCELLS];
static u8  s_ctag[NCELLS];            /* coverage BFS: 0 road cell, 1/2 leaf ring */

/* ---- coverage results */
static u8  s_cov_dist[COV_KINDS][NCELLS];
static u8  s_cov_q[COV_KINDS][NCELLS];   /* quality 0..100 per cell (lookup for growth/fields) */
static u16 s_cov_src[COV_KINDS][NCELLS];
static int s_cov_nsrc[COV_KINDS];
static u32 s_cov_dirty;
static int s_cov_rr;
static s32 s_load_svc[MAX_LOTS], s_load_shop[MAX_LOTS], s_catch[MAX_LOTS];
static u8  s_bq[MAX_LOTS];

/* ---- networks */
static u8  s_lot_pstate[MAX_LOTS], s_lot_wstate[MAX_LOTS], s_lot_wnew[MAX_LOTS];
static s32 s_lot_out[MAX_LOTS];
static u16 s_lot_gen[MAX_LOTS];
static u16 s_order[MAX_LOTS];
static u16 s_cnt[1025];
static u16 s_uf[MAX_COMP];
static u8  s_def_cond[B_COUNT];
static u32 s_def_srcmask[B_COUNT];     /* coverage kinds a def is a source for (COV_RES: if occupied) */
static NetStats s_ns[2];
static int s_dirty, s_turn, s_elem_factories;
/* incremental state from the last full flood: components as lot lists in BFS order */
static u16 s_tcomp[NTILES];             /* component of a flooded tile (valid where s_mark == s_pgen) */
static u16 s_pgen;                      /* s_gen of the last full flood */
static int s_pbb[4], s_pbb_full = 1;     /* PL4: box of tiles that may carry power bits (solve_power_full) */
static u32 s_pserial;                   /* world_struct_serial() at the last full flood */
static u16 s_pnext[MAX_LOTS], s_lot_pcomp[MAX_LOTS];
static u8  s_pknown[MAX_LOTS];
static u16 s_pc_head[MAX_COMP], s_pc_tail[MAX_COMP];
static s32 s_pc_supply[MAX_COMP];
static int s_pncomp, s_pvalid;
/* F1 water: tile graph (pipes + conducting lots + zoned land), see solve_water */
#define MAX_WSRC    (MAX_COMP - 1)
enum { WSF_POWERED = 1, WSF_AT_CAP = 2, WSF_PUMP = 4, WSF_TOWER = 8 };
typedef struct {
    u16 lot, net;                         /* net = union-find root (a source index) */
    s32 output, own_used, shared_used, left;
    s32 dry_units;
    u16 lots, lots_dry, reach, cut;       /* served/dry consumers (use > 0), reach = furthest served,
                                             cut = nearest dry (DIST_NONE: none) */
    u16 pressure_lots, shore;
    u8  flags, pad;
} WaterSrc;
static WaterSrc s_ws[MAX_WSRC + 1];       /* 1..s_nws */
static int s_nws;
/* per tile from the last BFS: (source index << 16) | distance, WI_NONE = not reached. Own arrays: the
   power incremental state lives in s_mark/s_tcomp. */
#define WI_NONE     0xFFFFFFFFu
#define WI_BLOCK    0xFFFFFFFEu                /* during/after a BFS: not passable (one read per neighbour) */
#define WI_REACHED(v) ((v) < WI_BLOCK)
static int s_wbb[4], s_wbb_old[4];          /* bounding box of the reached tiles: this BFS / the one before */
static u8  s_lw_ok[MAX_LOTS];               /* a lot's tiles carry its water bits (until its key changes) */
static u32 s_winfo[NTILES];
static u16 s_wq[NTILES];                  /* BFS queue */
static u8  s_wpass[NTILES];               /* passability (kept exact incrementally, see solve_water) */
static int s_wr_dirty, s_wr_x0, s_wr_y0, s_wr_x1, s_wr_y1;   /* pipes/zones/roads changed here */
static u32 s_lc_key[MAX_LOTS];            /* lot key at the last solve (x, y, rot, def; 0 = none) */
static u8  s_lc_ok[MAX_LOTS];             /* s_lot_wdist/wown valid for that key and the current BFS */
static int s_lc_hi;
static u32 s_wwb_key[MAX_WSRC + 1];       /* per source: inputs of its tiles' bits at the last write-back */
static u8  s_wwb_chg[MAX_WSRC + 1];
static u32 s_wtower_sig;
static u16 s_tq[NTILES], s_tqs[NTILES];   /* tower BFS queue, tower (source index) per tile */
static u8  s_tpress[NTILES];              /* empty tile inside a tower's pressure zone */
static u8  s_tqd[NTILES];                 /* tower BFS distance */
static u8  s_lot_press[MAX_LOTS];         /* lot inside a tower's pressure zone */
static u16 s_lot_wown[MAX_LOTS], s_lot_wdist[MAX_LOTS];
static u8  s_lot_wserved[MAX_LOTS];
static s32 s_wpool[MAX_WSRC + 1], s_wpool0[MAX_WSRC + 1], s_wnsup[MAX_WSRC + 1];
static s32 s_wgiven[MAX_WSRC + 1];
static u16 s_wfirst[MAX_WSRC + 1];
static u8  s_wcut[MAX_WSRC + 1], s_wcutn[MAX_WSRC + 1];
static u32 s_wsig, s_wpump_sig;
static int s_wvalid;
static int s_wbfs_count;                  /* BFS runs (tests: fast path vs full) */

/* F1 fix: the lot stamps wrap with the tile stamps (a stale s_lot_gen equal to the new s_gen made the full
   flood skip that lot: history-dependent brownouts after ~65k floods in one process) */
static void next_gen(void)  { if (++s_gen == 0)  { memset(s_mark, 0, sizeof(s_mark)); memset(s_lot_gen, 0, sizeof(s_lot_gen)); s_gen = 1; } }
static void next_cgen(void) { if (++s_cgen == 0) { memset(s_cmark, 0, sizeof(s_cmark)); s_cgen = 1; } }
static int  lot_active(const Lot *l) { return l->def != B_NONE && l->status == LOT_ACTIVE; }
static int  cells_w(void) { return (g_world.w + 1) >> FIELD_SHIFT; }
static int  cells_h(void) { return (g_world.h + 1) >> FIELD_SHIFT; }

static int is_access_road(int idx)
{
    int c = g_world.t[idx].road & TR_CLASS_MASK;
    return c && (g_roads[c].flags & RDF_ZONE_ACCESS);
}

/* footprint-centre cell of a lot (no call chain: hot in every lot loop) */
static int lot_cell(const Lot *l)
{
    const BuildingDef *d = &g_bdefs[l->def];
    int w = (l->rot & 1) ? d->h : d->w, h = (l->rot & 1) ? d->w : d->h;
    return CELL_IDX((l->x + ((w - 1) >> 1)) >> FIELD_SHIFT, (l->y + ((h - 1) >> 1)) >> FIELD_SHIFT);
}

/* ------------------------------------------------------------------ lifecycle */
void net_init(void)
{
    memset(s_mark, 0, sizeof(s_mark)); memset(s_lot_gen, 0, sizeof(s_lot_gen)); s_gen = 0;   /* F1 fix: lot stamps too */
    memset(s_cmark, 0, sizeof(s_cmark)); s_cgen = 0;
    net_reset();
}

void net_reset(void)
{
    int i;
    for (i = 0; i < B_COUNT; i++) {
        const BuildingDef *d = &g_bdefs[i];
        u32 m = 0;
        s_def_cond[i] = (u8)(i != B_NONE && !(d->flags & BDF_NO_CONDUCT));
        if (i != B_NONE) {
            if (d->cat == BCAT_SERVICE) m |= 1u << d->service;
            if (d->cat == BCAT_ZONE && d->zone == ZONE_COMMERCIAL) m |= 1u << COV_SHOP;
            if ((d->cat == BCAT_ZONE && (d->zone == ZONE_COMMERCIAL || d->zone == ZONE_INDUSTRY)) || d->jobs > 0) m |= 1u << COV_JOBS;
            if (d->cat == BCAT_ZONE && (d->zone == ZONE_HOUSES || d->zone == ZONE_APARTMENTS)) m |= 1u << COV_RES;
        }
        s_def_srcmask[i] = m;
    }
    memset(s_cov_dist, COV_NONE, sizeof(s_cov_dist));
    memset(s_cov_q, 0, sizeof(s_cov_q));
    memset(s_cov_src, 0, sizeof(s_cov_src));
    memset(s_cov_nsrc, 0, sizeof(s_cov_nsrc));
    memset(s_load_svc, 0, sizeof(s_load_svc));
    memset(s_load_shop, 0, sizeof(s_load_shop));
    memset(s_catch, 0, sizeof(s_catch));
    memset(s_bq, 0, sizeof(s_bq));
    memset(s_lot_pstate, 0, sizeof(s_lot_pstate));
    memset(s_lot_wstate, 0, sizeof(s_lot_wstate));
    memset(s_lot_out, 0, sizeof(s_lot_out));
    memset(s_ns, 0, sizeof(s_ns));
    memset(s_tpress, 0, sizeof(s_tpress));
    memset(s_lot_press, 0, sizeof(s_lot_press));
    memset(s_lot_wown, 0, sizeof(s_lot_wown));
    memset(s_lot_wdist, 0xFF, sizeof(s_lot_wdist));
    memset(s_ws, 0, sizeof(s_ws));
    s_nws = 0; s_wpump_sig = 0; s_wsig = 0; s_wtower_sig = 0;
    memset(s_wpass, 0, sizeof(s_wpass)); memset(s_wwb_key, 0, sizeof(s_wwb_key)); memset(s_wwb_chg, 0, sizeof(s_wwb_chg));
    memset(s_winfo, 0xFF, sizeof(s_winfo));  /* nothing reached */
    s_wr_dirty = 0;
    memset(s_lc_key, 0, sizeof(s_lc_key)); memset(s_lc_ok, 0, sizeof(s_lc_ok)); s_lc_hi = 1;
    memset(s_lw_ok, 0, sizeof(s_lw_ok)); s_wbb[0] = s_wbb[1] = 9999; s_wbb[2] = s_wbb[3] = -1; memcpy(s_wbb_old, s_wbb, sizeof(s_wbb));
    s_cov_dirty = (1u << COV_KINDS) - 1;
    s_cov_rr = 0;
    s_dirty = NET_POWER | NET_WATER;
    s_rr_dirty = 2;
    memset(s_roadreach, RR_NONE, sizeof(s_roadreach));   /* M3: no stale reach from the previous city until phase 9 */
    s_elem_factories = 0;
    s_turn = 0;
    s_pvalid = 0;
    s_wvalid = 0;
    s_pbb_full = 1; s_pbb[2] = -1;                       /* PL4 */
}

void net_mark_dirty(int nets)
{
    s_dirty |= nets & (NET_POWER | NET_WATER);
    if (nets & NET_COVERAGE) s_cov_dirty = (1u << COV_KINDS) - 1;
}

void net_mark_roads(int x0, int y0, int x1, int y1)
{
    if (s_rr_dirty == 2) return;
    if (!s_rr_dirty) { s_rr_x0 = x0; s_rr_y0 = y0; s_rr_x1 = x1; s_rr_y1 = y1; s_rr_dirty = 1; }
    else {
        s_rr_x0 = IMIN(s_rr_x0, x0); s_rr_y0 = IMIN(s_rr_y0, y0);
        s_rr_x1 = IMAX(s_rr_x1, x1); s_rr_y1 = IMAX(s_rr_y1, y1);
    }
    if ((s_rr_x1 - s_rr_x0 + 1) * (s_rr_y1 - s_rr_y0 + 1) > g_world.w * g_world.h / 4) s_rr_dirty = 2;
}

/* ------------------------------------------------------------------ road reach + road cells */
static void update_roads(void)
{
    int x0, y0, x1, y1, ex0, ey0, ex1, ey1, x, y, w = g_world.w, h = g_world.h, cx, cy;
    if (!s_rr_dirty) return;
    if (s_rr_dirty == 2) { x0 = 0; y0 = 0; x1 = w - 1; y1 = h - 1; }
    else {
        x0 = ICLAMP(s_rr_x0, 0, w - 1); y0 = ICLAMP(s_rr_y0, 0, h - 1);
        x1 = ICLAMP(s_rr_x1, 0, w - 1); y1 = ICLAMP(s_rr_y1, 0, h - 1);
    }
    s_rr_dirty = 0;
    ex0 = IMAX(0, x0 - RR_NONE); ey0 = IMAX(0, y0 - RR_NONE);
    ex1 = IMIN(w - 1, x1 + RR_NONE); ey1 = IMIN(h - 1, y1 + RR_NONE);
    /* Chebyshev 2-pass distance transform inside the window; values outside it are still valid */
    for (y = ey0; y <= ey1; y++)
        for (x = ex0; x <= ex1; x++) s_roadreach[TILE_IDX(x, y)] = (u8)(is_access_road(TILE_IDX(x, y)) ? 0 : RR_NONE);
    for (y = ey0; y <= ey1; y++)
        for (x = ex0; x <= ex1; x++) {
            int i = TILE_IDX(x, y), v = s_roadreach[i];
            if (!v) continue;
            if (x > 0) v = IMIN(v, s_roadreach[i - 1] + 1);
            if (y > 0) {
                v = IMIN(v, s_roadreach[i - MAP_STRIDE] + 1);
                if (x > 0) v = IMIN(v, s_roadreach[i - MAP_STRIDE - 1] + 1);
                if (x < w - 1) v = IMIN(v, s_roadreach[i - MAP_STRIDE + 1] + 1);
            }
            s_roadreach[i] = (u8)v;
        }
    for (y = ey1; y >= ey0; y--)
        for (x = ex1; x >= ex0; x--) {
            int i = TILE_IDX(x, y), v = s_roadreach[i];
            if (!v) continue;
            if (x < w - 1) v = IMIN(v, s_roadreach[i + 1] + 1);
            if (y < h - 1) {
                v = IMIN(v, s_roadreach[i + MAP_STRIDE] + 1);
                if (x > 0) v = IMIN(v, s_roadreach[i + MAP_STRIDE - 1] + 1);
                if (x < w - 1) v = IMIN(v, s_roadreach[i + MAP_STRIDE + 1] + 1);
            }
            s_roadreach[i] = (u8)v;
        }
    /* road cells overlapping the changed rect */
    for (cy = y0 >> FIELD_SHIFT; cy <= y1 >> FIELD_SHIFT; cy++)
        for (cx = x0 >> FIELD_SHIFT; cx <= x1 >> FIELD_SHIFT; cx++) {
            int a = 0, tx, ty;
            for (ty = cy << FIELD_SHIFT; ty <= (cy << FIELD_SHIFT) + 1 && ty < h; ty++)
                for (tx = cx << FIELD_SHIFT; tx <= (cx << FIELD_SHIFT) + 1 && tx < w; tx++)
                    a += is_access_road(TILE_IDX(tx, ty));
            s_cell_acc[CELL_IDX(cx, cy)] = (u8)a;
        }
}

int net_road_reach(int x, int y) { return world_in(x, y) ? s_roadreach[TILE_IDX(x, y)] : RR_NONE; }

/* ------------------------------------------------------------------ cell BFS helpers */
/* Seed a source lot on the cell graph: its centre cell if passable, else passable cells within 1
   cell (distance 2 tiles), else within 2 cells (distance 4). Returns cells seeded. */
static int cell_seed(const Lot *l, const u8 *pass, u16 src, int *qt)
{
    int c = lot_cell(l), cx = c % FIELD_MAX_W, cy = c / FIELD_MAX_W, r, a, b, n = 0, cw = cells_w(), ch = cells_h();
    for (r = 0; r <= 2 && n == 0; r++)
        for (b = -r; b <= r; b++)
            for (a = -r; a <= r; a++) {
                int nx = cx + a, ny = cy + b, nc;
                if (IMAX(IABS(a), IABS(b)) != r) continue;
                if (nx < 0 || ny < 0 || nx >= cw || ny >= ch) continue;
                nc = CELL_IDX(nx, ny);
                if (!pass[nc]) continue;
                n++;
                if (s_cmark[nc] == s_cgen) continue;          /* an earlier source got there first */
                s_cq[(*qt)++] = (u16)nc;
                s_cmark[nc] = s_cgen; s_cdist[nc] = (u16)(2 * r); s_csrc[nc] = src;
            }
    return n;
}

/* ------------------------------------------------------------------ power (tile flood) */
static u8 s_cond[NTILES];               /* per solve: 1 conductive, 2 plain road (jumpable), 0 else */

/* conductive for power: a line, a conducting lot, or (F1, DESIGN 14.2) zoned land without a lot */
static int power_cond(const Tile *t)
{
    if (t->road & TR_POWER_LINE) return 1;
    if (t->lot) return s_def_cond[g_world.lots[t->lot].def];
    return ZONE_CONDUCTS(TILE_ZONE(t)) && !(t->road & TR_CLASS_MASK);   /* M3: not forestry/farm land */
}

#define conductive(idx) (s_cond[idx] == 1)
/* PL4: s_cond is evaluated lazily (only tiles the flood looks at: s_cmk == s_gen), and the bits pass covers only
   the box of the last flood's tiles + every box where a solver set power bits since (s_pbb; whole map after a
   reset/load). Same values, same flood order, same changed tiles as the full-map passes. */
static u16 s_cmk[NTILES];
static void pbb_add(int x0, int y0, int x1, int y1)
{
    if (s_pbb[2] < 0) { s_pbb[0] = x0; s_pbb[1] = y0; s_pbb[2] = x1; s_pbb[3] = y1; return; }
    s_pbb[0] = IMIN(s_pbb[0], x0); s_pbb[1] = IMIN(s_pbb[1], y0); s_pbb[2] = IMAX(s_pbb[2], x1); s_pbb[3] = IMAX(s_pbb[3], y1);
}
#define PCOND(idx) (s_cmk[idx] == s_gen ? s_cond[idx] : pcond_eval(idx))
static u8 pcond_eval(int idx)
{
    const Tile *t = &g_world.t[idx];
    s_cmk[idx] = (u16)s_gen;
    return s_cond[idx] = (u8)(power_cond(t) ? 1 : (t->road & TR_CLASS_MASK) ? 2 : 0);
}

static void solve_power_full(void)
{
    int i, x, y, qt = 0, w = g_world.w, h = g_world.h, ncomp = 0, x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
    int fbx0, fby0, fbx1, fby1, rx0, ry0, rx1, ry1;
    NetStats *ns = &s_ns[0];
    memset(ns, 0, sizeof(*ns));
    next_gen();
    s_pgen = s_gen;
    s_pserial = world_struct_serial();
    if (s_gen == 1) memset(s_cmk, 0, sizeof(s_cmk));                     /* PL4: stamps wrapped with s_gen */
    fbx0 = fby0 = 9999; fbx1 = fby1 = -1;
    memset(s_pknown, 0, sizeof(s_pknown));
    for (i = 1; i < g_world.lot_hi; i++) {
        s_lot_pstate[i] = NLS_NONE;
        s_lot_pcomp[i] = 0;
        s_pknown[i] = (u8)(g_world.lots[i].def != B_NONE);
        if (g_bdefs[g_world.lots[i].def].cat == BCAT_POWER) s_lot_out[i] = 0;
    }
    for (i = 1; i < g_world.lot_hi; i++) {
        Lot *pl = &g_world.lots[i];
        const BuildingDef *pd = &g_bdefs[pl->def];
        int lx0, ly0, lx1, ly1, supply = 0, nl = 0, k, left, shorted = 0, start = qt, qh = qt;
        if (pd->cat != BCAT_POWER || !lot_active(pl) || pd->capacity <= 0) continue;
        if (s_mark[TILE_IDX(pl->x, pl->y)] == s_gen) continue;          /* joined an earlier component */
        if (ncomp + 1 < MAX_COMP) { s_pc_head[ncomp + 1] = 0; s_pc_tail[ncomp + 1] = 0; }
        world_lot_rect(i, &lx0, &ly0, &lx1, &ly1);
        for (y = ly0; y <= ly1; y++)
            for (x = lx0; x <= lx1; x++) { int idx = TILE_IDX(x, y); s_mark[idx] = s_gen; s_queue[qt++] = (u16)idx; }
        while (qh < qt) {
            int idx = s_queue[qh++], tx = idx & (MAP_STRIDE - 1), ty = idx >> MAP_SHIFT, lot = g_world.t[idx].lot;
            if (tx < fbx0) fbx0 = tx;
            if (tx > fbx1) fbx1 = tx;
            if (ty < fby0) fby0 = ty;
            if (ty > fby1) fby1 = ty;
            if (lot && s_lot_gen[lot] != s_gen) {
                Lot *l = &g_world.lots[lot];
                s_lot_gen[lot] = s_gen;
                if (ncomp + 1 < MAX_COMP) {
                    s_lot_pcomp[lot] = (u16)(ncomp + 1); s_pnext[lot] = 0;
                    if (s_pc_tail[ncomp + 1]) s_pnext[s_pc_tail[ncomp + 1]] = (u16)lot; else s_pc_head[ncomp + 1] = (u16)lot;
                    s_pc_tail[ncomp + 1] = (u16)lot;
                }
                if (g_bdefs[l->def].cat == BCAT_POWER) {
                    if (lot_active(l)) { s_lot_out[lot] = g_bdefs[l->def].capacity; supply += s_lot_out[lot]; }
                    s_lot_pstate[lot] = NLS_OK;
                } else s_order[nl++] = (u16)lot;
            }
            /* PL4: the 4 directions in DIR order (NE (0,-1), SE (+1,0), SW (0,+1), NW (-1,0)), unrolled */
#if POWER_JUMP_ROADS
#define PSTEP(ok, ni, jok, ji) \
            if ((ok) && s_mark[ni] != s_gen) { \
                int c_ = PCOND(ni); \
                if (c_ == 1) { s_mark[ni] = s_gen; s_queue[qt++] = (u16)(ni); } \
                else if (c_ == 2 && (jok) && s_mark[ji] != s_gen && PCOND(ji) == 1) { s_mark[ji] = s_gen; s_queue[qt++] = (u16)(ji); } \
            }
#else
#define PSTEP(ok, ni, jok, ji) \
            if ((ok) && s_mark[ni] != s_gen && PCOND(ni) == 1) { s_mark[ni] = s_gen; s_queue[qt++] = (u16)(ni); }
#endif
            PSTEP(ty > 0, idx - MAP_STRIDE, ty > 1, idx - 2 * MAP_STRIDE)
            PSTEP(tx < w - 1, idx + 1, tx < w - 2, idx + 2)
            PSTEP(ty < h - 1, idx + MAP_STRIDE, ty < h - 2, idx + 2 * MAP_STRIDE)
            PSTEP(tx > 0, idx - 1, tx > 1, idx - 2)
#undef PSTEP
        }
        /* allocate in BFS order: the furthest consumers lose power first */
        left = supply;
        for (k = 0; k < nl; k++) {
            Lot *l = &g_world.lots[s_order[k]];
            int use = lot_active(l) ? CPX_DEF_USE(l->def, power_use, CPXU_POWER) : 0;
            ns->demand += use;
            if (!shorted && use > left) shorted = 1;
            if (!shorted) { left -= use; s_lot_pstate[s_order[k]] = NLS_OK; if (use) ns->lots_served++; }
            else { s_lot_pstate[s_order[k]] = use ? NLS_SHORT : NLS_OK; if (use) ns->lots_short++; }
        }
        ns->supply += supply;
        ncomp++;
        if (ncomp < MAX_COMP) s_pc_supply[ncomp] = supply;
        for (k = start; k < qt; k++) {
            int idx = s_queue[k], lot = g_world.t[idx].lot;
            u8 b = ZF_POWER_NET;
            s_tcomp[idx] = (u16)(ncomp < MAX_COMP ? ncomp : 0);
            if (supply > 0 && (!lot || s_lot_pstate[lot] == NLS_OK)) b |= ZF_POWERED;
            s_newbits[idx] = b;
        }
    }
    ns->networks = ncomp;
    s_pncomp = IMIN(ncomp, MAX_COMP - 1);
    s_pvalid = ncomp < MAX_COMP;
    for (i = 1; i < g_world.lot_hi; i++) {
        Lot *l = &g_world.lots[i];
        if (l->def == B_NONE) continue;
        if (s_lot_pstate[i] == NLS_OK) l->flags |= LF_POWERED; else l->flags &= ~LF_POWERED;
        if (s_lot_pstate[i] == NLS_NONE && lot_active(l) && CPX_DEF_USE(l->def, power_use, CPXU_POWER) > 0) ns->lots_unconnected++;
    }
    /* bits: tiles of this flood or with bits from an earlier solve (PL4: s_pbb box; outside it none) */
    rx0 = 0; ry0 = 0; rx1 = w - 1; ry1 = h - 1;
    if (!s_pbb_full) {
        rx0 = fbx0; ry0 = fby0; rx1 = fbx1; ry1 = fby1;
        if (s_pbb[2] >= 0) { rx0 = IMIN(rx0, s_pbb[0]); ry0 = IMIN(ry0, s_pbb[1]); rx1 = IMAX(rx1, s_pbb[2]); ry1 = IMAX(ry1, s_pbb[3]); }
        rx0 = IMAX(rx0, 0); ry0 = IMAX(ry0, 0); rx1 = IMIN(rx1, w - 1); ry1 = IMIN(ry1, h - 1);
    }
    s_pbb_full = 0;
    s_pbb[0] = fbx0; s_pbb[1] = fby0; s_pbb[2] = fbx1; s_pbb[3] = fby1;
    for (y = ry0; y <= ry1; y++)
        for (x = rx0; x <= rx1; x++) {
            int idx = TILE_IDX(x, y);
            Tile *t = &g_world.t[idx];
            u8 nb = (u8)(s_mark[idx] == s_gen ? s_newbits[idx] : 0);
            if ((t->zone & (ZF_POWER_NET | ZF_POWERED)) != nb) {
                t->zone = (u8)((t->zone & ~(ZF_POWER_NET | ZF_POWERED)) | nb);
                x0 = IMIN(x0, x); y0 = IMIN(y0, y); x1 = IMAX(x1, x); y1 = IMAX(y1, y);
            }
        }
    if (x1 >= 0) world_notify(x0, y0, x1, y1, WCHG_NET);
}

static int live_conductive(const Tile *t) { return power_cond(t); }

/* contact of a new lot's border tile: returns comp > 0, 0 = nothing, -1 = conductive but in no
   component (an unpowered island: joining it needs a full flood) */
static int contact(int nx, int ny, int d)
{
    const Tile *t;
    int idx;
    if (!world_in(nx, ny)) return 0;
    idx = TILE_IDX(nx, ny); t = &g_world.t[idx];
    if (!live_conductive(t)) {
#if POWER_JUMP_ROADS
        if ((t->road & TR_CLASS_MASK) && !t->lot && !(t->road & TR_POWER_LINE)) {
            nx += g_dir_dx[d]; ny += g_dir_dy[d];
            if (!world_in(nx, ny)) return 0;
            idx = TILE_IDX(nx, ny); t = &g_world.t[idx];
            if (!live_conductive(t)) return 0;
        } else
#endif
        return 0;
    }
    if (s_mark[idx] == s_pgen && s_tcomp[idx]) return s_tcomp[idx];
    return -1;
}

/* Incremental power solve: no removals / roads / lines changed since the last full flood. New lots
   join the one component they touch (appended = furthest, first to lose power); anything harder
   (a new plant, a lot bridging components or touching an unpowered island) falls back to the full
   flood. Then every component is re-allocated from its stored lot list. Returns 0 = need full. */
static int solve_power_incremental(void)
{
    int i, c, x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
    NetStats *ns = &s_ns[0];
    if (!s_pvalid || s_pserial != world_struct_serial() || s_pgen != s_gen) return 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        Lot *l = &g_world.lots[i];
        int lx0, ly0, lx1, ly1, xx, yy, comp = 0, island = 0;
        if (l->def == B_NONE || s_pknown[i]) continue;
        if (g_bdefs[l->def].cat == BCAT_POWER) return 0;
        s_pknown[i] = 1;
        s_lot_pcomp[i] = 0;
        if (!s_def_cond[l->def]) continue;
        world_lot_rect(i, &lx0, &ly0, &lx1, &ly1);
        for (yy = ly0; yy <= ly1; yy++)
            for (xx = lx0; xx <= lx1; xx++) {
                int d;
                for (d = 0; d < DIR_COUNT; d++) {
                    int nx = xx + g_dir_dx[d], ny = yy + g_dir_dy[d], k;
                    if (nx >= lx0 && nx <= lx1 && ny >= ly0 && ny <= ly1) continue;
                    k = contact(nx, ny, d);
                    if (k < 0) island = 1;
                    else if (k > 0) { if (comp && comp != k) return 0; comp = k; }
                }
            }
        if (comp && island) return 0;
        if (!comp) continue;                                  /* stays unconnected (an island) */
        for (yy = ly0; yy <= ly1; yy++)
            for (xx = lx0; xx <= lx1; xx++) { s_mark[TILE_IDX(xx, yy)] = s_pgen; s_tcomp[TILE_IDX(xx, yy)] = (u16)comp; }
        s_lot_pcomp[i] = (u16)comp; s_pnext[i] = 0;
        if (s_pc_tail[comp]) s_pnext[s_pc_tail[comp]] = (u16)i; else s_pc_head[comp] = (u16)i;
        s_pc_tail[comp] = (u16)i;
    }
    memset(ns, 0, sizeof(*ns));
    for (c = 1; c <= s_pncomp; c++) {
        int id, supply = 0, left, shorted = 0;
        for (id = s_pc_head[c]; id; id = s_pnext[id]) {
            Lot *l = &g_world.lots[id];
            if (g_bdefs[l->def].cat == BCAT_POWER) { s_lot_out[id] = lot_active(l) ? g_bdefs[l->def].capacity : 0; supply += s_lot_out[id]; }
        }
        if ((supply > 0) != (s_pc_supply[c] > 0)) return 0;  /* lines' powered bit flips: full flood */
        s_pc_supply[c] = supply;
        left = supply;
        for (id = s_pc_head[c]; id; id = s_pnext[id]) {
            Lot *l = &g_world.lots[id];
            int use, st;
            u8 want;
            if (g_bdefs[l->def].cat == BCAT_POWER) { st = NLS_OK; }
            else {
                use = lot_active(l) ? CPX_DEF_USE(l->def, power_use, CPXU_POWER) : 0;
                ns->demand += use;
                if (!shorted && use > left) shorted = 1;
                if (!shorted) { left -= use; st = NLS_OK; if (use) ns->lots_served++; }
                else { st = use ? NLS_SHORT : NLS_OK; if (use) ns->lots_short++; }
                /* F1: a brownout depends on the lot order, and appended lots are ordered differently than in a
                   full flood (which is what runs after loading a save): let the full flood decide, so results
                   never depend on solve history (save/load determinism) */
                if (shorted && use) return 0;
            }
            s_lot_pstate[id] = (u8)st;
            if (st == NLS_OK) l->flags |= LF_POWERED; else l->flags &= ~LF_POWERED;
            want = (u8)(ZF_POWER_NET | (st == NLS_OK && supply > 0 ? ZF_POWERED : 0));
            if ((WT(l->x, l->y)->zone & (ZF_POWER_NET | ZF_POWERED)) != want) {
                int lx0, ly0, lx1, ly1, xx, yy;
                world_lot_rect(id, &lx0, &ly0, &lx1, &ly1);
                for (yy = ly0; yy <= ly1; yy++)
                    for (xx = lx0; xx <= lx1; xx++) {
                        Tile *t = WT(xx, yy);
                        t->zone = (u8)((t->zone & ~(ZF_POWER_NET | ZF_POWERED)) | want);
                    }
                pbb_add(lx0, ly0, lx1, ly1);                        /* PL4 */
                x0 = IMIN(x0, lx0); y0 = IMIN(y0, ly0); x1 = IMAX(x1, lx1); y1 = IMAX(y1, ly1);
            }
        }
        ns->supply += supply;
    }
    ns->networks = s_pncomp;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def != B_NONE && !s_lot_pcomp[i] && lot_active(l) && CPX_DEF_USE(l->def, power_use, CPXU_POWER) > 0) {
            s_lot_pstate[i] = NLS_NONE; ns->lots_unconnected++;
        }
    }
    if (x1 >= 0) world_notify(x0, y0, x1, y1, WCHG_NET);
    return 1;
}

static void solve_power(void) { if (!solve_power_incremental()) solve_power_full(); }

/* ------------------------------------------------------------------ water (F1: pipes, WATER.md 3-4)
   Graph: 4-neighbour tiles that carry a pipe, belong to a conducting lot (every lot but BDF_NO_CONDUCT,
   any status: the plumbing is in the ground), or are zoned land without a lot (DESIGN 14.2). Roads block
   unless piped (WATER_JUMP_ROADS 0).
   1. BFS from every active source footprint (lot id order, dist 0): per tile the pipe distance and the
      nearest source (FIFO ties -> earlier source); sources whose regions touch form a network (union-find).
      Skipped when the graph and the sources are unchanged (fast path: only 2.-4. run).
   2. Each consumer lot takes the smallest distance over its footprint (first in scan order on ties) and
      that tile's source. Counting sort by (distance, lot id).
      Phase A: each source serves its own consumers nearest first while use <= left; the first that
      doesn't fit cuts it (strict cut: no holes). Phase B: the network's leftovers serve the remaining
      consumers in global (distance, id) order until the pool no longer covers the next one.
   3. Towers: output TOWER_OUTPUT if the network also has a pump/well with output > 0; pressure zone = a
      second BFS from active towers cut at TOWER_PRESSURE_REACH.
   4. Bits: lot tiles ZF_WATER_NET (connected) / ZF_WATERED (served); other reached tiles ZF_WATER_NET and
      ZF_WATERED when a new building there would get water (nearer than its source's nearest dry
      consumer, or spare left in the network). One WCHG_NET for the changed box.
   Results depend only on the world (not on solve history), so the fast path and a full solve agree. */
/* world_lot_rect without the id checks (hot lot loops; l must be a used slot) */
static void lot_rect_q(const Lot *l, int *x0, int *y0, int *x1, int *y1)
{
    const BuildingDef *d = &g_bdefs[l->def];
    *x0 = l->x; *y0 = l->y;
    if (l->rot & 1) { *x1 = l->x + d->h - 1; *y1 = l->y + d->w - 1; }
    else            { *x1 = l->x + d->w - 1; *y1 = l->y + d->h - 1; }
}

static int pump_shore(int id)
{
    int x0, y0, x1, y1, i, j, n = 0;
    world_lot_rect(id, &x0, &y0, &x1, &y1);
    for (j = y0 - 2; j <= y1 + 2; j++)
        for (i = x0 - 2; i <= x1 + 2; i++)
            if (world_in(i, j) && TER_IS_WATER(TER_TYPE(WT(i, j)))) n++;
    return n;
}

static int lot_is_water_source(const Lot *l)
{
    const BuildingDef *d = &g_bdefs[l->def];
    return d->cat == BCAT_WATER && d->capacity > 0;
}

static u16 uf_find(u16 a) { while (s_uf[a] != a) { s_uf[a] = s_uf[s_uf[a]]; a = s_uf[a]; } return a; }
static void uf_meet(u16 a, u16 b)
{
    a = uf_find(a); b = uf_find(b);
    if (a != b) { if (a < b) s_uf[b] = a; else s_uf[a] = b; }
}

static int water_pass(const Tile *t)
{
    if (t->flags & TF_PIPE) return 1;
    if (t->lot) return s_def_cond[g_world.lots[t->lot].def];
    return ZONE_CONDUCTS(TILE_ZONE(t)) && !(t->road & TR_CLASS_MASK);   /* M3: not forestry/farm land */
}

/* sources and the pumps' power: a change means a new BFS */
static u32 water_signature(void)
{
    u32 h = HASH_INIT;
    int i;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_NONE || g_bdefs[l->def].cat != BCAT_WATER) continue;
        h = (h ^ (u32)(i * 8 + l->def * 2 + (l->status == LOT_ACTIVE))) * 16777619u;
        h = (h ^ (u32)(l->flags & LF_POWERED)) * 16777619u;
    }
    return h;
}

static u32 pump_power_sig(void)
{
    u32 h = HASH_INIT;
    int i;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_PUMP) h = (h ^ (u32)(i * 2 + ((l->flags & LF_POWERED) ? 1 : 0))) * 16777619u;
    }
    return h;
}

/* the sim's world listener: pipes, zones or roads changed in this rect (lots are tracked by the solver) */
void net_water_mark_rect(int x0, int y0, int x1, int y1)
{
    if (!s_wr_dirty) { s_wr_x0 = x0; s_wr_y0 = y0; s_wr_x1 = x1; s_wr_y1 = y1; s_wr_dirty = 1; }
    else {
        s_wr_x0 = IMIN(s_wr_x0, x0); s_wr_y0 = IMIN(s_wr_y0, y0);
        s_wr_x1 = IMAX(s_wr_x1, x1); s_wr_y1 = IMAX(s_wr_y1, y1);
    }
    s_dirty |= NET_WATER;
}

/* passability of a rect brought up to date; returns 1 if anything changed */
static int water_pass_rect(int x0, int y0, int x1, int y1)
{
    int x, y, changed = 0;
    x0 = IMAX(x0, 0); y0 = IMAX(y0, 0); x1 = IMIN(x1, g_world.w - 1); y1 = IMIN(y1, g_world.h - 1);
    for (y = y0; y <= y1; y++) {
        const Tile *t = WT(x0, y);
        u8 *wp = &s_wpass[TILE_IDX(x0, y)];
        for (x = 0; x <= x1 - x0; x++, t++) {
            u8 p = (u8)water_pass(t);
            if (p != wp[x]) { wp[x] = p; changed = 1; }
        }
    }
    return changed;
}

static u32 lot_key(const Lot *l)
{
    if (l->def == B_NONE) return 0;
    return 0x80000000u | (u32)l->x | ((u32)l->y << 8) | ((u32)l->rot << 16) | ((u32)l->def << 18);
}

static void key_rect(u32 key, int *x0, int *y0, int *x1, int *y1)
{
    int w, h;
    def_footprint((int)((key >> 18) & 0x1FFF), (int)((key >> 16) & 3), &w, &h);
    *x0 = (int)(key & 255); *y0 = (int)((key >> 8) & 255); *x1 = *x0 + w - 1; *y1 = *y0 + h - 1;
}

/* a removed lot's tiles keep the lot's bits: their sources must re-evaluate them */
static void touch_owner_of_rect(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            u32 v;
            if (!world_in(x, y)) continue;
            v = s_winfo[TILE_IDX(x, y)];
            if (WI_REACHED(v) && (v >> 16) <= (u32)s_nws) s_wwb_chg[v >> 16] = 1;
        }
}

/* 1. multi-source BFS (also rebuilds the source list and the networks) */
static void water_bfs(void)
{
    int i, x, y, qh = 0, qt = 0, w = g_world.w, h = g_world.h, bx0, by0, bx1, by1;
    u16 *q;
    s_wbfs_count++;
    q = s_wq;
    for (y = 0; y < h; y++) memset(&s_winfo[TILE_IDX(0, y)], 0xFF, (size_t)w * sizeof(u32));
    memcpy(s_wbb_old, s_wbb, sizeof(s_wbb));
    s_nws = 0;
    s_uf[0] = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        int lx0, ly0, lx1, ly1;
        u32 si;
        if (l->def == B_NONE || !lot_is_water_source(l) || !lot_active(l)) continue;
        if (s_nws >= MAX_WSRC) break;
        si = (u32)++s_nws;
        memset(&s_ws[si], 0, sizeof(WaterSrc));
        s_ws[si].lot = (u16)i;
        s_uf[si] = (u16)si;
        lot_rect_q(&g_world.lots[i], &lx0, &ly0, &lx1, &ly1);
        for (y = ly0; y <= ly1; y++)
            for (x = lx0; x <= lx1; x++) {
                int idx = TILE_IDX(x, y);
                if (WI_REACHED(s_winfo[idx])) continue;
                s_winfo[idx] = si << 16;
                q[qt++] = (u16)idx;
            }
    }
    bx0 = by0 = 9999; bx1 = by1 = -1;
    while (qh < qt) {
        int idx = q[qh++], tx = idx & (MAP_STRIDE - 1), ty = idx >> MAP_SHIFT;
        u32 v = s_winfo[idx], own = v & 0xFFFF0000u, nv = v + 1, o;
        if (tx < bx0) bx0 = tx;
        if (tx > bx1) bx1 = tx;
        if (ty < by0) by0 = ty;
        if (ty > by1) by1 = ty;
#define WSTEP(ni) \
        if (s_wpass[ni]) { \
            o = s_winfo[ni]; \
            if (o == WI_NONE) { s_winfo[ni] = nv; q[qt++] = (u16)(ni); } \
            else if ((o & 0xFFFF0000u) != own) uf_meet((u16)(o >> 16), (u16)(own >> 16)); \
        }
        if (tx > 0) { WSTEP(idx - 1) }
        if (tx < w - 1) { WSTEP(idx + 1) }
        if (ty > 0) { WSTEP(idx - MAP_STRIDE) }
        if (ty < h - 1) { WSTEP(idx + MAP_STRIDE) }
#undef WSTEP
    }
    s_wbb[0] = bx0; s_wbb[1] = by0; s_wbb[2] = bx1; s_wbb[3] = by1;
    for (i = 1; i <= s_nws; i++) s_ws[i].net = uf_find((u16)i);
}

/* 3b. pressure zones: BFS from towers that deliver, cut at TOWER_PRESSURE_REACH tiles */
static void water_towers(void)
{
    int i, y, qh = 0, qt = 0, w = g_world.w, h = g_world.h;
    for (y = 0; y < h; y++) memset(&s_tpress[TILE_IDX(0, y)], 0, (size_t)w);
    memset(s_lot_press, 0, (size_t)g_world.lot_hi);
    for (i = 1; i <= s_nws; i++) {
        int lx0, ly0, lx1, ly1, x;
        s_ws[i].pressure_lots = 0;
        if (!(s_ws[i].flags & WSF_TOWER) || s_ws[i].output <= 0) continue;
        world_lot_rect(s_ws[i].lot, &lx0, &ly0, &lx1, &ly1);
        for (y = ly0; y <= ly1; y++)
            for (x = lx0; x <= lx1; x++) {
                int idx = TILE_IDX(x, y);
                if (s_tpress[idx]) continue;
                s_tpress[idx] = 1; s_tqd[idx] = 0; s_tq[qt++] = (u16)idx;
                s_tqs[idx] = (u16)i;
            }
    }
    while (qh < qt) {
        int idx = s_tq[qh++], tx = idx & (MAP_STRIDE - 1), ty = idx >> MAP_SHIFT, d, lot = g_world.t[idx].lot;
        if (lot && !s_lot_press[lot]) {
            s_lot_press[lot] = 1;
            if (g_bdefs[g_world.lots[lot].def].cat == BCAT_ZONE) s_ws[s_tqs[idx]].pressure_lots++;
        }
        if (s_tqd[idx] >= TOWER_PRESSURE_REACH) continue;
        for (d = 0; d < DIR_COUNT; d++) {
            int nx = tx + g_dir_dx[d], ny = ty + g_dir_dy[d], ni;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            ni = TILE_IDX(nx, ny);
            if (!s_wpass[ni] || s_tpress[ni]) continue;
            s_tpress[ni] = 1; s_tqd[ni] = (u8)(s_tqd[idx] + 1); s_tqs[ni] = s_tqs[idx]; s_tq[qt++] = (u16)ni;
        }
    }
}

/* bits of a reached non-lot tile (pipe / zoned land): connected; wet when a new building there would get
   water (nearer than the source's nearest dry consumer, or the network has spare) */
static u8 water_tile_bits(u32 v)
{
    const WaterSrc *ws = &s_ws[v >> 16];
    u8 nb = ZF_WATER_NET;
    if (s_wnsup[ws->net] > 0 && (ws->cut != DIST_NONE ? (v & 0xFFFF) < ws->cut : s_wpool[ws->net] > 0)) nb |= ZF_WATERED;
    return nb;
}

static void solve_water(void)
{
    int i, k, x, y, w = g_world.w, h = g_world.h, nl = 0, changed = 0, bfs, full = !s_wvalid, hi;
    int x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
    u32 sig = water_signature(), tsig = HASH_INIT;
    NetStats *ns = &s_ns[1];
    memset(ns, 0, sizeof(*ns));
    /* 0. passability, kept exact: everything after a reset/load; else the rect where pipes, zones or
          roads changed, plus the footprints of lots placed/removed/changed since the last solve */
    if (full) { water_pass_rect(0, 0, w - 1, h - 1); changed = 1; s_wr_dirty = 0; }
    else if (s_wr_dirty) { changed |= water_pass_rect(s_wr_x0, s_wr_y0, s_wr_x1, s_wr_y1); s_wr_dirty = 0; }
    hi = IMAX(g_world.lot_hi, s_lc_hi);
    for (i = 1; i < hi; i++) {
        u32 key = i < g_world.lot_hi ? lot_key(&g_world.lots[i]) : 0, old = s_lc_key[i];
        int ax0, ay0, ax1, ay1;
        if (key == old) continue;
        if (old) { key_rect(old, &ax0, &ay0, &ax1, &ay1); changed |= water_pass_rect(ax0, ay0, ax1, ay1); touch_owner_of_rect(ax0, ay0, ax1, ay1); }
        if (key) { key_rect(key, &ax0, &ay0, &ax1, &ay1); changed |= water_pass_rect(ax0, ay0, ax1, ay1); }
        s_lc_key[i] = key; s_lc_ok[i] = 0; s_lw_ok[i] = 0;
    }
    s_lc_hi = g_world.lot_hi;
    s_wpump_sig = pump_power_sig();
    bfs = full || changed || sig != s_wsig;
    if (bfs) { water_bfs(); s_wsig = sig; s_wvalid = 1; memset(s_lc_ok, 0, (size_t)g_world.lot_hi); }
    /* sources: outputs now (pump shore + power; towers need a producing network) */
    for (i = 1; i < g_world.lot_hi; i++) if (g_bdefs[g_world.lots[i].def].cat == BCAT_WATER) s_lot_out[i] = 0;
    for (i = 1; i <= s_nws; i++) {
        WaterSrc *ws = &s_ws[i];
        const Lot *l = &g_world.lots[ws->lot];
        const BuildingDef *d = &g_bdefs[l->def];
        u16 lot = ws->lot, net = ws->net, pl = ws->pressure_lots;
        memset(ws, 0, sizeof(*ws));
        ws->lot = lot; ws->net = net; ws->pressure_lots = pl;
        ws->cut = DIST_NONE;
        if (l->def == B_PUMP) {
            ws->flags |= WSF_PUMP;
            ws->shore = (u16)pump_shore(lot);
            if (l->flags & LF_POWERED) { ws->flags |= WSF_POWERED; ws->output = IMIN(d->capacity, ws->shore * PUMP_PER_WATER_TILE); }
        } else if (l->def == B_WATER_TOWER) ws->flags |= WSF_TOWER;
        else ws->output = d->capacity;
        s_lot_out[lot] = ws->output;
    }
    for (i = 0; i <= s_nws; i++) s_wnsup[i] = 0;
    for (i = 1; i <= s_nws; i++) if (!(s_ws[i].flags & WSF_TOWER)) s_wnsup[s_ws[i].net] += s_ws[i].output;
    for (i = 1; i <= s_nws; i++)
        if ((s_ws[i].flags & WSF_TOWER) && s_wnsup[s_ws[i].net] > 0) { s_ws[i].output = TOWER_OUTPUT; s_lot_out[s_ws[i].lot] = TOWER_OUTPUT; }
    for (i = 0; i <= s_nws; i++) s_wnsup[i] = 0;
    for (i = 1; i <= s_nws; i++) {
        s_wnsup[s_ws[i].net] += s_ws[i].output;
        ns->supply += s_ws[i].output;
        s_ws[i].left = s_ws[i].output;
        s_wcut[i] = 0;
        if (s_ws[i].flags & WSF_TOWER) tsig = (tsig ^ (u32)(i * 2 + (s_ws[i].output > 0))) * 16777619u;
    }
    /* 3b. pressure zones: only when the graph or a tower's output changed (the lots inside are counted then) */
    if (bfs || tsig != s_wtower_sig) { water_towers(); s_wtower_sig = tsig; }
    /* 2. consumers: distance + owner from the footprint (cached per lot while the graph is unchanged) */
    memset(s_cnt, 0, sizeof(s_cnt));
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        s_lot_wnew[i] = NLS_NONE; s_lot_wserved[i] = 0;
        if (l->def == B_NONE) { s_lot_wown[i] = 0; continue; }
        if (!s_lc_ok[i]) {
            int lx0, ly0, lx1, ly1, best = DIST_NONE, own = 0;
            lot_rect_q(&g_world.lots[i], &lx0, &ly0, &lx1, &ly1);
            for (y = ly0; y <= ly1; y++)
                for (x = lx0; x <= lx1; x++) {
                    u32 v = s_winfo[TILE_IDX(x, y)];
                    if (WI_REACHED(v) && (int)(v & 0xFFFF) < best) { best = (int)(v & 0xFFFF); own = (int)(v >> 16); }
                }
            s_lot_wdist[i] = (u16)best; s_lot_wown[i] = (u16)own; s_lc_ok[i] = 1;
        }
        if (!s_lot_wown[i]) {
            if (lot_active(l) && CPX_DEF_USE(l->def, water_use, CPXU_WATER) > 0) ns->lots_unconnected++;
            continue;
        }
        if (lot_is_water_source(l)) { s_lot_wnew[i] = (u8)(s_lot_out[i] > 0 ? NLS_OK : NLS_SHORT); continue; }
        s_cnt[IMIN(s_lot_wdist[i], 1023)]++;
        nl++;
    }
    {
        int sum = 0, v;
        for (v = 0; v <= 1024; v++) { int n = s_cnt[v]; s_cnt[v] = (u16)sum; sum += n; }
        for (i = 1; i < g_world.lot_hi; i++)
            if (s_lot_wown[i] && s_lot_wnew[i] == NLS_NONE && g_world.lots[i].def != B_NONE)
                s_order[s_cnt[IMIN(s_lot_wdist[i], 1023)]++] = (u16)i;
    }
    /* phase A: own catchment, nearest first, strict cut */
    for (k = 0; k < nl; k++) {
        int id = s_order[k], si = s_lot_wown[id];
        const Lot *l = &g_world.lots[id];
        int use = lot_active(l) ? CPX_DEF_USE(l->def, water_use, CPXU_WATER) : 0;
        WaterSrc *ws = &s_ws[si];
        ns->demand += use;
        if (!s_wcut[si] && use <= ws->left) { ws->left -= use; ws->own_used += use; s_lot_wserved[id] = 1; }
        else s_wcut[si] = 1;
    }
    /* phase B: the network's spare flows on to its dry consumers in global (distance, id) order */
    for (i = 0; i <= s_nws; i++) { s_wpool[i] = 0; s_wcutn[i] = 0; }
    for (i = 1; i <= s_nws; i++) s_wpool[s_ws[i].net] += s_ws[i].left;
    for (i = 0; i <= s_nws; i++) s_wpool0[i] = s_wpool[i];
    for (k = 0; k < nl; k++) {
        int id = s_order[k], n;
        const Lot *l;
        int use;
        if (s_lot_wserved[id]) continue;
        l = &g_world.lots[id];
        use = lot_active(l) ? CPX_DEF_USE(l->def, water_use, CPXU_WATER) : 0;
        n = s_ws[s_lot_wown[id]].net;
        if (!s_wcutn[n] && use <= s_wpool[n]) { s_wpool[n] -= use; s_lot_wserved[id] = 2; }
        else s_wcutn[n] = 1;
    }
    /* shared water credited to the donors in proportion to their spare (display only) */
    for (i = 1; i <= s_nws; i++) {
        WaterSrc *ws = &s_ws[i];
        s32 tot = s_wpool0[ws->net] - s_wpool[ws->net];
        if (tot > 0 && s_wpool0[ws->net] > 0) ws->shared_used = (s32)((s64)tot * ws->left / s_wpool0[ws->net]);
    }
    for (i = 0; i <= s_nws; i++) { s_wgiven[i] = 0; s_wfirst[i] = 0; }
    for (i = 1; i <= s_nws; i++) {          /* rounding remainder to the first donor of each network */
        s_wgiven[s_ws[i].net] += s_ws[i].shared_used;
        if (!s_wfirst[s_ws[i].net] && s_ws[i].left > 0) s_wfirst[s_ws[i].net] = (u16)i;
    }
    for (i = 1; i <= s_nws; i++)
        if (s_ws[i].net == i && s_wfirst[i]) s_ws[s_wfirst[i]].shared_used += s_wpool0[i] - s_wpool[i] - s_wgiven[i];
    /* per-lot states and per-source numbers */
    for (k = 0; k < nl; k++) {
        int id = s_order[k], si = s_lot_wown[id], dist = s_lot_wdist[id];
        const Lot *l = &g_world.lots[id];
        int use = lot_active(l) ? CPX_DEF_USE(l->def, water_use, CPXU_WATER) : 0;
        WaterSrc *ws = &s_ws[si];
        if (s_lot_wserved[id]) {
            s_lot_wnew[id] = NLS_OK;
            if (use) { ns->lots_served++; ns->used += use; ws->lots++; if (dist > ws->reach) ws->reach = (u16)dist; }
        } else {
            s_lot_wnew[id] = NLS_SHORT;
            if (use) {
                ns->lots_short++; ws->lots_dry++; ws->dry_units += use;
                if (dist < ws->cut) ws->cut = (u16)dist;
            }
        }
    }
    for (i = 1; i <= s_nws; i++) {
        WaterSrc *ws = &s_ws[i];
        u32 wk;
        ws->left -= ws->shared_used;
        if (ws->lots_dry) ws->flags |= WSF_AT_CAP;
        if (s_ws[i].net == i && s_wnsup[i] > 0) ns->networks++;
        wk = ((u32)ws->cut << 2) | (s_wnsup[ws->net] > 0 ? 2u : 0u) | (s_wpool[ws->net] > 0 ? 1u : 0u);
        if (wk != s_wwb_key[i] || bfs) s_wwb_chg[i] = 1;
        s_wwb_key[i] = wk;
    }
    /* 4. lot flags + lot tiles (a lot's tiles share the bits; new/changed lots get all their tiles) */
    for (i = 1; i < g_world.lot_hi; i++) {
        Lot *l = &g_world.lots[i];
        int st = s_lot_wnew[i];
        u8 nb = (u8)(st == NLS_OK ? ZF_WATER_NET | ZF_WATERED : st == NLS_SHORT ? ZF_WATER_NET : 0);
        int lx0, ly0, lx1, ly1;
        s_lot_wstate[i] = (u8)st;
        if (l->def == B_NONE) continue;
        if (st == NLS_OK) l->flags |= LF_WATERED; else l->flags &= ~LF_WATERED;
        if (!full && s_lw_ok[i] && (WT(l->x, l->y)->zone & (ZF_WATER_NET | ZF_WATERED)) == nb) continue;
        s_lw_ok[i] = 1;
        lot_rect_q(&g_world.lots[i], &lx0, &ly0, &lx1, &ly1);
        for (y = ly0; y <= ly1; y++)
            for (x = lx0; x <= lx1; x++) {
                Tile *t = WT(x, y);
                if ((t->zone & (ZF_WATER_NET | ZF_WATERED)) == nb) continue;
                t->zone = (u8)((t->zone & ~(ZF_WATER_NET | ZF_WATERED)) | nb);
                x0 = IMIN(x0, x); y0 = IMIN(y0, y); x1 = IMAX(x1, x); y1 = IMAX(y1, y);
            }
    }
    /* other tiles (pipes, zoned land): the whole map in row order after a BFS/reset (tiles may have left the
       graph); else only when some source's inputs changed, and then only that source's tiles */
    for (i = 1; i <= s_nws; i++) if (s_wwb_chg[i]) break;
    if (full || bfs || i <= s_nws) {
        int bx0 = 0, by0 = 0, bx1 = w - 1, by1 = h - 1;      /* tiles with bits: reached now or by the last BFS */
        if (!full) {
            bx0 = s_wbb[0]; by0 = s_wbb[1]; bx1 = s_wbb[2]; by1 = s_wbb[3];
            if (bfs && s_wbb_old[2] >= 0) {
                bx0 = IMIN(bx0, s_wbb_old[0]); by0 = IMIN(by0, s_wbb_old[1]);
                bx1 = IMAX(bx1, s_wbb_old[2]); by1 = IMAX(by1, s_wbb_old[3]);
            }
            bx0 = IMAX(bx0, 0); by0 = IMAX(by0, 0); bx1 = IMIN(bx1, w - 1); by1 = IMIN(by1, h - 1);
        }
        for (y = by0; y <= by1; y++) {
            Tile *t = WT(bx0, y);
            const u32 *wi = &s_winfo[TILE_IDX(0, y)];
            for (x = bx0; x <= bx1; x++, t++) {
                u32 v = wi[x];
                u8 nb;
                if (t->lot) continue;
                if (!WI_REACHED(v)) { if (!(t->zone & (ZF_WATER_NET | ZF_WATERED))) continue; nb = 0; }
                else if (!bfs && !full && !s_wwb_chg[v >> 16]) continue;
                else nb = water_tile_bits(v);
                if ((t->zone & (ZF_WATER_NET | ZF_WATERED)) != nb) {
                    t->zone = (u8)((t->zone & ~(ZF_WATER_NET | ZF_WATERED)) | nb);
                    x0 = IMIN(x0, x); y0 = IMIN(y0, y); x1 = IMAX(x1, x); y1 = IMAX(y1, y);
                }
            }
        }
    }
    for (i = 0; i <= s_nws; i++) s_wwb_chg[i] = 0;
    if (x1 >= 0) world_notify(x0, y0, x1, y1, WCHG_NET);
}

/* ------------------------------------------------------------------ step */
void net_step(void)
{
    int i, both;
    update_roads();
    both = (s_dirty & (NET_POWER | NET_WATER)) == (NET_POWER | NET_WATER);
    if ((s_dirty & NET_POWER) && (!both || s_turn == 0)) {
        solve_power(); s_dirty &= ~NET_POWER; s_turn = 1;
        if (pump_power_sig() != s_wpump_sig) s_dirty |= NET_WATER;     /* F1: a pump gained/lost power */
    }
    else if (s_dirty & NET_WATER) { solve_water(); s_dirty &= ~NET_WATER; s_turn = 0; }
    s_elem_factories = 0;
    for (i = 1; i < g_world.lot_hi; i++)
        if (g_world.lots[i].def == B_ELEMENT_FACTORY && lot_active(&g_world.lots[i])) s_elem_factories++;
}

void net_solve_all(void)
{
    int k;
    update_roads();
    solve_power();
    solve_water();
    s_dirty &= ~(NET_POWER | NET_WATER);
    for (k = 0; k < COV_KINDS; k++) net_coverage_step(k);
}

int net_element_factories(void) { return s_elem_factories; }

int net_power_near(int x0, int y0, int x1, int y1)
{
    int i, j, d, best = NLS_NONE;
    for (j = y0; j <= y1; j++)
        for (i = x0; i <= x1; i++) {
            if (i != x0 && i != x1 && j != y0 && j != y1) continue;
            for (d = 0; d < DIR_COUNT; d++) {
                int nx = i + g_dir_dx[d], ny = j + g_dir_dy[d];
                const Tile *t;
                if (nx >= x0 && nx <= x1 && ny >= y0 && ny <= y1) continue;
                if (!world_in(nx, ny)) continue;
                t = WT(nx, ny);
                if (!(t->zone & ZF_POWER_NET) && TILE_ROAD(t) && !t->lot && POWER_JUMP_ROADS) {
                    nx += g_dir_dx[d]; ny += g_dir_dy[d];
                    if (!world_in(nx, ny)) continue;
                    t = WT(nx, ny);
                }
                if (t->zone & ZF_POWERED) return NLS_OK;
                if (t->zone & ZF_POWER_NET) best = NLS_SHORT;
            }
        }
    return best;
}

/* F1: wet ground (WATER.md 3.3): the tile itself (zoned land is in the graph) or a tile within WATER_SPREAD */
int net_water_near(int x, int y)
{
    int i, j, best = NLS_NONE;
    if (!world_in(x, y)) return NLS_NONE;
    for (j = y - WATER_SPREAD; j <= y + WATER_SPREAD; j++)
        for (i = x - WATER_SPREAD; i <= x + WATER_SPREAD; i++) {
            const Tile *t;
            if (!world_in(i, j)) continue;
            t = WT(i, j);
            if (t->zone & ZF_WATERED) return NLS_OK;
            if (t->zone & ZF_WATER_NET) best = NLS_SHORT;
        }
    return best;
}

/* ------------------------------------------------------------------ coverage (cell graph) */
static int cov_is_source(int kind, const Lot *l)
{
    const BuildingDef *d;
    if (!lot_active(l)) return 0;
    d = &g_bdefs[l->def];
    if (kind < SVC_COUNT) return d->cat == BCAT_SERVICE && d->service == kind;
    if (kind == COV_SHOP) return d->cat == BCAT_ZONE && d->zone == ZONE_COMMERCIAL;
    if (kind == COV_JOBS) return (d->cat == BCAT_ZONE && (d->zone == ZONE_COMMERCIAL || d->zone == ZONE_INDUSTRY))
                                 || d->jobs > 0;
    if (kind == COV_RES)  return d->cat == BCAT_ZONE && (d->zone == ZONE_HOUSES || d->zone == ZONE_APARTMENTS)
                                 && l->occupants > 0;
    return 0;
}

int net_cov_max_dist(int kind)
{
    if (kind >= 0 && kind < SVC_COUNT) return g_services[kind].max_dist;
    if (kind == COV_SHOP) return 20;
    if (kind == COV_JOBS || kind == COV_RES) return 60;
    return 0;
}

int net_coverage_next(void)
{
    int k, i, kind;
    for (k = 0; k < COV_KINDS; k++) {
        kind = (s_cov_rr + k) % COV_KINDS;
        if (s_cov_nsrc[kind] == 0) {
            /* nothing there last time: only worth it if a source appeared */
            for (i = 1; i < g_world.lot_hi; i++) if (cov_is_source(kind, &g_world.lots[i])) break;
            if (i >= g_world.lot_hi) continue;
        }
        s_cov_rr = (kind + 1) % COV_KINDS;
        return kind;
    }
    kind = s_cov_rr;
    s_cov_rr = (s_cov_rr + 1) % COV_KINDS;
    return kind;
}

/* Coverage BFS on the road-cell graph with "leaf" expansion: from every reached road cell the BFS
   also marks non-road cells up to 2 cells away (they don't propagate), which replaces a dilation pass
   (lots up to 3 tiles off the road are covered). */
static int cov_bfs(int qt, int maxd)
{
    int qh = 0, cw = cells_w(), ch = cells_h();
    while (qh < qt) {
        int c = s_cq[qh++], cx = c % FIELD_MAX_W, cy = c / FIELD_MAX_W, k, leaf = !s_cell_acc[c];
        int nd = s_cdist[c] + 2;
        if (leaf && s_ctag[c] >= 2) continue;              /* second-ring leaf: stop */
        for (k = 0; k < 8; k++) {
            static const s8 dx8[8] = { 0, 1, 0, -1, 1, 1, -1, -1 }, dy8[8] = { -1, 0, 1, 0, -1, 1, 1, -1 };
            int nx = cx + dx8[k], ny = cy + dy8[k], nc;
            if (nx < 0 || ny < 0 || nx >= cw || ny >= ch) continue;
            nc = CELL_IDX(nx, ny);
            if (s_cmark[nc] == s_cgen) continue;
            if (s_cell_acc[nc]) {
                if (leaf || k >= 4 || nd > maxd) continue;        /* roads: 4-neighbour, only from roads */
                s_ctag[nc] = 0;
            } else {
                if (nd > maxd + 4) continue;
                s_ctag[nc] = (u8)(leaf ? s_ctag[c] + 1 : 1);
            }
            s_cmark[nc] = s_cgen; s_cdist[nc] = (u16)nd; s_csrc[nc] = s_csrc[c];
            s_cq[qt++] = (u16)nc;
        }
    }
    return qt;
}

static u16 s_rl_cell[MAX_LOTS], s_rl_occ[MAX_LOTS];   /* occupied homes: centre cell, residents */
static u16 s_seeded[NCELLS];                        /* == s_cgen: seeds from this centre cell done */

void net_coverage_step(int kind)
{
    int i, qt = 0, nsrc = 0, maxd, cw, ch, k, nl = 0, nr = 0, want_res, per_cell;
    u32 bit = 1u << kind;
    if (kind < 0 || kind >= COV_KINDS) return;
    update_roads();
    maxd = net_cov_max_dist(kind);
    cw = cells_w(); ch = cells_h();
    want_res = kind < SVC_COUNT || kind == COV_SHOP;
    per_cell = kind == COV_JOBS || kind == COV_RES;      /* source identity not needed: one seed per cell */
    next_cgen();
    /* one pass over the lots: sources of this kind + the list of occupied homes */
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        u32 m;
        int c;
        if (l->status != LOT_ACTIVE || l->def == B_NONE) continue;
        m = s_def_srcmask[l->def];
        if ((m & (1u << COV_RES)) && l->occupants) {
            if (want_res) { s_rl_cell[nr] = (u16)lot_cell(l); s_rl_occ[nr] = l->occupants; nr++; }
        } else m &= ~(1u << COV_RES);
        if (!(m & bit)) continue;
        nsrc++;
        if (kind < SVC_COUNT) { s_load_svc[i] = 0; s_catch[i] = 0; s_order[nl++] = (u16)i; }
        else if (kind == COV_SHOP) s_load_shop[i] = 0;
        if (per_cell) {
            c = lot_cell(l);
            if (s_seeded[c] == s_cgen) continue;
            s_seeded[c] = s_cgen;
        }
        cell_seed(l, s_cell_acc, (u16)i, &qt);
    }
    for (k = 0; k < qt; k++) s_ctag[s_cq[k]] = 0;
    s_cov_dirty &= ~bit;
    if (nsrc == 0 && s_cov_nsrc[kind] == 0) return;
    s_cov_nsrc[kind] = nsrc;
    qt = cov_bfs(qt, maxd);
    for (k = 0; k < ch; k++) {
        memset(&s_cov_dist[kind][CELL_IDX(0, k)], COV_NONE, cw);
        memset(&s_cov_src[kind][CELL_IDX(0, k)], 0, cw * sizeof(u16));
        memset(&s_cov_q[kind][CELL_IDX(0, k)], 0, cw);
    }
    for (k = 0; k < qt; k++) {
        int c = s_cq[k], dd = IMIN(s_cdist[c], maxd);
        s_cov_dist[kind][c] = (u8)dd; s_cov_src[kind][c] = s_csrc[c];
        if (kind < SVC_COUNT) s_catch[s_csrc[c]]++;
    }
    /* loads: residents assigned to their nearest building */
    if (want_res) {
        int pct = kind < SVC_COUNT ? g_services[kind].demand_pct : 100;
        const u16 *srcmap = s_cov_src[kind];
        if (kind == COV_SHOP) { for (k = 0; k < nr; k++) if (srcmap[s_rl_cell[k]]) s_load_shop[srcmap[s_rl_cell[k]]] += s_rl_occ[k]; }
        else for (k = 0; k < nr; k++) if (srcmap[s_rl_cell[k]]) s_load_svc[srcmap[s_rl_cell[k]]] += s_rl_occ[k] * pct / 100;
        if (kind < SVC_COUNT) {
            int f = g_budget.funding[kind] ? g_budget.funding[kind] : 100;
            if (kind == SVC_SCHOOL) f = f * politics_mod(PM_SCHOOL_CAP_PCT) / 100;   /* M1: school reform */
            for (k = 0; k < nl; k++) {
                s32 cap;
                i = s_order[k];
                cap = g_bdefs[g_world.lots[i].def].capacity * f / 100;
                s_bq[i] = (u8)(s_load_svc[i] <= cap ? 100 : IMAX(1, (s32)((s64)cap * 100 / s_load_svc[i])));
            }
        }
    }
    /* quality per cell: building quality x distance falloff (100% at the door, 50% at max_dist) */
    for (k = 0; k < qt; k++) {
        int c = s_cq[k], bq = kind < SVC_COUNT ? s_bq[s_csrc[c]] : 100;
        s_cov_q[kind][c] = (u8)(bq * (100 - 50 * s_cov_dist[kind][c] / IMAX(1, maxd)) / 100);
    }
}

int net_cov_lot(int svc, int x, int y)
{
    if (svc < 0 || svc >= COV_KINDS || !world_in(x, y)) return 0;
    return s_cov_src[svc][CELL_IDX(x >> FIELD_SHIFT, y >> FIELD_SHIFT)];
}

int net_cov_dist(int svc, int x, int y)
{
    if (svc < 0 || svc >= COV_KINDS || !world_in(x, y)) return COV_NONE;
    return s_cov_dist[svc][CELL_IDX(x >> FIELD_SHIFT, y >> FIELD_SHIFT)];
}

int net_cov_quality(int svc, int x, int y)
{
    if (svc < 0 || svc >= COV_KINDS || !world_in(x, y)) return 0;
    return s_cov_q[svc][CELL_IDX(x >> FIELD_SHIFT, y >> FIELD_SHIFT)];
}

void net_service_load(int lot, s32 *load, s32 *capacity)
{
    const Lot *l = world_lot(lot);
    const BuildingDef *d;
    *load = 0; *capacity = 0;
    if (!l) return;
    d = def_building(l->def);
    if (d->cat == BCAT_SERVICE) {
        int f = g_budget.funding[d->service] ? g_budget.funding[d->service] : 100;
        if (d->service == SVC_SCHOOL) f = f * politics_mod(PM_SCHOOL_CAP_PCT) / 100;   /* M1 */
        *load = s_load_svc[lot]; *capacity = d->capacity * f / 100;
    } else if (d->cat == BCAT_ZONE && d->zone == ZONE_COMMERCIAL) {
        *load = s_load_shop[lot]; *capacity = d->capacity;
    }
}

s32 net_lot_load(int kind, int lot)
{
    if (lot <= 0 || lot >= MAX_LOTS) return 0;
    if (kind == COV_SHOP) return s_load_shop[lot];
    if (kind >= 0 && kind < SVC_COUNT) return s_load_svc[lot];
    return 0;
}

const u8  *net_cov_q_map(int kind)    { return s_cov_q[ICLAMP(kind, 0, COV_KINDS - 1)]; }
const u8  *net_cov_dist_map(int kind) { return s_cov_dist[ICLAMP(kind, 0, COV_KINDS - 1)]; }
const u16 *net_cov_src_map(int kind)  { return s_cov_src[ICLAMP(kind, 0, COV_KINDS - 1)]; }

int net_building_quality(int lot) { return (lot > 0 && lot < MAX_LOTS) ? s_bq[lot] : 0; }
int net_catchment_tiles(int lot) { return (lot > 0 && lot < MAX_LOTS) ? s_catch[lot] << (2 * FIELD_SHIFT) : 0; }

int net_water_tower_reach(int x, int y)
{
    int lot;
    if (!world_in(x, y)) return 0;
    lot = WT(x, y)->lot;
    return lot ? s_lot_press[lot] : s_tpress[TILE_IDX(x, y)] != 0;
}

/* ------------------------------------------------------------------ F1 water queries */
static int water_src_index(int lot)
{
    int si;
    if (lot <= 0 || lot >= MAX_LOTS) return 0;
    si = s_lot_wown[lot];
    if (si <= 0 || si > s_nws || s_ws[si].lot != lot) return 0;
    return si;
}

int net_water_source(int lot, WaterSrcInfo *out)
{
    int si = water_src_index(lot);
    const WaterSrc *ws;
    memset(out, 0, sizeof(*out));
    if (!si) return 0;
    ws = &s_ws[si];
    out->lot = lot; out->output = ws->output;
    out->own_used = ws->own_used; out->shared_used = ws->shared_used; out->used = ws->own_used + ws->shared_used;
    out->spare = ws->output - out->used;
    out->lots = ws->lots; out->lots_dry = ws->lots_dry; out->dry_units = ws->dry_units;
    out->reach = ws->reach; out->at_capacity = (ws->flags & WSF_AT_CAP) != 0;
    out->shore = ws->shore; out->needs_power = (ws->flags & WSF_PUMP) && !(ws->flags & WSF_POWERED);
    out->pressure_lots = ws->pressure_lots; out->network = ws->net;
    return 1;
}

int net_lot_water_src(int lot, int *dist)
{
    int si;
    if (dist) *dist = 0;
    if (lot <= 0 || lot >= MAX_LOTS) return 0;
    si = s_lot_wown[lot];
    if (si <= 0 || si > s_nws) return 0;
    if (dist) *dist = s_lot_wdist[lot];
    return s_ws[si].lot;
}

int net_water_tile_state(int x, int y)
{
    const Tile *t;
    if (!world_in(x, y)) return NLS_NONE;
    t = WT(x, y);
    if (!(t->zone & ZF_WATER_NET)) return NLS_NONE;
    return (t->zone & ZF_WATERED) ? NLS_OK : NLS_SHORT;
}

int net_water_bfs_count(void) { return s_wbfs_count; }

int net_water_tile_src(int x, int y)
{
    int idx, lot;
    if (!world_in(x, y)) return 0;
    lot = WT(x, y)->lot;
    if (lot) return net_lot_water_src(lot, NULL);
    idx = TILE_IDX(x, y);
    if (!WI_REACHED(s_winfo[idx]) || (s_winfo[idx] >> 16) == 0 || (int)(s_winfo[idx] >> 16) > s_nws) return 0;
    return s_ws[s_winfo[idx] >> 16].lot;
}

void net_water_summary(WaterSummary *out)
{
    const NetStats *ns = &s_ns[1];
    int i;
    memset(out, 0, sizeof(*out));
    out->networks = ns->networks; out->sources = s_nws; out->lots_short = ns->lots_short;
    out->lots_unconnected = ns->lots_unconnected; out->supply = ns->supply; out->used = ns->used;
    for (i = 1; i <= s_nws; i++) {
        const WaterSrc *ws = &s_ws[i];
        s32 spare = ws->output - ws->own_used - ws->shared_used;
        if (spare > out->best_spare) { out->best_spare = spare; out->best_spare_lot = ws->lot; }
        if (ws->lots_dry > out->worst_dry) { out->worst_dry = ws->lots_dry; out->worst_lot = ws->lot; }
    }
}

/* legend: W source, w/d/u lot watered / dry (connected) / not connected, - x o pipe wet / short / dry,
   : ; zoned land wet / connected but dry, # road, ~ water, . other */
void net_water_dump(FILE *f, int x0, int y0, int x1, int y1)
{
    int x, y;
    x0 = ICLAMP(x0, 0, g_world.w - 1); x1 = ICLAMP(x1, 0, g_world.w - 1);
    y0 = ICLAMP(y0, 0, g_world.h - 1); y1 = ICLAMP(y1, 0, g_world.h - 1);
    fprintf(f, "water layer x %d..%d y %d..%d: supply %d, used %d, demand %d, served %d, short %d, unconnected %d, networks %d\n",
            x0, x1, y0, y1, s_ns[1].supply, s_ns[1].used, s_ns[1].demand, s_ns[1].lots_served, s_ns[1].lots_short,
            s_ns[1].lots_unconnected, s_ns[1].networks);
    fprintf(f, "legend: W source, w/d/u lot watered/dry/not connected, - x o pipe wet/short/dry, : ; zoned wet/dry, # road, ~ water\n");
    for (y = y0; y <= y1; y++) {
        fprintf(f, "%4d ", y);
        for (x = x0; x <= x1; x++) {
            const Tile *t = WT(x, y);
            int st = net_water_tile_state(x, y);
            char c = '.';
            if (t->lot) {
                const Lot *l = &g_world.lots[t->lot];
                if (g_bdefs[l->def].cat == BCAT_WATER) c = 'W';
                else c = s_lot_wstate[t->lot] == NLS_OK ? 'w' : s_lot_wstate[t->lot] == NLS_SHORT ? 'd' : 'u';
            } else if (t->flags & TF_PIPE) c = st == NLS_OK ? '-' : st == NLS_SHORT ? 'x' : 'o';
            else if (TILE_ZONE(t)) c = st == NLS_OK ? ':' : st == NLS_SHORT ? ';' : 'z';
            else if (TILE_ROAD(t)) c = '#';
            else if (TER_IS_WATER(TER_TYPE(t))) c = '~';
            fputc(c, f);
        }
        fputc('\n', f);
    }
    for (x = 1; x <= s_nws; x++) {
        const WaterSrc *ws = &s_ws[x];
        const Lot *l = &g_world.lots[ws->lot];
        fprintf(f, "source %d: %s lot %d at %d,%d net %d output %d used %d (own %d shared %d) lots %d dry %d (%d units)"
                   " reach %d cut %d shore %d flags %x pressure %d\n", x, g_bdefs[l->def].key, ws->lot, l->x, l->y,
                ws->net, ws->output, ws->own_used + ws->shared_used, ws->own_used, ws->shared_used, ws->lots,
                ws->lots_dry, ws->dry_units, ws->reach, ws->cut == DIST_NONE ? -1 : ws->cut, ws->shore, ws->flags,
                ws->pressure_lots);
    }
}

int net_lot_power_state(int lot) { return (lot > 0 && lot < MAX_LOTS) ? s_lot_pstate[lot] : 0; }
int net_lot_water_state(int lot) { return (lot > 0 && lot < MAX_LOTS) ? s_lot_wstate[lot] : 0; }
s32 net_source_output(int lot) { return (lot > 0 && lot < MAX_LOTS) ? s_lot_out[lot] : 0; }
const NetStats *net_stats(int net) { return &s_ns[net == NET_WATER ? 1 : 0]; }
/* ------------------------------------------------------------------ T10: save
   Chunk "NET ": the solver RESULTS and scheduling flags, which depend on the world as it was when each
   solve ran (coverage is round robin, power/water alternate), so they cannot be recomputed after load
   without changing the continuation (determinism across save/load). Scratch arrays and fast-path caches
   are not saved: load invalidates the caches and recomputes road reach (a pure function of the roads).
   F1: version 2 (pipes): per-lot water results, the source table and the tower pressure zones.
   A version-1 chunk (mains in roads) is not loaded: the caller re-solves everything (net_solve_all). */
#include "save.h"
int net_save(struct SaveFile *sf)
{
    save_begin_chunk(sf, "NET ", 2);
    save_s32(sf, s_dirty); save_s32(sf, s_turn); save_s32(sf, s_elem_factories);
    save_u32(sf, s_cov_dirty); save_s32(sf, s_cov_rr); save_u32(sf, s_wpump_sig);
    save_bytes(sf, s_cov_nsrc, sizeof(s_cov_nsrc));
    save_rle(sf, s_cov_dist, sizeof(s_cov_dist));
    save_rle(sf, s_cov_q, sizeof(s_cov_q));
    save_rle(sf, s_cov_src, sizeof(s_cov_src));
    save_rle(sf, s_load_svc, sizeof(s_load_svc));
    save_rle(sf, s_load_shop, sizeof(s_load_shop));
    save_rle(sf, s_catch, sizeof(s_catch));
    save_rle(sf, s_bq, sizeof(s_bq));
    save_rle(sf, s_lot_pstate, sizeof(s_lot_pstate));
    save_rle(sf, s_lot_wstate, sizeof(s_lot_wstate));
    save_rle(sf, s_lot_out, sizeof(s_lot_out));
    save_rle(sf, s_lot_wown, sizeof(s_lot_wown));
    save_rle(sf, s_lot_wdist, sizeof(s_lot_wdist));
    save_rle(sf, s_lot_press, sizeof(s_lot_press));
    save_rle(sf, s_tpress, sizeof(s_tpress));
    save_s32(sf, s_nws);
    save_bytes(sf, &s_ws[1], s_nws * (int)sizeof(WaterSrc));
    save_bytes(sf, s_ns, sizeof(s_ns));
    save_end_chunk(sf);
    return 1;
}
int net_load(struct SaveFile *sf)
{
    int ver;
    if (!load_find_chunk(sf, "NET ", &ver)) return 0;
    if (ver < 2) { log_printf("net: NET v%d (water mains in roads): re-solving the networks", ver); return 0; }
    s_dirty = load_s32(sf); s_turn = load_s32(sf); s_elem_factories = load_s32(sf);
    s_cov_dirty = load_u32(sf); s_cov_rr = load_s32(sf); s_wpump_sig = load_u32(sf);
    load_bytes(sf, s_cov_nsrc, sizeof(s_cov_nsrc));
    load_rle(sf, s_cov_dist, sizeof(s_cov_dist));
    load_rle(sf, s_cov_q, sizeof(s_cov_q));
    load_rle(sf, s_cov_src, sizeof(s_cov_src));
    load_rle(sf, s_load_svc, sizeof(s_load_svc));
    load_rle(sf, s_load_shop, sizeof(s_load_shop));
    load_rle(sf, s_catch, sizeof(s_catch));
    load_rle(sf, s_bq, sizeof(s_bq));
    load_rle(sf, s_lot_pstate, sizeof(s_lot_pstate));
    load_rle(sf, s_lot_wstate, sizeof(s_lot_wstate));
    load_rle(sf, s_lot_out, sizeof(s_lot_out));
    load_rle(sf, s_lot_wown, sizeof(s_lot_wown));
    load_rle(sf, s_lot_wdist, sizeof(s_lot_wdist));
    load_rle(sf, s_lot_press, sizeof(s_lot_press));
    load_rle(sf, s_tpress, sizeof(s_tpress));
    s_nws = load_s32(sf);
    if (s_nws < 0 || s_nws > MAX_WSRC) { s_nws = 0; return 0; }
    load_bytes(sf, &s_ws[1], s_nws * (int)sizeof(WaterSrc));
    load_bytes(sf, s_ns, sizeof(s_ns));
    s_rr_dirty = 2; s_pvalid = 0; s_wvalid = 0; update_roads();
    s_pbb_full = 1; s_pbb[2] = -1;                       /* PL4: loaded tiles carry their bits */
    memset(s_winfo, 0xFF, sizeof(s_winfo));   /* per-tile BFS data is not saved: the next solve rebuilds it */
    s_wr_dirty = 0; s_lc_hi = 1;
    memset(s_lc_key, 0, sizeof(s_lc_key)); memset(s_lc_ok, 0, sizeof(s_lc_ok)); memset(s_lw_ok, 0, sizeof(s_lw_ok));
    s_wbb[0] = s_wbb[1] = 9999; s_wbb[2] = s_wbb[3] = -1; memcpy(s_wbb_old, s_wbb, sizeof(s_wbb));
    return load_ok(sf);
}
