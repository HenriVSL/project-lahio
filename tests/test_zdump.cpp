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
    if (getenv("KAUP_DUMPMAP")) {   /* coarse map (4x4 tiles per char) for aiming screenshots: dominant use per cell */
        int cx, cy, x, y;
        printf("map (cell = 4x4 tiles; col = x/4, row = y/4): A apts  H houses  C shops  I industry  P power  S other  = rail  ~ water  . land\n");
        for (cy = 0; cy < g_world.h / 4; cy++) {
            char line[80];
            for (cx = 0; cx < g_world.w / 4 && cx < 79; cx++) {
                int k[8] = {0,0,0,0,0,0,0,0}, best = 7, bv = 0, j;
                for (y = cy * 4; y < cy * 4 + 4; y++) for (x = cx * 4; x < cx * 4 + 4; x++) {
                    const Tile *t = WT(x, y);
                    if (t->lot) {
                        const BuildingDef *d = &g_bdefs[g_world.lots[t->lot].def];
                        if (d->cat == BCAT_ZONE) k[d->zone == ZONE_APARTMENTS ? 0 : d->zone == ZONE_HOUSES ? 1 :
                                                   d->zone == ZONE_COMMERCIAL ? 2 : 3] += 2;
                        else k[d->cat == BCAT_POWER ? 4 : 5] += 2;
                    } else if (TILE_HAS_RAIL(t)) k[6] += 3;
                    else if (TER_IS_WATER(TER_TYPE(t))) k[7]++;
                    else if (TER_TYPE(t) == TER_FOREST) k[5]++;   /* forest shows as S-like "f" below */
                }
                for (j = 0; j < 8; j++) if (k[j] > bv) { bv = k[j]; best = j; }
                line[cx] = bv < 3 ? '.' : "AHCIPf=~"[best];
            }
            line[cx] = 0;
            printf("%3d %s\n", cy * 4, line);
        }
        for (x = 0; x < g_world.w; x += 8) for (y = 0; y < g_world.h; y++)
            if (TILE_ROAD(WT(x, y)) == ROAD_RAIL) printf("rail x %d: y %d (lot %d)\n", x, y, WT(x, y)->lot);
        for (i = 1; i < g_world.lot_hi; i++)
            if (g_world.lots[i].def == B_TOWN_HALL || g_world.lots[i].def == B_STATION)
                printf("lot %s at %d,%d\n", g_bdefs[g_world.lots[i].def].key, g_world.lots[i].x, g_world.lots[i].y);
    }
    n = sim_top_problems(pr, 8);
    for (i = 0; i < n; i++)
        printf("problem %d: req %d, %d lots, %d residents (near %d,%d)\n", i + 1, pr[i].req, (int)pr[i].count,
               (int)pr[i].residents, pr[i].x, pr[i].y);
}
