/* test_pl3.cpp - PL3 bugs & UX polish: motorway road masks match traffic's link rule, bus line edits and gift
   placements are undoable, the newsreel's founding line, the heat overlay without the blue carpet. Owner: PL3.
   The mill town helpers are copied from tests/test_complex.cpp (credit M6). */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "budget.h"
#include "complex.h"
#include "tools.h"
#include "traffic.h"
#include "transit.h"
#include "era.h"
#include "save.h"

static void p3_new(int year, int all)
{
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = 41; s.start_year = year; s.all_unlocked = all; s.politics = 0;
    sim_new_city(&s);
    tools_reset();
}

static void p3_road(int x0, int y0, int x1, int y1, int cls)
{
    int x, y;
    world_batch_begin();
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!TILE_ROAD(world_tile(x, y)) && !world_tile(x, y)->lot) world_set_road(x, y, cls);
    world_batch_end();
}

/* every car-road mask bit in the box equals traffic_link_ok */
static int mask_mismatches(int x0, int y0, int x1, int y1)
{
    int x, y, d, bad = 0;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            int c = TILE_ROAD(world_tile(x, y));
            if (!c || c == ROAD_RAIL) continue;
            for (d = 0; d < DIR_COUNT; d++) {
                int nx = x + g_dir_dx[d], ny = y + g_dir_dy[d], m, t;
                if (!world_in(nx, ny)) continue;
                m = (TILE_ROAD_MASK(world_tile(x, y)) >> d) & 1;
                t = traffic_link_ok(x, y, d) ? 1 : 0;
                if (m != t) { if (bad < 5) test_log("mask/link mismatch at %d,%d dir %d: mask %d link %d", x, y, d, m, t); bad++; }
            }
        }
    return bad;
}

TEST(pl3_motorway_mask_matches_traffic)
{
    p3_new(1970, 1);
    p3_road(10, 20, 60, 20, ROAD_MOTORWAY);
    p3_road(30, 10, 30, 19, ROAD_STREET);         /* street touching the side: no join */
    p3_road(40, 10, 40, 19, ROAD_STREET);         /* street crossing it: interchange */
    p3_road(40, 21, 40, 30, ROAD_STREET);
    p3_road(61, 20, 70, 20, ROAD_STREET);         /* street continuing from its end */
    p3_road(20, 21, 20, 25, ROAD_GRAVEL);         /* gravel touching the other side */
    CHECK_EQ(TILE_ROAD_MASK(world_tile(30, 20)), (1 << DIR_SE) | (1 << DIR_NW));   /* straight motorway */
    CHECK_EQ(TILE_ROAD_MASK(world_tile(30, 19)) & (1 << DIR_SW), 0);              /* the street ends short */
    CHECK_EQ(TILE_ROAD_MASK(world_tile(40, 20)), 15);                               /* crossing */
    CHECK(TILE_ROAD_MASK(world_tile(60, 20)) & (1 << DIR_SE));                      /* the end joins */
    CHECK_EQ(mask_mismatches(5, 5, 75, 35), 0);
    /* removing the far side of the crossing (2 tiles from the street above) turns it into a side touch */
    world_set_road(40, 21, ROAD_NONE);
    CHECK_EQ(TILE_ROAD_MASK(world_tile(40, 19)) & (1 << DIR_SW), 0);
    CHECK_EQ(TILE_ROAD_MASK(world_tile(40, 20)), (1 << DIR_SE) | (1 << DIR_NW));
    CHECK_EQ(mask_mismatches(5, 5, 75, 35), 0);
    world_set_road(40, 21, ROAD_STREET);
    CHECK_EQ(TILE_ROAD_MASK(world_tile(40, 20)), 15);
    CHECK_EQ(mask_mismatches(5, 5, 75, 35), 0);
}

static int click(int tx, int ty, int ctrl)
{
    ToolInput in;
    memset(&in, 0, sizeof(in));
    in.on_map = 1; in.tx = tx; in.ty = ty; in.pressed = 1; in.down = 1; in.ctrl = ctrl;
    return tools_update(&in);
}

TEST(pl3_bus_line_undo)
{
    Money m0;
    int lots0;
    p3_new(1960, 1);
    g_budget.unlimited = 0;                         /* all_unlocked = free money: count it here */
    g_budget.money = 100000;
    p3_road(10, 10, 40, 10, ROAD_STREET);
    lots0 = g_world.lot_count;
    m0 = g_budget.money;
    tools_select(TOOL_BUS_LINE, 0);
    CHECK_EQ(click(12, 10, 0), TRES_COMMITTED);
    CHECK_EQ(click(30, 10, 0), TRES_COMMITTED);
    CHECK_EQ(transit_line_count(), 1);
    CHECK_EQ(transit_line(0)->nstops, 2);
    CHECK_EQ(m0 - g_budget.money, BUS_LINE_COST + 2 * BUS_STOP_COST);
    test_log("line with 2 stops: %d shelters, undo count %d", g_world.lot_count - lots0, tools_undo_count());
    CHECK(tools_undo());                            /* the second stop */
    CHECK_EQ(transit_line(0)->nstops, 1);
    CHECK_EQ(m0 - g_budget.money, BUS_LINE_COST + BUS_STOP_COST);
    CHECK(tools_undo());                            /* the line */
    CHECK_EQ(transit_line_count(), 0);
    CHECK_EQ(g_budget.money, m0);
    CHECK_EQ(g_world.lot_count, lots0);             /* shelters gone */
    CHECK(tools_redo()); CHECK(tools_redo());
    CHECK_EQ(transit_line_count(), 1);
    CHECK_EQ(transit_line(0)->nstops, 2);
    CHECK_EQ(m0 - g_budget.money, BUS_LINE_COST + 2 * BUS_STOP_COST);
    /* Ctrl+click deletes the line; undo brings it back with its stops */
    CHECK_EQ(click(12, 10, 1), TRES_COMMITTED);
    CHECK_EQ(transit_line_count(), 0);
    CHECK(tools_undo());
    CHECK_EQ(transit_line_count(), 1);
    CHECK_EQ(transit_line(0)->nstops, 2);
    CHECK_EQ(transit_line(0)->sx[1], 30);
    CHECK(tools_undo());                            /* then the second stop again */
    CHECK_EQ(transit_line(0)->nstops, 1);
}

/* ---- gifts (mill town from test_complex.cpp, credit M6) */
#define MILL_X 14
#define MILL_Y 50
static void p3_terrain(int x0, int y0, int x1, int y1, int ter, int var)
{
    int x, y;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) world_set_terrain(x, y, ter, var);
}
static void p3_line(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++) if (!world_tile(x, y)->lot && !TILE_ROAD(world_tile(x, y))) world_set_power_line(x, y, 1);
}
static void p3_pipe(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++) for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++) world_set_pipe(x, y, 1);
}
static void p3_mill_town(void)
{
    int i, x, y;
    p3_road(0, 64, 127, 64, ROAD_MAIN);
    for (i = 0; i < 128; i++) world_set_tile_flags(i, 64, TF_PROTECTED, 0);
    world_set_tile_flags(0, 64, TF_EDGE_LINK, 0);
    world_set_tile_flags(127, 64, TF_EDGE_LINK, 0);
    p3_terrain(4, 30, 13, 62, TER_WATER, 0);
    p3_road(20, 46, 20, 63, ROAD_STREET);
    for (y = 40; y <= 62; y++) for (x = 80; x <= 110; x++) if (world_is_buildable(x, y)) world_set_zone(x, y, ZONE_HOUSES);
    p3_road(80, 44, 110, 44, ROAD_STREET); p3_road(80, 50, 110, 50, ROAD_STREET); p3_road(80, 56, 110, 56, ROAD_STREET);
    p3_road(79, 44, 79, 63, ROAD_STREET);
    world_lot_place(B_COAL_PLANT, 114, 60, ROT_0, 0, LOT_ACTIVE);
    p3_line(14, 63, 113, 63); p3_line(113, 61, 113, 62);
    world_lot_place(B_SCHOOL, 76, 60, ROT_0, 0, LOT_ACTIVE);
    world_lot_place(B_WELL, 112, 44, ROT_0, 0, LOT_ACTIVE);
    world_lot_place(B_WELL, 112, 56, ROT_0, 0, LOT_ACTIVE);
    p3_pipe(80, 50, 111, 50); p3_pipe(111, 44, 111, 56);
    p3_line(15, 55, 16, 55); p3_line(16, 56, 16, 62);
    world_lot_place(B_PUMP, MILL_X, 55, ROT_0, 0, LOT_ACTIVE);
}

TEST(pl3_gift_undo)
{
    int k, w, h, lot, lots0;
    u32 h0;
    p3_new(1955, 1);
    p3_mill_town();
    sim_run_months(18);
    CHECK(complex_place(CPX_PAPER, MILL_X, MILL_Y));
    sim_run_months(7);
    for (k = 0; k < 14 && !complex_gift_pending(); k++) { complex_set_mood(CPX_PAPER, 95); sim_run_months(1); }
    CHECK_EQ(complex_gift_pending(), 1);
    CHECK_EQ(complex_gift_def(0), B_ROW_HOUSES);
    p3_road(21, 47, 30, 47, ROAD_STREET);
    p3_terrain(22, 48, 25, 50, TER_FOREST, FOREST_SPRUCE);      /* the gift clears trees: undo grows them back */
    tools_month_changed();
    tools_reset();
    def_footprint(B_ROW_HOUSES, ROT_0, &w, &h);
    lots0 = g_world.lot_count;
    h0 = world_hash();
    tools_select(TOOL_GIFT, 0);
    CHECK_EQ(click(22 + (w - 1) / 2, 48 + (h - 1) / 2, 0), TRES_COMMITTED);
    CHECK_EQ(complex_gift_pending(), 0);
    CHECK_EQ(g_world.lot_count, lots0 + 1);
    lot = world_lot_at(22, 48);
    CHECK(lot && world_lot(lot)->def == B_ROW_HOUSES);
    CHECK(tools_can_undo());
    CHECK(tools_undo());
    CHECK_EQ(complex_gift_pending(), 1);
    CHECK_EQ(complex_gift_def(0), B_ROW_HOUSES);
    CHECK_EQ(g_world.lot_count, lots0);
    CHECK_EQ(TER_TYPE(world_tile(22, 48)), TER_FOREST);
    test_log("gift undo: world hash before %08x after undo %08x", h0, world_hash());
    CHECK(tools_redo());
    CHECK_EQ(complex_gift_pending(), 0);
    lot = world_lot_at(22, 48);
    CHECK(lot && world_lot(lot)->def == B_ROW_HOUSES);
}

TEST(pl3_newsreel_founding_line)
{
    char lines[12][96];
    int n;
    p3_new(1955, 0);
    n = era_newsreel_lines(lines, 12);
    CHECK(n > 0);
    test_log("newsreel line 1 at the start: %s", lines[0]);
    CHECK(strstr(lines[0], "founded") != NULL);
    CHECK(strstr(lines[0], ": 0 in") == NULL);
}

TEST(pl3_heat_overlay_no_carpet)
{
    int x, y, carrier_zoned = 0, wet = 0, carrier_pipe = 0, lots = 0;
    if (!load_game("out/P1_DENSE.KAU", NULL)) { test_log("out/P1_DENSE.KAU missing (run T=balance_era first)"); CHECK(1); return; }
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = world_tile(x, y);
            u8 v = sim_overlay_value(OVL_HEAT, 0, x, y);
            if (v == OVV_NONE) continue;
            if (t->lot) { lots++; continue; }
            if (OVV_LEVEL(v) == NETC_CARRIER) { if (t->flags & TF_PIPE) carrier_pipe++; else carrier_zoned++; }
            if (OVV_LEVEL(v) == NETC_WET) wet++;
        }
    for (x = 1; x < g_world.lot_hi; x++)
        if (g_world.lots[x].def == B_HEAT_PLANT) test_log("heat plant at %d,%d", g_world.lots[x].x, g_world.lots[x].y);
    test_log("heat overlay: lot tiles %d, heated pipes %d, faint zoned land %d, solid zoned land %d", lots, carrier_pipe, wet, carrier_zoned);
    CHECK_EQ(carrier_zoned, 0);
    CHECK(lots > 0);
}
