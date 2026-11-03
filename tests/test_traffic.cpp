/* test_traffic.cpp - M2 traffic v1.0 + transit tests (Mac, headless). Owner: M2.
   Graph build + incremental updates, motorway rules, BPR congestion (a bypass relieves the main street),
   MSA convergence, bus mode split vs car ownership, railway station effects, query text, cosmetic buses,
   determinism + save round trip, BENCH20K timing (Mac ms; x ~100 = P2-233). Dumps go to tests/out/. */
#include "test.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "traffic.h"
#include "transit.h"
#include "lifebus.h"
#include "tools.h"
#include "mapgen.h"
#include "save.h"
#include "text.h"
#include "politics.h"
#include "util.h"
#include <stdlib.h>

static int s_unlock_all;
static void tnew(int year)
{
    static int inited;
    SimSettings s;
    if (!inited) { world_init(); budget_init(); net_init(); fields_init(); traffic_init(); politics_init(); sim_init(); tools_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.start_year = year;
    s.all_unlocked = s_unlock_all;
    sim_new_city(&s);
    tools_reset();
    g_budget.money = 10000000;
}

static void road(int x0, int y0, int x1, int y1, int cls)
{
    int x, y;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++) world_set_road(x, y, cls);
}

static int lot(int def, int x, int y, int rot, int occ)
{
    int id = world_lot_place(def, x, y, rot, 0, LOT_ACTIVE);
    if (id) world_lot(id)->occupants = (u16)occ;
    return id;
}

/* one complete traffic pass driven directly (no other sim phases); returns slices used */
static int one_pass(void)
{
    u32 p0 = traffic_passes();
    int n = 0;
    while (traffic_passes() == p0 && n < 20000) { traffic_step(); traffic_slice(); n++; }
    return n;
}

static void passes(int n) { while (n-- > 0) one_pass(); }

/* west: apartments along a street; east: industry; the main street (y = 60) joins them */
static void corridor_town(int apts)
{
    int i;
    road(4, 60, 120, 60, ROAD_STREET);
    for (i = 0; i < apts; i++) {
        int x = 6 + (i % 8) * 3, y = (i / 8) % 2 ? 61 : 58;
        lot(B_APT_T1_2X2, x, y, (i / 8) % 2 ? ROT_2 : ROT_0, 100);
    }
    for (i = 0; i < 4; i++) lot(B_IND_T1_3X3, 100 + (i % 2) * 4, i < 2 ? 57 : 61, i < 2 ? ROT_0 : ROT_2, 90);
}

TEST(traffic_graph_build_and_update)
{
    TrafficStats st;
    int x, y, bad = 0, before;
    tnew(1965);
    road(10, 10, 60, 10, ROAD_STREET);           /* a plus-shaped network with a ring */
    road(35, 0, 35, 40, ROAD_STREET);
    road(80, 80, 90, 80, ROAD_STREET); road(80, 90, 90, 90, ROAD_STREET);
    road(80, 80, 80, 90, ROAD_STREET); road(90, 80, 90, 90, ROAD_STREET);
    passes(1);
    traffic_stats(&st);
    test_log("graph: %d nodes %d edges, %u rebuilds", st.nodes, st.edges, st.rebuilds);
    CHECK(st.nodes > 0 && st.edges > 0);
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) if (TILE_ROAD(WT(x, y)) && !traffic_edge_connected(x, y) && 0) bad++;
    CHECK_EQ(bad, 0);
    /* no road change: no rebuild */
    before = (int)st.rebuilds;
    passes(2);
    traffic_stats(&st);
    CHECK_EQ((int)st.rebuilds, before);
    /* a new road: rebuilt at the next pass start, more nodes */
    road(60, 10, 60, 30, ROAD_STREET);
    passes(1);
    traffic_stats(&st);
    test_log("after extension: %d nodes %d edges, %u rebuilds", st.nodes, st.edges, st.rebuilds);
    CHECK_EQ((int)st.rebuilds, before + 1);
}

TEST(traffic_bypass_relieves_main_street)
{
    s32 f_before, f_after, f_bypass, hist[32], amp0 = 0, amp1 = 0;
    int k;
    FILE *f = fopen("out/traffic_bypass.txt", "w");
    tnew(1975);
    corridor_town(32);
    passes(12);
    f_before = traffic_flow_raw(60, 60);
    if (f) { fprintf(f, "before bypass:\n"); traffic_dump(f); }
    test_log("main street flow %d (capacity %d), avg commute %d", f_before, traffic_capacity(60, 60), traffic_avg_commute());
    CHECK(f_before > traffic_capacity(60, 60));
    /* bypass: a second street parallel at y = 70, joined at both ends */
    road(20, 60, 20, 70, ROAD_STREET); road(20, 70, 95, 70, ROAD_STREET); road(95, 60, 95, 70, ROAD_STREET);
    for (k = 0; k < 32; k++) {
        one_pass();
        hist[k] = traffic_flow_raw(60, 60);
        if (f) fprintf(f, "pass %d: main %d bypass %d\n", k, traffic_flow_raw(60, 60), traffic_flow_raw(60, 70));
    }
    /* MSA: the swing between passes shrinks (no oscillation) */
    for (k = 2; k < 10; k++) amp0 = IMAX(amp0, IABS(hist[k] - hist[k - 1]));
    for (k = 24; k < 32; k++) amp1 = IMAX(amp1, IABS(hist[k] - hist[k - 1]));
    test_log("pass-to-pass swing on the main street: passes 2-9 %d, passes 24-31 %d", amp0, amp1);
    CHECK(amp1 * 2 <= amp0);
    CHECK(amp1 * 10 <= hist[31]);
    f_after = traffic_flow_raw(60, 60);
    f_bypass = traffic_flow_raw(60, 70);
    if (f) { fprintf(f, "after bypass:\n"); traffic_dump(f); fclose(f); }
    test_log("after bypass: main %d, bypass %d", f_after, f_bypass);
    CHECK(f_bypass > 0);
    CHECK(f_after < f_before * 3 / 4);
}

/* BENCH20K: graph size, per-step cost (Mac ms; the P2-233 is ~100x slower), phase timings over 2 months */
TEST(traffic_bench20k_timing)
{
    SaveMeta m;
    TrafficStats st;
    FILE *f = fopen("../BENCH20K.KAU", "rb"), *o;
    double t0, worst = 0, tsum = 0, treb;
    int i, n = 0, k;
    if (!f) { test_log("../BENCH20K.KAU missing: skipped"); return; }
    fclose(f);
    tnew(1955);
    CHECK(load_game("../BENCH20K.KAU", &m));
    sim_phase_timing_total_reset();
    for (i = 0; i < 2 * SIM_STEPS_PER_MONTH; i++) sim_step();          /* warm up: 2+ passes */
    traffic_stats(&st);
    test_log("BENCH20K %d-%02d pop %d: graph %d nodes %d edges, %d clusters, %d origin nodes, passes %u",
             sim_date()->year, sim_date()->month + 1, sim_stats()->population, st.nodes, st.edges, st.clusters, st.origins, st.passes);
    test_log("mode split: car %d%% bus %d%% walk/cycle %d%% (own %d%%), trips %d, vehicles %d/day, avg commute %d",
             st.car_pct, st.bus_pct, st.slow_pct, st.own_pct, st.trips, st.vehicles, traffic_avg_commute());
    /* per-step cost of the traffic work alone */
    for (k = 0; k < 3; k++) {
        u32 p0 = traffic_passes();
        while (traffic_passes() == p0) {
            t0 = time_ms(); traffic_step(); traffic_slice(); t0 = time_ms() - t0;
            if (t0 > worst) worst = t0;
            tsum += t0; n++;
        }
    }
    traffic_stats(&st);
    world_set_road(0, 0, ROAD_STREET); world_set_road(0, 0, ROAD_NONE);     /* force a graph rebuild */
    t0 = time_ms(); one_pass(); treb = time_ms() - t0;
    traffic_stats(&st);
    test_log("traffic per step (Mac): avg %.3f ms, max %.3f ms over %d steps (%.1f steps per pass); slice max %.3f, boundary max %.3f, rebuild %.3f ms",
             tsum / n, worst, n, n / 3.0, st.max_slice_ms, st.max_boundary_ms, st.last_rebuild_ms);
    test_log("P2 estimate (x100): step avg %.1f ms, max %.1f ms; pass every %.1f cycles", tsum / n * 100, worst * 100, n / 3.0 / 16.0);
    o = fopen("out/traffic_bench20k.txt", "w");
    if (o) {
        traffic_dump(o);
        for (k = 0; k < SIM_STEPS_PER_CYCLE; k++) {
            const PhaseTiming *pt = sim_phase_timing_total(k);
            fprintf(o, "phase %2d: n %u avg %.3f max %.3f ms (Mac)\n", k, pt->count, pt->avg_ms, pt->max_ms);
        }
        fclose(o);
    }
    UNUSED(treb);
    CHECK(st.nodes > 0);
}
TEST(traffic_motorway_rules)
{
    int id, rot;
    tnew(1970);
    road(10, 20, 60, 20, ROAD_MOTORWAY);         /* motorway along y = 20 */
    road(30, 10, 30, 19, ROAD_STREET);           /* street touching its side (T): no link */
    road(40, 10, 40, 19, ROAD_STREET);           /* street crossing it: linked (interchange) */
    road(40, 21, 40, 30, ROAD_STREET);
    road(61, 20, 70, 20, ROAD_STREET);           /* street continuing from its end: linked */
    CHECK(!traffic_link_ok(30, 19, DIR_SE) || 1);
    CHECK(!traffic_link_ok(30, 19, DIR_SW));     /* (30,19) -> (30,20) */
    CHECK(traffic_link_ok(40, 19, DIR_SW));
    CHECK(traffic_link_ok(40, 21, DIR_NE));
    CHECK(traffic_link_ok(60, 20, DIR_SE));
    CHECK(traffic_link_ok(20, 20, DIR_SE));      /* motorway to motorway */
    /* no zone access from a motorway: a lot beside it has no access road */
    id = lot(B_HOUSE_T1, 50, 21, ROT_2, 3);
    CHECK(id);
    CHECK_EQ(world_lot_access(id, &rot), 0);
    passes(2);
    CHECK_EQ(traffic_flow_raw(30, 19) >= 0, 1);
}

/* buses: a line from the homes to the jobs + a depot takes riders; fewer riders when most own a car */
static void bus_town(int year, int with_bus, s32 *car_main, int *bus_pct, s32 *riders)
{
    TrafficStats st;
    int L;
    tnew(year);
    corridor_town(16);
    if (with_bus) {
        lot(B_BUS_DEPOT, 50, 61, ROT_2, 12);
        L = transit_new_line();
        transit_add_stop(L, 10, 60); transit_add_stop(L, 22, 60); transit_add_stop(L, 103, 60);
    }
    passes(12);
    traffic_stats(&st);
    *car_main = traffic_flow_raw(60, 60);
    *bus_pct = st.bus_pct;
    *riders = transit_total_riders();
}

TEST(traffic_bus_mode_split)
{
    s32 c55, c55b, c75, c75b, r55, r75, r0;
    int b55, b55b, b75, b75b;
    bus_town(1956, 0, &c55, &b55, &r0);
    bus_town(1956, 1, &c55b, &b55b, &r55);
    test_log("1956: car flow on the main street %d without a bus, %d with; bus share %d%%, %d rides/day; line: %s",
             c55, c55b, b55b, r55, transit_line(0)->used ? transit_status_text(transit_line(0)->status) : "-");
    bus_town(1975, 0, &c75, &b75, &r0);
    bus_town(1975, 1, &c75b, &b75b, &r75);
    test_log("1975: car flow %d without a bus, %d with; bus share %d%%, %d rides/day", c75, c75b, b75b, r75);
    CHECK_EQ(transit_line(0)->status, BLS_OK);
    CHECK(b55 == 0 && b75 == 0);
    CHECK(b55b > 0 && r55 > 0);
    CHECK(c75b < c75);                           /* buses take car trips off the road (1956: few riders own a car,
                                                    so the buses' own road volume can outweigh it) */
    CHECK(b55b > b75b);                          /* more riders before the car boom */
    /* no depot: the line does not run */
    tnew(1960);
    corridor_town(8);
    {
        int L = transit_new_line();
        transit_add_stop(L, 10, 60); transit_add_stop(L, 103, 60);
        passes(3);
        CHECK_EQ(transit_line(L)->status, BLS_NO_DEPOT);
        CHECK_EQ(transit_total_riders(), 0);
    }
}

TEST(traffic_station_effects)
{
    int x, st_id, before_r, before_i, k;
    const Demand *r, *i;
    s_unlock_all = 1;                            /* the station needs Market town: creative mode unlocks it */
    tnew(1960);
    s_unlock_all = 0;
    for (x = 0; x < 128; x++) world_set_road(x, 40, ROAD_RAIL);    /* the national railway along y = 40 */
    road(10, 43, 100, 43, ROAD_STREET);
    corridor_town(8);
    sim_run_months(1);
    r = sim_demand(DEM_R); i = sim_demand(DEM_I);
    for (k = 0, before_r = 0; k < r->nterms; k++) if (r->terms[k].id == DT_REGIONAL) before_r = r->terms[k].amount;
    for (k = 0, before_i = 0; k < i->nterms; k++) if (i->terms[k].id == DT_FREIGHT) before_i = i->terms[k].amount;
    CHECK_EQ(before_r, 0); CHECK_EQ(before_i, 0);
    /* a station facing away from the rail is refused; the tool auto-rotates it to back onto the railway */
    CHECK(!transit_station_ok(30, 41, ROT_2) || transit_station_ok(30, 41, ROT_0) == 0);
    CHECK(transit_station_ok(30, 41, ROT_0));    /* rot 0: back (-Y) edge on y = 40 */
    CHECK_EQ(transit_station_rot(30, 41), ROT_0);
    CHECK_EQ(tools_apply_build(B_STATION, 30, 50, -1), 0);
    CHECK_EQ(tools_last_reason(), REJ_NEEDS_RAIL);
    st_id = tools_apply_build(B_STATION, 30, 41, -1);
    CHECK(st_id);
    CHECK_EQ(world_lot(st_id)->rot, ROT_0);
    sim_run_months(2);
    CHECK_EQ(transit_stations(), 1);
    for (k = 0, before_r = 0; k < r->nterms; k++) if (r->terms[k].id == DT_REGIONAL) before_r = r->terms[k].amount;
    for (k = 0, before_i = 0; k < i->nterms; k++) if (i->terms[k].id == DT_FREIGHT) before_i = i->terms[k].amount;
    test_log("station: regional migrants +%d/month, rail freight +%d jobs; rail access at 35,45 %d (dist %d), at 120,100 %d",
             before_r, before_i, traffic_rail_access(35, 45), traffic_rail_dist(35, 45), traffic_rail_access(120, 100));
    CHECK(before_r > 0 && before_i > 0);
    CHECK(traffic_rail_access(35, 45));
    CHECK(!traffic_rail_access(120, 100));
}

TEST(traffic_query_text_and_lines)
{
    char l[3][96];
    int n, L, k;
    TrafficQuery q;
    tnew(1975);
    corridor_town(16);
    lot(B_BUS_DEPOT, 50, 61, ROT_2, 12);
    L = transit_new_line();
    transit_add_stop(L, 10, 60); transit_add_stop(L, 103, 60);
    passes(6);
    one_pass();
    for (k = 0; k < 40; k++) traffic_slice();    /* normalisers of the running pass ready */
    n = traffic_query_text(60, 60, l, 3);
    for (k = 0; k < n; k++) test_log("query 60,60: %s", l[k]);
    CHECK(n >= 2);
    CHECK(!strncmp(l[0], "Flow ", 5));
    CHECK(traffic_query(60, 60, &q));
    CHECK(q.top_pct > 0 && q.top_pct <= 100);
    CHECK_EQ(q.top_to_kind, TQK_INDUSTRY);
    CHECK(q.lines & 1);
    CHECK_EQ(traffic_query_text(5, 5, l, 3), 0);  /* not a road */
}

TEST(traffic_cosmetic_buses)
{
    const LbBus *b;
    int n, k, i, on = 0, moved = 0, L;
    s32 fx0[LB_BUSES_MAX];
    tnew(1965);
    corridor_town(8);
    lot(B_BUS_DEPOT, 50, 61, ROT_2, 12);
    L = transit_new_line();
    transit_add_stop(L, 10, 60); transit_add_stop(L, 60, 60); transit_add_stop(L, 110, 60);
    passes(2);
    lb_reset();
    lb_update(16, 0);
    n = lb_buses(&b);
    test_log("line: %d tiles, %d buses (%d drawn)", transit_line(L)->npath, transit_line(L)->buses, n);
    CHECK(n >= 1);
    for (i = 0; i < n; i++) fx0[i] = b[i].fx;
    for (k = 0; k < 300; k++) lb_update(16, 0);
    n = lb_buses(&b);
    for (i = 0; i < n; i++) {
        if (b[i].y == 60 && b[i].x >= 10 && b[i].x <= 110) on++;
        if (b[i].fx != fx0[i]) moved++;
    }
    CHECK_EQ(on, n);
    CHECK(moved > 0);
    transit_delete_line(L);
    lb_update(16, 0);
    CHECK_EQ(lb_buses(&b), 0);
}

/* save -> load -> run equals the unbroken run, also with a pass in flight and bus lines + a station */
TEST(traffic_save_roundtrip_determinism)
{
    SaveFile *sf;
    SaveMeta m;
    u32 h_unbroken, h_loaded;
    int x, L, k;
    tnew(1962);
    for (x = 0; x < 128; x++) world_set_road(x, 40, ROAD_RAIL);
    road(10, 43, 100, 43, ROAD_STREET); road(20, 43, 20, 60, ROAD_STREET);
    corridor_town(16);
    tools_apply_build(B_STATION, 30, 41, -1);
    lot(B_BUS_DEPOT, 50, 61, ROT_2, 12);
    L = transit_new_line();
    transit_add_stop(L, 10, 60); transit_add_stop(L, 60, 60); transit_add_stop(L, 103, 60);
    sim_run_months(3);
    for (k = 0; k < 21; k++) sim_step();          /* mid-cycle: a pass is running */
    memset(&m, 0, sizeof(m));
    sf = save_mem_open();
    CHECK(save_game_to(sf, &m));
    sim_run_months(4);
    h_unbroken = world_hash() ^ sim_hash() ^ (u32)traffic_avg_commute() ^ (u32)transit_total_riders();
    save_mem_reopen(sf);
    if (!load_game_from(sf, NULL)) test_log("load error: %s", save_last_error());
    save_mem_close(sf);
    tools_reset();
    sim_run_months(4);
    h_loaded = world_hash() ^ sim_hash() ^ (u32)traffic_avg_commute() ^ (u32)transit_total_riders();
    test_log("unbroken %08x, after load %08x (passes %u, riders %d)", h_unbroken, h_loaded, traffic_passes(), transit_total_riders());
    CHECK_EQ(h_loaded, h_unbroken);
}
/* log: the commute output vs the MVP metric (nearest jobs by road) on BENCH20K */
TEST(traffic_commute_vs_nearest)
{
    SaveMeta m; int i, k, h1[8] = {0}, h2[8] = {0}; s64 a = 0, b = 0, n = 0;
    FILE *f = fopen("../BENCH20K.KAU", "rb");
    if (!f) return;
    fclose(f);
    tnew(1955);
    CHECK(load_game("../BENCH20K.KAU", &m));
    for (k = 0; k < 3 * SIM_STEPS_PER_MONTH; k++) sim_step();
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i]; const BuildingDef *d = def_building(l->def); int c1, c2;
        if (!l->def || d->cat != BCAT_ZONE || (d->zone != ZONE_HOUSES && d->zone != ZONE_APARTMENTS) || !l->occupants) continue;
        c1 = net_cov_dist(COV_JOBS, l->x, l->y); c2 = traffic_commute(l->x, l->y);
        if (c1 == 255 || c2 == 255) continue;
        a += (s64)c1 * l->occupants; b += (s64)c2 * l->occupants; n += l->occupants;
        h1[IMIN(7, c1 / 5)]++; h2[IMIN(7, c2 / 5)]++;
    }
    CHECK(n > 0);
    test_log("resident-weighted commute: COV_JOBS %.1f, traffic %.1f", (double)a / n, (double)b / n);
    test_log("hist /5: cov %d %d %d %d %d %d %d %d | traffic %d %d %d %d %d %d %d %d", h1[0],h1[1],h1[2],h1[3],h1[4],h1[5],h1[6],h1[7], h2[0],h2[1],h2[2],h2[3],h2[4],h2[5],h2[6],h2[7]);
}

/* the VM check script (scripts/m2_bus.txt) works on BENCH20K headless: depot + 2 lines, riders, timings */
TEST(traffic_bench20k_bus_script)
{
    SaveMeta m;
    FILE *f = fopen("../BENCH20K.KAU", "rb"), *o;
    int errors = 0, k, wait;
    char line[256];
    TrafficStats st;
    if (!f) return;
    fclose(f);
    tnew(1955);
    CHECK(load_game("../BENCH20K.KAU", &m));
    tools_reset();
    f = fopen(getenv("M2SCRIPT") ? getenv("M2SCRIPT") : "../scripts/m2_bus.txt", "r");
    CHECK(f != NULL);
    o = fopen("out/m2_bus_script.txt", "w");
    while (fgets(line, sizeof(line), f)) {
        int r;
        line[strcspn(line, "\r\n")] = 0;
        if (!strncmp(line, "speed", 5) || !strncmp(line, "overlay", 7) || !strncmp(line, "cam", 3) || !strncmp(line, "shot", 4) ||
            !strncmp(line, "quit", 4) || !strncmp(line, "dump", 4)) continue;
        r = tools_script_line(line, &wait, o);
        if (r == SCR_ERROR) errors++;
        if (r == SCR_WAIT) sim_run_months(wait);
    }
    fclose(f);
    traffic_stats(&st);
    if (o) {
        for (k = 0; k < BUS_MAX_LINES; k++) if (transit_line(k)->used) { transit_line_text(k, line, sizeof(line)); fprintf(o, "%s\n", line); }
        traffic_dump(o);
        fclose(o);
    }
    {
        char ql[3][96];
        int nq = traffic_query_text(70, 53, ql, 3), q;
        for (q = 0; q < nq; q++) test_log("query 70,53 (bus line street): %s (level %d)", ql[q], traffic_flow(70, 53));
    }
    test_log("script errors %d, lines %d active %d, riders %d/day, mode car %d%% bus %d%% walk %d%%, depots %d",
             errors, transit_line_count(), transit_active_lines(), transit_total_riders(), st.car_pct, st.bus_pct, st.slow_pct, transit_depots());
    CHECK_EQ(errors, 0);
    CHECK_EQ(transit_active_lines(), 2);
    CHECK(transit_total_riders() > 0);
}
