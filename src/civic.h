/* civic.h - M5 services & housing systems: the element factory (precast output caps Apartments T2+ upgrades
   per month), district heating over the water pipe graph, sewage works + lake pollution, fires (fire.cpp).
   PORTABLE, deterministic, integer-only. Owner: M5 (Milestone 2). DESIGN 4.3-4.5, 5.4, 5.7, 14;
   THEME 2.4-2.6, 2.12-2.14; docs/HANDOFF.md "M5".

   ELEMENTS: each active element factory makes CIVIC_ELEM_OUTPUT precast elements a month, scaled by its
   staffing (occupants / jobs), power (none without) and politics_mod(PM_ELEMENTS_PCT). One element = one
   resident of apartment capacity: an Apartments upgrade to tier 2 or 3 takes (new capacity - old capacity)
   elements from this month's pool. The pool is refilled at the month end (unused elements don't carry
   over), so the factory caps how many apartment residents can be added per month. REQ_ELEMENTS: to reach
   the tier the pool must hold the upgrade's need; standing buildings keep their tier (keep = met).

   HEAT and SEWAGE are "pipe utilities" solved on the same graph as water (net.cpp / WATER.md 3.3): tiles with a
   pipe, tiles of conducting lots (any status) and zoned empty land without a road; roads block unless piped.
   Like water: a multi-source BFS from every active source (heat plants / sewage works) gives each consumer its
   distance and nearest source; each source serves its own consumers nearest first until its capacity is used
   (strict cut), then the network's spare serves the rest nearest first (global distance, lot id order).
     heat:   consumers = apartments, commercial tier 2+, services and special buildings (houses heat with
             their own stoves, industry with its own boilers); use = the def's water_use. Served lots get
             LF_HEAT; tiles in reach get ZF_HEAT (overlay). Apartments T3 need REQ_HEAT (NLS_OK; to upgrade
             also the network's spare must cover the extra use, so an edge lot doesn't flip-flop). Fuel: coal,
             CIVIC_HEAT_FUEL mk per 100 heat units delivered a month (BL_UTILITIES; M7 can scale it with
             civic_set_fuel_pct for oil prices).
     sewage: consumers = watered resident lots (houses T2+, apartments, watered farms); load = residents.
             Residents beyond a works' capacity, or not connected to one, are UNTREATED: their sewage goes to
             the nearest lake/river cell within CIVIC_LAKE_REACH cells. Unwatered homes (cottages, outhouses)
             don't count.
   Solves run on a fixed schedule (phase 9: heat on even cycles, sewage + lakes on odd cycles), so results
   depend only on the world at that step (save/load safe; the results are saved too).

   LAKE POLLUTION: per water cell (2x2 field grid) 0..255; emission from untreated residents (+ civic_lake_emit
   from other modules: M6 paper mill), 3x3 spread among water cells, decay 1/32 per update (rivers 1/8). Land
   cells within 2 cells of polluted water get a land value term LVT_LAKE (-shore/5, max -25: the lakeside
   bonus is gone) and a happiness penalty (-shore/8, max -12). Politics: the Intelligentsia and Rural blocs
   name it (civic_lake_reason).

   FIRES (fire.cpp): once per cycle (phase 12), if the New Game disasters setting is on, a fire may start in a
   wooden grown lot (BDF_WOODEN: houses, kiosks, workshops, sawmills, farms): chance = sum over wooden lots of
   the fire-risk field x CIVIC_FIRE_IGNITE_PPB per billion (fire risk = wooden density x (1 - fire cover) x dry
   summer, fields.cpp). Every 4 steps ("a day") a burning lot: is put out with chance CIVIC_FIRE_BUCKETS_PCT + fire
   cover x 60%; spreads to each wooden grown lot sharing an edge with CIVIC_FIRE_SPREAD_PCT x (1 - cover) (x1.5 in
   June-August); burns down after CIVIC_FIRE_BURN_TICKS -> lot removed, its tiles TF_RUBBLE (cleared by the
   bulldozer or after CIVIC_RUBBLE_MONTHS; growth waits for it). A saved lot becomes active again, empty (its
   residents were moved out). Events: SE_FIRE b = 0 when an outbreak starts; b = 1 + lots lost when it is over
   (a = residents displaced). Own RNG stream (saved): fires never shift the growth RNG.

   Crime (DESIGN 5.7) is a field (fields.cpp FLD_CRIME, police coverage, politics PM_CRIME_PCT); civic only
   reports it. */
#ifndef CIVIC_H
#define CIVIC_H
#include "common.h"
#include "defs.h"

#define CIVIC_ELEM_OUTPUT       300   /* elements (= apartment residents) per factory per month at full staff */
#define CIVIC_HEAT_FUEL         15    /* mk per 100 heat units delivered per month (coal) */
#define CIVIC_LAKE_REACH        12    /* cells (24 tiles): sewage reaches a lake this close, else flows away */
#define CIVIC_FIRE_IGNITE_PPB   140   /* ignition chance per cycle per unit of summed wooden-lot fire risk (per 1e9) */
#define CIVIC_FIRE_SPREAD_PCT   9     /* per day to each edge-adjacent wooden neighbour without fire cover */
#define CIVIC_FIRE_BUCKETS_PCT  15    /* per day: put out by the neighbours' buckets (no brigade needed) */
#define CIVIC_FIRE_BURN_TICKS   6     /* days a lot burns before it is lost */
#define CIVIC_RUBBLE_MONTHS     6     /* ruins left before the town clears them */
#define CIVIC_FIRE_MAX          48

void civic_init(void);
void civic_reset(void);                     /* sim_new_city */
void civic_step(int phase, int cycle);      /* sim_step, after the phase's own work */
void civic_month(void);                     /* sim month end (books part): element pool, fuel, rubble ageing */

/* ---- elements */
s32  civic_elements_output(void);           /* this month's pool size (all factories) */
s32  civic_elements_left(void);
s32  civic_elements_used_month(void);       /* taken so far this month */
int  civic_elements_take(s32 n);            /* 1 = taken (growth: an apartment upgrade) */
s32  civic_factory_output(int lot);         /* one factory's monthly output now */

/* ---- heat and sewage (pipe utilities) */
enum { UT_HEAT, UT_SEWAGE, UT_COUNT };
typedef struct {
    int  lot;
    s32  output;                            /* capacity: heat units / residents treated */
    s32  used, own_used, shared_used, spare;
    int  lots, lots_short;                  /* consumers served / short in its area */
    s32  short_units;
    int  reach;                             /* tiles along the graph to the furthest served consumer */
    int  at_capacity;
    int  network;
} UtilSrcInfo;
int  civic_util_source(int ut, int lot, UtilSrcInfo *out);  /* 0 if lot is no active source of that kind */
int  civic_lot_util_state(int ut, int lot);  /* NLS_OK served / NLS_SHORT connected, over capacity / NLS_NONE */
int  civic_lot_util_src(int ut, int lot, int *dist);
s32  civic_lot_util_use(int ut, int lot);    /* heat units / residents this lot puts on the network now */
s32  civic_heat_spare_for(int lot);          /* spare heat left in the lot's network (upgrade check) */
typedef struct {
    int sources, networks, lots_served, lots_short, lots_unconnected;
    s32 supply, demand, used;
    s32 untreated;                           /* sewage: residents whose sewage reaches a lake untreated */
    s32 fuel_month;                          /* heat: last month's fuel bill */
} UtilSummary;
void civic_util_summary(int ut, UtilSummary *out);
void civic_set_fuel_pct(int pct);            /* M7 hook: fuel price in % of coal (100); not saved by civic */

/* ---- lakes */
int  civic_lake_pollution(int x, int y);     /* water tile: 0..255 (0 on land) */
int  civic_shore_pollution(int x, int y);    /* land tile: worst lake value within 2 cells */
int  civic_lv_lake_term(int cx, int cy);     /* fields.cpp: land value term (<= 0) by CELL */
int  civic_happy_lake_term(int cx, int cy);  /* fields.cpp: happiness term (<= 0) by CELL */
void civic_lake_emit(int x, int y, s32 amount);   /* M6 hook: add pollution at the nearest water to (x, y),
                                                     amount in "residents' worth" per sewage update */
int  civic_lake_reason(void);                /* 0..100: share of homes near polluted water (politics) */
int  civic_lake_avg(void);                   /* mean pollution of the polluted water cells (stats) */

/* ---- fires (fire.cpp) */
typedef struct { u16 lot; u8 x, y; u8 age, stage; } CivicFire;   /* stage 0..3 (art fire0-3 intensity) */
int  civic_fires(const CivicFire **out);     /* burning lots now */
int  civic_fire_ignite(int lot);             /* scripts/tests: start a fire in this lot (wooden or not) */
typedef struct {
    s32 started, spread, put_out, lots_lost, displaced;   /* totals since the new city */
    s32 outbreaks_year;                      /* outbreaks started this calendar year */
    int burning, rubble_tiles;
} FireStats;
const FireStats *civic_fire_stats(void);
void fire_reset(void);
void fire_tick(void);                        /* every 4 steps */
void fire_cycle(void);                       /* ignition check, once per cycle */
void fire_month(void);                       /* rubble ageing + clearing */
u32  fire_hash(void);
struct SaveFile;
void fire_save(struct SaveFile *sf);         /* inside the CIVC chunk */
void fire_load(struct SaveFile *sf, int ver);

/* ---- overlays, queries, scripts, debug */
u8   civic_overlay_value(int ovl, int param, int x, int y);   /* OVL_CRIME, OVL_HEAT, OVL_SEWAGE */
int  civic_lot_line(int lot, int k, char *buf, int size);     /* query lines k = 0.. (0 = no more) */
int  civic_tile_line(int x, int y, int k, char *buf, int size);
int  civic_req_why(int req, int lot, s32 *have, s32 *need);   /* checklist details for REQ_HEAT/ELEMENTS */
int  civic_script_line(int n, char **t, int ox, int oy, FILE *out);   /* -1 = not ours, else SCR_* */
s32  civic_stat_value(const char *name, int *ok);              /* script "expect": heated, fires, ... */
void civic_dump(FILE *f);
u32  civic_hash(void);

int  civic_save(struct SaveFile *sf);        /* chunk "CIVC" */
int  civic_load(struct SaveFile *sf);        /* 0 = no chunk (old save: fresh state, solved on schedule) */

#endif
