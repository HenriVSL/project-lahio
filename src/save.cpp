/* save.cpp - binary chunked save files, slots, autosave rotation, quicksave. PORTABLE. Owner: T10.
   Implements save.h. DESIGN 8.4.

   File layout (all integers little-endian):
       "KAUP"  u32 format version (SAVE_VERSION)
       chunk*  tag[4]  u32 chunk version  u32 data size  data[size]
       "END "  version 1, size 4: u32 FNV-1a hash of every byte of the file before this chunk header
   The first chunks are "META" (SaveMeta, explicit fields) and "THMB" (64x64 thumbnail classes), so the load
   menu reads only the first ~4.5 KB of a file (save_read_meta / save_read_thumb). Then the modules' own
   chunks in save order: TILE LOTS (world), SIM (sim incl. RNG + history), SIMX (sim per-cycle state),
   BUDG, BUDX (sub-mk remainders), FLDS, POLI, NET (network/coverage solver results), TRAF (T08 traffic). SIMX/BUDX/NET are
   what makes a loaded game continue bit-identically to an unbroken run (tests/test_save.cpp).

   Validation before anything is touched (load_game / load_game_from): magic, version 1..SAVE_VERSION,
   every chunk header inside the file, END chunk present and last, hash matches. A file that fails leaves
   the running game untouched. A file that validates but whose module loader then fails (cannot happen
   for files this code wrote) leaves the game reset to an empty world: callers start a new game then.

   RLE (save_rle/load_rle): a mode byte (0 = bytes as given, 2/4/8 = byte planes of that struct stride,
   the encoder picks the smallest), then byte runs. Control byte c: 0..127 = c+1 literal bytes follow;
   128..255 = the next byte repeated c-125 times (3..130). The decoder stops after exactly n output bytes
   and checks the chunk bounds (a corrupt stream sets the error flag, never writes past n).

   Memory: a SaveFile is a growable heap buffer (save time only, never per frame). */
#include "common.h"
#include "save.h"
#include "industry.h"
#include "complex.h"
#include "civic.h"
#include "era.h"           /* M7 */
#include "hard.h"          /* 1.1 D1 */
#include "season.h"        /* 1.1 W1 */
#include "util.h"
#include "world.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "traffic.h"
#include "politics.h"
#include <time.h>

struct SaveFile {
    u8  *buf;
    int  size, cap;          /* bytes used / allocated */
    int  pos;                /* read cursor */
    int  rd_end;             /* reader: end of the current chunk's data (reads past it fail) */
    int  chunk_hdr;          /* writer: offset of the open chunk's header, -1 none */
    int  error;
};

#define HDR_SIZE    8        /* magic + version */
#define CHUNK_HDR   12       /* tag + version + size */
#define SAVE_MAX_FILE (4 * 1024 * 1024)

static char s_dir[PATH_LEN];
static char s_last_error[128];

/* ================================================================== buffer */
static void sf_reserve(SaveFile *sf, int extra)
{
    int need, ncap;
    u8 *nb;
    if (sf->error) return;
    need = sf->size + extra;
    if (need <= sf->cap) return;
    ncap = sf->cap ? sf->cap : 64 * 1024;
    while (ncap < need) ncap *= 2;
    nb = (u8 *)realloc(sf->buf, ncap);
    if (!nb) { sf->error = 1; log_printf("save: error: out of memory (%d bytes)", ncap); return; }
    sf->buf = nb; sf->cap = ncap;
}

static void put(SaveFile *sf, const void *p, int n)
{
    if (!sf || n <= 0) return;
    sf_reserve(sf, n);
    if (sf->error) return;
    memcpy(sf->buf + sf->size, p, n);
    sf->size += n;
}

static void put_u32_at(SaveFile *sf, int off, u32 v)
{
    if (sf->error || off < 0 || off + 4 > sf->size) return;
    sf->buf[off] = (u8)v; sf->buf[off + 1] = (u8)(v >> 8); sf->buf[off + 2] = (u8)(v >> 16); sf->buf[off + 3] = (u8)(v >> 24);
}

static u32 get_u32_at(const u8 *b) { return (u32)b[0] | ((u32)b[1] << 8) | ((u32)b[2] << 16) | ((u32)b[3] << 24); }

SaveFile *save_mem_open(void)
{
    SaveFile *sf = (SaveFile *)calloc(1, sizeof(SaveFile));
    if (!sf) return NULL;
    sf->chunk_hdr = -1;
    return sf;
}

SaveFile *save_mem_reopen(SaveFile *sf)
{
    if (!sf) return NULL;
    sf->pos = 0;
    sf->rd_end = sf->size;
    sf->chunk_hdr = -1;
    return sf;
}

void save_mem_close(SaveFile *sf)
{
    if (!sf) return;
    free(sf->buf);
    free(sf);
}

int save_mem_size(SaveFile *sf) { return sf ? sf->size : 0; }
const u8 *save_mem_data(SaveFile *sf) { return sf ? sf->buf : NULL; }

SaveFile *save_mem_from(const void *data, int n)
{
    SaveFile *sf = save_mem_open();
    if (!sf) return NULL;
    put(sf, data, n);
    if (sf->error) { save_mem_close(sf); return NULL; }
    return save_mem_reopen(sf);
}

/* ================================================================== chunk writer */
int save_begin_chunk(SaveFile *sf, const char *tag4, int version)
{
    u8 h[CHUNK_HDR];
    if (!sf) return 0;
    if (sf->chunk_hdr >= 0) save_end_chunk(sf);          /* forgiving: close a chunk left open */
    memcpy(h, tag4, 4);
    h[4] = (u8)version; h[5] = (u8)(version >> 8); h[6] = (u8)(version >> 16); h[7] = (u8)(version >> 24);
    memset(h + 8, 0, 4);
    sf->chunk_hdr = sf->size;
    put(sf, h, CHUNK_HDR);
    return !sf->error;
}

void save_end_chunk(SaveFile *sf)
{
    if (!sf || sf->chunk_hdr < 0) return;
    put_u32_at(sf, sf->chunk_hdr + 8, (u32)(sf->size - sf->chunk_hdr - CHUNK_HDR));
    sf->chunk_hdr = -1;
}

void save_bytes(SaveFile *sf, const void *p, int n) { put(sf, p, n); }

void save_u8(SaveFile *sf, u32 v) { u8 b = (u8)v; put(sf, &b, 1); }
void save_u16(SaveFile *sf, u32 v) { u8 b[2]; b[0] = (u8)v; b[1] = (u8)(v >> 8); put(sf, b, 2); }
void save_u32(SaveFile *sf, u32 v)
{
    u8 b[4];
    b[0] = (u8)v; b[1] = (u8)(v >> 8); b[2] = (u8)(v >> 16); b[3] = (u8)(v >> 24);
    put(sf, b, 4);
}
void save_s32(SaveFile *sf, s32 v) { save_u32(sf, (u32)v); }

void save_str(SaveFile *sf, const char *s)
{
    int n = s ? (int)strlen(s) : 0;
    if (n > 65535) n = 65535;
    save_u16(sf, (u32)n);
    put(sf, s, n);
}

/* raw byte-run encoder (no mode byte) */
static void rle_encode(SaveFile *sf, const u8 *s, int n)
{
    int i = 0, lit = 0, run;
    u8 *o;
    sf_reserve(sf, n + n / 128 + 8);                 /* worst case: all literals */
    if (sf->error) return;
    o = sf->buf + sf->size;                          /* T13: write straight into the reserved buffer */
    while (i < n) {
        u8 v = s[i];
        run = 1;
        while (i + run < n && run < 130 && s[i + run] == v) run++;
        if (run >= 3) {
            if (lit) { *o++ = (u8)(lit - 1); memcpy(o, s + i - lit, lit); o += lit; lit = 0; }
            *o++ = (u8)(run + 125); *o++ = v;
            i += run;
        } else {
            lit++; i++;
            if (lit == 128) { *o++ = 127; memcpy(o, s + i - lit, lit); o += lit; lit = 0; }
        }
    }
    if (lit) { *o++ = (u8)(lit - 1); memcpy(o, s + i - lit, lit); o += lit; }
    sf->size = (int)(o - sf->buf);
}

/* scratch for the planar variants (save/load time only) */
static u8 *s_plane;
static int s_plane_cap;
static u8 *plane_buf(int n)
{
    if (n > s_plane_cap) {
        u8 *nb = (u8 *)realloc(s_plane, n);
        if (!nb) return NULL;
        s_plane = nb; s_plane_cap = n;
    }
    return s_plane;
}

/* encoded size of the byte-run stream of src read with a stride: the sequence src[b + i*st] for
   b = 0..st-1 (st = 1: the bytes as given). Count only, no output (fast mode choice). */
/* limit: T13 early exit once size reaches it (the caller only wants sizes below its best so far) */
static int rle_size_strided(const u8 *src, int n, int st, int limit)
{
    int m = n / st, b, i, size = 0, lit = 0, run;
    for (b = 0; b < st; b++) {
        const u8 *p = src + b;
        for (i = 0; i < m; ) {
            u8 v = p[i * st];
            run = 1;
            while (i + run < m && run < 130 && p[(i + run) * st] == v) run++;
            if (run >= 3) { if (lit) { size += 1 + lit; lit = 0; } size += 2; i += run; }
            else { lit++; i++; if (lit == 128) { size += 129; lit = 0; } }
            if (size >= limit) return size;
        }
        /* planes are encoded as one stream: literals may continue across planes, close enough */
    }
    if (lit) size += 1 + lit;
    return size;
}

/* Mode byte, then the byte-run stream. Mode 0 = the bytes as given; mode 2/4/8 = byte planes of a
   struct array with that stride (all byte 0s, then all byte 1s, ...), which compresses arrays of small
   structs (Tile = 8 bytes, u16/s32 arrays) far better. The mode with the smallest estimate is used. */
#define RLE_SAMPLE_MIN 16384
#define RLE_SAMPLES    32
#define RLE_SAMPLE_WIN 512
void save_rle(SaveFile *sf, const void *p, int n)
{
    static const int strides[3] = { 2, 4, 8 };
    const u8 *src = (const u8 *)p;
    int best_mode = 0, best_size, k, i, b, st, sz;
    u8 *pl;
    if (!sf || n <= 0 || sf->error) return;
    if (n <= RLE_SAMPLE_MIN) {
        best_size = rle_size_strided(src, n, 1, 0x7fffffff);
        for (k = 0; k < 3; k++) {
            st = strides[k];
            if (n % st) continue;
            sz = rle_size_strided(src, n, st, best_size);
            if (sz < best_size) { best_size = sz; best_mode = st; }
        }
    } else {
        /* T13: big arrays (net/traffic/world: ~1 MB per save, 4 estimate passes were ~half of the 0.37 s
           autosave on the P2): estimate the modes on RLE_SAMPLES evenly spaced windows instead. The file
           stays valid either way (the mode byte says how to decode); only the compression ratio can differ. */
        int sizes[4], w, step = (n / RLE_SAMPLES) & ~7, win = RLE_SAMPLE_WIN, off;
        sizes[0] = sizes[1] = sizes[2] = sizes[3] = 0;
        for (w = 0, off = 0; w < RLE_SAMPLES; w++, off += step) {
            int len = IMIN(win, n - off);
            if (len < 8) break;
            len &= ~7;
            sizes[0] += rle_size_strided(src + off, len, 1, 0x7fffffff);
            for (k = 0; k < 3; k++) if (!(n % strides[k])) sizes[k + 1] += rle_size_strided(src + off, len, strides[k], 0x7fffffff);
        }
        best_size = sizes[0];
        for (k = 0; k < 3; k++)
            if (!(n % strides[k]) && sizes[k + 1] < best_size) { best_size = sizes[k + 1]; best_mode = strides[k]; }
    }
    if (best_mode && (pl = plane_buf(n)) != NULL) {
        st = best_mode;
        for (b = 0; b < st; b++)
            for (i = 0; i < n / st; i++) pl[b * (n / st) + i] = src[i * st + b];
        src = pl;
    } else best_mode = 0;
    save_u8(sf, (u32)best_mode);
    rle_encode(sf, src, n);
}

/* ================================================================== chunk reader */
static int rd_avail(SaveFile *sf, int n)
{
    if (!sf || sf->error) return 0;
    if (n < 0 || sf->pos + n > sf->rd_end) { sf->error = 1; return 0; }
    return 1;
}

int load_find_chunk(SaveFile *sf, const char *tag4, int *version)
{
    int off;
    if (version) *version = 0;
    if (!sf || sf->size < HDR_SIZE) return 0;
    off = HDR_SIZE;
    while (off + CHUNK_HDR <= sf->size) {
        u32 len = get_u32_at(sf->buf + off + 8);
        if (len > (u32)(sf->size - off - CHUNK_HDR)) return 0;   /* truncated (validated files never are) */
        if (!memcmp(sf->buf + off, tag4, 4)) {
            if (version) *version = (int)get_u32_at(sf->buf + off + 4);
            sf->pos = off + CHUNK_HDR;
            sf->rd_end = sf->pos + (int)len;
            return 1;
        }
        off += CHUNK_HDR + (int)len;
    }
    return 0;
}

int load_bytes(SaveFile *sf, void *p, int n)
{
    if (n <= 0) return 1;
    if (!rd_avail(sf, n)) { memset(p, 0, n); return 0; }
    memcpy(p, sf->buf + sf->pos, n);
    sf->pos += n;
    return 1;
}

u32 load_u8(SaveFile *sf) { u8 b = 0; load_bytes(sf, &b, 1); return b; }
u32 load_u16(SaveFile *sf) { u8 b[2]; load_bytes(sf, b, 2); return (u32)b[0] | ((u32)b[1] << 8); }
u32 load_u32(SaveFile *sf) { u8 b[4]; load_bytes(sf, b, 4); return get_u32_at(b); }
s32 load_s32(SaveFile *sf) { return (s32)load_u32(sf); }

void load_str(SaveFile *sf, char *buf, int size)
{
    int n = (int)load_u16(sf), k;
    if (size <= 0) { if (rd_avail(sf, n)) sf->pos += n; return; }
    buf[0] = 0;
    if (!rd_avail(sf, n)) return;
    k = IMIN(n, size - 1);
    memcpy(buf, sf->buf + sf->pos, k);
    buf[k] = 0;
    sf->pos += n;
}

static int rle_decode(SaveFile *sf, u8 *d, int n)
{
    int o = 0, k;
    u32 c;
    while (o < n) {
        if (!rd_avail(sf, 1)) break;
        c = sf->buf[sf->pos++];
        if (c < 128) {
            k = (int)c + 1;
            if (o + k > n || !rd_avail(sf, k)) { sf->error = 1; break; }
            memcpy(d + o, sf->buf + sf->pos, k);
            sf->pos += k; o += k;
        } else {
            k = (int)c - 125;
            if (o + k > n || !rd_avail(sf, 1)) { sf->error = 1; break; }
            memset(d + o, sf->buf[sf->pos++], k);
            o += k;
        }
    }
    if (o < n) { memset(d + o, 0, n - o); return 0; }
    return !sf->error;
}

int load_rle(SaveFile *sf, void *p, int n)
{
    u8 *d = (u8 *)p, *pl;
    int mode, i, b, ok;
    if (n <= 0) return 1;
    mode = (int)load_u8(sf);
    if (!load_ok(sf)) { memset(p, 0, n); return 0; }
    if (mode == 0) return rle_decode(sf, d, n);
    if ((mode != 2 && mode != 4 && mode != 8) || n % mode || !(pl = plane_buf(n))) {
        sf->error = 1; memset(p, 0, n); return 0;
    }
    ok = rle_decode(sf, pl, n);
    for (b = 0; b < mode; b++)
        for (i = 0; i < n / mode; i++) d[i * mode + b] = pl[b * (n / mode) + i];
    return ok;
}

int load_ok(SaveFile *sf) { return sf && !sf->error; }

/* ================================================================== validation */
static void set_err(const char *msg)
{
    str_copy(s_last_error, msg, sizeof(s_last_error));
    log_printf("save: %s", msg);
}
const char *save_last_error(void) { return s_last_error; }

/* 1 if buf is a complete, intact save of a version we can read */
static int validate(const u8 *buf, int size)
{
    int off, ver, have_end = 0;
    if (size < HDR_SIZE + 2 * CHUNK_HDR) { set_err("not a " GAME_TITLE_ASCII " save (too short)"); return 0; }
    if (memcmp(buf, SAVE_MAGIC, 4)) { set_err("not a " GAME_TITLE_ASCII " save (bad magic)"); return 0; }
    ver = (int)get_u32_at(buf + 4);
    if (ver < 1 || ver > SAVE_VERSION) {
        char m[96];
        str_printf(m, sizeof(m), "save format version %d is not supported (this game reads 1..%d)", ver, SAVE_VERSION);
        set_err(m);
        return 0;
    }
    off = HDR_SIZE;
    while (off + CHUNK_HDR <= size) {
        u32 len = get_u32_at(buf + off + 8);
        if (len > (u32)(size - off - CHUNK_HDR)) { set_err("save file is truncated (chunk past the end)"); return 0; }
        if (!memcmp(buf + off, "END ", 4)) {
            u32 want, got;
            if (len != 4 || off + CHUNK_HDR + 4 != size) { set_err("save file has a bad end marker"); return 0; }
            want = get_u32_at(buf + off + CHUNK_HDR);
            got = hash_bytes(HASH_INIT, buf, off);
            if (want != got) { set_err("save file is corrupt (checksum mismatch)"); return 0; }
            have_end = 1;
            break;
        }
        off += CHUNK_HDR + (int)len;
    }
    if (!have_end) { set_err("save file is truncated (no end marker)"); return 0; }
    return 1;
}

/* ================================================================== meta + thumbnail */
static void meta_write(SaveFile *sf, const SaveMeta *m)
{
    save_begin_chunk(sf, "META", 1);
    save_str(sf, m->president);
    save_str(sf, m->town);
    save_s32(sf, m->year); save_s32(sf, m->month);
    save_s32(sf, m->population);
    save_s32(sf, m->money);
    save_u32(sf, m->seed);
    save_s32(sf, m->cam_x); save_s32(sf, m->cam_y); save_s32(sf, m->zoom);
    save_u32(sf, m->saved_time);
    save_s32(sf, m->town_status);
    save_s32(sf, m->start_year);
    save_s32(sf, m->map_w); save_s32(sf, m->map_h);
    save_end_chunk(sf);
}

static int meta_read(SaveFile *sf, SaveMeta *m)
{
    int ver;
    memset(m, 0, sizeof(*m));
    if (!load_find_chunk(sf, "META", &ver)) return 0;
    load_str(sf, m->president, NAME_LEN);
    load_str(sf, m->town, NAME_LEN);
    m->year = load_s32(sf); m->month = load_s32(sf);
    m->population = load_s32(sf);
    m->money = load_s32(sf);
    m->seed = load_u32(sf);
    m->cam_x = load_s32(sf); m->cam_y = load_s32(sf); m->zoom = load_s32(sf);
    m->saved_time = load_u32(sf);
    m->town_status = load_s32(sf);
    m->start_year = load_s32(sf);
    m->map_w = load_s32(sf); m->map_h = load_s32(sf);
    return load_ok(sf);
}

void save_meta_from_game(SaveMeta *m)
{
    const SimSettings *s = sim_settings();
    const SimDate *d = sim_date();
    const CityStats *cs = sim_stats();
    str_copy(m->president, s->president, NAME_LEN);
    str_copy(m->town, s->town, NAME_LEN);
    m->year = d->year; m->month = d->month;
    m->population = cs->population;
    m->money = g_budget.money;
    m->seed = s->seed;
    m->saved_time = (u32)time(NULL);
    m->town_status = cs->town_status;
    m->start_year = s->start_year;
    m->map_w = g_world.w; m->map_h = g_world.h;
}

/* thumbnail class of one tile */
static u8 thumb_class(const Tile *t)
{
    int ter = TER_TYPE(t), road = TILE_ROAD(t);
    if (t->lot) {
        const Lot *l = &g_world.lots[t->lot];
        const BuildingDef *d = def_building(l->def);
        if (d->cat == BCAT_ZONE) {
            switch (d->zone) {
            case ZONE_HOUSES: return THUMB_HOUSES;
            case ZONE_APARTMENTS: return THUMB_APARTMENTS;
            case ZONE_COMMERCIAL: return THUMB_COMMERCIAL;
            default: return THUMB_INDUSTRY;
            }
        }
        return THUMB_CIVIC;
    }
    if (road == ROAD_RAIL) return THUMB_RAIL;
    if (road) return THUMB_ROAD;
    if (ter == TER_WATER || ter == TER_RIVER) return THUMB_WATER;
    if (TILE_ZONE(t)) return THUMB_ZONED;
    if (ter == TER_FOREST) return THUMB_FOREST;
    if (ter == TER_ROCK) return THUMB_ROCK;
    if (ter == TER_FIELD) return THUMB_FIELD;
    return THUMB_GRASS;
}

void save_make_thumb(u8 *out)
{
    /* each thumbnail cell = the most "important" class among the tiles it covers (roads and buildings
       stay visible when the map is scaled down), ties: the most common */
    static const u8 prio[THUMB_COUNT] = { 0, 1, 1, 1, 1, 2, 5, 6, 7, 6, 6, 6, 4, 3 };
    int cx, cy, x, y;
    int w = IMAX(1, g_world.w), h = IMAX(1, g_world.h);
    for (cy = 0; cy < SAVE_THUMB; cy++)
        for (cx = 0; cx < SAVE_THUMB; cx++) {
            int cnt[THUMB_COUNT], k, best = THUMB_NONE;
            int x0 = cx * w / SAVE_THUMB, x1 = (cx + 1) * w / SAVE_THUMB;
            int y0 = cy * h / SAVE_THUMB, y1 = (cy + 1) * h / SAVE_THUMB;
            memset(cnt, 0, sizeof(cnt));
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            for (y = y0; y < y1 && y < h; y++)
                for (x = x0; x < x1 && x < w; x++) cnt[thumb_class(WT(x, y))]++;
            for (k = 1; k < THUMB_COUNT; k++)
                if (cnt[k] && (best == THUMB_NONE || prio[k] > prio[best] || (prio[k] == prio[best] && cnt[k] > cnt[best]))) best = k;
            out[cy * SAVE_THUMB + cx] = (u8)best;
        }
}

static void thumb_write(SaveFile *sf)
{
    static u8 th[SAVE_THUMB * SAVE_THUMB];
    save_make_thumb(th);
    save_begin_chunk(sf, "THMB", 1);
    save_u16(sf, SAVE_THUMB);
    save_rle(sf, th, sizeof(th));
    save_end_chunk(sf);
}

/* ================================================================== whole game */
int save_game_to(SaveFile *sf, const SaveMeta *meta)
{
    SaveMeta m;
    u8 h[HDR_SIZE];
    int end_off;
    if (!sf) return 0;
    memset(&m, 0, sizeof(m));
    if (meta) m = *meta;
    save_meta_from_game(&m);                 /* authoritative values from the game; camera from the caller */
    if (meta && meta->saved_time) m.saved_time = meta->saved_time;   /* tests: fixed time for byte-exact files */
    memcpy(h, SAVE_MAGIC, 4);
    h[4] = SAVE_VERSION; h[5] = h[6] = h[7] = 0;
    put(sf, h, HDR_SIZE);
    meta_write(sf, &m);
    thumb_write(sf);
    world_save(sf);
    sim_save(sf);
    sim_save_ext(sf);
    budget_save(sf);
    budget_save_ext(sf);
    fields_save(sf);
    politics_save(sf);
    net_save(sf);
    traffic_save(sf);
    industry_save(sf);                       /* M3: chunk INDU */
    complex_save(sf);                        /* M6: chunk CPLX */
    civic_save(sf);                          /* M5: chunk CIVC */
    era_save(sf);                            /* M7: chunks ERA + TRAD */
    hard_save(sf);                           /* 1.1 D1: chunk HARD (Sweden 1969-70) */
    season_save(sf);                         /* 1.1 W1: chunk SEAS (forecast, hard winter, winter costs) */
    save_end_chunk(sf);                      /* in case a module left one open */
    end_off = sf->size;
    save_begin_chunk(sf, "END ", 1);
    save_u32(sf, 0);
    save_end_chunk(sf);
    if (!sf->error) put_u32_at(sf, end_off + CHUNK_HDR, hash_bytes(HASH_INIT, sf->buf, end_off));
    return !sf->error;
}

int load_game_from(SaveFile *sf, SaveMeta *meta_out)
{
    SaveMeta m;
    SimSettings s;
    int ok = 1;
    double t0 = time_ms();
    if (!sf) return 0;
    s_last_error[0] = 0;
    if (!validate(sf->buf, sf->size)) return 0;
    save_mem_reopen(sf);
    if (!meta_read(sf, &m)) { set_err("save file has no readable META chunk"); return 0; }
    if (!load_find_chunk(sf, "TILE", NULL) || !load_find_chunk(sf, "SIM ", NULL)) {
        set_err("save file lacks the TILE or SIM chunk");
        return 0;
    }
    /* reset every module to new-city defaults; missing chunks keep these */
    sim_default_settings(&s);
    sim_new_city(&s);
    if (!world_load(sf)) { set_err("error: world chunks (TILE/LOTS) failed to load"); ok = 0; }
    if (ok && !sim_load(sf)) { set_err("error: SIM chunk failed to load"); ok = 0; }
    if (ok && load_find_chunk(sf, "BUDG", NULL) && !budget_load(sf)) { set_err("error: BUDG chunk failed to load"); ok = 0; }
    if (ok && load_find_chunk(sf, "FLDS", NULL) && !fields_load(sf)) { set_err("error: FLDS chunk failed to load"); ok = 0; }
    if (ok && !politics_load(sf)) { set_err("error: POLI chunk failed to load"); ok = 0; }
    if (!ok) {
        world_new(MAP_DEF_W, MAP_DEF_H);     /* consistent empty state; caller starts something new */
        sim_new_city(&s);
        return 0;
    }
    /* listeners (renderer, minimap, sim dirtiness) first; then the recomputed/restored derived state,
       so the restored dirty flags are not overwritten by the notification */
    world_notify_all(WCHG_ALL);
    net_reset();
    traffic_reset();
    traffic_load(sf);                        /* T08: chunk TRAF (missing: traffic rebuilds itself) */
    if (!net_load(sf)) net_solve_all();      /* old file without NET: correct, not bit-identical */
    sim_load_ext(sf);                        /* optional chunks: missing = values from sim_load / reset */
    if (load_find_chunk(sf, "BUDG", NULL)) budget_load_ext(sf);
    industry_after_load(industry_load(sf));  /* M3: chunk INDU; missing = old save: kinds migrated, recomputed */
    complex_after_load(complex_load(sf));    /* M6: chunk CPLX; missing = no complexes */
    civic_load(sf);                          /* M5: chunk CIVC; missing = fresh (solved on schedule) */
    era_load(sf);                            /* M7: chunks ERA + TRAD; missing = fresh for the save's date */
    hard_load(sf);                           /* 1.1 D1: chunk HARD; missing = fresh */
    season_load(sf);                         /* 1.1 W1: chunk SEAS; missing = fresh (no forecast) */
    if (!load_ok(sf)) {                      /* validated file whose optional chunk was malformed */
        set_err("error: an optional chunk (TRAF/NET/SIMX/BUDX) failed to load");
        world_new(MAP_DEF_W, MAP_DEF_H);
        sim_new_city(&s);
        return 0;
    }
    if (meta_out) *meta_out = m;
    log_printf("save: loaded %s / %s, %d-%02d, pop %d, %d bytes in %.1f ms", m.town, m.president, m.year, m.month + 1,
               m.population, sf->size, time_ms() - t0);
    return 1;
}

/* ================================================================== files */
static u8 *read_file(const char *path, int *size_out, int max_bytes)
{
    FILE *f = fopen(path, "rb");
    long n;
    u8 *buf;
    *size_out = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > SAVE_MAX_FILE) { fclose(f); return NULL; }
    if (max_bytes > 0 && n > max_bytes) n = max_bytes;
    buf = (u8 *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if ((long)fread(buf, 1, (size_t)n, f) != n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *size_out = (int)n;
    return buf;
}

int save_write_file(SaveFile *sf, const char *path)
{
    char tmp[PATH_LEN + 8];
    FILE *f;
    int ok;
    if (!sf || sf->error || !path || !path[0]) return 0;
    str_printf(tmp, sizeof(tmp), "%s.tmp", path);
    f = fopen(tmp, "wb");
    if (!f) { log_printf("save: error: cannot write %s", tmp); return 0; }
    ok = (int)fwrite(sf->buf, 1, sf->size, f) == sf->size;
    if (fclose(f) != 0) ok = 0;
    if (!ok) { remove(tmp); log_printf("save: error: write failed for %s", tmp); return 0; }
    remove(path);                            /* Win9x rename() fails if the target exists */
    if (rename(tmp, path) != 0) { log_printf("save: error: rename %s -> %s failed", tmp, path); return 0; }
    return 1;
}

int save_game(const char *path, const SaveMeta *meta)
{
    SaveFile *sf = save_mem_open();
    int ok;
    double t0 = time_ms(), t1;
    if (!sf) return 0;
    ok = save_game_to(sf, meta);
    t1 = time_ms();
    ok = ok && save_write_file(sf, path);
    log_printf("save: %s %s (%d bytes, %.1f ms: serialize %.1f, write %.1f)", ok ? "saved" : "error: failed to save", path,
               sf->size, time_ms() - t0, t1 - t0, time_ms() - t1);
    save_mem_close(sf);
    return ok;
}

int load_game(const char *path, SaveMeta *meta_out)
{
    int n, ok;
    u8 *buf = read_file(path, &n, 0);
    SaveFile *sf;
    if (!buf) { set_err("cannot open the save file"); log_printf("save: (%s)", path); return 0; }
    sf = save_mem_from(buf, n);
    free(buf);
    if (!sf) return 0;
    ok = load_game_from(sf, meta_out);
    save_mem_close(sf);
    return ok;
}

/* reads only the first chunks of a file (META, THMB) */
static SaveFile *open_head(const char *path)
{
    FILE *f = fopen(path, "rb");
    u8 hdr[HDR_SIZE + CHUNK_HDR];
    SaveFile *sf;
    int k, ver;
    if (!f) return NULL;
    if (fread(hdr, 1, HDR_SIZE, f) != HDR_SIZE || memcmp(hdr, SAVE_MAGIC, 4)) { fclose(f); return NULL; }
    ver = (int)get_u32_at(hdr + 4);
    if (ver < 1 || ver > SAVE_VERSION) { fclose(f); return NULL; }
    sf = save_mem_open();
    if (!sf) { fclose(f); return NULL; }
    put(sf, hdr, HDR_SIZE);
    for (k = 0; k < 2; k++) {                /* META, THMB */
        u8 ch[CHUNK_HDR];
        u32 len;
        if (fread(ch, 1, CHUNK_HDR, f) != CHUNK_HDR) break;
        len = get_u32_at(ch + 8);
        if (len > 64 * 1024) break;
        put(sf, ch, CHUNK_HDR);
        sf_reserve(sf, (int)len);
        if (sf->error || fread(sf->buf + sf->size, 1, len, f) != len) { sf->size -= CHUNK_HDR; break; }
        sf->size += (int)len;
    }
    fclose(f);
    return save_mem_reopen(sf);
}

int save_read_meta(const char *path, SaveMeta *out)
{
    SaveFile *sf = open_head(path);
    int ok;
    memset(out, 0, sizeof(*out));
    if (!sf) return 0;
    ok = meta_read(sf, out);
    save_mem_close(sf);
    return ok;
}

int save_read_thumb(const char *path, u8 *out)
{
    SaveFile *sf = open_head(path);
    int ok = 0, ver;
    memset(out, 0, SAVE_THUMB * SAVE_THUMB);
    if (!sf) return 0;
    if (load_find_chunk(sf, "THMB", &ver) && load_u16(sf) == SAVE_THUMB) ok = load_rle(sf, out, SAVE_THUMB * SAVE_THUMB);
    save_mem_close(sf);
    return ok;
}

int save_file_exists(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* ================================================================== paths, slots, autosave */
void save_set_dir(const char *dir) { str_copy(s_dir, dir ? dir : "", PATH_LEN); }

static void make_path(char *buf, int size, const char *name)
{
    int n = (int)strlen(s_dir);
    if (!n) { str_copy(buf, name, size); return; }
    if (s_dir[n - 1] == '\\' || s_dir[n - 1] == '/') str_printf(buf, size, "%s%s", s_dir, name);
#ifdef _WIN32
    else str_printf(buf, size, "%s\\%s", s_dir, name);
#else
    else str_printf(buf, size, "%s/%s", s_dir, name);
#endif
}

void save_slot_path(int slot, char *buf, int size)
{
    char n[16];
    str_printf(n, sizeof(n), "SAVE%d.KAU", ICLAMP(slot, 0, SAVE_SLOTS - 1) + 1);
    make_path(buf, size, n);
}

void save_auto_path(int n, char *buf, int size)
{
    char nm[16];
    str_printf(nm, sizeof(nm), "AUTO%d.KAU", ICLAMP(n, 0, SAVE_AUTOS - 1) + 1);
    make_path(buf, size, nm);
}

void save_quick_path(char *buf, int size) { make_path(buf, size, "QUICK.KAU"); }

int save_autosave(const SaveMeta *meta)
{
    char a[PATH_LEN], b[PATH_LEN];
    int i;
    /* AUTO1 = newest: AUTO3 dropped, AUTO2 -> AUTO3, AUTO1 -> AUTO2, then write AUTO1 */
    save_auto_path(SAVE_AUTOS - 1, a, sizeof(a));
    remove(a);
    for (i = SAVE_AUTOS - 2; i >= 0; i--) {
        save_auto_path(i, a, sizeof(a));
        save_auto_path(i + 1, b, sizeof(b));
        if (save_file_exists(a)) rename(a, b);
    }
    save_auto_path(0, a, sizeof(a));
    return save_game(a, meta);
}

int save_quicksave(const SaveMeta *meta)
{
    char p[PATH_LEN];
    save_quick_path(p, sizeof(p));
    return save_game(p, meta);
}

void save_any_path(int idx, char *buf, int size)
{
    if (idx < SAVE_SLOTS) save_slot_path(idx, buf, size);
    else if (idx == SAVE_SLOTS) save_quick_path(buf, size);
    else save_auto_path(idx - SAVE_SLOTS - 1, buf, size);
}

/* ================================================================== self-test (-test save in the VM,
   also run by the Mac tests): the same checks as tests/test_save.cpp on the real compiler/OS. Builds a
   small scripted town with world_* setters (layout from test_sim.cpp), logs every result. */
static int s_st_fail;
static void st_check(int ok, const char *what)
{
    log_printf("save_selftest: %s %s", ok ? "PASS" : "FAIL", what);
    if (!ok) s_st_fail++;
}

static void st_road(int x0, int y0, int x1, int y1, int cls)
{
    int x, y;
    world_batch_begin();
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!TILE_ROAD(WT(x, y)) && !WT(x, y)->lot) world_set_road(x, y, cls);
    world_batch_end();
}

static void st_zone(int x0, int y0, int x1, int y1, int z)
{
    int x, y;
    world_batch_begin();
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (world_is_buildable(x, y) && world_road_within(x, y, ZONE_ROAD_REACH)) world_set_zone(x, y, z);
    world_batch_end();
}

static void st_town(u32 seed)
{
    SimSettings s;
    int x, y;
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = seed;
    str_copy(s.town, "Koivulahti", NAME_LEN);
    sim_new_city(&s);
    for (y = 76; y <= 112; y++) for (x = 4; x <= 24; x++) world_set_terrain(x, y, TER_WATER, 0);
    st_road(0, 64, 127, 64, ROAD_STREET);
    world_set_tile_flags(0, 64, TF_EDGE_LINK | TF_PROTECTED, 0);
    world_set_tile_flags(127, 64, TF_EDGE_LINK | TF_PROTECTED, 0);
    for (x = 30; x <= 62; x += 8) st_road(x, 40, x, 88, ROAD_STREET);
    st_road(30, 40, 62, 40, ROAD_STREET); st_road(30, 56, 62, 56, ROAD_STREET); st_road(30, 80, 62, 80, ROAD_STREET);
    st_road(26, 88, 62, 88, ROAD_GRAVEL); st_road(26, 88, 26, 104, ROAD_GRAVEL);
    st_road(80, 44, 80, 63, ROAD_STREET); st_road(92, 44, 92, 63, ROAD_STREET); st_road(80, 44, 112, 44, ROAD_STREET);
    world_lot_place(B_FIRE_STATION, 39, 57, ROT_0, 0, LOT_ACTIVE);
    world_lot_place(B_TOWN_HALL, 63, 57, ROT_0, 0, LOT_ACTIVE);
    st_zone(31, 41, 61, 60, ZONE_HOUSES);
    st_zone(31, 61, 61, 67, ZONE_COMMERCIAL);
    st_zone(31, 68, 61, 87, ZONE_HOUSES);
    st_zone(81, 45, 91, 63, ZONE_INDUSTRY);
    world_lot_place(B_COAL_PLANT, 112, 45, ROT_0, 0, LOT_ACTIVE);
    for (x = 30; x <= 111; x++) world_set_power_line(x, 42, 1);
    for (y = 42; y <= 60; y++) world_set_power_line(111, y, 1);
    for (y = 41; y <= 88; y++) world_set_power_line(30, y, 1);
    world_lot_place(B_PUMP, 25, 90, ROT_0, 0, LOT_ACTIVE);
    world_lot_place(B_PUMP, 25, 100, ROT_0, 0, LOT_ACTIVE);
}

static void st_action(void)
{
    world_lot_place(B_SCHOOL, 39, 73, ROT_0, 0, LOT_ACTIVE);
    world_lot_place(B_SAUNA, 47, 81, ROT_0, 0, LOT_ACTIVE);
}

int save_selftest(void)
{
    SaveFile *a, *b;
    SaveMeta m, r;
    char path[PATH_LEN], line[160];
    u32 wh, sh, h_full, h_split;
    s32 pop;
    double t0;
    s_st_fail = 0;
    memset(&m, 0, sizeof(m));
    m.saved_time = 1000000; m.cam_x = 40; m.cam_y = 60;

    /* 1. memory round trip: hashes + byte-exact re-save */
    st_town(77);
    t0 = time_ms();
    sim_run_months(14);
    wh = world_hash(); sh = sim_hash(); pop = sim_stats()->population;
    str_printf(line, sizeof(line), "town after 14 months: pop %d, %d lots (%.0f ms)", pop, g_world.lot_count, time_ms() - t0);
    st_check(pop > 100, line);
    a = save_mem_open();
    t0 = time_ms();
    st_check(save_game_to(a, &m), "save_game_to");
    str_printf(line, sizeof(line), "save size %d bytes in %.1f ms", save_mem_size(a), time_ms() - t0);
    st_check(save_mem_size(a) > 1000 && save_mem_size(a) < 256 * 1024, line);
    st_town(5);
    save_mem_reopen(a);
    t0 = time_ms();
    st_check(load_game_from(a, &r), "load_game_from");
    str_printf(line, sizeof(line), "world_hash %08X == %08X, sim_hash %08X == %08X, pop %d == %d (load %.1f ms)",
               world_hash(), wh, sim_hash(), sh, sim_stats()->population, pop, time_ms() - t0);
    st_check(world_hash() == wh && sim_hash() == sh && sim_stats()->population == pop, line);
    b = save_mem_open();
    save_game_to(b, &m);
    st_check(save_mem_size(a) == save_mem_size(b) && !memcmp(a->buf, b->buf, a->size), "re-save is byte-identical");
    save_mem_close(b);

    /* 2. determinism: run 10 + action + 12 months unbroken vs. save/load at month 10 */
    st_town(4242); sim_run_months(10); st_action(); sim_run_months(12); h_full = sim_hash();
    st_town(4242); sim_run_months(10);
    b = save_mem_open(); save_game_to(b, &m);
    st_town(1); save_mem_reopen(b); load_game_from(b, NULL); save_mem_close(b);
    st_action(); sim_run_months(12); h_split = sim_hash();
    str_printf(line, sizeof(line), "determinism: unbroken %08X, save+load %08X", h_full, h_split);
    st_check(h_full == h_split, line);

    /* 3. files: slot write + meta/thumbnail read + load */
    save_slot_path(SAVE_SLOTS - 1, path, sizeof(path));
    save_mem_reopen(a);
    load_game_from(a, NULL);
    st_check(save_game(path, &m), path);
    st_check(save_read_meta(path, &r) && !strcmp(r.town, "Koivulahti") && r.population == pop, "save_read_meta");
    {
        static u8 th[SAVE_THUMB * SAVE_THUMB];
        st_check(save_read_thumb(path, th) && th[0] != THUMB_NONE, "save_read_thumb");
    }
    st_town(9);
    st_check(load_game(path, NULL) && world_hash() == wh, "load_game from the file");

    /* 4. corrupt copies are rejected and leave the running game alone */
    {
        int n = a->size;
        u8 *c = (u8 *)malloc(n);
        SaveFile *bad;
        memcpy(c, a->buf, n); c[n / 3] ^= 4;
        bad = save_mem_from(c, n);
        st_check(!load_game_from(bad, NULL) && world_hash() == wh, "flipped bit rejected, game untouched");
        save_mem_close(bad);
        memcpy(c, a->buf, n); c[4] = SAVE_VERSION + 1;
        bad = save_mem_from(c, n);
        st_check(!load_game_from(bad, NULL) && world_hash() == wh, "newer version rejected, game untouched");
        save_mem_close(bad);
        bad = save_mem_from(c, n / 2);
        st_check(!load_game_from(bad, NULL) && world_hash() == wh, "truncated file rejected, game untouched");
        save_mem_close(bad);
        free(c);
    }
    remove(path);
    save_mem_close(a);
    log_printf("save_selftest: %d failures", s_st_fail);
    return s_st_fail;
}
