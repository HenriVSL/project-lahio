/* vramprobe.cpp - PL4: Voodoo3 VRAM heap layout probe (offscreen surface placement per display mode).
   vramprobe.exe W H "spec" out.txt
   spec: comma-separated WxH[*N] surfaces allocated in order (video memory, offscreen plain); "|" frees all
   and starts a new sequence. Each allocation logs ok/FAIL, the surface's byte offset from the primary's
   memory (Lock), its pitch, and dwVidMemFree after. Fullscreen 16-bit, primary + 1 back buffer. */
#include <windows.h>
#include <ddraw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LPDIRECTDRAW s_dd;
static LPDIRECTDRAWSURFACE s_prim, s_back, s_surf[512];
static int s_n;
static FILE *s_f;
static unsigned long s_base;

static long vfree(void)
{
    DDCAPS hal, hel;
    memset(&hal, 0, sizeof(hal)); hal.dwSize = sizeof(hal);
    memset(&hel, 0, sizeof(hel)); hel.dwSize = sizeof(hel);
    if (FAILED(s_dd->GetCaps(&hal, &hel))) return -1;
    return (long)(hal.dwVidMemFree / 1024);
}

static unsigned long addr_of(LPDIRECTDRAWSURFACE s, long *pitch)
{
    DDSURFACEDESC d;
    unsigned long a;
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    if (FAILED(s->Lock(NULL, &d, DDLOCK_WAIT, NULL))) { *pitch = -1; return 0; }
    a = (unsigned long)d.lpSurface; *pitch = d.lPitch;
    s->Unlock(NULL);
    return a;
}

static void alloc_one(int w, int h)
{
    DDSURFACEDESC d;
    LPDIRECTDRAWSURFACE s = NULL;
    HRESULT hr;
    long pitch = 0;
    unsigned long a;
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    d.dwWidth = w; d.dwHeight = h;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    hr = s_dd->CreateSurface(&d, &s, NULL);
    if (FAILED(hr) || !s) { fprintf(s_f, "  %3d %4dx%-4d FAIL hr=%08lx free %ld KB\n", s_n, w, h, (unsigned long)hr, vfree()); return; }
    a = addr_of(s, &pitch);
    fprintf(s_f, "  %3d %4dx%-4d ok   off %8lu (row %5lu col %4lu) pitch %5ld  free %ld KB\n", s_n, w, h,
            a - s_base, pitch > 0 ? (a - s_base) / (unsigned long)pitch : 0, pitch > 0 ? ((a - s_base) % (unsigned long)pitch) / 2 : 0, pitch, vfree());
    if (s_n < 512) s_surf[s_n++] = s; else s->Release();
}

static void free_all(void)
{
    int i;
    for (i = 0; i < s_n; i++) s_surf[i]->Release();
    s_n = 0;
    fprintf(s_f, "  -- freed all, free %ld KB\n", vfree());
}

static LRESULT CALLBACK wp(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProc(h, m, w, l); }

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    int W = 800, H = 600;
    char spec[4096], out[260], *p;
    WNDCLASS wc;
    HWND hwnd;
    DDSURFACEDESC d;
    DDSCAPS c;
    long pitch;
    HRESULT hr;
    (void)hp; (void)show;
    spec[0] = 0; strcpy(out, "vramprobe.txt");
    sscanf(cmd, "%d %d %4095s %259s", &W, &H, spec, out);
    s_f = fopen(out, "w");
    if (!s_f) return 1;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wp; wc.hInstance = hi; wc.lpszClassName = "vramprobe";
    RegisterClass(&wc);
    hwnd = CreateWindow("vramprobe", "vramprobe", WS_POPUP, 0, 0, W, H, NULL, NULL, hi, NULL);
    ShowWindow(hwnd, SW_SHOWNORMAL); ShowWindow(hwnd, SW_SHOWNORMAL);
    if (FAILED(hr = DirectDrawCreate(NULL, &s_dd, NULL))) { fprintf(s_f, "DirectDrawCreate %08lx\n", (unsigned long)hr); return 1; }
    s_dd->SetCooperativeLevel(hwnd, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    if (FAILED(hr = s_dd->SetDisplayMode(W, H, 16))) { fprintf(s_f, "SetDisplayMode %08lx\n", (unsigned long)hr); return 1; }
    fprintf(s_f, "mode %dx%d: free before primary %ld KB\n", W, H, vfree());
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    d.dwFlags = DDSD_CAPS | DDSD_BACKBUFFERCOUNT; d.dwBackBufferCount = 1;
    d.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE | DDSCAPS_FLIP | DDSCAPS_COMPLEX;
    if (FAILED(hr = s_dd->CreateSurface(&d, &s_prim, NULL))) { fprintf(s_f, "primary %08lx\n", (unsigned long)hr); return 1; }
    c.dwCaps = DDSCAPS_BACKBUFFER;
    s_prim->GetAttachedSurface(&c, &s_back);
    s_base = addr_of(s_prim, &pitch);
    fprintf(s_f, "primary pitch %ld, free %ld KB\n", pitch, vfree());
    { unsigned long b = addr_of(s_back, &pitch); fprintf(s_f, "back off %lu pitch %ld\n", b - s_base, pitch); }
    for (p = strtok(spec, ","); p; p = strtok(NULL, ",")) {
        int w = 0, h = 0, n = 1, i;
        if (*p == '|') { free_all(); p++; if (!*p) continue; }
        if (sscanf(p, "%dx%d*%d", &w, &h, &n) < 2) continue;
        fprintf(s_f, "%s\n", p);
        for (i = 0; i < n; i++) alloc_one(w, h);
        fflush(s_f);
    }
    free_all();
    s_back = NULL;
    s_prim->Release();
    s_dd->RestoreDisplayMode();
    s_dd->SetCooperativeLevel(hwnd, DDSCL_NORMAL);
    s_dd->Release();
    fclose(s_f);
    DestroyWindow(hwnd);
    return 0;
}
