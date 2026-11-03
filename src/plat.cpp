/* plat.cpp - window, display backend, present + frame pacing, input (plat.h). WINDOWS ONLY. Owner: T01.
   Ported from SIROCCO ../sandline/src/plat.cpp + input.cpp (SIROCCO T01): window/DirectDraw/GDI setup,
   565/555 detection, surface restore. Display-locked pacing follows dev/runner (PERF.md "Frame pacing
   fix"); DirectDraw mode setup follows probe/probe.cpp. KAUPUNKI changes: runtime resolution (-res),
   frame pacing + automatic 30 fps fallback inside plat_present, message-based mouse/keyboard/text input.

   Backends: PB_DD_FULL (exclusive WxHx16, primary + 1 back buffer, Flip), PB_DD_WINDOW (VRAM back buffer,
   Blt through a clipper; needs a 16-bit desktop at least as large as the mode, else the largest mode that
   fits), PB_GDI (16-bit DIB + BitBlt). Uses the DX3-level IDirectDraw / IDirectDrawSurface interfaces.

   Pacing (see plat.h): present = [sleep until ~3.5 ms before the target blank] -> WaitForVerticalBlank
   -> Flip (fullscreen) / Blt (windowed). The blank we waited for is the new phase reference: no drift.
   -flipmode 1 (Flip 2 ms before the blank, then wait) and 2 (Flip NOVSYNC after the wait) were A/B
   tested on 86Box: all give 0-2 missed blanks per 15 s under host load; mode 1 occasionally blocks
   8-11 ms inside Flip (33 ms frames), so mode 0 is the default. Missed blanks on the VM mostly come
   from the host (other emulator/host load), not from the game: auto_update only counts a miss toward
   the 60 -> 30 drop when the frame itself was heavy (work + flip > 50% of the 60 fps budget). A frame "misses"
   when more blanks than planned passed (vsync) or present came > 1 ms after its deadline (timer).
   Input is collected from window messages (so taps shorter than a frame and VNC-injected keys are never
   lost) and snapshotted by input_poll(). */
#include "common.h"
#include "plat.h"
#include <ddraw.h>
#include "gfx.h"
#include "util.h"
#include "config.h"

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL 0x020A
#endif
#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif

#define SLEEP_MARGIN_MS    3.5   /* stop Sleep(1)ing this long before the target blank/deadline (only idle time) */
#define FLIP_LEAD_MS       2.0   /* -flipmode 1: Flip this long before the target blank (latches at it) */
#define VSYNC_MARGIN_MS    (g_cfg.flip_mode == 1 ? 2.5 : 1.0)  /* budget ends this long before the blank */
#define AUTO_HEAVY_FRAC    0.5   /* a miss counts toward dropping only if work + flip > this x budget60 */
#define TIMER_MARGIN_MS    0.5
#define AUTO_WINDOW        60    /* frames looked at for the drop decision */
#define AUTO_DROP_MISSES   6     /* misses within AUTO_WINDOW that drop 60 -> 30 */
#define AUTO_RAISE_FRAMES  90    /* consecutive light frames needed for 30 -> 60 */
#define AUTO_RAISE_FRAC    0.75  /* "light" = work <= this fraction of the 60 fps budget */

static HWND     s_hwnd;
static HDC      s_wdc;
static int      s_backend = PB_NONE;
static int      s_pf = PF_565;
static int      s_quit;
static int      s_sw = 800, s_sh = 600;
static char     s_backendName[80] = "none";
static int      s_hideCursor;
static int      s_needRestore;

/* GDI */
static HDC      s_memdc;
static HBITMAP  s_dib;
static HGDIOBJ  s_oldBmp;
static PIX     *s_bits;

/* DirectDraw */
static LPDIRECTDRAW        s_dd;
static LPDIRECTDRAWSURFACE s_prim, s_back;
static LPDIRECTDRAWCLIPPER s_clip;

/* pacing */
static double   s_vbMs;           /* measured refresh period, 0 = unknown */
static int      s_vbPer;          /* blanks per frame at 60 fps (0 = timer pacing) */
static int      s_policy;         /* 60, 30, 0 = auto */
static int      s_fpsCur = 60;
static double   s_lastVb;         /* time_ms of the last blank we waited for */
static double   s_deadline;       /* timer pacing: next present time */
static double   s_frameStart;
static u8       s_missHist[AUTO_WINDOW];
static int      s_missIdx, s_missCount, s_lightRun;
static double   s_dwellMs = 3000.0, s_dwellUntil, s_lastRaiseMs;
static PlatStats s_st;
static char     s_pacingName[64] = "timer";

static const char WNDCLASS_NAME[] = "Lahio98";

static void input_reset_all(void);
static void input_on_message(UINT msg, WPARAM wp, LPARAM lp);

/* ================================================================== small getters */
HWND   plat_hwnd(void) { return s_hwnd; }
int    plat_backend(void) { return s_backend; }
const char *plat_backend_name(void) { return s_backendName; }
int    plat_pixel_format(void) { return s_pf; }
int    plat_screen_w(void) { return s_sw; }
int    plat_screen_h(void) { return s_sh; }
void   plat_quit(void) { s_quit = 1; }
void  *plat_dd(void) { return s_dd; }
void  *plat_dd_back(void) { return s_back; }
PIX   *plat_gdi_bits(void) { return s_backend == PB_GDI ? s_bits : NULL; }
double plat_seconds(void) { return time_ms() / 1000.0; }
double plat_refresh_hz(void) { return s_vbMs > 0 ? 1000.0 / s_vbMs : 0.0; }
int    plat_fps_target(void) { return s_fpsCur; }
int    plat_fps_policy(void) { return s_policy; }
void   plat_note_lost(void) { s_needRestore = 1; }
void   plat_hide_os_cursor(int hide) { s_hideCursor = hide; }
const char *plat_pacing_name(void) { return s_pacingName; }

void plat_get_stats(PlatStats *st)
{
    s_st.vsync_blanks = s_vbPer ? s_vbPer * (60 / s_fpsCur) : 0;
    *st = s_st;
}

int plat_active(void)
{
    if (!s_hwnd) return 0;
    return GetForegroundWindow() == s_hwnd && !IsIconic(s_hwnd);
}

int plat_visible(void)
{
    if (!s_hwnd || IsIconic(s_hwnd)) return 0;
    if (s_backend == PB_DD_FULL && !plat_active()) return 0;
    if (s_needRestore && s_backend != PB_GDI) return 0;
    return 1;
}

/* ================================================================== surface restore */
static void try_restore(void)
{
    HRESULT hr;
    if (s_backend == PB_GDI || !s_prim) { s_needRestore = 0; return; }
    if (s_backend == PB_DD_FULL && !plat_active()) return;
    if (s_prim->IsLost() == DDERR_SURFACELOST) {
        hr = s_prim->Restore();
        if (FAILED(hr)) return;                       /* not ours yet (mode still switching): retry */
    }
    if (s_backend == PB_DD_WINDOW && s_back && s_back->IsLost() == DDERR_SURFACELOST) {
        if (FAILED(s_back->Restore())) return;
    }
    gfx_restore_all();
    s_needRestore = 0;
    s_lastVb = 0; s_deadline = 0;
    input_reset_all();
    log_printf("plat: surfaces restored (restore serial %d)", gfx_restore_serial());
}

/* ================================================================== window */
static void present_raw(void);

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        s_quit = 1;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (s_backend == PB_GDI && s_memdc) BitBlt(dc, 0, 0, s_sw, s_sh, s_memdc, 0, 0, SRCCOPY);
        EndPaint(hwnd, &ps);
        if (s_backend == PB_DD_WINDOW) present_raw();          /* re-show the last frame */
        return 0;
    }
    case WM_ACTIVATEAPP:
        input_reset_all();
        if (s_backend == PB_DD_FULL) {
            s_needRestore = 1;                                  /* deactivation loses the surfaces; */
            log_printf("plat: %s", wp ? "activated" : "deactivated");   /* restored in plat_pump */
        }
        return 0;
    case WM_KILLFOCUS:
        input_reset_all();
        break;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && (s_backend == PB_DD_FULL || s_hideCursor)) { SetCursor(NULL); return TRUE; }
        break;
    case WM_SYSKEYDOWN:
        input_on_message(msg, wp, lp);
        if (wp == VK_F4) break;               /* let Alt+F4 close */
        return 0;                             /* no menu beep on Alt / F10 */
    case WM_SYSKEYUP:
        input_on_message(msg, wp, lp);
        return 0;
    case WM_SYSCHAR:
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU || (wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_MONITORPOWER) return 0;
        break;
    case WM_KEYDOWN: case WM_KEYUP: case WM_CHAR:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_MOUSEWHEEL:
        input_on_message(msg, wp, lp);
        return 0;
    case WM_DESTROY:
        s_hwnd = NULL;
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

/* 565 vs 555: draw pure green into a 1x1 display-compatible bitmap, read the raw bits (runner.cpp) */
static int detect_gdi_format(HDC wdc)
{
    HDC m;
    HBITMAP b;
    HGDIOBJ o;
    WORD v = 0;
    if (GetDeviceCaps(wdc, BITSPIXEL) != 16) return PF_565;
    m = CreateCompatibleDC(wdc);
    b = CreateCompatibleBitmap(wdc, 1, 1);
    o = SelectObject(m, b);
    SetPixel(m, 0, 0, RGB(0, 255, 0));
    SelectObject(m, o);
    GetBitmapBits(b, 2, &v);
    DeleteObject(b);
    DeleteDC(m);
    return v == 0x03E0 ? PF_555 : PF_565;
}

static HWND make_window(HINSTANCE inst, int popup)
{
    DWORD style;
    RECT rc;
    int ww, wh, x, y;
    if (popup) {
        style = WS_POPUP;
        x = 0; y = 0; ww = s_sw; wh = s_sh;
    } else {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        rc.left = 0; rc.top = 0; rc.right = s_sw; rc.bottom = s_sh;
        AdjustWindowRect(&rc, style, FALSE);
        ww = rc.right - rc.left; wh = rc.bottom - rc.top;
        x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2; y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
        if (x < 0) x = 0;
        if (y < 0) y = 0;
    }
    return CreateWindowEx(popup ? WS_EX_TOPMOST : 0, WNDCLASS_NAME, GAME_TITLE, style, x, y, ww, wh, NULL, NULL, inst, NULL);
}

static void show_window(void)
{
    /* The first ShowWindow of a process uses the launcher's STARTUPINFO show state (agent98's `w98 run`
       starts programs hidden): call it twice so the window is really visible and presents are real. */
    ShowWindow(s_hwnd, SW_SHOWNORMAL);
    ShowWindow(s_hwnd, SW_SHOWNORMAL);
    UpdateWindow(s_hwnd);
    SetForegroundWindow(s_hwnd);
}

/* ================================================================== DirectDraw */
static void dd_release(void)
{
    if (s_back && s_backend != PB_DD_FULL) s_back->Release();   /* fullscreen back belongs to the chain */
    s_back = NULL;
    if (s_clip) { s_clip->Release(); s_clip = NULL; }
    if (s_prim) { s_prim->Release(); s_prim = NULL; }
    if (s_dd) {
        if (s_backend == PB_DD_FULL) { s_dd->RestoreDisplayMode(); s_dd->SetCooperativeLevel(s_hwnd, DDSCL_NORMAL); }
        s_dd->Release();
        s_dd = NULL;
    }
}

static int dd_format(LPDIRECTDRAWSURFACE s)
{
    DDSURFACEDESC d;
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    if (FAILED(s->GetSurfaceDesc(&d))) return -1;
    if (d.ddpfPixelFormat.dwRGBBitCount != 16) return -1;
    if (d.ddpfPixelFormat.dwGBitMask == 0x07E0) return PF_565;
    if (d.ddpfPixelFormat.dwGBitMask == 0x03E0) return PF_555;
    return -1;
}

/* returns 1 with s_backend set, or 0 (everything released) */
static int dd_init(int fullscreen)
{
    DDSURFACEDESC d;
    HRESULT hr;
    int pf;
    if (FAILED(hr = DirectDrawCreate(NULL, &s_dd, NULL))) { log_printf("plat: error: DirectDrawCreate failed 0x%08lx", hr); s_dd = NULL; return 0; }
    if (fullscreen) {
        s_backend = PB_DD_FULL;
        if (FAILED(hr = s_dd->SetCooperativeLevel(s_hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN))) { log_printf("plat: error: SetCooperativeLevel exclusive 0x%08lx", hr); goto fail; }
        if (FAILED(hr = s_dd->SetDisplayMode(s_sw, s_sh, 16))) { log_printf("plat: error: SetDisplayMode %dx%dx16 0x%08lx", s_sw, s_sh, hr); goto fail; }
        SetWindowPos(s_hwnd, HWND_TOPMOST, 0, 0, s_sw, s_sh, SWP_SHOWWINDOW);
        memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
        d.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT; d.dwBackBufferCount = 1;
        d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
        if (FAILED(hr = s_dd->CreateSurface(&d, &s_prim, NULL))) { log_printf("plat: error: CreateSurface flip chain 0x%08lx", hr); s_prim = NULL; goto fail; }
        {
            DDSCAPS c;
            c.dwCaps = DDSCAPS_BACKBUFFER;
            if (FAILED(hr = s_prim->GetAttachedSurface(&c, &s_back))) { log_printf("plat: error: GetAttachedSurface 0x%08lx", hr); s_back = NULL; goto fail; }
            s_back->Release();                      /* keep the pointer; owned by the primary chain */
        }
    } else {
        s_backend = PB_DD_WINDOW;
        if (FAILED(hr = s_dd->SetCooperativeLevel(s_hwnd, DDSCL_NORMAL))) { log_printf("plat: error: SetCooperativeLevel normal 0x%08lx", hr); goto fail; }
        memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
        d.dwFlags = DDSD_CAPS; d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (FAILED(hr = s_dd->CreateSurface(&d, &s_prim, NULL))) { log_printf("plat: error: CreateSurface primary 0x%08lx", hr); s_prim = NULL; goto fail; }
        if (dd_format(s_prim) < 0) { log_printf("plat: desktop is not 16 bpp, windowed DirectDraw unavailable"); goto fail; }
        if (FAILED(hr = s_dd->CreateClipper(0, &s_clip, NULL))) { log_printf("plat: error: CreateClipper 0x%08lx", hr); s_clip = NULL; goto fail; }
        s_clip->SetHWnd(0, s_hwnd);
        s_prim->SetClipper(s_clip);
        memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
        d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT; d.dwWidth = s_sw; d.dwHeight = s_sh;
        d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
        if (FAILED(hr = s_dd->CreateSurface(&d, &s_back, NULL))) { log_printf("plat: error: CreateSurface back buffer 0x%08lx", hr); s_back = NULL; goto fail; }
    }
    pf = dd_format(s_back);
    if (pf < 0) { log_printf("plat: back buffer is not 16-bit 555/565"); goto fail; }
    s_pf = pf;
    {
        DDCAPS hal;
        DWORD total = 0, freeMem = 0;
        memset(&hal, 0, sizeof(hal)); hal.dwSize = sizeof(hal);
        if (SUCCEEDED(s_dd->GetCaps(&hal, NULL))) { total = hal.dwVidMemTotal; freeMem = hal.dwVidMemFree; }
        str_printf(s_backendName, sizeof(s_backendName), "ddraw %s %dx%d %s", fullscreen ? "fullscreen" : "windowed",
                   s_sw, s_sh, s_pf == PF_565 ? "565" : "555");
        log_printf("plat: %s, video memory %lu KB total, %lu KB free", s_backendName, total / 1024, freeMem / 1024);
    }
    return 1;
fail:
    dd_release();
    s_backend = PB_NONE;
    return 0;
}

/* time 16 vertical blanks with QPC (dev/runner); sets s_vbMs and s_vbPer */
static void measure_refresh(void)
{
    double a, b, hz;
    int k, n;
    s_vbMs = 0; s_vbPer = 0;
    if (!s_dd) return;
    if (s_dd->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, NULL) != DD_OK) { log_printf("plat: WaitForVerticalBlank unsupported: timer pacing"); return; }
    a = time_ms();
    for (k = 0; k < 16; k++) if (s_dd->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, NULL) != DD_OK) return;
    b = time_ms();
    s_vbMs = (b - a) / 16.0;
    if (s_vbMs < 4.0 || s_vbMs > 40.0) { log_printf("plat: implausible blank period %.3f ms: timer pacing", s_vbMs); s_vbMs = 0; return; }
    hz = 1000.0 / s_vbMs;
    n = (int)(hz / 60.0 + 0.5);
    if (n >= 1 && n <= 4 && (hz - 60.0 * n < 0.03 * 60.0 * n) && (60.0 * n - hz < 0.03 * 60.0 * n)) s_vbPer = n;
    log_printf("plat: refresh %.2f Hz (blank period %.3f ms): %s", hz, s_vbMs,
               s_vbPer ? "display-locked pacing" : "not a multiple of 60 Hz, timer pacing");
}

/* ================================================================== GDI */
static int gdi_init(void)
{
    struct { BITMAPINFOHEADER h; DWORD m[3]; } bi;
    void *bits = NULL;
    int bpp;
    s_wdc = GetDC(s_hwnd);
    bpp = GetDeviceCaps(s_wdc, BITSPIXEL);
    s_pf = detect_gdi_format(s_wdc);
    ZeroMemory(&bi, sizeof(bi));
    bi.h.biSize = sizeof(BITMAPINFOHEADER);
    bi.h.biWidth = s_sw; bi.h.biHeight = -s_sh; bi.h.biPlanes = 1; bi.h.biBitCount = 16;
    bi.h.biCompression = BI_BITFIELDS;
    if (s_pf == PF_565) { bi.m[0] = 0xF800; bi.m[1] = 0x07E0; bi.m[2] = 0x001F; }
    else                { bi.m[0] = 0x7C00; bi.m[1] = 0x03E0; bi.m[2] = 0x001F; }
    s_dib = CreateDIBSection(s_wdc, (BITMAPINFO *)&bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!s_dib || !bits) { log_printf("plat: error: CreateDIBSection failed (%lu)", GetLastError()); return 0; }
    s_bits = (PIX *)bits;
    memset(s_bits, 0, s_sw * s_sh * sizeof(PIX));
    s_memdc = CreateCompatibleDC(s_wdc);
    s_oldBmp = SelectObject(s_memdc, s_dib);
    s_backend = PB_GDI;
    str_printf(s_backendName, sizeof(s_backendName), "gdi-dib %dx%d %s, desktop %d bpp", s_sw, s_sh, s_pf == PF_565 ? "565" : "555", bpp);
    log_printf("plat: %s", s_backendName);
    return 1;
}

/* ================================================================== init / shutdown */
static void set_res(int res)
{
    s_sw = res == RES_640 ? 640 : res == RES_1024 ? 1024 : 800;
    s_sh = res == RES_640 ? 480 : res == RES_1024 ? 768 : 600;
}

int plat_init(HINSTANCE inst, int res, int fullscreen, int gdi)
{
    WNDCLASS wc;
    int useDD = !gdi;

    s_quit = 0;
    set_res(res);
    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = WNDCLASS_NAME;
    if (!RegisterClass(&wc)) { log_printf("plat: error: RegisterClass failed (%lu)", GetLastError()); return 0; }

    if (!useDD) fullscreen = 0;
    if (!fullscreen) {
        /* windowed: the back buffer may not be wider than the desktop (probe 1); shrink the mode to fit */
        int dw = GetSystemMetrics(SM_CXSCREEN), dh = GetSystemMetrics(SM_CYSCREEN), r = res;
        while (r > RES_640) {
            set_res(r);
            if (s_sw <= dw && s_sh <= dh) break;
            r--;
        }
        set_res(r);
        if (r != res) log_printf("plat: warning: desktop %dx%d too small for the requested mode, windowed %dx%d", dw, dh, s_sw, s_sh);
    }
    s_hwnd = make_window(inst, fullscreen);
    if (!s_hwnd) { log_printf("plat: error: CreateWindow failed (%lu)", GetLastError()); return 0; }
    show_window();

    if (useDD && !dd_init(fullscreen) && fullscreen) {
        /* exclusive mode failed: try windowed DirectDraw in a normal window */
        DestroyWindow(s_hwnd);
        s_hwnd = make_window(inst, 0);
        if (!s_hwnd) return 0;
        show_window();
        dd_init(0);
    }
    if (s_backend == PB_NONE) {
        if (useDD) log_printf("plat: warning: DirectDraw unavailable, using the GDI software fallback");
        if (!gdi_init()) return 0;
    }
    if (g_cfg.vsync && s_dd) measure_refresh();
    plat_set_fps(g_cfg.fps);
    s_frameStart = time_ms();
    return 1;
}

void plat_shutdown(void)
{
    gfx_shutdown();
    dd_release();
    if (s_memdc) { SelectObject(s_memdc, s_oldBmp); DeleteDC(s_memdc); s_memdc = NULL; }
    if (s_dib) { DeleteObject(s_dib); s_dib = NULL; }
    s_bits = NULL;
    if (s_hwnd) {
        HWND h = s_hwnd;
        if (s_wdc) ReleaseDC(h, s_wdc);
        s_wdc = NULL;
        DestroyWindow(h);
        s_hwnd = NULL;
    }
    s_backend = PB_NONE;
}

int plat_pump(void)
{
    MSG m;
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) s_quit = 1;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }
    if (!s_hwnd) s_quit = 1;
    if (s_needRestore && s_hwnd) try_restore();
    return !s_quit;
}

/* ================================================================== pacing */
static void update_pacing_name(void)
{
    if (s_vbPer) str_printf(s_pacingName, sizeof(s_pacingName), "vsync %.2f Hz, %d blanks/frame", 1000.0 / s_vbMs, s_vbPer * (60 / s_fpsCur));
    else str_printf(s_pacingName, sizeof(s_pacingName), "timer %d fps", s_fpsCur);
}

static void set_fps_cur(int fps)
{
    s_fpsCur = fps;
    memset(s_missHist, 0, sizeof(s_missHist));
    s_missCount = 0; s_lightRun = 0;
    update_pacing_name();
}

void plat_set_fps(int fps)
{
    s_policy = (fps == 60 || fps == 30) ? fps : 0;
    set_fps_cur(s_policy ? s_policy : 60);
}

static double period_ms(int fps)
{
    if (s_vbPer) return s_vbMs * s_vbPer * (60 / fps);
    return 1000.0 / fps;
}

static double next_deadline(void)
{
    if (s_vbPer && s_lastVb > 0) return s_lastVb + period_ms(s_fpsCur);
    if (s_deadline > 0) return s_deadline;
    return s_frameStart + period_ms(s_fpsCur);
}

double plat_frame_budget_ms(void)
{
    double b = next_deadline() - (s_vbPer ? VSYNC_MARGIN_MS : TIMER_MARGIN_MS) - time_ms();
    return b > 0 ? b : 0;
}

static void sleep_until(double t, int spin)
{
    while (t - time_ms() > SLEEP_MARGIN_MS) Sleep(1);
    if (spin) while (time_ms() < t) {}
}

static HRESULT show_frame(void);

/* vsync pacing: shows the frame (flip_first: Flip before the blank, else Blt after it) and returns 1
   with the number of blanks since the previous frame's blank; 0 if the wait failed (frame not shown
   when !flip_first) */
static int vsync_present(int flip_first, int *blanks, double *flipMs)
{
    double now, t;
    int bpf = s_vbPer * (60 / s_fpsCur);
    if (flip_first == 1) {
        if (s_lastVb > 0) sleep_until(s_lastVb + bpf * s_vbMs - FLIP_LEAD_MS, 1);
        t = time_ms(); show_frame(); *flipMs = time_ms() - t;
        if (s_dd->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, NULL) != DD_OK) { s_lastVb = 0; return 0; }
    } else {
        if (s_lastVb > 0) sleep_until(s_lastVb + bpf * s_vbMs, 0);
        if (s_dd->WaitForVerticalBlank(DDWAITVB_BLOCKBEGIN, NULL) != DD_OK) return 0;
        t = time_ms(); show_frame(); *flipMs = time_ms() - t;
    }
    now = flip_first == 1 ? time_ms() : t;
    *blanks = s_lastVb > 0 ? (int)((now - s_lastVb) / s_vbMs + 0.5) : bpf;
    s_lastVb = now;
    return 1;
}

/* timer wait: returns 1 if the frame came late */
static int timer_wait(void)
{
    double now = time_ms(), per = period_ms(s_fpsCur), target = s_deadline > 0 ? s_deadline : now;
    int late = 0;
    if (now > target + 1.0) { late = 1; target = now; }
    else sleep_until(target, 1);
    s_deadline = target + per;
    return late;
}

static void auto_update(int missed, double work, double flipMs, double nowMs)
{
    if (missed && work + flipMs <= AUTO_HEAVY_FRAC * (period_ms(60) - VSYNC_MARGIN_MS)) missed = 0;   /* external hiccup */
    s_missCount -= s_missHist[s_missIdx];
    s_missHist[s_missIdx] = (u8)missed;
    s_missCount += missed;
    s_missIdx = (s_missIdx + 1) % AUTO_WINDOW;
    if (s_policy) return;
    if (s_fpsCur == 60) {
        if (s_missCount >= AUTO_DROP_MISSES) {
            int n = s_missCount;
            /* bounced back down soon after a raise: stay at 30 longer next time */
            if (s_lastRaiseMs > 0 && nowMs - s_lastRaiseMs < 10000.0) s_dwellMs = IMIN(s_dwellMs * 2.0, 30000.0);
            else s_dwellMs = 3000.0;
            s_dwellUntil = nowMs + s_dwellMs;
            set_fps_cur(30);
            s_st.drops++; s_st.last_switch_s = nowMs / 1000.0;
            log_printf("plat: fps auto 60 -> 30 (%d of the last %d frames missed, last work %.1f ms; dwell %.0f s)",
                       n, AUTO_WINDOW, work, s_dwellMs / 1000.0);
        }
    } else {
        double light = AUTO_RAISE_FRAC * (period_ms(60) - (s_vbPer ? VSYNC_MARGIN_MS : TIMER_MARGIN_MS));
        if (work <= light) s_lightRun++; else s_lightRun = 0;
        if (s_lightRun >= AUTO_RAISE_FRAMES && nowMs >= s_dwellUntil) {
            set_fps_cur(60);
            s_lastRaiseMs = nowMs;
            s_st.raises++; s_st.last_switch_s = nowMs / 1000.0;
            log_printf("plat: fps auto 30 -> 60 (%d frames with work <= %.1f ms)", AUTO_RAISE_FRAMES, light);
        }
    }
}

static HRESULT show_frame(void)
{
    HRESULT hr = DD_OK;
    if (s_backend == PB_GDI) {
        if (s_wdc && s_memdc) BitBlt(s_wdc, 0, 0, s_sw, s_sh, s_memdc, 0, 0, SRCCOPY);
        return DD_OK;
    }
    if (!s_prim || !s_back) return DD_OK;
    gfx_unlock();
    if (s_backend == PB_DD_FULL) hr = s_prim->Flip(NULL, DDFLIP_WAIT | (g_cfg.flip_mode == 2 ? 8 /* DDFLIP_NOVSYNC (DX6) */ : 0));
    else {
        RECT dst, src;
        POINT p;
        p.x = 0; p.y = 0;
        ClientToScreen(s_hwnd, &p);
        dst.left = p.x; dst.top = p.y; dst.right = p.x + s_sw; dst.bottom = p.y + s_sh;
        src.left = 0; src.top = 0; src.right = s_sw; src.bottom = s_sh;
        hr = s_prim->Blt(&dst, s_back, &src, DDBLT_WAIT, NULL);
    }
    if (hr == DDERR_SURFACELOST) s_needRestore = 1;
    return hr;
}

static void present_raw(void)
{
    if (s_backend == PB_DD_WINDOW && s_hwnd && !IsIconic(s_hwnd) && !s_needRestore) show_frame();
}

void plat_present(void)
{
    double t0 = time_ms(), t1, t2, flipMs;
    int missed = 0, blanks = 0, shown = plat_visible();
    s_st.last_work_ms = t0 - s_frameStart;
    if (!shown) {
        /* minimized / inactive / surfaces lost: nothing to show, keep a calm timer pace */
        timer_wait();
        s_frameStart = time_ms();
        s_st.last_wait_ms = s_frameStart - t0; s_st.last_flip_ms = 0;
        return;
    }
    if (s_backend != PB_GDI) gfx_unlock();
    flipMs = 0;
    if (s_vbPer && s_dd && vsync_present(s_backend == PB_DD_FULL && g_cfg.flip_mode == 1, &blanks, &flipMs)) {
        int bpf = s_vbPer * (60 / s_fpsCur);
        if (blanks > bpf) { missed = 1; s_st.missed_blanks += blanks - bpf; }
        s_deadline = 0;
    } else if (s_vbPer && s_dd && s_backend == PB_DD_FULL && g_cfg.flip_mode == 1) {
        s_deadline = 0;                       /* flipped, but the blank wait failed: phase unknown */
    } else {
        missed = timer_wait();
        t1 = time_ms(); show_frame(); flipMs = time_ms() - t1;
    }
    t2 = time_ms();
    s_st.frames++;
    s_st.missed += missed;
    s_st.last_wait_ms = t2 - t0 - flipMs;
    s_st.last_flip_ms = flipMs;
    if (g_cfg.pace_trace > 0 && s_st.frames > 60) {
        g_cfg.pace_trace--;
        log_printf("pace: f %d t %.2f work %.2f wait %.2f flip %.2f blanks %d vb %.2f", s_st.frames, t0, s_st.last_work_ms,
                   s_st.last_wait_ms, flipMs, blanks, s_lastVb);
    }
    auto_update(missed, s_st.last_work_ms, flipMs, t2);
    s_frameStart = t2;
}

/* ================================================================== input */
static u8   s_kdown[256];                 /* held now (messages) */
static u8   s_kpressAcc[256], s_khitAcc[256];
static u8   s_kpress[256], s_khit[256];   /* snapshot for this frame */
static int  s_mbuttons, s_mpressAcc, s_mrelAcc, s_mdblAcc, s_wheelRaw;
static int  s_dragX0, s_dragY0;
static char s_textAcc[16];
static int  s_textLen;
static InputState s_in;

static void input_reset_all(void)
{
    memset(s_kdown, 0, sizeof(s_kdown));
    memset(s_kpressAcc, 0, sizeof(s_kpressAcc)); memset(s_khitAcc, 0, sizeof(s_khitAcc));
    memset(s_kpress, 0, sizeof(s_kpress)); memset(s_khit, 0, sizeof(s_khit));
    if (s_mbuttons && s_hwnd) ReleaseCapture();
    s_mbuttons = s_mpressAcc = s_mrelAcc = s_mdblAcc = s_wheelRaw = 0;
    s_textLen = 0; s_textAcc[0] = 0;
    s_in.buttons = s_in.pressed = s_in.released = s_in.dbl = s_in.wheel = 0;
    s_in.shift = s_in.ctrl = s_in.alt = 0; s_in.text[0] = 0;
}

void input_clear(void) { input_reset_all(); }

static void mouse_press(int b, LPARAM lp, int dbl)
{
    if (!s_mbuttons && s_hwnd) SetCapture(s_hwnd);
    s_mbuttons |= b; s_mpressAcc |= b;
    if (dbl) s_mdblAcc |= b;
    if (b == MOUSE_L || b == MOUSE_M) { s_dragX0 = (short)LOWORD(lp); s_dragY0 = (short)HIWORD(lp); }
}

static void mouse_release(int b)
{
    s_mbuttons &= ~b; s_mrelAcc |= b;
    if (!s_mbuttons) ReleaseCapture();
}

static void input_on_message(UINT msg, WPARAM wp, LPARAM lp)
{
    int vk = (int)(wp & 255);
    switch (msg) {
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (!((lp >> 30) & 1)) s_khitAcc[vk] = 1;      /* bit 30: key was already down = auto-repeat */
        s_kpressAcc[vk] = 1;
        s_kdown[vk] = 1;
        break;
    case WM_KEYUP: case WM_SYSKEYUP:
        s_kdown[vk] = 0;
        break;
    case WM_CHAR: {
        int c = (int)(wp & 255);
        if ((c >= 32 || c == '\b' || c == '\r') && c != 127 && s_textLen < (int)sizeof(s_textAcc) - 1) {
            s_textAcc[s_textLen++] = (char)c;
            s_textAcc[s_textLen] = 0;
        }
        break;
    }
    case WM_LBUTTONDOWN: mouse_press(MOUSE_L, lp, 0); break;
    case WM_RBUTTONDOWN: mouse_press(MOUSE_R, lp, 0); break;
    case WM_MBUTTONDOWN: mouse_press(MOUSE_M, lp, 0); break;
    case WM_LBUTTONDBLCLK: mouse_press(MOUSE_L, lp, 1); break;
    case WM_RBUTTONDBLCLK: mouse_press(MOUSE_R, lp, 1); break;
    case WM_MBUTTONDBLCLK: mouse_press(MOUSE_M, lp, 1); break;
    case WM_LBUTTONUP: mouse_release(MOUSE_L); break;
    case WM_RBUTTONUP: mouse_release(MOUSE_R); break;
    case WM_MBUTTONUP: mouse_release(MOUSE_M); break;
    case WM_MOUSEWHEEL: s_wheelRaw += (short)HIWORD(wp); break;
    }
}

void input_poll(void)
{
    POINT p;
    int active = plat_active();
    memcpy(s_kpress, s_kpressAcc, sizeof(s_kpress));
    memcpy(s_khit, s_khitAcc, sizeof(s_khit));
    memset(s_kpressAcc, 0, sizeof(s_kpressAcc));
    memset(s_khitAcc, 0, sizeof(s_khitAcc));
    s_in.buttons = s_mbuttons;
    s_in.pressed = s_mpressAcc; s_in.released = s_mrelAcc; s_in.dbl = s_mdblAcc;
    s_in.wheel = s_wheelRaw / WHEEL_DELTA;
    s_wheelRaw -= s_in.wheel * WHEEL_DELTA;
    s_mpressAcc = s_mrelAcc = s_mdblAcc = 0;
    memcpy(s_in.text, s_textAcc, sizeof(s_in.text));
    s_in.text[sizeof(s_in.text) - 1] = 0;
    s_textLen = 0; s_textAcc[0] = 0;
    s_in.drag_x0 = s_dragX0; s_in.drag_y0 = s_dragY0;
    s_in.shift = s_kdown[VK_SHIFT] || s_khit[VK_SHIFT];
    s_in.ctrl = s_kdown[VK_CONTROL] || s_khit[VK_CONTROL];
    s_in.alt = s_kdown[VK_MENU] || s_khit[VK_MENU];
    s_in.inside = 0;
    if (s_hwnd && GetCursorPos(&p)) {
        HWND under = WindowFromPoint(p);
        ScreenToClient(s_hwnd, &p);
        s_in.inside = p.x >= 0 && p.y >= 0 && p.x < s_sw && p.y < s_sh && (s_backend == PB_DD_FULL || under == s_hwnd);
        s_in.mx = ICLAMP((int)p.x, 0, s_sw - 1);
        s_in.my = ICLAMP((int)p.y, 0, s_sh - 1);
    }
    if (!active) {
        int mx = s_in.mx, my = s_in.my, inside = s_in.inside;
        input_reset_all();
        s_in.mx = mx; s_in.my = my; s_in.inside = inside;
    }
}

const InputState *input_get(void) { return &s_in; }
int key_down(int vk)    { return vk > 0 && vk < 256 && (s_kdown[vk] || s_khit[vk]); }
int key_pressed(int vk) { return vk > 0 && vk < 256 && s_kpress[vk]; }
int key_hit(int vk)     { return vk > 0 && vk < 256 && s_khit[vk]; }

void input_edge_scroll(int margin, int *dx, int *dy)
{
    *dx = 0; *dy = 0;
    if (!plat_active() || !s_in.inside) return;
    if (s_in.mx < margin) *dx = -1; else if (s_in.mx >= s_sw - margin) *dx = 1;
    if (s_in.my < margin) *dy = -1; else if (s_in.my >= s_sh - margin) *dy = 1;
}
