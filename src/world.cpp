/* world.cpp - the map: tiles, lots, setters with autotile masks + notifications, placement, dumps, hash.
   PORTABLE. Owner: T04. See world.h. */
#include "common.h"
#include "world.h"
#include "util.h"
#include "save.h"

World g_world;
static Tile s_outside;      /* returned by world_tile() for out-of-map coordinates */

static struct { WorldChangeFn fn; void *user; } s_lis[WORLD_MAX_LISTENERS];
static int s_batch_depth;
static int s_batch_kinds;
/* T13: a batch keeps a short list of rects instead of one bounding rect, so a growth slice that touches
   a few far-apart lots doesn't dirty a whole band of render chunks / field cells. Rects closer than
   WB_NEAR tiles merge; a full list merges the pair that grows the least. */
#define WB_MAX  8
#define WB_NEAR 3
typedef struct { int x0, y0, x1, y1, kinds; } WbRect;
static WbRect s_brect[WB_MAX + 1];   /* T12 fix: batch_add uses slot WB_MAX as the "one too many" temp (was a
                                        global overflow that clobbered the next globals, e.g. budget state) */
static int s_nbrect;
static int s_free_hint = 1;     /* lowest slot that may be free */
static u32 s_struct_serial = 1;  /* bumped by changes other than lot placement/def/status (net fast paths) */
static int s_road_n[ROAD_CLASS_COUNT], s_bridge_n[ROAD_CLASS_COUNT], s_line_n;   /* running counts (budget) */
static int s_pipe_n, s_pipe_wet_n;   /* F1: pipe tiles, of which under water */

static void recount_roads(void)
{
    int x, y;
    memset(s_road_n, 0, sizeof(s_road_n)); memset(s_bridge_n, 0, sizeof(s_bridge_n)); s_line_n = 0;
    s_pipe_n = 0; s_pipe_wet_n = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = WT(x, y);
            int c = TILE_ROAD(t);
            if (c) { s_road_n[c]++; if (TER_IS_WATER(TER_TYPE(t))) s_bridge_n[c]++; }
            if (t->road & TR_POWER_LINE) s_line_n++;
            if (t->flags & TF_PIPE) { s_pipe_n++; if (TER_IS_WATER(TER_TYPE(t))) s_pipe_wet_n++; }
        }
}

int world_pipe_tiles(int *under_water) { if (under_water) *under_water = s_pipe_wet_n; return s_pipe_n; }

u32 world_struct_serial(void) { return s_struct_serial; }

void world_road_stats(int *roads, int *bridges, int *lines)
{
    int i;
    for (i = 0; i < ROAD_CLASS_COUNT; i++) { if (roads) roads[i] = s_road_n[i]; if (bridges) bridges[i] = s_bridge_n[i]; }
    if (lines) *lines = s_line_n;
}

/* ------------------------------------------------------------------ basics */
void world_init(void)
{
    memset(&g_world, 0, sizeof(g_world));
    memset(s_lis, 0, sizeof(s_lis));
    s_batch_depth = 0; s_batch_kinds = 0; s_nbrect = 0;
    g_world.w = MAP_DEF_W; g_world.h = MAP_DEF_H;
    g_world.lot_hi = 1;
    s_outside.flags = TF_PROTECTED;
    s_free_hint = 1;
}

void world_new(int w, int h)
{
    memset(g_world.t, 0, sizeof(g_world.t));
    memset(g_world.lots, 0, sizeof(g_world.lots));
    g_world.w = ICLAMP(w, 8, MAP_MAX_W);
    g_world.h = ICLAMP(h, 8, MAP_MAX_H);
    g_world.lot_hi = 1; g_world.lot_count = 0;
    s_free_hint = 1;
    recount_roads();
    s_struct_serial++;
    world_notify_all(WCHG_ALL);
}

int world_in(int x, int y) { return x >= 0 && y >= 0 && x < g_world.w && y < g_world.h; }
const Tile *world_tile(int x, int y) { return world_in(x, y) ? WT(x, y) : &s_outside; }

Lot *world_lot(int id)
{
    if (id <= 0 || id >= MAX_LOTS || id >= g_world.lot_hi) return NULL;
    if (g_world.lots[id].def == B_NONE) return NULL;
    return &g_world.lots[id];
}

int world_lot_at(int x, int y) { return world_in(x, y) ? WT(x, y)->lot : 0; }

/* ------------------------------------------------------------------ notification */
int world_add_listener(WorldChangeFn fn, void *user)
{
    int i;
    for (i = 0; i < WORLD_MAX_LISTENERS; i++)
        if (s_lis[i].fn == fn && s_lis[i].user == user) return i;
    for (i = 0; i < WORLD_MAX_LISTENERS; i++)
        if (!s_lis[i].fn) { s_lis[i].fn = fn; s_lis[i].user = user; return i; }
    log_printf("world: warning: listener table full");
    return -1;
}

void world_remove_listener(WorldChangeFn fn, void *user)
{
    int i;
    for (i = 0; i < WORLD_MAX_LISTENERS; i++)
        if (s_lis[i].fn == fn && s_lis[i].user == user) { s_lis[i].fn = NULL; s_lis[i].user = NULL; }
}

static void notify_now(int x0, int y0, int x1, int y1, int kinds)
{
    int i;
    g_world.serial++;
    for (i = 0; i < WORLD_MAX_LISTENERS; i++)
        if (s_lis[i].fn) s_lis[i].fn(x0, y0, x1, y1, kinds, s_lis[i].user);
}

static long wb_area(int x0, int y0, int x1, int y1) { return (long)(x1 - x0 + 1) * (y1 - y0 + 1); }

static void wb_union(WbRect *a, const WbRect *b)
{
    a->x0 = IMIN(a->x0, b->x0); a->y0 = IMIN(a->y0, b->y0);
    a->x1 = IMAX(a->x1, b->x1); a->y1 = IMAX(a->y1, b->y1);
    a->kinds |= b->kinds;
}

static void batch_add(int x0, int y0, int x1, int y1, int kinds)
{
    WbRect r;
    int i, j, bi = 0, bj = 1;
    long best = -1;
    r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1; r.kinds = kinds;
    for (i = 0; i < s_nbrect; i++) {             /* near an existing rect: grow it */
        WbRect *b = &s_brect[i];
        if (x0 <= b->x1 + WB_NEAR && x1 >= b->x0 - WB_NEAR && y0 <= b->y1 + WB_NEAR && y1 >= b->y0 - WB_NEAR) {
            wb_union(b, &r);
            return;
        }
    }
    if (s_nbrect < WB_MAX) { s_brect[s_nbrect++] = r; return; }
    s_brect[s_nbrect] = r;                       /* one slot too many: merge the cheapest pair */
    for (i = 0; i <= WB_MAX; i++)
        for (j = i + 1; j <= WB_MAX; j++) {
            const WbRect *a = &s_brect[i], *b = &s_brect[j];
            long grow = wb_area(IMIN(a->x0, b->x0), IMIN(a->y0, b->y0), IMAX(a->x1, b->x1), IMAX(a->y1, b->y1))
                        - wb_area(a->x0, a->y0, a->x1, a->y1) - wb_area(b->x0, b->y0, b->x1, b->y1);
            if (best < 0 || grow < best) { best = grow; bi = i; bj = j; }
        }
    wb_union(&s_brect[bi], &s_brect[bj]);
    s_brect[bj] = s_brect[WB_MAX];
}

void world_notify(int x0, int y0, int x1, int y1, int kinds)
{
    int t;
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { t = y0; y0 = y1; y1 = t; }
    x0 = ICLAMP(x0, 0, g_world.w - 1); x1 = ICLAMP(x1, 0, g_world.w - 1);
    y0 = ICLAMP(y0, 0, g_world.h - 1); y1 = ICLAMP(y1, 0, g_world.h - 1);
    if (s_batch_depth > 0) {
        batch_add(x0, y0, x1, y1, kinds);
        s_batch_kinds |= kinds;
        g_world.serial++;
        return;
    }
    notify_now(x0, y0, x1, y1, kinds);
}

void world_notify_all(int kinds) { world_notify(0, 0, g_world.w - 1, g_world.h - 1, kinds); }

void world_batch_begin(void) { s_batch_depth++; }

void world_batch_end(void)
{
    if (s_batch_depth <= 0) return;
    if (--s_batch_depth == 0 && s_batch_kinds) {
        WbRect r[WB_MAX];
        int i, n = s_nbrect;
        memcpy(r, s_brect, sizeof(WbRect) * n);
        s_batch_kinds = 0; s_nbrect = 0;
        for (i = 0; i < n; i++) notify_now(r[i].x0, r[i].y0, r[i].x1, r[i].y1, r[i].kinds);
    }
}

/* ------------------------------------------------------------------ road masks */
static int road_links(int a, int b)   /* do classes a and b connect visually/logically? */
{
    if (a == ROAD_NONE || b == ROAD_NONE) return 0;
    if (a == ROAD_RAIL || b == ROAD_RAIL) return a == b;
    return 1;
}

static void road_mask_update(int x, int y)
{
    Tile *t;
    int d, cls, m = 0;
    if (!world_in(x, y)) return;
    t = WT(x, y);
    cls = TILE_ROAD(t);
    if (cls) {
        for (d = 0; d < DIR_COUNT; d++) {
            int nx = x + g_dir_dx[d], ny = y + g_dir_dy[d];
            if (!world_in(nx, ny)) {
                if (t->flags & TF_EDGE_LINK) m |= 1 << d;    /* road continues off the map */
                continue;
            }
            if (road_links(cls, TILE_ROAD(WT(nx, ny)))) m |= 1 << d;
        }
    }
    t->road = (u8)((t->road & (TR_CLASS_MASK | TR_POWER_LINE)) | (m << TR_MASK_SHIFT));
}

/* ------------------------------------------------------------------ setters */
void world_set_terrain(int x, int y, int ter, int variant)
{
    Tile *t;
    int ow, nw;
    if (!world_in(x, y)) return;
    t = WT(x, y);
    ow = TER_IS_WATER(TER_TYPE(t));
    t->terrain = (u8)((ter & 15) | ((variant & 15) << 4));
    nw = TER_IS_WATER(TER_TYPE(t));
    s_struct_serial++;
    if (ow != nw && TILE_ROAD(t)) s_bridge_n[TILE_ROAD(t)] += nw ? 1 : -1;
    if (ow != nw && (t->flags & TF_PIPE)) s_pipe_wet_n += nw ? 1 : -1;
    world_notify(x - 1, y - 1, x + 1, y + 1, WCHG_TERRAIN);   /* shore autotile of neighbours */
}

static void clear_vegetation(Tile *t)
{
    if (TER_TYPE(t) == TER_FOREST) t->terrain = (u8)(TER_GRASS | ((t->terrain >> 4) % 3) << 4);
    t->flags &= ~TF_TREE;
}

void world_set_road(int x, int y, int cls)
{
    Tile *t;
    int d;
    if (!world_in(x, y)) return;
    t = WT(x, y);
    cls = ICLAMP(cls, 0, ROAD_CLASS_COUNT - 1);
    if (TILE_ROAD(t)) { s_road_n[TILE_ROAD(t)]--; if (TER_IS_WATER(TER_TYPE(t))) s_bridge_n[TILE_ROAD(t)]--; }
    if (cls) { s_road_n[cls]++; if (TER_IS_WATER(TER_TYPE(t))) s_bridge_n[cls]++; }
    if (cls) clear_vegetation(t);
    s_struct_serial++;
    t->road = (u8)((t->road & ~TR_CLASS_MASK) | cls);
    road_mask_update(x, y);
    for (d = 0; d < DIR_COUNT; d++) road_mask_update(x + g_dir_dx[d], y + g_dir_dy[d]);
    world_notify(x - 1, y - 1, x + 1, y + 1, WCHG_ROAD | WCHG_FLAGS | (cls ? WCHG_TERRAIN : 0));
}

void world_set_power_line(int x, int y, int on)
{
    Tile *t;
    if (!world_in(x, y)) return;
    t = WT(x, y);
    if (!!(t->road & TR_POWER_LINE) != !!on) { s_line_n += on ? 1 : -1; s_struct_serial++; }
    if (on) { clear_vegetation(t); t->road |= TR_POWER_LINE; }
    else t->road &= ~TR_POWER_LINE;
    /* the renderer derives power-line masks from neighbours: invalidate them too */
    world_notify(x - 1, y - 1, x + 1, y + 1, WCHG_POWER_LINE | (on ? WCHG_TERRAIN | WCHG_FLAGS : 0));
}

void world_set_zone(int x, int y, int zone)
{
    Tile *t;
    if (!world_in(x, y)) return;
    t = WT(x, y);
    zone = ICLAMP(zone, 0, ZONE_COUNT - 1);
    if (TILE_ZONE(t) == zone) return;
    if (!TILE_ZONE(t) != !zone) s_struct_serial++;        /* F1: zoned land conducts power (DESIGN 14.2) */
    t->zone = (u8)((t->zone & ~ZF_ZONE_MASK) | zone);
    world_notify(x, y, x, y, WCHG_ZONE);
}

void world_set_tile_flags(int x, int y, int set, int clear)
{
    Tile *t;
    u8 nf;
    if (!world_in(x, y)) return;
    t = WT(x, y);
    nf = (u8)((t->flags & ~clear) | set);
    if (nf == t->flags) return;
    t->flags = nf;
    if ((set | clear) & TF_EDGE_LINK) road_mask_update(x, y);
    world_notify(x, y, x, y, WCHG_FLAGS);
}

/* F1: pipes are underground and drawn only in the water overlay: no struct serial bump (power and traffic
   don't care), the rect covers the neighbours whose pipe autotile mask changes */
void world_set_pipe(int x, int y, int on)
{
    Tile *t;
    if (!world_in(x, y)) return;
    t = WT(x, y);
    if (!(t->flags & TF_PIPE) == !on) return;
    if (on) t->flags |= TF_PIPE; else t->flags &= ~TF_PIPE;
    s_pipe_n += on ? 1 : -1;
    if (TER_IS_WATER(TER_TYPE(t))) s_pipe_wet_n += on ? 1 : -1;
    world_notify(x - 1, y - 1, x + 1, y + 1, WCHG_PIPE);
}

/* ------------------------------------------------------------------ lots */
void world_lot_rect(int id, int *x0, int *y0, int *x1, int *y1)
{
    Lot *l = world_lot(id);
    int w, h;
    if (!l) { *x0 = *y0 = *x1 = *y1 = 0; return; }
    def_footprint(l->def, l->rot, &w, &h);
    *x0 = l->x; *y0 = l->y; *x1 = l->x + w - 1; *y1 = l->y + h - 1;
}

static int is_water_ter(const Tile *t) { return TER_IS_WATER(TER_TYPE(t)); }

int world_lot_check(int def, int x, int y, int rot)
{
    const BuildingDef *d = def_building(def);
    int w, h, i, j, shore = 0, rapids = 0;
    if (def <= B_NONE || def >= B_COUNT) return PLACE_NO_SLOTS;
    def_footprint(def, rot & 3, &w, &h);
    if (!world_in(x, y) || !world_in(x + w - 1, y + h - 1)) return PLACE_OUT_OF_MAP;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            const Tile *t = WT(x + i, y + j);
            if (t->flags & TF_PROTECTED) return PLACE_PROTECTED;
            if (is_water_ter(t)) return PLACE_WATER;
            if (TILE_ROAD(t)) return PLACE_ROAD;
            if (t->lot || (t->road & TR_POWER_LINE)) return PLACE_OCCUPIED;
        }
    if (d->flags & (BDF_NEEDS_SHORE | BDF_NEEDS_RAPIDS)) {
        for (j = -1; j <= h; j++)
            for (i = -1; i <= w; i++) {
                const Tile *t;
                if ((i == -1 || i == w) && (j == -1 || j == h)) continue;     /* 4-adjacent ring only */
                if (i >= 0 && i < w && j >= 0 && j < h) continue;
                t = world_tile(x + i, y + j);
                if (t == &s_outside) continue;
                if (is_water_ter(t)) shore = 1;
                if (TER_TYPE(t) == TER_RIVER && (TER_VAR(t) & RIVER_RAPIDS_BIT)) rapids = 1;
            }
        if ((d->flags & BDF_NEEDS_SHORE) && !shore) return PLACE_NEEDS_SHORE;
        if ((d->flags & BDF_NEEDS_RAPIDS) && !rapids) return PLACE_NEEDS_RAPIDS;
    }
    if (d->flags & BDF_UNIQUE) {
        for (i = 1; i < g_world.lot_hi; i++)
            if (g_world.lots[i].def == def) return PLACE_UNIQUE;
    }
    if (g_world.lot_count >= MAX_LOTS - 1) return PLACE_NO_SLOTS;
    return PLACE_OK;
}

static int alloc_slot(void)
{
    int i;
    for (i = IMAX(1, s_free_hint); i < g_world.lot_hi; i++)
        if (g_world.lots[i].def == B_NONE) { s_free_hint = i + 1; return i; }
    if (g_world.lot_hi >= MAX_LOTS) return 0;
    s_free_hint = g_world.lot_hi + 1;
    return g_world.lot_hi++;
}

int world_lot_place(int def, int x, int y, int rot, int variant, int status)
{
    const BuildingDef *d = def_building(def);
    Lot *l;
    int id, w, h, i, j, kinds = WCHG_LOT;
    if (world_lot_check(def, x, y, rot) != PLACE_OK) return 0;
    if (d->flags & BDF_NO_ROTATE) rot = 0;
    id = alloc_slot();
    if (!id) return 0;
    l = &g_world.lots[id];
    memset(l, 0, sizeof(*l));
    l->def = (u8)def; l->x = (u8)x; l->y = (u8)y; l->rot = (u8)(rot & 3);
    l->variant = (u8)(d->variants ? variant % d->variants : 0);
    l->status = (u8)ICLAMP(status, 0, LOT_STATUS_COUNT - 1);
    l->build_left = (u8)(l->status == LOT_BUILDING ? IMAX(1, d->build_months) : 0);
    l->happy = 50;
    def_footprint(def, l->rot, &w, &h);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            Tile *t = WT(x + i, y + j);
            if (TER_TYPE(t) == TER_FOREST || (t->flags & TF_TREE)) kinds |= WCHG_TERRAIN | WCHG_FLAGS;
            clear_vegetation(t);
            t->lot = (u16)id;
            t->flags &= ~TF_LOT_FRONT;
        }
    WT(x + w - 1, y + h - 1)->flags |= TF_LOT_FRONT;
    g_world.lot_count++;
    world_notify(x, y, x + w - 1, y + h - 1, kinds);
    return id;
}

void world_lot_remove(int id)
{
    Lot *l = world_lot(id);
    int x0, y0, x1, y1, i, j;
    if (!l) return;
    world_lot_rect(id, &x0, &y0, &x1, &y1);
    for (j = y0; j <= y1; j++)
        for (i = x0; i <= x1; i++) {
            Tile *t = WT(i, j);
            if (t->lot == id) { t->lot = 0; t->flags &= ~TF_LOT_FRONT; t->zone &= ~(ZF_POWER_NET | ZF_POWERED); }
        }
    memset(l, 0, sizeof(*l));
    g_world.lot_count--;
    s_struct_serial++;
    if (id < s_free_hint) s_free_hint = id;
    while (g_world.lot_hi > 1 && g_world.lots[g_world.lot_hi - 1].def == B_NONE) g_world.lot_hi--;
    world_notify(x0, y0, x1, y1, WCHG_LOT);
}

void world_lot_set_def(int id, int def)
{
    Lot *l = world_lot(id);
    const BuildingDef *d;
    int ow, oh, nw, nh, x0, y0, x1, y1;
    if (!l || def <= B_NONE || def >= B_COUNT || l->def == def) return;
    def_footprint(l->def, l->rot, &ow, &oh);
    def_footprint(def, l->rot, &nw, &nh);
    if (ow != nw || oh != nh) { log_printf("world: error: set_def %d footprint mismatch", def); return; }
    d = def_building(def);
    l->def = (u8)def;
    if (d->variants) l->variant = (u8)(l->variant % d->variants);
    world_lot_rect(id, &x0, &y0, &x1, &y1);
    world_notify(x0, y0, x1, y1, WCHG_LOT);
}

void world_lot_set_status(int id, int status)
{
    Lot *l = world_lot(id);
    int x0, y0, x1, y1;
    if (!l || l->status == status) return;
    l->status = (u8)ICLAMP(status, 0, LOT_STATUS_COUNT - 1);
    if (l->status != LOT_BUILDING) l->build_left = 0;
    world_lot_rect(id, &x0, &y0, &x1, &y1);
    world_notify(x0, y0, x1, y1, WCHG_LOT);
}

void world_lot_set_variant(int id, int variant)
{
    Lot *l = world_lot(id);
    const BuildingDef *d;
    int x0, y0, x1, y1;
    if (!l) return;
    d = def_building(l->def);
    variant = d->variants ? variant % d->variants : 0;
    if (l->variant == variant) return;
    l->variant = (u8)variant;
    world_lot_rect(id, &x0, &y0, &x1, &y1);
    world_notify(x0, y0, x1, y1, WCHG_LOT);
}

static int access_class(int x, int y)
{
    const Tile *t = world_tile(x, y);
    int c = TILE_ROAD(t);
    return (c && (g_roads[c].flags & RDF_ZONE_ACCESS)) ? c : 0;
}

/* rotation whose front faces direction d */
static int rot_for_dir(int d)
{
    int r;
    for (r = 0; r < ROT_COUNT; r++) if (g_rot_front_dir[r] == d) return r;
    return 0;
}

int world_lot_access(int id, int *rot)
{
    int x0, y0, x1, y1, r, best = 0, bestrot = 0;
    Lot *l = world_lot(id);
    *rot = 0;
    if (!l) return 0;
    world_lot_rect(id, &x0, &y0, &x1, &y1);
    /* check sides in rotation order r0..r3 (front SW, SE, NE, NW); keep the best class */
    for (r = 0; r < ROT_COUNT; r++) {
        int d = g_rot_front_dir[r], i, c = 0;
        if (d == DIR_SW) for (i = x0; i <= x1; i++) c = IMAX(c, access_class(i, y1 + 1));
        if (d == DIR_NE) for (i = x0; i <= x1; i++) c = IMAX(c, access_class(i, y0 - 1));
        if (d == DIR_SE) for (i = y0; i <= y1; i++) c = IMAX(c, access_class(x1 + 1, i));
        if (d == DIR_NW) for (i = y0; i <= y1; i++) c = IMAX(c, access_class(x0 - 1, i));
        if (c > best) { best = c; bestrot = r; }
    }
    *rot = bestrot;
    return best;
}

int world_footprint_road_dist(int x0, int y0, int x1, int y1, int maxd, int *rot)
{
    int d, i, j;
    if (rot) *rot = 0;
    for (d = 1; d <= maxd; d++) {
        /* ring at Chebyshev distance d; prefer the sides in order SW, SE, NE, NW, then corners */
        int side, hit = -1;
        for (side = 0; side < 4 && hit < 0; side++) {
            int dir = g_rot_front_dir[side];
            if (dir == DIR_SW) { for (i = x0; i <= x1; i++) if (access_class(i, y1 + d)) { hit = dir; break; } }
            if (dir == DIR_SE) { for (j = y0; j <= y1; j++) if (access_class(x1 + d, j)) { hit = dir; break; } }
            if (dir == DIR_NE) { for (i = x0; i <= x1; i++) if (access_class(i, y0 - d)) { hit = dir; break; } }
            if (dir == DIR_NW) { for (j = y0; j <= y1; j++) if (access_class(x0 - d, j)) { hit = dir; break; } }
        }
        if (hit < 0) {
            /* full ring incl. corners */
            for (j = y0 - d; j <= y1 + d && hit < 0; j++)
                for (i = x0 - d; i <= x1 + d; i++) {
                    if (i > x0 - d && i < x1 + d && j > y0 - d && j < y1 + d) continue;
                    if (access_class(i, j)) { hit = (j > y1) ? DIR_SW : (j < y0) ? DIR_NE : (i > x1) ? DIR_SE : DIR_NW; break; }
                }
        }
        if (hit >= 0) { if (rot) *rot = rot_for_dir(hit); return d; }
    }
    return 0;
}

/* ------------------------------------------------------------------ queries */
int world_is_buildable(int x, int y)
{
    const Tile *t;
    if (!world_in(x, y)) return 0;
    t = WT(x, y);
    return !is_water_ter(t) && !TILE_ROAD(t) && !t->lot && !(t->flags & TF_PROTECTED) &&
           !(t->road & TR_POWER_LINE);
}

int world_road_within(int x, int y, int dist)
{
    int i, j;
    for (j = y - dist; j <= y + dist; j++)
        for (i = x - dist; i <= x + dist; i++)
            if (access_class(i, j)) return 1;
    return 0;
}

int world_count_zone(int zone)
{
    int x, y, n = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++)
            if (TILE_ZONE(WT(x, y)) == zone) n++;
    return n;
}

u32 world_hash(void)
{
    u32 h = HASH_INIT;
    int y, i;
    h = hash_bytes(h, &g_world.w, sizeof(int));
    h = hash_bytes(h, &g_world.h, sizeof(int));
    for (y = 0; y < g_world.h; y++) h = hash_bytes(h, WT(0, y), g_world.w * (int)sizeof(Tile));
    for (i = 1; i < g_world.lot_hi; i++)
        if (g_world.lots[i].def != B_NONE) h = hash_bytes(h, &g_world.lots[i], sizeof(Lot));
    return h;
}

/* ------------------------------------------------------------------ maintenance */
void world_recalc(void)
{
    int x, y, i, n = 0, hi = 1;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) { road_mask_update(x, y); WT(x, y)->flags &= ~TF_LOT_FRONT; }
    for (i = 1; i < MAX_LOTS; i++)
        if (g_world.lots[i].def != B_NONE) {
            int x0, y0, x1, y1;
            n++; hi = i + 1;
            g_world.lot_hi = IMAX(g_world.lot_hi, hi);
            world_lot_rect(i, &x0, &y0, &x1, &y1);
            if (world_in(x1, y1)) WT(x1, y1)->flags |= TF_LOT_FRONT;
        }
    g_world.lot_hi = hi;
    g_world.lot_count = n;
    s_free_hint = 1;
    recount_roads();
    s_struct_serial++;
}

/* ------------------------------------------------------------------ dumps */
static char tile_char(int x, int y)
{
    const Tile *t = WT(x, y);
    int ter = TER_TYPE(t), r = TILE_ROAD(t);
    if (t->lot) {
        const Lot *l = &g_world.lots[t->lot];
        const BuildingDef *d = def_building(l->def);
        char c = '?';
        switch (d->cat) {
        case BCAT_ZONE:
            c = d->zone == ZONE_HOUSES ? 'H' : d->zone == ZONE_APARTMENTS ? 'A' : d->zone == ZONE_COMMERCIAL ? 'C' :
                d->zone == ZONE_FARM ? 'F' : d->zone == ZONE_FORESTRY ? 'G' : 'I';     /* M3: farm, logging camp */
            if (l->status == LOT_BUILDING) c = (char)(c - 'A' + 'a');     /* under construction: lower case */
            else if (l->status == LOT_ABANDONED) c = '%';
            else if (d->tier >= 2) {                                         /* see legend */
                static const char tc[ZONE_COUNT][2] = { { '?', '?' }, { '2', '3' }, { 'B', 'Q' },
                                                        { 'D', 'E' }, { 'J', 'L' }, { 'G', 'G' }, { 'Y', 'Z' } };
                c = tc[d->zone][d->tier - 2];
            }
            break;
        case BCAT_POWER: c = 'P'; break;
        case BCAT_WATER: c = 'W'; break;
        case BCAT_SERVICE: c = 'S'; break;
        case BCAT_PARK: c = 'K'; break;
        default: c = 'X'; break;
        }
        return c;
    }
    if (r == ROAD_RAIL) return '=';
    if (r) return (t->road & TR_POWER_LINE) ? '*' : '#';
    if (t->road & TR_POWER_LINE) return '+';
    switch (TILE_ZONE(t)) {
    case ZONE_HOUSES: return 'h';
    case ZONE_APARTMENTS: return 'a';
    case ZONE_COMMERCIAL: return 'c';
    case ZONE_INDUSTRY: return 'i';
    case ZONE_FARM: return TER_TYPE(t) == TER_FIELD ? ';' : 'f';     /* M3 */
    case ZONE_FORESTRY: return TER_TYPE(t) == TER_FOREST ? ((t->aux >> 4) ? 't' : 'T') : 'g';
    default: break;
    }
    switch (ter) {
    case TER_WATER: return '~';
    case TER_RIVER: return 'r';
    case TER_FOREST: return 'T';
    case TER_ROCK: return '^';
    case TER_FIELD: return ',';
    case TER_MARSH: return '"';
    default: return '.';
    }
}

void world_dump(FILE *f, int x0, int y0, int x1, int y1)
{
    int x, y;
    x0 = ICLAMP(x0, 0, g_world.w - 1); x1 = ICLAMP(x1, 0, g_world.w - 1);
    y0 = ICLAMP(y0, 0, g_world.h - 1); y1 = ICLAMP(y1, 0, g_world.h - 1);
    fprintf(f, "world %dx%d, lots %d, rect x %d..%d y %d..%d (x across, y down)\n",
            g_world.w, g_world.h, g_world.lot_count, x0, x1, y0, y1);
    fprintf(f, "legend: ~ water r river T forest ^ rock . grass , field # road * road+line = rail + line\n"
               "        h a c i zoned; H A C I tier-1 lots (lower case = under construction); houses tier 2/3 = 2 3,\n"
               "        apartments/commercial/industry tier 2 = B D J (tier 3 = Q E L); %% abandoned,\n"
               "        P plant, W water, S service, K park, X special\n"
               "        M3: forestry T mature / t regrowing / g other, G logging camp; farm land f (; field), F farm T1, Y Z T2 T3\n");
    fprintf(f, "     ");
    for (x = x0; x <= x1; x++) fputc(x % 10 == 0 ? '0' + (x / 10) % 10 : ' ', f);
    fputc('\n', f);
    for (y = y0; y <= y1; y++) {
        fprintf(f, "%4d ", y);
        for (x = x0; x <= x1; x++) fputc(tile_char(x, y), f);
        fputc('\n', f);
    }
}

void world_dump_tile(FILE *f, int x, int y)
{
    const Tile *t;
    if (!world_in(x, y)) { fprintf(f, "tile %d,%d: outside the map\n", x, y); return; }
    t = WT(x, y);
    fprintf(f, "tile %d,%d: terrain %d var %d, road class %d mask %X line %d, zone %d net %d powered %d "
               "watered %d water-net %d pipe %d, flags %02X, lot %d, lv %d, aux %d\n",
            x, y, TER_TYPE(t), TER_VAR(t), TILE_ROAD(t), TILE_ROAD_MASK(t), (t->road & TR_POWER_LINE) ? 1 : 0,
            TILE_ZONE(t), (t->zone & ZF_POWER_NET) ? 1 : 0, (t->zone & ZF_POWERED) ? 1 : 0,
            (t->zone & ZF_WATERED) ? 1 : 0, (t->zone & ZF_WATER_NET) ? 1 : 0, (t->flags & TF_PIPE) ? 1 : 0,
            t->flags, t->lot, t->lv, t->aux);
    if (t->lot) {
        const Lot *l = &g_world.lots[t->lot];
        fprintf(f, "  lot %d: %s at %d,%d rot %d var %d status %d build_left %d occ %d age %d flags %02X "
                   "problems %08X growth %d happy %d\n",
                t->lot, def_building(l->def)->key, l->x, l->y, l->rot, l->variant, l->status, l->build_left,
                l->occupants, l->age_months, l->flags, (unsigned)l->problems, l->growth, l->happy);
    }
}

/* ------------------------------------------------------------------ save */
int world_save(struct SaveFile *sf)
{
    int y, i;
    save_begin_chunk(sf, "TILE", 2);     /* F1: v2 = TF_PIPE / ZF_WATER_NET are meaningful */
    save_u16(sf, (u32)g_world.w);
    save_u16(sf, (u32)g_world.h);
    for (y = 0; y < g_world.h; y++) save_rle(sf, WT(0, y), g_world.w * (int)sizeof(Tile));
    save_end_chunk(sf);
    save_begin_chunk(sf, "LOTS", 1);
    save_u32(sf, (u32)g_world.lot_count);
    for (i = 1; i < g_world.lot_hi; i++)
        if (g_world.lots[i].def != B_NONE) { save_u16(sf, (u32)i); save_bytes(sf, &g_world.lots[i], sizeof(Lot)); }
    save_end_chunk(sf);
    return 1;
}

int world_load(struct SaveFile *sf)
{
    int ver, y, i, n, w, h;
    if (!load_find_chunk(sf, "TILE", &ver)) return 0;
    w = (int)load_u16(sf); h = (int)load_u16(sf);
    if (w < 8 || h < 8 || w > MAP_MAX_W || h > MAP_MAX_H) return 0;
    memset(g_world.t, 0, sizeof(g_world.t));
    memset(g_world.lots, 0, sizeof(g_world.lots));
    g_world.w = w; g_world.h = h;
    for (y = 0; y < h; y++) load_rle(sf, WT(0, y), w * (int)sizeof(Tile));
    if (ver < 2) {
        /* F1 migration (WATER.md 5): v1 had water mains inside gravel roads, streets and main roads; lay a
           pipe under each of them so an old city keeps roughly the same water. Bits 6/7 were unused in v1. */
        int x;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++) {
                Tile *t = WT(x, y);
                int c = TILE_ROAD(t);
                t->flags &= ~TF_PIPE;
                t->zone &= ~ZF_WATER_NET;
                if (c && (g_roads[c].flags & RDF_CARRIES_WATER)) t->flags |= TF_PIPE;
            }
        log_printf("world: TILE v%d save: pipes laid under the old water mains (roads)", ver);
    }
    if (load_find_chunk(sf, "LOTS", &ver)) {
        n = (int)load_u32(sf);
        for (i = 0; i < n && load_ok(sf); i++) {
            int id = (int)load_u16(sf);
            if (id <= 0 || id >= MAX_LOTS) return 0;
            load_bytes(sf, &g_world.lots[id], sizeof(Lot));
        }
    }
    if (!load_ok(sf)) return 0;
    world_recalc();
    world_notify_all(WCHG_ALL);
    return 1;
}
