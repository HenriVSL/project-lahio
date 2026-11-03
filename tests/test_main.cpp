/* test_main.cpp - test runner + basic sanity tests. Owner: T00.
   Usage (from tests/): build/lahio_tests [filter]   runs tests whose name contains filter.
   Exit code = number of failed tests. */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "iso.h"

#define MAX_TESTS 512
static struct { const char *name, *file; TestFn fn; } s_tests[MAX_TESTS];
static int s_ntests, s_failed_cur;

TestReg::TestReg(const char *name, const char *file, TestFn fn)
{
    if (s_ntests < MAX_TESTS) {
        s_tests[s_ntests].name = name; s_tests[s_ntests].file = file; s_tests[s_ntests].fn = fn;
        s_ntests++;
    }
}

void test_fail(const char *file, int line, const char *expr)
{
    printf("    FAIL %s:%d: %s\n", file, line, expr);
    s_failed_cur = 1;
}

void test_fail_eq(const char *file, int line, const char *expr, long long a, long long b)
{
    printf("    FAIL %s:%d: %s (%lld != %lld)\n", file, line, expr, a, b);
    s_failed_cur = 1;
}

void test_log(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    str_vprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("    %s\n", buf);
}

int main(int argc, char **argv)
{
    const char *filter = argc > 1 ? argv[1] : "";
    int i, run = 0, failed = 0;
    double t0 = time_ms(), t;
    log_open("out/tests.log");                /* module log_printf output of the whole run */
    for (i = 0; i < s_ntests; i++) {
        if (filter[0] && !strstr(s_tests[i].name, filter)) continue;
        log_printf("=== test %s", s_tests[i].name);
        s_failed_cur = 0;
        t = time_ms();
        s_tests[i].fn();
        printf("%s %-32s %8.1f ms\n", s_failed_cur ? "FAIL" : "ok  ", s_tests[i].name, time_ms() - t);
        run++;
        failed += s_failed_cur;
    }
    printf("%d/%d tests passed (%.0f ms)%s\n", run - failed, run, time_ms() - t0,
           run == 0 ? "  [no tests matched]" : "");
    log_close();
    return failed;
}

/* ------------------------------------------------------------------ basic sanity (T00) */
TEST(types_and_layout)
{
    CHECK_EQ(sizeof(u8), 1);
    CHECK_EQ(sizeof(u16), 2);
    CHECK_EQ(sizeof(u32), 4);
    CHECK_EQ(sizeof(s64), 8);
    CHECK_EQ(sizeof(Tile), 8);
    CHECK_EQ(sizeof(Lot), 20);
    CHECK_EQ(TILE_IDX(3, 2), 2 * MAP_STRIDE + 3);
    CHECK_EQ(FLOORDIV(-1, 256), -1);
    CHECK_EQ(FLOORMOD(-1, 256), 255);
}

TEST(rng_deterministic)
{
    Rng a, b;
    int i;
    rng_seed(&a, 42); rng_seed(&b, 42);
    for (i = 0; i < 1000; i++) CHECK_EQ(rng_next(&a), rng_next(&b));
    for (i = 0; i < 1000; i++) { int v = rng_below(&a, 7); CHECK(v >= 0 && v < 7); }
}

TEST(defs_tables)
{
    int w, h;
    CHECK(def_building_find("house_t2") == B_HOUSE_T2);
    CHECK(def_building_find("no_such_thing") == B_NONE);
    def_footprint(B_APT_T1_3X1, ROT_1, &w, &h);
    CHECK_EQ(w, 1); CHECK_EQ(h, 3);
    CHECK_EQ(def_status_for_population(3000), TS_MARKET_TOWN);
    CHECK_EQ(def_era(1969), ERA_WELFARE);
    CHECK_EQ(g_dir_dx[g_rot_front_dir[ROT_0]], 0);   /* r0 front faces +Y (SW) */
    CHECK_EQ(g_dir_dy[g_rot_front_dir[ROT_0]], 1);
}

TEST(iso_projection_origin)
{
    int wx, wy;
    iso_init(128, 128);
    iso_tile_to_world(ZOOM_64, 0, 127, &wx, &wy);    /* left-most tile: north vertex at TW/2, */
    CHECK_EQ(wx, TILE_W(ZOOM_64) / 2);              /* so its left vertex touches wx = 0 */
    iso_tile_to_world(ZOOM_32, 0, 0, &wx, &wy);
    CHECK_EQ(wy, ISO_TOP_MARGIN(ZOOM_32));
}
