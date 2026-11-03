/* test_f3.cpp - F3 (Milestone 2 fix round): 1x1 industrial yards grow only where no 2x2 industrial lot fits
   (DESIGN 14.2), the upkeep index shown by the UI, the water overlay colours (watered lots blue). Owner: F3.
   Writes tests/out/f3_yards.txt (the yards of the town1 session with their neighbourhood). */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "budget.h"
#include "tools.h"
#include "mapgen.h"

/* 1 = a 2x2 rect containing (x, y) is all industry zone with no lot but the yard at (x, y) itself: a 2x2 lot
   would have fitted there when the yard grew (the rule forbids the yard then) */
static int yard_violates(int x, int y, int yard)
{
    int i, j, a, b, ok;
    for (j = y - 1; j <= y; j++)
        for (i = x - 1; i <= x; i++) {
            ok = 1;
            for (b = j; b <= j + 1 && ok; b++)
                for (a = i; a <= i + 1 && ok; a++) {
                    const Tile *t;
                    if (!world_in(a, b)) { ok = 0; break; }
                    t = WT(a, b);
                    if (TILE_ZONE(t) != ZONE_INDUSTRY || TILE_ROAD(t) || (t->lot && t->lot != yard)) ok = 0;
                    else if (!t->lot && !world_is_buildable(a, b)) ok = 0;
                }
            if (ok) return 1;
        }
    return 0;
}

TEST(f3_yards_fill_industrial_gaps)
{
    MapGenParams mp;
    SimSettings s;
    FILE *f;
    int i, yards = 0, bad = 0, ind_lots = 0, errors, ind_tiles = 0, x, y;
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); inited = 1; }
    mapgen_defaults(&mp);
    mp.seed = 7;
    CHECK(mapgen_generate(&mp));
    sim_default_settings(&s);
    s.seed = 7;
    sim_new_city(&s);
    tools_reset();
    tools_script_set_dir("out/");
    errors = tools_script_run_file("../scripts/town1.txt", NULL);
    sim_statlog_close();
    tools_script_set_dir("");
    f = fopen("out/f3_yards.txt", "w");
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = world_lot(i);
        const BuildingDef *d;
        int x0, y0, x1, y1;
        if (!l) continue;
        d = def_building(l->def);
        if (d->cat != BCAT_ZONE || d->zone != ZONE_INDUSTRY) continue;
        ind_lots++;
        if (l->def != B_IND_YARD) continue;
        yards++;
        world_lot_rect(i, &x0, &y0, &x1, &y1);
        if (yard_violates(x0, y0, i)) bad++;
        if (f) fprintf(f, "yard %d at %d,%d variant %d rot %d status %d%s\n", i, x0, y0, l->variant, l->rot, l->status,
                       yard_violates(x0, y0, i) ? "  VIOLATES (a 2x2 fits)" : "");
    }
    if (f) fclose(f);
    for (y = 0; y < g_world.h; y++) for (x = 0; x < g_world.w; x++) ind_tiles += TILE_ZONE(WT(x, y)) == ZONE_INDUSTRY;
    test_log("town1 (5 years): %d industrial lots, %d of them yards (%d%% of lots, %d of %d industrial tiles = %d%%), "
             "%d yards where a 2x2 would fit; script errors %d", ind_lots, yards, ind_lots ? yards * 100 / ind_lots : 0,
             yards, ind_tiles, ind_tiles ? yards * 100 / ind_tiles : 0, bad, errors);
    CHECK(yards * 100 <= ind_tiles * 15);    /* the 12% estate cap (YARD_CAP_PCT, sim.cpp, per 9x9 window) holds town-wide too */
    CHECK_EQ(errors, 0);
    CHECK(def_building(B_IND_YARD)->variants == 6 && def_building(B_IND_YARD)->w == 1);
    CHECK_EQ(bad, 0);
}

TEST(f3_yard_only_where_no_2x2_fits)
{
    /* a small layout: a street with a 1-wide industrial strip (only yards fit) and a 2x2 block (no yard may
       grow inside it while it is empty); I demand from a village of cottages (tier 1 industry needs a road) */
    static int inited;
    SimSettings s;
    int strip_yards = 0, block_yards = 0, other = 0, i;
    if (!inited) { world_init(); sim_init(); tools_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = 11;
    sim_new_city(&s);
    tools_reset();
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 40, 100, 40);
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 50, 100, 50);
    tools_apply_line(TOOL_ROAD, ROAD_STREET, 10, 40, 10, 50);
    tools_apply_rect(TOOL_ZONE, ZONE_HOUSES, 12, 41, 100, 44);
    tools_apply_rect(TOOL_ZONE, ZONE_INDUSTRY, 20, 49, 60, 49);      /* 1-wide strip along the y 50 street */
    tools_apply_rect(TOOL_ZONE, ZONE_INDUSTRY, 70, 48, 71, 49);      /* one 2x2 block */
    sim_set_speed(SPEED_3);
    sim_run_months(30);
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = world_lot(i);
        int x0, y0, x1, y1;
        if (!l || def_building(l->def)->zone != ZONE_INDUSTRY || def_building(l->def)->cat != BCAT_ZONE) continue;
        world_lot_rect(i, &x0, &y0, &x1, &y1);
        if (l->def == B_IND_YARD && y0 == 49 && x0 >= 20 && x0 <= 60) strip_yards++;
        else if (l->def == B_IND_YARD && x0 >= 70 && x0 <= 71) block_yards++;
        else other++;
    }
    test_log("pop %d: strip yards %d (of 41 tiles), yards in the 2x2 block %d, other industry lots %d",
             sim_stats()->population, strip_yards, block_yards, other);
    CHECK(strip_yards > 0);
    CHECK_EQ(block_yards, 0);
}

TEST(f3_upkeep_index)
{
    static const int years[5] = { 1955, 1959, 1960, 1970, 1985 };
    static const int want[5] = { 100, 100, 100, 125, 130 };   /* P1: cap 130% (was 150%) */
    int k;
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); inited = 1; }
    world_new(64, 64);
    for (k = 0; k < 5; k++) {
        sim_default_settings(&s);
        s.start_year = years[k];
        sim_new_city(&s);
        CHECK_EQ(budget_upkeep_index(), want[k]);
    }
}

TEST(f3_water_overlay_watered_lots_blue)
{
    static int inited;
    SimSettings s;
    int well;
    if (!inited) { world_init(); sim_init(); tools_init(); inited = 1; }
    world_new(64, 64);
    sim_default_settings(&s);
    sim_new_city(&s);
    well = world_lot_place(B_WELL, 10, 10, 0, 0, LOT_ACTIVE);
    world_set_pipe(11, 10, 1); world_set_pipe(12, 10, 1);
    world_lot_place(B_HOUSE_T2, 13, 10, 0, 0, LOT_ACTIVE);
    net_solve_all();
    CHECK(well);
    CHECK_EQ(sim_overlay_value(OVL_WATER, 0, 13, 10), OVV(OVR_NET, NETC_CARRIER));   /* watered house: blue */
    CHECK_EQ(sim_overlay_value(OVL_WATER, 0, 10, 10), OVV(OVR_NET, NETC_OK));        /* the source stays green */
    CHECK_EQ(sim_overlay_value(OVL_POWER, 0, 13, 10) == OVV(OVR_NET, NETC_CARRIER), 0); /* power keeps its mapping */
}
