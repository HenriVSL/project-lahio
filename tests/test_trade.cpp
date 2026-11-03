/* test_trade.cpp - M7: town status by decree, era events (1963 new markka, 1973 oil shock, 1990 end of clearing),
   the newspaper (headline generation strings), graphs history, clearing agreements and balance math, Western
   trade, loans, landmarks, determinism + save round-trip. Owner: M7. Dumps: tests/out/m7_*.txt.
   The town is scripts/town1.txt (seed 7 Lakeland). Run alone: make -f tests/Makefile.mac T=trade */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "budget.h"
#include "tools.h"
#include "traffic.h"
#include "mapgen.h"
#include "save.h"
#include "politics.h"
#include "industry.h"
#include "text.h"
#include "trade.h"
#include "era.h"

static void drain(void)
{
    SimEvent e;
    PolMsg m;
    EraMsg em;
    while (sim_poll_event(&e)) {}
    while (politics_poll_msg(&m)) {}
    while (era_poll_msg(&em)) {}
}

static int s_auto_rights = 1;     /* sign town rights like a player (0: test the gating) */
static void months(int n)
{
    while (n-- > 0) {
        sim_run_months(1); tools_month_changed(); drain();
        if (s_auto_rights && politics_decree_state(DEC_TOWN_RIGHTS) == DS_OK) politics_sign(DEC_TOWN_RIGHTS);
    }
}

static int scr(const char *line) { int w; return tools_script_line(line, &w, NULL); }

/* town1.txt (5 years of building) on a city that starts in start_year; politics on/off */
static int town1(int politics, int start_year, int script_years)
{
    MapGenParams mp;
    SimSettings s;
    static int inited;
    FILE *f;
    char line[256];
    int wait, waited = 0, errors = 0, r;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    mapgen_defaults(&mp);
    mp.seed = 7;
    if (!mapgen_generate(&mp)) return -1;
    sim_default_settings(&s);
    s.seed = 7;
    s.politics = politics;
    s.start_year = start_year;
    sim_new_city(&s);
    tools_reset();
    tools_script_reset();
    f = fopen("../scripts/town1.txt", "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        if (!strncmp(line, "statlog", 7) || !strncmp(line, "dump", 4) || !strncmp(line, "expect", 6)) continue;
        if (!strncmp(line, "wait", 4) && waited >= script_years * 12) break;
        r = tools_script_line(line, &wait, NULL);
        if (r == SCR_WAIT) { months(wait); waited += wait; }
        else if (r == SCR_ERROR) errors++;
    }
    fclose(f);
    drain();
    return errors;
}

static SaveFile *snap(void) { SaveFile *sf = save_mem_open(); save_game_to(sf, NULL); return sf; }
static int restore(SaveFile *sf)
{
    int ok;
    save_mem_reopen(sf);
    ok = load_game_from(sf, NULL);
    if (!ok) test_log("restore failed: %s", save_last_error());
    tools_reset();
    drain();
    return ok;
}

/* ------------------------------------------------------------------ save round-trip + determinism */
TEST(trade_save_roundtrip_determinism)
{
    SaveFile *sf;
    u32 h1, h2, e1;
    CHECK_EQ(town1(1, 1955, 3), 0);
    scr("trade consumer on");
    scr("trade loan bond");
    months(2);
    sf = snap();
    e1 = era_hash();
    months(14);
    h1 = sim_hash();
    CHECK(restore(sf));
    CHECK_EQ(era_hash(), e1);
    months(14);
    h2 = sim_hash();
    test_log("politics on: unbroken %08x, save+load %08x, era %08x", h1, h2, e1);
    CHECK_EQ(h1, h2);
    save_mem_close(sf);
}
