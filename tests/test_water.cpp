/* test_water.cpp - F1 water pipes + conduction (docs/research/WATER.md, DESIGN 14.1-14.2). Owner: F1.
   Headless tests of net.cpp's water solver, the pipe tool, the overlay values, save round trip and the
   v1 save migration. Text dumps go to tests/out/water_*.txt (DEBUG FROM TEXT: net_water_dump legend
   W source, w/d/u lot watered/dry/not connected, - x o pipe wet/short/dry, : ; zoned wet/dry). */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "budget.h"
#include "tools.h"
#include "traffic.h"
#include "save.h"
#include "text.h"

/* ------------------------------------------------------------------ helpers */
static void wnew(void)
{
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = 77;
    sim_new_city(&s);
    tools_reset();
    tools_set_pipes_with_roads(0);
    tools_set_show_pipes(0);
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

static void wdump(const char *name, int x0, int y0, int x1, int y1)
{
    char path[96];
    FILE *f;
    str_printf(path, sizeof(path), "out/water_%s.txt", name);
    f = fopen(path, "w");
    if (!f) return;
    net_water_dump(f, x0, y0, x1, y1);
    fclose(f);
}

/* the strict cut: every served consumer is at least as near as every dry one; returns served count */
static int check_cut(int *max_served, int *min_dry)
{
    int i, n = 0, d;
    *max_served = -1; *min_dry = 1 << 30;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_NONE || g_bdefs[l->def].cat != BCAT_ZONE || !net_lot_water_src(i, &d)) continue;
        if (net_lot_water_state(i) == NLS_OK) { n++; if (d > *max_served) *max_served = d; }
        else if (d < *min_dry) *min_dry = d;
    }
    return n;
}

/* ------------------------------------------------------------------ the rule */
/* one well, a pipe east and a row of houses: reach = furthest served house; more load nearer the well
   shrinks it monotonically and the furthest go dry first */
TEST(water_reach_shrinks_with_load)
{
    int well, i, prev_reach = 1 << 30, ms, md;
    WaterSrcInfo wi;
    wnew();
    well = lot(B_WELL, 10, 10);
    pipes(11, 10, 120, 10);
    for (i = 0; i < 100; i++) lot(B_HOUSE_T2, 11 + i, 11);          /* 100 units, all served */
    net_solve_all();
    CHECK(net_water_source(well, &wi));
    test_log("start: output %d used %d lots %d dry %d reach %d", wi.output, wi.used, wi.lots, wi.lots_dry, wi.reach);
    CHECK_EQ(wi.output, 150);
    CHECK_EQ(wi.used, 100);
    CHECK_EQ(wi.lots, 100);
    CHECK_EQ(wi.reach, 101);                    /* house at x 110: pipe tile 110,10 is 100 from the well + 1 */
    CHECK(!wi.at_capacity);
    CHECK(world_tile(120, 10)->zone & ZF_WATERED);      /* spare left: the pipe's far end is wet */
    for (i = 0; i < 90; i++) {                  /* load grows next to the well (north side of the pipe) */
        lot(B_HOUSE_T2, 11 + i, 9);
        net_solve_all();
        net_water_source(well, &wi);
        CHECK(wi.reach <= prev_reach);
        check_cut(&ms, &md);
        CHECK(ms <= md);
        prev_reach = wi.reach;
    }
    wdump("reach", 0, 5, 127, 14);
    test_log("after +90 near the well: used %d lots %d dry %d (%d units) reach %d", wi.used, wi.lots, wi.lots_dry, wi.dry_units, wi.reach);
    CHECK_EQ(wi.used, 150);
    CHECK_EQ(wi.lots, 150);
    CHECK_EQ(wi.lots_dry, 40);
    CHECK(wi.at_capacity);
    CHECK(wi.reach < 101);
    CHECK_EQ(net_lot_water_state(world_lot_at(110, 11)), NLS_SHORT);     /* the furthest */
    CHECK_EQ(net_lot_water_state(world_lot_at(11, 11)), NLS_OK);         /* the nearest */
    CHECK((world_tile(120, 10)->zone & (ZF_WATER_NET | ZF_WATERED)) == ZF_WATER_NET);   /* pipe: short (red) */
    CHECK_EQ(net_stats(NET_WATER)->used, 150);
    CHECK_EQ(net_stats(NET_WATER)->lots_short, 40);
}

/* two wells on one pipe: the overloaded one's dry consumers get the other's spare (nearest first);
   a well on a separate network can't help */
TEST(water_spare_flows_between_sources)
{
    int a, b, x, y, ms, md;
    WaterSrcInfo wa, wb;
    wnew();
    a = lot(B_WELL, 10, 10);
    b = lot(B_WELL, 121, 10);
    pipes(11, 10, 120, 10);
    for (y = 8; y <= 12; y++) if (y != 10) for (x = 11; x <= 60; x++) lot(B_HOUSE_T2, x, y);   /* 200, all nearer a */
    net_solve_all();
    net_water_source(a, &wa); net_water_source(b, &wb);
    wdump("spare", 0, 5, 127, 14);
    test_log("a: used %d (own %d shared %d) lots %d dry %d reach %d | b: used %d (own %d shared %d) lots %d spare %d",
             wa.used, wa.own_used, wa.shared_used, wa.lots, wa.lots_dry, wa.reach, wb.used, wb.own_used, wb.shared_used, wb.lots, wb.spare);
    CHECK_EQ(wa.network, wb.network);
    CHECK_EQ(wa.own_used, 150);
    CHECK_EQ(wb.own_used, 0);
    CHECK_EQ(wb.shared_used, 50);
    CHECK_EQ(wb.spare, 100);
    CHECK_EQ(net_stats(NET_WATER)->lots_short, 0);
    CHECK_EQ(net_stats(NET_WATER)->used, 200);
    CHECK_EQ(net_stats(NET_WATER)->networks, 1);
    /* cut the pipe in the middle: b is alone, a's 50 furthest go dry */
    world_set_pipe(90, 10, 0);
    net_solve_all();
    net_water_source(a, &wa); net_water_source(b, &wb);
    CHECK(wa.network != wb.network);
    CHECK_EQ(net_stats(NET_WATER)->networks, 2);
    CHECK_EQ(wa.lots_dry, 50);
    CHECK_EQ(wb.used, 0);
    check_cut(&ms, &md);
    CHECK(ms <= md);
    /* more pipe never hurts: a long dead-end branch changes nothing */
    pipes(30, 11, 30, 60);
    world_lot_remove(world_lot_at(30, 11)); world_lot_remove(world_lot_at(30, 12));
    pipes(30, 11, 30, 12);
    net_solve_all();
    net_water_source(a, &wa);
    CHECK_EQ(wa.used, 150);
}

/* roads block water unless a pipe crosses; power still jumps one road tile */
TEST(water_roads_block_unless_piped)
{
    int well, h1, h2, plant;
    wnew();
    well = lot(B_WELL, 10, 10);
    h1 = lot(B_HOUSE_T2, 11, 10);
    world_set_road(12, 9, ROAD_STREET); world_set_road(12, 10, ROAD_STREET); world_set_road(12, 11, ROAD_STREET);
    h2 = lot(B_HOUSE_T2, 13, 10);
    plant = lot(B_COAL_PLANT, 8, 9);
    CHECK(well && h1 && h2 && plant);
    net_solve_all();
    CHECK_EQ(net_lot_water_state(h1), NLS_OK);
    CHECK_EQ(net_lot_water_state(h2), NLS_NONE);
    CHECK_EQ(net_lot_power_state(h2), NLS_OK);            /* power jumps the plain road */
    world_set_pipe(12, 10, 1);                            /* a pipe under the street */
    net_solve_all();
    CHECK_EQ(net_lot_water_state(h2), NLS_OK);
    CHECK(world_tile(12, 10)->zone & ZF_WATERED);
    CHECK(!(world_tile(12, 9)->zone & ZF_WATER_NET));     /* the road tile beside it stays dry */
}

/* zoned land without buildings conducts water and power (DESIGN 14.2); dezoning breaks it */
TEST(water_zoned_empty_tiles_conduct)
{
    int well, plant, h, x;
    wnew();
    well = lot(B_WELL, 10, 10);
    plant = lot(B_COAL_PLANT, 8, 10);
    for (x = 11; x <= 20; x++) world_set_zone(x, 10, ZONE_HOUSES);
    for (x = 11; x <= 20; x++) world_set_zone(x, 11, ZONE_HOUSES);
    h = lot(B_HOUSE_T2, 21, 10);
    CHECK(well && plant && h);
    net_solve_all();
    wdump("zoned", 5, 7, 25, 13);
    CHECK_EQ(net_lot_water_state(h), NLS_OK);
    CHECK_EQ(net_lot_power_state(h), NLS_OK);
    CHECK_EQ(net_water_near(15, 10), NLS_OK);
    CHECK(world_tile(15, 11)->zone & ZF_WATERED);
    CHECK(world_tile(15, 11)->zone & ZF_POWERED);
    CHECK_EQ(net_power_near(15, 10, 15, 10), NLS_OK);
    world_set_zone(16, 10, ZONE_NONE); world_set_zone(16, 11, ZONE_NONE);   /* a gap */
    net_solve_all();
    CHECK_EQ(net_lot_water_state(h), NLS_NONE);
    CHECK_EQ(net_lot_power_state(h), NLS_NONE);
    CHECK_EQ(net_water_near(19, 10), NLS_NONE);
    CHECK_EQ(net_water_near(14, 10), NLS_OK);
    /* the sim's own dirtiness: zoning through the listener re-solves on net_step */
    world_set_zone(16, 10, ZONE_HOUSES);
    net_step(); net_step();
    CHECK_EQ(net_lot_water_state(h), NLS_OK);
    CHECK_EQ(net_lot_power_state(h), NLS_OK);
}

/* a pumping station without power gives nothing (its consumers are "connected but short");
   power arrives -> water flows on the next net_step after the power solve */
TEST(water_pump_needs_power)
{
    int pump, h, plant, y, k;
    WaterSrcInfo wi;
    wnew();
    for (y = 0; y < 40; y++) { world_set_terrain(0, y, TER_WATER, 0); world_set_terrain(1, y, TER_WATER, 0); }
    pump = lot(B_PUMP, 2, 20);
    h = lot(B_HOUSE_T2, 3, 20);
    CHECK(pump && h);
    net_solve_all();
    CHECK(net_water_source(pump, &wi));
    CHECK_EQ(wi.output, 0);
    CHECK(wi.needs_power);
    CHECK_EQ(wi.shore, 10);
    CHECK_EQ(net_lot_water_state(h), NLS_SHORT);
    plant = lot(B_COAL_PLANT, 2, 21);                     /* touches the pump: powers it */
    CHECK(plant);
    for (k = 0; k < 4; k++) net_step();                   /* power first, then water (alternating) */
    net_water_source(pump, &wi);
    CHECK_EQ(wi.output, 400);
    CHECK(!wi.needs_power);
    CHECK_EQ(net_lot_water_state(h), NLS_OK);
    world_lot_remove(plant);
    for (k = 0; k < 4; k++) net_step();
    CHECK_EQ(net_source_output(pump), 0);
    CHECK_EQ(net_lot_water_state(h), NLS_SHORT);
}

/* water tower: +200 when its network has a producer, and a 16-tile pressure zone (Apartments T2+) */
TEST(water_tower_pressure_zone)
{
    int well, tower, lone, near_, far_, x;
    WaterSrcInfo wi;
    wnew();
    well = lot(B_WELL, 10, 10);
    pipes(11, 10, 100, 10);
    tower = lot(B_WATER_TOWER, 40, 11);                   /* touches the pipe at 40,10 */
    near_ = lot(B_APT_T1_2X2, 54, 11);                     /* 54,11 is 14+1 from the tower along the pipe */
    far_ = lot(B_APT_T1_2X2, 60, 11);
    lone = lot(B_WATER_TOWER, 80, 30);                    /* no producer on its network */
    CHECK(well && tower && near_ && far_ && lone);
    net_solve_all();
    CHECK_EQ(net_source_output(tower), TOWER_OUTPUT);
    CHECK_EQ(net_source_output(lone), 0);
    CHECK(net_water_source(tower, &wi));
    test_log("tower: output %d pressure lots %d", wi.output, wi.pressure_lots);
    CHECK(net_water_tower_reach(55, 12));
    CHECK(!net_water_tower_reach(61, 12));
    CHECK(net_water_tower_reach(40 + 15, 10));            /* 16 tiles from the tower lot */
    CHECK(!net_water_tower_reach(40 + 17, 10));
    CHECK(!net_water_tower_reach(80, 31));
    CHECK_EQ(wi.pressure_lots, 1);
    for (x = 30; x <= 34; x++) world_set_zone(x, 11, ZONE_APARTMENTS);   /* empty zoned land in the zone */
    net_solve_all();
    CHECK(net_water_tower_reach(32, 11));
}

/* checklist reasons name the overloaded source: "Water: supply short (Groundwater well 144/150)" */
TEST(water_checklist_reasons)
{
    int well, i, k, far_ = 0, lone;
    LotInfo li;
    char buf[128];
    wnew();
    well = lot(B_WELL, 10, 10);
    pipes(11, 10, 120, 10);
    for (i = 0; i < 20; i++) far_ = lot(B_APT_T1_3X1, 11 + 3 * i, 11);     /* 20 x 9 = 180 > 150 */
    lone = lot(B_APT_T1_3X1, 50, 50);
    net_solve_all();
    CHECK(well && far_ && lone);
    sim_query_lot(far_, &li);
    buf[0] = 0;
    for (k = 0; k < li.next.n; k++) if (li.next.item[k].req == REQ_WATER) text_req_line(buf, sizeof(buf), &li.next.item[k]);
    test_log("far: %s", buf);
    CHECK(strstr(buf, "supply short (Groundwater well 144/150)") != NULL);     /* 16 x 9 fit, the 17th doesn't */
    sim_query_lot(lone, &li);
    buf[0] = 0;
    for (k = 0; k < li.next.n; k++) if (li.next.item[k].req == REQ_WATER) text_req_line(buf, sizeof(buf), &li.next.item[k]);
    test_log("lone: %s", buf);
    CHECK(strstr(buf, "no pipe within reach") != NULL);
}

/* overlay values: pipes wet/short/dry, lots, wet ground; a source param shows only its area */
TEST(water_overlay_values)
{
    int well, lone_pipe_x = 60;
    wnew();
    well = lot(B_WELL, 10, 10);
    pipes(11, 10, 20, 10);
    pipes(lone_pipe_x, 10, lone_pipe_x + 3, 10);
    lot(B_HOUSE_T2, 12, 11);
    world_set_road(25, 9, ROAD_STREET);
    net_solve_all();
    CHECK(well);
    CHECK_EQ(sim_overlay_value(OVL_WATER, 0, 15, 10), OVV(OVR_NET, NETC_CARRIER));
    CHECK_EQ(sim_overlay_value(OVL_WATER, 0, lone_pipe_x, 10), OVV(OVR_NET, NETC_NOT_CONNECTED));
    CHECK_EQ(sim_overlay_value(OVL_WATER, 0, 12, 11), OVV(OVR_NET, NETC_CARRIER));    /* F3: watered = blue */
    CHECK_EQ(sim_overlay_value(OVL_WATER, 0, 15, 11), OVV(OVR_NET, NETC_WET));         /* next to a wet pipe */
    CHECK_EQ(sim_overlay_value(OVL_WATER, 0, 15, 13), OVV_NONE);
    CHECK_EQ(sim_overlay_value(OVL_WATER, 0, 25, 9), OVV_NONE);                       /* road */
    CHECK_EQ(sim_overlay_value(OVL_WATER, 0, 10, 10), OVV(OVR_NET, NETC_OK));        /* the source */
    CHECK_EQ(sim_overlay_value(OVL_WATER, well, 15, 10), OVV(OVR_NET, NETC_CARRIER));
    CHECK_EQ(sim_overlay_value(OVL_WATER, well, lone_pipe_x, 10), OVV_NONE);          /* not its area */
    CHECK_EQ(net_water_tile_src(15, 10), well);
}

/* ------------------------------------------------------------------ the pipe tool */
TEST(water_pipe_tool_undo_redo)
{
    u32 h0, h1, h2;
    Money m0, m1;
    int n, y;
    wnew();
    for (y = 30; y <= 33; y++) world_set_terrain(20, y, TER_WATER, 0);
    world_set_road(15, 31, ROAD_STREET);
    lot(B_HOUSE_T2, 12, 31);
    tools_month_changed();
    h0 = world_hash(); m0 = g_budget.money;
    n = tools_apply_line(TOOL_PIPE, 0, 10, 31, 30, 31);       /* under a house, a street and a lake tile */
    h1 = world_hash(); m1 = g_budget.money;
    test_log("pipe: %d tiles, cost %d, pipes %d", n, m0 - m1, world_pipe_tiles(NULL));
    CHECK_EQ(n, 21);
    CHECK_EQ(m0 - m1, 20 * g_misc.pipe + g_misc.pipe_water);
    CHECK_EQ(world_pipe_tiles(NULL), 21);
    CHECK(world_tile(12, 31)->lot && (world_tile(12, 31)->flags & TF_PIPE));
    CHECK(TILE_ROAD(world_tile(15, 31)) == ROAD_STREET && (world_tile(15, 31)->flags & TF_PIPE));
    CHECK_EQ(tools_apply_line(TOOL_PIPE, 0, 10, 31, 30, 31), 0);   /* nothing to do */
    CHECK(tools_undo());
    CHECK_EQ(world_hash(), h0);
    CHECK_EQ(g_budget.money, m0);
    CHECK(tools_redo());
    CHECK_EQ(world_hash(), h1);
    CHECK_EQ(g_budget.money, m1);
    /* removal (Ctrl+drag = param 1) refunds 25% of tiles laid this month */
    n = tools_apply_line(TOOL_PIPE, 1, 10, 31, 14, 31);
    CHECK_EQ(n, 5);
    CHECK(!(world_tile(12, 31)->flags & TF_PIPE));
    CHECK(tools_undo());
    CHECK_EQ(world_hash(), h1);
    /* the bulldozer removes pipes only when they are shown (water overlay) */
    tools_apply_rect(TOOL_BULLDOZE, 0, 25, 31, 26, 31);
    CHECK(world_tile(25, 31)->flags & TF_PIPE);
    h2 = world_hash();
    CHECK_EQ(h2, h1);                                          /* nothing else there: nothing happened */
    tools_set_show_pipes(1);
    tools_apply_rect(TOOL_BULLDOZE, 0, 25, 31, 26, 31);
    CHECK(!(world_tile(25, 31)->flags & TF_PIPE));
    CHECK(tools_undo());
    CHECK_EQ(world_hash(), h1);
    tools_set_show_pipes(0);
    /* bulldozing the street keeps the pipe; building on a pipe keeps it */
    tools_apply_rect(TOOL_BULLDOZE, 0, 15, 31, 15, 31);
    CHECK(!TILE_ROAD(world_tile(15, 31)) && (world_tile(15, 31)->flags & TF_PIPE));
    CHECK(tools_apply_build(B_WELL, 16, 31, 0));
    CHECK(world_tile(16, 31)->flags & TF_PIPE);
}

TEST(water_pipes_with_roads_option)
{
    Money m0;
    int n;
    wnew();
    tools_month_changed();
    m0 = g_budget.money;
    n = tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 50, 19, 50);
    CHECK_EQ(n, 10);
    CHECK_EQ(world_pipe_tiles(NULL), 0);
    CHECK_EQ(m0 - g_budget.money, 10 * g_roads[ROAD_STREET].cost);
    tools_set_pipes_with_roads(1);
    m0 = g_budget.money;
    n = tools_apply_line(TOOL_ROAD, ROAD_GRAVEL, 10, 52, 19, 52);
    CHECK_EQ(world_pipe_tiles(NULL), 10);
    CHECK_EQ(m0 - g_budget.money, 10 * (g_roads[ROAD_GRAVEL].cost + g_misc.pipe));
    tools_set_pipes_with_roads(0);
    tools_apply_line(TOOL_ROAD, ROAD_GRAVEL, 10, 54, 19, 54);
    tools_set_pipes_with_roads(1);
    m0 = g_budget.money;
    n = tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 54, 19, 54);  /* upgrade: the pipe comes along */
    CHECK_EQ(m0 - g_budget.money, 10 * (g_roads[ROAD_STREET].cost - g_roads[ROAD_GRAVEL].cost + g_misc.pipe));
    CHECK_EQ(world_pipe_tiles(NULL), 20);
    CHECK(tools_undo());
    CHECK_EQ(world_pipe_tiles(NULL), 10);
    CHECK_EQ(g_budget.money, m0);
    tools_set_pipes_with_roads(0);
    CHECK(n > 0);
}

/* ------------------------------------------------------------------ determinism, fast path, saves */
static FILE *s_town_out;
#define tools_script_line(l, w, o) tools_script_line(l, w, s_town_out)
static void small_town(void)
{
    int x;
    tools_script_line("road 20 40 100 40 street", &x, NULL);
    tools_script_line("road 20 47 100 47 street", &x, NULL);
    tools_script_line("road 20 40 20 54 street", &x, NULL);
    tools_script_line("road 20 54 100 54 street", &x, NULL);
    tools_script_line("road 20 60 100 60 street", &x, NULL);
    tools_script_line("road 20 54 20 60 street", &x, NULL);
    tools_script_line("pipe 20 40 100 40", &x, NULL);
    tools_script_line("pipe 20 54 100 54", &x, NULL);
    tools_script_line("build coal_plant 10 41", &x, NULL);
    tools_script_line("power 12 41 20 41", &x, NULL);
    tools_script_line("power 20 47 100 47", &x, NULL);
    tools_script_line("build well 19 40", &x, NULL);
    tools_script_line("build well 60 39", &x, NULL);
    tools_script_line("build well 19 54", &x, NULL);
    tools_script_line("build school 30 48", &x, NULL);
    tools_script_line("build sauna 40 48", &x, NULL);
    tools_script_line("zone 21 41 99 53 houses", &x, NULL);
    tools_script_line("zone 21 55 99 59 industry", &x, NULL);
    tools_script_line("zone 21 37 99 39 commercial", &x, NULL);
    world_set_road(127, 40, ROAD_STREET);
    tools_script_line("road 101 40 127 40 street", &x, NULL);
    world_set_tile_flags(127, 40, TF_EDGE_LINK, 0);
}
#undef tools_script_line

static void months(int n)
{
    while (n-- > 0) { SimEvent ev; sim_run_months(1); while (sim_poll_event(&ev)) {} tools_month_changed(); }
}

TEST(water_determinism_and_fast_path)
{
    static u8 st[MAX_LOTS];
    u32 h[3];
    int k, i, diff = 0, n = 0;
    for (k = 0; k < 3; k++) {
        wnew();
        if (k == 0) s_town_out = fopen("out/water_town_script.txt", "w");
        small_town();
        if (s_town_out) { fclose(s_town_out); s_town_out = NULL; }
        months(30);
        h[k] = world_hash() ^ sim_hash();
    }
    test_log("hashes %08x %08x %08x, pop %d, water %d/%d short %d", h[0], h[1], h[2], sim_stats()->population,
             net_stats(NET_WATER)->used, net_stats(NET_WATER)->supply, net_stats(NET_WATER)->lots_short);
    CHECK_EQ(h[0], h[1]);
    CHECK_EQ(h[1], h[2]);
    wdump("town", 0, 30, 127, 60);
    /* fast path (graph unchanged) == full BFS */
    net_mark_dirty(NET_WATER); net_step();
    for (i = 1; i < g_world.lot_hi; i++) st[i] = (u8)net_lot_water_state(i);
    world_set_pipe(127, 127, 1); world_set_pipe(127, 127, 0);      /* forces the BFS */
    net_mark_dirty(NET_WATER); net_step();
    for (i = 1; i < g_world.lot_hi; i++) if (g_world.lots[i].def) { n++; if (st[i] != net_lot_water_state(i)) diff++; }
    CHECK(n > 100);
    CHECK_EQ(diff, 0);
    CHECK(sim_stats()->population > 1000);
}

TEST(water_save_roundtrip)
{
    SaveFile *sf;
    SaveMeta m;
    u32 wh, sh, h_unbroken;
    int i;
    static u8 st[MAX_LOTS];
    WaterSrcInfo a, b;
    int src;
    wnew();
    small_town();
    months(18);
    memset(&m, 0, sizeof(m));
    sf = save_mem_open();
    CHECK(save_game_to(sf, &m));
    wh = world_hash(); sh = sim_hash();
    for (i = 1; i < g_world.lot_hi; i++) st[i] = (u8)net_lot_water_state(i);
    src = world_lot_at(60, 39);
    CHECK(net_water_source(src, &a));
    months(12);
    h_unbroken = world_hash() ^ sim_hash();
    save_mem_reopen(sf);
    CHECK(load_game_from(sf, NULL));
    save_mem_close(sf);
    CHECK_EQ(world_hash(), wh);
    CHECK_EQ(sim_hash(), sh);
    for (i = 1; i < g_world.lot_hi; i++) if (st[i] != net_lot_water_state(i)) break;
    CHECK_EQ(i, g_world.lot_hi);
    CHECK(net_water_source(src, &b));
    CHECK(!memcmp(&a, &b, sizeof(a)));
    tools_reset();
    months(12);
    test_log("unbroken %08x, after load %08x", h_unbroken, world_hash() ^ sim_hash());
    CHECK_EQ(world_hash() ^ sim_hash(), h_unbroken);
}

/* v1 saves (water mains in roads): pipes are laid under gravel roads, streets and main roads on load */
TEST(water_old_save_migration)
{
    SaveMeta m;
    int x, y, roads = 0, piped = 0, other = 0;
    FILE *f = fopen("../AUTO1.KAU", "rb");
    if (!f) { test_log("../AUTO1.KAU missing: skipped"); return; }
    fclose(f);
    wnew();
    CHECK(load_game("../AUTO1.KAU", &m));
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = WT(x, y);
            int c = TILE_ROAD(t);
            if (c && (g_roads[c].flags & RDF_CARRIES_WATER)) { roads++; if (t->flags & TF_PIPE) piped++; }
            else if (t->flags & TF_PIPE) other++;
        }
    test_log("AUTO1 (%d): %d water-class road tiles, %d piped, %d pipes elsewhere; water %d/%d served %d short %d unconnected %d",
             m.year, roads, piped, other, net_stats(NET_WATER)->used, net_stats(NET_WATER)->supply,
             net_stats(NET_WATER)->lots_served, net_stats(NET_WATER)->lots_short, net_stats(NET_WATER)->lots_unconnected);
    wdump("auto1", 0, 0, g_world.w - 1, g_world.h - 1);
    CHECK(roads > 0);
    if (other > 0) {   /* PL2: AUTO1.KAU is now the user's play-test autosave (pipes off the roads: a post-F1 save, no migration) */
        test_log("AUTO1 is a post-F1 save (%d pipes off roads): migration check skipped", other);
        CHECK(net_stats(NET_WATER)->lots_served > 0);
        return;
    }
    CHECK_EQ(piped, roads);
    CHECK_EQ(other, 0);
    CHECK(net_stats(NET_WATER)->lots_served > 0);
    CHECK(net_stats(NET_WATER)->supply > 0);
}

/* the solve on the 20k bench city (BENCH20K.KAU, v1 -> migrated): full BFS and fast path, Mac ms
   (x ~100 for the P2-233 estimate, then confirm in the VM with -bench) */
TEST(water_bench20k_solve_time)
{
    SaveMeta m;
    int k, reps = 20;
    double t0, tfull = 1e9, tfast = 1e9, tpow = 1e9, t;
    FILE *f = fopen("../BENCH20K.KAU", "rb"), *o;
    if (!f) { test_log("../BENCH20K.KAU missing: skipped"); return; }
    fclose(f);
    wnew();
    CHECK(load_game("../BENCH20K.KAU", &m));
    for (k = 0; k < reps; k++) {
        world_set_pipe(0, 0, (k & 1) == 0);               /* passability changes: full BFS */
        net_mark_dirty(NET_WATER);
        t0 = time_ms(); net_step(); t = time_ms() - t0; if (t < tfull) tfull = t;
        net_mark_dirty(NET_WATER);
        t0 = time_ms(); net_step(); t = time_ms() - t0; if (t < tfast) tfast = t;
        world_set_power_line(0, 1, (k & 1) == 0);         /* structural: full power flood */
        net_mark_dirty(NET_POWER);
        t0 = time_ms(); net_step(); t = time_ms() - t0; if (t < tpow) tpow = t;
        net_step();                                       /* (a pump power change may queue water) */
    }
    test_log("BENCH20K pop %d lots %d: water full %.3f ms, fast %.3f ms, power flood %.3f ms (Mac; P2 ~x100)",
             sim_stats()->population, g_world.lot_count, tfull, tfast, tpow);
    test_log("water %d/%d, served %d short %d unconnected %d networks %d", net_stats(NET_WATER)->used,
             net_stats(NET_WATER)->supply, net_stats(NET_WATER)->lots_served, net_stats(NET_WATER)->lots_short,
             net_stats(NET_WATER)->lots_unconnected, net_stats(NET_WATER)->networks);
    o = fopen("out/water_bench20k.txt", "w");
    if (o) {
        fprintf(o, "BENCH20K pop %d lots %d: water full %.3f ms, fast %.3f ms, power flood %.3f ms (Mac; P2 ~x100)\n",
                sim_stats()->population, g_world.lot_count, tfull, tfast, tpow);
        fclose(o);
    }
    CHECK(tfull < 2.0 * tpow);            /* relative guard (the Mac is shared: absolute ms vary 3x); VM numbers: bench.txt "F1 water solve" */
}

/* save/load determinism on the scripted town1 session (M1 report): run the script, save after N months, load
   into a fresh city, continue; the hash must equal the unbroken run's. */
#include "mapgen.h"
static int s_t1_steps, s_t1_init;     /* extra sim steps after the split month (mid-cycle saves); net_init on load */
static u32 town1_run(int split_at, int total, u32 *h_at_split)
{
    static char buf[64 * 1024];
    FILE *f = fopen("../scripts/town1.txt", "rb");
    MapGenParams mp;
    SimSettings ss;
    char *p, *line;
    int n, wait, months = 0;
    if (!f) return 0;
    n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    wnew();
    mapgen_defaults(&mp); mp.seed = 7; mp.preset = MAP_PRESET_LAKELAND; mapgen_generate(&mp);
    sim_default_settings(&ss); ss.seed = 7; sim_new_city(&ss);
    tools_reset(); tools_script_reset();
    tools_script_set_dir("out/");
    for (p = buf; *p && months < total; ) {
        line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        if (strncmp(line, "statlog", 7) == 0 || strncmp(line, "dump", 4) == 0 || strncmp(line, "expect", 6) == 0) continue;
        if (tools_script_line(line, &wait, NULL) == SCR_WAIT) {
            while (wait-- > 0 && months < total) {
                SimEvent ev;
                sim_run_months(1);
                while (sim_poll_event(&ev)) {}
                tools_month_changed();
                months++;
                if (split_at < 0 && months == -split_at) { int st; for (st = 0; st < s_t1_steps; st++) sim_step(); }
                if (months == split_at) {
                    SaveFile *sf;
                    int st;
                    for (st = 0; st < s_t1_steps; st++) sim_step();
                    sf = save_mem_open();
                    SaveMeta m;
                    memset(&m, 0, sizeof(m));
                    save_game_to(sf, &m);
                    if (h_at_split) *h_at_split = world_hash() ^ sim_hash();
                    world_new(64, 64); sim_new_city(&ss);      /* forget everything */
                    if (s_t1_init) net_init();                /* like a fresh process */
                    save_mem_reopen(sf);
                    if (!load_game_from(sf, NULL)) { save_mem_close(sf); return 0; }
                    save_mem_close(sf);
                    if (h_at_split && *h_at_split != (world_hash() ^ sim_hash())) *h_at_split = 0;
                }
            }
        }
    }
    while (months < total) { SimEvent ev; sim_run_months(1); while (sim_poll_event(&ev)) {} tools_month_changed(); months++; }
    tools_script_set_dir("");
    return world_hash() ^ sim_hash();
}

TEST(water_town1_save_determinism)
{
    static const int ns[3] = { 7, 18, 30 };
    int k;
    for (k = 0; k < 3; k++) {
        u32 hs = 1, a, b;
        s_t1_steps = k * 5 + 3; s_t1_init = k & 1;
        a = town1_run(-ns[k], ns[k] + 12, NULL);
        b = town1_run(ns[k], ns[k] + 12, &hs);
        test_log("town1 split at %d: unbroken %08x, save+load %08x %s (load exact: %s)", ns[k], a, b, a == b ? "same" : "DIFFERENT",
                 hs ? "yes" : "no");
        CHECK(hs != 0);
        CHECK_EQ(a, b);
    }
}
