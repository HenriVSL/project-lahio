/* test_industry.cpp - M3: industry kinds, wood/milk supply, forestry rotation + regrowth, farm tiers,
   exports split, query strings, determinism and save round-trip. Owner: M3. Debug from text: dumps in
   tests/out/industry_*.txt. Small worlds built with world_* setters (like test_sim.cpp). */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "industry.h"
#include "save.h"
#include "text.h"
#include "tools.h"
#include "traffic.h"
#include "mapgen.h"

/* ------------------------------------------------------------------ helpers */
static void it_new(u32 seed, int year)
{
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = seed;
    s.start_year = year;
    sim_new_city(&s);
}

static void it_road(int x0, int y0, int x1, int y1, int cls)
{
    int x, y;
    world_batch_begin();
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!TILE_ROAD(world_tile(x, y)) && !world_tile(x, y)->lot) world_set_road(x, y, cls);
    world_batch_end();
}

static void it_terrain(int x0, int y0, int x1, int y1, int ter, int var, int aux)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) { world_set_terrain(x, y, ter, var); WT(x, y)->aux = (u8)aux; }
}

static int it_zone(int x0, int y0, int x1, int y1, int z)
{
    int x, y, n = 0;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (world_is_buildable(x, y)) { world_set_zone(x, y, z); n++; }
    return n;
}

static void it_line(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++) if (!world_tile(x, y)->lot) world_set_power_line(x, y, 1);
}

static int it_place(int def, int x, int y, int rot, int status)
{
    int id = world_lot_place(def, x, y, rot, 0, status);
    if (!id) test_log("place %s at %d,%d failed: %d", def_building(def)->key, x, y, world_lot_check(def, x, y, rot));
    return id;
}

/* run steps until the next phase-15 (industry_cycle) has happened */
static void it_cycle(void)
{
    int i;
    for (i = 0; i < SIM_STEPS_PER_CYCLE; i++) sim_step();
}

static void it_picks(int x, int y, int n, int *cnt)
{
    u8 defs[4];
    int i;
    memset(cnt, 0, sizeof(int) * IK_COUNT);
    for (i = 0; i < n; i++) if (industry_candidates(x, y, defs, 4) > 0) cnt[industry_last_pick()]++;
}

static void it_dump(const char *name)
{
    char path[96];
    FILE *f;
    str_printf(path, sizeof(path), "out/%s", name);
    f = fopen(path, "w");
    if (!f) return;
    industry_dump(f);
    sim_dump_report(f);
    world_dump(f, 0, 0, g_world.w - 1, g_world.h - 1);
    fclose(f);
}

/* ------------------------------------------------------------------ defs */
TEST(industry_defs_and_kinds)
{
    int b, n = 0;
    CHECK_EQ(def_selfcheck(), 0);
    CHECK_EQ(industry_def_kind(B_IND_WOOD_T1_3X3), IK_WOOD);
    CHECK_EQ(industry_def_kind(B_IND_METAL_T2_2X2), IK_METAL);
    CHECK_EQ(industry_def_kind(B_IND_TEXTILE_T2_3X3), IK_TEXTILE);
    CHECK_EQ(industry_def_kind(B_IND_DAIRY_T1_2X2), IK_FOOD);
    CHECK_EQ(industry_def_kind(B_IND_ELEC_T2_2X2), IK_ELEC);
    CHECK_EQ(industry_def_kind(B_HOUSE_T1), IK_NONE);
    CHECK_EQ(industry_def_input(B_IND_WOOD_T1_3X3, NULL), 9);
    CHECK_EQ(industry_def_input(B_IND_DAIRY_T1_2X2, NULL), 15);
    for (b = 1; b < B_COUNT; b++) {
        const BuildingDef *d = def_building(b);
        if (d->cat == BCAT_ZONE && d->zone == ZONE_INDUSTRY && d->variants && industry_def_kind(b) == IK_NONE && strcmp(d->key, "ind_yard"))
            { test_log("industrial def without a kind: %s", d->key); n++; }
    }
    CHECK_EQ(n, 0);
    CHECK(!strcmp(def_building(B_FARM_T3)->key, "farm_t3"));
    CHECK_EQ(def_building(B_FARM_T3)->era_year, 1962);
    CHECK(def_building(B_FARM_T3)->capacity < def_building(B_FARM_T2)->capacity);  /* mechanisation sheds hands */
    CHECK(!ZONE_CONDUCTS(ZONE_FORESTRY) && !ZONE_CONDUCTS(ZONE_FARM) && ZONE_CONDUCTS(ZONE_INDUSTRY));
    CHECK(!strcmp(text_zone_name(ZONE_FORESTRY), "Forestry"));
}

/* ------------------------------------------------------------------ kind choice by site + must rules */
TEST(industry_kind_by_site)
{
    int a[IK_COUNT], b[IK_COUNT], c[IK_COUNT], i;
    it_new(11, 1955);
    it_road(0, 64, 127, 64, ROAD_STREET);
    /* site A: lakeside, forest within reach */
    it_terrain(4, 66, 13, 80, TER_WATER, 0, 0);
    it_terrain(14, 74, 26, 86, TER_FOREST, FOREST_SPRUCE, 12);
    it_zone(14, 65, 22, 72, ZONE_INDUSTRY);
    /* site B: inland by the railway */
    it_road(90, 74, 120, 74, ROAD_RAIL);
    it_zone(100, 65, 108, 72, ZONE_INDUSTRY);
    /* site C: by a river */
    it_terrain(56, 66, 58, 90, TER_RIVER, 0, 0);
    it_zone(59, 65, 66, 72, ZONE_INDUSTRY);
    it_cycle();
    test_log("kind demand: wood %d metal %d textile %d dairy %d elec %d (I target %d)", industry_kinds()->demand[IK_WOOD],
             industry_kinds()->demand[IK_METAL], industry_kinds()->demand[IK_TEXTILE], industry_kinds()->demand[IK_FOOD],
             industry_kinds()->demand[IK_ELEC], sim_demand(DEM_I)->target);
    {   /* the kind demands sum to the I target */
        s32 sum = 0;
        for (i = 1; i < IK_COUNT; i++) sum += industry_kinds()->demand[i];
        CHECK_EQ(sum, sim_demand(DEM_I)->target);
    }
    it_picks(15, 66, 400, a);
    it_picks(101, 66, 400, b);
    it_picks(60, 66, 400, c);
    test_log("lake+forest: wood %d metal %d textile %d dairy %d | rail: wood %d metal %d textile %d dairy %d | river: wood %d metal %d textile %d",
             a[IK_WOOD], a[IK_METAL], a[IK_TEXTILE], a[IK_FOOD], b[IK_WOOD], b[IK_METAL], b[IK_TEXTILE], b[IK_FOOD],
             c[IK_WOOD], c[IK_METAL], c[IK_TEXTILE]);
    CHECK(a[IK_WOOD] > b[IK_WOOD] * 2);          /* log pond + forest: sawmills */
    CHECK(b[IK_METAL] > a[IK_METAL]);            /* railway: engineering */
    CHECK(c[IK_TEXTILE] > b[IK_TEXTILE]);        /* water power: textiles */
    CHECK_EQ(a[IK_ELEC] + b[IK_ELEC] + c[IK_ELEC], 0);   /* 1955: no electronics */
    /* must rule: wood short -> no sawmill anywhere, engineering takes the demand */
    for (i = 0; i < 4; i++) it_place(B_IND_WOOD_T1_3X3, 30 + 4 * i, 65, ROT_2, LOT_ACTIVE);
    it_cycle();
    it_zone(14, 88, 22, 95, ZONE_INDUSTRY);                /* a fresh lakeside + forest site (A is built up now) */
    it_terrain(4, 88, 13, 95, TER_WATER, 0, 0);
    test_log("two sawmills: wood need %d supply %d, wood demand %d (short %d)", industry_wood()->need, industry_wood()->supply,
             industry_kinds()->demand[IK_WOOD], industry_kinds()->short_jobs[IK_WOOD]);
    CHECK(industry_wood()->supply * 100 < IND_INPUT_GROW_PCT * (industry_wood()->need + 4));
    it_picks(15, 89, 400, a);
    CHECK_EQ(a[IK_WOOD], 0);
    CHECK(a[IK_METAL] > 0);
    /* milk: one co-op dairy runs on the regional milk; a second one does not fit */
    it_place(B_IND_DAIRY_T1_2X2, 48, 65, ROT_2, LOT_ACTIVE);
    it_cycle();
    test_log("milk need %d supply %d", industry_milk()->need, industry_milk()->supply);
    it_zone(14, 96, 22, 103, ZONE_INDUSTRY);
    it_picks(15, 97, 400, a);
    test_log("one dairy: milk need %d supply %d, dairy picks %d", industry_milk()->need, industry_milk()->supply, a[IK_FOOD]);
    CHECK_EQ(a[IK_FOOD], 0);
    /* a full industrial zone never stalls: no free 2x2 -> no candidates, else always at least the fallback */
    {
        u8 d[4];
        int n = industry_candidates(15, 98, d, 4);
        CHECK(n >= 1);
        CHECK(industry_def_rank(d[0]) >= industry_def_rank(d[n - 1]));
    }
    it_dump("industry_site.txt");
}

/* ------------------------------------------------------------------ forestry: camps, rotation, regrowth */
TEST(industry_forestry_rotation_regrowth)
{
    int i, m, cut_x = -1, cut_y = -1, cut_month = -1, back = -1, stages = 0, last_stage = -1, camps0;
    it_new(12, 1955);
    it_road(0, 64, 127, 64, ROAD_STREET);
    it_terrain(30, 65, 49, 80, TER_FOREST, FOREST_MIXED, 12);
    CHECK(it_zone(30, 65, 49, 80, ZONE_FORESTRY) == 20 * 16);
    /* wood users: eight sawmills along the road (need 72, regional 10) */
    for (i = 0; i < 8; i++) it_place(B_IND_WOOD_T1_3X3, 60 + 4 * i, 65, ROT_2, LOT_ACTIVE);
    sim_run_months(6);
    camps0 = industry_wood()->camps;
    test_log("after 6 months: camps %d (forestry tiles %d), harvest tiles %d, capacity %d wu, cut %d tiles, pct %d",
             camps0, industry_wood()->forestry_tiles, industry_wood()->harvest_tiles, industry_wood()->forestry_cap,
             industry_wood()->cut_tiles_total, industry_wood()->pct);
    CHECK(camps0 >= 1);                                    /* camps grow by the road on their own */
    CHECK(industry_wood()->forestry_cap > 0);
    CHECK(industry_wood()->supply == industry_wood()->regional + industry_wood()->forestry_cap);
    /* follow the first cut tile back to maturity */
    for (m = 0; m < 80 && back < 0; m++) {
        sim_run_months(1);
        if (cut_x < 0) {
            int x, y;
            for (y = 65; y <= 80 && cut_x < 0; y++)
                for (x = 30; x <= 49 && cut_x < 0; x++)
                    if (TER_TYPE(WT(x, y)) == TER_FOREST && FOREST_REGROW(WT(x, y)) == 1) { cut_x = x; cut_y = y; cut_month = m; }
            continue;
        }
        {
            int g = FOREST_REGROW(WT(cut_x, cut_y)), st = industry_forest_stage(g);
            if (st != last_stage) { stages++; last_stage = st; }
            if (g == 0 && m > cut_month) back = m - cut_month;
        }
    }
    test_log("cut tile %d,%d at month %d, mature again after %d months (%d stage changes); wood: delivered %d of %d (%d%%),"
             " cut %d wu/month, total %d tiles", cut_x, cut_y, cut_month, back, stages, industry_wood()->delivered,
             industry_wood()->need, industry_wood()->pct, industry_wood()->cut_wu, industry_wood()->cut_tiles_total);
    CHECK(cut_x >= 0);
    CHECK(back >= 44 && back <= 48);                       /* 12 steps x 4 months */
    CHECK(stages >= 4);                                    /* cut -> seedlings -> young -> mature */
    CHECK(industry_wood()->cut_tiles_total > 10);
    CHECK(industry_wood()->delivered <= industry_wood()->need);
    CHECK(industry_wood()->regrowing > 0 && industry_wood()->regrowing <= industry_wood()->forest_tiles);
    {   /* the sawmills are short of wood (72 needed): the ones left see REQ_WOOD */
        int lot = 0, unmet = 0;
        for (i = 1; i < g_world.lot_hi; i++) if (g_world.lots[i].def == B_IND_WOOD_T1_3X3 || g_world.lots[i].def == B_IND_WOOD_T2_3X3) { lot = i; break; }
        if (lot && industry_wood()->pct < IND_INPUT_KEEP_PCT) unmet = (industry_lot_unmet(lot, 0) >> REQ_WOOD) & 1;
        test_log("sawmills left %d, wood pct %d, REQ_WOOD unmet %d", industry_kinds()->lots[IK_WOOD], industry_wood()->pct, unmet);
        CHECK(industry_wood()->pct >= IND_INPUT_KEEP_PCT || unmet || industry_kinds()->lots[IK_WOOD] == 0);
    }
    {   /* query lines for a forest tile and a camp */
        char b[128];
        int camp = 0;
        CHECK(industry_tile_line(cut_x, cut_y, 0, b, sizeof(b)) > 0);
        test_log("tile: %s", b);
        for (i = 1; i < g_world.lot_hi; i++) if (g_world.lots[i].def == B_LOGGING_CAMP) { camp = i; break; }
        CHECK(camp);
        CHECK(industry_lot_line(camp, 0, b, sizeof(b)) > 0 && strstr(b, "Forest:"));
        test_log("camp: %s", b);
    }
    {   /* forestry land conducts nothing */
        CHECK(!(WT(40, 72)->zone & ZF_POWERED));
    }
    it_dump("industry_forestry.txt");
}

/* ------------------------------------------------------------------ farms: growth, claims, tiers, shedding */
TEST(industry_farm_tiers_and_shedding)
{
    int i, t3 = 0, bad = 0, n2 = 0, fields = 0, farm = 0;
    s32 milk0;
    it_new(13, 1960);
    it_road(0, 64, 127, 64, ROAD_MAIN);                    /* main road: T3 (mechanised) needs it */
    for (i = 0; i < 128; i++) world_set_tile_flags(i, 64, TF_PROTECTED, 0);
    world_set_tile_flags(0, 64, TF_EDGE_LINK, 0);
    world_set_tile_flags(127, 64, TF_EDGE_LINK, 0);
    it_zone(30, 65, 70, 72, ZONE_FARM);
    it_place(B_COAL_PLANT, 74, 62, ROT_0, LOT_ACTIVE);
    it_line(30, 63, 73, 63);
    it_place(B_SCHOOL, 26, 62, ROT_0, LOT_ACTIVE);
    it_place(B_IND_DAIRY_T1_2X2, 90, 65, ROT_2, LOT_ACTIVE);   /* a dairy: milk demand for the farms */
    milk0 = industry_milk()->supply;
    sim_run_months(30);
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d = def_building(l->def);
        if (l->def == B_NONE || d->zone != ZONE_FARM) continue;
        farm++;
        fields += l->spare & 0xFF;
        if (d->tier == 2) n2++;
        if (d->tier == 3) t3++;
        if (l->occupants > d->capacity) bad++;
    }
    test_log("1962-07: farms %d (T2 %d, T3 %d), fields %d, milk supply %d (start %d), farm demand %d, residents %d, homeless %d",
             farm, n2, t3, fields, industry_milk()->supply, milk0, industry_milk()->farm_demand, industry_milk()->farm_residents,
             sim_stats()->homeless);
    CHECK(farm >= 2);
    CHECK(fields >= farm * 4);
    CHECK(industry_milk()->farms > 0 && industry_milk()->supply > milk0);
    {   /* claimed grass became field */
        int x, y, nf = 0;
        for (y = 65; y <= 72; y++) for (x = 30; x <= 70; x++) if (TER_TYPE(WT(x, y)) == TER_FIELD) nf++;
        CHECK(nf >= fields);
    }
    CHECK(t3 >= 1);                                        /* 1962+: power + main road -> mechanised */
    CHECK_EQ(bad, 0);                                      /* the mechanised tier sheds residents */
    {
        char b[128];
        for (i = 1; i < g_world.lot_hi; i++) if (def_building(g_world.lots[i].def)->zone == ZONE_FARM) break;
        CHECK(industry_lot_line(i, 0, b, sizeof(b)) > 0 && strstr(b, "Fields:"));
        test_log("farm: %s", b);
    }
    it_dump("industry_farms.txt");
}

/* the T2 -> T3 upgrade moves the extra residents to the homeless pool (structural change) */
TEST(industry_farm_mechanisation_sheds)
{
    int id, i;
    s32 occ_before = 0;
    it_new(14, 1962);
    it_road(0, 64, 127, 64, ROAD_MAIN);
    it_zone(40, 65, 44, 67, ZONE_FARM);
    it_place(B_COAL_PLANT, 74, 62, ROT_0, LOT_ACTIVE);
    it_line(40, 63, 73, 63);
    it_place(B_SCHOOL, 36, 62, ROT_0, LOT_ACTIVE);         /* T2 keeps needing its school */
    id = it_place(B_FARM_T2, 42, 65, ROT_2, LOT_ACTIVE);
    CHECK(id);
    g_world.lots[id].occupants = 6;
    for (i = 0; i < 12 * SIM_STEPS_PER_MONTH && g_world.lots[id].def == B_FARM_T2; i++) { occ_before = g_world.lots[id].occupants; sim_step(); }
    test_log("farm %d: def %s, occupants %d (before %d), homeless %d, step %d", id, def_building(g_world.lots[id].def)->key,
             g_world.lots[id].occupants, occ_before, sim_stats()->homeless, i);
    CHECK_EQ(g_world.lots[id].def, B_FARM_T3);
    CHECK(g_world.lots[id].occupants <= def_building(B_FARM_T3)->capacity);
}

/* ------------------------------------------------------------------ exports split, query strings */
TEST(industry_exports_split_and_query)
{
    int ids[IK_COUNT], k, year;
    char b[160];
    ReqItem it;
    it_new(15, 1966);
    it_road(0, 64, 127, 64, ROAD_STREET);
    it_terrain(4, 66, 13, 80, TER_WATER, 0, 0);
    ids[IK_WOOD] = it_place(B_IND_WOOD_T1_3X3, 14, 65, ROT_2, LOT_ACTIVE);
    ids[IK_METAL] = it_place(B_IND_METAL_T2_2X2, 30, 65, ROT_2, LOT_ACTIVE);
    ids[IK_TEXTILE] = it_place(B_IND_TEXTILE_T2_3X3, 40, 65, ROT_2, LOT_ACTIVE);
    ids[IK_FOOD] = it_place(B_IND_DAIRY_T1_2X2, 50, 65, ROT_2, LOT_ACTIVE);
    ids[IK_ELEC] = it_place(B_IND_ELEC_T2_2X2, 60, 65, ROT_2, LOT_ACTIVE);
    /* power for the tier-2 works (the line jumps the street), and workers: cottages along two streets */
    it_place(B_COAL_PLANT, 70, 62, ROT_0, LOT_ACTIVE);
    it_line(10, 63, 69, 63);
    for (k = 0; k < 4; k++) it_road(10, 40 + 6 * k, 69, 40 + 6 * k, ROAD_STREET);
    it_road(9, 40, 9, 63, ROAD_STREET);                    /* the workers' streets reach the works */
    for (k = 0; k < 4 * 60; k++) {
        int x = 10 + (k % 60), y = 41 + 6 * (k / 60), id;
        world_set_zone(x, y, ZONE_HOUSES);
        id = world_lot_place(B_HOUSE_T1, x, y, ROT_2, 0, LOT_ACTIVE);
        if (id) g_world.lots[id].occupants = 3;
    }
    sim_run_months(2);
    year = sim_date()->year;
    for (k = 1; k < IK_COUNT; k++) {
        const Lot *l = world_lot(ids[k]);
        if (l) test_log("lot %d %s status %d occ %d problems %08x", ids[k], def_building(l->def)->key, l->status, l->occupants, l->problems);
    }
    for (k = 1; k < IK_COUNT; k++) {
        const IndKinds *K = industry_kinds();
        test_log("%-12s jobs %4d export %4d: West %4d East %4d (%d%% West)", industry_kind_plural(k), K->jobs[k], K->export_vol[k],
                 K->export_west[k], K->export_east[k], K->export_vol[k] ? K->export_west[k] * 100 / K->export_vol[k] : -1);
        CHECK(K->jobs[k] > 0);
        CHECK(K->export_west[k] + K->export_east[k] == K->export_vol[k]);
        CHECK(IABS(K->export_west[k] * 100 - K->export_vol[k] * industry_west_pct(k, year)) <= 100);
    }
    CHECK(industry_kinds()->export_west[IK_WOOD] > industry_kinds()->export_east[IK_WOOD]);       /* wood earns the West */
    CHECK(industry_kinds()->export_east[IK_METAL] > industry_kinds()->export_west[IK_METAL]);     /* metal fills clearing */
    CHECK(industry_kinds()->export_east[IK_TEXTILE] > industry_kinds()->export_west[IK_TEXTILE]);
    /* query explanations */
    CHECK(industry_lot_line(ids[IK_WOOD], 0, b, sizeof(b)) > 0); test_log("%s", b);
    CHECK(strstr(b, "Sawmill:") && strstr(b, "log pond"));
    CHECK(industry_lot_line(ids[IK_WOOD], 1, b, sizeof(b)) > 0); test_log("%s", b);
    CHECK(strstr(b, "Wood 9 wu/month") && strstr(b, "regional"));
    CHECK(industry_lot_line(ids[IK_WOOD], 2, b, sizeof(b)) > 0); test_log("%s", b);
    CHECK(strstr(b, "West 60%"));
    CHECK(industry_lot_line(ids[IK_METAL], 0, b, sizeof(b)) > 0); test_log("%s", b);
    CHECK(strstr(b, "Engineering works:"));
    CHECK(industry_lot_line(ids[IK_FOOD], 1, b, sizeof(b)) > 0); test_log("%s", b);
    CHECK(strstr(b, "Milk 15 mu/month"));
    CHECK(industry_lot_reqs(ids[IK_WOOD], 1, &it, 1) == 1);
    text_req_line(b, sizeof(b), &it); test_log("%s", b);
    CHECK(strstr(b, "Wood supply"));
    CHECK(industry_demand_line(IK_WOOD, b, sizeof(b), &it.have) > 0);
    {   /* the query checklist of the sawmill carries the wood item */
        LotInfo li;
        int found = 0;
        CHECK(sim_query_lot(ids[IK_WOOD], &li));
        for (k = 0; k < li.next.n; k++) if (li.next.item[k].req == REQ_WOOD) found = 1;
        CHECK(found);
    }
    it_dump("industry_exports.txt");
}

/* ------------------------------------------------------------------ determinism + save round-trip */
static void rt_build(void)
{
    int i;
    it_road(0, 64, 127, 64, ROAD_MAIN);
    for (i = 0; i < 128; i++) world_set_tile_flags(i, 64, TF_PROTECTED, 0);
    world_set_tile_flags(0, 64, TF_EDGE_LINK, 0);
    world_set_tile_flags(127, 64, TF_EDGE_LINK, 0);
    it_terrain(4, 66, 13, 80, TER_WATER, 0, 0);
    it_terrain(14, 74, 34, 92, TER_FOREST, FOREST_SPRUCE, 12);
    it_road(24, 65, 24, 92, ROAD_GRAVEL);
    it_zone(14, 74, 34, 92, ZONE_FORESTRY);
    it_zone(14, 65, 22, 72, ZONE_INDUSTRY);
    it_zone(40, 65, 70, 72, ZONE_FARM);
    it_zone(80, 40, 110, 62, ZONE_HOUSES);
    it_road(80, 50, 110, 50, ROAD_STREET); it_road(80, 56, 110, 56, ROAD_STREET); it_road(80, 44, 110, 44, ROAD_STREET);
    it_place(B_COAL_PLANT, 114, 60, ROT_0, LOT_ACTIVE);
    it_line(14, 63, 113, 63);
    it_place(B_SCHOOL, 76, 60, ROT_0, LOT_ACTIVE);
    for (i = 0; i < 4; i++) it_place(B_IND_WOOD_T1_3X3, 26 + 4 * i, 65, ROT_2, LOT_ACTIVE);
}

static u32 rt_run(int split, int n, int m, int *pop)
{
    SaveMeta meta;
    SaveFile *sf;
    it_new(77, 1958);
    rt_build();
    sim_run_months(n);
    if (split) {
        memset(&meta, 0, sizeof(meta)); meta.saved_time = 1000000;
        sf = save_mem_open();
        save_game_to(sf, &meta);
        it_new(1, 1955);
        save_mem_reopen(sf);
        if (!load_game_from(sf, NULL)) { test_log("load failed: %s", save_last_error()); save_mem_close(sf); return 0; }
        save_mem_close(sf);
    }
    /* a player action after the save point, then a split inside a month */
    world_set_zone(30, 70, ZONE_INDUSTRY);
    sim_run_months(m);
    { int k; for (k = 0; k < 23; k++) sim_step(); }
    *pop = sim_stats()->population;
    return sim_hash();
}

TEST(industry_determinism_and_save)
{
    int p0, p1, p2, n;
    u32 h0, h1, h2;
    h0 = rt_run(0, 30, 24, &p0);
    it_dump("industry_roundtrip.txt");
    h1 = rt_run(0, 30, 24, &p1);
    h2 = rt_run(1, 30, 24, &p2);
    test_log("unbroken %08x / %08x, save+load %08x; pop %d %d %d; camps %d farms %d sawmills %d cut %d", h0, h1, h2, p0, p1, p2,
             industry_wood()->camps, industry_milk()->farm_lots, industry_kinds()->lots[IK_WOOD], industry_wood()->cut_tiles_total);
    CHECK_EQ(h0, h1);
    CHECK_EQ(h0, h2);
    CHECK(industry_wood()->camps > 0 && industry_milk()->farm_lots > 0 && industry_wood()->cut_tiles_total > 0);
    /* a save split in the middle of a month (between cycles) */
    for (n = 5; n <= 7; n++) {
        SaveMeta meta;
        SaveFile *sf;
        int k;
        u32 a, b;
        it_new(78, 1958); rt_build(); sim_run_months(20);
        for (k = 0; k < n * 7; k++) sim_step();
        memset(&meta, 0, sizeof(meta)); meta.saved_time = 1000000;
        sf = save_mem_open(); save_game_to(sf, &meta);
        sim_run_months(14); a = sim_hash();
        it_new(2, 1955); save_mem_reopen(sf);
        CHECK(load_game_from(sf, NULL));
        save_mem_close(sf);
        sim_run_months(14); b = sim_hash();
        test_log("split at step +%d: %08x %08x", n * 7, a, b);
        CHECK_EQ(a, b);
    }
}


/* ------------------------------------------------------------------ the VM script, headless (scripts/m3_industry.txt) */
TEST(industry_script_session)
{
    MapGenParams mp;
    SimSettings s;
    FILE *out;
    int errors;
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    mapgen_defaults(&mp);
    mp.seed = 7;
    CHECK(mapgen_generate(&mp));
    sim_default_settings(&s);
    s.seed = 7;
    sim_new_city(&s);
    tools_reset();
    tools_script_reset();
    out = fopen("out/m3_script_out.txt", "w");
    remove("out/m3_industry.txt");
    tools_script_set_dir("out/");
    errors = tools_script_run_file("../scripts/m3_industry.txt", out);
    sim_statlog_close();
    tools_script_set_dir("");
    if (out) fclose(out);
    test_log("m3_industry: %d errors, pop %d, money %d; sawmills %d dairies %d camps %d farms %d (T3 %d), cut %d, world %08x",
             errors, sim_stats()->population, g_budget.money, industry_kinds()->lots[IK_WOOD], industry_kinds()->lots[IK_FOOD],
             industry_wood()->camps, industry_milk()->farm_lots, industry_milk()->farm_tier[3], industry_wood()->cut_tiles_total,
             world_hash());
    if (errors < 0) { test_log("scripts/m3_industry.txt missing"); return; }
    CHECK_EQ(errors, 0);
}
