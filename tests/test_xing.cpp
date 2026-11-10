/* test_xing.cpp - 1.2 X1: crossing the railway. Level crossings (gravel road / street / main road over straight track),
   motorway overpasses, power lines over the track, pipes under it; placement rules + reject reasons, costs, bulldoze
   (the rail stays), undo/redo, traffic links, zone access, power, water, save round trip, the user's 1.1 save, and the
   scripts/x1_xing.txt run on the seed 1 railway map (the same file the VM B check runs). Owner: X1.
   Storage: Tile.flags TF_RAIL (bit 7, reserved before 1.2) on a car-road tile = the rail runs under it (world.h). */
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
#include "text.h"

#define RY 40          /* the test railway runs along X at y = RY (rail axis 10) */
#define XING_USER_SAVE "../../../release/1.1rc1 save/AUTO1.KAU"

static void xnew(int all)
{
    SimSettings s;
    static int inited;
    int x;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = 41; s.all_unlocked = all;
    sim_new_city(&s);
    tools_reset();
    tools_set_pipes_with_roads(0);
    tools_set_show_pipes(0);
    g_budget.money = 200000;
    /* the initial railway as mapgen lays it: protected, edge links at both ends; a bend at x = 100 down to y = 60 */
    for (x = 0; x <= 100; x++) { world_set_road(x, RY, ROAD_RAIL); world_set_tile_flags(x, RY, TF_PROTECTED, 0); }
    for (x = RY + 1; x <= 60; x++) { world_set_road(100, x, ROAD_RAIL); world_set_tile_flags(100, x, TF_PROTECTED, 0); }
    world_set_tile_flags(0, RY, TF_EDGE_LINK, 0);
}

static int bfs_road(int x0, int y0, int x1, int y1)      /* car links (traffic_link_ok), like `check road` */
{
    static u8 seen[MAP_MAX_W * MAP_MAX_H];
    static u16 q[MAP_MAX_W * MAP_MAX_H];
    int qh = 0, qt = 0, d;
    memset(seen, 0, sizeof(seen));
    seen[TILE_IDX(x0, y0)] = 1; q[qt++] = (u16)TILE_IDX(x0, y0);
    while (qh < qt) {
        int i = q[qh++], x = i & (MAP_STRIDE - 1), y = i >> MAP_SHIFT;
        if (x == x1 && y == y1) return 1;
        for (d = 0; d < DIR_COUNT; d++) {
            int ni = TILE_IDX(x + g_dir_dx[d], y + g_dir_dy[d]);
            if (!traffic_link_ok(x, y, d) || seen[ni]) continue;
            seen[ni] = 1; q[qt++] = (u16)ni;
        }
    }
    return 0;
}

static int rail_ok(void)          /* the railway still runs end to end with straight masks except the bend */
{
    int x, y;
    for (x = 1; x < 100; x++) if (world_rail_mask(x, RY) != 10) return 0;
    if (world_rail_mask(100, RY) != ((1 << DIR_NW) | (1 << DIR_SW))) return 0;
    for (y = RY + 1; y < 60; y++) if (world_rail_mask(100, y) != 5) return 0;
    return TILE_HAS_RAIL(world_tile(0, RY));
}

TEST(xing_street_across_rail)
{
    Money m0;
    int r[ROAD_CLASS_COUNT], n;
    const Tile *t;
    xnew(0);
    CHECK(rail_ok());
    world_road_stats(r, NULL, NULL);
    CHECK_EQ(r[ROAD_RAIL], 121);
    m0 = g_budget.money;
    n = tools_apply_line(TOOL_ROAD, ROAD_STREET, 50, 30, 50, 50);      /* straight across (along Y) */
    CHECK_EQ(n, 21);
    CHECK_EQ(g_budget.money, m0 - 20 * g_roads[ROAD_STREET].cost - (g_roads[ROAD_STREET].cost + RAIL_XING_COST));
    t = world_tile(50, RY);
    CHECK_EQ(TILE_ROAD(t), ROAD_STREET);
    CHECK(t->flags & TF_RAIL);
    CHECK(t->flags & TF_PROTECTED);                 /* the rail keeps its protection */
    CHECK(TILE_IS_XING(t));
    CHECK(TILE_HAS_RAIL(t));
    CHECK_EQ(TILE_ROAD_MASK(t), 5);                 /* the street's own mask: NE + SW */
    CHECK_EQ(world_rail_axis(50, RY), 10);
    CHECK(rail_ok());                               /* rail neighbours still link through it */
    CHECK_EQ(TILE_ROAD_MASK(world_tile(49, RY)), 10);
    CHECK_EQ(TILE_ROAD_MASK(world_tile(50, RY - 1)), 5);
    CHECK_EQ(world_xing_count(), 1);
    world_road_stats(r, NULL, NULL);
    CHECK_EQ(r[ROAD_RAIL], 121);                    /* still counted as track (upkeep, complex_map_has_rail) */
    CHECK_EQ(r[ROAD_STREET], 21);
    CHECK(bfs_road(50, 30, 50, 50));
    CHECK(!traffic_link_ok(50, RY, DIR_SE));        /* no car link onto the rail */
    /* gravel and main road too; upgrade a crossing (only the difference is charged) */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_GRAVEL, 20, 35, 20, 45), 11);
    CHECK_EQ(TILE_ROAD(world_tile(20, RY)), ROAD_GRAVEL);
    m0 = g_budget.money;
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 20, 35, 20, 45), 11);
    CHECK_EQ(g_budget.money, m0 - 11 * (g_roads[ROAD_STREET].cost - g_roads[ROAD_GRAVEL].cost));
    CHECK_EQ(TILE_ROAD(world_tile(20, RY)), ROAD_STREET);
    CHECK(world_tile(20, RY)->flags & TF_RAIL);
    CHECK_EQ(world_xing_count(), 2);
    { FILE *f = fopen("out/xing_map.txt", "w"); if (f) { world_dump(f, 10, 30, 60, 50); world_dump_tile(f, 50, RY); fclose(f); } }
}

TEST(xing_rejects)
{
    const ToolPreview *p;
    xnew(0);
    /* along the track */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 30, RY, 35, RY), 0);
    CHECK_EQ(tools_last_reason(), REJ_XING_ANGLE);
    /* turning on the track (L path whose corner is the rail tile) */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 25, RY, 30, RY + 5), 5);  /* x first: corner at (30, RY) */
    CHECK(TILE_ROAD(world_tile(30, RY)) == ROAD_RAIL);
    CHECK(!(world_tile(30, RY)->flags & TF_RAIL));
    /* ending on the track */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 40, 30, 40, RY), 10);      /* the rest is built */
    p = tools_preview();
    CHECK_EQ(p->main_reason, REJ_XING_PLACE);
    CHECK(!(world_tile(40, RY)->flags & TF_RAIL));
    /* the bend */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 103, 30, 103, 60), 31);    /* east of the bend: no rail */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 100, 30, 100, 39), 10);
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 100, 39, 103, 39), 2);    /* x first along y = 39 */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 100, 38, 101, RY), 1);      /* y first: over the bend to (101,40) */
    CHECK_EQ(tools_preview()->main_reason, REJ_XING_CORNER);
    CHECK_EQ(TILE_ROAD(world_tile(100, RY)), ROAD_RAIL);
    /* the map-edge tile (TF_EDGE_LINK) */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 0, 35, 0, 45), 10);
    CHECK_EQ(tools_preview()->main_reason, REJ_XING_PLACE);
    CHECK_EQ(TILE_ROAD(world_tile(0, RY)), ROAD_RAIL);
    /* right next to another crossing along the track; one tile further is fine */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 60, 35, 60, 45), 11);
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 61, 35, 61, 45), 10);
    CHECK_EQ(tools_preview()->main_reason, REJ_XING_NEAR);
    CHECK_EQ(TILE_ROAD(world_tile(61, RY)), ROAD_RAIL);
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 62, 35, 62, 45), 11);
    CHECK(TILE_IS_XING(world_tile(62, RY)));
    /* a crossing whose far side is blocked: the crossing is rejected (no dead end on the track) */
    CHECK(tools_apply_build(B_FIRE_STATION, 70, RY + 1, 0));
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 70, 35, 70, 45), 9);      /* 35-39 + 42-45 (1x1 station at 70,41) */
    CHECK_EQ(TILE_ROAD(world_tile(70, RY)), ROAD_RAIL);
    /* reasons have texts */
    CHECK(strcmp(text_place_reason(REJ_XING_CORNER), "?") != 0);
    CHECK(strcmp(text_place_reason(REJ_XING_ANGLE), "?") != 0);
    CHECK(strcmp(text_place_reason(REJ_XING_NEAR), "?") != 0);
    CHECK(strcmp(text_place_reason(REJ_XING_PLACE), "?") != 0);
    CHECK(rail_ok());
}

TEST(xing_bulldoze_keeps_rail_and_undo)
{
    Money m0;
    u32 h0, h1;
    xnew(0);
    h0 = world_hash();
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 50, 30, 50, 50), 21);
    h1 = world_hash();
    m0 = g_budget.money;
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 50, RY, 50, RY), 1);
    CHECK_EQ(g_budget.money, m0 - g_misc.bulldoze_tile + (g_roads[ROAD_STREET].cost + RAIL_XING_COST) * g_misc.refund_pct / 100);
    CHECK_EQ(TILE_ROAD(world_tile(50, RY)), ROAD_RAIL);
    CHECK(!(world_tile(50, RY)->flags & TF_RAIL));
    CHECK(world_tile(50, RY)->flags & TF_PROTECTED);
    CHECK(rail_ok());
    CHECK(!bfs_road(50, 30, 50, 50));
    CHECK_EQ(world_xing_count(), 0);
    /* the plain (protected) rail can't be bulldozed */
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 50, RY, 50, RY), 0);
    CHECK_EQ(tools_last_reason(), REJ_PROTECTED_TILE);
    /* a rect over the crossing and the rail: the crossing's road goes, the rail stays */
    CHECK(tools_undo());
    CHECK(TILE_IS_XING(world_tile(50, RY)));
    CHECK_EQ(world_hash(), h1);
    CHECK(bfs_road(50, 30, 50, 50));
    CHECK(tools_undo());
    CHECK_EQ(TILE_ROAD(world_tile(50, RY)), ROAD_RAIL);
    CHECK_EQ(world_hash(), h0);
    CHECK(tools_redo());
    CHECK_EQ(world_hash(), h1);
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 45, RY - 1, 55, RY + 1), 2 + 1);  /* road tiles 50,39 50,41 + xing */
    CHECK_EQ(TILE_ROAD(world_tile(50, RY)), ROAD_RAIL);
    CHECK(rail_ok());
    /* unprotected track (not made by mapgen, e.g. a test town): same, the road goes */
    world_set_tile_flags(30, RY, 0, TF_PROTECTED);
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_GRAVEL, 30, 35, 30, 45), 11);
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 30, RY, 30, RY), 1);
    CHECK_EQ(TILE_ROAD(world_tile(30, RY)), ROAD_RAIL);
}

TEST(xing_motorway_overpass)
{
    Money m0;
    const Tile *t;
    xnew(1);
    g_budget.money = 200000;
    m0 = g_budget.money;
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_MOTORWAY, 80, 20, 80, 60), 41);
    t = world_tile(80, RY);
    CHECK_EQ(TILE_ROAD(t), ROAD_MOTORWAY);
    CHECK(t->flags & TF_RAIL);
    CHECK_EQ(TILE_ROAD_MASK(t), 5);
    CHECK(rail_ok());
    CHECK(bfs_road(80, 20, 80, 60));
    CHECK(!traffic_link_ok(80, RY, DIR_NW));
    CHECK(!traffic_link_ok(79, RY, DIR_SE));
    /* price: 40 motorway tiles + the overpass at the motorway's bridge price (creative mode may not charge: plan) */
    CHECK_EQ(tools_preview()->cost, 40 * g_roads[ROAD_MOTORWAY].cost + g_roads[ROAD_MOTORWAY].bridge_cost);
    UNUSED(m0);
    /* upgrade a level crossing to a motorway overpass; bulldoze: rail stays */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_GRAVEL, 60, 35, 60, 45), 11);
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_MOTORWAY, 60, 35, 60, 45), 11);
    CHECK_EQ(TILE_ROAD(world_tile(60, RY)), ROAD_MOTORWAY);
    CHECK(TILE_IS_XING(world_tile(60, RY)));
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 80, RY, 80, RY), 1);
    CHECK_EQ(TILE_ROAD(world_tile(80, RY)), ROAD_RAIL);
    CHECK(rail_ok());
}

TEST(xing_zone_access_and_traffic_across)
{
    int x, y, xid = 0;
    xnew(0);
    /* the town road: an edge link north, down across the rail to a neighbourhood south of it */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 50, 0, 50, 60), 61);
    world_set_tile_flags(50, 0, TF_EDGE_LINK, 0);
    CHECK(TILE_IS_XING(world_tile(50, RY)));
    /* zoning next to the crossing reaches the road (3 tiles) */
    CHECK(tools_apply_rect(TOOL_ZONE, ZONE_HOUSES, 47, RY + 1, 49, RY + 3) > 0);
    CHECK_EQ(TILE_ZONE(world_tile(47, RY + 2)), ZONE_HOUSES);
    CHECK(world_road_within(47, RY + 1, 3));
    /* a lot south of the rail: road access, and a road path to the edge link north of the rail */
    xid = tools_apply_build(B_FIRE_STATION, 51, 50, -1);
    CHECK(xid);
    CHECK(world_lot_access(xid, &x));
    CHECK(bfs_road(50, 51, 50, 0));
    /* traffic: the graph has the crossing on the run; route cars across. Run a few months with houses north and
       jobs south: the crossing tile carries flow */
    for (y = 2; y < RY - 2; y += 4) tools_apply_rect(TOOL_ZONE, ZONE_HOUSES, 47, y, 49, y + 2);
    for (y = RY + 6; y < 60; y += 4) tools_apply_rect(TOOL_ZONE, ZONE_INDUSTRY, 51, y, 53, y + 2);
    CHECK(tools_apply_build(B_COAL_PLANT, 40, 5, -1));
    for (x = 42; x < 49; x++) tools_apply_line(TOOL_POWER_LINE, 0, x, 6, x, 6);
    tools_apply_line(TOOL_POWER_LINE, 0, 46, 7, 46, 58);     /* down the west side: across the rail */
    for (x = 0; x < 24; x++) { sim_run_months(1); tools_month_changed(); { SimEvent e; while (sim_poll_event(&e)) {} } }
    test_log("after 24 months: pop %d jobs %d, crossing flow %d, rail tile 46,%d line %d powered %d",
             sim_stats()->population, sim_stats()->jobs, world_tile(50, RY)->aux, RY,
             (world_tile(46, RY)->road & TR_POWER_LINE) ? 1 : 0, (world_tile(46, RY)->zone & ZF_POWERED) ? 1 : 0);
    CHECK(world_tile(46, RY)->road & TR_POWER_LINE);
    CHECK(world_tile(46, RY + 5)->zone & ZF_POWERED);
    CHECK(sim_stats()->jobs > 0);
    CHECK(world_tile(50, RY)->aux > 0);
    { FILE *f = fopen("out/xing_town.txt", "w"); if (f) { world_dump(f, 35, 0, 60, 60); fclose(f); } }
}

TEST(xing_power_line_over_rail)
{
    int x;
    xnew(0);
    /* along the track or turning on it: rejected; straight across: a wire over the rail */
    CHECK_EQ(tools_apply_line(TOOL_POWER_LINE, 0, 30, RY, 33, RY), 0);
    CHECK_EQ(tools_last_reason(), REJ_XING_ANGLE);
    CHECK_EQ(tools_apply_line(TOOL_POWER_LINE, 0, 25, RY, 30, RY + 4), 0 + 4);   /* corner on the rail rejected */
    CHECK(!(world_tile(30, RY)->road & TR_POWER_LINE));
    CHECK_EQ(tools_apply_line(TOOL_POWER_LINE, 0, 35, 30, 35, 50), 21);
    CHECK(world_tile(35, RY)->road & TR_POWER_LINE);
    CHECK_EQ(TILE_ROAD(world_tile(35, RY)), ROAD_RAIL);
    CHECK(rail_ok());
    CHECK_EQ(tools_apply_line(TOOL_POWER_LINE, 0, 100, 34, 100, RY), 6);          /* ends on the bend: rejected */
    CHECK_EQ(tools_preview()->main_reason, REJ_XING_CORNER);
    /* bulldoze the protected rail tile removes the line only */
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 35, RY, 35, RY), 1);
    CHECK_EQ(TILE_ROAD(world_tile(35, RY)), ROAD_RAIL);
    CHECK(!(world_tile(35, RY)->road & TR_POWER_LINE));
    /* conduction: a plant north, a line across the rail to zoned land south (line over rail), and a second line that
       stops at the rail: power jumps ONE rail tile like one plain road tile (net.h) */
    CHECK_EQ(tools_apply_line(TOOL_POWER_LINE, 0, 35, 30, 35, 50), 1);
    CHECK(tools_apply_build(B_COAL_PLANT, 34, 28, 0));
    net_solve_all();
    CHECK(world_tile(35, 50)->zone & ZF_POWERED);
    CHECK(world_tile(35, RY)->zone & ZF_POWERED);
    for (x = 36; x <= 40; x++) tools_apply_line(TOOL_POWER_LINE, 0, x, 30, x, 30);
    CHECK_EQ(tools_apply_line(TOOL_POWER_LINE, 0, 40, 30, 40, RY - 1), 9);
    CHECK_EQ(tools_apply_line(TOOL_POWER_LINE, 0, 40, RY + 1, 40, RY + 3), 3);
    net_solve_all();
    CHECK(world_tile(40, RY + 3)->zone & ZF_POWERED);          /* across one rail tile, no wire on it */
    CHECK(!(world_tile(40, RY)->road & TR_POWER_LINE));
    /* a line over a level crossing (road + rail + wire) */
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 60, 35, 60, 45), 11);
    CHECK_EQ(tools_apply_line(TOOL_POWER_LINE, 0, 60, 36, 60, 44), 9);
    CHECK(world_tile(60, RY)->road & TR_POWER_LINE);
    CHECK(TILE_IS_XING(world_tile(60, RY)));
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 60, RY, 60, RY), 1);
    CHECK_EQ(TILE_ROAD(world_tile(60, RY)), ROAD_RAIL);
    CHECK(rail_ok());
}

TEST(xing_pipes_under_rail)
{
    xnew(0);
    /* a well north of the rail, a pipe across the track and a crossing, zoned land south */
    CHECK(tools_apply_build(B_WELL, 30, 30, 0));
    CHECK_EQ(tools_apply_line(TOOL_PIPE, 0, 30, 31, 30, 50), 20);
    CHECK(world_tile(30, RY)->flags & TF_PIPE);
    CHECK_EQ(TILE_ROAD(world_tile(30, RY)), ROAD_RAIL);
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 30, 36, 30, 44), 9);         /* a crossing over the pipe */
    CHECK(TILE_IS_XING(world_tile(30, RY)));
    CHECK(world_tile(30, RY)->flags & TF_PIPE);
    CHECK(tools_apply_rect(TOOL_ZONE, ZONE_HOUSES, 31, 45, 32, 47) > 0);
    net_solve_all();
    CHECK(world_tile(30, 50)->zone & ZF_WATER_NET);
    CHECK(world_tile(31, 46)->zone & ZF_WATER_NET);
    /* bulldozing the crossing (pipes hidden) keeps the pipe and the rail */
    CHECK_EQ(tools_apply_rect(TOOL_BULLDOZE, 0, 30, RY, 30, RY), 1);
    CHECK(world_tile(30, RY)->flags & TF_PIPE);
    CHECK_EQ(TILE_ROAD(world_tile(30, RY)), ROAD_RAIL);
}

TEST(xing_save_roundtrip)
{
    SaveFile *sf;
    u32 h, sh;
    xnew(1);
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_STREET, 50, 30, 50, 50), 21);
    CHECK_EQ(tools_apply_line(TOOL_ROAD, ROAD_MOTORWAY, 80, 30, 80, 50), 21);
    CHECK_EQ(tools_apply_line(TOOL_POWER_LINE, 0, 35, 30, 35, 50), 21);
    h = world_hash(); sh = sim_hash();
    sf = save_mem_open();
    CHECK(save_game_to(sf, NULL));
    world_new(128, 128);
    CHECK_EQ(world_xing_count(), 0);
    save_mem_reopen(sf);
    CHECK(load_game_from(sf, NULL));
    save_mem_close(sf);
    CHECK_EQ(world_hash(), h);
    CHECK_EQ(sim_hash(), sh);
    CHECK_EQ(world_xing_count(), 2);
    CHECK(TILE_IS_XING(world_tile(50, RY)));
    CHECK_EQ(TILE_ROAD(world_tile(80, RY)), ROAD_MOTORWAY);
}

/* the user's 1.1 save (Koivukoski 1996) loads unchanged: no TF_RAIL bits, rail masks as saved, save round trip identical */
TEST(xing_old_save_unchanged)
{
    SaveMeta meta;
    SaveFile *sf;
    FILE *f = fopen(XING_USER_SAVE, "rb");
    int x, y, nrail = 0;
    u32 h, sh;
    if (!f) { test_log(XING_USER_SAVE " missing: skipped"); return; }
    fclose(f);
    world_init(); sim_init(); tools_init(); traffic_init();
    CHECK(load_game(XING_USER_SAVE, &meta));
    CHECK_EQ(world_xing_count(), 0);
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = WT(x, y);
            CHECK(!(t->flags & TF_RAIL));
            if (TILE_ROAD(t) == ROAD_RAIL) nrail++;
        }
    h = world_hash(); sh = sim_hash();
    sf = save_mem_open(); save_game_to(sf, NULL);
    save_mem_reopen(sf);
    CHECK(load_game_from(sf, NULL));
    save_mem_close(sf);
    CHECK_EQ(world_hash(), h);
    CHECK_EQ(sim_hash(), sh);
    test_log("1.1rc1 AUTO1: %d rail tiles, world %08x, sim %08x, pop %d", nrail, h, sh, sim_stats()->population);
    tools_reset();
}

/* scripts/x1_xing.txt on the seed 1 Lakeland railway map = VM B: lahio.exe -seed 1 -rail -script scripts\x1_xing.txt */
TEST(xing_script_seed1)
{
    MapGenParams p;
    SimSettings s;
    FILE *out;
    int errors;
    world_init(); sim_init(); tools_init(); traffic_init();
    mapgen_defaults(&p); p.seed = 1; p.railway = 1;
    mapgen_generate(&p);
    sim_default_settings(&s); s.seed = 1;
    sim_new_city(&s);
    tools_reset();
    remove("out/x1_xing_dump.txt");
    out = fopen("out/x1_xing_out.txt", "w");
    tools_script_set_dir("out/");
    errors = tools_script_run_file("../scripts/x1_xing.txt", out);
    tools_script_set_dir("");
    if (out) fclose(out);
    test_log("x1_xing: %d errors, crossings %d, pop %d, money %d, world %08x", errors, world_xing_count(),
             sim_stats()->population, g_budget.money, world_hash());
    CHECK_EQ(errors, 0);
}
