/* test_pl1.cpp - PL1 guidance: the Cabinet's advice from real saves (the user's play-test 2 town: PT2_1992.KAU =
   Jan 1992, PT2_1990.KAU = the Jan 1990 autosave with a power shortage and an emergency loan), the special edition
   rate limit, hindsight + the 1990 verdict, progression / unlock sentences, the power network summary, the ERA v2
   + ADVC save round trip. Output: tests/out/pl1_advice.txt (debug from text). */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "budget.h"
#include "save.h"
#include "tools.h"
#include "traffic.h"
#include "politics.h"
#include "era.h"
#include "advice.h"
#include "text.h"

static int load_save(const char *path)
{
    static int inited;
    SaveMeta meta;
    if (!inited) { if (!g_world.w) world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    if (!load_game(path, &meta)) { test_log("load %s failed: %s", path, save_last_error()); return 0; }
    return 1;
}

static void dump_to(const char *title)
{
    FILE *f = fopen("out/pl1_advice.txt", "a");
    if (!f) return;
    fprintf(f, "==== %s\n", title);
    advice_dump(f);
    fclose(f);
}

static int has_kind(const Advice *v, int n, int kind) { int k; for (k = 0; k < n; k++) if (v[k].kind == kind) return 1; return 0; }

TEST(pl1_advice_pt2_1992)
{
    Advice v[3], h;
    int n, k;
    FILE *f = fopen("out/pl1_advice.txt", "w");
    if (f) { fprintf(f, "PL1 advice from saves (tests/test_pl1.cpp)\n"); fclose(f); }
    if (!load_save("../PT2_1992.KAU")) { CHECK(0); return; }
    dump_to("PT2_1992.KAU (Mustakangas, Jan 1992)");
    n = advice_collect(v, 3);
    for (k = 0; k < n; k++) test_log("%d. %s (%d): %s", k + 1, advice_kind_name(v[k].kind), (int)v[k].score, v[k].text);
    CHECK_EQ(n, 3);
    /* the save: 3,915 residents wait for district heating, 1,850 for a water tower, C demand +64, school 2,586/2,400 */
    CHECK(has_kind(v, n, ADV_HEAT));
    CHECK(has_kind(v, n, ADV_APT_TOWER));
    CHECK(advice_find(ADV_SCHOOL, &h));
    CHECK(advice_find(ADV_SHOPS, &h));
    CHECK(!advice_find(ADV_POWER_SHORT, &h));                    /* 2,734 / 3,400: fine */
    CHECK(!advice_find(ADV_MONEY_DEBT, &h));
    CHECK(advice_find(ADV_HEAT, &h));
    CHECK(strstr(h.text, "3,915") != NULL);
    CHECK(strstr(h.text, "tier 3") != NULL && strstr(h.text, "heat") != NULL);
    CHECK(h.go == AGO_HEAT && h.x >= 0);                          /* clickable: jumps to a block */
    CHECK(advice_find(ADV_SCHOOL, &h) && strstr(h.text, "2,586") && strstr(h.text, "2,400") && h.go == AGO_SERVICES);
    for (k = 0; k < n; k++) CHECK((int)strlen(v[k].text) < ADV_TEXT - 1);
}

TEST(pl1_advice_power_short_1990)
{
    Advice v[3], h;
    PowerNetInfo pt, pn;
    int n, k, plant = 0;
    if (!load_save("../PT2_1990.KAU")) { CHECK(0); return; }
    dump_to("PT2_1990.KAU (Mustakangas, Jan 1990: power 2,572 / 2,400)");
    n = advice_collect(v, 3);
    for (k = 0; k < n; k++) test_log("%d. %s (%d): %s", k + 1, advice_kind_name(v[k].kind), (int)v[k].score, v[k].text);
    CHECK(n >= 1);
    CHECK_EQ(v[0].kind, ADV_POWER_SHORT);                          /* the power line comes first */
    CHECK(strstr(v[0].text, "107%") != NULL);                      /* 2,572 / 2,400 */
    CHECK(strstr(v[0].text, "41 buildings") != NULL);
    CHECK(v[0].go == AGO_POWER && v[0].x >= 0);
    CHECK(has_kind(v, n, ADV_MONEY_DEBT));                         /* emergency loan 2,132 mk */
    if (advice_find(ADV_APT_ELEMENTS, &h)) test_log("elements: %s", h.text);   /* PL2 moves the element gate: logged only */
    /* the power summary: total = net_stats; a plant's network sums to the same when there is one network */
    advice_power_total(&pt);
    test_log("power total %d / %d (%d%%), %d short, spare %d, %d networks", (int)pt.demand, (int)pt.supply, pt.pct, pt.lots_short, (int)pt.spare, pt.plants);
    CHECK_EQ(pt.supply, 2400); CHECK_EQ(pt.demand, 2572); CHECK_EQ(pt.pct, 107); CHECK_EQ(pt.lots_short, 41); CHECK_EQ(pt.spare, 0);
    for (k = 1; k < g_world.lot_hi && !plant; k++) if (g_world.lots[k].def != B_NONE && g_bdefs[g_world.lots[k].def].cat == BCAT_POWER) plant = k;
    CHECK(plant > 0);
    CHECK(advice_power_network(plant, &pn));
    test_log("plant %d at %d,%d network: %d / %d (%d%%), used %d, spare %d, %d plants, %d buildings, %d short", plant, g_world.lots[plant].x, g_world.lots[plant].y, (int)pn.demand, (int)pn.supply,
             pn.pct, (int)pn.used, (int)pn.spare, pn.plants, pn.lots, pn.lots_short);
    if (net_stats(NET_POWER)->networks == 1) {
        CHECK_EQ(pn.supply, pt.supply); CHECK_EQ(pn.demand, pt.demand); CHECK_EQ(pn.lots_short, pt.lots_short);
    } else CHECK(pn.supply <= pt.supply && pn.demand <= pt.demand);
    CHECK(pn.used <= pn.supply);
    CHECK_EQ(pn.lots, net_stats(NET_POWER)->networks == 1 ? net_stats(NET_POWER)->lots_served + net_stats(NET_POWER)->lots_short : pn.lots);
}

TEST(pl1_newspaper_cabinet_and_special)
{
    NewsIssue n;
    Advice m;
    int k, y;
    if (!load_save("../PT2_1990.KAU")) { CHECK(0); return; }
    era_news_build_year(&n);
    CHECK(n.ncab >= 1 && n.ncab <= NEWS_CAB);
    CHECK_EQ(n.cab_kind[0], ADV_POWER_SHORT);
    CHECK(strstr(n.cab[0], "respectfully") != NULL);
    era_news_build_special(&n, ADV_POWER_SHORT);
    CHECK_EQ(n.kind, NEWS_SPECIAL);
    CHECK(strstr(n.head[0], "LIGHTS OUT") != NULL);
    CHECK(strstr(n.deck[1], "Nobody advises me") != NULL);
    CHECK(n.ncab >= 1);
    /* rate limit: a special edition at most once a game year, never in December */
    y = sim_date()->year;
    k = advice_special_due(&m);
    test_log("special due in %d-%02d: %d (%s)", y, sim_date()->month + 1, k, k ? advice_kind_name(m.kind) : "-");
    if (k) {
        CHECK_EQ(m.kind, ADV_POWER_SHORT);
        CHECK(!advice_special_due(&m));                 /* not twice in the same year */
    }
    {   /* save round trip keeps the Cabinet lines and the special-edition year */
        SaveFile *sf = save_mem_open();
        NewsIssue before;
        char w0[8] = "news", w1[8] = "year", *tv[2];
        tv[0] = w0; tv[1] = w1;
        era_script_line(2, tv, NULL);
        before = *era_news();
        save_game_to(sf, NULL);
        save_mem_reopen(sf);
        CHECK(load_game_from(sf, NULL));
        CHECK(era_news() != NULL);
        CHECK_EQ(era_news()->ncab, before.ncab);
        CHECK(!strcmp(era_news()->cab[0], before.cab[0]));
        CHECK_EQ(era_news()->cab_go[0], before.cab_go[0]);
        CHECK_EQ(era_news()->cab_x[0], before.cab_x[0]);
        if (k) CHECK(!advice_special_due(&m));
        save_mem_close(sf);
    }
}

TEST(pl1_progress_and_unlocks)
{
    char b[160];
    int ts, p;
    if (!load_save("../PT2_1992.KAU")) { CHECK(0); return; }
    p = advice_progress(b, sizeof(b), &ts);
    test_log("progress %d%%: %s", p, b);
    /* a Town of 12,933: next is Regional centre at 40,000 */
    CHECK_EQ(sim_stats()->town_status, TS_TOWN);
    CHECK_EQ(ts, TS_REGIONAL_CENTRE);
    CHECK(!strcmp(b, "Next: Regional centre at 40,000 (you have 12,933)"));
    CHECK_EQ(p, 32);
    advice_unlock_text(b, sizeof(b), TS_REGIONAL_CENTRE, 1965);
    test_log("unlock: %s", b);
    CHECK(!strcmp(b, "Unlocks at Regional centre (40,000 residents)"));
    advice_unlock_text(b, sizeof(b), TS_CITY, 1995);
    CHECK(!strcmp(b, "Unlocks at City (100,000 residents) and 1995"));
    advice_unlock_text(b, sizeof(b), TS_VILLAGE, 1995);
    CHECK(!strcmp(b, "Unlocks in 1995"));
    CHECK_EQ(advice_unlock_text(b, sizeof(b), TS_VILLAGE, 1955), 0);
    advice_status_unlocks(TS_MARKET_TOWN, b, sizeof(b));
    test_log("Market town brings: %s", b);
    CHECK(strstr(b, "Metalli works") != NULL);                     /* proper names keep their capitals */
    advice_status_unlocks(g_bdefs[B_ELEMENT_FACTORY].status_req, b, sizeof(b));   /* PL2 moves the gates: data-driven */
    test_log("the element factory's status brings: %s", b);
    CHECK(strstr(b, "element factory") != NULL || strstr(b, "Element factory") != NULL);
    advice_status_unlocks(g_bdefs[B_WATER_TOWER].status_req, b, sizeof(b));   /* PL2 may move the tower: data-driven */
    test_log("the water tower's status brings: %s", b);
    CHECK(strstr(b, "Water tower") != NULL || strstr(b, "water tower") != NULL);
    advice_status_unlocks(TS_MARKET_TOWN, b, sizeof(b));
    advice_status_unlocks(g_bdefs[B_HEAT_PLANT].status_req, b, sizeof(b));
    CHECK(strstr(b, "heat plant") != NULL);
    CHECK(strstr(b, " and ") != NULL);
    CHECK(advice_purpose(B_HEAT_PLANT) && strstr(advice_purpose(B_HEAT_PLANT), "tier 3"));
}

/* hindsight on a town that runs short of power for a year; the newsreel verdict names it */
TEST(pl1_hindsight_and_verdict)
{
    char h[48], b[240], lines[3][120];
    int k, n, got = 0;
    if (!load_save("../PT2_1990.KAU")) { CHECK(0); return; }
    advice_reset();
    for (k = 0; k < 12; k++) advice_month();             /* 12 months of this town's state: short of power, in debt */
    got = advice_hindsight(h, sizeof(h), b, sizeof(b));
    test_log("hindsight: %s / %s", h, b);
    CHECK(got);
    CHECK(strstr(h, "IN HINDSIGHT") != NULL);
    CHECK(strstr(b, "hindsight") != NULL);
    advice_year_end();
    CHECK(!advice_hindsight(h, sizeof(h), b, sizeof(b)));  /* a new year: nothing tallied yet */
    for (k = 0; k < 24; k++) advice_month();
    n = advice_verdict(lines, 3);
    for (k = 0; k < n; k++) test_log("verdict %d: %s", k + 1, lines[k]);
    CHECK(n >= 2 && n <= 3);
    CHECK(strstr(lines[0], "lights") != NULL || strstr(lines[0], "oil") != NULL || strstr(lines[0], "red") != NULL ||
          strstr(lines[0], "precast") != NULL);
}
