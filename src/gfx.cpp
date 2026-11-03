/* gfx.cpp - blit-oriented 2D drawing (see gfx.h for the model). WINDOWS SIDE. Owner: T01.
   Ported from SIROCCO ../sandline/src/gfx.cpp (SIROCCO T01): colour packing, unrolled software kernels,
   keyed spans, BMP load/save, stipple half-fills, Bresenham lines, GDI glyph capture, surface restore.
   KAUPUNKI changes: one VRAM surface per image (atlases <= 640 wide, any height: no 512 splitting),
   hardware render TARGETS (BltFast/ColorFill into an image surface), VRAM accounting with a loud
   failure instead of the system-memory fallback, runtime screen size, generic bitmap fonts (GfxFont)
   for T03's glyph atlases, debug font built on GfxFont, software mouse cursor.

   Destination of a drawing call: hw_dst() = the back buffer or a hardware target's surface (BltFast,
   Blt COLORFILL), otherwise a software view (get_soft) = art image px, a GDI DIB, or a locked surface
   for pixel-type calls. A software lock stays until the next blit-type call or the present. */
#include "common.h"
#include "gfx.h"
#include "plat.h"
#include <ddraw.h>
#include <math.h>
#include "util.h"

#ifndef NONANTIALIASED_QUALITY
#define NONANTIALIASED_QUALITY 3
#endif

typedef struct { PIX *px; int w, h, pitch; } ST;   /* software target (pitch in pixels) */

static int   s_init;
static int   s_hw;                            /* DirectDraw backend */
static LPDIRECTDRAW        s_dd;
static LPDIRECTDRAWSURFACE s_back;
static int   s_555;
static PIX   s_halfMask = 0x7BEF;             /* clears each channel's top bit after >> 1 */
static PIX   s_key = 0xF81F;                  /* gfx_rgb(255,0,255) in 565 */
static int   s_sw = 800, s_sh = 600;

static Img  *s_tgt;                           /* NULL = screen */
static int   s_tw = 800, s_th = 600;
static int   s_cx0, s_cy0, s_cx1 = 800, s_cy1 = 600;
static int   s_ox, s_oy;

static LPDIRECTDRAWSURFACE s_lockSurf;        /* surface locked for software drawing */
static ST    s_lockST;
static int   s_nBlits, s_nLocks;
static Img  *s_reg;                           /* registry of images with surfaces */
static int   s_serial, s_vramUsed, s_vramFails, s_warnWide, s_warnSrc;
static unsigned s_useClock;

/* ================================================================ colours */
PIX gfx_rgb(int r, int g, int b)
{
    r = ICLAMP(r, 0, 255); g = ICLAMP(g, 0, 255); b = ICLAMP(b, 0, 255);
    if (s_555) return (PIX)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
    return (PIX)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void gfx_unpack(PIX c, int *r, int *g, int *b)
{
    int r5, g6, b5;
    if (s_555) {
        r5 = (c >> 10) & 31; g6 = (c >> 5) & 31; b5 = c & 31;
        *r = (r5 << 3) | (r5 >> 2); *g = (g6 << 3) | (g6 >> 2); *b = (b5 << 3) | (b5 >> 2);
    } else {
        r5 = (c >> 11) & 31; g6 = (c >> 5) & 63; b5 = c & 31;
        *r = (r5 << 3) | (r5 >> 2); *g = (g6 << 2) | (g6 >> 4); *b = (b5 << 3) | (b5 >> 2);
    }
}

PIX gfx_scale(PIX c, int f256)
{
    int r, g, b;
    gfx_unpack(c, &r, &g, &b);
    if (f256 < 0) f256 = 0;
    return gfx_rgb((r * f256) >> 8, (g * f256) >> 8, (b * f256) >> 8);
}

PIX gfx_mix(PIX a, PIX b, int f)
{
    int ra, ga, ba, rb, gb, bb, ia;
    if (f <= 0) return a;
    if (f >= 256) return b;
    ia = 256 - f;
    if (s_555) {
        ra = (a >> 10) & 31; ga = (a >> 5) & 31; ba = a & 31;
        rb = (b >> 10) & 31; gb = (b >> 5) & 31; bb = b & 31;
        return (PIX)((((ra * ia + rb * f) >> 8) << 10) | (((ga * ia + gb * f) >> 8) << 5) | ((ba * ia + bb * f) >> 8));
    }
    ra = (a >> 11) & 31; ga = (a >> 5) & 63; ba = a & 31;
    rb = (b >> 11) & 31; gb = (b >> 5) & 63; bb = b & 31;
    return (PIX)((((ra * ia + rb * f) >> 8) << 11) | (((ga * ia + gb * f) >> 8) << 5) | ((ba * ia + bb * f) >> 8));
}

PIX gfx_key(void) { return s_key; }
int gfx_hw(void) { return s_hw; }
int gfx_screen_w(void) { return s_sw; }
int gfx_screen_h(void) { return s_sh; }

/* ================================================================ software row kernels (SIROCCO) */
static __forceinline void fill16(PIX *p, int n, PIX c)
{
    DWORD v, *d;
    int k;
    if (n <= 0) return;
    if ((DWORD)p & 2) { *p++ = c; n--; }
    v = c | ((DWORD)c << 16); d = (DWORD *)p;
    k = n >> 1;
    while (k >= 8) { d[0] = v; d[1] = v; d[2] = v; d[3] = v; d[4] = v; d[5] = v; d[6] = v; d[7] = v; d += 8; k -= 8; }
    while (k--) *d++ = v;
    if (n & 1) *(PIX *)d = c;
}

static __forceinline void copy16(PIX *d, const PIX *s, int n)
{
    DWORD *dd;
    int k;
    if (n <= 0) return;
    if ((DWORD)d & 2) { *d++ = *s++; n--; }
    dd = (DWORD *)d; k = n >> 1;
    if (!((DWORD)s & 2)) {
        const DWORD *ss = (const DWORD *)s;
        while (k >= 8) { dd[0] = ss[0]; dd[1] = ss[1]; dd[2] = ss[2]; dd[3] = ss[3]; dd[4] = ss[4]; dd[5] = ss[5]; dd[6] = ss[6]; dd[7] = ss[7]; dd += 8; ss += 8; k -= 8; }
        while (k--) *dd++ = *ss++;
        if (n & 1) *(PIX *)dd = *(const PIX *)ss;
    } else {
        while (k >= 4) {
            dd[0] = s[0] | ((DWORD)s[1] << 16); dd[1] = s[2] | ((DWORD)s[3] << 16);
            dd[2] = s[4] | ((DWORD)s[5] << 16); dd[3] = s[6] | ((DWORD)s[7] << 16);
            dd += 4; s += 8; k -= 4;
        }
        while (k--) { *dd++ = s[0] | ((DWORD)s[1] << 16); s += 2; }
        if (n & 1) *(PIX *)dd = *s;
    }
}

static __forceinline PIX half_px(PIX a, PIX c) { return (PIX)(((a >> 1) & s_halfMask) + ((c >> 1) & s_halfMask)); }

static __forceinline void half16(PIX *p, int n, PIX c)
{
    DWORD m, hc, *d;
    int k;
    if (n <= 0) return;
    if ((DWORD)p & 2) { *p = half_px(*p, c); p++; n--; }
    m = s_halfMask | ((DWORD)s_halfMask << 16);
    hc = (c >> 1) & s_halfMask; hc |= hc << 16;
    d = (DWORD *)p; k = n >> 1;
    while (k >= 4) {
        d[0] = ((d[0] >> 1) & m) + hc; d[1] = ((d[1] >> 1) & m) + hc;
        d[2] = ((d[2] >> 1) & m) + hc; d[3] = ((d[3] >> 1) & m) + hc;
        d += 4; k -= 4;
    }
    while (k--) { *d = ((*d >> 1) & m) + hc; d++; }
    if (n & 1) { PIX *q = (PIX *)d; *q = half_px(*q, c); }
}

/* ================================================================ locks / destinations */
void gfx_unlock(void)
{
    if (s_lockSurf) { s_lockSurf->Unlock(NULL); s_lockSurf = NULL; }
}

static int lock_surf(LPDIRECTDRAWSURFACE s, int w, int h, ST *st)
{
    DDSURFACEDESC d;
    HRESULT hr;
    if (!s) return 0;
    if (s_lockSurf == s) { *st = s_lockST; return 1; }
    gfx_unlock();
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    hr = s->Lock(NULL, &d, DDLOCK_WAIT, NULL);
    if (hr == DDERR_SURFACELOST) { plat_note_lost(); return 0; }
    if (FAILED(hr)) return 0;
    s_lockST.px = (PIX *)d.lpSurface; s_lockST.pitch = d.lPitch / 2;
    s_lockST.w = w; s_lockST.h = h;
    s_lockSurf = s; s_nLocks++;
    *st = s_lockST;
    return 1;
}

/* hardware destination of the current target, or NULL (software) */
static LPDIRECTDRAWSURFACE hw_dst(void)
{
    if (!s_hw) return NULL;
    if (!s_tgt) return s_back;
    if (s_tgt->target) return (LPDIRECTDRAWSURFACE)s_tgt->surf;
    return NULL;
}

/* software view of the current target (locks a hw surface if needed) */
static int get_soft(ST *st)
{
    if (s_tgt) {
        if (s_tgt->px) {
            st->px = s_tgt->px; st->w = s_tgt->w; st->h = s_tgt->h; st->pitch = s_tgt->w;
            if (!s_tgt->target) s_tgt->dirty = 1;
            return 1;
        }
        return lock_surf((LPDIRECTDRAWSURFACE)s_tgt->surf, s_tgt->w, s_tgt->h, st);
    }
    if (s_hw) return lock_surf(s_back, s_sw, s_sh, st);
    st->px = plat_gdi_bits();
    if (!st->px) return 0;
    st->w = s_sw; st->h = s_sh; st->pitch = s_sw;
    return 1;
}

/* ================================================================ image surfaces */
static void free_spans(Img *im)
{
    if (im->span) { free(im->span); im->span = NULL; }
    if (im->row) { free(im->row); im->row = NULL; }
}

static void build_spans(Img *im)
{
    int x, y, n = 0, cap;
    free_spans(im);
    if (!im->keyed || !im->px) return;
    cap = im->h * 4 + 64;
    im->row = (int *)malloc((im->h + 1) * sizeof(int));
    im->span = (u16 *)malloc(cap * 2 * sizeof(u16));
    if (!im->row || !im->span) { log_printf("gfx: error: out of memory (spans)"); free_spans(im); return; }
    for (y = 0; y < im->h; y++) {
        const PIX *p = im->px + y * im->w;
        int start = -1;
        im->row[y] = n;
        for (x = 0; x <= im->w; x++) {
            int op = x < im->w && p[x] != s_key;
            if (op && start < 0) start = x;
            else if (!op && start >= 0) {
                if (n >= cap) {
                    u16 *ns;
                    cap *= 2;
                    ns = (u16 *)realloc(im->span, cap * 2 * sizeof(u16));
                    if (!ns) { log_printf("gfx: error: out of memory (spans)"); free_spans(im); return; }
                    im->span = ns;
                }
                im->span[n * 2] = (u16)start; im->span[n * 2 + 1] = (u16)(x - start);
                n++; start = -1;
            }
        }
    }
    im->row[im->h] = n;
}

static int vram_free_kb(void)
{
    DDCAPS hal;
    if (!s_dd) return -1;
    memset(&hal, 0, sizeof(hal)); hal.dwSize = sizeof(hal);
    if (FAILED(s_dd->GetCaps(&hal, NULL))) return -1;
    return (int)(hal.dwVidMemFree / 1024);
}

static void set_key(LPDIRECTDRAWSURFACE s, int keyed)
{
    if (keyed) {
        DDCOLORKEY ck;
        ck.dwColorSpaceLowValue = ck.dwColorSpaceHighValue = s_key;
        s->SetColorKey(DDCKEY_SRCBLT, &ck);
    } else s->SetColorKey(DDCKEY_SRCBLT, NULL);
}

static void reg_link(Img *im)
{
    im->prev = NULL; im->next = s_reg;
    if (s_reg) s_reg->prev = im;
    s_reg = im;
}

static void release_surface(Img *im)
{
    if (!im->surf) return;
    if (s_lockSurf == (LPDIRECTDRAWSURFACE)im->surf) gfx_unlock();
    ((LPDIRECTDRAWSURFACE)im->surf)->Release();
    im->surf = NULL;
    s_vramUsed -= im->vram_kb; im->vram_kb = 0;
    if (im->prev) im->prev->next = im->next; else if (s_reg == im) s_reg = im->next;
    if (im->next) im->next->prev = im->prev;
    im->next = im->prev = NULL;
}

/* one VRAM surface for im (hardware backend); 0 = refused (logged loudly) */
static int alloc_surface(Img *im, const char *what)
{
    DDSURFACEDESC d;
    LPDIRECTDRAWSURFACE s = NULL;
    HRESULT hr;
    int kb;
    if (!s_hw || !s_dd) return 1;
    kb = (im->w * im->h * 2 + 1023) / 1024;
    if (im->w > s_sw) {
        log_printf("gfx: error: %s %dx%d is wider than the screen (%d): VRAM surfaces can't be (probe 1)", what, im->w, im->h, s_sw);
        return 0;
    }
    if (!im->target && im->w > ATLAS_MAX_W && !s_warnWide++)
        log_printf("gfx: warning: %s is %d px wide; art must be <= %d for 640x480", what, im->w, ATLAS_MAX_W);
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    d.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT; d.dwWidth = im->w; d.dwHeight = im->h;
    d.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY;
    hr = s_dd->CreateSurface(&d, &s, NULL);
    if (FAILED(hr) || !s) {
        s_vramFails++;
        log_printf("gfx: error: %s: %s %dx%d needs %d KB; gfx uses %d KB, %d KB free (hr 0x%08lx)",
                   hr == DDERR_OUTOFVIDEOMEMORY ? "VRAM full" : "CreateSurface failed", what, im->w, im->h, kb,
                   s_vramUsed, vram_free_kb(), (unsigned long)hr);
        return 0;
    }
    if (im->keyed) set_key(s, 1);
    im->surf = s;
    im->vram_kb = kb;
    s_vramUsed += kb;
    reg_link(im);
    return 1;
}

static void upload(Img *im)
{
    LPDIRECTDRAWSURFACE s = (LPDIRECTDRAWSURFACE)im->surf;
    DDSURFACEDESC d;
    HRESULT hr;
    int y;
    if (!s || !im->px) return;
    if (s_lockSurf == s) gfx_unlock();
    memset(&d, 0, sizeof(d)); d.dwSize = sizeof(d);
    hr = s->Lock(NULL, &d, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
    if (hr == DDERR_SURFACELOST) { s->Restore(); hr = s->Lock(NULL, &d, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL); }
    if (FAILED(hr)) return;
    for (y = 0; y < im->h; y++)
        memcpy((char *)d.lpSurface + y * d.lPitch, im->px + y * im->w, im->w * sizeof(PIX));
    s->Unlock(NULL);
}

/* make the VRAM copy (and spans) reflect px */
static void refresh(Img *im)
{
    if (!im->dirty) return;
    im->dirty = 0;
    free_spans(im);                      /* rebuilt lazily by the next software blit */
    if (im->surf) upload(im);
}

void gfx_img_update(Img *im) { if (im && im->px) { im->dirty = 1; refresh(im); } }

void gfx_img_set_keyed(Img *im, int keyed)
{
    if (!im) return;
    im->keyed = keyed ? 1 : 0;
    free_spans(im);
    if (im->surf) set_key((LPDIRECTDRAWSURFACE)im->surf, im->keyed);
}

static void hw_fill_surf(LPDIRECTDRAWSURFACE dst, int x0, int y0, int x1, int y1, PIX c)
{
    DDBLTFX fx;
    RECT r;
    HRESULT hr;
    gfx_unlock();
    memset(&fx, 0, sizeof(fx)); fx.dwSize = sizeof(fx); fx.dwFillColor = c;
    r.left = x0; r.top = y0; r.right = x1; r.bottom = y1;
    hr = dst->Blt(&r, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    if (hr == DDERR_SURFACELOST) plat_note_lost();
    s_nBlits++;
}

int gfx_restore_all(void)
{
    Img *im;
    int lost = 0, art = 0, tg = 0, failed = 0;
    gfx_unlock();
    for (im = s_reg; im; im = im->next) {
        LPDIRECTDRAWSURFACE s = (LPDIRECTDRAWSURFACE)im->surf;
        if (!s || s->IsLost() != DDERR_SURFACELOST) continue;
        lost++;
        if (FAILED(s->Restore())) { failed++; continue; }
        if (im->px && !im->target) { upload(im); art++; }
        else { hw_fill_surf(s, 0, 0, im->w, im->h, 0); tg++; }
    }
    if (lost) {
        s_serial++;
        log_printf("gfx: %d lost surfaces restored: %d art re-uploaded from system memory, %d targets cleared for "
                   "re-render, %d failed (restore serial %d)", lost, art, tg, failed, s_serial);
    }
    return lost;
}

int gfx_restore_serial(void) { return s_serial; }

static int new_img(int w, int h, int keyed, Img *out, const char *what)
{
    memset(out, 0, sizeof(*out));
    if (w <= 0 || h <= 0) return 0;
    if (s_hw && w > s_sw) {
        log_printf("gfx: error: %s %dx%d is wider than the screen (%d)", what, w, h, s_sw);
        return 0;
    }
    out->px = (PIX *)malloc(w * h * sizeof(PIX));
    if (!out->px) { log_printf("gfx: error: out of memory (%s %dx%d)", what, w, h); return 0; }
    out->w = w; out->h = h; out->keyed = keyed ? 1 : 0;
    return 1;
}

/* px filled: create + upload the VRAM copy */
static int finish_img(Img *out, const char *what)
{
    if (!alloc_surface(out, what)) { gfx_free_img(out); return 0; }
    if (out->surf) upload(out);
    out->dirty = 0;
    return 1;
}

/* T13: art residency without copying the pixels (art.cpp zoom swap at 1024x768) */
int gfx_img_evict(Img *im)
{
    if (!im || !im->px || im->target) return 0;
    if (s_tgt == im) gfx_set_target(NULL);
    release_surface(im);
    return 1;
}

int gfx_img_restore(Img *im)
{
    if (!im || !im->px || im->target) return 0;
    if (im->surf || !s_hw) return 1;
    if (!alloc_surface(im, "image")) return 0;
    upload(im);
    im->dirty = 0;
    return 1;
}

int gfx_make_img(int w, int h, int keyed, Img *out)
{
    int i;
    if (!new_img(w, h, keyed, out, "image")) return 0;
    for (i = 0; i < w * h; i++) out->px[i] = keyed ? s_key : 0;
    return finish_img(out, "image");
}

int gfx_make_target(int w, int h, Img *out)
{
    memset(out, 0, sizeof(*out));
    if (w <= 0 || h <= 0) return 0;
    out->w = w; out->h = h; out->target = 1;
    if (s_hw) {
        if (!alloc_surface(out, "target")) { memset(out, 0, sizeof(*out)); return 0; }
        hw_fill_surf((LPDIRECTDRAWSURFACE)out->surf, 0, 0, w, h, 0);
        return 1;
    }
    out->px = (PIX *)calloc(w * h, sizeof(PIX));
    if (!out->px) { log_printf("gfx: error: out of memory (target %dx%d)", w, h); memset(out, 0, sizeof(*out)); return 0; }
    return 1;
}

void gfx_free_img(Img *im)
{
    if (!im) return;
    if (s_tgt == im) gfx_set_target(NULL);
    release_surface(im);
    if (im->px) free(im->px);
    free_spans(im);
    memset(im, 0, sizeof(*im));
}

static int s_loadNoVram;          /* F3: gfx_load_bmp_sys: pixels only, no VRAM surface (= an evicted image) */
int gfx_load_bmp_sys(const char *path, Img *out, int keyed)
{
    int r;
    s_loadNoVram = 1;
    r = gfx_load_bmp(path, out, keyed);
    s_loadNoVram = 0;
    return r;
}

int gfx_load_bmp(const char *path, Img *out, int keyed)
{
    FILE *f;
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    unsigned char *row = NULL;
    int w, h, topDown, stride, x, y, ok = 0;
    double t0 = time_ms();

    memset(out, 0, sizeof(*out));
    f = fopen(path, "rb");
    if (!f) { log_printf("gfx: error: cannot open %s", path); return 0; }
    if (fread(&fh, sizeof(fh), 1, f) != 1 || fh.bfType != 0x4D42 ||
        fread(&ih, sizeof(ih), 1, f) != 1) { log_printf("gfx: error: %s: not a BMP", path); fclose(f); return 0; }
    if (ih.biBitCount != 24 || ih.biCompression != BI_RGB || ih.biWidth <= 0 || ih.biHeight == 0) {
        log_printf("gfx: error: %s: need 24-bit BI_RGB (got %d bpp, compression %lu)", path, ih.biBitCount, ih.biCompression);
        fclose(f); return 0;
    }
    w = ih.biWidth; topDown = ih.biHeight < 0; h = topDown ? -ih.biHeight : ih.biHeight;
    stride = (w * 3 + 3) & ~3;
    if (!new_img(w, h, keyed, out, path)) { fclose(f); return 0; }
    row = (unsigned char *)malloc(stride);
    if (!row) goto done;
    fseek(f, fh.bfOffBits, SEEK_SET);
    for (y = 0; y < h; y++) {
        PIX *d = out->px + (topDown ? y : h - 1 - y) * w;
        const unsigned char *s = row;
        if (fread(row, stride, 1, f) != 1) { log_printf("gfx: error: %s: truncated", path); goto done; }
        for (x = 0; x < w; x++, s += 3) {
            int b = s[0], g = s[1], r = s[2];
            PIX p;
            if (keyed && r == 255 && g == 0 && b == 255) { d[x] = s_key; continue; }
            if (s_555) p = (PIX)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
            else       p = (PIX)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            if (keyed && p == s_key) p ^= 1;          /* never collide with the key */
            d[x] = p;
        }
    }
    ok = 1;
done:
    if (row) free(row);
    fclose(f);
    if (!ok) { gfx_free_img(out); return 0; }
    if (s_loadNoVram) out->dirty = 0;         /* F3: system memory only; gfx_img_restore makes the surface later */
    else if (!finish_img(out, path)) return 0;
    log_printf("gfx: loaded %s %dx%d%s, %d KB VRAM, %.0f ms (gfx total %d KB, free %d KB)", path, w, h,
               keyed ? " keyed" : "", out->vram_kb, time_ms() - t0, s_vramUsed, vram_free_kb());
    return 1;
}

/* ================================================================ target / clip / origin */
void gfx_set_target(Img *im)
{
    Img *old = s_tgt;
    if (im && !im->px && !im->surf) im = NULL;
    gfx_unlock();
    s_tgt = im;
    if (old && old != im && !old->target) refresh(old);
    s_tw = im ? im->w : s_sw; s_th = im ? im->h : s_sh;
    s_ox = s_oy = 0;
    gfx_reset_clip();
}

Img *gfx_target(void) { return s_tgt; }

void gfx_set_clip(int x0, int y0, int x1, int y1)
{
    s_cx0 = ICLAMP(x0, 0, s_tw); s_cy0 = ICLAMP(y0, 0, s_th);
    s_cx1 = ICLAMP(x1, 0, s_tw); s_cy1 = ICLAMP(y1, 0, s_th);
}

void gfx_reset_clip(void) { s_cx0 = 0; s_cy0 = 0; s_cx1 = s_tw; s_cy1 = s_th; }
void gfx_set_origin(int ox, int oy) { s_ox = ox; s_oy = oy; }
void gfx_get_clip(int *x0, int *y0, int *x1, int *y1) { *x0 = s_cx0; *y0 = s_cy0; *x1 = s_cx1; *y1 = s_cy1; }
void gfx_get_origin(int *ox, int *oy) { *ox = s_ox; *oy = s_oy; }

void gfx_stats(int *blits, int *locks)
{
    if (blits) *blits = s_nBlits;
    if (locks) *locks = s_nLocks;
    s_nBlits = 0; s_nLocks = 0;
}

/* ================================================================ low-level ops (target coords, clipped) */
static void hw_blit(LPDIRECTDRAWSURFACE dst, const Img *im, int sx, int sy, int w, int h, int dx, int dy)
{
    RECT r;
    HRESULT hr;
    DWORD flags = (im->keyed ? DDBLTFAST_SRCCOLORKEY : DDBLTFAST_NOCOLORKEY) | DDBLTFAST_WAIT;
    gfx_unlock();
    r.left = sx; r.top = sy; r.right = sx + w; r.bottom = sy + h;
    hr = dst->BltFast(dx, dy, (LPDIRECTDRAWSURFACE)im->surf, &r, flags);
    if (hr == DDERR_SURFACELOST) plat_note_lost();
    s_nBlits++;
}

static void sw_blit(const ST *t, Img *im, int sx, int sy, int w, int h, int dx, int dy)
{
    int y, sx1;
    PIX *drow;
    const PIX *srow;
    if (im->keyed && !im->span) build_spans(im);
    if (!im->keyed || !im->span) {
        drow = t->px + dy * t->pitch + dx; srow = im->px + sy * im->w + sx;
        for (y = 0; y < h; y++, drow += t->pitch, srow += im->w) copy16(drow, srow, w);
        return;
    }
    sx1 = sx + w;
    drow = t->px + dy * t->pitch + dx - sx;        /* drow[x] = destination of source column x */
    srow = im->px + sy * im->w;
    for (y = 0; y < h; y++, drow += t->pitch, srow += im->w) {
        const u16 *s = im->span + 2 * im->row[sy + y], *e = im->span + 2 * im->row[sy + y + 1];
        for (; s < e; s += 2) {
            int a = s[0], b = a + s[1];
            if (b <= sx) continue;
            if (a >= sx1) break;
            if (a < sx) a = sx;
            if (b > sx1) b = sx1;
            copy16(drow + a, srow + a, b - a);
        }
    }
}

/* clip a source rect + destination (target coords) against the image and the clip rect */
static int clip_blit(const Img *im, int *sx, int *sy, int *w, int *h, int *dx, int *dy)
{
    if (*sx < 0) { *dx -= *sx; *w += *sx; *sx = 0; }
    if (*sy < 0) { *dy -= *sy; *h += *sy; *sy = 0; }
    if (*sx + *w > im->w) *w = im->w - *sx;
    if (*sy + *h > im->h) *h = im->h - *sy;
    if (*dx < s_cx0) { *sx += s_cx0 - *dx; *w -= s_cx0 - *dx; *dx = s_cx0; }
    if (*dy < s_cy0) { *sy += s_cy0 - *dy; *h -= s_cy0 - *dy; *dy = s_cy0; }
    if (*dx + *w > s_cx1) *w = s_cx1 - *dx;
    if (*dy + *h > s_cy1) *h = s_cy1 - *dy;
    return *w > 0 && *h > 0;
}

/* blit with target coordinates (origin already applied) */
static void blit_t(Img *im, int sx, int sy, int w, int h, int dx, int dy)
{
    LPDIRECTDRAWSURFACE dst;
    ST t;
    if (!im || im == s_tgt || (!im->px && !im->surf)) return;
    if (!clip_blit(im, &sx, &sy, &w, &h, &dx, &dy)) return;
    if (im->dirty) refresh(im);
    dst = hw_dst();
    if (dst) {
        if (!im->surf) {
            if (!s_warnSrc++) log_printf("gfx: warning: blit from an image without a VRAM surface ignored");
            return;
        }
        hw_blit(dst, im, sx, sy, w, h, dx, dy);
        return;
    }
    if (!im->px) {
        if (!s_warnSrc++) log_printf("gfx: warning: a VRAM target can't be blitted into a software target");
        return;
    }
    if (get_soft(&t)) sw_blit(&t, im, sx, sy, w, h, dx, dy);
}

/* ---- stipple patterns for 50% fills on hardware (SIROCCO) */
#define STIP_W 258
#define STIP_H 128
#define NSTIP  8
typedef struct { int used; PIX c; unsigned last; Img img; } Stip;
static Stip s_stip[NSTIP];

static int s_stipFailWait;             /* F3: calls to skip after all stipple sizes failed (no log spam) */
static Img *stipple(PIX c)
{
    int i, x, y, best = 0;
    Stip *st;
    for (i = 0; i < NSTIP; i++) if (s_stip[i].used && s_stip[i].c == c) { s_stip[i].last = ++s_useClock; return &s_stip[i].img; }
    if (s_stipFailWait > 0) { s_stipFailWait--; return NULL; }
    for (i = 0; i < NSTIP; i++) {
        if (!s_stip[i].used) { best = i; break; }
        if (s_stip[i].last < s_stip[best].last) best = i;
    }
    st = &s_stip[best];
    if (st->used) gfx_free_img(&st->img);
    st->used = 0;
    {   /* F3: VRAM is tight at 800x600 with the 64-zoom art resident (T08: the windowed pause menu got no
           258x128 stipple): fall back to shorter strips (more blits, same picture) before giving up */
        static const int hs[3] = { STIP_H, 32, 8 };
        int k, h;
        for (k = 0; k < 3; k++) {
            h = hs[k];
            if (!new_img(STIP_W, h, 1, &st->img, "stipple")) return NULL;
            for (y = 0; y < h; y++) for (x = 0; x < STIP_W; x++)
                st->img.px[y * STIP_W + x] = ((x + y) & 1) ? s_key : (c == s_key ? (PIX)(c ^ 1) : c);
            if (finish_img(&st->img, "stipple")) break;
        }
        if (k == 3) { s_stipFailWait = 600; return NULL; }   /* no dim for ~10 s of calls, then retry */
        if (k) log_printf("gfx: stipple fallback 258x%d (VRAM short)", h);
    }
    st->used = 1; st->c = c; st->last = ++s_useClock;
    return &st->img;
}

static void hw_half(LPDIRECTDRAWSURFACE dst, int x0, int y0, int x1, int y1, PIX c)
{
    Img *st = stipple(c);
    int x, y, ph = (x0 + y0) & 1;
    if (!st) return;
    for (y = y0; y < y1; y += st->h) {                     /* F3: strip height of the stipple we got */
        int h = y1 - y < st->h ? y1 - y : st->h;
        for (x = x0; x < x1; x += STIP_W - 2) {
            int w = x1 - x < STIP_W - 2 ? x1 - x : STIP_W - 2;
            hw_blit(dst, st, ph, 0, w, h, x, y);
        }
    }
}

/* rect ops in target coords (origin applied, not clipped). mode 0 fill, 1 half */
static void rect_t(int x0, int y0, int x1, int y1, PIX c, int mode)
{
    LPDIRECTDRAWSURFACE dst;
    ST t;
    int y;
    if (x0 < s_cx0) x0 = s_cx0;
    if (y0 < s_cy0) y0 = s_cy0;
    if (x1 > s_cx1) x1 = s_cx1;
    if (y1 > s_cy1) y1 = s_cy1;
    if (x0 >= x1 || y0 >= y1) return;
    dst = hw_dst();
    if (dst) {
        if (mode == 0) hw_fill_surf(dst, x0, y0, x1, y1, c);
        else hw_half(dst, x0, y0, x1, y1, c);
        return;
    }
    if (!get_soft(&t)) return;
    for (y = y0; y < y1; y++) {
        PIX *p = t.px + y * t.pitch + x0;
        if (mode == 0) fill16(p, x1 - x0, c);
        else half16(p, x1 - x0, c);
    }
}

/* ================================================================ public primitives */
void gfx_clear(PIX c)
{
    LPDIRECTDRAWSURFACE dst = hw_dst();
    ST t;
    int y;
    if (dst) { hw_fill_surf(dst, 0, 0, s_tw, s_th, c); return; }
    if (!get_soft(&t)) return;
    for (y = 0; y < t.h; y++) fill16(t.px + y * t.pitch, t.w, c);
}

void gfx_fill(int x0, int y0, int x1, int y1, PIX c)      { rect_t(x0 - s_ox, y0 - s_oy, x1 - s_ox, y1 - s_oy, c, 0); }
void gfx_fill_half(int x0, int y0, int x1, int y1, PIX c) { rect_t(x0 - s_ox, y0 - s_oy, x1 - s_ox, y1 - s_oy, c, 1); }

void gfx_rect(int x0, int y0, int x1, int y1, PIX c)
{
    if (x1 - x0 <= 0 || y1 - y0 <= 0) return;
    gfx_fill(x0, y0, x1, y0 + 1, c);
    gfx_fill(x0, y1 - 1, x1, y1, c);
    gfx_fill(x0, y0 + 1, x0 + 1, y1 - 1, c);
    gfx_fill(x1 - 1, y0 + 1, x1, y1 - 1, c);
}

void gfx_pixel(int x, int y, PIX c)
{
    ST t;
    x -= s_ox; y -= s_oy;
    if (x < s_cx0 || x >= s_cx1 || y < s_cy0 || y >= s_cy1) return;
    if (get_soft(&t)) t.px[y * t.pitch + x] = c;
}

/* ---- lines: Cohen-Sutherland against the clip rect, then Bresenham (SIROCCO) */
static int outcode(int x, int y)
{
    int c = 0;
    if (x < s_cx0) c |= 1; else if (x >= s_cx1) c |= 2;
    if (y < s_cy0) c |= 4; else if (y >= s_cy1) c |= 8;
    return c;
}

static int clip_line(int *px0, int *py0, int *px1, int *py1)
{
    double x0 = *px0, y0 = *py0, x1 = *px1, y1 = *py1;
    int c0 = outcode(*px0, *py0), c1 = outcode(*px1, *py1), guard = 0;
    if (s_cx0 >= s_cx1 || s_cy0 >= s_cy1) return 0;
    while (c0 | c1) {
        int c;
        double x, y;
        if (c0 & c1) return 0;
        if (++guard > 8) return 0;
        c = c0 ? c0 : c1;
        if (c & 8)      { y = s_cy1 - 1; x = x0 + (x1 - x0) * (y - y0) / (y1 - y0); }
        else if (c & 4) { y = s_cy0;     x = x0 + (x1 - x0) * (y - y0) / (y1 - y0); }
        else if (c & 2) { x = s_cx1 - 1; y = y0 + (y1 - y0) * (x - x0) / (x1 - x0); }
        else            { x = s_cx0;     y = y0 + (y1 - y0) * (x - x0) / (x1 - x0); }
        if (c == c0) { x0 = x; y0 = y; c0 = outcode((int)floor(x0 + 0.5), (int)floor(y0 + 0.5)); }
        else         { x1 = x; y1 = y; c1 = outcode((int)floor(x1 + 0.5), (int)floor(y1 + 0.5)); }
    }
    *px0 = (int)floor(x0 + 0.5); *py0 = (int)floor(y0 + 0.5);
    *px1 = (int)floor(x1 + 0.5); *py1 = (int)floor(y1 + 0.5);
    return 1;
}

void gfx_line(int x0, int y0, int x1, int y1, PIX c)
{
    int dx, dy, sx, sy, err, e2;
    PIX *p;
    ST t;
    x0 -= s_ox; y0 -= s_oy; x1 -= s_ox; y1 -= s_oy;
    if (!clip_line(&x0, &y0, &x1, &y1)) return;
    if (!get_soft(&t)) return;
    dx = x1 > x0 ? x1 - x0 : x0 - x1; sx = x0 < x1 ? 1 : -1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0; sy = y0 < y1 ? t.pitch : -t.pitch;   /* dy <= 0 */
    err = dx + dy;
    p = t.px + y0 * t.pitch + x0;
    for (;;) {
        *p = c;
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { if (x0 == x1) break; err += dy; x0 += sx; p += sx; }
        if (e2 <= dx) { if (y0 == y1) break; err += dx; y0 += (sy > 0 ? 1 : -1); p += sy; }
    }
}

/* ---- image blits */
void gfx_blit_part(const Img *im, int sx, int sy, int w, int h, int dx, int dy)
{
    blit_t((Img *)im, sx, sy, w, h, dx - s_ox, dy - s_oy);
}

void gfx_blit(const Img *im, int x, int y)
{
    if (im) blit_t((Img *)im, 0, 0, im->w, im->h, x - s_ox, y - s_oy);
}

int gfx_peek(const Img *im, int x, int y)
{
    ST t;
    int v;
    if (!im) {
        if (x < 0 || y < 0 || x >= s_sw || y >= s_sh) return -1;
        if (s_hw) { if (!lock_surf(s_back, s_sw, s_sh, &t)) return -1; }
        else { t.px = plat_gdi_bits(); t.pitch = s_sw; if (!t.px) return -1; }
    } else {
        if (x < 0 || y < 0 || x >= im->w || y >= im->h) return -1;
        if (im->dirty) refresh((Img *)im);
        if (im->surf) { if (!lock_surf((LPDIRECTDRAWSURFACE)im->surf, im->w, im->h, &t)) return -1; }
        else if (im->px) return im->px[y * im->w + x];
        else return -1;
    }
    v = t.px[y * t.pitch + x];
    gfx_unlock();
    return v;
}

/* ================================================================ bitmap fonts */
void gfx_font_init(GfxFont *f, const Img *atlas, int line_h, int baseline)
{
    memset(f, 0, sizeof(*f));
    f->img = atlas; f->line_h = line_h; f->baseline = baseline;
}

void gfx_font_glyph(GfxFont *f, int code, int x, int y, int w, int h, int ox, int oy, int adv)
{
    code &= 255;
    f->gx[code] = (s16)x; f->gy[code] = (s16)y;
    f->gw[code] = (u8)ICLAMP(w, 0, 255); f->gh[code] = (u8)ICLAMP(h, 0, 255);
    f->ox[code] = (s8)ICLAMP(ox, -128, 127); f->oy[code] = (s8)ICLAMP(oy, -128, 127);
    f->adv[code] = (u8)ICLAMP(adv, 0, 255);
}

static int text_impl(const GfxFont *f, int x, int y, const char *s, int n, int draw)
{
    int pen = x, widest = 0, i, vis;
    if (!f || !s) return 0;
    vis = draw && f->img && y - s_oy < s_cy1 && y + f->line_h - s_oy > s_cy0;
    for (i = 0; s[i] && (n < 0 || i < n); i++) {
        int c = (u8)s[i];
        if (c == '\n') {
            if (pen - x > widest) widest = pen - x;
            pen = x; y += f->line_h;
            vis = draw && f->img && y - s_oy < s_cy1 && y + f->line_h - s_oy > s_cy0;
            continue;
        }
        if (vis && f->gw[c]) gfx_blit_part(f->img, f->gx[c], f->gy[c], f->gw[c], f->gh[c], pen + f->ox[c], y + f->oy[c]);
        pen += f->adv[c];
    }
    if (pen - x > widest) widest = pen - x;
    return widest;
}

int gfx_text(const GfxFont *f, int x, int y, const char *s) { return text_impl(f, x, y, s, -1, 1); }
int gfx_text_n(const GfxFont *f, int x, int y, const char *s, int n) { return text_impl(f, x, y, s, n, 1); }
int gfx_text_width(const GfxFont *f, const char *s) { return text_impl(f, 0, 0, s, -1, 0); }

/* ---- debug font: Tahoma 11 captured with GDI into a coverage mask (SIROCCO build_font), one keyed
   atlas per colour built from the mask on first use */
#define DF_FIRST 32
#define DF_LAST  126
#define DF_N     (DF_LAST - DF_FIRST + 1)
#define DF_COLS  16
#define DF_PAD   2
#define CAP_W    64
#define CAP_H    32
#define NDBG     8
typedef struct { int used; PIX c; unsigned last; Img img; GfxFont font; } DbgAtlas;
static DbgAtlas s_dbg[NDBG];
static u8  *s_dfMask;                 /* atlas-sized coverage mask (1 = ink) */
static int  s_dfW, s_dfH, s_dfCellW, s_dfAdv[DF_N], s_dfInk[DF_N];

static void build_debug_font(void)
{
    BITMAPINFO bi;
    void *bits = NULL;
    HDC sdc, dc;
    HBITMAP bmp;
    HGDIOBJ ob, of;
    HFONT hf;
    TEXTMETRIC tm;
    int c, x, y, maxAdv = 0, rows;
    DWORD *cap;
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = CAP_W; bi.bmiHeader.biHeight = -CAP_H;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    sdc = GetDC(NULL);
    dc = CreateCompatibleDC(sdc);
    bmp = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, sdc);
    hf = CreateFont(-11, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                    NONANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Tahoma");
    if (!dc || !bmp || !bits || !hf) {
        log_printf("gfx: warning: debug font capture failed");
        if (hf) DeleteObject(hf);
        if (dc) DeleteDC(dc);
        if (bmp) DeleteObject(bmp);
        return;
    }
    cap = (DWORD *)bits;
    ob = SelectObject(dc, bmp);
    of = SelectObject(dc, hf);
    GetTextMetrics(dc, &tm);
    s_dfH = IMIN((int)tm.tmHeight, CAP_H);
    for (c = DF_FIRST; c <= DF_LAST; c++) {
        char ch = (char)c;
        SIZE sz;
        GetTextExtentPoint32(dc, &ch, 1, &sz);
        s_dfAdv[c - DF_FIRST] = sz.cx;
        if (sz.cx > maxAdv) maxAdv = sz.cx;
    }
    s_dfCellW = maxAdv + 2 * DF_PAD + 2;
    rows = (DF_N + DF_COLS - 1) / DF_COLS;
    s_dfW = DF_COLS * s_dfCellW;
    s_dfMask = (u8 *)calloc(s_dfW * rows * s_dfH, 1);
    if (s_dfMask) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        for (c = DF_FIRST; c <= DF_LAST; c++) {
            char ch = (char)c;
            int g = c - DF_FIRST, cx0 = (g % DF_COLS) * s_dfCellW, cy0 = (g / DF_COLS) * s_dfH;
            memset(cap, 0, CAP_W * CAP_H * 4);
            TextOut(dc, DF_PAD, 0, &ch, 1);
            GdiFlush();
            for (y = 0; y < s_dfH; y++) for (x = 0; x < s_dfCellW && x < CAP_W; x++)
                if (((cap[y * CAP_W + x] >> 8) & 255) >= 128) { s_dfMask[(cy0 + y) * s_dfW + cx0 + x] = 1; s_dfInk[g] = 1; }
        }
    }
    SelectObject(dc, of);
    SelectObject(dc, ob);
    DeleteObject(hf);
    DeleteObject(bmp);
    DeleteDC(dc);
}

const GfxFont *gfx_debug_font(PIX c)
{
    int i, best = 0, g, rows;
    DbgAtlas *a;
    if (!s_dfMask) return NULL;
    for (i = 0; i < NDBG; i++) if (s_dbg[i].used && s_dbg[i].c == c) { s_dbg[i].last = ++s_useClock; return &s_dbg[i].font; }
    for (i = 0; i < NDBG; i++) {
        if (!s_dbg[i].used) { best = i; break; }
        if (s_dbg[i].last < s_dbg[best].last) best = i;
    }
    a = &s_dbg[best];
    if (a->used) gfx_free_img(&a->img);
    a->used = 0;
    rows = (DF_N + DF_COLS - 1) / DF_COLS;
    if (!new_img(s_dfW, rows * s_dfH, 1, &a->img, "debug font")) return NULL;
    for (i = 0; i < a->img.w * a->img.h; i++) a->img.px[i] = s_dfMask[i] ? (c == s_key ? (PIX)(c ^ 1) : c) : s_key;
    if (!finish_img(&a->img, "debug font")) return NULL;
    gfx_font_init(&a->font, &a->img, s_dfH, s_dfH - 2);
    for (g = 0; g < DF_N; g++)
        gfx_font_glyph(&a->font, DF_FIRST + g, (g % DF_COLS) * s_dfCellW, (g / DF_COLS) * s_dfH,
                       s_dfInk[g] ? s_dfCellW : 0, s_dfH, -DF_PAD, 0, s_dfAdv[g]);
    a->used = 1; a->c = c; a->last = ++s_useClock;
    return &a->font;
}

int gfx_debug_text(int x, int y, const char *s, PIX c)
{
    const GfxFont *f = gfx_debug_font(c);
    return f ? gfx_text(f, x, y, s) : 0;
}

int gfx_debug_text_h(void) { return s_dfH; }

/* ================================================================ cursor */
static const char *const s_arrowMap[] = {
    "X          ", "XX         ", "X.X        ", "X..X       ", "X...X      ", "X....X     ",
    "X.....X    ", "X......X   ", "X.......X  ", "X........X ", "X.....XXXXX", "X..X..X    ",
    "X.X X..X   ", "XX  X..X   ", "X    X..X  ", "     X..X  ", "      XX   " };
static Img        s_arrow;
static const Img *s_curImg;
static int        s_curSx, s_curSy, s_curW, s_curH, s_curHx, s_curHy, s_curShow = 1, s_curCustom;

static void build_arrow(void)
{
    int x, y, h = COUNTOF(s_arrowMap), w = 11;
    if (!gfx_make_img(w, h, 1, &s_arrow)) return;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        char ch = s_arrowMap[y][x];
        s_arrow.px[y * w + x] = ch == 'X' ? gfx_rgb(0, 0, 0) : ch == '.' ? gfx_rgb(255, 255, 255) : s_key;
    }
    gfx_img_update(&s_arrow);
    gfx_cursor_set(NULL, 0, 0, 0, 0, 0, 0);
}

void gfx_cursor_set(const Img *im, int sx, int sy, int w, int h, int hotx, int hoty)
{
    s_curCustom = im != NULL;
    if (!im) { im = &s_arrow; sx = 0; sy = 0; w = s_arrow.w; h = s_arrow.h; hotx = 0; hoty = 0; }
    s_curImg = im; s_curSx = sx; s_curSy = sy; s_curW = w; s_curH = h; s_curHx = hotx; s_curHy = hoty;
    plat_hide_os_cursor(s_curCustom);
}

void gfx_cursor_show(int on) { s_curShow = on; }

void gfx_cursor_draw(int x, int y)
{
    Img *t = s_tgt;
    int c0, c1, c2, c3, ox, oy;
    if (!s_curShow || !s_curImg || (plat_backend() != PB_DD_FULL && !s_curCustom)) return;
    gfx_get_clip(&c0, &c1, &c2, &c3); gfx_get_origin(&ox, &oy);
    if (t) gfx_set_target(NULL);
    gfx_reset_clip(); gfx_set_origin(0, 0);
    gfx_blit_part(s_curImg, s_curSx, s_curSy, s_curW, s_curH, x - s_curHx, y - s_curHy);
    if (t) gfx_set_target(t);
    gfx_set_clip(c0, c1, c2, c3); gfx_set_origin(ox, oy);
}

/* ================================================================ init / shutdown / stats / dump */
int gfx_init(void)
{
    s_sw = plat_screen_w(); s_sh = plat_screen_h();
    s_555 = plat_pixel_format() == PF_555;
    s_halfMask = s_555 ? 0x3DEF : 0x7BEF;
    s_key = gfx_rgb(255, 0, 255);
    s_dd = (LPDIRECTDRAW)plat_dd();
    s_back = (LPDIRECTDRAWSURFACE)plat_dd_back();
    s_hw = s_dd && s_back;
    s_tgt = NULL;
    s_init = 1;
    gfx_set_target(NULL);
    build_debug_font();
    build_arrow();
    log_printf("gfx: %s, %s, screen %dx%d, debug font h=%d, VRAM free %d of %d KB", s_hw ? "hardware blits" : "software",
               s_555 ? "RGB555" : "RGB565", s_sw, s_sh, s_dfH, gfx_vram_free_kb(), gfx_vram_total_kb());
    return 1;
}

void gfx_shutdown(void)
{
    int i;
    if (!s_init) return;
    gfx_unlock();
    for (i = 0; i < NDBG; i++) if (s_dbg[i].used) { gfx_free_img(&s_dbg[i].img); s_dbg[i].used = 0; }
    for (i = 0; i < NSTIP; i++) if (s_stip[i].used) { gfx_free_img(&s_stip[i].img); s_stip[i].used = 0; }
    gfx_free_img(&s_arrow);
    if (s_dfMask) { free(s_dfMask); s_dfMask = NULL; }
    /* release the surfaces of images the modules still hold (pixels stay valid) */
    while (s_reg) release_surface(s_reg);
    s_hw = 0; s_dd = NULL; s_back = NULL; s_tgt = NULL;
    s_init = 0;
}

int gfx_vram_free_kb(void) { return vram_free_kb(); }

int gfx_vram_total_kb(void)
{
    DDCAPS hal;
    if (!s_dd) return -1;
    memset(&hal, 0, sizeof(hal)); hal.dwSize = sizeof(hal);
    if (FAILED(s_dd->GetCaps(&hal, NULL))) return -1;
    return (int)(hal.dwVidMemTotal / 1024);
}

int gfx_vram_used_kb(void) { return s_vramUsed; }
int gfx_vram_failures(void) { return s_vramFails; }

int gfx_img_count(int *art, int *targets)
{
    Img *im;
    int a = 0, t = 0;
    for (im = s_reg; im; im = im->next) { if (im->target) t++; else a++; }
    if (art) *art = a;
    if (targets) *targets = t;
    return a + t;
}

void gfx_log_vram(const char *when)
{
    int a, t;
    gfx_img_count(&a, &t);
    log_printf("gfx: vram %s: gfx uses %d KB (%d art images, %d targets), free %d of %d KB, %d refused",
               when ? when : "", s_vramUsed, a, t, gfx_vram_free_kb(), gfx_vram_total_kb(), s_vramFails);
}

int gfx_save_bmp(const char *path)
{
    BITMAPFILEHEADER fh;
    BITMAPINFOHEADER ih;
    int stride = (s_sw * 3 + 3) & ~3, x, y, r, g, b;
    unsigned char *row;
    FILE *f;
    ST t;
    Img *oldT = s_tgt;
    if (oldT) gfx_set_target(NULL);
    if (!get_soft(&t)) { log_printf("gfx: error: save_bmp: screen not readable"); return 0; }
    f = fopen(path, "wb");
    if (!f) { log_printf("gfx: error: cannot write %s", path); gfx_unlock(); return 0; }
    row = (unsigned char *)calloc(stride, 1);
    if (!row) { fclose(f); gfx_unlock(); return 0; }
    ZeroMemory(&fh, sizeof(fh)); ZeroMemory(&ih, sizeof(ih));
    fh.bfType = 0x4D42; fh.bfOffBits = sizeof(fh) + sizeof(ih); fh.bfSize = fh.bfOffBits + stride * s_sh;
    ih.biSize = sizeof(ih); ih.biWidth = s_sw; ih.biHeight = s_sh; ih.biPlanes = 1;
    ih.biBitCount = 24; ih.biCompression = BI_RGB; ih.biSizeImage = stride * s_sh;
    fwrite(&fh, sizeof(fh), 1, f);
    fwrite(&ih, sizeof(ih), 1, f);
    for (y = s_sh - 1; y >= 0; y--) {
        const PIX *s = t.px + y * t.pitch;
        for (x = 0; x < s_sw; x++) {
            gfx_unpack(s[x], &r, &g, &b);
            row[x * 3] = (unsigned char)b; row[x * 3 + 1] = (unsigned char)g; row[x * 3 + 2] = (unsigned char)r;
        }
        fwrite(row, stride, 1, f);
    }
    free(row);
    fclose(f);
    gfx_unlock();
    if (oldT) gfx_set_target(oldT);
    log_printf("gfx: saved %s", path);
    return 1;
}
