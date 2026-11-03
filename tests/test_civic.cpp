/* test_civic.cpp - M5 services & housing (civic.h, fire.cpp): element cap, district heating (nearest first,
   shortage), sewage + lake tint + shore penalty, the phone requirement, fires (start only outside cover, capped spread,
   brigade, rubble, disasters off), save round trip and determinism. Owner: M5. Dumps: tests/out/civic_*.txt.
   The 20-year bot runs with these systems are in test_balance.cpp (tests/out/civic_<style>_<map>.txt). */
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

static void cnew(int disasters)
{
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = 55;
    s.disasters = disasters;
    sim_new_city(&s);
    tools_reset();
    tools_set_pipes_with_roads(0);
}

static void pipes(int x0, int y0, int x1, int y1)
{
    int x, y;
    world_batch_begin();
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++) world_set_pipe(x, y, 1);
    world_batch_end();
}

static int lot(int def, int x, int y) { return world_lot_place(def, x, y, ROT_0, 0, LOT_ACTIVE); }
static void heat_solve(void) { civic_step(9, 0); }
static void sewage_solve(void) { civic_step(9, 1); }

/* ------------------------------------------------------------------ elements */
/* the pool is the factories' output; a take beyond it fails, so upgrades per month can't exceed the output */
TEST(civic_element_pool_caps_upgrades)
{
    int f, k, taken = 0;
    s32 out;
    cnew(0);
    f = lot(B_ELEMENT_FACTORY, 20, 20);
    civic_month();
    CHECK_EQ(civic_elements_output(), 0);                 /* no power, no staff: nothing */
    g_world.lots[f].flags |= LF_POWERED;
    g_world.lots[f].occupants = (u16)(g_bdefs[B_ELEMENT_FACTORY].jobs / 2);
    civic_month();
    out = civic_elements_output();
    test_log("half-staffed powered factory: %d elements a month (full %d)", out, CIVIC_ELEM_OUTPUT);
    CHECK_EQ(out, CIVIC_ELEM_OUTPUT / 2);
    for (k = 0; k < 10; k++) if (civic_elements_take(60)) taken += 60;   /* T1 -> T2 3x1 lamellitalo: +60 */
    CHECK(taken <= out && taken > out - 60);
    CHECK_EQ(civic_elements_left(), out - taken);
    civic_month();                                        /* refilled; nothing carries over */
    CHECK_EQ(civic_elements_left(), out);
}

/* ------------------------------------------------------------------ heat */
/* one heat plant at the west end of a pipe, point blocks along it: the nearest are heated, the far ones short
   (strict cut); a second plant at the far end heats the rest; heated pipe tiles carry ZF_HEAT */
TEST(civic_heat_nearest_first_and_shortage)
{
    int hp, i, ids[30], n = 0, served = 0, maxd_ok = -1, mind_short = 1 << 30, d;
    UtilSrcInfo si;
    UtilSummary us;
    cnew(0);
    hp = lot(B_HEAT_PLANT, 2, 10);
    pipes(4, 10, 125, 10);
    for (i = 0; i < 30; i++) ids[n++] = lot(B_APT_T3_2X2, 4 + i * 4, 11);   /* 32 units each = 960 > 600 */
    heat_solve();
    CHECK(civic_util_source(UT_HEAT, hp, &si));
    for (i = 0; i < n; i++) {
        civic_lot_util_src(UT_HEAT, ids[i], &d);
        if (civic_lot_util_state(UT_HEAT, ids[i]) == NLS_OK) { served++; if (d > maxd_ok) maxd_ok = d; CHECK(g_world.lots[ids[i]].flags & LF_HEAT); }
        else { CHECK_EQ(civic_lot_util_state(UT_HEAT, ids[i]), NLS_SHORT); if (d < mind_short) mind_short = d; CHECK(!(g_world.lots[ids[i]].flags & LF_HEAT)); }
    }
    test_log("one plant: %d/%d served, used %d/%d, reach %d, short %d (%d units), nearest short at %d", served, n, si.used,
             si.output, si.reach, si.lots_short, si.short_units, mind_short);
    CHECK_EQ(served, 600 / 32);
    CHECK(maxd_ok < mind_short);                          /* the furthest go cold first */
    CHECK(si.at_capacity && si.lots_short == n - served);
    CHECK(WT(5, 10)->zone & ZF_HEAT);                     /* pipe in reach */
    CHECK(!(WT(124, 10)->zone & ZF_HEAT));                /* pipe beyond the cut */
    CHECK_EQ(OVV_LEVEL(sim_overlay_value(OVL_HEAT, 0, 4, 11)), NETC_OK);
    CHECK_EQ(OVV_LEVEL(sim_overlay_value(OVL_HEAT, 0, 120, 11)), NETC_SHORTAGE);
    lot(B_HEAT_PLANT, 126, 9);                            /* touches the pipe's east end */
    heat_solve();
    civic_util_summary(UT_HEAT, &us);
    test_log("two plants: served %d short %d, supply %d demand %d, networks %d", us.lots_served, us.lots_short, us.supply,
             us.demand, us.networks);
    CHECK_EQ(us.lots_short, 0);
    CHECK_EQ(us.networks, 1);
    CHECK(WT(124, 10)->zone & ZF_HEAT);
    /* the T3 requirement: a heated T2 block may upgrade only if the network has spare for the bigger use */
    {
        const TierReq *r;
        int nr = def_tier_reqs(ZONE_APARTMENTS, 3, &r), k, has = 0;
        for (k = 0; k < nr; k++) if (r[k].req == REQ_HEAT) has = 1;
        CHECK(has);
    }
}

/* heat plants upkeep (coal included) on the "Plants and pumps" line, scaled by the M7 fuel hook */
TEST(civic_heat_fuel_upkeep)
{
    Term t[MAX_TERMS];
    int n, k;
    s32 base = 0, oil = 0;
    cnew(0);
    lot(B_HEAT_PLANT, 10, 10);
    n = budget_projection(t, MAX_TERMS);
    for (k = 0; k < n; k++) if (t[k].id == BL_UTILITIES) base = t[k].amount;
    civic_set_fuel_pct(200);
    n = budget_projection(t, MAX_TERMS);
    for (k = 0; k < n; k++) if (t[k].id == BL_UTILITIES) oil = t[k].amount;
    civic_set_fuel_pct(100);
    test_log("utilities with a heat plant: %d mk, at fuel 200%%: %d mk", base, oil);
    CHECK(base < 0 && oil < base);
}

/* ------------------------------------------------------------------ sewage + lakes */
static void lake(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) world_set_terrain(x, y, TER_WATER, 0);
}

TEST(civic_sewage_pollutes_lake_and_shore)
{
    int i, ids[40], n = 0, k, p0, p1, p2, lv;
    UtilSummary us;
    FILE *f;
    cnew(0);
    lake(40, 40, 70, 60);
    for (i = 0; i < 40; i++) {                             /* 40 watered lamellitalo blocks (4,000 residents) by the shore */
        int id = lot(B_APT_T1_2X2, 22 + 2 * (i % 8), 30 + 2 * (i / 8));
        g_world.lots[id].occupants = 100; g_world.lots[id].flags |= LF_WATERED;
        ids[n++] = id;
    }
    for (k = 0; k < 4; k++) sewage_solve();
    p0 = civic_lake_pollution(41, 41);
    civic_util_summary(UT_SEWAGE, &us);
    for (k = 0; k < 40; k++) sewage_solve();
    p1 = civic_lake_pollution(41, 41);
    lv = civic_lv_lake_term(38 >> 1, 38 >> 1);
    test_log("untreated %d residents: lake by the houses %d after 4 updates, %d after 44; far side %d; shore LV term %d,"
             " happiness %d; water overlay %02x", us.untreated, p0, p1, civic_lake_pollution(69, 59), lv,
             civic_happy_lake_term(38 >> 1, 38 >> 1), sim_overlay_value(OVL_WATER, 0, 41, 41));
    CHECK_EQ(us.untreated, 4000);
    CHECK(p1 > p0 && p1 >= CIVIC_LAKE_TINT);
    CHECK(civic_lake_pollution(69, 59) < p1);              /* it spreads, thinning out */
    CHECK(lv < 0 && lv >= -10);
    CHECK(civic_happy_lake_term(38 >> 1, 38 >> 1) < 0);
    CHECK_EQ(OVV_RAMP(sim_overlay_value(OVL_WATER, 0, 41, 41)), OVR_BAD);   /* the tint in the water overlay */
    /* a sewage works on the houses' pipes treats them: no new pollution, the lake recovers */
    lot(B_SEWAGE_WORKS, 20, 30);                           /* touches the blocks: buildings conduct */
    for (k = 0; k < 60; k++) sewage_solve();
    civic_util_summary(UT_SEWAGE, &us);
    p2 = civic_lake_pollution(41, 41);
    test_log("with a sewage works: treated %d, untreated %d, lake %d", us.used, us.untreated, p2);
    CHECK_EQ(us.untreated, 0);
    CHECK_EQ(us.used, 4000);
    CHECK(p2 < p1 / 3);
    f = fopen("out/civic_sewage.txt", "w");
    if (f) { civic_dump(f); fclose(f); }
    for (k = 0; k < n; k++) CHECK_EQ(civic_lot_util_state(UT_SEWAGE, ids[k]), NLS_OK);
}

/* ------------------------------------------------------------------ phone, services, art variants */
TEST(civic_phone_requirement_and_defs)
{
    const TierReq *r;
    int n = def_tier_reqs(ZONE_HOUSES, 3, &r), k, phone = 0;
    for (k = 0; k < n; k++) if (r[k].req == REQ_PHONE) phone = 1;
    CHECK(phone);
    CHECK(g_bdefs[B_POST].variants > 0 && g_bdefs[B_POLICE].variants > 0 && g_bdefs[B_LIBRARY].variants > 0);
    CHECK(g_bdefs[B_HEALTH_CENTRE].variants > 0 && g_bdefs[B_ELEMENT_FACTORY].variants > 0);
    CHECK(g_bdefs[B_APT_T2_3X1].variants == 2 && g_bdefs[B_APT_T3_2X2].variants == 2 && g_bdefs[B_COM_T3_2X2].variants == 2);
    CHECK(g_bdefs[B_HEAT_PLANT].variants && g_bdefs[B_SEWAGE_WORKS].variants);
    CHECK_EQ(def_building_find("heat_plant"), B_HEAT_PLANT);
    CHECK_EQ(def_building_find("sewage_works"), B_SEWAGE_WORKS);
    CHECK_EQ(def_selfcheck(), 0);
}

/* ------------------------------------------------------------------ fires */
/* a street of wooden houses; returns the first house id */
static int wooden_street(int y, int n)
{
    int i, first = 0;
    for (i = 0; i < n; i++) world_set_road(10 + i, y, ROAD_STREET);
    for (i = 0; i < n; i++) {
        int id = lot(B_HOUSE_T2, 10 + i, y + 1);
        g_world.lots[id].occupants = 6;
        if (!first) first = id;
    }
    return first;
}

static void fire_days(int n) { int k; for (k = 0; k < n; k++) fire_tick(); }

TEST(civic_fire_spread_capped_and_rubble)
{
    int first, k, rubble = 0, x;
    const FireStats *fs;
    SimEvent e;
    int ev_start = 0, ev_end = 0;
    cnew(1);
    first = wooden_street(20, 60);
    net_solve_all();
    while (sim_poll_event(&e)) {}
    CHECK(civic_fire_ignite(world_lot_at(40, 21)));
    CHECK_EQ(g_world.lots[world_lot_at(40, 21)].status, LOT_BURNING);
    CHECK(WT(40, 21)->flags & TF_BURNING);
    fire_days(40);
    fs = civic_fire_stats();
    for (x = 10; x < 70; x++) if (WT(x, 21)->flags & TF_RUBBLE) rubble++;
    while (sim_poll_event(&e)) if (e.type == SE_FIRE) { if (e.b == 0) ev_start++; else ev_end++; }
    test_log("no brigade: started %d, spread %d, put out %d, lost %d, displaced %d, rubble tiles %d, events %d/%d",
             fs->started, fs->spread, fs->put_out, fs->lots_lost, fs->displaced, rubble, ev_start, ev_end);
    CHECK_EQ(fs->burning, 0);
    CHECK(fs->spread <= CIVIC_FIRE_SPREAD_MAX);
    CHECK_EQ(fs->lots_lost + fs->put_out, 1 + fs->spread);
    CHECK_EQ(rubble, fs->lots_lost);
    CHECK_EQ(ev_start, 1); CHECK_EQ(ev_end, 1);
    UNUSED(first);
    /* ruins: the bulldozer clears them (and undo puts them back); otherwise the town clears them after a while */
    for (x = 10; x < 70; x++) if (WT(x, 21)->flags & TF_RUBBLE) break;
    if (x < 70) {
        int wait;
        g_budget.money = 10000;
        CHECK(tools_script_line("bulldoze 10 21 69 21", &wait, NULL) == SCR_OK);
        CHECK(!(WT(x, 21)->flags & TF_RUBBLE));
        CHECK(tools_undo());
        CHECK(WT(x, 21)->flags & TF_RUBBLE);
        for (k = 0; k < CIVIC_RUBBLE_MONTHS; k++) civic_month();
        CHECK(!(WT(x, 21)->flags & TF_RUBBLE));
    }
}

/* many fires with and without the brigade: spread stays capped; with a fire station on the street most are put out */
TEST(civic_fire_brigade_puts_out)
{
    int k, lost_open = 0, lost_cover = 0, out_cover = 0, started = 0;
    cnew(1);
    wooden_street(20, 40);
    net_solve_all();
    for (k = 0; k < 40; k++) {
        int l = world_lot_at(20 + (k % 20), 21);
        if (!l || g_world.lots[l].status != LOT_ACTIVE) continue;
        civic_fire_ignite(l); fire_days(30);
    }
    lost_open = civic_fire_stats()->lots_lost;
    cnew(1);
    wooden_street(20, 40);
    lot(B_FIRE_STATION, 30, 19);
    world_set_road(30, 18, ROAD_STREET);
    net_solve_all();
    test_log("fire cover on the street: %d%%", net_cov_quality(SVC_FIRE, 25, 21));
    CHECK(net_cov_quality(SVC_FIRE, 25, 21) >= CIVIC_FIRE_COVER_SAFE);
    for (k = 0; k < 40; k++) {
        int l = world_lot_at(20 + (k % 20), 21);
        if (!l || g_world.lots[l].status != LOT_ACTIVE) continue;
        started++;
        civic_fire_ignite(l); fire_days(30);
    }
    lost_cover = civic_fire_stats()->lots_lost; out_cover = civic_fire_stats()->put_out;
    test_log("40 fires: lots lost without a brigade %d, with one %d (put out %d of %d started + spread)", lost_open,
             lost_cover, out_cover, started);
    CHECK(lost_cover * 3 < lost_open);
    CHECK(out_cover > lost_cover);
}

/* start rate: only outside fire cover, never with disasters off; rare (about one outbreak in a few years for a
   few hundred unprotected wooden houses) */
static int outbreaks_in(int cycles)
{
    int k;
    s32 s0 = civic_fire_stats()->started;
    for (k = 0; k < cycles; k++) { fire_cycle(); fire_days(4); }
    return civic_fire_stats()->started - s0;
}

TEST(civic_fire_start_rate_and_disasters_off)
{
    int y, n_open, n_off, n_cover, k;
    cnew(1);
    for (y = 0; y < 6; y++) wooden_street(10 + y * 3, 60);     /* 360 houses */
    net_solve_all();
    for (k = 0; k < 4; k++) fields_step_social(k);
    test_log("fire risk avg %d at a house", field_at(FLD_FIRE_RISK, 30, 11));
    n_open = outbreaks_in(48 * 10);                              /* 10 years */
    cnew(0);
    for (y = 0; y < 6; y++) wooden_street(10 + y * 3, 60);
    net_solve_all();
    for (k = 0; k < 4; k++) fields_step_social(k);
    n_off = outbreaks_in(48 * 10);
    cnew(1);
    for (y = 0; y < 6; y++) wooden_street(10 + y * 3, 60);
    for (y = 0; y < 6; y++)                                       /* three stations along every street */
        for (k = 0; k < 3; k++) lot(B_FIRE_STATION, 20 + k * 20, 9 + y * 3);
    net_solve_all();
    for (k = 0; k < 4; k++) fields_step_social(k);
    n_cover = outbreaks_in(48 * 10);
    test_log("outbreaks in 10 years, 360 wooden houses: no brigade %d, disasters off %d, fire stations %d (cover %d%%)",
             n_open, n_off, n_cover, net_cov_quality(SVC_FIRE, 30, 17));
    CHECK(n_open >= 2 && n_open <= 25);
    CHECK_EQ(n_off, 0);
    CHECK(n_cover < n_open / 2);
}

/* ------------------------------------------------------------------ save round trip + determinism */
TEST(civic_save_roundtrip)
{
    SaveFile *sf;
    SaveMeta m;
    u32 h0, h_unbroken;
    int k;
    cnew(1);
    lake(60, 60, 90, 80);
    wooden_street(20, 40);
    for (k = 0; k < 20; k++) { int id = world_lot_at(10 + k, 21); g_world.lots[id].flags |= LF_WATERED; }
    lot(B_HEAT_PLANT, 2, 30);
    pipes(4, 30, 60, 30);
    for (k = 0; k < 8; k++) lot(B_APT_T3_2X2, 6 + k * 6, 31);
    net_solve_all();
    sim_run_months(3);
    civic_fire_ignite(world_lot_at(30, 21));
    for (k = 0; k < 9; k++) sim_step();                     /* mid-fire, mid-cycle */
    memset(&m, 0, sizeof(m));
    sf = save_mem_open();
    CHECK(save_game_to(sf, &m));
    h0 = world_hash() ^ sim_hash();
    sim_run_months(6);
    h_unbroken = world_hash() ^ sim_hash();
    save_mem_reopen(sf);
    k = load_game_from(sf, NULL);
    save_mem_close(sf);
    if (!k) test_log("load failed: %s", save_last_error());
    CHECK(k);
    CHECK_EQ(world_hash() ^ sim_hash(), h0);
    tools_reset();
    sim_run_months(6);
    test_log("unbroken %08x, after load %08x (fires %d, heated %d)", h_unbroken, world_hash() ^ sim_hash(),
             civic_fire_stats()->started, (int)civic_stat_value("heated", &k));
    CHECK_EQ(world_hash() ^ sim_hash(), h_unbroken);
}

/* ------------------------------------------------------------------ the VM demo town (screenshots)
   A 1972 town on the s7 map (Lakeland seed 7, the town1 site) with everything M5 adds, saved as
   tests/out/M5DEMO.KAU (copy to the project root for the VM: lahio.exe -load M5DEMO.KAU -script
   scripts\m5_shots.txt). Creative mode (all unlocked); grown buildings placed with the build command. */
#include "mapgen.h"
static int scr(const char *line)
{
    int wait;
    return tools_script_line(line, &wait, NULL) == SCR_OK;
}

TEST(civic_demo_town_save)
{
    MapGenParams p;
    SimSettings s;
    SaveMeta m;
    char b[96];
    int k, x, y, bad = 0;
    FILE *f;
    cnew(1);
    mapgen_defaults(&p); p.seed = 7; p.preset = MAP_PRESET_LAKELAND;
    mapgen_generate(&p);
    sim_default_settings(&s); s.seed = 7; s.start_year = 1970; s.all_unlocked = 1; s.disasters = 1;
    str_copy(s.town, "Koivula", NAME_LEN);
    sim_new_city(&s);
    tools_reset();
    g_budget.money = 200000;
    bad += !scr("townhall");
    bad += !scr("road 73 73 101 73 street"); bad += !scr("road 74 79 101 79 street"); bad += !scr("road 74 85 101 85 street");
    bad += !scr("road 74 73 74 85 street"); bad += !scr("road 83 73 83 85 street"); bad += !scr("road 93 73 93 85 street");
    bad += !scr("pipe 74 79 101 79"); bad += !scr("pipe 73 73 101 73");
    bad += !scr("power 102 73 102 85"); bad += !scr("power 74 73 102 73"); bad += !scr("power 74 79 102 79");
    bad += !scr("build coal_plant 104 76"); bad += !scr("power 102 76 103 76");
    bad += !scr("build pump 73 72"); bad += !scr("power 73 73 73 73");
    /* the concrete block: point blocks and slabs, a heat plant, district services */
    for (k = 0; k < 4; k++) { str_printf(b, sizeof(b), "build apt_t3_2x2 %d 74", 75 + 2 * k); bad += !scr(b); }
    bad += !scr("build apt_t3_3x1 75 77"); bad += !scr("build apt_t3_3x1 78 77"); bad += !scr("build apt_t2_2x2 81 76");
    bad += !scr("build heat_plant 84 74"); bad += !scr("build com_t3_2x2 86 74"); bad += !scr("build com_t3_2x1 88 74");
    bad += !scr("build police 90 74"); bad += !scr("build library 91 74"); bad += !scr("build post 92 74");
    bad += !scr("build health_centre 86 77"); bad += !scr("build sewage_works 90 77"); bad += !scr("build water_tower 92 77");
    bad += !scr("build com_t3_1x1 88 77");
    /* the wooden street south of it: rintamamiestalo houses, one already burnt down, two burning */
    for (y = 80; y <= 84; y++) for (x = 75; x <= 82; x++) { str_printf(b, sizeof(b), "build house_t2 %d %d", x, y); bad += !scr(b); }
    for (y = 80; y <= 84; y++) for (x = 84; x <= 92; x++) { str_printf(b, sizeof(b), "build house_t3 %d %d", x, y); bad += !scr(b); }
    net_solve_all();
    for (k = 1; k < g_world.lot_hi; k++) {
        Lot *l = &g_world.lots[k];
        if (l->def != B_NONE && g_bdefs[l->def].cat == BCAT_ZONE) l->occupants = (u16)g_bdefs[l->def].capacity;
    }
    civic_fire_ignite(world_lot_at(81, 84));
    for (k = 0; k < 40; k++) fire_tick();                 /* burns down (no fire station): ruins */
    for (k = 0; k < 30; k++) { civic_lake_emit(80, 76, 1500); civic_step(9, 1); }   /* years of untreated sewage */
    heat_solve();
    for (k = 0; k < 8; k++) { fields_step_social(k & 3); fields_step_land_value(k & 3); fields_step_pollution(); }
    civic_fire_ignite(world_lot_at(77, 81)); civic_fire_ignite(world_lot_at(78, 82));
    for (k = 0; k < 3; k++) fire_tick();
    test_log("demo: %d failed commands, burning %d, lost %d, heated %d, lake by the town %d, rubble at 81,84: %d",
             bad, civic_fire_stats()->burning, civic_fire_stats()->lots_lost, (int)civic_stat_value("heated", &k),
             civic_lake_pollution(72, 76), (WT(81, 84)->flags & TF_RUBBLE) != 0);
    CHECK_EQ(bad, 0);
    memset(&m, 0, sizeof(m));
    m.cam_x = 84; m.cam_y = 80;
    CHECK(save_game("out/M5DEMO.KAU", &m));
    f = fopen("out/civic_demo.txt", "w");
    if (f) { civic_dump(f); world_dump(f, 66, 66, 106, 92); fclose(f); }
}

/* the VM headless check (scripts/m5_town1.txt): same file on the Mac; compare out/m5_tools.txt (world hash) and
   out/m5_civic.txt with the VC6 run's files */
TEST(civic_m5_town1_script)
{
    MapGenParams p;
    SimSettings s;
    FILE *out;
    int errors;
    cnew(1);
    mapgen_defaults(&p); p.seed = 7; p.preset = MAP_PRESET_LAKELAND;
    mapgen_generate(&p);
    sim_default_settings(&s); s.seed = 7;
    sim_new_city(&s);
    tools_reset();
    remove("out/m5_tools.txt"); remove("out/m5_civic.txt");
    out = fopen("out/m5_town1_out.txt", "w");
    tools_script_set_dir("out/");
    errors = tools_script_run_file("../scripts/m5_town1.txt", out);
    sim_statlog_close();
    tools_script_set_dir("");
    if (out) fclose(out);
    test_log("m5_town1: %d errors, pop %d, money %d, world %08x, fires %d", errors, sim_stats()->population, g_budget.money,
             world_hash(), civic_fire_stats()->started);
    CHECK_EQ(errors, 0);
}
