/* test_mapgen.cpp - map generator tests (T06). Debug from text: per preset/seed an ASCII map with the
   statistics goes to tests/out/mapgen_<preset>_<seed>.txt, a terrain-colour preview to
   tests/out/mapgen_<preset>_<seed>.ppm (4 px per tile for seed 1, 1 px otherwise), and a summary table
   of all runs to tests/out/mapgen_summary.txt. */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "mapgen.h"

static const char *const s_pkeys[MAP_PRESET_COUNT] = { "lakeland", "river", "coast", "forest" };

static void init_once(void)
{
    static int inited;
    if (!inited) { world_init(); inited = 1; }
}

static int gen(int preset, u32 seed, int size, int railway)
{
    MapGenParams p;
    init_once();
    mapgen_defaults(&p);
    p.preset = preset; p.seed = seed; p.w = p.h = size; p.railway = railway;
    return mapgen_generate(&p);
}

/* ------------------------------------------------------------------ checks */
static int count_bits(int m) { int n = 0; while (m) { n += m & 1; m >>= 1; } return n; }

/* national road / railway: every tile of class cls has exactly 2 connections (edge links count),
   bridges are straight (mask 05/10) runs of <= BRIDGE_MAX_LEN. Returns the number of problems. */
static int check_lines(int cls, int log)
{
    int x, y, bad = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = WT(x, y);
            int m;
            if (TILE_ROAD(t) != cls) continue;
            m = TILE_ROAD_MASK(t);
            if (count_bits(m) != 2) { bad++; if (log) test_log("line %d at %d,%d mask %d", cls, x, y, m); }
            if (TER_IS_WATER(TER_TYPE(t))) {
                int d, run = 1;
                if (m != 5 && m != 10) { bad++; if (log) test_log("bent bridge at %d,%d mask %d", x, y, m); }
                d = m == 5 ? DIR_NE : DIR_NW;
                {
                    int cx = x + g_dir_dx[d], cy = y + g_dir_dy[d];
                    while (world_in(cx, cy) && TER_IS_WATER(TER_TYPE(WT(cx, cy))) && TILE_ROAD(WT(cx, cy))) {
                        run++; cx += g_dir_dx[d]; cy += g_dir_dy[d];
                    }
                    d = DIR_OPPOSITE(d);
                    cx = x + g_dir_dx[d]; cy = y + g_dir_dy[d];
                    while (world_in(cx, cy) && TER_IS_WATER(TER_TYPE(WT(cx, cy))) && TILE_ROAD(WT(cx, cy))) {
                        run++; cx += g_dir_dx[d]; cy += g_dir_dy[d];
                    }
                }
                if (run > BRIDGE_MAX_LEN) { bad++; if (log) test_log("bridge too long at %d,%d (%d)", x, y, run); }
            }
            if (!(t->flags & TF_PROTECTED)) bad++;
        }
    return bad;
}

static int count_edge_links(int cls)
{
    int x, y, n = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++)
            if ((WT(x, y)->flags & TF_EDGE_LINK) && TILE_ROAD(WT(x, y)) == cls) n++;
    return n;
}

/* ------------------------------------------------------------------ outputs */
static void preview_rgb(const Tile *t, u8 *rgb)
{
    static const u8 forest[FOREST_COUNT][3] = { { 58, 88, 52 }, { 36, 60, 40 }, { 122, 160, 84 }, { 86, 110, 60 } };
    int ter = TER_TYPE(t), v = TER_VAR(t), r = TILE_ROAD(t);
    u8 c[3] = { 255, 0, 255 };
    if (r == ROAD_RAIL) { c[0] = 130; c[1] = 50; c[2] = 50; }
    else if (r) { if (TER_IS_WATER(ter)) { c[0] = 170; c[1] = 140; c[2] = 100; } else { c[0] = c[1] = c[2] = 50; } }
    else switch (ter) {
        case TER_WATER: c[0] = 47; c[1] = 75; c[2] = 102; break;
        case TER_RIVER: if (v & RIVER_RAPIDS_BIT) { c[0] = 232; c[1] = 238; c[2] = 240; }
                        else { c[0] = 70; c[1] = 105; c[2] = 140; } break;
        case TER_FOREST: c[0] = forest[v & 3][0]; c[1] = forest[v & 3][1]; c[2] = forest[v & 3][2]; break;
        case TER_ROCK: c[0] = 150; c[1] = 140; c[2] = 132; break;
        case TER_FIELD: if (v == FIELD_PLOUGH) { c[0] = 100; c[1] = 76; c[2] = 58; } else { c[0] = 214; c[1] = 194; c[2] = 122; } break;
        case TER_GRASS:
            if (t->flags & TF_TREE) { c[0] = 80; c[1] = 112; c[2] = 58; }
            else if (v == 2) { c[0] = 150; c[1] = 165; c[2] = 92; }
            else { c[0] = 118; c[1] = 142; c[2] = 76; }
            break;
        default: break;
    }
    rgb[0] = c[0]; rgb[1] = c[1]; rgb[2] = c[2];
}

static void write_ppm(const char *path, int scale)
{
    FILE *f = fopen(path, "wb");
    int x, y, sy, sx, w = g_world.w, h = g_world.h;
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w * scale, h * scale);
    for (y = 0; y < h; y++)
        for (sy = 0; sy < scale; sy++)
            for (x = 0; x < w; x++) {
                u8 rgb[3];
                int shore = mapgen_shore_mask(x, y);
                preview_rgb(WT(x, y), rgb);
                for (sx = 0; sx < scale; sx++) {
                    u8 o[3];
                    o[0] = rgb[0]; o[1] = rgb[1]; o[2] = rgb[2];
                    /* shore tiles: a sand dot in the middle so the autotile ring is visible at 4 px */
                    if (scale >= 4 && shore != 15 && shore != 0 && sx >= 1 && sx <= 2 && sy >= 1 && sy <= 2 &&
                        !TILE_ROAD(WT(x, y))) { o[0] = 196; o[1] = 182; o[2] = 140; }
                    fwrite(o, 1, 3, f);
                }
            }
    fclose(f);
}

static void report_line(char *buf, int size, int preset, u32 seed)
{
    const MapGenReport *r = mapgen_report();
    str_printf(buf, size,
               "%-10s %4u %3dx%-3d water %2d%% forest %2d%% rock %d%% field %2d%% build %2d%% open %2d%% "
               "road %3d (br %d/%d) rdfor %2d%% river %d rap %d hyd %2d rail %d lakes %2d isl %2d fix %3d "
               "shore %d %.2f ms",
               mapgen_preset_name(preset), seed, g_world.w, g_world.h, r->water_pct, r->forest_pct, r->rock_pct,
               r->field_pct, r->buildable_pct, r->open_pct, r->road_tiles, r->bridge_tiles, r->bridge_max,
               r->road_forest_pct, r->river, r->rapids_tiles, r->hydro_sites, r->railway, r->lakes, r->islands,
               r->pinches_fixed, r->shore_errors, r->ms);
}

static void write_dump(int preset, u32 seed)
{
    char path[128], line[400];
    FILE *f;
    str_printf(path, sizeof(path), "out/mapgen_%s_%u.txt", s_pkeys[preset], seed);
    f = fopen(path, "w");
    if (!f) return;
    report_line(line, sizeof(line), preset, seed);
    fprintf(f, "%s\ntown site %d,%d  road %d,%d -> %d,%d  rapids at %d,%d\n\n", line,
            mapgen_report()->town_x, mapgen_report()->town_y, mapgen_report()->road_x0, mapgen_report()->road_y0,
            mapgen_report()->road_x1, mapgen_report()->road_y1, mapgen_report()->rapids_x, mapgen_report()->rapids_y);
    mapgen_dump_ascii(f);
    fclose(f);
}

/* all constraints of DESIGN 3 / TASKS T06 on the current map; returns the number of failures */
static int constraints(int preset, int log)
{
    const MapGenReport *r = mapgen_report();
    int bad = 0;
    if (mapgen_shore_errors() != 0) { bad++; if (log) test_log("shore errors %d", mapgen_shore_errors()); }
    if (!mapgen_road_connected()) { bad++; if (log) test_log("road not connected"); }
    if (count_edge_links(ROAD_MAIN) != 2) { bad++; if (log) test_log("road edge links %d", count_edge_links(ROAD_MAIN)); }
    if (r->buildable_pct < 55) { bad++; if (log) test_log("buildable %d%%", r->buildable_pct); }
    if (r->road_forest_pct >= 50) { bad++; if (log) test_log("road in forest %d%%", r->road_forest_pct); }
    if (r->bridge_max > BRIDGE_MAX_LEN) { bad++; if (log) test_log("bridge %d", r->bridge_max); }
    if (check_lines(ROAD_MAIN, log)) { bad++; }
    if (r->town_x < 0 || TILE_ROAD(WT(r->town_x, r->town_y)) != ROAD_MAIN) { bad++; if (log) test_log("no town site"); }
    if (preset == MAP_PRESET_RIVER && (!r->river || !r->rapids_tiles || !r->hydro_sites)) {
        bad++; if (log) test_log("river town without river/rapids/hydro site (%d %d %d)", r->river, r->rapids_tiles, r->hydro_sites);
    }
    if (r->river && r->rapids_tiles && !r->hydro_sites) { bad++; if (log) test_log("rapids without a hydro site"); }
    return bad;
}

/* ------------------------------------------------------------------ tests */
TEST(mapgen_shore_rule)
{
    int x, y;
    init_once();
    world_new(16, 16);
    CHECK_EQ(mapgen_shore_errors(), 0);
    CHECK_EQ(mapgen_shore_mask(5, 5), 15);
    /* a 2x2 pond at (6..7, 6..7) */
    for (y = 6; y <= 7; y++) for (x = 6; x <= 7; x++) world_set_terrain(x, y, TER_WATER, 0);
    CHECK_EQ(mapgen_shore_errors(), 0);
    CHECK_EQ(mapgen_shore_mask(6, 6), 0);         /* water tile */
    CHECK_EQ(mapgen_shore_mask(5, 5), 1 | 2 | 4);  /* NW-diagonal neighbour: only its s corner is water */
    CHECK_EQ(mapgen_shore_mask(6, 5), 1 | 2);      /* north neighbour: w and s corners wet */
    CHECK_EQ(mapgen_shore_mask(8, 6), 2 | 8);      /* east neighbour: n and w corners wet */
    CHECK_EQ(mapgen_shore_mask(8, 8), 2 | 4 | 8);  /* SE diagonal: n corner wet */
    /* a single-tile speck is an error, so is a diagonal checkerboard and a 1-wide land strip */
    world_set_terrain(12, 12, TER_WATER, 0);
    CHECK(mapgen_shore_errors() > 0);
    world_set_terrain(12, 12, TER_GRASS, 0);
    world_set_terrain(8, 8, TER_WATER, 0); world_set_terrain(9, 8, TER_WATER, 0);
    world_set_terrain(8, 9, TER_WATER, 0); world_set_terrain(9, 9, TER_WATER, 0);  /* touches the pond at a corner */
    CHECK(mapgen_shore_errors() > 0);
    /* a 1-wide river is fine */
    world_new(16, 16);
    for (y = 0; y < 16; y++) world_set_terrain(7, y, TER_RIVER, DIR_SW);
    CHECK_EQ(mapgen_shore_errors(), 0);
    CHECK_EQ(mapgen_shore_mask(6, 4), 1 | 4);
}

TEST(mapgen_presets)
{
    static const u32 seeds[3] = { 1, 2, 3 };
    int preset, k, fails = 0;
    FILE *sum = fopen("out/mapgen_summary.txt", "w");
    for (preset = 0; preset < MAP_PRESET_COUNT; preset++)
        for (k = 0; k < 3; k++) {
            char line[400], path[128];
            int ok = gen(preset, seeds[k], 128, 0), bad;
            CHECK(ok);
            report_line(line, sizeof(line), preset, seeds[k]);
            test_log("%s", line);
            if (sum) fprintf(sum, "%s\n", line);
            write_dump(preset, seeds[k]);
            str_printf(path, sizeof(path), "out/mapgen_%s_%u.ppm", s_pkeys[preset], seeds[k]);
            write_ppm(path, seeds[k] == 1 ? 4 : 1);
            bad = constraints(preset, 1);
            fails += bad;
        }
    if (sum) fclose(sum);
    CHECK_EQ(fails, 0);
}

TEST(mapgen_deterministic)
{
    u32 h1, h2, h3;
    gen(MAP_PRESET_LAKELAND, 77, 128, 1); h1 = world_hash();
    gen(MAP_PRESET_COAST, 5, 128, 0);
    gen(MAP_PRESET_LAKELAND, 77, 128, 1); h2 = world_hash();
    gen(MAP_PRESET_LAKELAND, 78, 128, 1); h3 = world_hash();
    test_log("hash seed 77: %08x %08x, seed 78: %08x", h1, h2, h3);
    CHECK_EQ(h1, h2);
    CHECK(h1 != h3);
}

/* 50 seeds per preset: every constraint holds, plus min/max statistics and timing */
TEST(mapgen_many_seeds)
{
    int preset, fails = 0;
    for (preset = 0; preset < MAP_PRESET_COUNT; preset++) {
        int s, wmin = 100, wmax = 0, fmin = 100, fmax = 0, bmin = 100, omin = 100, rf = 0, nriver = 0, nbr = 0;
        double tsum = 0, tmax = 0;
        for (s = 100; s < 150; s++) {
            const MapGenReport *r;
            int bad;
            gen(preset, (u32)s, 128, (s & 1));
            r = mapgen_report();
            bad = constraints(preset, 0);
            if (bad) {
                test_log("%s seed %d: %d constraint failures:", mapgen_preset_name(preset), s, bad);
                constraints(preset, 1);
                write_dump(preset, (u32)s);
            }
            if ((s & 1) && !r->railway) test_log("%s seed %d: no railway", mapgen_preset_name(preset), s);
            if ((s & 1) && r->railway && check_lines(ROAD_RAIL, 1)) { bad++; test_log("rail problems seed %d", s); }
            fails += bad;
            wmin = IMIN(wmin, r->water_pct); wmax = IMAX(wmax, r->water_pct);
            fmin = IMIN(fmin, r->forest_pct); fmax = IMAX(fmax, r->forest_pct);
            bmin = IMIN(bmin, r->buildable_pct); omin = IMIN(omin, r->open_pct);
            rf = IMAX(rf, r->road_forest_pct);
            nriver += r->river; nbr += r->bridge_tiles > 0;
            tsum += r->ms; if (r->ms > tmax) tmax = r->ms;
        }
        test_log("%-10s water %d-%d%% forest %d-%d%% buildable >= %d%% open >= %d%% roadside forest <= %d%% "
                 "rivers %d/50 bridged %d/50, %.2f ms avg %.2f max",
                 mapgen_preset_name(preset), wmin, wmax, fmin, fmax, bmin, omin, rf, nriver, nbr, tsum / 50, tmax);
    }
    CHECK_EQ(fails, 0);
}

TEST(mapgen_256)
{
    int preset, fails = 0;
    for (preset = 0; preset < MAP_PRESET_COUNT; preset++) {
        char line[400];
        CHECK(gen(preset, 9, 256, 1));
        report_line(line, sizeof(line), preset, 9);
        test_log("%s", line);
        fails += constraints(preset, 1);
        if (preset == MAP_PRESET_LAKELAND) {
            write_ppm("out/mapgen_lakeland_256.ppm", 2);
            write_dump(preset, 9);
        }
    }
    CHECK_EQ(fails, 0);
}

TEST(mapgen_railway)
{
    int preset, fails = 0, laid = 0;
    for (preset = 0; preset < MAP_PRESET_COUNT; preset++) {
        u32 s;
        for (s = 1; s <= 3; s++) {
            gen(preset, s, 128, 1);
            if (!mapgen_report()->railway) { test_log("%s seed %u: no railway", mapgen_preset_name(preset), s); continue; }
            laid++;
            if (count_edge_links(ROAD_RAIL) != 2) { fails++; test_log("rail edge links %d", count_edge_links(ROAD_RAIL)); }
            fails += check_lines(ROAD_RAIL, 1);
            fails += constraints(preset, 1);
            if (s == 1) {
                char path[128];
                str_printf(path, sizeof(path), "out/mapgen_%s_rail.ppm", s_pkeys[preset]);
                write_ppm(path, 2);
            }
        }
    }
    test_log("railway laid on %d/12 maps", laid);
    CHECK(laid >= 10);
    CHECK_EQ(fails, 0);
}
