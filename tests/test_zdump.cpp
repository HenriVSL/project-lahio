/* test_zdump.cpp - debug helper (orchestrator): load a save and print the sim report, demand terms and top problems.
   Only runs with KAUP_DUMPSAVE=path:  KAUP_DUMPSAVE=AUTO1.KAU make -f tests/Makefile.mac T=dump_save */
#include "test.h"
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
    n = sim_top_problems(pr, 8);
    for (i = 0; i < n; i++)
        printf("problem %d: req %d, %d lots, %d residents (near %d,%d)\n", i + 1, pr[i].req, (int)pr[i].count,
               (int)pr[i].residents, pr[i].x, pr[i].y);
}
