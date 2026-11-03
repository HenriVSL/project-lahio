/* test_p1.cpp - P1 pre-play-test integration checks: the heat overlay on a loaded 1990 lahio town, the apartment
   upgrade rule, the one-card-a-year gate. Owner: P1. Needs tests/out/P1_DENSE.KAU (balance_era_1955_1990). */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "save.h"
#include "civic.h"

TEST(p1_heat_overlay_values_after_load)
{
    int x, y, cnt[OVR_COUNT + 1], plant = 0, i;
    memset(cnt, 0, sizeof(cnt));
    if (!load_game("out/P1_DENSE.KAU", NULL)) { test_log("out/P1_DENSE.KAU missing (run T=balance_era first)"); CHECK(1); return; }
    for (i = 1; i < g_world.lot_hi; i++) if (g_world.lots[i].def == B_HEAT_PLANT) plant = i;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            u8 v = sim_overlay_value(OVL_HEAT, 0, x, y);
            if (v == OVV_NONE) cnt[OVR_COUNT]++;
            else cnt[OVV_RAMP(v)]++;
        }
    test_log("heat overlay after load: plant lot %d, tiles by ramp: net %d, none %d", plant, cnt[OVR_NET], cnt[OVR_COUNT]);
    CHECK(plant > 0);
    CHECK(cnt[OVR_NET] > 0);
}
