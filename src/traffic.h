/* traffic.h - v1.0 traffic: road graph, block O/D, cluster Dijkstra with BPR congestion, gravity split,
   mode split (car / bus / walk-cycle), method of successive averages. PORTABLE logic, deterministic,
   integer-only. Owner: M2 (was T08's MVP). DESIGN 4.1, 5.8; DESIGN_LESSONS 3.5, 4.7.
   Cosmetic cars are NOT here: they are life.h / lifecar.h (T11), driven by traffic_flow() (Tile.aux).
   Bus lines, stops and the railway station live in transit.h (M2); traffic reads them at each pass start.

   Model (details and constants at the top of traffic.cpp):
   - GRAPH: nodes = car-road tiles that are junctions, dead ends, class changes, map-edge links, or every
     4th tile of a long run; edges = the runs between them (one road class each). Rebuilt from the tiles
     when a world listener saw WCHG_ROAD (at the next pass start; a running pass keeps its graph, which is
     saved with it). Motorways give no zone access and link to other roads only at motorway ends and at
     crossings (a road on both sides of the motorway tile): traffic_link_ok().
   - O/D: every occupied home puts its workers (work trips) and residents (shopping trips) on the graph node
     of its access road; lots with jobs are destinations, merged into <= 32 clusters of 8x8-tile blocks
     (smallest cluster joins its nearest until 32 remain).
   - PASS: one multi-source Dijkstra per destination cluster over edge costs
       len * tile_time(class) * (1 + 0.15 (flow/capacity)^4)            (BPR)
     then for every node: car time (+ parking), walk/cycle time (road length x 40 TU), bus time (walk to a
     stop of a line that also serves the cluster + wait + ride; buses get half the congestion delay); mode
     split by car ownership (era) and the time differences; gravity weight J_d * exp(-t / kappa). Two sweeps
     per pass: A = every node's exact normaliser (sum over clusters) + commute potential, B = the same
     Dijkstras again with exact shares, car trips pushed down the shortest-path tree (leaves to root, O(N)).
     Passes are time-sliced: traffic_slice() runs a bounded amount of cluster work in every sim step (a
     deterministic work quota, never wall time; also the between-pass stages: cells, graph rebuild, pass
     start); traffic_step() (phase 14) closes a finished pass. A 20k town needs ~4 cycles per pass (about a
     month), a small one passes every cycle.
   - MSA: edge flow += (assigned - flow) / k, k = 2 after a graph change or a halving/doubling of demand,
     +1 per pass up to 20; the smoothed flows are rescaled by the change of total demand first, so growth
     shows at once and only the route split is averaged (converges, no oscillation).
   - OUTPUTS (written at pass end only): road Tile.aux = flow level 0..255 (128 = the class capacity),
     FLD_TRAFFIC (sum of road levels / 8 per cell), FLD_NOISE (road class noise x level), commute time per
     2x2 field cell in "commute tiles" (mode-weighted gravity-average trip, TU_COMMUTE = 80 TU per unit, scaled
     to match the MVP's nearest-jobs road distance the sim terms were tuned on; congestion raises it; 255 =
     no route to any jobs, which is the REQ_ROUTE checklist item), rail access per cell, bus ridership per
     line, city mode shares. */
#ifndef TRAFFIC_H
#define TRAFFIC_H
#include "common.h"

#define TRAFFIC_BLOCK 8                     /* tiles per O/D block side */

void traffic_init(void);
void traffic_reset(void);                   /* new city / load: clears everything (listener registered) */
void traffic_step(void);                    /* phase 14: finish a completed pass + start the next + a slice */
int  traffic_flow(int x, int y);            /* road tile flow level 0..255, 128 = capacity (0 = not a road) */
s32  traffic_flow_raw(int x, int y);        /* car trips/day on the tile ("Flow 1,840 / capacity 1,200") */
int  traffic_commute(int x, int y);         /* commute time (commute tiles) at this tile's cell; 255 none */
s32  traffic_avg_commute(void);             /* residents' mean commute (commute tiles) */
int  traffic_edge_connected(int x, int y);  /* 1 if the road at (x,y) reaches a TF_EDGE_LINK tile */

/* ---- T08 additions (kept) */
int  traffic_next_dir(int x, int y);        /* DIR_* toward the biggest job cluster along its shortest-path
                                               tree (cars may use it); -1 none / not a road / no pass yet */
s32  traffic_total_trips(void);             /* commuters (work trips) routed in the last pass */
u32  traffic_passes(void);                  /* completed passes since reset */
struct SaveFile;
int  traffic_save(struct SaveFile *sf);     /* chunk "TRAF" v2: outputs, graph, running pass, transit */
int  traffic_load(struct SaveFile *sf);     /* call AFTER traffic_reset (v1 chunks: ignored, rebuilt) */

/* ---- M2 v1.0 additions */
void traffic_slice(void);                   /* sim_step calls it in every phase except 14: bounded pass work */
const u8 *traffic_commute_map(void);        /* per field cell (stride FIELD_MAX_W): commute tiles, 255 = no
                                               route; gaps (new roads) and before the first pass: net's COV_JOBS */
int  traffic_route_ok(int x, int y);        /* 1 = residents here can reach jobs (or the town has no jobs yet) */
int  traffic_capacity(int x, int y);        /* capacity of the road class at (x,y) (0 none) */
int  traffic_link_ok(int x, int y, int dir);/* car link between (x,y) and its neighbour (motorway rule) */
int  traffic_rail_access(int x, int y);     /* M3 hook: 1 if within RAIL_ACCESS_REACH road tiles of an active
                                               railway station (freight), else 0 */
int  traffic_rail_dist(int x, int y);       /* road tiles to the nearest active station, 255 none */
#define RAIL_ACCESS_REACH 20

typedef struct {
    s32 flow, capacity;                     /* car trips/day, class capacity */
    int level_pct;                          /* flow * 100 / capacity */
    int road_class;
    int top_pct;                            /* share of the tile's flow of the top origin -> destination pair */
    int top_from_x, top_from_y;             /* a tile in the origin district (text_district_at) */
    int top_to_x, top_to_y;                 /* representative tile of the destination cluster */
    int top_to_kind;                        /* TQK_* */
    int bus_riders;                         /* rides/day of bus lines using this tile (0 none) */
    int lines;                              /* bit mask of bus lines on this tile */
} TrafficQuery;
enum { TQK_JOBS, TQK_INDUSTRY, TQK_SHOPS, TQK_CIVIC };
int  traffic_query(int x, int y, TrafficQuery *out);   /* 0 = not a car road. Breakdown recomputed on demand
                                                          (one Dijkstra per cluster, cached per pass) */
int  traffic_query_text(int x, int y, char lines[][96], int max);  /* ready lines for the Query panel */

typedef struct {
    int nodes, edges, clusters, origins;    /* graph + current pass */
    int msa_k;                              /* MSA divisor used at the last pass end */
    int pass_running, cluster_cursor;
    u32 passes, rebuilds;
    int car_pct, bus_pct, slow_pct;         /* work trips by mode, last pass */
    int own_pct;                            /* car ownership by era */
    s32 trips;                              /* commuters routed */
    s32 vehicles;                           /* car trips/day put on the roads */
    double last_slice_ms, max_slice_ms;     /* timing (debug; not simulation input) */
    double last_boundary_ms, max_boundary_ms;
    double last_rebuild_ms;
} TrafficStats;
void traffic_stats(TrafficStats *out);
int  traffic_car_own_pct(int year);         /* car ownership by year: 25% 1955 -> 85% 1975 */
void traffic_dump(FILE *f);                 /* DEBUG FROM TEXT: graph/pass summary + busiest edges */
/* ---- PL4 addition */
void traffic_slice_phase(int phase);        /* sim_step's slice: phase 9's sweep slice runs in phase 10 instead
                                               (same results; phase 9 has the network solves) */

#endif
