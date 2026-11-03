/* gfx.h - blit-oriented 2D drawing: images, VRAM render targets, fills, bitmap text, BMP load/save.
   WINDOWS SIDE (header itself has no windows.h). Owner: T01. Port of SIROCCO gfx.cpp. DESIGN 11.1-11.4,
   probe/RESULTS.md.

   Image kinds:
   - ART image (gfx_load_bmp / gfx_make_img): system-memory pixels img.px plus ONE VRAM surface used for
     hardware blits. Max width = the screen width (art sheets: ATLAS_MAX_W = 640, so 640x480 works; a
     wider art image logs a warning), any height (probe: 4096 tall works). Created and uploaded at load
     time, so a full VRAM is detected at load. ALL art must sit in VRAM: keyed blits from system memory
     are 8x slower and can stall (probe 2); if VRAM runs out gfx logs "gfx: error: VRAM full ..." and the
     call returns 0 (no silent system-memory fallback). Edit img.px only at load time, then call
     gfx_img_update (slow bus copy). gfx_set_target(&artImg) draws into px in SOFTWARE (load-time
     composition; uploaded when the target changes or before the next blit from it).
   - TARGET image (gfx_make_target): VRAM-only surface that hardware blits draw INTO (render chunk cache
     256x128, UI cache panels, minimap). img.px is NULL on the DirectDraw backends (with -gdi targets are
     system-memory images). Contents are LOST when DirectDraw loses surfaces (alt-tab, minimize, mode
     change): gfx restores the surface memory and increments gfx_restore_serial(); owners (render, ui)
     compare the serial each frame and re-render everything. Created cleared to black, opaque; call
     gfx_img_set_keyed(&t, 1) for a target that is blitted with transparency (clear it to gfx_key()).
   With the GDI backend all images live in system memory and blits are software (slow, but correct).
   Never copy an Img by value (it is linked into the restore registry); keep it in a static/struct.

   Drawing goes to the current target: the screen back buffer (gfx_set_target(NULL), default) or an
   image. All blit/fill calls subtract the origin (gfx_set_origin) and clip to the clip rect (gfx_set_clip,
   default = whole target). Rect coordinates x1,y1 are exclusive. gfx_set_target resets clip and origin.
   Blitting an image into itself is ignored. A TARGET (no px) can be blitted to the screen or another
   target, not into a software target.

   Cost model (probe 2): ~5 us CPU per blit call + ~8 ns/px on the emulated blitter, which runs in parallel
   with the CPU. Budget ~300 dynamic blits per frame on top of the chunk layer. Software pixel drawing on a
   hardware target (gfx_pixel/line, gfx_peek) locks that surface and waits for the blitter: avoid per
   frame, use only for debug overlays and tests. gfx_fill_half is a keyed 50% stipple blit on hardware.
   Colour key: pixels == gfx_key() (RGB 255,0,255) are transparent in keyed images. 24-bit art pixels
   that are not exactly the key but convert to the key's 16-bit value are nudged by one bit. */
#ifndef GFX_H
#define GFX_H
#include "common.h"

typedef struct Img {
    int   w, h;
    int   keyed;           /* 1: gfx_key() pixels are transparent when blitting FROM this image */
    int   target;          /* 1: render target (px == NULL on the DirectDraw backends) */
    PIX  *px;              /* system-memory pixels, pitch = w (art images); NULL for hw targets */
    /* ---- internal (gfx.cpp only) */
    void *surf;            /* LPDIRECTDRAWSURFACE (NULL in software) */
    int   dirty;           /* px changed since the last upload */
    int   vram_kb;         /* accounted VRAM */
    struct Img *next, *prev; /* registry of live images (surface restore) */
    u16  *span;            /* keyed software blits: opaque runs (x, len), built lazily */
    int  *row;             /* span index per row (h + 1 entries) */
} Img;

int  gfx_init(void);                     /* after plat_init; 0 = failure (logged) */
void gfx_shutdown(void);                 /* releases all surfaces (idempotent; plat_shutdown calls it) */
int  gfx_hw(void);                       /* 1 = DirectDraw hardware blits, 0 = software */
int  gfx_screen_w(void);
int  gfx_screen_h(void);

/* ---- target, clip, origin */
void gfx_set_target(Img *im);            /* NULL = screen back buffer; resets clip and origin */
Img *gfx_target(void);
void gfx_set_clip(int x0, int y0, int x1, int y1); /* in target pixels (origin not applied), clamped */
void gfx_reset_clip(void);
void gfx_set_origin(int ox, int oy);     /* drawing at (x,y) lands at (x - ox, y - oy) */
void gfx_get_clip(int *x0, int *y0, int *x1, int *y1);
void gfx_get_origin(int *ox, int *oy);

/* ---- colours (screen layout: RGB565 or RGB555) */
PIX  gfx_rgb(int r, int g, int b);
void gfx_unpack(PIX c, int *r, int *g, int *b);
PIX  gfx_mix(PIX a, PIX b, int f256);    /* a*(256-f)/256 + b*f/256 */
PIX  gfx_scale(PIX c, int f256);         /* brightness * f256/256, clamped */
PIX  gfx_key(void);

/* ---- fills (ColorFill on hw targets / the screen) */
void gfx_clear(PIX c);                   /* whole target, ignores clip */
void gfx_fill(int x0, int y0, int x1, int y1, PIX c);
void gfx_fill_half(int x0, int y0, int x1, int y1, PIX c); /* 50% stipple (keyed pattern blit) */
void gfx_rect(int x0, int y0, int x1, int y1, PIX c);      /* 1 px outline (4 fills) */
void gfx_pixel(int x, int y, PIX c);     /* software; see cost note */
void gfx_line(int x0, int y0, int x1, int y1, PIX c);

/* ---- images */
int  gfx_load_bmp(const char *path, Img *out, int keyed); /* 24-bit BI_RGB; 1 ok, 0 fail (logged) */
int  gfx_make_img(int w, int h, int keyed, Img *out);     /* art image filled with key (keyed) or black */
int  gfx_make_target(int w, int h, Img *out);             /* VRAM render target; 0 if VRAM is full */
void gfx_img_set_keyed(Img *im, int keyed);               /* change transparency (sets the surface key) */
void gfx_img_update(Img *im);            /* re-upload after editing im->px (slow bus copy: not per frame) */
void gfx_free_img(Img *im);              /* releases VRAM, zeroes the struct */
void gfx_blit(const Img *im, int x, int y);
void gfx_blit_part(const Img *im, int sx, int sy, int w, int h, int dx, int dy); /* the workhorse */
int  gfx_restore_serial(void);           /* increments whenever surfaces were lost and restored */
int  gfx_peek(const Img *im, int x, int y); /* debug/tests: pixel as stored in VRAM (or px in software);
                                               NULL = back buffer; -1 if unreadable. Locks: slow. */

/* ---- bitmap fonts (glyph atlas). The game's fonts come from T03 (ArtFont in art.h): art.cpp/ui.cpp
   fill a GfxFont from the generated glyph table with gfx_font_init + gfx_font_glyph per glyph. Each
   colour variant is its own atlas (body_dark, body_light...), so drawing takes no colour. One keyed
   blit per visible glyph: compose text into UI cache targets, don't redraw it every frame (probe 7). */
typedef struct {
    const Img *img;                      /* keyed glyph atlas (VRAM) */
    int  line_h, baseline;
    s16  gx[256], gy[256];               /* glyph rect in img per byte code */
    u8   gw[256], gh[256];               /* gw == 0: no visible pixels (space) or missing */
    s8   ox[256], oy[256];               /* pen position (top of the line) -> glyph top-left */
    u8   adv[256];                       /* pen advance; 0 with gw 0 = missing (skipped) */
} GfxFont;
void gfx_font_init(GfxFont *f, const Img *atlas, int line_h, int baseline);
void gfx_font_glyph(GfxFont *f, int code, int x, int y, int w, int h, int ox, int oy, int adv);
int  gfx_text(const GfxFont *f, int x, int y, const char *s);  /* (x,y) = top-left of the first line;
                                            '\n' starts a new line; returns the widest line's width */
int  gfx_text_width(const GfxFont *f, const char *s);          /* widest line, no drawing */
int  gfx_text_n(const GfxFont *f, int x, int y, const char *s, int n); /* first n bytes only */

/* ---- debug text (GDI-built Tahoma 11, ASCII 32..126, one cached keyed atlas per colour (up to 8);
   for fps counters and dev overlays only; the game UI uses T03's bitmap fonts) */
int  gfx_debug_text(int x, int y, const char *s, PIX c);  /* returns width */
int  gfx_debug_text_h(void);             /* line height */
const GfxFont *gfx_debug_font(PIX c);    /* the GfxFont behind gfx_debug_text for colour c */

/* ---- mouse cursor (software, drawn by main.cpp as the frame's last blit). Fullscreen always draws it;
   windowed draws it only when a custom image is set (the Windows cursor is hidden then). */
void gfx_cursor_set(const Img *im, int sx, int sy, int w, int h, int hotx, int hoty); /* im NULL = arrow */
void gfx_cursor_show(int on);
void gfx_cursor_draw(int x, int y);      /* screen target; uses the current clip-free screen state */

/* ---- stats and screenshots */
void gfx_stats(int *blits, int *locks);  /* since the previous call (bench) */
int  gfx_vram_free_kb(void);             /* IDirectDraw::GetCaps dwVidMemFree, or -1 */
int  gfx_vram_total_kb(void);            /* dwVidMemTotal, or -1 */
int  gfx_vram_used_kb(void);             /* accounted by gfx (art + targets, incl. gfx's own atlases) */
int  gfx_img_count(int *art, int *targets); /* live images with VRAM surfaces */
int  gfx_vram_failures(void);            /* surface creations refused for lack of VRAM since init */
void gfx_log_vram(const char *when);     /* one log line: used/free, number of images */
int  gfx_save_bmp(const char *path);     /* back buffer -> 24-bit BMP (call before plat_present) */

/* ---- internal hooks used by plat.cpp */
void gfx_unlock(void);                   /* release a software lock (before blits / present) */
int  gfx_restore_all(void);              /* restore lost surfaces, re-upload art, bump the serial if
                                            anything was lost; returns the number of lost surfaces */

/* ---- T13 additions: free / re-create an art image's VRAM copy while keeping its system-memory pixels
   (art residency swap without malloc + two full-sheet copies). An evicted image must not be blitted. */
int  gfx_img_evict(Img *im);             /* 1 ok */
int  gfx_img_restore(Img *im);           /* 1 ok, 0 = VRAM full (logged) */
/* F3 addition: load a BMP into system memory only (the state of an evicted image; gfx_img_restore uploads it) */
int  gfx_load_bmp_sys(const char *path, Img *out, int keyed);

#endif
