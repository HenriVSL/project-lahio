/* plat.h - window, display backend, present + frame pacing, input. WINDOWS ONLY. Owner: T01.
   Port of SIROCCO's plat.cpp/input.cpp (../sandline/src) to KAUPUNKI's modes. DESIGN 1, 11.1-11.3, 11.5.

   Backends (chosen in plat_init):
   - PB_DD_FULL:   DirectDraw exclusive fullscreen WxHx16 (default 800x600), primary + 1 back buffer, Flip.
   - PB_DD_WINDOW: DirectDraw windowed (-window): VRAM back buffer, Blt to the primary through a clipper;
                   needs a 16-bit desktop, else falls back to PB_GDI.
   - PB_GDI:       software fallback (-gdi): 16-bit DIB, BitBlt.
   Only plat.cpp and gfx.cpp know DirectDraw; everyone else draws through gfx.h.

   Frame pacing (DESIGN 1, 11.3.6): plat_present() shows the frame and then waits for the next frame
   deadline: 60 fps by default (Flip on every 2nd blank of the 120 Hz display), 30 fps when
   plat_set_fps(30) or when the automatic fallback is active (fps=auto: frames overrunning for a while
   drop to 30, hysteresis back to 60). Never free-run (86Box stalls up to 1 s).
   The time between "all blits issued" and plat_present() is where main.cpp runs the simulation slice:
   plat_frame_budget_ms() says how much CPU time is left before the deadline.

   T01 implementation notes (plat.cpp header has the details):
   - Display-locked pacing: at init the refresh is measured with IDirectDraw::WaitForVerticalBlank over
     16 blanks. If it is a multiple of 60 Hz (within 3%) each frame is shown for refresh/60 blanks (VM:
     2 at 60 fps, 4 at 30 fps): present sleeps until ~3.5 ms before the target blank, waits for it, then
     Flips (fullscreen) or Blts (windowed). Otherwise (-novsync, GDI, odd refresh) a QPC timer paces.
     plat_frame_budget_ms() ends 1.0 ms (vsync) / 0.5 ms (timer) before the deadline.
   - Auto 30 fps (fps=auto): >= 6 missed HEAVY frames (work + flip > 50% of the budget; misses from
     host hiccups don't count) in the last 60 drop to 30; back to 60 once the last
     90 frames all had work (frame start -> present call) <= 75% of the 60-fps budget, but not before a
     dwell time (3 s, doubled up to 30 s when it bounces within 10 s).
   - Lost surfaces (alt-tab, minimize, mode change) are restored in plat_pump once the app is active
     again: primary/back first, then gfx_restore_all() (art re-uploaded from img.px, render targets
     keep garbage and gfx_restore_serial() increments). */
#ifndef PLAT_H
#define PLAT_H
#include "common.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#define PF_565 0
#define PF_555 1
enum { PB_NONE, PB_DD_FULL, PB_DD_WINDOW, PB_GDI };

int    plat_init(HINSTANCE inst, int res, int fullscreen, int gdi); /* res = RES_*; 0 on failure */
void   plat_shutdown(void);                /* restores the desktop mode */
HWND   plat_hwnd(void);
int    plat_backend(void);                 /* PB_* */
const char *plat_backend_name(void);       /* e.g. "ddraw fullscreen 800x600 565" (logs, bench.txt) */
int    plat_pixel_format(void);            /* PF_565 / PF_555 */
int    plat_screen_w(void);                /* current mode, e.g. 800 */
int    plat_screen_h(void);
int    plat_pump(void);                    /* handle messages; 0 = quit requested */
void   plat_quit(void);                    /* the next plat_pump returns 0 */
int    plat_active(void);                  /* foreground and not minimized */
int    plat_visible(void);                 /* worth drawing (surfaces ok, not minimized) */

void   plat_present(void);                 /* show the frame, then wait for the frame deadline */
void   plat_set_fps(int fps);              /* 60, 30, or 0 = auto (default from config) */
int    plat_fps_target(void);              /* currently active target: 60 or 30 */
double plat_refresh_hz(void);              /* measured display refresh (VM: ~119.96) */
double plat_frame_budget_ms(void);         /* ms left until the next present deadline (>= 0) */
double plat_seconds(void);                 /* QPC seconds since start */

/* Frame pacing statistics (bench.txt, debug overlay). Counters run since plat_init. */
typedef struct {
    int    vsync_blanks;       /* blanks per frame at the current target, 0 = timer pacing */
    int    frames;             /* presents */
    int    missed;             /* frames that missed their blank/deadline (shown longer than planned) */
    int    missed_blanks;      /* vsync: extra blanks waited in total */
    int    drops, raises;      /* automatic 60 -> 30 and 30 -> 60 switches */
    double last_work_ms;       /* previous frame: frame start -> plat_present call */
    double last_wait_ms;       /* previous frame: time spent sleeping/waiting for the blank */
    double last_flip_ms;       /* previous frame: Flip/Blt/BitBlt call time */
    double last_switch_s;      /* plat_seconds() of the last automatic switch (0 = none) */
} PlatStats;
void        plat_get_stats(PlatStats *st);
const char *plat_pacing_name(void);        /* e.g. "vsync 119.96 Hz, 2 blanks/frame" or "timer 60 fps" */
int         plat_fps_policy(void);         /* 60, 30 or 0 = auto */

/* ---- for gfx.cpp only */
void   plat_note_lost(void);               /* a blit/lock returned DDERR_SURFACELOST: restore at the next pump */
void   plat_hide_os_cursor(int hide);      /* windowed: hide the Windows cursor over the client area */
void  *plat_dd(void);                      /* LPDIRECTDRAW or NULL */
void  *plat_dd_back(void);                 /* LPDIRECTDRAWSURFACE drawn into each frame, or NULL */
PIX   *plat_gdi_bits(void);                /* PB_GDI: DIB pixels (pitch = screen w), else NULL */

/* ================================================================== input
   input_poll() once per frame (main.cpp, before game_update). Everything below describes the state at
   the last poll. Mouse coordinates are screen pixels. Keys are Windows virtual-key codes (VK_*, 'A'..'Z',
   '0'..'9'). All keys read as up while the window is inactive. */
enum { MOUSE_L = 1, MOUSE_R = 2, MOUSE_M = 4 };

typedef struct {
    int  mx, my;               /* mouse position (clamped to the screen) */
    int  inside;               /* mouse is inside the client area */
    int  buttons;              /* MOUSE_* held now */
    int  pressed, released;    /* MOUSE_* edges since the previous poll */
    int  dbl;                  /* MOUSE_* double clicks since the previous poll */
    int  wheel;                /* wheel notches since the previous poll (+ = away from the user) */
    int  drag_x0, drag_y0;     /* position where the current left/middle press started */
    int  shift, ctrl, alt;     /* modifier state */
    char text[16];             /* typed characters since the previous poll (WM_CHAR, Latin-1, NUL-terminated;
                                  backspace = '\b', enter = '\r') for text fields (president/town name) */
} InputState;

void  input_poll(void);
const InputState *input_get(void);
int   key_down(int vk);                    /* held now */
int   key_pressed(int vk);                 /* went down since the previous poll, incl. auto-repeat */
int   key_hit(int vk);                     /* went down since the previous poll, first press only */
void  input_clear(void);                   /* forget edges/held state (after mode switches, focus loss) */
/* Edge-scroll helper: direction (-1/0/+1 per axis) when the mouse is within `margin` px of a screen edge
   and the window is active; 0,0 otherwise. */
void  input_edge_scroll(int margin, int *dx, int *dy);

#endif
