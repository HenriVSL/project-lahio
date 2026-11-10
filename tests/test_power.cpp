/* test_power.cpp - 1.2 S1 "Power" (docs/DESIGN_1_2.md 2-4): plant rows, fuel rules (station surcharge, peat immune), the
   reactor letter (gate, price, build months, clearing import, politics effects, save chunk, old saves), the Cabinet's
   plant and the small-plant hindsight, the user's 1996 save (old-save load + the 60k city on the new plants).
   Owner: S1. Dumps: tests/out/power_*.txt. Run alone: make -f tests/Makefile.mac T=power */
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
#include "trade.h"
#include "era.h"
#include "advice.h"
#include "season.h"
#include "power.h"

/* the user's play-test 3 save (1996, Koivukoski, 58,193 residents, Normal, 32 coal plants), relative to tests/ */
#define USER_SAVE "../../../release/1.1rc1 save/AUTO1.KAU"

static void drain(void)
{
    SimEvent e;
    PolMsg m;
    EraMsg em;
    while (sim_poll_event(&e)) {}
    while (politics_poll_msg(&m)) {}
    while (era_poll_msg(&em)) {}
}
static void months(int n) { while (n-- > 0) { sim_run_months(1); tools_month_changed(); drain(); } }

static void new_city(int politics, int year, int all, int difficulty)
{
    MapGenParams mp;
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    mapgen_defaults(&mp);
    mp.seed = 7;
    mapgen_generate(&mp);
    sim_default_settings(&s);
    s.seed = 7; s.politics = politics; s.start_year = year; s.all_unlocked = all; s.difficulty = difficulty;
    sim_new_city(&s);
    tools_reset();
    tools_script_reset();
    g_budget.money = 500000;
}

/* the first position (row scan from (cx, cy) outwards) where the def fits, optionally next to water */
static int find_site(int def, int cx, int cy, int *ox, int *oy)
{
    int r, x, y;
    for (r = 0; r < 120; r++)
        for (y = cy - r; y <= cy + r; y++)
            for (x = cx - r; x <= cx + r; x++) {
                if (IMAX(IABS(x - cx), IABS(y - cy)) != r) continue;
                if (world_lot_check(def, x, y, 0) == PLACE_OK) { *ox = x; *oy = y; return 1; }
            }
    return 0;
}

static int place_active(int def, int cx, int cy)
{
    int x, y;
    if (!find_site(def, cx, cy, &x, &y)) return 0;
    return world_lot_place(def, x, y, ROT_0, 0, LOT_ACTIVE);
}

/* ------------------------------------------------------------------ the rows */
TEST(power_defs_rows)
{
    const BuildingDef *st = def_building(B_COAL_STATION), *pe = def_building(B_PEAT_PLANT), *nu = def_building(B_NUCLEAR_PLANT);
    /* saved def ids of 1.1 keep their values: the station is the last row, the old plants where they were */
    CHECK_EQ(B_COAL_PLANT, 26); CHECK_EQ(B_NUCLEAR_PLANT, 30); CHECK_EQ(B_FRIENDSHIP_STATUE + 1, B_COAL_STATION);
    CHECK_EQ(B_COAL_STATION + 1, B_COUNT);
    CHECK(B_COUNT < 256);                                   /* Lot.def is a u8 */
    CHECK_EQ(def_selfcheck(), 0);
    CHECK_EQ(def_building_find("coal_station"), B_COAL_STATION);
    CHECK(DEF_IS_PLANT(B_COAL_STATION) && DEF_IS_PLANT(B_HYDRO_PLANT) && !DEF_IS_PLANT(B_PUMP));
    CHECK(st->cat == BCAT_POWER && st->w == 3 && st->h == 3 && st->status_req == TS_TOWN && st->era_year == 1962 && st->capacity == 2000);
    CHECK(pe->status_req == TS_MARKET_TOWN && pe->era_year == 1972 && pe->capacity == 1000);
    CHECK(nu->status_req == TS_REGIONAL_CENTRE && nu->era_year == 1977 && (nu->flags & BDF_NEEDS_SHORE) && nu->capacity == 5000 && !(nu->flags & BDF_SMOKE));
    /* the 1.1 rows are unchanged (a game without the new plants plays like 1.1) */
    CHECK(def_building(B_COAL_PLANT)->cost == 2500 && def_building(B_COAL_PLANT)->upkeep == 110 && def_building(B_COAL_PLANT)->capacity == 500);
    CHECK(def_building(B_OIL_PLANT)->cost == 5000 && def_building(B_OIL_PLANT)->upkeep == 100 && def_building(B_OIL_PLANT)->capacity == 900);
    CHECK(def_building(B_HYDRO_PLANT)->cost == 6000 && def_building(B_HYDRO_PLANT)->capacity == 600);
    /* per 1,000 units of upkeep: small coal 220 > station 125 < peat 140 > nuclear 90 (BALANCE.md 12) */
    CHECK(st->upkeep * 1000 / st->capacity < def_building(B_COAL_PLANT)->upkeep * 1000 / 500);
    CHECK(nu->upkeep * 1000 / nu->capacity < st->upkeep * 1000 / st->capacity);
    CHECK(st->upkeep * 1000 / st->capacity < pe->upkeep * 1000 / pe->capacity);
    /* and the station is dearer per unit to build than small plants: a lump, not a free upgrade */
    CHECK(st->cost * 500 > def_building(B_COAL_PLANT)->cost * st->capacity);
    test_log("station %d mk/%d/mo %d units; peat %d/%d %d; nuclear %d/%d %d", (int)st->cost, (int)st->upkeep, st->capacity,
             (int)pe->cost, (int)pe->upkeep, pe->capacity, (int)nu->cost, (int)nu->upkeep, nu->capacity);
}

TEST(power_guide_rows_order)
{
    PowerGuideRow r[8];
    char note[200];
    int n;
    new_city(0, 1955, 0, DIFF_EASY);
    n = power_guide_rows(r, 8);
    CHECK_EQ(n, 6);
    CHECK_EQ(r[0].def, B_COAL_PLANT); CHECK_EQ(r[1].def, B_COAL_STATION); CHECK_EQ(r[2].def, B_OIL_PLANT);
    CHECK_EQ(r[3].def, B_PEAT_PLANT); CHECK_EQ(r[4].def, B_HYDRO_PLANT); CHECK_EQ(r[5].def, B_NUCLEAR_PLANT);
    CHECK(r[0].unlocked && !r[1].unlocked && !r[5].unlocked);
    CHECK(strstr(r[1].unlock, "Town") && strstr(r[1].unlock, "1962"));
    CHECK(strstr(r[5].unlock, "Regional centre") && strstr(r[5].unlock, "1977"));
    power_guide_note(note, sizeof(note));
    test_log("guide: %s | %s %s %s %s | note: %s", r[1].name, r[1].units, r[1].cost, r[1].upkeep, r[1].unlock, note);
}

/* ------------------------------------------------------------------ fuel (trade.cpp) and the peat plants' Rural approval */
TEST(power_fuel_station_and_peat)
{
    s32 f0, f1, f2, f3;
    int r0, r1, k;
    new_city(1, 1975, 1, DIFF_EASY);                        /* coal index 180 in 1975 */
    months(1); f0 = trade_state()->fuel_cost;
    CHECK(place_active(B_COAL_PLANT, 64, 64));
    months(1); f1 = trade_state()->fuel_cost;
    CHECK(place_active(B_COAL_STATION, 64, 64));
    months(1); f2 = trade_state()->fuel_cost;
    r0 = politics_approval_target(BLOC_RURAL);
    CHECK(place_active(B_PEAT_PLANT, 64, 64));
    months(1); f3 = trade_state()->fuel_cost;
    r1 = politics_approval_target(BLOC_RURAL);
    test_log("1975 fuel: none %d, + coal plant %d, + station %d, + peat %d; Rural target %d -> %d (peat +%d)", f0, f1, f2, f3, r0, r1, power_peat_rural());
    CHECK(f1 > f0);
    CHECK(IABS((f2 - f1) - POWER_STATION_COAL_X * (f1 - f0)) <= POWER_STATION_COAL_X);   /* the station pays four small plants'
                                                                                         surcharge (x the upkeep index, rounded) */
    CHECK_EQ(f3, f2);                                       /* peat: no import price */
    CHECK_EQ(power_peat_rural(), POWER_PEAT_RURAL);
    for (k = 0; k < 3; k++) CHECK(place_active(B_PEAT_PLANT, 64, 64));
    CHECK_EQ(power_peat_rural(), POWER_PEAT_RURAL_MAX);     /* small and steady: capped */
    {   /* before 1974 the station pays no surcharge */
        new_city(0, 1965, 1, DIFF_EASY);
        CHECK(place_active(B_COAL_STATION, 64, 64));
        months(1);
        CHECK_EQ(trade_state()->fuel_cost, 0);
    }
}

/* ------------------------------------------------------------------ the reactor letter */
static int nuclear_site(int *ox, int *oy) { return find_site(B_NUCLEAR_PLANT, 64, 64, ox, oy); }

TEST(power_reactor_letter_gate_and_east)
{
    int x, y, id, k, east0;
    Money m0;
    s32 b_ctrl, b_east;
    const Lot *l;
    SaveFile *sf;
    u32 wh, sh;
    /* the control: the same town without a reactor, the clearing balance after 6 months */
    new_city(1, 1978, 1, DIFF_EASY);
    months(6); b_ctrl = trade_state()->balance;
    new_city(1, 1978, 1, DIFF_EASY);
    CHECK_EQ(power_reactor_choice(), RX_NONE);
    CHECK(power_reactor_needed());
    CHECK_EQ(power_hash(), 0u);
    CHECK(nuclear_site(&x, &y));
    CHECK_EQ(tools_apply_build(B_NUCLEAR_PLANT, x, y, 0), 0);
    CHECK_EQ(tools_last_reason(), REJ_REACTOR);             /* the letter first */
    {   /* the shore: an inland site is refused */
        int ix, iy, found = 0;
        for (iy = 10; iy < g_world.h - 10 && !found; iy++)
            for (ix = 10; ix < g_world.w - 10 && !found; ix++)
                if (world_lot_check(B_COAL_STATION, ix, iy, 0) == PLACE_OK && world_lot_check(B_NUCLEAR_PLANT, ix, iy, 0) == PLACE_NEEDS_SHORE) found = 1;
        CHECK(found);
    }
    east0 = politics_meter(METER_EAST);
    CHECK(power_reactor_choose(RX_EAST));
    CHECK(!power_reactor_choose(RX_WEST));                  /* once a game */
    CHECK(!power_reactor_needed());
    CHECK_EQ(politics_meter(METER_EAST), east0 + 8);
    CHECK_EQ(power_build_cost(B_NUCLEAR_PLANT), 21000);
    m0 = 0;
    id = tools_apply_build(B_NUCLEAR_PLANT, x, y, 0);
    { int lx, ly; tools_last_commit(&lx, &ly, &m0); }      /* the sandbox (all unlocked) does not charge: read the price */
    CHECK(id > 0);
    l = world_lot(id);
    CHECK_EQ(l->status, LOT_BUILDING);
    CHECK_EQ(l->build_left, 18);
    test_log("reactor price %d mk (21,000 + the site)", (int)m0);
    CHECK(m0 >= 21000 && m0 < 21000 + 400);                 /* + clearing the site */
    CHECK_EQ(power_clearing_import(), POWER_REACTOR_IMPORT);
    CHECK(power_hash() != 0);
    months(6);
    b_east = trade_state()->balance;
    test_log("clearing balance after 6 months: control %d, Eastern reactor %d (import %d a month; an empty town's deficit stops at "
             "3 x the tolerance)", b_ctrl, b_east, trade_state()->reactor_import);
    CHECK_EQ(trade_state()->reactor_import, POWER_REACTOR_IMPORT);
    CHECK(b_east < b_ctrl);
    CHECK_EQ(net_source_output(id), 0);                     /* no power while it is built */
    {   /* the query panel: the reactor and the construction months */
        char ln[128];
        int f, nl = 0;
        for (k = 0; power_lot_line(id, k, ln, sizeof(ln), &f); k++) { test_log("query: %s", ln); nl++; }
        CHECK(nl >= 3);
        power_lot_line(id, 0, ln, sizeof(ln), &f);
        CHECK(strstr(ln, "Eastern reactor") != NULL);
        power_lot_line(id, 1, ln, sizeof(ln), &f);
        CHECK(strstr(ln, "12 months") != NULL);
    }
    /* save mid-construction: the choice and the lot come back */
    sf = save_mem_open(); save_game_to(sf, NULL);
    wh = world_hash(); sh = sim_hash();
    power_reset();
    CHECK_EQ(power_reactor_choice(), RX_NONE);
    save_mem_reopen(sf);
    CHECK(load_game_from(sf, NULL));
    save_mem_close(sf);
    CHECK_EQ(power_reactor_choice(), RX_EAST);
    CHECK_EQ(world_hash(), wh); CHECK_EQ(sim_hash(), sh);
    months(12);
    l = world_lot(id);
    CHECK_EQ(l->status, LOT_ACTIVE);
    CHECK_EQ(power_clearing_import(), 0);
    CHECK_EQ(power_online_year(), 1979);
    CHECK(net_source_output(id) > 0 || sim_stats()->power_demand == 0);
    {
        char head[48], body[240];
        CHECK(power_news_filler(1980, head, sizeof(head), body, sizeof(body)));
        CHECK(!power_news_filler(1979, head, sizeof(head), body, sizeof(body)));
        test_log("January 1980 filler: %s: %s", head, body);
    }
}

TEST(power_reactor_west_and_loviisa)
{
    int x, y, id, auth0;
    char t[128];
    new_city(1, 1978, 1, DIFF_EASY);
    auth0 = politics_authority();
    CHECK(power_reactor_choose(RX_WEST));
    CHECK_EQ(politics_authority(), auth0 - 2);              /* Moscow expresses interest */
    CHECK_EQ(power_build_cost(B_NUCLEAR_PLANT), 30000);
    CHECK(nuclear_site(&x, &y));
    id = tools_apply_build(B_NUCLEAR_PLANT, x, y, 0);
    CHECK(id > 0 && world_lot(id)->build_left == 12);
    CHECK_EQ(power_clearing_import(), 0);
    power_reactor_terms(RX_WEST, t, sizeof(t)); test_log("west: %s", t);
    new_city(0, 1978, 1, DIFF_EASY);                        /* politics off: no bloc effects, the price and months still apply */
    CHECK(power_reactor_choose(RX_LOVIISA));
    CHECK_EQ(power_build_cost(B_NUCLEAR_PLANT), 25500);
    CHECK(nuclear_site(&x, &y));
    id = tools_apply_build(B_NUCLEAR_PLANT, x, y, 0);
    CHECK(id > 0 && world_lot(id)->build_left == 24);
    CHECK_EQ(power_clearing_import(), POWER_REACTOR_IMPORT / 2);
    power_reactor_terms(RX_LOVIISA, t, sizeof(t)); test_log("loviisa: %s", t);
    {   /* after clearing trade ends the Eastern parts are paid in markka */
        new_city(0, 1991, 1, DIFF_EASY);
        months(1);
        CHECK_EQ(power_reactor_cost_pct(RX_EAST), 90);
        CHECK(power_reactor_choose(RX_EAST));
        CHECK(nuclear_site(&x, &y));
        id = tools_apply_build(B_NUCLEAR_PLANT, x, y, 0);
        CHECK(id > 0);
        CHECK_EQ(power_clearing_import(), 0);
    }
    {   /* script: "reactor west" (the VM -script replay path) */
        int w;
        new_city(0, 1978, 1, DIFF_EASY);
        CHECK_EQ(tools_script_line("reactor west", &w, NULL), SCR_OK);
        CHECK_EQ(power_reactor_choice(), RX_WEST);
        CHECK_EQ(tools_script_line("reactor east", &w, NULL), SCR_ERROR);
    }
}

/* ------------------------------------------------------------------ the Cabinet's plant (the hindsight: the user's save below) */
TEST(power_advice_pick_and_hindsight)
{
    int k;
    new_city(0, 1955, 0, DIFF_EASY);
    CHECK_EQ(power_advice_pick(), B_NONE);                  /* a village: the small coal plant is the answer */
    new_city(0, 1966, 1, DIFF_EASY);
    for (k = 0; k < 2; k++) CHECK(place_active(B_COAL_PLANT, 64, 64));
    g_budget.money = 15000;                                 /* (the sandbox has the reactor too: 21,000 at the least) */
    CHECK_EQ(power_advice_pick(), B_COAL_STATION);
    g_budget.money = 5000;
    CHECK_EQ(power_advice_pick(), B_NONE);                  /* not affordable */
    new_city(0, 1975, 1, DIFF_EASY);
    months(1);
    CHECK_EQ(power_advice_pick(), B_NUCLEAR_PLANT);         /* all unlocked: nuclear (1977 is past in the sandbox) */
}

/* ------------------------------------------------------------------ the user's 1996 save */
static int load_user(void)
{
    SaveMeta meta;
    FILE *f = fopen(USER_SAVE, "rb");
    if (!f) return 0;
    fclose(f);
    world_init(); sim_init(); tools_init(); traffic_init();
    if (!load_game(USER_SAVE, &meta)) { test_log("load failed: %s", save_last_error()); return -1; }
    tools_reset();
    return 1;
}

TEST(power_user_1996_save_loads)
{
    int r = load_user(), k;
    u32 h1, h2;
    SaveFile *sf;
    if (r == 0) { test_log(USER_SAVE " missing: skipped"); return; }
    CHECK_EQ(r, 1);
    CHECK_EQ(sim_stats()->population, 58193);
    CHECK_EQ(power_count(B_COAL_PLANT, 0), 32);
    CHECK_EQ(power_count(B_COAL_STATION, 0), 0);
    CHECK_EQ(diff_level(), DIFF_NORMAL);
    CHECK_EQ(sim_stats()->town_status, TS_REGIONAL_CENTRE);
    CHECK_EQ(power_reactor_choice(), RX_NONE);              /* old save: not decided */
    CHECK(power_reactor_needed());                          /* the letter waits for the first visit to the tool */
    CHECK_EQ(power_hash(), 0u);
    /* new-build round trip: identical state (no POWR chunk written: nothing decided) */
    sf = save_mem_open(); save_game_to(sf, NULL);
    h1 = sim_hash();
    save_mem_reopen(sf);
    CHECK(load_game_from(sf, NULL));
    save_mem_close(sf);
    CHECK_EQ(sim_hash(), h1);
    /* and it plays on deterministically */
    months(6); h1 = sim_hash();
    r = load_user(); CHECK_EQ(r, 1);
    months(6); h2 = sim_hash();
    CHECK_EQ(h1, h2);
    for (k = 0; k < 1; k++) test_log("1996 save: pop %d after 6 months, power %d/%d, short %d", sim_stats()->population,
                                     sim_stats()->power_demand, sim_stats()->power_supply, sim_stats()->power_short_lots);
}

/* the 60k city on the new plants (DESIGN_1_2 1: "a 60k city runs on 6-10 plants"): load the 1996 save, build the
   replacement plants next to the network while the coal plants still run, then bulldoze the 32 coal plants and play
   24 months. Variants: control (32 coal plants), stations only, Western nuclear + stations, peat only. The treasury is
   topped up equally in every variant so only the plant costs differ; tests/out/power_60k.txt. */
typedef struct { const char *name; int plants, dark_months, short_max, pop_end; s32 util, fuel, upfront; } Variant;

static int site_near_net(int def, int *ox, int *oy)
{
    int x, y, i, j, w, h, tx, ty, best = 1 << 30;
    def_footprint(def, 0, &w, &h);
    if (!tools_town_hall(&tx, &ty)) { tx = g_world.w / 2; ty = g_world.h / 2; }
    for (y = 1; y + h < g_world.h - 1; y++)
        for (x = 1; x + w < g_world.w - 1; x++) {
            int d = IABS(x - tx) + IABS(y - ty), ok = 0;
            if (d >= best || world_lot_check(def, x, y, 0) != PLACE_OK) continue;
            for (j = y - 1; j <= y + h && !ok; j++)
                for (i = x - 1; i <= x + w && !ok; i++) {
                    const Tile *t = world_tile(i, j);
                    if (i >= x && i < x + w && j >= y && j < y + h) continue;
                    if ((t->zone & ZF_POWER_NET) && !TILE_ROAD(t)) ok = 1;
                }
            if (!ok) continue;
            best = d; *ox = x; *oy = y;
        }
    return best < (1 << 30);
}

static void run_variant(Variant *v, int kind, FILE *f)
{
    int i, k, x, y, n = 0, target = 16000, nuc = 0;
    s32 have = 0;
    Money m0;
    memset(v, 0, sizeof(*v));
    v->name = kind == 0 ? "control: 32 coal plants" : kind == 1 ? "coal power stations" : kind == 2 ? "Western nuclear + stations" : "peat plants";
    if (load_user() != 1) return;
    g_budget.money = 400000;
    m0 = g_budget.money;
    if (kind == 2) { power_reactor_choose(RX_WEST); }
    while (kind && have < target && n < 40) {
        int def = kind == 1 ? B_COAL_STATION : kind == 3 ? B_PEAT_PLANT : (nuc < 2 ? B_NUCLEAR_PLANT : B_COAL_STATION);
        int id;
        if (!site_near_net(def, &x, &y)) { if (f) fprintf(f, "  %s: no site for %s after %d plants\n", v->name, g_bdefs[def].key, n); break; }
        id = tools_apply_build(def, x, y, 0);
        if (!id) { if (f) fprintf(f, "  %s: build %s at %d,%d failed (%d)\n", v->name, g_bdefs[def].key, x, y, tools_last_reason()); break; }
        if (def == B_NUCLEAR_PLANT) nuc++;
        have += g_bdefs[def].capacity; n++;
    }
    v->upfront = (s32)(m0 - g_budget.money);
    if (kind == 2) months(12);                              /* the reactors are built while the coal plants run */
    if (kind) {                                             /* the coal plants go */
        for (i = 1; i < g_world.lot_hi; i++)
            if (g_world.lots[i].def == B_COAL_PLANT) world_lot_remove(i);
    }
    for (k = 0; k < 24; k++) {
        months(1);
        if (sim_stats()->power_short_lots > 0) v->dark_months++;
        v->short_max = IMAX(v->short_max, sim_stats()->power_short_lots);
        if (k >= 12) { v->util += (s32)-g_budget.last[BL_UTILITIES]; v->fuel += (s32)-g_budget.last[BL_FUEL]; }
    }
    v->util /= 12; v->fuel /= 12;
    for (i = 1, v->plants = 0; i < g_world.lot_hi; i++) if (g_world.lots[i].def != B_NONE && DEF_IS_PLANT(g_world.lots[i].def)) v->plants++;
    v->pop_end = sim_stats()->population;
    if (f) fprintf(f, "%-28s plants %2d  upfront %7d  utilities %6d/mo  fuel %4d/mo  dark months %2d (max %4d lots)  pop %6d  power %d/%d\n",
                   v->name, v->plants, (int)v->upfront, (int)v->util, (int)v->fuel, v->dark_months, v->short_max, v->pop_end,
                   sim_stats()->power_demand, sim_stats()->power_supply);
}

TEST(power_user_60k_city_on_new_plants)
{
    Variant v[4];
    FILE *f;
    int k;
    s32 save;
    f = fopen(USER_SAVE, "rb");
    if (!f) { test_log(USER_SAVE " missing: skipped"); return; }
    fclose(f);
    f = fopen("out/power_60k.txt", "w");
    if (f) fprintf(f, "1.2 S1: the user's 1996 Koivukoski (58,193 residents, Normal) on new plants, 24 months; utilities/fuel = mean of\n"
                      "months 13-24 (utilities includes the pumps, the same in every variant); upfront = construction incl. site clearing\n");
    for (k = 0; k < 4; k++) run_variant(&v[k], k, f);
    if (f) {
        for (k = 1; k < 4; k++) {
            save = (v[0].util + v[0].fuel) - (v[k].util + v[k].fuel);
            fprintf(f, "%-28s saves %5d mk/month against the coal plants; pays back its %d mk in %d months\n", v[k].name, (int)save,
                    (int)v[k].upfront, save > 0 ? (int)(v[k].upfront / save) : -1);
        }
        fclose(f);
    }
    for (k = 0; k < 4; k++) test_log("%-28s plants %2d upfront %7d util %6d fuel %4d dark %d (max %d) pop %d", v[k].name, v[k].plants,
                                     (int)v[k].upfront, (int)v[k].util, (int)v[k].fuel, v[k].dark_months, v[k].short_max, v[k].pop_end);
    CHECK_EQ(v[0].plants, 32);
    CHECK(v[1].plants >= 6 && v[1].plants <= 10);           /* DESIGN_1_2 1: 6-10 plants */
    CHECK(v[1].dark_months <= 2);                           /* the stations carry the town */
    CHECK(v[1].pop_end * 100 >= v[0].pop_end * 97);
    save = (v[0].util + v[0].fuel) - (v[1].util + v[1].fuel);
    CHECK(save > 0);                                        /* bigger plants are cheaper to run ... */
    CHECK(v[1].upfront / save >= 24);                       /* ... but no free lunch: two years or more to pay back */
}

/* DESIGN_1_2 4 on the user's town: 10 of the 32 coal plants gone = a brownout with 22 small plants while the station (and
   nuclear: Regional centre) is on offer; the Cabinet names the plant it can afford, and the January paper's hindsight is
   the small-plant lesson */
TEST(power_user_save_cabinet_and_hindsight)
{
    Advice a;
    int r = load_user(), i, gone = 0, m;
    char head[64], body[240];
    if (r == 0) { test_log(USER_SAVE " missing: skipped"); return; }
    CHECK_EQ(r, 1);
    for (i = 1; i < g_world.lot_hi && gone < 10; i++) if (g_world.lots[i].def == B_COAL_PLANT) { world_lot_remove(i); gone++; }   /* 11,000
                                                               units for ~11,200 of summer use: dark all year, 22 small plants left */
    g_budget.money = 15000;                                 /* a station, not a reactor */
    months(1);
    CHECK(sim_stats()->power_short_lots > 0);
    CHECK_EQ(power_advice_pick(), B_COAL_STATION);
    CHECK(advice_find(ADV_POWER_SHORT, &a));
    test_log("cabinet: %s", a.text);
    CHECK(strstr(a.text, "coal power station") != NULL);
    g_budget.money = 40000;
    CHECK_EQ(power_advice_pick(), B_NUCLEAR_PLANT);
    CHECK(advice_find(ADV_POWER_SHORT, &a));
    CHECK(strstr(a.text, "nuclear power plant") != NULL);
    for (m = 0; m < 11; m++) {                             /* through December: the January 1997 paper; the town sheds load as */
        g_budget.money = 40000;                             /* lots decline, so a plant more goes whenever the lights come back */
        if (sim_stats()->power_short_lots == 0)
            for (i = 1; i < g_world.lot_hi; i++) if (g_world.lots[i].def == B_COAL_PLANT) { world_lot_remove(i); break; }
        months(1);
    }
    advice_dump(stdout);
    CHECK_EQ(sim_date()->year, 1997);
    {
        const NewsIssue *n = era_news_kind(NEWS_YEAR);
        CHECK(n != NULL);
        test_log("January %d hindsight: %s: %s", n->year, n->hind_head, n->hind);
        CHECK(strstr(n->hind_head, "SMALL PLANTS") != NULL || strstr(n->hind_head, "WINTER") != NULL);   /* a hard winter's line wins */
    }
    CHECK(advice_hindsight(head, sizeof(head), body, sizeof(body)) >= 0);
}
