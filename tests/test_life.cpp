/* test_life.cpp - cosmetic car logic (lifecar.cpp): lane positions per direction, path continuity,
   turning choices, spacing, no car off the road over 10,000 simulated steps. Owner: T11.
   Writes tests/out/life_soak.txt (per-1000-step counts). */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "iso.h"
#include "lifecar.h"

static void life_world(void)
{
    /* no sim needed (and no sim_init: an early sim_init + a later world_init elsewhere dropped the sim's
       listener for later test files); world_init only if nobody did yet (test_save.cpp) */
    if (!g_world.w) world_init();
    world_new(128, 128);
    iso_init(128, 128);
    lc_init();
    lc_reset(77);
    lc_debug_force_flow(-1);
}

static void road(int x0, int y0, int x1, int y1, int cls)
{
    int x, y;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!TILE_ROAD(world_tile(x, y))) world_set_road(x, y, cls);
}

/* the soak network: crossing, T, dead ends, gravel, main, motorway, a bridge over a lake, a 2x2 loop,
   an edge link, a railway next to it */
static void build_network(void)
{
    int x, y;
    road(10, 20, 50, 20, ROAD_STREET);
    road(30, 10, 30, 40, ROAD_STREET);
    road(50, 20, 50, 30, ROAD_MAIN);
    road(51, 30, 60, 30, ROAD_MOTORWAY);
    road(20, 21, 20, 28, ROAD_GRAVEL);
    road(21, 28, 26, 28, ROAD_GRAVEL);
    for (y = 35; y <= 37; y++) for (x = 36; x <= 38; x++) world_set_terrain(x, y, TER_WATER, 0);
    road(31, 36, 42, 36, ROAD_STREET);                     /* bridges at x 36..38 */
    road(60, 10, 61, 11, ROAD_STREET);                     /* 2x2 loop of curves */
    road(31, 40, 127, 40, ROAD_STREET);
    world_set_tile_flags(127, 40, TF_EDGE_LINK, 0);
    road(20, 45, 40, 45, ROAD_RAIL);
}

static void view_mid(LcView *v)
{
    v->zoom = ZOOM_32;
    v->vx0 = 1700; v->vy0 = 350; v->vx1 = 2600; v->vy1 = 850;
    v->margin = 300;
}

static int near(s32 a, s32 b, int tol) { return IABS(a - b) <= tol; }

TEST(life_lane_offsets_per_direction)
{
    const LcCar *cars;
    int k, dir;
    s32 fx, fy;
    life_world();
    road(10, 20, 40, 20, ROAD_STREET);                     /* along X */
    road(25, 5, 25, 15, ROAD_STREET);                      /* along Y */
    road(5, 30, 15, 30, ROAD_GRAVEL);
    cars = lc_cars();
    k = lc_debug_spawn(20, 20, DIR_SE, DIR_SE, 128 << 8, 0) - 1;   /* heading +X: y + 3.6 units */
    CHECK(k >= 0);
    lc_car_point(&cars[k], &fx, &fy, &dir);
    CHECK_EQ(fx, 20 * 256 + 128); CHECK_EQ(fy, 20 * 256 + 128 + 29); CHECK_EQ(dir, DIR_SE);
    k = lc_debug_spawn(22, 20, DIR_NW, DIR_NW, 128 << 8, 0) - 1;   /* heading -X: y - 3.6 */
    lc_car_point(&cars[k], &fx, &fy, &dir);
    CHECK_EQ(fx, 22 * 256 + 128); CHECK_EQ(fy, 20 * 256 + 128 - 29); CHECK_EQ(dir, DIR_NW);
    k = lc_debug_spawn(25, 10, DIR_SW, DIR_SW, 128 << 8, 0) - 1;   /* heading +Y: x - 3.6 */
    lc_car_point(&cars[k], &fx, &fy, &dir);
    CHECK_EQ(fx, 25 * 256 + 128 - 29); CHECK_EQ(fy, 10 * 256 + 128); CHECK_EQ(dir, DIR_SW);
    k = lc_debug_spawn(25, 12, DIR_NE, DIR_NE, 128 << 8, 0) - 1;   /* heading -Y: x + 3.6 */
    lc_car_point(&cars[k], &fx, &fy, &dir);
    CHECK_EQ(fx, 25 * 256 + 128 + 29); CHECK_EQ(fy, 12 * 256 + 128); CHECK_EQ(dir, DIR_NE);
    k = lc_debug_spawn(10, 30, DIR_SE, DIR_SE, 128 << 8, 0) - 1;   /* gravel: 2.5 units */
    lc_car_point(&cars[k], &fx, &fy, &dir);
    CHECK_EQ(fy, 30 * 256 + 128 + 20);
    CHECK_EQ(lc_lane_offset(ROAD_MAIN, 0), 38);
    CHECK_EQ(lc_lane_offset(ROAD_MOTORWAY, 1), 77);
    CHECK_EQ(lc_count(), 5);
}

TEST(life_paths_are_continuous)
{
    LcCar c;
    int d, k, li, dir, bad = 0, steps;
    static const int lats[3] = { 20, 29, 40 };
    static const int offs[4] = { 0, 1, 3, 2 };            /* straight, right, left, U-turn */
    s32 fx, fy, px, py, sx, sy, ex, ey;
    life_world();
    memset(&c, 0, sizeof(c));
    c.x = 40; c.y = 40;
    for (d = 0; d < 4; d++)
        for (k = 0; k < 4; k++)
            for (li = 0; li < 3; li++) {
                int e = (d + offs[k]) & 3, rdx = -g_dir_dy[d], rdy = g_dir_dx[d], erx = -g_dir_dy[e], ery = g_dir_dx[e];
                c.din = (u8)d; c.dout = (u8)e; c.lat = c.lat0 = (s16)lats[li];
                c.len = (e == d) ? (256 << 8) : 0;
                /* path_len is internal: measure the length by walking until the exit edge */
                sx = 40 * 256 + 128 - g_dir_dx[d] * 128 + rdx * lats[li];
                sy = 40 * 256 + 128 - g_dir_dy[d] * 128 + rdy * lats[li];
                ex = 40 * 256 + 128 + g_dir_dx[e] * 128 + erx * lats[li];
                ey = 40 * 256 + 128 + g_dir_dy[e] * 128 + ery * lats[li];
                c.s = 0;
                lc_car_point(&c, &fx, &fy, &dir);
                if (!near(fx, sx, 1) || !near(fy, sy, 1) || dir != d) { bad++; test_log("start d%d e%d lat%d: %d,%d want %d,%d", d, e, lats[li], fx, fy, sx, sy); }
                px = fx; py = fy;
                for (steps = 1; steps < 1200; steps++) {      /* 1 fx steps: no jumps > 2 fx */
                    c.s = steps << 8;
                    lc_car_point(&c, &fx, &fy, &dir);
                    if (IABS(fx - px) + IABS(fy - py) > 3) { bad++; test_log("jump d%d e%d lat%d at %d", d, e, lats[li], steps); break; }
                    if (FLOORDIV(fx, 256) != 40 || FLOORDIV(fy, 256) != 40) { bad++; test_log("left tile d%d e%d", d, e); break; }
                    px = fx; py = fy;
                    if (near(fx, ex, 1) && near(fy, ey, 1) && dir == e) break;
                }
                if (steps == 1200) { bad++; test_log("never reached exit d%d e%d lat%d (at %d,%d want %d,%d)", d, e, lats[li], fx, fy, ex, ey); }
            }
    CHECK_EQ(bad, 0);
}

TEST(life_turning_prefers_flow)
{
    int i, n[4] = { 0, 0, 0, 0 }, id;
    life_world();
    road(10, 20, 30, 20, ROAD_STREET);
    road(20, 10, 20, 30, ROAD_STREET);                     /* crossing at 20,20 */
    id = world_lot_place(B_HOUSE_T1, 18, 23, ROT_0, 0, LOT_ACTIVE);
    CHECK(id > 0);
    g_world.lots[id].occupants = 600;                      /* busy south-west branch */
    CHECK(lc_flow(20, 21) > lc_flow(21, 20) + 100);
    for (i = 0; i < 10000; i++) n[lc_debug_choose(20, 20, DIR_SE)]++;
    test_log("heading SE at the crossing: NE %d SE %d SW %d NW(back) %d", n[DIR_NE], n[DIR_SE], n[DIR_SW], n[DIR_NW]);
    CHECK_EQ(n[DIR_NW], 0);
    CHECK(n[DIR_SW] > 6000);
    CHECK(n[DIR_SE] > n[DIR_NE]);
    CHECK(n[DIR_NE] > 500);
    CHECK_EQ(lc_debug_choose(10, 20, DIR_NW), DIR_SE);    /* dead end: U-turn */
    CHECK_EQ(lc_debug_choose(30, 20, DIR_SE), DIR_NW);
}

TEST(life_spacing_stop_behind)
{
    const LcCar *cars;
    int a, b, i, ok = 1;
    LcView v;
    LcParams p;
    life_world();
    road(10, 20, 40, 20, ROAD_STREET);
    lc_debug_force_flow(0);
    cars = lc_cars();
    b = lc_debug_spawn(30, 20, DIR_SE, DIR_SE, 0, 1) - 1;  /* front car */
    a = lc_debug_spawn(29, 20, DIR_SE, DIR_SE, 0, 2) - 1;  /* 256 fx behind */
    CHECK(a >= 0 && b >= 0);
    CHECK_EQ(lc_debug_gap(a), 256 << 8);
    CHECK(lc_debug_spawn(29, 20, DIR_SE, DIR_SE, 100 << 8, 3) == 0);   /* too close: refused */
    CHECK(lc_debug_spawn(29, 20, DIR_NW, DIR_NW, 100 << 8, 3) > 0);    /* other lane: fine */
    memset(&v, 0, sizeof(v));
    iso_world_size(ZOOM_32, &v.vx1, &v.vy1);
    v.zoom = ZOOM_32;
    p.year = 1970; p.population = 0; p.frozen = 0;
    /* front car drives into the dead end at x = 40, U-turns; the car behind never closes below the gap */
    for (i = 0; i < 2000 && ok; i++) {
        lc_update(16, &v, &p);
        if (!cars[a].used || !cars[b].used) break;
        if (cars[a].x == cars[b].x && cars[a].y == cars[b].y && cars[a].din == cars[b].din &&
            IABS(cars[a].s - cars[b].s) < ((LC_GAP - 1) << 8)) ok = 0;
        if (cars[a].din == DIR_SE && cars[b].din == DIR_SE && cars[b].x == cars[a].x + 1 &&
            (cars[a].len - cars[a].s) + cars[b].s < ((LC_GAP - 1) << 8)) ok = 0;
    }
    test_log("ran %d steps, cars %d", i, lc_count());
    CHECK(ok);
}

static int is_rail(int x, int y) { return TILE_ROAD(world_tile(x, y)) == ROAD_RAIL; }

TEST(life_soak_10000_steps)
{
    const LcCar *cars;
    LcView v;
    LcParams p;
    LcStats st;
    int step, i, j, offroad = 0, offtile = 0, rail = 0, lanebad = 0, maxAlive = 0, sumAlive = 0, removed = 0, onRemoved = 0, laneghost = 0, crossing = 0;
    double t0, tmax = 0, tsum = 0;
    FILE *f;
    life_world();
    build_network();
    lc_debug_force_flow(200);
    cars = lc_cars();
    view_mid(&v);
    p.year = 1972; p.population = 50000; p.frozen = 0;
    f = fopen("out/life_soak.txt", "w");
    if (f) fprintf(f, "step alive target visible blocked ghosts spawned despawned offview life road edge excess turns uturns tiles\n");
    for (step = 0; step < 10000; step++) {
        if (step == 5000) { world_set_road(30, 30, ROAD_NONE); removed = 1; }
        if (step == 3000) { v.vx0 += 200; v.vx1 += 200; }                 /* scroll a bit */
        if (step == 7000) { v.zoom = ZOOM_64; v.vx0 = 3600; v.vy0 = 700; v.vx1 = 4256; v.vy1 = 1220; } /* zoom in */
        t0 = time_ms();
        lc_update(16, &v, &p);
        t0 = time_ms() - t0;
        tsum += t0; if (t0 > tmax) tmax = t0;
        for (i = 0; i < LC_MAX_CARS; i++) {
            const LcCar *c = &cars[i];
            if (!c->used) continue;
            if (!lc_car_road(c->x, c->y)) offroad++;
            if (is_rail(c->x, c->y)) rail++;
            if (FLOORDIV(c->fx, 256) != c->x || FLOORDIV(c->fy, 256) != c->y) offtile++;
            if (removed && c->x == 30 && c->y == 30) onRemoved++;
            for (j = i + 1; j < LC_MAX_CARS; j++) {
                const LcCar *b = &cars[j];
                if (b->used && b->x == c->x && b->y == c->y && ((b->din ^ c->din) & 1) &&
                    IABS(b->fx - c->fx) + IABS(b->fy - c->fy) < 64) crossing++;
                if (!b->used || c->ghost_ms || b->ghost_ms || b->x != c->x || b->y != c->y || b->din != c->din) continue;
                if (TILE_ROAD(world_tile(c->x, c->y)) == ROAD_MOTORWAY && b->lane != c->lane) continue;
                if (IABS(b->s - c->s) < ((LC_GAP - 8) << 8)) { if (c->ghosted || b->ghosted) laneghost++; else { lanebad++; if (lanebad < 6) test_log("lane: step %d tile %d,%d din %d douts %d/%d s %d/%d", step, c->x, c->y, c->din, c->dout, b->dout, c->s >> 8, b->s >> 8); } }
            }
        }
        lc_stats(&st);
        sumAlive += st.alive; if (st.alive > maxAlive) maxAlive = st.alive;
        if (f && step % 1000 == 999)
            fprintf(f, "%d %d %d %d %d %d %u %u %u %u %u %u %u %u %u %u\n", step + 1, st.alive, st.target, st.visible, st.blocked, st.ghosts,
                    st.spawned, st.despawned, st.gone_offview, st.gone_life, st.gone_road, st.gone_edge, st.gone_excess, st.turns, st.uturns, st.tiles);
    }
    lc_stats(&st);
    if (f) {
        fprintf(f, "offroad %d offtile %d rail %d lane spacing violations (non-ghost) %d on removed tile %d\n", offroad, offtile, rail, lanebad, onRemoved);
        fprintf(f, "alive avg %.1f max %d; update ms avg %.4f max %.3f (Mac)\n", sumAlive / 10000.0, maxAlive, tsum / 10000, tmax);
        fclose(f);
    }
    test_log("alive avg %.1f max %d target %d; spawned %u despawned %u (offview %u life %u road %u edge %u excess %u)",
             sumAlive / 10000.0, maxAlive, st.target, st.spawned, st.despawned, st.gone_offview, st.gone_life, st.gone_road, st.gone_edge, st.gone_excess);
    test_log("ghost-related lane overlaps %d, crossing-traffic overlaps (car-frames) %d", laneghost, crossing);
    test_log("turns %u uturns %u tiles %u; offroad %d offtile %d rail %d lanebad %d; update avg %.4f ms max %.3f ms (Mac)",
             st.turns, st.uturns, st.tiles, offroad, offtile, rail, lanebad, tsum / 10000, tmax);
    CHECK_EQ(offroad, 0);
    CHECK_EQ(offtile, 0);
    CHECK_EQ(rail, 0);
    CHECK_EQ(onRemoved, 0);
    CHECK_EQ(lanebad, 0);
    CHECK(maxAlive >= 60);
    CHECK(st.turns > 100 && st.uturns > 5);
}

TEST(life_target_scales_with_era_and_population)
{
    LcView v;
    LcParams p;
    LcStats st;
    int t1955, t1970, i;
    life_world();
    build_network();
    lc_debug_force_flow(120);
    view_mid(&v);
    p.year = 1955; p.population = 855; p.frozen = 0;
    for (i = 0; i < 300; i++) lc_update(16, &v, &p);
    lc_stats(&st); t1955 = st.target;
    lc_reset(5);
    p.year = 1970; p.population = 6000;
    for (i = 0; i < 300; i++) lc_update(16, &v, &p);
    lc_stats(&st); t1970 = st.target;
    test_log("target 1955/855 pop: %d cars, 1970/6000 pop: %d cars (now %d alive)", t1955, t1970, st.alive);
    CHECK(t1955 < 30);
    CHECK(t1970 > 2 * t1955);
    CHECK(st.alive > 0);
}

TEST(life_cars_leave_at_edge_links)
{
    LcView v;
    LcParams p;
    LcStats st;
    int i;
    life_world();
    road(100, 40, 127, 40, ROAD_STREET);
    world_set_tile_flags(127, 40, TF_EDGE_LINK, 0);
    lc_debug_force_flow(0);
    CHECK(lc_debug_spawn(125, 40, DIR_SE, DIR_SE, 0, 4) > 0);
    memset(&v, 0, sizeof(v));
    iso_world_size(ZOOM_32, &v.vx1, &v.vy1);
    v.zoom = ZOOM_32;
    p.year = 1970; p.population = 0; p.frozen = 0;
    for (i = 0; i < 1000 && lc_count(); i++) lc_update(16, &v, &p);
    lc_stats(&st);
    test_log("left after %d steps, edge exits %u", i, st.gone_edge);
    CHECK_EQ(st.gone_edge, 1);
}

/* F3 (T12 note): the flow floor scales with car ownership (12 x lc_own_pct(year) / 50), so a 1956 town has
   fewer cosmetic cars and a 1975 one more than with the old fixed floor of 12 (BALANCE.md: ~9 / 29 / 51). */
TEST(life_floor_scales_with_car_ownership)
{
    static const int years[3] = { 1956, 1965, 1975 };
    LcView v;
    LcParams p;
    LcStats st;
    int t[3], k, i;
    for (k = 0; k < 3; k++) {
        life_world();
        build_network();
        view_mid(&v);
        p.year = years[k]; p.population = 50000; p.frozen = 0;
        for (i = 0; i < 400; i++) lc_update(16, &v, &p);
        lc_stats(&st);
        t[k] = st.target;
    }
    test_log("floor-only targets (no traffic, own %d/%d/%d%%): 1956 %d, 1965 %d, 1975 %d cars", lc_own_pct(1956),
             lc_own_pct(1965), lc_own_pct(1975), t[0], t[1], t[2]);
    CHECK(t[0] < t[1] && t[1] < t[2]);
    CHECK(t[2] >= 4 * t[0]);              /* old floor: target ~ own; now ~ own^2 (1975/1956 = 3.3^2) */
}
