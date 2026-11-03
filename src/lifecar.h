/* lifecar.h - cosmetic car logic (movement, lanes, turning, spacing, spawn/despawn). PORTABLE (Mac-tested
   in tests/test_life.cpp). Owner: T11. Drawing, smoke and shimmer are in life.cpp (Windows side).
   DESIGN 5.8; STYLE.md 9 (right-hand traffic, lane offsets, directions).

   Positions are in 1/256 tile ("fx", iso.h iso_pos_to_world); 1 art unit = 8 fx (a tile is 32 units).
   A car is always on one road tile (x, y). It entered heading `din` (DIR_*, the direction of travel)
   and leaves through edge `dout` (chosen on entry): dout == din straight, din+1 right turn, din+3 left
   turn (quarter arcs about the tile corner, like the road art's curves, radius 128 fx -/+ the lane),
   dout == opposite(din) U-turn (to the tile centre, semicircle, back). `s` = progress along that path
   (fx x 256), `len` = path length. Lane = right-hand side: lateral offset `lat` to the right of the
   heading, right(dx, dy) = (-dy, dx), i.e. heading +X drives at y + lat, heading +Y at x - lat.
   Lane offsets by the road class of the tile: gravel 2.5 units (20 fx), street 3.6 (29), main 4.8 (38),
   motorway 5.0 / 9.6 (40 / 77, two lanes). On a straight tile the lateral offset blends from the previous
   tile's to this tile's, so class changes do not jump.

   Spacing: per-tile car lists (rebuilt every update). A car yields to the car ahead in its lane: same
   tile and same entry (further along), same tile and same exit lane but entered from elsewhere (closer
   to the exit: merges), or the next tile having entered through our exit. It brakes so it never closes
   below LC_GAP (centre to centre). A car stopped for 3 s ignores others for 1.5 s (deadlock breaker,
   e.g. a loop full of cars). Crossing traffic at junctions is not resolved (cosmetic).

   Turning: on entering a tile the exit is chosen among connected car-road neighbours except back,
   weight 24 + flow(neighbour), x1.5 straight on; none -> U-turn. Edge-link tiles may exit off the map:
   the car leaves (despawns). Flow = traffic_flow() once traffic.cpp writes any (checked by a slow scan),
   until then an estimate from the occupants of lots within 2 tiles plus a road-class bonus. */
#ifndef LIFECAR_H
#define LIFECAR_H
#include "common.h"

#define LC_MAX_CARS   160
#define LC_CAR_CAP    150        /* target never exceeds this */
#define LC_GAP        150        /* fx centre to centre (car ~17.5 units long incl. the 1.25x art) */
#define LC_COLOURS    12         /* car0..car11 */

typedef struct {
    u8  used;
    u8  x, y;                    /* tile */
    u8  din, dout;               /* DIR_* */
    u8  col;                     /* colour 0..LC_COLOURS-1 */
    u8  lane;                    /* motorway lane 0 (inner) / 1 (outer) */
    u8  spd_pct;                 /* personal speed factor 88..112 */
    s16 lat0, lat;               /* lateral offset fx at entry / target on this tile */
    s32 s, len;                  /* progress / path length, fx * 256 */
    s32 v;                       /* speed, fx * 256 per second */
    s32 age_ms, life_ms;
    u16 stuck_ms, ghost_ms;
    s32 fx, fy;                  /* position after the last update (absolute, 1/256 tile) */
    u8  dir;                     /* sprite heading DIR_* after the last update */
    u8  ghosted;                 /* has used the deadlock breaker (tests) */
    u8  pad[2];
} LcCar;

/* The area the cars live in, in world pixels at `zoom` (iso.h): spawn/keep inside vis grown by margin;
   only the ring outside vis is used for spawning once the area holds half of the target. */
typedef struct {
    int zoom;
    int vx0, vy0, vx1, vy1;      /* visible world-pixel rect (exclusive max) */
    int margin;                  /* px around it */
} LcView;

typedef struct {
    int year;                    /* car ownership: 25% in 1955 .. 100% in 1970 (120% max); not applied
                                    to traffic.cpp's flow level, which already has the era's car share */
    s32 population;              /* caps the count: 6 + pop * ownership / 10 */
    int frozen;                  /* 1: no movement (paused); spawning/despawning still runs */
} LcParams;

typedef struct {
    int alive, target, visible, blocked, ghosts;
    u32 spawned, despawned;
    u32 gone_offview, gone_life, gone_road, gone_edge, gone_excess;
    u32 turns, uturns, tiles;
    int traffic_live;            /* 1 = using traffic_flow() */
} LcStats;

void lc_init(void);
void lc_reset(u32 seed);
void lc_update(int dt_ms, const LcView *v, const LcParams *p);
int  lc_count(void);
const LcCar *lc_cars(void);      /* LC_MAX_CARS slots, check .used */
/* Slots in ascending depth order (x + y of the tile under the centre), for cheap sprite sorting. */
int  lc_draw_order(const u8 **order);
void lc_stats(LcStats *out);

/* ---- helpers (also used by the tests) */
int  lc_car_road(int x, int y);  /* in map, road class with RDF_CARS */
int  lc_flow(int x, int y);      /* flow level 0..255 used for spawning/turning */
int  lc_own_pct(int year);
int  lc_lane_offset(int road_class, int lane);
void lc_car_point(const LcCar *c, s32 *fx, s32 *fy, int *dir);   /* position on its path now */
/* tests: spawn a car at a given tile/entry/exit/progress (0 if refused), force a constant flow level
   (-1 = normal) */
int  lc_debug_spawn(int x, int y, int din, int dout, s32 s, int col);
void lc_debug_force_flow(int flow);
s32  lc_debug_gap(int slot);     /* distance to the car ahead (fx * 256), large if none */
int  lc_debug_choose(int x, int y, int heading);   /* one random exit choice on entering (x, y) */

#endif
