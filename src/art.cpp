/* art.cpp - art sheets of both zooms + UI sheets + fonts in VRAM, sprite lookup by NAME (hash table),
   lot-sprite naming fallbacks, anchored drawing, selftest. WINDOWS SIDE. Owner: T05. art.h has the contract.

   Ids: every distinct sprite name gets one id (world names are shared by both zooms, UI names are valid
   for both zooms). Id 0 is the placeholder (a pink diamond per zoom). Lookups hash the name (FNV-1a,
   open addressing); art_lot_sprite results should be cached by the caller (render.cpp does).

   Zoom residency (T05 addition, art.h): art_set_resident_zoom(z) evicts the world sheets of the other
   zoom from VRAM (their pixels move into a heap buffer) and uploads the wanted ones. render.cpp uses it
   only when VRAM is too short for both zooms + the chunk pool (1024x768 on the 14 MB Voodoo3). */
#include "common.h"
#include "art.h"
#include "util.h"
#include "config.h"
#include "defs.h"
#include "art_world.h"
#include "art_ui.h"

#define ART_MAX_SHEETS 64          /* PL4: world pages (<= 512x256) of both zooms + UI sheets */
#define ART_MAX_IDS    2048
#define ART_MAX_PIECES 6144        /* PL4: runtime pieces (world pieces of both zooms + one per UI sprite) */
#define ART_HASH       4096          /* power of two, > 2 * ids */
#define ART_LOGGED     256

typedef struct {
    const ArtSheet *def;
    Img   img;
    int   loaded;                    /* img holds a VRAM surface + px */
    PIX  *saved;                     /* evicted: pixels kept here (pitch = w) */
    int   w, h;
} Sheet;

static Sheet  s_sheets[ART_MAX_SHEETS];
static int    s_nsheets, s_inited, s_resident = -1;
static int    s_blocked[ZOOM_COUNT];   /* F3: that zoom's world art does not fit the VRAM heap (1024x768 + 64 zoom) */
static const char *s_names[ART_MAX_IDS];
static ArtSprite s_spr[ZOOM_COUNT][ART_MAX_IDS];
static s8     s_sheetOf[ZOOM_COUNT][ART_MAX_IDS];   /* -1 = none in that zoom; -2 = placeholder image */
static s16    s_hash[ART_HASH];                     /* id + 1, 0 = empty */
static int    s_nids;
static Img    s_missImg;
static u32    s_logged[ART_LOGGED];
static int    s_nlogged;
static int    s_maxUp[ZOOM_COUNT], s_maxHalfW[ZOOM_COUNT];

/* PL4: runtime pieces. Every sprite draws through its pieces (UI sprites and the placeholder have one piece at
   0,0 covering the whole box; world sprites 0..n trimmed pieces on the pages, art.h ArtPiece). */
typedef struct { const Img *img; s16 x, y, w, h, dx, dy; } RtPiece;
static RtPiece s_pc[ART_MAX_PIECES];
static int    s_npc;
static int    s_pageSlot[ART_MAX_SHEETS];   /* art_world_sheets index -> s_sheets slot */
static PIX   *s_cbuf;                      /* art_pixels scratch for multi-piece sprites */
static int    s_cbufN;

static int add_piece(const Img *img, int x, int y, int w, int h, int dx, int dy)
{
    RtPiece *p;
    if (s_npc >= ART_MAX_PIECES) { log_printf("art: error: more than %d pieces", ART_MAX_PIECES); return -1; }
    p = &s_pc[s_npc];
    p->img = img; p->x = (s16)x; p->y = (s16)y; p->w = (s16)w; p->h = (s16)h; p->dx = (s16)dx; p->dy = (s16)dy;
    return s_npc++;
}

static u32 name_hash(const char *s)
{
    u32 h = 2166136261u;
    while (*s) { h ^= (u8)*s++; h *= 16777619u; }
    return h;
}

static int lookup(const char *name)
{
    u32 h = name_hash(name) & (ART_HASH - 1);
    while (s_hash[h]) {
        int id = s_hash[h] - 1;
        if (!strcmp(s_names[id], name)) return id;
        h = (h + 1) & (ART_HASH - 1);
    }
    return -1;
}

static int add_name(const char *name)
{
    u32 h;
    int id = lookup(name);
    if (id >= 0) return id;
    if (s_nids >= ART_MAX_IDS) return -1;
    id = s_nids++;
    s_names[id] = name;
    s_sheetOf[0][id] = s_sheetOf[1][id] = -1;
    h = name_hash(name) & (ART_HASH - 1);
    while (s_hash[h]) h = (h + 1) & (ART_HASH - 1);
    s_hash[h] = (s16)(id + 1);
    return id;
}

/* the placeholder: a pink diamond (64x32 at x 0, 32x16 at x 64) */
static void make_missing(void)
{
    int z, x, y;
    PIX c = gfx_rgb(255, 40, 200), e = gfx_rgb(120, 0, 90);
    if (!gfx_make_img(96, 32, 1, &s_missImg)) return;
    for (z = 0; z < ZOOM_COUNT; z++) {
        int tw = TILE_W(z), th = TILE_H(z), ox = z ? 64 : 0;
        for (y = 0; y < th; y++)
            for (x = 0; x < tw; x++) {
                int du = 2 * x + 1 - tw, dv = 2 * y + 1 - th;
                int in = IABS(du) * th + IABS(dv) * tw < tw * th;
                int edge = IABS(du) * th + IABS(dv) * tw >= tw * th - 2 * tw;
                if (in) s_missImg.px[y * 96 + ox + x] = edge ? e : c;
            }
    }
    gfx_img_update(&s_missImg);
}

static void set_missing(void)
{
    int z;
    s_names[0] = "missing";
    s_nids = 1;
    s_hash[name_hash("missing") & (ART_HASH - 1)] = 1;
    for (z = 0; z < ZOOM_COUNT; z++) {
        ArtSprite *s = &s_spr[z][0];
        s->img = &s_missImg;
        s->x = (s16)(z ? 64 : 0); s->y = 0; s->w = (s16)TILE_W(z); s->h = (s16)TILE_H(z);
        s->ax = (s16)(TILE_W(z) / 2); s->ay = (s16)TILE_H(z); s->hx = s->hy = -1;
        s->piece0 = (s16)add_piece(&s_missImg, s->x, 0, s->w, s->h, 0, 0); s->npieces = 1;   /* PL4 */
        s_sheetOf[z][0] = -2;
    }
}

static int register_sheet(int si)
{
    const ArtSheet *sh = s_sheets[si].def;
    int i, z, dup = 0;
    for (i = 0; i < sh->count; i++) {
        const ArtSpr *a = &sh->spr[i];
        int id = add_name(a->name);
        if (id < 0) { log_printf("art: error: more than %d sprite names", ART_MAX_IDS); return 0; }
        for (z = 0; z < ZOOM_COUNT; z++) {
            ArtSprite *s;
            if (sh->zoom != ART_ZOOM_ANY && sh->zoom != z) continue;
            if (s_sheetOf[z][id] >= 0) { if (dup++ < 5) log_printf("art: warning: duplicate sprite \"%s\" (zoom %d)", a->name, z); continue; }
            s = &s_spr[z][id];
            s->img = &s_sheets[si].img;
            s->x = a->x; s->y = a->y; s->w = a->w; s->h = a->h;
            s->ax = a->ax; s->ay = a->ay; s->hx = a->hx; s->hy = a->hy;
            s->piece0 = (s16)add_piece(&s_sheets[si].img, a->x, a->y, a->w, a->h, 0, 0); s->npieces = 1;   /* PL4 */
            if (s->piece0 < 0) return 0;
            s_sheetOf[z][id] = (s8)si;
            if (sh->zoom != ART_ZOOM_ANY) {
                if (a->ay > s_maxUp[z]) s_maxUp[z] = a->ay;
                if (a->ax > s_maxHalfW[z]) s_maxHalfW[z] = a->ax;
                if (a->w - a->ax > s_maxHalfW[z]) s_maxHalfW[z] = a->w - a->ax;
            }
        }
    }
    return 1;
}

/* PL4: one zoom's world sprites from their piece set (pages already loaded: s_pageSlot) */
static int register_set(const ArtPieceSet *set)
{
    int i, k, z = set->zoom, dup = 0, anySlot = -1;
    for (i = 0; i < ART_WORLD_SHEET_COUNT; i++)
        if (art_world_sheets[i].zoom == z && s_pageSlot[i] >= 0) { anySlot = s_pageSlot[i]; break; }
    if (z < 0 || z >= ZOOM_COUNT || anySlot < 0) { log_printf("art: error: piece set for zoom %d has no pages", z); return 0; }
    for (i = 0; i < set->count; i++) {
        const ArtPSpr *a = &set->spr[i];
        ArtSprite *s;
        int id = add_name(a->name), slot = anySlot;
        if (id < 0) { log_printf("art: error: more than %d sprite names", ART_MAX_IDS); return 0; }
        if (s_sheetOf[z][id] >= 0) { if (dup++ < 5) log_printf("art: warning: duplicate sprite \"%s\" (zoom %d)", a->name, z); continue; }
        if (a->first < 0 || a->first + a->count > set->npieces) { log_printf("art: error: %s: bad piece range", a->name); return 0; }
        s = &s_spr[z][id];
        s->w = a->w; s->h = a->h; s->ax = a->ax; s->ay = a->ay; s->hx = a->hx; s->hy = a->hy;
        s->piece0 = (s16)s_npc; s->npieces = a->count;
        s->img = NULL; s->x = s->y = 0;
        for (k = 0; k < a->count; k++) {
            const ArtPiece *p = &set->pieces[a->first + k];
            int ps = p->page < ART_WORLD_SHEET_COUNT ? s_pageSlot[p->page] : -1;
            if (ps < 0) { log_printf("art: error: %s: piece on unknown page %d", a->name, p->page); return 0; }
            if (add_piece(&s_sheets[ps].img, p->x, p->y, p->w, p->h, p->dx, p->dy) < 0) return 0;
            if (k == 0) { s->img = &s_sheets[ps].img; s->x = p->x; s->y = p->y; slot = ps; }
        }
        s_sheetOf[z][id] = (s8)slot;
        if (a->ay > s_maxUp[z]) s_maxUp[z] = a->ay;
        if (a->ax > s_maxHalfW[z]) s_maxHalfW[z] = a->ax;
        if (a->w - a->ax > s_maxHalfW[z]) s_maxHalfW[z] = a->w - a->ax;
    }
    return 1;
}

static int evict(Sheet *S);              /* F3: used by art_init's 32-zoom fallback */
static int reload(Sheet *S);

int art_init(void)
{
    int i, n, z, missing1 = 0;
    double t0 = time_ms();
    if (s_inited) return 1;
    memset(s_hash, 0, sizeof(s_hash));
    s_npc = 0;                                         /* PL4 */
    for (i = 0; i < ART_MAX_SHEETS; i++) s_pageSlot[i] = -1;
    make_missing();
    set_missing();
    s_nsheets = 0;
    /* DESIGN 14.5 (orchestrator fix): only ONE zoom's world art is resident at any resolution. Pass 0 loads the
       32-zoom world sheets and evicts each right away (pixels stay in system memory, T13 gfx_img_evict), so the
       temporary VRAM is at most one sheet; pass 1 loads the 64-zoom world sheets and the UI sheets. Zoom changes
       swap residency via art_set_resident_zoom (render_set_zoom). */
    {
        int pass;
        for (pass = 0; pass < 2; pass++)
        for (i = 0; i < ART_WORLD_SHEET_COUNT + ART_UI_SHEET_COUNT && s_nsheets < ART_MAX_SHEETS; i++) {
            const ArtSheet *sh = i < ART_WORLD_SHEET_COUNT ? &art_world_sheets[i] : &art_ui_sheets[i - ART_WORLD_SHEET_COUNT];
            int other = (sh->zoom != ART_ZOOM_ANY && sh->zoom != ZOOM_64);
            Sheet *S;
            if ((pass == 0) != other) continue;
            S = &s_sheets[s_nsheets];
            memset(S, 0, sizeof(*S));
            S->def = sh;
            /* F3: the 32-zoom sheets go to system memory only: loading them into VRAM first and evicting them left
               a hole that fragmented the 1024x768 heap (VM B: world3_64 640x1467 refused with 4,477 KB free) */
            if (!(other ? gfx_load_bmp_sys(data_path(sh->bmp), &S->img, sh->keyed) : gfx_load_bmp(data_path(sh->bmp), &S->img, sh->keyed))) {
                /* F3: a 64-zoom world sheet that does not fit (VM B 1024x768: 4,477 KB free, but no 640x1467 hole
                   in the Voodoo3 heap): keep its pixels, block the 64 zoom and run at the 32 zoom (below) */
                if (sh->zoom == ZOOM_64 && gfx_load_bmp_sys(data_path(sh->bmp), &S->img, sh->keyed)) {
                    log_printf("art: warning: %s does not fit in VRAM (free %d KB): 64 zoom off at this resolution", sh->bmp, gfx_vram_free_kb());
                    s_blocked[ZOOM_64] = 1;
                    other = 1;
                } else {
                    log_printf("art: error: cannot load %s (VRAM used %d KB, free %d KB)", sh->bmp, gfx_vram_used_kb(), gfx_vram_free_kb());
                    return 0;
                }
            }
            S->loaded = 1; S->w = S->img.w; S->h = S->img.h;
            if (i < ART_WORLD_SHEET_COUNT) s_pageSlot[i] = s_nsheets;      /* PL4: a world page (pieces below) */
            if (!register_sheet(s_nsheets)) return 0;
            if (other) S->loaded = 0;                  /* F3: never in VRAM (was: evict(S)) */
            s_nsheets++;
        }
    }
    for (i = 0; i < ART_WORLD_SET_COUNT; i++)          /* PL4: world sprites = pieces on the pages */
        if (!register_set(&art_world_sets[i])) return 0;
    s_resident = ZOOM_64;
    if (s_blocked[ZOOM_64]) {                      /* F3: 32 zoom resident instead (degraded, but playable) */
        for (i = 0; i < s_nsheets; i++) if (s_sheets[i].def->zoom == ZOOM_64) evict(&s_sheets[i]);
        for (i = 0; i < s_nsheets; i++)
            if (s_sheets[i].def->zoom == ZOOM_32 && !reload(&s_sheets[i])) return 0;
        s_resident = ZOOM_32;
    }
    /* world names should exist in both zooms */
    for (i = 1; i < s_nids; i++) {
        int in0 = s_sheetOf[0][i] >= 0, in1 = s_sheetOf[1][i] >= 0;
        if (in0 != in1) { if (missing1 < 10) log_printf("art: warning: \"%s\" exists only at zoom %d", s_names[i], in0 ? 0 : 1); missing1++; }
    }
    s_inited = 1;                                      /* DESIGN 14.5: s_resident set above (64, or 32 when blocked) */
    for (z = 0, n = 0; z < ZOOM_COUNT; z++) n += s_maxUp[z];
    log_printf("art: %d sheets, %d names (%d in one zoom only), %d pieces, %d fonts, VRAM %d KB (world 64: %d KB, 32: %d KB), "
               "max sprite up %d/%d px, %.0f ms", s_nsheets, s_nids, missing1, s_npc, ART_UI_FONT_COUNT, art_vram_kb(),
               art_world_vram_kb(ZOOM_64), art_world_vram_kb(ZOOM_32), s_maxUp[0], s_maxUp[1], time_ms() - t0);
    gfx_log_vram("art loaded");
    return 1;
}

void art_shutdown(void)
{
    int i;
    for (i = 0; i < s_nsheets; i++) {
        if (s_sheets[i].loaded) gfx_free_img(&s_sheets[i].img);
        if (s_sheets[i].saved) free(s_sheets[i].saved);
        memset(&s_sheets[i], 0, sizeof(Sheet));
    }
    s_nsheets = 0;
    s_npc = 0;                                         /* PL4 */
    if (s_cbuf) { free(s_cbuf); s_cbuf = NULL; s_cbufN = 0; }
    gfx_free_img(&s_missImg);
    memset(s_hash, 0, sizeof(s_hash));
    s_nids = 0; s_inited = 0; s_nlogged = 0; s_resident = -1;
    memset(s_blocked, 0, sizeof(s_blocked));   /* F3 */
}

/* ------------------------------------------------------------------ residency */
static int evict(Sheet *S)
{
    if (!S->loaded) return 1;
    if (!gfx_img_evict(&S->img)) { log_printf("art: error: cannot evict %s", S->def->bmp); return 0; }   /* T13: px kept */
    S->loaded = 0;
    return 1;
}

static int reload(Sheet *S)
{
    if (S->loaded) return 1;
    if (!gfx_img_restore(&S->img)) {
        log_printf("art: error: VRAM full reloading %s (free %d KB)", S->def->bmp, gfx_vram_free_kb());
        return 0;
    }
    S->loaded = 1;
    return 1;
}

int art_set_resident_zoom(int zoom)
{
    int i, ok = 1;
    double t0 = time_ms();
    if (!s_inited || zoom == s_resident) return 1;
    if (zoom >= 0 && zoom < ZOOM_COUNT && s_blocked[zoom]) return 0;     /* F3: does not fit: stay */
    for (i = 0; i < s_nsheets; i++) {                  /* evict first: make room */
        Sheet *S = &s_sheets[i];
        if (S->def->zoom != ART_ZOOM_ANY && zoom >= 0 && S->def->zoom != zoom) ok &= evict(S);
    }
    for (i = 0; i < s_nsheets; i++) {
        Sheet *S = &s_sheets[i];
        if (S->def->zoom == ART_ZOOM_ANY || zoom < 0 || S->def->zoom == zoom) ok &= reload(S);
    }
    if (!ok && zoom >= 0 && s_resident >= 0) {      /* F3: back to the zoom we had, and don't try this one again */
        int old = s_resident;
        for (i = 0; i < s_nsheets; i++) if (s_sheets[i].def->zoom == zoom) evict(&s_sheets[i]);
        for (i = 0; i < s_nsheets; i++) if (s_sheets[i].def->zoom == old) reload(&s_sheets[i]);
        s_blocked[zoom] = 1;
        log_printf("art: zoom %d art does not fit (VRAM free %d KB): staying at zoom %d", zoom, gfx_vram_free_kb(), old);
        return 0;
    }
    log_printf("art: resident zoom %d: VRAM %d KB, %.0f ms%s", zoom, art_vram_kb(), time_ms() - t0, ok ? "" : " (FAILED)");
    s_resident = zoom;
    return ok;
}

int art_resident_zoom(void) { return s_resident; }
int art_zoom_available(int zoom) { return zoom >= 0 && zoom < ZOOM_COUNT && !s_blocked[zoom]; }   /* F3 */

/* ------------------------------------------------------------------ lookup */
static void log_missing(const char *name)
{
    u32 h = name_hash(name);
    int i;
    for (i = 0; i < s_nlogged; i++) if (s_logged[i] == h) return;
    if (s_nlogged < ART_LOGGED) s_logged[s_nlogged++] = h;
    log_printf("art: missing sprite \"%s\"", name);
}

int art_find(const char *name)
{
    int id = name ? lookup(name) : -1;
    if (id < 0) { if (name) log_missing(name); return ART_MISSING; }
    return id;
}

int art_findf(const char *fmt, ...)
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    str_vprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return art_find(buf);
}

int art_exists(const char *name) { return name && lookup(name) >= 0; }

static int findq(const char *fmt, ...)       /* silent formatted lookup, -1 if absent */
{
    char buf[96];
    va_list ap;
    va_start(ap, fmt);
    str_vprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return lookup(buf);
}

static int lot_normal(const char *key, int v, int r)
{
    int id = findq("%s_v%d_r%d", key, v, r);
    if (id < 0) id = findq("%s_v%d_r0", key, v);
    if (id < 0) id = findq("%s_v0_r%d", key, r);
    if (id < 0) id = findq("%s_v0_r0", key);
    return id;
}

int art_lot_sprite(const char *key, int variant, int rot, int kind, int w, int h)
{
    int id = -1;
    char want[96];
    rot &= 3;
    if (variant < 0) variant = 0;
    switch (kind) {
    case ART_LOT_ABANDONED:
        id = findq("%s_v%d_ab_r%d", key, variant, rot);
        if (id < 0) id = findq("%s_v0_ab_r%d", key, rot);
        if (id < 0) id = findq("%s_v%d_ab_r0", key, variant);
        if (id < 0) id = findq("%s_v0_ab_r0", key);
        if (id < 0) id = lot_normal(key, variant, rot);
        break;
    case ART_LOT_BUILDING:
        id = findq("%s_build_r%d", key, rot);
        if (id < 0) id = findq("build_%dx%d_r%d", w, h, rot);
        if (id < 0) id = findq("build_%dx%d_r0", w, h);
        break;
    case ART_LOT_GROUND:
        id = findq("%s_ground_r%d", key, rot);
        if (id < 0) id = findq("ground_%dx%d_r%d", w, h, rot);
        if (id < 0) id = findq("ground_%dx%d_r0", w, h);
        break;
    default:
        id = lot_normal(key, variant, rot);
        break;
    }
    if (id < 0) {
        str_printf(want, sizeof(want), "%s_v%d_%sr%d (kind %d)", key, variant, kind == ART_LOT_ABANDONED ? "ab_" : "", rot, kind);
        log_missing(want);
        return ART_MISSING;
    }
    return id;
}

const ArtSprite *art_get(int zoom, int id)
{
    int si;
    if (zoom < 0 || zoom >= ZOOM_COUNT) zoom = 0;
    if (id <= 0 || id >= s_nids || s_sheetOf[zoom][id] == -1) return &s_spr[zoom][0];
    si = s_sheetOf[zoom][id];
    if (si >= 0 && !s_sheets[si].loaded) {                 /* zoom not resident: the other zoom's copy */
        int oz = zoom ^ 1, so = s_sheetOf[oz][id];
        if (so >= 0 && s_sheets[so].loaded) return &s_spr[oz][id];
    }
    return &s_spr[zoom][id];
}

const char *art_name(int id) { return (id >= 0 && id < s_nids) ? s_names[id] : "?"; }
int art_count(void) { return s_nids; }

int art_variants(const char *key)
{
    int v = 0;
    while (v < 64 && findq("%s_v%d_r0", key, v) >= 0) v++;
    return v;
}

/* ------------------------------------------------------------------ drawing */
void art_draw(int zoom, int id, int x, int y)
{
    const ArtSprite *s = art_get(zoom, id);
    const RtPiece *p = &s_pc[s->piece0];
    int k;
    x -= s->ax; y -= s->ay;
    for (k = 0; k < s->npieces; k++, p++) gfx_blit_part(p->img, p->x, p->y, p->w, p->h, x + p->dx, y + p->dy);   /* PL4 */
}

/* PL4: the sprite-local rect (sx, sy, w, h) at (dx, dy): one blit per overlapping piece */
void art_blit_part(int zoom, int id, int sx, int sy, int w, int h, int dx, int dy)
{
    const ArtSprite *s = art_get(zoom, id);
    const RtPiece *p = &s_pc[s->piece0];
    int k, x1 = sx + w, y1 = sy + h;
    for (k = 0; k < s->npieces; k++, p++) {
        int ix0 = IMAX(sx, p->dx), iy0 = IMAX(sy, p->dy), ix1 = IMIN(x1, p->dx + p->w), iy1 = IMIN(y1, p->dy + p->h);
        if (ix0 >= ix1 || iy0 >= iy1) continue;
        gfx_blit_part(p->img, p->x + ix0 - p->dx, p->y + iy0 - p->dy, ix1 - ix0, iy1 - iy0, dx + ix0 - sx, dy + iy0 - sy);
    }
}

void art_draw_cols(int zoom, int id, int x, int y, int col0, int col1)
{
    const ArtSprite *s = art_get(zoom, id);
    if (col0 < 0) col0 = 0;
    if (col1 > s->w) col1 = s->w;
    if (col1 <= col0) return;
    art_blit_part(zoom, id, col0, 0, col1 - col0, s->h, x - s->ax + col0, y - s->ay);   /* PL4 */
}

/* pixels of exactly the record art_get returns (which may be the other zoom's copy when not resident).
   PL4: a sprite whose single piece is its whole box points into the page; others are assembled into s_cbuf. */
const PIX *art_pixels(int zoom, int id, int *pitch)
{
    const ArtSprite *s = art_get(zoom, id);
    const RtPiece *p = &s_pc[s->piece0];
    int k, n = s->w * s->h, i, y;
    PIX key = gfx_key();
    if (s->npieces == 1 && p->dx == 0 && p->dy == 0 && p->w == s->w && p->h == s->h) {
        if (!p->img || !p->img->px) return NULL;
        *pitch = p->img->w;
        return p->img->px + p->y * p->img->w + p->x;
    }
    if (n <= 0) return NULL;
    if (n > s_cbufN) {
        PIX *nb = (PIX *)realloc(s_cbuf, n * sizeof(PIX));
        if (!nb) return NULL;
        s_cbuf = nb; s_cbufN = n;
    }
    for (i = 0; i < n; i++) s_cbuf[i] = key;
    for (k = 0; k < s->npieces; k++, p++) {
        if (!p->img || !p->img->px) continue;
        for (y = 0; y < p->h; y++)
            memcpy(s_cbuf + (p->dy + y) * s->w + p->dx, p->img->px + (p->y + y) * p->img->w + p->x, p->w * sizeof(PIX));
    }
    *pitch = s->w;
    return s_cbuf;
}

int art_opaque(int zoom, int id, int px, int py)
{
    const ArtSprite *s = art_get(zoom, id);
    const RtPiece *p = &s_pc[s->piece0];
    int k;
    if (px < 0 || py < 0 || px >= s->w || py >= s->h) return 0;
    for (k = 0; k < s->npieces; k++, p++) {             /* PL4: the piece holding the pixel (pieces don't overlap) */
        if (px < p->dx || py < p->dy || px >= p->dx + p->w || py >= p->dy + p->h) continue;
        if (!p->img || !p->img->px) return 1;
        return p->img->px[(p->y + py - p->dy) * p->img->w + p->x + px - p->dx] != gfx_key();
    }
    return 0;
}

int art_max_up(int zoom) { return s_maxUp[zoom & 1] > 0 ? s_maxUp[zoom & 1] : (256 >> zoom); }
int art_max_half_w(int zoom) { return s_maxHalfW[zoom & 1] > 0 ? s_maxHalfW[zoom & 1] : (96 >> zoom); }

/* ------------------------------------------------------------------ fonts */
const ArtFont *art_font(const char *name)
{
    int i;
    for (i = 0; i < ART_UI_FONT_COUNT; i++)
        if (!strcmp(art_ui_fonts[i]->name, name)) return art_ui_fonts[i];
    log_printf("art: missing font \"%s\"", name);
    return NULL;
}

const Img *art_font_img(const ArtFont *f)
{
    int i;
    if (!f) return NULL;
    for (i = 0; i < s_nsheets; i++)
        if (!strcmp(s_sheets[i].def->bmp, f->bmp)) return s_sheets[i].loaded ? &s_sheets[i].img : NULL;
    return NULL;
}

/* ------------------------------------------------------------------ reports */
int art_vram_kb(void)
{
    int i, kb = s_missImg.vram_kb;
    for (i = 0; i < s_nsheets; i++) if (s_sheets[i].loaded) kb += s_sheets[i].img.vram_kb;
    return kb;
}

int art_world_vram_kb(int zoom)
{
    int i, kb = 0;
    for (i = 0; i < s_nsheets; i++)
        if (s_sheets[i].def->zoom == zoom) kb += s_sheets[i].w * s_sheets[i].h * 2 / 1024;
    return kb;
}

/* every name the code needs. Exact names count as missing when absent; lot sprites count as missing only
   when art_lot_sprite's documented fallbacks end at the placeholder (fallback use is reported). */
static int s_stMissing, s_stFallback;

static void need(const char *fmt, ...)
{
    char buf[96];
    int z, id;
    va_list ap;
    va_start(ap, fmt);
    str_vprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    id = lookup(buf);
    if (id < 0) { s_stMissing++; log_printf("art: selftest: MISSING %s", buf); return; }
    for (z = 0; z < ZOOM_COUNT; z++)
        if (s_sheetOf[z][id] == -1) { s_stMissing++; log_printf("art: selftest: MISSING %s at zoom %d", buf, z); }
}

static void need_lot(const char *key, int v, int r, int kind, int w, int h)
{
    int id = art_lot_sprite(key, v, r, kind, w, h);
    char exact[96];
    if (id == ART_MISSING) { s_stMissing++; log_printf("art: selftest: MISSING lot %s v%d r%d kind %d", key, v, r, kind); return; }
    if (kind == ART_LOT_NORMAL) str_printf(exact, sizeof(exact), "%s_v%d_r%d", key, v, r);
    else if (kind == ART_LOT_ABANDONED) str_printf(exact, sizeof(exact), "%s_v%d_ab_r%d", key, v, r);
    else if (kind == ART_LOT_BUILDING) str_printf(exact, sizeof(exact), "%s_build_r%d", key, r);
    else str_printf(exact, sizeof(exact), "%s_ground_r%d", key, r);
    if (strcmp(art_name(id), exact)) s_stFallback++;
}

int art_selftest(void)
{
    static const char *const fk[4] = { "mixed", "spruce", "birch", "pine" };
    static const char *const trees[6] = { "birch", "birch_au", "spruce", "pine", "apple", "rowan" };
    static const char *const dirs[4] = { "px", "ny", "nx", "py" };
    static const char *const icons_extra[] = { "icon_o_none", "icon_o_power", "icon_o_water", "icon_o_pollution",
        "icon_o_land_value", "icon_o_traffic", "icon_o_fire_risk", "icon_o_growth", "icon_o_zones", "icon_o_catchment",
        "icon_z_houses", "icon_z_apartments", "icon_z_commercial", "icon_z_industry", "icon_powerline", NULL };
    int b, v, r, k, m, i, n0;
    if (!s_inited) return -1;
    s_stMissing = 0; s_stFallback = 0;
    for (k = 0; k < 3; k++) { need("grass%d", k); need("water%d", k); }
    for (m = 1; m <= 14; m++) { need("shore%02d", m); need("shore%02d_1", m); need("shore%02d_2", m); }
    for (k = 0; k < 3; k++) for (v = 0; v < 3; v++) { need("river_%c_%d", "xyn"[k], v); need("rapids_%c_%d", "xyn"[k], v); }
    need("field_crop"); need("field_plough"); need("marsh"); need("rock0"); need("rock1");
    for (k = 0; k < 4; k++) for (v = 0; v < 3; v++) need("forest_%s%d", fk[k], v);
    for (k = 0; k < 6; k++) need("tree_%s", trees[k]);
    for (b = ROAD_GRAVEL; b < ROAD_CLASS_COUNT; b++) {
        for (m = 0; m < 16; m++) need("%s_%02d", g_roads[b].key, m);
        need("%s_bridge_05", g_roads[b].key); need("%s_bridge_10", g_roads[b].key);
    }
    for (m = 0; m < 16; m++) { need("power_%02d", m); if (m) need("wire_%02d", m); }
    for (k = 0; k < OVR_COUNT; k++) for (v = 0; v < 8; v++) need("ovl_%d_%d", k, v);
    need("cur_ok"); need("cur_bad"); need("cur_sel"); need("cur_query");
    for (k = 1; k < ZONE_COUNT; k++) need("zone_%d", k);
    for (k = 0; k < 12; k++) for (r = 0; r < 4; r++) need("car%d_%s", k, dirs[r]);
    for (k = 0; k < 6; k++) need("smoke%d", k);
    n0 = s_stMissing;
    for (b = 1; b < B_COUNT; b++) {
        const BuildingDef *d = def_building(b);
        int nr = (d->flags & BDF_NO_ROTATE) ? 1 : 4;
        if (!d->variants) continue;
        for (r = 0; r < nr; r++) {
            for (v = 0; v < d->variants; v++) {
                need_lot(d->key, v, r, ART_LOT_NORMAL, d->w, d->h);
                if (d->cat == BCAT_ZONE) need_lot(d->key, v, r, ART_LOT_ABANDONED, d->w, d->h);
            }
            need_lot(d->key, 0, r, ART_LOT_BUILDING, d->w, d->h);
            need_lot(d->key, 0, r, ART_LOT_GROUND, d->w, d->h);
        }
        if (art_variants(d->key) < d->variants)
            log_printf("art: selftest: %s: defs variants %d, art has %d", d->key, d->variants, art_variants(d->key));
        if (d->cat != BCAT_ZONE && !(d->flags & BDF_MODULE)) need("icon_b_%s", d->key);   /* PL3: modules use the complex's icon */
    }
    for (b = ROAD_GRAVEL; b < ROAD_CLASS_COUNT; b++) need("icon_r_%s", g_roads[b].key);
    for (i = 0; icons_extra[i]; i++) need("%s", icons_extra[i]);
    for (i = 0; i < ART_UI_FONT_COUNT; i++) if (!art_font_img(art_ui_fonts[i])) { s_stMissing++; log_printf("art: selftest: font %s has no sheet", art_ui_fonts[i]->name); }
    log_printf("art: selftest: %d names checked missing %d (lots: %d), %d lot sprites use a documented fallback; VRAM %d KB",
               s_nids, s_stMissing, s_stMissing - n0, s_stFallback, art_vram_kb());
    return s_stMissing;
}
