/* render.cpp - world chunk cache, painter's order with 32-px strip slicing, overlays, dynamic sprites with
   the occlusion fix, picking, minimap image, debug dumps, the -test render town + bench. WINDOWS SIDE.
   Owner: T05. DESIGN 11.3, 11.4; probe/RESULTS.md 4-8; STYLE.md 3-4; art/ART.md 6.

   CHUNKS. The static world is pre-rendered into 256x128 VRAM targets on a fixed grid of world-pixel space
   (iso.h) at the current zoom. s_idx maps grid cell -> pool slot. Pool = visible chunks + one margin row
   and column (sized at render_init from the view and the free VRAM). Per frame render_update():
     1. visible chunks that are missing or invalid are rendered NOW (no holes, ever; a flush = 1 slow frame)
     2. dirty visible chunks (world edits) nearest the view centre first, while the budget lasts
     3. overlay-check chunks (WCHG_NET/FIELDS while an overlay is on): re-render only if the hash of the
        chunk's overlay values changed
     4. prefetch: the margin row/column in the scroll direction
     5. water shimmer (render_set_anim_frame): visible water chunks, oldest first
   A dirty chunk keeps showing its old picture until it is re-rendered (a frame or two at most).

   PAINTER'S ORDER inside a chunk (STYLE.md 4):
     ground pass   terrain, water/shore (shimmer frame), roads, rails, zone tints of empty zoned tiles;
                   flat-overlay mode: lot ground sprites. Tiles covered by an object that includes its own
                   ground (lots, forest, rock, bridges; art check: all cover their footprint) are skipped.
     object pass   lots, forest/rock, bridges, single trees, power poles/wires, cut into strips on the
                   half-tile column grid and sorted by key = depth * 4 + sub. Depth of a strip = x + y of
                   the front-most footprint tile in that column; a multi-tile lot gives its two centre
                   columns (front tile) one merged piece and every other column its own piece, so a lot
                   costs w + h - 1 blits and 1x1 objects one blit. sub: 0 lot/forest/rock/bridge,
                   1 dynamic sprite, 2 tree, 3 power (a wire over a road tile draws above a car).
     overlay pass  ovl_<ramp>_<level> checker-dither diamond per tile, over everything (DESIGN 11.4).
   Strips in different columns never overlap, so sorting by key alone gives the correct per-column order.

   DYNAMIC SPRITES (render_sprite): sorted by key, drawn after the chunk blits; then the occlusion fix
   redraws every object piece in front (key greater) that overlaps the sprite, clipped to the sprite
   rectangle. Bridges never occlude (cars drive onto them). RS_NO_OCCLUDE sprites (cursor marks, ghosts,
   floating numbers) are drawn last in submission order; RS_HALF uses a cached checker-keyed copy. */
#include "common.h"
#include "plat.h"
#include "render.h"
#include "gfx.h"
#include "util.h"
#include "config.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "traffic.h"
#include "politics.h"
#include "industry.h"
#include "mapgen.h"
#include "ui_layout.h"
#include "season.h"         /* 1.1 W2: the art season (season_is_winter / season_changed) */
#include <math.h>

#define MAX_POOL        96
#define MAX_PIECES      2400
#define IDX_MAX         (66 * 72)
#define MAX_REPAIRS     4            /* non-mandatory chunk renders per frame: the CPU budget alone let 7-9
                                        through at 1024x768 and the emulated blitter (screen + chunk fills)
                                        then missed the 60 fps blank (bench 2026-11-03) */
#define RESERVE_KB      500          /* VRAM headroom left after the pool (T01: usable = free - ~450 KB).
                                        ui_init (UI caches, ~640 KB) runs BEFORE render_init (T09), so the
                                        pool takes what is left; fewer chunks only shrink the margin */

enum { CF_DIRTY = 1, CF_ANIM = 2, CF_OVL = 4, CF_INVALID = 8 };
enum { PF_NO_OCCLUDE = 1 };
enum { SUB_BASE = 0, SUB_DYN = 1, SUB_TREE = 2, SUB_POWER = 3 };

typedef struct {
    Img  img;
    int  ci, cj;                 /* grid cell, ci < 0 = free slot */
    int  flags;                  /* CF_* */
    int  water;                  /* contains shimmering water */
    int  wx0, wy0, wx1, wy1;     /* T13: world-px box of the shimmering tiles (valid when water) */
    u32  ovl_hash;
    int  last;                   /* frame it was last visible/prefetched */
    int  rendered;               /* frame of the last render */
} Chunk;

typedef struct {
    int  key;                    /* depth * 4 + sub */
    s16  id;                     /* art id */
    u16  lot;
    s16  tx, ty;                 /* anchor tile (front tile for lots) */
    int  sx, sy;                 /* sprite top-left in world px */
    s16  c0, c1;                 /* sprite columns [c0, c1) */
    u8   flags;                  /* PF_* */
} Piece;

typedef struct {
    s16 id;
    int wx, wy;                  /* anchor, world px */
    int key;
    u16 flags;                   /* RS_* */
} RSprite;

typedef struct { int id, zoom, last; Img img; } Ghost;

static Chunk  s_pool[MAX_POOL];
static int    s_npool, s_inited, s_residency;
static s16    s_idx[IDX_MAX];
static int    s_cols, s_rows, s_idxZoom = -1;
static int    s_frame = 1, s_serial;
static int    s_ovl = OVL_NONE, s_ovlParam, s_ovlShow;
static int    s_anim;
static int    s_lastCamX, s_lastCamY, s_dirX, s_dirY;
static PIX    s_bg;
static RenderStats s_st;
static double s_avgChunkMs[ZOOM_COUNT] = { 0.5, 1.5 };
static int    s_listener;
static Piece  s_pieces[MAX_PIECES];
static int    s_npieces, s_piecesDropped;
static RSprite s_spr[RENDER_MAX_SPRITES];
static int    s_nspr;
static Ghost  s_ghost[4];
static u32    s_lotStamp[MAX_LOTS];
static u32    s_stampNo;
static u32    s_totalChunks;
static RenderTotals s_tot;          /* T13 bench totals */
static int    s_why = RR_VISIBLE;    /* reason of the next render_chunk */
static double s_totalChunkMs;

/* sprite ids resolved at init */
static int s_idGrass[3], s_idWater[3], s_idShore[16][3], s_idCrop, s_idPlough, s_idMarsh;
static int s_idRock[2], s_idForest[FOREST_COUNT][3], s_idTree[6];
static int s_idStage[3][2], s_idHay;      /* M3: forest_cut/seed/young (regrowing forestry tiles), field_hay */
static int s_idRubble[2];                  /* M5: burnt ruins (TF_RUBBLE): rubble0/1, else the 1x1 construction ground */
#define FARM_HAY(t, h) (TER_TYPE(t) == TER_FIELD && TILE_ZONE(t) == ZONE_FARM && !TILE_ROAD(t) && s_idHay >= 0 && ((h) % 3u) == 0)
static int s_idRoad[ROAD_CLASS_COUNT][16], s_idBridge[ROAD_CLASS_COUNT][2];
static int s_idRiver[2][3][3];          /* [rapids][axis x, y, n][frame] */
static int s_idPower[16], s_idWire[16], s_idOvl[OVR_COUNT][8], s_idZone[ZONE_COUNT];
static int s_idPipe[16][3];       /* F1: pipe_<MM>_<wet|short|dry> (water overlay), -1 = no art */
static int s_zoneView;            /* F3: persistent zone view (render_set_zone_view) */
static int s_idZoneV[16][2];      /* F3: zonev_<Z>_02 / zonev_<Z>_04 (front edges), -1 = no art */
static s16 s_lotCache[B_COUNT][16][4][4];
/* 1.2 X1 railway crossings (X2 sprites, 'lot'-kind like the bridges: own ground, drawn in the object pass):
   [class][road axis 0 = 05 (along Y), 1 = 10 (along X)]: <grav|road|main>_xing_MM (MM = the road's axis; fallback
   the alias rail_xing_MM, MM = the rail's axis), mway_rail_MM (MM = the motorway's axis). -1 = no art: a level
   crossing then draws the road sprite as ground, an overpass the rail + the motorway bridge (temporary). */
static int s_idXing[ROAD_CLASS_COUNT][2];
static int s_idRailPower[2];      /* rail_power_MM (MM = rail axis) if X2 made it, else -1 (wires as over a road) */

static void flush_all(int hard);

/* ================================================================== helpers */
#define HW(z) (TILE_W(z) / 2)
#define HH(z) (TILE_H(z) / 2)

static int is_water(const Tile *t) { return TER_IS_WATER(TER_TYPE(t)); }

/* shore rule (T06, mapgen.h): shores are drawn on LAND tiles; water tiles are always full water.
   mapgen_shore_mask: 15 plain land, 1..14 shore<NN> (land corner bits n=1 e=2 w=4 s=8), 0 water */
static int shore_of(int x, int y)
{
    int m = mapgen_shore_mask(x, y);
    return (m > 0 && m < 15) ? m : 0;
}

/* 1.2 X1: road-axis index of a crossing tile (0 = road along Y = 05, 1 = along X = 10) */
static int xing_axis(int x, int y) { return world_rail_axis(x, y) == 5 ? 1 : 0; }
static int xing_sprite(int x, int y, const Tile *t)
{
    int cls = TILE_ROAD(t), id = s_idXing[cls][xing_axis(x, y)];
    if (id < 0 && cls == ROAD_MOTORWAY) id = s_idBridge[ROAD_MOTORWAY][xing_axis(x, y)];   /* fallback overpass */
    return id;
}

static int power_mask(int x, int y)
{
    int d, m = 0;
    for (d = 0; d < DIR_COUNT; d++) {
        int nx = x + g_dir_dx[d], ny = y + g_dir_dy[d];
        const Tile *n;
        if (!world_in(nx, ny)) continue;
        n = WT(nx, ny);
        if (n->road & TR_POWER_LINE) m |= 1 << d;
        else if (n->lot && def_building(g_world.lots[n->lot].def)->cat == BCAT_POWER) m |= 1 << d;
    }
    return m;
}

typedef struct { int s0, s1, d0, d1; } TRange;

/* tiles whose ground diamond overlaps the world rect [X0,X1) x [Y0,Y1): s = x + y, d = x - y */
static void tile_range(int z, int X0, int Y0, int X1, int Y1, TRange *r)
{
    int hw = HW(z), hh = HH(z), th = TILE_H(z), O = iso_origin_x(z), M = ISO_TOP_MARGIN(z);
    r->s0 = FLOORDIV(Y0 - M - th, hh) + 1;
    r->s1 = FLOORDIV(Y1 - 1 - M, hh);
    r->d0 = FLOORDIV(X0 - O - hw, hw) + 1;
    r->d1 = FLOORDIV(X1 - O + hw - 1, hw);
    if (r->s0 < 0) r->s0 = 0;
    if (r->s1 > g_world.w + g_world.h - 2) r->s1 = g_world.w + g_world.h - 2;
    if (r->d0 < -(g_world.h - 1)) r->d0 = -(g_world.h - 1);
    if (r->d1 > g_world.w - 1) r->d1 = g_world.w - 1;
}

static void south_vertex(int z, int x, int y, int *wx, int *wy)
{
    iso_tile_to_world(z, x, y, wx, wy);
    *wy += TILE_H(z);
}

static int lot_sprite_id(const Lot *l, int flat)
{
    const BuildingDef *d = def_building(l->def);
    int kind = flat ? ART_LOT_GROUND : l->status == LOT_BUILDING ? ART_LOT_BUILDING :
               (l->status == LOT_ABANDONED || l->status == LOT_BURNING) ? ART_LOT_ABANDONED : ART_LOT_NORMAL;   /* M5: burning = charred (life draws the flames) */
    int v = (kind == ART_LOT_NORMAL || kind == ART_LOT_ABANDONED) ? (l->variant & 15) : 0;
    int r = (d->flags & BDF_NO_ROTATE) ? 0 : (l->rot & 3);
    s16 *c = &s_lotCache[l->def][v][r][kind];
    if (*c < 0) *c = (s16)art_lot_sprite(d->key, v, r, kind, d->w, d->h);
    return *c;
}

/* ground sprite of a tile not covered by an object (-1 = nothing); *water = shimmering */
static int ground_id(int x, int y, const Tile *t, int *water)
{
    int ter = TER_TYPE(t), cls = TILE_ROAD(t);
    int m;
    *water = 0;
    if (cls) {
        if (TER_IS_WATER(ter)) return -1;                     /* bridge: object pass */
        if (t->flags & TF_RAIL) {                             /* 1.2 X1: crossing / overpass (TILE_IS_XING) */
            if (s_idXing[cls][xing_axis(x, y)] >= 0) return -1;            /* own ground: object pass */
            if (cls == ROAD_MOTORWAY) return s_idRoad[ROAD_RAIL][world_rail_mask(x, y) & 15];   /* fallback */
        }
        return s_idRoad[cls][TILE_ROAD_MASK(t)];               /* also on shore tiles (T02) */
    }
    if (ter == TER_RIVER) {                                    /* flow axis from the direction bits */
        int dir = TER_VAR(t) & 3, ax = (dir == DIR_SE || dir == DIR_NW) ? 0 : 1, f = s_anim;
        int dx = g_dir_dx[dir], dy = g_dir_dy[dir];
        int dn = !world_in(x + dx, y + dy) || is_water(WT(x + dx, y + dy));   /* off-map counts as river */
        int up = !world_in(x - dx, y - dy) || is_water(WT(x - dx, y - dy));
        if (!(dn && up)) ax = 2;                                              /* bend / end: non-directional */
        else if (dir == DIR_NW || dir == DIR_NE) f = 2 - s_anim;             /* streaks toward -axis */
        *water = 1;
        return s_idRiver[(TER_VAR(t) & RIVER_RAPIDS_BIT) ? 1 : 0][ax][f];
    }
    if (TER_IS_WATER(ter)) { *water = !art_season(); return s_idWater[s_anim]; }   /* 1.1 W2: ice does not shimmer */
    m = shore_of(x, y);
    if (m) { *water = !art_season(); return s_idShore[m][s_anim]; }      /* forest/rock on a shore: shore + a tree */
    switch (ter) {
    case TER_FOREST: case TER_ROCK: return -1;
    case TER_FIELD: return (TER_VAR(t) & 1) ? s_idPlough : s_idCrop;
    case TER_MARSH: return s_idMarsh;
    default: return s_idGrass[TER_VAR(t) % 3];
    }
}

/* ================================================================== object pieces */
static int add_piece(Piece *p, int max, int n, int z, int id, int svx, int svy, int key, int lot, int tx, int ty, int flags)
{
    const ArtSprite *sp;
    if (id < 0 || n >= max) { if (n >= max) s_piecesDropped++; return n; }
    sp = art_get(z, id);
    p[n].key = key; p[n].id = (s16)id; p[n].lot = (u16)lot; p[n].tx = (s16)tx; p[n].ty = (s16)ty;
    p[n].sx = svx - sp->ax; p[n].sy = svy - sp->ay; p[n].c0 = 0; p[n].c1 = sp->w; p[n].flags = (u8)flags;
    return n + 1;
}

/* the strips of a lot anchored at its front tile (x, y): centre pair merged, every other column alone */
static int lot_pieces(int z, int lotId, int x, int y, Piece *p, int max, int n, int flat)
{
    const Lot *l = &g_world.lots[lotId];
    const ArtSprite *sp;
    int id = lot_sprite_id(l, flat), svx, svy, left, cs, hw = HW(z), D = x + y, k;
    south_vertex(z, x, y, &svx, &svy);
    sp = art_get(z, id);
    left = svx - sp->ax;
    if (FLOORMOD(left - svx, hw) != 0 || sp->w <= 2 * hw) {        /* 1x1 or off-grid: one piece */
        return add_piece(p, max, n, z, id, svx, svy, D * 4 + SUB_BASE, lotId, x, y, 0);
    }
    for (cs = left; cs < left + sp->w; cs += hw) {
        int c0 = cs - left, c1 = c0 + hw, depth;
        if (cs == svx - hw) { c1 = c0 + 2 * hw; depth = D; }
        else if (cs < svx - hw) depth = D - (svx - hw - cs) / hw;
        else depth = D - ((cs - svx - hw) / hw + 1);
        if (c1 > sp->w) c1 = sp->w;
        if (n >= max) { s_piecesDropped++; return n; }
        k = n++;
        p[k].key = depth * 4 + SUB_BASE; p[k].id = (s16)id; p[k].lot = (u16)lotId; p[k].tx = (s16)x; p[k].ty = (s16)y;
        p[k].sx = left; p[k].sy = svy - sp->ay; p[k].c0 = (s16)c0; p[k].c1 = (s16)c1; p[k].flags = 0;
        if (cs == svx - hw) cs += hw;
    }
    return n;
}

/* every object piece anchored at tile (x, y) */
static int tile_objects(int z, int x, int y, int flat, Piece *p, int max, int n)
{
    const Tile *t = WT(x, y);
    int ter = TER_TYPE(t), svx, svy, D = x + y;
    u32 h;
    if (t->lot) {
        if ((t->flags & TF_LOT_FRONT) && !flat) n = lot_pieces(z, t->lot, x, y, p, max, n, 0);
        return n;
    }
    if (ter != TER_FOREST && ter != TER_ROCK && !(t->flags & TF_TREE) && !(t->road & TR_POWER_LINE) &&
        !(TILE_ROAD(t) && TER_IS_WATER(ter)) && !TILE_IS_XING(t) && !(ter == TER_FIELD && TILE_ZONE(t) == ZONE_FARM) && !(t->flags & TF_RUBBLE)) return n;
    south_vertex(z, x, y, &svx, &svy);
    h = hash_xy(x, y, 0x5EED);
    if ((ter == TER_FOREST || ter == TER_ROCK) && shore_of(x, y)) {      /* shore tile: ground stays shore */
        static const u8 sp_tree[FOREST_COUNT] = { 0, 1, 0, 2 };              /* birch, spruce, pine */
        if (ter == TER_FOREST && industry_forest_stage(FOREST_REGROW(t)) != FST_CUT)    /* M3: no tree on a fresh cut */
            n = add_piece(p, max, n, z, s_idTree[sp_tree[TER_VAR(t) & 3]], svx, svy, D * 4 + SUB_TREE, 0, x, y, 0);
    }
    else if (ter == TER_FOREST) {           /* M3: regrowing forestry tiles show their stage */
        int st = industry_forest_stage(FOREST_REGROW(t));
        n = add_piece(p, max, n, z, st ? s_idStage[st - 1][h & 1] : s_idForest[TER_VAR(t) & 3][h % 3], svx, svy, D * 4 + SUB_BASE, 0, x, y, 0);
    }
    else if (ter == TER_ROCK) n = add_piece(p, max, n, z, s_idRock[(h >> 4) & 1], svx, svy, D * 4 + SUB_BASE, 0, x, y, 0);
    if (TILE_ROAD(t) && TER_IS_WATER(ter)) {
        int cls = TILE_ROAD(t);
        n = add_piece(p, max, n, z, s_idBridge[cls][(TILE_ROAD_MASK(t) & 10) ? 1 : 0], svx, svy, D * 4 + SUB_BASE, 0, x, y, PF_NO_OCCLUDE);
    }
    else if (TILE_IS_XING(t))                                            /* 1.2 X1: like a bridge on land */
        n = add_piece(p, max, n, z, xing_sprite(x, y, t), svx, svy, D * 4 + SUB_BASE, 0, x, y, PF_NO_OCCLUDE);
    if ((t->flags & TF_TREE) && !TILE_ROAD(t) && ter != TER_FOREST)
        n = add_piece(p, max, n, z, s_idTree[(h >> 8) % 6], svx, svy, D * 4 + SUB_TREE, 0, x, y, 0);
    if (FARM_HAY(t, h)) n = add_piece(p, max, n, z, s_idHay, svx, svy, D * 4 + SUB_BASE, 0, x, y, 0);   /* M3: hay poles */
    if ((t->flags & TF_RUBBLE) && !TILE_ROAD(t)) n = add_piece(p, max, n, z, s_idRubble[(h >> 5) & 1], svx, svy, D * 4 + SUB_BASE, 0, x, y, 0);   /* M5 */
    if (t->road & TR_POWER_LINE) {
        int m = power_mask(x, y);
        int id = TILE_ROAD(t) ? (m ? s_idWire[m] : -1) : s_idPower[m];
        if (TILE_ROAD(t) == ROAD_RAIL && s_idRailPower[(TILE_ROAD_MASK(t) & 10) ? 1 : 0] >= 0)   /* 1.2 X1 */
            id = s_idRailPower[(TILE_ROAD_MASK(t) & 10) ? 1 : 0];
        n = add_piece(p, max, n, z, id, svx, svy, D * 4 + SUB_POWER, 0, x, y, 0);
    }
    return n;
}

static int tile_has_objects(const Tile *t)
{
    int ter = TER_TYPE(t);
    if (t->lot) return (t->flags & TF_LOT_FRONT) != 0;
    return ter == TER_FOREST || ter == TER_ROCK || (t->flags & TF_TREE) || (t->road & TR_POWER_LINE) ||
           (TILE_ROAD(t) && TER_IS_WATER(ter)) || (ter == TER_FIELD && TILE_ZONE(t) == ZONE_FARM) ||   /* M3: hay */
           TILE_IS_XING(t) ||                                                                         /* 1.2 X1 */
           (t->flags & TF_RUBBLE);                                                                    /* M5: ruins */
}

/* F3: persistent zone view, ART.md 6 front-edge rule. For tile T and its front edges SE (02, neighbour +X)
   and SW (04, neighbour +Y): zone(T) != zone(N) -> zonev_<zone(T) ? zone(T) : zone(N)>_<e> at T's south
   vertex. The band lies inside T, so it is keyed right after T's own lot/object (depth * 4 + 1): a lot strip
   in the same half-column has T as its front-most tile when T sits on the lot's front boundary, and anything
   in front of T sorts later. Bands are flat: PF_NO_OCCLUDE (never redrawn over cars). */
static int zone_of(int x, int y) { return world_in(x, y) ? TILE_ZONE(WT(x, y)) : 0; }
static int zone_edges(int x, int y)
{
    int z = zone_of(x, y);
    return (z != zone_of(x + 1, y) ? 1 : 0) | (z != zone_of(x, y + 1) ? 2 : 0);
}
static int zone_pieces(int z, int x, int y, Piece *p, int max, int n)
{
    int e = zone_edges(x, y), zt = zone_of(x, y), k, svx, svy;
    if (!e) return n;
    south_vertex(z, x, y, &svx, &svy);
    for (k = 0; k < 2; k++) {
        int zz, id;
        if (!(e & (1 << k))) continue;
        zz = zt ? zt : zone_of(x + (k == 0), y + (k == 1));
        id = (zz > 0 && zz < 16) ? s_idZoneV[zz][k] : -1;
        if (id >= 0) n = add_piece(p, max, n, z, id, svx, svy, (x + y) * 4 + SUB_DYN, 0, x, y, PF_NO_OCCLUDE);
    }
    return n;
}

/* insertion sort by key (pieces arrive nearly sorted: scan order is by depth) */
static void sort_pieces(Piece *p, int n)
{
    int i, j;
    Piece tmp;
    for (i = 1; i < n; i++) {
        if (p[i].key >= p[i - 1].key) continue;
        tmp = p[i];
        j = i - 1;
        while (j >= 0 && p[j].key > tmp.key) { p[j + 1] = p[j]; j--; }
        p[j + 1] = tmp;
    }
}

/* all pieces whose sprite may overlap the world rect, in draw order; only those overlapping are kept */
static int gather_pieces(int z, int X0, int Y0, int X1, int Y1, int min_depth, int flat, Piece *p, int max)
{
    TRange r;
    int s, d, x, y, n = 0, i, k;
    tile_range(z, X0 - art_max_half_w(z), Y0, X1 + art_max_half_w(z), Y1 + art_max_up(z), &r);
    if (r.s0 < min_depth) r.s0 = min_depth;
    for (s = r.s0; s <= r.s1; s++)
        for (d = r.d0 + ((r.d0 - s) & 1); d <= r.d1; d += 2) {
            x = (s + d) >> 1; y = (s - d) >> 1;
            if (x < 0 || y < 0 || x >= g_world.w || y >= g_world.h) continue;
            if (!tile_has_objects(WT(x, y)) && !(s_zoneView && zone_edges(x, y))) continue;
            k = n;
            n = tile_objects(z, x, y, flat, p, max, n);
            if (s_zoneView) n = zone_pieces(z, x, y, p, max, n);   /* F3 */
            /* keep only pieces overlapping the rect */
            for (i = k; i < n; ) {
                const ArtSprite *sp = art_get(z, p[i].id);
                int px0 = p[i].sx + p[i].c0, px1 = p[i].sx + p[i].c1;
                if (px1 <= X0 || px0 >= X1 || p[i].sy >= Y1 || p[i].sy + sp->h <= Y0) { p[i] = p[n - 1]; n--; }
                else i++;
            }
        }
    sort_pieces(p, n);
    return n;
}

/* T13: occluders of a dynamic sprite (occlude() ran gather_pieces with the tile range widened by the widest
   sprite, ~40 us per car on the P2). Same pieces, found from a tighter tile range:
   - a 1x1 object lies inside its tile's column span, so no sideways widening is needed, and it reaches at
     most s_smallUp[z] px above its south vertex (tiles whose objects can't reach up to the rect are skipped);
   - a strip of a multi-tile lot always has a footprint tile straight below it in the same column, so the
     lot is found from ANY footprint tile in the range (once per query, stamped) and anchored at its front
     tile; its sprite rect is checked before it is cut into strips.
   Depth filter as before: pieces in front have key > the sprite's key, and the bottom footprint tile of
   such a column has s > depth, so it is inside the range. */
static int s_smallUp[ZOOM_COUNT];
static void small_up_init(int z);

static int gather_occluders(int z, int X0, int Y0, int X1, int Y1, int min_depth, int flat, Piece *p, int max)
{
    TRange r;
    int s, d, x, y, n = 0, i, k, svx, svy;
    if (!s_smallUp[z]) small_up_init(z);
    tile_range(z, X0, Y0, X1, Y1 + art_max_up(z), &r);
    if (r.s0 < min_depth) r.s0 = min_depth;
    s_stampNo++;
    for (s = r.s0; s <= r.s1; s++)
        for (d = r.d0 + ((r.d0 - s) & 1); d <= r.d1; d += 2) {
            const Tile *t;
            x = (s + d) >> 1; y = (s - d) >> 1;
            if (x < 0 || y < 0 || x >= g_world.w || y >= g_world.h) continue;
            t = WT(x, y);
            k = n;
            if (t->lot) {
                int lx0, ly0, lx1, ly1, id;
                const ArtSprite *sp;
                if (flat || s_lotStamp[t->lot] == s_stampNo) continue;
                s_lotStamp[t->lot] = s_stampNo;
                world_lot_rect(t->lot, &lx0, &ly0, &lx1, &ly1);
                id = lot_sprite_id(&g_world.lots[t->lot], 0);
                if (id < 0) continue;
                sp = art_get(z, id);
                south_vertex(z, lx1, ly1, &svx, &svy);
                if (svx - sp->ax >= X1 || svx - sp->ax + sp->w <= X0 || svy - sp->ay >= Y1 || svy - sp->ay + sp->h <= Y0) continue;
                n = lot_pieces(z, t->lot, lx1, ly1, p, max, n, 0);
            } else {
                if (!tile_has_objects(t)) continue;
                south_vertex(z, x, y, &svx, &svy);
                if (svy - s_smallUp[z] >= Y1) continue;
                n = tile_objects(z, x, y, flat, p, max, n);
            }
            for (i = k; i < n; ) {                     /* keep only pieces overlapping the rect */
                const ArtSprite *sp = art_get(z, p[i].id);
                int px0 = p[i].sx + p[i].c0, px1 = p[i].sx + p[i].c1;
                if (px1 <= X0 || px0 >= X1 || p[i].sy >= Y1 || p[i].sy + sp->h <= Y0) { p[i] = p[n - 1]; n--; }
                else i++;
            }
        }
    sort_pieces(p, n);
    return n;
}

static void small_up_init(int z)      /* lazily for the current (resident) zoom: art_get needs it resident */
{
    int i, k;
    {
        int m = TILE_H(z);
        const int *ids[8];
        int cnt[8];
        ids[0] = s_idTree; cnt[0] = 6; ids[1] = &s_idForest[0][0]; cnt[1] = FOREST_COUNT * 3;
        ids[2] = s_idRock; cnt[2] = 2; ids[3] = s_idPower; cnt[3] = 16; ids[4] = s_idWire; cnt[4] = 16;
        ids[5] = &s_idBridge[0][0]; cnt[5] = ROAD_CLASS_COUNT * 2;
        ids[6] = &s_idXing[0][0]; cnt[6] = ROAD_CLASS_COUNT * 2; ids[7] = s_idRailPower; cnt[7] = 2;   /* 1.2 X1 */
        for (k = 0; k < 8; k++)
            for (i = 0; i < cnt[k]; i++) {
                const ArtSprite *sp;
                if (ids[k][i] < 0) continue;
                sp = art_get(z, ids[k][i]);
                if (sp->ay > m) m = sp->ay;
            }
        s_smallUp[z] = m;
    }
}

static void draw_piece(int z, const Piece *p)
{
    const ArtSprite *sp = art_get(z, p->id);
    art_blit_part(z, p->id, p->c0, 0, p->c1 - p->c0, sp->h, p->sx + p->c0, p->sy);   /* PL4: art pieces */
}

/* ================================================================== chunk rendering */
static int flat_mode(void) { return s_ovl != OVL_NONE && !s_ovlShow; }

static u32 overlay_hash(int z, int X0, int Y0, int X1, int Y1, int draw)
{
    TRange r;
    int s, d, x, y, tx, ty;
    u32 h = HASH_INIT;
    tile_range(z, X0, Y0, X1, Y1, &r);
    for (s = r.s0; s <= r.s1; s++)
        for (d = r.d0 + ((r.d0 - s) & 1); d <= r.d1; d += 2) {
            u8 v;
            x = (s + d) >> 1; y = (s - d) >> 1;
            if (x < 0 || y < 0 || x >= g_world.w || y >= g_world.h) continue;
            v = sim_overlay_value(s_ovl, s_ovlParam, x, y);
            h = (h ^ v) * 16777619u;
            if (draw && v != OVV_NONE && OVV_RAMP(v) < OVR_COUNT) {
                south_vertex(z, x, y, &tx, &ty);
                art_draw(z, s_idOvl[OVV_RAMP(v)][OVV_LEVEL(v)], tx, ty);
            }
            if (s_ovl == OVL_WATER && (WT(x, y)->flags & TF_PIPE)) {   /* F1: the pipe after its diamond */
                int pm = 0, k, st = net_water_tile_state(x, y), id;
                for (k = 0; k < DIR_COUNT; k++) {
                    int nx = x + g_dir_dx[k], ny = y + g_dir_dy[k];
                    if (world_in(nx, ny) && (WT(nx, ny)->flags & TF_PIPE)) pm |= 1 << k;
                }
                id = s_idPipe[pm][st == NLS_OK ? 0 : st == NLS_SHORT ? 1 : 2];
                h = (h ^ (u32)(0x100 | (pm << 2) | st)) * 16777619u;
                if (draw && id >= 0) { south_vertex(z, x, y, &tx, &ty); art_draw(z, id, tx, ty); }
            }
        }
    return h;
}

/* full = the whole chunk; else T13 shimmer repair: only the box of the chunk's water tiles is redrawn
   (clip + the same three passes restricted to the box), so a chunk with a strip of shore costs a fraction */
static void render_region(Chunk *c, int X0, int Y0, int X1, int Y1, int full)
{
    int z = g_cam.zoom, CX0 = c->ci * CHUNK_W, CY0 = c->cj * CHUNK_H;
    int flat = flat_mode(), s, d, x, y, i, n, wx, wy, wat, hw = HW(z), th = TILE_H(z);
    TRange r;
    double t0 = time_ms();
    gfx_set_target(&c->img);
    gfx_set_origin(CX0, CY0);
    if (full) {
        gfx_clear(s_bg);
        c->water = 0;
        c->wx0 = X1; c->wy0 = Y1; c->wx1 = X0; c->wy1 = Y0;
    } else {
        gfx_set_clip(X0 - CX0, Y0 - CY0, X1 - CX0, Y1 - CY0);
        gfx_fill(X0, Y0, X1, Y1, s_bg);
    }
    s_stampNo++;
    /* ---- ground pass */
    tile_range(z, X0, Y0, X1, Y1, &r);
    for (s = r.s0; s <= r.s1; s++)
        for (d = r.d0 + ((r.d0 - s) & 1); d <= r.d1; d += 2) {
            const Tile *t;
            int id;
            x = (s + d) >> 1; y = (s - d) >> 1;
            if (x < 0 || y < 0 || x >= g_world.w || y >= g_world.h) continue;
            t = WT(x, y);
            if (t->lot) {
                if (flat && s_lotStamp[t->lot] != s_stampNo) {      /* the lot's flat ground, once */
                    int lx0, ly0, lx1, ly1;
                    s_lotStamp[t->lot] = s_stampNo;
                    world_lot_rect(t->lot, &lx0, &ly0, &lx1, &ly1);
                    south_vertex(z, lx1, ly1, &wx, &wy);
                    art_draw(z, lot_sprite_id(&g_world.lots[t->lot], 1), wx, wy);
                }
                continue;
            }
            id = ground_id(x, y, t, &wat);
            if (id < 0) continue;
            south_vertex(z, x, y, &wx, &wy);
            if (wat && full) {
                c->water = 1;
                c->wx0 = IMIN(c->wx0, wx - hw); c->wx1 = IMAX(c->wx1, wx + hw);
                c->wy0 = IMIN(c->wy0, wy - th); c->wy1 = IMAX(c->wy1, wy);
            }
            art_draw(z, id, wx, wy);
            if (TILE_ZONE(t) && s_ovl == OVL_NONE && !TILE_ROAD(t) && !wat) art_draw(z, s_idZone[TILE_ZONE(t)], wx, wy);
        }
    /* ---- object pass */
    n = gather_pieces(z, X0, Y0, X1, Y1, 0, flat, s_pieces, MAX_PIECES);
    for (i = 0; i < n; i++) draw_piece(z, &s_pieces[i]);
    s_npieces = n;
    /* ---- overlay pass */
    if (s_ovl != OVL_NONE) {
        u32 h = overlay_hash(z, X0, Y0, X1, Y1, 1);
        if (full) c->ovl_hash = h;
    }
    gfx_set_target(NULL);
    if (full) {
        c->flags = 0;
        c->wx0 = IMAX(c->wx0, X0); c->wy0 = IMAX(c->wy0, Y0); c->wx1 = IMIN(c->wx1, X1); c->wy1 = IMIN(c->wy1, Y1);
    } else c->flags &= ~CF_ANIM;
    c->rendered = s_frame;
    {
        double ms = time_ms() - t0;
        s_st.chunks_rendered++;
        s_st.chunk_ms += ms;
        s_totalChunks++;
        s_totalChunkMs += ms;
        if (full) s_avgChunkMs[z] = s_avgChunkMs[z] * 0.9 + ms * 0.1;
        s_tot.chunks[s_why]++;
        s_tot.chunk_ms[s_why] += ms;
        if (ms > s_tot.chunk_ms_max) s_tot.chunk_ms_max = ms;
    }
}

static void render_chunk(Chunk *c)
{
    int X0 = c->ci * CHUNK_W, Y0 = c->cj * CHUNK_H;
    render_region(c, X0, Y0, X0 + CHUNK_W, Y0 + CHUNK_H, 1);
}

/* shimmer: redraw only the water box when the chunk needs nothing else and the box is small enough */
static void render_chunk_anim(Chunk *c)
{
    long area = (long)(c->wx1 - c->wx0) * (c->wy1 - c->wy0);
    if ((c->flags & ~CF_ANIM) || !c->water || c->wx1 <= c->wx0 || c->wy1 <= c->wy0 || area * 4 > (long)CHUNK_W * CHUNK_H * 3) {
        render_chunk(c);
        return;
    }
    render_region(c, c->wx0, c->wy0, c->wx1, c->wy1, 0);
}

/* ================================================================== pool */
static void index_setup(void)
{
    int w, h, i;
    iso_world_size(g_cam.zoom, &w, &h);
    s_cols = (w + CHUNK_W - 1) / CHUNK_W;
    s_rows = (h + CHUNK_H - 1) / CHUNK_H;
    if (s_cols * s_rows > IDX_MAX) {
        log_printf("render: warning: chunk grid %dx%d too large, clipped", s_cols, s_rows);
        s_rows = IDX_MAX / s_cols;
    }
    for (i = 0; i < IDX_MAX; i++) s_idx[i] = -1;
    for (i = 0; i < s_npool; i++) { s_pool[i].ci = -1; s_pool[i].flags = 0; }
    s_idxZoom = g_cam.zoom;
}

/* hard: forget positions (zoom/map change); soft: keep slots, everything must re-render before use */
static void flush_all(int hard)
{
    int i;
    if (hard || s_idxZoom != g_cam.zoom) { index_setup(); return; }
    for (i = 0; i < s_npool; i++) if (s_pool[i].ci >= 0) s_pool[i].flags |= CF_INVALID;
}

static int acquire(int ci, int cj, int allow_visible_evict)
{
    int i, best = -1;
    for (i = 0; i < s_npool; i++) if (s_pool[i].ci < 0) { best = i; break; }
    if (best < 0) {
        for (i = 0; i < s_npool; i++) {
            if (s_pool[i].last >= s_frame && !allow_visible_evict) continue;
            if (best < 0 || s_pool[i].last < s_pool[best].last) best = i;
        }
        if (best < 0) return -1;
        s_idx[s_pool[best].cj * s_cols + s_pool[best].ci] = -1;
    }
    s_pool[best].ci = ci; s_pool[best].cj = cj;
    s_pool[best].flags = CF_INVALID;
    s_pool[best].last = s_frame;
    s_idx[cj * s_cols + ci] = (s16)best;
    return best;
}

static void mark_rect(int wx0, int wy0, int wx1, int wy1, int flag)
{
    int i;
    for (i = 0; i < s_npool; i++) {
        Chunk *c = &s_pool[i];
        int X0, Y0;
        if (c->ci < 0) continue;
        X0 = c->ci * CHUNK_W; Y0 = c->cj * CHUNK_H;
        if (wx1 <= X0 || wx0 >= X0 + CHUNK_W || wy1 <= Y0 || wy0 >= Y0 + CHUNK_H) continue;
        c->flags |= flag;
    }
}

static void on_world_change(int x0, int y0, int x1, int y1, int kinds, void *user)
{
    int wx0, wy0, wx1, wy1, flag;
    UNUSED(user);
    if (!s_inited) return;
    if (kinds & ~(WCHG_NET | WCHG_FIELDS | WCHG_PIPE)) flag = CF_DIRTY;    /* F1: pipes only show in overlays */
    else if (s_ovl != OVL_NONE) flag = CF_OVL;
    else return;
    if (flag == CF_DIRTY && x0 <= 0 && y0 <= 0 && x1 >= g_world.w - 1 && y1 >= g_world.h - 1 && (kinds & (WCHG_TERRAIN | WCHG_LOT)) == (WCHG_TERRAIN | WCHG_LOT)) {
        flush_all(0);                         /* new map / load */
        return;
    }
    /* +1 tile: shore corners, power-line masks next to plants, road masks; F3 zone view: the outer tile's band */
    iso_tile_rect_bounds(g_cam.zoom, x0 - 1, y0 - 1, x1 + 1, y1 + 1, &wx0, &wy0, &wx1, &wy1);
    mark_rect(wx0, wy0, wx1, wy1, flag);
}

/* ================================================================== init */
static int pick_id(const char *name) { return art_find(name); }

static void resolve_ids(void)
{
    static const char *const fk[FOREST_COUNT] = { "mixed", "spruce", "birch", "pine" };
    static const char *const trees[6] = { "birch", "spruce", "pine", "birch_au", "rowan", "apple" };
    int i, k, m;
    char buf[48];
    for (i = 0; i < 3; i++) { s_idGrass[i] = art_findf("grass%d", i); s_idWater[i] = art_findf("water%d", i); }
    for (m = 0; m < 16; m++)
        for (k = 0; k < 3; k++) {
            if (m == 0 || m == 15) { s_idShore[m][k] = s_idWater[k]; continue; }
            if (k == 0) s_idShore[m][k] = art_findf("shore%02d", m);
            else {
                str_printf(buf, sizeof(buf), "shore%02d_%d", m, k);
                s_idShore[m][k] = art_exists(buf) ? art_find(buf) : s_idShore[m][0];
            }
        }
    for (k = 0; k < 2; k++)
        for (m = 0; m < 3; m++)
            for (i = 0; i < 3; i++) {
                str_printf(buf, sizeof(buf), "%s_%c_%d", k ? "rapids" : "river", "xyn"[m], i);
                s_idRiver[k][m][i] = art_exists(buf) ? art_find(buf) : s_idWater[i];
            }
    s_idCrop = pick_id("field_crop"); s_idPlough = pick_id("field_plough"); s_idMarsh = pick_id("marsh");
    s_idRock[0] = pick_id("rock0"); s_idRock[1] = art_exists("rock1") ? pick_id("rock1") : s_idRock[0];
    for (k = 0; k < FOREST_COUNT; k++)
        for (i = 0; i < 3; i++) {
            str_printf(buf, sizeof(buf), "forest_%s%d", fk[k], i);
            s_idForest[k][i] = art_exists(buf) ? art_find(buf) : art_findf("forest_%s", fk[k]);
        }
    for (i = 0; i < 6; i++) s_idTree[i] = art_findf("tree_%s", trees[i]);
    for (k = 0; k < 3; k++)                 /* M3: forest stages, fallback = mature mixed forest */
        for (i = 0; i < 2; i++) {
            static const char *const stn[3] = { "cut", "seed", "young" };
            str_printf(buf, sizeof(buf), "forest_%s%d", stn[k], i);
            s_idStage[k][i] = art_exists(buf) ? art_find(buf) : s_idForest[FOREST_MIXED][i];
        }
    s_idHay = art_exists("field_hay") ? art_find("field_hay") : -1;
    for (i = 0; i < 2; i++)                   /* M5: ruins; fallback = the bare construction ground */
        s_idRubble[i] = art_exists(i ? "rubble1" : "rubble0") ? art_find(i ? "rubble1" : "rubble0") : art_find("build_1x1_r0");
    for (k = 0; k < ROAD_CLASS_COUNT; k++)
        for (m = 0; m < 16; m++) s_idRoad[k][m] = k ? art_findf("%s_%02d", g_roads[k].key, m) : ART_MISSING;
    for (k = 1; k < ROAD_CLASS_COUNT; k++) {
        s_idBridge[k][0] = art_findf("%s_bridge_05", g_roads[k].key);
        s_idBridge[k][1] = art_findf("%s_bridge_10", g_roads[k].key);
    }
    for (k = 0; k < ROAD_CLASS_COUNT; k++)          /* 1.2 X1: crossings (silent: X2 art may not exist yet) */
        for (i = 0; i < 2; i++) {
            static const char *const mm[2] = { "05", "10" };
            s_idXing[k][i] = -1;
            if (k == ROAD_NONE || k == ROAD_RAIL) continue;
            if (k == ROAD_MOTORWAY) str_printf(buf, sizeof(buf), "mway_rail_%s", mm[i]);
            else str_printf(buf, sizeof(buf), "%s_xing_%s", g_roads[k].key, mm[i]);
            if (!art_exists(buf) && k != ROAD_MOTORWAY) str_printf(buf, sizeof(buf), "rail_xing_%s", mm[1 - i]);
            if (art_exists(buf)) s_idXing[k][i] = art_find(buf);
        }
    for (i = 0; i < 2; i++) s_idRailPower[i] = art_exists(i ? "rail_power_10" : "rail_power_05") ? art_find(i ? "rail_power_10" : "rail_power_05") : -1;
    for (m = 0; m < 16; m++) { s_idPower[m] = art_findf("power_%02d", m); s_idWire[m] = m ? art_findf("wire_%02d", m) : -1; }
    for (k = 0; k < OVR_COUNT; k++) for (i = 0; i < 8; i++) s_idOvl[k][i] = art_findf("ovl_%d_%d", k, i);
    for (m = 0; m < 16; m++)
        for (k = 0; k < 3; k++) {
            static const char *const pst[3] = { "wet", "short", "dry" };
            sprintf(buf, "pipe_%02d_%s", m, pst[k]);
            s_idPipe[m][k] = art_exists(buf) ? art_find(buf) : -1;   /* no art: the overlay diamond alone */
        }
    s_idZone[0] = -1;
    for (k = 1; k < ZONE_COUNT; k++) {      /* M3: forestry/farm tints have no art yet: the zone overlay diamond */
        str_printf(buf, sizeof(buf), "zone_%d", k);
        s_idZone[k] = art_exists(buf) ? art_find(buf) : art_findf("ovl_%d_%d", OVR_ZONE, k);
    }
    for (k = 0; k < 16; k++)             /* F3: zone view front-edge bands (zones without art: no band) */
        for (i = 0; i < 2; i++) {
            sprintf(buf, "zonev_%d_%s", k, i ? "04" : "02");
            s_idZoneV[k][i] = (k > 0 && art_exists(buf)) ? art_find(buf) : -1;
        }
    for (i = 0; i < B_COUNT * 16 * 4 * 4; i++) (&s_lotCache[0][0][0][0])[i] = -1;
}

int render_init(void)
{
    int vw, vh, vc, vr, vis, want, freeKb, n, i;
    if (s_inited) return 1;
    iso_init(g_world.w > 0 ? g_world.w : MAP_DEF_W, g_world.h > 0 ? g_world.h : MAP_DEF_H);
    if (g_cam.view_w <= 0) cam_set_view(0, 0, gfx_screen_w(), gfx_screen_h());
    resolve_ids();
    s_bg = gfx_rgb(40, 44, 40);
    vw = g_cam.view_w; vh = g_cam.view_h;
    vc = (vw + CHUNK_W - 1) / CHUNK_W + 1;
    vr = (vh + CHUNK_H - 1) / CHUNK_H + 1;
    vis = vc * vr;
    want = vis + vc + vr + 1;
    if (want > MAX_POOL) want = MAX_POOL;
    freeKb = gfx_vram_free_kb();
    if (art_resident_zoom() >= 0) s_residency = 1;   /* DESIGN 14.5: art_init loads one zoom only (orchestrator fix) */
    if (freeKb >= 0 && freeKb - want * 64 < RESERVE_KB && art_resident_zoom() < 0) {
        log_printf("render: VRAM free %d KB is short for %d chunks + %d KB reserve: one zoom's art resident at a time", freeKb, want, RESERVE_KB);
        s_residency = 1;
        if (!art_set_resident_zoom(ZOOM_64)) return 0;          /* size the pool for the bigger set */
        freeKb = gfx_vram_free_kb();
    }
    n = want;
    if (freeKb >= 0 && (freeKb - RESERVE_KB) / 64 < n) n = IMAX(vis, (freeKb - RESERVE_KB) / 64);
    for (s_npool = 0; s_npool < n; s_npool++) {
        memset(&s_pool[s_npool], 0, sizeof(Chunk));
        if (!gfx_make_target(CHUNK_W, CHUNK_H, &s_pool[s_npool].img)) break;
        s_pool[s_npool].ci = -1;
    }
    if (s_npool < vis) log_printf("render: error: only %d chunk targets (visible needs %d): holes will show", s_npool, vis);
    if (s_residency && g_cam.zoom != ZOOM_64 && !art_set_resident_zoom(g_cam.zoom)) return 0;
    for (i = 0; i < 4; i++) memset(&s_ghost[i], 0, sizeof(Ghost));
    if (!s_listener) { world_add_listener(on_world_change, NULL); s_listener = 1; }
    s_serial = gfx_restore_serial();
    s_inited = 1;
    s_st.chunks_pool = s_npool;          /* F3: for game.cpp's VRAM budget line at start */
    index_setup();
    log_printf("render: view %dx%d, visible up to %d chunks, pool %d of %d wanted (%d KB), residency %s, art %d KB, VRAM free %d KB",
               vw, vh, vis, s_npool, want, s_npool * CHUNK_W * CHUNK_H * 2 / 1024, s_residency ? "per zoom" : "both zooms",
               art_vram_kb(), gfx_vram_free_kb());
    return s_npool > 0;
}

void render_shutdown(void)
{
    int i;
    if (s_listener) { world_remove_listener(on_world_change, NULL); s_listener = 0; }
    for (i = 0; i < s_npool; i++) gfx_free_img(&s_pool[i].img);
    for (i = 0; i < 4; i++) if (s_ghost[i].img.w) gfx_free_img(&s_ghost[i].img);
    s_npool = 0;
    s_inited = 0;
}

void render_reset(void)
{
    iso_init(g_world.w, g_world.h);
    if (s_inited) index_setup();
    s_nspr = 0;
}

void render_set_zoom(int zoom, int sx, int sy)
{
    if (zoom < 0 || zoom >= ZOOM_COUNT || zoom == g_cam.zoom) return;
    if (!art_zoom_available(zoom)) return;     /* F3: that zoom's art does not fit at this resolution */
    if (s_residency) {                         /* PL4: drop the ghost copies (per zoom anyway) so they leave no hole
                                                  between the art pages that the swap frees and re-creates */
        int i;
        for (i = 0; i < 4; i++) if (s_ghost[i].img.w) { gfx_free_img(&s_ghost[i].img); memset(&s_ghost[i], 0, sizeof(Ghost)); }
    }
    if (s_residency && !art_set_resident_zoom(zoom)) { log_printf("render: error: zoom %d art not resident", zoom); return; }
    cam_set_zoom(zoom, sx, sy);
    if (s_inited) index_setup();
    s_lastCamX = g_cam.x; s_lastCamY = g_cam.y;
}

void render_set_overlay(int ovl, int param, int show_buildings)
{
    if (ovl == s_ovl && param == s_ovlParam && !!show_buildings == s_ovlShow) return;
    if (ovl == s_ovl && !!show_buildings == s_ovlShow && s_inited) {   /* F1: only the param (highlighted source) */
        int i;
        s_ovlParam = param;
        for (i = 0; i < s_npool; i++) if (s_pool[i].ci >= 0) s_pool[i].flags |= CF_OVL;
        return;
    }
    s_ovl = ovl; s_ovlParam = param; s_ovlShow = !!show_buildings;
    if (s_inited) flush_all(0);
}

int render_overlay(void) { return s_ovl; }

/* F3: persistent zone view; baked into the chunks, so a toggle re-renders them (one slow frame) */
void render_set_zone_view(int on)
{
    if (!!on == s_zoneView) return;
    s_zoneView = !!on;
    if (s_inited) flush_all(0);
    log_printf("render: zone view %s", s_zoneView ? "on" : "off");
}
int render_zone_view(void) { return s_zoneView; }

void render_set_anim_frame(int frame)
{
    int i;
    frame = ((frame % 3) + 3) % 3;
    if (frame == s_anim) return;
    s_anim = frame;
    for (i = 0; i < s_npool; i++) if (s_pool[i].ci >= 0 && s_pool[i].water) s_pool[i].flags |= CF_ANIM;
}

void render_invalidate_tiles(int x0, int y0, int x1, int y1)
{
    int wx0, wy0, wx1, wy1;
    iso_tile_rect_bounds(g_cam.zoom, x0, y0, x1, y1, &wx0, &wy0, &wx1, &wy1);
    mark_rect(wx0, wy0, wx1, wy1, CF_DIRTY);
}

void render_invalidate_all(void) { if (s_inited) flush_all(0); }

/* ================================================================== per frame */
typedef struct { int ci0, ci1, cj0, cj1; } VRange;

static void visible_range(VRange *v, int clampit)
{
    v->ci0 = FLOORDIV(g_cam.x, CHUNK_W);
    v->ci1 = FLOORDIV(g_cam.x + g_cam.view_w - 1, CHUNK_W);
    v->cj0 = FLOORDIV(g_cam.y, CHUNK_H);
    v->cj1 = FLOORDIV(g_cam.y + g_cam.view_h - 1, CHUNK_H);
    if (clampit) {
        if (v->ci0 < 0) v->ci0 = 0;
        if (v->cj0 < 0) v->cj0 = 0;
        if (v->ci1 > s_cols - 1) v->ci1 = s_cols - 1;
        if (v->cj1 > s_rows - 1) v->cj1 = s_rows - 1;
    }
}

static int chunk_dist(int ci, int cj)        /* to the view centre, in px (Manhattan) */
{
    int cx = g_cam.x + g_cam.view_w / 2, cy = g_cam.y + g_cam.view_h / 2;
    return IABS(ci * CHUNK_W + CHUNK_W / 2 - cx) + 2 * IABS(cj * CHUNK_H + CHUNK_H / 2 - cy);
}

static double s_t0;
static int s_repairs;
static int over_budget(double budget)
{
    return s_repairs >= (g_cfg.max_repairs > 0 ? g_cfg.max_repairs : MAX_REPAIRS) || time_ms() - s_t0 + s_avgChunkMs[g_cam.zoom] * 0.5 > budget;
}

/* render the best visible chunk carrying `flag`; returns 0 when none is left */
static int repair_one(const VRange *v, int flag)
{
    int i, j, best = -1, bestD = 0x7fffffff;
    for (j = v->cj0; j <= v->cj1; j++)
        for (i = v->ci0; i <= v->ci1; i++) {
            int k = s_idx[j * s_cols + i], dd;
            if (k < 0 || !(s_pool[k].flags & flag)) continue;
            dd = flag == CF_ANIM ? s_pool[k].rendered : chunk_dist(i, j);
            if (dd < bestD) { bestD = dd; best = k; }
        }
    if (best < 0) return 0;
    if (flag == CF_OVL && !(s_pool[best].flags & (CF_DIRTY | CF_ANIM | CF_INVALID))) {
        Chunk *c = &s_pool[best];
        u32 h = overlay_hash(g_cam.zoom, c->ci * CHUNK_W, c->cj * CHUNK_H, (c->ci + 1) * CHUNK_W, (c->cj + 1) * CHUNK_H, 0);
        s_tot.ovl_checks++;
        if (h == c->ovl_hash) { c->flags &= ~CF_OVL; return 1; }
    }
    s_why = flag == CF_ANIM ? RR_ANIM : flag == CF_OVL ? RR_OVERLAY : RR_DIRTY;
    if (flag == CF_ANIM) render_chunk_anim(&s_pool[best]);
    else render_chunk(&s_pool[best]);
    s_repairs++;
    return 1;
}

static int s_mmReady;                     /* minimap colours (section "minimap") */
static int s_mmStep;                      /* 1.1 B1: the next step of the colour build (mm_init_step) */

/* 1.1 W2: the season to show (INI winter_art=0 / missing pages: art_set_season does nothing). Polled every frame;
   the swap re-uploads the art pages into their surfaces, then every cached picture made from the old pixels goes:
   chunks (visible ones re-render this frame, the rest as they come into view), ghost copies, minimap colours. */
static void season_poll(void)
{
    int want, ch = season_changed();
    double t0;
    want = g_cfg.force_season >= 0 ? g_cfg.force_season : season_is_winter();
    if (!g_cfg.winter_art) want = 0;          /* 1.1 B1: Settings "Winter scenery" off: summer art from the next frame */
    if (want == art_season() || !art_winter_available()) { art_set_season(want); return; }   /* (cancels a pending step) */
    t0 = time_ms();
    if (art_set_season(want)) {               /* 0 after step 1 (px swapped, VRAM unchanged): again next frame */
        int i;
        for (i = 0; i < 4; i++) if (s_ghost[i].img.w) { gfx_free_img(&s_ghost[i].img); memset(&s_ghost[i], 0, sizeof(Ghost)); }
        s_mmReady = 0; s_mmStep = 0;         /* 1.1 B1: minimap colours rebuilt in steps (render_minimap_prepare) */
        flush_all(0);
        log_printf("render: season %s (hook %d): art swap + cache flush %.1f ms", want ? "winter" : "summer", ch, time_ms() - t0);
    }
}

void render_update(double budget_ms)
{
    VRange v;
    int i, j, k, pass;
    if (!s_inited) return;
    season_poll();                            /* 1.1 W2 */
    s_frame++;
    s_st.chunks_rendered = 0; s_st.chunk_ms = 0;
    if (gfx_restore_serial() != s_serial) {
        s_serial = gfx_restore_serial();
        log_printf("render: surfaces restored (serial %d): re-rendering all chunks", s_serial);
        flush_all(0);
    }
    if (s_idxZoom != g_cam.zoom) index_setup();
    s_dirX = g_cam.x > s_lastCamX ? 1 : g_cam.x < s_lastCamX ? -1 : 0;
    s_dirY = g_cam.y > s_lastCamY ? 1 : g_cam.y < s_lastCamY ? -1 : 0;
    s_lastCamX = g_cam.x; s_lastCamY = g_cam.y;
    visible_range(&v, 1);
    s_t0 = time_ms();
    s_repairs = 0;
    /* 1. visible: touch, render missing/invalid now */
    for (j = v.cj0; j <= v.cj1; j++)
        for (i = v.ci0; i <= v.ci1; i++) {
            k = s_idx[j * s_cols + i];
            if (k < 0) k = acquire(i, j, 0);
            if (k < 0) continue;
            s_pool[k].last = s_frame;
            if (s_pool[k].flags & CF_INVALID) { s_why = RR_VISIBLE; render_chunk(&s_pool[k]); }
        }
    /* 2./3. dirty, then overlay checks, nearest first */
    for (pass = 0; pass < 2; pass++)
        while (!over_budget(budget_ms) && repair_one(&v, pass ? CF_OVL : CF_DIRTY)) {}
    /* 4. prefetch the margin in the scroll direction */
    if (s_dirX || s_dirY) {
        int pi0 = v.ci0 - 1, pi1 = v.ci1 + 1, pj0 = v.cj0 - 1, pj1 = v.cj1 + 1;
        for (;;) {
            int best = -1, bi = 0, bj = 0, bestD = 0x7fffffff;
            if (over_budget(budget_ms)) break;
            for (j = pj0; j <= pj1; j++)
                for (i = pi0; i <= pi1; i++) {
                    int edge = (s_dirX > 0 && i == pi1) || (s_dirX < 0 && i == pi0) || (s_dirY > 0 && j == pj1) || (s_dirY < 0 && j == pj0);
                    int dd;
                    if (!edge || i < 0 || j < 0 || i >= s_cols || j >= s_rows) continue;
                    k = s_idx[j * s_cols + i];
                    if (k >= 0 && !(s_pool[k].flags & (CF_INVALID | CF_DIRTY))) { s_pool[k].last = s_frame; continue; }
                    dd = chunk_dist(i, j);
                    if (dd < bestD) { bestD = dd; best = k; bi = i; bj = j; }
                }
            if (bestD == 0x7fffffff) break;
            if (best < 0) best = acquire(bi, bj, 0);
            if (best < 0) break;
            s_pool[best].last = s_frame;
            s_why = RR_PREFETCH;
            render_chunk(&s_pool[best]);
            s_repairs++;
        }
    }
    /* 5. water shimmer */
    while (!over_budget(budget_ms) && repair_one(&v, CF_ANIM)) {}
    s_tot.frames++;
    if (s_st.chunk_ms > s_tot.frame_chunk_ms_max) s_tot.frame_chunk_ms_max = s_st.chunk_ms;
    s_st.chunks_pool = s_npool;
    s_st.chunks_valid = s_st.chunks_dirty = 0;
    for (i = 0; i < s_npool; i++) {
        if (s_pool[i].ci < 0) continue;
        if (s_pool[i].flags) s_st.chunks_dirty++; else s_st.chunks_valid++;
    }
}

void render_world(void)
{
    VRange v;
    int i, j, k, x, y;
    if (!s_inited) return;
    gfx_set_target(NULL);
    gfx_set_clip(g_cam.view_x, g_cam.view_y, g_cam.view_x + g_cam.view_w, g_cam.view_y + g_cam.view_h);
    visible_range(&v, 0);
    for (j = v.cj0; j <= v.cj1; j++)
        for (i = v.ci0; i <= v.ci1; i++) {
            x = g_cam.view_x + i * CHUNK_W - g_cam.x;
            y = g_cam.view_y + j * CHUNK_H - g_cam.y;
            k = (i >= 0 && j >= 0 && i < s_cols && j < s_rows) ? s_idx[j * s_cols + i] : -1;
            if (k >= 0 && !(s_pool[k].flags & CF_INVALID)) gfx_blit(&s_pool[k].img, x, y);
            else gfx_fill(x, y, x + CHUNK_W, y + CHUNK_H, s_bg);
        }
    gfx_reset_clip();
}

/* ================================================================== dynamic sprites */
void render_sprite(int art_id, int wx, int wy, int depth_x, int depth_y, int flags)
{
    RSprite *s;
    if (s_nspr >= RENDER_MAX_SPRITES) { s_st.sprites_dropped++; return; }
    s = &s_spr[s_nspr++];
    s->id = (s16)art_id; s->wx = wx; s->wy = wy;
    s->key = (depth_x + depth_y) * 4 + SUB_DYN;
    s->flags = (u16)flags;
}

void render_tile_mark(int art_id, int x, int y)
{
    int wx, wy;
    south_vertex(g_cam.zoom, x, y, &wx, &wy);
    render_sprite(art_id, wx, wy, x, y, RS_NO_OCCLUDE);
}

void render_lot_ghost(int art_id, int x, int y, int w, int h)
{
    int wx, wy;
    south_vertex(g_cam.zoom, x + w - 1, y + h - 1, &wx, &wy);
    render_sprite(art_id, wx, wy, x + w - 1, y + h - 1, RS_NO_OCCLUDE | RS_HALF);
}

static const Img *ghost_img(int z, int id)
{
    int i, best = 0, x, y, pitch;
    const ArtSprite *sp;
    const PIX *src;
    Ghost *g;
    PIX key = gfx_key();
    static int fail_id = -1, fail_z = -1;     /* F3: no VRAM for this ghost: don't retry (and log) every frame */
    for (i = 0; i < 4; i++)
        if (s_ghost[i].img.w && s_ghost[i].id == id && s_ghost[i].zoom == z) { s_ghost[i].last = s_frame; return &s_ghost[i].img; }
    if (id == fail_id && z == fail_z) return NULL;
    for (i = 1; i < 4; i++) if (s_ghost[i].last < s_ghost[best].last) best = i;
    g = &s_ghost[best];
    if (g->img.w) gfx_free_img(&g->img);
    sp = art_get(z, id);
    src = art_pixels(z, id, &pitch);
    if (!src || !gfx_make_img(sp->w, sp->h, 1, &g->img)) { fail_id = id; fail_z = z; return NULL; }
    for (y = 0; y < sp->h; y++)
        for (x = 0; x < sp->w; x++)
            g->img.px[y * sp->w + x] = ((x + y) & 1) ? key : src[y * pitch + x];
    gfx_img_update(&g->img);
    g->id = id; g->zoom = z; g->last = s_frame;
    return &g->img;
}

/* redraw the object pieces in front of sprite s, clipped to its world rect */
static int occlude(int z, const RSprite *s, int X0, int Y0, int X1, int Y1)
{
    static Piece pc[256];
    int n, i, blits = 0, depth = s->key / 4;
    n = gather_occluders(z, X0, Y0, X1, Y1, depth, flat_mode(), pc, 256);
    for (i = 0; i < n; i++) {
        const Piece *p = &pc[i];
        const ArtSprite *sp;
        int ix0, iy0, ix1, iy1;
        if (p->key <= s->key || (p->flags & PF_NO_OCCLUDE)) continue;
        sp = art_get(z, p->id);
        ix0 = IMAX(X0, p->sx + p->c0); ix1 = IMIN(X1, p->sx + p->c1);
        iy0 = IMAX(Y0, p->sy); iy1 = IMIN(Y1, p->sy + sp->h);
        if (ix0 >= ix1 || iy0 >= iy1) continue;
        art_blit_part(z, p->id, ix0 - p->sx, iy0 - p->sy, ix1 - ix0, iy1 - iy0, ix0, iy0);   /* PL4: art pieces */
        blits++;
    }
    return blits;
}

/* T13 self-check (-test render / -test life): the tight occluder search must find exactly the occluding
   pieces the wide search finds. Returns the number of sprites whose occluder sets differ. */
static int occl_key(const Piece *p) { return p->id * 7919 + p->c0 * 131 + p->sx * 31 + p->sy; }
static int occluders_selfcheck(void)
{
    static Piece pa[256], pb[256];
    int z = g_cam.zoom, i, j, k, bad = 0;
    for (i = 0; i < s_nspr; i++) {
        const RSprite *s = &s_spr[i];
        const ArtSprite *sp;
        int X0, Y0, na, nb, ca = 0, cb = 0;
        u32 ha = 0, hb = 0;
        if (s->flags & RS_NO_OCCLUDE) continue;
        sp = art_get(z, s->id);
        X0 = s->wx - sp->ax; Y0 = s->wy - sp->ay;
        na = gather_pieces(z, X0, Y0, X0 + sp->w, Y0 + sp->h, s->key / 4, flat_mode(), pa, 256);
        nb = gather_occluders(z, X0, Y0, X0 + sp->w, Y0 + sp->h, s->key / 4, flat_mode(), pb, 256);
        for (j = 0; j < na; j++) if (pa[j].key > s->key && !(pa[j].flags & PF_NO_OCCLUDE)) { ca++; ha += (u32)occl_key(&pa[j]) * 2654435761u; }
        for (k = 0; k < nb; k++) if (pb[k].key > s->key && !(pb[k].flags & PF_NO_OCCLUDE)) { cb++; hb += (u32)occl_key(&pb[k]) * 2654435761u; }
        if (ca != cb || ha != hb) {
            if (bad < 5) log_printf("render: occluder self-check: sprite %s at %d,%d key %d: wide %d, tight %d", art_name(s->id), s->wx, s->wy, s->key, ca, cb);
            bad++;
        }
    }
    return bad;
}
static int s_occlCheck, s_occlChecked, s_occlBad;

void render_flush_sprites(void)
{
    static s16 order[RENDER_MAX_SPRITES];
    int i, j, n = 0, z = g_cam.zoom;
    s_st.sprites = s_nspr;
    s_st.occlusion_blits = 0;
    if (!s_inited || !s_nspr) { s_nspr = 0; return; }
    if (s_occlCheck) { s_occlBad += occluders_selfcheck(); s_occlChecked += s_nspr; }
    gfx_set_target(NULL);
    gfx_set_clip(g_cam.view_x, g_cam.view_y, g_cam.view_x + g_cam.view_w, g_cam.view_y + g_cam.view_h);
    gfx_set_origin(g_cam.x - g_cam.view_x, g_cam.y - g_cam.view_y);   /* draw in world coordinates */
    for (i = 0; i < s_nspr; i++) if (!(s_spr[i].flags & RS_NO_OCCLUDE)) order[n++] = (s16)i;
    for (i = 1; i < n; i++) {                                        /* stable insertion sort by key */
        s16 t = order[i];
        for (j = i - 1; j >= 0 && s_spr[order[j]].key > s_spr[t].key; j--) order[j + 1] = order[j];
        order[j + 1] = t;
    }
    for (i = 0; i < n; i++) {
        const RSprite *s = &s_spr[order[i]];
        const ArtSprite *sp = art_get(z, s->id);
        int X0 = s->wx - sp->ax, Y0 = s->wy - sp->ay;
        if (X0 >= g_cam.x + g_cam.view_w || Y0 >= g_cam.y + g_cam.view_h || X0 + sp->w <= g_cam.x || Y0 + sp->h <= g_cam.y) continue;
        art_draw(z, s->id, s->wx, s->wy);
        s_st.occlusion_blits += occlude(z, s, X0, Y0, X0 + sp->w, Y0 + sp->h);
    }
    for (i = 0; i < s_nspr; i++) {
        const RSprite *s = &s_spr[i];
        if (!(s->flags & RS_NO_OCCLUDE)) continue;
        if (s->flags & RS_HALF) {
            const ArtSprite *sp = art_get(z, s->id);
            const Img *g = ghost_img(z, s->id);
            if (g) gfx_blit(g, s->wx - sp->ax, s->wy - sp->ay);
            else art_draw(z, s->id, s->wx, s->wy);    /* F3: VRAM short: a solid ghost instead of none */
        } else art_draw(z, s->id, s->wx, s->wy);
    }
    gfx_reset_clip();
    gfx_set_origin(0, 0);
    s_tot.sprites += s_nspr;
    s_tot.occlusion_blits += s_st.occlusion_blits;
    s_nspr = 0;
}

int render_lot_art(int lot)
{
    const Lot *l = world_lot(lot);
    return l ? lot_sprite_id(l, 0) : ART_MISSING;
}

/* ================================================================== picking */
int render_pick_tile(int sx, int sy, int *x, int *y) { return cam_pick(sx, sy, x, y); }

int render_pick_lot(int sx, int sy)
{
    static Piece pc[256];
    int z = g_cam.zoom, wx, wy, n, i, x, y;
    if (!s_inited) return 0;
    wx = g_cam.x + sx - g_cam.view_x; wy = g_cam.y + sy - g_cam.view_y;
    n = gather_pieces(z, wx, wy, wx + 1, wy + 1, 0, flat_mode(), pc, 256);
    for (i = n - 1; i >= 0; i--) {
        const Piece *p = &pc[i];
        if (!art_opaque(z, p->id, wx - p->sx, wy - p->sy)) continue;
        if (p->lot) return p->lot;
    }
    if (!cam_pick(sx, sy, &x, &y)) return 0;
    return world_lot_at(x, y);
}

/* ================================================================== minimap */
static PIX s_mmTer[TER_COUNT][4], s_mmRoad[ROAD_CLASS_COUNT], s_mmZone[ZONE_COUNT], s_mmLine, s_mmBg;
static PIX s_mmOvl[OVR_COUNT][8];
static PIX s_mmLot[B_COUNT][3];

static PIX avg_color(int id)
{
    int pitch, x, y, r, g, b, n = 0, sr = 0, sg = 0, sb = 0;
    const ArtSprite *sp = art_get(ZOOM_32, id);
    const PIX *p = art_pixels(ZOOM_32, id, &pitch);
    PIX key = gfx_key();
    if (!p) return gfx_rgb(255, 0, 200);
    for (y = 0; y < sp->h; y++)
        for (x = 0; x < sp->w; x++) {
            PIX c = p[y * pitch + x];
            if (c == key) continue;
            gfx_unpack(c, &r, &g, &b);
            sr += r; sg += g; sb += b; n++;
        }
    if (!n) return gfx_rgb(255, 0, 200);
    return gfx_rgb(sr / n, sg / n, sb / n);
}

/* 1.1 B1: the minimap colours (averages of the 32-zoom sprites, ~0.4 ms each on the P2) are built in steps: after a
   season swap ui.cpp asks render_minimap_prepare() each frame and it spends about 6 ms (the whole set cost 226 ms in
   one frame on VM B, on top of the 135 ms page upload). Step 0 terrain, 1 roads/zones/lines, 2.. overlays, then the
   buildings; render_minimap (first build, overlay switch) still does it all at once. */
static int mm_init_step(double budget_ms)
{
    int i, k, v;
    double t0 = time_ms();
    static const char *const fk[FOREST_COUNT] = { "forest_mixed0", "forest_spruce0", "forest_birch0", "forest_pine0" };
    if (s_mmReady) return 1;
    do {
        if (s_mmStep == 0) {
            for (i = 0; i < 3; i++) s_mmTer[TER_GRASS][i] = avg_color(s_idGrass[i]);
            s_mmTer[TER_GRASS][3] = s_mmTer[TER_GRASS][0];
            for (i = 0; i < 4; i++) s_mmTer[TER_FOREST][i] = avg_color(art_find(fk[i]));
            for (i = 0; i < 4; i++) { s_mmTer[TER_ROCK][i] = avg_color(s_idRock[0]); s_mmTer[TER_WATER][i] = s_mmTer[TER_RIVER][i] = avg_color(s_idWater[0]); }
            s_mmTer[TER_FIELD][0] = s_mmTer[TER_FIELD][2] = avg_color(s_idCrop);
            s_mmTer[TER_FIELD][1] = s_mmTer[TER_FIELD][3] = avg_color(s_idPlough);
            for (i = 0; i < 4; i++) s_mmTer[TER_MARSH][i] = avg_color(s_idMarsh);
        } else if (s_mmStep == 1) {
            s_mmRoad[0] = 0;
            for (k = 1; k < ROAD_CLASS_COUNT; k++) s_mmRoad[k] = gfx_scale(avg_color(s_idRoad[k][5]), 230);
            for (k = 1; k < ZONE_COUNT; k++) s_mmZone[k] = gfx_mix(s_mmTer[TER_GRASS][0], avg_color(s_idZone[k]), 128);
            s_mmLine = gfx_rgb(90, 86, 80);
            s_mmBg = gfx_rgb(40, 44, 40);
        } else if (s_mmStep < 2 + OVR_COUNT) {
            k = s_mmStep - 2;
            for (v = 0; v < 8; v++) s_mmOvl[k][v] = avg_color(s_idOvl[k][v]);
        } else {
            const BuildingDef *d;
            i = s_mmStep - 2 - OVR_COUNT;
            d = def_building(i);
            if (!d->variants) s_mmLot[i][0] = s_mmLot[i][1] = s_mmLot[i][2] = gfx_rgb(150, 140, 130);
            else {
                s_mmLot[i][0] = avg_color(art_lot_sprite(d->key, 0, 0, ART_LOT_NORMAL, d->w, d->h));
                s_mmLot[i][1] = gfx_scale(gfx_mix(s_mmLot[i][0], gfx_rgb(128, 128, 128), 160), 200);
                s_mmLot[i][2] = avg_color(art_lot_sprite(d->key, 0, 0, ART_LOT_BUILDING, d->w, d->h));
            }
            if (i == B_COUNT - 1) { s_mmReady = 1; s_mmStep = 0; return 1; }
        }
        s_mmStep++;
    } while (time_ms() - t0 < budget_ms);
    return 0;
}
static void mm_init(void) { while (!mm_init_step(1e9)) {} }
int render_minimap_prepare(void) { return mm_init_step(6.0); }

static PIX mm_tile(int x, int y, int ovl)
{
    const Tile *t = WT(x, y);
    PIX c;
    int cls = TILE_ROAD(t);
    if (t->lot) {
        const Lot *l = &g_world.lots[t->lot];
        c = s_mmLot[l->def][l->status == LOT_ABANDONED ? 1 : l->status == LOT_BUILDING ? 2 : 0];
    } else if (cls && !TER_IS_WATER(TER_TYPE(t))) c = s_mmRoad[cls];
    else if (cls) c = gfx_mix(s_mmRoad[cls], s_mmTer[TER_WATER][0], 96);
    else if (t->road & TR_POWER_LINE) c = s_mmLine;
    else if (TILE_ZONE(t) && !TER_IS_WATER(TER_TYPE(t))) c = s_mmZone[TILE_ZONE(t)];
    else c = s_mmTer[TER_TYPE(t)][TER_VAR(t) & 3];
    if (ovl != OVL_NONE) {
        u8 v = sim_overlay_value(ovl, s_ovl == ovl ? s_ovlParam : 0, x, y);
        if (v == OVV_NONE || OVV_RAMP(v) >= OVR_COUNT) c = gfx_scale(c, 150);
        else c = gfx_mix(c, s_mmOvl[OVV_RAMP(v)][OVV_LEVEL(v)], 200);
    }
    return c;
}

void render_minimap_tile(int w, int h, int px, int py, int *tx, int *ty)
{
    int W = IMAX(g_world.w, g_world.h);
    s32 a = (s32)(2 * py + 1) * w, b = (s32)(2 * px + 1 - w) * h, den = 2 * (s32)w * h;
    *tx = (int)FLOORDIV((a + b) * W, den);
    *ty = (int)FLOORDIV((a - b) * W, den);
}

void render_minimap_pos(int w, int h, int tx, int ty, int *px, int *py)
{
    int W = IMAX(g_world.w, g_world.h);
    *px = (int)FLOORDIV((s32)(2 * tx - 2 * ty) * w, 4 * W) + w / 2;
    *py = (int)FLOORDIV((s32)(2 * tx + 2 * ty + 2) * h, 4 * W);
}

static PIX s_halfMask;
static PIX half_mix(PIX a, PIX b) { return (PIX)(((a >> 1) & s_halfMask) + ((b >> 1) & s_halfMask)); }

void render_minimap_rows(PIX *dst, int pitch, int w, int h, int ovl, int y0, int y1)
{
    int u, v, x, y, x2, y2;
    if (!s_mmReady) mm_init();
    if (!s_halfMask) s_halfMask = (PIX)(gfx_rgb(255, 255, 255) == 0xFFFF ? 0x7BEF : 0x3DEF);
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;
    for (v = y0; v < y1; v++)
        for (u = 0; u < w; u++) {
            const Tile *t;
            PIX c;
            render_minimap_tile(w, h, u, v, &x, &y);
            if (x < 0 || y < 0 || x >= g_world.w || y >= g_world.h) { dst[v * pitch + u] = s_mmBg; continue; }
            t = WT(x, y);
            c = mm_tile(x, y, ovl);
            x2 = x + ((u + v) & 1); y2 = y + (((u + v) & 1) ^ 1);    /* second sample: 2 tiles per pixel */
            if (x2 < g_world.w && y2 < g_world.h && !TILE_ROAD(t)) {
                const Tile *t2 = WT(x2, y2);
                if (TILE_ROAD(t2) && !t->lot) c = mm_tile(x2, y2, ovl);   /* thin roads stay visible */
                else if (t2->lot != t->lot || t2->terrain != t->terrain || t2->zone != t->zone || ovl != OVL_NONE)
                    c = half_mix(c, mm_tile(x2, y2, ovl));
            }
            dst[v * pitch + u] = c;
        }
}

void render_minimap(PIX *dst, int pitch, int w, int h, int ovl) { render_minimap_rows(dst, pitch, w, h, ovl, 0, h); }

/* ================================================================== stats, dumps */
void render_stats(RenderStats *out) { *out = s_st; }
void render_totals(RenderTotals *out, int reset) { if (out) *out = s_tot; if (reset) memset(&s_tot, 0, sizeof(s_tot)); }

void render_dump_at(FILE *f, int sx, int sy)
{
    static Piece pc[256];
    int z = g_cam.zoom, wx = g_cam.x + sx - g_cam.view_x, wy = g_cam.y + sy - g_cam.view_y;
    int ci = FLOORDIV(wx, CHUNK_W), cj = FLOORDIV(wy, CHUNK_H), k = -1, x, y, n, i, on;
    if (!f) return;
    if (ci >= 0 && cj >= 0 && ci < s_cols && cj < s_rows) k = s_idx[cj * s_cols + ci];
    fprintf(f, "render_dump_at screen %d,%d = world %d,%d zoom %d (cam %d,%d view %d,%d %dx%d) overlay %d%s\n",
            sx, sy, wx, wy, z, g_cam.x, g_cam.y, g_cam.view_x, g_cam.view_y, g_cam.view_w, g_cam.view_h, s_ovl,
            flat_mode() ? " flat" : "");
    fprintf(f, "  chunk %d,%d (world %d,%d) slot %d", ci, cj, ci * CHUNK_W, cj * CHUNK_H, k);
    if (k >= 0) fprintf(f, " flags 0x%x water %d rendered frame %d (now %d) last %d", s_pool[k].flags, s_pool[k].water, s_pool[k].rendered, s_frame, s_pool[k].last);
    fprintf(f, "\n  column %d (half-tile grid, %d px)\n", FLOORDIV(wx - iso_origin_x(z), HW(z)), HW(z));
    on = iso_world_to_tile(z, wx, wy, &x, &y);
    fprintf(f, "  ground tile %d,%d%s\n", x, y, on ? "" : " (off map)");
    if (on) {
        int wat, id = ground_id(x, y, WT(x, y), &wat);
        fprintf(f, "  ground sprite %s%s\n", id >= 0 ? art_name(id) : "(covered by an object)", wat ? " (water)" : "");
        world_dump_tile(f, x, y);
    }
    n = gather_pieces(z, wx, wy, wx + 1, wy + 1, 0, flat_mode(), pc, 256);
    fprintf(f, "  %d object pieces cover this pixel, in draw order (last opaque one is visible):\n", n);
    for (i = 0; i < n; i++) {
        const Piece *p = &pc[i];
        fprintf(f, "    key %d (depth %d sub %d) %-24s anchor tile %d,%d lot %d cols %d..%d sprite px %d,%d %s\n",
                p->key, p->key / 4, p->key & 3, art_name(p->id), p->tx, p->ty, p->lot, p->c0, p->c1,
                wx - p->sx, wy - p->sy, art_opaque(z, p->id, wx - p->sx, wy - p->sy) ? "opaque" : "transparent");
    }
    fprintf(f, "  pick lot: %d\n", render_pick_lot(sx, sy));
}

/* ================================================================== bench camera, test town, -test render */
void render_bench_camera(double t)
{
    /* Lissajous over the town centre (tile 60,64), the same screen speed at both zooms (peak ~330 px/s) */
    int wx, wy, cx, cy;
    iso_tile_to_world(g_cam.zoom, 60, 66, &wx, &wy);
    cx = wx + (int)(1200.0 * sin(t * 0.27));
    cy = wy + (int)(650.0 * sin(t * 0.43 + 0.6));
    cam_scroll(cx - g_cam.view_w / 2 - g_cam.x, cy - g_cam.view_h / 2 - g_cam.y);
}

/* value noise 0..255 on a cell grid (bilinear), deterministic */
static int vnoise(int x, int y, int cell, u32 salt)
{
    int cx = FLOORDIV(x, cell), cy = FLOORDIV(y, cell), fx = x - cx * cell, fy = y - cy * cell;
    int a = (int)(hash_xy(cx, cy, salt) & 255), b = (int)(hash_xy(cx + 1, cy, salt) & 255);
    int c = (int)(hash_xy(cx, cy + 1, salt) & 255), d = (int)(hash_xy(cx + 1, cy + 1, salt) & 255);
    int top = a + (b - a) * fx / cell, bot = c + (d - c) * fx / cell;
    return top + (bot - top) * fy / cell;
}

static void road_line(int x0, int y0, int x1, int y1, int cls)
{
    int x, y;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++) world_set_road(x, y, cls);
}

static int place(const char *key, int x, int y, int rot, int variant, int status)
{
    int def = def_building_find(key), id;
    if (def == B_NONE) { log_printf("render: town: unknown building %s", key); return 0; }
    id = world_lot_place(def, x, y, rot, variant, status);
    if (id && def_building(def)->cat == BCAT_ZONE) {
        int lx0, ly0, lx1, ly1, i, j;
        world_lot_rect(id, &lx0, &ly0, &lx1, &ly1);
        for (j = ly0; j <= ly1; j++) for (i = lx0; i <= lx1; i++) world_set_zone(i, j, def_building(def)->zone);
    }
    return id;
}

static void house(int x, int y, int rot, u32 salt, int tier_bias)
{
    u32 h = hash_xy(x, y, salt);
    int tier = (int)(h % 10) < 2 ? 1 : (int)(h % 10) < 7 ? 2 : 3, status = LOT_ACTIVE;
    char key[16];
    tier = ICLAMP(tier + tier_bias, 1, 3);
    if ((h >> 8) % 23 == 0) status = LOT_ABANDONED;
    else if ((h >> 8) % 17 == 0) status = LOT_BUILDING;
    str_printf(key, sizeof(key), "house_t%d", tier);
    place(key, x, y, rot, (int)((h >> 12) & 15), status);
}

void render_build_test_town(u32 seed)
{
    int x, y, i, k, W = 128, H = 128;
    SimSettings ss;
    UNUSED(seed);
    world_new(W, H);
    /* ---- terrain: grass variants, lake NW, a strait in the east, forest belts, rock, fields */
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            Tile *t = WT(x, y);
            u32 h = hash_xy(x, y, 77);
            int lake, n = vnoise(x, y, 6, 11) - 128, f = vnoise(x, y, 9, 12), sp = vnoise(x, y, 14, 13);
            int dx = x - 40, dy = y - 36;
            lake = dx * dx * 144 + dy * dy * 256 + n * 90 < 144 * 256;
            lake |= ((x - 58) * (x - 58) + (y - 26) * (y - 26)) * 4 + n < 9 * 9 * 4;
            if (x >= 84 && x <= 86 && y < 108) lake = 1;                   /* strait (bridges) */
            if (x >= 80 && x <= 90 && y >= 102 && y <= 112 && (x - 85) * (x - 85) + (y - 107) * (y - 107) < 22) lake = 1;
            if (lake) { t->terrain = TER_WATER; continue; }
            t->terrain = (u8)(TER_GRASS | (((h & 15) < 9 ? 0 : (h & 15) < 14 ? 1 : 2) << 4));
            if (f > 150 && (IABS(x - 60) > 22 || IABS(y - 70) > 24) && !(x > 88 && y > 88))
                t->terrain = (u8)(TER_FOREST | ((sp < 80 ? FOREST_SPRUCE : sp < 130 ? FOREST_MIXED : sp < 180 ? FOREST_PINE : FOREST_BIRCH) << 4));
            else if (f < 40 && ((h >> 5) & 7) == 0) t->terrain = TER_ROCK;
            if (x >= 92 && x <= 122 && y >= 90 && y <= 120) t->terrain = (u8)(TER_FIELD | ((((x - 92) / 4) & 1) << 4));
        }
    /* a 1-tile river out of the southern pond: south, a bend east (with rapids), south to the edge */
    for (y = 112; y <= 120; y++) WT(85, y)->terrain = (u8)(TER_RIVER | (DIR_SW << 4));
    for (x = 86; x <= 100; x++) WT(x, 120)->terrain = (u8)(TER_RIVER | ((DIR_SE | ((x == 93 || x == 94) ? RIVER_RAPIDS_BIT : 0)) << 4));
    for (y = 121; y < H; y++) WT(100, y)->terrain = (u8)(TER_RIVER | (DIR_SW << 4));
    /* clean shores to the mapgen rule (mapgen.h): land and lake tiles in 2x2 blocks, no checkerboards */
    for (k = 0; k < 12 && mapgen_shore_errors(); k++)
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int wat = is_water(WT(x, y)), ok = 0, ox, oy;
                if (TER_TYPE(WT(x, y)) == TER_RIVER) continue;           /* rivers may be 1 tile wide */
                for (oy = -1; oy <= 0; oy++)
                    for (ox = -1; ox <= 0; ox++) {
                        int x0 = x + ox, y0 = y + oy;
                        if (x0 < 0 || y0 < 0 || x0 + 1 >= W || y0 + 1 >= H) continue;
                        if (is_water(WT(x0, y0)) == wat && is_water(WT(x0 + 1, y0)) == wat &&
                            is_water(WT(x0, y0 + 1)) == wat && is_water(WT(x0 + 1, y0 + 1)) == wat) ok = 1;
                    }
                if (!ok) WT(x, y)->terrain = (u8)(wat ? TER_GRASS : TER_WATER);
                if (x + 1 < W && y + 1 < H && is_water(WT(x, y)) == is_water(WT(x + 1, y + 1)) &&
                    is_water(WT(x + 1, y)) == is_water(WT(x, y + 1)) && is_water(WT(x, y)) != is_water(WT(x + 1, y)))
                    WT(x + 1, y)->terrain = WT(x, y)->terrain;                 /* break the checkerboard */
            }
    log_printf("render: test town: shore errors after cleanup %d", mapgen_shore_errors());
    world_recalc();
    world_notify_all(WCHG_ALL);
    world_batch_begin();
    /* ---- roads: national main road (x axis) with a bridge, main road south, streets, gravel lanes,
       a motorway in the east, a railway with a bridge */
    road_line(0, 64, W - 1, 64, ROAD_MAIN);
    world_set_tile_flags(0, 64, TF_EDGE_LINK, 0); world_set_tile_flags(W - 1, 64, TF_EDGE_LINK, 0);
    road_line(60, 65, 60, H - 1, ROAD_MAIN);
    road_line(52, 46, 52, 63, ROAD_STREET); road_line(68, 46, 68, 63, ROAD_STREET); road_line(76, 50, 76, 63, ROAD_STREET);
    road_line(52, 56, 76, 56, ROAD_STREET); road_line(52, 46, 68, 46, ROAD_STREET); road_line(68, 50, 82, 50, ROAD_STREET);
    road_line(44, 65, 44, 92, ROAD_GRAVEL); road_line(36, 70, 59, 70, ROAD_GRAVEL); road_line(36, 76, 59, 76, ROAD_GRAVEL);
    road_line(36, 82, 59, 82, ROAD_GRAVEL); road_line(36, 88, 59, 88, ROAD_GRAVEL); road_line(36, 70, 36, 88, ROAD_GRAVEL);
    road_line(52, 70, 52, 88, ROAD_GRAVEL); road_line(46, 58, 51, 58, ROAD_GRAVEL);
    road_line(110, 0, 110, H - 1, ROAD_MOTORWAY);
    road_line(62, 78, W - 1, 78, ROAD_RAIL);
    road_line(70, 65, 70, 77, ROAD_STREET); road_line(61, 72, 69, 72, ROAD_GRAVEL);
    world_set_road(110, 78, ROAD_MOTORWAY); road_line(70, 78, 70, 81, ROAD_STREET);   /* 1.2 X1: overpass, level crossing */
    road_line(30, 50, 51, 50, ROAD_GRAVEL);                               /* lakeside lane to the pump */
    /* ---- house streets (fronts face the lane) */
    for (x = 37; x <= 59; x++) {
        if (x == 44 || x == 52) continue;
        for (k = 0; k < 4; k++) {
            int ly = 70 + 6 * k;
            house(x, ly - 1, ROT_0, 101 + k, k == 0 ? 1 : k == 3 ? -1 : 0);
            house(x, ly + 1, ROT_2, 201 + k, k == 0 ? 1 : k == 3 ? -1 : 0);
        }
    }
    for (y = 72; y <= 86; y++) { house(43, y, ROT_1, 301, 0); house(45, y, ROT_3, 302, 0); house(51, y, ROT_1, 303, 0); house(53, y, ROT_3, 304, 0); }
    for (y = 66; y <= 90; y++) { house(59, y, ROT_1, 305, 1); house(61, y, ROT_3, 306, 1); }
    /* ---- centre: apartments, commercial, services */
    place("apt_t1_3x1", 53, 55, ROT_0, 0, LOT_ACTIVE); place("apt_t1_3x1", 56, 55, ROT_0, 1, LOT_ACTIVE);
    place("apt_t1_3x1", 60, 55, ROT_0, 2, LOT_ACTIVE); place("apt_t1_2x2", 64, 54, ROT_0, 0, LOT_ACTIVE);
    place("apt_t1_3x1", 53, 57, ROT_2, 1, LOT_ACTIVE); place("apt_t1_2x2", 56, 57, ROT_2, 1, LOT_ACTIVE);
    place("apt_t1_2x2", 58, 57, ROT_2, 2, LOT_BUILDING); place("apt_t1_3x1", 61, 57, ROT_2, 0, LOT_ABANDONED);
    place("apt_t1_3x1", 69, 51, ROT_3, 0, LOT_ACTIVE); place("apt_t1_3x1", 71, 51, ROT_2, 2, LOT_ACTIVE);
    place("apt_t2_3x1", 72, 54, ROT_0, 0, LOT_ACTIVE);
    place("com_t1_1x1", 53, 63, ROT_0, 0, LOT_ACTIVE); place("com_t1_2x1", 54, 63, ROT_0, 1, LOT_ACTIVE);
    place("com_t2_2x1", 56, 63, ROT_0, 0, LOT_ACTIVE); place("com_t2_2x2", 58, 62, ROT_0, 1, LOT_ACTIVE);
    place("com_t1_2x2", 60, 62, ROT_0, 0, LOT_ACTIVE); place("com_t2_1x1", 62, 63, ROT_0, 2, LOT_ACTIVE);
    place("com_t2_2x1", 63, 63, ROT_0, 1, LOT_ABANDONED); place("com_t1_1x1", 65, 63, ROT_0, 2, LOT_ACTIVE);
    place("com_t2_2x2", 66, 62, ROT_0, 0, LOT_ACTIVE); place("com_t1_2x1", 69, 63, ROT_0, 2, LOT_ACTIVE);
    place("com_t2_1x1", 71, 63, ROT_0, 1, LOT_ACTIVE); place("com_t1_2x2", 72, 62, ROT_0, 1, LOT_ACTIVE);
    place("com_t2_2x1", 53, 65, ROT_2, 2, LOT_ACTIVE); place("com_t1_1x1", 55, 65, ROT_2, 1, LOT_BUILDING);
    place("town_hall", 53, 59, ROT_0, 0, LOT_ACTIVE);
    place("school", 55, 59, ROT_1, 0, LOT_ACTIVE);
    place("clinic", 57, 60, ROT_0, 0, LOT_ACTIVE); place("fire_station", 58, 60, ROT_0, 0, LOT_ACTIVE);
    place("sauna", 66, 57, ROT_2, 0, LOT_ACTIVE);
    place("park", 59, 59, ROT_0, 0, LOT_ACTIVE); place("park", 60, 59, ROT_0, 1, LOT_ACTIVE); place("park", 61, 60, ROT_0, 2, LOT_ACTIVE);
    place("water_tower", 66, 47, ROT_0, 1, LOT_ACTIVE); place("water_tower", 47, 66, ROT_0, 0, LOT_ACTIVE);
    place("well", 50, 66, ROT_0, 0, LOT_ACTIVE);
    for (i = 0, k = 0; i < 400 && !k; i++) {                          /* pump on the lake shore near the lane */
        x = 30 + i % 22; y = 51 - i / 22;
        if (y > 0 && world_is_buildable(x, y) && is_water(WT(x, y - 1))) k = place("pump", x, y, ROT_2, 0, LOT_ACTIVE);
    }
    /* ---- industry by the railway, a coal plant and its line */
    place("ind_t1_2x2", 64, 79, ROT_2, 0, LOT_ACTIVE); place("ind_t2_2x2", 66, 79, ROT_2, 1, LOT_ACTIVE);
    place("ind_t1_3x3", 71, 79, ROT_2, 1, LOT_ACTIVE); place("ind_t2_3x3", 74, 79, ROT_2, 0, LOT_ACTIVE);
    place("ind_t2_2x2", 77, 79, ROT_2, 0, LOT_ABANDONED); place("ind_t1_2x2", 79, 79, ROT_2, 1, LOT_BUILDING);
    place("ind_t1_2x2", 64, 75, ROT_0, 1, LOT_ACTIVE); place("ind_t2_2x2", 66, 75, ROT_0, 0, LOT_ACTIVE);
    place("ind_t2_3x3", 71, 74, ROT_1, 1, LOT_ACTIVE); place("ind_t1_3x3", 74, 74, ROT_0, 0, LOT_ACTIVE);
    place("coal_plant", 78, 70, ROT_3, 0, LOT_ACTIVE);
    for (x = 62; x <= 77; x++) world_set_power_line(x, 70, 1);           /* crosses the street at x=70 */
    for (y = 66; y <= 69; y++) world_set_power_line(62, y, 1);
    for (y = 90; y <= 98; y++) world_set_power_line(62, y, 1);
    /* ---- east of the strait: houses along the main road, zoned land waiting, trees */
    for (x = 88; x <= 106; x++) { if ((x & 3) != 0) house(x, 63, ROT_0, 401, 0); }
    for (x = 88; x <= 104; x++) for (y = 66; y <= 68; y++) world_set_zone(x, y, (x < 96) ? ZONE_HOUSES : ZONE_COMMERCIAL);
    for (x = 40; x <= 50; x++) for (y = 92; y <= 94; y++) world_set_zone(x, y, ZONE_INDUSTRY);
    for (x = 46; x <= 50; x++) for (y = 54; y <= 57; y++) world_set_zone(x, y, ZONE_APARTMENTS);
    world_batch_end();
    /* single trees around the houses */
    for (i = 0; i < 900; i++) {
        u32 h = hash_xy(i, 7, 909);
        x = 30 + (int)(h % 70); y = 44 + (int)((h >> 8) % 56);
        if (world_is_buildable(x, y) && TER_TYPE(WT(x, y)) == TER_GRASS && !TILE_ZONE(WT(x, y))) world_set_tile_flags(x, y, TF_TREE, 0);
    }
    /* ---- simulation state for the overlays (no growth is run) */
    sim_default_settings(&ss);
    sim_new_city(&ss);
    net_solve_all();
    sim_run_months(4);                     /* occupants, coverage, a few grown lots (~0.2 s on the P2) */
    for (i = 0; i < 24; i++) { fields_step_pollution(); fields_step_land_value(i & 3); fields_step_social(i & 3); }
    log_printf("render: test town: %d lots, world hash %08x", g_world.lot_count, world_hash());
}

/* ------------------------------------------------------------------ -test render: demo life */
typedef struct { int x, y, dir, p, col, spd; } DemoCar;
#define DEMO_CARS  80
#define DEMO_PUFFS 96
static DemoCar s_cars[DEMO_CARS];
static int     s_ncars;
static struct { int lot; float age; } s_puffs[DEMO_PUFFS];
static int     s_smokeLots[32], s_nsmoke;
static int     s_carIds[12][4];

static int car_road(int x, int y)
{
    return world_in(x, y) && TILE_ROAD(WT(x, y)) && (g_roads[TILE_ROAD(WT(x, y))].flags & RDF_CARS);
}

static void demo_init(void)
{
    static const char *const dn[4] = { "ny", "px", "py", "nx" };     /* DIR_NE, SE, SW, NW */
    int i, k, d, x, y;
    for (i = 0; i < 12; i++) for (d = 0; d < 4; d++) s_carIds[i][d] = art_findf("car%d_%s", i, dn[d]);
    s_ncars = 0;
    for (i = 0; i < 4000 && s_ncars < DEMO_CARS; i++) {
        u32 h = hash_xy(i, 3, 4242);
        x = 30 + (int)(h % 90); y = 40 + (int)((h >> 8) % 70);
        if (!car_road(x, y)) continue;
        for (d = 0; d < 4; d++) if (TILE_ROAD_MASK(WT(x, y)) & (1 << ((d + (h >> 20)) & 3))) break;
        if (d == 4) continue;
        s_cars[s_ncars].x = x; s_cars[s_ncars].y = y; s_cars[s_ncars].dir = (int)((d + (h >> 20)) & 3);
        s_cars[s_ncars].p = (int)((h >> 12) & 127) - 64; s_cars[s_ncars].col = s_ncars % 12;
        s_cars[s_ncars].spd = 6 + (int)((h >> 24) & 3);
        s_ncars++;
    }
    s_nsmoke = 0;
    for (k = 1; k < g_world.lot_hi && s_nsmoke < 32; k++) {
        const Lot *l = &g_world.lots[k];
        if (l->def && (def_building(l->def)->flags & BDF_SMOKE) && l->status == LOT_ACTIVE) s_smokeLots[s_nsmoke++] = k;
    }
    memset(s_puffs, 0, sizeof(s_puffs));
}

static void demo_update(double dt)
{
    static double acc;
    int i, d, o;
    for (i = 0; i < s_ncars; i++) {
        DemoCar *c = &s_cars[i];
        c->p += c->spd;
        if (c->p < 128) continue;
        c->p -= 256;
        c->x += g_dir_dx[c->dir]; c->y += g_dir_dy[c->dir];
        if (!car_road(c->x, c->y)) { c->x -= g_dir_dx[c->dir]; c->y -= g_dir_dy[c->dir]; c->dir = DIR_OPPOSITE(c->dir); continue; }
        /* mostly straight; at junctions sometimes turn; dead end: back */
        {
            int m = TILE_ROAD_MASK(WT(c->x, c->y)) | 0, turn[3];
            o = (int)(hash_xy(c->x * 3, c->y, i + 7) & 1);
            turn[0] = (c->dir + (o ? 1 : 3)) & 3; turn[1] = (c->dir + (o ? 3 : 1)) & 3; turn[2] = c->dir;
            if (!(m & (1 << c->dir)) || (hash_xy(c->x, c->y, i) & 3) == 0) {
                for (d = 0; d < 3; d++) if (m & (1 << turn[d])) break;
                c->dir = d < 3 ? turn[d] : DIR_OPPOSITE(c->dir);
            }
        }
    }
    acc += dt;
    for (i = 0; i < DEMO_PUFFS; i++) if (s_puffs[i].lot) { s_puffs[i].age += (float)dt; if (s_puffs[i].age > 2.4f) s_puffs[i].lot = 0; }
    if (acc > 0.4) {
        acc = 0;
        for (o = 0; o < s_nsmoke; o++)
            for (i = 0; i < DEMO_PUFFS; i++) if (!s_puffs[i].lot) { s_puffs[i].lot = s_smokeLots[o]; s_puffs[i].age = 0; break; }
    }
}

static void demo_draw(int cars_on)
{
    int z = g_cam.zoom, i, wx, wy;
    if (cars_on)
        for (i = 0; i < s_ncars; i++) {
            const DemoCar *c = &s_cars[i];
            s32 fx = c->x * 256 + 128 + g_dir_dx[c->dir] * c->p, fy = c->y * 256 + 128 + g_dir_dy[c->dir] * c->p;
            switch (c->dir) {                                        /* right-hand lane (STYLE.md 9) */
            case DIR_SE: fy += 36; break;
            case DIR_NW: fy -= 36; break;
            case DIR_SW: fx -= 26; break;
            default:     fx += 26; break;
            }
            iso_pos_to_world(z, fx, fy, &wx, &wy);
            render_sprite(s_carIds[c->col][c->dir], wx, wy, (int)FLOORDIV(fx, 256), (int)FLOORDIV(fy, 256), 0);
        }
    for (i = 0; i < DEMO_PUFFS; i++) {
        const Lot *l;
        const ArtSprite *sp;
        int lx0, ly0, lx1, ly1, f;
        if (!s_puffs[i].lot) continue;
        l = &g_world.lots[s_puffs[i].lot];
        sp = art_get(z, lot_sprite_id(l, 0));
        if (sp->hx < 0) continue;
        world_lot_rect(s_puffs[i].lot, &lx0, &ly0, &lx1, &ly1);
        south_vertex(z, lx1, ly1, &wx, &wy);
        f = ICLAMP((int)(s_puffs[i].age / 0.4f), 0, 5);
        wx += sp->hx - sp->ax + ((int)(s_puffs[i].age * 9.0f) >> z);
        wy += sp->hy - sp->ay - ((int)(s_puffs[i].age * 26.0f) >> z);
        render_sprite(art_findf("smoke%d", f), wx, wy, lx1, ly1, 0);
    }
}

/* ------------------------------------------------------------------ -test render: checks, loop, bench */
static int iso_selfcheck(void)
{
    int z, x, y, wx, wy, px, py, fails = 0;
    iso_init(g_world.w > 0 ? g_world.w : 128, g_world.h > 0 ? g_world.h : 128);
    for (z = 0; z < ZOOM_COUNT; z++)
        for (y = 0; y < 128; y += 3)
            for (x = 0; x < 128; x += 3) {
                iso_tile_to_world(z, x, y, &wx, &wy);
                if (!iso_world_to_tile(z, wx, wy + TILE_H(z) / 2, &px, &py) || px != x || py != y) fails++;
                iso_world_to_tile(z, wx - TILE_W(z) / 2, wy + TILE_H(z) / 2, &px, &py);   /* west vertex px: */
                if (px == x && py == y) fails++;                                      /* belongs to a neighbour */
            }
    log_printf("render: selftest: iso round trips, %d failures", fails);
    return fails;
}

#define BENCH_MAX 9000
typedef struct { float t, interval, work, cache, world, sprites, ui, chunkMs; int chunks, blits; } BFrame;
static BFrame *s_bf;
static int     s_nbf;

static int bfcmp(const void *a, const void *b)
{
    float d = *(const float *)a - *(const float *)b;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

static void bench_write(const char *path, double secs)
{
    FILE *f = fopen(path, "w");
    static float iv[BENCH_MAX];
    int i, n = s_nbf, over20 = 0, over185 = 0, maxChunks = 0, totalChunks = 0, blits = 0, maxBlits = 0;
    double work = 0, workMax = 0, cms = 0, cmsMax = 0, cache = 0, cacheMax = 0, wsum = 0, wmax = 0, ssum = 0, smax = 0, usum = 0, umax = 0;
    int worst[10], nw = 0, k, j;
    PlatStats ps;
    if (!f || !n) { if (f) fclose(f); return; }
    plat_get_stats(&ps);
    for (i = 0; i < n; i++) {
        const BFrame *b = &s_bf[i];
        iv[i] = b->interval;
        if (b->interval > 20.0f) over20++;
        if (b->interval > 18.5f) over185++;
        work += b->work; if (b->work > workMax) workMax = b->work;
        cms += b->chunkMs; if (b->chunkMs > cmsMax) cmsMax = b->chunkMs;
        cache += b->cache; if (b->cache > cacheMax) cacheMax = b->cache;
        wsum += b->world; if (b->world > wmax) wmax = b->world;
        ssum += b->sprites; if (b->sprites > smax) smax = b->sprites;
        usum += b->ui; if (b->ui > umax) umax = b->ui;
        totalChunks += b->chunks; if (b->chunks > maxChunks) maxChunks = b->chunks;
        blits += b->blits; if (b->blits > maxBlits) maxBlits = b->blits;
        /* keep the 10 longest frames */
        for (k = 0; k < nw && s_bf[worst[k]].interval >= b->interval; k++) {}
        if (k < 10) { for (j = IMIN(nw, 9); j > k; j--) worst[j] = worst[j - 1]; worst[k] = i; if (nw < 10) nw++; }
    }
    qsort(iv, n, sizeof(float), bfcmp);
    fprintf(f, GAME_TITLE_ASCII " render bench (T05, -test render)\n");
    fprintf(f, "backend: %s, %s\npacing: %s\n", plat_backend_name(), gfx_hw() ? "hardware blits" : "software", plat_pacing_name());
    fprintf(f, "view %dx%d at %d,%d, zoom %d (%dx%d tiles), overlay %d%s, cars %d, bench path: render_bench_camera\n",
            g_cam.view_w, g_cam.view_h, g_cam.view_x, g_cam.view_y, g_cam.zoom, TILE_W(g_cam.zoom), TILE_H(g_cam.zoom),
            s_ovl, s_ovlShow ? " (buildings shown)" : "", s_ncars);
    fprintf(f, "duration %.2f s, frames %d, avg fps %.2f\n", secs, n, n / secs);
    fprintf(f, "frame interval ms: min %.2f median %.2f p95 %.2f p99 %.2f max %.2f; frames > 18.5 ms: %d, > 20 ms: %d\n",
            iv[0], iv[n / 2], iv[n * 95 / 100], iv[n * 99 / 100], iv[n - 1], over185, over20);
    fprintf(f, "work ms (frame start -> present): avg %.3f max %.3f\n", work / n, workMax);
    fprintf(f, "chunk renders: %d (%.2f per frame, max %d in one frame), %.3f ms per chunk\n", totalChunks,
            (double)totalChunks / n, maxChunks, totalChunks ? cms / totalChunks : 0.0);
    fprintf(f, "chunk ms per frame: avg %.3f max %.3f\n", cms / n, cmsMax);
    fprintf(f, "section ms per frame:   avg     max\n  cache   %10.3f %7.3f\n  world   %10.3f %7.3f\n  sprites %10.3f %7.3f\n  ui      %10.3f %7.3f  (test stand-in: status line, minimap 16 rows/frame)\n",
            cache / n, cacheMax, wsum / n, wmax, ssum / n, smax, usum / n, umax);
    fprintf(f, "blits per frame avg %.1f max %d\n", (double)blits / n, maxBlits);
    fprintf(f, "missed frames (plat) %d, auto fps drops %d\n", ps.missed, ps.drops);
    fprintf(f, "VRAM: art %d KB (resident zoom %d), chunk pool %d x 64 KB = %d KB, gfx used %d KB, free %d KB of %d KB\n",
            art_vram_kb(), art_resident_zoom(), s_npool, s_npool * 64, gfx_vram_used_kb(), gfx_vram_free_kb(), gfx_vram_total_kb());
    fprintf(f, "\nworst frames:      t  interval    work   cache   world sprites      ui chunks  blits\n");
    for (k = 0; k < nw; k++) {
        const BFrame *b = &s_bf[worst[k]];
        fprintf(f, "              %7.2f %9.2f %7.2f %7.2f %7.2f %7.2f %7.2f %6d %6d\n", b->t, b->interval, b->work, b->cache, b->world, b->sprites, b->ui, b->chunks, b->blits);
    }
    fclose(f);
    log_printf("render: bench %.2f fps avg, max interval %.2f ms, %d frames > 20 ms, written %s", n / secs, iv[n - 1], over20, path);
}

static const int s_ovlCycle[] = { OVL_NONE, OVL_LAND_VALUE, OVL_POLLUTION, OVL_POWER, OVL_WATER, OVL_SVC_SCHOOL,
                                  OVL_FIRE_RISK, OVL_GROWTH, OVL_ZONES };

int render_selftest(void)
{
    int frameNo = 0, miniRow = 0, miniUpload = 0, fails = iso_selfcheck(), missing, li, sw, sh, ovlIdx = 0, carsOn = 1, ghost = 0, running = 1, blits, locks;
    const UiLayout *L;
    Img bar, mini;
    double t0, now, prev, tFrame0, lastPresent, nextZoom = 2.0, benchT0 = -1, nextBar = 0, nextMini = 0, nextAnim = 0, runT;
    char line[200];
    PIX colBar, colTeak, colText;
    if (!gfx_screen_w()) return fails;                         /* headless: math only */
    if (!art_init()) return fails + 1;
    missing = art_selftest();
    log_printf("render: selftest: art_selftest %d missing", missing);
    world_init(); budget_init(); net_init(); fields_init(); traffic_init(); politics_init(); sim_init();
    render_build_test_town(g_cfg.seed);
    sw = gfx_screen_w(); sh = gfx_screen_h();
    li = sw <= 640 ? 0 : sw <= 800 ? 1 : 2;
    L = &ui_layouts[li];
    g_cam.zoom = (g_cfg.start_zoom == 1) ? ZOOM_32 : ZOOM_64;
    cam_set_view(L->map.x, L->map.y, L->map.w, L->map.h);
    if (!render_init()) return fails + 1;
    if (g_cfg.start_cam_x >= 0 && g_cfg.start_cam_y >= 0) cam_center_on_tile(g_cfg.start_cam_x, g_cfg.start_cam_y);
    else cam_center_on_tile(58, 64);
    if (g_cfg.start_overlay > 0) render_set_overlay(g_cfg.start_overlay % 100, 0, g_cfg.start_overlay >= 100);
    demo_init();
    memset(&bar, 0, sizeof(bar)); memset(&mini, 0, sizeof(mini));
    gfx_make_target(sw, L->top.h, &bar);
    gfx_make_img(L->mmap.w, L->mmap.h, 0, &mini);
    colBar = gfx_rgb(UI_COL_BAR); colTeak = gfx_rgb(UI_COL_TEAK); colText = gfx_rgb(UI_COL_CREAM_LT);
    gfx_debug_font(colText);                                 /* build the GDI glyph atlas before timing */
    if (g_cfg.bench) { s_bf = (BFrame *)calloc(BENCH_MAX, sizeof(BFrame)); s_nbf = 0; }
    s_occlCheck = !g_cfg.bench;                                /* T13: compare tight vs wide occluder search */
    log_printf("render: selftest: town ready, entering the frame loop (keys: arrows, PgUp/PgDn, O overlay, Tab, D dump, G ghost, C cars, Esc)");
    t0 = prev = lastPresent = plat_seconds();
    gfx_stats(&blits, &locks);
    while (running) {
        const InputState *in;
        int mx, my, tx, ty;
        double dt, cacheMs, worldMs, sprMs;
        prof_frame_begin();
        if (!plat_pump()) break;
        input_poll();
        in = input_get();
        now = plat_seconds(); runT = now - t0;
        tFrame0 = now;
        dt = now - prev; prev = now;
        if (dt > 0.1) dt = 0.1;
        if (g_cfg.exit_after >= 0 && runT >= g_cfg.exit_after && !(g_cfg.shot_time >= 0 && runT < g_cfg.exit_after + 5)) break;
        if (key_hit(VK_ESCAPE)) break;
        mx = in->mx; my = in->my;
        if (key_hit(VK_PRIOR) || in->wheel > 0) render_set_zoom(ZOOM_64, mx, my);
        if (key_hit(VK_NEXT) || in->wheel < 0) render_set_zoom(ZOOM_32, mx, my);
        if (key_hit('O')) { ovlIdx = (ovlIdx + 1) % COUNTOF(s_ovlCycle); render_set_overlay(s_ovlCycle[ovlIdx], 0, s_ovlShow); }
        if (key_hit(VK_TAB)) render_set_overlay(s_ovl, s_ovlParam, !s_ovlShow);
        if (key_hit('C')) carsOn = !carsOn;
        if (key_hit('G')) ghost = !ghost;
        if (key_hit('D')) {
            FILE *df = fopen(data_path("render_dump.txt"), "a");
            if (df) { render_dump_at(df, mx, my); fclose(df); }
        }
        if (g_cfg.bench) {
            if (runT >= 1.0 && benchT0 < 0) benchT0 = now;
            render_bench_camera(runT);
        } else {
            int ex = 0, ey = 0, sp = g_cfg.scroll_speed > 0 ? g_cfg.scroll_speed : 8;
            if (key_down(VK_LEFT)) ex--;
            if (key_down(VK_RIGHT)) ex++;
            if (key_down(VK_UP)) ey--;
            if (key_down(VK_DOWN)) ey++;
            if (!ex && !ey && g_cfg.fullscreen && g_cfg.edge_scroll) input_edge_scroll(4, &ex, &ey);
            cam_push(ex * sp, ey * sp);
            cam_update();
        }
        if (g_cfg.start_speed == 3 && runT >= nextZoom) {          /* -test render -speed 3: zoom ping-pong, timed */
            double z0 = time_ms(), z1;
            int c0 = (int)s_totalChunks;
            nextZoom = runT + 2.0;
            render_set_zoom(g_cam.zoom ^ 1, g_cam.view_x + g_cam.view_w / 2, g_cam.view_y + g_cam.view_h / 2);
            z1 = time_ms();
            render_update(3.0);
            log_printf("render: zoom switch to %d: set_zoom %.1f ms (art residency %s), flush render %.1f ms, %d chunks",
                       g_cam.zoom, z1 - z0, s_residency ? "swap" : "none", time_ms() - z1, (int)s_totalChunks - c0);
        }
        demo_update(dt);
        if (runT >= nextAnim) { nextAnim = runT + 0.3; render_set_anim_frame(s_anim + 1); }

        if (miniUpload) { gfx_img_update(&mini); miniUpload = 0; }   /* upload = lock: do it while the blitter is idle */
        prof_begin(PROF_CACHE); render_update(3.0); prof_end(PROF_CACHE);
        cacheMs = prof_frame_ms(PROF_CACHE);
        prof_begin(PROF_WORLD); render_world(); prof_end(PROF_WORLD);
        worldMs = prof_frame_ms(PROF_WORLD);
        prof_begin(PROF_SPRITES);
        demo_draw(carsOn);
        if (cam_pick(mx, my, &tx, &ty)) {
            render_tile_mark(art_find(ghost ? "cur_sel" : "cur_ok"), tx, ty);
            if (ghost) render_lot_ghost(art_find("house_t2_v3_r0"), tx, ty, 1, 1);
        }
        render_flush_sprites();
        prof_end(PROF_SPRITES);
        sprMs = prof_frame_ms(PROF_SPRITES);
        /* UI stand-in: frame fills, a status line composed twice a second, the minimap once a second */
        prof_begin(PROF_UI);
        if (runT >= nextBar) {
            RenderStats st;
            render_stats(&st);
            nextBar = runT + 0.5;
            gfx_set_target(&bar);
            gfx_clear(colBar);
            str_printf(line, sizeof(line), "T05 render test  zoom %d  ovl %d%s  pool %d valid %d dirty %d  chunk avg %.2f ms  sprites %d occl %d  tile %d,%d",
                       g_cam.zoom, s_ovl, s_ovlShow ? "+b" : "", st.chunks_pool, st.chunks_valid, st.chunks_dirty,
                       s_avgChunkMs[g_cam.zoom], st.sprites, st.occlusion_blits, tx, ty);
            gfx_debug_text(4, 2, line, colText);
            gfx_set_target(NULL);
        }
        if (runT >= nextMini) {                                  /* 16 rows per frame, upload when done */
            render_minimap_rows(mini.px, mini.w, mini.w, mini.h, s_ovl, miniRow, miniRow + 16);
            miniRow += 16;
            if (miniRow >= mini.h) { miniUpload = 1; miniRow = 0; nextMini = runT + 1.0; }
        }
        gfx_blit(&bar, 0, 0);
        gfx_fill(L->panel.x, L->panel.y, sw, sh, colTeak);
        gfx_fill(L->ticker.x, L->ticker.y, L->ticker.x + L->ticker.w, L->ticker.y + L->ticker.h, colBar);
        gfx_blit(&mini, L->mmap.x, L->mmap.y);
        prof_end(PROF_UI);
        if (++frameNo == 30) {                                     /* one text dump at the view centre */
            FILE *df = fopen(data_path("render_dump.txt"), "w");
            if (df) { render_dump_at(df, g_cam.view_x + g_cam.view_w / 2, g_cam.view_y + g_cam.view_h / 2); fclose(df); }
        }
        if (g_cfg.shot_time >= 0 && runT >= g_cfg.shot_time) { gfx_save_bmp(data_path(g_cfg.shot_path)); g_cfg.shot_time = -1.0f; }
        gfx_cursor_draw(mx, my);
        {
            double work = (plat_seconds() - tFrame0) * 1000.0;
            RenderStats st;
            plat_present();
            gfx_stats(&blits, &locks);
            render_stats(&st);
            now = plat_seconds();
            if (s_bf && benchT0 >= 0 && s_nbf < BENCH_MAX) {
                BFrame *b = &s_bf[s_nbf++];
                b->t = (float)(now - benchT0); b->interval = (float)((now - lastPresent) * 1000.0);
                b->work = (float)work; b->ui = (float)prof_frame_ms(PROF_UI); b->cache = (float)cacheMs; b->world = (float)worldMs; b->sprites = (float)sprMs;
                b->chunkMs = (float)st.chunk_ms; b->chunks = st.chunks_rendered; b->blits = blits;
            }
            lastPresent = now;
            if (s_bf && benchT0 >= 0 && now - benchT0 >= g_cfg.bench) { bench_write(data_path("bench.txt"), now - benchT0); running = 0; }
        }
    }
    gfx_free_img(&bar); gfx_free_img(&mini);
    if (s_bf) { free(s_bf); s_bf = NULL; }
    render_shutdown();
    log_printf("render: selftest done: %d failures, %d art names missing, %u chunks rendered (%.3f ms avg)",
               fails, missing, s_totalChunks, s_totalChunks ? s_totalChunkMs / s_totalChunks : 0.0);
    log_printf("render: occluder self-check (T13): %d sprite submissions checked, %d differ", s_occlChecked, s_occlBad);
    return fails + missing + (s_occlBad ? 1 : 0);
}
