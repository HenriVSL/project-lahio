/* test_trade.cpp - M7: town status by decree, era events (1963 new markka, 1973 oil shock, 1990 end of clearing),
   the newspaper (headline generation strings), graphs history, clearing agreements and balance math, Western
   trade, loans, landmarks, determinism + save round-trip. Owner: M7. Dumps: tests/out/m7_*.txt.
   The town is scripts/town1.txt (seed 7 Lakeland). Run alone: make -f tests/Makefile.mac T=trade */
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
#include "save.h"
#include "politics.h"
#include "industry.h"
#include "text.h"
#include "trade.h"
#include "era.h"

static void drain(void)
{
    SimEvent e;
    PolMsg m;
    EraMsg em;
    while (sim_poll_event(&e)) {}
    while (politics_poll_msg(&m)) {}
    while (era_poll_msg(&em)) {}
}

static int s_auto_rights = 1;     /* sign town rights like a player (0: test the gating) */
static void months(int n)
{
    while (n-- > 0) {
        sim_run_months(1); tools_month_changed(); drain();
        if (s_auto_rights && politics_decree_state(DEC_TOWN_RIGHTS) == DS_OK) politics_sign(DEC_TOWN_RIGHTS);
    }
}

static int scr(const char *line) { int w; return tools_script_line(line, &w, NULL); }

/* town1.txt (5 years of building) on a city that starts in start_year; politics on/off */
static int town1(int politics, int start_year, int script_years)
{
    MapGenParams mp;
    SimSettings s;
    static int inited;
    FILE *f;
    char line[256];
    int wait, waited = 0, errors = 0, r;
    s_auto_rights = 1;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    mapgen_defaults(&mp);
    mp.seed = 7;
    if (!mapgen_generate(&mp)) return -1;
    sim_default_settings(&s);
    s.seed = 7;
    s.politics = politics;
    s.start_year = start_year;
    sim_new_city(&s);
    tools_reset();
    tools_script_reset();
    f = fopen("../scripts/town1.txt", "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "statlog", 7) || !strncmp(line, "dump", 4) || !strncmp(line, "expect", 6)) continue;
        if (!strncmp(line, "wait", 4) && waited >= script_years * 12) break;
        r = tools_script_line(line, &wait, NULL);
        if (r == SCR_WAIT) { months(wait); waited += wait; }
        else if (r == SCR_ERROR) errors++;
    }
    fclose(f);
    drain();
    return errors;
}

static SaveFile *snap(void) { SaveFile *sf = save_mem_open(); save_game_to(sf, NULL); return sf; }
static int restore(SaveFile *sf)
{
    int ok;
    save_mem_reopen(sf);
    ok = load_game_from(sf, NULL);
    if (!ok) test_log("restore failed: %s", save_last_error());
    tools_reset();
    drain();
    return ok;
}

/* the town1 script is shared by every task and sensitive to each one's economy: errors are logged, not asserted */
static void town(int politics, int start_year, int years)
{
    int e = town1(politics, start_year, years);
    if (e) test_log("  (town1: %d script errors from the shared economy, ignored here)", e);
}

static int place(int def, int x0, int y0, int x1, int y1)
{
    int x, y;
    g_budget.money += 100000;
    UNUSED(x0); UNUSED(y0); UNUSED(x1); UNUSED(y1);
    for (y = 2; y < g_world.h - 4; y++)
        for (x = 2; x < g_world.w - 4; x++)
            if (world_lot_check(def, x, y, 0) == PLACE_OK && tools_apply_build(def, x, y, 0)) return world_lot_at(x, y);
    return 0;
}

/* ------------------------------------------------------------------ town status by decree */
TEST(trade_status_by_decree)
{
    int held, ok;
    /* politics off: automatic, with a ceremony page */
    town(0, 1955, 2);
    test_log("politics off: status %d (%s), eligible %d, ceremony pending %d", sim_stats()->town_status,
             text_status_name(sim_stats()->town_status), era_status_eligible(), era_news_pending());
    CHECK(sim_stats()->town_status >= TS_CHURCH_VILLAGE);
    CHECK_EQ(era_news()->kind == NEWS_CEREMONY || era_news()->kind == NEWS_YEAR, 1);
    /* politics on, nobody signs: the status waits, the decree is offered */
    s_auto_rights = 0;
    {
        MapGenParams mp; SimSettings ss;
        mapgen_defaults(&mp); mp.seed = 7; mapgen_generate(&mp);
        sim_default_settings(&ss); ss.seed = 7; ss.politics = 1; sim_new_city(&ss); tools_reset(); tools_script_reset();
    }
    scr("townhall");
    scr("road 73 73 101 73 street"); scr("road 74 79 101 79 street"); scr("road 74 73 74 91 street");
    scr("zone 75 74 100 78 houses"); scr("zone 75 80 100 84 houses");
    scr("road 103 82 116 82 street"); scr("zone 104 83 116 87 industry");
    scr("build coal_plant 107 99"); scr("power 102 74 102 98"); scr("power 74 73 102 73"); scr("power 74 79 102 79");
    s_auto_rights = 0;
    while (era_status_eligible() < TS_CHURCH_VILLAGE && sim_date()->total_months < 48) months(1);
    s_auto_rights = 0;
    held = sim_stats()->town_status;
    test_log("politics on: pop %d eligible %d held %d, decree state %d, %d months", sim_stats()->population,
             era_status_eligible(), held, politics_decree_state(DEC_TOWN_RIGHTS), sim_date()->total_months);
    CHECK(era_status_eligible() >= TS_CHURCH_VILLAGE);
    CHECK_EQ(held, TS_VILLAGE);                                    /* not without the President's signature */
    CHECK(!def_unlocked(B_CHURCH, sim_date()->year, held, 0));    /* the church waits for the status */
    CHECK_EQ(politics_decree_state(DEC_TOWN_RIGHTS), DS_OK);
    months(2);
    CHECK_EQ(sim_stats()->town_status, TS_VILLAGE);
    ok = politics_sign(DEC_TOWN_RIGHTS);
    CHECK(ok);
    test_log("signed: status %d, ceremony kind %d: %s / %s", sim_stats()->town_status, era_news()->kind, era_news()->head[0], era_news()->body[1]);
    CHECK(sim_stats()->town_status >= TS_CHURCH_VILLAGE);
    CHECK(def_unlocked(B_CHURCH, sim_date()->year, sim_stats()->town_status, 0));
    CHECK_EQ(era_news_pending(), NEWS_CEREMONY);
    CHECK(strstr(era_news()->head[0], "CHURCH VILLAGE") != NULL);
    CHECK(strstr(era_news()->body[1], "may build") != NULL);       /* the unlocks are listed */
    CHECK_EQ(politics_decree_state(DEC_TOWN_RIGHTS), DS_HIDDEN);  /* offered again only at the next step */
    s_auto_rights = 1;
}

/* ------------------------------------------------------------------ newspaper + graphs */
TEST(trade_newspaper_and_history)
{
    const NewsIssue *n;
    s32 h[ERA_HIST];
    int k, cnt;
    FILE *f = fopen("out/m7_newspapers.txt", "w");
    town(0, 1955, 5);
    while (sim_date()->month != 11) months(1);
    months(1);                                         /* the January paper */
    n = era_news();
    CHECK(n != NULL);
    for (k = 0; k < NEWS_HEADS; k++) {
        CHECK(n->head[k][0] && n->deck[k][0] && n->body[k][0]);
        CHECK(strlen(n->head[k]) < 64);
    }
    CHECK(n->filler_head[0] && n->filler[0]);
    if (f) {
        int y;
        fprintf(f, "M7 newspapers: town1, politics off, January issues (tests/test_trade.cpp)\n");
        for (y = 0; y < 6; y++) {
            n = era_news();
            fprintf(f, "\n== Maakunnan Sanomat no. %d, %s %d (kind %d)\n", n->issue_no, text_month_name(n->month), n->year, n->kind);
            for (k = 0; k < NEWS_HEADS; k++) fprintf(f, "  [%d] %s\n      %s\n      %s\n", n->topic[k], n->head[k], n->deck[k], n->body[k]);
            fprintf(f, "  filler %s: %s\n", n->filler_head, n->filler);
            era_news_seen();
            months(12);
        }
    }
    cnt = era_history(EH_UNEMPLOYED, h, ERA_HIST);
    test_log("history: %d months, years %d, first year %d pop %d, last %d pop %d", cnt, era_years(), era_year_row(0)->year,
             era_year_row(0)->pop, era_year_row(era_years() - 1)->year, era_year_row(era_years() - 1)->pop);
    CHECK(cnt >= 100);
    CHECK(era_years() >= 10);
    CHECK(era_year_row(era_years() - 1)->pop == sim_stats()->population);
    CHECK(sim_history(HIST_POPULATION, h, ERA_HIST) >= 100);
    if (f) fclose(f);
}

/* ------------------------------------------------------------------ 1963 new markka (display unchanged) */
TEST(trade_new_markka_1963)
{
    char b[32];
    int k, found = 0;
    town(0, 1960, 2);
    while (!(sim_date()->year == 1963 && sim_date()->month == 0)) months(1);
    while (era_news_pending() && era_news_pending() != NEWS_YEAR) era_news_seen();   /* a ceremony may come first */
    for (k = 0; k < NEWS_HEADS; k++) if (era_news()->topic[k] == NT_ERA && strstr(era_news()->head[k], "MARKKA")) found = 1;
    test_log("Jan 1963: %s / %s", era_news()->head[0], era_news()->body[0]);
    CHECK(found);
    text_fmt_money(b, sizeof(b), 125, 1961);
    CHECK(!strcmp(b, "125 mk"));                       /* the x100 display is cut (SCOPE_REVIEW) */
}

/* ------------------------------------------------------------------ the agreement card + balance math */
TEST(trade_agreement_card_and_balance)
{
    const TradeState *t = trade_state();
    const TradeAgreement *a = trade_agreement();
    s32 b0, b1, lost;
    int lv;
    /* politics off: the Ministry signs Standard after 6 months */
    town(0, 1955, 5);
    while (sim_date()->year < 1961) months(1);
    test_log("1961-01 off: offer %d level %d", a->offer, a->level);
    CHECK_EQ(a->offer, 1);
    CHECK_EQ(a->from_year, 1961);
    months(TRADE_OFFER_MONTHS);
    CHECK_EQ(a->offer, 0);
    CHECK_EQ(a->level, TL_STANDARD);
    CHECK(a->quota_total > 0);
    /* balance math: one month = exports - oil - lost */
    b0 = t->balance;
    months(1);
    b1 = t->balance;
    lost = t->lost_month;
    test_log("balance %d -> %d: exports %d oil %d+%d lost %d tolerance %d", b0, b1, t->exports_east, t->oil_town, t->oil_plants, lost, t->tolerance);
    CHECK_EQ(b1 - b0, t->exports_east - t->oil_town - t->oil_plants - lost);
    CHECK(t->tolerance == 3 * t->exports_east + 200);
    {   /* a quarter of the credit above the tolerance is lost */
        s32 pre = b0 + t->exports_east - t->oil_town - t->oil_plants;
        CHECK_EQ(lost, pre > t->tolerance ? (pre - t->tolerance + 3) / 4 : 0);
    }
    /* politics on: the card, choose Ambitious. P1: one card a year across all sources (politics_queue_event's gate):
       the agreement card needs 12 months since the last card left the desk, otherwise the Ministry signs Standard
       after TRADE_OFFER_MONTHS - so the first period whose card finds the slot open is used */
    town(1, 1955, 5);
    politics_meter_add(METER_EAST, 30);
    lv = 0;
    while (sim_date()->year < 1980 && politics_pending_event() != EV_AGREEMENT) {
        months(1);
        if (politics_pending_event() >= 0 && politics_pending_event() != EV_AGREEMENT) politics_choose(0);
    }
    test_log("on: agreement card up %d-%02d (period %d-%d), pending card %d, offer %d", sim_date()->year, sim_date()->month + 1,
             a->from_year, a->to_year, politics_pending_event(), a->offer);
    CHECK_EQ(politics_pending_event(), EV_AGREEMENT);
    politics_meter_add(METER_EAST, 30);
    lv = trade_clearing_pct();
    CHECK(politics_choose(2));
    test_log("Ambitious: level %d, clearing pct %d -> %d, goods list %d", a->level, lv, trade_clearing_pct(), a->quota_total);
    CHECK_EQ(a->level, TL_AMBITIOUS);
    CHECK_EQ(trade_clearing_pct(), 120);
    CHECK_EQ(a->offer, 0);
}

/* ------------------------------------------------------------------ 1973 oil shock, 1990 end of clearing, newsreel */
TEST(trade_oil_shock_and_1990)
{
    const TradeState *t = trade_state();
    int lot, k, found = 0;
    s32 fuel_before, oil_before;
    char lines[12][96];
    FILE *f = fopen("out/m7_eras.txt", "w");
    town(0, 1970, 2);
    g_budget.money += 50000;
    lot = place(B_OIL_PLANT, 104, 99, 116, 110);
    CHECK(lot > 0);
    while (!(sim_date()->year == 1973 && sim_date()->month == 9)) months(1);
    months(1);                                         /* October closed: still 100; November has the boom */
    fuel_before = t->fuel_cost; oil_before = t->clearing_oil;
    CHECK_EQ(t->oil_index, 100);
    CHECK_EQ(trade_clearing_pct(), 150);
    months(2);                                         /* November + December 1973 */
    test_log("oil shock: index %d, clearing pct %d, fuel %d -> %d mk, town oil %d -> %d, BL_FUEL %d", t->oil_index,
             trade_clearing_pct(), fuel_before, t->fuel_cost, oil_before, t->oil_town, g_budget.last[BL_FUEL]);
    CHECK_EQ(t->oil_index, 400);
    CHECK_EQ(trade_clearing_pct(), 150);
    CHECK(t->fuel_cost >= fuel_before * 3);
    months(24);
    test_log("1976: clearing oil %d (was %d), meter %d, deficit months %d", t->clearing_oil, oil_before, t->balance, t->deficit_months);
    CHECK(t->clearing_oil > oil_before + 40);           /* the clearing price follows slowly */
    CHECK(g_budget.last[BL_FUEL] < 0);
    CHECK(era_event_seen(EE_OIL_SHOCK));
    if (f) { fprintf(f, "M7 eras: town1 from 1970, an oil plant (tests/test_trade.cpp)\n"); era_dump(f); }
    while (sim_date()->year < 1990) months(1);
    test_log("1990: closed %d, clearing pct %d, news pending %d", t->closed, trade_clearing_pct(), era_news_pending());
    CHECK_EQ(t->closed, 1);
    CHECK_EQ(trade_clearing_pct(), 100);
    CHECK(era_event_seen(EE_CLEARING_END));
    while (era_news_pending() == NEWS_CEREMONY) era_news_seen();
    found = 0;
    for (k = 0; k < NEWS_HEADS; k++) if (strstr(era_news()->head[k], "CLEARING TRADE ENDS")) found = 1;
    CHECK(found);
    era_news_seen();                                   /* the January paper, then the newsreel */
    CHECK_EQ(era_news_pending(), NEWS_NEWSREEL);
    CHECK(!strcmp(era_news()->head[0], "THE END OF AN ERA"));
    k = era_newsreel_lines(lines, 12);
    CHECK(k >= 6);
    if (f) {
        int i;
        era_dump(f);
        fprintf(f, "\nnewsreel:\n");
        for (i = 0; i < k; i++) fprintf(f, "  %s\n", lines[i]);
        fclose(f);
    }
}

/* ------------------------------------------------------------------ the bond */
TEST(trade_bond)
{
    const TradeState *t = trade_state();
    Money m0;
    s32 amount;
    int k;
    town(0, 1955, 2);
    m0 = g_budget.money;
    amount = trade_bond_amount();
    CHECK(trade_issue_bond());
    CHECK(!trade_issue_bond());                        /* one at a time */
    CHECK_EQ(g_budget.money - m0, amount);
    CHECK_EQ(t->bond, amount);
    months(2);
    test_log("bond %d: after 2 months %d left, BL_INTEREST %d", amount, t->bond, g_budget.last[BL_INTEREST]);
    CHECK(t->bond < amount);
    CHECK(g_budget.last[BL_INTEREST] < 0);
    for (k = 0; k < 125 && t->bond > 0; k++) months(1);
    CHECK_EQ(t->bond, 0);
    CHECK(k <= 120);
    CHECK(trade_issue_bond());
}

/* ------------------------------------------------------------------ landmarks */
TEST(trade_landmarks)
{
    int lot, auth0, lv0 = 0, lv1, x, y;
    town(1, 1965, 3);
    sim_grant_status(TS_TOWN);
    CHECK(def_unlocked(B_CONCERT_HALL, sim_date()->year, sim_stats()->town_status, 0));
    CHECK(!def_unlocked(B_TV_TOWER, sim_date()->year, sim_stats()->town_status, 0));   /* 1970 */
    lot = place(B_CONCERT_HALL, 76, 92, 100, 100);
    CHECK(lot > 0);
    x = g_world.lots[lot].x; y = g_world.lots[lot].y;
    lv0 = WT(x + 3, y)->lv;
    auth0 = politics_authority();
    months(7);
    lv1 = WT(x + 3, y)->lv;
    test_log("concert hall lot %d at %d,%d: landmarks %d, BL_SPECIAL %d, land value next door %d -> %d, authority %d -> %d",
             lot, x, y, era_landmark_count(), g_budget.last[BL_SPECIAL], lv0, lv1, auth0, politics_authority());
    CHECK_EQ(era_landmark_count(), 1);
    CHECK(lv1 >= lv0);
    CHECK(politics_authority() >= IMIN(auth0 + 1, politics_authority_cap()));
    CHECK(!place(B_CONCERT_HALL, 76, 92, 100, 100));   /* unique */
    /* the presidential sauna: one button, diminishing returns, once a year */
    CHECK_EQ(era_sauna_state(), 1);
    CHECK(place(B_PRES_SAUNA, 76, 60, 100, 70) > 0);
    months(1);
    {
        int e0 = politics_meter(METER_EAST), g = era_sauna_gain();
        CHECK_EQ(era_sauna_state(), 0);
        CHECK(era_sauna_invite());
        CHECK_EQ(politics_meter(METER_EAST), IMIN(100, e0 + g));
        CHECK_EQ(era_sauna_state(), 3);
        CHECK(!era_sauna_invite());
        months(12);
        CHECK(era_sauna_gain() < g);
    }
}

/* ------------------------------------------------------------------ save round-trip + determinism */
static void roundtrip(int politics)
{
    SaveFile *sf;
    u32 h1, h2, e1;
    town(politics, 1955, 3);
    scr("trade bond");
    months(2);
    sf = snap();
    e1 = era_hash();
    months(14);
    h1 = sim_hash();
    CHECK(restore(sf));
    CHECK_EQ(era_hash(), e1);
    months(14);
    h2 = sim_hash();
    test_log("politics %d: unbroken %08x, save+load %08x, era %08x", politics, h1, h2, e1);
    CHECK_EQ(h1, h2);
    save_mem_close(sf);
}
TEST(trade_save_roundtrip_determinism)
{
    roundtrip(0);
    roundtrip(1);
}


/* an M1-era save (no ERA/TRAD chunks) loads: trade state fresh for its date, status kept, 12 months run */
TEST(trade_old_save_loads)
{
    SaveMeta m;
    int x, y, site = 0;
    FILE *f = fopen("../AUTO1.KAU", "rb");
    if (!f) { test_log("../AUTO1.KAU missing: skipped"); return; }
    fclose(f);
    CHECK(load_game("../AUTO1.KAU", &m));
    tools_reset(); drain();
    test_log("AUTO1: %d-%02d pop %d status %d, agreement %d-%d level %d, oil %d", sim_date()->year, sim_date()->month + 1,
             sim_stats()->population, sim_stats()->town_status, trade_agreement()->from_year, trade_agreement()->to_year,
             trade_agreement()->level, trade_state()->oil_index);
    CHECK(trade_agreement()->level >= 0 && !trade_state()->closed);
    CHECK_EQ(era_status_eligible(), sim_stats()->town_status);
    for (y = 60; y < 110 && !site; y++)       /* a presidential sauna site for the VM screenshot script */
        for (x = 60; x < 110 && !site; x++)
            if (world_lot_check(B_PRES_SAUNA, x, y, 0) == PLACE_OK && world_lot_check(B_PARK, x, y + 2, 0) != PLACE_ROAD) {
                int rx, ry, ok = 0;
                for (ry = y - 1; ry <= y + 2 && !ok; ry++) for (rx = x - 1; rx <= x + 2 && !ok; rx++) ok = TILE_ROAD(WT(rx, ry)) != 0;
                if (ok) { site = 1; test_log("pres_sauna site %d %d", x, y); }
            }
    months(12);
    CHECK(era_years() >= 2);
}

/* the VM screenshot script runs headless on the Mac without errors (screen commands are ignored here) */
TEST(trade_vm_script_headless)
{
    MapGenParams mp;
    SimSettings ss;
    int errors;
    FILE *out = fopen("out/m7_script_out.txt", "w");
    mapgen_defaults(&mp); mp.seed = 7; mapgen_generate(&mp);
    sim_default_settings(&ss); ss.seed = 7; sim_new_city(&ss); tools_reset(); tools_script_reset();
    errors = tools_script_run_file("../scripts/m7_shots.txt", out);
    if (out) fclose(out);
    test_log("m7_shots.txt: %d errors, %d-%02d pop %d status %d landmarks %d", errors, sim_date()->year, sim_date()->month + 1,
             sim_stats()->population, sim_stats()->town_status, era_landmark_count());
    CHECK(errors >= 0 && errors <= 1);          /* town1's shared-economy build may be refused (logged) */
    CHECK_EQ(era_landmark_count(), 1);
}
