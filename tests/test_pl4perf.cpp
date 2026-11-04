/* test_pl4perf.cpp - PL4 CPU-spike harness (Mac, headless). Owner: PL4. Only with KAUP_PL4PERF=1.

   For BENCH20K.KAU and PT2_1992.KAU: load, run KAUP_PL4MONTHS (default 24) months step by step with
   scripted edits every 37 steps (roads, pipes, zoning, bulldozing, power lines at deterministic
   pseudo-random places: forces water/heat/traffic graph rebuilds), and write
     out/pl4perf_<save>.txt   per-month world/sim hash (compare before/after a perf change: must be identical)
                              + per-phase avg/max Mac ms + the 12 slowest steps (from step 64: after the
                              load's whole-map recalcs).
   KAUP_PL4NOEDIT=1: no edits (the VM bench situation).
   KAUP_PL4RT=1: also a save/load round trip (in memory) every 97 steps at phases 0, 1, 10 or 15.
   Mac ms x ~30 = P2-233 ms (calibrated against VM bench.txt phase numbers). */
#include "test.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "traffic.h"
#include "tools.h"
#include "save.h"
#include "util.h"
#include <stdlib.h>

static u32 s_lcg;
static int rnd(int n) { s_lcg = s_lcg * 1103515245u + 12345u; return (int)((s_lcg >> 8) % (u32)n); }

static void pl4_edit(int k, FILE *out)
{
    char line[128];
    int w = g_world.w, h = g_world.h, x = 2 + rnd(w - 12), y = 2 + rnd(h - 12), wait, l = 3 + rnd(8);
    switch (k % 6) {
    case 0: str_printf(line, sizeof(line), "road %d %d %d %d street straight", x, y, x + l, y); break;
    case 1: str_printf(line, sizeof(line), "pipe %d %d %d %d", x, y, x, y + l); break;
    case 2: str_printf(line, sizeof(line), "zone %d %d %d %d %s", x, y, x + 3, y + 3, (k / 6) % 2 ? "houses" : "industry"); break;
    case 3: str_printf(line, sizeof(line), "bulldoze %d %d %d %d", x, y, x + 1, y + 1); break;
    case 4: str_printf(line, sizeof(line), "road %d %d %d %d main straight", x, y, x, y + l); break;
    default: str_printf(line, sizeof(line), "power %d %d %d %d", x, y, x + l, y); break;
    }
    tools_script_line(line, &wait, out);
}

static void pl4_run(const char *save, const char *tag)
{
    static int inited;
    SaveMeta m;
    char path[128];
    FILE *f, *log;
    int months = getenv("KAUP_PL4MONTHS") ? atoi(getenv("KAUP_PL4MONTHS")) : 24, step = 0, i, k = 0;
    u32 m0;
    double worst[12];
    int wph[12], wstep[12];
    if (!inited) { if (!g_world.w) world_init(); budget_init(); net_init(); fields_init(); traffic_init(); sim_init(); tools_init(); inited = 1; }
    if (!load_game(save, &m)) { test_log("pl4perf: cannot load %s", save); return; }
    tools_reset();
    s_lcg = 12345;
    g_budget.money = 90000000;
    for (i = 0; i < 12; i++) { worst[i] = 0; wph[i] = wstep[i] = 0; }
    str_printf(path, sizeof(path), "out/pl4perf_%s.txt", tag);
    f = fopen(path, "w");
    str_printf(path, sizeof(path), "out/pl4perf_%s_script.txt", tag);
    log = fopen(path, "w");
    if (!f) return;
    sim_phase_timing_total_reset();
    m0 = sim_date()->total_months;
    while (sim_date()->total_months < m0 + (u32)months) {
        int ph = sim_date()->step;
        double t0, dt;
        u32 mon = sim_date()->total_months;
        if (step % 37 == 36 && !getenv("KAUP_PL4NOEDIT")) pl4_edit(k++, log);
        if (getenv("KAUP_PL4RT") && step % 97 == 96 && (ph == 10 || ph == 1 || ph == 15 || ph == 0)) {   /* save round trip at stage boundaries */
            SaveFile *sf = save_mem_open();
            SaveMeta mm;
            save_game_to(sf, &m);
            save_mem_reopen(sf);
            if (!load_game_from(sf, &mm)) test_log("pl4perf: round trip load failed");
            save_mem_close(sf);
            g_budget.money = g_budget.money;   /* (money is saved: unchanged) */
        }
        t0 = time_ms();
        sim_step();
        dt = time_ms() - t0;
        for (i = 0; i < 12; i++) if (dt > worst[i]) {
            int j;
            for (j = 11; j > i; j--) { worst[j] = worst[j - 1]; wph[j] = wph[j - 1]; wstep[j] = wstep[j - 1]; }
            worst[i] = dt; wph[i] = ph; wstep[i] = step;
            break;
        }
        step++;
        if (step == 64) {                                  /* timings from month 2 on (month 1 = load recalcs) */
            sim_phase_timing_total_reset();
            for (i = 0; i < 12; i++) { worst[i] = 0; wph[i] = wstep[i] = 0; }
        }
        if (sim_date()->total_months != mon)
            fprintf(f, "month %u pop %d money %d world %08x sim %08x\n", sim_date()->total_months - m0, sim_stats()->population,
                    g_budget.money, world_hash(), sim_hash());
    }
    fprintf(f, "TIMING (Mac ms) phase n avg max\n");
    for (i = 0; i < SIM_STEPS_PER_CYCLE; i++) {
        const PhaseTiming *p = sim_phase_timing_total(i);
        fprintf(f, "  phase %2d %5u %8.3f %8.3f\n", i, p->count, p->avg_ms, p->max_ms);
    }
    fprintf(f, "slowest steps:\n");
    for (i = 0; i < 12; i++) fprintf(f, "  step %5d phase %2d %8.3f ms\n", wstep[i], wph[i], worst[i]);
    fclose(f);
    if (log) fclose(log);
    test_log("pl4perf %s: %d steps, %d edits, final world %08x sim %08x", tag, step, k, world_hash(), sim_hash());
}

TEST(pl4perf_spikes)
{
    if (!getenv("KAUP_PL4PERF")) return;
    pl4_run("../BENCH20K.KAU", "bench20k");
    pl4_run("../PT2_1992.KAU", "pt2");
}
