/* mapgen.h - procedural terrain. PORTABLE, deterministic from the seed. Owner: T06. DESIGN 3.

   Writes g_world directly (world_new, then terrain into g_world.t[], national road via world_set_road
   with TF_PROTECTED | TF_EDGE_LINK on its edge tiles, optional railway), then world_recalc() and
   world_notify_all(WCHG_ALL). Must produce shores the autotiler can draw (no single-tile diagonal
   pinches), buildable land >= 55%, and a national road connected edge to edge.

   Integer-only (same map on VC6 and on the Mac for the same seed and size). Pipeline (mapgen.cpp):
   value-noise fields -> lakes (percentile threshold + cellular automaton + speck removal) -> optional
   river (least-cost path along a noise height, edge to edge or edge to lake/sea) -> shore cleanup
   (mapgen_shore_errors rules below) -> forest/rock masks -> national road (least-cost path with turn
   penalty and straight bridge jumps) -> town site -> optional railway -> farm fields -> forest species,
   meadows, single trees, Tile.aux.

   What the generator writes per tile:
     terrain  TER_GRASS (variant 0/1, 2 = meadow), TER_FOREST (variant FOREST_*), TER_ROCK, TER_WATER
              (lake/sea), TER_RIVER (variant: bits 0-1 flow DIR_*, RIVER_RAPIDS_BIT on the rapids),
              TER_FIELD (FIELD_CROP / FIELD_PLOUGH)
     road     ROAD_MAIN for the national road (on water = bridge, straight runs <= BRIDGE_MAX_LEN),
              ROAD_RAIL for the railway; both TF_PROTECTED, end tiles TF_EDGE_LINK
     flags    TF_TREE (single trees on grass)
     aux      forest: density 1..15; water/river: distance to the nearest land tile 1..15

   SHORE AUTOTILE RULE (contract with the renderer, art shore01..14, STYLE.md 4): shores are drawn on
   LAND tiles; water tiles (TER_WATER, TER_RIVER) are always full water (bridges need full water tiles).
   A land tile's corner (vertex) is water if any of the up-to-4 map tiles that meet at that vertex is
   water; out-of-map tiles count as land. Corner bits: n=1 vertex (x,y), e=2 (x+1,y), w=4 (x,y+1),
   s=8 (x+1,y+1). mapgen_shore_mask() returns that mask: 15 = plain land, 1..14 = sprite shore<NN>
   (06/09 = diagonal isthmus), 0 for water tiles. The generator guarantees (mapgen_shore_errors() == 0):
     - every land tile lies in a 2x2 block of land tiles inside the map (so its mask is never 0),
     - every lake tile lies in a 2x2 block of water tiles (no specks; rivers may be 1 tile wide),
     - no 2x2 window is a diagonal checkerboard (no land or water touching only at a corner). */
#ifndef MAPGEN_H
#define MAPGEN_H
#include "common.h"

enum { MAP_PRESET_LAKELAND, MAP_PRESET_RIVER, MAP_PRESET_COAST, MAP_PRESET_FOREST, MAP_PRESET_COUNT };

typedef struct {
    u32 seed;
    int preset;                /* MAP_PRESET_* */
    int w, h;                  /* default MAP_DEF_W x MAP_DEF_H (32..256) */
    int railway;               /* 1 = railway strip along one edge */
    int river;                 /* -1 = preset default, 0 = none, 1 = force */
} MapGenParams;

typedef struct {
    int water_pct, forest_pct, rock_pct, field_pct, buildable_pct;
    int pinches_fixed;         /* shore cleanup count */
    int road_x0, road_y0, road_x1, road_y1;   /* national road end tiles */
    double ms;                 /* generation time */
    /* T06 additions */
    int open_pct;              /* grass + field (buildable without clearing or blasting) */
    int road_tiles, bridge_tiles, bridge_max;  /* national road length, tiles on water, longest bridge */
    int road_forest_pct;       /* forest share of the land within 4 tiles of the national road */
    int town_x, town_y;        /* suggested town hall site: a national road tile in a cleared area */
    int river;                 /* 1 if a river was carved */
    int rapids_x, rapids_y;    /* first rapids tile (-1 = none) */
    int rapids_tiles;          /* river tiles with RIVER_RAPIDS_BIT */
    int hydro_sites;           /* positions where world_lot_check(B_HYDRO_PLANT) == PLACE_OK */
    int railway;               /* 1 if the railway was laid */
    int rail_x0, rail_y0, rail_x1, rail_y1;
    int shore_errors;          /* mapgen_shore_errors() after generation (must be 0) */
    int lakes, islands;        /* water bodies / land bodies not touching the edge */
} MapGenReport;

void mapgen_defaults(MapGenParams *p);           /* Lakeland, 128x128, seed 1, railway off */
int  mapgen_generate(const MapGenParams *p);     /* 1 ok (also fills the report) */
const MapGenReport *mapgen_report(void);
const char *mapgen_preset_name(int preset);      /* "Lakeland" */
int  mapgen_preset_from_name(const char *name);  /* "lakeland" -> MAP_PRESET_LAKELAND, -1 unknown */

/* T06 additions (work on the current g_world, any source: generated or loaded) */
int  mapgen_shore_mask(int x, int y);            /* shore corner mask, rule above */
int  mapgen_shore_errors(void);                  /* count of rule violations (0 = every shore drawable) */
int  mapgen_road_connected(void);                /* 1 if two TF_EDGE_LINK road tiles (not rail) on
                                                    different map edges are connected by road tiles */
void mapgen_dump_ascii(FILE *f);                 /* map with species/rapids/bridges + legend (tests) */

#endif
