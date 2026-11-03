/* defs.h - static data tables: terrain, zones, road classes, services, buildings, eras, town status,
   tier requirements, overlays. PORTABLE. Owner: T04 (values tuned later by T12). DESIGN 3, 4, 5.4, 7, 11.7.

   Ids are enums. They are stored in save files, so within a milestone ids are APPEND-ONLY (add new ones
   just before the *_COUNT entry; never reorder or delete). The tables live in defs.cpp as static const
   arrays indexed by id. Display names are plain English strings in the tables (single language; text.cpp
   holds sentences, formats and name generators). Art is referenced by NAME, never by sheet index: a
   building's `key` is also its sprite stem (art.h "Naming"), so art can be regenerated freely. */
#ifndef DEFS_H
#define DEFS_H
#include "common.h"

/* ------------------------------------------------------------------ terrain (Tile.terrain low 4 bits)
   The high 4 bits are the variant: grass 0..2 (2 = meadow), forest species FOREST_*, field FIELD_*,
   river: bits 0-1 flow direction DIR_*, bit 2 rapids. Shore is NOT a terrain type: the renderer
   autotiles land/water corners. Forest is drawn as a 1x1 lot-like sprite per tile; clearing it (bulldoze)
   turns it into grass. */
enum { TER_GRASS, TER_FOREST, TER_ROCK, TER_WATER, TER_RIVER, TER_FIELD, TER_MARSH, TER_COUNT };
enum { FOREST_MIXED, FOREST_SPRUCE, FOREST_BIRCH, FOREST_PINE, FOREST_COUNT };
enum { FIELD_CROP, FIELD_PLOUGH, FIELD_KIND_COUNT };
#define TER_IS_WATER(t)   ((t) == TER_WATER || (t) == TER_RIVER)
#define RIVER_RAPIDS_BIT  4

/* ------------------------------------------------------------------ zones and demand */
enum { ZONE_NONE, ZONE_HOUSES, ZONE_APARTMENTS, ZONE_COMMERCIAL, ZONE_INDUSTRY,
       ZONE_FORESTRY,        /* M3: painted on forest; logging camps, rotation cutting (industry.cpp) */
       ZONE_FARM,            /* M3: painted on grass/fields; farmsteads claim fields (industry.cpp) */
       ZONE_COUNT };
#define ZONE_CONDUCTS(z) ((z) != ZONE_NONE && (z) < ZONE_FORESTRY)   /* M3: empty zoned land conducts power and
                                                                      water, except forestry and farm zones */
enum { DEM_R, DEM_C, DEM_I, DEM_COUNT };      /* houses + apartments share R demand */
#define ZONE_DEMAND(z) ((z) == ZONE_COMMERCIAL ? DEM_C : (z) == ZONE_INDUSTRY ? DEM_I : DEM_R)
#define TIER_MAX 3
#define ZONE_ROAD_REACH 3                     /* zone tiles must be within 3 tiles of a road (DESIGN 4.2) */

/* ------------------------------------------------------------------ road classes (Tile.road bits 0-2) */
enum { ROAD_NONE, ROAD_GRAVEL, ROAD_STREET, ROAD_MAIN, ROAD_MOTORWAY, ROAD_RAIL, ROAD_CLASS_COUNT };
enum { RDF_ZONE_ACCESS = 1,   /* lots/zones can use it as access (not motorway, not rail) */
       RDF_CARS        = 2,   /* cosmetic cars and traffic use it (not rail) */
       RDF_BRIDGE_OK   = 4,   /* may cross water as a straight bridge (max BRIDGE_MAX_LEN) */
       RDF_CARRIES_WATER = 8  /* F1: no longer carries water (pipes do, WATER.md). Kept for: v1 saves get a
                                 pipe under these classes on load; "lay pipes with new roads" lays them */ };
#define BRIDGE_MAX_LEN 8

typedef struct {
    const char *key;          /* sprite prefix: "grav", "road", "main", "mway", "rail" */
    const char *name;         /* "Gravel road" */
    s16   era_year;           /* first year available */
    u8    status_req;         /* TS_* needed */
    u8    flags;              /* RDF_* */
    s16   capacity;           /* commuters/day before congestion (DESIGN 4.1) */
    u8    speed;              /* relative 1 (slow) .. 4 (fast) */
    u8    pollution;          /* emitted per tile per month at full flow (dust, exhaust) */
    u8    noise;
    Money cost;               /* per tile */
    Money bridge_cost;        /* per water tile */
    Money upkeep10;           /* per 10 tiles per month (whole mk can't express per-tile cents) */
} RoadDef;

/* ------------------------------------------------------------------ services (road-distance coverage) */
enum { SVC_FIRE, SVC_SCHOOL, SVC_HEALTH, SVC_SAUNA, SVC_POLICE, SVC_LIBRARY, SVC_POST, SVC_COUNT };
#define SVC_NONE 255

typedef struct {
    const char *key;          /* "fire", "school", ... */
    const char *name;         /* "Fire protection", "Education" */
    u8    max_dist;           /* max road distance in tiles from the building (BFS cut-off) */
    u8    demand_pct;         /* load per resident in % (school: 20 = pupils are 20% of residents) */
    u8    overlay;            /* OVL_SVC_* */
} ServiceDef;

/* ------------------------------------------------------------------ eras and town status (DESIGN 7) */
enum { ERA_RECONSTRUCTION, ERA_STRUCTURAL, ERA_WELFARE, ERA_LATE_COLD_WAR, ERA_RENEWAL, ERA_COUNT };
enum { TS_VILLAGE, TS_CHURCH_VILLAGE, TS_MARKET_TOWN, TS_TOWN, TS_REGIONAL_CENTRE, TS_CITY, TS_COUNT };
typedef struct { const char *name; s16 year_from; } EraDef;
typedef struct { const char *name; s32 population; Money reward; } StatusDef;

/* ------------------------------------------------------------------ requirements (tier checklist)
   Bit (1 << REQ_*) is used in Lot.problems. Labels/sentences are in text.cpp (text_req_label). */
enum { REQ_ROAD,          /* road access (lot touches a usable road) */
       REQ_POWER,         /* powered (not brownout) */
       REQ_WATER,         /* watered */
       REQ_WATER_TOWER,   /* within reach of a water tower ("water pressure") */
       REQ_ELEMENTS,      /* element factory supply (apartments T2+) */
       REQ_SCHOOL,
       REQ_SAUNA,
       REQ_HEALTH,
       REQ_SHOP,          /* commercial within reach */
       REQ_PHONE,         /* post & telephone */
       REQ_LAND_VALUE,    /* need = minimum land value */
       REQ_LOW_POLLUTION, /* need = maximum pollution */
       REQ_CUSTOMERS,     /* commercial: residents within reach; need = count */
       REQ_WORKERS,       /* industry: workers within commute reach */
       REQ_MAIN_ROAD,     /* main road (or rail) access */
       REQ_HEAT,          /* district heating (v1.0) */
       REQ_WOOD,          /* M3: sawmills: wood supply (industry.cpp; have/need = supply % / 75) */
       REQ_MILK,          /* M3: dairies: milk supply */
       REQ_ROUTE,         /* M2: homes need a road route to the town's jobs (traffic.cpp traffic_route_ok) */
       REQ_COUNT };
typedef struct { u8 req; s16 need; } TierReq; /* need: threshold where the requirement has one */

/* ------------------------------------------------------------------ buildings */
enum { BCAT_NONE, BCAT_ZONE /* grown by the sim */, BCAT_POWER, BCAT_WATER, BCAT_SERVICE, BCAT_PARK,
       BCAT_SPECIAL /* town hall, element factory, station, landmarks */, BCAT_COUNT };
enum { BDF_WOODEN      = 0x01,  /* burns easily (fire risk) */
       BDF_NEEDS_SHORE = 0x02,  /* must touch water (pumping station) */
       BDF_NEEDS_RAPIDS= 0x04,  /* must sit on river rapids (hydro) */
       BDF_UNIQUE      = 0x08,  /* at most one (town hall, presidential sauna) */
       BDF_NO_CONDUCT  = 0x10,  /* does NOT conduct power to neighbours (parks) */
       BDF_NO_ROTATE   = 0x20,  /* art has only r0 (symmetric: water tower, park) */
       BDF_SMOKE       = 0x40   /* has chimney smoke (life.cpp uses the sprite hotspot) */ };

enum { B_NONE,
       /* grown zone buildings: key = sprite stem, e.g. "house_t2", "apt_t1_3x1" */
       B_HOUSE_T1, B_HOUSE_T2, B_HOUSE_T3,
       B_APT_T1_3X1, B_APT_T1_2X2, B_APT_T2_3X1, B_APT_T2_2X2, B_APT_T3_3X1, B_APT_T3_2X2,
       B_COM_T1_1X1, B_COM_T1_2X1, B_COM_T1_2X2, B_COM_T2_1X1, B_COM_T2_2X1, B_COM_T2_2X2,
       B_COM_T3_1X1, B_COM_T3_2X1, B_COM_T3_2X2,
       B_IND_T1_2X2, B_IND_T1_3X3, B_IND_T2_2X2, B_IND_T2_3X3, B_IND_T3_2X2, B_IND_T3_3X3,
       /* placed by the player */
       B_TOWN_HALL,
       B_COAL_PLANT, B_HYDRO_PLANT, B_OIL_PLANT, B_PEAT_PLANT, B_NUCLEAR_PLANT,
       B_PUMP, B_WELL, B_WATER_TOWER,
       B_FIRE_STATION, B_SCHOOL, B_CLINIC, B_SAUNA, B_POLICE, B_LIBRARY, B_POST, B_HEALTH_CENTRE,
       B_PARK, B_SPORTS_FIELD, B_ICE_RINK, B_CHURCH,
       B_ELEMENT_FACTORY, B_STATION,
       /* M3 industry kinds (industry.cpp: industry_def_kind; ART.md 3.1 keys) */
       B_IND_WOOD_T1_3X3, B_IND_WOOD_T1_2X2, B_IND_WOOD_T2_3X3,
       B_IND_METAL_T1_2X2, B_IND_METAL_T2_2X2, B_IND_METAL_T3_3X3,
       B_IND_TEXTILE_T1_2X2, B_IND_TEXTILE_T2_3X3,
       B_IND_DAIRY_T1_2X2, B_IND_DAIRY_T2_2X2,
       B_IND_ELEC_T2_2X2,
       /* M3 farm zone tiers and the forestry zone's logging camp */
       B_FARM_T1, B_FARM_T2, B_FARM_T3,
       B_LOGGING_CAMP,
       /* F3: 1x1 industrial yard (timber stack, steel stock, workshop, coal heap...; F2/M4 art, 6 variants):
          grows only where no 2x2 industrial lot fits (DESIGN 14.2, sim.cpp grow_yard) */
       B_IND_YARD,
       /* M2 transit (transit.h): bus depot (lines run only with one), bus stop shelter (placed by the bus line tool) */
       B_BUS_DEPOT, B_BUS_STOP,
       B_COUNT };

typedef struct {
    const char *key;          /* stable ASCII id + art sprite stem ("house_t2", "coal_plant") */
    const char *name;         /* English display name ("Rintamamiestalo") */
    u8    cat;                /* BCAT_* */
    u8    zone;               /* ZONE_* for BCAT_ZONE, else ZONE_NONE */
    u8    tier;               /* 1..3 for BCAT_ZONE, 0 otherwise */
    u8    w, h;               /* footprint in tiles at rot 0 (X x Y); rot 1/3 swaps */
    u8    service;            /* SVC_* or SVC_NONE */
    u8    flags;              /* BDF_* */
    u8    status_req;         /* TS_* needed to unlock */
    s16   era_year;           /* first year available */
    Money cost;               /* placement cost (0 for grown buildings) */
    Money upkeep;             /* per month at 100% funding */
    s16   capacity;           /* residents (R) / jobs (C,I) per LOT, power units (plant), water units
                                 (pump/well), service capacity (residents served / pupils) */
    s16   jobs;               /* jobs provided by non-zone buildings (plants, services) */
    s16   power_use;          /* power units consumed */
    s16   water_use;          /* water units consumed */
    s16   pollution;          /* emitted per month into its field cell(s) */
    s16   land_value;         /* +/- effect on nearby land value */
    u8    lv_radius;          /* radius in tiles of the land value effect (parks: 4) */
    u8    build_months;       /* construction stage duration (0 = instant) */
    u8    variants;           /* art variants (0 = this def is never grown/placed in this build) */
    u8    upgrade_to;         /* B_* id of the next tier with the same footprint, or B_NONE */
} BuildingDef;

/* ------------------------------------------------------------------ misc constants (tunable) */
typedef struct {
    Money power_line;         /* per tile */
    Money power_line_upkeep10;/* per 10 tiles per month */
    Money bulldoze_tile;      /* demolition per tile */
    Money clear_forest;       /* per forest tile (gives timber_income once) */
    Money timber_income;
    Money blast_rock;         /* building on rock: extra per tile */
    Money zone_tile;          /* zoning cost per tile (may be 0) */
    u8    refund_pct;         /* bulldoze refund for buildings placed this month (25) */
    Money pipe;               /* F1: water pipe per tile (WATER.md 3.2) */
    Money pipe_water;         /* F1: per tile under water */
    Money pipe_upkeep10;      /* F1: per 10 pipe tiles per month */
} MiscDef;

/* F1 water constants (WATER.md 3.3-3.4) */
#define WATER_JUMP_ROADS      0     /* roads block water unless a pipe runs under/across them (net.cpp) */
#define WATER_SPREAD          1     /* empty tile is "wet" if a wet tile is within this Chebyshev distance */
#define TOWER_PRESSURE_REACH  16    /* water tower pressure zone, tiles along pipes and buildings */
#define TOWER_OUTPUT          200   /* = the water tower's capacity in g_bdefs */

/* ------------------------------------------------------------------ overlays (DESIGN 8.1, 11.4)
   sim_overlay_value() (sim.h) maps a tile to an OVV code that render.cpp bakes into chunks with the
   art's checker-dither diamonds ("ovl_<ramp>_<level>", art.h). */
enum { OVL_NONE, OVL_POWER, OVL_WATER, OVL_POLLUTION, OVL_LAND_VALUE, OVL_TRAFFIC, OVL_FIRE_RISK,
       OVL_SVC_FIRE, OVL_SVC_SCHOOL, OVL_SVC_HEALTH, OVL_SVC_SAUNA, OVL_SVC_POLICE, OVL_SVC_LIBRARY,
       OVL_SVC_POST,
       OVL_GROWTH,      /* "why isn't it growing": colour = main unmet requirement */
       OVL_ZONES,       /* zone tints, buildings flat */
       OVL_CATCHMENT,   /* one service building's catchment; parameter = lot id */
       OVL_COUNT };
#define OVL_SVC(svc) (OVL_SVC_FIRE + (svc))
/* Ramps: gradual ones use levels 0..7; categorical ones use the level as a category. */
enum { OVR_BAD,         /* 0 clean .. 7 heavy (pollution, fire risk, traffic) */
       OVR_GOOD,        /* 0 low .. 7 high (land value) */
       OVR_COVER,       /* 0 none .. 7 full (service coverage quality) */
       OVR_NET,         /* categories NETC_* */
       OVR_ZONE,        /* category = ZONE_* */
       OVR_PROBLEM,     /* category = GROWC_* below (growth overlay; legend in ui) */
       OVR_COUNT };
enum { NETC_OK, NETC_NOT_CONNECTED, NETC_SHORTAGE /* brownout / low pressure */, NETC_CARRIER /* line/main */,
       NETC_WET          /* F1: empty tile where a new building would get water (faint blue) */ };
/* OVL_GROWTH categories (T04): the main unmet requirement of a zoned tile / grown lot, grouped to 8. */
enum { GROWC_OK,          /* growing / nothing missing */
       GROWC_ROAD,        /* REQ_ROAD */
       GROWC_POWER,       /* REQ_POWER */
       GROWC_WATER,       /* REQ_WATER, REQ_WATER_TOWER */
       GROWC_SERVICES,    /* REQ_SCHOOL, SAUNA, HEALTH, SHOP, PHONE */
       GROWC_LAND_VALUE,  /* REQ_LAND_VALUE */
       GROWC_POLLUTION,   /* REQ_LOW_POLLUTION or a strongly negative pollution term */
       GROWC_OTHER,       /* REQ_CUSTOMERS, WORKERS, ELEMENTS, MAIN_ROAD, HEAT, or no demand */
       GROWC_COUNT };
int  def_req_growc(int req);                      /* REQ_* -> GROWC_* */
#define OVV(ramp, level)  ((u8)(((ramp) << 3) | ((level) & 7)))
#define OVV_RAMP(v)       ((v) >> 3)
#define OVV_LEVEL(v)      ((v) & 7)
#define OVV_NONE          0xFF        /* draw nothing on this tile */

/* ------------------------------------------------------------------ itemised numbers
   "Every number is visible and explained" (DESIGN 0.2): demand, growth score, land value and budget
   projections are returned as a list of Terms. The id's meaning depends on the list kind (DT_*, ST_*,
   LVT_*, BL_*); text_term_label(kind, id) in text.h names them. */
typedef struct { u8 id; s32 amount; } Term;
#define MAX_TERMS 12

/* ------------------------------------------------------------------ table access (defs.cpp) */
extern const BuildingDef g_bdefs[B_COUNT];
extern const RoadDef     g_roads[ROAD_CLASS_COUNT];
extern const ServiceDef  g_services[SVC_COUNT];
extern const EraDef      g_eras[ERA_COUNT];
extern const StatusDef   g_statuses[TS_COUNT];
extern const MiscDef     g_misc;

const BuildingDef *def_building(int id);          /* out of range -> g_bdefs[B_NONE] (never NULL) */
int  def_building_find(const char *key);          /* by key; B_NONE if unknown */
int  def_zone_building(int zone, int tier, int w, int h); /* grown def for that lot size, or B_NONE */
void def_footprint(int id, int rot, int *w, int *h);     /* tiles occupied at that rotation */
int  def_unlocked(int id, int year, int status, int all_unlocked); /* 1 = may be built now */
int  def_road_unlocked(int cls, int year, int status, int all_unlocked);
int  def_zone_unlocked(int zone, int year, int status, int all_unlocked); /* zone tool available */
int  def_tier_reqs(int zone, int tier, const TierReq **out); /* requirements to REACH `tier`; count */
int  def_era(int year);                           /* ERA_* */
int  def_status_for_population(s32 pop);          /* TS_* by thresholds (sim keeps the max reached) */
int  def_selfcheck(void);                         /* table consistency (ids, footprints, upgrade_to);
                                                     0 ok, else number of problems (logged) */

#endif
