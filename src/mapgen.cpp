/* mapgen.cpp - procedural terrain (T06). PORTABLE, integer-only, deterministic from the seed.
   See mapgen.h for the pipeline, what is written per tile and the shore autotile rule.

   Memory: static scratch arrays sized for the 256x256 maximum (~6 MB BSS, most of it the router's
   state arrays). Nothing is allocated. */
#include "common.h"
#include "mapgen.h"
#include "world.h"
#include "defs.h"
#include "util.h"

#define NT          (MAP_MAX_W * MAP_MAX_H)
#define RT_NB       4096            /* Dial bucket ring: > the largest single move cost */
#define RT_MAXMOVE  (RT_NB - 1)
#define RAPIDS_LEN  4

static MapGenReport s_rep;
static const char *const s_names[MAP_PRESET_COUNT] = { "Lakeland", "River town", "Coast", "Forest" };
static const char *const s_keys[MAP_PRESET_COUNT] = { "lakeland", "river", "coast", "forest" };

/* ------------------------------------------------------------------ presets */
typedef struct {
    int water_pm, water_var;   /* lake (and sea) water target per mille of the map, +- seed variation */
    int lx, ly;                /* log2 of the largest lake-noise lattice step along x / y (x = NW-SE) */
    int forest_pm;             /* forest per mille of the land */
    int rock_pm;               /* granite outcrops per mille of the land */
    int farms;                 /* farm clusters along the national road */
    int field_pm;              /* field cap per mille of the land */
    int river_pm;              /* chance of a river when MapGenParams.river = -1 (per mille) */
    int min_lake;              /* smaller water bodies are filled (before cleanup) */
    int pers;                  /* lake noise persistence (higher = more bays and islands) */
    int island_pm;             /* share of open water (>= 3 tiles from shore) raised into islands */
} Preset;

static const Preset s_presets[MAP_PRESET_COUNT] = {
    /* water  var  lx ly forest rock farms field river minlake pers isl */
    {  230,   40,  5, 4,  380,   30,  6,   80,   300,  10,   65,   70 },   /* Lakeland */
    {   80,   30,  4, 4,  340,   25,  8,  110,  1000,  10,   55,   40 },   /* River town */
    {  290,   30,  5, 4,  330,   40,  5,   70,   400,  10,   65,   60 },   /* Coast */
    {  100,   30,  4, 4,  600,   40,  4,   45,   400,  10,   55,   40 },   /* Forest */
};

/* ------------------------------------------------------------------ scratch */
static int s_w, s_h;
static u32 s_seed;
static Rng s_rng;
static u16 s_nwater[NT], s_nheight[NT], s_nmeander[NT], s_nforest[NT], s_ndry[NT], s_nmoist[NT],
           s_nrock[NT], s_npatch[NT];
static s32 s_acc[NT];
static u8  s_lat[(MAP_MAX_W / 2 + 4) * (MAP_MAX_H / 2 + 4)];
static u8  s_mask[NT], s_tmp[NT];
static u8  s_dw[NT];              /* land: distance to water (1 = adjacent); water: distance to land */
static u8  s_mark[NT];            /* MK_* */
static s32 s_lab[NT];             /* component labels */
static s32 s_csize[NT + 1];       /* component sizes (label index) */
static u8  s_cedge[NT + 1];       /* component touches the map edge */
static s32 s_queue[NT];
static s32 s_path[NT];            /* route result: tile indices, source -> target */
static u8  s_pdir[NT];            /* arrival direction at each path tile */
static int s_plen;
static s32 s_rpath[NT];           /* the river path (kept for rapids) */
static u8  s_rpdir[NT];
static int s_rplen;
static s32 s_npath[NT];           /* the national road path (kept for fields/town) */
static u8  s_npdir[NT];
static int s_nplen;

enum { MK_RAPIDS = 0x01,          /* within 3 tiles of the rapids (hydro site: keep roads away) */
       MK_ROAD   = 0x02,          /* national road tile */
       MK_ROADADJ = 0x04,         /* 4-neighbour of the national road */
       MK_ROADNEAR = 0x08,        /* within 3 tiles of the national road */
       MK_TOWN   = 0x10,          /* town site (cleared, no fields) */
       MK_ISLAND = 0x20,          /* small island (pine / rock) */
       MK_RAIL   = 0x40 };

/* ------------------------------------------------------------------ small helpers */
/* world_in() lives in world.cpp and is not inlined there: this local copy is the generator's hot path */
static int in_map(int x, int y) { return (unsigned)x < (unsigned)g_world.w && (unsigned)y < (unsigned)g_world.h; }
static int is_w(int x, int y) { return in_map(x, y) && TER_IS_WATER(TER_TYPE(WT(x, y))); }
static int is_l(int x, int y) { return in_map(x, y) && !TER_IS_WATER(TER_TYPE(WT(x, y))); }
static int ter_at(int i) { return g_world.t[i].terrain & 15; }
static void set_ter(int x, int y, int ter, int var)
{
    Tile *t = WT(x, y);
    t->terrain = (u8)((ter & 15) | ((var & 15) << 4));
}

static int edge_dist(int e, int x, int y)
{
    switch (e) {
    case DIR_NE: return y;
    case DIR_SE: return s_w - 1 - x;
    case DIR_SW: return s_h - 1 - y;
    default:     return x;
    }
}
static int edge_pos(int e, int x, int y) { return (e == DIR_NE || e == DIR_SW) ? x : y; }
static int edge_len(int e) { return (e == DIR_NE || e == DIR_SW) ? s_w : s_h; }
static void edge_tile(int e, int pos, int *x, int *y)
{
    switch (e) {
    case DIR_NE: *x = pos; *y = 0; break;
    case DIR_SE: *x = s_w - 1; *y = pos; break;
    case DIR_SW: *x = pos; *y = s_h - 1; break;
    default:     *x = 0; *y = pos; break;
    }
}
static int min_edge_dist(int x, int y)
{
    return IMIN(IMIN(x, y), IMIN(s_w - 1 - x, s_h - 1 - y));
}
/* 1 if moving in direction dir at (x,y) does not run parallel to a map edge closer than margin */
static int edge_ok(int x, int y, int dir, int margin)
{
    if (dir & 1) return y >= margin && y < s_h - margin;    /* moving along x: parallel to the y edges */
    return x >= margin && x < s_w - margin;
}

/* ------------------------------------------------------------------ value noise (integer)
   Sum of octaves of bilinear (smoothstep) value noise on hashed lattices. Octave o has the lattice
   step 2^(lx-o) along x and 2^(ly-o) along y (>= 2); octave amplitudes fall by pers percent.
   Output 0..65280. */
static void noise_fill(u16 *out, u32 salt, int lx, int ly, int oct, int pers)
{
    static int wx[MAP_MAX_W], gx[MAP_MAX_W], wy[MAP_MAX_H], gy[MAP_MAX_H];
    int o, x, y, tot = 0, amp = 64;
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) s_acc[TILE_IDX(x, y)] = 0;
    for (o = 0; o < oct; o++) {
        int sx = IMAX(lx - o, 1), sy = IMAX(ly - o, 1);
        int stx = 1 << sx, sty = 1 << sy;
        int gw = (s_w >> sx) + 3, gh = (s_h >> sy) + 3;
        int i, j;
        u32 s = hash_u32(s_seed * 0x9E3779B9u + salt * 7919u + (u32)o * 104729u);
        int ox = (int)(hash_u32(s ^ 0x51ed) & (stx - 1)), oy = (int)(hash_u32(s ^ 0xa3c5) & (sty - 1));
        /* random lattice offset per octave: octaves don't share lattice lines (no straight valleys) */
        for (j = 0; j < gh; j++)
            for (i = 0; i < gw; i++) s_lat[j * gw + i] = (u8)(hash_xy(i, j, s) & 255);
        for (x = 0; x < s_w; x++) {
            int f = (((x + ox) & (stx - 1)) << 8) >> sx;          /* 0..255 */
            wx[x] = (f * f * (768 - 2 * f)) >> 16;                /* smoothstep, 0..255 */
            gx[x] = (x + ox) >> sx;
        }
        for (y = 0; y < s_h; y++) {
            int f = (((y + oy) & (sty - 1)) << 8) >> sy;
            wy[y] = (f * f * (768 - 2 * f)) >> 16;
            gy[y] = (y + oy) >> sy;
        }
        for (y = 0; y < s_h; y++) {
            const u8 *r0 = &s_lat[gy[y] * gw], *r1 = r0 + gw;
            int wyy = wy[y];
            s32 *acc = &s_acc[TILE_IDX(0, y)];
            for (x = 0; x < s_w; x++) {
                int g = gx[x], w = wx[x];
                int top = (r0[g] << 8) + (r0[g + 1] - r0[g]) * w;
                int bot = (r1[g] << 8) + (r1[g + 1] - r1[g]) * w;
                acc[x] += (((top << 8) + (bot - top) * wyy) >> 8) * amp;
            }
        }
        tot += amp;
        amp = IMAX(1, amp * pers / 100);
    }
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) out[TILE_IDX(x, y)] = (u16)(s_acc[TILE_IDX(x, y)] / tot);
}

/* Threshold v such that about pm per mille of the selected tiles have f >= v (sel NULL = all). */
static int percentile(const u16 *f, const u8 *sel, u8 selval, int pm)
{
    static int hist[1024];
    int x, y, n = 0, want, acc = 0, b;
    memset(hist, 0, sizeof(hist));
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int i = TILE_IDX(x, y);
            if (sel && sel[i] != selval) continue;
            hist[f[i] >> 6]++; n++;
        }
    want = (int)(((s64)n * pm + 500) / 1000);
    if (want <= 0) return 0x10000;
    for (b = 1023; b > 0; b--) {
        acc += hist[b];
        if (acc >= want) break;
    }
    return b << 6;
}

/* ------------------------------------------------------------------ components and distances */
/* Labels 4-connected components of water (water=1) or land (water=0) tiles into s_lab (others -1).
   Fills s_csize / s_cedge per label; returns the number of components. */
static int label_comps(int water)
{
    int x, y, n = 0;
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) s_lab[TILE_IDX(x, y)] = -1;
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int i = TILE_IDX(x, y), qh = 0, qt = 0, size = 0, edge = 0;
            if (s_lab[i] >= 0 || (is_w(x, y) != water)) continue;
            s_lab[i] = n; s_queue[qt++] = i;
            while (qh < qt) {
                int c = s_queue[qh++], cx = c & (MAP_STRIDE - 1), cy = c >> MAP_SHIFT, d;
                size++;
                if (cx == 0 || cy == 0 || cx == s_w - 1 || cy == s_h - 1) edge = 1;
                for (d = 0; d < DIR_COUNT; d++) {
                    int nx = cx + g_dir_dx[d], ny = cy + g_dir_dy[d], ni;
                    if (!in_map(nx, ny)) continue;
                    ni = TILE_IDX(nx, ny);
                    if (s_lab[ni] >= 0 || is_w(nx, ny) != water) continue;
                    s_lab[ni] = n; s_queue[qt++] = ni;
                }
            }
            s_csize[n] = size; s_cedge[n] = (u8)edge;
            n++;
        }
    return n;
}

/* s_dw: land tiles = 4-connected distance to the nearest water (1 = adjacent), water tiles = distance
   to the nearest land; capped at cap. */
static void distance_fields(int cap)
{
    int x, y, qh = 0, qt = 0, pass;
    for (pass = 0; pass < 2; pass++) {          /* 0: from water into land, 1: from land into water */
        qh = qt = 0;
        for (y = 0; y < s_h; y++)
            for (x = 0; x < s_w; x++) {
                int i = TILE_IDX(x, y), w = is_w(x, y);
                if (w == (pass == 0)) { s_tmp[i] = 0; s_queue[qt++] = i; }
                else s_tmp[i] = 255;
            }
        while (qh < qt) {
            int c = s_queue[qh++], cx = c & (MAP_STRIDE - 1), cy = c >> MAP_SHIFT, d;
            for (d = 0; d < DIR_COUNT; d++) {
                int nx = cx + g_dir_dx[d], ny = cy + g_dir_dy[d], ni;
                if (!in_map(nx, ny)) continue;
                ni = TILE_IDX(nx, ny);
                if (s_tmp[ni] != 255) continue;
                s_tmp[ni] = (u8)IMIN(s_tmp[c] + 1, cap);
                s_queue[qt++] = ni;
            }
        }
        for (y = 0; y < s_h; y++)
            for (x = 0; x < s_w; x++) {
                int i = TILE_IDX(x, y);
                if (is_w(x, y) == (pass == 1)) s_dw[i] = (u8)(s_tmp[i] == 255 ? cap : s_tmp[i]);
            }
    }
}

/* ------------------------------------------------------------------ shore rule (mapgen.h) */
static int in_block(int x, int y, int water)
{
    int ox, oy;
    for (oy = -1; oy <= 0; oy++)
        for (ox = -1; ox <= 0; ox++) {
            int x0 = x + ox, y0 = y + oy;
            if (x0 < 0 || y0 < 0 || x0 + 1 >= g_world.w || y0 + 1 >= g_world.h) continue;
            if (water) {
                if (is_w(x0, y0) && is_w(x0 + 1, y0) && is_w(x0, y0 + 1) && is_w(x0 + 1, y0 + 1)) return 1;
            } else {
                if (is_l(x0, y0) && is_l(x0 + 1, y0) && is_l(x0, y0 + 1) && is_l(x0 + 1, y0 + 1)) return 1;
            }
        }
    return 0;
}

/* window (x0..x0+1, y0..y0+1): 1 = water on the main diagonal only, 2 = on the other only, 0 = no */
static int checker(int x0, int y0)          /* caller guarantees x0 + 1 < w, y0 + 1 < h */
{
    const Tile *t = &g_world.t[TILE_IDX(x0, y0)];
    int a = TER_IS_WATER(t[0].terrain & 15), b = TER_IS_WATER(t[1].terrain & 15);
    int c = TER_IS_WATER(t[MAP_STRIDE].terrain & 15), d = TER_IS_WATER(t[MAP_STRIDE + 1].terrain & 15);
    if (a && d && !b && !c) return 1;
    if (b && c && !a && !d) return 2;
    return 0;
}

int mapgen_shore_mask(int x, int y)
{
    static const s8 vx[4] = { 0, 1, 0, 1 }, vy[4] = { 0, 0, 1, 1 };   /* n e w s vertices */
    int c, m = 0;
    if (!in_map(x, y) || is_w(x, y)) return 0;
    for (c = 0; c < 4; c++) {
        int px = x + vx[c], py = y + vy[c];            /* vertex (px,py): tiles px-1..px, py-1..py */
        if (!is_w(px - 1, py - 1) && !is_w(px, py - 1) && !is_w(px - 1, py) && !is_w(px, py)) m |= 1 << c;
    }
    return m;
}

int mapgen_shore_errors(void)
{
    int x, y, n = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            int ter = TER_TYPE(WT(x, y));
            if (!TER_IS_WATER(ter)) { if (!in_block(x, y, 0)) n++; }
            else if (ter == TER_WATER && !in_block(x, y, 1)) n++;
            if (x + 1 < g_world.w && y + 1 < g_world.h && checker(x, y)) n++;
        }
    return n;
}

static int river_dir_near(int x, int y)
{
    int d;
    for (d = 0; d < DIR_COUNT; d++) {
        int nx = x + g_dir_dx[d], ny = y + g_dir_dy[d];
        if (in_map(nx, ny) && TER_TYPE(WT(nx, ny)) == TER_RIVER) return TER_VAR(WT(nx, ny)) & 3;
    }
    return -1;
}

/* Grow land: turn the lake tiles of the 2x2 block around (x,y) with the fewest water tiles into land
   (blocks with river tiles are skipped). Returns 0 if every block contains river. */
static int grow_land_block(int x, int y)
{
    int ox, oy, best = 5, bx = 0, by = 0, i, j;
    for (oy = -1; oy <= 0; oy++)
        for (ox = -1; ox <= 0; ox++) {
            int x0 = x + ox, y0 = y + oy, n = 0, river = 0;
            if (x0 < 0 || y0 < 0 || x0 + 1 >= s_w || y0 + 1 >= s_h) continue;
            for (j = 0; j < 2; j++)
                for (i = 0; i < 2; i++) {
                    int ter = TER_TYPE(WT(x0 + i, y0 + j));
                    if (ter == TER_RIVER) river = 1;
                    if (ter == TER_WATER) n++;
                }
            if (!river && n < best) { best = n; bx = x0; by = y0; }
        }
    if (best == 5) return 0;
    for (j = 0; j < 2; j++)
        for (i = 0; i < 2; i++)
            if (TER_TYPE(WT(bx + i, by + j)) == TER_WATER) set_ter(bx + i, by + j, TER_GRASS, 0);
    return 1;
}

/* Make every shore drawable (rules in mapgen.h). First passes: lake specks become land, thin land
   becomes water (river if it touches the river), checkerboards are filled with land (or river where
   a river is involved, so rivers stay connected). Thin diagonal land bridges can flip back and forth
   between land and water, so from pass 6 on thin land GROWS into the best 2x2 block instead (land
   only increases: converges). Returns the number of fixes. */
static u8 s_win[NT];             /* window (x..x+1, y..y+1): 1 = all water, 2 = all land, 0 = mixed/outside */
static void compute_windows(void)
{
    int x, y;
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) s_mask[TILE_IDX(x, y)] = (u8)TER_IS_WATER(ter_at(TILE_IDX(x, y)));
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int i = TILE_IDX(x, y), m;
            if (x + 1 >= s_w || y + 1 >= s_h) { s_win[i] = 0; continue; }
            m = s_mask[i] + s_mask[i + 1] + s_mask[i + MAP_STRIDE] + s_mask[i + MAP_STRIDE + 1];
            s_win[i] = (u8)(m == 4 ? 1 : m == 0 ? 2 : 0);
        }
}
/* in_block() from the window cache (bit 1 water, 2 land) */
static int win_block(int x, int y, int bit)
{
    int i = TILE_IDX(x, y), w = s_win[i];
    if (x > 0) w |= s_win[i - 1];
    if (y > 0) { w |= s_win[i - MAP_STRIDE]; if (x > 0) w |= s_win[i - MAP_STRIDE - 1]; }
    return w & bit;
}

static int shore_fix(void)
{
    int it, x, y, ch = 0, total = 0;
    for (it = 0; it < 40; it++) {
        int grow = it >= 6;
        ch = 0;
        /* (window caches are safe here: a tile outside every same-kind window can change without
           breaking another tile's window) */
        compute_windows();
        for (y = 0; y < s_h; y++)
            for (x = 0; x < s_w; x++)
                if (TER_TYPE(WT(x, y)) == TER_WATER && !win_block(x, y, 1)) { set_ter(x, y, TER_GRASS, 0); ch++; }
        compute_windows();
        for (y = 0; y < s_h; y++)
            for (x = 0; x < s_w; x++)
                if (!s_mask[TILE_IDX(x, y)] && !win_block(x, y, 2) && !in_block(x, y, 0)) {
                    int rd = river_dir_near(x, y);
                    if (grow && grow_land_block(x, y)) { ch++; continue; }
                    if (rd >= 0) set_ter(x, y, TER_RIVER, rd);
                    else set_ter(x, y, TER_WATER, 0);
                    ch++;
                }
        for (y = 0; y + 1 < s_h; y++)
            for (x = 0; x + 1 < s_w; x++) {
                int k = checker(x, y), ax, ay, bx, by, lx0, ly0, lx1, ly1;
                if (!k) continue;
                if (k == 1) { ax = x; ay = y; bx = x + 1; by = y + 1; lx0 = x + 1; ly0 = y; lx1 = x; ly1 = y + 1; }
                else        { ax = x + 1; ay = y; bx = x; by = y + 1; lx0 = x; ly0 = y; lx1 = x + 1; ly1 = y + 1; }
                if (TER_TYPE(WT(ax, ay)) == TER_RIVER || TER_TYPE(WT(bx, by)) == TER_RIVER) {
                    int rd = river_dir_near(lx0, ly0);
                    set_ter(lx0, ly0, TER_RIVER, rd < 0 ? 0 : rd);
                    rd = river_dir_near(lx1, ly1);
                    set_ter(lx1, ly1, TER_RIVER, rd < 0 ? 0 : rd);
                } else {
                    set_ter(ax, ay, TER_GRASS, 0);
                    set_ter(bx, by, TER_GRASS, 0);
                }
                ch++;
            }
        total += ch;
        if (!ch) break;
    }
    if (ch) log_printf("mapgen: warning: shore cleanup did not converge (%d changes in the last pass)", ch);
    return total;
}

/* ------------------------------------------------------------------ least-cost router
   Dial's algorithm over states (tile, arrival direction) with a turn penalty, so routes are long
   straight runs with few corners. Road/rail routes may jump straight across water (bridges). */
enum { RT_RIVER, RT_ROAD, RT_RAIL };
typedef struct {
    int mode;
    int turn;                   /* extra cost of a 90-degree turn */
    int margin;                 /* no running parallel to a map edge closer than this */
    int br_lake, br_river;      /* longest bridge over lake water / river-only water (0 = none) */
    int br_tile, br_fixed;
    int target_edge;            /* goal: arrive on this edge going outward (-1 = none) */
    int tgt_lo, tgt_hi;         /* allowed position range on the target edge */
    int tgt_ex_lo, tgt_ex_hi;   /* excluded position range on the target edge (rail) */
    int tgt_pref;               /* preferred position: + |pos - pref| * 2 */
    int lake_min;               /* river: entering a lake of >= lake_min tiles ends the route (0 = off) */
    int src_edge;               /* river: the lake end must be >= 20 tiles from this edge */
    int tgt_tile;               /* goal: this tile (TILE_IDX), any direction (-1 = none) */
    int forbid;                 /* 1 = tiles with s_tmp[i] set are impassable (road second leg) */
} Route;
typedef struct { int x, y, dir, cost; } RtSrc;

static s32 s_rdist[NT * 4], s_rpred[NT * 4], s_rnext[NT * 4], s_rprev[NT * 4];
static s32 s_bhead[RT_NB];
static int s_qcount;

static void q_unlink(int s)
{
    int b = s_rdist[s] & (RT_NB - 1);
    if (s_rprev[s] >= 0) s_rnext[s_rprev[s]] = s_rnext[s]; else s_bhead[b] = s_rnext[s];
    if (s_rnext[s] >= 0) s_rprev[s_rnext[s]] = s_rprev[s];
    s_rnext[s] = -2;
    s_qcount--;
}

static void q_relax(int s, s32 d, int pred)
{
    int b;
    if (d >= s_rdist[s]) return;
    if (s_rnext[s] != -2) q_unlink(s);
    s_rdist[s] = d;
    s_rpred[s] = pred;
    b = d & (RT_NB - 1);
    s_rprev[s] = -1;
    s_rnext[s] = s_bhead[b];
    if (s_bhead[b] >= 0) s_rprev[s_bhead[b]] = s;
    s_bhead[b] = s;
    s_qcount++;
}

/* cost of entering (x,y) moving in direction dir; -1 = impassable */
static int rt_cost(const Route *r, int x, int y, int dir)
{
    int i = TILE_IDX(x, y), ter = ter_at(i), c;
    const Tile *t = &g_world.t[i];
    if (!edge_ok(x, y, dir, r->margin)) return -1;
    if (r->forbid && s_tmp[i]) return -1;
    if (r->mode == RT_RIVER) {
        if (TER_IS_WATER(ter)) c = 1;
        else c = 2 + (s_nheight[i] >> 12) + IMIN(IABS((int)s_nmeander[i] - 32640) >> 7, 120);
        /* follows the meandering mid-isoline of the meander noise, gently downhill */
    } else {
        if (TER_IS_WATER(ter) || TILE_ROAD(t)) return -1;
        if (r->mode == RT_RAIL && (s_mark[i] & MK_ROADADJ)) return -1;
        c = ter == TER_ROCK ? 45 : ter == TER_FOREST ? 13 : 10;
        c += s_npatch[i] >> 13;
        if (s_dw[i] == 1) c += 3;
        if (s_mark[i] & MK_RAPIDS) c += 100;
        if (r->mode == RT_RAIL) {
            if (s_mark[i] & MK_ROADNEAR) c += 30;
            if (s_mark[i] & MK_TOWN) c += 40;
        }
    }
    if (r->target_edge >= 0 && dir == r->target_edge && edge_dist(dir, x, y) == 0) {
        int pos = edge_pos(dir, x, y);
        if (pos < r->tgt_lo || pos > r->tgt_hi) return -1;
        if (pos >= r->tgt_ex_lo && pos <= r->tgt_ex_hi) return -1;
        c += IABS(pos - r->tgt_pref) * 2;
    }
    return c;
}

static int rt_target(const Route *r, int x, int y, int dir)
{
    int i = TILE_IDX(x, y);
    if (r->tgt_tile == i) return 1;
    if (r->target_edge >= 0 && dir == r->target_edge && edge_dist(dir, x, y) == 0) return 1;
    if (r->lake_min > 0 && ter_at(i) == TER_WATER && s_lab[i] >= 0 && s_csize[s_lab[i]] >= r->lake_min &&
        edge_dist(r->src_edge, x, y) >= 20)
        return 1;
    return 0;
}

/* Returns the goal state or -1; the path is read back with rt_path. */
static int rt_run(const Route *r, const RtSrc *src, int nsrc)
{
    int x, y, k, cur = 0x7fffffff;
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int s = TILE_IDX(x, y) * 4;
            for (k = 0; k < 4; k++) { s_rdist[s + k] = 0x7fffffff; s_rnext[s + k] = -2; s_rpred[s + k] = -1; }
        }
    for (k = 0; k < RT_NB; k++) s_bhead[k] = -1;
    s_qcount = 0;
    for (k = 0; k < nsrc; k++) {
        int c = rt_cost(r, src[k].x, src[k].y, src[k].dir);
        if (c < 0) continue;
        c += src[k].cost;
        q_relax(TILE_IDX(src[k].x, src[k].y) * 4 + src[k].dir, c, -1);
        cur = IMIN(cur, c);
    }
    while (s_qcount > 0) {
        int s, t, d, nd;
        while (s_bhead[cur & (RT_NB - 1)] < 0) cur++;
        s = s_bhead[cur & (RT_NB - 1)];
        q_unlink(s);
        t = s >> 2; d = s & 3; x = t & (MAP_STRIDE - 1); y = t >> MAP_SHIFT;
        if (rt_target(r, x, y, d)) return s;
        for (nd = 0; nd < DIR_COUNT; nd++) {
            int nx = x + g_dir_dx[nd], ny = y + g_dir_dy[nd], c, tp;
            if (nd == DIR_OPPOSITE(d) || !in_map(nx, ny)) continue;
            tp = nd != d ? r->turn : 0;
            c = rt_cost(r, nx, ny, nd);
            if (c >= 0) {
                q_relax(TILE_IDX(nx, ny) * 4 + nd, cur + IMIN(c + tp, RT_MAXMOVE), s);
            } else if (r->br_river > 0 && is_w(nx, ny)) {
                /* bridge: straight run of water tiles, then land */
                int len = 0, lake = 0, ok = 1, cx = nx, cy = ny;
                while (in_map(cx, cy) && is_w(cx, cy)) {
                    const Tile *wt = WT(cx, cy);
                    if ((TER_TYPE(wt) == TER_RIVER && (TER_VAR(wt) & RIVER_RAPIDS_BIT)) || TILE_ROAD(wt) ||
                        !edge_ok(cx, cy, nd, r->margin) || (r->forbid && s_tmp[TILE_IDX(cx, cy)])) { ok = 0; break; }
                    if (TER_TYPE(wt) == TER_WATER) lake = 1;
                    if (++len > r->br_river) { ok = 0; break; }
                    cx += g_dir_dx[nd]; cy += g_dir_dy[nd];
                }
                if (!ok || !in_map(cx, cy) || len > (lake ? r->br_lake : r->br_river)) continue;
                c = rt_cost(r, cx, cy, nd);
                if (c < 0) continue;
                q_relax(TILE_IDX(cx, cy) * 4 + nd, cur + IMIN(c + tp + len * r->br_tile + r->br_fixed, RT_MAXMOVE), s);
            }
        }
    }
    return -1;
}

/* path of goal state s into s_path/s_pdir (source first); bridge tiles are filled in */
static int rt_path(int s)
{
    int n = 0, i;
    while (s >= 0 && n < NT) {
        int t = s >> 2, d = s & 3, p = s_rpred[s];
        int x = t & (MAP_STRIDE - 1), y = t >> MAP_SHIFT;
        s_path[n] = t; s_pdir[n] = (u8)d; n++;
        if (p >= 0) {
            int pt = p >> 2;
            x -= g_dir_dx[d]; y -= g_dir_dy[d];
            while (TILE_IDX(x, y) != pt && n < NT) {
                s_path[n] = TILE_IDX(x, y); s_pdir[n] = (u8)d; n++;
                x -= g_dir_dx[d]; y -= g_dir_dy[d];
            }
        }
        s = p;
    }
    for (i = 0; i < n / 2; i++) {
        s32 tt = s_path[i]; u8 td = s_pdir[i];
        s_path[i] = s_path[n - 1 - i]; s_pdir[i] = s_pdir[n - 1 - i];
        s_path[n - 1 - i] = tt; s_pdir[n - 1 - i] = td;
    }
    s_plen = n;
    return n;
}

/* sources along edge e (inward dir), positions lo..hi minus ex_lo..ex_hi; cost |pos - pref| * 2
   (+ a preference for high ground when high_ground: river springs) */
static RtSrc s_src[MAP_MAX_W + MAP_MAX_H];
static int edge_sources(int e, int lo, int hi, int pref, int ex_lo, int ex_hi, int high_ground)
{
    int pos, n = 0;
    for (pos = lo; pos <= hi; pos++) {
        int x, y;
        if (pos >= ex_lo && pos <= ex_hi) continue;
        edge_tile(e, pos, &x, &y);
        s_src[n].x = x; s_src[n].y = y; s_src[n].dir = DIR_OPPOSITE(e);
        s_src[n].cost = IABS(pos - pref) * 2 + (high_ground ? (65535 - s_nheight[TILE_IDX(x, y)]) >> 10 : 0);
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ stages */
static int stage_lakes(const Preset *ps, int preset)   /* returns the sea edge (Coast) or -1 */
{
    int x, y, it, thr, n, wpm, sea = -1;
    noise_fill(s_nwater, 1, ps->lx, ps->ly, 4, ps->pers);
    wpm = ps->water_pm + rng_range(&s_rng, -ps->water_var, ps->water_var);
    if (preset == MAP_PRESET_COAST) {
        int span;
        sea = rng_below(&s_rng, 4);
        span = (sea == DIR_NE || sea == DIR_SW) ? s_h : s_w;      /* map depth across the coast */
        for (y = 0; y < s_h; y++)
            for (x = 0; x < s_w; x++) {
                int i = TILE_IDX(x, y), d = edge_dist(sea, x, y), r = span * 45 / 100;
                s32 g = d < r ? (s32)(r - d) * 65280 / r : 0;
                s32 v = ((s32)s_nwater[i] * 70 + g * 55) / 100;
                s_nwater[i] = (u16)ICLAMP(v, 0, 65535);
            }
    }
    thr = percentile(s_nwater, NULL, 0, wpm);
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) s_mask[TILE_IDX(x, y)] = (u8)(s_nwater[TILE_IDX(x, y)] >= thr);
    /* cellular automaton: majority of the 3x3 (out-of-map = land) */
    for (it = 0; it < 3; it++) {
        for (y = 0; y < s_h; y++)
            for (x = 0; x < s_w; x++) {
                int c = 0, dx, dy;
                for (dy = -1; dy <= 1; dy++)
                    for (dx = -1; dx <= 1; dx++)
                        if (in_map(x + dx, y + dy)) c += s_mask[TILE_IDX(x + dx, y + dy)];
                s_tmp[TILE_IDX(x, y)] = (u8)(c >= 5);
            }
        for (y = 0; y < s_h; y++)
            for (x = 0; x < s_w; x++) s_mask[TILE_IDX(x, y)] = s_tmp[TILE_IDX(x, y)];
    }
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++)
            set_ter(x, y, s_mask[TILE_IDX(x, y)] ? TER_WATER : TER_GRASS, 0);
    /* fill ponds, sink islets */
    n = label_comps(1);
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int i = TILE_IDX(x, y);
            if (s_lab[i] >= 0 && s_csize[s_lab[i]] < ps->min_lake) set_ter(x, y, TER_GRASS, 0);
        }
    n = label_comps(0);
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int i = TILE_IDX(x, y);
            if (s_lab[i] >= 0 && s_csize[s_lab[i]] < 4 && !s_cedge[s_lab[i]]) set_ter(x, y, TER_WATER, 0);
        }
    UNUSED(n);
    /* islands: high-frequency bumps in open water (>= 3 tiles from the shore) */
    if (ps->island_pm > 0) {
        distance_fields(63);
        for (y = 0; y < s_h; y++)
            for (x = 0; x < s_w; x++) {
                int i = TILE_IDX(x, y);
                s_tmp[i] = (u8)(is_w(x, y) && s_dw[i] >= 3);
            }
        noise_fill(s_nmeander, 9, 3, 3, 2, 50);           /* (meander noise is refilled later) */
        thr = percentile(s_nmeander, s_tmp, 1, ps->island_pm);
        for (y = 0; y < s_h; y++)
            for (x = 0; x < s_w; x++) {
                int i = TILE_IDX(x, y), dx, dy;
                if (!s_tmp[i] || s_nmeander[i] < thr) continue;
                for (dy = 0; dy <= 1; dy++)                  /* at least 2x2 */
                    for (dx = 0; dx <= 1; dx++)
                        if (in_map(x + dx, y + dy) && s_dw[TILE_IDX(x + dx, y + dy)] >= 2 &&
                            is_w(x + dx, y + dy)) set_ter(x + dx, y + dy, TER_GRASS, 0);
            }
    }
    s_rep.pinches_fixed += shore_fix();
    return sea;
}

/* River: least-cost path along the noise height, edge to edge (or to a big lake / the sea). */
static int stage_river(int preset, int sea)
{
    int attempt;
    noise_fill(s_nmeander, 3, 4, 4, 3, 60);
    for (attempt = 0; attempt < 4; attempt++) {
        Route r;
        int e, tgt, len, n, s, i, k, land = 0;
        memset(&r, 0, sizeof(r));
        r.tgt_tile = -1;
        if (sea >= 0) e = DIR_OPPOSITE(sea);
        else e = (rng_below(&s_rng, 4) + attempt) & 3;
        tgt = DIR_OPPOSITE(e);
        len = edge_len(e);
        label_comps(1);
        r.mode = RT_RIVER; r.turn = 3; r.margin = 3;
        r.target_edge = tgt; r.tgt_lo = edge_len(tgt) / 6; r.tgt_hi = edge_len(tgt) * 5 / 6;
        r.tgt_ex_lo = 1; r.tgt_ex_hi = 0; r.tgt_pref = r.tgt_lo + rng_below(&s_rng, r.tgt_hi - r.tgt_lo + 1);
        r.src_edge = e;
        r.lake_min = sea >= 0 ? 150 : preset == MAP_PRESET_RIVER ? 0 : 40;
        n = edge_sources(e, len / 5, len * 4 / 5, len / 4 + rng_below(&s_rng, len / 2), 1, 0, 1);
        s = rt_run(&r, s_src, n);
        if (s < 0) continue;
        rt_path(s);
        for (i = 0; i < s_plen; i++) if (ter_at(s_path[i]) != TER_WATER) land++;
        if (land < 16) continue;
        /* carve: 1 tile wide upstream, 2 wide from 40% of the length on */
        for (i = 0; i < s_plen; i++) {
            int t = s_path[i], x = t & (MAP_STRIDE - 1), y = t >> MAP_SHIFT;
            int fd = i + 1 < s_plen ? s_pdir[i + 1] : s_pdir[i];
            if (ter_at(t) == TER_WATER) continue;
            set_ter(x, y, TER_RIVER, fd);
            if (i * 10 >= s_plen * 4) {
                int wx = x + g_dir_dx[(s_pdir[i] + 1) & 3], wy = y + g_dir_dy[(s_pdir[i] + 1) & 3];
                if (is_l(wx, wy)) set_ter(wx, wy, TER_RIVER, fd);
            }
        }
        for (k = 0; k < s_plen; k++) { s_rpath[k] = s_path[k]; s_rpdir[k] = s_pdir[k]; }
        s_rplen = s_plen;
        return 1;
    }
    return 0;
}

static int hydro_site_near(int px, int py)
{
    int x0, y0;
    for (y0 = py - 2; y0 <= py + 1; y0++)
        for (x0 = px - 2; x0 <= px + 1; x0++) {
            int i, j, ok = 1, adj = 0;
            for (j = 0; j < 2 && ok; j++)
                for (i = 0; i < 2; i++) {
                    int x = x0 + i, y = y0 + j;
                    if (!is_l(x, y)) { ok = 0; break; }
                    if (IABS(x - px) + IABS(y - py) == 1) adj = 1;
                }
            if (ok && adj) return 1;
        }
    return 0;
}

/* rapids: the RAPIDS_LEN river-only path tiles with the largest height drop that have a hydro site */
static void stage_rapids(void)
{
    int i, k, best = -1, bests = -0x7fffffff;
    for (i = 6; i + RAPIDS_LEN + 6 <= s_rplen; i++) {
        int ok = 1, site = 0, score;
        for (k = 0; k < RAPIDS_LEN; k++) {
            int t = s_rpath[i + k], tx = t & (MAP_STRIDE - 1), ty = t >> MAP_SHIFT;
            if (ter_at(t) != TER_RIVER || min_edge_dist(tx, ty) < 8) { ok = 0; break; }
            if (s_rpdir[i + k] != s_rpdir[i]) { ok = 0; break; }     /* straight run: one flow direction */
            if (hydro_site_near(tx, ty)) site = 1;
        }
        if (!ok) continue;
        score = (int)s_nheight[s_rpath[i]] - (int)s_nheight[s_rpath[i + RAPIDS_LEN - 1]] + (site ? 100000 : 0);
        if (score > bests) { bests = score; best = i; }
    }
    if (best < 0) return;
    for (k = 0; k < RAPIDS_LEN; k++) {
        int t = s_rpath[best + k], tx = t & (MAP_STRIDE - 1), ty = t >> MAP_SHIFT, d, dx, dy;
        for (d = -1; d < DIR_COUNT; d++) {
            int nx = d < 0 ? tx : tx + g_dir_dx[d], ny = d < 0 ? ty : ty + g_dir_dy[d];
            if (d >= 0 && ((d ^ s_rpdir[best + k]) & 1) == 0) continue;   /* only the perpendicular twin */
            if (!in_map(nx, ny) || TER_TYPE(WT(nx, ny)) != TER_RIVER) continue;
            WT(nx, ny)->terrain |= (u8)(RIVER_RAPIDS_BIT << 4);
            for (dy = -3; dy <= 3; dy++)
                for (dx = -3; dx <= 3; dx++)
                    if (in_map(nx + dx, ny + dy)) s_mark[TILE_IDX(nx + dx, ny + dy)] |= MK_RAPIDS;
        }
    }
}

static void stage_cover(const Preset *ps)
{
    int x, y, thr, n, i;
    noise_fill(s_nforest, 4, 5, 5, 4, 55);
    noise_fill(s_ndry, 5, 5, 5, 3, 50);
    noise_fill(s_nrock, 6, 2, 2, 2, 50);
    /* land selector in s_tmp: 1 = land */
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) s_tmp[TILE_IDX(x, y)] = (u8)is_l(x, y);
    thr = percentile(s_nforest, s_tmp, 1, ps->forest_pm);
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            i = TILE_IDX(x, y);
            if (s_tmp[i] && s_nforest[i] >= thr) set_ter(x, y, TER_FOREST, FOREST_MIXED);
        }
    /* granite: high-frequency blobs, more where it is dry (pine ground) */
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            i = TILE_IDX(x, y);
            s_acc[i] = ((s32)s_nrock[i] * 2 + s_ndry[i]) / 3;
            s_nmoist[i] = (u16)s_acc[i];          /* temporary: rock score (moist noise is made later) */
        }
    thr = percentile(s_nmoist, s_tmp, 1, ps->rock_pm);
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            i = TILE_IDX(x, y);
            if (s_tmp[i] && s_nmoist[i] >= thr && s_dw[i] != 1) set_ter(x, y, TER_ROCK, 0);
        }
    /* small islands: granite and pine */
    n = label_comps(0);
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            i = TILE_IDX(x, y);
            if (s_lab[i] < 0 || s_cedge[s_lab[i]] || s_csize[s_lab[i]] > 24) continue;
            s_mark[i] |= MK_ISLAND;
            if ((hash_xy(x, y, s_seed ^ 0x15) & 255) < 80) set_ter(x, y, TER_ROCK, 0);
            else set_ter(x, y, TER_FOREST, FOREST_PINE);
        }
    UNUSED(n);
}

static void mark_road_tiles(void)
{
    int i, dx, dy;
    for (i = 0; i < s_nplen; i++) {
        int t = s_npath[i], x = t & (MAP_STRIDE - 1), y = t >> MAP_SHIFT;
        s_mark[t] |= MK_ROAD;
        for (dy = -3; dy <= 3; dy++)
            for (dx = -3; dx <= 3; dx++) {
                int nx = x + dx, ny = y + dy;
                if (!in_map(nx, ny)) continue;
                s_mark[TILE_IDX(nx, ny)] |= MK_ROADNEAR;
                if (IABS(dx) + IABS(dy) == 1) s_mark[TILE_IDX(nx, ny)] |= MK_ROADADJ;
            }
    }
}

static void lay_path(int cls, int *x0, int *y0, int *x1, int *y1)
{
    int i;
    for (i = 0; i < s_plen; i++) {
        int t = s_path[i];
        Tile *tl = &g_world.t[t];
        if (!TER_IS_WATER(tl->terrain & 15)) tl->terrain = TER_GRASS;
        tl->road = (u8)((tl->road & ~TR_CLASS_MASK) | cls);
        tl->flags = (u8)((tl->flags & ~TF_TREE) | TF_PROTECTED);
    }
    g_world.t[s_path[0]].flags |= TF_EDGE_LINK;
    g_world.t[s_path[s_plen - 1]].flags |= TF_EDGE_LINK;
    *x0 = s_path[0] & (MAP_STRIDE - 1); *y0 = s_path[0] >> MAP_SHIFT;
    *x1 = s_path[s_plen - 1] & (MAP_STRIDE - 1); *y1 = s_path[s_plen - 1] >> MAP_SHIFT;
}

/* position range on edge e (per mille lo..hi of its length), away from the corner shared with edge
   other when the two edges are adjacent (no short corner-cutting roads) */
static void edge_range(int e, int other, int lo_pm, int hi_pm, int *lo, int *hi)
{
    int len = edge_len(e), high;
    *lo = len * lo_pm / 1000; *hi = len * hi_pm / 1000;
    if (other < 0 || ((e ^ other) & 1) == 0) return;          /* opposite (or none) */
    /* NE/SW edges run along x: the corner with SE is at the high end; SE/NW run along y: corner with SW high */
    high = (e == DIR_NE || e == DIR_SW) ? (other == DIR_SE) : (other == DIR_SW);
    if (high) *hi = IMIN(*hi, len * 55 / 100);
    else *lo = IMAX(*lo, len * 45 / 100);
}

/* national road: entry/exit edge pairs in order of preference, bridges only over narrows/rivers */
static int stage_road(int preset, int sea, int river_edge)
{
    int pairs[12][2], np = 0, pass, k, e1, e2;
    e1 = rng_below(&s_rng, 4);
    if (sea >= 0) {
        if (rng_below(&s_rng, 10) < 6) { pairs[np][0] = (sea + 1) & 3; pairs[np][1] = (sea + 3) & 3; }
        else { pairs[np][0] = (sea + 2) & 3; pairs[np][1] = (sea + 1 + 2 * rng_below(&s_rng, 2)) & 3; }
        np++;
    } else if (preset == MAP_PRESET_RIVER && river_edge >= 0) {
        pairs[np][0] = (river_edge + 1) & 3; pairs[np][1] = (river_edge + 3) & 3; np++;
    } else {
        e2 = rng_below(&s_rng, 10) < 7 ? DIR_OPPOSITE(e1) : (e1 + 1 + 2 * rng_below(&s_rng, 2)) & 3;
        pairs[np][0] = e1; pairs[np][1] = e2; np++;
    }
    for (k = 0; k < 4; k++) {                 /* fallbacks: every other pair not touching the sea */
        int a = (e1 + k) & 3, b;
        for (b = 0; b < 4; b++) {
            int j, dup = 0;
            if (a == b || a == sea || b == sea) continue;
            for (j = 0; j < np; j++) if (pairs[j][0] == a && pairs[j][1] == b) dup = 1;
            if (!dup && np < 12) { pairs[np][0] = a; pairs[np][1] = b; np++; }
        }
    }
    for (pass = 0; pass < 3; pass++)
        for (k = 0; k < np; k++) {
            Route r;
            int a = pairs[k][0], b = pairs[k][1], n, s, alo, ahi, blo, bhi, apref, bpref;
            memset(&r, 0, sizeof(r));
            r.mode = RT_ROAD; r.turn = 50; r.margin = 3;
            r.br_lake = pass < 2 ? 4 : BRIDGE_MAX_LEN; r.br_river = BRIDGE_MAX_LEN;
            r.br_tile = 40; r.br_fixed = 120;
            r.tgt_tile = -1;
            edge_range(a, b, 200, 800, &alo, &ahi);
            edge_range(b, a, 200, 800, &blo, &bhi);
            r.target_edge = b; r.tgt_lo = blo; r.tgt_hi = bhi;
            r.tgt_ex_lo = 1; r.tgt_ex_hi = 0;
            bpref = (blo * 3 + bhi) / 4 + rng_below(&s_rng, (bhi - blo) / 2 + 1);
            apref = (alo * 3 + ahi) / 4 + rng_below(&s_rng, (ahi - alo) / 2 + 1);
            r.tgt_pref = bpref;
            n = edge_sources(a, alo, ahi, apref, 1, 0, 0);
            if (pass == 0) {
                /* two legs through a waypoint off the straight line: a road with gentle turns */
                int ax, ay, bx, by, wx, wy, wt, d1, n1, i, dx, dy;
                edge_tile(a, apref, &ax, &ay);
                edge_tile(b, bpref, &bx, &by);
                wx = (ax + bx) / 2 + rng_range(&s_rng, -s_w / 4, s_w / 4);
                wy = (ay + by) / 2 + rng_range(&s_rng, -s_h / 4, s_h / 4);
                wx = ICLAMP(wx, s_w / 5, s_w * 4 / 5); wy = ICLAMP(wy, s_h / 5, s_h * 4 / 5);
                wt = -1;
                for (i = 0; i <= 12 && wt < 0; i++)                 /* nearest usable land tile */
                    for (dy = -i; dy <= i && wt < 0; dy++)
                        for (dx = -i; dx <= i; dx++) {
                            int tx = wx + dx, ty = wy + dy, ti;
                            if (IMAX(IABS(dx), IABS(dy)) != i || !in_map(tx, ty)) continue;
                            ti = TILE_IDX(tx, ty);
                            if (is_w(tx, ty) || ter_at(ti) == TER_ROCK || (s_mark[ti] & MK_RAPIDS) ||
                                min_edge_dist(tx, ty) < 12) continue;
                            wt = ti; break;
                        }
                if (wt < 0) continue;
                r.target_edge = -1; r.tgt_tile = wt;
                s = rt_run(&r, s_src, n);
                if (s < 0) continue;
                n1 = rt_path(s);
                for (i = 0; i < n1; i++) { s_npath[i] = s_path[i]; s_npdir[i] = s_pdir[i]; }
                d1 = s_pdir[n1 - 1];
                /* second leg may not touch the first (no spurious junctions) */
                for (dy = 0; dy < s_h; dy++)
                    for (dx = 0; dx < s_w; dx++) s_tmp[TILE_IDX(dx, dy)] = 0;
                for (i = 0; i + 1 < n1; i++) {
                    int t = s_path[i], tx = t & (MAP_STRIDE - 1), ty = t >> MAP_SHIFT, d;
                    s_tmp[t] = 1;
                    for (d = 0; d < DIR_COUNT; d++)
                        if (in_map(tx + g_dir_dx[d], ty + g_dir_dy[d]) &&
                            TILE_IDX(tx + g_dir_dx[d], ty + g_dir_dy[d]) != wt)
                            s_tmp[TILE_IDX(tx + g_dir_dx[d], ty + g_dir_dy[d])] = 1;
                }
                r.target_edge = b; r.tgt_tile = -1; r.forbid = 1;
                s_src[0].x = wt & (MAP_STRIDE - 1); s_src[0].y = wt >> MAP_SHIFT; s_src[0].dir = d1; s_src[0].cost = 0;
                s = rt_run(&r, s_src, 1);
                if (s < 0) continue;
                rt_path(s);
                for (i = 1; i < s_plen && n1 + i - 1 < NT; i++) { s_npath[n1 + i - 1] = s_path[i]; s_npdir[n1 + i - 1] = s_pdir[i]; }
                s_nplen = n1 + s_plen - 1;
                for (i = 0; i < s_nplen; i++) { s_path[i] = s_npath[i]; s_pdir[i] = s_npdir[i]; }
                s_plen = s_nplen;
            } else {
                s = rt_run(&r, s_src, n);
                if (s < 0) continue;
                rt_path(s);
                for (n = 0; n < s_plen; n++) { s_npath[n] = s_path[n]; s_npdir[n] = s_pdir[n]; }
                s_nplen = s_plen;
            }
            lay_path(ROAD_MAIN, &s_rep.road_x0, &s_rep.road_y0, &s_rep.road_x1, &s_rep.road_y1);
            mark_road_tiles();
            return 1;
        }
    return 0;
}

/* last resort: a causeway of land straight across the map, then route again */
static void causeway(void)
{
    int x, y = s_h / 2, dy;
    for (x = 0; x < s_w; x++)
        for (dy = -1; dy <= 1; dy++)
            if (is_w(x, y + dy)) set_ter(x, y + dy, TER_GRASS, 0);
    s_rep.pinches_fixed += shore_fix();
    distance_fields(63);
}

static void stage_town(void)
{
    int i, best = -1, bests = -0x7fffffff, cx = s_w / 2, cy = s_h / 2, dx, dy, tx, ty;
    for (i = 0; i < s_nplen; i++) {
        int t = s_npath[i], x = t & (MAP_STRIDE - 1), y = t >> MAP_SHIFT, score = 0;
        if (is_w(x, y) || min_edge_dist(x, y) < 10) continue;
        for (dy = -7; dy <= 7; dy++)
            for (dx = -7; dx <= 7; dx++) {
                int nx = x + dx, ny = y + dy, ter;
                if (!in_map(nx, ny)) continue;
                ter = TER_TYPE(WT(nx, ny));
                if (TER_IS_WATER(ter)) continue;
                score += ter == TER_ROCK ? 1 : ter == TER_FOREST ? 2 : 3;
                if (s_mark[TILE_IDX(nx, ny)] & MK_RAPIDS) score -= 6;
            }
        score -= 2 * (IABS(x - cx) + IABS(y - cy));
        if (score > bests) { bests = score; best = i; }
    }
    if (best < 0) best = s_nplen / 2;
    tx = s_npath[best] & (MAP_STRIDE - 1); ty = s_npath[best] >> MAP_SHIFT;
    s_rep.town_x = tx; s_rep.town_y = ty;
    for (dy = -9; dy <= 9; dy++)
        for (dx = -9; dx <= 9; dx++) {
            int nx = tx + dx, ny = ty + dy, r2 = dx * dx + dy * dy, ter;
            int lim = 50 + (int)(hash_xy(nx, ny, s_seed ^ 0x70) % 21) - 10;
            if (!in_map(nx, ny)) continue;
            if (r2 <= 81) s_mark[TILE_IDX(nx, ny)] |= MK_TOWN;
            if (r2 > lim) continue;
            ter = TER_TYPE(WT(nx, ny));
            if (ter == TER_FOREST || ter == TER_ROCK) set_ter(nx, ny, TER_GRASS, 0);
        }
}

static void stage_rail(void)
{
    int a, b, pass;
    for (pass = 0; pass < 2; pass++) {
        Route r;
        int la, lb, n, s, rpa, rpb, x0, y0, x1, y1;
        /* same edge pair as the national road (it cannot cross the road, so it stays on one side) */
        a = DIR_OPPOSITE(s_npdir[0]);
        b = s_npdir[s_nplen - 1];
        if (pass == 1) { n = a; a = b; b = n; }
        la = edge_len(a); lb = edge_len(b);
        rpa = edge_pos(a, s_rep.road_x0, s_rep.road_y0);
        rpb = edge_pos(b, s_rep.road_x1, s_rep.road_y1);
        if (pass == 1) {
            rpa = edge_pos(a, s_rep.road_x1, s_rep.road_y1);
            rpb = edge_pos(b, s_rep.road_x0, s_rep.road_y0);
        }
        memset(&r, 0, sizeof(r));
        r.tgt_tile = -1;
        r.mode = RT_RAIL; r.turn = 150; r.margin = 3;
        r.br_lake = 4; r.br_river = BRIDGE_MAX_LEN; r.br_tile = 40; r.br_fixed = 150;
        r.target_edge = b; r.tgt_lo = lb / 10; r.tgt_hi = lb * 9 / 10;
        r.tgt_ex_lo = rpb - 16; r.tgt_ex_hi = rpb + 16; r.tgt_pref = rpb < lb / 2 ? lb * 3 / 4 : lb / 4;
        n = edge_sources(a, la / 10, la * 9 / 10, rpa < la / 2 ? la * 3 / 4 : la / 4, rpa - 16, rpa + 16, 0);
        s = rt_run(&r, s_src, n);
        if (s < 0) continue;
        rt_path(s);
        lay_path(ROAD_RAIL, &x0, &y0, &x1, &y1);
        for (n = 0; n < s_plen; n++) s_mark[s_path[n]] |= MK_RAIL;
        s_rep.railway = 1;
        s_rep.rail_x0 = x0; s_rep.rail_y0 = y0; s_rep.rail_x1 = x1; s_rep.rail_y1 = y1;
        return;
    }
}

static int field_ok(int x, int y)
{
    int i, ter;
    if (!in_map(x, y)) return 0;
    i = TILE_IDX(x, y);
    ter = ter_at(i);
    if (ter != TER_GRASS && ter != TER_FOREST) return 0;
    if (g_world.t[i].road || (s_mark[i] & (MK_TOWN | MK_RAPIDS | MK_ISLAND))) return 0;
    return s_dw[i] >= 2;
}

static void stage_fields(const Preset *ps)
{
    int f, land = 0, cap, nf = 0, x, y, farms;
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) land += is_l(x, y);
    cap = (int)((s64)land * ps->field_pm / 1000);
    farms = ps->farms * s_w * s_h / (128 * 128);          /* scale with the map area */
    for (f = 0; f < farms && nf < cap; f++) {
        int tries, cx = -1, cy = -1, rects, k;
        for (tries = 0; tries < 24; tries++) {
            int i, t, tx, ty, perp, off;
            if (s_nplen < 16) break;
            i = 6 + rng_below(&s_rng, s_nplen - 12);
            t = s_npath[i]; tx = t & (MAP_STRIDE - 1); ty = t >> MAP_SHIFT;
            if (IMAX(IABS(tx - s_rep.town_x), IABS(ty - s_rep.town_y)) <= 12) continue;
            perp = (s_npdir[i] + 1 + 2 * rng_below(&s_rng, 2)) & 3;
            off = rng_range(&s_rng, 3, 7);
            tx += g_dir_dx[perp] * off; ty += g_dir_dy[perp] * off;
            if (!field_ok(tx, ty) || s_dw[TILE_IDX(tx, ty)] < 3) continue;
            cx = tx; cy = ty;
            break;
        }
        if (cx < 0) continue;
        rects = rng_range(&s_rng, 5, 9);
        for (k = 0; k < rects && nf < cap; k++) {
            int rw = rng_range(&s_rng, 3, 8), rh = rng_range(&s_rng, 2, 6), rx, ry, i, j, tmp;
            int kind = rng_below(&s_rng, 100) < 35 ? FIELD_PLOUGH : FIELD_CROP;
            if (rng_below(&s_rng, 2)) { tmp = rw; rw = rh; rh = tmp; }
            rx = cx + rng_range(&s_rng, -9, 9) - rw / 2;
            ry = cy + rng_range(&s_rng, -9, 9) - rh / 2;
            for (j = 0; j < rh; j++)
                for (i = 0; i < rw; i++)
                    if (field_ok(rx + i, ry + j)) {
                        Tile *t = WT(rx + i, ry + j);
                        t->terrain = (u8)(TER_FIELD | (kind << 4));
                        t->flags &= ~TF_TREE;
                        nf++;
                    }
        }
    }
}

/* forest along the road: keep it from walling in the national road */
static int road_forest_pm(void)
{
    int x, y, land = 0, forest = 0;
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int i = TILE_IDX(x, y), ter = ter_at(i);
            if (!(s_mark[i] & MK_ROADNEAR) || TER_IS_WATER(ter) || g_world.t[i].road) continue;
            land++;
            if (ter == TER_FOREST) forest++;
        }
    return land ? forest * 1000 / land : 0;
}

static void stage_open_roadside(void)
{
    int x, y, pm = road_forest_pm();
    if (pm <= 450) return;
    /* clear roadside clearings: forest within 3 tiles of the road where the patch noise is low */
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int i = TILE_IDX(x, y);
            if (!(s_mark[i] & MK_ROADNEAR) || ter_at(i) != TER_FOREST || (s_mark[i] & MK_ISLAND)) continue;
            if ((int)s_npatch[i] < 65280 * (pm - 300) / 1000 + 16000) set_ter(x, y, TER_GRASS, 0);
        }
}

/* neighbourhood masks: s_nb1 / s_nb2 bit NB_* set if that terrain is within Chebyshev distance 1 / 2
   (separable box dilation of the per-tile bits) */
enum { NB_ROCK = 1, NB_FIELD = 2, NB_FOREST = 4 };
static u8 s_nb1[NT], s_nb2[NT];
static void dilate(u8 *dst, int r)
{
    int x, y, k;
    for (y = 0; y < s_h; y++)                    /* rows: s_mask -> s_tmp */
        for (x = 0; x < s_w; x++) {
            u8 m = 0;
            for (k = IMAX(0, x - r); k <= IMIN(s_w - 1, x + r); k++) m |= s_mask[TILE_IDX(k, y)];
            s_tmp[TILE_IDX(x, y)] = m;
        }
    for (y = 0; y < s_h; y++)                    /* columns: s_tmp -> dst */
        for (x = 0; x < s_w; x++) {
            u8 m = 0;
            for (k = IMAX(0, y - r); k <= IMIN(s_h - 1, y + r); k++) m |= s_tmp[TILE_IDX(x, k)];
            dst[TILE_IDX(x, y)] = m;
        }
}
static void neighbourhoods(void)
{
    int x, y;
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int ter = ter_at(TILE_IDX(x, y));
            s_mask[TILE_IDX(x, y)] = (u8)(ter == TER_ROCK ? NB_ROCK : ter == TER_FIELD ? NB_FIELD :
                                          ter == TER_FOREST ? NB_FOREST : 0);
        }
    dilate(s_nb1, 1);
    dilate(s_nb2, 2);
}

static void stage_species_and_details(void)
{
    int x, y, tpine, tspruce, tmeadow, fthr;
    noise_fill(s_nmoist, 7, 4, 4, 3, 50);
    neighbourhoods();
    /* effective dryness / moisture into s_acc (dry) and s_nmoist (moist) */
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int i = TILE_IDX(x, y), dw = s_dw[i];
            s32 dry = s_ndry[i], moist = s_nmoist[i];
            if (s_nb1[i] & NB_ROCK) dry += 14000;
            if (dw <= 2) moist += 12000; else if (dw <= 5) moist += 6000;
            moist += (65280 - s_nheight[i]) / 2;          /* hollows hold water */
            s_ndry[i] = (u16)ICLAMP(dry * 2 / 3, 0, 65535);
            s_nmoist[i] = (u16)ICLAMP(moist * 2 / 3, 0, 65535);
            s_tmp[i] = (u8)(ter_at(i) == TER_FOREST);
        }
    tpine = percentile(s_ndry, s_tmp, 1, 220);
    tspruce = percentile(s_nmoist, s_tmp, 1, 300);
    fthr = percentile(s_nforest, s_tmp, 1, 1000);
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) s_tmp[TILE_IDX(x, y)] = (u8)(ter_at(TILE_IDX(x, y)) == TER_GRASS);
    tmeadow = percentile(s_npatch, s_tmp, 1, 200);
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int i = TILE_IDX(x, y), ter = ter_at(i), d;
            u32 h = hash_xy(x, y, s_seed ^ 0x5eed);
            Tile *t = &g_world.t[i];
            t->aux = 0;
            if (ter == TER_FOREST) {
                int sp, edge = 0, dens;
                for (d = 0; d < DIR_COUNT; d++) {
                    int nx = x + g_dir_dx[d], ny = y + g_dir_dy[d];
                    if (in_map(nx, ny) && TER_TYPE(WT(nx, ny)) != TER_FOREST && !is_w(nx, ny)) edge = 1;
                }
                if (s_mark[i] & MK_ISLAND) sp = FOREST_PINE;
                else if (s_ndry[i] >= tpine) sp = FOREST_PINE;
                else if (s_nmoist[i] >= tspruce) sp = FOREST_SPRUCE;
                else if ((s_nb2[i] & NB_FIELD) && (h & 127) < 90) sp = FOREST_BIRCH;
                else if (edge && (h & 127) < 56) sp = FOREST_BIRCH;
                else if (s_npatch[i] > 44000) sp = FOREST_BIRCH;
                else sp = FOREST_MIXED;
                t->terrain = (u8)(TER_FOREST | (sp << 4));
                dens = 5 + (int)(((s32)s_nforest[i] - fthr) * 10 / IMAX(1, 65280 - fthr)) + (int)((h >> 8) & 1);
                if (edge) dens -= 3;
                if (sp == FOREST_BIRCH) dens--;
                t->aux = (u8)ICLAMP(dens, 1, 15);
            } else if (ter == TER_GRASS && !t->road) {
                int p, var = (int)((h >> 4) & 1);
                if (s_npatch[i] >= tmeadow && !(s_mark[i] & MK_ROADADJ)) var = 2;
                t->terrain = (u8)(TER_GRASS | (var << 4));
                p = 25;                                           /* per mille */
                if (s_nb2[i] & NB_FOREST) p = 90;
                else if (s_nb1[i] & NB_FIELD) p = 60;
                if (var == 2) p += 30;
                if (s_mark[i] & MK_TOWN) p = 15;
                if (s_mark[i] & (MK_ROADADJ | MK_RAIL)) p = 0;
                if ((int)((h >> 12) % 1000) < p) t->flags |= TF_TREE;
            }
            if (TER_IS_WATER(ter)) t->aux = (u8)IMIN(s_dw[i], 15);
        }
}

static double s_tst[12];
static int s_nst;
#define STAGE_DONE() do { if (s_nst < 12) s_tst[s_nst++] = time_ms(); } while (0)
/* ------------------------------------------------------------------ public */
void mapgen_defaults(MapGenParams *p)
{
    memset(p, 0, sizeof(*p));
    p->seed = 1; p->preset = MAP_PRESET_LAKELAND; p->w = MAP_DEF_W; p->h = MAP_DEF_H; p->river = -1;
}

static void report_stats(void)
{
    int x, y, n = s_w * s_h, water = 0, forest = 0, rock = 0, field = 0, build = 0, open = 0, run = 0;
    int i;
    for (y = 0; y < s_h; y++)
        for (x = 0; x < s_w; x++) {
            int ter = TER_TYPE(WT(x, y));
            if (TER_IS_WATER(ter)) water++;
            if (ter == TER_FOREST) forest++;
            if (ter == TER_ROCK) rock++;
            if (ter == TER_FIELD) field++;
            if (world_is_buildable(x, y)) {
                build++;
                if (ter == TER_GRASS || ter == TER_FIELD) open++;
            }
            if (ter == TER_RIVER && (TER_VAR(WT(x, y)) & RIVER_RAPIDS_BIT)) {
                if (!s_rep.rapids_tiles) { s_rep.rapids_x = x; s_rep.rapids_y = y; }
                s_rep.rapids_tiles++;
            }
        }
    s_rep.water_pct = water * 100 / n;
    s_rep.forest_pct = forest * 100 / n;
    s_rep.rock_pct = rock * 100 / n;
    s_rep.field_pct = field * 100 / n;
    s_rep.buildable_pct = build * 100 / n;
    s_rep.open_pct = open * 100 / n;
    s_rep.road_tiles = s_nplen;
    for (i = 0; i < s_nplen; i++) {
        if (TER_IS_WATER(ter_at(s_npath[i]))) { s_rep.bridge_tiles++; run++; s_rep.bridge_max = IMAX(s_rep.bridge_max, run); }
        else run = 0;
    }
    s_rep.road_forest_pct = road_forest_pm() / 10;
    if (s_rep.rapids_tiles)
        for (y = IMAX(0, s_rep.rapids_y - 8); y < IMIN(s_h, s_rep.rapids_y + 8); y++)
            for (x = IMAX(0, s_rep.rapids_x - 8); x < IMIN(s_w, s_rep.rapids_x + 8); x++)
                if (world_lot_check(B_HYDRO_PLANT, x, y, 0) == PLACE_OK) s_rep.hydro_sites++;
    s_rep.lakes = 0;
    n = label_comps(1);
    for (i = 0; i < n; i++) if (!s_cedge[i]) s_rep.lakes++;
    s_rep.islands = 0;
    n = label_comps(0);
    for (i = 0; i < n; i++) if (!s_cedge[i]) s_rep.islands++;
    s_rep.shore_errors = mapgen_shore_errors();
}

int mapgen_generate(const MapGenParams *p)
{
    const Preset *ps;
    double t0 = time_ms();
    int preset, river, sea = -1, river_edge = -1, ok;
    MapGenParams d;
    if (!p) { mapgen_defaults(&d); p = &d; }
    preset = (p->preset >= 0 && p->preset < MAP_PRESET_COUNT) ? p->preset : MAP_PRESET_LAKELAND;
    ps = &s_presets[preset];
    memset(&s_rep, 0, sizeof(s_rep));
    s_rep.rapids_x = s_rep.rapids_y = -1;
    s_rep.town_x = s_rep.town_y = -1;
    world_new(ICLAMP(p->w ? p->w : MAP_DEF_W, 32, MAP_MAX_W), ICLAMP(p->h ? p->h : MAP_DEF_H, 32, MAP_MAX_H));
    s_w = g_world.w; s_h = g_world.h;
    s_seed = p->seed;
    rng_seed(&s_rng, hash_u32(p->seed * 2654435761u + (u32)preset * 977u + 12345u));
    s_rplen = s_nplen = 0;
    memset(s_mark, 0, sizeof(s_mark));

    s_nst = 0; s_tst[s_nst++] = time_ms();
    noise_fill(s_nheight, 2, 6, 6, 3, 50);
    noise_fill(s_npatch, 8, 3, 3, 3, 60);
    sea = stage_lakes(ps, preset);
    STAGE_DONE();

    river = p->river;
    if (river < 0) river = rng_below(&s_rng, 1000) < ps->river_pm;
    if (river && stage_river(preset, sea)) {
        s_rep.river = 1;
        river_edge = DIR_OPPOSITE(s_rpdir[0]);
        s_rep.pinches_fixed += shore_fix();
        stage_rapids();
    }
    STAGE_DONE();
    distance_fields(63);
    stage_cover(ps);
    STAGE_DONE();

    ok = stage_road(preset, sea, river_edge);
    if (!ok) {
        log_printf("mapgen: warning: no road route, building a causeway (seed %u)", p->seed);
        causeway();
        ok = stage_road(preset, -1, -1);
    }
    STAGE_DONE();
    if (ok) {
        stage_town();
        if (p->railway) stage_rail();
        stage_fields(ps);
        stage_open_roadside();
    }
    STAGE_DONE();
    stage_species_and_details();
    STAGE_DONE();

    world_recalc();
    world_notify_all(WCHG_ALL);
    report_stats();
    s_rep.ms = time_ms() - t0;
    {
        int k; char b[200]; int n = 0;
        for (k = 1; k < s_nst; k++) n += str_printf(b + n, 200 - n, " %.2f", s_tst[k] - s_tst[k - 1]);
        log_printf("mapgen: stage ms (lakes river cover road town/rail/fields details):%s", b);
    }
    log_printf("mapgen: %s seed %u %dx%d: water %d%% forest %d%% rock %d%% field %d%% buildable %d%% open %d%%, "
               "road %d tiles (%d bridge, max %d), river %d rapids %d hydro sites %d, rail %d, fixed %d, "
               "shore errors %d, %.1f ms",
               s_names[preset], p->seed, s_w, s_h, s_rep.water_pct, s_rep.forest_pct, s_rep.rock_pct,
               s_rep.field_pct, s_rep.buildable_pct, s_rep.open_pct, s_rep.road_tiles, s_rep.bridge_tiles,
               s_rep.bridge_max, s_rep.river, s_rep.rapids_tiles, s_rep.hydro_sites, s_rep.railway,
               s_rep.pinches_fixed, s_rep.shore_errors, s_rep.ms);
    return ok;
}

const MapGenReport *mapgen_report(void) { return &s_rep; }
const char *mapgen_preset_name(int preset) { return (preset >= 0 && preset < MAP_PRESET_COUNT) ? s_names[preset] : "?"; }
int  mapgen_preset_from_name(const char *name)
{
    int i;
    for (i = 0; i < MAP_PRESET_COUNT; i++) if (str_ieq(name, s_keys[i])) return i;
    return -1;
}

int mapgen_road_connected(void)
{
    int x, y, w = g_world.w, h = g_world.h;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            const Tile *t = WT(x, y);
            int qh = 0, qt = 0, e0, i;
            if (!(t->flags & TF_EDGE_LINK) || !TILE_ROAD(t) || TILE_ROAD(t) == ROAD_RAIL) continue;
            e0 = x == 0 ? DIR_NW : y == 0 ? DIR_NE : x == w - 1 ? DIR_SE : DIR_SW;
            for (i = 0; i < h; i++) { int j; for (j = 0; j < w; j++) s_tmp[TILE_IDX(j, i)] = 0; }
            s_tmp[TILE_IDX(x, y)] = 1; s_queue[qt++] = TILE_IDX(x, y);
            while (qh < qt) {
                int c = s_queue[qh++], cx = c & (MAP_STRIDE - 1), cy = c >> MAP_SHIFT, d;
                const Tile *ct = &g_world.t[c];
                if ((ct->flags & TF_EDGE_LINK) && c != TILE_IDX(x, y)) {
                    int e = cx == 0 ? DIR_NW : cy == 0 ? DIR_NE : cx == w - 1 ? DIR_SE : DIR_SW;
                    if (e != e0) return 1;
                }
                for (d = 0; d < DIR_COUNT; d++) {
                    int nx = cx + g_dir_dx[d], ny = cy + g_dir_dy[d], ni, cls;
                    if (!in_map(nx, ny)) continue;
                    ni = TILE_IDX(nx, ny);
                    cls = TILE_ROAD(&g_world.t[ni]);
                    if (s_tmp[ni] || !cls || cls == ROAD_RAIL) continue;
                    s_tmp[ni] = 1; s_queue[qt++] = ni;
                }
            }
            return 0;                        /* the first edge link found does not reach another edge */
        }
    return 0;
}

void mapgen_dump_ascii(FILE *f)
{
    static const char sp[FOREST_COUNT] = { 'm', 's', 'b', 'p' };
    int x, y;
    fprintf(f, "legend: ~ lake  r river (flow) R rapids  m s b p forest mixed/spruce/birch/pine  ^ rock\n"
               "        , crop  ; plough  . grass  \" meadow  t single tree  # national road  = bridge\n"
               "        H railway  h rail bridge\n");
    for (y = 0; y < g_world.h; y++) {
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = WT(x, y);
            int ter = TER_TYPE(t), v = TER_VAR(t), r = TILE_ROAD(t);
            char c = '?';
            if (r == ROAD_RAIL) c = TER_IS_WATER(ter) ? 'h' : 'H';
            else if (r) c = TER_IS_WATER(ter) ? '=' : '#';
            else switch (ter) {
                case TER_WATER:  c = '~'; break;
                case TER_RIVER:  c = (v & RIVER_RAPIDS_BIT) ? 'R' : 'r'; break;
                case TER_FOREST: c = sp[v & 3]; break;
                case TER_ROCK:   c = '^'; break;
                case TER_FIELD:  c = v == FIELD_PLOUGH ? ';' : ','; break;
                case TER_GRASS:  c = (t->flags & TF_TREE) ? 't' : v == 2 ? '"' : '.'; break;
                default:         c = '?'; break;
            }
            fputc(c, f);
        }
        fputc('\n', f);
    }
}
