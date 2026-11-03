/* test_politics.cpp - M1 politics: blocs read the city, decrees change sim parameters and revert, slots and
   cooldowns, letters, event cards (rate, choices, expiry), elections (margin math, emergency law), money
   bookings, determinism + save round-trip with politics on, politics off = neutral. Owner: M1.
   The town is scripts/town1.txt (seed 7 Lakeland, 5 years, the same file the VM -script runs).
   Dumps: tests/out/politics_*.txt. Run alone: make -f tests/Makefile.mac T=politics */
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
#include "fields.h"

static int scr(const char *line) { int w; return tools_script_line(line, &w, NULL); }

static void drain(void)
{
    SimEvent e;
    PolMsg m;
    while (sim_poll_event(&e)) {}
    while (politics_poll_msg(&m)) {}
}

static void months(int n)
{
    while (n-- > 0) {
        sim_run_months(1); tools_month_changed(); drain();
        if (politics_decree_state(DEC_TOWN_RIGHTS) == DS_OK) politics_sign(DEC_TOWN_RIGHTS);   /* M7: status by decree */
    }
}

/* town1.txt with politics on/off; script waits run the sim (drained like the game would) */
static int town1(int politics, int script_years)
{
    MapGenParams mp;
    SimSettings s;
    static int inited;
    FILE *f;
    char line[256];
    int wait, waited = 0, errors = 0, r;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    mapgen_defaults(&mp);
    mp.seed = 7;
    if (!mapgen_generate(&mp)) return -1;
    sim_default_settings(&s);
    s.seed = 7;
    s.politics = politics;
    sim_new_city(&s);
    tools_reset();
    tools_script_reset();
    f = fopen("../scripts/town1.txt", "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "statlog", 7) || !strncmp(line, "dump", 4)) continue;
        if (!strncmp(line, "wait", 4) && waited >= script_years * 12) break;
        r = tools_script_line(line, &wait, NULL);
        if (r == SCR_WAIT) { months(wait); waited += wait; }
        else if (r == SCR_ERROR) errors++;
    }
    fclose(f);
    return errors;
}

static int fx_has(const PolFx *fx, int kind, int target, int value)
{
    int i;
    for (i = 0; i < POL_FX_MAX; i++) if (fx[i].kind == kind && fx[i].target == target && fx[i].value == value) return 1;
    return 0;
}

/* ------------------------------------------------------------------ off = neutral */
TEST(politics_off_is_neutral)
{
    int k;
    PolMsg m;
    CHECK_EQ(town1(0, 5), 0);
    CHECK(!politics_enabled());
    for (k = 0; k < PM_COUNT; k++) CHECK_EQ(politics_mod(k), (k == PM_HAPPY_ADD || k == PM_APT_SCORE_ADD) ? 0 : 100);
    CHECK_EQ(g_budget.year[BL_POLITICS] + g_budget.last_year[BL_POLITICS], 0);
    CHECK_EQ(politics_popularity(), 50);
    CHECK(!politics_sign(DEC_RENT));
    CHECK(!politics_send_letter(LET_EMBASSY));
    CHECK(!politics_queue_event(EV_SAUNA));
    CHECK(!politics_poll_msg(&m));
    for (k = 0; k < DEM_COUNT; k++) {
        const Demand *d = sim_demand(k);
        int t;
        for (t = 0; t < d->nterms; t++) CHECK(d->terms[t].id != DT_POLICY);
    }
    test_log("off: pop %d, hash %08x (politics adds nothing to the hash when off)", sim_stats()->population, sim_hash());
}

/* ------------------------------------------------------------------ blocs read the city */
static SaveFile *snap(void)
{
    SaveFile *sf = save_mem_open();
    save_game_to(sf, NULL);
    return sf;
}
static void restore(SaveFile *sf)
{
    save_mem_reopen(sf);
    if (!load_game_from(sf, NULL)) test_log("restore failed: %s", save_last_error());
    tools_reset();
    drain();
}

/* target of bloc b one month after `change` vs the same month without it (same save, same seed) */
static int bloc_delta(SaveFile *base, int bloc, void (*change)(void), int n_months)
{
    int with, without;
    restore(base);
    months(n_months);
    without = politics_approval_target(bloc);
    restore(base);
    change();
    months(n_months);
    with = politics_approval_target(bloc);
    test_log("  delta probe: unemployed %d workers %d jobs %d power %d/%d short %d, bloc %d %d -> %d", sim_stats()->unemployed,
             sim_stats()->workers, sim_stats()->jobs, sim_stats()->power_demand, sim_stats()->power_supply,
             sim_stats()->power_short_lots, bloc, without, with);
    return with - without;
}

static void ch_tax_res(void) { budget_set_tax(16, g_budget.tax_bus); }
static void ch_tax_bus(void) { budget_set_tax(g_budget.tax_res, 16); }
static void ch_tax_low(void) { budget_set_tax(5, 5); }
static void ch_no_industry(void)   /* bulldoze the industrial district: jobs vanish */
{
    int i, x0, y0, x1, y1;
    g_budget.money += 200000;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_NONE || def_building(l->def)->zone != ZONE_INDUSTRY) continue;
        world_lot_rect(i, &x0, &y0, &x1, &y1);
        tools_apply_rect(TOOL_BULLDOZE, 0, x0, y0, x1, y1);
    }
}
static void ch_clear_forest(void)
{
    int x, y;
    g_budget.money += 200000;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++)
            if (TER_TYPE(WT(x, y)) == TER_FOREST && !WT(x, y)->lot) world_set_terrain(x, y, TER_GRASS, 0);
}
static void ch_power_cut(void)
{
    int i, x0, y0, x1, y1;
    g_budget.money += 200000;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_COAL_PLANT) { world_lot_rect(i, &x0, &y0, &x1, &y1); tools_apply_rect(TOOL_BULLDOZE, 0, x0, y0, x1, y1); }
    }
}
static void ch_east_up(void) { politics_meter_add(METER_EAST, 30); }
static void ch_west_up(void) { politics_meter_add(METER_WEST, 40); }
static void ch_park(void)
{
    int x, y, done = 0;
    g_budget.money += 50000;
    for (y = 70; y < 100 && !done; y++)
        for (x = 74; x < 102 && !done; x++)
            if (world_lot_check(B_PARK, x, y, 0) == PLACE_OK && tools_apply_build(B_PARK, x, y, 0)) done = 1;
}

TEST(politics_blocs_respond_to_the_city)
{
    SaveFile *base;
    FILE *f;
    int d[12], b;
    CHECK_EQ(town1(1, 5), 0);
    CHECK(politics_enabled());
    f = fopen("out/politics_blocs.txt", "w");
    if (f) { politics_dump(f); fclose(f); }
    for (b = 0; b < BLOC_COUNT; b++)
        test_log("%-14s approval %d target %d weight %d.%d%% members %d", politics_bloc_name(b), politics_approval(b),
                 politics_approval_target(b), politics_weight(b) / 10, politics_weight(b) % 10, politics_members(b));
    {
        s32 w = 0;
        for (b = 0; b < BLOC_COUNT; b++) { w += politics_weight(b); CHECK(politics_approval(b) >= 0 && politics_approval(b) <= 100); }
        CHECK(w >= 990 && w <= 1000);
    }
    base = snap();
    d[0] = bloc_delta(base, BLOC_WORKERS, ch_tax_res, 1);
    d[1] = bloc_delta(base, BLOC_RURAL, ch_tax_res, 1);
    d[2] = bloc_delta(base, BLOC_INDUSTRIALISTS, ch_tax_bus, 1);
    d[3] = bloc_delta(base, BLOC_WORKERS, ch_tax_low, 1);
    d[4] = bloc_delta(base, BLOC_WORKERS, ch_no_industry, 1);
    d[5] = bloc_delta(base, BLOC_RURAL, ch_clear_forest, 1);
    d[6] = bloc_delta(base, BLOC_INDUSTRIALISTS, ch_power_cut, 1);
    d[7] = bloc_delta(base, BLOC_INTELLIGENTSIA, ch_west_up, 1);
    d[8] = bloc_delta(base, BLOC_RURAL, ch_east_up, 1);
    d[9] = bloc_delta(base, BLOC_INTELLIGENTSIA, ch_park, 2);
    test_log("res tax 16%%: Workers %+d, Rural %+d | bus tax 16%%: Industrialists %+d | tax 5%%: Workers %+d", d[0], d[1], d[2], d[3]);
    test_log("industry bulldozed: Workers %+d | forest cleared: Rural %+d | power cut: Industrialists %+d", d[4], d[5], d[6]);
    test_log("Western trade +40: Intelligentsia %+d | Eastern +30: Rural %+d | a park: Intelligentsia %+d", d[7], d[8], d[9]);
    CHECK(d[0] < 0);
    CHECK(d[1] < 0);
    CHECK(d[2] < 0);
    CHECK(d[3] > 0);
    CHECK(d[4] < 0);
    CHECK(d[5] < 0);
    CHECK(d[6] < 0);
    CHECK(d[7] > 0);
    CHECK(d[8] > 0);
    CHECK(d[9] > 0);
    save_mem_close(base);
}

/* urbanisation shifts the electorate from Rural to Workers (the historical arc, DESIGN 6) */
TEST(politics_weights_follow_housing)
{
    int r0, w0, r1, w1;
    CHECK_EQ(town1(1, 1), 0);
    r0 = politics_weight(BLOC_RURAL); w0 = politics_weight(BLOC_WORKERS);
    g_budget.money += 100000;
    scr("zone 104 83 115 87 apartments");
    scr("zone 104 89 115 94 apartments");
    months(48);
    r1 = politics_weight(BLOC_RURAL); w1 = politics_weight(BLOC_WORKERS);
    test_log("year 1: Rural %d.%d%% Workers %d.%d%%; 4 years later (apartments): Rural %d.%d%% Workers %d.%d%%, apts %d",
             r0 / 10, r0 % 10, w0 / 10, w0 % 10, r1 / 10, r1 % 10, w1 / 10, w1 % 10, sim_stats()->residents_zone[ZONE_APARTMENTS]);
    CHECK(r1 < r0);
    CHECK(w1 > w0);
}

/* ------------------------------------------------------------------ decrees */
TEST(politics_decrees_change_parameters_and_revert)
{
    int a0, a1, k;
    s32 cap0, cap1;
    CHECK_EQ(town1(1, 5), 0);
    politics_authority_add(30);
    a0 = politics_authority();
    /* housing programme: apartment growth score, construction rebate */
    CHECK_EQ(politics_decree_state(DEC_HOUSING), DS_OK);
    CHECK(politics_sign(DEC_HOUSING));
    a1 = politics_authority();
    CHECK_EQ(a0 - a1, g_decrees[DEC_HOUSING].cost);
    CHECK_EQ(politics_mod(PM_APT_SCORE_ADD), 4);
    CHECK_EQ(politics_mod(PM_CONSTRUCTION_PCT), 90);
    {
        TileInfo ti;
        int found = 0, x, y, t;
        g_budget.money += 10000;
        scr("bulldoze 104 83 108 86");       /* F3: 1x1 industrial yards now fill the gaps this test zoned over */
        scr("zone 104 83 108 86 apartments");
        for (y = 83; y <= 86 && !found; y++)
            for (x = 104; x <= 108 && !found; x++)
                if (sim_query_tile(x, y, &ti) && ti.zone == ZONE_APARTMENTS && !ti.lot)
                    for (t = 0; t < ti.nterms; t++) if (ti.terms[t].id == ST_POLICY && ti.terms[t].amount == 4) found = 1;
        test_log("housing programme: apartment score term found %d", found);
        CHECK(found);
    }
    /* repeal at half cost (rounded up) -> neutral again, cooldown 12 */
    a0 = politics_authority();
    CHECK(politics_repeal(DEC_HOUSING));
    CHECK_EQ(a0 - politics_authority(), (g_decrees[DEC_HOUSING].cost + 1) / 2);
    CHECK_EQ(politics_mod(PM_APT_SCORE_ADD), 0);
    CHECK_EQ(politics_mod(PM_CONSTRUCTION_PCT), 100);
    CHECK_EQ(politics_decree_cooldown(DEC_HOUSING), POL_COOLDOWN);
    /* regional subsidy: R demand gets a negative DT_POLICY term, money on BL_POLITICS */
    months(POL_DECREE_GAP);
    CHECK(politics_sign(DEC_REGIONAL));
    months(2);                                   /* politics books after the month's ledger closes: shown a month later */
    {
        const Demand *r = sim_demand(DEM_R);
        s32 pol = 0;
        for (k = 0; k < r->nterms; k++) if (r->terms[k].id == DT_POLICY) pol = r->terms[k].amount;
        test_log("regional subsidy: R policy term %d, BL_POLITICS last %d", pol, g_budget.last[BL_POLITICS]);
        CHECK(pol < 0);
        CHECK(g_budget.last[BL_POLITICS] <= -150);
    }
    /* school reform: school capacity +20% */
    sim_run_months(1);
    cap0 = sim_stats()->svc_capacity[SVC_SCHOOL];
    months(POL_DECREE_GAP);
    CHECK_EQ(politics_decree_state(DEC_SCHOOL), sim_date()->year >= 1965 ? DS_OK : DS_TOO_EARLY);
    politics_repeal(DEC_REGIONAL);
    {
        /* town1 ends in 1960: school reform is "not before 1965"; check the parameter via smokestack + school by
           running the clock to 1965 */
        while (sim_date()->year < 1965) months(6);
        months(POL_DECREE_GAP);
        cap0 = sim_stats()->svc_capacity[SVC_SCHOOL];
        CHECK(politics_sign(DEC_SCHOOL));
        months(1);
        cap1 = sim_stats()->svc_capacity[SVC_SCHOOL];
        test_log("school reform: school capacity %d -> %d, upkeep line %d", cap0, cap1, g_budget.last[BL_POLITICS]);
        CHECK(cap1 * 100 >= cap0 * 118 && cap1 * 100 <= cap0 * 122);
        CHECK_EQ(politics_mod(PM_SCHOOL_CAP_PCT), 120);
    }
    /* friendship celebrations: Eastern relations now and each month */
    {
        int e0;
        politics_authority_add(30);
        months(POL_DECREE_GAP);
        e0 = politics_meter(METER_EAST);
        CHECK(politics_sign(DEC_FRIENDSHIP));
        CHECK_EQ(politics_meter(METER_EAST), IMIN(100, e0 + 10));
        CHECK(politics_mod(PM_CLEARING_PCT) > 100 || politics_meter(METER_EAST) <= 50);
    }
}

TEST(politics_smokestack_cuts_industrial_pollution)
{
    SaveFile *base;
    s32 p_without, p_with;
    CHECK_EQ(town1(1, 5), 0);
    politics_authority_add(30);
    base = snap();
    months(6);
    p_without = sim_stats()->avg_pollution;
    {
        int x, y; s32 s = 0;
        for (y = 80; y < 100; y++) for (x = 102; x < 118; x++) s += field_at(FLD_POLLUTION, x, y);
        p_without = s;
    }
    restore(base);
    CHECK(politics_sign(DEC_SMOKESTACK));
    CHECK_EQ(politics_mod(PM_IND_POLLUTION_PCT), 60);
    CHECK_EQ(politics_mod(PM_IND_DEMAND_PCT), 85);
    months(6);
    {
        int x, y; s32 s = 0;
        for (y = 80; y < 100; y++) for (x = 102; x < 118; x++) s += field_at(FLD_POLLUTION, x, y);
        p_with = s;
    }
    test_log("pollution over the industrial district after 6 months: %d without, %d with smokestack standards", p_without, p_with);
    CHECK(p_with < p_without);
    save_mem_close(base);
}

TEST(politics_slots_cooldowns_gap)
{
    static const int five[5] = { DEC_RENT, DEC_REGIONAL, DEC_FRIENDSHIP, DEC_PRESS, DEC_BONFIRE };
    int k;
    CHECK_EQ(town1(1, 2), 0);
    for (k = 0; k < 5; k++) {
        politics_authority_add(40);
        CHECK_EQ(politics_decree_state(five[k]), DS_OK);
        CHECK(politics_sign(five[k]));
        CHECK_EQ(politics_decree_state(DEC_HOUSING), DS_GAP);   /* the cabinet is drafting */
        months(POL_DECREE_GAP);
    }
    CHECK_EQ(politics_active_count(), 5);
    politics_authority_add(40);
    CHECK_EQ(politics_decree_state(DEC_HOUSING), DS_NO_SLOT);
    CHECK(!politics_sign(DEC_HOUSING));
    /* one-shot decrees take no slot */
    CHECK_EQ(politics_decree_state(DEC_EMERGENCY), DS_OK);
    /* repeal frees a slot; the repealed decree cools off for 12 months */
    CHECK(politics_repeal(DEC_BONFIRE));
    CHECK_EQ(politics_decree_state(DEC_BONFIRE), DS_COOLDOWN);
    CHECK_EQ(politics_decree_state(DEC_HOUSING), DS_OK);
    months(POL_COOLDOWN - 1);
    CHECK_EQ(politics_decree_state(DEC_BONFIRE), DS_COOLDOWN);
    months(1);
    CHECK(politics_decree_state(DEC_BONFIRE) != DS_COOLDOWN);
    /* authority */
    politics_authority_add(-100);
    CHECK_EQ(politics_authority(), 0);
    CHECK_EQ(politics_decree_state(DEC_HOUSING), DS_NO_AUTHORITY);
    CHECK_EQ(politics_repeal_state(DEC_RENT), DS_NO_AUTHORITY);
    /* hidden hook decrees are not offered until a later task enables them */
    CHECK_EQ(politics_decree_state(DEC_TOWN_RIGHTS), DS_HIDDEN);
    politics_enable_hook_decree(DEC_TOWN_RIGHTS, 1);
    CHECK(politics_decree_state(DEC_TOWN_RIGHTS) != DS_HIDDEN);
    /* authority income = popularity / 20 per month, capped */
    {
        int a0 = politics_authority(), p = politics_popularity();
        months(10);
        test_log("authority 0 -> %d in 10 months at popularity ~%d (cap %d)", politics_authority(), p, politics_authority_cap());
        CHECK(politics_authority() >= a0 + 10 * IMAX(0, p - 10) / 20 - 1);
        months(200);
        CHECK(politics_authority() <= politics_authority_cap());
    }
}

/* ------------------------------------------------------------------ letters */
TEST(politics_letters)
{
    int k, building = 0, building_after = 0;
    CHECK_EQ(town1(1, 1), 0);
    politics_authority_add(30);
    g_budget.money += 20000;
    scr("zone 104 83 115 94 industry");
    for (k = 0; k < 6 * SIM_STEPS_PER_CYCLE; k++) sim_step();    /* some lots start building */
    for (k = 1; k < g_world.lot_hi; k++) if (g_world.lots[k].def && g_world.lots[k].status == LOT_BUILDING) building++;
    CHECK(politics_send_letter(LET_HOUSING));
    for (k = 1; k < g_world.lot_hi; k++) if (g_world.lots[k].def && g_world.lots[k].status == LOT_BUILDING) building_after++;
    test_log("letter to the Housing Board: %d lots under construction -> %d", building, building_after);
    CHECK(building == 0 || building_after < building);
    CHECK_EQ(politics_letter_state(LET_EMBASSY), DS_GAP);
    months(POL_LETTER_GAP);
    {
        int e0 = politics_meter(METER_EAST), w0 = politics_meter(METER_WEST);
        CHECK(politics_send_letter(LET_EMBASSY));
        CHECK_EQ(politics_meter(METER_EAST), IMIN(100, e0 + 6));
        CHECK_EQ(politics_meter(METER_WEST), IMAX(0, w0 - 2));
    }
    months(POL_LETTER_GAP);
    {
        Money m0 = g_budget.money;
        CHECK(politics_send_letter(LET_FINANCE));
        test_log("letter to the Ministry of Finance: %+d mk", g_budget.money - m0);
        CHECK(g_budget.money > m0);
    }
}

/* ------------------------------------------------------------------ events */
TEST(politics_event_choices_apply_effects)
{
    PolFx fx[POL_FX_MAX];
    int e0, ev, k;
    Money m0;
    CHECK_EQ(town1(1, 2), 0);
    while (politics_pending_event() >= 0) politics_choose(0);
    CHECK(politics_force_event(EV_DELEGATION));
    CHECK(!politics_queue_event(EV_SAUNA));          /* one card at a time */
    e0 = politics_meter(METER_EAST); m0 = g_budget.money;
    CHECK(politics_choose(0));
    CHECK_EQ(politics_meter(METER_EAST), IMIN(100, e0 + 8));
    CHECK_EQ(g_budget.money, m0 - 600);
    CHECK_EQ(politics_pending_event(), -1);
    CHECK(politics_months_to_event() >= POL_EVENT_MIN && politics_months_to_event() <= POL_EVENT_MAX);
    /* sauna diplomacy: diminishing returns */
    politics_choice_fx(EV_SAUNA, 0, fx);
    CHECK(fx_has(fx, FX_METER, METER_EAST, 12 * 2 / (2 + politics_event_count(EV_SAUNA))));
    for (ev = 0; ev < 3; ev++) { CHECK(politics_force_event(EV_SAUNA)); CHECK(politics_choose(0)); }
    politics_choice_fx(EV_SAUNA, 0, fx);
    test_log("sauna after 3 visits: East +%d (first visit +12)", fx[1].value);
    CHECK(fx_has(fx, FX_METER, METER_EAST, 12 * 2 / 5));
    /* timed effects: strike, let it run -> industrial demand -30% for 3 months */
    CHECK(politics_force_event(EV_STRIKE));
    CHECK(politics_choose(1));
    CHECK_EQ(politics_mod(PM_IND_DEMAND_PCT), 70);
    months(3);
    CHECK_EQ(politics_mod(PM_IND_DEMAND_PCT), 100);
    /* temperance card repeals medium beer */
    while (sim_date()->year < 1965) months(6);
    politics_authority_add(30);
    months(POL_DECREE_GAP);
    while (politics_pending_event() >= 0) politics_choose(0);
    CHECK(politics_sign(DEC_BEER));
    CHECK(politics_force_event(EV_TEMPERANCE));
    CHECK(politics_choose(1));
    CHECK(!politics_decree_active(DEC_BEER));
    /* an unanswered card resolves itself (first choice) after a year */
    k = politics_event_count(EV_TV);
    CHECK(politics_force_event(EV_TV));
    months(POL_EVENT_EXPIRE - 1);
    CHECK_EQ(politics_pending_event(), EV_TV);
    months(1);
    CHECK(politics_pending_event() != EV_TV);
    CHECK_EQ(politics_event_count(EV_TV), k + 1);
}

TEST(politics_event_rate)
{
    int m, fired = 0, last = -100, min_gap = 999, prev = -1, gaps = 0, campaign = 0;
    FILE *f = fopen("out/politics_events.txt", "w");
    CHECK_EQ(town1(1, 0), 0);
    for (m = 0; m < 20 * 12; m++) {
        int ev;
        months(1);
        ev = politics_pending_event();
        if (ev >= 0 && ev != prev) {
            if (ev == EV_CAMPAIGN) campaign++;
            else {
                fired++;
                if (last >= 0) { min_gap = IMIN(min_gap, m - last); gaps++; }
                last = m;
            }
            if (f) fprintf(f, "%d-%02d %s\n", sim_date()->year, sim_date()->month + 1, g_events[ev].key);
            politics_choose(ev == EV_CAMPAIGN ? 0 : (m % g_events[ev].nch));
        }
        prev = politics_pending_event();
    }
    if (f) fclose(f);
    test_log("20 years: %d cards (+%d campaign cards), %d gaps, shortest %d months", fired, campaign, gaps, min_gap);
    CHECK(fired >= 8 && fired <= 20);           /* <= 1 per 1-2 game years */
    CHECK(min_gap >= POL_EVENT_MIN);
    CHECK(campaign >= 2);
}

/* ------------------------------------------------------------------ elections */
TEST(politics_elections_margin_and_emergency_law)
{
    const PolElection *e;
    int y0, held0, ok_math;
    CHECK_EQ(town1(1, 0), 0);
    y0 = politics_next_election();
    test_log("start 1955: first election January %d", y0);
    CHECK_EQ(y0, 1962);
    while (politics_elections_held() == 0) { months(1); if (politics_pending_event() >= 0) politics_choose(politics_pending_event() == EV_CAMPAIGN ? 1 : 0); }
    e = politics_last_election();
    CHECK(e != NULL);
    ok_math = e->electors == 151 + ICLAMP((e->share - 40) * 109 / 35, 0, 129);
    test_log("election %d: %d electors (share %d = popularity + momentum %d / 2 + campaign %d + turnout %d), verdict %d, term %+d",
             e->year, e->electors, e->share, e->momentum, e->campaign, e->turnout, e->verdict, politics_term_mod());
    CHECK(ok_math);
    CHECK(e->electors >= 151 && e->electors <= 280);
    CHECK_EQ(e->verdict, e->electors < 170 ? -1 : e->electors >= 230 ? 1 : 0);
    CHECK_EQ(politics_term_mod(), e->verdict);
    CHECK_EQ(politics_authority_cap(), POL_AUTH_CAP + 5 * e->verdict);
    CHECK_EQ(e->campaign, 8);
    CHECK_EQ(politics_next_election(), 1968);
    /* emergency powers law: skips 1968 */
    politics_authority_add(40);
    while (politics_decree_gap() > 0) months(1);
    held0 = politics_elections_held();
    {
        int s0 = politics_meter(METER_WEST);
        CHECK(politics_sign(DEC_EMERGENCY));
        CHECK_EQ(politics_next_election(), 1974);
        CHECK_EQ(politics_meter(METER_WEST), IMAX(0, s0 - 10));
        CHECK_EQ(politics_active_count(), 0);
    }
    months(POL_DECREE_GAP);
    CHECK_EQ(politics_decree_state(DEC_EMERGENCY), DS_ALREADY);
    while (sim_date()->year < 1969) { months(1); if (politics_pending_event() >= 0) politics_choose(0); }
    CHECK_EQ(politics_elections_held(), held0);
    CHECK(politics_last_election()->skipped);
    while (sim_date()->year < 1975) { months(1); if (politics_pending_event() >= 0) politics_choose(0); }
    CHECK_EQ(politics_elections_held(), held0 + 1);
    test_log("1974: %d electors, verdict %d", politics_last_election()->electors, politics_last_election()->verdict);
}

/* margin math at the extremes: a hated president still wins 151-149; a beloved one gets a landslide */
TEST(politics_election_extremes)
{
    int lo, hi;
    CHECK_EQ(town1(1, 0), 0);
    budget_set_tax(20, 20);
    politics_meter_add(METER_EAST, -60);
    politics_meter_add(METER_WEST, -60);
    while (politics_elections_held() == 0) { months(1); if (politics_pending_event() >= 0) politics_choose(0); }
    lo = politics_last_election()->electors;
    CHECK_EQ(town1(1, 0), 0);
    budget_set_tax(4, 4);
    g_budget.money += 400000;
    while (politics_elections_held() == 0) {
        months(1);
        politics_meter_add(METER_WEST, 3);
        if (politics_pending_event() >= 0) politics_choose(politics_pending_event() == EV_CAMPAIGN ? 2 : 0);
    }
    hi = politics_last_election()->electors;
    test_log("taxes 20%% + cold relations: %d electors (verdict %d); taxes 4%% + campaign: %d electors (verdict %d)", lo,
             lo < 170 ? -1 : lo >= 230 ? 1 : 0, hi, hi < 170 ? -1 : hi >= 230 ? 1 : 0);
    CHECK(lo >= 151 && lo < 170);
    CHECK(hi > lo + 30);
}

/* ------------------------------------------------------------------ determinism, save round-trip */
TEST(politics_determinism_and_save_roundtrip)
{
    SaveFile *sf, *sf2;
    u32 a, c, ph0, ph1;
    int n1, n2, same;
    CHECK_EQ(town1(1, 3), 0);
    politics_authority_add(20);
    CHECK(politics_sign(DEC_RENT));
    months(14);
    if (politics_pending_event() < 0) politics_force_event(EV_STRIKE);
    CHECK(politics_choose(0));
    CHECK(politics_force_event(EV_SAUNA));          /* a pending card, timed effects and a decree in the save */
    ph0 = politics_hash();
    sf = snap();
    months(30);
    a = sim_hash();
    /* load: the politics state is restored exactly, a re-save is byte-identical */
    restore(sf);
    ph1 = politics_hash();
    CHECK(politics_enabled());
    CHECK(politics_decree_active(DEC_RENT));
    CHECK_EQ(politics_pending_event(), EV_SAUNA);
    CHECK_EQ(ph1, ph0);
    sf2 = snap();
    n1 = save_mem_size(sf); n2 = save_mem_size(sf2);
    {   /* the POLI chunk and everything after it (META holds the save time) */
        int o1 = 0, o2 = 0, i;
        for (i = 8; i + 4 <= n1; i++) if (!memcmp(save_mem_data(sf) + i, "POLI", 4)) { o1 = i; break; }
        for (i = 8; i + 4 <= n2; i++) if (!memcmp(save_mem_data(sf2) + i, "POLI", 4)) { o2 = i; break; }
        same = o1 && o1 == o2 && n1 == n2 && !memcmp(save_mem_data(sf) + o1, save_mem_data(sf2) + o2, n1 - o1 - 16);
    }
    /* a second independent run from scratch gives the same city */
    CHECK_EQ(town1(1, 3), 0);
    politics_authority_add(20);
    CHECK(politics_sign(DEC_RENT));
    months(14);
    if (politics_pending_event() < 0) politics_force_event(EV_STRIKE);
    CHECK(politics_choose(0));
    CHECK(politics_force_event(EV_SAUNA));
    months(30);
    c = sim_hash();
    test_log("politics hash at save %08x, after load %08x; re-save identical %d (%d/%d bytes); 30 months later: run 1 %08x,"
             " run 2 %08x", ph0, ph1, same, n1, n2, a, c);
    CHECK(same);
    CHECK_EQ(a, c);
    save_mem_close(sf);
    save_mem_close(sf2);
}

/* old saves (no POLI chunk) load with fresh politics according to the saved setting */
TEST(politics_old_save_loads)
{
    SaveMeta m;
    FILE *f = fopen("../AUTO1.KAU", "rb");
    if (!f) { test_log("../AUTO1.KAU missing: skipped"); return; }
    fclose(f);
    CHECK(load_game("../AUTO1.KAU", &m));
    test_log("AUTO1: politics %d (setting %d), popularity %d", politics_enabled(), sim_settings()->politics, politics_popularity());
    CHECK_EQ(politics_enabled(), sim_settings()->politics);
}
