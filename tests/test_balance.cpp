/* test_balance.cpp - T12 balance and feel: 20-year play-style runs on 3 maps, year tables, loose milestone
   assertions so later changes don't silently break pacing. Owner: T12. Details: docs/BALANCE.md.

   The players are small deterministic BOTS, not fixed scripts: each month they look at the city like a
   player would (power/water margins, service loads, the top problems, empty zones, demand bars, money)
   and issue ordinary tool commands through the script interpreter (tools_script_line: the same code path
   as the mouse and as -script in the game). Every command is also recorded, so each run leaves a
   replayable script: tests/out/bal_<style>_<map>.txt (copy to scripts/ and run it in the VM with
   -seed S -preset P -script FILE -fast to reproduce the same city there).

   Styles: planner (services as needed), greedy (zones, power, water, never a service), industry (one
   neighbourhood zoned industry, early estate), dense (apartments from 1960), tax13/tax5 (planner at 13%/5%).
   Output: tests/out/balance_<style>_<map>.txt (year table + milestones), tests/out/balance_summary.txt.
   Run alone: make -f tests/Makefile.mac T=balance */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "tools.h"
#include "traffic.h"
#include "mapgen.h"
#include "politics.h"
#include "industry.h"
#include "complex.h"
#include "save.h"
#include "transit.h"
#include "civic.h"
#include "text.h"
#include "era.h"           /* PL2: era messages in bal_months */
#include "hard.h"          /* 1.1 D1: Sweden 1969-70 (per-level runs) */
#include "advice.h"        /* 1.1 D1: quiet years (no Cabinet warning) */
#include "season.h"        /* 1.1 D1: the winter peak (planner habit on Normal/Hard) */

/* ------------------------------------------------------------------ maps: neighbourhood modules
   A module is 29 x 22 tiles incl. its border streets (rows 0/7/14/21, columns 0/14/28): every zone tile
   is within 3 tiles of a street, ~470 zone tiles = ~2,800 residents at tier 2, ~3,700 at tier 3. */
typedef struct {
    const char *key; u32 seed; int preset;
    int mod[4][2];              /* module origins in build order */
    int ind[4];                 /* industrial estate x, y, w, h (2 bands of 7 rows + plant sites) */
    int conn[4];                /* power line from the estate to the first module */
} BalMap;

static const BalMap s_maps[] = {
    { "s7",  7,  MAP_PRESET_LAKELAND, { { 73, 73 }, { 73, 52 }, { 46, 31 }, { 74, 31 } }, { 103, 74, 24, 21 }, { 103, 76, 101, 76 } },
    { "c3",  3,  MAP_PRESET_COAST,    { { 68, 40 }, { 96, 40 }, { 68, 19 }, { 96, 19 } }, { 68, 5, 56, 14 },   { 68, 19, 68, 40 } },
    { "r30", 30, MAP_PRESET_RIVER,    { { 52, 55 }, { 24, 55 }, { 52, 34 }, { 24, 34 } }, { 24, 20, 56, 14 },  { 52, 34, 52, 55 } },
};
#define NMAPS ((int)COUNTOF(s_maps))

/* service slots inside a module (north tile, relative); wells touch two streets */
enum { SL_SCHOOL, SL_SCHOOL2, SL_SAUNA, SL_SAUNA2, SL_SAUNA3, SL_CLINIC, SL_FIRE, SL_TOWER, SL_PARK,
       SL_WELL, SL_WELL2, SL_WELL3, SL_WELL4, SL_WELL5, SL_WELL6, SL_WELL7,
       SL_POST, SL_POLICE, SL_LIBRARY, SL_SEWAGE, SL_HEAT, SL_TOWER2, SL_TOWER3,   /* M5 */
       SL_COUNT };
static const struct { const char *key; s8 x, y, sz; } s_slot[SL_COUNT] = {
    { "school", 1, 8, 2 }, { "school", 15, 15, 2 }, { "sauna", 15, 1, 1 }, { "sauna", 27, 8, 1 }, { "sauna", 1, 18, 1 },
    { "clinic", 15, 8, 1 }, { "fire_station", 1, 15, 1 }, { "water_tower", 1, 1, 1 }, { "park", 13, 1, 1 },
    { "well", 27, 1, 1 }, { "well", 27, 15, 1 }, { "well", 1, 6, 1 }, { "well", 13, 13, 1 }, { "well", 1, 3, 1 },
    { "well", 27, 4, 1 }, { "well", 13, 8, 1 },
    /* M5: post office (houses T3 phone), police, library, a sewage works and a heat plant (2x2, apartments T3) */
    { "post", 15, 6, 1 }, { "police", 13, 20, 1 }, { "library", 15, 13, 1 }, { "sewage_works", 11, 1, 2 },
    { "heat_plant", 11, 8, 2 }, { "water_tower", 15, 11, 1 }, { "water_tower", 27, 18, 1 },
};

/* ------------------------------------------------------------------ styles */
enum { BS_PLANNER, BS_GREEDY, BS_INDUSTRY, BS_DENSE, BS_TAX13, BS_TAX5,
       BS_POLITICS,             /* M1: the planner with politics on, using decrees, letters and event cards */
       BS_RURAL,                /* M3: the planner + a forestry zone on the nearest forest and a farm zone */
       BS_MILL,                 /* M6: rural + the paper mill in year 3, a station at Market town, every gift placed (s7) */
       BS_METAL,                /* M6: the planner + Valtion Metalli by the railway at Market town, politics off (s7) */
       BS_NAIVE,                /* PL2: a first-time human (PLAYTEST 2 item 7c): houses + apartments, power and water only
                                   after shortages, services only where the checklist complains, few shops, no element
                                   factory or heat, politics on but town rights signed a year late, other decrees never */
       BS_COUNT };
static const char *s_style_name[BS_COUNT] = { "planner", "greedy", "industry", "dense", "tax13", "tax5", "politics", "rural",
                                              "mill", "metal", "naive" };

typedef struct {
    int pop, money, net, opnet, demR, demC, demI, jobs, unemp, emig, homeless;
    int h1, h2, h3, apt, c1, c2, i1, i2, aband, status, tavg10, tmax, preq, pres;
} BalRow;

typedef struct {
    int style, map;
    BalRow row[21];               /* row[k] = January of 1955 + k (row[0] = start) */
    int nrows;
    int first_lot_steps;          /* steps from the first zoning to the first grown lot */
    int status_month[TS_COUNT];   /* months since start when the status was reached (-1 never) */
    int min_money, loans, max_drop_pct, errors, cmds;
    int money_y2;                 /* money at January 1957 */
    s32 opnet_sum[21];
    u32 hash;
    int n_school, n_sauna, n_clinic, n_fire, n_plant, n_well;  /* service buildings at the end */
    int pol_signed, pol_letters, pol_cards, pol_elections, pol_pop_min, pol_pop_max, pol_electors[4];   /* M1 */
    int ind_lots[IK_COUNT], camps, farms, farms_t3, wood_cut, farm_res;                              /* M3 */
} BalRun;

/* ------------------------------------------------------------------ bot */
static const BalMap *M;
static int  s_style, s_mods, s_ind1, s_plants, s_tower, s_com_big[4], s_errors, s_cmds;
static u32  s_used[4];             /* slot bits used per module */
static int  s_level = DIFF_EASY;   /* 1.1 D1: the difficulty of the next bot town (Easy = the 1.0 runs) */
static FILE *s_rec;               /* recorded script */
static int  s_rec_wait;
static const char *s_rec_tag = "";  /* P1: file-name tag of the recorded script ("_era": the 1955-1990 runs) */

static void rec_flush_wait(void)
{
    if (s_rec && s_rec_wait) fprintf(s_rec, "wait %d\n", s_rec_wait);
    s_rec_wait = 0;
}

static int s_cmd_try;              /* M6: a command that may fail (a logging road across a pond) is not an error */
static int cmd(const char *fmt, ...)
{
    char line[160];
    int wait, r;
    va_list ap;
    va_start(ap, fmt);
    str_vprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    rec_flush_wait();
    if (s_rec) fprintf(s_rec, "%s\n", line);
    r = tools_script_line(line, &wait, NULL);
    s_cmds++;
    if (r == SCR_ERROR) { if (!s_cmd_try) s_errors++; log_printf("balance: command failed%s: %s", s_cmd_try ? " (tried)" : "", line); return 0; }
    return 1;
}

static int s_mod_kind[4];          /* P1: the zone each module was built with (dense: module 1 predates 1960 = houses) */
static int zone_kind(int m)
{
    if (m >= 0 && m < s_mods && m < 4) return s_mod_kind[m];
    if (s_style == BS_INDUSTRY && m == 2) return ZONE_INDUSTRY;
    if (s_style == BS_DENSE && sim_date()->year >= 1960 && m >= 1) return ZONE_APARTMENTS;
    if (s_style == BS_NAIVE && (m & 1) && def_zone_unlocked(ZONE_APARTMENTS, sim_date()->year, sim_stats()->town_status, 0))
        return ZONE_APARTMENTS;    /* PL2: houses and apartments in turn, apartments as soon as the zone unlocks */
    return ZONE_HOUSES;
}
static int services_on(void) { return s_style != BS_GREEDY; }

static int build_slot(int m, int sl)
{
    int x = M->mod[m][0] + s_slot[sl].x, y = M->mod[m][1] + s_slot[sl].y, sz = s_slot[sl].sz, i, j, occupied = 0;
    if (s_used[m] & (1u << sl)) return 0;
    for (j = y; j < y + sz; j++) for (i = x; i < x + sz; i++) if (world_lot_at(i, j)) occupied = 1;
    if (occupied) cmd("bulldoze %d %d %d %d", x, y, x + sz - 1, y + sz - 1);
    s_used[m] |= 1u << sl;
    return cmd("build %s %d %d", s_slot[sl].key, x, y);
}

static void build_module(int m)
{
    int ox = M->mod[m][0], oy = M->mod[m][1], r, kind = zone_kind(m);
    static const char *zn[ZONE_COUNT] = { "", "houses", "apartments", "commercial", "industry", "forestry", "farm" };
    for (r = 0; r <= 21; r += 7) cmd("road %d %d %d %d street straight", ox, oy + r, ox + 28, oy + r);
    for (r = 0; r <= 28; r += 14) cmd("road %d %d %d %d street straight", ox + r, oy, ox + r, oy + 21);
    /* F1: pipes under the two inner street rows (WATER.md 3.2: one pipe per pair of block rows; buildings and
       zoned land carry the water through the blocks, the well/tower slots lie inside them). Industry needs
       no water in Milestone 1. */
    if (kind != ZONE_INDUSTRY) for (r = 7; r <= 14; r += 7) cmd("pipe %d %d %d %d", ox, oy + r, ox + 28, oy + r);
    if (kind != ZONE_INDUSTRY) {
        for (r = 0; r <= 21; r += 7) cmd("power %d %d %d %d", ox, oy + r, ox + 28, oy + r);
        cmd("power %d %d %d %d", ox, oy, ox, oy + 21);
        if (services_on() && s_style != BS_NAIVE) { build_slot(m, SL_SCHOOL); build_slot(m, SL_SAUNA); }
        build_slot(m, SL_WELL);
        if (s_style != BS_NAIVE) build_slot(m, SL_WELL2);      /* PL2: the naive player adds water when it runs short */
    } else {
        cmd("power %d %d %d %d", ox, oy, ox, oy + 21);
        build_slot(m, SL_WELL);
    }
    cmd("zone %d %d %d %d %s", ox + 1, oy + 1, ox + 27, oy + 20, zn[kind]);
    if (kind != ZONE_INDUSTRY) cmd("zone %d %d %d %d commercial", ox + (s_style == BS_NAIVE ? 21 : 15), oy + 18, ox + 27, oy + 20);   /* PL2: few shops */
    if (m < 4) s_mod_kind[m] = kind;
    s_mods++;
}

static void build_estate(int part)
{
    int x = M->ind[0], y = M->ind[1], w = M->ind[2], h = M->ind[3], r, b0 = part ? 7 : 0, b1 = part ? (h / 7) * 7 : 7, c;
    for (r = b0; r <= b1; r += 7) { cmd("road %d %d %d %d street straight", x, y + r, x + w, y + r); cmd("power %d %d %d %d", x, y + r, x + w, y + r); }
    for (c = 0; c <= w; c += 14) cmd("road %d %d %d %d street straight", x + c, y + b0, x + c, y + b1);
    if (w % 14) cmd("road %d %d %d %d street straight", x + w, y + b0, x + w, y + b1);
    cmd("power %d %d %d %d", x, y + b0, x, y + b1);
    for (r = b0; r < b1; r += 7) {     /* plant sites (2 rows x 12 tiles at the west end) stay free */
        cmd("zone %d %d %d %d industry", x + 13, y + r + 1, x + w - 1, y + r + 6);
        cmd("zone %d %d %d %d industry", x + 1, y + r + 3, x + 12, y + r + 6);
    }
    if (part) s_ind1 = 1;
}

/* M3: the next plant's price incl. clearing forest / blasting rock on its site (the tool charges it) */
static Money plant_cost(void)
{
    int n = s_plants, x = M->ind[0] + 1 + 2 * (n % 6), y = M->ind[1] + 1 + 7 * (n / 6), i, j;
    Money c = g_bdefs[B_COAL_PLANT].cost;
    for (j = y; j < y + 2; j++)
        for (i = x; i < x + 2; i++) {
            int ter = TER_TYPE(world_tile(i, j));
            if (ter == TER_FOREST) c += g_misc.clear_forest;
            else if (ter == TER_ROCK) c += g_misc.blast_rock;
        }
    return c;
}

static int build_plant(void)
{
    int n = s_plants, x = M->ind[0] + 1 + 2 * (n % 6), y = M->ind[1] + 1 + 7 * (n / 6);
    if (n >= 12) return 0;
    s_plants++;
    return cmd("build coal_plant %d %d", x, y);
}

static int module_of(int x, int y)
{
    int m;
    for (m = 0; m < s_mods; m++)
        if (x >= M->mod[m][0] && x <= M->mod[m][0] + 28 && y >= M->mod[m][1] && y <= M->mod[m][1] + 21) return m;
    return -1;
}

static int next_slot(int first, int last, int prefer_m)
{
    int m, sl, k;
    for (k = -1; k < s_mods; k++) {
        m = k < 0 ? prefer_m : k;
        if (m < 0 || m >= s_mods || zone_kind(m) == ZONE_INDUSTRY) continue;
        for (sl = first; sl <= last; sl++) if (!(s_used[m] & (1u << sl))) return m * 32 + sl;
    }
    return -1;
}

/* 1.1 B1: the town's health centre (2x2, Town) on a free 2x2 slot of a neighbourhood (the heat-plant, sewage or second
   school place), bulldozing what grew there */
static int build_hcentre(Money *cash)
{
    static const u8 sp[3] = { SL_HEAT, SL_SEWAGE, SL_SCHOOL2 };
    int m, q;
    if (!def_unlocked(B_HEALTH_CENTRE, sim_date()->year, sim_stats()->town_status, 0) || *cash < g_bdefs[B_HEALTH_CENTRE].cost) return 0;
    for (m = 0; m < s_mods; m++) {
        if (zone_kind(m) == ZONE_INDUSTRY) continue;
        for (q = 0; q < 3; q++) {
            int sl = sp[q], x = M->mod[m][0] + s_slot[sl].x, y = M->mod[m][1] + s_slot[sl].y;
            if (s_used[m] & (1u << sl)) continue;
            s_used[m] |= 1u << sl;
            cmd("bulldoze %d %d %d %d", x, y, x + 1, y + 1);
            if (cmd("build health_centre %d %d", x, y)) { *cash -= g_bdefs[B_HEALTH_CENTRE].cost; return 1; }
        }
    }
    return 0;
}

static int empty_zone_tiles(int zone)
{
    int x, y, n = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = WT(x, y);
            if (!t->lot && TILE_ZONE(t) == zone) n++;
        }
    return n;
}

/* M5: the Milestone-2 services as a careful player adds them: post offices (the phone queue: houses T3), police
   when the town grows or crime shows, a library when there is money, sewage works when untreated sewage reaches
   the lakes; the dense style also builds the element factory (apartments T2+) and heat plants (apartments T3). */
static int bot_unlocked(int def) { return def_unlocked(def, sim_date()->year, sim_stats()->town_status, 0); }

static int s_factory;
static void bot_m5(Money *cash)
{
    const CityStats *st = sim_stats();
    UtilSummary us;
    int s, m, k;
    SimProblem pr[4];
    int np = sim_top_problems(pr, 4), phone_problem = 0;
    Money keep = st->power_demand * 10 >= st->power_supply * 7 ? plant_cost() : 0;   /* power comes first */
    Money cash0 = *cash;
    for (k = 0; k < np; k++) if (pr[k].req == REQ_PHONE && pr[k].residents >= 60) phone_problem = 1;
    if (phone_problem && !st->svc_capacity[SVC_POST]) keep = 0;   /* the first exchange can't wait */
    *cash -= keep;
    if (bot_unlocked(B_POST) && *cash >= (phone_problem || !st->svc_capacity[SVC_POST] ? 800 : 3500) && st->population >= 1500 &&
        (!st->svc_capacity[SVC_POST] || st->svc_load[SVC_POST] * 10 >= st->svc_capacity[SVC_POST] * 9 || phone_problem)) {
        s = next_slot(SL_POST, SL_POST, phone_problem ? module_of(pr[0].x, pr[0].y) : -1);
        if (s >= 0 && build_slot(s / 32, s % 32)) *cash -= 600;
    }
    if (bot_unlocked(B_POLICE) && *cash >= 20000 && st->population >= 5000 &&
        (!st->svc_capacity[SVC_POLICE] || st->svc_load[SVC_POLICE] * 10 >= st->svc_capacity[SVC_POLICE] * 9)) {
        s = next_slot(SL_POLICE, SL_POLICE, -1);
        if (s >= 0 && build_slot(s / 32, s % 32)) *cash -= 600;
    }
    if (bot_unlocked(B_LIBRARY) && *cash >= 20000 && st->svc_capacity[SVC_LIBRARY] < st->population / 2) {
        s = next_slot(SL_LIBRARY, SL_LIBRARY, -1);
        if (s >= 0 && build_slot(s / 32, s % 32)) *cash -= 800;
    }
    civic_util_summary(UT_SEWAGE, &us);
    if (bot_unlocked(B_SEWAGE_WORKS) && *cash >= 8000 && us.untreated >= 2000 && civic_lake_avg() >= CIVIC_LAKE_TINT) {
        s = next_slot(SL_SEWAGE, SL_SEWAGE, -1);
        if (s >= 0 && build_slot(s / 32, s % 32)) *cash -= 2500;
    }
    if (s_style == BS_DENSE) {
        if (!s_factory && bot_unlocked(B_ELEMENT_FACTORY) && *cash >= 8000) {   /* P1: factory 5,000 mk */
            int x = M->ind[0] + M->ind[2] - 4, y = M->ind[1] + 1;
            s_factory = 1;
            cmd("bulldoze %d %d %d %d", x, y, x + 2, y + 2);
            if (cmd("build element_factory %d %d", x, y)) *cash -= 5000;
        }
        for (m = 1; m < s_mods; m++) {           /* apartments T2+ need the water pressure: towers across the block */
            static const u8 tw[3] = { SL_TOWER, SL_TOWER2, SL_TOWER3 };
            if (zone_kind(m) != ZONE_APARTMENTS || !bot_unlocked(B_WATER_TOWER) || *cash < 3000) continue;
            for (k = 0; k < 3; k++) if (!(s_used[m] & (1u << tw[k]))) break;
            if (k < 3 && build_slot(m, tw[k])) { *cash -= 1200; break; }
        }
        civic_util_summary(UT_HEAT, &us);
        if (bot_unlocked(B_HEAT_PLANT) && *cash >= 6000 && st->lots_tier[ZONE_APARTMENTS][2] > 0 &&
            (us.supply == 0 || us.lots_short > 0 || us.lots_unconnected > 0 ||   /* P1: + unheated blocks; 1.1 D1: the winter peak */
             (s32)((s64)us.used * (s_level != DIFF_EASY ? season_peak_pct() : 100) / IMAX(100, s_level != DIFF_EASY ? season_heat_pct() : 100)) * 10 >= us.supply * 9)) {
            for (m = 1; m < s_mods; m++) if (zone_kind(m) == ZONE_APARTMENTS && !(s_used[m] & (1u << SL_HEAT))) break;
            if (m < s_mods && build_slot(m, SL_HEAT)) *cash -= 3500;
        }
    }
    *cash += keep;
    UNUSED(cash0);
}

/* 1.1 D1: the budget habit on Normal/Hard (Easy keeps the 1.0 bots): each January a planner-style player looks at last
   year's books: a loss with less than half a year of expenses in the bank (or a negative treasury) -> one notch of tax
   (at most 13 %); a surplus with 18 months (Hard 9) of expenses banked -> one notch back down (not below the level's
   tolerance: above it taxes cost migrants and factories) */
static void bot_budget(void)
{
    s32 net = 0, ex = 0;
    int k, t = g_budget.tax_res;
    if (s_level == DIFF_EASY || sim_date()->month != 0 || s_style == BS_GREEDY || s_style == BS_TAX13 || s_style == BS_TAX5) return;
    for (k = 0; k < BL_COUNT; k++) {
        if (k == BL_CONSTRUCTION || k == BL_TIMBER) continue;
        net += g_budget.last_year[k];
        if (g_budget.last_year[k] < 0) ex -= g_budget.last_year[k];
    }
    ex /= 12;
    if ((net < 0 && g_budget.money < ex * 6) || g_budget.money < 0) {
        if (t < 13) cmd("tax %d %d", t + 1, t + 1);
    } else if (net > 0 && g_budget.money > ex * (s_level == DIFF_HARD ? 9 : 12) && t > diff()->tax_neutral)   /* B1: Normal 18 -> 12 */
        cmd("tax %d %d", t - 1, t - 1);   /* taxes above the tolerance cost growth: back down once the books allow */
}

/* one decision round (monthly): at most a few actions, cheapest urgent ones first */
static void bot_month(void)
{
    const CityStats *st = sim_stats();
    SimProblem pr[4];
    int np, k, s, m, done = 0;
    Money cash = g_budget.money;
    /* utilities ahead of demand (a player watching the power/water bars) */
    /* M3: the plant site may need forest clearing / rock blasting on top of the plant's price */
    s32 pdem = st->power_demand, ppeak = 0;
    if (s_level != DIFF_EASY) {   /* 1.1 B1: a planner reads the forecast (DESIGN_1_1 2.6: supply >= the forecast peak): the 1.0
                                     rule on the summer use, plus a plant when the October-November winter peak exceeds supply
                                     (the Cabinet's winter line; D1 kept 80 % of the peak all year: never warned, plants idle) */
        pdem = (s32)((s64)pdem * 100 / IMAX(100, season_power_pct()));
        if (sim_date()->month == 9 || sim_date()->month == 10) ppeak = (s32)((s64)pdem * season_peak_pct() / 100);
    }
    if ((st->power_short_lots > 0 || pdem * 10 >= st->power_supply * 8 || ppeak > st->power_supply) && cash >= plant_cost()) {
        Money pc = plant_cost();
        if (build_plant()) cash -= pc;
    }
    if ((st->water_short_lots > 0 || st->water_demand * 10 >= (s_level != DIFF_EASY && season_forecast() == SFC_HARD ?   /* 1.1 D1: */
            st->water_supply * season_water_pct() / 100 * diff()->hard_water_pct / IMAX(1, season_water_pct()) : st->water_supply) * 8) && cash >= 250) {   /* frost */
        s = next_slot(SL_WELL, SL_WELL7, -1);
        if (s >= 0 && build_slot(s / 32, s % 32)) cash -= 250;
    }
    if (services_on()) {
        /* capacity: loads near capacity, plus the first clinic / fire station once the village grows */
        static const struct { int svc, first, last, minpop; Money cost; } want[4] = {
            { SVC_SAUNA, SL_SAUNA, SL_SAUNA3, 0, 400 }, { SVC_SCHOOL, SL_SCHOOL, SL_SCHOOL2, 0, 1000 },
            { SVC_HEALTH, SL_CLINIC, SL_CLINIC, 700, 700 }, { SVC_FIRE, SL_FIRE, SL_FIRE, 900, 500 } };
        for (k = 0; k < 4 && done < 2; k++) {
            int need = st->svc_capacity[want[k].svc] ? st->svc_load[want[k].svc] * 10 >= st->svc_capacity[want[k].svc] * 9
                                                     : st->population >= want[k].minpop;
            if (!need || cash < want[k].cost + 1000) continue;
            s = next_slot(want[k].first, want[k].last, -1);
            if (s >= 0 && build_slot(s / 32, s % 32)) { cash -= want[k].cost; done++; }
        }
        /* coverage holes: the top problems name a service and a place */
        np = sim_top_problems(pr, 4);
        for (k = 0; k < np && done < 3; k++) {
            int first = -1, last = -1;
            Money cost = 0;
            if (pr[k].residents < 60) continue;
            if (pr[k].req == REQ_SAUNA) { first = SL_SAUNA; last = SL_SAUNA3; cost = 400; }
            else if (pr[k].req == REQ_SCHOOL) { first = SL_SCHOOL; last = SL_SCHOOL2; cost = 1000; }
            else if (pr[k].req == REQ_HEALTH) { first = SL_CLINIC; last = SL_CLINIC; cost = 700; }
            else if (pr[k].req == REQ_LIBRARY && bot_unlocked(B_LIBRARY)) { first = SL_LIBRARY; last = SL_LIBRARY; cost = 800; }   /* 1.1 D1 (never on Easy) */
            if (first < 0 || cash < cost + 1000) continue;
            m = module_of(pr[k].x, pr[k].y);
            if (m < 0) continue;
            for (s = first; s <= last; s++) if (!(s_used[m] & (1u << s))) break;
            if (s <= last && build_slot(m, s)) { cash -= cost; done++; }
        }
        bot_m5(&cash);
        if (s_level != DIFF_EASY) {   /* 1.1 D1: a planner reads the paper: the era expectations a year ahead (hard.h) */
            int yr = sim_date()->year, mo = sim_date()->month;
            for (m = 0; m < s_mods && done < 3; m++) {
                int z = zone_kind(m);
                if (z != ZONE_HOUSES && z != ZONE_APARTMENTS) continue;
                if (hard_expect_phase(HX_LIBRARY, z, 3, yr, mo) && bot_unlocked(B_LIBRARY) && !(s_used[m] & (1u << SL_LIBRARY)) &&
                    cash >= 1800 && build_slot(m, SL_LIBRARY)) { cash -= 800; done++; }
                if (hard_expect_phase(HX_CLINIC, z, 2, yr, mo) && z == ZONE_APARTMENTS && !(s_used[m] & (1u << SL_CLINIC)) &&
                    cash >= 1700 && build_slot(m, SL_CLINIC)) { cash -= 700; done++; }
            }
            if (done < 3 && hard_expect_phase(HX_HCENTRE, ZONE_HOUSES, 3, yr, mo) && !hard_hcentre_ok(NULL, NULL) && cash >= 5000) {
                Money c2 = cash - 1000;   /* 1.1 B1: the 1985 health centre a year ahead (keeps 1,000 mk) */
                if (build_hcentre(&c2)) { cash = c2 + 1000; done++; }
            }
        }
        if (!s_tower && st->town_status >= TS_MARKET_TOWN && cash >= 2000 && s_mods >= 2) {
            s_tower = 1;
            if (build_slot(1, SL_TOWER)) cash -= 1200;
        }
    }
    /* expansion: a new neighbourhood when the zones are nearly built out. F1: demand >= 0 (was > 0): zoned
       land conducting power and the pipe costs moved the s7 planner run into a frozen state with the R bar at
       exactly 0 for years (jobs-limited, industry allowance below one lot); a player expands then too. */
    if (s_level != DIFF_EASY && (sim_date()->month == 9 || sim_date()->month == 10) && cash < 6000 + plant_cost()) {}   /* 1.1 D1:
                                                     October-November: keep a reserve for the winter, no new neighbourhood */
    else if (s_mods < 4 && sim_demand(DEM_R)->value >= 0 &&
        empty_zone_tiles(ZONE_HOUSES) + empty_zone_tiles(ZONE_APARTMENTS) / 2 < 50 && cash >= 6000) {
        build_module(s_mods);
        cash = g_budget.money;
    }
    if (!s_ind1 && (sim_demand(DEM_I)->value > 20 || s_style == BS_INDUSTRY) && cash >= 2500) {
        build_estate(1);
        cash = g_budget.money;
    }
    /* more shops when commercial demand stays high and the strips are full */
    if (sim_demand(DEM_C)->value > 35 && empty_zone_tiles(ZONE_COMMERCIAL) < 6) {
        for (m = 0; m < s_mods; m++) if (!s_com_big[m] && zone_kind(m) != ZONE_INDUSTRY) break;
        if (m < s_mods) {
            s_com_big[m] = 1;
            cmd("zone %d %d %d %d commercial", M->mod[m][0] + 15, M->mod[m][1] + 15, M->mod[m][0] + 27, M->mod[m][1] + 17);
        }
    }
    /* parks when rich (a player spending a surplus) */
    if (services_on() && cash > 30000)
        for (m = 0; m < s_mods; m++) if (!(s_used[m] & (1u << SL_PARK))) { build_slot(m, SL_PARK); break; }
}

/* ------------------------------------------------------------------ PL2: naive human bot (BS_NAIVE)
   What the play-test 2 player did (PLAYTEST.md session 2, item 7): zones houses and apartments, a corner of shops per
   neighbourhood, reacts to shortages instead of planning ahead (a plant after three months of brownouts, a well after
   two dry months), builds a service only where the checklist names it for many residents, never builds the element
   factory, the heat plant, police, a library or sewage works, and has never opened the President's desk: event cards
   wait three months and get the first answer, the town-rights decree is signed a year after it was offered, no other
   decree is ever signed. It expands like the planner (when the zones are nearly full). */
static int s_nv_power, s_nv_water, s_nv_rights, s_nv_card, s_nv_com, s_nv_full[SVC_COUNT], s_nv_hole[REQ_COUNT];
static int s_nv_nofactory;         /* PL2 item 10: 1 = this naive player never builds the element factory (debt check) */
static int s_nv_broke;             /* months the treasury has been negative (the "TREASURY EMPTY" headlines) */
static int s_nv_ot, s_nv_ot_svc = -1;   /* 1.1 B1: months the Cabinet has named the same overtime bill */
static int s_nv_hc;                /* 1.1 B1: months the health-centre expectation has been in the paper */
static int s_jitter;                /* B1 tuning: LAHIO_B1_JITTER=n (0 = the real runs) */
static void bot_naive(void)
{
    const CityStats *st = sim_stats();
    SimProblem pr[6];
    int np = sim_top_problems(pr, 6), k, s, m, done = 0, power_prob = st->power_short_lots > 0, water_prob = st->water_short_lots > 0;
    Money cash = g_budget.money;
    for (k = 0; k < np; k++) {
        if (pr[k].req == REQ_POWER && pr[k].residents >= 100) power_prob = 1;
        if (pr[k].req == REQ_WATER && pr[k].residents >= 100) water_prob = 1;
    }
    s_nv_power = power_prob ? s_nv_power + 1 : 0;
    s_nv_water = water_prob ? s_nv_water + 1 : 0;
    if (s_nv_power >= 3 && cash >= plant_cost()) { Money pc = plant_cost(); if (build_plant()) { cash -= pc; s_nv_power = 0; } }
    if (s_nv_water >= 2 && cash >= 250) {
        s = next_slot(SL_WELL, SL_WELL7, -1);
        if (s >= 0 && build_slot(s / 32, s % 32)) { cash -= 250; s_nv_water = 0; }
    }
    /* a full service (the ticker's "school full" warning) gets a second building after half a year */
    {
        static const struct { int svc, first, last; Money cost; } want[4] = {
            { SVC_SCHOOL, SL_SCHOOL, SL_SCHOOL2, 1000 }, { SVC_SAUNA, SL_SAUNA, SL_SAUNA3, 400 },
            { SVC_HEALTH, SL_CLINIC, SL_CLINIC, 700 }, { SVC_FIRE, SL_FIRE, SL_FIRE, 500 } };
        for (k = 0; k < 4; k++) {
            int sv = want[k].svc;
            s_nv_full[sv] = st->svc_capacity[sv] && st->svc_load[sv] > st->svc_capacity[sv] ? s_nv_full[sv] + 1 : 0;
            if (done || s_nv_full[sv] < 6 || cash < want[k].cost + 1000) continue;
            s = next_slot(want[k].first, want[k].last, -1);
            if (s >= 0 && build_slot(s / 32, s % 32)) { cash -= want[k].cost; done++; s_nv_full[sv] = 0; }
        }
    }
    /* services where the checklist has complained for three months */
    for (k = 0; k < REQ_COUNT; k++) {
        int q, seen = 0;
        for (q = 0; q < np; q++) if (pr[q].req == k && pr[q].residents >= 100) seen = 1;
        s_nv_hole[k] = seen ? s_nv_hole[k] + 1 : 0;
    }
    for (k = 0; k < np && done < 1; k++) {
        int first = -1, last = -1;
        Money cost = 0;
        if (pr[k].residents < 100 || s_nv_hole[pr[k].req] < 3) continue;
        if (pr[k].req == REQ_SAUNA) { first = SL_SAUNA; last = SL_SAUNA3; cost = 400; }
        else if (pr[k].req == REQ_SCHOOL) { first = SL_SCHOOL; last = SL_SCHOOL2; cost = 1000; }
        else if (pr[k].req == REQ_HEALTH) { first = SL_CLINIC; last = SL_CLINIC; cost = 700; }
        else if (pr[k].req == REQ_PHONE && bot_unlocked(B_POST)) { first = SL_POST; last = SL_POST; cost = 600; }
        else if (pr[k].req == REQ_LIBRARY && bot_unlocked(B_LIBRARY)) { first = SL_LIBRARY; last = SL_LIBRARY; cost = 800; }   /* 1.1 D1 */
        else if (pr[k].req == REQ_WATER_TOWER && bot_unlocked(B_WATER_TOWER)) { first = SL_TOWER; last = SL_TOWER; cost = 1200; }
        if (first < 0 || cash < cost + 500) continue;
        m = module_of(pr[k].x, pr[k].y);
        if (m < 0) continue;
        if (first == SL_TOWER) {                      /* the three tower slots are not contiguous */
            static const u8 tw[3] = { SL_TOWER, SL_TOWER2, SL_TOWER3 };
            for (s = 0; s < 3 && (s_used[m] & (1u << tw[s])); s++) {}
            if (s < 3 && build_slot(m, tw[s])) { cash -= cost; done++; }
            continue;
        }
        for (s = first; s <= last; s++) if (!(s_used[m] & (1u << s))) break;
        if (s > last) { s = next_slot(first, last, m); if (s >= 0) { m = s / 32; s %= 32; } else s = last + 1; }   /* elsewhere */
        if (s <= last && build_slot(m, s)) { cash -= cost; done++; s_nv_hole[pr[k].req] = 0; }
    }
    /* 1.1 B1 (Normal/Hard; Easy keeps the 1.0 bot): the paper's Cabinet box (three lines) names a full service or an overtime
       bill for three months running -> the building it names, if the treasury holds its price (a first-time player reads
       the paper; the 1.0 bot only looked at the checklist and wanted 1,000 mk to spare) */
    if (s_level != DIFF_EASY) {
        static const u8 sl[SVC_COUNT][2] = { { SL_FIRE, SL_FIRE }, { SL_SCHOOL, SL_SCHOOL2 }, { SL_CLINIC, SL_CLINIC },
                                             { SL_SAUNA, SL_SAUNA3 }, { SL_POLICE, SL_POLICE }, { SL_LIBRARY, SL_LIBRARY }, { SL_POST, SL_POST } };
        Advice av[3];
        int na = advice_collect(av, 3), q, sv = -1;
        for (q = 0; q < na && sv < 0; q++) {
            if (av[q].kind == ADV_OVERTIME) sv = (int)av[q].a;
            else if (av[q].kind == ADV_HEALTH) sv = SVC_HEALTH;
            else if (av[q].kind == ADV_SCHOOL) sv = SVC_SCHOOL;
            else if (av[q].kind == ADV_SAUNA) sv = SVC_SAUNA;
            else if (av[q].kind == ADV_EXPECT_LIBRARY) sv = SVC_LIBRARY;
            else if (av[q].kind == ADV_EXPECT_CLINIC) sv = SVC_HEALTH;
            else if (av[q].kind == ADV_EXPECT_HCENTRE) sv = SVC_COUNT;   /* the health centre (below) */
        }
        if (sv == SVC_COUNT) {   /* the National Board of Health's line: the town's health centre */
            s_nv_hc = s_nv_hc + 1;
            if (s_nv_hc >= 3 && !done) { Money c2 = cash; if (build_hcentre(&c2)) { cash = c2; done++; s_nv_hc = 0; } }
            sv = -1;
        } else s_nv_hc = 0;
        s_nv_ot = sv >= 0 && sv == s_nv_ot_svc ? s_nv_ot + 1 : 1;
        s_nv_ot_svc = sv;
        if (sv >= 0 && s_nv_ot >= 3 && !done && cash >= g_bdefs[B_FIRE_STATION + sv].cost && bot_unlocked(B_FIRE_STATION + sv)) {
            const BuildingDef *bd = &g_bdefs[B_FIRE_STATION + sv];
            s = next_slot(sl[sv][0], sl[sv][1], -1);
            if (s < 0 && bd->w == 1) {   /* its own slots are taken: a spare 1x1 slot of the neighbourhood (park, towers, wells) */
                static const u8 spare[6] = { SL_PARK, SL_TOWER3, SL_TOWER2, SL_WELL7, SL_WELL6, SL_WELL5 };
                for (m = 0; m < s_mods && s < 0; m++) {
                    if (zone_kind(m) == ZONE_INDUSTRY) continue;
                    for (q = 0; q < 6; q++) if (!(s_used[m] & (1u << spare[q]))) { s = m * 32 + spare[q]; break; }
                }
                if (s >= 0) {
                    int x = M->mod[s / 32][0] + s_slot[s % 32].x, y = M->mod[s / 32][1] + s_slot[s % 32].y;
                    s_used[s / 32] |= 1u << (s % 32);
                    if (world_lot_at(x, y)) cmd("bulldoze %d %d %d %d", x, y, x, y);
                    if (cmd("build %s %d %d", bd->key, x, y)) { cash -= bd->cost; done++; s_nv_ot = 0; }
                    s = -1;
                }
            }
            if (s >= 0 && build_slot(s / 32, s % 32)) { cash -= bd->cost; done++; s_nv_ot = 0; }
        }
    }
    /* the element factory, a year and a half after the paper first counted blocks waiting for precast elements */
    if (!done && !s_factory && !s_nv_nofactory && s_nv_hole[REQ_ELEMENTS] >= 18 && bot_unlocked(B_ELEMENT_FACTORY) && cash >= 6000) {
        int x = M->ind[0] + M->ind[2] - 4, y = M->ind[1] + 1;
        s_factory = 1;
        cmd("bulldoze %d %d %d %d", x, y, x + 2, y + 2);
        if (cmd("build element_factory %d %d", x, y)) { cash -= 5000; done++; }
    }
    /* a new police station and a library once the town feels like a town (a player likes new buildings) */
    if (!done && st->town_status >= TS_MARKET_TOWN && st->population >= 5000 && cash >= 8000) {
        if (!st->svc_capacity[SVC_POLICE] && bot_unlocked(B_POLICE)) { s = next_slot(SL_POLICE, SL_POLICE, -1); if (s >= 0 && build_slot(s / 32, s % 32)) { cash -= 600; done++; } }
        else if (!st->svc_capacity[SVC_LIBRARY] && bot_unlocked(B_LIBRARY)) { s = next_slot(SL_LIBRARY, SL_LIBRARY, -1); if (s >= 0 && build_slot(s / 32, s % 32)) { cash -= 800; done++; } }
    }
    if (!done && st->population >= 2500 && !st->svc_capacity[SVC_FIRE] && cash >= 3000) {   /* a fire station "some day" */
        s = next_slot(SL_FIRE, SL_FIRE, -1);
        if (s >= 0 && build_slot(s / 32, s % 32)) cash -= 500;
    }
    /* expansion like everyone: a new neighbourhood when the zones are nearly full */
    if (s_mods < 4 && empty_zone_tiles(ZONE_HOUSES) + empty_zone_tiles(ZONE_APARTMENTS) / 2 < 50 && cash >= 6000) {
        build_module(s_mods);
        cash = g_budget.money;
    }
    if (!s_ind1 && sim_demand(DEM_I)->value > 20 && cash >= 2500) { build_estate(1); cash = g_budget.money; }
    /* more shops only after a year of loud commercial demand */
    s_nv_com = sim_demand(DEM_C)->value > 60 && empty_zone_tiles(ZONE_COMMERCIAL) < 6 ? s_nv_com + 1 : 0;
    if (s_nv_com >= 12) {          /* three small blocks per neighbourhood at most (s_com_big counts them) */
        static const s8 blk[3][4] = { { 21, 15, 27, 17 }, { 15, 18, 20, 20 }, { 15, 15, 20, 17 } };
        for (m = 0; m < s_mods; m++) if (s_com_big[m] < 3 && zone_kind(m) != ZONE_INDUSTRY) break;
        if (m < s_mods) {
            const s8 *b = blk[s_com_big[m]++];
            cmd("zone %d %d %d %d commercial", M->mod[m][0] + b[0], M->mod[m][1] + b[1], M->mod[m][0] + b[2], M->mod[m][1] + b[3]);
        }
        s_nv_com = 0;
    }
    if (cash > 30000)
        for (m = 0; m < s_mods; m++) if (!(s_used[m] & (1u << SL_PARK))) { build_slot(m, SL_PARK); break; }
    /* the paper says TREASURY EMPTY: one notch of tax, a second after a year; back to 9% when the books are healthy */
    s_nv_broke = g_budget.money < 0 ? s_nv_broke + 1 : 0;
    if ((s_nv_broke == 3 && g_budget.tax_res < 10) || (s_nv_broke == 15 && g_budget.tax_res < 11))
        cmd("tax %d %d", g_budget.tax_res + 1, g_budget.tax_bus + 1);
    else if (g_budget.money > 15000 && !g_budget.loan && g_budget.tax_res > 9) cmd("tax 9 9");
    /* the President's desk, seldom visited */
    if (politics_decree_state(DEC_TOWN_RIGHTS) == DS_OK) { if (++s_nv_rights >= 12 && cmd("pol sign townrights")) s_nv_rights = 0; }
    else s_nv_rights = 0;
    if (politics_pending_event() >= 0) { if (++s_nv_card >= 3 && cmd("pol choose 0")) s_nv_card = 0; }
    else s_nv_card = 0;
}

/* ------------------------------------------------------------------ M3: rural bot (BS_RURAL)
   In year 2 the planner also zones forestry on the nearest big forest (a gravel logging road into it) and a farm
   zone on open land next to a neighbourhood (a farm street with a power line, so farms can electrify and mechanise). */
static int s_rural_done;

static int rect_overlaps(int x0, int y0, int w, int h, int ax, int ay, int aw, int ah, int margin)
{
    return x0 < ax + aw + margin && ax - margin < x0 + w && y0 < ay + ah + margin && ay - margin < y0 + h;
}

static int rect_share(int x0, int y0, int w, int h, int forest)
{
    int x, y, n = 0, m;
    for (m = 0; m < 4; m++) if (rect_overlaps(x0, y0, w, h, M->mod[m][0], M->mod[m][1], 29, 22, 2)) return -1;   /* future neighbourhoods */
    if (rect_overlaps(x0, y0, w, h, M->ind[0], M->ind[1], M->ind[2] + 1, M->ind[3] + 1, 2)) return -1;           /* the estate */
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++) {
            const Tile *t;
            if (!world_in(x, y)) return -1;
            t = WT(x, y);
            if (t->lot || TILE_ZONE(t) || TILE_ROAD(t) || (t->flags & TF_PROTECTED)) return -1;
            if (forest ? TER_TYPE(t) == TER_FOREST : (TER_TYPE(t) == TER_GRASS || TER_TYPE(t) == TER_FIELD)) n++;
        }
    return n * 100 / (w * h);
}

/* the free rect (share >= min %) nearest to (cx, cy), scanning every 2 tiles */
static int find_rect(int cx, int cy, int w, int h, int forest, int min, int maxd, int *ox, int *oy)
{
    int x, y, best = 1 << 30;
    for (y = 2; y + h < g_world.h - 2; y += 2)
        for (x = 2; x + w < g_world.w - 2; x += 2) {
            int d = IABS(x + w / 2 - cx) + IABS(y + h / 2 - cy);
            if (d > maxd || d >= best || rect_share(x, y, w, h, forest) < min) continue;
            best = d; *ox = x; *oy = y;
        }
    return best < (1 << 30);
}

/* nearest access road tile to (x, y) */
static int near_road(int x, int y, int *rx, int *ry)
{
    int r, i, j;
    for (r = 1; r < 40; r++)
        for (j = y - r; j <= y + r; j++)
            for (i = x - r; i <= x + r; i++) {
                const Tile *t;
                if (IMAX(IABS(i - x), IABS(j - y)) != r || !world_in(i, j)) continue;
                t = WT(i, j);
                if (TILE_ROAD(t) && (g_roads[TILE_ROAD(t)].flags & RDF_ZONE_ACCESS)) { *rx = i; *ry = j; return 1; }
            }
    return 0;
}

static void bot_rural(void)
{
    int fx, fy, ax, ay, cx = M->mod[0][0] + 14, cy = M->mod[0][1] + 11;
    if (s_rural_done || sim_date()->total_months < 18 || g_budget.money < 4000) return;
    s_rural_done = 1;
    /* forestry: a 14 x 12 block of mostly forest within ~45 tiles of the first neighbourhood */
    if (find_rect(cx, cy, 14, 12, 1, 75, 60, &fx, &fy)) {
        int mx = fx + 7;
        if (near_road(mx, fy + 6, &ax, &ay)) {
            cmd("road %d %d %d %d gravel", ax, ay, mx, fy + 6);       /* logging road to the middle of the block */
            cmd("road %d %d %d %d gravel straight", mx, fy, mx, fy + 11);
        }
        cmd("zone %d %d %d %d forestry", fx, fy, fx + 13, fy + 11);
    }
    /* farms: an 18 x 7 block of open land next to the town, a farm road + power line through its middle */
    if (find_rect(cx, cy, 18, 7, 0, 85, 70, &fx, &fy)) {
        int my = fy + 3;
        if (near_road(fx, my, &ax, &ay)) {
            cmd("road %d %d %d %d gravel", ax, ay, fx, my);
            cmd("power %d %d %d %d", ax, ay, fx, my);
        }
        cmd("road %d %d %d %d street straight", fx, my, fx + 17, my);   /* a street: farms can mechanise (T3) */
        cmd("power %d %d %d %d", fx, my, fx + 17, my);
        cmd("zone %d %d %d %d farm", fx, fy, fx + 17, fy + 6);
    }
}

/* ------------------------------------------------------------------ M6: complex bots (BS_MILL, BS_METAL; map s7 only)
   BS_MILL = the rural bot + Kuusikosken Paperi Oy once the town is a Church village with 7,000 mk (from year 3), on
   the shore site nearest the first neighbourhood, with a gravel road, a power line and a pipe to it; a railway station
   at Market town (PM3 needs one); every gift placed next to the mill. BS_METAL = the planner + Valtion Metalli by the
   railway at Market town (politics off: 6,000 mk). Year table: tests/out/complex_<style>_s7.txt */
static int s_cpx_done, s_cpx_station, s_cpx_gifts, s_cpx_forest2;
static FILE *s_ctab2;
static void cpx_track(void);

static int cpx_find_site(int kind, int *ox, int *oy)
{
    int x, y, best = 1 << 30, cx = M->mod[0][0] + 14, cy = M->mod[0][1] + 11, w, h, m, r;
    Money c;
    complex_size(kind, &w, &h);
    for (y = 1; y + h < g_world.h - 1; y++)
        for (x = 1; x + w < g_world.w - 1; x++) {
            int d = IABS(x + w / 2 - cx) + IABS(y + h / 2 - cy);
            if (d >= best) continue;
            for (m = 0; m < 4; m++) if (rect_overlaps(x, y, w, h, M->mod[m][0], M->mod[m][1], 29, 22, 1)) break;
            if (m < 4 || rect_overlaps(x, y, w, h, M->ind[0], M->ind[1], M->ind[2] + 1, M->ind[3] + 1, 1)) continue;
            r = complex_check_place(kind, x, y, &c);
            if (r == REJ_CPX_ROAD) d += 12;              /* a site that needs its own road is a little worse */
            else if (r != PLACE_OK) continue;
            if (d >= best) continue;
            best = d; *ox = x; *oy = y;
        }
    return best < (1 << 30);
}

/* nearest tile (Chebyshev rings) with a zone-byte flag that is free for a power line (no lot, no road) */
static int cpx_nearest(int x, int y, int zf, int free_tile, int *ox, int *oy)
{
    int r, i, j;
    for (r = 1; r < 60; r++)
        for (j = y - r; j <= y + r; j++)
            for (i = x - r; i <= x + r; i++) {
                const Tile *t;
                if (IMAX(IABS(i - x), IABS(j - y)) != r || !world_in(i, j)) continue;
                t = WT(i, j);
                if (!(t->zone & zf)) continue;
                if (free_tile && (t->lot || TILE_ROAD(t) || TER_IS_WATER(TER_TYPE(t)))) continue;
                *ox = i; *oy = j; return 1;
            }
    return 0;
}

static void cpx_connect(int kind, int x, int y, int nets)
{
    int w, h, ax, ay, tx, ty, sx, sy;
    Money c;
    complex_size(kind, &w, &h);
    sx = x + w; sy = y + h / 2;                            /* the tile east of the footprint's middle */
    if (!nets) {
        if (complex_check_place(kind, x, y, &c) == REJ_CPX_ROAD && near_road(sx, sy, &ax, &ay)) {
            int ry = (ax < sx && ay >= y && ay < y + h) ? y + h + 1 : ay;   /* never through the site: down the east side first */
            cmd("road %d %d %d %d gravel straight", sx, sy, sx, ry);
            cmd("road %d %d %d %d gravel", sx, ry, ax, ay);
        }
        return;
    }
    if (cpx_nearest(sx, y + h - 1, ZF_POWER_NET, 1, &tx, &ty)) cmd("power %d %d %d %d", sx, y + h - 1, tx, ty);
    if (cpx_nearest(x + 2, y, ZF_WATER_NET, 0, &tx, &ty)) cmd("pipe %d %d %d %d", x + 2, y, tx, ty);
}

static void cpx_row(void)
{
    const Complex *c = complex_of_kind(s_style == BS_MILL ? CPX_PAPER : CPX_METAL);
    if (!s_ctab2) return;
    if (!c) { fprintf(s_ctab2, "%d %6d | not built\n", sim_date()->year, sim_stats()->population); return; }
    fprintf(s_ctab2, "%d %6d | stage %d%s jobs %4d filled %4d out %3d%% | wood %3d got %3d%% | mood %3d gifts %d/%d | dep %2d%% town %d"
                     " contrib %4d | vol %4d W %4d E %4d | event %d/%d\n",
            sim_date()->year, sim_stats()->population, c->stage, c->next ? "+" : " ", c->jobs, c->employed, c->output,
            c->wood_need, c->wood_pct, c->mood, c->gifts, s_cpx_gifts, complex_dependency_pct(), complex_company_town(),
            (int)complex_contribution_last(), c->vol, c->west, c->east, c->ev, c->ev_state);
}

static void bot_complex(void)
{
    const CityStats *st = sim_stats();
    int kind = s_style == BS_MILL ? CPX_PAPER : CPX_METAL, x, y;
    const Complex *c = complex_of_kind(kind);
    if (!s_cpx_done && (kind == CPX_PAPER ? st->town_status >= TS_CHURCH_VILLAGE && sim_date()->total_months >= 24
                                          : st->town_status >= TS_MARKET_TOWN) && g_budget.money >= complex_tool_cost(kind == CPX_PAPER ? B_PM_HALL : B_VM_HALL) + 300) {
        s_cpx_done = 1;
        if (cpx_find_site(kind, &x, &y)) {
            int fx, fy, ax, ay;
            cpx_connect(kind, x, y, 0);                    /* its road first (if it needs one), the money for the works */
            cmd("complex place %s %d %d", kind == CPX_PAPER ? "paper" : "metal", x, y);
            cpx_connect(kind, x, y, 1);                    /* then a power line and a pipe */
            /* the mill's own forest: a second forestry block of ~350 tiles near it (PM2 needs 80 wu, PM3 120) */
            if (kind == CPX_PAPER && find_rect(x + 2, y + 2, 22, 16, 1, 70, 70, &fx, &fy) && near_road(fx + 11, fy + 8, &ax, &ay)) {
                s_cmd_try = 1;
                cmd("road %d %d %d %d gravel", ax, ay, fx + 11, fy + 8);
                cmd("road %d %d %d %d gravel straight", fx + 11, fy, fx + 11, fy + 15);
                cmd("road %d %d %d %d gravel straight", fx, fy + 8, fx + 21, fy + 8);
                s_cmd_try = 0;
                cmd("zone %d %d %d %d forestry", fx, fy, fx + 21, fy + 15);
            }
        }
        return;
    }
    if (!c) return;
    if (kind == CPX_PAPER && !s_cpx_station && st->town_status >= TS_MARKET_TOWN && g_budget.money >= 5000) {
        int best = 1 << 30, bx = 0, by = 0, rot, rr;
        s_cpx_station = 1;
        for (y = 2; y < g_world.h - 3; y++)
            for (x = 2; x < g_world.w - 3; x++) {
                int d = IABS(x - c->x) + IABS(y - c->y);
                if (d >= best || world_lot_check(B_STATION, x, y, 0) != PLACE_OK) continue;
                rot = transit_station_rot(x, y);
                if (rot < 0 || !transit_station_ok(x, y, rot) || !world_footprint_road_dist(x, y, x + 1, y + 1, 1, &rr)) continue;
                best = d; bx = x; by = y;
            }
        if (best < (1 << 30)) cmd("build station %d %d", bx, by);
    }
    /* PL2: PM2 runs the mill short of wood (mood -10, no gifts): a third logging block, like a player reading the mill's query */
    if (kind == CPX_PAPER && !s_cpx_forest2 && c->stage >= 2 && c->wood_pct < 90 && g_budget.money >= 3000) {
        int fx, fy, ax, ay;
        s_cpx_forest2 = 1;
        if (find_rect(c->x + 2, c->y + 2, 18, 14, 1, 70, 80, &fx, &fy) && near_road(fx + 9, fy + 7, &ax, &ay)) {
            s_cmd_try = 1;
            cmd("road %d %d %d %d gravel", ax, ay, fx + 9, fy + 7);
            cmd("road %d %d %d %d gravel straight", fx + 9, fy, fx + 9, fy + 13);
            s_cmd_try = 0;
            cmd("zone %d %d %d %d forestry", fx, fy, fx + 17, fy + 13);
        }
    }
    if (complex_gift_pending()) {                          /* the nearest free spot to the mill */
        int best = 1 << 30, bx = 0, by = 0, br = 0, r;
        for (y = c->y - 13; y <= c->y + 17; y++)
            for (x = c->x - 13; x <= c->x + 17; x++)
                for (r = 0; r < 2; r++) {
                    int d = IABS(x - c->x - 2) + IABS(y - c->y - 2);
                    if (d >= best || !world_in(x, y) || complex_check_gift(x, y, r) != PLACE_OK) continue;
                    best = d; bx = x; by = y; br = r;
                }
        if (best < (1 << 30) && cmd("complex gift %d %d %d", bx, by, br)) s_cpx_gifts++;
    }
}

/* M3: industry by kind, one row per year: tests/out/industry_<style>_<map>.txt */
static FILE *s_itab;
/* M5: services & housing, one row per year: tests/out/civic_<style>_<map>.txt (fires, crime, lakes, heat, elements,
   apartments by tier, and last month's budget lines so a money drain is visible) */
static FILE *s_ctab;
static void ctab_row(void)
{
    const FireStats *fs = civic_fire_stats();
    const CityStats *st = sim_stats();
    UtilSummary hs, ss;
    int k;
    if (!s_ctab) return;
    civic_util_summary(UT_HEAT, &hs); civic_util_summary(UT_SEWAGE, &ss);
    fprintf(s_ctab, "%d %6d %7d | fires %3d lost %3d out %3d rubble %3d | lake %3d untreated %5d treated %5d |"
                    " heat %4d/%4d lots %3d short %2d | elem %4d | apt %3d/%3d/%3d com3 %3d h3 %4d | svc P%d L%d T%d |",
            sim_date()->year, st->population, g_budget.money, fs->started, fs->lots_lost, fs->put_out, fs->rubble_tiles,
            civic_lake_avg(), ss.untreated, ss.used, hs.used, hs.supply, hs.lots_served, hs.lots_short,
            civic_elements_output(), st->lots_tier[ZONE_APARTMENTS][1], st->lots_tier[ZONE_APARTMENTS][2],
            st->lots_tier[ZONE_APARTMENTS][3], st->lots_tier[ZONE_COMMERCIAL][3], st->lots_tier[ZONE_HOUSES][3],
            st->svc_capacity[SVC_POLICE] / 3000, st->svc_capacity[SVC_LIBRARY] / 4000, st->svc_capacity[SVC_POST] / 3000);
    for (k = 0; k < BL_COUNT; k++) fprintf(s_ctab, " %d", g_budget.last[k]);
    fprintf(s_ctab, "\n");
}
static void itab_row(void)
{
    const IndKinds *K = industry_kinds();
    const IndWood *W = industry_wood();
    const IndMilk *Mk = industry_milk();
    if (!s_itab) return;
    fprintf(s_itab, "%d %6d | %3d %3d %3d %3d %3d %3d | %5d %5d %5d %5d %5d | %4d %4d %3d%% %4d %3d | %4d %4d %3d %2d/%2d/%2d | %5d %5d\n",
            sim_date()->year, sim_stats()->population,
            K->lots[IK_WOOD], K->lots[IK_METAL], K->lots[IK_TEXTILE], K->lots[IK_FOOD], K->lots[IK_ELEC], K->lots[IK_NONE],
            K->jobs[IK_WOOD], K->jobs[IK_METAL], K->jobs[IK_TEXTILE], K->jobs[IK_FOOD], K->jobs[IK_ELEC],
            W->need, W->supply, W->pct, W->cut_tiles_year, W->camps,
            Mk->need, Mk->supply, Mk->farm_lots, Mk->farm_tier[1], Mk->farm_tier[2], Mk->farm_tier[3],
            K->west_total, K->east_total);
}

/* ------------------------------------------------------------------ M1: politics bot (BS_POLITICS)
   A sensible President: answers every card when it arrives (scores the choices by bloc weights, the
   Eastern/Western balance, money and authority), keeps 2-4 decrees that fix the weakest bloc or meter,
   repeals money-costing decrees when the treasury is negative, writes a letter when one helps, and
   campaigns when popularity is middling. All through "pol ..." script commands (recorded, replayable). */
static FILE *s_ptl;               /* tests/out/politics_timeline_<map>.txt */
static int  s_pol_signed, s_pol_letters, s_pol_cards;

static int pol_choice_score(int ev, int ch)
{
    PolFx fx[POL_FX_MAX];
    int i, s = 0, east = politics_meter(METER_EAST), west = politics_meter(METER_WEST);
    politics_choice_fx(ev, ch, fx);
    for (i = 0; i < POL_FX_MAX; i++) {
        const PolFx *f = &fx[i];
        switch (f->kind) {
        case FX_BLOC:   s += f->value * politics_weight(f->target) / 100 * (f->months ? 1 : 2); break;
        case FX_METER:  s += f->value * ((f->target == METER_EAST ? east : west) < 55 ? 2 : 1) / 2; break;
        case FX_AUTH:   s += f->value; break;
        case FX_MONEY:  s += g_budget.money > 8000 ? f->value / 20 : f->value / 5; break;
        case FX_MONEY_POP: s += g_budget.money > 8000 ? f->value / 10 : f->value / 3; break;
        case FX_PM:     s += (f->target == PM_EMIGRATION_PCT || f->target == PM_UTILITY_PCT ? -f->value : f->value) / 4; break;
        case FX_ELECTION: s += politics_popularity() < 60 ? f->value : f->value / 3; break;
        default: break;
        }
    }
    return s;
}

static void pol_try_sign(int dec)
{
    if (politics_decree_state(dec) != DS_OK) return;
    if (politics_authority() < g_decrees[dec].cost + 2) return;
    if (cmd("pol sign %s", g_decrees[dec].key)) s_pol_signed++;
}

static void bot_politics(void)
{
    const CityStats *st = sim_stats();
    int ev = politics_pending_event(), k, best, bs, y = sim_date()->year, worst = -1, wa = 101;
    /* M7: town status comes by decree; the bot signs the town-rights petition as soon as it is offered */
    if (politics_decree_state(DEC_TOWN_RIGHTS) == DS_OK && cmd("pol sign townrights")) s_pol_signed++;
    /* event cards: answer at once */
    if (ev >= 0) {
        best = 0; bs = -1000;
        for (k = 0; k < g_events[ev].nch; k++) {
            int sc = pol_choice_score(ev, k);
            if (ev == EV_CAMPAIGN && k == 2) sc -= 4;              /* the rival trick costs the Intelligentsia */
            if (sc > bs) { bs = sc; best = k; }
        }
        if (cmd("pol choose %d", best)) s_pol_cards++;
    }
    /* money-costing decrees go when the treasury is empty */
    if (g_budget.money < 0)
        for (k = 0; k < DEC_COUNT; k++)
            if (politics_decree_active(k) && (k == DEC_REGIONAL || k == DEC_FRIENDSHIP || k == DEC_HOMECOMING || k == DEC_BONFIRE))
                { if (politics_repeal_state(k) == DS_OK) cmd("pol repeal %s", g_decrees[k].key); break; }
    /* decrees for the weakest bloc / meter (2-4 in force) */
    /* (the regional subsidy is a deliberate growth brake: this bot wants pacing, so it never signs it) */
    for (k = 0; k < BLOC_COUNT; k++) if (politics_weight(k) >= 50 && politics_approval(k) < wa) { wa = politics_approval(k); worst = k; }
    if (politics_active_count() < POL_SLOTS && politics_decree_gap() == 0 && sim_date()->total_months >= 6) {
        if (politics_meter(METER_EAST) < 40) pol_try_sign(DEC_FRIENDSHIP);
        else if (y >= 1961 && politics_meter(METER_WEST) < 40) pol_try_sign(DEC_WESTERN);
        else if (worst == BLOC_WORKERS && wa < 65) pol_try_sign(y >= 1965 ? DEC_FIVEDAY : DEC_RENT);
        else if (worst == BLOC_RURAL && wa < 65) pol_try_sign(DEC_BONFIRE);
        else if (worst == BLOC_INTELLIGENTSIA && wa < 65) pol_try_sign(y >= 1965 && g_budget.money > 10000 ? DEC_SCHOOL : DEC_SMOKESTACK);
        else if (worst == BLOC_INDUSTRIALISTS && wa < 60) {
            if (politics_decree_active(DEC_SMOKESTACK) && politics_repeal_state(DEC_SMOKESTACK) == DS_OK) cmd("pol repeal smokestack");
            else if (y >= 1961) pol_try_sign(DEC_WESTERN);
        }
        else if (y >= 1965 && st->emigrated_month * 1000 > st->population * 5) pol_try_sign(DEC_HOMECOMING);
    }
    /* letters */
    if (politics_letter_gap() == 0 && politics_authority() >= 12) {
        int building = st->lots_building;
        int let = building >= 10 ? LET_HOUSING : politics_meter(METER_EAST) < 45 ? LET_EMBASSY :
                  worst == BLOC_RURAL && wa < 55 ? LET_FARMERS : g_budget.money < 3000 ? LET_FINANCE :
                  sim_demand(DEM_I)->value < 0 ? LET_MINISTRY : -1;
        if (let >= 0 && cmd("pol letter %s", g_letters[let].key)) s_pol_letters++;
    }
}

static void pol_timeline_msgs(void)
{
    PolMsg m;
    char buf[200];
    while (politics_poll_msg(&m)) {
        int sev;
        if (!s_ptl || !politics_msg_text(buf, sizeof(buf), &m, &sev)) continue;
        if (m.type == PMSG_AUTH_FULL) continue;
        fprintf(s_ptl, "    %d-%02d  %s\n", sim_date()->year, sim_date()->month + 1, buf);
    }
}

static void pol_timeline_year(void)
{
    int b, d;
    if (!s_ptl) return;
    fprintf(s_ptl, "%d  pop %6d  popularity %3d  W %3d/%3d%% R %3d/%3d%% I %3d/%3d%% S %3d/%3d%%  auth %2d/%2d  east %3d west %3d  decrees:",
            sim_date()->year, sim_stats()->population, politics_popularity(),
            politics_approval(BLOC_WORKERS), politics_weight(BLOC_WORKERS) / 10, politics_approval(BLOC_RURAL), politics_weight(BLOC_RURAL) / 10,
            politics_approval(BLOC_INDUSTRIALISTS), politics_weight(BLOC_INDUSTRIALISTS) / 10,
            politics_approval(BLOC_INTELLIGENTSIA), politics_weight(BLOC_INTELLIGENTSIA) / 10,
            politics_authority(), politics_authority_cap(), politics_meter(METER_EAST), politics_meter(METER_WEST));
    for (d = 0; d < DEC_COUNT; d++) if (politics_decree_active(d)) fprintf(s_ptl, " %s", g_decrees[d].key);
    fprintf(s_ptl, "\n");
    UNUSED(b);
}

/* ------------------------------------------------------------------ runner */
static void bal_new_city(const BalMap *mp)
{
    MapGenParams p;
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    mapgen_defaults(&p);
    p.seed = mp->seed; p.preset = mp->preset;
    if (s_style == BS_MILL || s_style == BS_METAL) p.railway = 1;    /* M6: Valtion Metalli and the PM3 station need rail */
    mapgen_generate(&p);
    sim_default_settings(&s);
    s.seed = mp->seed;
    s.difficulty = s_level;            /* 1.1 D1 (replay: lahio.exe -difficulty LEVEL) */
    sim_new_city(&s);
    tools_reset();
    tools_script_reset();
}

/* traffic on the town's car roads (not the protected national road): average and max level, 128 =
   the class capacity at the era's car share. life.cpp spawns ~1.2 cars per tile at level 128 on top of a
   floor of 12, so the average is what makes 1955 vs 1965 vs 1975 streets look different. */
static void traffic_levels(int *avg10, int *mx)
{
    int x, y, n = 0;
    s32 sum = 0;
    *mx = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = WT(x, y);
            if (!TILE_ROAD(t) || (t->flags & TF_PROTECTED) || !(g_roads[TILE_ROAD(t)].flags & RDF_CARS)) continue;
            n++; sum += t->aux; if (t->aux > *mx) *mx = t->aux;
        }
    *avg10 = n ? (int)(sum * 10 / n) : 0;
}

static void take_row(BalRow *r, s32 opsum)
{
    const CityStats *st = sim_stats();
    SimProblem pr[1];
    memset(r, 0, sizeof(*r));
    r->pop = st->population; r->money = g_budget.money; r->net = budget_net_last(); r->opnet = opsum / 12;
    r->demR = sim_demand(DEM_R)->value; r->demC = sim_demand(DEM_C)->value; r->demI = sim_demand(DEM_I)->value;
    r->jobs = st->jobs; r->unemp = st->unemployed; r->homeless = st->homeless;
    r->h1 = st->lots_tier[ZONE_HOUSES][1]; r->h2 = st->lots_tier[ZONE_HOUSES][2]; r->h3 = st->lots_tier[ZONE_HOUSES][3];
    r->apt = st->lots_tier[ZONE_APARTMENTS][1] + st->lots_tier[ZONE_APARTMENTS][2] + st->lots_tier[ZONE_APARTMENTS][3];
    r->c1 = st->lots_tier[ZONE_COMMERCIAL][1]; r->c2 = st->lots_tier[ZONE_COMMERCIAL][2] + st->lots_tier[ZONE_COMMERCIAL][3];
    r->i1 = st->lots_tier[ZONE_INDUSTRY][1]; r->i2 = st->lots_tier[ZONE_INDUSTRY][2] + st->lots_tier[ZONE_INDUSTRY][3];
    r->aband = st->lots_abandoned; r->status = st->town_status; traffic_levels(&r->tavg10, &r->tmax);
    if (sim_top_problems(pr, 1)) { r->preq = pr[0].req; r->pres = pr[0].count; } else r->preq = -1;
}

static const char *req_short(int req)
{
    static const char *n[REQ_COUNT] = { "road", "power", "water", "tower", "elements", "school", "sauna", "health",
                                        "shop", "phone", "landval", "pollution", "customers", "workers", "mainroad", "heat",
                                        "wood", "milk", "route", "library", "hcentre" };   /* 1.1 D1: + library; B1: + health centre */
    return req >= 0 && req < REQ_COUNT ? n[req] : "-";
}

/* run state between bal_begin / bal_months / bal_end (one run at a time) */
static int s_month_no, s_bot_off;
static int s_agr_desk, s_agr_auto;      /* PL2: clearing agreements signed by the President / by the Ministry (era messages) */
static int s_prev_pend = -1, s_cards_y, s_campaigns_y;   /* P1: card counter (era tables) */
static s32 s_opsum, s_prev_pop, s_emig;
static Advice s_adv_tick;              /* 1.1 B1: the Cabinet's top line at the month tick, before the bot acts (what the
                                          player reads that month; score 0 = none) */

static void bal_begin(int style, int map, BalRun *out, int record)
{
    char path[96];
    int steps, k;
    {
        const char *j = getenv("LAHIO_B1_JITTER");
        s_jitter = j && s_level != DIFF_EASY ? atoi(j) : 0;
    }
    memset(out, 0, sizeof(*out));
    out->style = style; out->map = map;
    for (k = 0; k < TS_COUNT; k++) out->status_month[k] = -1;
    M = &s_maps[map];
    s_style = style; s_mods = 0; s_ind1 = 0; s_plants = 0; s_tower = 0; s_errors = 0; s_cmds = 0; s_rec_wait = 0;
    s_month_no = 0; s_bot_off = 0; s_agr_desk = s_agr_auto = 0; s_opsum = 0; s_prev_pop = 0; s_emig = 0; s_rural_done = 0; s_factory = 0;
    s_cpx_done = s_cpx_station = s_cpx_gifts = s_cpx_forest2 = 0;                    /* M6 */
    s_ctab2 = NULL;
    if (record == 1 && (style == BS_MILL || style == BS_METAL)) {
        str_printf(path, sizeof(path), "out/complex_%s_%s.txt", s_style_name[style], s_maps[map].key);
        s_ctab2 = fopen(path, "w");
        if (s_ctab2) fprintf(s_ctab2, "M6 complex bot: style %s, map %s (seed %u); January rows (tests/test_balance.cpp)\n",
                             s_style_name[style], s_maps[map].key, s_maps[map].seed);
    }
    memset(s_used, 0, sizeof(s_used)); memset(s_com_big, 0, sizeof(s_com_big));
    bal_new_city(M);
    s_rec = NULL;
    s_rec_tag = record == 2 ? "_era" : "";      /* P1: record 2 = only the script, as bal_<style>_<map>_era.txt */
    if (record == 2 && s_level != DIFF_EASY) {  /* 1.1 D1: bal_<style>_<map>_era_normal.txt (replay with -difficulty normal) */
        static char tag[24];
        str_printf(tag, sizeof(tag), "_era_%s", s_level == DIFF_HARD ? "hard" : "normal");
        s_rec_tag = tag;
    }
    if (record) {
        str_printf(path, sizeof(path), "out/bal_%s_%s%s.txt", s_style_name[style], M->key, s_rec_tag);
        s_rec = fopen(path, "w");
    }
    if (s_rec) fprintf(s_rec, "# bal_%s_%s%s.txt - T12 balance bot run, recorded by tests/test_balance.cpp (docs/BALANCE.md).\n"
                              "#   VM: lahio.exe -seed %u -preset %s%s -script scripts\\bal_%s_%s%s.txt -fast   (same city as the Mac run)\n"
                              "statlog bal_%s_%s%s.csv\n",
                       s_style_name[style], M->key, s_rec_tag, M->seed, mapgen_preset_name(M->preset),
                       s_level == DIFF_HARD ? " -difficulty hard" : s_level == DIFF_NORMAL ? " -difficulty normal" : "",   /* 1.1 D1 */
                       s_style_name[style], M->key, s_rec_tag, s_style_name[style], M->key, s_rec_tag);
    if (style == BS_NAIVE) {            /* PL2: the New Game default */
        cmd("pol enable"); s_nv_power = s_nv_water = s_nv_rights = s_nv_card = s_nv_com = s_nv_broke = 0; s_nv_ot = 0; s_nv_ot_svc = -1; s_nv_hc = 0;
        memset(s_nv_full, 0, sizeof(s_nv_full)); memset(s_nv_hole, 0, sizeof(s_nv_hole));
    }
    if (style == BS_POLITICS) {         /* M1: politics on from the first month (replays with plain -seed) */
        cmd("pol enable");
        s_pol_signed = s_pol_letters = s_pol_cards = 0;
        str_printf(path, sizeof(path), "out/politics_timeline_%s.txt", M->key);
        s_ptl = record == 1 ? fopen(path, "w") : NULL;
        if (s_ptl) fprintf(s_ptl, "M1 politics timeline: politics bot on map %s (seed %u %s), 1955-1975 (tests/test_balance.cpp)\n"
                                  "yearly rows: approval/weight per bloc (Workers Rural Industrialists Intelligentsia); indented: ticker lines\n",
                           M->key, M->seed, mapgen_preset_name(M->preset));
    }
    s_itab = NULL;
    s_ctab = NULL;
    if (record == 1) {      /* M5 */
        str_printf(path, sizeof(path), "out/civic_%s_%s.txt", s_style_name[style], M->key);
        s_ctab = fopen(path, "w");
        if (s_ctab) {
            fprintf(s_ctab, "M5 services & housing: style %s, map %s; January rows (tests/test_balance.cpp). Budget lines:", s_style_name[style], M->key);
            for (k = 0; k < BL_COUNT; k++) fprintf(s_ctab, " %d=%s", k, text_term_label(TERMK_BUDGET, k));
            fprintf(s_ctab, "\n");
        }
    }
    if (record == 1) {      /* M3: industry by kind table */
        str_printf(path, sizeof(path), "out/industry_%s_%s.txt", s_style_name[style], M->key);
        s_itab = fopen(path, "w");
        if (s_itab) fprintf(s_itab, "M3 industry by kind: style %s, map %s (seed %u %s); January rows (tests/test_balance.cpp)\n"
                                    "year    pop |  lots: saw eng tex dai ele yrd |  jobs: saw   eng   tex dairy  elec | wood need sup pct cut/y camps"
                                    " | milk need sup farms T1/T2/T3 | export West  East\n",
                            s_style_name[style], M->key, M->seed, mapgen_preset_name(M->preset));
    }
    if (style == BS_TAX13) cmd("tax 13 13");
    if (style == BS_TAX5) cmd("tax 5 5");
    cmd("townhall");
    /* first neighbourhood + the first industrial band + a plant, like the first minutes of play */
    build_module(0);
    build_estate(0);
    if (style != BS_NAIVE) build_plant();          /* PL2: the naive player waits for the checklist to ask for power */
    cmd("power %d %d %d %d", M->conn[0], M->conn[1], M->conn[2], M->conn[3]);
    if (s_rec) fprintf(s_rec, "speed 3\n");
    /* first houses: steps until a lot grows (speed 2 = 6 steps/s: 60 steps = 10 s) */
    steps = 0;
    while (sim_stats()->lots_zone == 0 && steps < 4 * SIM_STEPS_PER_MONTH) { sim_step(); steps++; }
    out->first_lot_steps = steps;
    /* finish the partial month so the bot sees whole months (these steps + the 1-step books of the
       first bal_months month = exactly the replay's first "wait 1") */
    while (sim_date()->step != 0 || sim_date()->cycle != 0) sim_step();
    take_row(&out->row[0], 0);
    out->min_money = g_budget.money;
}

static void bal_months(BalRun *out, int n)
{
    while (n-- > 0) {
        SimEvent ev;
        s32 con;
        int months = ++s_month_no;
        sim_run_months(1);
        s_rec_wait++;
        {   /* P1: cards that went up this month (the politics bot answers at once, so a pending card is new) */
            int pe = politics_pending_event();
            if (pe >= 0 && pe != s_prev_pend) { if (pe == EV_CAMPAIGN) s_campaigns_y++; else s_cards_y++; }
        }
        while (sim_poll_event(&ev)) {
            if (ev.type == SE_STATUS_UP && ev.a < TS_COUNT && out->status_month[ev.a] < 0) out->status_month[ev.a] = months;
            if (ev.type == SE_BANKRUPT_WARN && ev.b == 1) out->loans++;
        }
        {   /* PL2: from the stats too (a busy growth month can overflow the event queue: politics r30 missed Town) */
            int ts;
            for (ts = 1; ts <= sim_stats()->town_status && ts < TS_COUNT; ts++) if (out->status_month[ts] < 0) out->status_month[ts] = months;
            if (g_budget.loans_taken > out->loans) out->loans = g_budget.loans_taken;
        }
        tools_month_changed();
        {
            EraMsg em;
            while (era_poll_msg(&em)) { if (em.type == EM_SIGNED) s_agr_desk++; else if (em.type == EM_AUTO_SIGNED) s_agr_auto++; }
        }
        s_emig += sim_stats()->emigrated_month;
        con = g_budget.last[BL_CONSTRUCTION] + g_budget.last[BL_TIMBER];
        s_opsum += budget_net_last() - con - (g_budget.last[BL_GRANTS] > 1000 ? g_budget.last[BL_GRANTS] : 0);
        if (g_budget.money < out->min_money) out->min_money = g_budget.money;
        if (months % 12 == 0 && months / 12 <= 20) {
            int y = months / 12;
            take_row(&out->row[y], s_opsum);
            out->row[y].emig = s_emig; s_emig = 0;
            out->opnet_sum[y] = s_opsum;
            if (y == 2) out->money_y2 = g_budget.money;
            if (s_prev_pop > 500 && out->row[y].pop < s_prev_pop) {
                int d = (int)((s64)(s_prev_pop - out->row[y].pop) * 100 / s_prev_pop);
                if (d > out->max_drop_pct) out->max_drop_pct = d;
            }
            s_prev_pop = out->row[y].pop;
            s_opsum = 0;
            itab_row();
            ctab_row();
            out->nrows = y + 1;
        }
        if (advice_collect(&s_adv_tick, 1) <= 0) s_adv_tick.score = 0;   /* 1.1 B1: before the bot answers it */
        if (!s_bot_off && s_jitter && (months + s_jitter) % 7 == 0) {}   /* B1 tuning (LAHIO_B1_JITTER): skip a decision round */
        else if (!s_bot_off) { if (s_style == BS_NAIVE) bot_naive(); else { bot_month(); bot_budget(); } }   /* 1.1 D1: bot_budget */
        if ((s_style == BS_RURAL || s_style == BS_MILL) && !s_bot_off) bot_rural();
        if ((s_style == BS_MILL || s_style == BS_METAL) && !s_bot_off) bot_complex();   /* M6 */
        if (months % 12 == 0 && months / 12 <= 20) cpx_row();                          /* M6 */
        cpx_track();
        if (s_style == BS_POLITICS) {
            pol_timeline_msgs();
            if (months % 12 == 0) pol_timeline_year();
            if (months >= 12) {
                int pp = politics_popularity();
                if (!out->pol_pop_min || pp < out->pol_pop_min) out->pol_pop_min = pp;
                if (pp > out->pol_pop_max) out->pol_pop_max = pp;
            }
            if (politics_last_election() && politics_elections_held() > 0 && politics_elections_held() <= 4)
                out->pol_electors[politics_elections_held() - 1] = politics_last_election()->electors;
            if (!s_bot_off) bot_politics();
            pol_timeline_msgs();
        }
        s_prev_pend = politics_pending_event();
    }
}

/* M6: what the complex bots achieved (balance_complex_bots) */
typedef struct { int built, stage_max, jobs_max, dep_max, gifts, contrib_sum, vol_sum, east_sum, stage_year[4]; } CpxRun;
static CpxRun s_cpx_run[2];

static void cpx_track(void)
{
    const Complex *c;
    CpxRun *r;
    if (s_style != BS_MILL && s_style != BS_METAL) return;
    r = &s_cpx_run[s_style == BS_METAL];
    c = complex_of_kind(s_style == BS_MILL ? CPX_PAPER : CPX_METAL);
    if (!c) return;
    r->built = 1;
    if (c->stage > r->stage_max) { r->stage_max = c->stage; if (c->stage < 4) r->stage_year[c->stage] = sim_date()->year; }
    r->jobs_max = IMAX(r->jobs_max, c->jobs);
    r->dep_max = IMAX(r->dep_max, complex_dependency_pct());
    r->gifts = s_cpx_gifts;
    r->contrib_sum += complex_contribution_last();
    r->vol_sum += c->vol; r->east_sum += c->east;
}

static void bal_end(BalRun *out)
{
    int i;
    rec_flush_wait();
    if (s_ctab2) {                    /* M6: a save of the end state for the VM screenshots (camera on the complex) */
        const Complex *c = complex_of_kind(out->style == BS_MILL ? CPX_PAPER : CPX_METAL);
        SaveMeta sm;
        memset(&sm, 0, sizeof(sm));
        if (c) { sm.cam_x = c->x + 2; sm.cam_y = c->y + 2; sm.zoom = ZOOM_64; }
        save_game(out->style == BS_MILL ? "out/M6_MILL.KAU" : "out/M6_METAL.KAU", &sm);
    }
    if (s_ctab2) { fprintf(s_ctab2, "end: "); complex_dump(s_ctab2); world_dump(s_ctab2, 0, 0, g_world.w - 1, g_world.h - 1); fclose(s_ctab2); s_ctab2 = NULL; }
    if (s_rec) { fprintf(s_rec, "dump report bal_%s_%s%s_report.txt\n", s_style_name[out->style], M->key, s_rec_tag); fclose(s_rec); s_rec = NULL; }
    out->errors = s_errors; out->cmds = s_cmds;
    if (s_itab) {
        fprintf(s_itab, "end: "); industry_dump(s_itab);
        if (out->style == BS_RURAL) world_dump(s_itab, 0, 0, g_world.w - 1, g_world.h - 1);   /* M3: where it all grew */
        fclose(s_itab); s_itab = NULL;
    }
    if (s_ctab) { civic_dump(s_ctab); sim_dump_report(s_ctab); fclose(s_ctab); s_ctab = NULL; }
    for (i = 0; i < IK_COUNT; i++) out->ind_lots[i] = industry_kinds()->lots[i];
    out->camps = industry_wood()->camps; out->farms = industry_milk()->farm_lots; out->farms_t3 = industry_milk()->farm_tier[3];
    out->wood_cut = industry_wood()->cut_tiles_total; out->farm_res = industry_milk()->farm_residents;
    out->hash = sim_hash();
    if (out->style == BS_POLITICS) {      /* M1 */
        out->pol_signed = s_pol_signed; out->pol_letters = s_pol_letters; out->pol_cards = s_pol_cards;
        out->pol_elections = politics_elections_held();
        if (s_ptl) {
            fprintf(s_ptl, "end: %d decrees signed, %d letters, %d cards answered, %d elections, pop %d, money %d, hash %08x\n",
                    s_pol_signed, s_pol_letters, s_pol_cards, out->pol_elections, sim_stats()->population, g_budget.money, out->hash);
            politics_dump(s_ptl);
            fclose(s_ptl);
            s_ptl = NULL;
        }
    }
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_NONE) continue;
        if (l->def == B_SCHOOL) out->n_school++;
        else if (l->def == B_SAUNA) out->n_sauna++;
        else if (l->def == B_CLINIC) out->n_clinic++;
        else if (l->def == B_FIRE_STATION) out->n_fire++;
        else if (l->def == B_COAL_PLANT) out->n_plant++;
        else if (l->def == B_WELL) out->n_well++;
    }
}

static void bal_run(int style, int map, BalRun *out)
{
    bal_begin(style, map, out, 1);
    bal_months(out, 240);
    bal_end(out);
}

static void bal_write(const BalRun *r)
{
    char path[96];
    FILE *f;
    int k;
    str_printf(path, sizeof(path), "out/balance_%s_%s.txt", s_style_name[r->style], s_maps[r->map].key);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "T12 balance run: style %s, map %s (seed %u %s), 1955-1975, Normal funds\n", s_style_name[r->style],
            s_maps[r->map].key, s_maps[r->map].seed, mapgen_preset_name(s_maps[r->map].preset));
    fprintf(f, "first lot %d steps after zoning (%.1f s at speed 2); status: church village month %d, market town %d, town %d;"
               " min money %d, emergency loans %d, worst yearly pop drop %d%%, %d commands (%d failed)\n",
            r->first_lot_steps, r->first_lot_steps / 6.0, r->status_month[TS_CHURCH_VILLAGE], r->status_month[TS_MARKET_TOWN],
            r->status_month[TS_TOWN], r->min_money, r->loans, r->max_drop_pct, r->cmds, r->errors);
    fprintf(f, "end: %d schools, %d saunas, %d clinics, %d fire stations (residents per building %d / %d / %d / %d), %d coal plants, %d wells\n",
            r->n_school, r->n_sauna, r->n_clinic, r->n_fire, r->row[r->nrows - 1].pop / IMAX(1, r->n_school),
            r->row[r->nrows - 1].pop / IMAX(1, r->n_sauna), r->row[r->nrows - 1].pop / IMAX(1, r->n_clinic),
            r->row[r->nrows - 1].pop / IMAX(1, r->n_fire), r->n_plant, r->n_well);
    fprintf(f, "year    pop   money opnet/m  R   C   I   jobs unemp emig hmls  h1/h2/h3   apt  c1/c2  i1/i2 ab st traffic top problem\n");
    for (k = 0; k < r->nrows; k++) {
        const BalRow *w = &r->row[k];
        fprintf(f, "%d %6d %7d %+6d %4d %3d %3d %6d %5d %4d %4d %4d/%3d/%3d %4d %3d/%-3d %3d/%-3d %2d %2d %3d.%d/%-3d %s(%d)\n",
                1955 + k, w->pop, w->money, w->opnet, w->demR, w->demC, w->demI, w->jobs, w->unemp,
                w->emig, w->homeless, w->h1, w->h2, w->h3, w->apt, w->c1, w->c2, w->i1, w->i2, w->aband,
                w->status, w->tavg10 / 10, w->tavg10 % 10, w->tmax, req_short(w->preq), w->pres);
    }
    fclose(f);
}

/* ------------------------------------------------------------------ tests */
static BalRun s_run[BS_COUNT][NMAPS];
static int s_ran[BS_COUNT][NMAPS];

static const BalRun *get_run(int style, int map)
{
    if (!s_ran[style][map]) { bal_run(style, map, &s_run[style][map]); bal_write(&s_run[style][map]); s_ran[style][map] = 1; }
    return &s_run[style][map];
}

TEST(balance_runs_and_summary)
{
    FILE *f = fopen("out/balance_summary.txt", "w");
    int st, mp;
    for (st = 0; st < BS_COUNT; st++)
        for (mp = 0; mp < NMAPS; mp++) {
            const BalRun *r;
            if ((st == BS_TAX13 || st == BS_TAX5 || st == BS_MILL || st == BS_METAL) && mp != 0) continue;   /* M6: s7 only */
            r = get_run(st, mp);
            test_log("%-8s %-3s: pop y1 %5d y3 %5d y5 %5d y10 %5d y20 %5d | 500 @%dm 3k @%dm 10k @%dm | money y2 %6d end %7d"
                     " min %6d loans %d | opnet y10 %+5d y20 %+5d | drop %d%% fail %d",
                     s_style_name[st], s_maps[mp].key, r->row[1].pop, r->row[3].pop, r->row[5].pop, r->row[10].pop,
                     r->row[20].pop, r->status_month[1], r->status_month[2], r->status_month[3], r->money_y2,
                     r->row[20].money, r->min_money, r->loans, r->row[10].opnet, r->row[20].opnet, r->max_drop_pct, r->errors);
            if (f) fprintf(f, "%-8s %-3s: pop y1 %5d y3 %5d y5 %5d y10 %5d y20 %5d | 500 @%dm 3k @%dm 10k @%dm | money y2 %6d"
                              " end %7d min %6d loans %d | opnet y10 %+5d y20 %+5d | drop %d%%\n",
                           s_style_name[st], s_maps[mp].key, r->row[1].pop, r->row[3].pop, r->row[5].pop, r->row[10].pop,
                           r->row[20].pop, r->status_month[1], r->status_month[2], r->status_month[3], r->money_y2,
                           r->row[20].money, r->min_money, r->loans, r->row[10].opnet, r->row[20].opnet, r->max_drop_pct);
            {   /* F3: share of 1x1 yards among the industrial lots (lots[IK_NONE] = yards) */
                int k, ind = 0;
                for (k = 0; k < IK_COUNT; k++) ind += r->ind_lots[k];
                test_log("%-8s %-3s: industrial lots %d, yards %d (%d%%)", s_style_name[st], s_maps[mp].key, ind,
                         r->ind_lots[IK_NONE], ind ? r->ind_lots[IK_NONE] * 100 / ind : 0);
            }
        }
    if (f) fclose(f);
    CHECK(1);
}

/* pacing and money milestones of the careful planner on all three maps (DESIGN 7, T12 goals 1-3) */
TEST(balance_planner_pacing)
{
    int mp;
    for (mp = 0; mp < NMAPS; mp++) {
        const BalRun *r = get_run(BS_PLANNER, mp);
        test_log("%s: first lot %d steps, church village m%d, market town m%d, town m%d, money y2 %d min %d end %d,"
                 " opnet y10 %+d y20 %+d, loans %d, worst drop %d%%", s_maps[mp].key, r->first_lot_steps,
                 r->status_month[TS_CHURCH_VILLAGE], r->status_month[TS_MARKET_TOWN], r->status_month[TS_TOWN],
                 r->money_y2, r->min_money, r->row[20].money, r->row[10].opnet, r->row[20].opnet, r->loans, r->max_drop_pct);
        CHECK(r->first_lot_steps <= 60);                                        /* first houses within ~10 s at speed 2 */
        CHECK(r->status_month[TS_CHURCH_VILLAGE] > 0 && r->status_month[TS_CHURCH_VILLAGE] <= 15);   /* ~1 year */
        CHECK(r->status_month[TS_MARKET_TOWN] >= 24 && r->status_month[TS_MARKET_TOWN] <= 60);      /* 3-4 years (2-5) */
        CHECK(r->status_month[TS_TOWN] >= 72 && r->status_month[TS_TOWN] <= 160);                   /* 8-12 years (6-13) */
        CHECK(r->money_y2 > 0);                                                 /* money lasts past year 2 */
        CHECK(r->min_money > -5000 && r->loans <= 2);                           /* dips, no debt spiral */
        CHECK(r->row[20].money < 250000);                                       /* no absurd pile */
        CHECK(r->row[20].opnet > -600 && r->row[20].opnet < 1500);              /* built-out town: tight books */
        CHECK(r->max_drop_pct <= 10);                                           /* no oscillation / spiral */
        CHECK(r->row[20].h3 > r->row[20].h1);                                   /* services -> tier 3 */
    }
}

/* no services: tier 1 only, slower, and not a money machine (goal 3); every lot names the missing school */
TEST(balance_greedy_not_profitable)
{
    int mp;
    for (mp = 0; mp < NMAPS; mp++) {
        const BalRun *g = get_run(BS_GREEDY, mp), *p = get_run(BS_PLANNER, mp);
        test_log("%s: greedy pop y10 %d (planner %d), opnet y10 %+d y20 %+d (planner %+d %+d), top problem %s",
                 s_maps[mp].key, g->row[10].pop, p->row[10].pop, g->row[10].opnet, g->row[20].opnet, p->row[10].opnet,
                 p->row[20].opnet, req_short(g->row[20].preq));
        CHECK(g->row[20].h2 + g->row[20].h3 == 0);
        CHECK(g->row[10].pop * 10 < p->row[10].pop * 7);
        CHECK(g->row[10].opnet < 250 && g->row[20].opnet < 250);
        CHECK(g->row[20].opnet < p->row[20].opnet + 100);
        CHECK(g->row[20].preq == REQ_SCHOOL || g->row[20].preq == REQ_SAUNA);
    }
}

/* taxes above neutral visibly slow growth, below it speeds growth but empties the treasury (goal 3) */
TEST(balance_tax_pressure)
{
    const BalRun *p = get_run(BS_PLANNER, 0), *hi = get_run(BS_TAX13, 0), *lo = get_run(BS_TAX5, 0);
    test_log("pop y5/y10: 13%% %d/%d, 9%% %d/%d, 5%% %d/%d; min money 5%% %d", hi->row[5].pop, hi->row[10].pop,
             p->row[5].pop, p->row[10].pop, lo->row[5].pop, lo->row[10].pop, lo->min_money);
    CHECK(hi->row[10].pop * 10 < p->row[10].pop * 8);
    CHECK(lo->row[5].pop > p->row[5].pop);
    CHECK(lo->min_money < 0);
}

/* the other styles stay sane: no collapse, the emigration valve instead of riots, apartments get built */
TEST(balance_styles_sane)
{
    int st, mp, apts = 0;
    for (st = BS_INDUSTRY; st <= BS_DENSE; st++)
        for (mp = 0; mp < NMAPS; mp++) {
            const BalRun *r = get_run(st, mp);
            CHECK(r->row[20].pop > 5000);
            CHECK(r->max_drop_pct <= 10);
            CHECK(r->loans <= 3);
            if (st == BS_DENSE) apts += r->row[20].apt;
        }
    test_log("apartment lots in the dense runs: %d", apts);
    CHECK(apts > 0);
}

/* readable failure and recovery (goal 4): a job shock sends people to Sweden instead of a death spiral and
   the town recovers when industry is zoned again; a power cut downgrades tiers, rebuilding restores them */
TEST(balance_failure_and_recovery)
{
    BalRun r;
    int x = s_maps[0].ind[0], y = s_maps[0].ind[1], w = s_maps[0].ind[2], h = s_maps[0].ind[3], i;
    s32 pop0, jobs0, emig0, pop_low, t3_0, t3_low;
    int nplants = 0;
    bal_begin(BS_PLANNER, 0, &r, 0);
    bal_months(&r, 12 * 12);                              /* 1967: built-out planner town */
    s_bot_off = 1;
    pop0 = sim_stats()->population; jobs0 = sim_stats()->jobs; emig0 = sim_stats()->emigrated_total;
    if (g_budget.money < 20000) g_budget.money = 20000;  /* bulldozing costs money; this test is about the sim's response */
    /* 1) the industrial estate closes (bulldoze every industry lot, keep the plants) */
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def != B_NONE && def_building(l->def)->zone == ZONE_INDUSTRY) cmd("bulldoze %d %d %d %d", l->x, l->y, l->x, l->y);
    }
    cmd("dezone %d %d %d %d", x + 1, y + 1, x + w - 1, y + h - 1);
    bal_months(&r, 24);
    pop_low = sim_stats()->population;
    test_log("job shock: pop %d -> %d, jobs %d -> %d, emigrated +%d, unemployed %d, abandoned %d", pop0, pop_low, jobs0,
             sim_stats()->jobs, sim_stats()->emigrated_total - emig0, sim_stats()->unemployed, sim_stats()->lots_abandoned);
    CHECK(sim_stats()->emigrated_total - emig0 > pop0 / 20);                /* the valve is visible */
    CHECK(pop_low < pop0 && pop_low * 10 > pop0 * 3);   /* hurt (most jobs gone), not destroyed: equilibrium ~2 x jobs + pull */
    if (g_budget.money < 20000) g_budget.money = 20000;
    build_estate(1);                                                          /* re-zone the estate */
    bal_months(&r, 48);
    s_bot_off = 1;
    test_log("after re-zoning: pop %d, jobs %d, unemployed %d", sim_stats()->population, sim_stats()->jobs, sim_stats()->unemployed);
    /* M2: 90% -> 85%. With traffic's commute map the re-zoned town settles job-limited (pop ~ 2 x jobs, ~70
       unemployed) at 85-93% of the pre-shock population after 4 years (10,055 of 11,765 in the M2 run) */
    CHECK(sim_stats()->population * 100 > pop0 * 85);
    /* 2) power cut: all plants gone for a year */
    pop0 = sim_stats()->population; t3_0 = sim_stats()->lots_tier[ZONE_HOUSES][3];
    if (g_budget.money < 20000) g_budget.money = 20000;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_COAL_PLANT) { cmd("bulldoze %d %d %d %d", l->x, l->y, l->x + 1, l->y + 1); nplants++; }
    }
    bal_months(&r, 12);
    t3_low = sim_stats()->lots_tier[ZONE_HOUSES][3];
    test_log("power cut: pop %d -> %d, tier-3 houses %d -> %d, abandoned %d", pop0, sim_stats()->population, t3_0, t3_low,
             sim_stats()->lots_abandoned);
    CHECK(t3_low < t3_0 / 2);                                                 /* tiers fall without power */
    s_plants = 0;
    if (g_budget.money < 20000) g_budget.money = 20000;  /* (the outage emptied the treasury) */
    for (i = 0; i < nplants + 2; i++) build_plant();
    s_bot_off = 0;                                                            /* the player is back */
    for (i = 0; i < 6; i++) {
        SimProblem pr[2];
        int np;
        bal_months(&r, 6);
        np = sim_top_problems(pr, 2);
        test_log("  +%2d months: money %d, pop %d, houses %d/%d/%d, R target %d smoothed %d, power %d/%d short %d, top %s(%d) %s(%d)",
                 6 * (i + 1), g_budget.money, sim_stats()->population, sim_stats()->lots_tier[ZONE_HOUSES][1], sim_stats()->lots_tier[ZONE_HOUSES][2],
                 sim_stats()->lots_tier[ZONE_HOUSES][3], sim_demand(DEM_R)->target, sim_demand(DEM_R)->smoothed,
                 sim_stats()->power_demand, sim_stats()->power_supply, sim_stats()->power_short_lots,
                 np > 0 ? req_short(pr[0].req) : "-", np > 0 ? pr[0].count : 0, np > 1 ? req_short(pr[1].req) : "-", np > 1 ? pr[1].count : 0);
    }
    test_log("rebuilt: pop %d, tier-3 houses %d, abandoned %d", sim_stats()->population,
             sim_stats()->lots_tier[ZONE_HOUSES][3], sim_stats()->lots_abandoned);
    /* F1: 90% -> 85%. The town now recovers to the same ~10.2k as before (shops limit it in both versions),
       but zoned land conducting power lets it peak higher before the cut (11.6k vs 10.7k): 88% of the peak. */
    CHECK(sim_stats()->population * 100 > pop0 * 85);
    CHECK(sim_stats()->lots_tier[ZONE_HOUSES][3] * 10 > t3_0 * 6);   /* F1: 8 -> 7 (1,104 of 1,448; was 1,124 of 1,214);
                                       M2: 7 -> 6 (942 of 1,427): the town re-settles job-limited (R target -650, top problem
                                       "shop"), so fewer houses can climb back to tier 3 within the 3 years */
    bal_end(&r);
}

/* same bot, same city: determinism of the whole 20-year session */
TEST(balance_deterministic)
{
    BalRun r;
    const BalRun *a = get_run(BS_PLANNER, 1);
    bal_begin(BS_PLANNER, 1, &r, 0);
    bal_months(&r, 240);
    bal_end(&r);
    CHECK_EQ(r.hash, a->hash);
    CHECK_EQ(r.row[20].pop, a->row[20].pop);
}

/* the first ten minutes as the user described them (goal 1): one street off the national road, a block
   of houses, then a plant, wells, a school and a sauna. First houses within ~10 s at speed 2, the plant and
   water affordable right away, money lasting two years without piling up. */
static int bulldoze_build(const char *key, int x, int y, int sz)
{
    int i, j, occ = 0;
    for (j = y; j < y + sz; j++) for (i = x; i < x + sz; i++) if (world_lot_at(i, j)) occ = 1;
    if (occ) cmd("bulldoze %d %d %d %d", x, y, x + sz - 1, y + sz - 1);
    return cmd("build %s %d %d", key, x, y);
}

TEST(balance_first_minutes)
{
    int steps = 0, m;
    Money after_zoning;
    s32 pop3, pop12;
    M = &s_maps[0];
    s_rec = NULL; s_errors = 0;
    bal_new_city(M);
    cmd("townhall");
    cmd("road 101 70 80 70 street");                  /* west from the national road at the town site */
    cmd("zone 81 67 95 73 houses");
    cmd("zone 96 67 100 73 commercial");              /* a corner shop or two */
    cmd("road 103 74 112 74 street");                 /* workshops across the national road */
    cmd("zone 104 71 112 77 industry");
    after_zoning = g_budget.money;
    while (sim_stats()->lots_zone == 0 && steps < 4 * SIM_STEPS_PER_MONTH) { sim_step(); steps++; }
    sim_run_months(1);
    test_log("first lot after %d steps (%.1f s at speed 2), money after the street + zone %d", steps, steps / 6.0, after_zoning);
    CHECK(steps <= 60);
    CHECK(after_zoning >= g_bdefs[B_COAL_PLANT].cost + g_bdefs[B_PUMP].cost + g_bdefs[B_SCHOOL].cost);
    /* month 2: power and water like a player who read the checklist */
    cmd("build coal_plant 90 64");
    cmd("power 90 66 90 69");
    bulldoze_build("well", 80, 71, 1);
    bulldoze_build("well", 100, 69, 1);
    sim_run_months(1);
    pop3 = sim_stats()->population;
    bulldoze_build("school", 84, 68, 2);
    bulldoze_build("sauna", 95, 71, 1);
    for (m = 0; m < 9; m++) { sim_run_months(1); tools_month_changed(); }
    pop12 = sim_stats()->population;
    /* year 2: a second street and more houses, a commercial corner */
    cmd("road 80 77 101 77 street");
    cmd("road 80 70 80 77 street");
    cmd("zone 81 74 100 76 houses");
    cmd("zone 81 78 100 80 houses");
    for (m = 0; m < 12; m++) { sim_run_months(1); tools_month_changed(); }
    test_log("pop month 3 %d, month 12 %d, month 24 %d; money month 24 %d (net %+d), status %d, %d failed commands",
             pop3, pop12, sim_stats()->population, g_budget.money, budget_net_last(), sim_stats()->town_status, s_errors);
    CHECK(s_errors == 0);
    CHECK(pop3 > 50);
    CHECK(pop12 >= 300 && pop12 <= 1500);
    CHECK(g_budget.money > 2000 && g_budget.money < 40000);
}

/* M1: the politics bot keeps the planner's pacing (politics is a layer on top, not a brake), uses the
   system (decrees, letters, cards) and never loses an election. Timeline: tests/out/politics_timeline_<map>.txt */
TEST(balance_politics_bot)
{
    int mp;
    for (mp = 0; mp < NMAPS; mp++) {
        const BalRun *r = get_run(BS_POLITICS, mp), *p = get_run(BS_PLANNER, mp);
        test_log("%s: politics pop y10 %d y20 %d (planner %d %d), town m%d (planner m%d), money y20 %d (planner %d); %d decrees,"
                 " %d letters, %d cards, elections %d (%d/%d/%d electors), popularity %d..%d", s_maps[mp].key,
                 r->row[10].pop, r->row[20].pop, p->row[10].pop, p->row[20].pop, r->status_month[TS_TOWN], p->status_month[TS_TOWN],
                 r->row[20].money, p->row[20].money, r->pol_signed, r->pol_letters, r->pol_cards, r->pol_elections,
                 r->pol_electors[0], r->pol_electors[1], r->pol_electors[2], r->pol_pop_min, r->pol_pop_max);
        CHECK_EQ(r->errors, 0);
        CHECK(r->status_month[TS_CHURCH_VILLAGE] > 0 && r->status_month[TS_CHURCH_VILLAGE] <= 15);
        CHECK(r->status_month[TS_MARKET_TOWN] >= 24 && r->status_month[TS_MARKET_TOWN] <= 60);
        CHECK(r->status_month[TS_TOWN] >= 72 && r->status_month[TS_TOWN] <= 160);
        CHECK(r->row[20].pop * 10 >= p->row[20].pop * 8 && r->row[20].pop * 10 <= p->row[20].pop * 13);
        CHECK(r->min_money > -5000 && r->loans <= 2);
        CHECK(r->max_drop_pct <= 10);
        CHECK(r->pol_signed >= 4);
        CHECK(r->pol_cards >= 8);
        CHECK(r->pol_elections >= 2);
        CHECK(r->pol_electors[0] >= 151);
        CHECK(r->pol_pop_min >= 25 && r->pol_pop_max <= 95);
    }
}

/* M3: the rural bot (planner + forestry + farms) keeps the planner's pace; its forest feeds sawmills, its farms
   grow, electrify and mechanise, and milk lets dairies grow. Table: tests/out/industry_rural_<map>.txt */
TEST(balance_rural_bot)
{
    int mp;
    for (mp = 0; mp < NMAPS; mp++) {
        const BalRun *r = get_run(BS_RURAL, mp), *p = get_run(BS_PLANNER, mp);
        test_log("%s: rural pop y10 %d y20 %d (planner %d %d), town m%d; lots saw %d eng %d tex %d dairy %d elec %d; camps %d,"
                 " cut %d tiles; farms %d (T3 %d, %d residents); errors %d", s_maps[mp].key, r->row[10].pop, r->row[20].pop,
                 p->row[10].pop, p->row[20].pop, r->status_month[TS_TOWN], r->ind_lots[IK_WOOD], r->ind_lots[IK_METAL],
                 r->ind_lots[IK_TEXTILE], r->ind_lots[IK_FOOD], r->ind_lots[IK_ELEC], r->camps, r->wood_cut, r->farms,
                 r->farms_t3, r->farm_res, r->errors);
        CHECK_EQ(r->errors, 0);
        CHECK(r->status_month[TS_MARKET_TOWN] >= 24 && r->status_month[TS_MARKET_TOWN] <= 60);
        CHECK(r->status_month[TS_TOWN] >= 72 && r->status_month[TS_TOWN] <= 170);
        CHECK(r->row[20].pop * 10 >= p->row[20].pop * 8 && r->row[20].pop * 10 <= p->row[20].pop * 13);
        CHECK(r->min_money > -6000 && r->loans <= 2);
        CHECK(r->max_drop_pct <= 10);
        CHECK(r->camps >= 1 && r->wood_cut > 0);
        CHECK(r->farms >= 2);
        CHECK(r->ind_lots[IK_METAL] > 0);
    }
}

/* M6: the mill bot (rural + paper mill) and the metal bot (planner + Valtion Metalli), map s7. The mill grows with the
   town (PM2 at 3,000 and 80 wu of wood, PM3 from 1963 with a station), the town becomes a company town, the patron's
   gifts get placed, the slump does not wreck the town. Tables: tests/out/complex_mill_s7.txt, complex_metal_s7.txt */
TEST(balance_complex_bots)
{
    const BalRun *r = get_run(BS_MILL, 0), *p = get_run(BS_RURAL, 0), *v = get_run(BS_METAL, 0), *q = get_run(BS_PLANNER, 0);
    const CpxRun *a = &s_cpx_run[0], *b = &s_cpx_run[1];
    test_log("mill: built %d, stage %d (PM2 %d, PM3 %d), jobs max %d, dependency max %d%%, gifts placed %d, contribution %d mk,"
             " exports %d; pop y10 %d y20 %d (rural %d %d), min money %d, loans %d, drop %d%%, errors %d",
             a->built, a->stage_max, a->stage_year[2], a->stage_year[3], a->jobs_max, a->dep_max, a->gifts, a->contrib_sum,
             a->vol_sum, r->row[10].pop, r->row[20].pop, p->row[10].pop, p->row[20].pop, r->min_money, r->loans, r->max_drop_pct, r->errors);
    test_log("metal: built %d, lines %d, jobs max %d, East exports %d of %d; pop y10 %d y20 %d (planner %d %d), min money %d,"
             " loans %d, errors %d", b->built, b->stage_max, b->jobs_max, b->east_sum, b->vol_sum, v->row[10].pop, v->row[20].pop,
             q->row[10].pop, q->row[20].pop, v->min_money, v->loans, v->errors);
    CHECK_EQ(r->errors, 0);
    CHECK_EQ(v->errors, 0);
    CHECK(a->built && a->stage_max >= 2);
    CHECK(a->gifts >= 1);
    CHECK(r->loans <= 2 && r->max_drop_pct <= 20);
    CHECK(b->built && b->jobs_max >= 300 && b->east_sum > b->vol_sum / 2);
    /* the railway map's planner stalls 1960-66 on power and money before the works can be afforded (1970); the loans
       come from that stall, not from the works (no upkeep, +900 jobs, exports) */
    CHECK(v->loans <= 5 && v->row[20].pop * 100 >= v->row[10].pop * 95);   /* PL2: > -> >= 95% (both runs plateau at the 4 modules) */
}

/* ------------------------------------------------------------------ M7: 1955-1990 (eras, the 1973 oil shock, landmarks)
   The planner (politics off) and the politics bot (on) play 35 years on s7. Like a rich player they buy a landmark each
   January the treasury holds more than 40,000 mk (the money sinks of DESIGN_LESSONS 4.14, BALANCE.md 5). Tables:
   tests/out/balance_era_<style>_s7.txt (yearly: pop, money, operating net, status, clearing meter, oil, fuel,
   landmarks, the newspaper's main headline). */
#include "trade.h"
#include "era.h"
typedef struct {
    int pop[37], money[37], opnet[37], status[37], loan[37], cards[37], a2[37], a3[37];
    int led[37][BL_COUNT];          /* the previous year's ledger, monthly mean per line */
    int landmarks, town_year, max_money, min_money, money75, money90, loans, max_cards, neg_years;
    int church_year, market_year, max_loan, debt_months;     /* PL2: status years, the deepest emergency debt, months in debt */
    int agr_desk, agr_auto;                                  /* PL2: agreements signed at the desk / by the Ministry */
    /* 1.1 D1: per-level runs (DESIGN_1_1 2.6) */
    u32 hash;                                                /* sim_hash at the end (Easy = the 1.0 values) */
    int sweden, emig_all, max_drop, below65, quiet, treas_med10, loanmax_run, warn[37];
    u32 warnk[37];                                           /* 1.1 B1: Cabinet warning kinds (1 << ADV_*) per year */
    int tax[37], dem_r[37];                                  /* 1.1 B1: residential tax and the R bar each January */
    int hw[37], nov_peak[37], nov_sup[37], dark[37];         /* 1.1 B1: hard-winter forecast, 1 Nov power peak / supply, winter
                                                                months with lots in the dark (by winter year) */
} EraRun;

static int era_buy_landmark(void)
{
    static const int lm[5] = { B_CONCERT_HALL, B_TV_TOWER, B_SKI_JUMP, B_PRES_SAUNA, B_FRIENDSHIP_STATUE };
    int k, i, x, y, have;
    for (k = 0; k < 5; k++) {
        const BuildingDef *d = def_building(lm[k]);
        if (!def_unlocked(lm[k], sim_date()->year, sim_stats()->town_status, 0) || g_budget.money < d->cost + 25000) continue;
        for (i = 1, have = 0; i < g_world.lot_hi; i++) if (g_world.lots[i].def == lm[k]) have = 1;
        if (have) continue;
        if (!find_rect(M->mod[0][0] + 14, M->mod[0][1] + 11, d->w + 2, d->h + 2, 0, 100, 60, &x, &y)) continue;
        if (world_lot_check(lm[k], x + 1, y + 1, 0) != PLACE_OK) continue;
        if (cmd("build %s %d %d 0", d->key, x + 1, y + 1)) return 1;
    }
    return 0;
}

/* P1: one 1955-1990 run of any bot style on s7 with a full year table (tests/out/balance_era_<style>_s7.txt): pop,
   money, operating net/month (12-month mean without construction/timber/interest), emergency loan outstanding and
   loans taken, house tiers, apartment tiers 1/2/3, commercial and industrial lots, status, cards that went up that
   year (+ election campaign cards), upkeep index, fuel, element output, clearing balance and the January headline.
   The planner's run is recorded as a replayable script (bal_planner_s7_era.txt) for the VM check. */
static FILE *s_dbg;
static void era_run_map(int style, int map, EraRun *e, const char *path, int record)
{
    BalRun r;
    FILE *f = fopen(path, "w");
    int y, k, k2, lm_run = 0;
    const char *dbg = getenv("LAHIO_B1_DEBUG");
    if (dbg && dbg[0] == '1' && map == 0) {
        char dp[96];
        str_printf(dp, sizeof(dp), "out/b1_months_%s_%d.txt", s_style_name[style], s_level);
        s_dbg = fopen(dp, "w");
    }
    s32 opsum;
    memset(e, 0, sizeof(*e));
    bal_begin(style, map, &r, record ? 2 : 0);
    s_prev_pend = -1;
    e->min_money = g_budget.money;
    if (f) fprintf(f, "1955-1990 run: style %s, map s7 (tests/test_balance.cpp balance_era_1955_1990); a landmark each January the\n"
                      "treasury holds its cost + 25,000 mk (above 40,000). opnet = operating net/month (12-month mean, construction,\n"
                      "timber and loan payments excluded); loan = emergency loan outstanding (taken); cards = event cards that went up\n"
                      "that year (+ election campaign); upk = upkeep index %%; clearing = balance/tolerance\n"
                      "year    pop   money opnet   loan(n)  h1/ h2/ h3   a1/a2/a3  com ind st cards upk fuel elem clearing      headline\n",
                   s_style_name[style]);
    for (y = 0; y <= 35; y++) {
        const TradeState *t = trade_state();
        const CityStats *st = sim_stats();
        e->pop[y] = st->population; e->money[y] = g_budget.money; e->status[y] = st->town_status;
        e->loan[y] = g_budget.loan; e->cards[y] = s_cards_y;
        e->tax[y] = g_budget.tax_res; e->dem_r[y] = sim_demand(DEM_R)->value;
        e->a2[y] = st->lots_tier[ZONE_APARTMENTS][2]; e->a3[y] = st->lots_tier[ZONE_APARTMENTS][3];
        for (k = 0; k < BL_COUNT; k++) e->led[y][k] = g_budget.last_year[k] / 12;
        if (y && s_cards_y > e->max_cards) e->max_cards = s_cards_y;
        if (g_budget.money > e->max_money) e->max_money = g_budget.money;
        if (y >= 20 && g_budget.money < 0) e->neg_years++;
        if (!e->town_year && st->town_status >= TS_TOWN) e->town_year = sim_date()->year;
        if (!e->church_year && st->town_status >= TS_CHURCH_VILLAGE) e->church_year = sim_date()->year;
        if (!e->market_year && st->town_status >= TS_MARKET_TOWN) e->market_year = sim_date()->year;
        if (f) fprintf(f, "%d %6d %7d %+5d %6d(%d) %3d/%3d/%3d %4d/%2d/%2d %4d %3d %d %3d+%d %3d %4d %4d %6d/%-6d %s\n",
                       sim_date()->year, e->pop[y], e->money[y], e->opnet[y], g_budget.loan, g_budget.loans_taken,
                       st->lots_tier[ZONE_HOUSES][1], st->lots_tier[ZONE_HOUSES][2], st->lots_tier[ZONE_HOUSES][3],
                       st->lots_tier[ZONE_APARTMENTS][1], e->a2[y], e->a3[y],
                       st->lots_tier[ZONE_COMMERCIAL][1] + st->lots_tier[ZONE_COMMERCIAL][2] + st->lots_tier[ZONE_COMMERCIAL][3],
                       st->lots_tier[ZONE_INDUSTRY][1] + st->lots_tier[ZONE_INDUSTRY][2] + st->lots_tier[ZONE_INDUSTRY][3],
                       e->status[y], s_cards_y, s_campaigns_y, budget_upkeep_index(), t->fuel_cost, civic_elements_output(),
                       t->balance, t->tolerance, era_news() ? era_news()->head[0] : "-");
        s_cards_y = s_campaigns_y = 0;
        if (y == 35) break;
        while (era_news_pending()) era_news_seen();
        if (g_budget.money > 40000) era_buy_landmark();
        opsum = 0;
        for (k = 0; k < 12; k++) {
            bal_months(&r, 1);
            if (s_dbg) {   /* 1.1 B1 debug (LAHIO_B1_DEBUG=1): monthly rows */
                const CityStats *cs = sim_stats();
                fprintf(s_dbg, "%d-%02d pop %5d money %7d loan %5d tax %2d R %+4d emptyH %4d emptyA %4d pw %5d/%5d short %3d water %5d/%5d"
                        " short %3d net %+5d svc", sim_date()->year, sim_date()->month + 1, cs->population, g_budget.money, g_budget.loan,
                        g_budget.tax_res, sim_demand(DEM_R)->value, empty_zone_tiles(ZONE_HOUSES), empty_zone_tiles(ZONE_APARTMENTS),
                        cs->power_demand, cs->power_supply, cs->power_short_lots, cs->water_demand, cs->water_supply, cs->water_short_lots,
                        budget_net_last());
                for (k2 = 0; k2 < SVC_COUNT; k2++) fprintf(s_dbg, " %d%%", cs->svc_capacity[k2] ? cs->svc_load[k2] * 100 / cs->svc_capacity[k2] : 0);
                {
                    Advice t3[4];
                    int n3 = advice_collect(t3, 4), q3;
                    fprintf(s_dbg, " top");
                    for (q3 = 0; q3 < n3; q3++) fprintf(s_dbg, " %s:%d", advice_kind_name(t3[q3].kind), (int)t3[q3].score);
                }
                fprintf(s_dbg, " nvot %d/%d ot", s_nv_ot, s_nv_ot_svc);
                for (k2 = 0; k2 < SVC_COUNT; k2++) fprintf(s_dbg, " %d", (int)budget_overtime_mk(k2));
                fprintf(s_dbg, " | %s %d", s_adv_tick.score ? advice_kind_name(s_adv_tick.kind) : "-", s_adv_tick.score);
                if (hard_sweden()->active) fprintf(s_dbg, " | sweden jobs %d homes %d content %d pull %d (unemp %d/%d homeless %d vacant %d happy %d)",
                    hard_sweden()->jobs, hard_sweden()->homes, hard_sweden()->content, hard_sweden()->pull, cs->unemployed, cs->workers,
                    cs->homeless, cs->vacant, cs->avg_happiness);
                if (sim_date()->month == 0) {   /* January: what holds each zone tier back (problem bits) */
                    int z2, t2, i2, q2, cnt[REQ_COUNT], nl;
                    for (z2 = ZONE_HOUSES; z2 <= ZONE_APARTMENTS; z2++)
                        for (t2 = 1; t2 <= 2; t2++) {
                            memset(cnt, 0, sizeof(cnt)); nl = 0;
                            for (i2 = 1; i2 < g_world.lot_hi; i2++) {
                                const Lot *l2 = &g_world.lots[i2];
                                const BuildingDef *d2 = def_building(l2->def);
                                if (l2->def == B_NONE || d2->cat != BCAT_ZONE || d2->zone != z2 || d2->tier != t2) continue;
                                nl++;
                                for (q2 = 0; q2 < REQ_COUNT; q2++) if (l2->problems & (1u << q2)) cnt[q2]++;
                            }
                            fprintf(s_dbg, "\n   %s T%d %d lots:", z2 == ZONE_HOUSES ? "houses" : "apts", t2, nl);
                            for (q2 = 0; q2 < REQ_COUNT; q2++) if (cnt[q2]) fprintf(s_dbg, " %s %d", req_short(q2), cnt[q2]);
                        }
                }
                fprintf(s_dbg, "\n");
            }
            if (g_budget.money < e->min_money) e->min_money = g_budget.money;
            if (g_budget.loan > e->max_loan) e->max_loan = g_budget.loan;
            if (g_budget.loan > 0 || g_budget.money < 0) e->debt_months++;
            {   /* 1.1 B1: by the calendar year of the tick (the 1 January tick belongs to the new year) */
                int wy = ICLAMP(sim_date()->year - 1955, 0, 36);
                if (s_adv_tick.score >= 500) { e->warn[wy]++; e->warnk[wy] |= 1u << s_adv_tick.kind; }
                if (sim_date()->month == 10) {   /* 1 November: the forecast and the winter peak against supply */
                    SeasonPeak pk;
                    season_peak(&pk);
                    e->hw[wy] = season_forecast() == SFC_HARD; e->nov_peak[wy] = pk.power_peak; e->nov_sup[wy] = pk.power_supply;
                }
                if (season_is_winter() && sim_stats()->power_short_lots > 0) e->dark[season_winter_year(sim_date()->year, sim_date()->month) - 1955]++;
            }   /* 1.1 B1: read at the month
                                                     tick, before the bot answers (D1 read it after: a bot that reacts to the
                                                     same numbers in the same month never "saw" a warning) */   /* 1.1 D1: months with a Cabinet warning (a
                                                     shortage, money, a waiting decree, winter, Sweden, an expectation due) */
            lm_run = g_budget.loan >= 40000 ? lm_run + 1 : 0;        /* 1.1 D1: stuck at LOAN_MAX (budget.cpp) */
            if (lm_run > e->loanmax_run) e->loanmax_run = lm_run;
            opsum += budget_net_last() - g_budget.last[BL_CONSTRUCTION] - g_budget.last[BL_TIMBER] - g_budget.last[BL_INTEREST];
        }
        e->opnet[y + 1] = opsum / 12;
    }
    if (s_dbg) { fclose(s_dbg); s_dbg = NULL; }
    e->landmarks = era_landmark_count();
    e->loans = g_budget.loans_taken;
    e->agr_desk = s_agr_desk; e->agr_auto = s_agr_auto;
    if (style == BS_DENSE) {      /* P1: the 1990 lahio town for the VM's heat-overlay / apartment screenshots */
        SaveMeta sm;
        int i;
        memset(&sm, 0, sizeof(sm));
        sm.zoom = ZOOM_64;
        for (i = 1; i < g_world.lot_hi; i++)
            if (g_world.lots[i].def == B_HEAT_PLANT) { sm.cam_x = g_world.lots[i].x + 1; sm.cam_y = g_world.lots[i].y + 1; break; }
        save_game("out/P1_DENSE.KAU", &sm);
    }
    e->money75 = e->money[20]; e->money90 = e->money[35];
    bal_end(&r);
    {   /* 1.1 D1: Sweden 1969-70, the worst yearly drop, years under the 1965 population, the coasting metrics */
        int med[11], n = 0, i, j;
        e->hash = r.hash;
        e->sweden = hard_sweden()->total;
        e->emig_all = sim_stats()->emigrated_total;
        for (y = 1; y <= 35; y++) {
            if (e->pop[y - 1] > 500 && e->pop[y] < e->pop[y - 1])
                e->max_drop = IMAX(e->max_drop, (int)((s64)(e->pop[y - 1] - e->pop[y]) * 100 / e->pop[y - 1]));
            if (y > 10 && e->pop[y] < e->pop[10]) e->below65++;
        }
        for (y = 20; y < 35; y++) if (!e->warn[y]) e->quiet++;      /* 1975-1989: years without a Cabinet warning */
        for (y = 25; y <= 35; y++) {                                  /* Januaries 1980-1990: treasury / gross expenses */
            s32 ex = 0;
            for (k = 0; k < BL_COUNT; k++) if (k != BL_CONSTRUCTION && e->led[y][k] < 0) ex -= e->led[y][k];
            med[n++] = ex > 0 ? (int)((s64)e->money[y] * 10 / ex) : 0;
        }
        for (i = 0; i < n; i++) for (j = i + 1; j < n; j++) if (med[j] < med[i]) { int t = med[i]; med[i] = med[j]; med[j] = t; }
        e->treas_med10 = med[n / 2];
    }
    if (f) {
        fprintf(f, "summary: pop 1965 %d 1975 %d 1990 %d; money 1975 %d 1990 %d min %d max %d; emergency loans %d (deepest %d,"
                   " %d months in debt); negative Januaries 1975-90 %d; apartments T2/T3 1965 %d/%d 1975 %d/%d 1990 %d/%d; most cards"
                   " in a year %d; church village %d, market town %d, town %d; landmarks %d\n",
                e->pop[10], e->pop[20], e->pop[35], e->money75, e->money90, e->min_money, e->max_money, e->loans, e->max_loan,
                e->debt_months, e->neg_years, e->a2[10], e->a3[10], e->a2[20], e->a3[20], e->a2[35], e->a3[35], e->max_cards,
                e->church_year, e->market_year, e->town_year, e->landmarks);
        fprintf(f, "ledger (previous year, mk/month):");
        for (k = 0; k < BL_COUNT; k++) fprintf(f, " %d=%s", k, text_term_label(TERMK_BUDGET, k));
        fprintf(f, "\n");
        for (y = 1; y <= 35; y++) {
            fprintf(f, "%d", 1955 + y);
            for (k = 0; k < BL_COUNT; k++) fprintf(f, " %5d", e->led[y][k]);
            fprintf(f, "\n");
        }
        fprintf(f, "1.1 B1: tax %% / R bar each January; Cabinet warnings (score >= 500, months: kinds) per year\n");
        for (y = 0; y < 35; y++) {
            int q;
            fprintf(f, "%d tax %2d R %+4d winter %s peak %5d/%5d dark %d warn %2d:", 1955 + y, e->tax[y], e->dem_r[y],
                    e->hw[y] ? "HARD" : "ord ", e->nov_peak[y], e->nov_sup[y], e->dark[y], e->warn[y]);
            for (q = 0; q < ADV_COUNT; q++) if (e->warnk[y] & (1u << q)) fprintf(f, " %s", advice_kind_name(q));
            fprintf(f, "\n");
        }
        {   /* P1: what holds the apartment blocks back (problem bits of the T1/T2 blocks at the end) */
            int i, q, cnt[REQ_COUNT], n = 0;
            memset(cnt, 0, sizeof(cnt));
            for (i = 1; i < g_world.lot_hi; i++) {
                const Lot *l = &g_world.lots[i];
                const BuildingDef *d = def_building(l->def);
                if (l->def == B_NONE || d->cat != BCAT_ZONE || d->zone != ZONE_APARTMENTS || d->tier >= 3) continue;
                n++;
                for (q = 0; q < REQ_COUNT; q++) if (l->problems & (1u << q)) cnt[q]++;
            }
            fprintf(f, "apartment blocks below T3 at the end: %d; unmet next-tier items:", n);
            for (q = 0; q < REQ_COUNT; q++) if (cnt[q]) fprintf(f, " %s %d", req_short(q), cnt[q]);
            fprintf(f, "\n");
        }
        {   /* PL2: the town's top problems and the demand bars at the end */
            SimProblem pr[8];
            int q, np = sim_top_problems(pr, 8);
            fprintf(f, "top problems at the end:");
            for (q = 0; q < np; q++) fprintf(f, " %s %d lots/%d res;", req_short(pr[q].req), (int)pr[q].count, (int)pr[q].residents);
            fprintf(f, " demand R %d C %d I %d\n", sim_demand(DEM_R)->value, sim_demand(DEM_C)->value, sim_demand(DEM_I)->value);
            sim_dump_report(f);
        }
        civic_dump(f);
        era_dump(f);
        fclose(f);
    }
}

static void era_run(int style, EraRun *e, const char *path, int record) { era_run_map(style, 0, e, path, record); }

/* PL2 item 10 (PLAYTEST 2: "got into debt without building the element factory"): the naive player who never builds it
   keeps tier-1 blocks all game, but the town must not spiral: at most two emergency loans, repaid, money positive in 1990 */
TEST(balance_naive_skips_element_factory)
{
    EraRun e;
    s_nv_nofactory = 1;
    era_run(BS_NAIVE, &e, "out/balance_era_naive_nofactory_s7.txt", 0);
    s_nv_nofactory = 0;
    test_log("naive without the element factory: pop 1965 %d 1975 %d 1990 %d, money 1975 %d 1990 %d min %d, loans %d (deepest %d,"
             " %d months in debt), market town %d, town %d, apt T2 1990 %d", e.pop[10], e.pop[20], e.pop[35], e.money75, e.money90,
             e.min_money, e.loans, e.max_loan, e.debt_months, e.market_year, e.town_year, e.a2[35]);
    CHECK_EQ(e.a2[35], 0);                                  /* no elements, no tier-2 blocks */
    CHECK(e.loans <= 2);
    CHECK(e.money90 > 0 && e.max_loan <= 25000);
    CHECK(e.market_year > 0 && e.market_year <= 1966);
}

static EraRun s_er[BS_COUNT];
static int s_er_done;                  /* 1.1 D1: the Easy s7 runs are reused by balance_levels_1955_1990 */
TEST(balance_era_1955_1990)
{
    EraRun *er = s_er;
    static const int styles[BS_COUNT] = { BS_PLANNER, BS_POLITICS, BS_DENSE, BS_MILL, BS_METAL,      /* P1's five first (er[0..4]) */
                                          BS_NAIVE, BS_GREEDY, BS_INDUSTRY, BS_RURAL, BS_TAX13, BS_TAX5 };   /* PL2: every style */
    char path[96];
    FILE *sf = fopen("out/balance_era_summary.txt", "w");
    int k;
    if (sf) fprintf(sf, "PL2 1955-1990 runs on s7 (tests/test_balance.cpp balance_era_1955_1990), January values; status years; L = emergency\n"
                        "loans taken (deepest outstanding, months with a loan or a negative treasury)\n"
                        "style      1960   1965   1970   1975   1980   1990 | money 1965   1975   1990    min | L (deep, months) |"
                        " church market town | apt T2/T3 1965 1975 1990 | agreements desk/Ministry\n");
    for (k = 0; k < BS_COUNT; k++) {
        const EraRun *e = &er[k];
        str_printf(path, sizeof(path), "out/balance_era_%s_s7.txt", s_style_name[styles[k]]);
        era_run(styles[k], &er[k], path, styles[k] == BS_PLANNER || styles[k] == BS_NAIVE);   /* PL2: + the naive script */
        test_log("%-8s: pop 1965 %5d 1975 %5d 1990 %5d | money 1975 %6d 1990 %7d min %6d max %6d | loans %d neg %d |"
                 " apt T2/T3 1965 %d/%d 1975 %d/%d 1990 %d/%d | cards max %d | town %d landmarks %d",
                 s_style_name[styles[k]], e->pop[10], e->pop[20], e->pop[35], e->money75, e->money90, e->min_money, e->max_money,
                 e->loans, e->neg_years, e->a2[10], e->a3[10], e->a2[20], e->a3[20], e->a2[35], e->a3[35], e->max_cards,
                 e->town_year, e->landmarks);
        if (sf) fprintf(sf, "%-8s %6d %6d %6d %6d %6d %6d | %6d %6d %6d %6d | %d (%5d, %3d) | %4d %4d %4d | %d/%d %d/%d %d/%d | %d/%d\n",
                        s_style_name[styles[k]], e->pop[5], e->pop[10], e->pop[15], e->pop[20], e->pop[25], e->pop[35],
                        e->money[10], e->money75, e->money90, e->min_money, e->loans, e->max_loan, e->debt_months,
                        e->church_year, e->market_year, e->town_year, e->a2[10], e->a3[10], e->a2[20], e->a3[20], e->a2[35], e->a3[35],
                        e->agr_desk, e->agr_auto);
    }
    if (sf) fclose(sf);
    for (k = 0; k < BS_COUNT; k++) {                        /* PL2: checked after the table is complete */
        const EraRun *e = &er[k];
        if (styles[k] == BS_TAX5) continue;                 /* PL2: the 5% town is the deliberate bankrupt (balance_tax_pressure) */
        CHECK(e->pop[35] * 10 >= e->pop[20] * 8);           /* no collapse after the oil shocks or 1990 */
        if (styles[k] != BS_TAX13) CHECK(e->money90 < 150000);   /* no runaway money pile by 1990 (PL2: 13% taxes buy money
                                                               with growth: 331k by 1990 is the trade, balance_tax_pressure) */
        CHECK(e->max_cards <= 1);                           /* P1: <= 1 card per game year (campaign cards apart) */
        if (styles[k] == BS_POLITICS || styles[k] == BS_NAIVE) CHECK_EQ(e->agr_auto, 0);   /* PL2: every agreement reaches the desk */
        if (styles[k] == BS_NAIVE) {                        /* PL2 item 7c: a first-time human still gets there */
            CHECK(e->market_year > 0 && e->market_year <= 1966);
            CHECK(e->town_year > 0 && e->town_year <= 1982);
            CHECK(e->loans <= 2 && e->money90 > 0);
        }
    }
    CHECK(er[0].town_year > 0 && er[1].town_year > 0);      /* town status reached (by decree with politics on) */
    {   /* 1.1 D1: Easy == 1.0 bit for bit. These are the 1.0 world hashes of the 11 runs (v1.0 sources, 2026-11-08) and the
           BALANCE.md 9 planner result; any change on Easy is a regression (DESIGN_1_1 2.1) */
        static const u32 h10[BS_COUNT] = { 0x6f2ab540u, 0x9f3bf1f7u, 0x73348375u, 0x58206c10u, 0x2ad2626eu, 0xc81d1741u,
                                           0xb89e799au, 0xdc41e9ccu, 0x6a4ceeb3u, 0x3342719eu, 0x9723bb09u };
        for (k = 0; k < BS_COUNT; k++) {
            if (er[k].hash != h10[k]) test_log("Easy != 1.0: %s hash %08x (1.0: %08x)", s_style_name[styles[k]], er[k].hash, h10[k]);
            CHECK_EQ(er[k].hash, h10[k]);
        }
        CHECK_EQ(er[0].pop[35], 12188);
        CHECK_EQ(er[0].money90, 4171);
        s_er_done = 1;
    }
}

/* ------------------------------------------------------------------ 1.1 D1: the three levels, 1955-1990 (DESIGN_1_1 2.6)
   Every style on s7 at Normal and Hard (LAHIO_LEVELS_ALL=1 in the environment: also c3 and r30 at all three levels), one
   year table per run (tests/out/balance_era_<style>_<map>_<level>.txt; Easy on s7 = balance_era_<style>_s7.txt), the
   summary tests/out/balance_levels_summary.txt with the 2.6 targets per row (MISS = a tuning target not met yet, for B1),
   and checks for the guards that hold on every level (no collapse, <= 1 card a year, never stuck at the loan cap). */
static const int s_lv_styles[BS_COUNT] = { BS_PLANNER, BS_POLITICS, BS_DENSE, BS_MILL, BS_METAL, BS_NAIVE, BS_GREEDY,
                                           BS_INDUSTRY, BS_RURAL, BS_TAX13, BS_TAX5 };
static EraRun s_lv[NMAPS][DIFF_COUNT][BS_COUNT];
static int s_lv_ran[NMAPS][DIFF_COUNT];

static void lv_run(int map, int level)
{
    char path[96];
    int k;
    if (s_lv_ran[map][level]) return;
    if (map == 0 && level == DIFF_EASY && s_er_done) { memcpy(s_lv[0][0], s_er, sizeof(s_er)); s_lv_ran[0][0] = 1; return; }
    s_level = level;
    for (k = 0; k < BS_COUNT; k++) {
        const char *only = getenv("LAHIO_B1_ONLY");   /* B1 tuning: one style only (the others stay empty) */
        if (only && only[0] && strcmp(only, s_style_name[s_lv_styles[k]]) != 0) { memset(&s_lv[map][level][k], 0, sizeof(EraRun)); continue; }
        if (map == 0 && level == DIFF_EASY) str_printf(path, sizeof(path), "out/balance_era_%s_s7.txt", s_style_name[s_lv_styles[k]]);
        else str_printf(path, sizeof(path), "out/balance_era_%s_%s_%s.txt", s_style_name[s_lv_styles[k]], s_maps[map].key,
                        level == DIFF_HARD ? "hard" : level == DIFF_NORMAL ? "normal" : "easy");
        era_run_map(s_lv_styles[k], map, &s_lv[map][level][k], path,
                    map == 0 && level != DIFF_EASY && (s_lv_styles[k] == BS_PLANNER || s_lv_styles[k] == BS_NAIVE));
    }
    s_level = DIFF_EASY;
    s_lv_ran[map][level] = 1;
}

static int lv_pct(int v, int base) { return base > 0 ? (int)((s64)v * 100 / base) : 0; }

/* the 2.6 targets for one run (Normal/Hard): misses appended to buf; returns the number of misses */
static int lv_targets(int style, int level, const EraRun *e, const EraRun *easy, char *buf, int size)
{
    int miss = 0, peak = 0, y, sw = lv_pct(e->sweden * 10, e->pop[14]);   /* Sweden loss in tenths of % of Jan 1969 */
    int p90 = lv_pct(e->pop[35], easy->pop[35]);
    char *p = buf;
#define MISS(cond, txt) do { if (!(cond)) { miss++; p += str_printf(p, size - (int)(p - buf), " %s", txt); } } while (0)
    buf[0] = 0;
    for (y = 0; y <= 35; y++) peak = IMAX(peak, e->pop[y]);
    MISS(e->max_cards <= 1, "cards/year>1");
    if (level == DIFF_NORMAL) {
        MISS(e->max_drop <= 15, "drop>15%");
        if (style == BS_PLANNER || style == BS_POLITICS) {
            MISS(e->town_year > 0 && e->town_year <= 1966, "town>1966");
            MISS(e->loans == 0, "loans>0");
            MISS(p90 >= 90, "pop90<90%");
            MISS(sw <= 20, "sweden>2%");
        }
        if (style == BS_NAIVE) {
            MISS(e->loans <= 2, "loans>2");
            MISS(e->max_drop >= 3, "no-crisis(drop<3%)");
            MISS(p90 >= 70, "pop90<70%");
            MISS(sw >= 40 && sw <= 80, "sweden-not-4-8%");
        }
        if (style == BS_GREEDY) { MISS(e->pop[35] < easy->pop[35], "greedy>=easy"); MISS(e->loans >= 1, "greedy-no-loan"); }
        if (style == BS_TAX13) MISS(e->treas_med10 <= 240, "tax13>24months");
        if (style == BS_PLANNER || style == BS_DENSE || style == BS_MILL) {
            MISS(e->treas_med10 <= 120, "treasury>12months");
            MISS(e->quiet <= 6, "quiet>6");
        }
    } else if (level == DIFF_HARD) {
        MISS(e->max_drop <= 25, "drop>25%");
        MISS(e->loanmax_run <= 60, "stuck-at-loan-cap");
        if (style == BS_PLANNER || style == BS_POLITICS) {
            MISS(e->town_year > 0 && e->town_year <= 1970, "town>1970");
            MISS(e->loans <= 1 && e->loan[35] == 0, "loans>1-or-uncleared");
        }
        if (style == BS_NAIVE) {
            MISS(e->loans <= 3, "loans>3");
            MISS(e->below65 <= 3, "below-1965-pop>3y");
            MISS(p90 >= 50, "pop90<50%");
        }
        if (style == BS_GREEDY) MISS(e->pop[35] * 2 >= peak, "greedy<50%peak");
        if (style == BS_PLANNER || style == BS_DENSE || style == BS_MILL) {
            MISS(e->treas_med10 <= 60, "treasury>6months");
            MISS(e->quiet <= 3, "quiet>3");
        }
    }
#undef MISS
    return miss;
}

TEST(balance_levels_1955_1990)
{
    static const char *const lvn[DIFF_COUNT] = { "easy", "normal", "hard" };
    const char *all = getenv("LAHIO_LEVELS_ALL");
    int nmaps = all && all[0] == '1' ? NMAPS : 1, mp, lv, k, misses = 0;
    FILE *f = fopen("out/balance_levels_summary.txt", "w");
    if (f) fprintf(f, "1.1 D1 per-level 1955-1990 runs (tests/test_balance.cpp balance_levels_1955_1990), January values. pop%% = 1990\n"
                      "population as %% of the same style on Easy; L = emergency loans (deepest, months in debt); swe = Sweden 1969-70 loss\n"
                      "in %% of the January 1969 population; drop = worst yearly fall %%; <65 = years below the 1965 population; treas =\n"
                      "median treasury 1980-90 in months of gross expenses; quiet = years 1975-89 without a Cabinet warning (advice score\n"
                      ">= 500: shortages, money, a waiting decree, winter, Sweden, an expectation due); misses =\n"
                      "DESIGN_1_1 2.6 targets not met (winter targets wait for W1/B1)\n"
                      "1.1 B1: hw = hard winters 1955-89, dark = winter months with lots in the dark (power short)\n"
                      "map style    level    1965   1975   1990 pop%% | money90    min | L (deep, mon) | town |  swe drop <65 | treas quiet | hw dark | hash     misses\n");
    for (mp = 0; mp < nmaps; mp++)
        for (lv = 0; lv < DIFF_COUNT; lv++) {
            lv_run(mp, lv);
            for (k = 0; k < BS_COUNT; k++) {
                const EraRun *e = &s_lv[mp][lv][k], *ez = &s_lv[mp][0][k];
                char ms[200];
                int nm = lv == DIFF_EASY ? 0 : lv_targets(s_lv_styles[k], lv, e, ez, ms, sizeof(ms));
                if (lv == DIFF_EASY) ms[0] = 0;
                misses += nm;
                int hwn = 0, dkn = 0, yy;
                for (yy = 0; yy < 35; yy++) { hwn += e->hw[yy]; dkn += e->dark[yy]; }
                if (f) fprintf(f, "%-3s %-8s %-6s %6d %6d %6d %4d | %7d %6d | %d (%5d, %3d) | %4d | %2d.%d %4d %3d | %3d.%d %5d | %2d %4d | %08x%s\n",
                               s_maps[mp].key, s_style_name[s_lv_styles[k]], lvn[lv], e->pop[10], e->pop[20], e->pop[35],
                               lv_pct(e->pop[35], ez->pop[35]), e->money90, e->min_money, e->loans, e->max_loan, e->debt_months,
                               e->town_year, lv_pct(e->sweden * 10, e->pop[14]) / 10, lv_pct(e->sweden * 10, e->pop[14]) % 10,
                               e->max_drop, e->below65, e->treas_med10 / 10, IABS(e->treas_med10) % 10, e->quiet, hwn, dkn, e->hash, ms);
                if (lv != DIFF_EASY)
                    test_log("%s %-8s %-6s: pop 1990 %5d (%3d%% of Easy) money %7d min %7d loans %d town %d sweden %d drop %d%% treas %d.%d quiet %d%s",
                             s_maps[mp].key, s_style_name[s_lv_styles[k]], lvn[lv], e->pop[35], lv_pct(e->pop[35], ez->pop[35]), e->money90,
                             e->min_money, e->loans, e->town_year, e->sweden, e->max_drop, e->treas_med10 / 10, IABS(e->treas_med10) % 10,
                             e->quiet, ms);
            }
        }
    if (f) { fprintf(f, "%d target misses in all\n", misses); fclose(f); }
    /* guards (fail softly, pillar 7). Checked: <= 1 card a year everywhere; Normal: no yearly fall > 15 %, no collapse after
       1975, the first-time human finishes 1990 above its 1965 town; Hard (s7, the planner-style bots that read the forecast
       and the paper): no yearly fall > 25 %, never > 5 years at the loan cap. Listed only (B1): metal (operating surplus but
       its 1980s instalments wait for a 2,000 mk treasury), naive, industry, greedy, tax13 on Hard, and c3/r30 */
    {
        int hard_miss = 0;
        for (mp = 0; mp < nmaps; mp++)
            for (lv = 1; lv < DIFF_COUNT; lv++)
                for (k = 0; k < BS_COUNT; k++) {
                    const EraRun *e = &s_lv[mp][lv][k];
                    if (s_lv_styles[k] == BS_TAX5) continue;    /* the deliberate bankrupt */
                    CHECK(e->max_cards <= 1);
                    if (lv == DIFF_HARD) {
                        int st = s_lv_styles[k], planner_class = st == BS_PLANNER || st == BS_POLITICS || st == BS_DENSE ||
                                                                 st == BS_MILL || st == BS_RURAL;
                        if (e->max_drop > 25 || e->loanmax_run > 60) {
                            hard_miss++;
                            test_log("Hard guard miss%s: %s %s drop %d%%, %d months at the loan cap", planner_class ? "" : " (B1)",
                                     s_maps[mp].key, s_style_name[st], e->max_drop, e->loanmax_run);
                        }
                        if (mp == 0 && planner_class) { CHECK(e->max_drop <= 25); CHECK(e->loanmax_run <= 60); }   /* recoverable */
                        continue;
                    }
                    CHECK(e->max_drop <= 15);
                    CHECK(e->pop[35] * 10 >= e->pop[20] * 6);   /* no collapse after 1975 */
                    if (s_lv_styles[k] == BS_NAIVE) CHECK(e->pop[35] > e->pop[10]);
                }
        test_log("%d target misses (balance_levels_summary.txt), %d Hard guard misses", misses, hard_miss);
    }
}

/* ------------------------------------------------------------------ 1.1 D1: levers, save, Sweden, expectations */
/* orchestrator rule (season.cpp season_hard_rule): no hard winter in a game's first two winters, on any level */
TEST(d1_no_hard_winter_in_first_two)
{
    int fx, lv;
    for (lv = DIFF_NORMAL; lv < DIFF_COUNT; lv++) {
        s_level = lv;
        bal_new_city(&s_maps[0]);
        s_level = DIFF_EASY;
        CHECK(!season_hard_rule(1955, &fx) && !season_hard_rule(1956, &fx));
        CHECK(season_hard_rule(1957, &fx) && season_hard_rule(1986, &fx) && fx);
    }
}

/* the table: Easy = the 1.0 constants, Normal and Hard never easier than Easy */
TEST(d1_table_easy_is_1_0)
{
    const DiffParams *e = &g_diff[DIFF_EASY];
    int lv;
    CHECK(!strcmp(diff_name(DIFF_EASY), "Easy") && !strcmp(diff_name(DIFF_NORMAL), "Normal") && !strcmp(diff_name(DIFF_HARD), "Hard"));
    CHECK_EQ(e->upkeep_pct, 100); CHECK_EQ(e->upkeep_cap, 130); CHECK_EQ(e->tax_yield_pct, 100); CHECK_EQ(e->tax_neutral, TAX_NEUTRAL);
    CHECK_EQ(e->overtime_free, 125); CHECK_EQ(e->emig_unemp_div, 8); CHECK_EQ(e->emig_homeless_div, 4);
    CHECK_EQ(e->decline_checks, SIM_G_DOWN); CHECK_EQ(e->decline_util_checks, 8); CHECK_EQ(e->sweden_permille, 0);
    CHECK_EQ(e->expectations, 0); CHECK_EQ(e->card_neg_pct, 100); CHECK_EQ(e->card_gap_min, POL_EVENT_MIN);
    CHECK_EQ(e->card_gap_max, POL_EVENT_MAX); CHECK_EQ(e->auth_div, 20); CHECK_EQ(e->east_target, 50); CHECK_EQ(e->shock_pct, 100);
    CHECK_EQ(e->fire_ppb, CIVIC_FIRE_IGNITE_PPB); CHECK_EQ(e->fire_spread_max, CIVIC_FIRE_SPREAD_MAX); CHECK_EQ(e->fire_winter_pct, 100);
    CHECK_EQ(e->winter_load_pct, 0); CHECK_EQ(e->hard_winter_pct, 0); CHECK_EQ(e->hard_load_pct, 0); CHECK_EQ(e->hard_water_pct, 100);
    CHECK_EQ(e->plough_pct, 0); CHECK_EQ(e->plough_hard_pct, 0); CHECK_EQ(e->frost_repair10, 0);
    for (lv = DIFF_NORMAL; lv < DIFF_COUNT; lv++) {
        const DiffParams *p = &g_diff[lv], *q = &g_diff[lv - 1];
        CHECK(p->upkeep_pct >= q->upkeep_pct && p->upkeep_cap >= q->upkeep_cap && p->tax_yield_pct <= q->tax_yield_pct);
        CHECK(p->tax_neutral <= q->tax_neutral && p->overtime_free <= q->overtime_free && p->emig_unemp_div <= q->emig_unemp_div);
        CHECK(p->decline_checks <= q->decline_checks && p->decline_util_checks <= q->decline_util_checks && p->decline_checks >= 1);
        CHECK(p->card_neg_pct >= q->card_neg_pct && p->card_gap_max <= q->card_gap_max && p->card_gap_min >= POL_EVENT_MIN);
        CHECK(p->auth_div >= q->auth_div && p->east_target <= q->east_target && p->shock_pct >= q->shock_pct);
        CHECK(p->fire_ppb >= q->fire_ppb && p->sweden_permille >= q->sweden_permille && p->expectations);
    }
    {   /* the Guide page and the notes name every lever; Easy has no lever notes (nothing differs from 1.0) */
        char ln[20][100], b[100];
        int n, k;
        diff_set_level(DIFF_EASY);
        for (k = 0; k < HN_LEVEL; k++) CHECK(!hard_note(k, b, sizeof(b)));
        diff_set_level(DIFF_HARD);
        n = hard_level_lines(ln, 20);
        test_log("Guide page (Hard), %d lines:", n);
        for (k = 0; k < n; k++) { test_log("  %s", ln[k]); CHECK(strlen(ln[k]) < 99); }
        CHECK(n >= 14);
        CHECK(hard_note(HN_UPKEEP, b, sizeof(b)) || sim_date()->year < 1961);
        CHECK(hard_note(HN_DECLINE, b, sizeof(b)) && hard_note(HN_CARDS, b, sizeof(b)) && hard_note(HN_EAST, b, sizeof(b)));
        CHECK(hard_note(HN_FIRE, b, sizeof(b)) && hard_note(HN_SHOCK, b, sizeof(b)) && hard_note(HN_AUTHORITY, b, sizeof(b)));
        diff_set_level(DIFF_EASY);
    }
}

/* the levers act where they should: the same town's books, card effects and authority per level (diff_set_level on
   the live town, then back) */
TEST(d1_levers_bite)
{
    BalRun r;
    Term t[MAX_TERMS + 16];
    Money svc[DIFF_COUNT], tax[DIFF_COUNT], roads[DIFF_COUNT];
    int lv, n, k, ev, ch, i, inc[DIFF_COUNT];
    s_level = DIFF_EASY;
    bal_begin(BS_POLITICS, 0, &r, 0);
    bal_months(&r, 12 * 12);                     /* January 1967: the phase-in is complete */
    for (lv = 0; lv < DIFF_COUNT; lv++) {
        diff_set_level(lv);
        n = budget_projection(t, COUNTOF(t));
        svc[lv] = tax[lv] = roads[lv] = 0;
        for (k = 0; k < n; k++) {
            if (t[k].id >= BL_SVC_FIRST && t[k].id < BL_SVC_FIRST + SVC_COUNT) svc[lv] += t[k].amount;
            if (t[k].id == BL_TAX_RES || t[k].id == BL_TAX_BUS) tax[lv] += t[k].amount;
            if (t[k].id == BL_ROADS) roads[lv] += t[k].amount;
        }
        inc[lv] = politics_authority_income10();
    }
    diff_set_level(DIFF_EASY);
    test_log("1967 books by level: services %d / %d / %d, taxes %d / %d / %d, roads %d / %d / %d; authority %d / %d / %d tenths",
             svc[0], svc[1], svc[2], tax[0], tax[1], tax[2], roads[0], roads[1], roads[2], inc[0], inc[1], inc[2]);
    CHECK(svc[0] < 0 && svc[1] < svc[0] && svc[2] < svc[1]);                       /* dearer upkeep (negative lines) */
    CHECK(IABS(roads[1] * 100 / roads[0] - g_diff[DIFF_NORMAL].upkeep_pct) <= 2);
    CHECK(IABS(tax[2] * 100 / tax[0] - g_diff[DIFF_HARD].tax_yield_pct) <= 2 && tax[1] == tax[0]);
    CHECK(inc[0] > inc[1] && inc[1] > inc[2]);
    for (ev = 0, i = 0; ev < EV_COUNT && !i; ev++)                                   /* a card with a money cost */
        for (ch = 0; ch < g_events[ev].nch && !i; ch++)
            for (k = 0; k < POL_FX_MAX; k++)
                if (g_events[ev].ch[ch].fx[k].kind == FX_MONEY && g_events[ev].ch[ch].fx[k].value <= -10 && ev != EV_SAUNA) {
                    PolFx fx[POL_FX_MAX];
                    int v0 = g_events[ev].ch[ch].fx[k].value;
                    diff_set_level(DIFF_HARD);
                    politics_choice_fx(ev, ch, fx);
                    diff_set_level(DIFF_EASY);
                    test_log("card %s choice %d: money %d on Easy, %d on Hard", g_events[ev].key, ch, v0 * 10, fx[k].value * 10);
                    CHECK_EQ(fx[k].value, v0 * g_diff[DIFF_HARD].card_neg_pct / 100);
                    i = 1;
                    break;
                }
    CHECK(i);
}

/* each level survives save + load bit for bit, keeps its level, and continues identically; 1.0 saves load as Easy */
TEST(d1_save_roundtrip_levels)
{
    int lv;
    for (lv = 0; lv < DIFF_COUNT; lv++) {
        BalRun r;
        SaveFile *sf;
        u32 h0, h1, h2;
        s32 sw0;
        s_level = lv;
        bal_begin(BS_PLANNER, 0, &r, 0);
        bal_months(&r, 14 * 12 + 5);              /* June 1969: Sweden months on Normal/Hard */
        s_level = DIFF_EASY;
        sf = save_mem_open();
        CHECK(save_game_to(sf, NULL));
        h0 = sim_hash(); sw0 = hard_sweden()->total;
        sim_run_months(20);
        h1 = sim_hash();
        save_mem_reopen(sf);
        diff_set_level(DIFF_EASY);
        CHECK(load_game_from(sf, NULL));
        CHECK_EQ(diff_level(), lv);
        CHECK_EQ(sim_settings()->difficulty, lv);
        CHECK_EQ(sim_hash(), h0);
        CHECK_EQ(hard_sweden()->total, sw0);
        sim_run_months(20);
        h2 = sim_hash();
        save_mem_close(sf);
        test_log("%s: hash at save %08x, 20 months on %08x / after load %08x; Sweden 1969 so far %d", diff_name(lv), h0, h1, h2, sw0);
        CHECK_EQ(h2, h1);
        if (lv == DIFF_EASY) CHECK_EQ(sw0, 0); else CHECK(sw0 > 0);
    }
    {   /* the user's 1.0 play-test save (SIM chunk v2): Easy */
        diff_set_level(DIFF_HARD);
        if (load_game("../PT2_1990.KAU", NULL)) {
            test_log("PT2_1990.KAU (1.0) loads as %s", diff_name(diff_level()));
            CHECK_EQ(diff_level(), DIFF_EASY);
            CHECK_EQ(sim_settings()->difficulty, DIFF_EASY);
        } else test_log("PT2_1990.KAU not found: 1.0-save check skipped");
        diff_set_level(DIFF_EASY);
    }
}

/* Sweden 1969-70: the term, its three reasons, 24 months, the sub-total; nothing on Easy or in other years */
TEST(d1_sweden_years)
{
    int lv;
    FILE *f = fopen("out/d1_sweden.txt", "w");
    for (lv = 0; lv < DIFF_COUNT; lv++) {
        BalRun r;
        int m, months = 0;
        s32 sum = 0;
        s_level = lv;
        bal_begin(lv == DIFF_EASY ? BS_PLANNER : BS_NAIVE, 0, &r, 0);
        s_level = DIFF_EASY;
        bal_months(&r, 13 * 12);                  /* January 1968 */
        for (m = 0; m < 48; m++) {                /* 1968-1971 */
            const HardSweden *sw = hard_sweden();
            const CityStats *st = sim_stats();
            const Demand *dr = sim_demand(DEM_R);
            s32 term = 0, expect;
            int k, y = sim_date()->year;
            bal_months(&r, 1);
            for (k = 0; k < dr->nterms; k++) if (dr->terms[k].id == DT_SWEDEN) term = dr->terms[k].amount;
            expect = (s32)((s64)st->population * g_diff[lv].sweden_permille * (3 - sw->reasons) * (3 - sw->reasons) / 9000);   /* B1 */
            if (f) fprintf(f, "%s %d-%02d pop %5d unemp %4d homeless %3d vacant %4d happy %2d | active %d reasons %d (%d%d%d) pull %3d term %4d left %3d total %4d\n",
                           diff_name(lv), y, sim_date()->month ? sim_date()->month : 12, st->population, st->unemployed, st->homeless, st->vacant,
                           st->avg_happiness, sw->active, sw->reasons, sw->jobs, sw->homes, sw->content, sw->pull, term, sw->month, sw->total);
            if (lv == DIFF_EASY) CHECK(!sw->active);
            if (sw->active) {
                months++;
                sum += sw->month;
                CHECK_EQ(sw->reasons, sw->jobs + sw->homes + sw->content);
                CHECK(term == -sw->pull);
                CHECK(IABS(sw->pull - expect) <= IMAX(2, expect / 20));   /* stats move a little between the cycle and here */
            } else CHECK_EQ(term, 0);
        }
        test_log("%s: %d Sweden months, %d residents left with the recruiters (sub-total %d)", diff_name(lv), months, sum, hard_sweden()->total);
        if (lv == DIFF_EASY) { CHECK_EQ(months, 0); CHECK_EQ(hard_sweden()->total, 0); }
        else { CHECK_EQ(months, 24); CHECK_EQ(hard_sweden()->total, sum); CHECK(sum > 0); }
    }
    if (f) fclose(f);
}

/* era expectations: hard_expect_phase by date; on Normal the library line shows in 1974, blocks upgrades from 1975,
   standing tier-3 homes keep their tier through 1975 (grace) and lose patience from 1976 */
TEST(d1_era_expectations)
{
    BalRun r;
    int i, y, t3_75 = 0, bit_75 = 0, bit_76 = 0, lack_76 = 0, item74 = 0, item75 = 0;
    diff_set_level(DIFF_EASY);
    CHECK_EQ(hard_expect_phase(HX_LIBRARY, ZONE_HOUSES, 3, 1980, 0), 0);
    diff_set_level(DIFF_NORMAL);
    CHECK_EQ(hard_expect_phase(HX_LIBRARY, ZONE_HOUSES, 3, 1973, 11), 0);
    CHECK_EQ(hard_expect_phase(HX_LIBRARY, ZONE_HOUSES, 3, 1974, 0), 1);
    CHECK_EQ(hard_expect_phase(HX_LIBRARY, ZONE_HOUSES, 3, 1975, 11), 2);
    CHECK_EQ(hard_expect_phase(HX_LIBRARY, ZONE_HOUSES, 3, 1976, 0), 3);
    CHECK_EQ(hard_expect_phase(HX_LIBRARY, ZONE_HOUSES, 2, 1976, 0), 0);
    CHECK_EQ(hard_expect_phase(HX_LIBRARY, ZONE_APARTMENTS, 3, 1976, 0), 3);
    CHECK_EQ(hard_expect_phase(HX_CLINIC, ZONE_APARTMENTS, 2, 1979, 5), 1);
    CHECK_EQ(hard_expect_phase(HX_CLINIC, ZONE_HOUSES, 3, 1985, 5), 0);
    diff_set_level(DIFF_EASY);
    s_level = DIFF_NORMAL;
    bal_begin(BS_NAIVE, 0, &r, 0);                /* the naive player does not build libraries ahead */
    s_level = DIFF_EASY;
    for (y = 1955; y < 1977; y++) {
        bal_months(&r, 12);
        for (i = 1; i < g_world.lot_hi; i++) {
            const Lot *l = &g_world.lots[i];
            const BuildingDef *d = def_building(l->def);
            LotInfo li;
            int k;
            if (l->def == B_NONE || d->cat != BCAT_ZONE || l->status != LOT_ACTIVE || d->zone != ZONE_HOUSES) continue;
            if (sim_date()->year == 1975 && d->tier == 2 && sim_query_lot(i, &li))    /* January 1975, the T2 -> T3 checklist */
                for (k = 0; k < li.next.n; k++) if (li.next.item[k].req == REQ_LIBRARY && li.next.item[k].why != 3) item75++;
            if (sim_date()->year == 1974 && d->tier == 2 && sim_query_lot(i, &li))
                for (k = 0; k < li.next.n; k++) if (li.next.item[k].req == REQ_LIBRARY && li.next.item[k].why == 3 && li.next.item[k].met) item74++;
            if (d->tier != 3) continue;
            if (sim_date()->year == 1976) { t3_75++; if (l->problems & (1u << REQ_LIBRARY)) bit_75++; }   /* December 1975 books */
        }
        if (sim_date()->year == 1976) {            /* six more months: the grace is over */
            bal_months(&r, 6);
            for (i = 1; i < g_world.lot_hi; i++) {
                const Lot *l = &g_world.lots[i];
                const BuildingDef *d = def_building(l->def);
                int x0, y0, x1, y1;
                if (l->def == B_NONE || d->cat != BCAT_ZONE || d->tier != 3 || l->status != LOT_ACTIVE || d->zone != ZONE_HOUSES) continue;
                if (l->problems & (1u << REQ_LIBRARY)) bit_76++;
                world_lot_rect(i, &x0, &y0, &x1, &y1);
                if (net_cov_q_map(SVC_LIBRARY)[(((y0 + y1) >> 1) >> FIELD_SHIFT) * FIELD_MAX_W + (((x0 + x1) >> 1) >> FIELD_SHIFT)] < 20) lack_76++;
            }
            break;
        }
    }
    test_log("Normal naive: 1974 T2 checklists with \"library (expected from 1975)\" %d; 1975 real library items %d; Jan 1976 T3 houses %d,"
             " library-short %d; mid-1976 T3 houses without library cover %d, flagged %d", item74, item75, t3_75, bit_75, lack_76, bit_76);
    CHECK(item74 > 0 && item75 > 0);
    CHECK_EQ(bit_75, 0);                          /* standing tiers keep it through the grace year */
    CHECK_EQ(bit_76, lack_76);                    /* after it, every uncovered tier-3 home has the library problem */
}

/* 1.1 B1 (HANDOFF D1 open issue): the lot query's decline countdown uses process_lot's threshold: a lot short only of power
   and/or water counts down from decline_util_checks (Normal 8 weeks), any other cause from decline_checks (4). The naive
   player's Normal town 1955-1975 has brownout winters to look at. */
TEST(b1_decline_countdown_utility)
{
    BalRun r;
    int m, i, util = 0, other = 0, bad = 0;
    s_level = DIFF_NORMAL;
    bal_begin(BS_NAIVE, 0, &r, 0);
    for (m = 0; m < 20 * 12; m++) {
        bal_months(&r, 1);
        if (!season_is_winter()) continue;
        for (i = 1; i < g_world.lot_hi; i++) {
            const Lot *l = &g_world.lots[i];
            LotInfo li;
            if (l->def == B_NONE || l->growth >= 0 || def_building(l->def)->cat != BCAT_ZONE || !sim_query_lot(i, &li)) continue;
            if (li.growth_threshold == -diff()->decline_util_checks) {
                util++;
                if (net_lot_power_state(i) != NLS_SHORT && net_lot_water_state(i) != NLS_SHORT) bad++;   /* must be a utility lot */
            } else if (li.growth_threshold == -diff()->decline_checks) other++;
            else bad++;
        }
    }
    bal_end(&r);
    s_level = DIFF_EASY;
    test_log("winter lots counting down 1955-1975 (Normal naive): %d on the power/water threshold (%d weeks), %d on the other (%d), %d wrong",
             util, diff()->decline_util_checks, other, diff()->decline_checks, bad);
    CHECK(util > 0);
    CHECK_EQ(bad, 0);
}
