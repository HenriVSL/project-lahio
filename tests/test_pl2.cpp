/* test_pl2.cpp - PL2 (polish wave): gameplay fixes and balance. Owner: PL2.
   - railway on every new map (mapgen option, New Game default) and the road fallback for Valtion Metalli / the station
   - the apartment tier-2 gate (water tower + school, no elements), elements for tier 3
   - the clearing agreement waits for its card instead of the Ministry signing near elections
   - debt: the emergency loan is recoverable
   Logs in tests/out/pl2_*.txt. Run alone: make -f tests/Makefile.mac T=pl2 */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "budget.h"
#include "tools.h"
#include "traffic.h"
#include "mapgen.h"
#include "politics.h"
#include "complex.h"
#include "transit.h"
#include "trade.h"
#include "era.h"
#include "civic.h"
#include "text.h"

static void pl2_init(void)
{
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
}

static int s_pl2_level = DIFF_EASY;     /* 1.1 B1: the level of the next pl2_new_city (the level replays) */
static void pl2_new_city(u32 seed, int preset, int railway, int politics)
{
    MapGenParams p;
    SimSettings s;
    pl2_init();
    mapgen_defaults(&p);
    p.seed = seed; p.preset = preset; p.railway = railway;
    mapgen_generate(&p);
    sim_default_settings(&s);
    s.seed = seed; s.politics = politics;
    s.difficulty = s_pl2_level;
    sim_new_city(&s);
    tools_reset();
    tools_script_reset();
}

static int rail_tiles(void)
{
    int rn[ROAD_CLASS_COUNT];
    world_road_stats(rn, NULL, NULL);
    return rn[ROAD_RAIL];
}

/* item 9 (1): with the option on (the New Game default since PL2) every map of every preset gets its railway */
TEST(pl2_railway_on_every_map)
{
    FILE *f = fopen("out/pl2_railway.txt", "w");
    int preset, seed, miss = 0, n = 0, shortl = 0;
    for (preset = 0; preset < MAP_PRESET_COUNT; preset++)
        for (seed = 1; seed <= 24; seed++) {
            const MapGenReport *r;
            pl2_new_city((u32)seed, preset, 1, 0);
            r = mapgen_report();
            n++;
            if (!r->railway || rail_tiles() < 20) {
                miss++;
                test_log("no railway: %s seed %d (%d rail tiles)", mapgen_preset_name(preset), seed, rail_tiles());
            } else if (rail_tiles() < 64) shortl++;   /* a corner strip (roads can't cross track: mapgen keeps it by an edge) */
            if (f) fprintf(f, "%-9s seed %2d: railway %d, %3d rail tiles, %d,%d -> %d,%d\n", mapgen_preset_name(preset), seed,
                           r->railway, rail_tiles(), r->rail_x0, r->rail_y0, r->rail_x1, r->rail_y1);
        }
    test_log("%d of %d maps have a railway, %d of them a short corner line", n - miss, n, shortl);
    if (f) { fprintf(f, "%d of %d maps have a railway, %d of them a short corner line\n", n - miss, n, shortl); fclose(f); }
    CHECK_EQ(miss, 0);
}

/* item 9 (2, 3): Valtion Metalli by a main road when the map has no railway, the station explains itself */
static int vm_scan(int *ok, int *x_ok, int *y_ok)
{
    int x, y, w, h, nmain = 0;
    Money c;
    complex_size(CPX_METAL, &w, &h);
    *ok = 0;
    for (y = 1; y + h < g_world.h - 1; y++)
        for (x = 1; x + w < g_world.w - 1; x++) {
            int r = complex_check_place(CPX_METAL, x, y, &c);
            if (r == PLACE_OK || r == REJ_CPX_ROAD) { if (!*ok) { *x_ok = x; *y_ok = y; } (*ok)++; }   /* by the track/road (an access road may still be needed) */
            else if (r == REJ_CPX_MAIN_ROAD) nmain++;
        }
    return nmain;
}

static int main_road_near(int x0, int y0, int x1, int y1, int cls)
{
    int x, y;
    for (y = y0 - 2; y <= y1 + 2; y++)
        for (x = x0 - 2; x <= x1 + 2; x++) if (world_in(x, y) && TILE_ROAD(WT(x, y)) == cls) return 1;
    return 0;
}

TEST(pl2_metal_and_station_without_rail)
{
    SimSettings s;
    int ok = 0, xo = 0, yo = 0, w, h, nmain, wait, r;
    char line[64];
    /* s7 without the railway (the test maps' default and the play-test's kind of map) */
    pl2_new_city(7, MAP_PRESET_LAKELAND, 0, 0);
    s = *sim_settings(); s.all_unlocked = 1; sim_new_city(&s);
    CHECK_EQ(complex_map_has_rail(), 0);
    complex_size(CPX_METAL, &w, &h);
    nmain = vm_scan(&ok, &xo, &yo);
    test_log("no rail: %d Valtion Metalli sites OK (first %d,%d), %d refused for want of a main road", ok, xo, yo, nmain);
    CHECK(ok > 0);
    CHECK(nmain > 0);
    if (ok) CHECK(main_road_near(xo, yo, xo + w - 1, yo + h - 1, ROAD_MAIN));
    CHECK(str_ieq(text_place_reason(REJ_CPX_MAIN_ROAD), "?") == 0);
    /* the station: refused with "no railway on this map" wherever it goes */
    str_printf(line, sizeof(line), "build station %d %d", xo, yo);
    r = tools_script_line(line, &wait, NULL);
    test_log("station on a rail-less map: result %d, reason %d (%s)", r, tools_last_reason(), text_place_reason(tools_last_reason()));
    CHECK_EQ(tools_last_reason(), REJ_NO_RAILWAY);
    /* the same map with the railway: only a site by the track will do */
    pl2_new_city(7, MAP_PRESET_LAKELAND, 1, 0);
    s = *sim_settings(); s.all_unlocked = 1; sim_new_city(&s);
    CHECK_EQ(complex_map_has_rail(), 1);
    nmain = vm_scan(&ok, &xo, &yo);
    test_log("rail: %d sites OK (first %d,%d), %d main-road refusals", ok, xo, yo, nmain);
    CHECK(ok > 0);
    CHECK_EQ(nmain, 0);
    if (ok) CHECK(main_road_near(xo, yo, xo + w - 1, yo + h - 1, ROAD_RAIL));
}

/* the play-test save: loads, taxes and loan state (KAUP_DUMPSAVE-free: PT2_1992.KAU in the project root) */
#include "save.h"
TEST(pl2_pt2_save_plays_on)
{
    SaveMeta meta;
    s32 pop0, m0;
    int k;
    FILE *f;
    pl2_init();
    if (!load_game("../PT2_1992.KAU", &meta)) { test_log("PT2_1992.KAU: %s (skipped)", save_last_error()); return; }
    pop0 = sim_stats()->population; m0 = g_budget.money;
    test_log("PT2 1992: pop %d, money %d, loan %d (rate %d), tax %d/%d, status %d, rail %d, overtime fire %d police %d library %d school %d",
             pop0, m0, g_budget.loan, g_budget.loan_rate, g_budget.tax_res, g_budget.tax_bus, sim_stats()->town_status,
             complex_map_has_rail(), budget_overtime_pct(SVC_FIRE), budget_overtime_pct(SVC_POLICE), budget_overtime_pct(SVC_LIBRARY),
             budget_overtime_pct(SVC_SCHOOL));
    f = fopen("out/pl2_pt2_playon.txt", "w");
    for (k = 0; k < 10; k++) {
        sim_run_months(12);
        if (f) fprintf(f, "%d pop %d money %d net %+d loan %d apt T1/T2/T3 %d/%d/%d\n", sim_date()->year, sim_stats()->population,
                       g_budget.money, budget_net_last(), g_budget.loan, sim_stats()->lots_tier[ZONE_APARTMENTS][1],
                       sim_stats()->lots_tier[ZONE_APARTMENTS][2], sim_stats()->lots_tier[ZONE_APARTMENTS][3]);
    }
    if (f) fclose(f);
    test_log("PT2 +10 years (%d): pop %d, money %d, net %+d, loan %d", sim_date()->year, sim_stats()->population, g_budget.money,
             budget_net_last(), g_budget.loan);
    CHECK(sim_stats()->population * 10 >= pop0 * 8);       /* plays on without a collapse */
    CHECK(g_budget.loans_taken <= 2 || g_budget.money > -20000);
}

/* item 7a: everything a tier-2 block needs unlocks at Church village (the element factory from 1960) */
static int has_req(int zone, int tier, int req)
{
    const TierReq *r;
    int n = def_tier_reqs(zone, tier, &r), k;
    for (k = 0; k < n; k++) if (r[k].req == req) return 1;
    return 0;
}

TEST(pl2_apartment_tier2_gate)
{
    CHECK(has_req(ZONE_APARTMENTS, 2, REQ_WATER_TOWER) && has_req(ZONE_APARTMENTS, 2, REQ_ELEMENTS) && has_req(ZONE_APARTMENTS, 2, REQ_SCHOOL));
    CHECK(!has_req(ZONE_APARTMENTS, 2, REQ_HEAT) && has_req(ZONE_APARTMENTS, 3, REQ_HEAT));
    CHECK(def_zone_unlocked(ZONE_APARTMENTS, 1956, TS_CHURCH_VILLAGE, 0));
    CHECK(def_unlocked(B_WATER_TOWER, 1956, TS_CHURCH_VILLAGE, 0));
    CHECK(!def_unlocked(B_ELEMENT_FACTORY, 1959, TS_CHURCH_VILLAGE, 0));
    CHECK(def_unlocked(B_ELEMENT_FACTORY, 1960, TS_CHURCH_VILLAGE, 0));
    CHECK(!def_unlocked(B_ELEMENT_FACTORY, 1965, TS_VILLAGE, 0));
    CHECK(!def_unlocked(B_HEAT_PLANT, 1965, TS_CHURCH_VILLAGE, 0));       /* tier 3 stays a Market-town step */
}

/* item 10: an emergency loan is paid back in instalments while the treasury holds 2,000+ (was: only above 15,000) */
TEST(pl2_loan_instalments)
{
    int k;
    Money loan0;
    pl2_new_city(7, MAP_PRESET_LAKELAND, 0, 0);
    g_budget.loan = 30000; g_budget.money = 8000;
    loan0 = g_budget.loan;
    for (k = 0; k < 12; k++) { budget_month_end(); if (g_budget.money < 3000) g_budget.money = 8000; }
    test_log("loan 30,000 with 8,000 in the treasury: %d after a year (rate %d per mille)", g_budget.loan, g_budget.loan_rate);
    CHECK(g_budget.loan < loan0 - 4000);                 /* ~1/60 a month */
    CHECK_EQ(g_budget.loan_rate, LOAN_RATE);
    g_budget.loan = 30000; g_budget.money = 1500;
    budget_month_end();
    CHECK(g_budget.loan == 30000);                       /* no instalment below the floor */
}

/* the recorded 1955-1990 bot scripts (tests/out/bal_<style>_s7_era.txt, written by balance_era_1955_1990; copies in
   scripts/ for the VM: lahio.exe -seed 7 -preset lakeland -script scripts\bal_naive_s7_era.txt -fast) replay headless on
   the Mac to the same town as the bot run: the VM must print the same pop/money (out/pl2_replay.txt) */
static void replay(const char *name, FILE *log)
{
    char path[96];
    FILE *out;
    int errors;
    pl2_new_city(7, MAP_PRESET_LAKELAND, 0, 0);
    str_printf(path, sizeof(path), "out/%s", name);
    out = fopen("out/pl2_replay_out.txt", "w");
    tools_script_set_dir("out/");
    errors = tools_script_run_file(path, out);
    sim_statlog_close();
    tools_script_set_dir("");
    if (out) fclose(out);
    test_log("%s: %d errors, %d-%02d pop %d money %d loan %d status %d, world %08x", name, errors, sim_date()->year,
             sim_date()->month + 1, sim_stats()->population, g_budget.money, g_budget.loan, sim_stats()->town_status, world_hash());
    if (log) fprintf(log, "%s: %d errors, %d-%02d pop %d money %d loan %d status %d, world %08x\n", name, errors, sim_date()->year,
                     sim_date()->month + 1, sim_stats()->population, g_budget.money, g_budget.loan, sim_stats()->town_status, world_hash());
    if (errors < 0) return;
    CHECK_EQ(errors, 0);
}

TEST(pl2_era_scripts_replay)
{
    FILE *log = fopen("out/pl2_replay.txt", "w");
    replay("bal_planner_s7_era.txt", log);
    replay("bal_naive_s7_era.txt", log);
    s_pl2_level = DIFF_NORMAL;               /* 1.1 B1: the level scripts (VM: -difficulty normal|hard) */
    replay("bal_planner_s7_era_normal.txt", log);
    replay("bal_naive_s7_era_normal.txt", log);
    s_pl2_level = DIFF_HARD;
    replay("bal_planner_s7_era_hard.txt", log);
    replay("bal_naive_s7_era_hard.txt", log);
    s_pl2_level = DIFF_EASY;
    if (log) fclose(log);
}
