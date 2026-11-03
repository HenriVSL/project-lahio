/* test_perf.cpp - T13 performance helpers and checks (Mac, headless). Owner: T13.

   perf_bench_city (only with KAUP_BENCHCITY=1): builds the dense bench town (scripts/bench20k.txt, generated
   by tools/gen_bench20k.py) on the Lakeland seed-7 map, runs 12 years headless and writes BENCH20K.KAU in
   the project root. The VM bench loads it: lahio.exe -load BENCH20K.KAU -bench 60 -speed 3.
   Save files are portable (VC6 hashes = clang hashes, T10), so the Mac-made file loads in the game.

   perf_phase_timing: per-phase Mac timings of the dense bench town (x ~100 = P2-233), when the file exists. */
#include "test.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "traffic.h"
#include "tools.h"
#include "mapgen.h"
#include "save.h"
#include "util.h"
#include <stdlib.h>

static void perf_fresh(u32 seed)
{
    static int inited;
    MapGenParams mp;
    SimSettings s;
    if (!inited) { world_init(); budget_init(); net_init(); fields_init(); traffic_init(); sim_init(); tools_init(); inited = 1; }
    mapgen_defaults(&mp);
    mp.seed = seed;
    mapgen_generate(&mp);
    sim_default_settings(&s);
    s.seed = seed;
    sim_new_city(&s);
    tools_reset();
}

TEST(perf_bench_city)
{
    FILE *out;
    int errors;
    SaveMeta m;
    if (!getenv("KAUP_BENCHCITY")) return;
    perf_fresh(7);
    g_budget.money = 90000000;                      /* bench city: no money limits while building */
    out = fopen("out/bench20k_script_out.txt", "w");
    tools_script_set_dir("out/");
    errors = tools_script_run_file("../scripts/bench20k.txt", out);
    tools_script_set_dir("");
    if (out) fclose(out);
    test_log("bench20k: %d script errors (rejected placements are expected), pop %d, jobs %d, lots %d, money %d, %d-%02d",
             errors, sim_stats()->population, sim_stats()->jobs, g_world.lot_count, g_budget.money,
             sim_date()->year, sim_date()->month + 1);
    out = fopen("out/bench20k_map.txt", "w");
    if (out) { world_dump(out, 0, 0, g_world.w - 1, g_world.h - 1); fclose(out); }
    memset(&m, 0, sizeof(m));
    m.cam_x = 60; m.cam_y = 60; m.zoom = 0;
    CHECK(save_game("../BENCH20K.KAU", &m));
    test_log("bench20k: saved ../BENCH20K.KAU, world %08x sim %08x", world_hash(), sim_hash());
    CHECK(sim_stats()->population > 15000);
}

/* run a script file with our own month loop; full = force a whole-map field terrain recalc every step */
static u32 run_script_hash(const char *path, int full, int *pop)
{
    static char buf[64 * 1024];
    FILE *f = fopen(path, "rb");
    char *p, *line;
    int n, wait;
    if (!f) return 0;
    n = (int)fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = 0;
    perf_fresh(7);
    tools_script_set_dir("out/");
    for (p = buf; *p; ) {
        line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
        if (strncmp(line, "statlog", 7) == 0 || strncmp(line, "dump", 4) == 0) continue;
        if (tools_script_line(line, &wait, NULL) == SCR_WAIT) {
            u32 target = sim_date()->total_months + (u32)wait;
            while (sim_date()->total_months < target) {
                if (full) fields_mark_dirty(WCHG_TERRAIN);
                sim_step();
            }
        }
    }
    tools_script_set_dir("");
    *pop = sim_stats()->population;
    return sim_hash();
}

/* T13: batch rect lists + incremental field terrain recalc give the same city as whole-map recalcs */
TEST(perf_incremental_fields_match_full)
{
    int pa, pb;
    u32 a = run_script_hash("../scripts/town1.txt", 0, &pa);
    u32 b = run_script_hash("../scripts/town1.txt", 1, &pb);
    test_log("town1: incremental %08x (pop %d), full recalc %08x (pop %d)", a, pa, b, pb);
    if (!a) { test_log("scripts/town1.txt missing"); return; }
    CHECK_EQ(a, b);
}

/* KAUP_SAVETIME=1: per-module save cost of the bench town (Mac ms; the P2 is ~100-300x slower) */
TEST(perf_save_timing)
{
    SaveMeta m;
    SaveFile *sf;
    double t0;
    int i;
    if (!getenv("KAUP_SAVETIME")) return;
    perf_fresh(7);
    CHECK(load_game("../BENCH20K.KAU", &m));
    t0 = time_ms();
    for (i = 0; i < 20; i++) { sf = save_mem_open(); save_game_to(sf, &m); save_mem_close(sf); }
    test_log("save_game_to: %.3f ms", (time_ms() - t0) / 20);
    sf = save_mem_open(); save_game_to(sf, &m); test_log("save size %d bytes (BENCH20K.KAU on disk was written by the same code at creation)", save_mem_size(sf)); save_mem_close(sf);
#define TIME1(call) do { sf = save_mem_open(); t0 = time_ms(); for (i = 0; i < 20; i++) { call; } test_log("%-16s %.3f ms", #call, (time_ms() - t0) / 20); save_mem_close(sf); } while (0)
    TIME1(world_save(sf)); TIME1(sim_save(sf)); TIME1(sim_save_ext(sf)); TIME1(budget_save(sf));
    TIME1(fields_save(sf)); TIME1(net_save(sf)); TIME1(traffic_save(sf));
}
