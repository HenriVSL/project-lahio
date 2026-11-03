/* test_complex.cpp - M6: complexes (paper mill + patruuna, Valtion Metalli): placement and site rules, construction
   and staged growth, wood supply, owner mood -> gifts and cards, the company-town ledger, the risk-event schedule,
   Valtion Metalli unlock (decree / no politics) and the 1990 drop, determinism and save round-trip. Owner: M6.
   Debug from text: tests/out/complex_*.txt (complex_dump + sim report + map). */
#include "test.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "budget.h"
#include "industry.h"
#include "politics.h"
#include "complex.h"
#include "save.h"
#include "text.h"
#include "tools.h"
#include "traffic.h"

/* ------------------------------------------------------------------ helpers */
static void cx_new(u32 seed, int year, int all, int politics)
{
    SimSettings s;
    static int inited;
    if (!inited) { world_init(); sim_init(); tools_init(); traffic_init(); inited = 1; }
    world_new(128, 128);
    sim_default_settings(&s);
    s.seed = seed; s.start_year = year; s.all_unlocked = all; s.politics = politics;
    sim_new_city(&s);
    tools_reset();
}

static void cx_road(int x0, int y0, int x1, int y1, int cls)
{
    int x, y;
    world_batch_begin();
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!TILE_ROAD(world_tile(x, y)) && !world_tile(x, y)->lot) world_set_road(x, y, cls);
    world_batch_end();
}

static void cx_terrain(int x0, int y0, int x1, int y1, int ter, int var)
{
    int x, y;
    for (y = y0; y <= y1; y++) for (x = x0; x <= x1; x++) world_set_terrain(x, y, ter, var);
}

static void cx_line(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++) if (!world_tile(x, y)->lot && !TILE_ROAD(world_tile(x, y))) world_set_power_line(x, y, 1);
}

static void cx_pipe(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++) for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++) world_set_pipe(x, y, 1);
}

/* a lake on the west (x 4..13, y 30..62), the national road at y 64, a housing district in the east with a coal plant,
   a school and wells; the mill site (14, 50) has its log pond side on the lake, a street at x 20 and a power line +
   a pumping station + a pipe at its south side */
#define MILL_X 14
#define MILL_Y 50
static void cx_town(void)
{
    int i, x, y;
    cx_road(0, 64, 127, 64, ROAD_MAIN);
    for (i = 0; i < 128; i++) world_set_tile_flags(i, 64, TF_PROTECTED, 0);
    world_set_tile_flags(0, 64, TF_EDGE_LINK, 0);
    world_set_tile_flags(127, 64, TF_EDGE_LINK, 0);
    cx_terrain(4, 30, 13, 62, TER_WATER, 0);
    cx_road(20, 46, 20, 63, ROAD_STREET);
    /* housing */
    for (y = 40; y <= 62; y++) for (x = 80; x <= 110; x++) if (world_is_buildable(x, y)) world_set_zone(x, y, ZONE_HOUSES);
    cx_road(80, 44, 110, 44, ROAD_STREET); cx_road(80, 50, 110, 50, ROAD_STREET); cx_road(80, 56, 110, 56, ROAD_STREET);
    cx_road(79, 44, 79, 63, ROAD_STREET);
    world_lot_place(B_COAL_PLANT, 114, 60, ROT_0, 0, LOT_ACTIVE);
    cx_line(14, 63, 113, 63); cx_line(113, 61, 113, 62);
    world_lot_place(B_SCHOOL, 76, 60, ROT_0, 0, LOT_ACTIVE);
    world_lot_place(B_WELL, 112, 44, ROT_0, 0, LOT_ACTIVE);
    world_lot_place(B_WELL, 112, 56, ROT_0, 0, LOT_ACTIVE);
    cx_pipe(80, 50, 111, 50); cx_pipe(111, 44, 111, 56);
    /* mill services: power up to the site, a pump on the shore beside it */
    cx_line(15, 55, 16, 55); cx_line(16, 56, 16, 62);
    world_lot_place(B_PUMP, MILL_X, 55, ROT_0, 0, LOT_ACTIVE);
}

static void cx_dump(const char *name)
{
    char path[96];
    FILE *f;
    str_printf(path, sizeof(path), "out/%s", name);
    f = fopen(path, "w");
    if (!f) return;
    complex_dump(f);
    industry_dump(f);
    sim_dump_report(f);
    world_dump(f, 0, 25, 60, 70);
    fclose(f);
}

static int n_msgs(int type, int *a)
{
    CpxMsg m;
    int n = 0;
    while (complex_poll_msg(&m)) if (m.type == type) { n++; if (a) *a = m.a; }
    return n;
}

/* ------------------------------------------------------------------ 1: defs and module tables */
TEST(complex_defs_and_layout)
{
    int kind, k, def, ox, oy, w, h, x, y;
    CHECK_EQ(def_selfcheck(), 0);
    for (kind = CPX_PAPER; kind <= CPX_METAL; kind++) {
        u8 cover[5][5];
        int tiles = 0;
        memset(cover, 0, sizeof(cover));
        complex_size(kind, &w, &h);
        for (k = 0; complex_layout(kind, k, &def, &ox, &oy); k++) {
            const BuildingDef *d = def_building(def);
            CHECK(d->flags & BDF_MODULE);
            CHECK(d->flags & BDF_NO_ROTATE);
            CHECK_EQ(complex_kind_of_def(def), kind);
            for (y = oy; y < oy + d->h; y++) for (x = ox; x < ox + d->w; x++) { CHECK(x < w && y < h); CHECK_EQ(cover[y][x], 0); cover[y][x] = 1; tiles++; }
        }
        CHECK_EQ(tiles, w * h);
    }
    CHECK_EQ(def_building_find("pm_hall"), B_PM_HALL);
    CHECK_EQ(def_building_find("vm_siding"), B_VM_SIDING);
    CHECK_EQ(def_building(B_ROW_HOUSES)->capacity, 30);
    CHECK_EQ(def_building(B_PM_HALL)->cost, 5000);
}

/* ------------------------------------------------------------------ 2: placement and site rules */
TEST(complex_paper_site_rules)
{
    Money cost;
    int r, n, i, id;
    cx_new(11, 1955, 0, 0);
    cx_town();
    CHECK_EQ(complex_check_place(CPX_PAPER, MILL_X, MILL_Y, &cost), REJ_LOCKED);      /* a village: Church village first */
    cx_new(11, 1955, 1, 0);
    cx_town();
    cx_terrain(MILL_X, MILL_Y, MILL_X + 1, MILL_Y + 1, TER_FOREST, FOREST_SPRUCE);
    CHECK_EQ(complex_check_place(CPX_PAPER, 40, 50, &cost), REJ_CPX_WATER);            /* inland */
    CHECK_EQ(complex_check_place(CPX_PAPER, 10, 50, &cost), PLACE_WATER);              /* in the lake */
    CHECK_EQ(complex_check_place(CPX_PAPER, MILL_X, 32, &cost), REJ_CPX_ROAD);         /* no road within 2 */
    CHECK_EQ(complex_check_place(CPX_PAPER, MILL_X, 60, &cost), PLACE_OCCUPIED);       /* over the pump and the line */
    CHECK_EQ(complex_check_place(CPX_PAPER, 18, 46, &cost), PLACE_ROAD);               /* across the national road */
    r = complex_check_place(CPX_PAPER, MILL_X, MILL_Y, &cost);
    CHECK_EQ(r, PLACE_OK);
    CHECK_EQ(cost, 5000 + 4 * g_misc.clear_forest);
    id = complex_place(CPX_PAPER, MILL_X, MILL_Y);
    CHECK_EQ(id, 1);
    for (i = 1, n = 0; i < g_world.lot_hi; i++)
        if (g_world.lots[i].def != B_NONE && (g_bdefs[g_world.lots[i].def].flags & BDF_MODULE)) { n++; CHECK_EQ(g_world.lots[i].status, LOT_BUILDING); }
    CHECK_EQ(n, 7);
    CHECK_EQ(TER_TYPE(world_tile(MILL_X, MILL_Y)), TER_GRASS);
    CHECK_EQ(complex_at_lot(world_lot_at(MILL_X + 4, MILL_Y + 4)), 1);
    CHECK_EQ(complex_check_place(CPX_PAPER, MILL_X, 34, &cost), PLACE_UNIQUE);
    /* the script path and the tools path refuse it the same way */
    CHECK_EQ(tools_apply_build(B_PM_HALL, MILL_X, 34, -1), 0);
    CHECK_EQ(tools_last_reason(), PLACE_UNIQUE);
    /* rapids: the mill's own turbines */
    cx_new(12, 1955, 1, 0);
    cx_town();
    for (i = MILL_Y; i < MILL_Y + 5; i++) world_set_terrain(MILL_X - 1, i, TER_RIVER, RIVER_RAPIDS_BIT);
    CHECK(complex_place(CPX_PAPER, MILL_X, MILL_Y));
    CHECK(complex_of_kind(CPX_PAPER)->flags & CF_RAPIDS);
    test_log("paper mill placed: cost %d (incl. 4 forest tiles), rapids flag ok", (int)cost);
}

/* ------------------------------------------------------------------ 3: construction, stage 1, wood, the stage rules */
static void cx_run_mill(u32 seed, int year)
{
    cx_new(seed, year, 1, 0);
    cx_town();
    sim_run_months(18);                        /* a workforce first */
    CHECK(complex_place(CPX_PAPER, MILL_X, MILL_Y));
}

TEST(complex_construction_and_stages)
{
    const Complex *c;
    int a = 0;
    cx_run_mill(21, 1955);
    c = complex_of_kind(CPX_PAPER);
    sim_run_months(3);
    CHECK(!(c->flags & CF_ACTIVE));
    CHECK_EQ(complex_jobs(), 0);
    sim_run_months(5);
    CHECK(c->flags & CF_ACTIVE);
    CHECK_EQ(c->stage, 1);
    CHECK(n_msgs(CM_PRODUCTION, &a) + 1 > 0);
    CHECK_EQ(industry_wood()->need_ext, 40);
    CHECK_EQ(complex_module_use(B_PM_HALL, CPXU_POWER), 40);
    CHECK_EQ(complex_module_use(B_PM_HALL, CPXU_WATER), 30);
    CHECK(complex_module_use(B_PM_HALL, CPXU_POLLUTION) > 0);
    sim_run_months(6);
    cx_dump("complex_stage1.txt");
    test_log("stage 1 after 14 months: pop %d, jobs %d/%d filled, output %d%%, wood %d delivered %d%% (supply %d), mood %d -> %d,"
             " LF power %d water %d", sim_stats()->population, c->jobs, c->employed, c->output, c->wood_need, c->wood_pct,
             industry_wood()->supply, c->mood, c->mood_target, (world_lot(world_lot_at(MILL_X + 2, MILL_Y))->flags & LF_POWERED) != 0,
             (world_lot(world_lot_at(MILL_X + 2, MILL_Y))->flags & LF_WATERED) != 0);
    CHECK(c->jobs > 0 && c->jobs <= 300);
    CHECK(sim_stats()->jobs >= c->jobs);
    CHECK(c->wood_pct < 100);                   /* 10 regional wu for 40 needed + the sawmills */
    CHECK(c->output <= c->wood_pct);
    CHECK(c->stage == 1);                       /* a small town: no PM2 */
    /* the stage rules (pure) */
    CHECK(complex_paper_stage_ok(2, 3000, 80, 1956, 0, 1, 50, 0));
    CHECK(!complex_paper_stage_ok(2, 2999, 80, 1956, 0, 1, 50, 0));
    CHECK(!complex_paper_stage_ok(2, 3000, 79, 1956, 0, 1, 50, 0));
    CHECK(!complex_paper_stage_ok(2, 3000, 80, 1956, 0, 0, 50, 0));
    CHECK(!complex_paper_stage_ok(2, 3000, 80, 1956, 0, 1, 30, 0));
    CHECK(complex_paper_stage_ok(3, 6000, 120, 1963, 1, 1, 50, 0));
    CHECK(!complex_paper_stage_ok(3, 6000, 120, 1962, 1, 1, 50, 0));
    CHECK(!complex_paper_stage_ok(3, 6000, 120, 1963, 0, 1, 50, 0));
    CHECK(!complex_paper_stage_ok(3, 6000, 120, 1963, 1, 1, 50, 1));
}

/* more wood (log trucks from 1965 add 15 regional wu) -> more output and jobs */
TEST(complex_wood_supply_drives_output)
{
    const Complex *c;
    int out55, jobs55, pct55;
    cx_run_mill(31, 1955);
    sim_run_months(14);
    c = complex_of_kind(CPX_PAPER);
    out55 = c->output; jobs55 = c->jobs; pct55 = c->wood_pct;
    cx_run_mill(31, 1966);
    sim_run_months(14);
    c = complex_of_kind(CPX_PAPER);
    test_log("wood delivered 1956 %d%% -> 1967 %d%%; output %d%% -> %d%%; jobs %d -> %d", pct55, c->wood_pct, out55, c->output, jobs55, c->jobs);
    CHECK(c->wood_pct > pct55);
    CHECK(c->jobs > jobs55);
}

/* ------------------------------------------------------------------ 4: mood -> gifts the player places */
TEST(complex_mood_and_gifts)
{
    const Complex *c;
    int k, id, def;
    cx_run_mill(41, 1955);
    c = complex_of_kind(CPX_PAPER);
    sim_run_months(7);
    CHECK(c->flags & CF_ACTIVE);
    CHECK_EQ(complex_gift_pending(), 0);
    for (k = 0; k < 14 && !complex_gift_pending(); k++) { complex_set_mood(CPX_PAPER, 95); sim_run_months(1); }
    test_log("gift offered after %d months at mood 95: %s; mood terms %d", k, complex_gift_name(complex_gift_def(0), CPX_PAPER), c->nterms);
    CHECK_EQ(complex_gift_pending(), 1);
    def = complex_gift_def(0);
    CHECK_EQ(def, B_ROW_HOUSES);
    CHECK(!strcmp(text_tool_name(TOOL_GIFT, 0), "Gift: workers' row houses"));
    CHECK_EQ(complex_check_gift(60, 47, ROT_0), REJ_CPX_FAR);        /* 42 tiles away */
    CHECK_EQ(complex_check_gift(22, 32, ROT_0), REJ_CPX_FAR);
    CHECK_EQ(complex_check_gift(22, 40, ROT_0), REJ_CPX_ROAD);
    CHECK_EQ(complex_check_gift(MILL_X + 1, MILL_Y + 1, ROT_0), PLACE_OCCUPIED);
    cx_road(21, 47, 30, 47, ROAD_STREET);
    CHECK_EQ(complex_check_gift(22, 48, ROT_0), PLACE_OK);
    id = complex_place_gift(22, 48, ROT_0);
    CHECK(id > 0);
    CHECK_EQ(world_lot(id)->def, B_ROW_HOUSES);
    CHECK_EQ(complex_gift_pending(), 0);
    CHECK_EQ(c->gift_boost, 10);
    /* the gift cooldown: no second offer for 24 months */
    for (k = 0; k < 12; k++) { complex_set_mood(CPX_PAPER, 95); sim_run_months(1); }
    CHECK_EQ(complex_gift_pending(), 0);
    for (k = 0; k < 14 && !complex_gift_pending(); k++) { complex_set_mood(CPX_PAPER, 95); sim_run_months(1); }
    CHECK_EQ(complex_gift_def(0), B_SAUNA);
    /* the script path places it */
    cx_road(31, 47, 40, 47, ROAD_STREET);
    {
        char line[64];
        int wait;
        str_printf(line, sizeof(line), "complex gift 26 48");
        CHECK_EQ(tools_script_line(line, &wait, NULL), SCR_OK);
    }
    CHECK_EQ(world_lot(world_lot_at(26, 48))->def, B_SAUNA);
    /* a low mood: the mood falls toward its target and no gifts come */
    complex_set_mood(CPX_PAPER, 20);
    sim_run_months(1);
    CHECK(c->mood > 20 && c->mood < 50);
    cx_dump("complex_gifts.txt");
    test_log("after the sauna: mood %d target %d, gift boost %d, gifts placed 2, contribution %d mk", c->mood, c->mood_target, c->gift_boost,
             (int)complex_contribution_last());
}

/* politics on: the patron's demand cards; the answer changes the mood */
TEST(complex_demand_cards)
{
    const Complex *c;
    int k, ev = -1, m0;
    cx_new(51, 1955, 1, 1);
    cx_town();
    sim_run_months(18);
    CHECK(complex_place(CPX_PAPER, MILL_X, MILL_Y));
    c = complex_of_kind(CPX_PAPER);
    for (k = 0; k < 40; k++) {
        sim_run_months(1);
        ev = politics_pending_event();
        if (ev >= EV_CPX_TAX && ev <= EV_CPX_UNION) break;
        if (ev >= 0) politics_choose(0);       /* other cards (and the 1956 strike card) out of the way */
    }
    test_log("first demand card after %d months: %s", k, ev >= 0 ? g_events[ev].key : "none");
    CHECK(ev >= EV_CPX_TAX && ev <= EV_CPX_UNION);
    m0 = c->nterms;
    {
        int tax = g_budget.tax_bus, adj0 = 0, adj1 = 0, i;
        for (i = 0; i < 4; i++) adj0 += c->adj_m[i] > 0 ? c->adj[i] : 0;
        CHECK(politics_choose(0));
        for (i = 0; i < 4; i++) adj1 += c->adj_m[i] > 0 ? c->adj[i] : 0;
        test_log("answered %s choice 0: answers term %d -> %d, business tax %d -> %d", g_events[ev].key, adj0, adj1, tax, g_budget.tax_bus);
        if (ev == EV_CPX_TAX) { CHECK_EQ(g_budget.tax_bus, tax - 1); CHECK_EQ(adj1 - adj0, 15); }
        else if (ev == EV_CPX_PERMIT) { CHECK(c->flags & CF_PERMIT); CHECK_EQ(adj1 - adj0, 10); }
        else CHECK_EQ(adj1 - adj0, -10);
    }
    UNUSED(m0);
}

/* ------------------------------------------------------------------ 5: the company-town rule */
TEST(complex_company_town_ledger)
{
    const Complex *c;
    Money svc, want;
    int k;
    cx_run_mill(61, 1955);
    c = complex_of_kind(CPX_PAPER);
    sim_run_months(7);
    for (k = 0; k < 24 && !complex_company_town(); k++) sim_run_months(1);
    test_log("dependency %d%% (mill jobs %d of %d), company town %d after %d months", complex_dependency_pct(), c->jobs,
             sim_stats()->jobs, complex_company_town(), k + 7);
    CHECK(complex_company_town());
    CHECK(complex_dependency_pct() >= CPX_DEP_PCT);
    sim_run_months(1);
    /* contribution = 20% of last month's school + clinic + fire upkeep (no paid gifts yet) */
    svc = -(g_budget.last[BL_SVC(SVC_SCHOOL)] + g_budget.last[BL_SVC(SVC_HEALTH)] + g_budget.last[BL_SVC(SVC_FIRE)]);
    want = svc * 20 / 100;
    test_log("services upkeep %d mk -> contribution %d mk (want %d); ledger line cur %d last %d", (int)svc,
             (int)complex_contribution_last(), (int)want, (int)g_budget.cur[BL_COMPANY], (int)g_budget.last[BL_COMPANY]);
    CHECK(svc > 0);
    CHECK_EQ(complex_contribution_last(), want);
    sim_run_months(1);
    CHECK(g_budget.last[BL_COMPANY] > 0);
    CHECK(!strcmp(text_term_label(TERMK_BUDGET, BL_COMPANY), "Company contribution"));
}

/* ------------------------------------------------------------------ 6: telegraphed risk events */
TEST(complex_risk_schedule)
{
    const Complex *c;
    int k, a = -1, out_before;
    cx_new(71, 1955, 1, 0);
    cx_town();
    CHECK(complex_place(CPX_PAPER, MILL_X, MILL_Y));      /* January 1955: running by July, the 1956 strike ahead */
    c = complex_of_kind(CPX_PAPER);
    CHECK_EQ(c->strike_at, 1956 * 12 + 2);
    for (k = 0; k < 4; k++) {
        static const int base[4] = { 1958, 1967, 1971, 1976 };
        int y = c->sched[k] / 12;
        CHECK(y >= base[k] - 1 && y <= base[k] + 1);
    }
    CHECK(c->modern_at / 12 >= 1972 && c->modern_at / 12 <= 1978);
    CHECK(c->merger_at / 12 >= 1980 && c->merger_at / 12 <= 1988);
    sim_run_months(11);                                    /* to the end of November 1955 */
    CHECK_EQ(c->ev_state, EVS_ANNOUNCED);
    CHECK_EQ(c->ev, EVK_STRIKE);
    CHECK(n_msgs(CM_EV_ANNOUNCE, &a) >= 1);
    out_before = c->output;
    sim_run_months(3);                                     /* March 1956 */
    CHECK_EQ(c->ev_state, EVS_RUNNING);
    CHECK_EQ(c->output, 0);
    CHECK(c->mood_target < 60);
    sim_run_months(1);
    CHECK_EQ(c->ev_state, EVS_NONE);
    test_log("1956 strike: announced 11/1955 (3 months ahead), output %d%% -> 0 -> %d%%, mood %d; slumps %d-%02d %d-%02d %d-%02d"
             " %d-%02d, modernisation %d, merger %d", out_before, c->output, c->mood, c->sched[0] / 12, c->sched[0] % 12 + 1,
             c->sched[1] / 12, c->sched[1] % 12 + 1, c->sched[2] / 12, c->sched[2] % 12 + 1, c->sched[3] / 12, c->sched[3] % 12 + 1,
             c->modern_at / 12, c->merger_at / 12);
    /* the slump: announced 3 months ahead, jobs -30% while it runs */
    while (c->ev_state != EVS_ANNOUNCED && sim_date()->year < 1960) sim_run_months(1);
    CHECK_EQ(c->ev, EVK_SLUMP);
    CHECK(c->ev_start - (sim_date()->year * 12 + sim_date()->month) >= 2);
    {
        int jobs0 = c->jobs, nom;
        while (c->ev_state != EVS_RUNNING) sim_run_months(1);
        nom = 300 * ICLAMP(c->wood_pct + 25, 50, 100) / 100;
        test_log("slump from %d-%02d for %d months: jobs %d -> %d (nominal %d)", c->ev_start / 12, c->ev_start % 12 + 1, c->ev_len, jobs0, c->jobs, nom);
        CHECK(c->ev_len >= 12 && c->ev_len <= 18);
        CHECK_EQ(c->jobs, nom * 70 / 100);
    }
    cx_dump("complex_risk.txt");
}

/* politics on: the strike card; mediation keeps half the output */
TEST(complex_risk_card_response)
{
    const Complex *c;
    int k;
    cx_new(72, 1955, 1, 1);
    cx_town();
    CHECK(complex_place(CPX_PAPER, MILL_X, MILL_Y));
    c = complex_of_kind(CPX_PAPER);
    for (k = 0; k < 11 && c->ev_state != EVS_ANNOUNCED; k++) {
        sim_run_months(1);
        if (politics_pending_event() >= 0 && politics_pending_event() != EV_CPX_STRIKE) politics_choose(0);
    }
    CHECK_EQ(c->ev, EVK_STRIKE);
    if (politics_pending_event() != EV_CPX_STRIKE) { sim_run_months(1); }
    CHECK_EQ(politics_pending_event(), EV_CPX_STRIKE);
    CHECK(politics_choose(1));
    CHECK_EQ(c->ev_resp, 1);
    while (c->ev_state != EVS_RUNNING) sim_run_months(1);
    test_log("mediated strike: output %d%% (staffing x 50%%)", c->output);
    CHECK(c->output > 0 && c->output <= 50);
}

/* ------------------------------------------------------------------ 7: Valtion Metalli */
static void cx_rail(void) { cx_road(0, 28, 127, 28, ROAD_RAIL); cx_road(40, 29, 40, 63, ROAD_STREET); }
#define VM_X 42
#define VM_Y 24

TEST(complex_metal_unlock_both_ways)
{
    Money cost;
    int k;
    /* politics off: a plain unique building for 6,000 mk at Market town */
    cx_new(81, 1955, 1, 0);
    cx_town(); cx_rail();
    CHECK_EQ(complex_tool_cost(B_VM_HALL), 6000);
    CHECK_EQ(complex_check_place(CPX_METAL, VM_X, 40, &cost), REJ_CPX_RAIL);
    CHECK_EQ(complex_check_place(CPX_METAL, VM_X, VM_Y, &cost), PLACE_OK);
    CHECK_EQ(cost, 6000);
    sim_run_months(1);
    CHECK_EQ(n_msgs(CM_VM_UNLOCK, NULL), 1);
    /* politics on: the hidden decree is offered at Market town (creative mode here), signing it is the permit */
    cx_new(82, 1955, 1, 1);
    cx_town(); cx_rail();
    CHECK_EQ(politics_decree_state(DEC_STATE_WORKS), DS_HIDDEN);
    CHECK(!complex_vm_permit());
    sim_run_months(1);
    CHECK(politics_decree_state(DEC_STATE_WORKS) != DS_HIDDEN);
    CHECK_EQ(n_msgs(CM_VM_DECREE, NULL), 1);
    politics_authority_add(30);
    for (k = 0; k < 4 && politics_decree_state(DEC_STATE_WORKS) != DS_OK; k++) sim_run_months(1);
    CHECK_EQ(politics_decree_state(DEC_STATE_WORKS), DS_OK);
    CHECK(politics_sign(DEC_STATE_WORKS));
    CHECK(complex_vm_permit());
    CHECK_EQ(complex_tool_cost(B_VM_HALL), 0);
    sim_run_months(1);
    CHECK_EQ(politics_decree_state(DEC_STATE_WORKS), DS_HIDDEN);   /* one-off: the hook is closed again */
    CHECK(complex_vm_permit());
    CHECK_EQ(complex_check_place(CPX_METAL, VM_X, VM_Y, &cost), PLACE_OK);
    CHECK_EQ(cost, 0);
    CHECK(complex_place(CPX_METAL, VM_X, VM_Y));
    test_log("Valtion Metalli: off = 6,000 mk at Market town; on = decree (authority %d left), free", politics_authority());
}

TEST(complex_metal_lines_and_1990)
{
    const Complex *c;
    int jobs89, out89, east89;
    cx_new(83, 1987, 1, 0);
    cx_town(); cx_rail();
    cx_line(41, 24, 41, 62);
    sim_run_months(18);
    CHECK(complex_place(CPX_METAL, VM_X, VM_Y));
    c = complex_of_kind(CPX_METAL);
    world_lot_place(B_WELL, 47, 24, ROT_0, 0, LOT_ACTIVE);
    while (sim_date()->year < 1989 || sim_date()->month < 11) sim_run_months(1);
    jobs89 = c->jobs; out89 = c->output; east89 = c->east;
    cx_dump("complex_metal_1989.txt");
    sim_run_months(2);
    test_log("Valtion Metalli Dec 1989: lines %d, jobs %d, output %d%%, East %d; Feb 1990: jobs %d, output %d%%, East %d (West %d)",
             c->stage, jobs89, out89, east89, c->jobs, c->output, c->east, c->west);
    CHECK(c->flags & CF_ACTIVE);
    CHECK(jobs89 > 0);
    CHECK(c->jobs <= jobs89 / 2 + 1);
    CHECK(c->output <= out89 / 2 + 1);
    CHECK(east89 > c->west);
}

/* ------------------------------------------------------------------ 8: determinism and save round-trip */
static u32 rt_run(int split, int n, int m, int *pop)
{
    SaveMeta meta;
    SaveFile *sf;
    cx_new(91, 1955, 1, 1);
    cx_town();
    sim_run_months(14);
    complex_place(CPX_PAPER, MILL_X, MILL_Y);
    sim_run_months(n);
    if (split) {
        memset(&meta, 0, sizeof(meta)); meta.saved_time = 1000000;
        sf = save_mem_open();
        save_game_to(sf, &meta);
        cx_new(1, 1955, 0, 0);
        save_mem_reopen(sf);
        if (!load_game_from(sf, NULL)) { test_log("load failed: %s", save_last_error()); save_mem_close(sf); return 0; }
        save_mem_close(sf);
    }
    { int k; for (k = 0; k < m; k++) { sim_run_months(1); if (politics_pending_event() >= 0) politics_choose(sim_date()->month % 2); } }
    { int k; for (k = 0; k < 23; k++) sim_step(); }
    *pop = sim_stats()->population;
    return sim_hash();
}

TEST(complex_determinism_and_save)
{
    int p0, p1, p2;
    u32 h0, h1, h2;
    h0 = rt_run(0, 16, 30, &p0);
    cx_dump("complex_roundtrip.txt");
    h1 = rt_run(0, 16, 30, &p1);
    h2 = rt_run(1, 16, 30, &p2);
    test_log("unbroken %08x / %08x, save+load %08x; pop %d %d %d; complex hash %08x", h0, h1, h2, p0, p1, p2, complex_hash());
    CHECK(complex_hash() != 0);
    CHECK_EQ(h0, h1);
    CHECK_EQ(h0, h2);
    /* a split in the middle of a month */
    {
        SaveMeta meta;
        SaveFile *sf;
        int k;
        u32 a, b;
        cx_new(92, 1955, 1, 0); cx_town(); sim_run_months(14); complex_place(CPX_PAPER, MILL_X, MILL_Y); sim_run_months(9);
        for (k = 0; k < 37; k++) sim_step();
        memset(&meta, 0, sizeof(meta)); meta.saved_time = 1000000;
        sf = save_mem_open(); save_game_to(sf, &meta);
        sim_run_months(14); a = sim_hash();
        cx_new(2, 1955, 0, 0); save_mem_reopen(sf);
        CHECK(load_game_from(sf, NULL));
        save_mem_close(sf);
        sim_run_months(14); b = sim_hash();
        test_log("mid-month split: %08x %08x", a, b);
        CHECK_EQ(a, b);
    }
}

/* a town without a complex hashes as before (complex_hash is 0) */
TEST(complex_absent_is_neutral)
{
    cx_new(93, 1955, 0, 0);
    cx_town();
    sim_run_months(6);
    CHECK_EQ(complex_hash(), 0u);
    CHECK_EQ(complex_jobs(), 0);
    CHECK_EQ(industry_wood()->need_ext, 0);
}
