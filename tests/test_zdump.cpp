/* test_zdump.cpp - debug helper (orchestrator): load a save and print the sim report, demand terms and top problems.
   Only runs with KAUP_DUMPSAVE=path:  KAUP_DUMPSAVE=AUTO1.KAU make -f tests/Makefile.mac T=dump_save */
#include "test.h"
#include <string.h>
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "save.h"

TEST(dump_save)
{
    const char *path = getenv("KAUP_DUMPSAVE");
    SaveMeta meta;
    SimProblem pr[8];
    int i, n;
    if (!path) return;
    if (!g_world.w) world_init();
    sim_init();
    if (!load_game(path, &meta)) { printf("load failed: %s\n", save_last_error()); CHECK(0); return; }
    sim_dump_report(stdout);
    {   /* per-def lot counts (orchestrator debug) */
        int cnt[256], d; memset(cnt, 0, sizeof(cnt));
        for (i = 1; i < g_world.lot_hi; i++) if (g_world.lots[i].def != B_NONE) cnt[g_world.lots[i].def]++;
        for (d = 0; d < B_COUNT; d++) if (cnt[d] && g_bdefs[d].cat == BCAT_POWER) printf("power lots: %s x %d\n", g_bdefs[d].key, cnt[d]);
    }
    n = sim_top_problems(pr, 8);
    for (i = 0; i < n; i++)
        printf("problem %d: req %d, %d lots, %d residents (near %d,%d)\n", i + 1, pr[i].req, (int)pr[i].count,
               (int)pr[i].residents, pr[i].x, pr[i].y);
}
