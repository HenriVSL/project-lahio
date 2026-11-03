/* main.cpp - WinMain, init order, frame loop, -bench/-shot/-exitafter/-test, T01 test scene. Owner: T01.
   DESIGN 11.1, 11.3 step 4/6, 11.5; ARCH.md 2. Loop structure and bench report modelled on SIROCCO's
   ../sandline/src/main.cpp (SIROCCO T01), rebuilt for a variable-step city builder.

   Per frame (order matters, ARCH.md 2):
       prof_frame_begin; plat_pump; input_poll                      PROF_INPUT
       game_update(dt)                                              PROF_TOOLS
       game_draw()  -> all blits issued                             PROF_CACHE/WORLD/SPRITES/UI inside
       [-shot] gfx_save_bmp; gfx_cursor_draw                         (cursor = last blit)
       game_sim_slice(dt, plat_frame_budget_ms() - 1 ms)            PROF_SIM: the blitter works meanwhile
       plat_present()  -> waits for the 60/30 fps blank, Flips      PROF_PRESENT
       snd_tick(); bench bookkeeping
   main.cpp owns snd_init/snd_tick/snd_shutdown (game.cpp must not call snd_tick again).
   Run time for -shot/-exitafter/-bench = wall seconds since the first frame.

   TEST SCENE (until T08's game.cpp returns 1 from game_init, or always with -test scene): a 128x128
   field of placeholder 64x32 diamonds rendered into 256x128 VRAM chunk targets (LRU pool, like T05's
   cache) and scrolled with the arrow keys / edge scroll (bench: a scripted Lissajous path), plus a text
   panel composed into a VRAM target 5x a second (like T09's caches). Keys: Esc quit, 6/3/A = fps
   60/30/auto, F11 debug overlay, typing goes into the "text:" line (Backspace, Enter clears).
   -direct blits every diamond each frame instead (full-redraw stress); -simload MS adds a busy sim
   slice (with -bench only in the middle third) to show the automatic 60 -> 30 -> 60 switching.

   Self-tests (-test NAME, exit code = failures, -1 = unknown): scene (the scene), restore (scene that
   minimizes + restores itself and verifies the surface restore path), gfx (blits/fills/clip/origin/keys
   into VRAM targets checked by reading VRAM back, VRAM accounting, width limit, GfxFont), vram (fill VRAM
   with 256x128 targets until the loud failure, check accounting and release), art, render, snd. */
#include "common.h"
#include "plat.h"
#include <mmsystem.h>
#include <math.h>
#include "gfx.h"
#include "config.h"
#include "util.h"
#include "game.h"
#include "snd.h"
#include "art.h"
#include "render.h"
#include "ui.h"
#include "life.h"
#include "menu.h"
#include "save.h"

static double s_t0;               /* plat_seconds at the first frame */
static int    s_scene;            /* 1: T01 test scene instead of the game */
static int    s_restoreTest;
static int    s_exitCode;
static int    s_frameNo = 1;     /* starts at 1: pool slots with last == 0 are free */

/* fps counter (1 s windows) */
static double s_fpsT0;
static int    s_fpsFrames;
static double s_fps;

/* ================================================================== test helpers */
static int s_fails, s_checks;

static void check(int ok, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    str_vprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    s_checks++;
    if (!ok) s_fails++;
    log_printf("test: %s %s", ok ? "PASS" : "FAIL", buf);
}

/* ================================================================== test scene */
#define SC_MAP      128
#define SC_OX       (SC_MAP * 32)          /* world x of the map's top vertex */
#define SC_KINDS    5
#define SC_MAXPOOL  96
typedef struct { int used, cx, cy; int last; Img img; } ScChunk;
static ScChunk s_pool[SC_MAXPOOL];
static int     s_npool, s_scReady;
static Img     s_tiles, s_panel;
static double  s_camFx, s_camFy;
static int     s_camX, s_camY;
static int     s_visChunks, s_renders, s_rendersTotal, s_rendersAfterRestore;
static double  s_renderMs;
static double  s_panelT;
static int     s_panelAfterRestore;
static int     s_serialSeen;
static char    s_typed[48];
static int     s_simLoadOn;
static PIX     s_bg, s_white, s_yellow, s_panelBg;

static int sc_kind(int i, int j)
{
    int dx = i - 44, dy = j - 70, ex = i - 96, ey = j - 30;
    if (dx * dx + 2 * dy * dy < 260 || ex * ex + ey * ey < 120) return 3;   /* lakes */
    if ((i & 15) == 8 || (j & 15) == 8) return 4;                           /* road grid */
    return (int)(hash_xy(i, j, 7) % 3);
}

static void sc_make_tiles(void)
{
    static const int col[SC_KINDS][3] = { { 104, 122, 78 }, { 92, 112, 70 }, { 118, 128, 84 }, { 70, 98, 120 }, { 120, 116, 108 } };
    int k, x, y;
    if (!gfx_make_img(64 * SC_KINDS, 32, 1, &s_tiles)) return;
    for (k = 0; k < SC_KINDS; k++) for (y = 0; y < 32; y++) {
        int hw = y < 16 ? 2 * y + 2 : 2 * (31 - y) + 2, xs = 32 - hw, xe = 32 + hw;
        for (x = xs; x < xe; x++) {
            int n = (int)(hash_xy(x + k * 64, y, 3) & 15) - 8, edge = (x == xs || x == xe - 1) ? -18 : 0;
            s_tiles.px[y * s_tiles.w + k * 64 + x] = gfx_rgb(col[k][0] + n + edge, col[k][1] + n + edge, col[k][2] + n + edge);
        }
    }
    gfx_img_update(&s_tiles);
}

static void sc_init(void)
{
    int sw = plat_screen_w(), sh = plat_screen_h(), want, i;
    s_bg = gfx_rgb(28, 32, 38); s_white = gfx_rgb(236, 232, 220); s_yellow = gfx_rgb(250, 226, 140);
    s_panelBg = gfx_rgb(20, 22, 26);
    sc_make_tiles();
    if (!g_cfg.test_direct) {
        want = (sw / CHUNK_W + 2) * (sh / CHUNK_H + 2) + 8;
        if (want > SC_MAXPOOL) want = SC_MAXPOOL;
        for (s_npool = 0; s_npool < want; s_npool++) {
            memset(&s_pool[s_npool], 0, sizeof(ScChunk));
            if (!gfx_make_target(CHUNK_W, CHUNK_H, &s_pool[s_npool].img)) break;
        }
        log_printf("scene: chunk pool %d of %d targets %dx%d (%d KB)", s_npool, want, CHUNK_W, CHUNK_H, s_npool * CHUNK_W * CHUNK_H * 2 / 1024);
    }
    gfx_make_target(IMIN(560, sw - 16), 7 * (gfx_debug_text_h() + 1) + 8, &s_panel);
    for (i = 0; i < 4; i++) gfx_debug_font(i == 0 ? s_white : i == 1 ? s_yellow : gfx_rgb(160, 220, 160));
    s_camFx = SC_MAP * 32 - sw / 2; s_camFy = SC_MAP * 16 - sh / 2;
    s_serialSeen = gfx_restore_serial();
    s_panelT = -1;
    s_scReady = 1;
    gfx_log_vram("scene ready");
}

static void sc_shutdown(void)
{
    int i;
    for (i = 0; i < s_npool; i++) gfx_free_img(&s_pool[i].img);
    s_npool = 0;
    gfx_free_img(&s_panel);
    gfx_free_img(&s_tiles);
}

/* draw every diamond overlapping the world rect [wx0,wx1) x [wy0,wy1) at world coordinates */
static void sc_draw_tiles(int wx0, int wy0, int wx1, int wy1)
{
    int s, d, s0 = FLOORDIV(wy0 - 31, 16), s1 = FLOORDIV(wy1 - 1, 16);
    int d0 = FLOORDIV(wx0 - SC_OX - 31, 32), d1 = FLOORDIV(wx1 - 1 - SC_OX + 32, 32);
    for (s = s0; s <= s1; s++) {
        if (s < 0 || s > 2 * (SC_MAP - 1)) continue;
        for (d = d0; d <= d1; d++) {
            int i, j;
            if ((s + d) & 1) continue;
            i = (s + d) / 2; j = (s - d) / 2;
            if (i < 0 || j < 0 || i >= SC_MAP || j >= SC_MAP) continue;
            gfx_blit_part(&s_tiles, sc_kind(i, j) * 64, 0, 64, 32, d * 32 + SC_OX - 32, s * 16);
        }
    }
}

static void sc_render_chunk(ScChunk *c)
{
    double t0 = time_ms();
    gfx_set_target(&c->img);
    gfx_clear(s_bg);
    gfx_set_origin(c->cx * CHUNK_W, c->cy * CHUNK_H);
    sc_draw_tiles(c->cx * CHUNK_W, c->cy * CHUNK_H, c->cx * CHUNK_W + CHUNK_W, c->cy * CHUNK_H + CHUNK_H);
    gfx_set_target(NULL);
    s_renderMs += time_ms() - t0;
    s_renders++; s_rendersTotal++;
}

static void sc_compose_panel(void)
{
    const InputState *in = input_get();
    PlatStats ps;
    char l[7][160];
    int i, a, t, lh = gfx_debug_text_h() + 1;
    plat_get_stats(&ps);
    gfx_img_count(&a, &t);
    str_printf(l[0], 160, GAME_TITLE_ASCII " T01 engine test scene   arrows/edge: scroll  6/3/A: fps  F11  Esc: quit");
    str_printf(l[1], 160, "%s | %s", plat_backend_name(), plat_pacing_name());
    str_printf(l[2], 160, "fps %.1f  target %d (%s)  work %.2f ms  wait %.2f  flip %.2f  missed %d  drops %d raises %d",
               s_fps, plat_fps_target(), plat_fps_policy() ? "fixed" : "auto", ps.last_work_ms, ps.last_wait_ms,
               ps.last_flip_ms, ps.missed, ps.drops, ps.raises);
    str_printf(l[3], 160, "cam %d,%d  %s  %d visible chunks, %d renders (%.2f ms each), pool %d  simload %d ms%s",
               s_camX, s_camY, g_cfg.test_direct ? "DIRECT" : "chunked", s_visChunks, s_rendersTotal,
               s_rendersTotal ? s_renderMs / s_rendersTotal : 0.0, s_npool, g_cfg.simload_ms, s_simLoadOn ? " ON" : "");
    str_printf(l[4], 160, "VRAM gfx %d KB  free %d KB  images %d art + %d targets  refused %d  restore serial %d",
               gfx_vram_used_kb(), gfx_vram_free_kb(), a, t, gfx_vram_failures(), gfx_restore_serial());
    str_printf(l[5], 160, "mouse %d,%d %s  btn %c%c%c  wheel  drag from %d,%d  mods %s%s%s",
               in->mx, in->my, in->inside ? "in" : "out", in->buttons & MOUSE_L ? 'L' : '-', in->buttons & MOUSE_M ? 'M' : '-',
               in->buttons & MOUSE_R ? 'R' : '-', in->drag_x0, in->drag_y0, in->shift ? "S" : "", in->ctrl ? "C" : "", in->alt ? "A" : "");
    str_printf(l[6], 160, "text: %s_", s_typed);
    gfx_set_target(&s_panel);
    gfx_clear(s_panelBg);
    gfx_rect(0, 0, s_panel.w, s_panel.h, gfx_rgb(90, 80, 60));
    for (i = 0; i < 7; i++) gfx_debug_text(6, 4 + i * lh, l[i], i == 0 ? s_yellow : s_white);
    gfx_set_target(NULL);
    if (s_restoreTest && gfx_restore_serial() > 0) s_panelAfterRestore++;
}

static void sc_update(double dt, double runT)
{
    const InputState *in = input_get();
    int dx = 0, dy = 0, ex, ey, i, sw = plat_screen_w(), sh = plat_screen_h();
    double speed = g_cfg.scroll_speed * 60.0 * (in->shift ? 2.0 : 1.0);
    if (log_enabled()) {                                  /* input events (event lines, not per frame) */
        int vk;
        for (vk = 1; vk < 256; vk++) if (key_pressed(vk))
            log_printf("input: key 0x%02x %s%s%s%s", vk, key_hit(vk) ? "hit" : "repeat", in->shift ? " shift" : "",
                       in->ctrl ? " ctrl" : "", in->alt ? " alt" : "");
        if (in->pressed || in->released || in->dbl || in->wheel)
            log_printf("input: mouse %d,%d %s pressed %d released %d dbl %d held %d wheel %d drag from %d,%d",
                       in->mx, in->my, in->inside ? "in" : "out", in->pressed, in->released, in->dbl, in->buttons,
                       in->wheel, in->drag_x0, in->drag_y0);
        if (in->text[0]) {
            char hex[64];
            int k, n = 0;
            for (k = 0; in->text[k] && n < 60; k++) n += str_printf(hex + n, 64 - n, "%02x ", (u8)in->text[k]);
            log_printf("input: text \"%s\" (%s)", in->text, hex);
        }
    }
    if (key_hit(VK_ESCAPE)) plat_quit();
    if (key_hit('6')) plat_set_fps(60);
    if (key_hit('3')) plat_set_fps(30);
    if (key_hit('A')) plat_set_fps(0);
    if (key_hit(VK_F11)) g_cfg.show_fps = !g_cfg.show_fps;
    for (i = 0; in->text[i]; i++) {
        int n = (int)strlen(s_typed);
        char c = in->text[i];
        if (c == '\b') { if (n) s_typed[n - 1] = 0; }
        else if (c == '\r') s_typed[0] = 0;
        else if (n < (int)sizeof(s_typed) - 1) { s_typed[n] = c; s_typed[n + 1] = 0; }
    }
    if (g_cfg.bench) {
        double t = runT;
        s_camFx = SC_MAP * 32 - sw / 2 + 1500.0 * sin(t * 0.31);
        s_camFy = SC_MAP * 16 - sh / 2 + 700.0 * sin(t * 0.43);
    } else {
        if (key_down(VK_LEFT)) dx--;
        if (key_down(VK_RIGHT)) dx++;
        if (key_down(VK_UP)) dy--;
        if (key_down(VK_DOWN)) dy++;
        if (g_cfg.edge_scroll) { input_edge_scroll(4, &ex, &ey); dx += ex; dy += ey; }
        dx = ICLAMP(dx, -1, 1); dy = ICLAMP(dy, -1, 1);
        s_camFx += dx * speed * dt;
        s_camFy += dy * speed * dt;
    }
    if (s_camFx < -256) s_camFx = -256;
    if (s_camFy < -256) s_camFy = -256;
    if (s_camFx > SC_MAP * 64 - sw + 256) s_camFx = SC_MAP * 64 - sw + 256;
    if (s_camFy > SC_MAP * 32 - sh + 256) s_camFy = SC_MAP * 32 - sh + 256;
    s_camX = (int)floor(s_camFx); s_camY = (int)floor(s_camFy);   /* whole-pixel camera, floored once */
}

static void sc_draw(void)
{
    int sw = plat_screen_w(), sh = plat_screen_h(), cx, cy, i, cx0, cx1, cy0, cy1;
    double now = plat_seconds();
    const InputState *in = input_get();
    if (gfx_restore_serial() != s_serialSeen) {          /* surfaces lost: every target is garbage */
        for (i = 0; i < s_npool; i++) s_pool[i].used = 0;
        s_serialSeen = gfx_restore_serial();
        s_panelT = -1;
        log_printf("scene: restore serial %d: chunk pool flushed, panel recomposed", s_serialSeen);
    }
    gfx_set_target(NULL);
    gfx_set_origin(s_camX, s_camY);
    cx0 = FLOORDIV(s_camX, CHUNK_W); cx1 = FLOORDIV(s_camX + sw - 1, CHUNK_W);
    cy0 = FLOORDIV(s_camY, CHUNK_H); cy1 = FLOORDIV(s_camY + sh - 1, CHUNK_H);
    s_visChunks = (cx1 - cx0 + 1) * (cy1 - cy0 + 1);
    if (g_cfg.test_direct || !s_npool) {
        prof_begin(PROF_WORLD);
        gfx_clear(s_bg);
        sc_draw_tiles(s_camX, s_camY, s_camX + sw, s_camY + sh);
        prof_end(PROF_WORLD);
    } else {
        /* cache repair: render every missing visible chunk into the least recently used slot */
        prof_begin(PROF_CACHE);
        for (cy = cy0; cy <= cy1; cy++) for (cx = cx0; cx <= cx1; cx++) {
            int best = -1, found = -1;
            for (i = 0; i < s_npool; i++) {
                ScChunk *c = &s_pool[i];
                if (c->used && c->cx == cx && c->cy == cy) { found = i; break; }
                if (c->last != s_frameNo && (best < 0 || !c->used || (s_pool[best].used && c->last < s_pool[best].last))) best = i;
            }
            if (found < 0 && best >= 0) {
                ScChunk *c = &s_pool[best];
                c->used = 1; c->cx = cx; c->cy = cy;
                sc_render_chunk(c);
                if (s_restoreTest && gfx_restore_serial() > 0) s_rendersAfterRestore++;
                found = best;
            }
            if (found >= 0) s_pool[found].last = s_frameNo;
        }
        gfx_set_target(NULL);
        gfx_set_origin(s_camX, s_camY);
        prof_end(PROF_CACHE);
        prof_begin(PROF_WORLD);
        for (cy = cy0; cy <= cy1; cy++) for (cx = cx0; cx <= cx1; cx++) {
            for (i = 0; i < s_npool; i++) if (s_pool[i].used && s_pool[i].cx == cx && s_pool[i].cy == cy) {
                gfx_blit(&s_pool[i].img, cx * CHUNK_W, cy * CHUNK_H);
                break;
            }
        }
        prof_end(PROF_WORLD);
    }
    gfx_set_origin(0, 0);
    prof_begin(PROF_SPRITES);
    if (in->buttons & MOUSE_L) {                          /* drag rectangle (input test) */
        gfx_rect(IMIN(in->drag_x0, in->mx), IMIN(in->drag_y0, in->my), IMAX(in->drag_x0, in->mx) + 1,
                 IMAX(in->drag_y0, in->my) + 1, s_yellow);
    }
    prof_end(PROF_SPRITES);
    prof_begin(PROF_UI);
    if (s_panelT < 0 || now - s_panelT >= 0.2) { sc_compose_panel(); s_panelT = now; }
    gfx_blit(&s_panel, 8, 8);
    prof_end(PROF_UI);
}

static void sc_sim(double budget_ms, double runT)
{
    double t0;
    int on = g_cfg.simload_ms > 0;
    UNUSED(budget_ms);
    if (on && g_cfg.bench) on = runT >= 1.0 + g_cfg.bench / 3.0 && runT < 1.0 + 2.0 * g_cfg.bench / 3.0;
    if (on != s_simLoadOn) log_printf("scene: synthetic sim load %d ms %s at %.2f s", g_cfg.simload_ms, on ? "on" : "off", runT);
    s_simLoadOn = on;
    if (!on) return;
    t0 = time_ms();                     /* like a sim step that must run whatever the budget */
    while (time_ms() - t0 < g_cfg.simload_ms) {}
}

/* -test restore: minimize at 1.5 s, restore at 3 s, verify at 5 s */
static void restore_script(double runT)
{
    static int phase, serial0;
    int i, bad = 0;
    if (phase == 0 && runT >= 1.5) {
        serial0 = gfx_restore_serial();
        log_printf("test: restore: minimizing the window (serial %d, backend %s)", serial0, plat_backend_name());
        ShowWindow(plat_hwnd(), SW_MINIMIZE);
        phase = 1;
    } else if (phase == 1 && runT >= 3.0) {
        log_printf("test: restore: restoring the window");
        ShowWindow(plat_hwnd(), SW_RESTORE);
        SetForegroundWindow(plat_hwnd());
        phase = 2;
    } else if (phase == 2 && runT >= 5.0) {
        check(gfx_restore_serial() > serial0, "surfaces were lost and restored (serial %d -> %d)", serial0, gfx_restore_serial());
        check(plat_visible(), "window visible again after restore");
        for (i = 0; i < 64; i++) {
            int x = (int)(hash_u32(i * 2 + 1) % (u32)s_tiles.w), y = (int)(hash_u32(i * 2 + 2) % 32u);
            if (gfx_peek(&s_tiles, x, y) != s_tiles.px[y * s_tiles.w + x]) bad++;
        }
        check(bad == 0, "art image VRAM copy re-uploaded (%d of 64 sampled pixels differ from img.px)", bad);
        check(s_rendersAfterRestore > 0, "chunk targets re-rendered after the restore (%d renders)", s_rendersAfterRestore);
        check(s_panelAfterRestore > 0, "UI panel target recomposed after the restore (%d times)", s_panelAfterRestore);
        s_exitCode = s_fails;
        log_printf("test: restore: %d checks, %d failures", s_checks, s_fails);
        plat_quit();
        phase = 3;
    }
}

/* ================================================================== debug overlay (game mode, F11) */
static Img    s_ovl;
static double s_ovlT = -1;
static int    s_ovlSerial = -1;

static void draw_debug_overlay(void)
{
    PlatStats ps;
    char buf[2][128];
    double now = plat_seconds();
    PIX green = gfx_rgb(140, 240, 140);
    if (!s_ovl.w && !gfx_make_target(300, 2 * (gfx_debug_text_h() + 1) + 4, &s_ovl)) return;
    if (s_ovlT < 0 || now - s_ovlT >= 0.5 || s_ovlSerial != gfx_restore_serial()) {
        plat_get_stats(&ps);
        str_printf(buf[0], 128, "%.1f fps (target %d%s)  work %.1f ms  missed %d", s_fps, plat_fps_target(),
                   plat_fps_policy() ? "" : " auto", ps.last_work_ms, ps.missed);
        str_printf(buf[1], 128, "VRAM gfx %d KB, free %d KB", gfx_vram_used_kb(), gfx_vram_free_kb());
        gfx_set_target(&s_ovl);
        gfx_clear(0);
        gfx_debug_text(4, 2, buf[0], green);
        gfx_debug_text(4, 3 + gfx_debug_text_h(), buf[1], green);
        gfx_set_target(NULL);
        s_ovlT = now; s_ovlSerial = gfx_restore_serial();
    }
    gfx_blit(&s_ovl, plat_screen_w() - s_ovl.w - 4, 4);
}

/* ================================================================== bench */
#define MAX_FT     36000
#define NWORST     10
#define MAX_SWITCH 32
typedef struct { float t, interval, work, sec[PROF_COUNT]; int blits, fps; } FrameRec;
static float   *s_ft;
static FrameRec s_worst[NWORST];
static int      s_nworst, s_benching, s_benchDone, s_nft, s_nWin, s_winFrames;
static double   s_benchT0, s_lastPresentT, s_winT0, s_minWin = 1e9, s_maxWin, s_workSum, s_workMax, s_flipSum, s_flipMax;
static int      s_slowFlips;
static double   s_budgetSum, s_budgetMin = 1e9, s_budgetNow;
static int      s_blitSum, s_blitMax, s_lockSum, s_frames30;
static PlatStats s_ps0;
static int      s_vramFree0;
static struct { float t; int from, to; } s_switch[MAX_SWITCH];
static int      s_nswitch, s_lastFps = 60;

static int fcmp(const void *a, const void *b)
{
    float d = *(const float *)a - *(const float *)b;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

static void bench_write(const char *cmdline)
{
    FILE *f = fopen(data_path("bench.txt"), "w");
    double secs = plat_seconds() - s_benchT0, perF;
    int nf = s_nft, i, k, a, t;
    static const float edges[] = { 15.0f, 18.5f, 25.0f, 31.0f, 35.5f, 50.0f, 1e9f };
    static const char *const names[] = { "< 15", "15-18.5 (60 fps)", "18.5-25", "25-31", "31-35.5 (30 fps)", "35.5-50", ">= 50 (stall)" };
    int hist[7];
    PlatStats ps;
    if (!f) return;
    perF = nf ? 1.0 / nf : 0;
    plat_get_stats(&ps);
    gfx_img_count(&a, &t);
    fprintf(f, GAME_TITLE_ASCII " bench (T01)\ncmdline: %s\n", cmdline);
    fprintf(f, "backend: %s, %s\n", plat_backend_name(), gfx_hw() ? "hardware blits" : "software");
    fprintf(f, "pacing: %s, fps policy %s, refresh %.2f Hz\n", plat_pacing_name(),
            plat_fps_policy() == 60 ? "60" : plat_fps_policy() == 30 ? "30" : "auto", plat_refresh_hz());
    fprintf(f, "scene: %s%s, simload %d ms\n", s_scene ? "T01 test scene" : "game",
            s_scene ? (g_cfg.test_direct ? " (direct blits)" : " (chunk targets)") : "", g_cfg.simload_ms);
    fprintf(f, "duration %.2f s, frames %d, avg fps %.2f, 1 s windows min %.1f max %.1f\n", secs, nf, nf / secs,
            s_nWin ? s_minWin : 0.0, s_maxWin);
    fprintf(f, "frames at 30 fps target: %d of %d; auto switches: %d down, %d up\n", s_frames30, nf,
            ps.drops - s_ps0.drops, ps.raises - s_ps0.raises);
    for (i = 0; i < s_nswitch; i++) fprintf(f, "  switch at %.2f s: %d -> %d fps\n", s_switch[i].t, s_switch[i].from, s_switch[i].to);
    fprintf(f, "missed frames %d (extra blanks %d)\n", ps.missed - s_ps0.missed, ps.missed_blanks - s_ps0.missed_blanks);
    if (nf > 0) {
        memset(hist, 0, sizeof(hist));
        for (i = 0; i < nf; i++) { for (k = 0; s_ft[i] >= edges[k]; k++) {} hist[k]++; }
        qsort(s_ft, nf, sizeof(float), fcmp);
        fprintf(f, "frame interval ms: min %.2f median %.2f p95 %.2f p99 %.2f max %.2f\n",
                s_ft[0], s_ft[nf / 2], s_ft[nf * 95 / 100], s_ft[nf * 99 / 100], s_ft[nf - 1]);
        fprintf(f, "frame interval histogram:\n");
        for (k = 0; k < 7; k++) fprintf(f, "  %-18s %6d  %5.1f%%\n", names[k], hist[k], 100.0 * hist[k] / nf);
    }
    fprintf(f, "work ms (frame start -> present call): avg %.3f max %.3f\n", s_workSum * perF, s_workMax);
    fprintf(f, "sim slice budget ms (plat_frame_budget_ms - 1 at slice start): avg %.2f min %.2f\n", s_budgetSum * perF, s_budgetMin);
    fprintf(f, "flip/blt call ms: avg %.3f max %.3f, calls > 1 ms: %d\n", s_flipSum * perF, s_flipMax, s_slowFlips);
    fprintf(f, "blits per frame avg %.1f max %d; software locks per frame %.2f\n", s_blitSum * perF, s_blitMax, s_lockSum * perF);
    fprintf(f, "\nper-frame section ms:      avg      max\n");
    for (i = 0; i < PROF_COUNT; i++)
        fprintf(f, "  %-10s %12.3f %8.3f\n", prof_name(i), prof_ms(i) * perF, prof_max_ms(i));
    fprintf(f, "(present = sleep + wait for the blank + Flip; sim = sim slice incl. synthetic load)\n");
    fprintf(f, "\nworst %d frames (t = s since bench start):\n", s_nworst);
    fprintf(f, "      t  interval    work  target blits |");
    for (i = 0; i < PROF_COUNT; i++) fprintf(f, " %7.7s", prof_name(i));
    fprintf(f, "\n");
    for (k = 0; k < s_nworst; k++) {
        FrameRec *r = &s_worst[k];
        fprintf(f, "%7.2f %9.2f %7.2f %7d %5d |", r->t, r->interval, r->work, r->fps, r->blits);
        for (i = 0; i < PROF_COUNT; i++) fprintf(f, " %7.2f", r->sec[i]);
        fprintf(f, "\n");
    }
    fprintf(f, "\nVRAM: gfx uses %d KB (%d art images, %d targets), free %d KB of %d KB (free at bench start %d KB), refused %d\n",
            gfx_vram_used_kb(), a, t, gfx_vram_free_kb(), gfx_vram_total_kb(), s_vramFree0, gfx_vram_failures());
    if (s_scene) fprintf(f, "scene: %d chunk renders, avg %.2f ms\n", s_rendersTotal, s_rendersTotal ? s_renderMs / s_rendersTotal : 0.0);
    else game_bench_report(f, secs);
    fclose(f);
    log_printf("bench: %.2f fps avg, %d frames, written bench.txt", nf / secs, nf);
}

static void bench_frame(double now, const char *cmdline)
{
    PlatStats ps;
    double interval;
    int b, l, i, k;
    if (!g_cfg.bench) return;
    plat_get_stats(&ps);
    if (!s_benching) {
        if (now - s_t0 >= 1.0) {
            s_benching = 1; s_benchT0 = now; s_winT0 = now; s_lastPresentT = now;
            prof_reset(); gfx_stats(NULL, NULL);
            s_nft = 0; s_winFrames = 0; s_ps0 = ps; s_vramFree0 = gfx_vram_free_kb();
            s_lastFps = plat_fps_target();
            if (!s_scene) game_bench_begin();
            log_printf("bench: measuring %d s", g_cfg.bench);
        }
        return;
    }
    gfx_stats(&b, &l);
    s_blitSum += b; s_lockSum += l;
    if (b > s_blitMax) s_blitMax = b;
    interval = (now - s_lastPresentT) * 1000.0;
    s_lastPresentT = now;
    if (s_nft < MAX_FT && s_ft) s_ft[s_nft++] = (float)interval;
    s_workSum += ps.last_work_ms;
    s_flipSum += ps.last_flip_ms;
    s_budgetSum += s_budgetNow;
    if (s_budgetNow < s_budgetMin) s_budgetMin = s_budgetNow;
    if (ps.last_flip_ms > s_flipMax) s_flipMax = ps.last_flip_ms;
    if (ps.last_flip_ms > 1.0) s_slowFlips++;
    if (ps.last_work_ms > s_workMax) s_workMax = ps.last_work_ms;
    if (plat_fps_target() == 30) s_frames30++;
    if (plat_fps_target() != s_lastFps && s_nswitch < MAX_SWITCH) {
        s_switch[s_nswitch].t = (float)(now - s_benchT0); s_switch[s_nswitch].from = s_lastFps; s_switch[s_nswitch].to = plat_fps_target();
        s_nswitch++;
    }
    s_lastFps = plat_fps_target();
    /* keep the NWORST longest intervals, sorted descending */
    for (k = 0; k < s_nworst && s_worst[k].interval >= interval; k++) {}
    if (k < NWORST) {
        FrameRec r;
        r.t = (float)(now - s_benchT0); r.interval = (float)interval; r.work = (float)ps.last_work_ms;
        r.blits = b; r.fps = plat_fps_target();
        for (i = 0; i < PROF_COUNT; i++) r.sec[i] = (float)prof_frame_ms(i);
        if (s_nworst < NWORST) s_nworst++;
        for (i = s_nworst - 1; i > k; i--) s_worst[i] = s_worst[i - 1];
        s_worst[k] = r;
    }
    s_winFrames++;
    if (now - s_winT0 >= 1.0) {
        double fps = s_winFrames / (now - s_winT0);
        if (fps < s_minWin) s_minWin = fps;
        if (fps > s_maxWin) s_maxWin = fps;
        s_nWin++; s_winFrames = 0; s_winT0 = now;
    }
    if (now - s_benchT0 >= g_cfg.bench) { bench_write(cmdline); s_benchDone = 1; }
}

/* ================================================================== gfx / vram self-tests */
static void draw_test_diamond(Img *im, PIX c)
{
    int y;
    gfx_set_target(im);
    for (y = 0; y < 32; y++) {
        int hw = y < 16 ? 2 * y + 2 : 2 * (31 - y) + 2;
        gfx_fill(32 - hw, y, 32 + hw, y + 1, c);
    }
    gfx_set_target(NULL);
}

static int test_gfx(void)
{
    Img art, tgt, tgt2, wide;
    GfxFont fnt;
    PIX A = gfx_rgb(200, 40, 40), B = gfx_rgb(24, 32, 200), C = gfx_rgb(40, 200, 40);
    int used0, free0, used1, free1, x, y, n, w, ok;
    log_printf("test: gfx on %s (%s)", plat_backend_name(), gfx_hw() ? "hardware" : "software");
    /* warm gfx's own caches used below (debug font atlas for A, stipple for C) before measuring */
    if (gfx_make_target(16, 16, &tgt)) {
        gfx_set_target(&tgt); gfx_fill_half(0, 0, 8, 8, C); gfx_debug_text(0, 0, "x", A); gfx_set_target(NULL);
        gfx_free_img(&tgt);
    }
    used0 = gfx_vram_used_kb(); free0 = gfx_vram_free_kb();
    ok = gfx_make_img(64, 32, 1, &art);
    check(ok, "art image 64x32 keyed created");
    draw_test_diamond(&art, A);
    ok = gfx_make_target(256, 128, &tgt);
    check(ok, "render target 256x128 created");
    if (!ok) return s_fails;
    used1 = gfx_vram_used_kb(); free1 = gfx_vram_free_kb();
    if (gfx_hw()) {
        check(used1 - used0 == 64 + 4, "VRAM accounting: +%d KB for a 64 KB target + 4 KB art (expected 68)", used1 - used0);
        check(free0 - free1 >= 60 && free0 - free1 <= 76, "driver free VRAM dropped %d KB (expected ~68)", free0 - free1);
    }
    check(gfx_peek(&art, 32, 16) == A && gfx_peek(&art, 1, 1) == gfx_key(), "art VRAM copy uploaded (centre = colour, corner = key)");
    /* keyed blits + origin + clip into the target */
    gfx_set_target(&tgt);
    gfx_clear(B);
    gfx_set_origin(-10, -20);
    gfx_set_clip(0, 0, 100, 128);
    gfx_blit(&art, 0, 0);                  /* lands at 10,20 */
    gfx_blit(&art, 80, 0);                 /* lands at 90,20; clipped at x = 100 */
    gfx_fill(0, 60, 20, 70, C);            /* lands at 10..30 x 80..90 */
    gfx_set_target(NULL);
    check(gfx_peek(&tgt, 42, 36) == A, "keyed blit into target with origin: centre pixel");
    check(gfx_peek(&tgt, 11, 21) == B, "colour key transparent in target");
    check(gfx_peek(&tgt, 99, 36) == A, "clip: pixel inside the clip rect drawn");
    check(gfx_peek(&tgt, 100, 36) == B, "clip: pixel right of the clip rect untouched");
    check(gfx_peek(&tgt, 15, 85) == C && gfx_peek(&tgt, 9, 85) == B && gfx_peek(&tgt, 30, 85) == B, "ColorFill into target with origin, exclusive x1");
    /* target -> screen */
    gfx_set_target(NULL);
    gfx_clear(0);
    gfx_blit(&tgt, 100, 100);
    check(gfx_peek(NULL, 142, 136) == A && gfx_peek(NULL, 111, 121) == B && gfx_peek(NULL, 99, 99) == 0, "target blitted to the back buffer");
    /* target -> target (opaque) */
    ok = gfx_make_target(64, 64, &tgt2);
    check(ok, "second target 64x64 created");
    gfx_set_target(&tgt2);
    gfx_clear(C);
    gfx_blit_part(&tgt, 10, 20, 64, 32, 0, 0);
    gfx_set_target(NULL);
    check(gfx_peek(&tgt2, 32, 16) == A && gfx_peek(&tgt2, 1, 1) == B && gfx_peek(&tgt2, 5, 40) == C, "target -> target opaque blit_part");
    /* keyed target */
    gfx_img_set_keyed(&tgt2, 1);
    gfx_set_target(&tgt2);
    gfx_clear(gfx_key());
    gfx_fill(10, 10, 20, 20, A);
    gfx_set_target(&tgt);
    gfx_clear(B);
    gfx_blit(&tgt2, 0, 0);
    gfx_set_target(NULL);
    check(gfx_peek(&tgt, 15, 15) == A && gfx_peek(&tgt, 30, 30) == B, "keyed target blitted with transparency");
    /* stipple half fill */
    gfx_set_target(&tgt);
    gfx_clear(B);
    gfx_fill_half(0, 0, 8, 8, C);
    gfx_set_target(NULL);
    x = gfx_peek(&tgt, 0, 0); y = gfx_peek(&tgt, 1, 0);
    check((x == C && y == B) || (x == B && y == C), "fill_half: checker pattern (%04x %04x)", x, y);
    /* debug text into a target */
    gfx_set_target(&tgt);
    gfx_clear(B);
    w = gfx_debug_text(2, 2, "Kaupunki 123", A);
    gfx_set_target(NULL);
    n = 0;
    for (y = 0; y < 16; y++) for (x = 0; x < 100; x++) if (gfx_peek(&tgt, x, y) == A) n++;
    check(w > 40 && n > 30, "debug text drawn into a target (width %d, %d ink pixels)", w, n);
    /* GfxFont primitive over an arbitrary atlas */
    gfx_font_init(&fnt, &art, 32, 28);
    gfx_font_glyph(&fnt, 'X', 0, 0, 64, 32, 0, 0, 70);
    gfx_set_target(&tgt);
    gfx_clear(B);
    w = gfx_text(&fnt, 0, 0, "XX\nX");
    gfx_set_target(NULL);
    check(w == 140 && gfx_peek(&tgt, 32 + 70, 16) == A && gfx_peek(&tgt, 32, 48) == A && gfx_peek(&tgt, 102, 48) == B,
          "GfxFont: advance, newline, glyph pixels (width %d)", w);
    /* width limit */
    if (gfx_hw()) {
        ok = gfx_make_img(gfx_screen_w() + 64, 8, 0, &wide);
        check(!ok, "image wider than the screen refused");
        if (ok) gfx_free_img(&wide);
    }
    check(gfx_save_bmp(data_path("gfxtest.bmp")), "back buffer saved as gfxtest.bmp");
    gfx_free_img(&tgt2);
    gfx_free_img(&tgt);
    gfx_free_img(&art);
    check(gfx_vram_used_kb() == used0, "VRAM accounting back to %d KB after freeing (now %d)", used0, gfx_vram_used_kb());
    gfx_log_vram("after gfx test");
    log_printf("test: gfx: %d checks, %d failures", s_checks, s_fails);
    return s_fails;
}

static int test_vram(void)
{
    Img *t;
    int n = 0, cap = 512, used0 = gfx_vram_used_kb(), free0 = gfx_vram_free_kb(), fails0 = gfx_vram_failures(), freeFull, i;
    double t0 = time_ms();
    if (!gfx_hw()) { log_printf("test: vram needs the DirectDraw backend"); return 1; }
    t = (Img *)calloc(cap, sizeof(Img));
    if (!t) return 1;
    gfx_log_vram("before fill");
    while (n < cap && gfx_make_target(CHUNK_W, CHUNK_H, &t[n])) n++;
    freeFull = gfx_vram_free_kb();
    log_printf("test: vram: %d targets of 64 KB (%d KB) allocated in %.0f ms before the refusal; free then %d KB",
               n, n * 64, time_ms() - t0, freeFull);
    gfx_log_vram("full");
    check(n > 0 && n < cap, "VRAM ran out after %d targets", n);
    check(gfx_vram_failures() == fails0 + 1, "exactly one refusal counted (%d)", gfx_vram_failures() - fails0);
    /* measured: the Voodoo3 driver refuses 64 KB surfaces with ~450 KB still reported free */
    check(n * 64 <= free0 && n * 64 >= free0 - 640, "allocated %d KB of %d KB reported free at start (usable = free - %d KB)",
          n * 64, free0, free0 - n * 64);
    check(gfx_vram_used_kb() - used0 == n * 64, "accounting +%d KB (expected %d)", gfx_vram_used_kb() - used0, n * 64);
    for (i = 0; i < n; i++) gfx_free_img(&t[i]);
    free(t);
    check(gfx_vram_used_kb() == used0, "accounting back to %d KB (now %d)", used0, gfx_vram_used_kb());
    check(IABS(gfx_vram_free_kb() - free0) <= 64, "driver free VRAM back to %d KB (now %d)", free0, gfx_vram_free_kb());
    log_printf("test: vram: %d checks, %d failures", s_checks, s_fails);
    return s_fails;
}

/* ================================================================== WinMain */
static void shutdown_all(int game)
{
    if (s_scene) sc_shutdown();
    if (s_ovl.w) gfx_free_img(&s_ovl);
    if (game) game_shutdown();
    snd_shutdown();
    plat_shutdown();
    timeEndPeriod(1);
}

static int run_test(const char *name)
{
    int code = -1;
    if (str_ieq(name, "gfx")) code = test_gfx();
    else if (str_ieq(name, "vram")) code = test_vram();
    else if (str_ieq(name, "art")) code = art_init() ? art_selftest() : 1;
    else if (str_ieq(name, "render")) code = render_selftest();
    else if (str_ieq(name, "snd")) code = snd_selftest();
    else if (str_ieq(name, "ui")) code = ui_selftest();         /* T09 */
    else if (str_ieq(name, "life")) code = life_selftest();     /* T11 */
    else if (str_ieq(name, "menu")) code = menu_selftest();     /* T10 */
    else if (str_ieq(name, "save")) code = save_selftest();     /* T10 */
    else log_printf("test: unknown test \"%s\"", name);
    log_printf("test %s: result %d (%s)", name, code, code == 0 ? "pass" : code < 0 ? "not implemented" : "failures");
    return code;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmdline, int show)
{
    double prevT, now, dt, runT, wall0;
    int game = 0;
    UNUSED(prev); UNUSED(show);

    config_init(cmdline);
    if (g_cfg.log) log_open(data_path(g_cfg.log_path[0] ? g_cfg.log_path : "lahio.log"));
    if (config_ini_migrated()) log_printf("config: settings migrated from %s to %s", CONFIG_OLD_INI, config_ini_path());
    timeBeginPeriod(1);
    wall0 = plat_seconds();
    log_printf("%s %s start: cmdline \"%s\", seed %u", GAME_TITLE_ASCII, GAME_VERSION, cmdline, g_cfg.seed);

    if (g_cfg.fast) {                      /* headless: no window, no rendering */
        int code = 0;
        game_init();
        if (g_cfg.test_name[0]) code = run_test(g_cfg.test_name);
        else if (g_cfg.simtest_months > 0) code = game_simtest(g_cfg.simtest_months, g_cfg.statlog_path);
        else if (game_script_path()[0]) code = game_script_headless();   /* T08: -fast -script FILE */
        game_shutdown();
        log_printf("main: exit %d (%.2f s)", code, plat_seconds() - wall0);
        log_close();
        timeEndPeriod(1);
        return code;
    }

    if (!plat_init(inst, g_cfg.res, g_cfg.fullscreen, g_cfg.gdi)) {
        log_printf("main: error: plat_init failed");
        MessageBox(NULL, "Could not create the game window.", GAME_TITLE, MB_OK | MB_ICONERROR);
        timeEndPeriod(1);
        return 1;
    }
    if (!gfx_init()) {
        log_printf("main: error: gfx_init failed");
        MessageBox(plat_hwnd(), "Could not initialise graphics.", GAME_TITLE, MB_OK | MB_ICONERROR);
        plat_shutdown(); timeEndPeriod(1);
        return 1;
    }
    gfx_log_vram("after mode set");
    if (g_cfg.sound && !snd_init(plat_hwnd())) log_printf("main: sound off");
    snd_set_volume(g_cfg.volume);

    if (g_cfg.test_name[0]) {
        if (str_ieq(g_cfg.test_name, "scene")) s_scene = 1;
        else if (str_ieq(g_cfg.test_name, "restore")) { s_scene = 1; s_restoreTest = 1; }
        else {
            int code = run_test(g_cfg.test_name);
            shutdown_all(0);
            log_printf("main: exit %d", code);
            log_close();
            return code;
        }
    } else {
        game = 1;
        if (!game_init()) { s_scene = 1; log_printf("main: game_init returned 0: T01 test scene"); }
    }
    if (s_scene) sc_init();
    if (g_cfg.bench) s_ft = (float *)malloc(MAX_FT * sizeof(float));
    log_printf("main: init done in %.2f s", plat_seconds() - wall0);

    s_t0 = prevT = s_fpsT0 = plat_seconds();
    for (;;) {
        const InputState *in;
        double budget;
        prof_frame_begin();
        prof_begin(PROF_INPUT);
        if (!plat_pump()) { prof_end(PROF_INPUT); break; }
        input_poll();
        prof_end(PROF_INPUT);
        in = input_get();
        now = plat_seconds();
        dt = now - prevT; prevT = now;
        if (dt < 0) dt = 0;
        if (dt > 0.1) dt = 0.1;
        runT = now - s_t0;
        if (g_cfg.exit_after >= 0 && runT >= g_cfg.exit_after &&
            !(g_cfg.shot_time >= 0 && g_cfg.shot_time <= g_cfg.exit_after && runT < g_cfg.exit_after + 5.0)) break;
        if (!s_scene && game_wants_quit()) break;
        if (key_hit(VK_F11) && !s_scene) g_cfg.show_fps = !g_cfg.show_fps;

        prof_begin(PROF_TOOLS);
        if (s_scene) {
            sc_update(dt, runT);
            if (s_restoreTest) restore_script(runT);
        } else {
            if (g_cfg.bench) game_bench_camera(runT);
            game_update(dt);
        }
        prof_end(PROF_TOOLS);

        if (plat_visible()) {
            if (s_scene) sc_draw(); else game_draw();
            prof_begin(PROF_UI);
            if (g_cfg.show_fps && !s_scene) draw_debug_overlay();
            prof_end(PROF_UI);
            if (g_cfg.shot_time >= 0 && runT >= g_cfg.shot_time) {
                gfx_save_bmp(data_path(g_cfg.shot_path));
                g_cfg.shot_time = -1.0f;
            }
            gfx_cursor_draw(in->mx, in->my);
        }

        prof_begin(PROF_SIM);
        budget = plat_frame_budget_ms() - 1.0;
        if (budget < 0) budget = 0;
        s_budgetNow = budget;
        if (s_scene) sc_sim(budget, runT); else game_sim_slice(dt, budget);
        prof_end(PROF_SIM);

        prof_begin(PROF_PRESENT);
        plat_present();
        prof_end(PROF_PRESENT);
        snd_tick();

        now = plat_seconds();
        s_frameNo++;
        s_fpsFrames++;
        if (now - s_fpsT0 >= 1.0) { s_fps = s_fpsFrames / (now - s_fpsT0); s_fpsFrames = 0; s_fpsT0 = now; }
        bench_frame(now, cmdline);
        if (s_benchDone) break;
    }
    {
        PlatStats ps;
        plat_get_stats(&ps);
        log_printf("main: loop end after %.2f s, %d frames, %d missed, auto drops %d raises %d", plat_seconds() - s_t0,
                   ps.frames, ps.missed, ps.drops, ps.raises);
    }
    gfx_log_vram("at exit");
    shutdown_all(game);
    log_printf("main: exit %d", s_exitCode);
    log_close();
    return s_exitCode;
}
