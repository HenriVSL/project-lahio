/* iso.h - isometric projection, world-pixel space, screen<->tile picking, the camera. PORTABLE (pure
   integer math; tools tests may use it). Owner: T05. DESIGN 11.2, 11.3.7; STYLE.md 3.

   World-pixel space (per zoom): the whole map drawn at that zoom, with every coordinate >= 0.
     north vertex of tile (x, y):  wx = (x - y) * TW/2 + iso_origin_x(zoom)
                                   wy = (x + y) * TH/2 + ISO_TOP_MARGIN(zoom)
     TW = TILE_W(zoom), TH = TILE_H(zoom); south vertex = north + (0, TH).
   iso_origin_x = g_world.h * TW/2 (so the left vertex of tile (0, h-1) is at wx = 0). ISO_TOP_MARGIN leaves room for the
   tallest sprites of the top row. Chunks (render.h) are a fixed CHUNK_W x CHUNK_H grid in this space.
   Screen = world - camera (camera = world pixel at the view's top-left, WHOLE pixels, CLAUDE.md lesson:
   floor once per frame so nothing jiggles). */
#ifndef ISO_H
#define ISO_H
#include "common.h"

#define ISO_TOP_MARGIN(z)   (256 >> (z))   /* >= tallest sprite above its tile's north vertex */
#define ISO_MAX_SPRITE_UP(z) (256 >> (z))  /* invalidation: sprites reach at most this far above the
                                              north vertex of their back tile */

void iso_init(int map_w, int map_h);         /* after world_new/load (origin depends on the map size) */
int  iso_origin_x(int zoom);
void iso_world_size(int zoom, int *w, int *h);              /* world-pixel extent incl. margins */
void iso_tile_to_world(int zoom, int x, int y, int *wx, int *wy);   /* north vertex */
/* Sub-tile position (units of 1/256 tile, for cars and smoke) -> world pixel. */
void iso_pos_to_world(int zoom, s32 fx, s32 fy, int *wx, int *wy);
/* World pixel -> tile under it (diamond-exact, ground plane). Returns 0 if off the map (x, y still set). */
int  iso_world_to_tile(int zoom, int wx, int wy, int *x, int *y);
/* Screen rect covered by a tile rect's ground diamonds, extended up by ISO_MAX_SPRITE_UP (invalidation). */
void iso_tile_rect_bounds(int zoom, int x0, int y0, int x1, int y1, int *wx0, int *wy0, int *wx1, int *wy1);

/* ------------------------------------------------------------------ camera */
typedef struct {
    int zoom;                  /* ZOOM_* */
    int x, y;                  /* world pixel at the view's top-left (whole pixels) */
    int view_x, view_y;        /* view rect on screen (map area, excludes UI panels) */
    int view_w, view_h;
    s32 fx, fy;                /* sub-pixel position x256 (smooth scrolling); x,y = floor(f / 256) */
    s32 vx, vy;                /* velocity x256 px per frame (eased scrolling, keys/edge) */
} Camera;
extern Camera g_cam;

void cam_set_view(int x, int y, int w, int h);
void cam_center_on_tile(int x, int y);
void cam_center_tile(int *x, int *y);        /* tile at the view centre */
void cam_scroll(int dx, int dy);             /* immediate, px */
void cam_push(int dx, int dy);               /* accelerate (keys/edge scroll); call per frame */
void cam_update(void);                       /* per frame: apply velocity, friction, clamp, floor */
void cam_set_zoom(int zoom, int sx, int sy); /* keep the world point under screen (sx, sy) fixed */
int  cam_pick(int sx, int sy, int *x, int *y);   /* screen -> tile (ground plane); 0 if off map/view */
void cam_tile_to_screen(int x, int y, int *sx, int *sy);   /* north vertex on screen */

#endif
