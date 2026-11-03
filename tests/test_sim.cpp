/* test_sim.cpp - headless simulation tests (T04). Debug from text: dumps, CSV and reports go to
   tests/out/. The scripted town is built with world_* setters directly (tools are T08's), paying
   through budget_spend like the tools will. */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"

/* ------------------------------------------------------------------ helpers */
static void new_town(u32 seed)
{
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = seed;
    sim_new_city(&s);
}

static void road(int x0, int y0, int x1, int y1, int cls)
{
    int x, y, n = 0;
    world_batch_begin();
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!TILE_ROAD(world_tile(x, y)) && !world_tile(x, y)->lot) {
                world_set_road(x, y, cls); n++;
                /* F1: the scripted town keeps its old water layout: a pipe under every gravel road/street
                   (= "lay pipes with new roads"; mains in roads were the M1 rule) */
                if (g_roads[cls].flags & RDF_CARRIES_WATER) world_set_pipe(x, y, 1);
            }
    world_batch_end();
    budget_spend(n * (g_roads[cls].cost + ((g_roads[cls].flags & RDF_CARRIES_WATER) ? g_misc.pipe : 0)), BL_CONSTRUCTION);
}

static void pline(int x0, int y0, int x1, int y1)
{
    int x, y, n = 0;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!world_tile(x, y)->lot) { world_set_power_line(x, y, 1); n++; }
    budget_spend(n * g_misc.power_line, BL_CONSTRUCTION);
}

static int zone(int x0, int y0, int x1, int y1, int z)
{
    int x, y, n = 0;
    world_batch_begin();
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (world_is_buildable(x, y) && world_road_within(x, y, ZONE_ROAD_REACH)) { world_set_zone(x, y, z); n++; }
    world_batch_end();
    return n;
}

static int s_build_fail;
static int build(int def, int x, int y, int rot)
{
    int id, w, h, i, j;
    /* like a player: bulldoze grown buildings in the way first */
    def_footprint(def, rot, &w, &h);
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) {
            int l = world_lot_at(i, j);
            if (l && def_building(g_world.lots[l].def)->cat == BCAT_ZONE) world_lot_remove(l);
        }
    id = world_lot_place(def, x, y, rot, 0, LOT_ACTIVE);
    if (id) budget_spend(def_building(def)->cost, BL_CONSTRUCTION);
    else { s_build_fail++; test_log("build %s at %d,%d failed: %d", def_building(def)->key, x, y, world_lot_check(def, x, y, rot)); }
    return id;
}

static void lake(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) world_set_terrain(x, y, TER_WATER, 0);
}

static int count_lots(int zone_, int tier, int status)
{
    int i, n = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d = def_building(l->def);
        if (!l->def || d->cat != BCAT_ZONE) continue;
        if (zone_ && d->zone != zone_) continue;
        if (tier && d->tier != tier) continue;
        if (status >= 0 && l->status != status) continue;
        n++;
    }
    return n;
}

static void dump_file(const char *name)
{
    char path[128];
    FILE *f;
    str_printf(path, sizeof(path), "out/%s", name);
    f = fopen(path, "w");
    if (!f) return;
    sim_dump_report(f);
    world_dump(f, 0, 0, g_world.w - 1, g_world.h - 1);
    fclose(f);
}

/* ------------------------------------------------------------------ the scripted town
   Layout (x across, y down; wind blows pollution toward -y = NE):
   - national road (street, protected, edge links) along y = 64 across the map
   - lake x 4..24, y 76..112; pumping station on its east shore at (25, 90), street y = 88 to it
   - housing grid west: streets x = 30,38,46,54,62 (y 40..88) and y = 40,48,56,72,80,88 (x 30..62)
   - commercial strip along both sides of the national road x 31..61
   - industry east (x 80..104, y 44..63) with its own streets, coal plant at (112, 44)
   - power: trunk line from the plant along y = 42 to x = 30, lines along the housing streets
   - services added in stages so tier upgrades can be checked against requirements */
static void town_stage0(void)
{
    int x, y;
    lake(4, 76, 24, 112);
    road(0, 64, 127, 64, ROAD_STREET);
    for (x = 0; x < 128; x++) world_set_tile_flags(x, 64, TF_PROTECTED, 0);
    world_set_tile_flags(0, 64, TF_EDGE_LINK, 0);
    world_set_tile_flags(127, 64, TF_EDGE_LINK, 0);
    for (x = 30; x <= 62; x += 8) road(x, 40, x, 88, ROAD_STREET);
    road(30, 40, 62, 40, ROAD_STREET); road(30, 48, 62, 48, ROAD_STREET); road(30, 56, 62, 56, ROAD_STREET);
    road(30, 72, 62, 72, ROAD_STREET); road(30, 80, 62, 80, ROAD_STREET); road(26, 88, 62, 88, ROAD_GRAVEL);
    road(26, 88, 26, 104, ROAD_GRAVEL);
    /* industry streets */
    road(80, 44, 80, 63, ROAD_STREET); road(92, 44, 92, 63, ROAD_STREET); road(104, 44, 104, 63, ROAD_STREET);
    road(80, 44, 112, 44, ROAD_STREET); road(80, 54, 104, 54, ROAD_STREET);
    /* service sites first (a player reserves them), then zones */
    build(B_FIRE_STATION, 39, 57, ROT_0);
    build(B_CLINIC, 47, 57, ROT_0);
    build(B_TOWN_HALL, 63, 57, ROT_0);
    zone(31, 41, 61, 55, ZONE_HOUSES);
    zone(31, 57, 61, 60, ZONE_HOUSES);
    zone(31, 61, 61, 63, ZONE_COMMERCIAL);
    zone(31, 65, 61, 67, ZONE_COMMERCIAL);
    zone(31, 68, 61, 87, ZONE_HOUSES);
    zone(81, 45, 103, 63, ZONE_INDUSTRY);
    /* power: coal plant + trunk + street lines */
    build(B_COAL_PLANT, 112, 45, ROT_0);
    pline(30, 42, 111, 42);
    pline(111, 42, 111, 60);
    for (y = 40; y <= 88; y += 8) if (y != 64) pline(30, y, 62, y);
    pline(30, 41, 30, 88);
    /* water: two pumps on the east shore of the lake (gravel main x = 26) */
    pline(27, 80, 27, 106);               /* F1: pumping stations need power (jumps the gravel road) */
    pline(27, 88, 30, 88);
    build(B_PUMP, 25, 90, ROT_0);
    build(B_PUMP, 25, 100, ROT_0);
}

static void town_stage1(void)     /* year 2: schools + public saunas -> houses may reach tier 2 */
{
    build(B_SCHOOL, 39, 73, ROT_0);
    build(B_SCHOOL, 49, 44, ROT_0);
    build(B_SAUNA, 55, 57, ROT_0);
    build(B_SAUNA, 47, 81, ROT_0);
    build(B_SAUNA, 39, 49, ROT_0);
    build(B_SAUNA, 55, 73, ROT_0);
    build(B_SAUNA, 31, 81, ROT_0);
    build(B_COAL_PLANT, 112, 50, ROT_0);
    build(B_COAL_PLANT, 112, 55, ROT_0);
    build(B_PUMP, 25, 95, ROT_0);
}

static void town_stage2(void)     /* year 3: apartments east of the grid + school, well, clinic, park */
{
    road(62, 40, 76, 40, ROAD_STREET); road(70, 40, 70, 63, ROAD_STREET); road(76, 40, 76, 63, ROAD_STREET);
    road(62, 48, 76, 48, ROAD_STREET);
    build(B_SCHOOL, 55, 49, ROT_0);
    build(B_SAUNA, 63, 49, ROT_0);
    build(B_FIRE_STATION, 63, 41, ROT_0);
    zone(63, 41, 75, 55, ZONE_APARTMENTS);
    build(B_COAL_PLANT, 112, 35, ROT_0);
    pline(111, 35, 111, 42);
    build(B_CLINIC, 39, 81, ROT_0);
    build(B_FIRE_STATION, 55, 81, ROT_0);
    build(B_PARK, 31, 57, ROT_0);
    build(B_PUMP, 25, 105, ROT_0);
}

static void town_stage3(void)     /* year 6: water tower, more industry, more services */
{
    road(80, 30, 104, 30, ROAD_STREET); road(80, 30, 80, 44, ROAD_STREET); road(104, 30, 104, 44, ROAD_STREET);
    road(92, 30, 92, 44, ROAD_STREET);
    zone(81, 31, 103, 43, ZONE_INDUSTRY);
    build(B_WATER_TOWER, 69, 41, ROT_0);
    build(B_SCHOOL, 31, 49, ROT_0);
    build(B_SAUNA, 47, 65, ROT_0);
    build(B_COAL_PLANT, 112, 30, ROT_0);
    pline(111, 30, 111, 35);
    build(B_PUMP, 25, 80, ROT_0);
    road(26, 80, 26, 88, ROAD_GRAVEL);
}

typedef struct { int year; s32 pop, jobs, unemployed, emig, money, demR, demC, demI, h1, h2, a1, c, i, aband; } YearRow;

static double s_tsum[SIM_STEPS_PER_CYCLE], s_tmax[SIM_STEPS_PER_CYCLE];
static u32 s_tcnt[SIM_STEPS_PER_CYCLE];

static void timed_months(int months)
{
    u32 target = sim_date()->total_months + (u32)months;
    while (sim_date()->total_months < target) {
        int ph = sim_date()->step;
        double t0 = time_ms(), dt;
        sim_step();
        dt = time_ms() - t0;
        s_tsum[ph] += dt; s_tcnt[ph]++;
        if (dt > s_tmax[ph]) s_tmax[ph] = dt;
    }
}

static void timing_reset(void) { memset(s_tsum, 0, sizeof(s_tsum)); memset(s_tmax, 0, sizeof(s_tmax)); memset(s_tcnt, 0, sizeof(s_tcnt)); }

static void timing_log(const char *what)
{
    int k;
    FILE *f = fopen("out/sim_timing.txt", "a");
    for (k = 0; k < SIM_STEPS_PER_CYCLE; k++) {
        test_log("%s phase %2d: avg %.4f ms max %.4f ms (%u steps)", what, k, s_tcnt[k] ? s_tsum[k] / s_tcnt[k] : 0.0, s_tmax[k], s_tcnt[k]);
        if (f) fprintf(f, "%s phase %2d: avg %.4f ms max %.4f ms (%u steps)\n", what, k, s_tcnt[k] ? s_tsum[k] / s_tcnt[k] : 0.0, s_tmax[k], s_tcnt[k]);
    }
    if (f) fclose(f);
}

static int run_scripted_town(YearRow *rows, int years, const char *csv, u32 *hash_out, int *t2_before_school)
{
    int y, m, n = 0;
    timing_reset();
    s_build_fail = 0;
    new_town(1955);
    if (csv) sim_statlog_open(csv);
    town_stage0();
    *t2_before_school = 0;
    for (y = 0; y < years; y++) {
        if (y == 2) town_stage1();
        if (y == 3) town_stage2();
        if (y == 6) town_stage3();
        for (m = 0; m < 12; m++) {
            timed_months(1);
            if (y < 2 && count_lots(ZONE_HOUSES, 2, -1) > 0) *t2_before_school = 1;
        }
        if (rows) {
            const CityStats *st = sim_stats();
            YearRow *r = &rows[n++];
            r->year = sim_date()->year - 1; r->pop = st->population; r->jobs = st->jobs; r->unemployed = st->unemployed;
            r->emig = st->emigrated_total; r->money = g_budget.money;
            r->demR = sim_demand(DEM_R)->value; r->demC = sim_demand(DEM_C)->value; r->demI = sim_demand(DEM_I)->value;
            r->h1 = st->lots_tier[ZONE_HOUSES][1]; r->h2 = st->lots_tier[ZONE_HOUSES][2];
            r->a1 = st->lots_tier[ZONE_APARTMENTS][1];
            r->c = st->lots_tier[ZONE_COMMERCIAL][1] + st->lots_tier[ZONE_COMMERCIAL][2];
            r->i = st->lots_tier[ZONE_INDUSTRY][1] + st->lots_tier[ZONE_INDUSTRY][2];
            r->aband = st->lots_abandoned;
        }
    }
    if (csv) sim_statlog_close();
    if (hash_out) *hash_out = sim_hash();
    return n;
}

/* ------------------------------------------------------------------ tests */
TEST(sim_defs_selfcheck)
{
    const TierReq *r;
    CHECK_EQ(def_selfcheck(), 0);
    CHECK(def_tier_reqs(ZONE_HOUSES, 2, &r) == 6);      /* M2: + REQ_ROUTE (route to jobs) at the end */
    CHECK_EQ(r[3].req, REQ_SCHOOL);
    CHECK(def_tier_reqs(ZONE_HOUSES, 3, &r) == 10);    /* T12: 9 -> 8, no REQ_PHONE in M1 (no post art); M2: + REQ_ROUTE;
                                                          M5: + REQ_PHONE again (post art), from 1960 */
    CHECK(def_building(B_COAL_PLANT)->capacity == 500); /* T12: 400 -> 500 */
    CHECK(def_zone_building(ZONE_INDUSTRY, 2, 3, 3) == B_IND_T2_3X3);
}

TEST(world_roads_masks_lots)
{
    int id, rot, x0, y0, x1, y1;
    new_town(1);
    road(10, 10, 20, 10, ROAD_STREET);
    road(15, 5, 15, 15, ROAD_STREET);
    CHECK_EQ(TILE_ROAD_MASK(world_tile(15, 10)), 15);                 /* cross */
    CHECK_EQ(TILE_ROAD_MASK(world_tile(10, 10)), 1 << DIR_SE);        /* dead end pointing +x */
    CHECK_EQ(TILE_ROAD_MASK(world_tile(12, 10)), (1 << DIR_SE) | (1 << DIR_NW));
    world_set_road(15, 10, ROAD_NONE);
    CHECK_EQ(TILE_ROAD_MASK(world_tile(14, 10)), 1 << DIR_NW);
    world_set_road(15, 10, ROAD_STREET);
    /* lots */
    CHECK_EQ(world_lot_check(B_SCHOOL, 16, 11, ROT_0), PLACE_OK);
    CHECK_EQ(world_lot_check(B_SCHOOL, 14, 11, ROT_0), PLACE_ROAD);
    id = world_lot_place(B_SCHOOL, 16, 11, ROT_0, 0, LOT_ACTIVE);
    CHECK(id > 0);
    CHECK_EQ(world_lot_at(17, 12), id);
    CHECK(world_tile(17, 12)->flags & TF_LOT_FRONT);
    CHECK(!(world_tile(16, 11)->flags & TF_LOT_FRONT));
    CHECK_EQ(world_lot_check(B_CLINIC, 17, 12, ROT_0), PLACE_OCCUPIED);
    CHECK_EQ(world_lot_access(id, &rot), ROAD_STREET);
    CHECK(rot == ROT_2 || rot == ROT_3);                              /* road on the NE (y 10) or NW (x 15) side */
    CHECK_EQ(world_lot_check(B_PUMP, 30, 30, ROT_0), PLACE_NEEDS_SHORE);
    lake(31, 30, 33, 32);
    CHECK_EQ(world_lot_check(B_PUMP, 30, 30, ROT_0), PLACE_OK);
    CHECK_EQ(world_lot_check(B_TOWN_HALL, 40, 40, ROT_0), PLACE_OK);
    CHECK(world_lot_place(B_TOWN_HALL, 40, 40, ROT_0, 0, LOT_ACTIVE) > 0);
    CHECK_EQ(world_lot_check(B_TOWN_HALL, 50, 40, ROT_0), PLACE_UNIQUE);
    world_lot_rect(id, &x0, &y0, &x1, &y1);
    CHECK_EQ(x1 - x0, 1);
    world_lot_remove(id);
    CHECK_EQ(world_lot_at(17, 12), 0);
    CHECK(world_lot(id) == NULL);
    CHECK_EQ(world_footprint_road_dist(17, 13, 17, 13, 3, &rot), 2);
}

static int s_notes, s_note_kinds, s_nx0, s_nx1;
static void note_fn(int x0, int y0, int x1, int y1, int kinds, void *user)
{
    UNUSED(y0); UNUSED(y1); UNUSED(user);
    if (!s_notes || x0 < s_nx0) s_nx0 = x0;
    if (!s_notes || x1 > s_nx1) s_nx1 = x1;
    s_notes++; s_note_kinds |= kinds;
}

TEST(world_batching_and_hash)
{
    u32 h1, h2;
    new_town(1);
    world_add_listener(note_fn, NULL);
    s_notes = 0; s_note_kinds = 0;
    world_batch_begin();
    world_set_road(10, 10, ROAD_STREET);
    world_set_zone(20, 12, ZONE_HOUSES);
    world_batch_end();
    world_remove_listener(note_fn, NULL);
    CHECK_EQ(s_notes, 2);                 /* T13: far-apart changes stay separate rects (short rect list) */
    CHECK((s_note_kinds & (WCHG_ROAD | WCHG_ZONE)) == (WCHG_ROAD | WCHG_ZONE));
    CHECK_EQ(s_nx0, 9); CHECK_EQ(s_nx1, 20);
    h1 = world_hash();
    world_set_zone(21, 12, ZONE_HOUSES);
    h2 = world_hash();
    CHECK(h1 != h2);
}

/* first houses appear (construction stage) within ~1 game month of zoning when demand is high */
TEST(sim_first_houses_fast)
{
    int steps = 0;
    new_town(7);
    road(0, 64, 127, 64, ROAD_STREET);
    road(40, 50, 40, 63, ROAD_STREET);
    zone(36, 50, 44, 63, ZONE_HOUSES);
    while (count_lots(ZONE_HOUSES, 0, -1) == 0 && steps < 200) { sim_step(); steps++; }
    test_log("first house after %d steps (%d per month)", steps, SIM_STEPS_PER_MONTH);
    CHECK(steps <= SIM_STEPS_PER_MONTH);
    sim_run_months(2);
    CHECK(count_lots(ZONE_HOUSES, 1, LOT_ACTIVE) >= 10);
    CHECK(sim_stats()->population > 0);
}

/* brownout: furthest consumers lose power first */
TEST(net_power_brownout)
{
    int i, near_id, far_id, ids[80], n = 0;
    new_town(3);
    build(B_COAL_PLANT, 10, 10, ROT_0);          /* 500 units (T12: was 400; the line below 40 -> 45 lots) */
    /* a line of apartment lots (12 units each) eastwards from the plant, touching each other */
    for (i = 0; i < 45; i++) {
        int id = world_lot_place(B_APT_T1_2X2, 12 + 2 * i, 10, ROT_0, 0, LOT_ACTIVE);
        if (id) ids[n++] = id;
    }
    CHECK(n == 45);                               /* demand 540 > 500 */
    net_solve_all();
    near_id = ids[0]; far_id = ids[n - 1];
    CHECK_EQ(net_stats(NET_POWER)->supply, 500);
    CHECK_EQ(net_stats(NET_POWER)->demand, 540);
    CHECK_EQ(net_lot_power_state(near_id), NLS_OK);
    CHECK_EQ(net_lot_power_state(far_id), NLS_SHORT);
    CHECK(world_tile(12, 10)->zone & ZF_POWERED);
    CHECK((world_tile(12 + 2 * 44, 10)->zone & (ZF_POWER_NET | ZF_POWERED)) == ZF_POWER_NET);
    CHECK_EQ(net_stats(NET_POWER)->lots_served, 41);       /* 41 * 12 = 492 <= 500 */
    CHECK_EQ(net_stats(NET_POWER)->lots_short, 4);
    /* a gap breaks the network; a power line bridges it; a single road tile is jumped */
    world_lot_remove(ids[20]);
    net_solve_all();
    CHECK_EQ(net_lot_power_state(ids[21]), NLS_NONE);
    world_set_road(52, 10, ROAD_STREET); world_set_road(52, 11, ROAD_STREET);
    pline(53, 10, 53, 10);
    net_solve_all();
    CHECK(net_lot_power_state(ids[21]) != NLS_NONE);
}

/* water: pumping station capacity from shore tiles, pipes under the street, shortage order, tower
   (F1: the detailed water tests are in test_water.cpp) */
TEST(net_water_shortage)
{
    int i, ids[60], n = 0, pump, tower;
    new_town(4);
    lake(0, 20, 9, 40);
    road(10, 18, 10, 60, ROAD_STREET);
    road(10, 30, 100, 30, ROAD_STREET);
    CHECK_EQ(world_lot_check(B_PUMP, 11, 25, ROT_0), PLACE_NEEDS_SHORE);
    world_set_road(10, 25, ROAD_NONE);
    pump = build(B_PUMP, 10, 25, ROT_0);          /* west side touches the lake: 10 water tiles in reach */
    world_set_road(10, 24, ROAD_STREET); world_set_road(11, 24, ROAD_STREET);
    build(B_COAL_PLANT, 11, 25, ROT_0);           /* F1: pumping stations need power (touches the pump) */
    CHECK(pump > 0);
    for (i = 0; i < 44; i++) {                    /* apartments along the street y 30, south side */
        int id = world_lot_place(B_APT_T1_2X2, 12 + 2 * i, 31, ROT_0, 0, LOT_ACTIVE);
        if (id) ids[n++] = id;
    }
    net_solve_all();
    test_log("pump output %d, demand %d, served %d short %d unconnected %d", net_source_output(pump),
             net_stats(NET_WATER)->demand, net_stats(NET_WATER)->lots_served, net_stats(NET_WATER)->lots_short,
             net_stats(NET_WATER)->lots_unconnected);
    CHECK_EQ(net_source_output(pump), 400);       /* 10 water tiles within 2 x 40 */
    CHECK_EQ(net_lot_water_state(ids[0]), NLS_OK);
    CHECK_EQ(net_lot_water_state(ids[n - 1]), NLS_SHORT);
    CHECK(net_stats(NET_WATER)->lots_short > 0);
    CHECK(world_tile(12, 31)->zone & ZF_WATERED);
    CHECK((world_tile(99, 30)->zone & (ZF_WATER_NET | ZF_WATERED)) == ZF_WATER_NET);   /* pipe beyond reach */
    /* a water tower on the street pipe: +200 units and a pressure zone of 16 tiles */
    tower = world_lot_place(B_WATER_TOWER, 14, 29, ROT_0, 0, LOT_ACTIVE);   /* touches the street pipe */
    CHECK(tower > 0);
    net_solve_all();
    CHECK_EQ(net_source_output(tower), TOWER_OUTPUT);
    CHECK_EQ(net_source_output(pump), 400);
    CHECK(net_water_tower_reach(16, 31));
    CHECK(!net_water_tower_reach(90, 31));
    /* nothing far away: unconnected */
    CHECK_EQ(net_water_near(60, 80), NLS_NONE);
}

/* coverage: nearest building, load = residents x demand_pct, quality falls with overload */
TEST(net_coverage_load_capacity)
{
    int school, i, n = 0, cx;
    s32 load, cap;
    new_town(5);
    road(10, 20, 100, 20, ROAD_STREET);
    school = build(B_SCHOOL, 20, 18, ROT_0);           /* 2x2 at y 18..19, touching the road */
    CHECK(school > 0);
    for (i = 0; i < 40; i++) {
        int id = world_lot_place(B_APT_T1_2X2, 12 + 2 * i, 21, ROT_0, 0, LOT_ACTIVE);
        if (id) { g_world.lots[id].occupants = 150; n++; }   /* T12: 125 -> 150 so the 500-pupil school overloads */
    }
    net_coverage_step(SVC_SCHOOL);
    net_service_load(school, &load, &cap);
    test_log("school load %d / capacity %d, served %d lots, catchment %d tiles", load, cap, n, net_catchment_tiles(school));
    CHECK_EQ(cap, def_building(B_SCHOOL)->capacity);   /* T12: 400 -> 500 pupils */
    /* 25 road tiles each way covered: lots whose centre cell lies within reach */
    CHECK(load > cap && load <= 40 * 25);
    CHECK(net_building_quality(school) < 100);
    CHECK_EQ(net_cov_lot(SVC_SCHOOL, 22, 22), school);
    CHECK_EQ(net_cov_lot(SVC_SCHOOL, 95, 22), 0);       /* beyond 25 road tiles */
    cx = net_cov_quality(SVC_SCHOOL, 22, 22);
    CHECK(cx > 0 && cx < 100);
    /* a second school splits the load by distance */
    build(B_SCHOOL, 60, 18, ROT_0);
    net_coverage_step(SVC_SCHOOL);
    net_service_load(school, &load, &cap);
    CHECK(load <= 420 * 150 / 125);   /* T12: scaled with the occupants above */
    CHECK_EQ(net_cov_lot(SVC_SCHOOL, 62, 22), world_lot_at(60, 18));
    /* funding scales capacity */
    budget_set_funding(SVC_SCHOOL, 50);
    net_coverage_step(SVC_SCHOOL);
    net_service_load(school, &load, &cap);
    CHECK_EQ(cap, def_building(B_SCHOOL)->capacity / 2);   /* T12: was 200 (400 pupils) */
}

/* emigration valve: homes without jobs -> unemployment -> people leave for Sweden */
TEST(sim_emigration_unemployment)
{
    const CityStats *st;
    new_town(11);
    road(0, 64, 127, 64, ROAD_STREET);
    road(30, 40, 30, 63, ROAD_STREET); road(40, 40, 40, 63, ROAD_STREET); road(50, 40, 50, 63, ROAD_STREET);
    zone(26, 40, 54, 63, ZONE_HOUSES);
    sim_run_months(36);
    st = sim_stats();
    test_log("pop %d jobs %d unemployed %d emigrated total %d demand R %d", st->population, st->jobs,
             st->unemployed, st->emigrated_total, sim_demand(DEM_R)->value);
    CHECK(st->population > 50);      /* T12: was > 100; the jobless equilibrium is ~pull/2 and pull 1955-59 is 70 (was 80) */
    CHECK(st->jobs < st->workers);
    CHECK(st->emigrated_total > 50);
    CHECK(sim_demand(DEM_I)->target > 0);             /* idle workers want industry */
}

TEST(sim_budget_lines)
{
    Money before, sum = 0;
    int k;
    new_town(12);
    town_stage0();
    sim_run_months(13);
    before = g_budget.money;
    sim_run_months(1);
    for (k = 0; k < BL_COUNT; k++) sum += g_budget.last[k];
    test_log("money %d -> %d, net %d, res tax %d, bus tax %d, roads %d, utilities %d, fire %d",
             before, g_budget.money, sum, g_budget.last[BL_TAX_RES], g_budget.last[BL_TAX_BUS],
             g_budget.last[BL_ROADS], g_budget.last[BL_UTILITIES], g_budget.last[BL_SVC(SVC_FIRE)]);
    CHECK_EQ(g_budget.money - before, sum);
    CHECK(g_budget.last[BL_TAX_RES] > 0);
    CHECK(g_budget.last[BL_GRANTS] >= 0);
    CHECK(g_budget.last[BL_ROADS] < 0);
    CHECK(g_budget.last[BL_UTILITIES] < 0);
    CHECK_EQ(g_budget.last[BL_SVC(SVC_FIRE)], -def_building(B_FIRE_STATION)->upkeep);  /* T12: was -30 (1956: index 100%) */
    /* emergency loan after 6 negative months */
    g_budget.money = -50000;
    sim_run_months(7);
    CHECK(g_budget.loans_taken >= 1);
    CHECK(g_budget.loan > 0);
}

/* the scripted town: 15 years, CSV, curves checked numerically, determinism */
TEST(sim_scripted_town)
{
    YearRow rows[20], rows2[20];
    u32 h1, h2;
    int n, i, t2b, t2b2;
    FILE *f;
    n = run_scripted_town(rows, 15, "out/town_stats.csv", &h1, &t2b);
    dump_file("town_end.txt");
    f = fopen("out/town_years.txt", "w");
    test_log("year   pop  jobs unemp  emig   money  dR  dC  dI   H1   H2  A1   C   I  ab");
    for (i = 0; i < n; i++) {
        char line[160];
        str_printf(line, sizeof(line), "%d %5d %5d %5d %5d %7d %3d %3d %3d %4d %4d %3d %3d %3d %3d", rows[i].year,
                   rows[i].pop, rows[i].jobs, rows[i].unemployed, rows[i].emig, rows[i].money, rows[i].demR,
                   rows[i].demC, rows[i].demI, rows[i].h1, rows[i].h2, rows[i].a1, rows[i].c, rows[i].i, rows[i].aband);
        test_log("%s", line);
        if (f) fprintf(f, "%s\n", line);
    }
    if (f) fclose(f);
    remove("out/sim_timing.txt");
    timing_log("town");
    CHECK_EQ(s_build_fail, 0);
    /* tier rules: no tier-2 houses before school + sauna (stage1 at year 2) */
    CHECK(!t2b);
    CHECK(rows[1].h2 == 0);
    CHECK(rows[4].h2 > 0);                  /* upgrades happen once requirements are met */
    /* growth */
    CHECK(rows[0].pop > 200);
    CHECK(rows[4].pop > rows[1].pop);
    CHECK(rows[n - 1].pop > 3000);
    CHECK(rows[n - 1].a1 > 0);
    CHECK(rows[n - 1].i > 0 && rows[n - 1].c > 0);
    CHECK(rows[n - 1].jobs > rows[n - 1].pop / 4);
    /* determinism: same script, same hash */
    run_scripted_town(rows2, 15, NULL, &h2, &t2b2);
    test_log("hash %08X vs %08X", h1, h2);
    CHECK_EQ(h1, h2);
    CHECK_EQ(rows2[n - 1].pop, rows[n - 1].pop);
}

TEST(sim_query_and_overlay)
{
    int x, y, found = 0, i;
    LotInfo li;
    TileInfo ti;
    SimProblem pr[8];
    int np;
    new_town(21);
    town_stage0();
    sim_run_months(6);
    for (i = 1; i < g_world.lot_hi && !found; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_HOUSE_T1 && l->status == LOT_ACTIVE) found = i;
    }
    CHECK(found);
    CHECK(sim_query_lot(found, &li));
    CHECK_EQ(li.tier, 1);
    CHECK(li.next.n == 6);                  /* road power water school sauna + M2 route to jobs */
    CHECK(li.next.item[0].req == REQ_ROAD && li.next.item[0].met);
    CHECK(li.next.item[3].req == REQ_SCHOOL && !li.next.item[3].met);
    CHECK(li.nterms > 0);
    CHECK(sim_query_tile(li.lot ? g_world.lots[found].x : 0, g_world.lots[found].y, &ti));
    np = sim_top_problems(pr, 8);
    test_log("top problems: %d, first req %d count %d residents %d", np, np ? pr[0].req : -1, np ? pr[0].count : 0, np ? pr[0].residents : 0);
    CHECK(np > 0);
    CHECK(pr[0].req == REQ_SCHOOL || pr[0].req == REQ_SAUNA || pr[0].req == REQ_POWER || pr[0].req == REQ_WATER);
    /* overlays return sane codes */
    for (y = 0; y < 128; y += 3)
        for (x = 0; x < 128; x += 3) {
            u8 v = sim_overlay_value(OVL_POWER, 0, x, y);
            CHECK(v == OVV_NONE || OVV_RAMP(v) == OVR_NET);
            v = sim_overlay_value(OVL_LAND_VALUE, 0, x, y);
            CHECK(v == OVV_NONE || OVV_RAMP(v) == OVR_GOOD);
            v = sim_overlay_value(OVL_GROWTH, 0, x, y);
            CHECK(v == OVV_NONE || (OVV_RAMP(v) == OVR_PROBLEM && OVV_LEVEL(v) < GROWC_COUNT));
        }
    CHECK(OVV_RAMP(sim_overlay_value(OVL_CATCHMENT, world_lot_at(39, 57), 39, 57)) == OVR_COVER);
}

/* ------------------------------------------------------------------ dense city timing
   A worst-case 128x128 town: street grid every 8 tiles, everything zoned, enough plants, pumps and
   services; run until it is dense, then time every phase and every solver separately. */
static void dense_city(void)
{
    int x, y, bx, by;
    new_town(77);
    lake(0, 0, 127, 2);                            /* north shore for pumps (row 3), street row 4 */
    for (y = 4; y < 128; y += 8) road(0, y, 127, y, ROAD_STREET);
    for (x = 0; x < 128; x += 8) road(x, 4, x, 127, ROAD_STREET);
    world_set_tile_flags(0, 68, TF_EDGE_LINK, 0);
    for (by = 0; by < 16; by++)
        for (bx = 0; bx < 16; bx++) {
            int x0 = bx * 8 + 1, y0 = by * 8 + 5, z;
            if (y0 + 6 >= 128) continue;
            if (by == 0) {                          /* pumps on the shore row + plants */
                if (bx < 8) { world_lot_place(B_PUMP, x0, 3, ROT_0, 0, LOT_ACTIVE); }
            }
            if ((bx == 3 || bx == 12) && (by == 2 || by == 8 || by == 13)) { build(B_COAL_PLANT, x0, y0, ROT_0); build(B_COAL_PLANT, x0 + 3, y0, ROT_0); build(B_COAL_PLANT, x0, y0 + 3, ROT_0); continue; }
            if ((bx + by) % 3 == 0) { build(B_SCHOOL, x0, y0, ROT_0); build(B_SAUNA, x0 + 3, y0, ROT_0); build(B_CLINIC, x0 + 5, y0, ROT_0); build(B_FIRE_STATION, x0, y0 + 3, ROT_0); build(B_SAUNA, x0 + 3, y0 + 3, ROT_0); }
            z = (bx >= 12) ? ZONE_INDUSTRY : (by % 4 == 1) ? ZONE_COMMERCIAL : (bx % 3 == 1) ? ZONE_APARTMENTS : ZONE_HOUSES;
            zone(x0, y0, x0 + 6, y0 + 6, z);
        }
    for (y = 4; y < 128; y += 8) pline(0, y, 127, y);
    for (x = 0; x < 128; x += 8) pline(x, 4, x, 127);
}

TEST(sim_dense_timing)
{
    int k, i;
    double t0, tp, tw, tr, tc[COV_KINDS], tf[4], tg, tagg;
    const CityStats *st;
    FILE *f;
    s_build_fail = 0;
    dense_city();
    sim_run_months(12 * 12);
    st = sim_stats();
    test_log("dense city after 12 years: pop %d jobs %d lots %d (building %d) power %d/%d water %d/%d unemployed %d",
             st->population, st->jobs, g_world.lot_count, st->lots_building, st->power_demand, st->power_supply,
             st->water_demand, st->water_supply, st->unemployed);
    dump_file("dense_end.txt");
    timing_reset();
    timed_months(6);
    timing_log("dense");
    /* solvers in isolation (best of 5, warm) */
    tp = tw = tr = tg = tagg = 1e9;
    for (k = 0; k < COV_KINDS; k++) tc[k] = 1e9;
    for (k = 0; k < 4; k++) tf[k] = 1e9;
    for (i = 0; i < 60; i++) {
        net_mark_dirty(NET_POWER); t0 = time_ms(); net_step(); tp = IMIN(tp, time_ms() - t0);
        net_mark_dirty(NET_WATER); t0 = time_ms(); net_step(); tw = IMIN(tw, time_ms() - t0);
        net_mark_dirty(NET_COVERAGE); net_mark_dirty(0); t0 = time_ms(); net_step(); tr = IMIN(tr, time_ms() - t0);
        for (k = 0; k < COV_KINDS; k++) { t0 = time_ms(); net_coverage_step(k); tc[k] = IMIN(tc[k], time_ms() - t0); }
        t0 = time_ms(); fields_step_pollution(); tf[0] = IMIN(tf[0], time_ms() - t0);
        t0 = time_ms(); fields_step_land_value(i & 3); tf[1] = IMIN(tf[1], time_ms() - t0);
        t0 = time_ms(); fields_step_social(i & 3); tf[2] = IMIN(tf[2], time_ms() - t0);
    }
    f = fopen("out/sim_timing.txt", "a");
    test_log("isolated (Mac ms): power %.4f water %.4f roads-reach+net %.4f pollution %.4f landvalue %.4f social %.4f",
             tp, tw, tr, tf[0], tf[1], tf[2]);
    if (f) fprintf(f, "isolated (Mac ms): power %.4f water %.4f roads-reach+net %.4f pollution %.4f landvalue %.4f social %.4f\n",
                   tp, tw, tr, tf[0], tf[1], tf[2]);
    for (k = 0; k < COV_KINDS; k++) {
        test_log("coverage kind %d: %.4f ms", k, tc[k]);
        if (f) fprintf(f, "coverage kind %d: %.4f ms\n", k, tc[k]);
    }
    if (f) fclose(f);
    CHECK(st->population > 15000);
}

/* Profiling helper: KAUP_PROFILE=1 build/lahio_tests sim_profile_loop  (then `sample` the pid).
   Does nothing in normal runs. */
TEST(sim_profile_loop)
{
    int i;
    if (!getenv("KAUP_PROFILE")) return;
    dense_city();
    sim_run_months(12 * 12);
    for (i = 0; i < 40000; i++) {
        int k;
        const char *what = getenv("KAUP_PROFILE");
        if (what[0] == 'p') { net_mark_dirty(NET_POWER); net_step(); }
        else if (what[0] == 'w') { net_mark_dirty(NET_WATER); net_step(); }
        else if (what[0] == 'c') { for (k = 0; k < COV_KINDS; k++) net_coverage_step(k); }
        else sim_step();
    }
}

/* incremental power solve == full flood */
TEST(net_power_incremental_matches_full)
{
    static u8 inc[MAX_LOTS];
    int i, diff = 0, n = 0;
    new_town(31);
    town_stage0();
    sim_run_months(8);                    /* lots of growth: incremental solves in between */
    net_solve_all();                      /* incremental path (no structural change) */
    for (i = 1; i < g_world.lot_hi; i++) inc[i] = (u8)net_lot_power_state(i);
    world_set_power_line(127, 127, 1);    /* structural change -> the next solve is a full flood */
    world_set_power_line(127, 127, 0);
    net_solve_all();
    for (i = 1; i < g_world.lot_hi; i++) {
        if (!g_world.lots[i].def) continue;
        n++;
        if (inc[i] != net_lot_power_state(i)) diff++;
    }
    test_log("%d lots, %d differ between incremental and full power solve", n, diff);
    CHECK(n > 100);
    CHECK(diff * 50 <= n);                /* appended lots may differ only in brownout order */
}
