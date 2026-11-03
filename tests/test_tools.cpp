/* test_tools.cpp - T08: tools (paths, costs, rejected tiles, upgrades, bridges, zones, buildings,
   bulldoze refunds, undo/redo exactness), the script interpreter and traffic. Dumps to tests/out/. */
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
#include "mapgen.h"

static void fresh(void)
{
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    sim_new_city(&s);
    tools_reset();
}

static void lake(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) world_set_terrain(x, y, TER_WATER, 0);
}

static int count_state(const ToolPreview *p, int st)
{
    int i, n = 0;
    for (i = 0; i < p->n; i++) if (p->tiles[i].state == st) n++;
    return n;
}

TEST(tools_road_l_path_and_cost)
{
    Money m0;
    int n;
    fresh();
    m0 = g_budget.money;
    n = tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 10, 20, 15);   /* |dx| >= |dy|: x first */
    CHECK_EQ(n, 16);
    CHECK_EQ(g_budget.money, m0 - 16 * g_roads[ROAD_STREET].cost);
    CHECK_EQ(TILE_ROAD(world_tile(20, 10)), ROAD_STREET);          /* the corner */
    CHECK_EQ(TILE_ROAD(world_tile(10, 15)), ROAD_NONE);
    CHECK_EQ(TILE_ROAD(world_tile(20, 15)), ROAD_STREET);
    /* autotile: the corner links SW (to 20,11) and NW (to 19,10) */
    CHECK_EQ(TILE_ROAD_MASK(world_tile(20, 10)), (1 << DIR_SW) | (1 << DIR_NW));
    /* y-first when |dy| > |dx| */
    n = tools_apply_line(TOOL_ROAD, ROAD_GRAVEL, 40, 40, 42, 50);
    CHECK_EQ(n, 13);
    CHECK_EQ(TILE_ROAD(world_tile(40, 50)), ROAD_GRAVEL);
    /* drawing over an existing road of the same class does nothing (and is not an error) */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 10, 20, 10), 0);
    CHECK_EQ(tools_last_reason(), REJ_NOTHING_TO_DO);
}

TEST(tools_road_forest_rock_costs)
{
    Money m0, t0;
    fresh();
    world_set_terrain(31, 5, TER_FOREST, FOREST_PINE);
    world_set_terrain(32, 5, TER_ROCK, 0);
    m0 = g_budget.money; t0 = g_budget.cur[BL_TIMBER];
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 30, 5, 33, 5), 4);
    CHECK_EQ(g_budget.money, m0 - 4 * 12 - g_misc.clear_forest - g_misc.blast_rock + g_misc.timber_income);
    CHECK_EQ(g_budget.cur[BL_TIMBER] - t0, g_misc.timber_income);
    CHECK_EQ(TER_TYPE(world_tile(31, 5)), TER_GRASS);
    CHECK_EQ(TER_TYPE(world_tile(32, 5)), TER_GRASS);                 /* blasted */
}

TEST(tools_road_upgrade_charges_difference)
{
    Money m0;
    fresh();
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_GRAVEL, 5, 5, 9, 5), 5);
    m0 = g_budget.money;
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 3, 5, 9, 5), 7);    /* 2 new + 5 upgrades */
    CHECK_EQ(g_budget.money, m0 - 2 * 12 - 5 * (12 - 5));
    CHECK_EQ(TILE_ROAD(world_tile(5, 5)), ROAD_STREET);
    /* a lower class over a higher one: no-op */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_GRAVEL, 3, 5, 9, 5), 0);
    CHECK_EQ(TILE_ROAD(world_tile(7, 5)), ROAD_STREET);
    /* main road is locked in 1955 (market town) */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_MAIN, 3, 8, 9, 8), 0);
    CHECK_EQ(tools_last_reason(), REJ_LOCKED);
}

TEST(tools_bridges)
{
    const ToolPreview *p;
    Money m0;
    fresh();
    lake(20, 0, 25, 127);                 /* 6 wide strait */
    lake(60, 0, 69, 127);                 /* 10 wide: too long */
    m0 = g_budget.money;
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 18, 30, 27, 30), 10);
    CHECK_EQ(g_budget.money, m0 - 4 * 12 - 6 * g_roads[ROAD_STREET].bridge_cost);
    CHECK_EQ(TILE_ROAD(world_tile(22, 30)), ROAD_STREET);
    /* too long: the water run is rejected, the land parts are built */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 58, 30, 71, 30), 4);
    p = tools_preview();
    CHECK_EQ(p->n_rejected, 10);
    CHECK_EQ(p->main_reason, REJ_BRIDGE_TOO_LONG);
    CHECK_EQ(TILE_ROAD(world_tile(64, 30)), ROAD_NONE);
    /* L-path turning on the water: not straight */
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 18, 50, 23, 55);
    p = tools_preview();
    CHECK(p->n_rejected > 0);
    CHECK_EQ(p->main_reason, REJ_BRIDGE_NOT_STRAIGHT);
    CHECK_EQ(TILE_ROAD(world_tile(21, 50)), ROAD_NONE);
    /* ending in the water: rejected (bridges must land) */
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 18, 70, 22, 70);
    CHECK_EQ(TILE_ROAD(world_tile(21, 70)), ROAD_NONE);
    CHECK_EQ(TILE_ROAD(world_tile(19, 70)), ROAD_STREET);
}

TEST(tools_zone_road_reach)
{
    const ToolPreview *p;
    int n;
    fresh();
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 40, 30, 40);
    n = tools_apply_rect(TOOL_ZONE, ZONE_HOUSES, 10, 30, 30, 50);
    /* rows 37..39 and 41..43 (6 rows x 21) accepted; the road row is a no-op; others rejected */
    CHECK_EQ(n, 6 * 21);
    p = tools_preview();
    CHECK_EQ(p->n_rejected, 21 * 21 - 7 * 21);
    CHECK_EQ(p->main_reason, REJ_NO_ROAD_REACH);
    CHECK_EQ(count_state(p, PT_REJECT), p->n_rejected);
    CHECK_EQ(p->eff_residents, 6 * 21 * def_building(B_HOUSE_T1)->capacity);
    CHECK_EQ(TILE_ZONE(world_tile(15, 43)), ZONE_HOUSES);
    CHECK_EQ(TILE_ZONE(world_tile(15, 44)), ZONE_NONE);
    /* apartments are locked at village status */
    CHECK_EQ(tools_apply_rect(TOOL_ZONE, ZONE_APARTMENTS, 10, 38, 12, 39), 0);
    CHECK_EQ(tools_last_reason(), REJ_LOCKED);
    /* rezone to commercial and dezone */
    CHECK_EQ(tools_apply_rect(TOOL_ZONE, ZONE_COMMERCIAL, 10, 41, 30, 41), 21);
    CHECK_EQ(tools_apply_rect(TOOL_DEZONE, 0, 10, 41, 30, 43), 63);
    CHECK_EQ(TILE_ZONE(world_tile(20, 42)), ZONE_NONE);
}

TEST(tools_build_rotation_and_costs)
{
    int id;
    Money m0;
    const Lot *l;
    fresh();
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 60, 40, 60);
    m0 = g_budget.money;
    id = tools_apply_build(B_FIRE_STATION, 12, 59, -1);        /* road to the SW (+y) */
    CHECK(id > 0);
    l = world_lot(id);
    CHECK_EQ(l->rot, ROT_0);
    CHECK(l->flags & LF_NEW);
    CHECK_EQ(g_budget.money, m0 - def_building(B_FIRE_STATION)->cost);
    id = tools_apply_build(B_CLINIC, 14, 61, -1);              /* road to the NE (-y) */
    CHECK_EQ(world_lot(id)->rot, ROT_2);
    id = tools_apply_build(B_SCHOOL, 20, 61, 1);               /* explicit rotation kept */
    CHECK_EQ(world_lot(id)->rot, ROT_1);
    /* on forest: clearing cost + timber */
    world_set_terrain(30, 58, TER_FOREST, 0); world_set_terrain(31, 58, TER_FOREST, 0);
    m0 = g_budget.money;
    id = tools_apply_build(B_COAL_PLANT, 30, 58, -1);
    CHECK(id > 0);
    CHECK_EQ(g_budget.money, m0 - def_building(B_COAL_PLANT)->cost - 2 * g_misc.clear_forest + 2 * g_misc.timber_income); /* T12: was 3000 */
    CHECK_EQ(world_lot(id)->rot, ROT_0);
    /* occupied / on the road / unique / shore */
    CHECK_EQ(tools_apply_build(B_CLINIC, 12, 59, -1), 0);
    CHECK_EQ(tools_last_reason(), PLACE_OCCUPIED);
    CHECK_EQ(tools_apply_build(B_CLINIC, 25, 60, -1), 0);
    CHECK_EQ(tools_last_reason(), PLACE_ROAD);
    CHECK_EQ(tools_apply_build(B_PUMP, 50, 50, -1), 0);
    CHECK_EQ(tools_last_reason(), PLACE_NEEDS_SHORE);
    CHECK_EQ(tools_apply_build(B_WATER_TOWER, 50, 50, -1), 0);  /* market town */
    CHECK_EQ(tools_last_reason(), REJ_LOCKED);
    /* not enough money */
    g_budget.money = 100;
    CHECK_EQ(tools_apply_build(B_SCHOOL, 33, 61, -1), 0);
    CHECK_EQ(tools_last_reason(), REJ_NO_MONEY);
}

TEST(tools_bulldoze_refund)
{
    int id;
    Money m0;
    fresh();
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 60, 40, 60);
    id = tools_apply_build(B_SCHOOL, 12, 58, -1);
    CHECK(id > 0);
    m0 = g_budget.money;
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 13, 59, 13, 59), 1);   /* one tile of the 2x2 removes all */
    CHECK_EQ(world_lot_at(12, 58), 0);
    CHECK_EQ(g_budget.money, m0 - 4 * g_misc.bulldoze_tile + 1000 * g_misc.refund_pct / 100);
    /* roads built this month: 25% back; protected tiles rejected */
    m0 = g_budget.money;
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 30, 60, 34, 60), 5);
    CHECK_EQ(g_budget.money, m0 - 5 * g_misc.bulldoze_tile + 5 * (12 * g_misc.refund_pct / 100));
    world_set_tile_flags(20, 60, TF_PROTECTED, 0);
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 20, 60, 20, 60), 0);
    CHECK_EQ(tools_last_reason(), REJ_PROTECTED_TILE);
    /* forest clearing gives timber */
    world_set_terrain(50, 50, TER_FOREST, 1);
    m0 = g_budget.money;
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 50, 50, 51, 50), 1);
    CHECK_EQ(g_budget.money, m0 - g_misc.clear_forest + g_misc.timber_income);
    CHECK_EQ(TER_TYPE(world_tile(50, 50)), TER_GRASS);
}

TEST(tools_undo_redo_exact)
{
    u32 h0, h1;
    Money m0, m1;
    int i, n;
    fresh();
    lake(70, 0, 74, 127);
    for (i = 0; i < 20; i++) world_set_terrain(40 + i, 20, TER_FOREST, i & 3);
    world_set_tile_flags(45, 25, TF_TREE, 0);
    world_set_terrain(46, 25, TER_ROCK, 0);
    h0 = world_hash(); m0 = g_budget.money;
    tools_apply_line(TOOL_ROAD, ROAD_GRAVEL, 35, 20, 80, 20);         /* forest + bridge */
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 35, 20, 50, 20);         /* upgrade */
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 44, 21, 44, 30);
    tools_apply_line(TOOL_POWER_LINE, 0, 30, 25, 50, 25);             /* tree, rock, crossing */
    tools_apply_rect(TOOL_ZONE, ZONE_HOUSES, 36, 21, 43, 24);
    tools_apply_build(B_COAL_PLANT, 52, 21, -1);
    tools_apply_build(B_SAUNA, 45, 21, -1);
    tools_apply_rect(TOOL_BULLDOZE, 0, 45, 21, 45, 21);
    tools_apply_rect(TOOL_BULLDOZE, 0, 38, 20, 41, 20);
    tools_apply_rect(TOOL_DEZONE, 0, 36, 21, 37, 24);
    n = tools_undo_count();
    CHECK_EQ(n, 10);
    h1 = world_hash(); m1 = g_budget.money;
    CHECK(h1 != h0);
    for (i = 0; i < n; i++) CHECK(tools_undo());
    CHECK(!tools_can_undo());
    CHECK_EQ(world_hash(), h0);
    CHECK_EQ(g_budget.money, m0);
    CHECK_EQ(g_budget.cur[BL_CONSTRUCTION], 0);
    CHECK_EQ(g_budget.cur[BL_TIMBER], 0);
    for (i = 0; i < n; i++) CHECK(tools_redo());
    CHECK_EQ(world_hash(), h1);
    CHECK_EQ(g_budget.money, m1);
    /* undo half, then a new command drops the redo branch */
    for (i = 0; i < 3; i++) tools_undo();
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 100, 20, 100);
    CHECK(!tools_can_redo());
    CHECK_EQ(tools_undo_count(), n - 3 + 1);
    /* month change clears the log */
    sim_run_months(1);
    tools_month_changed();
    CHECK(!tools_can_undo());
}

TEST(tools_mouse_grammar)
{
    ToolInput in;
    const ToolPreview *p;
    int r;
    fresh();
    memset(&in, 0, sizeof(in));
    in.on_map = 1;
    tools_select(TOOL_ROAD, ROAD_STREET);
    in.tx = 10; in.ty = 10; in.pressed = 1; in.down = 1;
    CHECK_EQ(tools_update(&in), TRES_NONE);
    in.pressed = 0; in.tx = 20; in.ty = 13; in.shift = 1;        /* Shift: straight along x */
    tools_update(&in);
    p = tools_preview();
    CHECK(p->dragging);
    CHECK_EQ(p->n, 11);
    CHECK_EQ(p->cost, 11 * 12);
    CHECK_EQ(tools_preview_mask(15, 10), (1 << DIR_SE) | (1 << DIR_NW));
    in.released = 1; in.down = 0;
    r = tools_update(&in);
    CHECK_EQ(r, TRES_COMMITTED);
    CHECK_EQ(TILE_ROAD(world_tile(20, 10)), ROAD_STREET);
    CHECK_EQ(TILE_ROAD(world_tile(20, 13)), ROAD_NONE);
    /* right click cancels a drag, then deselects the tool, then queries */
    in.released = 0; in.shift = 0; in.pressed = 1; in.down = 1; in.tx = 30; in.ty = 30;
    tools_update(&in);
    in.pressed = 0; in.cancel = 1;
    CHECK_EQ(tools_update(&in), TRES_CANCELLED);
    CHECK_EQ(TILE_ROAD(world_tile(30, 30)), ROAD_NONE);
    CHECK_EQ(tools_tool(NULL), TOOL_ROAD);
    in.down = 0;
    CHECK_EQ(tools_update(&in), TRES_CANCELLED);
    CHECK_EQ(tools_tool(NULL), TOOL_NONE);
    CHECK_EQ(tools_update(&in), TRES_QUERY);
    /* build hover ghost + click */
    in.cancel = 0;
    tools_select(TOOL_BUILD, B_SCHOOL);
    in.tx = 15; in.ty = 8; in.pressed = 0;
    tools_update(&in);
    p = tools_preview();
    CHECK(p->lot_ok);
    CHECK_EQ(p->lot_w, 2);
    CHECK_EQ(p->cost, 1000);
    in.pressed = 1;
    CHECK_EQ(tools_update(&in), TRES_COMMITTED);
    CHECK(world_lot_at(15, 8) != 0);
}

TEST(tools_script_and_traffic)
{
    FILE *out = fopen("out/tools_script.txt", "w");
    FILE *sf = fopen("out/tools_script_in.txt", "w");
    int errors, x, maxflow = 0;
    fresh();
    CHECK(out && sf);
    /* national road + a small town (like the -script session, but on a blank map) */
    fprintf(sf, "# test script\nroad 0 64 127 64 street\nlog national road done\n"
                "build town_hall 60 62\n"
                "road 40 50 80 50\nroad 40 50 40 78\nroad 80 50 80 78\nroad 40 78 80 78\nroad 60 50 60 78\n"
                "zone 41 51 59 63 houses\nzone 41 65 59 77 houses\nzone 61 65 79 70 commercial\nzone 61 71 79 77 industry\n"
                "build coal_plant 90 60\npower 82 61 89 61\npower 81 61 81 61\n"
                "build well 44 48\nbuild well 48 48\nbuild well 52 48\n"
                "build school 62 52\nbuild sauna 66 52\nbuild fire_station 68 52\nbuild clinic 70 52\n"
                "speed 3\nwait 24\nexpect pop > 300\nexpect money > -50000\ndump report\ndump tools\nnonsense command\n");
    fclose(sf);
    world_set_tile_flags(0, 64, TF_EDGE_LINK, 0);   /* set before the road: mask update */
    world_set_tile_flags(127, 64, TF_EDGE_LINK, 0);
    errors = tools_script_run_file("out/tools_script_in.txt", out);
    fclose(out);
    CHECK_EQ(errors, 0);
    CHECK(sim_stats()->population > 300);
    CHECK(traffic_passes() > 0);
    CHECK(traffic_total_trips() > 0);
    for (x = 41; x < 80; x++) maxflow = IMAX(maxflow, traffic_flow_raw(x, 64));
    test_log("pop %d, trips %d, avg commute %d, national road max flow %d, block commute at 50,55: %d",
             sim_stats()->population, traffic_total_trips(), traffic_avg_commute(), maxflow, traffic_commute(50, 55));
    CHECK(traffic_edge_connected(40, 60));
    CHECK(traffic_commute(50, 55) < 255);
    CHECK(traffic_next_dir(40, 60) >= 0);
}

/* the -script session's map and script: same commands the VM runs (scripts/town1.txt) */
TEST(tools_script_session_town1)
{
    MapGenParams mp;
    FILE *out;
    FILE *map;
    int errors;
    fresh();
    mapgen_defaults(&mp);
    mp.seed = 7;
    if (!mapgen_generate(&mp)) { CHECK(0); }
    {
        SimSettings s;
        sim_default_settings(&s);
        s.seed = 7;
        sim_new_city(&s);
        tools_reset();
    }
    test_log("town1 map: town site %d,%d, road %d,%d -> %d,%d, buildable %d%%", mapgen_report()->town_x,
             mapgen_report()->town_y, mapgen_report()->road_x0, mapgen_report()->road_y0, mapgen_report()->road_x1,
             mapgen_report()->road_y1, mapgen_report()->buildable_pct);
    map = fopen("out/town1_map_before.txt", "w");
    if (map) { mapgen_dump_ascii(map); fclose(map); }
    out = fopen("out/town1_script_out.txt", "w");
    remove("out/town1_report.txt");
    tools_script_set_dir("out/");
    errors = tools_script_run_file("../scripts/town1.txt", out);
    sim_statlog_close();
    tools_script_set_dir("");
    if (out) fclose(out);
    map = fopen("out/town1_map_after.txt", "w");
    if (map) { world_dump(map, 0, 0, g_world.w - 1, g_world.h - 1); fclose(map); }
    test_log("town1: %d errors, pop %d, money %d, status %d", errors, sim_stats()->population, g_budget.money,
             sim_stats()->town_status);
    if (errors < 0) { test_log("scripts/town1.txt missing"); return; }
    CHECK_EQ(errors, 0);
}

/* F3 (PLAYTEST 5): selecting any tool deselects the previous one: Gravel road -> Industry kept the road tool.
   The portable part: tools_select replaces the tool, also in the middle of a drag (the drag is dropped, the
   release does not commit the old tool), and the new tool works on the next drag. (The UI part: a tool key with
   a fly-out now selects its last-used entry at once, ui.cpp; checked by -test ui in the VM.) */
static int t_drag(int x0, int y0, int x1, int y1)
{
    ToolInput in;
    int res;
    memset(&in, 0, sizeof(in));
    in.on_map = 1; in.tx = x0; in.ty = y0; in.down = 1; in.pressed = 1;
    tools_update(&in);
    in.pressed = 0; in.tx = x1; in.ty = y1;
    tools_update(&in);
    in.down = 0; in.released = 1;
    res = tools_update(&in);
    return res;
}

TEST(tools_switch_deselects_previous)
{
    ToolInput in;
    int param, x, y, roads = 0, zoned = 0;
    fresh();
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 30, 40, 30);
    tools_select(TOOL_ROAD, ROAD_GRAVEL);
    CHECK_EQ(tools_tool(&param), TOOL_ROAD);
    /* start a gravel drag, then pick Industry before releasing */
    memset(&in, 0, sizeof(in));
    in.on_map = 1; in.tx = 12; in.ty = 32; in.down = 1; in.pressed = 1;
    tools_update(&in);
    CHECK(tools_dragging());
    tools_select(TOOL_ZONE, ZONE_INDUSTRY);
    CHECK_EQ(tools_tool(&param), TOOL_ZONE);
    CHECK_EQ(param, ZONE_INDUSTRY);
    CHECK(!tools_dragging());
    in.pressed = 0; in.tx = 20; in.ty = 32; in.down = 0; in.released = 1;
    tools_update(&in);                                 /* the release of the old drag commits nothing */
    for (x = 10; x <= 22; x++) if (TILE_ROAD(world_tile(x, 32))) roads++;
    CHECK_EQ(roads, 0);
    /* the new tool zones on its own drag; the road tool is gone */
    CHECK_EQ(t_drag(12, 31, 16, 33), TRES_COMMITTED);
    for (y = 31; y <= 33; y++) for (x = 12; x <= 16; x++) zoned += TILE_ZONE(world_tile(x, y)) == ZONE_INDUSTRY;
    for (x = 10; x <= 22; x++) if (TILE_ROAD(world_tile(x, 31)) || TILE_ROAD(world_tile(x, 33))) roads++;
    test_log("after Gravel -> Industry: %d industry tiles, %d stray road tiles, tool %d/%d", zoned, roads, tools_tool(&param), param);
    CHECK_EQ(zoned, 15);
    CHECK_EQ(roads, 0);
    /* every tool replaces every other one (button, fly-out and hotkey all end in tools_select) */
    for (x = TOOL_QUERY; x < TOOL_COUNT; x++) {
        tools_select(TOOL_ROAD, ROAD_GRAVEL);
        tools_select(x, x == TOOL_ZONE ? ZONE_COMMERCIAL : x == TOOL_BUILD ? B_SCHOOL : 0);
        CHECK_EQ(tools_tool(NULL), x);
    }
}
