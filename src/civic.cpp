/* civic.cpp - M5: element factory output, district heating and sewage over the pipe graph, lake pollution,
   overlays/queries/scripts for them. Fires are in fire.cpp. PORTABLE, deterministic, integer-only.
   Owner: M5. See civic.h. The pipe-utility solver follows F1's water solver (net.cpp solve_water,
   WATER.md 3.5): multi-source BFS, own area nearest first with a strict cut, then the network's spare in
   global (distance, lot id) order. It is a full solve every time (no fast path): ~6-10k graph tiles in a big
   town, a few ms on the P2 every second cycle per utility. */
#include "common.h"
#include "civic.h"
#include "world.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "politics.h"
#include "save.h"
#include "tools.h"
#include "text.h"
#include "util.h"

#define NTILES      (MAP_MAX_W * MAP_MAX_H)
#define NCELLS      (FIELD_MAX_W * FIELD_MAX_H)
#define CELL_IDX(cx, cy) ((cy) * FIELD_MAX_W + (cx))
#define MAX_USRC    255
#define DIST_NONE   0xFFFF
#define UI_NONE     0xFFFFFFFFu
#define LAKE_FIX    16

/* ---- elements */
static s32 s_elem_out, s_elem_left, s_elem_used;

/* ---- pipe utilities */
typedef struct {
    u16 lot, net;
    s32 output, left, own_used, shared_used, short_units;
    u16 lots, lots_short, reach, cut;
} USrc;
static USrc s_us[UT_COUNT][MAX_USRC + 1];      /* 1..s_nus */
static int  s_nus[UT_COUNT];
static u8   s_lst[UT_COUNT][MAX_LOTS];         /* NLS_* per lot */
static u8   s_lsi[UT_COUNT][MAX_LOTS];         /* source index (0 = none) */
static u16  s_ldist[UT_COUNT][MAX_LOTS];
static s32  s_pool[UT_COUNT][MAX_USRC + 1];    /* spare left per network root after the allocation */
static s32  s_nsup[UT_COUNT][MAX_USRC + 1];    /* supply per network root */
static UtilSummary s_sum[UT_COUNT];
static int  s_fuel_pct = 100;
/* scratch (one solve at a time) */
static u32  s_info[NTILES];                    /* (source << 16) | distance, UI_NONE = not reached */
static u16  s_q[NTILES];
static u8   s_pass[NTILES];
static u16  s_uf[MAX_USRC + 1];
static u16  s_order[MAX_LOTS];
static u16  s_cnt[1025];
static u8   s_served[MAX_LOTS];
static u8   s_cutA[MAX_USRC + 1], s_cutN[MAX_USRC + 1];
static s32  s_pool0[MAX_USRC + 1];
static u8   s_cond[B_COUNT];
/* PL4: s_info / s_pass are valid where s_igen / s_pgen == s_ig (no whole-map clear per solve; passability is
   evaluated only for tiles the BFS looks at); heat_apply's tile pass covers the box of this solve's reached
   tiles and heated lots + the box of the tiles it lit last time (s_hbb; whole map after a reset/load). */
static u16  s_igen[NTILES], s_pgen[NTILES], s_ig;
static int  s_ubb[4], s_hbb[4], s_hbb_full = 1;
#define INFO(idx) (s_igen[idx] == s_ig ? s_info[idx] : UI_NONE)

/* ---- lakes */
static u16  s_lake[NCELLS];                    /* x LAKE_FIX, water cells */
static u16  s_lext[NCELLS];                    /* civic_lake_emit accumulation (cell of the emitter) */
static u8   s_water[NCELLS];                   /* 1 lake cell, 2 river cell (recomputed per update) */
static s16  s_near[NCELLS];                    /* nearest water cell (index) within CIVIC_LAKE_REACH, -1 none */
static u8   s_shore[NCELLS];                   /* worst lake value within 2 cells (land value, happiness) */
static u32  s_lemit[NCELLS];                   /* scratch: residents' worth arriving per water cell */
static u16  s_ltmp[NCELLS];
static u16  s_cq[NCELLS];
static int  s_lake_reason, s_lake_avg;
/* PL4: terrain-only lake cell maps (lake_cells_build): water cells in scan order, "water within 2 cells" */
static u16  s_wlist[NCELLS];
static int  s_nwl, s_lk_valid, s_lk_dirty, s_lk_x0, s_lk_y0, s_lk_x1, s_lk_y1, s_lk_cw, s_lk_ch;
static u8   s_near2[NCELLS];

static int cells_w(void) { return (g_world.w + 1) >> FIELD_SHIFT; }
static int cells_h(void) { return (g_world.h + 1) >> FIELD_SHIFT; }

/* ------------------------------------------------------------------ lifecycle */
void civic_init(void) { civic_reset(); }

void civic_reset(void)
{
    int i;
    for (i = 0; i < B_COUNT; i++) s_cond[i] = (u8)(i != B_NONE && !(g_bdefs[i].flags & BDF_NO_CONDUCT));
    s_elem_out = s_elem_left = s_elem_used = 0;
    memset(s_us, 0, sizeof(s_us)); memset(s_nus, 0, sizeof(s_nus));
    memset(s_lst, 0, sizeof(s_lst)); memset(s_lsi, 0, sizeof(s_lsi)); memset(s_ldist, 0xFF, sizeof(s_ldist));
    memset(s_pool, 0, sizeof(s_pool)); memset(s_nsup, 0, sizeof(s_nsup)); memset(s_sum, 0, sizeof(s_sum));
    memset(s_lake, 0, sizeof(s_lake)); memset(s_lext, 0, sizeof(s_lext)); memset(s_shore, 0, sizeof(s_shore));
    memset(s_water, 0, sizeof(s_water)); memset(s_near, 0xFF, sizeof(s_near));
    s_lk_valid = 0; s_lk_dirty = 0; s_nwl = 0;          /* PL4: lake cell maps rebuilt at the next update */
    s_hbb_full = 1; s_hbb[2] = -1;                      /* PL4: tiles may carry ZF_HEAT from a load */
    s_lake_reason = s_lake_avg = 0;
    fire_reset();
}

void civic_set_fuel_pct(int pct) { s_fuel_pct = ICLAMP(pct, 10, 1000); }
int  civic_fuel_pct(void) { return s_fuel_pct; }

/* ------------------------------------------------------------------ elements */
s32 civic_factory_output(int lot)
{
    const Lot *l = world_lot(lot);
    const BuildingDef *d;
    s32 out;
    if (!l || l->def != B_ELEMENT_FACTORY || l->status != LOT_ACTIVE || !(l->flags & LF_POWERED)) return 0;
    d = def_building(l->def);
    out = d->jobs > 0 ? (s32)CIVIC_ELEM_OUTPUT * IMIN((int)l->occupants, d->jobs) / d->jobs : CIVIC_ELEM_OUTPUT;
    return POL_PCT(out, politics_mod(PM_ELEMENTS_PCT));
}

s32 civic_elements_output(void) { return s_elem_out; }
s32 civic_elements_left(void) { return s_elem_left; }
s32 civic_elements_used_month(void) { return s_elem_used; }
int civic_elements_take(s32 n)
{
    if (n > s_elem_left) return 0;
    s_elem_left -= n; s_elem_used += n;
    return 1;
}

static void elements_month(void)
{
    int i;
    s_elem_out = 0;
    for (i = 1; i < g_world.lot_hi; i++)
        if (g_world.lots[i].def == B_ELEMENT_FACTORY) s_elem_out += civic_factory_output(i);
    s_elem_left = s_elem_out; s_elem_used = 0;
}

/* ------------------------------------------------------------------ pipe utilities: who is what */
static int util_source_def(int ut) { return ut == UT_HEAT ? B_HEAT_PLANT : B_SEWAGE_WORKS; }

static int is_res(const BuildingDef *d)
{
    return d->cat == BCAT_ZONE && (d->zone == ZONE_HOUSES || d->zone == ZONE_APARTMENTS || d->zone == ZONE_FARM);
}

/* heat units a def takes when active (0 = not a heat consumer) */
static s32 heat_def_use(int def)
{
    const BuildingDef *d = &g_bdefs[def];
    if (def == B_HEAT_PLANT || def == B_SEWAGE_WORKS || def == B_ELEMENT_FACTORY) return 0;
    if (d->cat == BCAT_ZONE) {
        if (d->zone == ZONE_APARTMENTS) return d->water_use;
        if (d->zone == ZONE_COMMERCIAL && d->tier >= 2) return d->water_use;
        return 0;
    }
    if (d->cat == BCAT_SERVICE || d->cat == BCAT_SPECIAL) return d->water_use;
    return 0;
}

static int is_consumer(int ut, int def)
{
    if (def == B_NONE) return 0;
    if (ut == UT_HEAT) return heat_def_use(def) > 0;
    return is_res(&g_bdefs[def]);
}

static s32 lot_use(int ut, const Lot *l)
{
    if (l->status != LOT_ACTIVE) return 0;
    if (ut == UT_HEAT) return heat_def_use(l->def);
    return (l->flags & LF_WATERED) ? (s32)l->occupants : 0;      /* sewers follow the water mains */
}

s32 civic_lot_util_use(int ut, int lot)
{
    const Lot *l = world_lot(lot);
    if (!l || ut < 0 || ut >= UT_COUNT || !is_consumer(ut, l->def)) return 0;
    return lot_use(ut, l);
}

static int util_pass(const Tile *t)
{
    if (t->flags & TF_PIPE) return 1;
    if (t->lot) return s_cond[g_world.lots[t->lot].def];
    return ZONE_CONDUCTS(TILE_ZONE(t)) && !(t->road & TR_CLASS_MASK);
}

static u16 uf_find(u16 a) { while (s_uf[a] != a) { s_uf[a] = s_uf[s_uf[a]]; a = s_uf[a]; } return a; }
static void uf_meet(u16 a, u16 b)
{
    a = uf_find(a); b = uf_find(b);
    if (a != b) { if (a < b) s_uf[b] = a; else s_uf[a] = b; }
}

static void lot_rect_q(const Lot *l, int *x0, int *y0, int *x1, int *y1)
{
    const BuildingDef *d = &g_bdefs[l->def];
    *x0 = l->x; *y0 = l->y;
    if (l->rot & 1) { *x1 = l->x + d->h - 1; *y1 = l->y + d->w - 1; }
    else            { *x1 = l->x + d->w - 1; *y1 = l->y + d->h - 1; }
}

/* ------------------------------------------------------------------ the solver */
static void util_solve(int ut)
{
    int i, k, x, y, w = g_world.w, h = g_world.h, qh = 0, qt = 0, nl = 0, ns = 0, srcdef = util_source_def(ut);
    USrc *S = s_us[ut];
    UtilSummary *sum = &s_sum[ut];
    memset(sum, 0, sizeof(*sum));
    /* 0. passability (lazily, PL4) and "nothing reached": a new stamp */
    if (++s_ig == 0) { memset(s_igen, 0, sizeof(s_igen)); memset(s_pgen, 0, sizeof(s_pgen)); s_ig = 1; }
    s_ubb[0] = s_ubb[1] = 9999; s_ubb[2] = s_ubb[3] = -1;
    /* 1. sources in lot id order, BFS */
    s_uf[0] = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        int lx0, ly0, lx1, ly1;
        if (l->def != srcdef || l->status != LOT_ACTIVE) continue;
        if (ns >= MAX_USRC) break;
        ns++;
        memset(&S[ns], 0, sizeof(USrc));
        S[ns].lot = (u16)i; S[ns].cut = DIST_NONE;
        S[ns].output = S[ns].left = g_bdefs[l->def].capacity;
        s_uf[ns] = (u16)ns;
        lot_rect_q(l, &lx0, &ly0, &lx1, &ly1);
        for (y = ly0; y <= ly1; y++)
            for (x = lx0; x <= lx1; x++) {
                int idx = TILE_IDX(x, y);
                if (INFO(idx) != UI_NONE) continue;
                s_info[idx] = (u32)ns << 16; s_igen[idx] = s_ig;
                s_q[qt++] = (u16)idx;
            }
    }
    s_nus[ut] = ns;
    while (qh < qt) {
        int idx = s_q[qh++], tx = idx & (MAP_STRIDE - 1), ty = idx >> MAP_SHIFT, d;
        u32 v = s_info[idx], own = v & 0xFFFF0000u;
        if (tx < s_ubb[0]) s_ubb[0] = tx;
        if (tx > s_ubb[2]) s_ubb[2] = tx;
        if (ty < s_ubb[1]) s_ubb[1] = ty;
        if (ty > s_ubb[3]) s_ubb[3] = ty;
        for (d = 0; d < DIR_COUNT; d++) {
            int nx = tx + g_dir_dx[d], ny = ty + g_dir_dy[d], ni;
            u32 o;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            ni = TILE_IDX(nx, ny);
            if (s_pgen[ni] != s_ig) { s_pass[ni] = (u8)util_pass(&g_world.t[ni]); s_pgen[ni] = s_ig; }
            if (!s_pass[ni]) continue;
            o = INFO(ni);
            if (o == UI_NONE) { s_info[ni] = (v & 0xFFFF) < 0xFFFE ? v + 1 : v; s_igen[ni] = s_ig; s_q[qt++] = (u16)ni; }
            else if ((o & 0xFFFF0000u) != own) uf_meet((u16)(o >> 16), (u16)(own >> 16));
        }
    }
    for (i = 1; i <= ns; i++) S[i].net = uf_find((u16)i);
    for (i = 0; i <= ns; i++) { s_nsup[ut][i] = 0; s_cutA[i] = 0; s_cutN[i] = 0; }
    for (i = 1; i <= ns; i++) { s_nsup[ut][S[i].net] += S[i].output; sum->supply += S[i].output; }
    /* 2. consumers: distance + owner from the footprint; counting sort by (distance, lot id) */
    memset(s_cnt, 0, sizeof(s_cnt));
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        int lx0, ly0, lx1, ly1, best = DIST_NONE, own = 0;
        s_lst[ut][i] = NLS_NONE; s_lsi[ut][i] = 0; s_ldist[ut][i] = DIST_NONE; s_served[i] = 0;
        if (l->def == B_NONE || !is_consumer(ut, l->def)) continue;
        lot_rect_q(l, &lx0, &ly0, &lx1, &ly1);
        for (y = ly0; y <= ly1; y++)
            for (x = lx0; x <= lx1; x++) {
                u32 v = INFO(TILE_IDX(x, y));
                if (v != UI_NONE && (int)(v & 0xFFFF) < best) { best = (int)(v & 0xFFFF); own = (int)(v >> 16); }
            }
        if (!own) {
            if (lot_use(ut, l) > 0) sum->lots_unconnected++;
            continue;
        }
        s_lsi[ut][i] = (u8)own; s_ldist[ut][i] = (u16)best;
        s_cnt[IMIN(best, 1023)]++;
        nl++;
    }
    {
        int acc = 0, v;
        for (v = 0; v <= 1024; v++) { int n = s_cnt[v]; s_cnt[v] = (u16)acc; acc += n; }
        for (i = 1; i < g_world.lot_hi; i++) if (s_lsi[ut][i]) s_order[s_cnt[IMIN(s_ldist[ut][i], 1023)]++] = (u16)i;
    }
    /* phase A: own area nearest first, strict cut */
    for (k = 0; k < nl; k++) {
        int id = s_order[k], si = s_lsi[ut][id];
        s32 use = lot_use(ut, &g_world.lots[id]);
        sum->demand += use;
        if (!s_cutA[si] && use <= S[si].left) { S[si].left -= use; S[si].own_used += use; s_served[id] = 1; }
        else s_cutA[si] = 1;
    }
    /* phase B: the network's spare, global order, strict cut per network */
    for (i = 0; i <= ns; i++) s_pool[ut][i] = 0;
    for (i = 1; i <= ns; i++) s_pool[ut][S[i].net] += S[i].left;
    for (i = 0; i <= ns; i++) s_pool0[i] = s_pool[ut][i];
    for (k = 0; k < nl; k++) {
        int id = s_order[k], n;
        s32 use;
        if (s_served[id]) continue;
        use = lot_use(ut, &g_world.lots[id]);
        n = S[s_lsi[ut][id]].net;
        if (!s_cutN[n] && use <= s_pool[ut][n]) { s_pool[ut][n] -= use; s_served[id] = 2; }
        else s_cutN[n] = 1;
    }
    for (i = 1; i <= ns; i++) {                 /* shared out, credited in proportion to the spare (display) */
        s32 tot = s_pool0[S[i].net] - s_pool[ut][S[i].net];
        if (tot > 0 && s_pool0[S[i].net] > 0) S[i].shared_used = (s32)((s64)tot * S[i].left / s_pool0[S[i].net]);
    }
    /* states + per-source numbers */
    for (k = 0; k < nl; k++) {
        int id = s_order[k], si = s_lsi[ut][id], dist = s_ldist[ut][id];
        s32 use = lot_use(ut, &g_world.lots[id]);
        USrc *s = &S[si];
        if (s_served[id]) {
            s_lst[ut][id] = NLS_OK;
            if (use) { sum->lots_served++; sum->used += use; s->lots++; if (dist > s->reach) s->reach = (u16)dist; }
        } else {
            s_lst[ut][id] = NLS_SHORT;
            if (use) { sum->lots_short++; s->lots_short++; s->short_units += use; if (dist < s->cut) s->cut = (u16)dist; }
        }
    }
    for (i = 1; i <= ns; i++) {
        S[i].left -= S[i].shared_used;
        if (S[i].net == i) sum->networks++;
    }
    sum->sources = ns;
}

/* heat: LF_HEAT on served lots, ZF_HEAT on lot tiles of served lots and on graph tiles where a new building
   would get heat (nearer than its source's cut, or spare in the network); one WCHG_NET for the changed box */
static void heat_apply(void)
{
    int i, x, y, w = g_world.w, h = g_world.h, x0 = 9999, y0 = 9999, x1 = -1, y1 = -1;
    int rx0 = s_ubb[0], ry0 = s_ubb[1], rx1 = s_ubb[2], ry1 = s_ubb[3], ox0 = 9999, oy0 = 9999, ox1 = -1, oy1 = -1;
    USrc *S = s_us[UT_HEAT];
    for (i = 1; i < g_world.lot_hi; i++) {
        Lot *l = &g_world.lots[i];
        if (l->def == B_NONE) continue;
        if (s_lst[UT_HEAT][i] == NLS_OK || l->def == B_HEAT_PLANT) {
            int lx0, ly0, lx1, ly1;
            l->flags |= LF_HEAT;
            lot_rect_q(l, &lx0, &ly0, &lx1, &ly1);
            rx0 = IMIN(rx0, lx0); ry0 = IMIN(ry0, ly0); rx1 = IMAX(rx1, lx1); ry1 = IMAX(ry1, ly1);
        } else l->flags &= ~LF_HEAT;
    }
    if (s_hbb_full) { rx0 = 0; ry0 = 0; rx1 = w - 1; ry1 = h - 1; s_hbb_full = 0; }
    else if (s_hbb[2] >= 0) { rx0 = IMIN(rx0, s_hbb[0]); ry0 = IMIN(ry0, s_hbb[1]); rx1 = IMAX(rx1, s_hbb[2]); ry1 = IMAX(ry1, s_hbb[3]); }
    rx0 = IMAX(rx0, 0); ry0 = IMAX(ry0, 0); rx1 = IMIN(rx1, w - 1); ry1 = IMIN(ry1, h - 1);
    for (y = ry0; y <= ry1; y++) {
        Tile *t = WT(rx0, y);
        for (x = rx0; x <= rx1; x++, t++) {
            u32 v = INFO(TILE_IDX(x, y));
            int on = 0;
            if (t->lot) on = (g_world.lots[t->lot].flags & LF_HEAT) != 0;
            else if (v != UI_NONE) {
                const USrc *s = &S[v >> 16];
                if (s_nsup[UT_HEAT][s->net] > 0 && (s->cut != DIST_NONE ? (v & 0xFFFF) < s->cut : s_pool[UT_HEAT][s->net] > 0)) on = 1;
            }
            if (on) { ox0 = IMIN(ox0, x); oy0 = IMIN(oy0, y); ox1 = IMAX(ox1, x); oy1 = IMAX(oy1, y); }
            if (on != ((t->zone & ZF_HEAT) != 0)) {
                t->zone = (u8)(on ? (t->zone | ZF_HEAT) : (t->zone & ~ZF_HEAT));
                x0 = IMIN(x0, x); y0 = IMIN(y0, y); x1 = IMAX(x1, x); y1 = IMAX(y1, y);
            }
        }
    }
    s_hbb[0] = ox0; s_hbb[1] = oy0; s_hbb[2] = ox1; s_hbb[3] = oy1;
    if (x1 >= 0) world_notify(x0, y0, x1, y1, WCHG_NET);
}

/* ------------------------------------------------------------------ lakes */
/* PL4: the water cells (s_water), the nearest-water map (s_near) and the cells with water within 2 cells
   (s_near2) depend on the terrain only: kept between updates and rebuilt when a WCHG_TERRAIN rect turns a
   cell's water state (or after a reset/load). Same values and the same loop orders as recomputing them every
   update; the per-cell lake passes visit only the water cells (s_wlist, scan order) and the shore pass only
   cells near water (all other cells are 0, as before). */

static void lake_listener(int x0, int y0, int x1, int y1, int kinds, void *user)
{
    UNUSED(user);
    if (!(kinds & WCHG_TERRAIN)) return;
    if (!s_lk_dirty) { s_lk_x0 = x0; s_lk_y0 = y0; s_lk_x1 = x1; s_lk_y1 = y1; s_lk_dirty = 1; }
    else { s_lk_x0 = IMIN(s_lk_x0, x0); s_lk_y0 = IMIN(s_lk_y0, y0); s_lk_x1 = IMAX(s_lk_x1, x1); s_lk_y1 = IMAX(s_lk_y1, y1); }
}

static u8 cell_water(int cx, int cy)
{
    int a, b, wt = 0, rv = 0;
    for (b = cy * 2; b <= cy * 2 + 1; b++)
        for (a = cx * 2; a <= cx * 2 + 1; a++) {
            int ter;
            if (!world_in(a, b)) continue;
            ter = TER_TYPE(WT(a, b));
            if (ter == TER_WATER) wt++;
            else if (ter == TER_RIVER) rv++;
        }
    return (u8)(wt ? 1 : rv ? 2 : 0);
}

static void lake_cells_build(void)
{
    int cw = cells_w(), ch = cells_h(), cx, cy, qh = 0, qt = 0;
    /* water cells (any water tile in the cell; rivers flush faster) */
    s_nwl = 0;
    for (cy = 0; cy < ch; cy++)
        for (cx = 0; cx < cw; cx++) {
            int c = CELL_IDX(cx, cy);
            s_water[c] = cell_water(cx, cy);
            s_near[c] = -1;
            if (s_water[c]) { s_near[c] = (s16)c; s_cq[qt++] = (u16)c; s_ltmp[c] = 0; s_wlist[s_nwl++] = (u16)c; }
            else { s_lake[c] = 0; s_ltmp[c] = 0xFFFF; }
        }
    /* nearest water cell within CIVIC_LAKE_REACH (4-neighbour cell BFS; s_ltmp = distance) */
    while (qh < qt) {
        int c = s_cq[qh++], ccx = c % FIELD_MAX_W, ccy = c / FIELD_MAX_W, d;
        if (s_ltmp[c] >= CIVIC_LAKE_REACH) continue;
        for (d = 0; d < DIR_COUNT; d++) {
            int nx = ccx + g_dir_dx[d], ny = ccy + g_dir_dy[d], nc;
            if (nx < 0 || ny < 0 || nx >= cw || ny >= ch) continue;
            nc = CELL_IDX(nx, ny);
            if (s_ltmp[nc] != 0xFFFF) continue;
            s_ltmp[nc] = (u16)(s_ltmp[c] + 1); s_near[nc] = s_near[c]; s_cq[qt++] = (u16)nc;
        }
    }
    for (cy = 0; cy < ch; cy++)
        for (cx = 0; cx < cw; cx++) {
            int a, b, n = 0;
            for (b = IMAX(0, cy - 2); b <= IMIN(ch - 1, cy + 2) && !n; b++)
                for (a = IMAX(0, cx - 2); a <= IMIN(cw - 1, cx + 2); a++) if (s_water[CELL_IDX(a, b)]) { n = 1; break; }
            s_near2[CELL_IDX(cx, cy)] = (u8)n;
        }
    s_lk_valid = 1; s_lk_cw = cw; s_lk_ch = ch;
}

static void lake_cells_check(void)
{
    world_add_listener(lake_listener, NULL);            /* dedupes (world_init drops listeners) */
    if (s_lk_cw != cells_w() || s_lk_ch != cells_h()) s_lk_valid = 0;
    if (s_lk_valid && s_lk_dirty) {
        int cx0 = IMAX(0, s_lk_x0 >> 1), cy0 = IMAX(0, s_lk_y0 >> 1), cx1 = IMIN(cells_w() - 1, s_lk_x1 >> 1),
            cy1 = IMIN(cells_h() - 1, s_lk_y1 >> 1), cx, cy;
        for (cy = cy0; cy <= cy1 && s_lk_valid; cy++)
            for (cx = cx0; cx <= cx1; cx++)
                if (cell_water(cx, cy) != s_water[CELL_IDX(cx, cy)]) { s_lk_valid = 0; break; }
    }
    s_lk_dirty = 0;
    if (!s_lk_valid) lake_cells_build();
}

static void lake_update(void)
{
    int cw = cells_w(), ch = cells_h(), cx, cy, i, k, n = 0;
    s64 sum = 0;
    s32 untreated = 0, near_res = 0, all_res = 0;
    lake_cells_check();
    for (k = 0; k < s_nwl; k++) s_lemit[s_wlist[k]] = 0;
    /* untreated sewage: watered homes not served by a works (their sewers end in the lake) */
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        int x0, y0, x1, y1, c;
        if (l->def == B_NONE || !is_res(&g_bdefs[l->def]) || l->status != LOT_ACTIVE || !(l->flags & LF_WATERED) || !l->occupants) continue;
        if (s_lst[UT_SEWAGE][i] == NLS_OK) continue;
        untreated += l->occupants;
        lot_rect_q(l, &x0, &y0, &x1, &y1);
        c = CELL_IDX(((x0 + x1) >> 1) >> FIELD_SHIFT, ((y0 + y1) >> 1) >> FIELD_SHIFT);
        if (s_near[c] >= 0) s_lemit[s_near[c]] += l->occupants;
    }
    for (cy = 0; cy < ch; cy++)                 /* other modules' emissions (M6 paper mill) */
        for (cx = 0; cx < cw; cx++) {
            int c = CELL_IDX(cx, cy);
            if (s_lext[c] && s_near[c] >= 0) s_lemit[s_near[c]] += s_lext[c];
            s_lext[c] = 0;
        }
    s_sum[UT_SEWAGE].untreated = untreated;
    /* add (a resident's worth = 1/4 x LAKE_FIX), spread among water cells (missing neighbours count as the
       cell itself: no leak into the land), decay 1/32 (rivers 1/8) */
    for (k = 0; k < s_nwl; k++) {
        int c = s_wlist[k];
        s_lake[c] = (u16)IMIN(60000, (s32)s_lake[c] + (s32)IMIN(s_lemit[c], 200000u) / 4);
    }
    for (k = 0; k < s_nwl; k++) {
        int c = s_wlist[k], d;
        s32 v = 2 * (s32)s_lake[c];
        cx = c % FIELD_MAX_W; cy = c / FIELD_MAX_W;
        for (d = 0; d < DIR_COUNT; d++) {
            int nx = cx + g_dir_dx[d], ny = cy + g_dir_dy[d], nc;
            if (nx < 0 || ny < 0 || nx >= cw || ny >= ch) { v += s_lake[c]; continue; }
            nc = CELL_IDX(nx, ny);
            v += s_water[nc] ? s_lake[nc] : s_lake[c];
        }
        s_ltmp[c] = (u16)(v / 6);
    }
    for (k = 0; k < s_nwl; k++) {
        int c = s_wlist[k];
        s32 v = s_ltmp[c];
        v -= s_water[c] == 2 ? (v + 7) >> 3 : (v + 31) >> 5;
        s_lake[c] = (u16)IMAX(0, v);
        if (s_lake[c] >= 4 * LAKE_FIX) { sum += s_lake[c] / LAKE_FIX; n++; }
    }
    s_lake_avg = n ? (int)(sum / n) : 0;
    /* shore: worst lake value within 2 cells (separable max: rows into s_ltmp, then columns); 0 away from water */
    for (cy = 0; cy < ch; cy++)
        for (cx = 0; cx < cw; cx++) {
            int a, m = 0;
            if (s_near2[CELL_IDX(cx, cy)])
                for (a = IMAX(0, cx - 2); a <= IMIN(cw - 1, cx + 2); a++) {
                    int c = CELL_IDX(a, cy);
                    if (s_water[c]) m = IMAX(m, IMIN(255, s_lake[c] / LAKE_FIX));
                }
            s_ltmp[CELL_IDX(cx, cy)] = (u16)m;
        }
    for (cy = 0; cy < ch; cy++)
        for (cx = 0; cx < cw; cx++) {
            int b, m = 0;
            if (s_near2[CELL_IDX(cx, cy)])
                for (b = IMAX(0, cy - 2); b <= IMIN(ch - 1, cy + 2); b++) m = IMAX(m, s_ltmp[CELL_IDX(cx, b)]);
            s_shore[CELL_IDX(cx, cy)] = (u8)m;
        }
    /* politics: share of residents living by polluted water */
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_NONE || !is_res(&g_bdefs[l->def]) || l->status != LOT_ACTIVE) continue;
        all_res += l->occupants;
        if (s_shore[CELL_IDX(l->x >> FIELD_SHIFT, l->y >> FIELD_SHIFT)] >= 40) near_res += l->occupants;
    }
    s_lake_reason = all_res ? (int)((s64)near_res * 100 / all_res) : 0;
}

int civic_lake_pollution(int x, int y)
{
    int c;
    if (!world_in(x, y) || !TER_IS_WATER(TER_TYPE(WT(x, y)))) return 0;
    c = CELL_IDX(x >> FIELD_SHIFT, y >> FIELD_SHIFT);
    return IMIN(255, s_lake[c] / LAKE_FIX);
}
int civic_shore_pollution(int x, int y) { return world_in(x, y) ? s_shore[CELL_IDX(x >> FIELD_SHIFT, y >> FIELD_SHIFT)] : 0; }
int civic_lv_lake_term(int cx, int cy)
{
    int s;
    if (cx < 0 || cy < 0 || cx >= FIELD_MAX_W || cy >= FIELD_MAX_H) return 0;
    s = s_shore[CELL_IDX(cx, cy)];
    return s >= CIVIC_LAKE_TINT ? -IMIN(10, s / 8) : 0;
}
int civic_happy_lake_term(int cx, int cy)
{
    int s;
    if (cx < 0 || cy < 0 || cx >= FIELD_MAX_W || cy >= FIELD_MAX_H) return 0;
    s = s_shore[CELL_IDX(cx, cy)];
    return s >= CIVIC_LAKE_TINT ? -IMIN(5, s / 12) : 0;
}
void civic_lake_emit(int x, int y, s32 amount)
{
    int c;
    if (!world_in(x, y) || amount <= 0) return;
    c = CELL_IDX(x >> FIELD_SHIFT, y >> FIELD_SHIFT);
    s_lext[c] = (u16)IMIN(60000, (s32)s_lext[c] + amount);
}
int civic_lake_reason(void) { return s_lake_reason; }
int civic_lake_avg(void) { return s_lake_avg; }

/* ------------------------------------------------------------------ schedule */
void civic_step(int phase, int cycle)
{
    if (phase == 9) {
        if ((cycle & 1) == 0) { util_solve(UT_HEAT); heat_apply(); }
        else { util_solve(UT_SEWAGE); lake_update(); }
    }
    if ((phase & 3) == 2) fire_tick();          /* phases 2, 6, 10, 14: a "day" */
    if (phase == 12) fire_cycle();
}

void civic_month(void)
{
    elements_month();
    fire_month();
}

/* ------------------------------------------------------------------ queries */
int civic_util_source(int ut, int lot, UtilSrcInfo *out)
{
    int i;
    memset(out, 0, sizeof(*out));
    if (ut < 0 || ut >= UT_COUNT) return 0;
    for (i = 1; i <= s_nus[ut]; i++) {
        const USrc *s = &s_us[ut][i];
        if (s->lot != lot) continue;
        out->lot = lot; out->output = s->output; out->own_used = s->own_used; out->shared_used = s->shared_used;
        out->used = s->own_used + s->shared_used; out->spare = s->output - out->used;
        out->lots = s->lots; out->lots_short = s->lots_short; out->short_units = s->short_units;
        out->reach = s->reach; out->at_capacity = s->lots_short > 0; out->network = s->net;
        return 1;
    }
    return 0;
}

int civic_lot_util_state(int ut, int lot)
{
    if (ut < 0 || ut >= UT_COUNT || lot <= 0 || lot >= MAX_LOTS) return NLS_NONE;
    return s_lst[ut][lot];
}

int civic_lot_util_src(int ut, int lot, int *dist)
{
    int si;
    if (dist) *dist = 0;
    if (ut < 0 || ut >= UT_COUNT || lot <= 0 || lot >= MAX_LOTS) return 0;
    si = s_lsi[ut][lot];
    if (!si || si > s_nus[ut]) return 0;
    if (dist) *dist = s_ldist[ut][lot];
    return s_us[ut][si].lot;
}

s32 civic_heat_spare_for(int lot)
{
    int si;
    if (lot <= 0 || lot >= MAX_LOTS) return 0;
    si = s_lsi[UT_HEAT][lot];
    if (!si || si > s_nus[UT_HEAT]) return 0;
    return s_pool[UT_HEAT][s_us[UT_HEAT][si].net];
}

void civic_util_summary(int ut, UtilSummary *out)
{
    if (ut < 0 || ut >= UT_COUNT) { memset(out, 0, sizeof(*out)); return; }
    *out = s_sum[ut];
}

int civic_req_why(int req, int lot, s32 *have, s32 *need)
{
    const Lot *l = world_lot(lot);
    *have = 0; *need = 0;
    if (req == REQ_ELEMENTS) {
        *have = s_elem_left;
        if (l && def_building(l->def)->upgrade_to != B_NONE)
            *need = def_building(def_building(l->def)->upgrade_to)->capacity - def_building(l->def)->capacity;
        return s_elem_out > 0 ? 1 : 0;           /* 0 = no factory output at all, 1 = this month's used up */
    }
    if (req == REQ_HEAT) {
        int st = civic_lot_util_state(UT_HEAT, lot), src;
        UtilSrcInfo si;
        if (st == NLS_NONE) return 0;            /* not connected */
        src = civic_lot_util_src(UT_HEAT, lot, NULL);
        if (src && civic_util_source(UT_HEAT, src, &si)) { *have = si.used; *need = si.output; }
        return st == NLS_SHORT ? 2 : 3;          /* 2 = over capacity, 3 = heated (no spare for the upgrade) */
    }
    return 0;
}

/* ------------------------------------------------------------------ overlays */
u8 civic_overlay_value(int ovl, int param, int x, int y)
{
    const Tile *t;
    int lot, st;
    UNUSED(param);
    if (!world_in(x, y)) return OVV_NONE;
    t = WT(x, y);
    lot = t->lot;
    switch (ovl) {
    case OVL_HEAT:
        if (lot) {
            const Lot *l = &g_world.lots[lot];
            if (l->def == B_HEAT_PLANT) return OVV(OVR_NET, NETC_OK);
            if (!is_consumer(UT_HEAT, l->def)) return OVV_NONE;
            st = s_lst[UT_HEAT][lot];
            return OVV(OVR_NET, st == NLS_OK ? NETC_OK : st == NLS_SHORT ? NETC_SHORTAGE : NETC_NOT_CONNECTED);
        }
        /* PL3: the water overlay's style: heated pipes strong blue, zoned empty land that a new building would
           heat only the faint "wet" dither (was a solid blue carpet over every heated zoned tile) */
        if (!(t->zone & ZF_HEAT)) return OVV_NONE;
        if (t->flags & TF_PIPE) return OVV(OVR_NET, NETC_CARRIER);
        return TILE_ZONE(t) ? OVV(OVR_NET, NETC_WET) : OVV_NONE;
    }
    return OVV_NONE;
}

/* ------------------------------------------------------------------ query lines */
static int util_src_lines(int ut, int lot, char (*L)[96], int n)
{
    UtilSrcInfo si;
    char a[24], b[24];
    const char *unit = ut == UT_HEAT ? "units" : "residents";
    if (!civic_util_source(ut, lot, &si)) { str_copy(L[n++], ut == UT_HEAT ? "Heat: not in service" : "Sewage: not in service", 96); return n; }
    text_fmt_int(a, sizeof(a), si.used); text_fmt_int(b, sizeof(b), si.output);
    str_printf(L[n++], 96, "%s %s / %s %s%s", ut == UT_HEAT ? "Heat:" : "Treats", a, b, unit, si.at_capacity ? " - at capacity" : "");
    str_printf(L[n++], 96, "Serves %d buildings along the pipes, reach %d tiles", si.lots, si.reach);
    if (si.lots_short) {
        text_fmt_int(a, sizeof(a), si.short_units);
        str_printf(L[n++], 96, "%d beyond reach (%s %s): build another %s", si.lots_short, a, unit,
                   ut == UT_HEAT ? "heat plant" : "sewage works");
    } else { text_fmt_int(a, sizeof(a), si.spare); str_printf(L[n++], 96, "Spare %s %s", a, unit); }
    if (ut == UT_HEAT) {                         /* the plant's upkeep is its coal (budget: "Plants and pumps") */
        text_fmt_money(a, sizeof(a), (Money)((s64)g_bdefs[B_HEAT_PLANT].upkeep * s_fuel_pct / 100 * budget_upkeep_index() / 100),
                       sim_date()->year);
        str_printf(L[n++], 96, "Coal and upkeep: %s a month%s", a, s_fuel_pct > 100 ? " (fuel prices up)" : "");
    }
    return n;
}

int civic_lot_line(int lot, int k, char *buf, int size)
{
    static char L[8][96];
    const Lot *l = world_lot(lot);
    const BuildingDef *d;
    int n = 0, st, dist, src;
    if (!l) return 0;
    d = def_building(l->def);
    if (l->status == LOT_BURNING) str_copy(L[n++], "ON FIRE! The brigade is on its way, if there is one.", 96);
    if (l->def == B_HEAT_PLANT) n = util_src_lines(UT_HEAT, lot, L, n);
    else if (l->def == B_SEWAGE_WORKS) n = util_src_lines(UT_SEWAGE, lot, L, n);
    else if (l->def == B_ELEMENT_FACTORY) {
        char a[24], b[24];
        s32 out = civic_factory_output(lot);
        text_fmt_int(a, sizeof(a), out);
        if (l->status != LOT_ACTIVE) str_copy(L[n++], "Precast elements: not in production yet", 96);
        else if (!(l->flags & LF_POWERED)) str_copy(L[n++], "Precast elements: none - the factory has no power", 96);
        else str_printf(L[n++], 96, "Precast elements: %s a month (staff %d%%)", a, d->jobs ? IMIN(100, l->occupants * 100 / d->jobs) : 100);
        text_fmt_int(a, sizeof(a), s_elem_used); text_fmt_int(b, sizeof(b), s_elem_left);
        str_printf(L[n++], 96, "This month: %s used, %s left (1 element = 1 new flat-dweller)", a, b);
    } else {
        if (is_consumer(UT_HEAT, l->def) && l->status != LOT_BUILDING) {
            st = s_lst[UT_HEAT][lot]; src = civic_lot_util_src(UT_HEAT, lot, &dist);
            if (st == NLS_OK && src) str_printf(L[n++], 96, "Heating: yes - heat plant, %d tiles along pipes", dist);
            else if (st == NLS_SHORT) str_copy(L[n++], "Heating: no - pipes reach here, the heat plants are at capacity", 96);
            else if (d->zone == ZONE_APARTMENTS || d->tier >= 2 || d->cat != BCAT_ZONE) str_copy(L[n++], "Heating: own boilers (no district heat)", 96);
        }
        if (is_res(d) && (l->flags & LF_WATERED) && l->status == LOT_ACTIVE) {
            st = s_lst[UT_SEWAGE][lot];
            if (st == NLS_OK) str_copy(L[n++], "Sewage: treated at the sewage works", 96);
            else if (st == NLS_SHORT) str_copy(L[n++], "Sewage: untreated - the works is full, it goes to the lake", 96);
            else str_copy(L[n++], "Sewage: untreated - straight into the nearest lake", 96);
        }
    }
    if (k < 0 || k >= n) return 0;
    str_copy(buf, L[k], size);
    return 1;
}

int civic_tile_line(int x, int y, int k, char *buf, int size)
{
    static char L[4][96];
    const Tile *t;
    int n = 0, v;
    if (!world_in(x, y)) return 0;
    t = WT(x, y);
    if (TER_IS_WATER(TER_TYPE(t))) {
        v = civic_lake_pollution(x, y);
        str_printf(L[n++], 96, "Water quality: %s (sewage %d)", v >= 96 ? "foul" : v >= 40 ? "murky" : v >= 10 ? "fair" : "clean", v);
    } else if ((v = civic_shore_pollution(x, y)) >= CIVIC_LAKE_TINT) {
        str_printf(L[n++], 96, "Polluted shore nearby (%d): land value %d, happiness %d", v,
                   civic_lv_lake_term(x >> FIELD_SHIFT, y >> FIELD_SHIFT), civic_happy_lake_term(x >> FIELD_SHIFT, y >> FIELD_SHIFT));
    }
    if (t->flags & TF_RUBBLE) str_copy(L[n++], "Burnt ruins. Bulldoze them, or the town clears them in a few months.", 96);
    if (k < 0 || k >= n) return 0;
    str_copy(buf, L[k], size);
    return 1;
}

/* ------------------------------------------------------------------ scripts, stats, debug */
int civic_script_line(int n, char **t, int ox, int oy, FILE *out)
{
    if (n >= 1 && str_ieq(t[0], "fire")) {         /* fire X Y: start a fire in the lot there */
        int lot;
        if (n < 3) return SCR_ERROR;
        lot = world_lot_at(atoi(t[1]) + ox, atoi(t[2]) + oy);
        if (!lot || !civic_fire_ignite(lot)) return SCR_ERROR;
        if (out) fprintf(out, "script: fire -> lot %d burning\n", lot);
        return SCR_OK;
    }
    return -1;
}

static int count_lots(int zone, int tier)
{
    return sim_stats()->lots_tier[ICLAMP(zone, 0, ZONE_COUNT - 1)][ICLAMP(tier, 0, TIER_MAX)];
}

s32 civic_stat_value(const char *s, int *ok)
{
    const FireStats *fs = civic_fire_stats();
    *ok = 1;
    if (str_ieq(s, "heated")) return s_sum[UT_HEAT].lots_served;
    if (str_ieq(s, "heat_short")) return s_sum[UT_HEAT].lots_short;
    if (str_ieq(s, "treated")) return s_sum[UT_SEWAGE].used;
    if (str_ieq(s, "untreated")) return s_sum[UT_SEWAGE].untreated;
    if (str_ieq(s, "lake")) return s_lake_avg;
    if (str_ieq(s, "elements")) return s_elem_out;
    if (str_ieq(s, "fires")) return fs->started;
    if (str_ieq(s, "burning")) return fs->burning;
    if (str_ieq(s, "lost")) return fs->lots_lost;
    if (str_ieq(s, "rubble")) return fs->rubble_tiles;
    if (str_ieq(s, "apt2")) return count_lots(ZONE_APARTMENTS, 2);
    if (str_ieq(s, "apt3")) return count_lots(ZONE_APARTMENTS, 3);
    if (str_ieq(s, "com3")) return count_lots(ZONE_COMMERCIAL, 3);
    if (str_ieq(s, "houses3")) return count_lots(ZONE_HOUSES, 3);
    *ok = 0;
    return 0;
}

void civic_dump(FILE *f)
{
    int ut, i;
    const FireStats *fs = civic_fire_stats();
    fprintf(f, "civic: elements %d a month (used %d, left %d)\n", s_elem_out, s_elem_used, s_elem_left);
    for (ut = 0; ut < UT_COUNT; ut++) {
        const UtilSummary *s = &s_sum[ut];
        fprintf(f, "civic: %s: %d sources, %d networks, supply %d, demand %d, used %d, lots served %d short %d unconnected %d",
                ut == UT_HEAT ? "heat" : "sewage", s->sources, s->networks, s->supply, s->demand, s->used, s->lots_served,
                s->lots_short, s->lots_unconnected);
        if (ut == UT_HEAT) fprintf(f, ", fuel price %d%%\n", s_fuel_pct);
        else fprintf(f, ", untreated residents %d\n", s->untreated);
        for (i = 1; i <= s_nus[ut]; i++) {
            const USrc *u = &s_us[ut][i];
            fprintf(f, "civic:   source lot %d at %d,%d: %d/%d own %d shared %d, lots %d short %d, reach %d, net %d\n", u->lot,
                    g_world.lots[u->lot].x, g_world.lots[u->lot].y, u->output - u->left, u->output, u->own_used, u->shared_used,
                    u->lots, u->lots_short, u->reach, u->net);
        }
    }
    fprintf(f, "civic: lakes: avg polluted %d, homes by polluted water %d%%\n", s_lake_avg, s_lake_reason);
    fprintf(f, "civic: fires: started %d (spread %d), put out %d, lots lost %d, displaced %d, burning %d, rubble tiles %d,"
               " outbreaks this year %d\n", fs->started, fs->spread, fs->put_out, fs->lots_lost, fs->displaced, fs->burning,
            fs->rubble_tiles, fs->outbreaks_year);
}

u32 civic_hash(void)
{
    u32 h = HASH_INIT;
    h = hash_bytes(h, &s_elem_out, sizeof(s_elem_out));
    h = hash_bytes(h, &s_elem_left, sizeof(s_elem_left));
    h = hash_bytes(h, s_lake, sizeof(s_lake));
    h = hash_bytes(h, s_lst[UT_HEAT], g_world.lot_hi);
    h = hash_bytes(h, s_lst[UT_SEWAGE], g_world.lot_hi);
    return h ^ fire_hash();
}

/* ------------------------------------------------------------------ save: chunk CIVC v1 */
int civic_save(struct SaveFile *sf)
{
    int ut, hi = g_world.lot_hi;
    save_begin_chunk(sf, "CIVC", 1);
    save_s32(sf, s_elem_out); save_s32(sf, s_elem_left); save_s32(sf, s_elem_used);
    save_s32(sf, hi);
    for (ut = 0; ut < UT_COUNT; ut++) {
        save_s32(sf, s_nus[ut]);
        save_bytes(sf, s_us[ut], (int)sizeof(USrc) * (s_nus[ut] + 1));
        save_bytes(sf, s_pool[ut], (int)sizeof(s32) * (s_nus[ut] + 1));
        save_bytes(sf, s_nsup[ut], (int)sizeof(s32) * (s_nus[ut] + 1));
        save_rle(sf, s_lst[ut], hi); save_rle(sf, s_lsi[ut], hi); save_rle(sf, s_ldist[ut], hi * 2);
        save_bytes(sf, &s_sum[ut], sizeof(UtilSummary));
    }
    save_rle(sf, s_lake, sizeof(s_lake)); save_rle(sf, s_lext, sizeof(s_lext)); save_rle(sf, s_shore, sizeof(s_shore));
    save_s32(sf, s_lake_reason); save_s32(sf, s_lake_avg);
    fire_save(sf);
    save_end_chunk(sf);
    return 1;
}

int civic_load(struct SaveFile *sf)
{
    int ver, ut, hi;
    s32 v;
    civic_reset();
    if (!load_find_chunk(sf, "CIVC", &ver)) return 0;
    s_elem_out = load_s32(sf); s_elem_left = load_s32(sf); s_elem_used = load_s32(sf);
    v = load_s32(sf); hi = ICLAMP(v, 0, MAX_LOTS);            /* never a call inside ICLAMP (common.h) */
    for (ut = 0; ut < UT_COUNT; ut++) {
        v = load_s32(sf); s_nus[ut] = ICLAMP(v, 0, MAX_USRC);
        load_bytes(sf, s_us[ut], (int)sizeof(USrc) * (s_nus[ut] + 1));
        load_bytes(sf, s_pool[ut], (int)sizeof(s32) * (s_nus[ut] + 1));
        load_bytes(sf, s_nsup[ut], (int)sizeof(s32) * (s_nus[ut] + 1));
        load_rle(sf, s_lst[ut], hi); load_rle(sf, s_lsi[ut], hi); load_rle(sf, s_ldist[ut], hi * 2);
        load_bytes(sf, &s_sum[ut], sizeof(UtilSummary));
    }
    load_rle(sf, s_lake, sizeof(s_lake)); load_rle(sf, s_lext, sizeof(s_lext)); load_rle(sf, s_shore, sizeof(s_shore));
    s_lake_reason = load_s32(sf); s_lake_avg = load_s32(sf);
    fire_load(sf, ver);
    return load_ok(sf);
}
