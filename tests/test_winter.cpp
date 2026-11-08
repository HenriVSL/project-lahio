/* test_winter.cpp - 1.1 W1 winter simulation (season.h, DESIGN_1_1 section 3): calendar + the art hook, hard-winter rules
   and schedule per level, power/heat peaks per level, the T3 heat check against the winter peak (no flip-flop), hard frost
   on pumps/wells + frozen-pipe reports, the Winter ledger line, Easy = no-op, and the planner replay 1955-1990 on every
   level (Easy = the 1.0 numbers; Normal/Hard winter effects in the CSV; determinism + save round trip mid-winter).
   Owner: W1. Dumps: tests/out/winter_*.txt|csv. */
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
#include "save.h"
#include "text.h"
#include "civic.h"
#include "mapgen.h"
#include "era.h"
#include "advice.h"
#include "season.h"

static void winit(void)
{
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
}
static void wnew(int level, int disasters)
{
    SimSettings s;
    winit();
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = 77;
    s.disasters = disasters;
    s.difficulty = level;
    sim_new_city(&s);
    tools_reset();
    tools_set_pipes_with_roads(0);
}

/* tests only: put the sim date somewhere (the season is a pure function of it) */
static void set_date(int year, int month)
{
    SimDate *d = (SimDate *)sim_date();
    d->year = year; d->month = month;
}
static int lot(int def, int x, int y) { return world_lot_place(def, x, y, ROT_0, 0, LOT_ACTIVE); }
static void solve(void) { net_mark_dirty(NET_POWER | NET_WATER); net_solve_all(); civic_step(9, 0); }
static void drain_msgs(FILE *f)
{
    SeasonMsg m;
    char b[256];
    int sev;
    while (season_poll_msg(&m)) if (season_msg_text(b, sizeof(b), &m, &sev) && f) fprintf(f, "  [%d] %s\n", sev, b);
}

/* ------------------------------------------------------------------ calendar */
TEST(winter_calendar_and_art_hook)
{
    int m, k, flips = 0, wins = 0;
    for (m = 0; m < 12; m++) wins += season_month_is_winter(m);
    CHECK_EQ(wins, 4);
    CHECK(season_month_is_winter(11) && season_month_is_winter(0) && season_month_is_winter(2));
    CHECK(!season_month_is_winter(3) && !season_month_is_winter(10));
    CHECK_EQ(season_winter_year(1987, 1), 1986);
    CHECK_EQ(season_winter_year(1986, 11), 1986);
    CHECK_EQ(season_winter_year(1986, 10), 1986);
    wnew(DIFF_EASY, 1);                                    /* January 1955: winter */
    season_changed();                                      /* sync the poller */
    CHECK(season_is_winter());
    CHECK(!season_changed());
    for (k = 0; k < 24; k++) {                             /* two years of month ticks: Apr + Dec flips each year */
        sim_run_months(1);
        if (season_changed()) { flips++; CHECK(sim_date()->month == 3 || sim_date()->month == 11); }
    }
    test_log("24 months from Jan 1955: %d season flips (now %d-%02d, winter %d)", flips, sim_date()->year, sim_date()->month + 1,
             season_is_winter());
    CHECK_EQ(flips, 4);
    drain_msgs(NULL);
}

/* ------------------------------------------------------------------ hard winters */
TEST(winter_hard_rules_per_level)
{
    int fx;
    wnew(DIFF_EASY, 1);
    CHECK(!season_hard_rule(1960, &fx) && !season_hard_rule(1986, &fx));    /* Easy: never */
    wnew(DIFF_NORMAL, 1);
    CHECK(season_hard_rule(1960, &fx) && !fx);
    CHECK(season_hard_rule(1986, &fx) && fx);              /* the fixed 1986-87 */
    CHECK(!season_hard_rule(1968, &fx) && !season_hard_rule(1969, &fx) && !season_hard_rule(1970, &fx));   /* Sweden winters */
    CHECK(!season_hard_rule(1973, &fx));                   /* the first oil-shock winter */
    CHECK(!season_hard_rule(1985, &fx));                   /* not right before the fixed one */
    CHECK(season_hard_rule(1971, &fx) && season_hard_rule(1974, &fx));
    set_date(1960, 0); CHECK_EQ(season_fire_pct(), 150);   /* wood stoves Dec-Mar (D1's fire_winter_pct) */
    set_date(1960, 6); CHECK_EQ(season_fire_pct(), 100);
    wnew(DIFF_HARD, 1);
    CHECK(season_hard_rule(1968, &fx) && season_hard_rule(1973, &fx) && season_hard_rule(1985, &fx));   /* Hard does not skip */
    wnew(DIFF_HARD, 0);
    CHECK(!season_hard_rule(1960, &fx) && !season_hard_rule(1986, &fx));    /* disasters off: no hard winters */
}

/* the forecast on 1 November for 1955-1989 (no sim run: the month-end hook alone), per level and seed: deterministic,
   the stacking rules hold, 1986 is hard; rates near the table's 20 % / 33 % */
static int schedule(int level, int seed, int disasters, char *out, int size)
{
    SimSettings s;
    int y, n = 0, prev = 0, ok = 1;
    wnew(level, disasters);
    sim_default_settings(&s);
    s.seed = (u32)seed; s.difficulty = level; s.disasters = disasters;
    sim_new_city(&s);
    out[0] = 0;
    for (y = 1955; y <= 1989; y++) {
        set_date(y, 10); season_month();
        set_date(y, 11); season_month();
        if (season_forecast() == SFC_HARD) {
            char t[8];
            str_printf(t, sizeof(t), " %02d", y % 100);
            if ((int)strlen(out) + 4 < size) strcat(out, t);
            n++;
            if (level == DIFF_NORMAL && (prev == y - 1 || (y >= 1968 && y <= 1970) || y == 1973)) ok = 0;
            prev = y;
        }
        set_date(y + 1, 3); season_month();
    }
    drain_msgs(NULL);
    return ok ? n : -1;
}
TEST(winter_forecast_schedule_deterministic)
{
    char a[200], b[200];
    int seed, n, tot[DIFF_COUNT];
    FILE *f = fopen("out/winter_schedule.txt", "w");
    memset(tot, 0, sizeof(tot));
    for (seed = 1; seed <= 8; seed++) {
        int lv;
        for (lv = 0; lv < DIFF_COUNT; lv++) {
            n = schedule(lv, seed, 1, a, sizeof(a));
            CHECK(n >= 0);                                 /* Normal's stacking rules */
            CHECK_EQ(schedule(lv, seed, 1, b, sizeof(b)), n);
            CHECK(!strcmp(a, b));                          /* same seed, same winters */
            if (lv == DIFF_EASY) CHECK_EQ(n, 0);
            else CHECK(strstr(a, " 86") != NULL);
            tot[lv] += n;
            if (f) fprintf(f, "seed %d %-6s %2d hard winters:%s\n", seed, diff_name(lv), n, a);
        }
        CHECK_EQ(schedule(DIFF_HARD, seed, 0, a, sizeof(a)), 0);   /* disasters off */
    }
    test_log("hard winters 1955-89 over 8 seeds: Easy %d, Normal %d (%.0f%%), Hard %d (%.0f%%)", tot[0], tot[1],
             tot[1] * 100.0 / (8 * 35), tot[2], tot[2] * 100.0 / (8 * 35));
    if (f) fclose(f);
    CHECK(tot[1] >= 8 * 35 * 10 / 100 && tot[1] <= 8 * 35 * 25 / 100);
    CHECK(tot[2] >= 8 * 35 * 24 / 100 && tot[2] <= 8 * 35 * 42 / 100);
    CHECK(tot[2] > tot[1]);
}

/* ------------------------------------------------------------------ power: winter load per level */
/* a coal plant (500) and 14 point blocks (32 each = 448) in a row: summer fine; Normal +15 % = 515 -> the furthest block
   goes dark; hard winters take more */
static int power_served(int level, int month, int hard, s32 *demand)
{
    int i, ids[14], n = 0;
    wnew(level, 1);
    lot(B_COAL_PLANT, 2, 10);
    for (i = 0; i < 14; i++) ids[i] = lot(B_APT_T3_2X2, 4 + 2 * i, 10);
    set_date(1960, month);
    season_test_force(season_winter_year(1960, month), hard);
    solve();
    for (i = 0; i < 14; i++) if (net_lot_power_state(ids[i]) == NLS_OK) n++;
    if (demand) *demand = net_stats(NET_POWER)->demand;
    return n;
}
TEST(winter_power_peak_per_level)
{
    s32 d0, d1, d2, d3, d4, d5;
    int s0 = power_served(DIFF_NORMAL, 6, 0, &d0), s1 = power_served(DIFF_EASY, 0, 0, &d1);
    int s2 = power_served(DIFF_NORMAL, 0, 0, &d2), s3 = power_served(DIFF_HARD, 0, 0, &d3);
    int s4 = power_served(DIFF_NORMAL, 0, 1, &d4), s5 = power_served(DIFF_HARD, 0, 1, &d5);
    test_log("14 blocks x 32 on a 500 plant: Normal July %d (demand %d), Easy Jan %d (%d), Normal Jan %d (%d), Hard Jan %d (%d),"
             " Normal hard Jan %d (%d), Hard hard Jan %d (%d)", s0, d0, s1, d1, s2, d2, s3, d3, s4, d4, s5, d5);
    CHECK_EQ(s0, 14); CHECK_EQ(d0, 448);
    CHECK_EQ(s1, 14); CHECK_EQ(d1, 448);                   /* Easy: cosmetic winter */
    CHECK_EQ(s2, 13); CHECK_EQ(d2, 515);                   /* 448 x 115 %: 13 x 36.8 = 478 fits, 14 doesn't */
    CHECK_EQ(s3, 13); CHECK_EQ(d3, 538);                   /* x 120 % */
    CHECK_EQ(s4, 12); CHECK_EQ(d4, 582);                   /* x 130 %: 12 x 41.6 = 499 */
    CHECK_EQ(s5, 11); CHECK_EQ(d5, 627);                   /* x 140 %: 11 x 44.8 = 493 */
    {   /* tooltips and the plant query (Normal, January / October) */
        char b[96];
        int warn;
        power_served(DIFF_NORMAL, 0, 0, NULL);
        CHECK(season_tip_line(STIP_POWER, b, sizeof(b), &warn)); test_log("Jan tooltip: %s", b);
        CHECK(strstr(b, "+15%") != NULL);
        power_served(DIFF_NORMAL, 9, 0, NULL);
        CHECK(season_tip_line(STIP_POWER, b, sizeof(b), &warn)); test_log("Oct tooltip: %s (warn %d)", b, warn);
        CHECK(strstr(b, "~515 / 500") != NULL && warn);
        {
            Advice v;
            CHECK(advice_find(ADV_WINTER_POWER, &v)); test_log("Oct advice: %s", v.text);
            CHECK(strstr(v.text, "515") && strstr(v.text, "500"));
        }
        power_served(DIFF_NORMAL, 6, 0, NULL);
        CHECK(!season_tip_line(STIP_POWER, b, sizeof(b), &warn));   /* July: no line */
        CHECK(!advice_find(ADV_WINTER_POWER, NULL));
        power_served(DIFF_EASY, 9, 0, NULL);
        CHECK(!season_tip_line(STIP_POWER, b, sizeof(b), &warn));   /* Easy: never */
        CHECK(!advice_find(ADV_WINTER_POWER, NULL));
    }
}

/* ------------------------------------------------------------------ heat: winter peak + the T3 check */
static int heat_setup(int level, int month, int nblocks, int *t2)
{
    int hp, i;
    wnew(level, 1);
    hp = lot(B_HEAT_PLANT, 2, 10);
    for (i = 0; i < nblocks; i++) lot(B_APT_T3_2X2, 4 + 2 * i, 10);   /* 32 heat units each, a touching row */
    *t2 = lot(B_APT_T2_2X2, 4 + 2 * nblocks, 10);                      /* 20 units, would take 32 at T3 */
    set_date(1965, month);
    season_test_force(season_winter_year(1965, month), 0);
    solve();
    return hp;
}
static int heat_req_met(int id)
{
    LotInfo li;
    int k;
    if (!sim_query_lot(id, &li)) return -1;
    for (k = 0; k < li.next.n; k++) if (li.next.item[k].req == REQ_HEAT) return li.next.item[k].met;
    return -2;
}
TEST(winter_heat_peak_and_no_flip_flop)
{
    int t2, hp, i, served;
    UtilSummary us;
    UtilSrcInfo si;
    s32 spare, peak;
    /* 16 blocks (512) + the T2 (20) = 532 of 600: summer spare 68 >= the upgrade's +12, but the winter peak (x115 % =
       612) has none: 1.0 rules would upgrade in July and lose the block in January */
    hp = heat_setup(DIFF_NORMAL, 6, 16, &t2);
    spare = civic_heat_spare_for(t2); peak = civic_heat_spare_peak(t2);
    test_log("Normal July: heat spare %d now, %d at the winter peak; T3 heat req met %d", spare, peak, heat_req_met(t2));
    CHECK_EQ(spare, 68);
    CHECK(peak < 12);
    CHECK_EQ(heat_req_met(t2), 0);                         /* no upgrade in July */
    hp = heat_setup(DIFF_EASY, 6, 16, &t2);
    CHECK_EQ(civic_heat_spare_peak(t2), civic_heat_spare_for(t2));
    CHECK_EQ(heat_req_met(t2), 1);                         /* Easy = 1.0: it upgrades */
    /* January, Normal: the heat use is x115 %, the furthest blocks go cold; numbers shown at winter use */
    hp = heat_setup(DIFF_NORMAL, 0, 16, &t2);
    civic_util_summary(UT_HEAT, &us);
    CHECK(civic_util_source(UT_HEAT, hp, &si));
    for (i = 1, served = 0; i < g_world.lot_hi; i++) if (civic_lot_util_state(UT_HEAT, i) == NLS_OK && civic_lot_util_use(UT_HEAT, i) > 0) served++;
    test_log("Normal January: heat demand %d / supply %d, used %d, lots served %d short %d; plant used %d spare %d",
             us.demand, us.supply, us.used, us.lots_served, us.lots_short, si.used, si.spare);
    CHECK_EQ(us.demand, season_scale(532, 115));
    CHECK(us.lots_short >= 1);
    CHECK(si.used <= si.output);
    CHECK_EQ(heat_req_met(t2), 0);
    {
        char b[96];
        int warn;
        set_date(1965, 9);
        CHECK(season_tip_line(STIP_HEAT, b, sizeof(b), &warn)); test_log("Oct heat tooltip: %s", b);
    }
}

/* ------------------------------------------------------------------ water: hard frost, frozen pipes */
TEST(winter_hard_frost_frozen_pipes)
{
    int w, i, ids[12], dry;
    WaterSrcInfo wi;
    SeasonMsg m;
    char b[256];
    int sev, got = 0;
    for (i = 0; i < 2; i++) {
        int lv = i ? DIFF_HARD : DIFF_NORMAL, k;
        wnew(lv, 1);
        w = lot(B_WELL, 3, 10);
        for (k = 0; k < 12; k++) ids[k] = lot(B_APT_T1_2X2, 4 + 2 * k, 10);   /* 12 water units each = 144 of 150 */
        set_date(1966, 6); solve();
        CHECK(net_water_source(w, &wi)); CHECK_EQ(wi.output, 150); CHECK_EQ(wi.lots_dry, 0);
        set_date(1966, 0); season_test_force(1965, 0); solve();      /* ordinary winter: water untouched */
        CHECK(net_water_source(w, &wi)); CHECK_EQ(wi.output, 150);
        season_test_force(1965, 1); solve();                         /* hard winter: 90 / 85 % */
        CHECK(net_water_source(w, &wi));
        for (k = 0, dry = 0; k < 12; k++) if (net_lot_water_state(ids[k]) == NLS_SHORT) dry++;
        test_log("%s hard frost: well output %d, dry %d (furthest first: last %d)", diff_name(lv), wi.output, dry,
                 net_lot_water_state(ids[11]));
        CHECK_EQ(wi.output, lv == DIFF_HARD ? 128 : 135);
        CHECK_EQ(dry, lv == DIFF_HARD ? 2 : 1);
        CHECK_EQ(net_lot_water_state(ids[11]), NLS_SHORT);
        CHECK(season_lot_line(w, b, sizeof(b))); test_log("well query: %s", b);
        /* the month-end report: 1 February (January was hard) */
        while (season_poll_msg(&m)) {}
        set_date(1966, 1); season_month();
        while (season_poll_msg(&m))
            if (m.type == SM_FROZEN && season_msg_text(b, sizeof(b), &m, &sev)) { test_log("%s", b); got++; CHECK(strstr(b, "Pipes frozen in")); }
        CHECK(season_stats()->frozen_lots == dry);
        CHECK(season_suppress_event(SE_WATER_SHORTAGE));
    }
    CHECK_EQ(got, 2);
}

/* ------------------------------------------------------------------ the Winter ledger line */
static s32 proj_line(int id)
{
    Term t[MAX_TERMS * 2];
    int n = budget_projection(t, COUNTOF(t)), k;
    for (k = 0; k < n; k++) if (t[k].id == id) return t[k].amount;
    return 0;
}
TEST(winter_ledger_line)
{
    int x, lv;
    s32 roads, win, pipes;
    for (lv = 0; lv < DIFF_COUNT; lv++) {
        wnew(lv, 1);
        for (x = 0; x < 120; x++) { world_set_road(x, 20, ROAD_STREET); world_set_road(x, 30, ROAD_GRAVEL); world_set_pipe(x, 40, 1); world_set_pipe(x, 41, 1); }
        pipes = world_pipe_tiles(NULL);
        set_date(1970, 6);
        CHECK_EQ(proj_line(BL_WINTER), 0);                 /* summer: nothing */
        set_date(1970, 0); season_test_force(1969, 0);
        roads = -proj_line(BL_ROADS); win = -proj_line(BL_WINTER);
        test_log("%-6s January ordinary: roads %d mk, winter %d mk (%d%%)", diff_name(lv), roads, win, roads ? win * 100 / roads : 0);
        CHECK_EQ(win, roads * diff()->plough_pct / 100);
        season_test_force(1969, 1);
        win = -proj_line(BL_WINTER);
        test_log("%-6s January hard: winter %d mk (ploughing %d + frost repairs on %d pipe tiles)", diff_name(lv), win,
                 roads * diff()->plough_hard_pct / 100, pipes);
        if (lv == DIFF_EASY) CHECK_EQ(win, 0);
        else CHECK_EQ(win, roads * diff()->plough_hard_pct / 100 + pipes * diff()->frost_repair10 * budget_upkeep_index() / 1000);
        CHECK(strstr(text_term_label(TERMK_BUDGET, BL_WINTER), "Winter") != NULL);
    }
}

/* ------------------------------------------------------------------ Easy: no mechanics */
TEST(winter_easy_is_noop)
{
    int i, ids[14];
    u32 hs, hw;
    s32 dsum, dwin;
    wnew(DIFF_EASY, 1);
    lot(B_COAL_PLANT, 2, 10);
    for (i = 0; i < 14; i++) ids[i] = lot(B_APT_T3_2X2, 4 + 2 * i, 10);
    lot(B_HEAT_PLANT, 2, 20);
    set_date(1960, 6); solve(); hs = world_hash(); dsum = net_stats(NET_POWER)->demand;
    set_date(1961, 0); season_test_force(1960, 1); solve(); hw = world_hash(); dwin = net_stats(NET_POWER)->demand;
    CHECK(!season_mechanics());
    CHECK_EQ(season_power_pct(), 100); CHECK_EQ(season_heat_pct(), 100); CHECK_EQ(season_water_pct(), 100);
    CHECK_EQ(season_peak_pct(), 100); CHECK_EQ(season_fire_pct(), 100);
    CHECK_EQ(season_winter_upkeep(1000, 500, 130), 0);
    CHECK_EQ(season_hash(), 0u);
    CHECK_EQ(hs, hw); CHECK_EQ(dsum, dwin);
    CHECK_EQ(season_news_score(), 0);
    CHECK(!season_hindsight(NULL, 0, NULL, 0));
    CHECK(season_is_winter());                             /* the art still snows */
}

/* ------------------------------------------------------------------ the planner replay 1955-1990 on every level */
typedef struct {
    int pop1990, money1990, loans;
    int hard[40], nhard;
    int brown_months, frozen_months, cold_months, max_dark, max_frozen;
    s32 winter_mk, max_peak_pct;
    u32 hash;
} Replay;

static void month_row(Replay *r, FILE *lines)
{
    const SeasonStats *ss = season_stats();
    const SimDate *d = sim_date();
    if (season_month_is_winter((d->month + 11) % 12)) {
        r->winter_mk += -g_budget.last[BL_WINTER];
        if (ss->dark_lots > 0) r->brown_months++;
        if (ss->frozen_lots > 0) r->frozen_months++;
        if (ss->cold_lots > 0) r->cold_months++;
        if (ss->dark_lots > r->max_dark) r->max_dark = ss->dark_lots;
        if (ss->frozen_lots > r->max_frozen) r->max_frozen = ss->frozen_lots;
    }
    if (d->month == 11 && season_forecast() == SFC_HARD && r->nhard < 40) r->hard[r->nhard++] = d->year;
    {
        const NetStats *p = net_stats(NET_POWER);
        int pct = p->supply > 0 ? (int)((s64)p->demand * 100 / p->supply) : 0;
        if (pct > r->max_peak_pct) r->max_peak_pct = pct;
    }
    if (lines) {
        SeasonMsg m;
        char b[256];
        int sev;
        while (season_poll_msg(&m)) if (season_msg_text(b, sizeof(b), &m, &sev)) fprintf(lines, "%d-%02d [%d] %s\n", d->year, d->month + 1, sev, b);
    } else drain_msgs(NULL);
}

/* replays scripts/bal_planner_s7_era.txt (the PL2 planner, VM replay = Mac) on a level until stop_year (Jan); csv = stat log */
static int replay(int level, int stop_year, const char *csv, FILE *lines, Replay *r)
{
    MapGenParams p;
    SimSettings s;
    FILE *f;
    char line[256];
    int wait, rc, k;
    memset(r, 0, sizeof(*r));
    winit();                                               /* as test_balance.cpp bal_new_city */
    mapgen_defaults(&p);
    p.seed = 7; p.preset = MAP_PRESET_LAKELAND;
    mapgen_generate(&p);
    sim_default_settings(&s);
    s.seed = 7; s.difficulty = level;
    sim_new_city(&s);
    tools_reset();
    tools_script_reset();
    f = fopen("../scripts/bal_planner_s7_era.txt", "r");
    if (!f) return 0;
    if (csv) sim_statlog_open(csv);
    while (fgets(line, sizeof(line), f) && sim_date()->year < stop_year) {
        if (!strncmp(line, "statlog", 7)) continue;
        rc = tools_script_line(line, &wait, NULL);
        if (rc == SCR_WAIT) {
            SimEvent ev;
            for (k = 0; k < wait && sim_date()->year < stop_year; k++) { sim_run_months(1); month_row(r, lines); }
            while (sim_poll_event(&ev)) {}
            tools_month_changed();
        }
    }
    fclose(f);
    if (csv) sim_statlog_close();
    r->pop1990 = sim_stats()->population; r->money1990 = g_budget.money; r->loans = g_budget.loans_taken;
    r->hash = sim_hash();
    return 1;
}

TEST(winter_replay_planner_all_levels)
{
    static Replay R[DIFF_COUNT];
    FILE *f = fopen("out/winter_summary.txt", "w"), *ln = fopen("out/winter_lines.txt", "w");
    int lv, k;
    for (lv = 0; lv < DIFF_COUNT; lv++) {
        char csv[64], hy[200];
        Replay *r = &R[lv];
        str_printf(csv, sizeof(csv), "out/winter_planner_%s.csv", diff_name(lv));
        if (ln) fprintf(ln, "=== %s\n", diff_name(lv));
        CHECK(replay(lv, 9999, csv, ln, r));
        hy[0] = 0;
        for (k = 0; k < r->nhard; k++) { char t[8]; str_printf(t, sizeof(t), " %d", r->hard[k]); strcat(hy, t); }
        test_log("%-6s 1990: pop %d money %d loans %d | hard winters %d:%s | winter mk %d | months dark %d (max %d lots) frozen %d"
                 " (max %d) cold %d | peak power load %d%%", diff_name(lv), r->pop1990, r->money1990, r->loans, r->nhard, hy,
                 r->winter_mk, r->brown_months, r->max_dark, r->frozen_months, r->max_frozen, r->cold_months, r->max_peak_pct);
        if (f) fprintf(f, "%-6s 1990: pop %d money %d loans %d | hard winters %d:%s | Winter ledger 1955-90 %d mk | winter months"
                          " with brownouts %d (max %d lots), frozen pipes %d (max %d), cold radiators %d | peak power load %d%%\n",
                       diff_name(lv), r->pop1990, r->money1990, r->loans, r->nhard, hy, r->winter_mk, r->brown_months, r->max_dark,
                       r->frozen_months, r->max_frozen, r->cold_months, r->max_peak_pct);
    }
    if (f) fclose(f);
    if (ln) fclose(ln);
    /* Easy = 1.0: the PL2 planner's 1990 (BALANCE 9: 12,188 residents, 4,171 mk), no winter at all */
    CHECK_EQ(R[DIFF_EASY].pop1990, 12188);
    CHECK_EQ(R[DIFF_EASY].money1990, 4171);
    CHECK_EQ(R[DIFF_EASY].winter_mk, 0); CHECK_EQ(R[DIFF_EASY].nhard, 0); CHECK_EQ(R[DIFF_EASY].brown_months, 0);
    for (lv = DIFF_NORMAL; lv < DIFF_COUNT; lv++) {
        int has86 = 0;
        for (k = 0; k < R[lv].nhard; k++) if (R[lv].hard[k] == 1986) has86 = 1;
        CHECK(has86);
        CHECK(R[lv].winter_mk > 0);
        CHECK(R[lv].max_peak_pct > R[DIFF_EASY].max_peak_pct);
    }
    CHECK(R[DIFF_HARD].winter_mk > R[DIFF_NORMAL].winter_mk);
}

/* determinism and the save round trip in the middle of a hard winter (Normal, January 1966, forced in November 1965) */
TEST(winter_save_roundtrip_mid_winter)
{
    Replay r;
    SaveMeta sm;
    u32 h1, h2, h3;
    s32 dem;
    int fc;
    CHECK(replay(DIFF_NORMAL, 1965, NULL, NULL, &r));      /* to January 1965 */
    sim_run_months(10); drain_msgs(NULL);                  /* 1 November 1965 */
    season_test_force(1965, 1);
    sim_run_months(2); drain_msgs(NULL);                   /* 1 January 1966: hard winter, the paper is out */
    CHECK(season_hard_now());
    {
        const NewsIssue *n = era_news_kind(NEWS_YEAR);
        int k, found = 0;
        for (k = 0; n && k < NEWS_HEADS; k++) if (strstr(n->head[k], "MINUS 35")) found = 1;
        test_log("January 1966 paper: %s / %s / %s", n ? n->head[0] : "-", n ? n->head[1] : "-", n ? n->head[2] : "-");
        CHECK(found);
    }
    memset(&sm, 0, sizeof(sm));
    CHECK(save_game("out/W1_WINTER.KAU", &sm));
    dem = net_stats(NET_POWER)->demand; fc = season_forecast();
    sim_run_months(5); drain_msgs(NULL);
    h1 = sim_hash();
    CHECK(load_game("out/W1_WINTER.KAU", NULL));
    CHECK_EQ(diff_level(), DIFF_NORMAL);
    CHECK_EQ(season_forecast(), fc); CHECK(season_hard_now()); CHECK_EQ(net_stats(NET_POWER)->demand, dem);
    sim_run_months(5); drain_msgs(NULL);
    h2 = sim_hash();
    CHECK(load_game("out/W1_WINTER.KAU", NULL));
    sim_run_months(5); drain_msgs(NULL);
    h3 = sim_hash();
    test_log("Jan 1966 hard winter, +5 months: unbroken %08x, after load %08x, again %08x (winter cost %d mk)", h1, h2, h3,
             season_stats()->last_cost);
    CHECK_EQ(h1, h2); CHECK_EQ(h2, h3);
    CHECK(season_stats()->last_cost > 0);
    sim_run_months(7); drain_msgs(NULL);                   /* January 1967: the paper looks back at the hard winter */
    {
        const NewsIssue *n = era_news_kind(NEWS_YEAR);
        test_log("January 1967 hindsight: %s: %s", n ? n->hind_head : "-", n ? n->hind : "-");
        CHECK(n && strstr(n->hind_head, "HARD WINTER") && strstr(n->hind, "1965-66"));
    }
}
