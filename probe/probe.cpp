// probe.cpp - render/tech probe for the Kaupunki isometric city builder (Visual C++ 6.0, Win98).
//
// Based on dev/sandline/probe. Every run exits by itself and appends result lines to RESULT.TXT
// next to the exe.
//
//   probe info                          machine / display modes / DirectDraw caps
//   probe surf  res=640|800|1024 win=0|1          offscreen surface width/height limits, VRAM use
//   probe sweep res=...                 per-blit cost of typical blits (fullscreen, no flip)
//   probe sim                           simulation CPU budget, memory, mouse picking (no window)
//   probe iso   res=640|800|1024 win=0|1 tile=64|32 zoom=pre|stretch cache=0|1|2 chunk=256|512
//               pool=N ovl=none|tint|checker|fill|fill3 ui=0|1|2 veh=100 bld=60 map=256|128
//               scroll=0|1 occl=0|1 cap=0|30|60 mode=full|draw work=MS t=SECONDS
//     work=MS busy CPU work between issuing the frame's blits and presenting (CPU/blitter overlap)
//     cache=0 full redraw; 1 ground (+overlay) cached in VRAM chunks; 2 ground+buildings cached
//     zoom=stretch with tile=32: 64-px art drawn with hardware-stretched Blt at 1/2 size
//     ui=1 panel/top bar redrawn every frame; ui=2 composed into VRAM surfaces only when dirty
//     mode=draw renders without presenting (throughput, not capped by the 120 Hz flip)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "art.h"

// ---------------------------------------------------------------- options / globals
static char g_backend[16] = "";
static int  o_res = 800, o_win = 0, o_tile = 64, o_cache = 0, o_chunk = 512, o_pool = 0, o_ui = 0;
static int  o_veh = 100, o_bld = 60, o_map = 256, o_scroll = 1, o_secs = 8, o_cap = 0, o_occl = 0, o_work = 0;
static char o_zoom[16] = "pre", o_ovl[16] = "none", o_mode[16] = "full";
static char g_args[256] = "";

static HWND g_wnd;
static int  g_quit = 0;
static char g_dir[MAX_PATH], g_resPath[MAX_PATH];
static char g_note[512] = "";
static int  SW = 800, SH = 600;
static int  g_deskW, g_deskH;

static LARGE_INTEGER g_qf;
static double now_ms() { LARGE_INTEGER q; QueryPerformanceCounter(&q); return (double)q.QuadPart * 1000.0 / (double)g_qf.QuadPart; }

static void out(const char *fmt, ...)
{
    FILE *f = fopen(g_resPath, "a");
    va_list ap;
    if (!f) return;
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fclose(f);
}
static void fail(const char *what, HRESULT hr) { out("%-60s | FAIL: %s (hr=0x%08lX)\n", g_args, what, (unsigned long)hr); }

static int fdiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static unsigned g_rs = 12345;
static int rnd() { g_rs = g_rs * 1103515245u + 12345u; return (int)((g_rs >> 16) & 0x7FFF); }

static void pump()
{
    MSG m;
    while (PeekMessage(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) g_quit = 1;
        TranslateMessage(&m); DispatchMessage(&m);
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN: if (wp == VK_ESCAPE) g_quit = 1; return 0;
    case WM_CLOSE: g_quit = 1; return 0;
    case WM_SETCURSOR: if (LOWORD(lp) == HTCLIENT) { SetCursor(NULL); return TRUE; } break;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProc(h, msg, wp, lp);
}

static int make_window(HINSTANCE inst, int fullscreen)
{
    WNDCLASS wc; RECT r; DWORD style;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = "KaupProbe";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&wc);
    if (fullscreen || (SW >= g_deskW && SH >= g_deskH)) {
        g_wnd = CreateWindowEx(WS_EX_TOPMOST, "KaupProbe", "Kaupunki probe", WS_POPUP | WS_VISIBLE,
                               0, 0, fullscreen ? GetSystemMetrics(SM_CXSCREEN) : SW,
                               fullscreen ? GetSystemMetrics(SM_CYSCREEN) : SH, NULL, NULL, inst, NULL);
    } else {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
        r.left = 0; r.top = 0; r.right = SW; r.bottom = SH;
        AdjustWindowRect(&r, style, FALSE);
        g_wnd = CreateWindow("KaupProbe", "Kaupunki probe", style, 0, 0, r.right - r.left, r.bottom - r.top,
                             NULL, NULL, inst, NULL);
    }
    if (!g_wnd) return 0;
    ShowWindow(g_wnd, SW_SHOWNORMAL); ShowWindow(g_wnd, SW_SHOWNORMAL);
    UpdateWindow(g_wnd); SetForegroundWindow(g_wnd);
    pump();
    return 1;
}

// ---------------------------------------------------------------- DirectDraw basics
struct Fmt { int bpp; DWORD rm, gm, bm; };
static Fmt g_fmt;
static int mask_shift(DWORD m) { int s = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; s++; } return s; }
static int mask_bits(DWORD m) { int b = 0; m >>= mask_shift(m); while (m & 1) { m >>= 1; b++; } return b; }
static DWORD conv(int r, int g, int b)
{
    return ((DWORD)(r >> (8 - mask_bits(g_fmt.rm))) << mask_shift(g_fmt.rm)) |
           ((DWORD)(g >> (8 - mask_bits(g_fmt.gm))) << mask_shift(g_fmt.gm)) |
           ((DWORD)(b >> (8 - mask_bits(g_fmt.bm))) << mask_shift(g_fmt.bm));
}

static LPDIRECTDRAW        g_dd;
static LPDIRECTDRAWSURFACE g_prim, g_back, g_work;
static LPDIRECTDRAWCLIPPER g_clipper;
static int     g_fs;
static HRESULT g_lastHr;
static DWORD   g_vidTotal, g_vidAfterMode;
static LPDIRECTDRAWSURFACE g_reg[4200];
static int     g_nreg;

static DWORD vid_free(DWORD *total)
{
    DDCAPS hal, hel;
    memset(&hal, 0, sizeof(hal)); hal.dwSize = sizeof(hal);
    memset(&hel, 0, sizeof(hel)); hel.dwSize = sizeof(hel);
    if (!g_dd || FAILED(g_dd->GetCaps(&hal, &hel))) return 0;
    if (total) *total = hal.dwVidMemTotal;
    return hal.dwVidMemFree;
}

static LPDIRECTDRAWSURFACE make_surface(int w, int h, DWORD caps)
{
    DDSURFACEDESC d; LPDIRECTDRAWSURFACE s = NULL;
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT; d.dwWidth = w; d.dwHeight = h;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | caps;
    g_lastHr = g_dd->CreateSurface(&d, &s, NULL);
    if (FAILED(g_lastHr)) return NULL;
    if (g_nreg < 4200) g_reg[g_nreg++] = s;
    return s;
}
static void free_surface(LPDIRECTDRAWSURFACE s)
{
    int i;
    for (i = 0; i < g_nreg; i++) if (g_reg[i] == s) { g_reg[i] = g_reg[--g_nreg]; break; }
    s->Release();
}

static void dd_cleanup()
{
    while (g_nreg > 0) g_reg[--g_nreg]->Release();
    if (g_clipper) { g_clipper->Release(); g_clipper = NULL; }
    if (g_prim) { g_prim->Release(); g_prim = NULL; }
    if (g_dd) {
        if (g_fs) { g_dd->RestoreDisplayMode(); g_dd->SetCooperativeLevel(g_wnd, DDSCL_NORMAL); }
        g_dd->Release(); g_dd = NULL;
    }
}

static int dd_open(HINSTANCE inst, int fs)
{
    DDSURFACEDESC d; HRESULT hr;
    g_fs = fs;
    if (!make_window(inst, fs)) { fail("CreateWindow", 0); return 0; }
    if (FAILED(hr = DirectDrawCreate(NULL, &g_dd, NULL))) { fail("DirectDrawCreate", hr); return 0; }
    if (fs) {
        if (FAILED(hr = g_dd->SetCooperativeLevel(g_wnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN))) { fail("SetCooperativeLevel", hr); dd_cleanup(); return 0; }
        if (FAILED(hr = g_dd->SetDisplayMode(SW, SH, 16))) { fail("SetDisplayMode", hr); dd_cleanup(); return 0; }
        memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
        d.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT; d.dwBackBufferCount = 1;
        d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
        if (FAILED(hr = g_dd->CreateSurface(&d, &g_prim, NULL))) { fail("CreateSurface primary+flip", hr); dd_cleanup(); return 0; }
        {
            DDSCAPS c; c.dwCaps = DDSCAPS_BACKBUFFER;
            if (FAILED(hr = g_prim->GetAttachedSurface(&c, &g_back))) { fail("GetAttachedSurface", hr); dd_cleanup(); return 0; }
            g_back->Release();
        }
    } else {
        if (FAILED(hr = g_dd->SetCooperativeLevel(g_wnd, DDSCL_NORMAL))) { fail("SetCooperativeLevel", hr); dd_cleanup(); return 0; }
        memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
        d.dwFlags = DDSD_CAPS; d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (FAILED(hr = g_dd->CreateSurface(&d, &g_prim, NULL))) { fail("CreateSurface primary", hr); dd_cleanup(); return 0; }
        if (FAILED(hr = g_dd->CreateClipper(0, &g_clipper, NULL))) { fail("CreateClipper", hr); dd_cleanup(); return 0; }
        g_clipper->SetHWnd(0, g_wnd); g_prim->SetClipper(g_clipper);
    }
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    g_prim->GetSurfaceDesc(&d);
    g_fmt.bpp = d.ddpfPixelFormat.dwRGBBitCount;
    g_fmt.rm = d.ddpfPixelFormat.dwRBitMask; g_fmt.gm = d.ddpfPixelFormat.dwGBitMask; g_fmt.bm = d.ddpfPixelFormat.dwBBitMask;
    if (g_fmt.bpp != 16) { fail("primary is not 16 bpp", g_fmt.bpp); dd_cleanup(); return 0; }
    g_vidAfterMode = vid_free(&g_vidTotal);
    return 1;
}

static void wait_blt(LPDIRECTDRAWSURFACE s)
{
    while (s->GetBltStatus(DDGBS_ISBLTDONE) == DDERR_WASSTILLDRAWING) ;
}

// ---------------------------------------------------------------- assets
static double g_loadMs;
static int    g_keyMismatch;
static LPDIRECTDRAWSURFACE load_bmp(const char *name, DWORD mem, int key, int *pw, int *ph)
{
    char path[MAX_PATH]; HBITMAP hb, old; BITMAP bm; HDC mdc, sdc; LPDIRECTDRAWSURFACE s;
    double t0 = now_ms();
    sprintf(path, "%sart\\%s", g_dir, name);
    hb = (HBITMAP)LoadImage(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION);
    if (!hb) { g_lastHr = GetLastError(); return NULL; }
    GetObject(hb, sizeof(bm), &bm);
    s = make_surface(bm.bmWidth, bm.bmHeight, mem);
    if (!s) { DeleteObject(hb); return NULL; }
    mdc = CreateCompatibleDC(NULL);
    old = (HBITMAP)SelectObject(mdc, hb);
    if (FAILED(g_lastHr = s->GetDC(&sdc))) { SelectObject(mdc, old); DeleteDC(mdc); DeleteObject(hb); return NULL; }
    BitBlt(sdc, 0, 0, bm.bmWidth, bm.bmHeight, mdc, 0, 0, SRCCOPY);
    s->ReleaseDC(sdc);
    SelectObject(mdc, old); DeleteDC(mdc); DeleteObject(hb);
    if (key) {
        DDSURFACEDESC d; DWORD k = conv(255, 0, 255); DDCOLORKEY ck;
        memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
        if (SUCCEEDED(s->Lock(NULL, &d, DDLOCK_WAIT | DDLOCK_READONLY, NULL))) {
            DWORD p = *(unsigned short *)d.lpSurface;
            s->Unlock(NULL);
            if (p != k) { g_keyMismatch++; k = p; }
        }
        ck.dwColorSpaceLowValue = ck.dwColorSpaceHighValue = k;
        s->SetColorKey(DDCKEY_SRCBLT, &ck);
    }
    if (pw) *pw = bm.bmWidth;
    if (ph) *ph = bm.bmHeight;
    g_loadMs += now_ms() - t0;
    return s;
}

static LPDIRECTDRAWSURFACE g_terS[2], g_ovlS[2], g_objS[2], g_fontS, g_panelS, g_iconS, g_miniS, g_miniSys;
static LPDIRECTDRAWSURFACE g_panelC, g_barC;
static int GW = 8, GH = 12;

static int make_font_surface()
{
    HDC dc; DDBLTFX fx; DDCOLORKEY ck; int c; char ch; TEXTMETRIC tm;
    dc = GetDC(NULL); { HDC m = CreateCompatibleDC(dc); SelectObject(m, GetStockObject(ANSI_FIXED_FONT)); GetTextMetrics(m, &tm); DeleteDC(m); } ReleaseDC(NULL, dc);
    GW = tm.tmAveCharWidth; if (GW > 8) GW = 8;
    GH = tm.tmHeight; if (GH > 16) GH = 16;
    g_fontS = make_surface(16 * GW, 6 * GH, DDSCAPS_VIDEOMEMORY);
    if (!g_fontS) return 0;
    memset(&fx, 0, sizeof(fx)); fx.dwSize = sizeof(fx); fx.dwFillColor = 0;
    g_fontS->Blt(NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    if (FAILED(g_lastHr = g_fontS->GetDC(&dc))) return 0;
    SelectObject(dc, GetStockObject(ANSI_FIXED_FONT));
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    for (c = 0; c < 96; c++) { ch = (char)(c + 32); TextOut(dc, (c & 15) * GW, (c >> 4) * GH, &ch, 1); }
    g_fontS->ReleaseDC(dc);
    ck.dwColorSpaceLowValue = ck.dwColorSpaceHighValue = 0;
    g_fontS->SetColorKey(DDCKEY_SRCBLT, &ck);
    return 1;
}

static int load_assets()
{
    static const char *ter[2] = { "ter64.bmp", "ter32.bmp" }, *ovl[2] = { "ovl64.bmp", "ovl32.bmp" }, *obj[2] = { "obj64.bmp", "obj32.bmp" };
    int z;
    for (z = 0; z < 2; z++) {
        if (!(g_terS[z] = load_bmp(ter[z], DDSCAPS_VIDEOMEMORY, 1, NULL, NULL))) { fail(ter[z], g_lastHr); return 0; }
        if (!(g_ovlS[z] = load_bmp(ovl[z], DDSCAPS_VIDEOMEMORY, 1, NULL, NULL))) { fail(ovl[z], g_lastHr); return 0; }
        if (!(g_objS[z] = load_bmp(obj[z], DDSCAPS_VIDEOMEMORY, 1, NULL, NULL))) { fail(obj[z], g_lastHr); return 0; }
    }
    if (!(g_panelS = load_bmp("panel.bmp", DDSCAPS_VIDEOMEMORY, 0, NULL, NULL))) { fail("panel.bmp", g_lastHr); return 0; }
    if (!(g_iconS = load_bmp("icons.bmp", DDSCAPS_VIDEOMEMORY, 0, NULL, NULL))) { fail("icons.bmp", g_lastHr); return 0; }
    if (!make_font_surface()) { fail("font surface", g_lastHr); return 0; }
    return 1;
}

// ---------------------------------------------------------------- blit layer (counted, clipped)
enum { C_GROUND, C_OVL, C_OBJ, C_VEH, C_OCC, C_UI, C_CHUNK, C_CREND, C_UIC, C_N };
static const char *g_catName[C_N] = { "ground", "ovl", "obj", "veh", "occl", "ui", "chunk", "chunkrender", "uicompose" };
static LPDIRECTDRAWSURFACE g_tgt;
static RECT g_clip;                  // target pixels
static int  g_orgx, g_orgy, g_zs;    // world pixel at target (0,0); world px per target px
static int  g_cat, g_catOv = -1;
static double g_cnt[C_N];

static int blit_t(LPDIRECTDRAWSURFACE src, int dx, int dy, int sx, int sy, int w, int h, int keyed)
{
    RECT r;
    if (dx < g_clip.left) { sx += g_clip.left - dx; w -= g_clip.left - dx; dx = g_clip.left; }
    if (dy < g_clip.top) { sy += g_clip.top - dy; h -= g_clip.top - dy; dy = g_clip.top; }
    if (dx + w > g_clip.right) w = g_clip.right - dx;
    if (dy + h > g_clip.bottom) h = g_clip.bottom - dy;
    if (w <= 0 || h <= 0) return 0;
    r.left = sx; r.top = sy; r.right = sx + w; r.bottom = sy + h;
    g_tgt->BltFast(dx, dy, src, &r, (keyed ? DDBLTFAST_SRCCOLORKEY : DDBLTFAST_NOCOLORKEY) | DDBLTFAST_WAIT);
    g_cnt[g_catOv >= 0 ? g_catOv : g_cat] += 1;
    return 1;
}
// stretched: dest dw x dh, source k times larger (k integer)
static int blit_s(LPDIRECTDRAWSURFACE src, int dx, int dy, int dw, int dh, int sx, int sy, int k, int keyed)
{
    RECT s, d; int c;
    if (dx < g_clip.left) { c = g_clip.left - dx; sx += c * k; dw -= c; dx = g_clip.left; }
    if (dy < g_clip.top) { c = g_clip.top - dy; sy += c * k; dh -= c; dy = g_clip.top; }
    if (dx + dw > g_clip.right) dw = g_clip.right - dx;
    if (dy + dh > g_clip.bottom) dh = g_clip.bottom - dy;
    if (dw <= 0 || dh <= 0) return 0;
    d.left = dx; d.top = dy; d.right = dx + dw; d.bottom = dy + dh;
    s.left = sx; s.top = sy; s.right = sx + dw * k; s.bottom = sy + dh * k;
    g_tgt->Blt(&d, src, &s, DDBLT_WAIT | (keyed ? DDBLT_KEYSRC : 0), NULL);
    g_cnt[g_catOv >= 0 ? g_catOv : g_cat] += 1;
    return 1;
}
static int blit_w(LPDIRECTDRAWSURFACE src, int wx, int wy, int sx, int sy, int w, int h, int keyed)
{
    if (g_zs == 1) return blit_t(src, wx - g_orgx, wy - g_orgy, sx, sy, w, h, keyed);
    return blit_s(src, fdiv(wx - g_orgx, g_zs), fdiv(wy - g_orgy, g_zs), w / g_zs, h / g_zs, sx, sy, g_zs, keyed);
}
static void fill_t(int x, int y, int w, int h, DWORD col)
{
    DDBLTFX fx; RECT r;
    if (x < g_clip.left) { w -= g_clip.left - x; x = g_clip.left; }
    if (y < g_clip.top) { h -= g_clip.top - y; y = g_clip.top; }
    if (x + w > g_clip.right) w = g_clip.right - x;
    if (y + h > g_clip.bottom) h = g_clip.bottom - y;
    if (w <= 0 || h <= 0) return;
    memset(&fx, 0, sizeof(fx)); fx.dwSize = sizeof(fx); fx.dwFillColor = col;
    r.left = x; r.top = y; r.right = x + w; r.bottom = y + h;
    g_tgt->Blt(&r, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    g_cnt[g_catOv >= 0 ? g_catOv : g_cat] += 1;
}
static void fill_w(int wx, int wy, int w, int h, DWORD col)
{
    fill_t(fdiv(wx - g_orgx, g_zs), fdiv(wy - g_orgy, g_zs), w / g_zs, h / g_zs, col);
}
static void text_t(int x, int y, const char *s)
{
    for (; *s; s++, x += GW) {
        int c = (unsigned char)*s - 32;
        if (c > 0 && c < 96) blit_t(g_fontS, x, y, (c & 15) * GW, (c >> 4) * GH, GW, GH, 1);
    }
}

// ---------------------------------------------------------------- map / scene
enum { OV_NONE, OV_TINT, OV_CHECK, OV_FILL, OV_FILL3 };
static int N, TW, TH, HW, HH, OX, AZ, ZS, MAXH, MAXROWS, g_ovl;
static unsigned char *m_ter, *m_obj, *m_ovl;
#define IDX(tx, ty) ((ty) * N + (tx))
static DWORD g_lvlCol[5];

static int is_free(int x, int y)
{
    int t = m_ter[IDX(x, y)];
    return m_obj[IDX(x, y)] == 255 && t != 5 && (t < 6 || t > 8);
}

static void make_map()
{
    int x, y, i, n = N * N;
    m_ter = (unsigned char *)malloc(n); m_obj = (unsigned char *)malloc(n); m_ovl = (unsigned char *)malloc(n);
    g_rs = 4242;
    for (i = 0; i < n; i++) { int r = rnd() % 100; m_ter[i] = (unsigned char)(r < 88 ? r % 4 : r < 94 ? 4 : r < 97 ? 9 + (r & 1) * 3 : 11); m_obj[i] = 255; }
    for (i = 0; i < N / 16; i++) {      // lakes
        int cx = rnd() % N, cy = rnd() % N, r = 3 + rnd() % 7;
        for (y = imax(0, cy - r); y <= imin(N - 1, cy + r); y++)
            for (x = imax(0, cx - r); x <= imin(N - 1, cx + r); x++)
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) m_ter[IDX(x, y)] = 5;
    }
    for (y = 0; y < N; y++) for (x = 0; x < N; x++) {
        i = IDX(x, y);
        if (m_ter[i] == 5) continue;
        if ((x & 7) == 0 && (y & 7) == 0) m_ter[i] = 8;
        else if ((y & 7) == 0) m_ter[i] = 6;
        else if ((x & 7) == 0) m_ter[i] = 7;
    }
    for (y = 0; y < N; y++) for (x = 0; x < N; x++) {
        int k, o;
        if (!is_free(x, y) || rnd() % 100 >= o_bld) continue;
        k = rnd() % 100;
        if (k < 40) o = OB_HOUSE0 + rnd() % 4;
        else if (k < 58) o = OB_MID0 + rnd() % 3;
        else if (k < 68) o = OB_TALL0 + rnd() % 3;
        else if (k < 76 && x > 0 && y > 0 && is_free(x - 1, y) && is_free(x, y - 1) && is_free(x - 1, y - 1)) {
            o = OB_BIG0 + rnd() % 2;
            m_obj[IDX(x - 1, y)] = m_obj[IDX(x, y - 1)] = m_obj[IDX(x - 1, y - 1)] = 254;
        } else o = OB_TREE0 + rnd() % 3;
        m_obj[IDX(x, y)] = (unsigned char)o;
    }
    for (y = 0; y < N; y++) for (x = 0; x < N; x++) {
        double v = sin(x * 0.05) * cos(y * 0.07) + 0.5 * sin((x + y) * 0.11);
        int l = (int)((v + 1.5) / 3.0 * 5.0);
        m_ovl[IDX(x, y)] = (unsigned char)(l < 0 ? 0 : l > 4 ? 4 : l);
    }
}

// vehicles: local coordinates (1/256 tile) inside a box around the camera centre
struct Veh { int dir, k, l, speed, kind; int tx, ty, fx, fy; };
static Veh  g_veh[512];
static int  g_nveh, g_vbx, g_vby, g_vR;
static int  g_vhead[1024], g_vnext[512];
static double g_vehDrawn;

static void init_vehicles()
{
    int i;
    g_nveh = imin(o_veh, 512);
    g_vR = 8 * imax(1, (SH * ZS) / (16 * TH));
    for (i = 0; i < g_nveh; i++) {
        g_veh[i].dir = i & 1;
        g_veh[i].k = (rnd() % (g_vR / 4)) - g_vR / 8;
        g_veh[i].l = (rnd() % (2 * g_vR * 256)) - g_vR * 256;
        g_veh[i].speed = (rnd() & 1 ? 1 : -1) * (6 + rnd() % 10);
        g_veh[i].kind = OB_CAR0 + (i & 7);
    }
}

static void world_to_tile(int wx, int wy, int *tx, int *ty)
{
    // a - b = (wx - OX)/HW - 1, a + b = wy/HH
    int u = fdiv(wx - OX, HW) - 1, v = fdiv(wy, HH);
    *tx = fdiv(u + v, 2); *ty = fdiv(v - u, 2);
}

static void update_vehicles(int camcx, int camcy)
{
    int i, bx, by;
    world_to_tile(camcx, camcy, &bx, &by);
    g_vbx = bx & ~7; g_vby = by & ~7;
    for (i = 0; i < g_nveh; i++) {
        Veh *v = &g_veh[i]; int lane;
        v->l += v->speed;
        if (v->l >= g_vR * 256) v->l -= 2 * g_vR * 256;
        if (v->l < -g_vR * 256) v->l += 2 * g_vR * 256;
        lane = v->speed > 0 ? 128 + 50 : 128 - 50;
        if (v->dir == 0) { v->fx = g_vbx * 256 + v->l; v->fy = (g_vby + 8 * v->k) * 256 + lane; }
        else { v->fy = g_vby * 256 + v->l; v->fx = (g_vbx + 8 * v->k) * 256 + lane; }
        if (v->fx < 0) v->fx = 0; if (v->fy < 0) v->fy = 0;
        if (v->fx >= N * 256) v->fx = N * 256 - 1; if (v->fy >= N * 256) v->fy = N * 256 - 1;
        v->tx = v->fx >> 8; v->ty = v->fy >> 8;
    }
}

static void veh_world(Veh *v, int *wx, int *wy, int *w, int *h)
{
    const short *r = OBJ_RECT[AZ][v->kind];
    *w = r[2]; *h = r[3];
    *wx = (((v->fx - v->fy) * HW) >> 8) + OX + HW - r[2] / 2;
    *wy = (((v->fx + v->fy) * HH) >> 8) - r[3] + 2;
}

static int draw_vehicle(Veh *v)
{
    int wx, wy, w, h; const short *r = OBJ_RECT[AZ][v->kind];
    veh_world(v, &wx, &wy, &w, &h);
    return blit_w(g_objS[AZ], wx, wy, r[0], r[1], w, h, 1);
}

static void view_world(int *x0, int *y0, int *x1, int *y1)
{
    *x0 = g_orgx + g_clip.left * g_zs; *y0 = g_orgy + g_clip.top * g_zs;
    *x1 = g_orgx + g_clip.right * g_zs; *y1 = g_orgy + g_clip.bottom * g_zs;
}

static void draw_ground()
{
    int wx0, wy0, wx1, wy1, s, s0, s1, tx, tx0, tx1;
    LPDIRECTDRAWSURFACE ter = g_terS[AZ], ovl = g_ovlS[AZ];
    view_world(&wx0, &wy0, &wx1, &wy1);
    s0 = imax(0, fdiv(wy0 - TH, HH)); s1 = imin(2 * N - 2, fdiv(wy1, HH));
    for (s = s0; s <= s1; s++) {
        int wy = s * HH;
        tx0 = imax(imax(0, s - (N - 1)), fdiv(fdiv(wx0 - TW - OX, HW) + s, 2));
        tx1 = imin(imin(N - 1, s), fdiv(fdiv(wx1 - OX, HW) + s, 2) + 1);
        for (tx = tx0; tx <= tx1; tx++) {
            int i = IDX(tx, s - tx), t = m_ter[i], lvl = m_ovl[i], wx = (2 * tx - s) * HW + OX;
            g_cat = C_GROUND;
            blit_w(ter, wx, wy, (t & 7) * TW, ((t >> 3) + (g_ovl == OV_TINT ? 2 * lvl : 0)) * TH, TW, TH, 1);
            if (!lvl || g_ovl <= OV_TINT) continue;
            g_cat = C_OVL;
            if (g_ovl == OV_CHECK) blit_w(ovl, wx, wy, (lvl - 1) * TW, 0, TW, TH, 1);
            else if (g_ovl == OV_FILL) fill_w(wx + TW / 4, wy + TH / 4, TW / 2, TH / 2, g_lvlCol[lvl]);
            else {
                fill_w(wx + TW / 4, wy + TH / 8, TW / 2, TH / 4, g_lvlCol[lvl]);
                fill_w(wx + TW / 8, wy + 3 * TH / 8, 3 * TW / 4, TH / 4, g_lvlCol[lvl]);
                fill_w(wx + TW / 4, wy + 5 * TH / 8, TW / 2, TH / 4, g_lvlCol[lvl]);
            }
        }
    }
}

static void draw_objects(int withVeh)
{
    int wx0, wy0, wx1, wy1, s, s0, s1, tx, tx0, tx1, i;
    LPDIRECTDRAWSURFACE obj = g_objS[AZ];
    view_world(&wx0, &wy0, &wx1, &wy1);
    wx0 -= HW; wx1 += HW;                       // 2x2 buildings are wider than their anchor tile
    s0 = imax(0, fdiv(wy0 - TH, HH)); s1 = imin(2 * N - 2, fdiv(wy1 + MAXH - TH, HH) + 1);
    if (withVeh) {
        for (s = s0; s <= s1 && s - s0 < 1024; s++) g_vhead[s - s0] = -1;
        for (i = 0; i < g_nveh; i++) {
            int vs = g_veh[i].tx + g_veh[i].ty;
            if (vs < s0 || vs > s1 || vs - s0 >= 1024) continue;
            g_vnext[i] = g_vhead[vs - s0]; g_vhead[vs - s0] = i;
        }
    }
    for (s = s0; s <= s1; s++) {
        tx0 = imax(imax(0, s - (N - 1)), fdiv(fdiv(wx0 - TW - OX, HW) + s, 2));
        tx1 = imin(imin(N - 1, s), fdiv(fdiv(wx1 - OX, HW) + s, 2) + 1);
        g_cat = C_OBJ;
        for (tx = tx0; tx <= tx1; tx++) {
            int o = m_obj[IDX(tx, s - tx)];
            const short *r;
            if (o >= NOBJ) continue;
            r = OBJ_RECT[AZ][o];
            blit_w(obj, (2 * tx - s) * HW + OX + HW - r[2] / 2, s * HH + TH - r[3], r[0], r[1], r[2], r[3], 1);
        }
        if (withVeh && s - s0 < 1024) {
            g_cat = C_VEH;
            for (i = g_vhead[s - s0]; i >= 0; i = g_vnext[i]) g_vehDrawn += draw_vehicle(&g_veh[i]);
        }
    }
}

// vehicles on top of a cached ground+buildings layer; optionally redraw the buildings in front
// of each vehicle, clipped to the vehicle rectangle, so it is hidden correctly
static void draw_vehicles_top()
{
    int i, d, c, wx, wy, w, h;
    RECT save = g_clip;
    for (i = 0; i < g_nveh; i++) {
        Veh *v = &g_veh[i]; int s, vc;
        g_cat = C_VEH;
        if (!draw_vehicle(v)) continue;
        g_vehDrawn += 1;
        if (!o_occl) continue;
        veh_world(v, &wx, &wy, &w, &h);
        g_clip.left = imax(save.left, fdiv(wx - g_orgx, g_zs)); g_clip.top = imax(save.top, fdiv(wy - g_orgy, g_zs));
        g_clip.right = imin(save.right, fdiv(wx + w - g_orgx, g_zs) + 1); g_clip.bottom = imin(save.bottom, fdiv(wy + h - g_orgy, g_zs) + 1);
        s = v->tx + v->ty; vc = v->tx - v->ty;
        g_cat = C_OCC;
        for (d = 1; d <= MAXROWS; d++) {
            int s2 = s + d;
            for (c = vc - 3; c <= vc + 3; c++) {
                int tx, ty, o, ox, oy; const short *r;
                if (((s2 + c) & 1) != 0) continue;
                tx = (s2 + c) / 2; ty = (s2 - c) / 2;
                if (tx < 0 || ty < 0 || tx >= N || ty >= N) continue;
                o = m_obj[IDX(tx, ty)];
                if (o >= NOBJ) continue;
                r = OBJ_RECT[AZ][o];
                ox = (tx - ty) * HW + OX + HW - r[2] / 2; oy = s2 * HH + TH - r[3];
                if (ox >= wx + w || ox + r[2] <= wx || oy >= wy + h || oy + r[3] <= wy) continue;
                blit_w(g_objS[AZ], ox, oy, r[0], r[1], r[2], r[3], 1);
            }
        }
        g_clip = save;
    }
}

// ---------------------------------------------------------------- chunk cache
struct Chunk { int cx, cy, valid; DWORD stamp; LPDIRECTDRAWSURFACE s; };
static Chunk g_pool[256];
static int   g_npool, CW, CH;
static DWORD g_frame;
static double g_crMs, g_crN;

static void render_chunk(Chunk *c)
{
    LPDIRECTDRAWSURFACE st = g_tgt; RECT sc = g_clip; int sx = g_orgx, sy = g_orgy, sz = g_zs;
    g_tgt = c->s; g_clip.left = 0; g_clip.top = 0; g_clip.right = CW; g_clip.bottom = CH;
    g_orgx = c->cx * CW; g_orgy = c->cy * CH; g_zs = 1; g_catOv = C_CREND;
    draw_ground();
    if (o_cache == 2) draw_objects(0);
    g_catOv = -1;
    g_tgt = st; g_clip = sc; g_orgx = sx; g_orgy = sy; g_zs = sz;
    c->valid = 1;
}

static Chunk *get_chunk(int cx, int cy)
{
    int i, best = -1; Chunk *c; double t0;
    for (i = 0; i < g_npool; i++) if (g_pool[i].valid && g_pool[i].cx == cx && g_pool[i].cy == cy) return &g_pool[i];
    for (i = 0; i < g_npool; i++) {
        if (g_pool[i].stamp == g_frame && g_pool[i].valid) continue;
        if (best < 0 || !g_pool[i].valid || g_pool[i].stamp < g_pool[best].stamp) { best = i; if (!g_pool[i].valid) break; }
    }
    if (best < 0) return NULL;
    c = &g_pool[best]; c->cx = cx; c->cy = cy;
    t0 = now_ms();
    render_chunk(c);
    g_crMs += now_ms() - t0; g_crN += 1;
    return c;
}

static int g_chunkMiss;
static void draw_chunks()
{
    int wx0, wy0, wx1, wy1, cx, cy;
    view_world(&wx0, &wy0, &wx1, &wy1);
    g_cat = C_CHUNK;
    for (cy = fdiv(wy0, CH); cy <= fdiv(wy1 - 1, CH); cy++)
        for (cx = fdiv(wx0, CW); cx <= fdiv(wx1 - 1, CW); cx++) {
            Chunk *c = get_chunk(cx, cy);
            if (!c) { g_chunkMiss++; continue; }
            c->stamp = g_frame;
            blit_w(c->s, cx * CW, cy * CH, 0, 0, CW, CH, 0);
        }
}

// ---------------------------------------------------------------- UI
#define PANEL_W 160
static double g_uiComposeMs, g_uiComposeN;
static void draw_panel(int x0, int y0, int frame)
{
    int i; char buf[40];
    static const char *lab[14] = { "Population", "Funds", "Income", "Expenses", "Residential", "Commercial",
                                   "Industrial", "Power", "Water", "Pollution", "Crime", "Traffic", "Land value", "Happiness" };
    blit_t(g_panelS, x0, y0, 0, 0, PANEL_W, imin(600, SH), 0);
    if (SH > 600) blit_t(g_panelS, x0, y0 + 600, 0, 0, PANEL_W, SH - 600, 0);
    for (i = 0; i < 16; i++) blit_t(g_iconS, x0 + 8 + (i & 3) * 37, y0 + 8 + (i >> 2) * 34, i * 32, 0, 32, 32, 0);
    blit_t(g_miniS, x0 + 16, y0 + 150, 0, 0, 128, 128, 0);
    for (i = 0; i < 14; i++) {
        sprintf(buf, "%-11s%6ld", lab[i], (long)((frame / 30) * (i + 3) * 37 % 999999));
        text_t(x0 + 10, y0 + 292 + i * (GH + 3), buf);
    }
}
static char g_fpsText[16] = "0.0";
static void draw_bar(int x0, int y0, int w, int frame)
{
    char buf[96];
    fill_t(x0, y0, w, 16, conv(30, 34, 50));
    sprintf(buf, "KAUPUNKI  %02d/%04d  Funds $%7ld  Pop %6ld  FPS %s", 1 + (frame / 30) % 12, 1999 + frame / 360,
            1234567L - frame, 50000L + frame * 3, g_fpsText);
    text_t(x0 + 4, y0 + 2, buf);
}

static int build_minimap()
{
    DDSURFACEDESC d; int x, y; RECT r;
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    if (FAILED(g_lastHr = g_miniSys->Lock(NULL, &d, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL))) return 0;
    for (y = 0; y < 128; y++) {
        unsigned short *p = (unsigned short *)((unsigned char *)d.lpSurface + y * d.lPitch);
        for (x = 0; x < 128; x++) {
            int i = IDX(x * N / 128, y * N / 128), t = m_ter[i], o = m_obj[i];
            DWORD c = t == 5 ? conv(40, 80, 170) : (t >= 6 && t <= 8) ? conv(80, 80, 84) :
                      o < NOBJ ? (o >= OB_TREE0 ? conv(30, 110, 40) : o >= OB_TALL0 ? conv(160, 160, 200) : conv(200, 170, 140)) : conv(70, 140, 50);
            p[x] = (unsigned short)c;
        }
    }
    g_miniSys->Unlock(NULL);
    r.left = 0; r.top = 0; r.right = 128; r.bottom = 128;
    g_lastHr = g_miniS->BltFast(0, 0, g_miniSys, &r, DDBLTFAST_NOCOLORKEY | DDBLTFAST_WAIT);
    return SUCCEEDED(g_lastHr);
}

// ---------------------------------------------------------------- iso benchmark
static int  g_camx, g_camy, g_cvx, g_cvy, g_cx0, g_cy0, g_crange_x, g_crange_y;
static RECT g_mapClip;

static void update_camera()
{
    if (!o_scroll) return;
    g_camx += g_cvx; g_camy += g_cvy;
    if (g_camx > g_cx0 + g_crange_x || g_camx < g_cx0 - g_crange_x) g_cvx = -g_cvx;
    if (g_camy > g_cy0 + g_crange_y || g_camy < g_cy0 - g_crange_y) g_cvy = -g_cvy;
}

static void draw_frame(int f)
{
    int mw = g_mapClip.right - g_mapClip.left, mh = g_mapClip.bottom - g_mapClip.top;
    g_frame = (DWORD)f + 1;
    update_camera();
    update_vehicles(g_camx + mw * ZS / 2, g_camy + mh * ZS / 2);
    g_tgt = g_work; g_clip = g_mapClip; g_zs = ZS;
    g_orgx = g_camx - g_mapClip.left * ZS; g_orgy = g_camy - g_mapClip.top * ZS;
    if (o_cache) draw_chunks(); else draw_ground();
    if (o_cache == 2) draw_vehicles_top(); else draw_objects(1);
    g_clip.left = 0; g_clip.top = 0; g_clip.right = SW; g_clip.bottom = SH;
    g_cat = C_UI;
    if (o_ui == 0) text_t(4, 4, g_fpsText);
    else if (o_ui == 1) { draw_panel(SW - PANEL_W, 0, f); draw_bar(0, 0, SW - PANEL_W, f); }
    else {
        if (f % 30 == 0) {          // dirty: recompose the cached panel and bar
            LPDIRECTDRAWSURFACE st = g_tgt; RECT sc = g_clip; double t0 = now_ms();
            g_catOv = C_UIC;
            g_tgt = g_panelC; g_clip.left = 0; g_clip.top = 0; g_clip.right = PANEL_W; g_clip.bottom = SH;
            draw_panel(0, 0, f);
            g_tgt = g_barC; g_clip.right = SW - PANEL_W; g_clip.bottom = 16;
            draw_bar(0, 0, SW - PANEL_W, f);
            g_catOv = -1; g_tgt = st; g_clip = sc;
            g_uiComposeMs += now_ms() - t0; g_uiComposeN += 1;
        }
        blit_t(g_panelC, SW - PANEL_W, 0, 0, 0, PANEL_W, SH, 0);
        blit_t(g_barC, 0, 0, 0, 0, SW - PANEL_W, 16, 0);
    }
}

static int present()
{
    HRESULT hr;
    if (g_fs) hr = g_prim->Flip(NULL, DDFLIP_WAIT);
    else {
        RECT s, d; POINT p = { 0, 0 };
        ClientToScreen(g_wnd, &p);
        d.left = p.x; d.top = p.y; d.right = p.x + SW; d.bottom = p.y + SH;
        s.left = 0; s.top = 0; s.right = SW; s.bottom = SH;
        hr = g_prim->Blt(&d, g_work, &s, DDBLT_WAIT, NULL);
    }
    if (hr == DDERR_SURFACELOST) { g_prim->Restore(); return 1; }
    if (FAILED(hr)) { fail(g_fs ? "Flip" : "Blt to primary", hr); return 0; }
    return 1;
}

static void run_iso(HINSTANCE inst)
{
    int stretch = !strcmp(o_zoom, "stretch") && o_tile == 32, i, f = 0, counted = 0;
    int doPres = strcmp(o_mode, "draw") != 0, fpsFrames = 0;
    double t0, tStart = 0, tEnd, sumDraw = 0, sumPres = 0, sumIdle = 0, worst = 0, last, lastFps, nextT = 0;
    double cnt0[C_N], crMs0 = 0, crN0 = 0, veh0 = 0;
    DWORD vidAssets, vidEnd;
    char buf[512];

    AZ = (o_tile == 32 && !stretch) ? 1 : 0; ZS = stretch ? 2 : 1;
    TW = AZ ? 32 : 64; TH = TW / 2; HW = TW / 2; HH = TH / 2;
    N = o_map; OX = (N - 1) * HW; MAXH = 160 >> AZ; MAXROWS = (MAXH - TH) / HH + 1;
    g_ovl = !strcmp(o_ovl, "tint") ? OV_TINT : !strcmp(o_ovl, "checker") ? OV_CHECK : !strcmp(o_ovl, "fill") ? OV_FILL :
            !strcmp(o_ovl, "fill3") ? OV_FILL3 : OV_NONE;
    make_map();
    init_vehicles();
    if (!dd_open(inst, !o_win)) return;
    if (o_win) {
        if (!(g_work = make_surface(SW, SH, DDSCAPS_VIDEOMEMORY))) { fail("CreateSurface work", g_lastHr); dd_cleanup(); return; }
    } else g_work = g_back;
    if (!load_assets()) { dd_cleanup(); return; }
    g_lvlCol[0] = 0; g_lvlCol[1] = conv(0, 200, 0); g_lvlCol[2] = conv(220, 220, 0); g_lvlCol[3] = conv(255, 128, 0); g_lvlCol[4] = conv(255, 0, 0);
    g_miniS = make_surface(128, 128, DDSCAPS_VIDEOMEMORY);
    g_miniSys = make_surface(128, 128, DDSCAPS_SYSTEMMEMORY);
    if (!g_miniS || !g_miniSys || !build_minimap()) { fail("minimap", g_lastHr); dd_cleanup(); return; }
    {   // minimap rebuild cost (software into sysmem + upload)
        double a = now_ms();
        for (i = 0; i < 10; i++) build_minimap();
        wait_blt(g_miniS);
        sprintf(g_note + strlen(g_note), "minimap rebuild %.2f ms; ", (now_ms() - a) / 10);
    }
    if (o_ui == 2) {
        g_panelC = make_surface(PANEL_W, SH, DDSCAPS_VIDEOMEMORY);
        g_barC = make_surface(SW - PANEL_W, 16, DDSCAPS_VIDEOMEMORY);
        if (!g_panelC || !g_barC) { fail("ui cache surfaces", g_lastHr); dd_cleanup(); return; }
    }
    vidAssets = vid_free(NULL);
    g_mapClip.left = 0; g_mapClip.top = o_ui ? 16 : 0; g_mapClip.right = o_ui ? SW - PANEL_W : SW; g_mapClip.bottom = SH;
    if (o_cache) {
        int mw = (g_mapClip.right - g_mapClip.left) * ZS, mh = (g_mapClip.bottom - g_mapClip.top) * ZS, cols, rows, want;
        CW = o_chunk; CH = o_chunk / 2;
        cols = (mw + CW - 1) / CW + 1; rows = (mh + CH - 1) / CH + 1;
        want = o_pool ? o_pool : cols * rows + cols + rows;
        if (want > 256) want = 256;
        for (g_npool = 0; g_npool < want; g_npool++) {
            memset(&g_pool[g_npool], 0, sizeof(Chunk));
            if (!(g_pool[g_npool].s = make_surface(CW, CH, DDSCAPS_VIDEOMEMORY))) break;
        }
        sprintf(g_note + strlen(g_note), "pool %d/%d chunks %dx%d (%d KB, visible max %d); ", g_npool, want, CW, CH,
                g_npool * CW * CH * 2 / 1024, cols * rows);
        if (g_npool < cols * rows) sprintf(g_note + strlen(g_note), "POOL TOO SMALL hr=0x%08lX; ", (unsigned long)g_lastHr);
        if (!g_npool) { fail("chunk pool", g_lastHr); dd_cleanup(); return; }
    }
    vidEnd = vid_free(NULL);
    // camera
    {
        int mw = (g_mapClip.right - g_mapClip.left) * ZS, mh = (g_mapClip.bottom - g_mapClip.top) * ZS;
        g_cx0 = N * TW / 2 - mw / 2; g_cy0 = N * TH / 2 - mh / 2;
        g_cx0 &= ~1; g_cy0 &= ~1;
        g_crange_x = imax(64, N * TW / 4 - mw / 2); g_crange_y = imax(32, N * TH / 4 - mh / 2);
        g_camx = g_cx0; g_camy = g_cy0; g_cvx = 2 * ZS; g_cvy = 1 * ZS;
    }
    memset(g_cnt, 0, sizeof(g_cnt));
    t0 = now_ms(); tEnd = t0 + 1000.0 + o_secs * 1000.0; last = lastFps = t0;
    while (!g_quit) {
        double a, b, c;
        pump();
        a = now_ms();
        if (a >= tEnd) break;
        if (tStart == 0 && a - t0 >= 1000.0) {
            tStart = a; counted = 0; sumDraw = sumPres = sumIdle = worst = 0;
            memcpy(cnt0, g_cnt, sizeof(cnt0)); crMs0 = g_crMs; crN0 = g_crN; veh0 = g_vehDrawn; g_chunkMiss = 0;
            g_uiComposeMs = g_uiComposeN = 0;
        }
        draw_frame(f);
        if (o_work) {   // simulated game logic while the blitter drains its queue
            volatile unsigned acc = 0; double w0 = now_ms();
            while (now_ms() - w0 < o_work) { int z; for (z = 0; z < 2000; z++) acc += (unsigned)z * 2654435761u; }
        }
        b = now_ms();
        if (doPres && !present()) break;
        c = now_ms();
        if (o_cap) {
            if (nextT == 0 || c > nextT + 1000.0 / o_cap) nextT = c;
            nextT += 1000.0 / o_cap;
            while (now_ms() < nextT - 2.0) Sleep(1);
            while (now_ms() < nextT) ;
            sumIdle += now_ms() - c;
            c = now_ms();
        }
        f++; counted++; fpsFrames++;
        sumDraw += b - a; sumPres += c - b;
        if (tStart != 0 && c - last > worst) worst = c - last;
        last = c;
        if (c - lastFps > 500) { sprintf(g_fpsText, "%.1f", fpsFrames * 1000.0 / (c - lastFps)); lastFps = c; fpsFrames = 0; }
    }
    if (g_quit && now_ms() < tEnd) { out("%-60s | aborted\n", g_args); dd_cleanup(); return; }
    {
        double el = now_ms() - tStart, tot = 0; int k;
        out("%-60s | %6.1f fps | draw %6.2f | pres %5.2f | idle %5.2f | frame %6.2f | worst %6.1f ms\n",
            g_args, counted * 1000.0 / el, sumDraw / counted, (sumPres - sumIdle) / counted, sumIdle / counted, el / counted, worst);
        buf[0] = 0;
        for (k = 0; k < C_N; k++) {
            double v = (g_cnt[k] - cnt0[k]) / counted;
            tot += v;
            if (v > 0.005) sprintf(buf + strlen(buf), "%s %.1f ", g_catName[k], v);
        }
        out("    blits/frame %.0f: %s| vehicles drawn %.1f\n", tot, buf, (g_vehDrawn - veh0) / counted);
        if (o_cache) out("    chunk renders %.0f in %.1f s (%.1f/s), avg %.2f ms each, misses %d\n", g_crN - crN0, el / 1000.0,
                         (g_crN - crN0) * 1000.0 / el, g_crN > crN0 ? (g_crMs - crMs0) / (g_crN - crN0) : 0.0, g_chunkMiss);
        if (o_ui == 2 && g_uiComposeN > 0) out("    ui compose %.2f ms each (%.0f times)\n", g_uiComposeMs / g_uiComposeN, g_uiComposeN);
        out("    vidmem total %lu KB, free after mode %lu KB, after art %lu KB, after pool %lu KB; art load %.0f ms%s; %s\n",
            g_vidTotal / 1024, g_vidAfterMode / 1024, vidAssets / 1024, vidEnd / 1024, g_loadMs,
            g_keyMismatch ? " (KEY MISMATCH)" : "", g_note);
    }
    if (o_cache) {   // "the player built something": re-render one chunk, timed to completion
        Chunk *c = &g_pool[0]; double a; double n0 = g_cnt[C_CREND];
        wait_blt(g_work);
        a = now_ms();
        for (i = 0; i < 30; i++) { render_chunk(c); wait_blt(c->s); }
        out("    chunk re-render (blitter-complete): %.2f ms per %dx%d chunk, %.0f blits\n", (now_ms() - a) / 30, CW, CH, (g_cnt[C_CREND] - n0) / 30);
    }
    dd_cleanup();
}

// ---------------------------------------------------------------- sweep: per-blit cost
static double sweep_frames(int kind, LPDIRECTDRAWSURFACE src, int sx, int sy, int w, int h, int dw, int dh, int keyed, int K)
{
    double a, b; long frames = 0; RECT r, d; DDBLTFX fx; int x, y, j;
    memset(&fx, 0, sizeof(fx)); fx.dwSize = sizeof(fx); fx.dwFillColor = conv(200, 50, 50);
    r.left = sx; r.top = sy; r.right = sx + w; r.bottom = sy + h;
    a = now_ms();
    while ((b = now_ms()) - a < 1200.0 || frames < 3) {
        for (j = 0; j < K; j++) {
            x = rnd() % (SW - dw + 1); y = rnd() % (SH - dh + 1);
            if (kind == 0) g_back->BltFast(x, y, src, &r, (keyed ? DDBLTFAST_SRCCOLORKEY : DDBLTFAST_NOCOLORKEY) | DDBLTFAST_WAIT);
            else if (kind == 1) { d.left = x; d.top = y; d.right = x + dw; d.bottom = y + dh; g_back->Blt(&d, src, &r, DDBLT_WAIT | (keyed ? DDBLT_KEYSRC : 0), NULL); }
            else { d.left = x; d.top = y; d.right = x + dw; d.bottom = y + dh; g_back->Blt(&d, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx); }
        }
        if (FAILED(g_prim->Flip(NULL, DDFLIP_WAIT))) g_prim->Restore();
        frames++;
        if (g_quit) break;
        pump();
    }
    return (b - a) / frames;
}

// K blits per frame + Flip; K doubles until a frame takes > 30 ms. Cost per blit = slope between
// the last two K values (removes the flip / vsync share).
static void sweep_one(const char *name, int kind, LPDIRECTDRAWSURFACE src, int sx, int sy, int w, int h, int dw, int dh, int keyed)
{
    int K = 25; double ms = 0, prev = 0, slope;
    for (;;) {
        prev = ms;
        ms = sweep_frames(kind, src, sx, sy, w, h, dw, dh, keyed, K);
        if (ms > 30.0 || K >= 6400) break;
        K *= 2;
    }
    slope = (ms - prev) * 1000.0 / (K / 2);
    out("  %-40s K=%5d: %6.2f ms/frame (%5.1f fps) | %7.2f us/blit (slope) %7.2f (avg) | %6.1f Mpix/s\n",
        name, K, ms, 1000.0 / ms, slope, ms * 1000.0 / K, slope > 0 ? dw * dh / slope : 0.0);
}

static void run_sweep(HINSTANCE inst)
{
    LPDIRECTDRAWSURFACE big, full, terSys;
    const short *r;
    if (!dd_open(inst, 1)) return;
    if (!load_assets()) { dd_cleanup(); return; }
    big = make_surface(512, 256, DDSCAPS_VIDEOMEMORY);
    full = make_surface(SW, SH, DDSCAPS_VIDEOMEMORY);
    terSys = load_bmp("ter64.bmp", DDSCAPS_SYSTEMMEMORY, 1, NULL, NULL);
    if (!big || !full) { fail("sweep surfaces", g_lastHr); dd_cleanup(); return; }
    out("== sweep %dx%dx16 fullscreen: K blits per frame + Flip ==\n", SW, SH);
    out("  %-40s %6.2f ms/frame\n", "Flip only (K=0)", sweep_frames(0, big, 0, 0, 1, 1, 1, 1, 0, 0));
    sweep_one("BltFast 64x32 keyed (tile)", 0, g_terS[0], 0, 0, 64, 32, 64, 32, 1);
    sweep_one("BltFast 64x32 opaque", 0, g_terS[0], 0, 0, 64, 32, 64, 32, 0);
    sweep_one("Blt 64x32 keyed (Blt not BltFast)", 1, g_terS[0], 0, 0, 64, 32, 64, 32, 1);
    sweep_one("BltFast 32x16 keyed (zoomed-out tile)", 0, g_terS[1], 0, 0, 32, 16, 32, 16, 1);
    r = OBJ_RECT[0][OB_CAR0]; sweep_one("BltFast 16x12 keyed (car)", 0, g_objS[0], r[0], r[1], r[2], r[3], r[2], r[3], 1);
    sweep_one("BltFast 8xGH keyed (glyph)", 0, g_fontS, GW * 1, 0, GW, GH, GW, GH, 1);
    r = OBJ_RECT[0][OB_HOUSE0]; sweep_one("BltFast 64x48 keyed (house)", 0, g_objS[0], r[0], r[1], r[2], r[3], r[2], r[3], 1);
    r = OBJ_RECT[0][OB_MID0]; sweep_one("BltFast 64x96 keyed", 0, g_objS[0], r[0], r[1], r[2], r[3], r[2], r[3], 1);
    r = OBJ_RECT[0][OB_TALL0]; sweep_one("BltFast 64x160 keyed", 0, g_objS[0], r[0], r[1], r[2], r[3], r[2], r[3], 1);
    r = OBJ_RECT[0][OB_BIG0]; sweep_one("BltFast 128x128 keyed", 0, g_objS[0], r[0], r[1], r[2], r[3], r[2], r[3], 1);
    sweep_one("BltFast 256x128 opaque (chunk)", 0, big, 0, 0, 256, 128, 256, 128, 0);
    sweep_one("BltFast 512x256 opaque (chunk)", 0, big, 0, 0, 512, 256, 512, 256, 0);
    sweep_one("BltFast full screen opaque", 0, full, 0, 0, SW, SH, SW, SH, 0);
    sweep_one("Blt stretch 64x32->32x16 keyed", 1, g_terS[0], 0, 0, 64, 32, 32, 16, 1);
    sweep_one("Blt stretch 512x256->256x128 opaque", 1, big, 0, 0, 512, 256, 256, 128, 0);
    sweep_one("Blt stretch 256x128->512x256 opaque", 1, big, 0, 0, 256, 128, 512, 256, 0);
    sweep_one("ColorFill 8x8", 2, NULL, 0, 0, 8, 8, 8, 8, 0);
    sweep_one("ColorFill 32x16", 2, NULL, 0, 0, 32, 16, 32, 16, 0);
    sweep_one("ColorFill full screen", 2, NULL, 0, 0, SW, SH, SW, SH, 0);
    if (terSys) {   // only a few: this path can stall for a second per blit
        double a = now_ms(); int j; RECT r; r.left = 0; r.top = 0; r.right = 64; r.bottom = 32;
        for (j = 0; j < 5 && now_ms() - a < 5000; j++) { g_back->BltFast(100, 100, terSys, &r, DDBLTFAST_SRCCOLORKEY | DDBLTFAST_WAIT); wait_blt(g_back); }
        out("  %-40s %.1f ms/blit (%d blits)\n", "BltFast 64x32 keyed from SYSTEM memory", (now_ms() - a) / j, j);
        a = now_ms();
        for (j = 0; j < 5 && now_ms() - a < 5000; j++) { g_back->BltFast(100, 100, terSys, &r, DDBLTFAST_NOCOLORKEY | DDBLTFAST_WAIT); wait_blt(g_back); }
        out("  %-40s %.1f ms/blit (%d blits)\n", "BltFast 64x32 opaque from SYSTEM memory", (now_ms() - a) / j, j);
    } else out("  sysmem 64x32 source: FAIL hr=0x%08lX\n", (unsigned long)g_lastHr);
    dd_cleanup();
}

// ---------------------------------------------------------------- surf: surface limits / VRAM use
static void run_surf(HINSTANCE inst)
{
    static const int widths[] = { 256, 512, 640, 800, 1024, 1280, 1600, 2048 };
    static const int wh[][2] = { { 64, 1024 }, { 64, 2048 }, { 64, 4096 }, { 512, 1024 }, { 512, 2048 }, { 640, 2048 }, { 0, 2048 } };
    static const int pk[][2] = { { 64, 64 }, { 256, 128 }, { 512, 256 }, { 0, 256 } };
    int i, n; DWORD f0, f1; LPDIRECTDRAWSURFACE s;
    if (!dd_open(inst, !o_win)) return;
    out("== surf %s %dx%dx16: vidmem total %lu KB, free %lu KB ==\n", o_win ? "windowed (desktop)" : "fullscreen", SW, SH,
        g_vidTotal / 1024, g_vidAfterMode / 1024);
    for (i = 0; i < 8; i++) {
        int w = widths[i], sysok;
        f0 = vid_free(NULL);
        s = make_surface(w, 64, DDSCAPS_VIDEOMEMORY);
        f1 = vid_free(NULL);
        if (s) out("  vidmem %4dx64   ok, used %4lu KB (nominal %3d KB)", w, (f0 - f1) / 1024, w * 64 * 2 / 1024);
        else out("  vidmem %4dx64   FAIL hr=0x%08lX                    ", w, (unsigned long)g_lastHr);
        if (s) free_surface(s);
        s = make_surface(w, 64, DDSCAPS_SYSTEMMEMORY); sysok = s != NULL; if (s) free_surface(s);
        out(" | sysmem %s\n", sysok ? "ok" : "FAIL");
    }
    for (i = 0; i < 7; i++) {
        int w = wh[i][0] ? wh[i][0] : SW, h = wh[i][1];
        f0 = vid_free(NULL);
        s = make_surface(w, h, DDSCAPS_VIDEOMEMORY);
        f1 = vid_free(NULL);
        if (s) { out("  vidmem %4dx%-4d ok, used %4lu KB\n", w, h, (f0 - f1) / 1024); free_surface(s); }
        else out("  vidmem %4dx%-4d FAIL hr=0x%08lX\n", w, h, (unsigned long)g_lastHr);
    }
    for (i = 0; i < 4; i++) {
        int w = pk[i][0] ? pk[i][0] : SW, h = pk[i][1], base = g_nreg;
        f0 = vid_free(NULL);
        for (n = 0; n < 4000 && g_nreg < 4190; n++) if (!make_surface(w, h, DDSCAPS_VIDEOMEMORY)) break;
        f1 = vid_free(NULL);
        out("  fill VRAM with %4dx%-3d: %4d surfaces = %6lu KB nominal, free dropped %6lu KB, left %lu KB\n", w, h, n,
            (unsigned long)n * w * h * 2 / 1024, (f0 - f1) / 1024, f1 / 1024);
        while (g_nreg > base) g_reg[--g_nreg]->Release();
    }
    dd_cleanup();
}

// ---------------------------------------------------------------- sim: CPU budget
static int SN, SL;
static unsigned char *s_road, *s_cond, *s_vis, *s_map8, *s_out8;
static unsigned short *s_dist, *s_tmp;
static int *s_q, s_src[64], s_plant[4], s_reach;
struct STile { unsigned char ter, obj, pop, lv, pol, crime, pwr, wat; };
static STile *s_tiles;

static void sim_bfs()
{
    int head = 0, tail = 0, i, p, x, y, n = SN * SN; unsigned short d;
    memset(s_dist, 0xFF, n * 2);
    for (i = 0; i < 64; i++) { p = s_src[i]; if (s_dist[p] == 0xFFFF) { s_dist[p] = 0; s_q[tail++] = p; } }
    while (head < tail) {
        p = s_q[head++]; d = (unsigned short)(s_dist[p] + 1); x = p & (SN - 1); y = p >> SL;
        if (x > 0 && s_road[p - 1] && s_dist[p - 1] == 0xFFFF) { s_dist[p - 1] = d; s_q[tail++] = p - 1; }
        if (x < SN - 1 && s_road[p + 1] && s_dist[p + 1] == 0xFFFF) { s_dist[p + 1] = d; s_q[tail++] = p + 1; }
        if (y > 0 && s_road[p - SN] && s_dist[p - SN] == 0xFFFF) { s_dist[p - SN] = d; s_q[tail++] = p - SN; }
        if (y < SN - 1 && s_road[p + SN] && s_dist[p + SN] == 0xFFFF) { s_dist[p + SN] = d; s_q[tail++] = p + SN; }
    }
    s_reach = tail;
}

static void sim_flood()
{
    int sp = 0, i, p, x, y, n = SN * SN, reached = 0;
    memset(s_vis, 0, n);
    for (i = 0; i < 4; i++) { p = s_plant[i]; if (s_cond[p] && !s_vis[p]) { s_vis[p] = 1; s_q[sp++] = p; } }
    while (sp) {
        p = s_q[--sp]; reached++; x = p & (SN - 1); y = p >> SL;
        if (x > 0 && s_cond[p - 1] && !s_vis[p - 1]) { s_vis[p - 1] = 1; s_q[sp++] = p - 1; }
        if (x < SN - 1 && s_cond[p + 1] && !s_vis[p + 1]) { s_vis[p + 1] = 1; s_q[sp++] = p + 1; }
        if (y > 0 && s_cond[p - SN] && !s_vis[p - SN]) { s_vis[p - SN] = 1; s_q[sp++] = p - SN; }
        if (y < SN - 1 && s_cond[p + SN] && !s_vis[p + SN]) { s_vis[p + SN] = 1; s_q[sp++] = p + SN; }
    }
    s_reach = reached;
}

static int s_acc[1024];
static void blur_r(int r)        // separable box blur radius r (3x3: r=1, 5x5: r=2), clamp-to-edge, row order
{
    int x, y, k, d = 2 * r + 1, mul = 65536 / (d * d) + 1, last = SN - 1;
    for (y = 0; y < SN; y++) {   // horizontal running sum into s_tmp
        const unsigned char *src = s_map8 + y * SN; unsigned short *t = s_tmp + y * SN; int sum = 0;
        for (k = -r; k <= r; k++) sum += src[k < 0 ? 0 : k];
        for (x = 0; x < r; x++) { t[x] = (unsigned short)sum; sum += src[x + r + 1] - src[x - r < 0 ? 0 : x - r]; }
        for (; x < SN - r - 1; x++) { t[x] = (unsigned short)sum; sum += src[x + r + 1] - src[x - r]; }
        for (; x < SN; x++) { t[x] = (unsigned short)sum; sum += src[x + r + 1 > last ? last : x + r + 1] - src[x - r]; }
    }
    for (x = 0; x < SN; x++) s_acc[x] = 0;   // vertical: running sum of whole rows
    for (k = -r; k <= r; k++) { const unsigned short *t = s_tmp + (k < 0 ? 0 : k) * SN; for (x = 0; x < SN; x++) s_acc[x] += t[x]; }
    for (y = 0; y < SN; y++) {
        unsigned char *o = s_out8 + y * SN;
        const unsigned short *add = s_tmp + (y + r + 1 > last ? last : y + r + 1) * SN, *sub = s_tmp + (y - r < 0 ? 0 : y - r) * SN;
        for (x = 0; x < SN; x++) { o[x] = (unsigned char)((s_acc[x] * mul) >> 16); s_acc[x] += add[x] - sub[x]; }
    }
}
static void sim_blur3() { blur_r(1); }
static void sim_blur5() { blur_r(2); }
static void sim_blur3naive()
{
    int x, y;
    for (y = 1; y < SN - 1; y++) for (x = 1; x < SN - 1; x++) {
        const unsigned char *p = s_map8 + y * SN + x;
        s_out8[y * SN + x] = (unsigned char)(((p[-SN - 1] + p[-SN] + p[-SN + 1] + p[-1] + p[0] + p[1] + p[SN - 1] + p[SN] + p[SN + 1]) * 7282) >> 16);
    }
}
static void sim_update()
{
    int i, n = SN * SN;
    for (i = 0; i < n; i++) {
        STile *t = &s_tiles[i]; int v, c, lv;
        v = (t->lv * 3 - t->pol * 2 - t->crime + (t->pwr ? 16 : -32) + (t->wat ? 8 : -16)) >> 2;
        if (v > t->pop) t->pop++; else if (v < t->pop && t->pop) t->pop--;
        c = (t->crime * 7 + 255 - t->lv + (t->pop >> 2)) >> 3;
        t->crime = (unsigned char)(c > 255 ? 255 : c);
        lv = t->lv + (s_out8[i] < 64) - (s_out8[i] > 128) + (s_dist[i] < 20);
        t->lv = (unsigned char)(lv < 0 ? 0 : lv > 255 ? 255 : lv);
        t->pol = s_out8[i];
    }
}
static void sim_tick() { sim_bfs(); sim_flood(); blur_r(1); sim_update(); }

static double bench(void (*fn)(), double minMs, int *iters)
{
    double a = now_ms(), b; int n = 0;
    do { fn(); n++; b = now_ms(); } while (b - a < minMs || n < 3);
    if (iters) *iters = n;
    return (b - a) / n;
}

static void sim_grid(int n)
{
    int i, it, nn = n * n, roads = 0, cond = 0; double ms;
    SN = n; SL = 0; while ((1 << SL) < n) SL++;
    s_road = (unsigned char *)malloc(nn); s_cond = (unsigned char *)malloc(nn); s_vis = (unsigned char *)malloc(nn);
    s_map8 = (unsigned char *)malloc(nn); s_out8 = (unsigned char *)malloc(nn);
    s_dist = (unsigned short *)malloc(nn * 2); s_tmp = (unsigned short *)malloc(nn * 2);
    s_q = (int *)malloc(nn * sizeof(int)); s_tiles = (STile *)malloc(nn * sizeof(STile));
    g_rs = 777;
    for (i = 0; i < nn; i++) {
        int x = i & (n - 1), y = i >> SL;
        s_road[i] = (unsigned char)(((x & 7) == 0 || (y & 7) == 0) && rnd() % 100 < 97);
        s_cond[i] = (unsigned char)(s_road[i] || rnd() % 100 < 65);
        s_map8[i] = (unsigned char)(rnd() & 255);
        roads += s_road[i]; cond += s_cond[i];
        s_tiles[i].ter = 0; s_tiles[i].obj = (unsigned char)(rnd() & 31); s_tiles[i].pop = (unsigned char)(rnd() & 127);
        s_tiles[i].lv = (unsigned char)(rnd() & 255); s_tiles[i].pol = 0; s_tiles[i].crime = (unsigned char)(rnd() & 63);
        s_tiles[i].pwr = (unsigned char)(rnd() & 1); s_tiles[i].wat = (unsigned char)(rnd() & 1);
    }
    for (i = 0; i < 64; i++) { do s_src[i] = rnd() % nn; while (!s_road[s_src[i]]); }
    for (i = 0; i < 4; i++) { do s_plant[i] = rnd() % nn; while (!s_cond[s_plant[i]]); }
    out("  grid %dx%d (%d tiles, %d%% road, %d%% conductive):\n", n, n, nn, roads * 100 / nn, cond * 100 / nn);
    ms = bench(sim_bfs, 500, &it);   out("    BFS distance map, 64 sources over roads  %7.3f ms  (reached %d)\n", ms, s_reach);
    ms = bench(sim_flood, 500, &it); out("    flood fill power connectivity, 4 plants  %7.3f ms  (reached %d)\n", ms, s_reach);
    ms = bench(sim_blur3, 500, &it); out("    box blur 3x3 separable (byte map)        %7.3f ms\n", ms);
    ms = bench(sim_blur5, 500, &it); out("    box blur 5x5 separable                   %7.3f ms\n", ms);
    ms = bench(sim_blur3naive, 500, &it); out("    box blur 3x3 naive 9-tap                 %7.3f ms\n", ms);
    ms = bench(sim_update, 500, &it); out("    per-tile update (8-byte tile, ~25 ops)   %7.3f ms  (%.1f ns/tile)\n", ms, ms * 1e6 / nn);
    ms = bench(sim_tick, 1000, &it); out("    full tick (BFS+flood+blur3+update)       %7.3f ms\n", ms);
    free(s_road); free(s_cond); free(s_vis); free(s_map8); free(s_out8); free(s_dist); free(s_tmp); free(s_q); free(s_tiles);
}

static unsigned char g_mmask[32][64];
static void pick_test()
{
    int x, y, i, n = 1000000, mism = 0, tw = 64, th = 32, hw = 32, hh = 16;
    int *px = (int *)malloc(n * sizeof(int)), *py = (int *)malloc(n * sizeof(int));
    double a, b, c; long sum1 = 0, sum2 = 0;
    for (y = 0; y < th; y++) for (x = 0; x < tw; x++) {
        double dx = fabs(x + 0.5 - hw) / hw, dy = fabs(y + 0.5 - hh) / hh;
        g_mmask[y][x] = (unsigned char)(dx + dy < 1.0 ? 0 : (y < hh ? (x < hw ? 1 : 2) : (x < hw ? 3 : 4)));
    }
    g_rs = 99;
    for (i = 0; i < n; i++) { px[i] = (rnd() * 3) % 8192; py[i] = (rnd() * 5) % 4096; }
    a = now_ms();
    for (i = 0; i < n; i++) {
        int cx = px[i] >> 6, cy = py[i] >> 5, tx = cx + cy, ty = cy - cx;
        switch (g_mmask[py[i] & 31][px[i] & 63]) { case 1: tx--; break; case 2: ty--; break; case 3: ty++; break; case 4: tx++; break; }
        sum1 += tx * 3 + ty;
    }
    b = now_ms();
    for (i = 0; i < n; i++) {
        double fa = ((px[i] + 0.5) / hw - 1 + (py[i] + 0.5) / hh) / 2, fb = ((py[i] + 0.5) / hh - (px[i] + 0.5) / hw + 1) / 2;
        int tx = (int)floor(fa), ty = (int)floor(fb);
        sum2 += tx * 3 + ty;
    }
    c = now_ms();
    for (i = 0; i < n; i++) {   // verify per point
        int cx = px[i] >> 6, cy = py[i] >> 5, tx = cx + cy, ty = cy - cx;
        double fa = ((px[i] + 0.5) / hw - 1 + (py[i] + 0.5) / hh) / 2, fb = ((py[i] + 0.5) / hh - (px[i] + 0.5) / hw + 1) / 2;
        switch (g_mmask[py[i] & 31][px[i] & 63]) { case 1: tx--; break; case 2: ty--; break; case 3: ty++; break; case 4: tx++; break; }
        if (tx != (int)floor(fa) || ty != (int)floor(fb)) mism++;
    }
    out("  picking (64x32 mouse-map lookup): %.3f us/pick; float formula %.3f us/pick; mismatches %d of %d (checksums %ld/%ld)\n",
        (b - a) * 1000.0 / n, (c - b) * 1000.0 / n, mism, n, sum1, sum2);
    free(px); free(py);
}

static void mem_test()
{
    MEMORYSTATUS ms; void *blk[16]; int i, nb = 0; double a, t;
    ms.dwLength = sizeof(ms); GlobalMemoryStatus(&ms);
    out("  memory: phys %lu KB total, %lu KB free; page file free %lu KB; virtual free %lu KB; load %lu%%\n",
        ms.dwTotalPhys / 1024, ms.dwAvailPhys / 1024, ms.dwAvailPageFile / 1024, ms.dwAvailVirtual / 1024, ms.dwMemoryLoad);
    for (i = 0; i < 12; i++) {
        blk[i] = malloc(8 << 20);
        if (!blk[i]) { out("    malloc 8 MB #%d failed\n", i + 1); break; }
        nb++;
        a = now_ms(); memset(blk[i], i + 1, 8 << 20); t = now_ms() - a;
        out("    allocated+touched %3d MB: last 8 MB took %7.1f ms (%.0f MB/s)\n", nb * 8, t, 8000.0 / t);
        if (t > 4000) break;
    }
    a = now_ms();   // re-touch everything once: shows whether earlier blocks were swapped out
    for (i = 0; i < nb; i++) memset(blk[i], 0, 8 << 20);
    out("    re-touch all %d MB: %.1f ms\n", nb * 8, now_ms() - a);
    for (i = 0; i < nb; i++) free(blk[i]);
}

static void run_sim()
{
    out("== sim (CPU, no rendering) ==\n");
    sim_grid(128);
    sim_grid(256);
    pick_test();
    mem_test();
}

// ---------------------------------------------------------------- info
static HRESULT WINAPI enum_modes(LPDDSURFACEDESC d, LPVOID ctx)
{
    char *s = (char *)ctx;
    if (strlen(s) < 1900) sprintf(s + strlen(s), " %ldx%ldx%ld@%ld", d->dwWidth, d->dwHeight, d->ddpfPixelFormat.dwRGBBitCount, d->dwRefreshRate);
    return DDENUMRET_OK;
}
static void run_info()
{
    MEMORYSTATUS ms; static char buf[2048]; LPDIRECTDRAW dd; DDCAPS hel, hal;
    out("== info ==\n");
    ms.dwLength = sizeof(ms); GlobalMemoryStatus(&ms);
    out("  RAM: %lu KB total, %lu KB free\n", (unsigned long)(ms.dwTotalPhys / 1024), (unsigned long)(ms.dwAvailPhys / 1024));
    out("  desktop: %dx%d\n", g_deskW, g_deskH);
    if (SUCCEEDED(DirectDrawCreate(NULL, &dd, NULL))) {
        memset(&hal, 0, sizeof(hal)); hal.dwSize = sizeof(hal);
        memset(&hel, 0, sizeof(hel)); hel.dwSize = sizeof(hel);
        dd->GetCaps(&hal, &hel);
        out("  vidmem total %lu KB free %lu KB\n", hal.dwVidMemTotal / 1024, hal.dwVidMemFree / 1024);
        out("  HAL: BLT=%d STRETCH=%d (stretchX caps 0x%08lX) COLORFILL=%d CKEY_SRCBLT=%d CANBLTSYSMEM=%d\n",
            !!(hal.dwCaps & DDCAPS_BLT), !!(hal.dwCaps & DDCAPS_BLTSTRETCH), hal.dwFXCaps, !!(hal.dwCaps & DDCAPS_BLTCOLORFILL),
            !!(hal.dwCKeyCaps & DDCKEYCAPS_SRCBLT), !!(hal.dwCaps & DDCAPS_CANBLTSYSMEM));
        out("  HAL FX: ARITHSTRETCHY=%d STRETCHX=%d SHRINKX=%d STRETCHXN=%d SHRINKXN=%d\n",
            !!(hal.dwFXCaps & DDFXCAPS_BLTARITHSTRETCHY), !!(hal.dwFXCaps & DDFXCAPS_BLTSTRETCHX), !!(hal.dwFXCaps & DDFXCAPS_BLTSHRINKX),
            !!(hal.dwFXCaps & DDFXCAPS_BLTSTRETCHXN), !!(hal.dwFXCaps & DDFXCAPS_BLTSHRINKXN));
        buf[0] = 0;
        dd->EnumDisplayModes(0, NULL, buf, enum_modes);
        out("  display modes:%s\n", buf);
        dd->Release();
    } else out("  DirectDrawCreate FAILED\n");
}

// ---------------------------------------------------------------- main
static void parse(char *cmd)
{
    char *tok = strtok(cmd, " ");
    if (tok) strncpy(g_backend, tok, 15);
    while ((tok = strtok(NULL, " ")) != NULL) {
        char *eq = strchr(tok, '=');
        if (!eq) continue;
        *eq++ = 0;
        if (!strcmp(tok, "res")) o_res = atoi(eq);
        else if (!strcmp(tok, "win")) o_win = atoi(eq);
        else if (!strcmp(tok, "tile")) o_tile = atoi(eq);
        else if (!strcmp(tok, "cache")) o_cache = atoi(eq);
        else if (!strcmp(tok, "chunk")) o_chunk = atoi(eq);
        else if (!strcmp(tok, "pool")) o_pool = atoi(eq);
        else if (!strcmp(tok, "ui")) o_ui = atoi(eq);
        else if (!strcmp(tok, "veh")) o_veh = atoi(eq);
        else if (!strcmp(tok, "bld")) o_bld = atoi(eq);
        else if (!strcmp(tok, "map")) o_map = atoi(eq);
        else if (!strcmp(tok, "scroll")) o_scroll = atoi(eq);
        else if (!strcmp(tok, "occl")) o_occl = atoi(eq);
        else if (!strcmp(tok, "cap")) o_cap = atoi(eq);
        else if (!strcmp(tok, "t")) o_secs = atoi(eq);
        else if (!strcmp(tok, "work")) o_work = atoi(eq);
        else if (!strcmp(tok, "zoom")) strncpy(o_zoom, eq, 15);
        else if (!strcmp(tok, "ovl")) strncpy(o_ovl, eq, 15);
        else if (!strcmp(tok, "mode")) strncpy(o_mode, eq, 15);
    }
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmdline, int)
{
    char cmd[256];
    GetModuleFileName(NULL, g_dir, MAX_PATH);
    strrchr(g_dir, '\\')[1] = 0;
    sprintf(g_resPath, "%sRESULT.TXT", g_dir);
    QueryPerformanceFrequency(&g_qf);
    strncpy(g_args, cmdline, 255); strncpy(cmd, cmdline, 255); cmd[255] = 0;
    parse(cmd);
    g_deskW = GetSystemMetrics(SM_CXSCREEN); g_deskH = GetSystemMetrics(SM_CYSCREEN);
    SW = o_res; SH = o_res == 1024 ? 768 : o_res == 640 ? 480 : o_res == 1280 ? 1024 : 600;
    if (o_map != 128 && o_map != 256 && o_map != 512) o_map = 256;
    timeBeginPeriod(1);
    if (!strcmp(g_backend, "info")) run_info();
    else if (!strcmp(g_backend, "sim")) run_sim();
    else if (!strcmp(g_backend, "surf")) run_surf(inst);
    else if (!strcmp(g_backend, "sweep")) run_sweep(inst);
    else if (!strcmp(g_backend, "iso")) run_iso(inst);
    else out("unknown backend '%s'\n", g_backend);
    timeEndPeriod(1);
    if (g_wnd) DestroyWindow(g_wnd);
    return 0;
}
