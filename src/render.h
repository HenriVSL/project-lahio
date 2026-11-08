/* render.h - world chunk cache, painter's order with strip slicing, overlays, dynamic sprites with the
   occlusion fix, picking, minimap image. WINDOWS SIDE. Owner: T05. DESIGN 11.3, 11.4; probe/RESULTS.md 4-8.

   Chunk cache: the static world (ground, roads, power lines, lots, trees and the active overlay) is
   pre-rendered into CHUNK_W x CHUNK_H (256x128) VRAM target images on a fixed grid in world-pixel space
   (iso.h) of the current zoom. Pool = visible chunks + one margin ring, LRU. Missing/dirty chunks are
   rendered in render_update() within a per-frame budget (~3 ms), nearest the camera first, prefetching in
   the scroll direction. Zoom or overlay change flushes the pool (one slow frame). Surface loss (gfx
   restore serial) flushes too.

   Dirty updates: render_init registers a world listener (world.h). A change rect in tiles is converted
   with iso_tile_rect_bounds (footprint diamonds + ISO_MAX_SPRITE_UP) and every chunk it touches is
   marked dirty. WCHG_NET / WCHG_FIELDS only dirty chunks while an overlay that shows them is active.

   Per frame (called by game.cpp in this order, DESIGN 11.3 step 4):
     render_update()              repair dirty/missing chunks (PROF_CACHE)
     render_world()               opaque chunk blits into the view (PROF_WORLD)
     render_sprite()/render_tile_mark() ...   submissions from life, tools, ui (floating numbers)
     render_flush_sprites()       sort by depth, blit, occlusion fix (PROF_SPRITES)
   Then ui_draw, then main runs the sim slice, then plat_present. */
#ifndef RENDER_H
#define RENDER_H
#include "common.h"
#include "art.h"
#include "iso.h"

int  render_init(void);                  /* after art_init; pool sized for the screen; 0 = fatal */
void render_shutdown(void);
void render_reset(void);                 /* new map / load: iso_init, flush everything */
void render_set_zoom(int zoom, int sx, int sy);  /* cam_set_zoom + flush; (sx,sy) stays fixed */
void render_set_overlay(int ovl, int param, int show_buildings); /* OVL_*; param = lot for OVL_CATCHMENT;
                                            show_buildings 0 = flat lot_ground sprites (DESIGN 11.4) */
int  render_overlay(void);
void render_set_anim_frame(int frame);   /* water shimmer frame 0..2 (life.cpp drives the timer);
                                            re-renders visible water chunks within the chunk budget */
void render_invalidate_tiles(int x0, int y0, int x1, int y1);   /* inclusive tile rect */
void render_invalidate_all(void);

void render_update(double budget_ms);    /* chunk repair; budget ~3 ms */
void render_world(void);                 /* chunk blits for g_cam into the view rect */

/* ------------------------------------------------------------------ dynamic sprites
   Positions are WORLD pixels at the current zoom (iso.h) of the sprite's anchor. depth_x/y = the tile
   the sprite stands on (cars: tile under the centre): sprites sort with the world by (x + y) and get
   the occlusion fix (buildings in front redrawn clipped to the sprite, ~2 blits). Max RENDER_MAX_SPRITES
   per frame; extra submissions are dropped and counted. */
#define RENDER_MAX_SPRITES 512
enum { RS_NO_OCCLUDE = 1,    /* draw on top of everything (cursor, preview, floating numbers) */
       RS_HALF       = 2 };  /* 50% stipple (ghost preview of a building) */
void render_sprite(int art_id, int wx, int wy, int depth_x, int depth_y, int flags);
/* One-tile diamond marks for cursors and drag previews (art cur_ok/cur_bad/zone_<z>/...). Drawn with
   RS_NO_OCCLUDE above the world. */
void render_tile_mark(int art_id, int x, int y);
/* Ghost of a lot (building placement preview) at its NORTH tile, art_lot_sprite id. */
void render_lot_ghost(int art_id, int x, int y, int w, int h);
void render_flush_sprites(void);

/* ------------------------------------------------------------------ picking */
int  render_pick_tile(int sx, int sy, int *x, int *y);   /* ground plane (cam_pick) */
int  render_pick_lot(int sx, int sy);    /* lot whose sprite pixel is under the cursor (front-most,
                                            tall parts included), else the lot on the picked tile, 0 */

/* ------------------------------------------------------------------ minimap (ui draws it)
   1 px per tile of the CURRENT overlay (terrain colours if none) into a PIX buffer (software, e.g. the
   px of a gfx art image that ui uploads). Cheap enough to call once per second when g_world.serial changed. */
void render_minimap(PIX *dst, int pitch, int w, int h, int ovl);

/* ------------------------------------------------------------------ stats and debugging */
typedef struct {
    int chunks_pool, chunks_valid, chunks_dirty;
    int chunks_rendered;       /* this frame */
    double chunk_ms;           /* this frame */
    int sprites, sprites_dropped, occlusion_blits;
} RenderStats;
void render_stats(RenderStats *out);
/* Text dump of what is under a screen pixel: chunk index/state, tile, lot, sprites drawn in that strip
   with their depth (DEBUG FROM TEXT for sorting glitches). */
void render_dump_at(FILE *f, int sx, int sy);
int  render_selftest(void);              /* -test render: projection/picking round trips; 0 pass */

/* ------------------------------------------------------------------ T05 additions */
/* Minimap mapping for ui (render_minimap draws the map as a 45-degree diamond filling w x h; corners are
   background): minimap pixel -> tile (may be off the map) and tile -> minimap pixel (tile centre). */
void render_minimap_tile(int w, int h, int px, int py, int *tx, int *ty);
/* Rows y0..y1-1 only, so ui can spread a refresh over frames: a full 128x128 image costs ~10 ms on the
   P2-233 (render_minimap = all rows); 16 rows per frame keeps it near 1 ms. */
void render_minimap_rows(PIX *dst, int pitch, int w, int h, int ovl, int y0, int y1);
/* 1.1 B1: the minimap colours after a season swap, ~6 ms of work per call; 1 = ready (ui.cpp waits for it before the rows) */
int  render_minimap_prepare(void);

/* ------------------------------------------------------------------ T13 additions (bench report) */
enum { RR_VISIBLE, RR_DIRTY, RR_OVERLAY, RR_PREFETCH, RR_ANIM, RR_COUNT };   /* why a chunk was rendered */
typedef struct {
    u32 frames, chunks[RR_COUNT], sprites, occlusion_blits, ovl_checks;
    double chunk_ms[RR_COUNT], chunk_ms_max;      /* chunk_ms_max = slowest single chunk render */
    double frame_chunk_ms_max;                    /* most chunk-render ms in one render_update */
} RenderTotals;
void render_totals(RenderTotals *out, int reset);
void render_minimap_pos(int w, int h, int tx, int ty, int *px, int *py);
/* -bench: scripted scroll path (Lissajous over the town, ~330 px/s peak, same screen speed at both zooms);
   call once per frame with the run time in seconds (game_bench_camera can forward to it). */
void render_bench_camera(double t);
/* A believable test town on a fresh 128x128 world (all road classes, house streets, apartments, shops,
   industry, plant + power line, services, lake + shore, strait with bridges, forest, rock, fields,
   abandoned and under-construction lots, zoned empty land), then sim_new_city + net/fields so overlays
   have data. Needs world_init/sim_init (and the other sim module inits) done. */
void render_build_test_town(u32 seed);
/* Art id the chunk cache draws for a lot (status/variant/rotation fallbacks applied): life.cpp reads the
   chimney hotspot from art_get(zoom, id)->hx/hy; ART_MISSING for no lot. */
int  render_lot_art(int lot);

/* ------------------------------------------------------------------ F3 additions
   Persistent zone view (DESIGN 14.4, ART.md 6 front-edge rule): zone boundaries drawn with zonev_<Z>_<edge>
   pieces in the object pass, baked into the chunks; toggling flushes the pool. */
void render_set_zone_view(int on);
int  render_zone_view(void);

#endif
