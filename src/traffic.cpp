/* traffic.cpp - v1.0 traffic assignment (DESIGN 5.8, DESIGN_LESSONS 3.5). PORTABLE, deterministic,
   integer-only. Owner: M2 (rewrite of T08's MVP; the T08/T11 API is kept). Model overview: traffic.h.

   State groups (all saved in chunk TRAF v2, so save -> load -> run equals an unbroken run):
   - OUTPUTS: s_tflow (car trips/day per road tile), Tile.aux levels, FLD_TRAFFIC/FLD_NOISE (fields save),
     commute/rail cell maps, last-pass statistics.
   - GRAPH: built from the road tiles at a pass start after a WCHG_ROAD notification; node ids are in tile
     scan order, so a rebuild is a pure function of the roads. A running pass keeps its graph (road edits
     during a pass take effect at the next pass start); tiles that stopped being roads are skipped on output.
   - PASS: snapshot taken at the pass start (origins per node, destination clusters + source nodes, bus
     tables, edge costs) and the accumulators the cluster slices add to. The slices only touch this state.

   Time units (TU): a street tile at free flow = 16 (gravel 20, main 12, motorway 8); walking/cycling a road
   tile = TU_SLOW 40; walking to a stop = TU_WALK 48 per tile. Commute outputs are TU / TU_COMMUTE. */
#include "common.h"
#include "traffic.h"
#include "transit.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "text.h"
#include "save.h"

#define NTILES      (MAP_MAX_W * MAP_MAX_H)
#define CELLS       (FIELD_MAX_W * FIELD_MAX_H)
#define TG_MAXN     6144              /* graph nodes */
#define TG_MAXE     8192              /* graph edges */
#define TC_MAX      32                /* destination clusters */
#define TG_RUN_MAX  6                 /* longest edge in steps: long runs get a node every 6th tile so homes
                                         along them attach nbr where they are (<= 3 tiles off) */
#define REF_NONE    0xFFFFu
#define REF_NODE    0x8000u
#define INF32       0x7FFFFFFF
#define BLK_W       (MAP_MAX_W / TRAFFIC_BLOCK)
#define NBLK        (BLK_W * BLK_W)
#define MAX_SRC     8192              /* (cluster, node) destination attachments per pass */
#define MSA_KMAX    20                /* MSA divisor cap (route split converges; demand changes are rescaled) */
#define SLICE_WORK  1500              /* node visits per sim step (deterministic quota): about one cluster sweep of a
                                         20k town (~600 nodes) per step, ~1-2 ms on the P2 */
#define NO_STOP     0xFF

#define TU_SLOW      40               /* walking/cycling a road tile (a car on a street: 16) */
#define TU_COMMUTE   80               /* commute output unit (TU per "commute tile"): chosen so the values match the
                                         MVP's nearest-jobs road distance the sim's terms were tuned on (traffic test
                                         traffic_commute_vs_nearest, BENCH20K 1967: 5.1 nearest vs ~5-7) */
#define TU_WALK      48               /* walking to/from a stop, per tile (Chebyshev) */
#define TU_PARK      16
#define TU_WAIT      64               /* waiting for the bus */
#define BUS_RIDE_PCT 125              /* bus in-vehicle time vs car (stops, boarding) */
#define WORK_TRIPS   3                /* car trips/day per car-commuting worker (to work, back, one more) */
#define SHOP_TRIPS   1                /* per resident shopping by car: ~5 trips per worker in all (T12 scale) */
#define BUS_VOL      40               /* car-equivalent trips/day per bus on its route */
#define GRAV_STEP    16               /* gravity table step in TU */
#define GRAV_N       512
/* gravity decay exp(-t/kappa) per GRAV_STEP in Q16: work kappa 480 TU (~12 walk/cycle tiles), shopping 240 TU */
#define GRAV_R_WORK  63388
#define GRAV_R_SHOP  61309

static const u8 s_tu[ROAD_CLASS_COUNT] = { 0, 20, 16, 12, 8, 0 };

/* ------------------------------------------------------------------ outputs */
static u16 s_tflow[NTILES];           /* car trips/day per road tile (saturating) */
static u8  s_ccom[CELLS];             /* commute walk tiles per field cell, 255 none */
static u8  s_crail[CELLS];            /* road tiles to the nearest station per cell, 255 none */
static u32 s_passes, s_rebuilds;
static s32 s_avg_commute, s_trips, s_vehicles;
static int s_pct_car, s_pct_bus, s_pct_slow, s_msa_used;
static int s_dirty;                   /* roads changed since the graph was built */

/* ------------------------------------------------------------------ graph */
static int s_nn, s_ne, s_net;
static u16 s_ntile[TG_MAXN];
static u16 s_adj0[TG_MAXN + 1];
static u16 s_adj[2 * TG_MAXE];
static u16 s_ea[TG_MAXE], s_eb[TG_MAXE], s_et0[TG_MAXE], s_ent[TG_MAXE];
static u8  s_ecls[TG_MAXE], s_elen[TG_MAXE];
static s32 s_eflow[TG_MAXE];          /* MSA-smoothed car trips/day */
static u32 s_ecost[TG_MAXE];          /* TU, from s_eflow at the pass start */
static u16 s_etiles[NTILES];          /* interior tiles of all edges, a -> b order */
static u16 s_tref[NTILES];            /* tile -> REF_NODE|node, edge id, or REF_NONE */
static u8  s_nedge[TG_MAXN];          /* node reaches a map-edge link */
static int s_overflow_logged;

/* ------------------------------------------------------------------ pass snapshot + accumulators */
static int s_running, s_cursor, s_ncl, s_own, s_msa_k;
static int s_stage;                   /* TS_* below: work between two passes is spread over steps too */
static int s_own1024;
static s32 s_cJw[TC_MAX], s_cJs[TC_MAX], s_cJind[TC_MAX], s_cJcom[TC_MAX];
static u16 s_csrc0[TC_MAX], s_cnsrc[TC_MAX];
static u8  s_crx[TC_MAX], s_cry[TC_MAX];
static u8  s_cstop[TC_MAX][BUS_MAX_LINES];
static u16 s_cwalk[TC_MAX][BUS_MAX_LINES];
static int s_nsrc;
static u16 s_src[MAX_SRC];
static s32 s_Ww[TG_MAXN], s_Ws[TG_MAXN], s_remw[TG_MAXN], s_rems[TG_MAXN];
static u32 s_Dnw[TG_MAXN], s_Dns[TG_MAXN];   /* gravity normalisers of this pass (sweep A) */
static u32 s_caden[TG_MAXN];          /* sum of work gravity weights (commute potential of any node) */
static s64 s_canum[TG_MAXN];
static u32 s_ocnum[TG_MAXN], s_ocden[TG_MAXN];   /* realised commute of the node's workers */
static u8  s_ncom[TG_MAXN];           /* commute output per node (walk tiles) */
static u8  s_nrail[TG_MAXN];          /* road tiles to a station per node */
static s32 s_eA[TG_MAXE];             /* assigned this pass */
static u16 s_ebus[TG_MAXE];           /* bus volume on the edge (this pass) */
static u8  s_nstop[TG_MAXN][BUS_MAX_LINES];
static int s_nlines;                  /* lines in the snapshot (index = transit line index) */
static u8  s_lact[BUS_MAX_LINES], s_lnst[BUS_MAX_LINES];
static u16 s_lstile[BUS_MAX_LINES][BUS_MAX_STOPS];
static u32 s_lcum[BUS_MAX_LINES][BUS_MAX_STOPS];
static s32 s_lride[BUS_MAX_LINES];
static s32 s_mcar, s_mbus, s_mslow, s_mveh;      /* persons by mode (work trips), vehicles */
static s32 s_veh_prev;                /* vehicles assigned by the previous pass (MSA demand rescale) */
static u32 s_pass_id;                 /* bumped at every pass start (query cache) */
static int s_station_job;             /* the extra Dijkstra from the stations is done */

enum { TS_IDLE, TS_CELLS, TS_REBUILD, TS_START };

/* ------------------------------------------------------------------ scratch (never carried across steps) */
static u32 s_dist[TG_MAXN];
static u16 s_len[TG_MAXN], s_pred[TG_MAXN], s_order[TG_MAXN], s_heap[TG_MAXN], s_hpos[TG_MAXN];
static s32 s_load[TG_MAXN];
static int s_hn;
static u32 s_mdist[TG_MAXN];          /* biggest cluster's tree (traffic_next_dir; cosmetic, not saved) */
static u16 s_mpred[TG_MAXN];
static int s_mvalid;
static u16 s_fw[GRAV_N], s_fs[GRAV_N];
static s32 s_bJw[NBLK], s_bJs[NBLK], s_bJind[NBLK], s_bJcom[NBLK];
static s8  s_bcl[NBLK];
static u16 s_pnode[MAX_LOTS];         /* temp: (node, block) of job lots */
static u16 s_pblk[MAX_LOTS];
static u8  s_stamp[TG_MAXN];
static u8  s_estamp[TG_MAXE];
/* lot -> access road tile cache (pure function of the lot rect and the roads of the current graph) */
static u32 s_lot_key[MAX_LOTS];
static u16 s_lot_road[MAX_LOTS];      /* access road tile, 0xFFFF none */
static u16 s_lot_node[MAX_LOTS];      /* its graph node, 0xFFFF none */
/* query cache */
static int s_q_tile = -1;
static u32 s_q_pass;
static TrafficQuery s_q;
/* timing (debug only) */
static double s_last_slice_ms, s_max_slice_ms, s_last_bnd_ms, s_max_bnd_ms, s_last_rebuild_ms;
static double s_stage_ms[4], s_sweep_max_ms;    /* cells, rebuild, start; max cluster-sweep step (log per pass) */
static int s_listener;
static int s_pred_ok;                 /* PL4: slice work prediction valid (traffic_slice_phase) */

/* ================================================================== helpers */
static void clear_rows(void *arr, int elem, int byte)
{
    int y;
    for (y = 0; y < g_world.h; y++) memset((char *)arr + (size_t)TILE_IDX(0, y) * elem, byte, (size_t)g_world.w * elem);
}

static int car_cls(int x, int y)
{
    int c;
    if ((unsigned)x >= (unsigned)g_world.w || (unsigned)y >= (unsigned)g_world.h) return 0;   /* PL4: world_in inline */
    c = TILE_ROAD(WT(x, y));
    return (c && (g_roads[c].flags & RDF_CARS)) ? c : 0;
}
#define TX(t) ((t) & (MAP_STRIDE - 1))
#define TY(t) ((t) >> MAP_SHIFT)

static int mway_deg(int x, int y)
{
    int d, n = 0;
    for (d = 0; d < DIR_COUNT; d++) if (car_cls(x + g_dir_dx[d], y + g_dir_dy[d]) == ROAD_MOTORWAY) n++;
    return n;
}

/* DESIGN 4.1: a motorway connects to other roads only at its ends and at crossings (a road continues on
   the far side of the motorway tile = an interchange); a street merely touching its side is not linked. */
int traffic_link_ok(int x, int y, int d)
{
    int nx = x + g_dir_dx[d], ny = y + g_dir_dy[d], a = car_cls(x, y), b = car_cls(nx, ny), mx, my, back, o, oc;
    if (!a || !b) return 0;
    if ((a == ROAD_MOTORWAY) == (b == ROAD_MOTORWAY)) return 1;
    if (a == ROAD_MOTORWAY) { mx = x; my = y; back = d; } else { mx = nx; my = ny; back = DIR_OPPOSITE(d); }
    if (mway_deg(mx, my) <= 1) return 1;
    o = DIR_OPPOSITE(back);
    oc = car_cls(mx + g_dir_dx[o], my + g_dir_dy[o]);
    return oc && oc != ROAD_MOTORWAY;
}

int traffic_car_own_pct(int year) { return ICLAMP(25 + (year - 1955) * 3, 25, 85); }

static int cheb(int t0, int t1)
{
    int dx = IABS(TX(t0) - TX(t1)), dy = IABS(TY(t0) - TY(t1));
    return IMAX(dx, dy);
}

/* gravity tables (integer recurrence: identical on VC6 and clang) */
static void grav_init(void)
{
    int i;
    u32 a = 4096u << 4, b = 4096u << 4;     /* Q16 of 4096 (Q12 output) */
    for (i = 0; i < GRAV_N; i++) {
        s_fw[i] = (u16)IMAX(1, (int)(a >> 4)); s_fs[i] = (u16)IMAX(1, (int)(b >> 4));
        a = (u32)(((u64)a * GRAV_R_WORK) >> 16); b = (u32)(((u64)b * GRAV_R_SHOP) >> 16);
    }
}
static u32 fgrav(const u16 *tab, u32 t) { u32 i = t >> 4; return i >= GRAV_N ? 1u : tab[i]; }   /* GRAV_STEP 16 */

/* ================================================================== world listener */
static void on_world_change(int x0, int y0, int x1, int y1, int kinds, void *user)
{
    UNUSED(x0); UNUSED(y0); UNUSED(x1); UNUSED(y1); UNUSED(user);
    if (kinds & WCHG_ROAD) s_dirty = 1;
}

/* ================================================================== graph build */
static int tile_is_node(int x, int y, int c)
{
    int d, deg = 0, b;
    const Tile *t = WT(x, y);
    if (t->flags & TF_EDGE_LINK) return 1;
    for (d = 0; d < DIR_COUNT; d++) {
        b = car_cls(x + g_dir_dx[d], y + g_dir_dy[d]);
        if (!b) continue;
        if ((c == ROAD_MOTORWAY || b == ROAD_MOTORWAY) && !traffic_link_ok(x, y, d)) continue;
        if (b != c) return 1;                               /* class boundary */
        deg++;
    }
    return deg != 2;
}

static int add_node(int t)
{
    if (s_nn >= TG_MAXN) {
        if (!s_overflow_logged) { log_printf("traffic: warning: graph node limit %d reached", TG_MAXN); s_overflow_logged = 1; }
        return -1;
    }
    s_ntile[s_nn] = (u16)t;
    s_tref[t] = (u16)(REF_NODE | s_nn);
    return s_nn++;
}

static void add_edge(int a, int b, int t0, int nt, int cls)
{
    int e = s_ne++, k;
    s_ea[e] = (u16)a; s_eb[e] = (u16)b; s_et0[e] = (u16)t0; s_ent[e] = (u16)nt;
    s_ecls[e] = (u8)cls; s_elen[e] = (u8)IMIN(255, nt + 1);
    for (k = 0; k < nt; k++) s_tref[s_etiles[t0 + k]] = (u16)e;
}

/* trace the run leaving node n through direction d; creates at most one edge */
static void trace(int n, int d)
{
    int t = s_ntile[n], x = TX(t), y = TY(t), cx, cy, ct, r, prev = d, nt = 0, t0 = s_net, cls, k, nd;
    if (!traffic_link_ok(x, y, d)) return;
    cx = x + g_dir_dx[d]; cy = y + g_dir_dy[d]; ct = TILE_IDX(cx, cy);
    r = s_tref[ct];
    if (r != REF_NONE && (r & REF_NODE)) {                 /* adjacent nodes: one edge, from the lower tile */
        int m = r & ~REF_NODE;
        if (t < ct && s_ne < TG_MAXE) {
            int ca = car_cls(x, y), cb = car_cls(cx, cy);
            add_edge(n, m, t0, 0, g_roads[ca].capacity <= g_roads[cb].capacity ? ca : cb);
        }
        return;
    }
    if (r != REF_NONE || s_ne >= TG_MAXE) return;         /* already traced from the other end */
    cls = car_cls(cx, cy);
    for (;;) {
        if (nt >= TG_RUN_MAX - 1) {                        /* long run: this tile becomes a node */
            int m = add_node(ct);
            if (m < 0) break;
            add_edge(n, m, t0, nt, cls);
            return;
        }
        s_etiles[s_net++] = (u16)ct; nt++;
        s_tref[ct] = (u16)s_ne;                            /* provisional: this edge */
        for (k = 0, nd = -1; k < DIR_COUNT; k++)
            if (k != DIR_OPPOSITE(prev) && traffic_link_ok(cx, cy, k)) { nd = k; break; }
        if (nd < 0) break;                                 /* cannot happen for a degree-2 tile */
        cx += g_dir_dx[nd]; cy += g_dir_dy[nd]; ct = TILE_IDX(cx, cy); prev = nd;
        r = s_tref[ct];
        if (r != REF_NONE && (r & REF_NODE)) { add_edge(n, r & ~REF_NODE, t0, nt, cls); return; }
        if (r != REF_NONE) break;                          /* ran into another edge: inconsistent */
    }
    /* fallback (node limit / inconsistency): close the run at its last tile as a node */
    if (nt > 0) {
        int last = s_etiles[s_net - 1], m;
        s_net--; nt--; s_tref[last] = REF_NONE;
        m = add_node(last);
        if (m >= 0) add_edge(n, m, t0, nt, cls);
        else for (k = 0; k < nt; k++) s_tref[s_etiles[t0 + k]] = REF_NONE;
        if (m < 0) s_net = t0;
    }
}

static void build_csr(void)
{
    int i, e, k;
    static u16 fill[TG_MAXN];
    memset(s_adj0, 0, sizeof(s_adj0[0]) * (s_nn + 1));
    for (e = 0; e < s_ne; e++) { s_adj0[s_ea[e] + 1]++; if (s_eb[e] != s_ea[e]) s_adj0[s_eb[e] + 1]++; }
    for (i = 0; i < s_nn; i++) s_adj0[i + 1] = (u16)(s_adj0[i + 1] + s_adj0[i]);
    for (i = 0; i < s_nn; i++) fill[i] = s_adj0[i];
    for (e = 0; e < s_ne; e++) {
        k = s_ea[e]; s_adj[fill[k]++] = (u16)e;
        if (s_eb[e] != s_ea[e]) { k = s_eb[e]; s_adj[fill[k]++] = (u16)e; }
    }
}

static void build_edge_conn(void)
{
    int head = 0, tail = 0, i, k;
    memset(s_nedge, 0, (size_t)s_nn);
    for (i = 0; i < s_nn; i++)
        if (WT(TX(s_ntile[i]), TY(s_ntile[i]))->flags & TF_EDGE_LINK) { s_nedge[i] = 1; s_order[tail++] = (u16)i; }
    while (head < tail) {
        int n = s_order[head++];
        for (k = s_adj0[n]; k < s_adj0[n + 1]; k++) {
            int e = s_adj[k], m = s_ea[e] == n ? s_eb[e] : s_ea[e];
            if (!s_nedge[m]) { s_nedge[m] = 1; s_order[tail++] = (u16)m; }
        }
    }
}

/* tile refs from the graph arrays (after load) */
static void refs_from_graph(void)
{
    int i, e, k;
    clear_rows(s_tref, sizeof(s_tref[0]), 0xFF);
    for (i = 0; i < s_nn; i++) s_tref[s_ntile[i]] = (u16)(REF_NODE | i);
    for (e = 0; e < s_ne; e++) for (k = 0; k < s_ent[e]; k++) s_tref[s_etiles[s_et0[e] + k]] = (u16)e;
}

static u16 s_rtiles[NTILES];          /* PL4: car-road tiles of the map in scan order (graph_rebuild scratch) */

static void graph_rebuild(void)
{
    int x, y, i, e, k, w = g_world.w, h = g_world.h, done = 0, nr = 0, r;
    double t0 = time_ms();
    clear_rows(s_tref, sizeof(s_tref[0]), 0xFF);
    s_nn = s_ne = s_net = 0;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int c = car_cls(x, y);
            if (!c) continue;
            s_rtiles[nr++] = (u16)TILE_IDX(x, y);
            if (tile_is_node(x, y, c)) add_node(TILE_IDX(x, y));
        }
    for (;;) {
        for (; done < s_nn; done++)                         /* split nodes appended while tracing are traced too */
            for (k = 0; k < DIR_COUNT; k++) trace(done, k);
        /* rings without any junction: their first tile (scan order) becomes a node. PL4: scans the road-tile
           list instead of the map (same first tile; roads don't change during the rebuild) */
        for (r = 0, i = -1; r < nr; r++)
            if (s_tref[s_rtiles[r]] == REF_NONE) { i = add_node(s_rtiles[r]); break; }
        if (i < 0) break;
    }
    build_csr();
    build_edge_conn();
    /* new edges start from their tiles' last flows */
    for (e = 0; e < s_ne; e++) {
        s32 sum = 0;
        if (s_ent[e]) { for (k = 0; k < s_ent[e]; k++) sum += s_tflow[s_etiles[s_et0[e] + k]]; sum /= s_ent[e]; }
        else sum = IMIN(s_tflow[s_ntile[s_ea[e]]], s_tflow[s_ntile[s_eb[e]]]);
        s_eflow[e] = sum;
    }
    memset(s_lot_key, 0, sizeof(s_lot_key));
    s_msa_k = 1;
    s_dirty = 0;
    s_mvalid = 0;
    s_rebuilds++;
    s_pred_ok = 0;                                         /* PL4 */
    s_last_rebuild_ms = time_ms() - t0;
}

/* node a tile belongs to for O/D purposes (interior tiles: the nearer end) */
static int tile_node(int t)
{
    int r = s_tref[t], e, k;
    if (r == REF_NONE) return -1;
    if (r & REF_NODE) return r & ~REF_NODE;
    e = r;
    for (k = 0; k < s_ent[e]; k++) if (s_etiles[s_et0[e] + k] == t) break;
    return (k + 1 <= s_ent[e] - k) ? s_ea[e] : s_eb[e];
}

/* ================================================================== lots */
static int lot_road_search(int x0, int y0, int x1, int y1)
{
    int d, x, y, c;
    for (d = 1; d <= ZONE_ROAD_REACH; d++)
        for (y = y0 - d; y <= y1 + d; y++)
            for (x = x0 - d; x <= x1 + d; x++) {
                if (x > x0 - d && x < x1 + d && y > y0 - d && y < y1 + d) { x = x1 + d - 1; continue; }  /* ring only */
                if (!world_in(x, y)) continue;
                c = TILE_ROAD(WT(x, y));
                if (c && (g_roads[c].flags & RDF_ZONE_ACCESS) && (g_roads[c].flags & RDF_CARS)) return TILE_IDX(x, y);
            }
    return -1;
}

static int lot_node(int id)
{
    int x0, y0, x1, y1, t;
    u32 key;
    world_lot_rect(id, &x0, &y0, &x1, &y1);
    key = ((u32)x0 | ((u32)y0 << 8) | ((u32)(x1 - x0) << 16) | ((u32)(y1 - y0) << 20)) + 1;
    if (s_lot_key[id] != key) {
        s_lot_key[id] = key;
        t = lot_road_search(x0, y0, x1, y1);
        s_lot_road[id] = (u16)(t < 0 ? 0xFFFF : t);
        t = t < 0 ? -1 : tile_node(t);
        s_lot_node[id] = (u16)(t < 0 ? 0xFFFF : t);
    }
    return s_lot_node[id] == 0xFFFF ? -1 : s_lot_node[id];
}

/* ================================================================== edge cost (BPR) */
static u32 edge_cost(int e)
{
    int cls = s_ecls[e];
    s32 cap = g_roads[cls].capacity;
    u32 base = (u32)s_elen[e] * s_tu[cls], f;
    s32 r;
    if (cap <= 0) return base ? base : 1;
    r = (s32)IMIN((s64)s_eflow[e] * 256 / cap, (s64)1024);              /* flow/capacity, Q8, max 4 */
    r = (r * r) >> 8; r = (r * r) >> 8;                             /* ^4, Q8 */
    f = (u32)(256 + IMIN(r * 38 / 256, 16 * 256));                  /* 1 + 0.15 x^4, max 17x */
    base = (base * f) >> 8;
    return base ? base : 1;
}

/* per-tile time for bus routes: the tile's class and its own flow; buses suffer half the congestion delay
   (they pull past queues at stops and junctions), which makes a line the remedy for a jammed street */
static u32 tile_cost(int t)
{
    int c = car_cls(TX(t), TY(t));
    s32 cap, r;
    if (!c) return 16;
    cap = g_roads[c].capacity;
    r = (s32)IMIN((s64)s_tflow[t] * 256 / IMAX(1, cap), (s64)1024);
    r = (r * r) >> 8; r = (r * r) >> 8;
    return (u32)s_tu[c] * (u32)(256 + IMIN(r * 38 / 512, 8 * 256)) >> 8;
}

/* ================================================================== Dijkstra (multi-source, scratch arrays) */
static int heap_less(int a, int b) { return s_dist[a] < s_dist[b] || (s_dist[a] == s_dist[b] && a < b); }
static void heap_up(int i)
{
    int n = s_heap[i];
    while (i > 0) {
        int p = (i - 1) >> 1;
        if (!heap_less(n, s_heap[p])) break;
        s_heap[i] = s_heap[p]; s_hpos[s_heap[i]] = (u16)i; i = p;
    }
    s_heap[i] = (u16)n; s_hpos[n] = (u16)i;
}
static void heap_down(int i)
{
    int n = s_heap[i];
    for (;;) {
        int c = 2 * i + 1;
        if (c >= s_hn) break;
        if (c + 1 < s_hn && heap_less(s_heap[c + 1], s_heap[c])) c++;
        if (!heap_less(s_heap[c], n)) break;
        s_heap[i] = s_heap[c]; s_hpos[s_heap[i]] = (u16)i; i = c;
    }
    s_heap[i] = (u16)n; s_hpos[n] = (u16)i;
}

static int dijkstra(const u16 *src, int nsrc)
{
    int i, k, nset = 0;
    for (i = 0; i < s_nn; i++) { s_dist[i] = INF32; s_hpos[i] = 0xFFFF; }
    s_hn = 0;
    for (i = 0; i < nsrc; i++) {
        int n = src[i];
        if (s_dist[n] == 0) continue;
        s_dist[n] = 0; s_len[n] = 0; s_pred[n] = 0;
        s_heap[s_hn] = (u16)n; s_hpos[n] = (u16)s_hn; s_hn++;
        heap_up(s_hn - 1);
    }
    while (s_hn > 0) {
        int n = s_heap[0];
        s_hn--;
        s_hpos[n] = 0xFFFE;                                  /* settled */
        if (s_hn > 0) { s_heap[0] = s_heap[s_hn]; s_hpos[s_heap[0]] = 0; heap_down(0); }
        s_order[nset++] = (u16)n;
        for (k = s_adj0[n]; k < s_adj0[n + 1]; k++) {
            int e = s_adj[k], m = s_ea[e] == n ? s_eb[e] : s_ea[e];
            u32 nd = s_dist[n] + s_ecost[e];
            if (nd >= s_dist[m]) continue;
            s_dist[m] = nd; s_len[m] = (u16)IMIN(65535, s_len[n] + s_elen[e]); s_pred[m] = (u16)(e + 1);
            if (s_hpos[m] == 0xFFFF) { s_heap[s_hn] = (u16)m; s_hpos[m] = (u16)s_hn; s_hn++; heap_up(s_hn - 1); }
            else heap_up(s_hpos[m]);
        }
    }
    return nset;
}

/* ================================================================== mode split */
static u32 bus_time(int n, int d, int *line)
{
    int L;
    u32 best = INF32;
    *line = -1;
    for (L = 0; L < s_nlines; L++) {
        int so = s_nstop[n][L], sd = s_cstop[d][L];
        u32 t, ride;
        if (so == NO_STOP || sd == NO_STOP || so == sd) continue;
        ride = s_lcum[L][so] > s_lcum[L][sd] ? s_lcum[L][so] - s_lcum[L][sd] : s_lcum[L][sd] - s_lcum[L][so];
        t = (u32)cheb(s_ntile[n], s_lstile[L][so]) * TU_WALK + TU_WAIT + ride * BUS_RIDE_PCT / 100 + s_cwalk[d][L];
        if (t < best) { best = t; *line = L; }
    }
    return best;
}

/* shares of trips by car and bus in 1/1024 (rest walk/cycle) and the mode-weighted time. No bus: one
   multiply-shift (this runs for every node of every cluster: no divisions on the P2). */
#define TMAX 1000000u
static void mode_split(u32 tcar, u32 tslow, u32 tbus, int *pc, int *pb, u32 *tgen)
{
    int own = s_own1024, pbo, pbc, car, bus;
    if (tcar > TMAX) tcar = TMAX;
    if (tslow > TMAX) tslow = TMAX;
    if (tbus >= TMAX) {
        *pc = own; *pb = 0;
        *tgen = ((u32)own * tcar + (u32)(1024 - own) * tslow) >> 10;
        return;
    }
    pbo = ICLAMP(102 + ((s32)tcar - (s32)tbus) * 2, 0, 614);        /* car owners: 10% when as fast, max 60% */
    pbc = ICLAMP(512 + ((s32)tslow - (s32)tbus) * 2, 0, 973);       /* no car: bus vs walking/cycling, max 95% */
    car = (own * (1024 - pbo)) >> 10;
    bus = (own * pbo + (1024 - own) * pbc) >> 10;
    if (car + bus > 1024) bus = 1024 - car;
    *pc = car; *pb = bus;
    *tgen = ((u32)car * tcar + (u32)bus * tbus + (u32)(1024 - car - bus) * tslow) >> 10;
}

/* ================================================================== pass: start */
static void cluster_blocks(void)
{
    /* destination blocks -> at most TC_MAX clusters: the smallest cluster joins its nearest neighbour */
    static u8 cbx[NBLK], cby[NBLK];
    static s32 cj[NBLK];
    static s16 cid[NBLK];
    int n = 0, b, i, j, bw = (g_world.w + TRAFFIC_BLOCK - 1) / TRAFFIC_BLOCK, bh = (g_world.h + TRAFFIC_BLOCK - 1) / TRAFFIC_BLOCK;
    for (b = 0; b < NBLK; b++) s_bcl[b] = -1;
    for (j = 0; j < bh; j++)
        for (i = 0; i < bw; i++) {
            b = j * BLK_W + i;
            if (s_bJw[b] <= 0 && s_bJs[b] <= 0) continue;
            cbx[n] = (u8)i; cby[n] = (u8)j; cj[n] = s_bJw[b] + s_bJs[b]; cid[n] = (s16)b; s_bcl[b] = (s8)0;
            n++;
        }
    /* union by relabelling: lab[k] = representative entry index */
    {
        static s16 lab[NBLK], live[NBLK];
        static s32 sx[NBLK], sy[NBLK], sj[NBLK], ccx[NBLK], ccy[NBLK];
        int alive = n, k, a;
        for (k = 0; k < n; k++) {
            lab[k] = (s16)k; live[k] = (s16)k; sx[k] = cbx[k] * cj[k]; sy[k] = cby[k] * cj[k]; sj[k] = cj[k];
            ccx[k] = cbx[k]; ccy[k] = cby[k];
        }
        while (alive > TC_MAX) {
            int smallest = -1, nbr = -1, si = 0;
            s32 bd = 1 << 30;
            for (a = 0; a < alive; a++) { k = live[a]; if (smallest < 0 || sj[k] < sj[smallest] || (sj[k] == sj[smallest] && k < smallest)) { smallest = k; si = a; } }
            for (a = 0; a < alive; a++) {
                s32 dd;
                k = live[a];
                if (k == smallest) continue;
                dd = IMAX(IABS(ccx[k] - ccx[smallest]), IABS(ccy[k] - ccy[smallest])) * 65536 + IMIN(sj[k], 65535);
                if (dd < bd || (dd == bd && k < nbr)) { bd = dd; nbr = k; }      /* nearest, then the smaller one */
            }
            if (nbr < 0) break;
            lab[smallest] = (s16)nbr;          /* PL4: parent link (was: relabel all; resolved below, same labels) */
            sx[nbr] += sx[smallest]; sy[nbr] += sy[smallest]; sj[nbr] += sj[smallest];
            ccx[nbr] = sx[nbr] / IMAX(1, sj[nbr]); ccy[nbr] = sy[nbr] / IMAX(1, sj[nbr]);
            live[si] = live[--alive];
        }
        for (k = 0; k < n; k++) { int r = k; while (lab[r] != r) r = lab[r]; lab[k] = (s16)r; }   /* PL4 */
        /* cluster ids: representatives sorted by total weight desc (index order on ties) */
        s_ncl = 0;
        for (;;) {
            int best = -1;
            for (k = 0; k < n; k++) if (lab[k] == k && sj[k] >= 0 && (best < 0 || sj[k] > sj[best])) best = k;
            if (best < 0 || s_ncl >= TC_MAX) break;
            for (j = 0; j < n; j++) if (lab[j] == best) s_bcl[cid[j]] = (s8)s_ncl;
            s_crx[s_ncl] = (u8)IMIN(g_world.w - 1, sx[best] / IMAX(1, sj[best]) * TRAFFIC_BLOCK + TRAFFIC_BLOCK / 2);
            s_cry[s_ncl] = (u8)IMIN(g_world.h - 1, sy[best] / IMAX(1, sj[best]) * TRAFFIC_BLOCK + TRAFFIC_BLOCK / 2);
            sj[best] = -1;
            s_ncl++;
        }
    }
}

static void bus_tables(void)
{
    int L, s, d, k, x, y;
    s_nlines = 0;
    memset(s_nstop, NO_STOP, sizeof(s_nstop[0]) * s_nn);
    memset(s_cstop, NO_STOP, sizeof(s_cstop));
    memset(s_lact, 0, sizeof(s_lact));
    memset(s_ebus, 0, sizeof(s_ebus[0]) * s_ne);
    memset(s_estamp, 0, (size_t)s_ne);
    for (L = 0; L < BUS_MAX_LINES; L++) {
        const BusLine *bl = transit_line(L);
        u32 cum = 0;
        int p;
        s_lnst[L] = 0;
        if (!bl->used || bl->status != BLS_OK) continue;
        s_lact[L] = 1; s_nlines = L + 1;
        s_lnst[L] = bl->nstops;
        for (s = 0, p = 0; s < bl->nstops; s++) {
            for (; p < (int)bl->spos[s] && p + 1 < (int)bl->npath; p++) cum += tile_cost(TILE_IDX(bl->px[p + 1], bl->py[p + 1]));
            s_lstile[L][s] = (u16)TILE_IDX(bl->sx[s], bl->sy[s]);
            s_lcum[L][s] = cum;
        }
        /* bus volume on the route's edges (once per edge) */
        for (p = 0; p < bl->npath; p++) {
            int r = s_tref[TILE_IDX(bl->px[p], bl->py[p])];
            if (r == REF_NONE || (r & REF_NODE) || s_estamp[r] == L + 1) continue;
            s_estamp[r] = (u8)(L + 1);
            s_ebus[r] = (u16)IMIN(65535, s_ebus[r] + BUS_VOL * bl->buses);
        }
        /* nodes within walking distance of each stop */
        for (s = 0; s < bl->nstops; s++) {
            int st = s_lstile[L][s];
            for (y = TY(st) - BUS_WALK; y <= TY(st) + BUS_WALK; y++)
                for (x = TX(st) - BUS_WALK; x <= TX(st) + BUS_WALK; x++) {
                    int r, n;
                    if (!world_in(x, y)) continue;
                    r = s_tref[TILE_IDX(x, y)];
                    if (r == REF_NONE || !(r & REF_NODE)) continue;
                    n = r & ~REF_NODE;
                    if (s_nstop[n][L] == NO_STOP || cheb(s_ntile[n], st) < cheb(s_ntile[n], s_lstile[L][s_nstop[n][L]]))
                        s_nstop[n][L] = (u8)s;
                }
        }
        /* each cluster's best stop (from its source nodes) */
        for (d = 0; d < s_ncl; d++) {
            for (k = 0; k < s_cnsrc[d]; k++) {
                int n = s_src[s_csrc0[d] + k], so = s_nstop[n][L], wk;
                if (so == NO_STOP) continue;
                wk = cheb(s_ntile[n], s_lstile[L][so]) * TU_WALK;
                if (s_cstop[d][L] == NO_STOP || wk < s_cwalk[d][L]) { s_cstop[d][L] = (u8)so; s_cwalk[d][L] = (u16)wk; }
            }
        }
    }
}

static void pass_start(void)
{
    int i, b, n, np = 0, k, d;
    transit_pass_prepare();
    s_pass_id++;
    s_own = traffic_car_own_pct(sim_date()->year);
    s_own1024 = s_own * 1024 / 100;
    for (i = 0; i < s_ne; i++) s_ecost[i] = edge_cost(i);
    memset(s_Ww, 0, sizeof(s_Ww[0]) * s_nn); memset(s_Ws, 0, sizeof(s_Ws[0]) * s_nn);
    memset(s_bJw, 0, sizeof(s_bJw)); memset(s_bJs, 0, sizeof(s_bJs));
    memset(s_bJind, 0, sizeof(s_bJind)); memset(s_bJcom, 0, sizeof(s_bJcom));
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *dd;
        s32 jobs = 0;
        if (l->def == B_NONE) continue;
        dd = def_building(l->def);
        if (dd->cat == BCAT_ZONE && (dd->zone == ZONE_HOUSES || dd->zone == ZONE_APARTMENTS)) {
            s32 w;
            if (!l->occupants || l->status != LOT_ACTIVE) continue;
            n = lot_node(i);
            if (n < 0) continue;
            w = (s32)l->occupants * SIM_LABOUR_PCT / 100;
            s_Ww[n] += w ? w : 1;
            s_Ws[n] += l->occupants;
            continue;
        }
        if (l->status != LOT_ACTIVE) continue;
        if (dd->cat == BCAT_ZONE) { if (dd->zone == ZONE_COMMERCIAL || dd->zone == ZONE_INDUSTRY) jobs = dd->capacity; }
        else jobs = dd->jobs;
        if (jobs <= 0) continue;
        n = lot_node(i);
        if (n < 0) continue;
        b = (l->y / TRAFFIC_BLOCK) * BLK_W + l->x / TRAFFIC_BLOCK;
        s_bJw[b] += jobs;
        if (dd->cat == BCAT_ZONE && dd->zone == ZONE_COMMERCIAL) { s_bJs[b] += jobs; s_bJcom[b] += jobs; }
        else if (dd->cat == BCAT_ZONE) s_bJind[b] += jobs;
        if (np < MAX_LOTS) { s_pnode[np] = (u16)n; s_pblk[np] = (u16)b; np++; }
    }
    cluster_blocks();
    memset(s_cJw, 0, sizeof(s_cJw)); memset(s_cJs, 0, sizeof(s_cJs));
    memset(s_cJind, 0, sizeof(s_cJind)); memset(s_cJcom, 0, sizeof(s_cJcom));
    for (b = 0; b < NBLK; b++) {
        if (s_bcl[b] < 0) continue;
        d = s_bcl[b];
        s_cJw[d] += s_bJw[b]; s_cJs[d] += s_bJs[b]; s_cJind[d] += s_bJind[b]; s_cJcom[d] += s_bJcom[b];
    }
    /* sources per cluster (counting sort by cluster, dedupe nodes within a cluster) */
    /* sources per cluster: bucket the (node, block) pairs by cluster, dedupe nodes within a cluster */
    {
        static u16 cnt[TC_MAX + 1], fill[TC_MAX];
        static u16 tmp[MAX_LOTS];
        memset(cnt, 0, sizeof(cnt));
        for (k = 0; k < np; k++) if (s_bcl[s_pblk[k]] >= 0) cnt[s_bcl[s_pblk[k]] + 1]++;
        for (d = 0; d < s_ncl; d++) { cnt[d + 1] = (u16)(cnt[d + 1] + cnt[d]); fill[d] = cnt[d]; }
        for (k = 0; k < np; k++) if (s_bcl[s_pblk[k]] >= 0) tmp[fill[s_bcl[s_pblk[k]]]++] = s_pnode[k];
        s_nsrc = 0;
        memset(s_stamp, 0, (size_t)s_nn);
        for (d = 0; d < s_ncl; d++) {
            s_csrc0[d] = (u16)s_nsrc; s_cnsrc[d] = 0;
            for (k = cnt[d]; k < cnt[d + 1]; k++) {
                int nd = tmp[k];
                if (s_stamp[nd] == d + 1 || s_nsrc >= MAX_SRC) continue;
                s_stamp[nd] = (u8)(d + 1);
                s_src[s_nsrc++] = (u16)nd;
                s_cnsrc[d]++;
            }
        }
    }
    bus_tables();
    for (i = 0; i < s_nn; i++) { s_remw[i] = s_Ww[i]; s_rems[i] = s_Ws[i]; }
    memset(s_Dnw, 0, sizeof(s_Dnw[0]) * s_nn); memset(s_Dns, 0, sizeof(s_Dns[0]) * s_nn);
    memset(s_caden, 0, sizeof(s_caden[0]) * s_nn); memset(s_canum, 0, sizeof(s_canum[0]) * s_nn);
    memset(s_ocnum, 0, sizeof(s_ocnum[0]) * s_nn); memset(s_ocden, 0, sizeof(s_ocden[0]) * s_nn);
    memset(s_eA, 0, sizeof(s_eA[0]) * s_ne);
    memset(s_lride, 0, sizeof(s_lride));
    memset(s_nrail, 255, (size_t)s_nn);
    s_mcar = s_mbus = s_mslow = s_mveh = 0;
    s_cursor = 0; s_station_job = 0;
    s_running = 1;
    s_pred_ok = 0;                                         /* PL4 */
}

/* ================================================================== pass: one cluster
   Sweep A (load == 0): this pass's gravity normalisers D (sum over clusters of J_d f(t_nd)) and every node's
   commute potential. Sweep B (load == 1): the same Dijkstra again (costs are fixed during a pass, so the
   trees are identical), exact shares W * w / D, mode split, car trips pushed down the tree. Mode shares are
   in 1/1024. */
static int do_cluster(int d, int load)
{
    int nset = dijkstra(&s_src[s_csrc0[d]], s_cnsrc[d]), i;
    u32 Jw = (u32)s_cJw[d], Js = (u32)s_cJs[d];
    for (i = 0; i < nset; i++) {
        int n = s_order[i], pc, pb, line = -1;
        u32 tcar = s_dist[n] + TU_PARK, tslow = (u32)s_len[n] * TU_SLOW, tbus, tg, ww, ws;
        s32 pw = 0, ps = 0, veh, cw, cs;
        if (load && !s_Ww[n] && !s_Ws[n]) continue;
        tbus = s_nlines ? bus_time(n, d, &line) : (u32)INF32;
        mode_split(tcar, tslow, tbus, &pc, &pb, &tg);
        ww = Jw * fgrav(s_fw, tg);
        ws = Js * fgrav(s_fs, tg);
        if (!load) {
            s_Dnw[n] += ww; s_Dns[n] += ws;
            s_caden[n] += ww; s_canum[n] += (s64)ww * tg;
            continue;
        }
        if (s_remw[n] > 0 && s_Dnw[n] && ww) {
            pw = (s32)IMIN((s64)s_Ww[n] * ww / s_Dnw[n], (s64)s_remw[n]);
            s_remw[n] -= pw;
        }
        if (s_rems[n] > 0 && s_Dns[n] && ws) {
            ps = (s32)IMIN((s64)s_Ws[n] * ws / s_Dns[n], (s64)s_rems[n]);
            s_rems[n] -= ps;
        }
        if (!pw && !ps) continue;
        cw = (pw * pc) >> 10; cs = (ps * pc) >> 10;
        veh = cw * WORK_TRIPS + cs * SHOP_TRIPS;
        s_load[n] = veh;
        s_mcar += cw;
        if (pb) {
            s32 bw = (pw * pb) >> 10;
            s_mbus += bw; s_mslow += pw - cw - bw;
            if (line >= 0) s_lride[line] += bw * 2 + ((ps * pb) >> 10) * 2;
        } else s_mslow += pw - cw;
        s_mveh += veh;
        s_ocnum[n] += (u32)pw * (tg >> 2); s_ocden[n] += (u32)pw;
    }
    if (!load) return nset;
    /* push the car trips toward the destination along the tree (farthest first) */
    for (i = nset - 1; i >= 0; i--) {
        int n = s_order[i], e;
        if (!s_load[n]) continue;
        e = s_pred[n];
        if (e) { e--; s_eA[e] += s_load[n]; s_load[s_ea[e] == n ? s_eb[e] : s_ea[e]] += s_load[n]; }
        s_load[n] = 0;
    }
    if (d == 0) {                                           /* biggest cluster's tree for traffic_next_dir */
        memcpy(s_mdist, s_dist, sizeof(s_dist[0]) * s_nn);
        memcpy(s_mpred, s_pred, sizeof(s_pred[0]) * s_nn);
        s_mvalid = 1;
    }
    return nset;
}

static int do_stations(void)
{
    static u16 src[16];
    int i, ns = 0, nset;
    for (i = 0; i < transit_stations() && ns < 16; i++) {
        int n = lot_node(transit_station_lot(i));
        if (n >= 0) src[ns++] = (u16)n;
    }
    if (!ns) return 1;
    nset = dijkstra(src, ns);
    for (i = 0; i < nset; i++) s_nrail[s_order[i]] = (u8)IMIN(254, s_len[s_order[i]]);
    return nset;
}

static void pass_start(void);
static void cells_stage(void);

/* PL4: exact work prediction of the cluster sweeps. A cluster's Dijkstra settles every node reachable from
   its sources, so its node count is the summed size of the graph components holding them (a pure function
   of the graph and the pass snapshot: recomputed after a rebuild, a pass start or a load, never saved). */
static u16 s_ncomp[TG_MAXN];          /* component of a node */
static u16 s_compsz[TG_MAXN];         /* nodes per component */
static s32 s_clnset[TC_MAX];          /* predicted nset per cluster */
static void predict_init(void)
{
    int i, k, nc = 0, head, tail, d;
    for (i = 0; i < s_nn; i++) s_ncomp[i] = 0xFFFF;
    for (i = 0; i < s_nn; i++) {
        if (s_ncomp[i] != 0xFFFF) continue;
        head = tail = 0;
        s_order[tail++] = (u16)i; s_ncomp[i] = (u16)nc;
        while (head < tail) {
            int n = s_order[head++];
            for (k = s_adj0[n]; k < s_adj0[n + 1]; k++) {
                int e = s_adj[k], m = s_ea[e] == n ? s_eb[e] : s_ea[e];
                if (s_ncomp[m] == 0xFFFF) { s_ncomp[m] = (u16)nc; s_order[tail++] = (u16)m; }
            }
        }
        s_compsz[nc++] = (u16)tail;
    }
    memset(s_stamp, 0, (size_t)nc);
    for (d = 0; d < s_ncl; d++) {
        s32 n = 0;
        for (k = 0; k < s_cnsrc[d]; k++) {         /* distinct components (s_stamp: pass_start scratch, by component) */
            int c = s_ncomp[s_src[s_csrc0[d] + k]];
            if (s_stamp[c] == d + 1) continue;
            s_stamp[c] = (u8)(d + 1);
            n += s_compsz[c];
        }
        s_clnset[d] = n;
    }
    s_pred_ok = 1;
}

/* 1 = the next sweep slice would finish the sweeps and run the station Dijkstra (reads the live world) */
static int slice_reaches_stations(void)
{
    int c = s_cursor, work = 0;
    if (!s_pred_ok) predict_init();
    while (c < 2 * s_ncl && work < SLICE_WORK) {
        int d = c < s_ncl ? c : c - s_ncl;
        work += (c < s_ncl ? 2 : 3) * s_clnset[d] + 1;
        c++;
    }
    return c >= 2 * s_ncl && work < SLICE_WORK;
}

/* Every step except phase 14: the next piece of work. Between passes: commute/rail cells, a graph rebuild
   (only after road changes), the pass start; during a pass: cluster sweeps up to SLICE_WORK node visits. */
void traffic_slice(void)
{
    int work = 0;
    double t0 = time_ms();
    if (s_stage == TS_CELLS) { cells_stage(); s_stage = TS_REBUILD; s_stage_ms[0] = time_ms() - t0; }
    else if (s_stage == TS_REBUILD) { s_stage_ms[1] = 0; if (s_dirty) { graph_rebuild(); s_stage_ms[1] = time_ms() - t0; } s_stage = TS_START; }
    else if (s_stage == TS_START) { pass_start(); s_stage = TS_IDLE; s_stage_ms[2] = time_ms() - t0; s_sweep_max_ms = 0; }
    else if (s_running && !s_station_job) {
        while (s_cursor < 2 * s_ncl && work < SLICE_WORK) {
            int d = s_cursor < s_ncl ? s_cursor : s_cursor - s_ncl;
            work += (s_cursor < s_ncl ? 2 : 3) * do_cluster(d, s_cursor >= s_ncl) + 1;
            s_cursor++;
        }
        if (s_cursor >= 2 * s_ncl && work < SLICE_WORK) { do_stations(); s_station_job = 1; }
        if (time_ms() - t0 > s_sweep_max_ms) s_sweep_max_ms = time_ms() - t0;
    } else return;
    s_last_slice_ms = time_ms() - t0;
    if (s_last_slice_ms > s_max_slice_ms) s_max_slice_ms = s_last_slice_ms;
}

/* PL4: sim_step's slice. Phase 9 carries the network and heat/sewage solves, so its sweep slice moves to phase
   10, which then runs two. Results are unchanged: sweep slices read only the pass snapshot (the station
   Dijkstra at the end reads the world: a slice that would reach it is never moved), and nothing reads the pass
   accumulators before phase 14. Passes start only at phase 1 (cells 15, rebuild 0, start 1), so "sweeping at
   phase 10, not reaching the stations" means the phase-9 slice was moved; that needs no saved state. */
void traffic_slice_phase(int phase)
{
    int sweep = s_running && !s_station_job && s_stage == TS_IDLE;
    if (phase == 9 && sweep && !slice_reaches_stations()) return;
    if (phase == 10 && sweep && !slice_reaches_stations()) traffic_slice();
    traffic_slice();
}

/* ================================================================== pass: finish (outputs) */
static void cell_min(u8 *cells, int t, int v)
{
    int c = (TY(t) >> FIELD_SHIFT) * FIELD_MAX_W + (TX(t) >> FIELD_SHIFT);
    if (v < cells[c]) cells[c] = (u8)v;
}

/* road-tile values from node values (interior tiles: nearer end + distance), then a 2-cell min dilation */
static void build_cells(u8 *cells, const u8 *nval)
{
    static u8 tmp[CELLS];
    int cw = g_world.w >> FIELD_SHIFT, ch = g_world.h >> FIELD_SHIFT, x, y, e, k, i, r;
    for (y = 0; y < ch; y++) memset(&cells[y * FIELD_MAX_W], 255, (size_t)cw);
    for (i = 0; i < s_nn; i++) if (nval[i] != 255 && car_cls(TX(s_ntile[i]), TY(s_ntile[i]))) cell_min(cells, s_ntile[i], nval[i]);
    for (e = 0; e < s_ne; e++) {
        int va = nval[s_ea[e]], vb = nval[s_eb[e]], nt = s_ent[e];
        if (va == 255 && vb == 255) continue;
        for (k = 0; k < nt; k++) {
            int t = s_etiles[s_et0[e] + k], v = 255;
            if (!car_cls(TX(t), TY(t))) continue;
            if (va != 255) v = va + k + 1;
            if (vb != 255) v = IMIN(v, vb + nt - k);
            cell_min(cells, t, IMIN(254, v));
        }
    }
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            int m = 255;
            for (r = -2; r <= 2; r++) if (x + r >= 0 && x + r < cw) m = IMIN(m, cells[y * FIELD_MAX_W + x + r]);
            tmp[y * FIELD_MAX_W + x] = (u8)m;
        }
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            int m = 255;
            for (r = -2; r <= 2; r++) if (y + r >= 0 && y + r < ch) m = IMIN(m, tmp[(y + r) * FIELD_MAX_W + x]);
            cells[y * FIELD_MAX_W + x] = (u8)m;
        }
}

static void write_tile(int t, s32 flow, u8 *ftr, u8 *fno, int *rect)
{
    Tile *tl = &g_world.t[t];
    int c = car_cls(TX(t), TY(t)), lvl, cell, x = TX(t), y = TY(t);
    if (!c) return;
    s_tflow[t] = (u16)IMIN(65535, flow);
    lvl = g_roads[c].capacity > 0 ? (int)IMIN((s64)255, (s64)flow * 128 / g_roads[c].capacity) : 0;
    if ((tl->aux >> 5) != (lvl >> 5)) {
        if (x < rect[0]) rect[0] = x;
        if (y < rect[1]) rect[1] = y;
        if (x > rect[2]) rect[2] = x;
        if (y > rect[3]) rect[3] = y;
    }
    tl->aux = (u8)lvl;
    cell = (y >> FIELD_SHIFT) * FIELD_MAX_W + (x >> FIELD_SHIFT);
    ftr[cell] = (u8)IMIN(255, ftr[cell] + lvl / 8);
    fno[cell] = (u8)IMIN(255, fno[cell] + g_roads[c].noise * lvl / 64);
}

static void pass_finish(void)
{
    int e, i, k, y, cw = g_world.w >> FIELD_SHIFT, rect[4];
    s64 num = 0, den = 0;
    s32 tot;
    u8 *ftr = field_data_rw(FLD_TRAFFIC), *fno = field_data_rw(FLD_NOISE);
    static s32 nflow[TG_MAXN];
    /* MSA. Demand growth/decline is applied to the smoothed flows at once (scaled by the change of the total),
       so k can grow large and only the route split is averaged: the route split converges without the
       slow lag of a large divisor on a growing town. */
    s_msa_k = IMIN(s_msa_k + 1, MSA_KMAX);
    {
        s32 num = IMAX(1, s_mveh), den = IMAX(1, s_veh_prev);
        if (s_veh_prev <= 0 || num * 2 < den || num > den * 2) s_msa_k = IMIN(s_msa_k, 2);   /* big jump: re-adapt */
        for (e = 0; e < s_ne; e++) {
            s32 target = s_eA[e] + s_ebus[e], f = s_veh_prev > 0 ? (s32)((s64)s_eflow[e] * num / den) : s_eflow[e];
            s_eflow[e] = f + (target - f) / s_msa_k;
        }
        s_veh_prev = s_mveh;
    }
    s_msa_used = s_msa_k;
    for (i = 0; i < s_nn; i++) {
        s_ncom[i] = s_caden[i] ? (u8)IMIN((s64)254, (s64)((s_canum[i] / s_caden[i]) / TU_COMMUTE)) : 255;
        if (s_ocden[i]) { num += (s64)s_ocnum[i] * 4; den += s_ocden[i]; }
        nflow[i] = 0;
    }
    /* road tiles: interior = the edge's flow, node tiles = the busiest incident edge */
    rect[0] = rect[1] = 1 << 30; rect[2] = rect[3] = -1;
    for (y = 0; y < (g_world.h >> FIELD_SHIFT); y++) {
        memset(&ftr[y * FIELD_MAX_W], 0, (size_t)cw);
        memset(&fno[y * FIELD_MAX_W], 0, (size_t)cw);
    }
    for (e = 0; e < s_ne; e++) {
        for (k = 0; k < s_ent[e]; k++) write_tile(s_etiles[s_et0[e] + k], s_eflow[e], ftr, fno, rect);
        if (s_eflow[e] > nflow[s_ea[e]]) nflow[s_ea[e]] = s_eflow[e];
        if (s_eflow[e] > nflow[s_eb[e]]) nflow[s_eb[e]] = s_eflow[e];
    }
    for (i = 0; i < s_nn; i++) write_tile(s_ntile[i], nflow[i], ftr, fno, rect);
    if (rect[2] >= 0) world_notify(rect[0], rect[1], rect[2], rect[3], WCHG_FIELDS);
    for (k = 0; k < BUS_MAX_LINES; k++) if (transit_line(k)->used) transit_set_riders(k, s_lact[k] ? s_lride[k] : 0);
    s_avg_commute = den ? (s32)(num / den / TU_COMMUTE) : 0;
    tot = s_mcar + s_mbus + s_mslow;
    s_trips = tot;
    s_vehicles = s_mveh;
    s_pct_car = tot ? (int)((s64)s_mcar * 100 / tot) : 0;
    s_pct_bus = tot ? (int)((s64)s_mbus * 100 / tot) : 0;
    s_pct_slow = tot ? 100 - s_pct_car - s_pct_bus : 0;
    s_running = 0;
    s_passes++;
}

static void cells_stage(void)
{
    int i, any = 0;
    build_cells(s_ccom, s_ncom);
    for (i = 0; i < s_nn && !any; i++) any = s_nrail[i] != 255;
    if (any) build_cells(s_crail, s_nrail);
    else memset(s_crail, 255, sizeof(s_crail));
}

/* Phase 14: closes a finished pass (MSA + tile outputs; cells, rebuild and the next start follow in the
   next steps) or kicks off the very first pass; otherwise a normal slice. */
void traffic_step(void)
{
    double t0 = time_ms();
    if (sim_date()->cycle == SIM_CYCLES_PER_MONTH - 1) transit_month();
    if (s_running && s_station_job) pass_finish();
    else if (!s_running && s_stage == TS_IDLE) {}
    else { traffic_slice(); return; }
    s_stage = TS_CELLS;
    s_last_bnd_ms = time_ms() - t0;
    if (s_last_bnd_ms > s_max_bnd_ms) s_max_bnd_ms = s_last_bnd_ms;
    if (log_enabled())
        log_printf("traffic: pass %u done: %d nodes %d clusters, finish %.2f ms, last cells %.2f rebuild %.2f start %.2f, sweep step max %.2f ms; car %d%% bus %d%%, riders %d",
                   s_passes, s_nn, s_ncl, s_last_bnd_ms, s_stage_ms[0], s_stage_ms[1], s_stage_ms[2], s_sweep_max_ms,
                   s_pct_car, s_pct_bus, transit_total_riders());
}

/* ================================================================== init / reset */
void traffic_init(void) { grav_init(); transit_init(); traffic_reset(); }

void traffic_reset(void)
{
    if (!s_fw[0]) grav_init();
    world_add_listener(on_world_change, NULL);       /* dedupes; world_init drops listeners */
    s_listener = 1;
    /* internal state only: road aux and FLD_TRAFFIC/FLD_NOISE belong to world/fields (saved with them) */
    memset(s_tflow, 0, sizeof(s_tflow));
    memset(s_ccom, 255, sizeof(s_ccom));
    memset(s_crail, 255, sizeof(s_crail));
    memset(s_tref, 0xFF, sizeof(s_tref));
    memset(s_lot_key, 0, sizeof(s_lot_key));
    s_nn = s_ne = s_net = 0;
    s_passes = 0; s_rebuilds = 0;
    s_avg_commute = s_trips = s_vehicles = 0;
    s_pct_car = s_pct_bus = s_pct_slow = 0; s_msa_used = 0;
    s_dirty = 1;
    s_veh_prev = 0;
    s_running = 0; s_cursor = 0; s_ncl = 0; s_nsrc = 0; s_own = 25; s_own1024 = 256; s_msa_k = 1; s_station_job = 0;
    s_stage = TS_IDLE;
    s_nlines = 0; s_mvalid = 0; s_q_tile = -1; s_pass_id = 0;
    s_pred_ok = 0;                                         /* PL4 */
    s_overflow_logged = 0;
    s_max_slice_ms = s_max_bnd_ms = 0;
    transit_reset();
}

/* ================================================================== queries */
int traffic_flow(int x, int y)
{
    if (!car_cls(x, y)) return 0;
    return WT(x, y)->aux;
}

s32 traffic_flow_raw(int x, int y)
{
    if (!car_cls(x, y)) return 0;
    return s_tflow[TILE_IDX(x, y)];
}

int traffic_capacity(int x, int y)
{
    int c = car_cls(x, y);
    return c ? g_roads[c].capacity : 0;
}

/* The pass's cells, with gaps (255: roads newer than the graph, rebuilt at the next pass start) filled from net's
   road BFS to jobs, so new streets get their commute before the next pass. 255 = neither sees a route.
   Merged at most once per sim step (sim's growth scan reads it per tile). */
const u8 *traffic_commute_map(void)
{
    static u8 merged[CELLS];
    static u32 at_step = 0xFFFFFFFFu, at_pass = 0xFFFFFFFFu;
    const u8 *cov = net_cov_dist_map(COV_JOBS);
    int x, y, cw = g_world.w >> FIELD_SHIFT, ch = g_world.h >> FIELD_SHIFT;
    if (!s_passes) return cov;
    if (at_step == sim_date()->total_steps && at_pass == s_passes) return merged;
    at_step = sim_date()->total_steps; at_pass = s_passes;
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            int c = y * FIELD_MAX_W + x;
            merged[c] = s_ccom[c] != 255 ? s_ccom[c] : cov[c];
        }
    return merged;
}

int traffic_commute(int x, int y)
{
    if (!world_in(x, y)) return 255;
    return traffic_commute_map()[(y >> FIELD_SHIFT) * FIELD_MAX_W + (x >> FIELD_SHIFT)];
}

int traffic_route_ok(int x, int y)
{
    if (sim_stats()->jobs <= 0) return 1;
    return traffic_commute(x, y) != 255;
}

int traffic_rail_dist(int x, int y)
{
    if (!world_in(x, y)) return 255;
    return s_crail[(y >> FIELD_SHIFT) * FIELD_MAX_W + (x >> FIELD_SHIFT)];
}

int traffic_rail_access(int x, int y) { return traffic_rail_dist(x, y) <= RAIL_ACCESS_REACH; }

s32 traffic_avg_commute(void) { return s_avg_commute; }
s32 traffic_total_trips(void) { return s_trips; }
u32 traffic_passes(void) { return s_passes; }

int traffic_edge_connected(int x, int y)
{
    int r, n;
    if (!car_cls(x, y)) return 0;
    r = s_tref[TILE_IDX(x, y)];
    if (r == REF_NONE) return 0;
    n = (r & REF_NODE) ? (r & ~REF_NODE) : s_ea[r];
    return s_nedge[n];
}

/* direction from tile t to the adjacent tile u */
static int dir_to(int t, int u)
{
    int d;
    for (d = 0; d < DIR_COUNT; d++) if (TX(t) + g_dir_dx[d] == TX(u) && TY(t) + g_dir_dy[d] == TY(u)) return d;
    return -1;
}

int traffic_next_dir(int x, int y)
{
    int t, r, e, k, n, toward_a;
    if (!car_cls(x, y) || !s_mvalid) return -1;
    t = TILE_IDX(x, y);
    r = s_tref[t];
    if (r == REF_NONE) return -1;
    if (r & REF_NODE) {
        n = r & ~REF_NODE;
        e = s_mpred[n];
        if (!e || s_mdist[n] == (u32)INF32) return -1;
        e--;
        if (s_ent[e]) return dir_to(t, s_ea[e] == n ? s_etiles[s_et0[e]] : s_etiles[s_et0[e] + s_ent[e] - 1]);
        return dir_to(t, s_ntile[s_ea[e] == n ? s_eb[e] : s_ea[e]]);
    }
    e = r;
    for (k = 0; k < s_ent[e]; k++) if (s_etiles[s_et0[e] + k] == t) break;
    if (s_mdist[s_ea[e]] == (u32)INF32 && s_mdist[s_eb[e]] == (u32)INF32) return -1;
    toward_a = s_mdist[s_ea[e]] <= s_mdist[s_eb[e]];
    if (toward_a) return dir_to(t, k > 0 ? s_etiles[s_et0[e] + k - 1] : s_ntile[s_ea[e]]);
    return dir_to(t, k + 1 < s_ent[e] ? s_etiles[s_et0[e] + k + 1] : s_ntile[s_eb[e]]);
}

void traffic_stats(TrafficStats *o)
{
    int i;
    memset(o, 0, sizeof(*o));
    o->nodes = s_nn; o->edges = s_ne; o->clusters = s_ncl;
    for (i = 0; i < s_nn; i++) if (s_Ww[i]) o->origins++;
    o->msa_k = s_msa_used;
    o->pass_running = s_running; o->cluster_cursor = s_cursor;
    o->passes = s_passes; o->rebuilds = s_rebuilds;
    o->car_pct = s_pct_car; o->bus_pct = s_pct_bus; o->slow_pct = s_pct_slow;
    o->own_pct = s_own; o->trips = s_trips; o->vehicles = s_vehicles;
    o->last_slice_ms = s_last_slice_ms; o->max_slice_ms = s_max_slice_ms;
    o->last_boundary_ms = s_last_bnd_ms; o->max_boundary_ms = s_max_bnd_ms;
    o->last_rebuild_ms = s_last_rebuild_ms;
}

/* ---- road query breakdown: recompute this pass's assignment for one edge, by origin district (16x16) */
static int cluster_kind(int d)
{
    s32 j = s_cJw[d];
    if (j <= 0) return TQK_JOBS;
    if (s_cJind[d] * 2 > j) return TQK_INDUSTRY;
    if (s_cJcom[d] * 2 > j) return TQK_SHOPS;
    if (s_cJind[d] + s_cJcom[d] < j / 2) return TQK_CIVIC;
    return TQK_JOBS;
}

static void query_breakdown(int eq, TrafficQuery *q)
{
    static s32 acc[256];
    static u8 pass[TG_MAXN];
    int d, i, best_d = -1, best_dist = 0;
    s32 best = 0, total = 0;
    for (d = 0; d < s_ncl; d++) {
        int nset = dijkstra(&s_src[s_csrc0[d]], s_cnsrc[d]);
        memset(acc, 0, sizeof(acc));
        for (i = 0; i < nset; i++) {
            int n = s_order[i], e = s_pred[n], pc, pb, line = -1, di;
            u32 tcar, tslow, tbus, tg, ww, ws;
            s32 pw = 0, ps = 0, veh;
            pass[n] = 0;
            if (e) { e--; pass[n] = (u8)(e == eq || pass[s_ea[e] == n ? s_eb[e] : s_ea[e]]); }
            if (!pass[n] || (!s_Ww[n] && !s_Ws[n])) continue;
            tcar = s_dist[n] + TU_PARK; tslow = (u32)s_len[n] * TU_SLOW;
            tbus = s_nlines ? bus_time(n, d, &line) : (u32)INF32;
            mode_split(tcar, tslow, tbus, &pc, &pb, &tg);
            ww = (u32)s_cJw[d] * fgrav(s_fw, tg); ws = (u32)s_cJs[d] * fgrav(s_fs, tg);
            if (s_Dnw[n]) pw = (s32)((s64)s_Ww[n] * ww / s_Dnw[n]);
            if (s_Dns[n]) ps = (s32)((s64)s_Ws[n] * ws / s_Dns[n]);
            veh = ((pw * pc) >> 10) * WORK_TRIPS + ((ps * pc) >> 10) * SHOP_TRIPS;
            if (!veh) continue;
            di = (TX(s_ntile[n]) >> 4) + (TY(s_ntile[n]) >> 4) * 16;
            acc[di & 255] += veh;
            total += veh;
        }
        for (i = 0; i < 256; i++) if (acc[i] > best) { best = acc[i]; best_d = d; best_dist = i; }
    }
    if (best_d >= 0 && total > 0) {
        q->top_pct = (int)((s64)best * 100 / total);
        q->top_from_x = (best_dist & 15) * 16 + 8; q->top_from_y = (best_dist >> 4) * 16 + 8;
        q->top_to_x = s_crx[best_d]; q->top_to_y = s_cry[best_d];
        q->top_to_kind = cluster_kind(best_d);
    }
}

int traffic_query(int x, int y, TrafficQuery *out)
{
    int c = car_cls(x, y), t, r, eq = -1, k, L;
    memset(out, 0, sizeof(*out));
    if (!c) return 0;
    t = TILE_IDX(x, y);
    out->road_class = c;
    out->flow = s_tflow[t];
    out->capacity = g_roads[c].capacity;
    out->level_pct = out->capacity ? (int)((s64)out->flow * 100 / out->capacity) : 0;
    out->top_pct = -1;
    out->lines = transit_tile_lines(x, y);
    for (L = 0; L < BUS_MAX_LINES; L++) if (out->lines & (1 << L)) out->bus_riders += transit_line(L)->riders;
    if (s_q_tile == t && s_q_pass == s_pass_id && s_q.top_pct >= 0) {
        out->top_pct = s_q.top_pct; out->top_from_x = s_q.top_from_x; out->top_from_y = s_q.top_from_y;
        out->top_to_x = s_q.top_to_x; out->top_to_y = s_q.top_to_y; out->top_to_kind = s_q.top_to_kind;
        return 1;
    }
    r = s_tref[t];
    if (r != REF_NONE && s_ncl > 0 && s_pass_id && s_running && s_cursor >= s_ncl) {   /* normalisers complete */
        if (r & REF_NODE) {
            int n = r & ~REF_NODE;
            s32 bf = -1;
            for (k = s_adj0[n]; k < s_adj0[n + 1]; k++) if (s_eflow[s_adj[k]] > bf) { bf = s_eflow[s_adj[k]]; eq = s_adj[k]; }
        } else eq = r;
        if (eq >= 0) query_breakdown(eq, out);
    }
    s_q = *out; s_q_tile = t; s_q_pass = s_pass_id;
    return 1;
}

int traffic_query_text(int x, int y, char lines[][96], int max)
{
    TrafficQuery q;
    char a[32], b[32], from[NAME_LEN], to[NAME_LEN];
    int n = 0, L, first = 1;
    static const char *const kind[4] = { "jobs in", "the industry of", "the shops of", "the offices of" };
    if (max <= 0 || !traffic_query(x, y, &q)) return 0;
    text_fmt_int(a, sizeof(a), q.flow); text_fmt_int(b, sizeof(b), q.capacity);
    str_printf(lines[n++], 96, "Flow %s / capacity %s (%d%%)", a, b, q.level_pct);
    if (n < max && q.top_pct > 0) {
        text_district_at(q.top_from_x, q.top_from_y, from, sizeof(from));
        text_district_at(q.top_to_x, q.top_to_y, to, sizeof(to));
        str_printf(lines[n++], 96, "%d%% from %s to %s %s", q.top_pct, from, kind[q.top_to_kind & 3], to);
    }
    if (n < max && q.lines) {
        char *p = lines[n];
        int len = str_printf(p, 96, "Bus line");
        for (L = 0; L < BUS_MAX_LINES; L++)
            if (q.lines & (1 << L)) { len += str_printf(p + len, 96 - len, "%s %d", first ? "" : ",", transit_line(L)->number); first = 0; }
        text_fmt_int(a, sizeof(a), q.bus_riders);
        str_printf(p + len, 96 - len, ": %s rides a day", a);
        n++;
    }
    return n;
}

void traffic_dump(FILE *f)
{
    int i, e, best[8], nb = 0, k;
    if (!f) return;
    fprintf(f, "traffic: graph %d nodes %d edges %d interior tiles, rebuilds %u, dirty %d\n", s_nn, s_ne, s_net, s_rebuilds, s_dirty);
    fprintf(f, "traffic: passes %u, running %d cursor %d/%d clusters, msa k %d, own %d%%\n", s_passes, s_running, s_cursor, s_ncl, s_msa_used, s_own);
    fprintf(f, "traffic: trips %d (car %d%% bus %d%% walk/cycle %d%%), vehicles %d/day, avg commute %d\n",
            s_trips, s_pct_car, s_pct_bus, s_pct_slow, s_vehicles, s_avg_commute);
    {
        s32 w = 0, rem = 0, no = 0, nod = 0;
        for (i = 0; i < s_nn; i++) if (s_Ww[i]) { w += s_Ww[i]; rem += s_remw[i]; no++; if (!s_Dnw[i]) nod++; }
        fprintf(f, "traffic: origins %d nodes, %d workers, %d not yet assigned this pass, %d without a destination\n", no, w, rem, nod);
    }
    for (i = 0; i < s_ncl; i++)
        fprintf(f, "  cluster %2d at %3d,%3d jobs %6d shops %6d ind %6d sources %d\n", i, s_crx[i], s_cry[i], s_cJw[i], s_cJs[i], s_cJind[i], s_cnsrc[i]);
    for (e = 0; e < s_ne; e++) {
        for (k = nb; k > 0 && s_eflow[best[k - 1]] < s_eflow[e]; k--) if (k < 8) best[k] = best[k - 1];
        if (k < 8) { best[k] = e; if (nb < 8) nb++; }
    }
    for (k = 0; k < nb; k++) {
        e = best[k];
        fprintf(f, "  busy edge %d: %d,%d - %d,%d %s len %d flow %d cap %d cost %u\n", e, TX(s_ntile[s_ea[e]]), TY(s_ntile[s_ea[e]]),
                TX(s_ntile[s_eb[e]]), TY(s_ntile[s_eb[e]]), g_roads[s_ecls[e]].key, s_elen[e], s_eflow[e], g_roads[s_ecls[e]].capacity, s_ecost[e]);
    }
}

/* ================================================================== save ("TRAF" v2) */
#define SV_ARR(a, n)  save_rle(sf, (a), (int)(sizeof((a)[0]) * (n)))
#define LD_ARR(a, n)  load_rle(sf, (a), (int)(sizeof((a)[0]) * (n)))
int traffic_save(struct SaveFile *sf)
{
    save_begin_chunk(sf, "TRAF", 2);
    save_u32(sf, s_passes); save_u32(sf, s_rebuilds);
    save_s32(sf, s_avg_commute); save_s32(sf, s_trips); save_s32(sf, s_vehicles);
    save_s32(sf, s_pct_car); save_s32(sf, s_pct_bus); save_s32(sf, s_pct_slow); save_s32(sf, s_msa_used);
    save_s32(sf, s_dirty); save_s32(sf, s_veh_prev);
    save_rle(sf, s_tflow, sizeof(s_tflow));
    save_rle(sf, s_ccom, sizeof(s_ccom));
    save_rle(sf, s_crail, sizeof(s_crail));
    /* graph */
    save_s32(sf, s_nn); save_s32(sf, s_ne); save_s32(sf, s_net);
    SV_ARR(s_ntile, s_nn); SV_ARR(s_ea, s_ne); SV_ARR(s_eb, s_ne); SV_ARR(s_et0, s_ne); SV_ARR(s_ent, s_ne);
    SV_ARR(s_ecls, s_ne); SV_ARR(s_elen, s_ne); SV_ARR(s_eflow, s_ne); SV_ARR(s_ecost, s_ne); SV_ARR(s_etiles, s_net);
    SV_ARR(s_ncom, s_nn);
    /* pass */
    save_s32(sf, s_running); save_s32(sf, s_cursor); save_s32(sf, s_ncl); save_s32(sf, s_own); save_s32(sf, s_msa_k);
    save_s32(sf, s_station_job); save_u32(sf, s_pass_id); save_s32(sf, s_stage); save_s32(sf, s_own1024); save_s32(sf, s_nsrc); save_s32(sf, s_nlines);
    save_s32(sf, s_mcar); save_s32(sf, s_mbus); save_s32(sf, s_mslow); save_s32(sf, s_mveh);
    if (s_running) {
        SV_ARR(s_cJw, TC_MAX); SV_ARR(s_cJs, TC_MAX); SV_ARR(s_cJind, TC_MAX); SV_ARR(s_cJcom, TC_MAX);
        SV_ARR(s_csrc0, TC_MAX); SV_ARR(s_cnsrc, TC_MAX); SV_ARR(s_crx, TC_MAX); SV_ARR(s_cry, TC_MAX);
        save_rle(sf, s_cstop, sizeof(s_cstop)); save_rle(sf, s_cwalk, sizeof(s_cwalk));
        SV_ARR(s_src, s_nsrc);
        SV_ARR(s_Ww, s_nn); SV_ARR(s_Ws, s_nn); SV_ARR(s_remw, s_nn); SV_ARR(s_rems, s_nn);
        SV_ARR(s_Dnw, s_nn); SV_ARR(s_Dns, s_nn); SV_ARR(s_caden, s_nn); SV_ARR(s_canum, s_nn);
        SV_ARR(s_ocnum, s_nn); SV_ARR(s_ocden, s_nn); SV_ARR(s_nrail, s_nn);
        SV_ARR(s_eA, s_ne); SV_ARR(s_ebus, s_ne);
        save_rle(sf, s_nstop, (int)sizeof(s_nstop[0]) * s_nn);
        save_rle(sf, s_lact, sizeof(s_lact)); save_rle(sf, s_lnst, sizeof(s_lnst));
        save_rle(sf, s_lstile, sizeof(s_lstile)); save_rle(sf, s_lcum, sizeof(s_lcum)); save_rle(sf, s_lride, sizeof(s_lride));
    }
    transit_save(sf);
    save_end_chunk(sf);
    return 1;
}

int traffic_load(struct SaveFile *sf)
{
    int ver, v;
    if (!load_find_chunk(sf, "TRAF", &ver)) return 0;
    if (ver < 2) return 1;                         /* T08 MVP chunk: everything is rebuilt by the next passes */
    s_passes = load_u32(sf); s_rebuilds = load_u32(sf);
    s_avg_commute = load_s32(sf); s_trips = load_s32(sf); s_vehicles = load_s32(sf);
    s_pct_car = load_s32(sf); s_pct_bus = load_s32(sf); s_pct_slow = load_s32(sf); s_msa_used = load_s32(sf);
    s_dirty = load_s32(sf); s_veh_prev = load_s32(sf);
    load_rle(sf, s_tflow, sizeof(s_tflow));
    load_rle(sf, s_ccom, sizeof(s_ccom));
    load_rle(sf, s_crail, sizeof(s_crail));
    v = load_s32(sf); s_nn = ICLAMP(v, 0, TG_MAXN);           /* (never pass a load_* call to ICLAMP: macro) */
    v = load_s32(sf); s_ne = ICLAMP(v, 0, TG_MAXE);
    v = load_s32(sf); s_net = ICLAMP(v, 0, NTILES);
    LD_ARR(s_ntile, s_nn); LD_ARR(s_ea, s_ne); LD_ARR(s_eb, s_ne); LD_ARR(s_et0, s_ne); LD_ARR(s_ent, s_ne);
    LD_ARR(s_ecls, s_ne); LD_ARR(s_elen, s_ne); LD_ARR(s_eflow, s_ne); LD_ARR(s_ecost, s_ne); LD_ARR(s_etiles, s_net);
    LD_ARR(s_ncom, s_nn);
    s_running = load_s32(sf); s_cursor = load_s32(sf); v = load_s32(sf); s_ncl = ICLAMP(v, 0, TC_MAX); s_own = load_s32(sf);
    s_msa_k = load_s32(sf); s_station_job = load_s32(sf); s_pass_id = load_u32(sf); s_stage = load_s32(sf); s_own1024 = load_s32(sf);
    v = load_s32(sf); s_nsrc = ICLAMP(v, 0, MAX_SRC);
    v = load_s32(sf); s_nlines = ICLAMP(v, 0, BUS_MAX_LINES);
    s_mcar = load_s32(sf); s_mbus = load_s32(sf); s_mslow = load_s32(sf); s_mveh = load_s32(sf);
    if (s_running) {
        LD_ARR(s_cJw, TC_MAX); LD_ARR(s_cJs, TC_MAX); LD_ARR(s_cJind, TC_MAX); LD_ARR(s_cJcom, TC_MAX);
        LD_ARR(s_csrc0, TC_MAX); LD_ARR(s_cnsrc, TC_MAX); LD_ARR(s_crx, TC_MAX); LD_ARR(s_cry, TC_MAX);
        load_rle(sf, s_cstop, sizeof(s_cstop)); load_rle(sf, s_cwalk, sizeof(s_cwalk));
        LD_ARR(s_src, s_nsrc);
        LD_ARR(s_Ww, s_nn); LD_ARR(s_Ws, s_nn); LD_ARR(s_remw, s_nn); LD_ARR(s_rems, s_nn);
        LD_ARR(s_Dnw, s_nn); LD_ARR(s_Dns, s_nn); LD_ARR(s_caden, s_nn); LD_ARR(s_canum, s_nn);
        LD_ARR(s_ocnum, s_nn); LD_ARR(s_ocden, s_nn); LD_ARR(s_nrail, s_nn);
        LD_ARR(s_eA, s_ne); LD_ARR(s_ebus, s_ne);
        load_rle(sf, s_nstop, (int)sizeof(s_nstop[0]) * s_nn);
        load_rle(sf, s_lact, sizeof(s_lact)); load_rle(sf, s_lnst, sizeof(s_lnst));
        load_rle(sf, s_lstile, sizeof(s_lstile)); load_rle(sf, s_lcum, sizeof(s_lcum)); load_rle(sf, s_lride, sizeof(s_lride));
    }
    transit_load(sf);
    refs_from_graph();
    build_csr();
    build_edge_conn();
    s_mvalid = 0; s_q_tile = -1;
    s_pred_ok = 0;                                         /* PL4 */
    if (!load_ok(sf)) { traffic_reset(); return 0; }
    return 1;
}
