/* defs.cpp - static data tables. PORTABLE. Owner: T04 (numbers tuned by T12).
   T00 skeleton: ids, KEYS (= art sprite stems, a contract with T02), names, categories, footprints,
   unlock year/status from DESIGN 4 and 7. T04: economic numbers from DESIGN 4/5; T12 tuned them (old -> new
   values and reasons in docs/BALANCE.md section 3).
   Keep the row order identical to the enums in defs.h. */
#include "common.h"
#include "defs.h"
#include "util.h"
#include "era.h"           /* M7: era_unlock_override */

/* Units (T04): capacity = residents (R) / jobs (C, I) per LOT, power units (plants; 1 unit ~ one tier-2
   house tile, DESIGN 4.3), water units (pump/well), residents served (services; school: pupils).
   pollution = field units emitted per MONTH by the lot (spread over its tiles; 0..255 field scale, see
   fields.cpp). land_value = +/- on nearby land value within lv_radius tiles. variants = art variants T02
   generates; 0 = not in this build (never grown, tools hide it). Money in whole (new) markka.

   key, name, cat, zone, tier, w, h, service, flags, status_req, era_year,
   cost, upkeep, capacity, jobs, power_use, water_use, pollution, land_value, lv_radius,
   build_months, variants, upgrade_to */
#define ZB(key, name, zone, tier, w, h, flags, cap, pw, wa, pol, bm, var, up) \
    { key, name, BCAT_ZONE, zone, tier, w, h, SVC_NONE, flags, TS_VILLAGE, 1955, \
      0, 0, cap, 0, pw, wa, pol, 0, 0, bm, var, up }
#define ZBE(key, name, zone, tier, w, h, flags, year, cap, pw, wa, pol, bm, var, up) \
    { key, name, BCAT_ZONE, zone, tier, w, h, SVC_NONE, flags, TS_VILLAGE, year, \
      0, 0, cap, 0, pw, wa, pol, 0, 0, bm, var, up }        /* M3: zone def with an era year */
#define PB(key, name, cat, w, h, svc, flags, ts, year, cost, upk, cap, jobs, pw, wa, pol, lv, lvr, var) \
    { key, name, cat, ZONE_NONE, 0, w, h, svc, flags, ts, year, \
      cost, upk, cap, jobs, pw, wa, pol, lv, lvr, 0, var, B_NONE }

const BuildingDef g_bdefs[B_COUNT] = {
    PB("none", "Nothing", BCAT_NONE, 1, 1, SVC_NONE, 0, TS_VILLAGE, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
    /*  key           name                          zone            t  w  h  flags        cap pw wa pol bm var up */
    ZB("house_t1", "Cottage",                    ZONE_HOUSES,     1, 1, 1, BDF_WOODEN,     3, 0, 0,  0, 1, 4, B_HOUSE_T2),
    ZB("house_t2", "Rintamamiestalo",            ZONE_HOUSES,     2, 1, 1, BDF_WOODEN,     6, 1, 1,  0, 1, 8, B_HOUSE_T3),
    ZB("house_t3", "Two-storey rintamamiestalo", ZONE_HOUSES,     3, 1, 1, BDF_WOODEN,     8, 2, 2,  0, 1, 6, B_NONE),
    ZB("apt_t1_3x1", "Lamellitalo",              ZONE_APARTMENTS, 1, 3, 1, 0,             75, 9, 9,  0, 2, 3, B_APT_T2_3X1),
    ZB("apt_t1_2x2", "Lamellitalo",              ZONE_APARTMENTS, 1, 2, 2, 0,            100, 12, 12, 0, 2, 3, B_APT_T2_2X2),
    ZB("apt_t2_3x1", "Precast lamellitalo",      ZONE_APARTMENTS, 2, 3, 1, 0,            135, 15, 15, 0, 2, 2, B_APT_T3_3X1),   /* M5: art (ART.md 3.1) */
    ZB("apt_t2_2x2", "Precast lamellitalo",      ZONE_APARTMENTS, 2, 2, 2, 0,            180, 20, 20, 0, 2, 2, B_APT_T3_2X2),
    ZB("apt_t3_3x1", "Elementtitalo",            ZONE_APARTMENTS, 3, 3, 1, 0,            210, 24, 24, 0, 2, 2, B_NONE),
    ZB("apt_t3_2x2", "Pistetalo",                ZONE_APARTMENTS, 3, 2, 2, 0,            280, 32, 32, 0, 2, 2, B_NONE),
    ZB("com_t1_1x1", "Kiosk",                    ZONE_COMMERCIAL, 1, 1, 1, BDF_WOODEN,     4, 1, 1,  0, 1, 3, B_COM_T2_1X1),
    ZB("com_t1_2x1", "Co-op shop",               ZONE_COMMERCIAL, 1, 2, 1, 0,              8, 2, 1,  0, 1, 3, B_COM_T2_2X1),
    ZB("com_t1_2x2", "Co-op store",              ZONE_COMMERCIAL, 1, 2, 2, 0,             16, 4, 2,  0, 1, 2, B_COM_T2_2X2),
    ZB("com_t2_1x1", "Corner shop",              ZONE_COMMERCIAL, 2, 1, 1, 0,             10, 2, 1,  0, 1, 3, B_COM_T3_1X1),
    ZB("com_t2_2x1", "Shop row",                 ZONE_COMMERCIAL, 2, 2, 1, 0,             20, 4, 2,  0, 1, 3, B_COM_T3_2X1),
    ZB("com_t2_2x2", "Shops with flats",         ZONE_COMMERCIAL, 2, 2, 2, 0,             40, 8, 4,  0, 1, 2, B_COM_T3_2X2),
    ZB("com_t3_1x1", "Bank office",              ZONE_COMMERCIAL, 3, 1, 1, 0,             20, 4, 2,  0, 2, 2, B_NONE),   /* M5: art */
    ZB("com_t3_2x1", "Office building",          ZONE_COMMERCIAL, 3, 2, 1, 0,             40, 8, 4,  0, 2, 2, B_NONE),
    ZB("com_t3_2x2", "Department store",         ZONE_COMMERCIAL, 3, 2, 2, 0,             80, 16, 8, 0, 2, 2, B_NONE),
    ZB("ind_t1_2x2", "Workshop",                 ZONE_INDUSTRY,   1, 2, 2, BDF_WOODEN,    40, 6, 2, 40, 2, 2, B_IND_T2_2X2),
    ZB("ind_t1_3x3", "Sawmill",                  ZONE_INDUSTRY,   1, 3, 3, BDF_WOODEN,    90, 12, 4, 80, 2, 2, B_IND_T2_3X3),
    ZB("ind_t2_2x2", "Factory",                  ZONE_INDUSTRY,   2, 2, 2, BDF_SMOKE,    100, 12, 4, 80, 2, 2, B_IND_T3_2X2),
    ZB("ind_t2_3x3", "Factory",                  ZONE_INDUSTRY,   2, 3, 3, BDF_SMOKE,    225, 24, 8, 160, 2, 2, B_IND_T3_3X3),
    ZB("ind_t3_2x2", "Works",                    ZONE_INDUSTRY,   3, 2, 2, BDF_SMOKE,    160, 20, 6, 120, 2, 0, B_NONE),
    ZB("ind_t3_3x3", "Large works",              ZONE_INDUSTRY,   3, 3, 3, BDF_SMOKE,    360, 40, 12, 240, 2, 0, B_NONE),
    /*  key                name                   cat         w  h  svc          flags                     ts                  year   cost  upk   cap jobs pw wa pol  lv lvr var */
    PB("town_hall", "Town hall",              BCAT_SPECIAL, 2, 2, SVC_NONE, BDF_UNIQUE, TS_VILLAGE, 1955,              0,  20,    0, 10, 3, 2,   0, 10, 6, 1),
    PB("coal_plant", "Coal power plant",      BCAT_POWER,   2, 2, SVC_NONE, BDF_SMOKE, TS_VILLAGE, 1955,            2500, 110,  500, 30, 0, 2, 400, -15, 6, 1),
    PB("hydro_plant", "Hydro power plant",    BCAT_POWER,   2, 2, SVC_NONE, BDF_NEEDS_RAPIDS, TS_VILLAGE, 1955,     6000,  40,  600, 10, 0, 0,   0,  0, 0, 0),
    PB("oil_plant", "Oil power plant",        BCAT_POWER,   2, 2, SVC_NONE, BDF_SMOKE, TS_VILLAGE, 1965,            5000, 100,  900, 30, 0, 2, 300, -15, 6, 1),   /* M7: art = coal plant alias; fuel at the oil price (trade.cpp) */
    PB("peat_plant", "Peat power plant",      BCAT_POWER,   2, 2, SVC_NONE, BDF_SMOKE, TS_VILLAGE, 1970,            4000,  80,  700, 30, 0, 2, 350, -15, 6, 0),
    PB("nuclear_plant", "Nuclear power plant",BCAT_POWER,   3, 3, SVC_NONE, 0, TS_CITY, 1977,                      30000, 400, 3000, 80, 0, 8,   0, -10, 6, 0),
    PB("pump", "Pumping station",             BCAT_WATER,   1, 1, SVC_NONE, BDF_NEEDS_SHORE, TS_VILLAGE, 1955,       600,  20,  500,  3, 4, 0,   0,  0, 0, 1),
    PB("well", "Groundwater well",            BCAT_WATER,   1, 1, SVC_NONE, BDF_NO_ROTATE, TS_VILLAGE, 1955,         250,   6,  150,  1, 2, 0,   0,  0, 0, 1),
    PB("water_tower", "Water tower",          BCAT_WATER,   1, 1, SVC_NONE, BDF_NO_ROTATE, TS_CHURCH_VILLAGE, 1955, 1200,  10,  200,  1, 1, 0,   0,  5, 4, 2),   /* F1: 200-unit source (was +30%); PL2: Church village (was Market town): apartments T2 need its pressure */
    PB("fire_station", "Fire station",        BCAT_SERVICE, 1, 1, SVC_FIRE, 0, TS_VILLAGE, 1955,                     500,  50, 2500,  8, 2, 2,   0,  2, 3, 1),
    PB("school", "School",                    BCAT_SERVICE, 2, 2, SVC_SCHOOL, 0, TS_VILLAGE, 1955,                  1000, 100,  500, 15, 4, 4,   0,  5, 4, 1),
    PB("clinic", "Health clinic",             BCAT_SERVICE, 1, 1, SVC_HEALTH, 0, TS_VILLAGE, 1955,                   700,  70, 2500, 10, 3, 3,   0,  3, 3, 1),
    PB("sauna", "Public sauna",               BCAT_SERVICE, 1, 1, SVC_SAUNA, BDF_WOODEN | BDF_SMOKE, TS_VILLAGE, 1955, 400, 25, 1500,  3, 1, 4,   4,  3, 3, 1),
    PB("police", "Police station",            BCAT_SERVICE, 1, 1, SVC_POLICE, 0, TS_MARKET_TOWN, 1960,               600,  40, 3000, 12, 2, 2,   0,  0, 0, 1),   /* M5: art */
    PB("library", "Library",                  BCAT_SERVICE, 1, 1, SVC_LIBRARY, 0, TS_MARKET_TOWN, 1960,              800,  30, 4000,  6, 2, 2,   0,  6, 4, 1),
    PB("post", "Post and telephone",          BCAT_SERVICE, 1, 1, SVC_POST, 0, TS_CHURCH_VILLAGE, 1960,              600,  20, 6000, 15, 3, 2,   0,  0, 0, 1),   /* M5: art; houses T3 need it from 1960 (PHONE_YEAR, sim.cpp) */
    PB("health_centre", "Health centre",      BCAT_SERVICE, 2, 2, SVC_HEALTH, 0, TS_TOWN, 1972,                     4000, 150, 8000, 40, 8, 8,   0,  5, 4, 1),   /* M5: Town (DESIGN 7: Regional centre, 40k, out of reach in most towns) */
    PB("park", "Park",                        BCAT_PARK,    1, 1, SVC_NONE, BDF_NO_CONDUCT | BDF_NO_ROTATE, TS_VILLAGE, 1955, 100, 2, 0, 0, 0, 0, 0, 12, 4, 3),
    PB("sports_field", "Sports field",        BCAT_PARK,    2, 2, SVC_NONE, BDF_NO_CONDUCT, TS_CHURCH_VILLAGE, 1960, 400,   5,    0,  1, 0, 0,   0, 15, 5, 0),
    PB("ice_rink", "Ice rink",                BCAT_PARK,    2, 2, SVC_NONE, BDF_NO_CONDUCT, TS_TOWN, 1965,          1500,  20,    0,  2, 2, 1,   0, 15, 5, 0),
    PB("church", "Church",                    BCAT_SPECIAL, 2, 2, SVC_NONE, 0, TS_CHURCH_VILLAGE, 1955,             2500,  10,    0,  2, 1, 1,   0, 15, 6, 0),
    PB("element_factory", "Element factory",  BCAT_SPECIAL, 3, 3, SVC_NONE, BDF_SMOKE, TS_CHURCH_VILLAGE, 1960,    5000, 100,  300, 120, 30, 10, 200, -10, 5, 1),   /* M5: cap = elements/month (civic.h); P1: Market town 1962, 5,000 mk (was Town 1960, 8,000: towns below 10,000 never got T2 flats); PL2: Church village 1960 */
    PB("station", "Railway station",          BCAT_SPECIAL, 2, 2, SVC_NONE, BDF_UNIQUE, TS_MARKET_TOWN, 1955,       3000,  30,    0, 10, 3, 2,   0,  8, 5, 1),   /* M2: back to the rail (transit.h) */
    /* M3 industry kinds (docs/research/INDUSTRY.md 5.1; art keys ART.md 3.1, several are aliases of the
       M1 tier art). The kind is industry_def_kind() (industry.cpp), chosen per new lot by site + kind demand.
       Jobs keep the 10 / 25 / 40 per tile rhythm; pollution per kind (sawmill low, engineering medium,
       textile half of engineering, dairy/electronics very low). Textile T2, engineering T3 and electronics
       grow directly at their tier (industry_candidates) when that tier's requirements are met. The joinery and
       the sewing workshop have no tier 2 at their footprint, so they start bigger (60 jobs, 15 per tile).
        key                     name                      zone          t  w  h  flags                    year cap  pw  wa  pol bm var up */
    ZBE("ind_wood_t1_3x3",    "Steam sawmill",           ZONE_INDUSTRY, 1, 3, 3, BDF_WOODEN | BDF_SMOKE, 1955,  90, 12,  4,  80, 2, 2, B_IND_WOOD_T2_3X3),
    ZBE("ind_wood_t1_2x2",    "Joinery",                 ZONE_INDUSTRY, 1, 2, 2, BDF_WOODEN,             1955,  60,  8,  2,  30, 2, 1, B_NONE),
    ZBE("ind_wood_t2_3x3",    "Sawmill and planing mill",ZONE_INDUSTRY, 2, 3, 3, BDF_WOODEN | BDF_SMOKE, 1955, 225, 24,  8, 100, 2, 1, B_NONE),
    ZBE("ind_metal_t1_2x2",   "Engineering workshop",    ZONE_INDUSTRY, 1, 2, 2, 0,                      1955,  40,  6,  2,  40, 2, 1, B_IND_METAL_T2_2X2),
    ZBE("ind_metal_t2_2x2",   "Engineering works",       ZONE_INDUSTRY, 2, 2, 2, BDF_SMOKE,              1955, 100, 12,  4,  80, 2, 2, B_NONE),
    ZBE("ind_metal_t3_3x3",   "Large engineering works", ZONE_INDUSTRY, 3, 3, 3, BDF_SMOKE,              1960, 360, 40, 12, 240, 3, 1, B_NONE),
    ZBE("ind_textile_t1_2x2", "Sewing workshop",         ZONE_INDUSTRY, 1, 2, 2, BDF_SMOKE,              1955,  60,  8,  4,  20, 2, 1, B_NONE),
    ZBE("ind_textile_t2_3x3", "Textile mill",            ZONE_INDUSTRY, 2, 3, 3, BDF_SMOKE,              1955, 225, 24, 12,  80, 3, 2, B_NONE),
    ZBE("ind_dairy_t1_2x2",   "Co-op dairy",             ZONE_INDUSTRY, 1, 2, 2, BDF_SMOKE,              1955,  30,  6,  8,  10, 2, 1, B_IND_DAIRY_T2_2X2),
    ZBE("ind_dairy_t2_2x2",   "Central dairy",           ZONE_INDUSTRY, 2, 2, 2, BDF_SMOKE,              1955,  80, 12, 16,  20, 2, 1, B_NONE),
    ZBE("ind_elec_t2_2x2",    "Cable and electronics works", ZONE_INDUSTRY, 2, 2, 2, 0,                  1965, 100, 16,  4,  10, 2, 1, B_NONE),
    /* M3 farm zone (INDUSTRY.md 6.2): capacity = residents (self-employed); the mechanised tier sheds 2.
       Forestry zone: the logging camp holds the harvest jobs (1 per 2 wood units of its forest). */
    ZBE("farm_t1",            "Small farm",              ZONE_FARM,     1, 1, 1, BDF_WOODEN,             1955,   5,  0,  0,   0, 1, 2, B_FARM_T2),
    ZBE("farm_t2",            "Dairy farm",              ZONE_FARM,     2, 1, 1, BDF_WOODEN,             1955,   6,  1,  0,   0, 1, 2, B_FARM_T3),
    ZBE("farm_t3",            "Mechanised farm",         ZONE_FARM,     3, 1, 1, 0,                      1962,   4,  2,  0,   0, 1, 2, B_NONE),
    ZBE("logging_camp",       "Logging camp",            ZONE_FORESTRY, 1, 1, 1, BDF_WOODEN | BDF_SMOKE, 1955,   8,  0,  0,   0, 1, 2, B_NONE),
    /* F3: gap filler (DESIGN 14.2): 8 jobs on one tile (a workshop has 10 per tile), little power, no water */
    ZB("ind_yard",           "Industrial yard",         ZONE_INDUSTRY, 1, 1, 1, BDF_WOODEN,                    8,  1,  0,  10, 1, 6, B_NONE),
    /* M2 transit (transit.h): the depot is required for bus lines to run; the stop shelter is placed by the bus line
       tool next to a stop (decoration: no jobs, no upkeep; the stop itself is the road tile) */
    PB("bus_depot", "Bus depot",              BCAT_SPECIAL, 2, 2, SVC_NONE, 0, TS_CHURCH_VILLAGE, 1955,      1500,  25,    0, 12, 2, 1,  10, -3, 3, 1),
    PB("bus_stop", "Bus stop",                BCAT_SPECIAL, 1, 1, SVC_NONE, 0, TS_VILLAGE, 1955,                0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
    /* M6 complexes (complex.h, INDUSTRY.md 7): modules are ordinary lots placed together at rot 0. Power, water and
       pollution are 0 here: complex.cpp sets them per stage (CPX_DEF_USE in net/fields). The halls carry the
       complex price (the tool shows it) and the land value effect ("the smell of money": -20 within 8 tiles).
       Jobs are the complex's own (complex_jobs), so every module has jobs 0. No upkeep: the company pays its own. */
#define MOD (BDF_MODULE | BDF_NO_ROTATE)
    PB("pm_logyard", "Log yard",              BCAT_SPECIAL, 2, 2, SVC_NONE, MOD, TS_CHURCH_VILLAGE, 1955,      0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
    PB("pm_hall", "Paper mill",               BCAT_SPECIAL, 3, 3, SVC_NONE, MOD | BDF_SMOKE, TS_CHURCH_VILLAGE, 1955, 5000, 0, 0, 0, 0, 0,  0, -20, 8, 1),
    PB("pm_chipper", "Debarking drum",        BCAT_SPECIAL, 2, 1, SVC_NONE, MOD, TS_CHURCH_VILLAGE, 1955,      0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
    PB("pm_boiler", "Boiler house",           BCAT_SPECIAL, 2, 2, SVC_NONE, MOD | BDF_SMOKE, TS_CHURCH_VILLAGE, 1955, 0, 0,  0,  0, 0, 0,   0,  0, 0, 1),
    PB("pm_digester", "Digesters and chip silos", BCAT_SPECIAL, 2, 2, SVC_NONE, MOD, TS_CHURCH_VILLAGE, 1955,  0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
    PB("pm_gate", "Mill gate",                BCAT_SPECIAL, 1, 1, SVC_NONE, MOD, TS_CHURCH_VILLAGE, 1955,      0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
    PB("pm_office", "Mill office",            BCAT_SPECIAL, 1, 1, SVC_NONE, MOD, TS_CHURCH_VILLAGE, 1955,      0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
    PB("vm_hall", "Valtion Metalli works",    BCAT_SPECIAL, 3, 3, SVC_NONE, MOD | BDF_SMOKE, TS_MARKET_TOWN, 1955, 6000, 0, 0, 0, 0, 0,   0, -10, 4, 1),
    PB("vm_yard", "Crane yard",               BCAT_SPECIAL, 2, 2, SVC_NONE, MOD, TS_MARKET_TOWN, 1955,         0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
    PB("vm_store", "Roll store",              BCAT_SPECIAL, 2, 1, SVC_NONE, MOD, TS_MARKET_TOWN, 1955,         0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
    PB("vm_siding", "Rail siding",            BCAT_SPECIAL, 3, 1, SVC_NONE, MOD, TS_MARKET_TOWN, 1955,         0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
    PB("vm_gate", "Works gate",               BCAT_SPECIAL, 2, 1, SVC_NONE, MOD, TS_MARKET_TOWN, 1955,         0,   0,    0,  0, 0, 0,   0,  0, 0, 1),
#undef MOD
    /* M6 patron gifts: wooden workers' row houses = tier-2 housing (30 residents, the needs of a tier-2 house), never
       grown by the sim (only placed with TOOL_GIFT); the patron's villa = a land value landmark, closed to the public */
    ZB("row_houses",         "Workers' row houses",     ZONE_HOUSES,   2, 3, 1, BDF_WOODEN,                   30,  5,  5,   0, 1, 2, B_NONE),
    PB("patron_villa", "The patron's villa",  BCAT_SPECIAL, 2, 2, SVC_NONE, 0, TS_VILLAGE, 1955,                0,   0,    0,  2, 1, 1,   0, 20, 5, 1),
    /* M5 utilities (civic.h): capacity = heat units (heat plant; consumers take their water_use) / residents treated
       (sewage works). Both sit on the water pipe network. Coal-fired heat plant: smoke; its upkeep includes the coal
       (flat, scaled by civic_fuel_pct for M7's oil prices). */
    PB("heat_plant", "District heat plant",   BCAT_SPECIAL, 2, 2, SVC_NONE, BDF_SMOKE, TS_MARKET_TOWN, 1960,      3500, 110,  600, 15, 4, 4, 150, -8, 4, 1),
    PB("sewage_works", "Sewage works",        BCAT_SPECIAL, 2, 2, SVC_NONE, 0, TS_MARKET_TOWN, 1960,              2500,  40, 4000,  8, 4, 0,   0, -6, 4, 1),
    /* M7 landmarks (era.cpp): expensive money sinks for built-out towns with land value, authority (politics) and
       tourism income (less than the upkeep). Unlocks by status + year (DESIGN 7; TV tower at Town 1970 instead of
       City so towns of 10-40k reach it); the ski jump and the friendship statue also unlock with their event cards
       (era_unlock_override). The presidential sauna doubles sauna diplomacy (Eastern relations). */
    PB("concert_hall", "Concert hall",        BCAT_SPECIAL, 2, 2, SVC_NONE, BDF_UNIQUE, TS_TOWN, 1962,           15000, 120,    0, 20, 6, 4,   0, 30, 8, 1),
    PB("tv_tower", "Television tower",        BCAT_SPECIAL, 1, 1, SVC_NONE, BDF_UNIQUE, TS_TOWN, 1970,           12000,  60,    0,  8, 8, 1,   0, 10, 6, 1),
    PB("ski_jump", "Ski jump",                BCAT_SPECIAL, 3, 1, SVC_NONE, BDF_UNIQUE | BDF_NO_ROTATE, TS_MARKET_TOWN, 1958, 6000, 40, 0, 4, 2, 0, 0, 15, 6, 1),
    PB("pres_sauna", "Presidential sauna",    BCAT_SPECIAL, 2, 2, SVC_NONE, BDF_UNIQUE | BDF_WOODEN, TS_MARKET_TOWN, 1955, 4000, 25, 0, 3, 1, 2, 0, 20, 5, 1),
    PB("friendship_statue", "Friendship statue", BCAT_SPECIAL, 1, 1, SVC_NONE, BDF_UNIQUE, TS_MARKET_TOWN, 1960,   2000,   5,    0,  0, 0, 0,   0,  5, 3, 1),
};

/* key, name, era_year, status_req, flags, capacity, speed, pollution, noise, cost, bridge_cost, upkeep10 */
const RoadDef g_roads[ROAD_CLASS_COUNT] = {
    { "none", "No road", 0, TS_VILLAGE, 0, 0, 0, 0, 0, 0, 0, 0 },
    { "grav", "Gravel road", 1955, TS_VILLAGE, RDF_ZONE_ACCESS | RDF_CARS | RDF_BRIDGE_OK | RDF_CARRIES_WATER,
      300, 1, 3, 1, 5, 40, 1 },
    { "road", "Street", 1955, TS_VILLAGE, RDF_ZONE_ACCESS | RDF_CARS | RDF_BRIDGE_OK | RDF_CARRIES_WATER,
      800, 2, 1, 2, 12, 60, 3 },
    { "main", "Main road", 1960, TS_MARKET_TOWN, RDF_ZONE_ACCESS | RDF_CARS | RDF_BRIDGE_OK | RDF_CARRIES_WATER,
      2000, 3, 2, 4, 30, 120, 8 },
    { "mway", "Motorway", 1965, TS_REGIONAL_CENTRE, RDF_CARS | RDF_BRIDGE_OK, 4000, 4, 3, 6, 80, 250, 20 },
    { "rail", "Railway", 1955, TS_VILLAGE, RDF_BRIDGE_OK, 0, 4, 0, 3, 40, 150, 5 },
};

/* key, name, max_dist (road tiles), demand_pct (load per resident), overlay */
const ServiceDef g_services[SVC_COUNT] = {
    { "fire",    "Fire protection",    20, 100, OVL_SVC_FIRE },
    { "school",  "Education",          25,  20, OVL_SVC_SCHOOL },
    { "health",  "Health care",        30, 100, OVL_SVC_HEALTH },
    { "sauna",   "Public sauna",       25, 100, OVL_SVC_SAUNA },   /* T12: 15 -> 25 (one or two per neighbourhood) */
    { "police",  "Police",             25, 100, OVL_SVC_POLICE },
    { "library", "Library",            30, 100, OVL_SVC_LIBRARY },
    { "post",    "Post and telephone", 30, 100, OVL_SVC_POST },
};

const EraDef g_eras[ERA_COUNT] = {
    { "Reconstruction", 1955 }, { "Structural change", 1960 }, { "Welfare state", 1968 },
    { "Late Cold War", 1975 }, { "Renewal", 1983 },
};

/* name, population, reward (money granted with the status decree, DESIGN 7) */
const StatusDef g_statuses[TS_COUNT] = {
    { "Village", 0, 0 }, { "Church village", 500, 2000 }, { "Market town", 3000, 5000 },
    { "Town", 10000, 15000 }, { "Regional centre", 40000, 40000 }, { "City", 100000, 100000 },
};

/* power_line, power_line_upkeep10, bulldoze_tile, clear_forest, timber_income, blast_rock, zone_tile, refund_pct,
   F1: pipe, pipe_water, pipe_upkeep10 */
const MiscDef g_misc = { 4, 1, 2, 5, 3, 20, 0, 25, 3, 15, 1 };

/* ------------------------------------------------------------------ tier requirements (DESIGN 5.4)
   Requirements to REACH (and to keep) a tier. Lists are cumulative (T2 repeats T1's items) so one list
   is the whole checklist. T04 additions to the DESIGN table: tier 1 rows (houses/commercial/industry
   need only a road; apartments need power + water from the start), POWER for commercial/industry T2. */
static const TierReq s_req_house[3][8] = {
    { { REQ_ROAD, 3 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_WATER, 0 }, { REQ_SCHOOL, 0 }, { REQ_SAUNA, 0 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_WATER, 0 }, { REQ_SCHOOL, 0 }, { REQ_SAUNA, 0 },
      { REQ_HEALTH, 0 }, { REQ_SHOP, 0 }, { REQ_PHONE, 0 } },
};
/* T12: the post office (REQ_PHONE) had no art in Milestone 1, so count 7 dropped it. M5: B_POST has art, the
   phone queue is back (8); it counts from 1960, when the post office unlocks (sim.cpp PHONE_YEAR), so the
   1955-59 wooden towns keep their pace. */
static const int s_nreq_house[3] = { 1, 5, 8 };
static const TierReq s_req_house3_lv = { REQ_LAND_VALUE, 40 };
/* PL2 (PLAYTEST 2 item 7a): the tier-2 block's two new needs, the water tower's pressure and precast elements, both come
   at Church village now (the element factory from 1960; was Market town 1962), so a town of tier-1 blocks can grow
   towards the 3,000 of Market town instead of waiting for it (the play-test town sat at 2,445 in 1971 with 30 blocks
   held back by REQ_WATER_TOWER + REQ_ELEMENTS). The factory's monthly pool stays the lahio's growth engine (and cap). */
static const TierReq s_req_apt[3][8] = {
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_WATER, 0 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_WATER, 0 }, { REQ_WATER_TOWER, 0 }, { REQ_ELEMENTS, 0 },
      { REQ_SCHOOL, 0 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_WATER, 0 }, { REQ_WATER_TOWER, 0 }, { REQ_ELEMENTS, 0 },
      { REQ_SCHOOL, 0 }, { REQ_HEAT, 0 }, { REQ_LOW_POLLUTION, 48 } },   /* M5: district heating (DESIGN 5.4 v1.0) */
};
static const int s_nreq_apt[3] = { 3, 6, 8 };
static const TierReq s_req_apt3_lv = { REQ_LAND_VALUE, 40 };   /* P1: was 50 (lahio towers rose on cheap edge-of-town land; heat is the real T3 gate) */
static const TierReq s_req_com[3][6] = {
    { { REQ_ROAD, 3 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_CUSTOMERS, 25 }, { REQ_LAND_VALUE, 25 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_CUSTOMERS, 50 }, { REQ_LAND_VALUE, 45 }, { REQ_MAIN_ROAD, 0 } },
};
static const int s_nreq_com[3] = { 1, 4, 5 };
static const TierReq s_req_ind[3][5] = {
    { { REQ_ROAD, 3 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_WORKERS, 75 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_WORKERS, 85 }, { REQ_MAIN_ROAD, 0 } },
};
static const int s_nreq_ind[3] = { 1, 3, 4 };
/* M3: farms (INDUSTRY.md 6.2: T2 rural electrification + school, T3 mechanised: power + a street or main road,
   the road part checked per lot by industry_lot_unmet) and the forestry zone's logging camp (tier 1 only).
   Kind inputs (REQ_WOOD / REQ_MILK) are per def too: industry.cpp. */
static const TierReq s_req_farm[3][3] = {
    { { REQ_ROAD, 3 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 }, { REQ_SCHOOL, 0 } },
    { { REQ_ROAD, 3 }, { REQ_POWER, 0 } },
};
static const int s_nreq_farm[3] = { 1, 3, 2 };
static const TierReq s_req_forestry = { REQ_ROAD, 3 };

/* Flattened per (zone, tier) so callers get one contiguous array. Built on first use. */
static TierReq s_reqflat[ZONE_COUNT][TIER_MAX + 1][REQ_COUNT];
static int     s_nreqflat[ZONE_COUNT][TIER_MAX + 1];
static int     s_reqflat_done;

static void reqflat_add(int z, int t, const TierReq *r)
{
    if (s_nreqflat[z][t] < REQ_COUNT) s_reqflat[z][t][s_nreqflat[z][t]++] = *r;
}

static void reqflat_build(void)
{
    int t, i;
    for (t = 1; t <= TIER_MAX; t++) {
        for (i = 0; i < s_nreq_house[t - 1]; i++) reqflat_add(ZONE_HOUSES, t, &s_req_house[t - 1][i]);
        for (i = 0; i < s_nreq_apt[t - 1]; i++)   reqflat_add(ZONE_APARTMENTS, t, &s_req_apt[t - 1][i]);
        for (i = 0; i < s_nreq_com[t - 1]; i++)   reqflat_add(ZONE_COMMERCIAL, t, &s_req_com[t - 1][i]);
        for (i = 0; i < s_nreq_ind[t - 1]; i++)   reqflat_add(ZONE_INDUSTRY, t, &s_req_ind[t - 1][i]);
        for (i = 0; i < s_nreq_farm[t - 1]; i++)  reqflat_add(ZONE_FARM, t, &s_req_farm[t - 1][i]);   /* M3 */
        reqflat_add(ZONE_FORESTRY, t, &s_req_forestry);
    }
    reqflat_add(ZONE_HOUSES, 3, &s_req_house3_lv);
    reqflat_add(ZONE_APARTMENTS, 3, &s_req_apt3_lv);
    for (t = 1; t <= TIER_MAX; t++) {                 /* M2: a road route to the town's jobs (traffic.cpp) */
        static const TierReq route = { REQ_ROUTE, 0 };
        reqflat_add(ZONE_HOUSES, t, &route);
        reqflat_add(ZONE_APARTMENTS, t, &route);
    }
    s_reqflat_done = 1;
}

/* ------------------------------------------------------------------ access */
const BuildingDef *def_building(int id)
{
    return (id > 0 && id < B_COUNT) ? &g_bdefs[id] : &g_bdefs[B_NONE];
}

int def_building_find(const char *key)
{
    int i;
    for (i = 1; i < B_COUNT; i++)
        if (!strcmp(g_bdefs[i].key, key)) return i;
    return B_NONE;
}

int def_zone_building(int zone, int tier, int w, int h)
{
    int i;
    for (i = 1; i < B_COUNT; i++) {
        const BuildingDef *d = &g_bdefs[i];
        if (d->cat == BCAT_ZONE && d->zone == zone && d->tier == tier && d->w == w && d->h == h) return i;
    }
    return B_NONE;
}

void def_footprint(int id, int rot, int *w, int *h)
{
    const BuildingDef *d = def_building(id);
    if (rot & 1) { *w = d->h; *h = d->w; }
    else         { *w = d->w; *h = d->h; }
}

int def_unlocked(int id, int year, int status, int all_unlocked)
{
    const BuildingDef *d = def_building(id);
    if (id <= B_NONE || id >= B_COUNT) return 0;
    if (all_unlocked) return 1;
    if (era_unlock_override(id, year, status)) return 1;     /* M7: landmarks unlocked by event cards */
    return year >= d->era_year && status >= d->status_req;
}

int def_road_unlocked(int cls, int year, int status, int all_unlocked)
{
    if (cls <= ROAD_NONE || cls >= ROAD_CLASS_COUNT) return 0;
    if (all_unlocked) return 1;
    return year >= g_roads[cls].era_year && status >= g_roads[cls].status_req;
}

int def_zone_unlocked(int zone, int year, int status, int all_unlocked)
{
    UNUSED(year);
    if (zone <= ZONE_NONE || zone >= ZONE_COUNT) return 0;
    if (all_unlocked) return 1;
    return zone == ZONE_APARTMENTS ? status >= TS_CHURCH_VILLAGE : 1;   /* DESIGN 7 */
}

int def_tier_reqs(int zone, int tier, const TierReq **out)
{
    if (!s_reqflat_done) reqflat_build();
    if (zone <= ZONE_NONE || zone >= ZONE_COUNT || tier < 1 || tier > TIER_MAX) { *out = NULL; return 0; }
    *out = s_reqflat[zone][tier];
    return s_nreqflat[zone][tier];
}

int def_req_growc(int req)
{
    switch (req) {
    case REQ_ROAD: case REQ_ROUTE: return GROWC_ROAD;     /* M2: no route to jobs = a road problem */
    case REQ_POWER: return GROWC_POWER;
    case REQ_WATER: case REQ_WATER_TOWER: return GROWC_WATER;
    case REQ_SCHOOL: case REQ_SAUNA: case REQ_HEALTH: case REQ_SHOP: case REQ_PHONE: case REQ_LIBRARY: return GROWC_SERVICES;
    case REQ_LAND_VALUE: return GROWC_LAND_VALUE;
    case REQ_LOW_POLLUTION: return GROWC_POLLUTION;
    default: return GROWC_OTHER;
    }
}

int def_era(int year)
{
    int i;
    for (i = ERA_COUNT - 1; i > 0; i--)
        if (year >= g_eras[i].year_from) return i;
    return 0;
}

int def_status_for_population(s32 pop)
{
    int i;
    for (i = TS_COUNT - 1; i > 0; i--)
        if (pop >= g_statuses[i].population) return i;
    return 0;
}

int def_selfcheck(void)
{
    int i, j, bad = 0;
    for (i = 1; i < B_COUNT; i++) {
        const BuildingDef *d = &g_bdefs[i];
        if (!d->key || !d->name || d->w < 1 || d->h < 1 || d->w > 3 || d->h > 3) {
            log_printf("defs: error: building %d bad key/name/footprint", i); bad++;
        }
        for (j = 1; j < i; j++)
            if (!strcmp(g_bdefs[j].key, d->key)) { log_printf("defs: error: duplicate key %s", d->key); bad++; }
        if (d->cat == BCAT_ZONE) {
            if (d->zone <= ZONE_NONE || d->zone >= ZONE_COUNT || d->tier < 1 || d->tier > TIER_MAX) {
                log_printf("defs: error: %s zone/tier", d->key); bad++;
            }
            if (d->capacity <= 0) { log_printf("defs: error: %s has no capacity", d->key); bad++; }
            if (d->upgrade_to != B_NONE) {
                const BuildingDef *u = def_building(d->upgrade_to);
                if (u->zone != d->zone || u->tier != d->tier + 1 || u->w != d->w || u->h != d->h) {
                    log_printf("defs: error: %s upgrade_to %s mismatch", d->key, u->key); bad++;
                }
            }
        } else if (d->cat != BCAT_NONE) {
            if (d->zone != ZONE_NONE || d->tier != 0) { log_printf("defs: error: %s zone/tier on placed", d->key); bad++; }
            if (d->cat == BCAT_SERVICE && d->service >= SVC_COUNT) { log_printf("defs: error: %s service", d->key); bad++; }
            if (d->cat == BCAT_SERVICE && d->capacity <= 0) { log_printf("defs: error: %s capacity", d->key); bad++; }
        }
        if ((d->flags & BDF_NO_ROTATE) && d->w != d->h && !(d->flags & BDF_MODULE) && i != B_SKI_JUMP) {   /* M6: modules sit at rot 0; M7: the ski jump is fixed art */ log_printf("defs: error: %s no-rotate but not square", d->key); bad++; }
    }
    for (i = 1; i < ZONE_COUNT; i++) {
        const TierReq *r;
        if (def_tier_reqs(i, 1, &r) < 1 || r[0].req != REQ_ROAD) { log_printf("defs: error: zone %d tier 1 reqs", i); bad++; }
    }
    for (i = 0; i < SVC_COUNT; i++)
        if (g_services[i].max_dist == 0) { log_printf("defs: error: service %s max_dist 0", g_services[i].key); bad++; }
    return bad;
}

/* ------------------------------------------------------------------ 1.1 difficulty (D1, DESIGN_1_1 2.2)
   EASY must stay the 1.0 values exactly (the 1.0 balance runs and world hashes are the regression test). Normal/Hard
   values are the design's bot-tuning starting points (BALANCE.md section 10 records tuning). D1 tuning (with W1's
   winter in): Hard upkeep 115 -> 105 %, index cap 160 -> 150 % (1980), tax yield 90 -> 95 %, overtime 105 -> 110 %:
   with the design's values every bot but politics/metal ended 1955-1990 stuck at the emergency-loan cap (no recovery =
   against "fail softly"); the upkeep surcharge and yield cut phase in 1960-65 (budget.cpp); taxes start at the
   tolerance. Hard's pressure now comes from winter, decline, emigration, cards, Sweden, fires and the tax tolerance. */
const DiffParams g_diff[DIFF_COUNT] = {
    /* name     upk  cap  tax neu  ot  emU emH dch dut swe exp | card gmin gmax auth east shock fire spr fwin | wload hw hload hwat plg plgH frost */
    { "Easy",   100, 130, 100, 9, 125,  8,  4,  4,  8,  0,  0,   100,  12,  24,  20,  50,  100, 100, 3, 100,     0,  0,   0, 100,  0,   0,   0 },
    { "Normal", 105, 145, 100, 9, 115,  6,  3,  4,  6,  2,  1,   115,  12,  20,  24,  45,  115, 150, 3, 150,    15, 20,  15,  90, 25,  40,   1 },
    { "Hard",   105, 150,  95, 8, 110,  5,  3,  3,  4,  4,  1,   140,  12,  16,  30,  40,  130, 200, 4, 150,    20, 33,  20,  85, 35,  55,   2 },
};
static int s_diff_level = DIFF_EASY;
const DiffParams *diff(void) { return &g_diff[s_diff_level]; }
int  diff_level(void) { return s_diff_level; }
void diff_set_level(int level) { s_diff_level = level < 0 ? 0 : level >= DIFF_COUNT ? DIFF_COUNT - 1 : level; }
const char *diff_name(int level) { return g_diff[level < 0 ? 0 : level >= DIFF_COUNT ? DIFF_COUNT - 1 : level].name; }
