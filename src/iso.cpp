/* iso.cpp - isometric projection, world-pixel space, screen<->tile picking, the camera. PORTABLE.
   Owner: T05. DESIGN 11.2, 11.3.7; STYLE.md 3; probe/RESULTS.md 8 (mouse-map picking).

   Picking: a TW x TH "mouse map" per zoom. The world plane is cut into cells of TW x TH whose centre
   diamond is the tile with its north vertex at the cell's top centre; the four corners belong to the
   neighbours. The mask stores the region per pixel using the art's diamond rule (a pixel belongs to a
   tile if its CENTRE is strictly inside the diamond, STYLE.md 3), so picking matches the drawn pixels
   exactly and the tiles partition the plane with no gaps or overlaps.

   Camera: x/y are whole world pixels (floor of the x256 sub-pixel position, once per update), so the
   chunk layer and every sprite move by the same integer and nothing jiggles (CLAUDE.md lesson). The view
   centre is clamped to the map diamond (fractional tile coordinates 0..w, 0..h). */
#include "common.h"
#include "iso.h"

Camera g_cam;
static int s_map_w = MAP_DEF_W, s_map_h = MAP_DEF_H;
static u8  s_mask[ZOOM_COUNT][64 * 32];   /* region per pixel of a TW x TH cell (zoom 1 uses 32x16) */
static int s_maskReady;
static int s_pushX, s_pushY;              /* cam_push target velocity this frame (px/frame) */

enum { MR_CENTER, MR_TL, MR_TR, MR_BL, MR_BR };

static void build_masks(void)
{
    int z, u, v;
    for (z = 0; z < ZOOM_COUNT; z++) {
        int tw = TILE_W(z), th = TILE_H(z);
        for (v = 0; v < th; v++)
            for (u = 0; u < tw; u++) {
                int du = 2 * u + 1 - tw, dv = 2 * v + 1 - th, r;
                if (IABS(du) * th + IABS(dv) * tw < tw * th) r = MR_CENTER;
                else if (du < 0) r = dv < 0 ? MR_TL : MR_BL;
                else r = dv < 0 ? MR_TR : MR_BR;
                s_mask[z][v * tw + u] = (u8)r;
            }
    }
    s_maskReady = 1;
}

static void cam_clamp(void);

void iso_init(int map_w, int map_h)
{
    if (!s_maskReady) build_masks();
    s_map_w = map_w > 0 ? map_w : MAP_DEF_W;
    s_map_h = map_h > 0 ? map_h : MAP_DEF_H;
    if (g_cam.zoom < 0 || g_cam.zoom >= ZOOM_COUNT) g_cam.zoom = ZOOM_64;
    cam_clamp();
}

int iso_origin_x(int zoom) { return s_map_h * (TILE_W(zoom) / 2); }

void iso_world_size(int zoom, int *w, int *h)
{
    *w = (s_map_w + s_map_h) * (TILE_W(zoom) / 2);
    *h = (s_map_w + s_map_h) * (TILE_H(zoom) / 2) + ISO_TOP_MARGIN(zoom);
}

void iso_tile_to_world(int zoom, int x, int y, int *wx, int *wy)
{
    *wx = (x - y) * (TILE_W(zoom) / 2) + iso_origin_x(zoom);
    *wy = (x + y) * (TILE_H(zoom) / 2) + ISO_TOP_MARGIN(zoom);
}

void iso_pos_to_world(int zoom, s32 fx, s32 fy, int *wx, int *wy)
{
    s32 a = (fx - fy) * (TILE_W(zoom) / 2), b = (fx + fy) * (TILE_H(zoom) / 2);
    *wx = (int)FLOORDIV(a, 256) + iso_origin_x(zoom);
    *wy = (int)FLOORDIV(b, 256) + ISO_TOP_MARGIN(zoom);
}

int iso_world_to_tile(int zoom, int wx, int wy, int *x, int *y)
{
    int tw = TILE_W(zoom), th = TILE_H(zoom);
    int rx = wx - iso_origin_x(zoom) + tw / 2, ry = wy - ISO_TOP_MARGIN(zoom);
    int cx = FLOORDIV(rx, tw), cy = FLOORDIV(ry, th);
    int u = rx - cx * tw, v = ry - cy * th;
    int tx = cx + cy, ty = cy - cx;
    if (!s_maskReady) build_masks();
    switch (s_mask[zoom][v * tw + u]) {
    case MR_TL: tx--; break;
    case MR_TR: ty--; break;
    case MR_BL: ty++; break;
    case MR_BR: tx++; break;
    default: break;
    }
    *x = tx; *y = ty;
    return tx >= 0 && ty >= 0 && tx < s_map_w && ty < s_map_h;
}

void iso_tile_rect_bounds(int zoom, int x0, int y0, int x1, int y1, int *wx0, int *wy0, int *wx1, int *wy1)
{
    int hw = TILE_W(zoom) / 2, ax, ay;
    if (x1 < x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int t = y0; y0 = y1; y1 = t; }
    iso_tile_to_world(zoom, x0, y1, &ax, &ay);  *wx0 = ax - hw;                 /* west vertex of (x0,y1) */
    iso_tile_to_world(zoom, x1, y0, &ax, &ay);  *wx1 = ax + hw;                 /* east vertex of (x1,y0) */
    iso_tile_to_world(zoom, x0, y0, &ax, &ay);  *wy0 = ay - ISO_MAX_SPRITE_UP(zoom);
    iso_tile_to_world(zoom, x1, y1, &ax, &ay);  *wy1 = ay + TILE_H(zoom);       /* south vertex of (x1,y1) */
}

/* ================================================================== camera */
/* world pixel (x256) <-> fractional tile coordinates (x256), ground plane */
static void w_to_t(int zoom, s32 wfx, s32 wfy, s32 *tfx, s32 *tfy)
{
    s32 rx = wfx - iso_origin_x(zoom) * 256, ry = wfy - ISO_TOP_MARGIN(zoom) * 256;
    s32 a = rx / (TILE_W(zoom) / 2), b = ry / (TILE_H(zoom) / 2);   /* (x - y), (x + y) in x256 */
    *tfx = (a + b) / 2; *tfy = (b - a) / 2;
}

static void t_to_w(int zoom, s32 tfx, s32 tfy, s32 *wfx, s32 *wfy)
{
    *wfx = (tfx - tfy) * (TILE_W(zoom) / 2) + iso_origin_x(zoom) * 256;
    *wfy = (tfx + tfy) * (TILE_H(zoom) / 2) + ISO_TOP_MARGIN(zoom) * 256;
}

static void cam_clamp(void)
{
    s32 cx, cy, tx, ty, ntx, nty;
    int z = g_cam.zoom;
    cx = g_cam.fx + g_cam.view_w * 128;      /* view centre, x256 */
    cy = g_cam.fy + g_cam.view_h * 128;
    w_to_t(z, cx, cy, &tx, &ty);
    ntx = ICLAMP(tx, 0, s_map_w * 256);
    nty = ICLAMP(ty, 0, s_map_h * 256);
    if (ntx != tx || nty != ty) {
        t_to_w(z, ntx, nty, &cx, &cy);
        g_cam.fx = cx - g_cam.view_w * 128;
        g_cam.fy = cy - g_cam.view_h * 128;
        if (ntx != tx) g_cam.vx = g_cam.vy = 0;
        if (nty != ty) g_cam.vx = g_cam.vy = 0;
    }
    g_cam.x = (int)FLOORDIV(g_cam.fx, 256);
    g_cam.y = (int)FLOORDIV(g_cam.fy, 256);
}

void cam_set_view(int x, int y, int w, int h)
{
    s32 cx = g_cam.fx + g_cam.view_w * 128, cy = g_cam.fy + g_cam.view_h * 128;
    g_cam.view_x = x; g_cam.view_y = y; g_cam.view_w = w; g_cam.view_h = h;
    g_cam.fx = cx - w * 128; g_cam.fy = cy - h * 128;     /* keep the centre */
    cam_clamp();
}

void cam_center_on_tile(int x, int y)
{
    int wx, wy;
    iso_tile_to_world(g_cam.zoom, x, y, &wx, &wy);
    wy += TILE_H(g_cam.zoom) / 2;
    g_cam.fx = (s32)(wx - g_cam.view_w / 2) * 256;
    g_cam.fy = (s32)(wy - g_cam.view_h / 2) * 256;
    g_cam.vx = g_cam.vy = 0;
    cam_clamp();
}

void cam_center_tile(int *x, int *y)
{
    iso_world_to_tile(g_cam.zoom, g_cam.x + g_cam.view_w / 2, g_cam.y + g_cam.view_h / 2, x, y);
    *x = ICLAMP(*x, 0, s_map_w - 1);
    *y = ICLAMP(*y, 0, s_map_h - 1);
}

void cam_scroll(int dx, int dy)
{
    g_cam.fx += (s32)dx * 256;
    g_cam.fy += (s32)dy * 256;
    cam_clamp();
}

void cam_push(int dx, int dy) { s_pushX += dx; s_pushY += dy; }

/* ease the velocity toward the pushed target (1/4 per frame), then move */
static s32 ease(s32 v, s32 target)
{
    s32 d = target - v;
    if (d > -64 && d < 64) return target;          /* snap within 1/4 px/frame */
    return v + d / 4;
}

void cam_update(void)
{
    g_cam.vx = ease(g_cam.vx, (s32)s_pushX * 256);
    g_cam.vy = ease(g_cam.vy, (s32)s_pushY * 256);
    s_pushX = s_pushY = 0;
    g_cam.fx += g_cam.vx;
    g_cam.fy += g_cam.vy;
    cam_clamp();
}

void cam_set_zoom(int zoom, int sx, int sy)
{
    s32 ox, oy;
    if (zoom < 0 || zoom >= ZOOM_COUNT || zoom == g_cam.zoom) return;
    ox = (s32)(sx - g_cam.view_x) * 256; oy = (s32)(sy - g_cam.view_y) * 256;
    if (zoom > g_cam.zoom) {           /* zoom out: world coordinates halve */
        g_cam.fx = (g_cam.fx + ox) / 2 - ox;
        g_cam.fy = (g_cam.fy + oy) / 2 - oy;
    } else {
        g_cam.fx = (g_cam.fx + ox) * 2 - ox;
        g_cam.fy = (g_cam.fy + oy) * 2 - oy;
    }
    g_cam.vx = g_cam.vy = 0;
    g_cam.zoom = zoom;
    cam_clamp();
}

int cam_pick(int sx, int sy, int *x, int *y)
{
    int r = iso_world_to_tile(g_cam.zoom, g_cam.x + sx - g_cam.view_x, g_cam.y + sy - g_cam.view_y, x, y);
    if (sx < g_cam.view_x || sy < g_cam.view_y || sx >= g_cam.view_x + g_cam.view_w || sy >= g_cam.view_y + g_cam.view_h)
        return 0;
    return r;
}

void cam_tile_to_screen(int x, int y, int *sx, int *sy)
{
    int wx, wy;
    iso_tile_to_world(g_cam.zoom, x, y, &wx, &wy);
    *sx = wx - g_cam.x + g_cam.view_x;
    *sy = wy - g_cam.y + g_cam.view_y;
}
