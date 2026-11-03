/* industry.h - M3: industry kinds, wood and milk supply, forestry zone (rotation cutting + regrowth), farm
   zone (farmsteads claim fields), export volumes by kind. PORTABLE, deterministic, integer-only.
   Owner: M3 (Milestone 2). Spec: docs/research/INDUSTRY.md 5-6, DESIGN 14.3.

   Model (numbers below and in industry.cpp):
   - KINDS. Every grown industrial def has a kind (industry_def_kind: a table built from the def keys, so
     BuildingDef needs no new field): sawmill (IK_WOOD), engineering (IK_METAL, the fallback), textile,
     dairy (IK_FOOD), cable & electronics (IK_ELEC, 1965+, Town). A new industrial lot picks its kind by
     a deterministic weighted draw (sim RNG): weight = max(0, kind demand) x (100 + 2 x site bonus) / 100 for
     the kinds whose site rule passes (wood / milk supply >= 75% incl. this lot; electronics: school and
     clean air); all weights 0 -> engineering, so the zone never stalls for lack of wood or milk.
   - KIND DEMAND (every cycle, after sim's compute_demand): the I target split by export category,
       kind_k = (sC_k * clearing + sW_k * western + sL_k * localNeed) / 100 - capacity_k + other terms
     (tax, policy, ...) in proportion; the sum is exactly the I target. A kind whose input is short gets
     its demand cut to the input headroom and the rest moves to engineering ("wood short -N").
   - WOOD (wood units, wu / month): need = jobs / 10 per sawmill lot (+ external users: the M6 paper
     mill). Supply = regional wood (10, +15 railway station, +15 log floating before 1970 when a sawmill
     touches water or log trucks from 1965; max 40) + forestry capacity (each logging camp covers the
     forestry tiles within 4: a quarter wu per tile and month = 12 wu per tile every 48 months). At month
     end the forest part of the need is cut from mature tiles around the camps, nearest first: a cut tile
     gives 12 wu and regrows cut -> seedlings -> young -> mature in 48 months (Tile.aux high nibble on
     TER_FOREST tiles: 0 mature, 1..12 regrowing; the low nibble keeps the mapgen density).
   - MILK (mu / month): dairy need = jobs / 2; supply = regional 20 + claimed fields x tier (1/2/3).
     Farm demand = food need (20 + pop / 50) + dairies - supply: new farmsteads grow while it is > 0.
   - FARMS: a farmstead (1x1, ZONE_FARM, residents, self-employed, Rural bloc) grows on a farm tile within
     3 of a road with >= 6 unclaimed farm tiles within 3; it claims up to 8 (grass becomes field). Tiers
     T1 small farm 5, T2 dairy farm 6 (power + school), T3 mechanised 4 (power + main road, 1962): the
     upgrade to T3 sends 2 residents to the homeless pool (structural change).
   - EXPORTS: export volume per kind = working jobs x rate (sawmill 133, engineering 100, textile 67,
     dairy 100, electronics 200 per 100 jobs; engineering = the M1 export rate, so the budget's export line
     keeps its scale), split West/East (sawmill 60/40, engineering 25/75, textile 20/80 (10/90 from 1975),
     dairy 40/60, electronics 50/50). M7 reads industry_kinds() for clearing/Western deliveries.
   Debug from text: industry_dump(). */
#ifndef INDUSTRY_H
#define INDUSTRY_H
#include "common.h"
#include "defs.h"
#include "sim.h"

enum { IK_NONE, IK_WOOD, IK_METAL, IK_TEXTILE, IK_FOOD, IK_ELEC, IK_COUNT };

/* ---- tunables (tests and ui read them) */
#define IND_WOOD_JOBS_PER_WU   10    /* a sawmill lot needs capacity / 10 wu per month */
#define IND_MILK_JOBS_PER_MU   2     /* a dairy needs capacity / 2 mu per month */
#define IND_INPUT_GROW_PCT     75    /* new lot / upgrade: supply >= 75% of the need incl. this lot */
#define IND_INPUT_KEEP_PCT     60    /* below: REQ_WOOD / REQ_MILK unmet for the lots already there */
#define IND_WOOD_REGIONAL      10
#define IND_WOOD_STATION       15
#define IND_WOOD_TRANSPORT     15    /* floating (1955-69, sawmill by water) or log trucks (1965+) */
#define IND_WOOD_REGIONAL_MAX  40
#define IND_TILE_WU            12    /* wood from one cut forest tile */
#define IND_REGROW_STEPS       12    /* x 4 months = 48 months from stumps to mature */
#define IND_CAMP_RADIUS        4     /* a logging camp harvests forestry tiles within 4 */
#define IND_CAMP_SPACING       6     /* no new camp within 6 of another */
#define IND_CAMP_MIN_TILES     16    /* uncovered forestry tiles near a site before a camp grows */
#define IND_MILK_REGIONAL      20
#define IND_FARM_RADIUS        3
#define IND_FARM_FIELDS        8     /* fields claimed per farmstead */
#define IND_FARM_MIN_FREE      6     /* unclaimed farm tiles needed for a new farmstead */

/* forest stage of a TER_FOREST tile (render, query) */
enum { FST_MATURE, FST_CUT, FST_SEEDLINGS, FST_YOUNG };
#define FOREST_REGROW(t) ((t)->aux >> 4)            /* 0 mature, 1..IND_REGROW_STEPS regrowing */
int  industry_forest_stage(int regrow);              /* FST_* from FOREST_REGROW */

typedef struct {
    s32 need;               /* wu / month: all wood users (sawmills + external) */
    s32 need_ext;           /* of it external users (M6 paper mill) */
    s32 regional;           /* available off-map wood */
    s32 forestry_cap;       /* sustainable harvest capacity of the logging camps */
    s32 supply;             /* regional + forestry_cap (growth compares with this) */
    s32 delivered;          /* last month: wood the users got */
    s32 cut_wu;             /* last month: wood cut on the map */
    s32 stock;              /* cut wood carried to next month */
    s32 pct;                /* last month delivered / need 0..100 (100 when no need) */
    s32 camps, forestry_tiles, harvest_tiles, forest_tiles, regrowing, clearcut;
    s32 cut_tiles_month, cut_tiles_year, cut_tiles_total;
} IndWood;
typedef struct {
    s32 need;               /* mu / month: dairies */
    s32 regional;
    s32 farms;              /* milk from claimed fields */
    s32 supply;             /* regional + farms */
    s32 food_need;          /* the town's own: 20 + pop / 50 */
    s32 farm_demand;        /* food_need + need - supply: new farmsteads while > 0 */
    s32 pct;                /* supply / need 0..100 */
    s32 farm_lots, fields, farm_residents;
    s32 farm_tier[TIER_MAX + 1];
} IndMilk;
typedef struct {
    s32 demand[IK_COUNT];      /* jobs: this cycle's kind demand (sum = the I target) */
    s32 gross[IK_COUNT];       /* share of clearing + western + local need */
    s32 capacity[IK_COUNT];    /* jobs of the kind's lots (incl. under construction) */
    s32 short_jobs[IK_COUNT];  /* moved to engineering: input short (wood, milk) or not unlocked (electronics) */
    s32 jobs[IK_COUNT];        /* working jobs (active lots) */
    s32 lots[IK_COUNT];        /* lots[IK_NONE] = yards (F3 ind_yard; their jobs count with engineering) */
    s32 lots_tier[IK_COUNT][TIER_MAX + 1];
    s32 export_vol[IK_COUNT];  /* jobs x rate / 100 (engineering = 1 per job) */
    s32 export_west[IK_COUNT], export_east[IK_COUNT];
    s32 west_total, east_total; /* M7: Western orders delivered / clearing deliveries (volume units) */
    s32 picks[IK_COUNT];       /* new lots by kind since the start (tests, balance tables) */
} IndKinds;

/* ------------------------------------------------------------------ lifecycle (sim.cpp calls these) */
void industry_init(void);
void industry_reset(void);                 /* sim_new_city */
void industry_cycle(void);                 /* phase 15, after compute_demand: needs, supplies, kind demand */
void industry_month(void);                 /* month end: regrowth, harvest, farm claims, exports */

/* ------------------------------------------------------------------ kinds */
int  industry_def_kind(int def);           /* IK_* (IK_NONE: not an industrial def) */
int  industry_def_input(int def, int *milk); /* wood (or milk with *milk = 1) units per month at full jobs */
const char *industry_kind_name(int kind);  /* "Sawmill" */
const char *industry_kind_plural(int kind);/* "Sawmills" */
int  industry_export_weight(int def);      /* export rate per 100 jobs relative to engineering (100) */
int  industry_west_pct(int kind, int year);/* West share of the kind's exports */

/* ------------------------------------------------------------------ growth hooks (sim.cpp)
   industry_candidates: the empty industrial tile (x, y) (north tile): if a 2x2 is free there, picks the
   kind (sim RNG) and writes the kind's candidate defs, largest first (all square: parity 0); returns the
   count (0 = nothing fits). Higher-tier candidates (textile mill, large works, electronics) are placed
   only if sim's check of that tier's requirements passes. */
int  industry_candidates(int x, int y, u8 *defs, int max);
int  industry_last_pick(void);             /* IK_* of the last industry_candidates call (tests) */
void industry_grow_rural(int x, int y, int zone);   /* empty forestry / farm tile (growth band) */
s32  industry_rural_score(int zone, int x, int y);  /* demand term of the growth score for those zones */
u32  industry_lot_unmet(int lot, int next_tier);    /* REQ_WOOD / REQ_MILK bits of a kind lot */
int  industry_lot_reqs(int lot, int next_tier, ReqItem *out, int max);  /* checklist items (query) */
int  industry_def_rank(int def);           /* tests: candidate defs are ordered by this */

/* ------------------------------------------------------------------ numbers */
const IndWood  *industry_wood(void);
const IndMilk  *industry_milk(void);
const IndKinds *industry_kinds(void);
void industry_set_ext_wood(int slot, s32 wu);   /* M6: wood need of a complex (slot 0..3), 0 = none */
s32  industry_rural_forest_reason(void);   /* politics: Rural approval points for forestry (jobs +, clear-cuts -) */
int  industry_clearcut_pct(void);          /* clear-cut + seedling tiles per 100 forest tiles */
int  industry_tile_claimed(int x, int y);  /* farm field claimed by a farmstead */

/* ------------------------------------------------------------------ text (query panel, tooltips)
   Line k (0..) of the explanation; returns 0 when there is no line k. */
int  industry_lot_line(int lot, int k, char *buf, int size);      /* "Sawmill: lakeside log pond, ..." */
int  industry_tile_line(int x, int y, int k, char *buf, int size); /* forestry / farm tiles */
int  industry_demand_line(int kind, char *buf, int size, s32 *amount);  /* tooltip "Sawmills" + amount */

/* ------------------------------------------------------------------ debug, save */
void industry_dump(FILE *f);
u32  industry_hash(void);
void industry_after_load(int had_chunk);   /* old saves: generic industry lots -> kind defs; recompute */
struct SaveFile;
int  industry_save(struct SaveFile *sf);   /* chunk "INDU" v1 */
int  industry_load(struct SaveFile *sf);   /* 0 = no chunk (old save) */

#endif
