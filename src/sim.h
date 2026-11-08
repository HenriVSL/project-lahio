/* sim.h - simulation scheduler, growth/decline, demand, migration, city stats, queries, events, stat
   logging. PORTABLE, deterministic, integer-only. Owner: T04. DESIGN 5.

   Determinism (DESIGN 5.1): the same save/new-city settings + the same sequence of player actions at the
   same steps gives the same city. The wall clock only decides HOW MANY steps run in a frame
   (sim_frame), never what a step does. The sim RNG lives in the sim state and in the save.

   Scheduler: 1 step = 1 phase (DESIGN 5.3 table); 16 steps = 1 cycle; 4 cycles = 1 month. Phases:
     0 date/ticker/cosmetic events     1-8 growth scan (1/8 of the rows each)
     9 net_step (power/water if dirty) 10 fields_step_pollution    11 fields_step_land_value(cycle)
    12 fields_step_social(cycle)       13 net_coverage_step(service round robin)
    14 traffic_step                    15 demand/migration; at month end: budget_month_end,
                                          politics_month, status/unlocks, history, stat log row
   sim.cpp calls into net, fields, budget, traffic and politics; those never call back into sim except
   for read-only getters. The sim never calls render/ui/snd: it emits SimEvents and world notifications.

   Debug from text (user rule): sim_statlog_open() writes one CSV row per month; sim_dump_report() writes
   a readable report; world_dump() (world.h) shows the map. Use them in tests and -simtest runs. */
#ifndef SIM_H
#define SIM_H
#include "common.h"
#include "defs.h"
#include "util.h"

/* ------------------------------------------------------------------ new city settings (DESIGN 2) */
enum { FUNDS_GENEROUS, FUNDS_NORMAL, FUNDS_AUSTERE, FUNDS_COUNT };
typedef struct {
    char president[NAME_LEN];  /* default "Kekkola" */
    char town[NAME_LEN];
    int  start_year;           /* 1955, 1960, 1965, 1970 */
    int  funds;                /* FUNDS_* */
    int  politics;             /* 1 = politics on (Milestone 2) */
    int  disasters;
    int  all_unlocked;         /* creative mode: everything unlocked + unlimited money */
    u32  seed;                 /* sim RNG seed (usually = the map seed) */
    int  difficulty;           /* 1.1 D1: DIFF_* (defs.h); 0 = Easy = 1.0 rules (tests, -seed starts); New Game: Normal */
} SimSettings;
void sim_default_settings(SimSettings *s);

/* ------------------------------------------------------------------ lifecycle and scheduling */
void sim_init(void);                          /* once: registers its world listener (net dirtiness) */
void sim_new_city(const SimSettings *s);      /* after mapgen/world setup: resets date, demand, stats,
                                                 budget (budget_new), net, fields, traffic, politics */
const SimSettings *sim_settings(void);
void sim_set_speed(int speed);                /* SPEED_* */
int  sim_speed(void);
/* Main-loop entry (DESIGN 11.5): adds dt * steps_per_sec(speed) to the step backlog (capped at 1 s worth:
   if the machine can't keep up the game runs slower, it never skips work), then runs whole steps while
   the backlog >= 1 and time_ms() - start < budget_ms. Always runs at least one due step even if
   budget_ms <= 0, so the game advances on slow frames. Returns the number of steps run. */
int  sim_frame(double dt_seconds, double budget_ms);
void sim_step(void);                          /* exactly one step (tests, headless) */
void sim_run_months(int months);              /* headless: steps until `months` month ends passed */

typedef struct {
    int year;                  /* e.g. 1955 */
    int month;                 /* 0..11 */
    int cycle;                 /* 0..3 */
    int step;                  /* 0..15 = phase of the NEXT step */
    u32 total_steps;           /* since sim_new_city */
    u32 total_months;
} SimDate;
const SimDate *sim_date(void);
Rng  *sim_rng(void);                          /* the simulation stream (sim modules only) */

/* ------------------------------------------------------------------ demand (DESIGN 5.5) */
enum { DT_OPEN_JOBS, DT_MIGRATION, DT_ATTRACTIVENESS, DT_VACANT_HOMES, DT_EMIGRATION,
       DT_SPENDING, DT_COM_CAPACITY, DT_CLEARING, DT_WESTERN, DT_LOCAL_NEED, DT_IND_CAPACITY,
       DT_TAX,
       DT_POLICY,            /* M1: presidential decrees and event effects (0 with politics off) */
       DT_REGIONAL,          /* M2: regional migrants by train (railway station, transit.h) */
       DT_FREIGHT,           /* M2: rail freight orders (railway station) */
       DT_SWEDEN,            /* 1.1 D1: "Sweden is hiring" 1969-70 (Normal/Hard; hard.cpp), people per month */
       DT_COUNT };
typedef struct {
    s32  value;                /* bar -100..+100 = scaled `smoothed` (100*s/(|s|+K), K grows with the city) */
    s32  target;               /* unsmoothed target in people (R) / jobs (C, I), recomputed every cycle */
    int  nterms;
    Term terms[MAX_TERMS];     /* itemised: ids DT_*; amounts in the same units as target (sum = target) */
    s32  smoothed;             /* T04: people/jobs; moves 1/4 of the way to target at each month end */
    s32  allowance;            /* T04: people/jobs that may still be built this cycle = max(0, min(target,
                                  smoothed)); growth consumes it (the brake against over-building) */
} Demand;
const Demand *sim_demand(int dem);            /* DEM_R, DEM_C, DEM_I */

/* ------------------------------------------------------------------ city statistics (updated monthly
   and at growth-scan granularity; cheap to read every frame) */
typedef struct {
    s32 population;            /* residents */
    s32 jobs, workers, unemployed;
    s32 homeless;              /* families without housing (emigration valve input) */
    s32 emigrated_month, emigrated_total, immigrated_month;
    s32 residents_zone[ZONE_COUNT], jobs_zone[ZONE_COUNT];
    s32 lots_tier[ZONE_COUNT][TIER_MAX + 1];  /* [zone][tier] grown lots */
    s32 lots_building, lots_abandoned;
    s32 power_supply, power_demand, power_short_lots;
    s32 water_supply, water_demand, water_short_lots;
    s32 svc_load[SVC_COUNT], svc_capacity[SVC_COUNT];
    s32 avg_land_value, avg_pollution, avg_happiness, avg_fire_risk;  /* 0..255 field averages */
    s32 avg_commute;           /* road tiles */
    int town_status;           /* TS_* (max reached) */
    int era;                   /* ERA_* */
    /* T04 additions */
    s32 capacity_res;          /* homes incl. under construction */
    s32 vacant;                /* capacity_res - population */
    s32 jobs_fill_pct;         /* employed / jobs (labour availability for industry) */
    s32 lots_zone;             /* grown lots (all zones) */
    s32 migration_pull;        /* this cycle's rural migration pull (people/month) */
} CityStats;
const CityStats *sim_stats(void);

/* ------------------------------------------------------------------ queries (Query tool, tooltips)
   Requirement checklist for the next tier (DESIGN 4.6): "OK Road  OK Power  X Water (no main within
   reach)  OK School 312/400  X Land value 31 (needs 40)". have/need are in the requirement's units
   (land value 0..255, school load/capacity, ...); text_req_line() in text.h formats an item. */
typedef struct {
    u8  req;                   /* REQ_* */
    u8  met;                   /* 1 = satisfied */
    u8  why;                   /* REQ-specific reason code for the sentence (0 = generic) */
    s32 have, need;            /* e.g. 31 / 40; for services: load / capacity of the serving building */
    s32 lot;                   /* related lot (serving school, nearest shop...) or 0 */
} ReqItem;
typedef struct { int tier; int n; ReqItem item[REQ_COUNT]; } Checklist;   /* tier = tier being checked */

enum { ST_DEMAND, ST_LAND_VALUE, ST_SERVICES, ST_POLLUTION, ST_COMMUTE, ST_NOISE, ST_RANDOM,
       ST_POLICY,            /* M1: decrees (housing programme) */
       ST_COUNT };
typedef struct {
    int  lot, def, status, tier, rot, variant;
    s32  occupants, capacity;
    int  age_months;
    int  land_value, pollution, happy;  /* 0..255 / 0..100 */
    s32  score;                         /* growth score (DESIGN 5.4) */
    int  nterms;
    Term terms[MAX_TERMS];              /* score breakdown, ids ST_* */
    int  growth, growth_threshold;      /* hysteresis counter and the value that triggers a change */
    Checklist next;                     /* requirements for tier + 1 (n = 0 at max tier/non-zone) */
    s32  svc_load, svc_capacity;        /* service buildings: "School: 412/500 pupils" */
    s32  catchment_tiles;
    s32  power_out, water_out;          /* plants, pumps */
    Money upkeep;                       /* per month at current funding */
} LotInfo;
int  sim_query_lot(int lot, LotInfo *out);    /* 0 if no such lot */

typedef struct {
    int  x, y, terrain, zone, road, lot;
    int  powered, watered, land_value, pollution, traffic;
    u8   coverage[SVC_COUNT];               /* 0..100 quality per service */
    s32  score;                             /* growth score for an empty zoned tile */
    int  nterms;
    Term terms[MAX_TERMS];
    Checklist build;                        /* requirements to build tier 1 here (empty zoned tile) */
} TileInfo;
int  sim_query_tile(int x, int y, TileInfo *out);

/* Top problems (population/budget panels, DESIGN 8.3): "340 homes without water (near 41,17)". */
typedef struct { u8 req; s32 count; s32 residents; s16 x, y; } SimProblem;
int  sim_top_problems(SimProblem *out, int max);   /* sorted by residents affected; returns count */

/* Overlay value for a tile (DESIGN 11.4): OVV(ramp, level) or OVV_NONE. param = lot for OVL_CATCHMENT.
   Called by render.cpp for every tile of a chunk being re-rendered: must be cheap (table lookups). */
u8   sim_overlay_value(int ovl, int param, int x, int y);

/* ------------------------------------------------------------------ events (ticker, sound, ui)
   The sim queues events; game.cpp drains them every frame and routes them (ui ticker via text_event,
   snd, tools_month_changed on SE_MONTH, autosave on SE_YEAR). Ring of 64; when full the oldest is dropped. */
enum { SE_MONTH, SE_YEAR, SE_LOT_BUILT, SE_LOT_UPGRADED, SE_LOT_DOWNGRADED, SE_LOT_ABANDONED,
       SE_LOT_RESTORED, SE_STATUS_UP, SE_UNLOCK, SE_EMIGRATION, SE_POWER_SHORTAGE, SE_WATER_SHORTAGE,
       SE_SERVICE_FULL, SE_NO_MONEY, SE_BANKRUPT_WARN, SE_FIRE, SE_MILESTONE, SE_COUNT };
enum { SEV_INFO, SEV_WARN, SEV_URGENT, SEV_POLITICAL };
typedef struct {
    u8  type;                  /* SE_* */
    u8  severity;              /* SEV_* */
    s16 x, y;                  /* location (tile) or -1 */
    s32 a, b;                  /* type-specific: lot id, count, status, def ... */
} SimEvent;
int  sim_poll_event(SimEvent *e);             /* 1 = got one */
void sim_queue_event(int type, int sev, int x, int y, s32 a, s32 b);   /* M5: other sim modules (fire.cpp) */

/* ------------------------------------------------------------------ stat logging (debug from text) */
int  sim_statlog_open(const char *path);      /* CSV; header row now, then one row per month end: date,
                                                 CityStats, demand values + every term, budget lines
                                                 (last month), phase timings (avg/max ms) */
void sim_statlog_close(void);
void sim_dump_report(FILE *f);                /* multi-line readable report of everything above +
                                                 top problems */
typedef struct { double avg_ms, max_ms; u32 count; } PhaseTiming;
const PhaseTiming *sim_phase_timing(int phase); /* 0..15, since the last month end (reset after the
                                                   stat row is written) */
/* T13 (bench): the same, accumulated since sim_phase_timing_total_reset() (never reset by the sim). */
const PhaseTiming *sim_phase_timing_total(int phase);
void sim_phase_timing_total_reset(void);
/* Monthly history for graphs and the newspaper (last SIM_HISTORY months). */
enum { HIST_POPULATION, HIST_MONEY, HIST_DEMAND_R, HIST_DEMAND_C, HIST_DEMAND_I, HIST_JOBS,
       HIST_EMIGRATED, HIST_COUNT };
#define SIM_HISTORY 240
int  sim_history(int series, s32 *out, int max);   /* oldest first; returns count */

u32  sim_hash(void);

/* ---- T04 tuning constants (shared with tests/ui) */
#define SIM_LABOUR_PCT   50    /* workers per 100 residents */
#define SIM_T_GROW       8     /* growth score threshold to build/upgrade */
#define SIM_T_DECLINE    (-15) /* score below this counts toward decline */
#define SIM_G_UP         2     /* consecutive good checks (4 per month) before an upgrade */
#define SIM_G_DOWN       4     /* consecutive bad checks before a decline step */                          /* world + sim + budget state (determinism tests) */

struct SaveFile;
int  sim_save(struct SaveFile *sf);           /* chunk "SIM " incl. RNG, date, demand, history */
int  sim_load(struct SaveFile *sf);
int  sim_save_ext(struct SaveFile *sf);       /* T10: save: chunk "SIMX" (call after sim_save) */
int  sim_load_ext(struct SaveFile *sf);       /* T10: save: 0 = no SIMX chunk */
/* M7: raise the town status to ts now (rewards, SE_STATUS_UP, ceremony; era.cpp's town-rights decree) */
void sim_grant_status(int ts);

#endif
