/* world.h - the map: Tile and Lot arrays, terrain/road/zone setters, lot placement/removal, queries,
   change notification, text dumps. PORTABLE. Owner: T04. DESIGN 3, 4, 5.2.

   Ownership of the data: g_world is read by everyone. WRITE only through the setters below for anything
   visible (terrain, roads, power lines, zones, tile flags, lots' def/status/variant): they keep autotile
   masks consistent and notify listeners (the renderer invalidates chunks, the minimap redraws, the sim
   marks networks dirty). Fields that are not drawn (Tile.lv, Tile.aux, Lot.occupants, growth, problems,
   happy, age, flags LF_*) may be written directly by the simulation modules that own them.
   Bulk writers (mapgen, save load) may write g_world.t[] directly, then call world_recalc() and
   world_notify_all(WCHG_ALL).

   Coordinates: tile (x, y), 0 <= x < g_world.w, 0 <= y < g_world.h; x runs down-right (+X, SE),
   y down-left (+Y, SW) on screen (common.h). Rects are INCLUSIVE tile rects x0..x1, y0..y1. */
#ifndef WORLD_H
#define WORLD_H
#include "common.h"
#include "defs.h"

/* ------------------------------------------------------------------ Tile (8 bytes) */
typedef struct {
    u8  terrain;  /* bits 0-3 TER_*; bits 4-7 variant (defs.h: grass 0-2, FOREST_*, FIELD_*, river) */
    u8  road;     /* bits 0-2 ROAD_* class (0 none; road on water = bridge);
                     bit 3 TR_POWER_LINE (poles/wires on this tile; may share a road tile = crossing);
                     bits 4-7 road autotile mask of connected road neighbours: (1 << DIR_*) << 4.
                     Maintained by world_set_road / world_recalc; rail connects only to rail. */
    u8  zone;     /* bits 0-2 ZONE_*; bit 3 ZF_POWER_NET (in a network with a plant);
                     bit 4 ZF_POWERED (supplied: NET without POWERED = brownout); bit 5 ZF_WATERED
                     (F1: pipe / zoned tile: water reaches it within its source's reach; lot tile: lot is
                     watered); bit 6 ZF_HEAT (v1.0); bit 7 ZF_WATER_NET (F1: in a water network with a
                     source: NET without WATERED = short). Bits 3-7 are written by net.cpp. */
    u8  flags;    /* TF_* */
    u16 lot;      /* index into g_world.lots (0 = none) */
    u8  lv;       /* land value 0..255 cached per tile (fields.cpp) */
    u8  aux;      /* per-kind scratch: road tile = traffic flow level 0..255 (traffic.cpp);
                     forest = density 0..15 (mapgen); water = distance to shore 0..15 (mapgen) */
} Tile;
STATIC_ASSERT(sizeof(Tile) == 8, tile_is_8_bytes);

#define TER_TYPE(t)      ((t)->terrain & 15)
#define TER_VAR(t)       ((t)->terrain >> 4)
#define TR_CLASS_MASK    0x07
#define TR_POWER_LINE    0x08
#define TR_MASK_SHIFT    4
#define TILE_ROAD(t)     ((t)->road & TR_CLASS_MASK)
#define TILE_ROAD_MASK(t) ((t)->road >> TR_MASK_SHIFT)        /* bit (1 << DIR_*) */
#define ZF_ZONE_MASK     0x07
#define ZF_POWER_NET     0x08
#define ZF_POWERED       0x10
#define ZF_WATERED       0x20
#define ZF_HEAT          0x40
#define ZF_WATER_NET     0x80     /* F1 (WATER.md 4.1) */
#define TILE_ZONE(t)     ((t)->zone & ZF_ZONE_MASK)

enum { TF_PROTECTED  = 0x01,   /* cannot be bulldozed (national road, initial railway) */
       TF_EDGE_LINK  = 0x02,   /* road tile at the map edge that links to the outside world */
       TF_LOT_FRONT  = 0x04,   /* front-most (south, max x+y) tile of a lot: its sprite anchors here */
       TF_TREE       = 0x08,   /* a single decorative tree on grass (art tree_*, variant by hash);
                                  removed by roads, zoning growth and buildings */
       TF_BURNING    = 0x10,   /* on fire (v1.0) */
       TF_RUBBLE     = 0x20,   /* burnt-out / demolished remains (v1.0) */
       TF_PIPE       = 0x40,   /* F1: water pipe (underground: may share a tile with anything; WATER.md 3.2) */
       TF_RAIL       = 0x80 }; /* 1.2 X1: the railway runs under this car-road tile: a level crossing (gravel road,
                                  street, main road) or a motorway overpass. Only with a car road class (1..4);
                                  the tile is rail for rail checks (TILE_HAS_RAIL) and a road for everything else.
                                  Never set in 1.0/1.1 saves (bit 7 was reserved/zero), so no TILE version bump.
                                  world_set_road keeps it consistent (world_set_road below) */
#define TILE_HAS_RAIL(t) (TILE_ROAD(t) == ROAD_RAIL || ((t)->flags & TF_RAIL))   /* rail or a crossing/overpass */
#define TILE_IS_XING(t)  (((t)->flags & TF_RAIL) && TILE_ROAD(t) && TILE_ROAD(t) != ROAD_RAIL)

/* ------------------------------------------------------------------ Lot (20 bytes, one per building) */
enum { LOT_BUILDING,           /* construction stage sprite; build_left months remain */
       LOT_ACTIVE,
       LOT_ABANDONED,          /* greyed variant; occupants 0; reversible */
       LOT_BURNING,            /* v1.0 */
       LOT_STATUS_COUNT };
enum { LF_ROAD     = 0x01,     /* has road access (LF_* are cached by the sim each growth scan) */
       LF_POWERED  = 0x02,
       LF_WATERED  = 0x04,
       LF_HEAT     = 0x08,
       LF_NEW      = 0x10,     /* placed by the player this month (25% bulldoze refund, undo) */
       LF_PLAYER   = 0x20 };   /* placed by the player (vs grown) */

typedef struct {
    u8  def;          /* B_* ; B_NONE = free slot */
    u8  x, y;         /* NORTH (back, min x / min y) tile of the footprint */
    u8  rot;          /* ROT_*: side the front faces (common.h) */
    u8  variant;      /* art variant 0 .. def->variants-1 (picked by hash at growth) */
    u8  status;       /* LOT_* */
    u8  flags;        /* LF_* */
    u8  build_left;   /* months of construction left */
    u16 occupants;    /* residents (R) or workers (C/I, services, plants) present now */
    u16 age_months;
    u32 problems;     /* bits (1 << REQ_*) unmet for the NEXT tier (growth scan cache; query/overlay) */
    s8  growth;       /* hysteresis counter: > 0 toward build/upgrade, < 0 toward decline */
    u8  happy;        /* 0..100 */
    u16 spare;        /* reserved (keep the struct 20 bytes) */
} Lot;
STATIC_ASSERT(sizeof(Lot) == 20, lot_is_20_bytes);

/* ------------------------------------------------------------------ the world */
typedef struct {
    int  w, h;                         /* playable size (default 128) */
    Tile t[MAP_MAX_W * MAP_MAX_H];     /* index TILE_IDX(x, y) (stride 256) */
    Lot  lots[MAX_LOTS];               /* slot 0 unused */
    int  lot_hi;                       /* iteration bound: all used slots are < lot_hi */
    int  lot_count;                    /* used slots */
    u32  serial;                       /* bumped by every world_notify (cheap "changed?" checks) */
} World;
extern World g_world;

#define WT(x, y)  (&g_world.t[TILE_IDX(x, y)])     /* unchecked tile pointer */

void  world_init(void);                /* once at startup (keeps listeners across world_new) */
void  world_new(int w, int h);         /* all grass, no lots; notifies WCHG_ALL */
int   world_in(int x, int y);
const Tile *world_tile(int x, int y);  /* checked: out-of-map returns a static protected grass tile */
Lot  *world_lot(int id);               /* NULL for 0 / out of range / free slot */
int   world_lot_at(int x, int y);      /* lot id or 0 */

/* ---- setters (all notify) */
void  world_set_terrain(int x, int y, int ter, int variant);
void  world_set_road(int x, int y, int cls);       /* ROAD_NONE removes; updates masks of 5 tiles.
                                                      1.2 X1 on a rail tile: a car class makes a crossing/overpass
                                                      (TF_RAIL, class = cls); ROAD_NONE or ROAD_RAIL on a crossing
                                                      removes the road and leaves the rail (plain ROAD_RAIL).
                                                      Callers (tools) validate where crossings may go. */
void  world_set_power_line(int x, int y, int on);
void  world_set_zone(int x, int y, int zone);      /* ZONE_NONE dezones; keeps network bits */
void  world_set_tile_flags(int x, int y, int set, int clear);
void  world_set_pipe(int x, int y, int on);        /* F1: TF_PIPE; notifies WCHG_PIPE; does NOT bump
                                                      world_struct_serial (water compares its own graph) */
int   world_pipe_tiles(int *under_water);          /* F1: running count of pipe tiles (budget upkeep) */

/* ---- lots. (x, y) is always the NORTH tile; footprint from def_footprint(def, rot). */
enum { PLACE_OK, PLACE_OUT_OF_MAP, PLACE_WATER, PLACE_ROAD, PLACE_OCCUPIED, PLACE_PROTECTED,
       PLACE_NEEDS_SHORE, PLACE_NEEDS_RAPIDS, PLACE_UNIQUE, PLACE_NO_SLOTS, PLACE_COUNT };
int   world_lot_check(int def, int x, int y, int rot);   /* PLACE_* (cost/unlock checks are tools') */
int   world_lot_place(int def, int x, int y, int rot, int variant, int status); /* id or 0; clears
                                                            forest/trees under it, keeps zone */
void  world_lot_remove(int id);                          /* frees the slot; zone stays */
void  world_lot_set_def(int id, int def);                /* tier change, same footprint */
void  world_lot_set_status(int id, int status);
void  world_lot_set_variant(int id, int variant);
void  world_lot_rect(int id, int *x0, int *y0, int *x1, int *y1);  /* inclusive footprint */
int   world_lot_access(int id, int *rot);  /* best adjacent ZONE_ACCESS road class (0 none) and the
                                              rotation that faces it */
int   world_footprint_road_dist(int x0, int y0, int x1, int y1, int maxd, int *rot); /* T04: Chebyshev
                                              distance 1..maxd from the inclusive rect to the nearest
                                              ZONE_ACCESS road (0 = none) and the rotation facing it
                                              (grown lots: front faces the nearest road) */

/* ---- queries */
int   world_is_buildable(int x, int y);  /* land, no road, no lot, not protected */
int   world_road_within(int x, int y, int dist); /* 1 if a ZONE_ACCESS road is within Chebyshev
                                                    distance dist (zoning rule, DESIGN 4.2) */
int   world_count_zone(int zone);        /* tiles with that zone (stats/tests) */
u32   world_hash(void);                  /* FNV over tiles in w x h + used lots (determinism tests) */
u32   world_struct_serial(void);         /* T04: bumped by every change except lot placement and lot
                                          def/status/variant changes (net.cpp incremental solves).
                                          F1: also by zoning/dezoning (zoned land conducts power), not
                                          by pipes */
void  world_road_stats(int *roads, int *bridges, int *lines); /* T04: running tile counts per road class
                                          (arrays of ROAD_CLASS_COUNT, may be NULL) and power-line tiles */

/* ---- change notification (renderer, minimap, sim networks, life). Listeners are called synchronously
   inside the setter; they must be cheap (record the rect) and must NOT modify the world. */
enum { WCHG_TERRAIN = 0x01, WCHG_ROAD = 0x02, WCHG_POWER_LINE = 0x04, WCHG_ZONE = 0x08,
       WCHG_LOT = 0x10,       /* lot placed/removed/def/status/variant changed */
       WCHG_NET = 0x20,       /* ZF_POWER_NET/POWERED/WATERED changed (net.cpp; overlays only) */
       WCHG_FIELDS = 0x40,    /* field values changed (fields.cpp; overlays only; usually whole map) */
       WCHG_FLAGS = 0x80,     /* tile flags (trees, fire, rubble) */
       WCHG_PIPE = 0x100,     /* F1: TF_PIPE changed (rect includes the 4 neighbours: pipe autotile masks);
                                 drawn only in the water overlay, so the renderer treats it like WCHG_NET */
       WCHG_ALL = 0x1FF };
typedef void (*WorldChangeFn)(int x0, int y0, int x1, int y1, int kinds, void *user);
#define WORLD_MAX_LISTENERS 8
int   world_add_listener(WorldChangeFn fn, void *user);  /* slot, or -1 if full */
void  world_remove_listener(WorldChangeFn fn, void *user);
void  world_notify(int x0, int y0, int x1, int y1, int kinds);  /* clamps, bumps serial, calls */
void  world_notify_all(int kinds);
/* Batching (tools commit, sim growth scan): between begin/end, notifications are merged into a short
   list of rects (T13: up to 8; nearby rects merge, each keeps its own kinds), and the outermost end calls
   the listeners once per rect. Nests; the outermost end flushes. */
void  world_batch_begin(void);
void  world_batch_end(void);

/* ---- maintenance */
void  world_recalc(void);              /* rebuild road masks, TF_LOT_FRONT, lot_hi/free list after bulk
                                          writes (mapgen, load) */

/* ---- text dumps (DEBUG FROM TEXT): one char per tile with a legend, and full detail of one tile.
   Legend: '~' water, 'r' river, 'T' forest, '^' rock, '.' grass, ',' field, '#' road, '*' road with
   power line, '=' rail, '+' power line, 'h a c i' empty zoned tiles, 'H A C I' tier-1 lots (lower case
   while under construction), houses tier 2/3 '2' '3', apartments/commercial/industry tier 2 'B' 'D' 'J'
   (tier 3 'Q' 'E' 'L'), '%' abandoned, 'P' plant, 'W' water building, 'S' service, 'K' park,
   'X' special. The dump prints the legend itself. */
void  world_dump(FILE *f, int x0, int y0, int x1, int y1);
void  world_dump_tile(FILE *f, int x, int y);

/* ---- save (save.h chunks "TILE", "LOTS") */
struct SaveFile;
int   world_save(struct SaveFile *sf);
int   world_load(struct SaveFile *sf);  /* then world_recalc + notify_all inside */

int   world_last_tile_version(void);   /* TILE chunk version of the last world_load (1 = pre-pipes save) */

/* ---- 1.2 X1 railway crossings */
int   world_rail_mask(int x, int y);   /* rail autotile mask (1 << DIR_*) of a rail or crossing tile: plain rail =
                                          its road mask; crossing = neighbours that carry rail; 0 = no rail */
int   world_rail_axis(int x, int y);   /* 5 (rail along Y, NE-SW) or 10 (along X) for a crossing/straight rail,
                                          0 = not straight */
int   world_xing_count(void);          /* tiles with TF_RAIL (level crossings + motorway overpasses) */

#endif
