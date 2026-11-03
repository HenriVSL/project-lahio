/* lifecar.cpp - cosmetic car logic. PORTABLE. Owner: T11. See lifecar.h for the model.
   Lane offsets and turning reference: T05's demo_update/demo_draw (render.cpp), STYLE.md 9, and the road
   geometry of art/art_terrain.py (half widths: gravel 6.5, street 7, main 9.5, motorway 12.5 units;
   curves are quarter circles of radius 16 units about the tile corner). */
#include "common.h"
#include "lifecar.h"
#include "world.h"
#include "defs.h"
#include "traffic.h"
#include "iso.h"
#include "util.h"
#include <math.h>

#define BIG_GAP     0x3fffffff
#define FLOW_FLOOR  s_floor         /* every car road gets some cars; F3: scaled with car ownership */
#define GAP_Q       (LC_GAP << 8)
#define ACCEL_Q     (420 << 8)      /* fx*256 per s^2 */
#define STUCK_MS    3000
#define GHOST_MS    1500
#define SCAN_ROWS   2               /* traffic-live detection: map rows per update */

static LcCar  s_cars[LC_MAX_CARS];
static int    s_count;
static u8     s_head[MAP_MAX_W * MAP_MAX_H];    /* per tile: first car slot + 1 */
static u8     s_next[LC_MAX_CARS];
static u16    s_listTile[LC_MAX_CARS];
static u8     s_listed[LC_MAX_CARS];
static u8     s_order[LC_MAX_CARS];
static int    s_norder;
static const u8 s_bits[16] = { 0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4 };
static s16    s_sin[257];                       /* sin 0..pi/2, Q14 */
static Rng    s_rng;
static LcStats s_st;
static int    s_forceFlow = -1;
static int    s_scanRow;
static s32    s_ema;                            /* mean (flow + floor) over view samples, Q16 */
static int    s_emaZoom = -1;
static int    s_inited;
static int    s_floor = 12;                     /* F3 (T12 note): 12 x lc_own_pct(year) / 50, set per update:
                                                   ~9 / 29 / 51 cars in the 1956 / 1965 / 1975 views */

static const s16 s_baseSpeed[ROAD_CLASS_COUNT] = { 0, 190, 260, 330, 440, 0 };   /* fx per second */
static const s16 s_flowBonus[ROAD_CLASS_COUNT] = { 0, 0, 16, 48, 64, 0 };

/* ------------------------------------------------------------------ small helpers */
static int sin_q(int i) { return i <= 256 ? s_sin[i] : s_sin[512 - i]; }          /* i: 0..512 = 0..pi */
static int cos_q(int i) { return i <= 256 ? s_sin[256 - i] : -s_sin[i - 256]; }
static s32 qmul(s32 r, int q) { return (r * q + 8192) >> 14; }
static s32 arc_len(s32 r) { return (r * 402 + 128) >> 8; }                      /* r * pi/2 */
static s32 semi_len(s32 r) { return (r * 804 + 128) >> 8; }                     /* r * pi */
#define RIGHT_OF(d) (((d) + 1) & 3)
#define LEFT_OF(d)  (((d) + 3) & 3)

int lc_car_road(int x, int y)
{
    int c;
    if (!world_in(x, y)) return 0;
    c = TILE_ROAD(WT(x, y));
    return c && (g_roads[c].flags & RDF_CARS);
}

static int flow_estimate(int x, int y)
{
    int dx, dy, sum = 0, est;
    for (dy = -2; dy <= 2; dy++)
        for (dx = -2; dx <= 2; dx++) {
            const Tile *t;
            if (!world_in(x + dx, y + dy)) continue;
            t = WT(x + dx, y + dy);
            if (t->lot) sum += g_world.lots[t->lot].occupants;
        }
    est = sum / 3 + s_flowBonus[TILE_ROAD(WT(x, y))];
    return est > 255 ? 255 : est;
}

int lc_flow(int x, int y)
{
    if (s_forceFlow >= 0) return s_forceFlow;
    if (!world_in(x, y)) return 0;
    if (s_st.traffic_live) return traffic_flow(x, y);
    return flow_estimate(x, y);
}

int lc_own_pct(int year)
{
    int p = 25 + (year - 1955) * 5;
    return ICLAMP(p, 15, 120);
}

int lc_lane_offset(int cls, int lane)
{
    switch (cls) {
    case ROAD_GRAVEL:   return 20;
    case ROAD_MAIN:     return 38;
    case ROAD_MOTORWAY: return lane ? 77 : 40;
    default:            return 29;
    }
}

static s32 path_len(const LcCar *c)
{
    if (c->dout == c->din) return 256 << 8;
    if (c->dout == DIR_OPPOSITE(c->din)) return (256 + semi_len(c->lat)) << 8;
    return arc_len(c->dout == RIGHT_OF(c->din) ? 128 - c->lat : 128 + c->lat) << 8;
}

void lc_car_point(const LcCar *c, s32 *fx, s32 *fy, int *dir)
{
    int d = c->din, e = c->dout, dx = g_dir_dx[d], dy = g_dir_dy[d], rx = -dy, ry = dx;
    s32 cx = c->x * 256 + 128, cy = c->y * 256 + 128, a = c->s >> 8, L = c->lat;
    if (a < 0) a = 0;
    if (e == d) {                                              /* straight, lateral blend */
        s32 l;
        if (a > 256) a = 256;
        l = c->lat0 + (c->lat - c->lat0) * a / 256;
        *fx = cx + dx * (a - 128) + rx * l;
        *fy = cy + dy * (a - 128) + ry * l;
        *dir = d;
    } else if (e == DIR_OPPOSITE(d)) {                         /* U-turn: in, semicircle, out */
        s32 semi = semi_len(L);
        if (a < 128) {
            *fx = cx + dx * (a - 128) + rx * L; *fy = cy + dy * (a - 128) + ry * L; *dir = d;
        } else if (a < 128 + semi) {
            int i = (int)((a - 128) * 512 / semi), cq = cos_q(i), sq = sin_q(i);
            *fx = cx + rx * qmul(L, cq) + dx * qmul(L, sq);
            *fy = cy + ry * qmul(L, cq) + dy * qmul(L, sq);
            *dir = i < 171 ? d : i < 341 ? LEFT_OF(d) : e;
        } else {
            s32 a2 = a - 128 - semi;
            if (a2 > 128) a2 = 128;
            *fx = cx - dx * a2 - rx * L; *fy = cy - dy * a2 - ry * L; *dir = e;
        }
    } else {                                                   /* quarter arc about the corner */
        int ex = g_dir_dx[e], ey = g_dir_dy[e], i;
        s32 R = (e == RIGHT_OF(d)) ? 128 - L : 128 + L, al = arc_len(R), kx, ky, rc, rs;
        kx = cx - dx * 128 + ex * 128; ky = cy - dy * 128 + ey * 128;
        i = (int)(a * 256 / al);
        if (i > 256) i = 256;
        rc = qmul(R, cos_q(i)); rs = qmul(R, sin_q(i));
        *fx = kx - ex * rc + dx * rs;
        *fy = ky - ey * rc + dy * rs;
        *dir = i < 128 ? d : e;
    }
    /* the exact far edge belongs to the neighbour: keep the point on the car's tile (depth = car tile) */
    *fx = ICLAMP(*fx, c->x * 256, c->x * 256 + 255);
    *fy = ICLAMP(*fy, c->y * 256, c->y * 256 + 255);
}

/* exits a car can take from (x, y): bit e if the neighbour is a car road (or off the map at an edge link) */
static int exit_mask(int x, int y)
{
    const Tile *t = WT(x, y);
    int m = TILE_ROAD_MASK(t), e, out = 0;
    for (e = 0; e < DIR_COUNT; e++) {
        int nx = x + g_dir_dx[e], ny = y + g_dir_dy[e];
        if (!(m & (1 << e))) continue;
        if (!world_in(nx, ny)) { if (t->flags & TF_EDGE_LINK) out |= 1 << e; }
        else if (lc_car_road(nx, ny)) out |= 1 << e;
    }
    return out;
}

static int exit_weight(int x, int y, int e)
{
    int nx = x + g_dir_dx[e], ny = y + g_dir_dy[e];
    return 24 + (world_in(nx, ny) ? lc_flow(nx, ny) : lc_flow(x, y));
}

static int choose_exit(int x, int y, int heading)
{
    int m = exit_mask(x, y), e, w[DIR_COUNT], total = 0, r;
    for (e = 0; e < DIR_COUNT; e++) {
        w[e] = 0;
        if (e == DIR_OPPOSITE(heading) || !(m & (1 << e))) continue;
        w[e] = exit_weight(x, y, e);
        if (e == heading) w[e] = w[e] * 3 / 2;
        total += w[e];
    }
    if (!total) return DIR_OPPOSITE(heading);
    r = rng_below(&s_rng, total);
    for (e = 0; e < DIR_COUNT; e++) { if (r < w[e]) return e; r -= w[e]; }
    return heading;
}

static void enter_tile(LcCar *c, int heading)
{
    int cls = TILE_ROAD(WT(c->x, c->y));
    c->din = (u8)heading;
    c->dout = (u8)choose_exit(c->x, c->y, heading);
    c->lat0 = c->lat;
    c->lat = (s16)lc_lane_offset(cls, c->lane);
    if (c->dout != c->din) c->lat0 = c->lat;
    c->len = path_len(c);
    if (c->dout == c->din) {} else if (c->dout == DIR_OPPOSITE(c->din)) s_st.uturns++; else s_st.turns++;
}

/* ------------------------------------------------------------------ spacing */
static void lists_build(void)
{
    int i;
    for (i = 0; i < LC_MAX_CARS; i++) {
        LcCar *c = &s_cars[i];
        s_listed[i] = 0;
        if (!c->used) continue;
        s_listTile[i] = (u16)TILE_IDX(c->x, c->y);
        s_next[i] = s_head[s_listTile[i]];
        s_head[s_listTile[i]] = (u8)(i + 1);
        s_listed[i] = 1;
    }
}

static void lists_clear(void)
{
    int i;
    for (i = 0; i < LC_MAX_CARS; i++) if (s_listed[i]) { s_head[s_listTile[i]] = 0; s_listed[i] = 0; }
}

static s32 gap_ahead(int i)
{
    const LcCar *a = &s_cars[i];
    s32 best = BIG_GAP, d;
    int k, nx = a->x + g_dir_dx[a->dout], ny = a->y + g_dir_dy[a->dout];
    /* two motorway lanes are separate only while the road ahead is a motorway too (else they merge) */
    int mway = TILE_ROAD(WT(a->x, a->y)) == ROAD_MOTORWAY &&
               (!world_in(nx, ny) || TILE_ROAD(WT(nx, ny)) == ROAD_MOTORWAY);
    for (k = s_head[TILE_IDX(a->x, a->y)]; k; k = s_next[k - 1]) {
        int j = k - 1;
        const LcCar *b = &s_cars[j];
        if (j == i || !b->used || b->x != a->x || b->y != a->y) continue;
        if (mway && b->lane != a->lane) continue;
        if (b->din == a->din) {
            if (b->s > a->s || (b->s == a->s && j > i)) d = b->s - a->s; else continue;
        } else if (b->dout == a->dout) {
            s32 ra = a->len - a->s, rb = b->len - b->s;
            if (rb < ra || (rb == ra && j > i)) d = ra - rb; else continue;
        } else continue;
        if (d < best) best = d;
    }
    if (world_in(nx, ny)) {
        int junction = s_bits[TILE_ROAD_MASK(WT(nx, ny))] >= 3;
        for (k = s_head[TILE_IDX(nx, ny)]; k; k = s_next[k - 1]) {
            const LcCar *b = &s_cars[k - 1];
            if (!b->used || b->x != nx || b->y != ny) continue;
            /* junction: wait at the edge while crossing traffic is inside (not when it is nearly out) */
            if (junction && ((b->din ^ a->dout) & 1) && b->s < b->len - (64 << 8)) {
                d = (a->len - a->s) + GAP_Q;
                if (d < best) best = d;
                continue;
            }
            if (b->din != a->dout) continue;
            if (mway && b->lane != a->lane && TILE_ROAD(WT(nx, ny)) == ROAD_MOTORWAY) continue;
            d = (a->len - a->s) + b->s;
            if (d < best) best = d;
        }
    }
    return best;
}

s32 lc_debug_gap(int slot)
{
    s32 g;
    if (slot < 0 || slot >= LC_MAX_CARS || !s_cars[slot].used) return BIG_GAP;
    lists_build();
    g = gap_ahead(slot);
    lists_clear();
    return g;
}

/* would a car here overlap another (spawn check)? brute force over all cars */
static int spawn_conflict(const LcCar *c, int self)
{
    int j, nx = c->x + g_dir_dx[c->dout], ny = c->y + g_dir_dy[c->dout];
    int px = c->x - g_dir_dx[c->din], py = c->y - g_dir_dy[c->din];
    s32 rc = c->len - c->s;
    for (j = 0; j < LC_MAX_CARS; j++) {
        const LcCar *b = &s_cars[j];
        s32 rb;
        if (j == self || !b->used) continue;
        rb = b->len - b->s;
        if (IABS(b->fx - c->fx) + IABS(b->fy - c->fy) < 48) return 1;
        if (b->x == c->x && b->y == c->y) {
            if (b->din == c->din && IABS(b->s - c->s) < GAP_Q) return 1;
            if (b->dout == c->dout && IABS(rb - rc) < GAP_Q) return 1;
        } else if (b->x == nx && b->y == ny) {
            if (b->din == c->dout && rc + b->s < GAP_Q) return 1;
        } else if (b->x == px && b->y == py) {
            if (b->dout == c->din && rb + c->s < GAP_Q) return 1;
        }
    }
    return 0;
}

static int spawn_at(int x, int y, int din, int dout, s32 s, int col)
{
    int i, fdir;
    LcCar *c;
    if (!lc_car_road(x, y)) return 0;
    for (i = 0; i < LC_MAX_CARS && s_cars[i].used; i++) {}
    if (i == LC_MAX_CARS) return 0;
    c = &s_cars[i];
    memset(c, 0, sizeof(*c));
    c->x = (u8)x; c->y = (u8)y; c->din = (u8)din; c->dout = (u8)dout;
    c->col = (u8)(col % LC_COLOURS);
    c->lane = (u8)(rng_next(&s_rng) & 1);
    c->spd_pct = (u8)rng_range(&s_rng, 88, 112);
    c->lat = c->lat0 = (s16)lc_lane_offset(TILE_ROAD(WT(x, y)), c->lane);
    c->len = path_len(c);
    c->s = s < 0 ? 0 : s >= c->len ? c->len - 1 : s;
    c->life_ms = rng_range(&s_rng, 30000, 90000);
    c->v = (s_baseSpeed[TILE_ROAD(WT(x, y))] << 8) / 2;
    lc_car_point(c, &c->fx, &c->fy, &fdir);
    c->dir = (u8)fdir;
    if (spawn_conflict(c, i)) return 0;
    c->used = 1;
    s_count++;
    s_st.spawned++;
    return i + 1;
}

int lc_debug_spawn(int x, int y, int din, int dout, s32 s, int col)
{
    return spawn_at(x, y, din, dout, s, col);
}

void lc_debug_force_flow(int flow) { s_forceFlow = flow; }
int  lc_debug_choose(int x, int y, int heading) { return choose_exit(x, y, heading); }

static void despawn(int i, u32 *reason)
{
    if (!s_cars[i].used) return;
    s_cars[i].used = 0;
    s_count--;
    s_st.despawned++;
    (*reason)++;
}

/* random spawn in the area; ring_only: only outside the visible rect */
static int try_spawn(const LcView *v, int ring_only)
{
    int ex0 = v->vx0 - v->margin, ey0 = v->vy0 - v->margin, ex1 = v->vx1 + v->margin, ey1 = v->vy1 + v->margin;
    int wx, wy, x, y, m, f, e, n = 0, cand[DIR_COUNT], w[DIR_COUNT], total = 0, r, dout, din, k;
    s32 s;
    LcCar tmp;
    if (ex1 <= ex0 || ey1 <= ey0) return 0;
    wx = ex0 + rng_below(&s_rng, ex1 - ex0);
    wy = ey0 + rng_below(&s_rng, ey1 - ey0);
    if (ring_only && wx >= v->vx0 && wx < v->vx1 && wy >= v->vy0 && wy < v->vy1) return 0;
    if (!iso_world_to_tile(v->zoom, wx, wy, &x, &y) || !lc_car_road(x, y)) return 0;
    m = exit_mask(x, y);
    if (!m) return 0;
    f = lc_flow(x, y);
    if (rng_below(&s_rng, 256 + FLOW_FLOOR) >= f + FLOW_FLOOR) return 0;
    for (e = 0; e < DIR_COUNT; e++) if (m & (1 << e)) { w[n] = exit_weight(x, y, e); cand[n++] = e; total += w[n - 1]; }
    r = rng_below(&s_rng, total);
    for (k = 0; k < n - 1 && r >= w[k]; k++) r -= w[k];
    dout = cand[k];
    /* entry: from another connected side (din = opposite of that side); a dead end starts mid-tile */
    n = 0;
    for (e = 0; e < DIR_COUNT; e++) if ((m & (1 << e)) && e != dout) cand[n++] = DIR_OPPOSITE(e);
    memset(&tmp, 0, sizeof(tmp));
    if (n) { din = cand[rng_below(&s_rng, n)]; s = 0; }
    else { din = dout; s = 128 << 8; }
    tmp.din = (u8)din; tmp.dout = (u8)dout;
    tmp.lat = (s16)lc_lane_offset(TILE_ROAD(WT(x, y)), 0);
    s += rng_below(&s_rng, (int)((path_len(&tmp) - s) >> 8)) << 8;
    return spawn_at(x, y, din, dout, s, rng_below(&s_rng, LC_COLOURS)) != 0;
}

static int lot_adjacent(int x, int y)
{
    int d;
    for (d = 0; d < DIR_COUNT; d++) {
        int nx = x + g_dir_dx[d], ny = y + g_dir_dy[d];
        if (world_in(nx, ny) && WT(nx, ny)->lot) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ API */
void lc_init(void)
{
    int i;
    for (i = 0; i <= 256; i++) s_sin[i] = (s16)floor(sin(i * 3.14159265358979 / 512.0) * 16384.0 + 0.5);
    s_inited = 1;
    lc_reset(1);
}

void lc_reset(u32 seed)
{
    if (!s_inited) { lc_init(); }
    memset(s_cars, 0, sizeof(s_cars));
    memset(s_head, 0, sizeof(s_head));
    memset(s_listed, 0, sizeof(s_listed));
    memset(&s_st, 0, sizeof(s_st));
    s_count = 0; s_norder = 0; s_scanRow = 0;
    s_ema = 0; s_emaZoom = -1;
    rng_seed(&s_rng, seed ? seed : 0x11fe);
}

int lc_count(void) { return s_count; }
const LcCar *lc_cars(void) { return s_cars; }
int lc_draw_order(const u8 **order) { *order = s_order; return s_norder; }
void lc_stats(LcStats *out) { *out = s_st; out->alive = s_count; }

static void scan_traffic(void)
{
    int k, x, y;
    if (s_st.traffic_live || g_world.h <= 0) return;
    for (k = 0; k < SCAN_ROWS; k++) {
        y = s_scanRow;
        s_scanRow = (s_scanRow + 1) % g_world.h;
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = WT(x, y);
            if (!TILE_ROAD(t) || TER_IS_WATER(TER_TYPE(t))) continue;
            if (traffic_flow(x, y) > 0) { s_st.traffic_live = 1; return; }
        }
    }
}

static int sample_density(const LcView *v)
{
    int wx = v->vx0 + rng_below(&s_rng, IMAX(1, v->vx1 - v->vx0));
    int wy = v->vy0 + rng_below(&s_rng, IMAX(1, v->vy1 - v->vy0)), x, y;
    if (!iso_world_to_tile(v->zoom, wx, wy, &x, &y) || !lc_car_road(x, y) || !exit_mask(x, y)) return 0;
    return lc_flow(x, y) + FLOW_FLOOR;
}

static void update_target(const LcView *v, const LcParams *p)
{
    int k, own = lc_own_pct(p->year), tw = TILE_W(v->zoom), th = TILE_H(v->zoom);
    double tiles, t;
    s32 cap;
    k = ICLAMP(12 * own / 50, 3, 48);                 /* F3: car-ownership floor (BALANCE.md T11 note) */
    if (k != s_floor) { s_floor = k; s_emaZoom = -1; }   /* re-prime the mean with the new floor */
    if (s_emaZoom != v->zoom) {                       /* (re)prime: plain mean of 256 samples */
        s32 sum = 0;
        for (k = 0; k < 256; k++) sum += sample_density(v);
        s_ema = (sum << 16) / 256;
        s_emaZoom = v->zoom;
    }
    for (k = 0; k < 12; k++) s_ema += ((sample_density(v) << 16) - s_ema) >> 7;
    tiles = (double)(v->vx1 - v->vx0 + 2 * v->margin) * (v->vy1 - v->vy0 + 2 * v->margin) / (tw * th / 2);
    /* cars per road tile (both directions): estimate/forced flow 255 = 1.6 x car ownership of the year;
       traffic.cpp's level already includes the era's car share and is 128 at the class capacity:
       1.2 cars per tile at capacity */
    if (s_st.traffic_live && s_forceFlow < 0) t = s_ema / 65536.0 / 128.0 * tiles * 1.2;
    else t = s_ema / 65536.0 / 255.0 * tiles * 1.6 * own / 100.0;
    cap = 6 + (s32)((double)p->population * own / 1000.0);
    if (t > cap) t = cap;
    if (t > LC_CAR_CAP) t = LC_CAR_CAP;
    s_st.target = (int)t;
}

void lc_update(int dt_ms, const LcView *v, const LcParams *p)
{
    int i, j, z = v->zoom, excess, spawns, tries;
    int ex0 = v->vx0 - v->margin - 32, ey0 = v->vy0 - v->margin - 32;
    int ex1 = v->vx1 + v->margin + 32, ey1 = v->vy1 + v->margin + 32;
    if (!s_inited) lc_init();
    if (dt_ms < 0) dt_ms = 0;
    if (dt_ms > 100) dt_ms = 100;
    if (p->frozen) dt_ms = 0;
    scan_traffic();
    update_target(v, p);
    lists_build();
    s_st.blocked = 0; s_st.ghosts = 0; s_st.visible = 0;
    excess = s_count - (s_st.target + s_st.target / 8 + 2);
    for (i = 0; i < LC_MAX_CARS; i++) {
        LcCar *c = &s_cars[i];
        int fdir, wx, wy, vis, gone = 0;
        if (!c->used) continue;
        if (!lc_car_road(c->x, c->y)) { despawn(i, &s_st.gone_road); continue; }
        if (dt_ms) {
            s32 vt, gap, room = BIG_GAP, step;
            int cls = TILE_ROAD(WT(c->x, c->y)), pct = 100;
            c->age_ms += dt_ms;
            gap = c->ghost_ms ? BIG_GAP : gap_ahead(i);
            if (c->dout != c->din) pct = c->dout == DIR_OPPOSITE(c->din) ? 45 : c->dout == RIGHT_OF(c->din) ? 60 : 75;
            vt = (s_baseSpeed[cls] << 8) / 100 * c->spd_pct / 100 * pct;
            if (gap < BIG_GAP) {
                room = gap - GAP_Q;
                if (room < 0) room = 0;
                if (room * 4 < vt) vt = room * 4;
            }
            if (c->v < vt) { c->v += ACCEL_Q / 1000 * dt_ms; if (c->v > vt) c->v = vt; }
            else c->v = vt;
            step = c->v * dt_ms / 1000;
            if (step > room) step = room;
            if (step <= 0 && vt == 0) {
                s_st.blocked++;
                c->stuck_ms = (u16)IMIN(c->stuck_ms + dt_ms, 60000);
                if (c->stuck_ms > STUCK_MS) { c->ghost_ms = GHOST_MS; c->stuck_ms = 0; c->ghosted = 1; }
            } else c->stuck_ms = 0;
            if (c->ghost_ms) { s_st.ghosts++; c->ghost_ms = (u16)IMAX(0, c->ghost_ms - dt_ms); }
            c->s += step;
            while (c->s >= c->len) {
                int nx = c->x + g_dir_dx[c->dout], ny = c->y + g_dir_dy[c->dout];
                c->s -= c->len;
                if (!world_in(nx, ny)) { despawn(i, &s_st.gone_edge); gone = 1; break; }
                if (!lc_car_road(nx, ny)) { enter_tile(c, DIR_OPPOSITE(c->dout)); continue; }   /* road ahead gone */
                c->x = (u8)nx; c->y = (u8)ny;
                s_st.tiles++;
                if (c->age_ms > 2 * c->life_ms && (c->age_ms > 3 * c->life_ms || lot_adjacent(nx, ny))) {
                    despawn(i, &s_st.gone_life); gone = 1; break;                 /* "parks" */
                }
                enter_tile(c, c->dout);
            }
            if (gone) continue;
        }
        lc_car_point(c, &c->fx, &c->fy, &fdir);
        c->dir = (u8)fdir;
        iso_pos_to_world(z, c->fx, c->fy, &wx, &wy);
        if (wx < ex0 || wx >= ex1 || wy < ey0 || wy >= ey1) { despawn(i, &s_st.gone_offview); continue; }
        vis = wx >= v->vx0 - 24 && wx < v->vx1 + 24 && wy >= v->vy0 - 16 && wy < v->vy1 + 24;
        if (!vis && c->age_ms > c->life_ms) { despawn(i, &s_st.gone_life); continue; }
        if (!vis && excess > 0) { despawn(i, &s_st.gone_excess); excess--; continue; }
        if (vis) s_st.visible++;
    }
    lists_clear();
    /* spawn: fill the whole area while under half the target, then only the ring outside the view */
    spawns = s_count < s_st.target / 2 ? 6 : 2;
    for (tries = 0; tries < 24 && spawns > 0 && s_count < s_st.target; tries++)
        if (try_spawn(v, s_count >= s_st.target / 2)) spawns--;
    /* draw order: keep last frame's order (nearly sorted), append new cars, insertion sort by depth */
    {
        static u8 inOrder[LC_MAX_CARS];
        static int key[LC_MAX_CARS];
        int n = 0;
        for (i = 0; i < s_norder; i++) if (s_cars[s_order[i]].used) s_order[n++] = s_order[i];
        memset(inOrder, 0, sizeof(inOrder));
        for (i = 0; i < n; i++) inOrder[s_order[i]] = 1;
        for (i = 0; i < LC_MAX_CARS; i++) if (s_cars[i].used && !inOrder[i]) s_order[n++] = (u8)i;
        for (i = 0; i < LC_MAX_CARS; i++)
            if (s_cars[i].used) key[i] = (int)(FLOORDIV(s_cars[i].fx, 256) + FLOORDIV(s_cars[i].fy, 256));
        for (i = 1; i < n; i++) {
            u8 t = s_order[i];
            for (j = i - 1; j >= 0 && key[s_order[j]] > key[t]; j--) s_order[j + 1] = s_order[j];
            s_order[j + 1] = t;
        }
        s_norder = n;
    }
    s_st.alive = s_count;
}
