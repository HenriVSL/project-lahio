/* test_b1.cpp - 1.1 B1 balance across levels (BALANCE.md section 11): the new table fields keep Easy at 1.0, electric
   heating grows the winter load from 1975 (Normal/Hard), the Normal emergency loan (9 months, +15,000, instalments only
   from the month's surplus), the squared Sweden pull, the 1985 health-centre expectation (a Town's duty) and the new
   Cabinet lines (overtime bill named with its cure, the year's books in the red, a point of tax). Owner: B1. */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "budget.h"
#include "tools.h"
#include "traffic.h"
#include "civic.h"
#include "advice.h"
#include "season.h"
#include "hard.h"

static void binit(void)
{
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
}
static void bnew(int level)
{
    SimSettings s;
    binit();
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = 91;
    s.difficulty = level;
    sim_new_city(&s);
    tools_reset();
    tools_set_pipes_with_roads(0);
}
static void set_date(int year, int month)
{
    SimDate *d = (SimDate *)sim_date();
    d->year = year; d->month = month;
}

TEST(b1_table_easy_is_1_0)
{
    const DiffParams *e = &g_diff[DIFF_EASY], *n = &g_diff[DIFF_NORMAL], *h = &g_diff[DIFF_HARD];
    CHECK_EQ(e->winter_grow_max, 0); CHECK_EQ(e->loan_surplus, 0); CHECK_EQ(e->loan_after, LOAN_AFTER_MONTHS);
    CHECK_EQ(e->loan_cushion100, 100);
    CHECK(n->winter_grow_max > 0 && h->winter_grow_max >= n->winter_grow_max);
    CHECK(n->sweden_permille > 0 && h->sweden_permille >= n->sweden_permille);
    CHECK(h->loan_after <= n->loan_after && h->loan_cushion100 <= n->loan_cushion100);   /* Hard never kinder than Normal */
    test_log("Normal: upkeep x%d%%, index to %d%%, loan after %d months +%d mk, instalments from surplus %d, Sweden %d per mille,"
             " hard winters %d%%, electric heating +%d", n->upkeep_pct, n->upkeep_cap, n->loan_after, n->loan_cushion100 * 100,
             n->loan_surplus, n->sweden_permille, n->hard_winter_pct, n->winter_grow_max);
}

TEST(b1_electric_heating_grows_the_winter_load)
{
    int lv, y;
    for (lv = 0; lv < DIFF_COUNT; lv++) {
        char line[200], *p = line;
        bnew(lv);
        for (y = 1972; y <= 1992; y += 4) {
            set_date(y, 0);                    /* January = the winter of y - 1 */
            p += str_printf(p, (int)(sizeof(line) - (p - line)), " %d:%d", y, season_winter_load());
        }
        test_log("%s winter load by January:%s", diff_name(lv), line);
        set_date(1974, 11); CHECK_EQ(season_winter_load(), g_diff[lv].winter_load_pct);   /* before 1975: the base */
        set_date(1975, 11); CHECK_EQ(season_electric_load(), g_diff[lv].winter_grow_max ? 1 : 0);
        set_date(1976, 1);  CHECK_EQ(season_electric_load(), g_diff[lv].winter_grow_max ? 1 : 0);   /* Feb 1976 = winter 1975 */
        set_date(1990, 0);  CHECK_EQ(season_electric_load(), IMIN(15, (int)g_diff[lv].winter_grow_max));   /* winter 1989 */
        set_date(2010, 0);  CHECK_EQ(season_winter_load(), g_diff[lv].winter_load_pct + g_diff[lv].winter_grow_max);   /* the cap */
        set_date(1990, 6);  CHECK_EQ(season_power_pct(), 100);                                  /* summer: no load */
        if (lv == DIFF_EASY) { set_date(1989, 0); CHECK_EQ(season_power_pct(), 100); }          /* Easy: cosmetic winter */
    }
}

/* the emergency loan by level: Normal waits 9 months, lends the deficit + 15,000 and takes instalments only out of a
   month's operating surplus; Easy (1.0) and Hard take 1/60 whenever the treasury holds 2,000 */
TEST(b1_emergency_loan_by_level)
{
    int lv;
    for (lv = 0; lv < DIFF_COUNT; lv++) {
        int m, taken_at = 0, k;
        Money before;
        bnew(lv);
        for (k = 0; k < 6; k++) world_lot_place(B_SCHOOL, 10 + 3 * k, 10, ROT_0, 0, LOT_ACTIVE);   /* upkeep > the village grant */
        g_budget.money = -50000;
        for (m = 1; m <= 12 && !taken_at; m++) { budget_month_end(); if (g_budget.loans_taken) taken_at = m; }
        test_log("%s: loan after %d months in the red, %d mk", diff_name(lv), taken_at, g_budget.loan);
        CHECK_EQ(taken_at, g_diff[lv].loan_after);
        CHECK_EQ(g_budget.money, (Money)g_diff[lv].loan_cushion100 * 100);
        CHECK(budget_net_last() < 0);
        g_budget.money = 5000;                 /* a losing month (the schools cost more than the village grant brings) */
        before = g_budget.loan;
        budget_month_end();
        if (g_diff[lv].loan_surplus) CHECK_EQ(g_budget.loan, before);      /* nothing out of the till */
        else CHECK(g_budget.loan < before);                                 /* PL2: 1/60 */
    }
}

TEST(b1_sweden_pull_squared)
{
    /* the reasons cost 0, 1/9, 4/9, 9/9 of the level's per mille (hard.cpp; d1_sweden_years checks it in a running town) */
    int r;
    for (r = 0; r <= 3; r++)
        test_log("Normal, 12,000 residents, %d reasons to stay: %d a month", r,
                 (int)((s64)12000 * g_diff[DIFF_NORMAL].sweden_permille * (3 - r) * (3 - r) / 9000));
    /* against D1 (2 per mille, linear thirds): a careful town (one reason missing) loses less, a neglected one (two) more */
    CHECK((s64)12000 * g_diff[DIFF_NORMAL].sweden_permille * 1 / 9000 <= (s64)12000 * 2 * 1 / 3000);
    CHECK((s64)12000 * g_diff[DIFF_NORMAL].sweden_permille * 4 / 9000 > (s64)12000 * 2 * 2 / 3000);
}

TEST(b1_health_centre_expectation)
{
    s32 have, need;
    Advice v;
    bnew(DIFF_NORMAL);
    CHECK_EQ(hard_expect_phase(HX_HCENTRE, ZONE_HOUSES, 3, 1983, 11), 0);
    CHECK_EQ(hard_expect_phase(HX_HCENTRE, ZONE_HOUSES, 3, 1984, 0), 1);
    CHECK_EQ(hard_expect_phase(HX_HCENTRE, ZONE_APARTMENTS, 3, 1985, 6), 2);
    CHECK_EQ(hard_expect_phase(HX_HCENTRE, ZONE_HOUSES, 3, 1986, 0), 3);
    CHECK_EQ(hard_expect_phase(HX_HCENTRE, ZONE_HOUSES, 2, 1986, 0), 0);
    CHECK_EQ(hard_expect_req(HX_HCENTRE), REQ_HEALTH_CENTRE);
    CHECK(hard_hcentre_ok(&have, &need));      /* a village is not asked (the health centre is a Town's building) */
    CHECK_EQ(need, 0);
    CHECK(!advice_find(ADV_EXPECT_HCENTRE, NULL));
    bnew(DIFF_EASY);
    CHECK_EQ(hard_expect_phase(HX_HCENTRE, ZONE_HOUSES, 3, 1986, 0), 0);   /* Easy: none */
    UNUSED(v);
}

/* the Cabinet's money lines name what a point of tax brings; the year's books in the red speak in January */
TEST(b1_cabinet_money_lines)
{
    Advice v;
    int k;
    bnew(DIFF_NORMAL);
    g_budget.money = -500;
    g_budget.last[BL_TAX_RES] = 900; g_budget.last[BL_TAX_BUS] = 450;      /* 9 % each: 100 + 50 a point */
    CHECK(advice_find(ADV_MONEY_DEBT, &v));
    test_log("%s", v.text);
    CHECK(strstr(v.text, "A point of tax: ~150 mk a month.") != NULL);
    g_budget.money = 20000;
    for (k = 0; k < BL_COUNT; k++) g_budget.last_year[k] = 0;
    g_budget.last_year[BL_TAX_RES] = 30000; g_budget.last_year[BL_SVC(SVC_SCHOOL)] = -34000; g_budget.last_year[BL_FUEL] = -2000;
    set_date(1980, 0);
    ((SimDate *)sim_date())->total_months = 300;
    CHECK(advice_find(ADV_MONEY_YEAR, &v));
    test_log("%s (score %d)", v.text, (int)v.score);
    CHECK(v.score >= 500 && strstr(v.text, "1979") != NULL);
    set_date(1980, 4);
    CHECK(!advice_find(ADV_MONEY_YEAR, NULL));                               /* January-February only */
}
