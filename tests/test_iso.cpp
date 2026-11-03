/* test_iso.cpp - projection, picking and camera tests for iso.cpp. Owner: T05.
   Picking must agree with the art's diamond rule (pixel centre strictly inside, STYLE.md 3): checked
   against a brute-force float test for every pixel of a large area at both zooms. */
#include "test.h"
#include "iso.h"

/* brute force: which tile's diamond contains the centre of world pixel (wx, wy)? */
static int brute_tile(int z, int wx, int wy, int *bx, int *by)
{
    double hw = TILE_W(z) / 2.0, hh = TILE_H(z) / 2.0;
    double rx = wx + 0.5 - iso_origin_x(z), ry = wy + 0.5 - ISO_TOP_MARGIN(z);
    double a = rx / hw, b = ry / hh;          /* a = x - y, b = x + y (north vertex coords) */
    double fx = (a + b) / 2.0, fy = (b - a) / 2.0;
    int tx = (int)(fx + 1000.0) - 1000, ty = (int)(fy + 1000.0) - 1000;
    *bx = tx; *by = ty;
    /* exactly on an edge would be ambiguous; with odd numerators this never happens (iso.cpp) */
    return 1;
}

TEST(iso_pick_matches_diamond_rule)
{
    int z, wx, wy, x, y, bx, by, bad = 0, n = 0;
    iso_init(64, 64);
    for (z = 0; z < ZOOM_COUNT; z++) {
        int ox = iso_origin_x(z), oy = ISO_TOP_MARGIN(z);
        for (wy = oy - 40; wy < oy + 40 * TILE_H(z); wy++)
            for (wx = ox - 20 * TILE_W(z); wx < ox + 20 * TILE_W(z); wx++) {
                iso_world_to_tile(z, wx, wy, &x, &y);
                brute_tile(z, wx, wy, &bx, &by);
                n++;
                if (x != bx || y != by) { if (bad < 5) test_log("z%d (%d,%d): pick %d,%d brute %d,%d", z, wx, wy, x, y, bx, by); bad++; }
            }
    }
    test_log("%d pixels checked, %d mismatches", n, bad);
    CHECK_EQ(bad, 0);
}

TEST(iso_tile_round_trip_and_rows)
{
    int z, x, y, wx, wy, px, py, row, cnt;
    iso_init(128, 128);
    for (z = 0; z < ZOOM_COUNT; z++) {
        for (y = 0; y < 128; y += 7)
            for (x = 0; x < 128; x += 5) {
                iso_tile_to_world(z, x, y, &wx, &wy);
                /* tile centre and the 2 px of the top row pick back to the tile */
                CHECK(iso_world_to_tile(z, wx, wy + TILE_H(z) / 2, &px, &py));
                CHECK_EQ(px, x); CHECK_EQ(py, y);
                iso_world_to_tile(z, wx - 1, wy, &px, &py); CHECK_EQ(px, x); CHECK_EQ(py, y);
                iso_world_to_tile(z, wx, wy, &px, &py); CHECK_EQ(px, x); CHECK_EQ(py, y);
                iso_world_to_tile(z, wx - 2, wy, &px, &py); CHECK(px != x || py != y);
            }
        /* row widths 2, 6, ..., TW-2, TW-2, ..., 2 (STYLE.md 3) */
        iso_tile_to_world(z, 60, 60, &wx, &wy);
        for (row = 0; row < TILE_H(z); row++) {
            int want = row < TILE_H(z) / 2 ? 4 * row + 2 : 4 * (TILE_H(z) - 1 - row) + 2;
            cnt = 0;
            for (px = wx - TILE_W(z); px < wx + TILE_W(z); px++) {
                iso_world_to_tile(z, px, wy + row, &x, &y);
                if (x == 60 && y == 60) cnt++;
            }
            CHECK_EQ(cnt, want);
        }
    }
    /* sub-tile positions: the centre of tile (3,4) */
    iso_pos_to_world(ZOOM_64, 3 * 256 + 128, 4 * 256 + 128, &wx, &wy);
    iso_tile_to_world(ZOOM_64, 3, 4, &px, &py);
    CHECK_EQ(wx, px); CHECK_EQ(wy, py + 16);
    CHECK(!iso_world_to_tile(ZOOM_64, 0, 0, &x, &y));          /* off map */
}

TEST(iso_rect_bounds_cover_sprites)
{
    int wx0, wy0, wx1, wy1, wx, wy, x, y;
    iso_init(128, 128);
    iso_tile_rect_bounds(ZOOM_64, 10, 20, 12, 21, &wx0, &wy0, &wx1, &wy1);
    for (y = 20; y <= 21; y++)
        for (x = 10; x <= 12; x++) {
            iso_tile_to_world(ZOOM_64, x, y, &wx, &wy);
            CHECK(wx - 32 >= wx0 && wx + 32 <= wx1);
            CHECK(wy + 32 <= wy1);
            CHECK(wy - ISO_MAX_SPRITE_UP(ZOOM_64) >= wy0);
        }
    CHECK_EQ(wx1 - wx0, (3 + 2) * 32);
}

TEST(iso_camera_zoom_and_clamp)
{
    int x, y, wx, wy, sx, sy, cx0, cy0;
    iso_init(128, 128);
    g_cam.zoom = ZOOM_64;
    cam_set_view(0, 16, 656, 570);
    cam_center_on_tile(64, 64);
    cam_center_tile(&x, &y);
    CHECK_EQ(x, 64); CHECK_EQ(y, 64);
    /* zoom keeps the world point under the mouse */
    CHECK(cam_pick(200, 300, &x, &y));
    cam_set_zoom(ZOOM_32, 200, 300);
    CHECK(cam_pick(200, 300, &cx0, &cy0));
    CHECK(IABS(cx0 - x) <= 1 && IABS(cy0 - y) <= 1);
    cam_set_zoom(ZOOM_64, 200, 300);
    CHECK(cam_pick(200, 300, &cx0, &cy0));
    CHECK_EQ(cx0, x); CHECK_EQ(cy0, y);
    /* screen <-> tile */
    cam_tile_to_screen(70, 60, &sx, &sy);
    CHECK(cam_pick(sx, sy + 16, &x, &y));
    CHECK_EQ(x, 70); CHECK_EQ(y, 60);
    /* clamp: the view centre never leaves the map diamond */
    cam_scroll(-100000, -100000);
    iso_world_to_tile(ZOOM_64, g_cam.x + 328, g_cam.y + 285, &x, &y);
    CHECK(x >= -1 && y >= -1 && x <= 128 && y <= 128);
    cam_scroll(200000, 200000);
    iso_world_to_tile(ZOOM_64, g_cam.x + 328, g_cam.y + 285, &x, &y);
    CHECK(x >= -1 && y >= -1 && x <= 128 && y <= 128);
    /* eased scrolling: whole-pixel positions, converges to the pushed speed, stops */
    cam_center_on_tile(64, 64);
    wx = g_cam.x; wy = g_cam.y;
    for (x = 0; x < 30; x++) { cam_push(8, 0); cam_update(); }
    CHECK_EQ(g_cam.vx, 8 * 256);
    CHECK(g_cam.x > wx + 150);
    for (x = 0; x < 30; x++) cam_update();
    CHECK_EQ(g_cam.vx, 0);
    CHECK_EQ(g_cam.y, wy);
}
