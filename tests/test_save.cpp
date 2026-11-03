/* test_save.cpp - save/load tests (T10): RLE, byte-exact round trips, world/sim hashes after load,
   determinism across save/load, corrupt/old-version files rejected without touching the game, slot files,
   metadata + thumbnail, autosave rotation. Dumps go to tests/out/save_*.txt (debug from text).
   The town is built with world_* setters directly (helpers copied from test_sim.cpp, T04). */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "save.h"

/* ------------------------------------------------------------------ town helpers (from test_sim.cpp) */
static void new_town(u32 seed)
{
    SimSettings s;
    static int inited;
    /* world_init() only if nobody did yet: a second world_init drops the listeners other test files'
       sim_init registered (see report: test harness note) */
    if (!inited) { if (!g_world.w) world_init(); sim_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = seed;
    str_copy(s.town, "Koivulahti", NAME_LEN);
    sim_new_city(&s);
}

static void road(int x0, int y0, int x1, int y1, int cls)
{
    int x, y, n = 0;
    world_batch_begin();
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!TILE_ROAD(world_tile(x, y)) && !world_tile(x, y)->lot) { world_set_road(x, y, cls); n++; }
    world_batch_end();
    budget_spend(n * g_roads[cls].cost, BL_CONSTRUCTION);
}

static void pline(int x0, int y0, int x1, int y1)
{
    int x, y, n = 0;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!world_tile(x, y)->lot) { world_set_power_line(x, y, 1); n++; }
    budget_spend(n * g_misc.power_line, BL_CONSTRUCTION);
}

static void zone(int x0, int y0, int x1, int y1, int z)
{
    int x, y;
    world_batch_begin();
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (world_is_buildable(x, y) && world_road_within(x, y, ZONE_ROAD_REACH)) world_set_zone(x, y, z);
    world_batch_end();
}

static void build(int def, int x, int y)
{
    int w, h, i, j;
    def_footprint(def, ROT_0, &w, &h);
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++) {
            int l = world_lot_at(i, j);
            if (l && def_building(g_world.lots[l].def)->cat == BCAT_ZONE) world_lot_remove(l);
        }
    if (world_lot_place(def, x, y, ROT_0, 0, LOT_ACTIVE)) budget_spend(def_building(def)->cost, BL_CONSTRUCTION);
}

static void lake(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) world_set_terrain(x, y, TER_WATER, 0);
}

static void town_stage0(void)
{
    int x, y;
    lake(4, 76, 24, 112);
    road(0, 64, 127, 64, ROAD_STREET);
    for (x = 0; x < 128; x++) world_set_tile_flags(x, 64, TF_PROTECTED, 0);
    world_set_tile_flags(0, 64, TF_EDGE_LINK, 0);
    world_set_tile_flags(127, 64, TF_EDGE_LINK, 0);
    for (x = 30; x <= 62; x += 8) road(x, 40, x, 88, ROAD_STREET);
    road(30, 40, 62, 40, ROAD_STREET); road(30, 48, 62, 48, ROAD_STREET); road(30, 56, 62, 56, ROAD_STREET);
    road(30, 72, 62, 72, ROAD_STREET); road(30, 80, 62, 80, ROAD_STREET); road(26, 88, 62, 88, ROAD_GRAVEL);
    road(26, 88, 26, 104, ROAD_GRAVEL);
    road(80, 44, 80, 63, ROAD_STREET); road(92, 44, 92, 63, ROAD_STREET); road(104, 44, 104, 63, ROAD_STREET);
    road(80, 44, 112, 44, ROAD_STREET); road(80, 54, 104, 54, ROAD_STREET);
    build(B_FIRE_STATION, 39, 57);
    build(B_CLINIC, 47, 57);
    build(B_TOWN_HALL, 63, 57);
    zone(31, 41, 61, 55, ZONE_HOUSES);
    zone(31, 57, 61, 60, ZONE_HOUSES);
    zone(31, 61, 61, 63, ZONE_COMMERCIAL);
    zone(31, 65, 61, 67, ZONE_COMMERCIAL);
    zone(31, 68, 61, 87, ZONE_HOUSES);
    zone(81, 45, 103, 63, ZONE_INDUSTRY);
    build(B_COAL_PLANT, 112, 45);
    pline(30, 42, 111, 42);
    pline(111, 42, 111, 60);
    for (y = 40; y <= 88; y += 8) if (y != 64) pline(30, y, 62, y);
    pline(30, 41, 30, 88);
    build(B_PUMP, 25, 90);
    build(B_PUMP, 25, 100);
}

static void town_stage1(void)     /* a player action mid-game (after the save point in the split run) */
{
    build(B_SCHOOL, 39, 73);
    build(B_SCHOOL, 49, 44);
    build(B_SAUNA, 55, 57);
    build(B_SAUNA, 47, 81);
    build(B_SAUNA, 39, 49);
    build(B_COAL_PLANT, 112, 50);
    build(B_PUMP, 25, 95);
}

static int s_dump_n;
static void dump_state(const char *name)
{
    char path[128];
    FILE *f;
    str_printf(path, sizeof(path), "out/save_%s.txt", name);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "world_hash %08X sim_hash %08X\n", world_hash(), sim_hash());
    sim_dump_report(f);
    world_dump(f, 0, 0, g_world.w - 1, g_world.h - 1);
    fclose(f);
    s_dump_n++;
}

/* world snapshot + diff (debug from text: the first differing tiles/lots go to the test output) */
static Tile s_snap_t[MAP_MAX_W * MAP_MAX_H];
static Lot  s_snap_l[MAX_LOTS];
static void snap_world(void) { memcpy(s_snap_t, g_world.t, sizeof(s_snap_t)); memcpy(s_snap_l, g_world.lots, sizeof(s_snap_l)); }
static int diff_world(int maxlog)
{
    int x, y, i, nd = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            const Tile *o = &s_snap_t[TILE_IDX(x, y)], *n = WT(x, y);
            if (!memcmp(o, n, sizeof(Tile))) continue;
            if (nd++ < maxlog)
                test_log("tile %d,%d ter %02X/%02X road %02X/%02X zone %02X/%02X flags %02X/%02X lot %d/%d lv %d/%d aux %d/%d",
                         x, y, o->terrain, n->terrain, o->road, n->road, o->zone, n->zone, o->flags, n->flags, o->lot, n->lot,
                         o->lv, n->lv, o->aux, n->aux);
        }
    for (i = 1; i < MAX_LOTS; i++) {
        const Lot *o = &s_snap_l[i], *n = &g_world.lots[i];
        if (!memcmp(o, n, sizeof(Lot))) continue;
        if (nd++ < maxlog)
            test_log("lot %d def %d/%d status %d/%d flags %02X/%02X occ %d/%d problems %X/%X growth %d/%d happy %d/%d",
                     i, o->def, n->def, o->status, n->status, o->flags, n->flags, o->occupants, n->occupants,
                     (unsigned)o->problems, (unsigned)n->problems, o->growth, n->growth, o->happy, n->happy);
    }
    return nd;
}

/* which chunks differ between two save buffers (debug from text) */
static void diff_chunks(SaveFile *a, SaveFile *b)
{
    const u8 *da = save_mem_data(a), *db = save_mem_data(b);
    int na = save_mem_size(a), nb = save_mem_size(b), oa = 8, ob = 8;
    while (oa + 12 <= na && ob + 12 <= nb) {
        int la = (int)(da[oa + 8] | (da[oa + 9] << 8) | (da[oa + 10] << 16) | (da[oa + 11] << 24));
        int lb = (int)(db[ob + 8] | (db[ob + 9] << 8) | (db[ob + 10] << 16) | (db[ob + 11] << 24));
        if (la != lb || memcmp(da + oa, db + ob, 12 + la)) {
            int k = 0;
            while (k < la && k < lb && da[oa + 12 + k] == db[ob + 12 + k]) k++;
            test_log("chunk %.4s differs: %d vs %d bytes, first difference at data byte %d", (const char *)da + oa, la, lb, k);
        }
        oa += 12 + la; ob += 12 + lb;
    }
}

/* fixed meta for byte-exact comparisons */
static SaveMeta fixed_meta(void)
{
    SaveMeta m;
    memset(&m, 0, sizeof(m));
    m.cam_x = 50; m.cam_y = 60; m.zoom = 1;
    m.saved_time = 1000000;
    return m;
}

/* ------------------------------------------------------------------ RLE */
TEST(save_rle_roundtrip)
{
    static u8 src[5000], dst[5000];
    SaveFile *sf;
    int lens[] = { 1, 2, 3, 127, 128, 129, 130, 131, 260, 5000 }, li, i, ok;
    u32 h = 12345;
    for (li = 0; li < COUNTOF(lens); li++) {
        int n = lens[li];
        int pattern;
        for (pattern = 0; pattern < 4; pattern++) {
            for (i = 0; i < n; i++) {
                h = h * 1103515245u + 12345u;
                switch (pattern) {
                case 0: src[i] = (u8)(h >> 16); break;                 /* noise: all literals */
                case 1: src[i] = 7; break;                             /* one long run */
                case 2: src[i] = (u8)((i / 3) & 1); break;             /* runs of exactly 3 */
                default: src[i] = (u8)(((h >> 20) & 7) ? 0 : (h >> 8)); break;   /* mostly zeros */
                }
            }
            sf = save_mem_open();
            save_begin_chunk(sf, "TEST", 1);
            save_rle(sf, src, n);
            save_u32(sf, 0xDEADBEEF);
            save_end_chunk(sf);
            /* a reader needs the file header before the chunk: prepend one */
            {
                SaveFile *rd;
                u8 *tmp = (u8 *)malloc(save_mem_size(sf) + 8);
                memcpy(tmp, "KAUP\x01\0\0\0", 8);
                memcpy(tmp + 8, save_mem_data(sf), save_mem_size(sf));
                rd = save_mem_from(tmp, save_mem_size(sf) + 8);
                free(tmp);
                memset(dst, 0xAA, sizeof(dst));
                CHECK(load_find_chunk(rd, "TEST", &i));
                ok = load_rle(rd, dst, n);
                CHECK(ok);
                CHECK(!memcmp(src, dst, n));
                CHECK_EQ(load_u32(rd), 0xDEADBEEF);
                CHECK(load_ok(rd));
                load_u8(rd);                                           /* past the chunk end */
                CHECK(!load_ok(rd));
                save_mem_close(rd);
            }
            if (pattern == 1 && n == 5000) test_log("rle: 5000 equal bytes -> %d bytes", save_mem_size(sf) - 12 - 4);
            save_mem_close(sf);
        }
    }
}

/* ------------------------------------------------------------------ round trip */
TEST(save_roundtrip_byte_exact)
{
    SaveFile *a, *b;
    SaveMeta m = fixed_meta(), mo;
    u32 wh, sh, wh2, sh2;
    s32 pop;
    new_town(77);
    town_stage0();
    sim_run_months(14);
    wh = world_hash(); sh = sim_hash(); pop = sim_stats()->population;
    test_log("town after 14 months: pop %d, lots %d, world %08X sim %08X", pop, g_world.lot_count, wh, sh);
    CHECK(pop > 200);
    snap_world();
    a = save_mem_open();
    CHECK(save_game_to(a, &m));
    test_log("save size %d bytes", save_mem_size(a));
    CHECK(save_mem_size(a) < 200 * 1024);
    dump_state("before");

    /* wreck the state, then load */
    new_town(999);
    CHECK(world_hash() != wh);
    save_mem_reopen(a);
    CHECK(load_game_from(a, &mo));
    wh2 = world_hash(); sh2 = sim_hash();
    dump_state("after");
    if (wh2 != wh) test_log("%d tile/lot differences after load", diff_world(12));
    CHECK_EQ(mo.cam_x, 50); CHECK_EQ(mo.cam_y, 60); CHECK_EQ(mo.zoom, 1);
    CHECK(!strcmp(mo.town, "Koivulahti"));
    CHECK(!strcmp(mo.president, "Kekkola"));
    CHECK_EQ(mo.population, pop);
    CHECK_EQ(mo.year, sim_date()->year);
    CHECK_EQ(wh2, wh);
    CHECK_EQ(sim_stats()->population, pop);
    CHECK_EQ(sh2, sh);
    CHECK(!strcmp(sim_settings()->town, "Koivulahti"));

    /* re-save: byte-identical */
    b = save_mem_open();
    CHECK(save_game_to(b, &m));
    diff_chunks(a, b);
    CHECK_EQ(save_mem_size(b), save_mem_size(a));
    CHECK(!memcmp(save_mem_data(a), save_mem_data(b), save_mem_size(a)));
    save_mem_close(a);
    save_mem_close(b);
}

/* ------------------------------------------------------------------ determinism across save/load */
static u32 s_det_hash[3];
static void det_run(int split, int n, int m, u32 *hash_out, int *pop_out)
{
    SaveFile *sf = NULL;
    SaveMeta meta = fixed_meta();
    new_town(4242);
    town_stage0();
    sim_run_months(n);
    if (split) {
        sf = save_mem_open();
        save_game_to(sf, &meta);
        new_town(1);                          /* forget everything */
        save_mem_reopen(sf);
        if (!load_game_from(sf, NULL)) { *hash_out = 0; save_mem_close(sf); return; }
        save_mem_close(sf);
    }
    town_stage1();                            /* the same player action after the save point */
    sim_run_months(m);
    *hash_out = sim_hash();
    *pop_out = sim_stats()->population;
}

TEST(save_determinism_across_load)
{
    static const int ns[3] = { 7, 18, 30 };
    int k;
    for (k = 0; k < 3; k++) {
        u32 h_full, h_split;
        int p_full, p_split;
        det_run(0, ns[k], 12, &h_full, &p_full);
        if (k == 0) dump_state("det_full");
        det_run(1, ns[k], 12, &h_split, &p_split);
        if (k == 0) dump_state("det_split");
        test_log("N=%d M=12: unbroken %08X pop %d | save+load %08X pop %d %s", ns[k], h_full, p_full, h_split, p_split,
                 h_full == h_split ? "same" : "DIFFERENT");
        s_det_hash[k] = h_full;
        CHECK(h_split != 0);
        CHECK_EQ(h_split, h_full);
    }
}

/* ------------------------------------------------------------------ corrupt / old files */
TEST(save_corrupt_rejected)
{
    SaveFile *a, *bad;
    SaveMeta m = fixed_meta();
    u8 *copy;
    int n, i, k;
    u32 wh, sh;
    new_town(5);
    town_stage0();
    sim_run_months(3);
    a = save_mem_open();
    CHECK(save_game_to(a, &m));
    n = save_mem_size(a);
    copy = (u8 *)malloc(n);

    /* a different running game that must survive every failed load untouched */
    new_town(6);
    town_stage0();
    sim_run_months(2);
    wh = world_hash(); sh = sim_hash();

    for (k = 0; k < 7; k++) {
        int len = n;
        const char *what = "";
        memcpy(copy, save_mem_data(a), n);
        switch (k) {
        case 0: copy[n / 2] ^= 0x10; what = "flipped bit in the middle"; break;
        case 1: copy[n - 1] ^= 1; what = "flipped bit in the checksum"; break;
        case 2: len = n - 100; what = "truncated by 100 bytes"; break;
        case 3: len = 30; what = "truncated to 30 bytes"; break;
        case 4: copy[0] = 'X'; what = "bad magic"; break;
        case 5: copy[4] = SAVE_VERSION + 1; what = "newer format version"; break;
        default: copy[4] = 0; what = "format version 0"; break;
        }
        bad = save_mem_from(copy, len);
        i = load_game_from(bad, NULL);
        test_log("%-28s -> %s (%s)", what, i ? "LOADED" : "rejected", save_last_error());
        save_mem_close(bad);
        CHECK(!i);
        CHECK_EQ(world_hash(), wh);
        CHECK_EQ(sim_hash(), sh);
    }
    /* the intact copy still loads */
    bad = save_mem_from(save_mem_data(a), n);
    CHECK(load_game_from(bad, NULL));
    save_mem_close(bad);
    free(copy);
    save_mem_close(a);
}

/* ------------------------------------------------------------------ files, slots, meta, thumbnail, autosave */
TEST(save_files_slots_autosave)
{
    char p[PATH_LEN];
    SaveMeta m = fixed_meta(), r;
    static u8 th[SAVE_THUMB * SAVE_THUMB];
    int i, cnt[THUMB_COUNT], y;
    u32 wh;
    FILE *f;
    save_set_dir("out");
    save_slot_path(2, p, sizeof(p));
    CHECK(!strcmp(p, "out/SAVE3.KAU"));
    new_town(321);
    town_stage0();
    sim_run_months(6);
    wh = world_hash();
    CHECK(save_game(p, &m));
    CHECK(save_file_exists(p));
    CHECK(save_read_meta(p, &r));
    CHECK(!strcmp(r.town, "Koivulahti"));
    CHECK_EQ(r.population, sim_stats()->population);
    CHECK_EQ(r.money, g_budget.money);
    CHECK_EQ(r.year, 1955);
    CHECK_EQ(r.month, sim_date()->month);
    CHECK_EQ(r.map_w, 128);
    CHECK(save_read_thumb(p, th));
    memset(cnt, 0, sizeof(cnt));
    for (i = 0; i < SAVE_THUMB * SAVE_THUMB; i++) cnt[th[i] < THUMB_COUNT ? th[i] : 0]++;
    CHECK(cnt[THUMB_WATER] > 20);
    CHECK(cnt[THUMB_ROAD] > 50);
    CHECK(cnt[THUMB_NONE] == 0);
    f = fopen("out/save_thumb.txt", "w");
    if (f) {
        static const char ch[THUMB_COUNT + 1] = "?.T^,~#=HACIXz";
        for (y = 0; y < SAVE_THUMB; y++) {
            for (i = 0; i < SAVE_THUMB; i++) fputc(ch[th[y * SAVE_THUMB + i]], f);
            fputc('\n', f);
        }
        fclose(f);
    }
    /* load from the file */
    new_town(1);
    CHECK(load_game(p, &r));
    CHECK_EQ(world_hash(), wh);
    CHECK(!load_game("out/NO_SUCH_FILE.KAU", &r));

    /* autosave rotation: 4 yearly saves -> AUTO1 newest, AUTO3 oldest kept */
    for (i = 0; i < 3; i++) { save_auto_path(i, p, sizeof(p)); remove(p); }
    for (i = 0; i < 4; i++) {
        sim_run_months(12);
        CHECK(save_autosave(&m));
    }
    for (i = 0; i < 3; i++) {
        save_auto_path(i, p, sizeof(p));
        CHECK(save_read_meta(p, &r));
        test_log("%s: %d-%02d pop %d", p, r.year, r.month + 1, r.population);
        CHECK_EQ(r.year, sim_date()->year - i);
    }
    CHECK(save_quicksave(&m));
    save_quick_path(p, sizeof(p));
    CHECK(save_read_meta(p, &r));
    CHECK_EQ(r.year, sim_date()->year);
    save_any_path(SAVE_SLOTS, p, sizeof(p));
    CHECK(!strcmp(p, "out/QUICK.KAU"));
    save_any_path(SAVE_SLOTS + 1, p, sizeof(p));
    CHECK(!strcmp(p, "out/AUTO1.KAU"));
    save_set_dir("");
}

/* a file missing an optional chunk (here FLDS, as a future version might drop one) still loads */
TEST(save_missing_chunk_tolerated)
{
    SaveFile *a, *b;
    SaveMeta m = fixed_meta();
    const u8 *d;
    int n, off, ok;
    new_town(8);
    town_stage0();
    sim_run_months(2);
    a = save_mem_open();
    CHECK(save_game_to(a, &m));
    d = save_mem_data(a); n = save_mem_size(a);
    /* rebuild the file without FLDS: copy chunks, recompute the END hash */
    b = save_mem_open();
    save_bytes(b, d, 8);
    off = 8;
    while (off + 12 <= n) {
        int len = (int)(d[off + 8] | (d[off + 9] << 8) | (d[off + 10] << 16) | (d[off + 11] << 24));
        if (!memcmp(d + off, "END ", 4)) break;
        if (memcmp(d + off, "FLDS", 4)) save_bytes(b, d + off, 12 + len);
        off += 12 + len;
    }
    {
        u32 h = hash_bytes(HASH_INIT, save_mem_data(b), save_mem_size(b));
        save_begin_chunk(b, "END ", 1);
        save_u32(b, h);
        save_end_chunk(b);
    }
    save_mem_reopen(b);
    ok = load_game_from(b, NULL);
    test_log("without FLDS: %d -> %d bytes, load %s", n, save_mem_size(b), ok ? "ok" : save_last_error());
    CHECK(ok);
    save_mem_close(a);
    save_mem_close(b);
}

/* the portable self-test that -test save runs in the VM (VC6) */
TEST(save_selftest_portable)
{
    save_set_dir("out");
    if (!g_world.w) world_init();
    sim_init();
    CHECK_EQ(save_selftest(), 0);
    save_set_dir("");
}
