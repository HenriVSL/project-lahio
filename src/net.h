/* net.h - power and water networks, service coverage BFS. PORTABLE. Owner: T04. DESIGN 4.3, 4.4, 5.6.

   Power: flood fill from all plants over conductive tiles (lots without BDF_NO_CONDUCT, power lines);
   brownout by BFS order when demand > supply (tiles furthest from plants lose power first).
   F1 (DESIGN 14.1-14.2, docs/research/WATER.md): zoned land without a lot conducts power too.
   Water: player-laid pipes (TF_PIPE) + conducting lots + zoned land form the graph; roads block unless
   piped. Every active source (pumping station: needs power; well; water tower: TOWER_OUTPUT if its
   network also has a producing pump/well) serves its nearest consumers along the graph until its output
   is used (its REACH); the network's spare then flows on to the remaining consumers nearest first; the
   furthest go dry. Water tower pressure zone (TOWER_PRESSURE_REACH tiles) = REQ_WATER_TOWER.
   Results go into Tile.zone bits ZF_POWER_NET/ZF_POWERED/ZF_WATERED/ZF_WATER_NET and Lot.flags LF_POWERED/LF_WATERED;
   net.cpp notifies WCHG_NET with the bounding rect of tiles that changed.
   Coverage: multi-source BFS over road tiles from each building of a service type, cut at
   ServiceDef.max_dist; each tile is assigned to its nearest building; load = sum of demand assigned;
   quality = min(1, capacity/load) x distance falloff (DESIGN 5.6). Storage is internal to net.cpp. */
#ifndef NET_H
#define NET_H
#include "common.h"
#include "defs.h"

enum { NET_POWER = 1, NET_WATER = 2, NET_COVERAGE = 4, NET_ALL = 7 };

void net_init(void);
void net_reset(void);                     /* new city / load: clears results, marks everything dirty */
void net_mark_dirty(int nets);            /* NET_* bits; sim's world listener calls this */
void net_step(void);                      /* phase 9: solves power and water if dirty (or monthly) */
void net_coverage_step(int svc);          /* phase 13: recompute one service type */

typedef struct {
    s32 supply, demand;                   /* units */
    s32 lots_served, lots_short;          /* lots connected+supplied / connected but short */
    s32 lots_unconnected;
    int networks;                         /* separate connected components with a source */
    s32 used;                             /* F1 water: units actually delivered (served consumers) */
} NetStats;
const NetStats *net_stats(int net);       /* NET_POWER or NET_WATER */

/* Coverage queries (tile coordinates). */
int  net_cov_lot(int svc, int x, int y);      /* serving building's lot id, 0 = none */
int  net_cov_dist(int svc, int x, int y);     /* road distance to it, 255 = none */
int  net_cov_quality(int svc, int x, int y);  /* 0..100 */
void net_service_load(int lot, s32 *load, s32 *capacity); /* for a service building */
int  net_catchment_tiles(int lot);            /* tiles assigned to this building */
int  net_water_tower_reach(int x, int y);     /* 1 if inside a water tower's pressure zone (a lot: any of
                                                 its tiles within TOWER_PRESSURE_REACH along the graph) */

/* ---- T04 additions.
   Coverage kinds: the 7 services (SVC_*) plus three internal kinds computed the same way (multi-source
   road BFS): COV_SHOP (from active commercial lots; a shop's load = residents whose nearest shop it is =
   its customers), COV_JOBS (from lots with jobs; distance = commute), COV_RES (from occupied homes;
   distance = workers' reach for industry). net_cov_* accept any kind < COV_KINDS. Coverage is stored on
   the 2x2 field-cell grid (DESIGN 5.2) and spread up to 2 cells off the road (zones reach 3 tiles).
   Power conducts across ONE plain road tile between two conductive tiles (no crossing line needed for
   every street; DESIGN 4.3 interpretation, see HANDOFF). */
#define COV_SHOP   SVC_COUNT
#define COV_JOBS   (SVC_COUNT + 1)
#define COV_RES    (SVC_COUNT + 2)
#define COV_KINDS  (SVC_COUNT + 3)
enum { NLS_NONE, NLS_SHORT, NLS_OK };     /* per-lot network state (overlays, query) */

int  net_coverage_next(void);             /* kind the sim should pass to net_coverage_step next (dirty
                                             kinds first, kinds without buildings are skipped) */
int  net_cov_max_dist(int kind);          /* BFS cut-off in road tiles */
s32  net_lot_load(int kind, int lot);     /* load assigned to a source lot (customers of a shop, pupils
                                             of a school, ...) */
int  net_building_quality(int lot);       /* service building: min(100, capacity*funding/load) */
int  net_road_reach(int x, int y);        /* Chebyshev distance to the nearest ZONE_ACCESS road, 0 on
                                             the road itself, 4 = none within 3 (cap) */
void net_mark_roads(int x0, int y0, int x1, int y1); /* road tiles changed in this rect (sim listener):
                                             road reach + road cells are updated incrementally */
void net_solve_all(void);                 /* solve power, water and every coverage kind now (tests,
                                             after load) */
int  net_lot_power_state(int lot);        /* NLS_* (lots that use no power: state of their network) */
int  net_lot_water_state(int lot);        /* NLS_* */
s32  net_source_output(int lot);          /* plant/pump/well/tower output now (pump: shore tiles x 40 if
                                             powered, else 0; tower: TOWER_OUTPUT if its network produces) */
int  net_element_factories(void);         /* active element factories (apartments T2+ requirement) */
const u8  *net_cov_q_map(int kind);       /* per-cell quality 0..100, stride FIELD_MAX_W (hot loops) */
const u8  *net_cov_dist_map(int kind);    /* per-cell distance, 255 = none */
const u16 *net_cov_src_map(int kind);     /* per-cell serving lot */
int  net_power_near(int x0, int y0, int x1, int y1); /* NLS_* for an EMPTY rect (zone growth): a
                                             4-neighbour (or the tile across one road) is in a network */
int  net_water_near(int x, int y);        /* NLS_* for an empty tile ("wet ground"): the tile or one within
                                             WATER_SPREAD has ZF_WATERED (OK) / ZF_WATER_NET only (SHORT) */

/* ---- F1 water queries (WATER.md 3.6). All values are from the last water solve. */
typedef struct {
    int  lot;                             /* the source lot */
    s32  output;                          /* units now (0: pump without power / tower without producer) */
    s32  used, own_used, shared_used;     /* used = own area + shared with other areas of the network */
    s32  spare;                           /* output - used */
    int  lots, lots_dry;                  /* consumers (water_use > 0) served / dry in its area */
    s32  dry_units;                       /* water the dry ones would need */
    int  reach;                           /* tiles along the graph to the furthest served consumer */
    int  at_capacity;                     /* its area has dry consumers */
    int  shore;                           /* pumping station: water tiles within 2 */
    int  needs_power;                     /* pumping station without power */
    int  pressure_lots;                   /* water tower: grown buildings in its pressure zone */
    int  network;                         /* network id (shared by connected sources) */
} WaterSrcInfo;
int  net_water_source(int lot, WaterSrcInfo *out);   /* 0 if lot is not an active water source */
int  net_lot_water_src(int lot, int *dist);          /* source lot whose area the lot is in (0 = not
                                                        connected) + distance along the graph */
int  net_water_tile_state(int x, int y);  /* NLS_* of a tile from its bits (pipes, zoned land, lots) */
int  net_water_tile_src(int x, int y);    /* source lot whose area the tile is in (0 = none) */
typedef struct {
    int networks, sources, lots_short, lots_unconnected;
    s32 supply, used;
    int best_spare_lot; s32 best_spare;    /* source with the most spare (0 = none) */
    int worst_lot, worst_dry;              /* source with the most dry consumers (ticker location) */
} WaterSummary;
void net_water_summary(WaterSummary *out);
void net_water_dump(FILE *f, int x0, int y0, int x1, int y1); /* DEBUG FROM TEXT: water layer map */
void net_water_mark_rect(int x0, int y0, int x1, int y1); /* sim's world listener: pipes, zones or roads
                                             changed (marks NET_WATER; lots are tracked by the solver) */
int  net_water_bfs_count(void);           /* tests: full graph solves so far */

struct SaveFile;   /* T10: save: solver results + flags (chunk "NET "); without it, load falls back
                      to net_solve_all() (correct, but not bit-identical to an unbroken run) */
int  net_save(struct SaveFile *sf);       /* T10: save */
int  net_load(struct SaveFile *sf);       /* T10: save; 0 = no NET chunk */

#endif
