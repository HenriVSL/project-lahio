/* sim.cpp - scheduler, growth/decline, demand, migration/emigration, stats, queries, events, stat log.
   PORTABLE, deterministic, integer-only. Owner: T04. DESIGN 5. See sim.h.

   Model summary (numbers in defs.cpp and the constants below; T12 tunes):
   - Demand (phase 15, every cycle) in people (R) / jobs (C, I), itemised:
       R = openJobs*100/LABOUR + pull(year)*(attractiveness, tax) - vacantHomes - unemployed
       C = residents*spend%(era) - commercialCapacity          (tax_bus scales spending)
       I = 60% of workers + clearing + western orders - industrialCapacity
     `smoothed` moves 1/4 to the target per month; the bar is 100*s/(|s|+K). Growth may build at most
     `allowance` = max(0, min(target, smoothed)) per cycle (capacity under construction counts, so the
     target drops as soon as lots are placed).
   - Growth scan (phases 1-8, one row band each, band order rotates per cycle): empty zoned tiles may
     start a tier-1 lot (largest lot that fits, facing the nearest road) when score > SIM_T_GROW and
     tier-1 requirements are met; active lots upgrade after SIM_G_UP good checks, decline after
     SIM_G_DOWN bad ones (lose occupants, then a tier, then abandoned; reversible).
   - Residents move into vacant homes from the migration flow (pull + open jobs) and the homeless pool;
     jobs are filled city-wide (employed = min(workers, jobs)); 1/8 of the unemployed and 1/4 of the
     homeless emigrate to Sweden each month. */
#include "common.h"
#include <stddef.h>
#include "sim.h"
#include "world.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "traffic.h"
#include "transit.h"
#include "politics.h"
#include "save.h"
#include "industry.h"
#include "complex.h"
#include "civic.h"
#include "era.h"           /* M7: eras, town status by decree, trade hooks */
#include "trade.h"

/* T12 tuning constants (local; the shared ones are in sim.h) */
#define PHONE_YEAR 1960                 /* M5: houses tier 3 need a telephone (post office) from this year */
#define SIM_DECLINE_DEMAND_FLOOR (-6)   /* most negative demand term an ACTIVE lot's score sees */

#define EV_RING 64
#define MAX_PROBLEMS 8

static SimSettings s_set;
static SimDate     s_date;
static Demand      s_dem[DEM_COUNT];
static CityStats   s_stats;
static PhaseTiming s_phase[SIM_STEPS_PER_CYCLE];
static double      s_phase_sum[SIM_STEPS_PER_CYCLE];
static PhaseTiming s_phase_tot[SIM_STEPS_PER_CYCLE];   /* T13: since sim_phase_timing_total_reset (bench) */
static double      s_phase_tot_sum[SIM_STEPS_PER_CYCLE];
static Rng         s_rng;
static int         s_speed;
static double      s_backlog;
static s32         s_movein;              /* residents that may move in this cycle */
static s32         s_movein_homeless;     /* part of s_movein that comes from the homeless pool */
static s32         s_homeless;
static s32         s_res_track;           /* residents as the sim last left them (detects bulldozing) */
static int         s_bands_done;          /* growth bands done this cycle */
static s32         s_band_allow[DEM_COUNT], s_band_movein;
static SimEvent    s_ev[EV_RING];
static int         s_ev_head, s_ev_count;
static s32         s_hist[HIST_COUNT][SIM_HISTORY];
static int         s_hist_n, s_hist_pos;
static FILE       *s_statlog;
static int         s_status_max;
static u8          s_unl_b[B_COUNT], s_unl_r[ROAD_CLASS_COUNT], s_unl_z[ZONE_COUNT];
static u8          s_first_built[ZONE_COUNT][TIER_MAX + 1];
static int         s_lot_events_month;
static int         s_emig_cursor;
static int         s_warn_power, s_warn_water, s_warn_svc[SVC_COUNT];
static s32         s_milestone;
static SimProblem  s_problems[MAX_PROBLEMS];
static int         s_nproblems;
static s32         s_emigrated_cycle_acc, s_immigrated_acc;
static int         s_listener_added;
static int         s_month_pending;   /* month-end part 2 due in the next phase 0 */

static void push_event(int type, int sev, int x, int y, s32 a, s32 b)
{
    int idx;
    if (s_ev_count == EV_RING) { s_ev_head = (s_ev_head + 1) % EV_RING; s_ev_count--; }
    idx = (s_ev_head + s_ev_count) % EV_RING;
    s_ev[idx].type = (u8)type; s_ev[idx].severity = (u8)sev;
    s_ev[idx].x = (s16)x; s_ev[idx].y = (s16)y; s_ev[idx].a = a; s_ev[idx].b = b;
    s_ev_count++;
}

/* M5: other sim modules (fire.cpp) queue ticker events through the same ring */
void sim_queue_event(int type, int sev, int x, int y, s32 a, s32 b) { push_event(type, sev, x, y, a, b); }

int sim_poll_event(SimEvent *e)
{
    if (!s_ev_count) return 0;
    *e = s_ev[s_ev_head];
    s_ev_head = (s_ev_head + 1) % EV_RING;
    s_ev_count--;
    return 1;
}

/* ------------------------------------------------------------------ settings and lifecycle */
void sim_default_settings(SimSettings *s)
{
    memset(s, 0, sizeof(*s));
    str_copy(s->president, "Kekkola", NAME_LEN);
    str_copy(s->town, "Kaupunki", NAME_LEN);
    s->start_year = START_YEAR_DEFAULT;
    s->funds = FUNDS_NORMAL;
    s->politics = 0;           /* M1: off by default (tests, -seed starts); the New Game menu turns it on */
    s->disasters = 1;
    s->seed = 1;
}

static void on_world_change(int x0, int y0, int x1, int y1, int kinds, void *user)
{
    int nets = 0;
    UNUSED(user);
    if (kinds & WCHG_ROAD) { nets |= NET_POWER | NET_WATER | NET_COVERAGE; net_mark_roads(x0, y0, x1, y1); }
    if (kinds & WCHG_POWER_LINE) nets |= NET_POWER;
    if (kinds & WCHG_LOT) nets |= NET_POWER | NET_WATER | NET_COVERAGE;
    if (kinds & WCHG_TERRAIN) nets |= NET_WATER;
    if (kinds & WCHG_ZONE) nets |= NET_POWER | NET_WATER;             /* F1: zoned land conducts */
    if (kinds & (WCHG_PIPE | WCHG_ZONE | WCHG_ROAD)) net_water_mark_rect(x0, y0, x1, y1);   /* F1: water graph */
    if (nets) net_mark_dirty(nets);
    fields_mark_dirty_rect(kinds, x0, y0, x1, y1);   /* T13: incremental terrain recalc */
}

void sim_init(void)
{
    sim_default_settings(&s_set);
    rng_seed(&s_rng, 1);
    /* T10: re-register every time: a world_init() after an earlier sim_init drops all listeners */
    world_remove_listener(on_world_change, NULL);
    world_add_listener(on_world_change, NULL);
    s_listener_added = 1;
    net_init();
    fields_init();
    budget_init();
}

static void snapshot_unlocks(int emit)
{
    int i, y = s_date.year, st = s_status_max, all = s_set.all_unlocked;
    for (i = 1; i < B_COUNT; i++) {
        u8 u = (u8)(def_building(i)->cat != BCAT_ZONE && def_building(i)->variants && def_unlocked(i, y, st, all));
        if (u && !s_unl_b[i] && emit) push_event(SE_UNLOCK, SEV_INFO, -1, -1, i, 0);
        s_unl_b[i] = u;
    }
    for (i = 1; i < ROAD_CLASS_COUNT; i++) {
        u8 u = (u8)def_road_unlocked(i, y, st, all);
        if (u && !s_unl_r[i] && emit) push_event(SE_UNLOCK, SEV_INFO, -1, -1, i, 1);
        s_unl_r[i] = u;
    }
    for (i = 1; i < ZONE_COUNT; i++) {
        u8 u = (u8)def_zone_unlocked(i, y, st, all);
        if (u && !s_unl_z[i] && emit) push_event(SE_UNLOCK, SEV_INFO, -1, -1, i, 2);
        s_unl_z[i] = u;
    }
}

void sim_new_city(const SimSettings *s)
{
    int i;
    if (!s_listener_added) { world_add_listener(on_world_change, NULL); s_listener_added = 1; }
    s_set = *s;
    memset(&s_date, 0, sizeof(s_date));
    s_date.year = s->start_year;
    rng_seed(&s_rng, s->seed);
    memset(s_dem, 0, sizeof(s_dem));
    memset(&s_stats, 0, sizeof(s_stats));
    memset(s_phase, 0, sizeof(s_phase));
    memset(s_phase_sum, 0, sizeof(s_phase_sum));
    memset(s_hist, 0, sizeof(s_hist));
    memset(s_first_built, 0, sizeof(s_first_built));
    memset(s_warn_svc, 0, sizeof(s_warn_svc));
    s_hist_n = s_hist_pos = 0;
    s_ev_head = s_ev_count = 0;
    s_backlog = 0; s_movein = 0; s_movein_homeless = 0; s_homeless = 0; s_res_track = 0;
    s_bands_done = 0; s_lot_events_month = 0; s_emig_cursor = 1;
    s_warn_power = s_warn_water = 0; s_milestone = 0; s_nproblems = 0;
    s_emigrated_cycle_acc = s_immigrated_acc = 0;
    s_month_pending = 0;
    /* rural migrants are waiting at the start (DESIGN_LESSONS 4.14): demand starts high */
    s_dem[DEM_R].smoothed = 150; s_dem[DEM_C].smoothed = 10; s_dem[DEM_I].smoothed = 40;
    for (i = 0; i < DEM_COUNT; i++) s_dem[i].value = 100 * s_dem[i].smoothed / (s_dem[i].smoothed + 60);
    budget_new(budget_start_funds(s->funds, s->start_year), s->all_unlocked);
    net_reset();
    fields_reset();
    traffic_reset();
    industry_reset();
    complex_reset();           /* M6 */
    civic_reset();             /* M5 */
    politics_reset(s->politics);
    s_status_max = TS_VILLAGE;
    s_stats.town_status = TS_VILLAGE;
    s_stats.era = def_era(s_date.year);
    snapshot_unlocks(0);
    era_reset();               /* M7: trade agreement, history, newspaper */
}

const SimSettings *sim_settings(void) { return &s_set; }
void sim_set_speed(int speed) { s_speed = ICLAMP(speed, SPEED_PAUSE, SPEED_3); }
int  sim_speed(void) { return s_speed; }
const SimDate *sim_date(void) { return &s_date; }
Rng *sim_rng(void) { return &s_rng; }
const Demand *sim_demand(int dem) { return &s_dem[ICLAMP(dem, 0, DEM_COUNT - 1)]; }
const CityStats *sim_stats(void) { return &s_stats; }

/* ------------------------------------------------------------------ helpers */
static int is_res_zone(int z) { return z == ZONE_HOUSES || z == ZONE_APARTMENTS || z == ZONE_FARM; }   /* M3: farms house residents */

static int rect_road_reach(int x0, int y0, int x1, int y1)
{
    int i, j, r = 15;
    for (j = y0; j <= y1; j++)
        for (i = x0; i <= x1; i++) r = IMIN(r, net_road_reach(i, j));
    return r;
}

static int rect_best_road(int x0, int y0, int x1, int y1)
{
    int i, j, best = 0;
    for (j = y0 - ZONE_ROAD_REACH; j <= y1 + ZONE_ROAD_REACH; j++)
        for (i = x0 - ZONE_ROAD_REACH; i <= x1 + ZONE_ROAD_REACH; i++) {
            const Tile *t = world_tile(i, j);
            int c = TILE_ROAD(t);
            if (c == ROAD_RAIL) best = IMAX(best, ROAD_MAIN);
            else if (c && (g_roads[c].flags & RDF_ZONE_ACCESS)) best = IMAX(best, c);
        }
    return best;
}

static int svc_of_req(int req)
{
    switch (req) {
    case REQ_SCHOOL: return SVC_SCHOOL;
    case REQ_SAUNA: return SVC_SAUNA;
    case REQ_HEALTH: return SVC_HEALTH;
    case REQ_PHONE: return SVC_POST;
    default: return -1;
    }
}

/* Per-lot (or empty-rect) context computed once per scan: rect, centre cell, road reach, fields. */
typedef struct { int x0, y0, x1, y1, cx, cy, cell, reach, lv, pol; } Ctx;

static void ctx_make(Ctx *c, int x0, int y0, int x1, int y1)
{
    const u8 *pol = field_data(FLD_POLLUTION);
    c->x0 = x0; c->y0 = y0; c->x1 = x1; c->y1 = y1;
    c->cx = (x0 + x1) >> 1; c->cy = (y0 + y1) >> 1;
    c->cell = (c->cy >> FIELD_SHIFT) * FIELD_MAX_W + (c->cx >> FIELD_SHIFT);
    c->reach = (x0 == x1 && y0 == y1) ? net_road_reach(x0, y0) : rect_road_reach(x0, y0, x1, y1);
    c->lv = WT(c->cx, c->cy)->lv;
    c->pol = pol[c->cell];
}

/* Evaluate the requirements to reach/keep (zone, tier) for a lot (id) or an empty rect (id 0).
   keep = 1 uses the looser "keep" thresholds (hysteresis). cl (query only) also gets the have/need
   details. Returns the unmet bit mask. */
static u32 eval_reqs(int zone, int tier, int id, const Ctx *c, int keep, Checklist *cl)
{
    const TierReq *reqs;
    int n = def_tier_reqs(zone, tier, &reqs), k;
    u32 unmet = 0;
    if (cl) { cl->tier = tier; cl->n = 0; }
    for (k = 0; k < n; k++) {
        ReqItem it;
        int req = reqs[k].req, need = reqs[k].need, svc, st, met = 0;
        if (cl) { memset(&it, 0, sizeof(it)); it.req = (u8)req; it.need = need; }
        switch (req) {
        case REQ_ROAD:
            met = c->reach <= ZONE_ROAD_REACH && c->reach > 0;
            if (cl) it.have = c->reach;
            break;
        case REQ_POWER:
            st = id ? net_lot_power_state(id) : net_power_near(c->x0, c->y0, c->x1, c->y1);
            met = st == NLS_OK;
            if (cl) { it.have = met; it.need = 1; it.why = (u8)(st == NLS_SHORT ? 1 : 0); }
            break;
        case REQ_WATER:
            st = id ? net_lot_water_state(id) : net_water_near(c->cx, c->cy);
            met = st == NLS_OK;
            if (cl) {
                it.have = met; it.need = 1; it.why = (u8)(st == NLS_SHORT ? 1 : 0);
                if (!met && st == NLS_SHORT) {      /* F1: name the overloaded source: "(Pumping station 440/440)" */
                    WaterSrcInfo wi;
                    int src = id ? net_lot_water_src(id, NULL) : net_water_tile_src(c->cx, c->cy);
                    if (src && net_water_source(src, &wi)) { it.lot = src; it.have = wi.used; it.need = wi.output; it.why = 2; }
                }
            }
            break;
        case REQ_WATER_TOWER:
            met = net_water_tower_reach(c->cx, c->cy);
            if (cl) { it.have = met; it.need = 1; }
            break;
        case REQ_ELEMENTS: {      /* M5: this month's precast pool must cover the upgrade; standing blocks keep their tier */
            s32 nd = 1;
            if (id) {
                const BuildingDef *cd = def_building(g_world.lots[id].def);
                if (cd->upgrade_to != B_NONE && cd->tier < tier) nd = IMAX(1, def_building(cd->upgrade_to)->capacity - cd->capacity);
            }
            met = keep || civic_elements_left() >= nd;
            if (cl) { it.have = civic_elements_left(); it.need = nd; it.why = (u8)(civic_elements_output() > 0 ? 1 : 0); }
            break; }
        case REQ_HEAT:            /* M5: district heating (civic.cpp); an upgrade also needs the network's spare */
            st = id ? civic_lot_util_state(UT_HEAT, id) : NLS_NONE;
            met = st == NLS_OK;
            if (met && !keep && id) {
                const BuildingDef *cd = def_building(g_world.lots[id].def);
                if (cd->upgrade_to != B_NONE && cd->tier < tier)
                    met = civic_heat_spare_for(id) >= def_building(cd->upgrade_to)->water_use - cd->water_use;
            }
            if (cl) { it.why = (u8)civic_req_why(REQ_HEAT, id, &it.have, &it.need); it.lot = civic_lot_util_src(UT_HEAT, id, NULL); }
            break;
        case REQ_SCHOOL: case REQ_SAUNA: case REQ_HEALTH: case REQ_PHONE: {
            int src;
            if (req == REQ_PHONE && s_date.year < PHONE_YEAR) {    /* M5: the telephone age starts in 1960 */
                met = 1;
                if (cl) { it.why = 2; it.need = PHONE_YEAR; }
                break;
            }
            svc = svc_of_req(req);
            src = net_cov_src_map(svc)[c->cell];
            met = src && net_cov_q_map(svc)[c->cell] >= (keep ? 20 : 40);
            if (req == REQ_PHONE && keep) met = 1;     /* M5: a house that has its line keeps it (the queue is for new ones) */
            if (cl) { it.lot = src; if (src) net_service_load(src, &it.have, &it.need); it.why = (u8)(src ? 1 : 0); }
            break; }
        case REQ_SHOP:
            met = net_cov_src_map(COV_SHOP)[c->cell] != 0;
            if (cl) { it.lot = net_cov_src_map(COV_SHOP)[c->cell]; it.have = net_cov_dist_map(COV_SHOP)[c->cell]; it.need = net_cov_max_dist(COV_SHOP); }
            break;
        case REQ_LAND_VALUE:
            met = c->lv >= need - (keep ? 5 : 0);
            if (cl) it.have = c->lv;
            break;
        case REQ_LOW_POLLUTION:
            met = c->pol <= need + (keep ? 10 : 0);
            if (cl) it.have = c->pol;
            break;
        case REQ_CUSTOMERS: {
            int tiles = (c->x1 - c->x0 + 1) * (c->y1 - c->y0 + 1);
            s32 have = id ? net_lot_load(COV_SHOP, id) : 0, nd = need * tiles;
            met = have >= (keep ? nd * 2 / 3 : nd);
            if (cl) { it.have = have; it.need = nd; }
            break; }
        case REQ_WORKERS: {
            int rd = net_cov_dist_map(COV_RES)[c->cell];
            met = rd != 255 && s_stats.jobs_fill_pct >= need - (keep ? 15 : 0);
            if (cl) { it.have = s_stats.jobs_fill_pct; it.why = (u8)(rd == 255 ? 0 : 1); }
            break; }
        case REQ_ROUTE:                                       /* M2: a road route to jobs (traffic.cpp) */
            met = traffic_route_ok(c->x0, c->y0) || traffic_route_ok(c->x1, c->y1);
            if (cl) it.have = traffic_commute(c->x0, c->y0);
            break;
        case REQ_MAIN_ROAD:
            st = rect_best_road(c->x0, c->y0, c->x1, c->y1);
            met = st >= ROAD_MAIN;
            if (cl) { it.have = st; it.need = ROAD_MAIN; }
            break;
        default:
            met = 0;
            break;
        }
        if (!met) unmet |= 1u << req;
        if (cl && cl->n < REQ_COUNT) { it.met = (u8)met; cl->item[cl->n++] = it; }
    }
    return unmet;
}

/* Growth score (DESIGN 5.4) without the random term. Terms ids ST_*. */
static s32 score_at(int zone, const Ctx *c, Term *terms, int *nterms)
{
    s32 v[ST_COUNT];
    s32 total = 0;
    int k, n = 0, cell = c->cell;
    memset(v, 0, sizeof(v));
    v[ST_DEMAND] = s_dem[ZONE_DEMAND(zone)].value / 2;
    if (zone == ZONE_FARM || zone == ZONE_FORESTRY) {
        v[ST_DEMAND] = industry_rural_score(zone, c->cx, c->cy);   /* M3: rural lots (farm demand gates new ones) */
    } else if (is_res_zone(zone)) {
        int jd = traffic_commute_map()[cell];                /* M2: congested commute time (walk tiles) */
        v[ST_LAND_VALUE] = (c->lv - 30) / 3;
        v[ST_SERVICES] = (net_cov_q_map(SVC_SCHOOL)[cell] + net_cov_q_map(SVC_HEALTH)[cell] +
                          net_cov_q_map(SVC_SAUNA)[cell] + net_cov_q_map(SVC_FIRE)[cell]) / 40;
        v[ST_POLLUTION] = -c->pol / 4;
        if (s_stats.jobs > 0) {
            if (jd == 255) v[ST_COMMUTE] = -8;
            else if (jd > 20) v[ST_COMMUTE] = -IMIN(20, (jd - 20) / 2);
        }
        if (zone == ZONE_APARTMENTS) v[ST_POLICY] = politics_mod(PM_APT_SCORE_ADD);   /* M1: housing programme */
    } else if (zone == ZONE_COMMERCIAL) {
        v[ST_LAND_VALUE] = (c->lv - 25) / 3;
        v[ST_POLLUTION] = -c->pol / 8;
    } else {
        v[ST_LAND_VALUE] = (c->lv - 30) / 8;
        if (s_stats.population > 0 && net_cov_dist_map(COV_RES)[cell] == 255) v[ST_COMMUTE] = -8;
    }
    for (k = 0; k < ST_COUNT; k++) {
        total += v[k];
        if (terms && v[k] && n < MAX_TERMS) { terms[n].id = (u8)k; terms[n].amount = v[k]; n++; }
    }
    if (nterms) *nterms = n;
    return total;
}

static int noise_term(void) { return rng_range(&s_rng, -10, 10); }

/* rotation facing the nearest road among the rotations allowed by the footprint parity */
static int pick_rot(int x0, int y0, int x1, int y1, int square, int parity)
{
    int best = 99, bestrot = parity, r, d, i, j;
    for (r = 0; r < ROT_COUNT; r++) {
        int dir = g_rot_front_dir[r], found = 0;
        if (!square && (r & 1) != parity) continue;
        for (d = 1; d <= ZONE_ROAD_REACH && !found; d++) {
            if (dir == DIR_SW) for (i = x0 - d + 1; i <= x1 + d - 1 && !found; i++) found = TILE_ROAD(world_tile(i, y1 + d)) && (g_roads[TILE_ROAD(world_tile(i, y1 + d))].flags & RDF_ZONE_ACCESS);
            if (dir == DIR_NE) for (i = x0 - d + 1; i <= x1 + d - 1 && !found; i++) found = TILE_ROAD(world_tile(i, y0 - d)) && (g_roads[TILE_ROAD(world_tile(i, y0 - d))].flags & RDF_ZONE_ACCESS);
            if (dir == DIR_SE) for (j = y0 - d + 1; j <= y1 + d - 1 && !found; j++) found = TILE_ROAD(world_tile(x1 + d, j)) && (g_roads[TILE_ROAD(world_tile(x1 + d, j))].flags & RDF_ZONE_ACCESS);
            if (dir == DIR_NW) for (j = y0 - d + 1; j <= y1 + d - 1 && !found; j++) found = TILE_ROAD(world_tile(x0 - d, j)) && (g_roads[TILE_ROAD(world_tile(x0 - d, j))].flags & RDF_ZONE_ACCESS);
            if (found && d < best) { best = d; bestrot = r; }
        }
    }
    return bestrot;
}

static int rect_free_zone(int zone, int x0, int y0, int x1, int y1)
{
    int i, j;
    if (!world_in(x0, y0) || !world_in(x1, y1)) return 0;
    for (j = y0; j <= y1; j++)
        for (i = x0; i <= x1; i++) {
            const Tile *t = WT(i, j);
            if (TILE_ZONE(t) != zone || !world_is_buildable(i, j) || (t->flags & TF_RUBBLE)) return 0;   /* M5: ruins first */
        }
    return 1;
}

/* lot candidates per zone, largest first: def + rotation parity (1 = footprint swapped) */
typedef struct { u8 def, parity; } Cand;
static const Cand s_cand_h[] = { { B_HOUSE_T1, 0 } };
static const Cand s_cand_a[] = { { B_APT_T1_2X2, 0 }, { B_APT_T1_3X1, 0 }, { B_APT_T1_3X1, 1 } };
static const Cand s_cand_c[] = { { B_COM_T1_2X2, 0 }, { B_COM_T1_2X1, 0 }, { B_COM_T1_2X1, 1 }, { B_COM_T1_1X1, 0 } };
static const Cand s_cand_i[] = { { B_IND_T1_3X3, 0 }, { B_IND_T1_2X2, 0 } };   /* M3: unused (industry_candidates) */
static Cand s_cand_kind[4];                                                     /* M3: this tile's kind candidates */

static void count_first(int zone, int tier, int type, int x, int y, int def)
{
    if (s_first_built[zone][tier]) return;
    s_first_built[zone][tier] = 1;
    push_event(type, SEV_INFO, x, y, def, tier);
}

/* ------------------------------------------------------------------ growth scan (phases 1-8) */
/* F3: 1x1 industrial yard (DESIGN 14.2, PLAYTEST 8): a filler for GENUINE leftovers only (orchestrator: 70 of 93
   town1 industrial lots were yards with the first rule). It grows on an empty industrial tile when
   - no free 2x2 rect of the zone contains (x, y) (a 2x2 lot could still use it),
   - the neighbourhood has settled: no lot under construction and no empty industrial tile in the 3x3 around it
     that could still take a 2x2 (wait until the neighbours finish building),
   - yards stay at most YARD_CAP_PCT of the industrial tiles in the 9x9 window around it (the estate),
   with the usual score/chance/requirement rules. The variant follows the neighbouring industry kind (M3
   industry_def_kind): board yard next to wood, steel stock next to engineering, bales next to textile, milk
   cans next to dairies, workshop next to electronics; else steel / workshop / coal by position. */
#define YARD_CAP_PCT 12          /* yards fill at most 12% of an estate's industrial tiles; the rest of the leftovers stay zoned land (conducts) */
static int free_2x2_at(int x, int y)
{
    int i, j;
    for (j = y - 1; j <= y; j++)
        for (i = x - 1; i <= x; i++)
            if (rect_free_zone(ZONE_INDUSTRY, i, j, i + 1, j + 1)) return 1;
    return 0;
}

static int yard_variant(int x, int y, int variants)
{
    static const s8 vk[IK_COUNT] = { -1, 0, 1, 4, 5, 2 };    /* IK_NONE WOOD METAL TEXTILE FOOD ELEC */
    int k, v = -1;
    for (k = 0; k < DIR_COUNT && v < 0; k++) {
        const Lot *l = world_lot(world_lot_at(x + g_dir_dx[k], y + g_dir_dy[k]));
        int ik;
        if (!l || l->def == B_IND_YARD) continue;
        ik = industry_def_kind(l->def);
        if (ik > IK_NONE && ik < IK_COUNT) v = vk[ik];
    }
    if (v < 0) v = 1 + (int)(hash_xy(x, y, (u32)B_IND_YARD) % 3u);           /* steel stock, workshop, coal */
    return v < variants ? v : 0;
}

static void grow_yard(int x, int y)
{
    const BuildingDef *d = def_building(B_IND_YARD);
    int i, j, score, chance, rot, id, zt = 0, yt = 0;
    Ctx ctx;
    if (!d->variants || d->capacity > s_band_allow[DEM_I] * 2) return;
    if (free_2x2_at(x, y)) return;                                           /* a 2x2 fits: leave it to that */
    for (j = y - 1; j <= y + 1; j++)                                         /* neighbours still settling */
        for (i = x - 1; i <= x + 1; i++) {
            const Tile *t = world_tile(i, j);
            if (!world_in(i, j) || (i == x && j == y)) continue;
            if (t->lot) { if (g_world.lots[t->lot].status == LOT_BUILDING) return; }
            else if (TILE_ZONE(t) == ZONE_INDUSTRY && free_2x2_at(i, j)) return;
        }
    for (j = y - 4; j <= y + 4; j++)                                         /* estate cap */
        for (i = x - 4; i <= x + 4; i++) {
            const Tile *t;
            if (!world_in(i, j)) continue;
            t = WT(i, j);
            if (TILE_ZONE(t) != ZONE_INDUSTRY) continue;
            zt++;
            if (t->lot && g_world.lots[t->lot].def == B_IND_YARD) yt++;
        }
    if ((yt + 1) * 100 > zt * YARD_CAP_PCT) return;
    ctx_make(&ctx, x, y, x, y);
    if (ctx.reach > ZONE_ROAD_REACH) return;
    score = score_at(ZONE_INDUSTRY, &ctx, NULL, NULL) + noise_term();
    if (score <= SIM_T_GROW) return;
    chance = ICLAMP(20 + score, 20, 90);
    if (rng_below(&s_rng, 100) >= chance) return;
    if (eval_reqs(ZONE_INDUSTRY, 1, 0, &ctx, 0, NULL)) return;
    rot = pick_rot(x, y, x, y, 1, 0);
    id = world_lot_place(B_IND_YARD, x, y, rot, yard_variant(x, y, d->variants), LOT_BUILDING);
    if (!id) return;
    s_band_allow[DEM_I] -= d->capacity;
    s_dem[DEM_I].allowance -= d->capacity;
}

static void process_empty(int x, int y)
{
    const Tile *t = WT(x, y);
    int zone = TILE_ZONE(t), dm = ZONE_DEMAND(zone), nc = 0, k, chosen = -1, w, h, rot, score, chance;
    const Cand *cand = NULL;
    int x1, y1;
    const BuildingDef *d;
    Ctx ctx;
    if (zone == ZONE_FARM || zone == ZONE_FORESTRY) { industry_grow_rural(x, y, zone); return; }   /* M3 */
    if (s_band_allow[dm] <= 0) return;
    if (net_road_reach(x, y) > ZONE_ROAD_REACH) return;
    switch (zone) {
    case ZONE_HOUSES: cand = s_cand_h; nc = COUNTOF(s_cand_h); break;
    case ZONE_APARTMENTS: cand = s_cand_a; nc = COUNTOF(s_cand_a); break;
    case ZONE_COMMERCIAL: cand = s_cand_c; nc = COUNTOF(s_cand_c); break;
    case ZONE_INDUSTRY: {           /* M3: the kind (site + kind demand) decides the candidate defs */
        u8 dd[4];
        nc = industry_candidates(x, y, dd, 4);
        for (k = 0; k < nc; k++) { s_cand_kind[k].def = dd[k]; s_cand_kind[k].parity = 0; }
        cand = s_cand_kind;
        UNUSED(s_cand_i);
        break; }
    default: return;
    }
    if (zone == ZONE_INDUSTRY) {    /* F3: don't start a lot one tile in from the estate's edge when the lot could
                                       start on the edge tile itself: that leaves a 1-tile strip (yard bait) */
        if (rect_free_zone(zone, x - 1, y, x, y + 1) && !rect_free_zone(zone, x - 2, y, x - 2, y)) return;
        if (rect_free_zone(zone, x, y - 1, x + 1, y) && !rect_free_zone(zone, x, y - 2, x, y - 2)) return;
    }
    for (k = 0; k < nc; k++) {
        d = def_building(cand[k].def);
        if (!d->variants) continue;
        if (d->capacity > s_band_allow[dm] * 2 && k < nc - 1) continue;    /* too big for the demand left */
        def_footprint(cand[k].def, cand[k].parity, &w, &h);
        if (!rect_free_zone(zone, x, y, x + w - 1, y + h - 1)) continue;
        if (d->tier > 1) {          /* M3: kinds that start above tier 1 (textile mill, large works, electronics) */
            Ctx c2;
            ctx_make(&c2, x, y, x + w - 1, y + h - 1);
            if (eval_reqs(zone, d->tier, 0, &c2, 0, NULL)) continue;
        }
        chosen = k;
        if (k == nc - 1 || rng_below(&s_rng, 100) < 65) break;     /* prefer the larger lot */
    }
    if (chosen < 0) { if (zone == ZONE_INDUSTRY) grow_yard(x, y); return; }   /* F3: gap filler */
    d = def_building(cand[chosen].def);
    def_footprint(cand[chosen].def, cand[chosen].parity, &w, &h);
    x1 = x + w - 1; y1 = y + h - 1;
    ctx_make(&ctx, x, y, x1, y1);
    if (ctx.reach > ZONE_ROAD_REACH) return;
    score = score_at(zone, &ctx, NULL, NULL) + noise_term();
    if (score <= SIM_T_GROW) return;
    chance = ICLAMP(20 + score, 20, 90);
    if (rng_below(&s_rng, 100) >= chance) return;
    if (eval_reqs(zone, d->tier, 0, &ctx, 0, NULL)) return;
    rot = pick_rot(x, y, x1, y1, d->w == d->h, cand[chosen].parity);
    {
        int id = world_lot_place(cand[chosen].def, x, y, rot,
                                 (int)(hash_xy(x, y, (u32)cand[chosen].def) % (u32)IMAX(1, d->variants)), LOT_BUILDING);
        if (!id) return;
        s_band_allow[dm] -= d->capacity;
        s_dem[dm].allowance -= d->capacity;
    }
}

/* M5: Apartments tier 2+ take precast elements from this month's pool (civic.h) */
static int elements_ok(int zone, int up, s32 n)
{
    if (zone != ZONE_APARTMENTS || def_building(up)->tier < 2) return 1;
    return civic_elements_take(n);
}

/* P1: Apartments tier 2/3 (the wooden town -> concrete lahio theme) upgrade on the block's OWN conditions: the next
   tier's checklist (water tower, school, precast elements in this month's pool, heat + clean air + land value for
   T3), a full block and a good site score - NOT fresh R demand (a late town never has +60/+80 of allowance left in one
   band, so M5 saw one T2 block by 1975). A negative R bar doesn't count against the site; the element factory's
   monthly pool is the real cap. Guard: the town's vacant homes stay under 4% of its housing (min 120), so towers
   don't go up empty when nobody is moving in (jobs and migration still set the population). */
#define APT_UP_VACANT_PCT 4
#define APT_UP_VACANT_MIN 120
static int apt_upgrade_ok(s32 score, int dm)
{
    s32 own = score - IMIN(0, s_dem[dm].value / 2);
    return own > SIM_T_GROW && s_stats.vacant <= IMAX(APT_UP_VACANT_MIN, s_stats.capacity_res * APT_UP_VACANT_PCT / 100);
}

static int lower_def(const BuildingDef *d)
{
    int k, id = (int)(d - g_bdefs);
    if (d->tier <= 1) return B_NONE;
    for (k = 1; k < B_COUNT; k++) if (g_bdefs[k].upgrade_to == id) return k;   /* M3: the def that upgrades into it */
    if (industry_def_kind(id) != IK_NONE) return B_NONE;   /* M3: kinds grown at tier 2/3 have no lower tier */
    return def_zone_building(d->zone, d->tier - 1, d->w, d->h);
}

static void displace(s32 n) { if (n > 0) s_homeless += n; s_res_track -= n; }

static void decline_step(int id, Lot *l, const BuildingDef *d, u32 unmet_cur)
{
    int low = lower_def(d), x0, y0, x1, y1;
    world_lot_rect(id, &x0, &y0, &x1, &y1);
    if (is_res_zone(d->zone)) {
        s32 lowcap = low ? def_building(low)->capacity : 0;
        if (low && (unmet_cur || l->occupants <= lowcap)) {
            if (l->occupants > lowcap) { displace(l->occupants - lowcap); l->occupants = (u16)lowcap; }
            world_lot_set_def(id, low);
            push_event(SE_LOT_DOWNGRADED, SEV_INFO, x0, y0, id, d->tier - 1);
        } else if (l->occupants > 0) {
            s32 lose = IMIN((s32)l->occupants, IMAX(1, d->capacity / 4));
            l->occupants = (u16)(l->occupants - lose);
            displace(lose);
        } else {
            world_lot_set_status(id, LOT_ABANDONED);
            if (s_lot_events_month < 2) { push_event(SE_LOT_ABANDONED, SEV_WARN, x0, y0, id, 0); s_lot_events_month++; }
        }
    } else {
        if (low) {
            world_lot_set_def(id, low);
            push_event(SE_LOT_DOWNGRADED, SEV_INFO, x0, y0, id, d->tier - 1);
        } else {
            world_lot_set_status(id, LOT_ABANDONED);
            l->occupants = 0;
            if (s_lot_events_month < 2) { push_event(SE_LOT_ABANDONED, SEV_WARN, x0, y0, id, 0); s_lot_events_month++; }
        }
    }
}

static void process_lot(int id)
{
    Lot *l = &g_world.lots[id];
    const BuildingDef *d = def_building(l->def);
    int x0, y0, x1, y1, zone = d->zone, dm, up = B_NONE, score, full, w, h;
    u32 unmet_cur, unmet_next = 0;
    Ctx ctx;
    if (d->cat != BCAT_ZONE) return;
    dm = ZONE_DEMAND(zone);
    w = (l->rot & 1) ? d->h : d->w; h = (l->rot & 1) ? d->w : d->h;
    x0 = l->x; y0 = l->y; x1 = x0 + w - 1; y1 = y0 + h - 1;
    ctx_make(&ctx, x0, y0, x1, y1);
    if (ctx.reach <= ZONE_ROAD_REACH) l->flags |= LF_ROAD; else l->flags &= ~LF_ROAD;
    unmet_cur = eval_reqs(zone, d->tier, id, &ctx, 1, NULL) | industry_lot_unmet(id, 0);   /* M3: wood / milk */
    l->happy = (u8)field_data(FLD_HAPPINESS)[ctx.cell];
    score = l->status == LOT_BUILDING ? 0 : score_at(zone, &ctx, NULL, NULL) + noise_term();
    /* T12 death-spiral guard: negative demand stops NEW building (allowance) but must not by itself
       empty existing lots - otherwise emigration -> vacancy -> negative R -> decline -> more vacancy
       (and I/C the same through jobs) runs a whole town to zero. Decline needs a real cause (unmet
       requirement, pollution, low land value, commute). */
    if (l->status == LOT_ACTIVE && s_dem[dm].value / 2 < SIM_DECLINE_DEMAND_FLOOR)
        score += SIM_DECLINE_DEMAND_FLOOR - s_dem[dm].value / 2;
    full = is_res_zone(zone) ? l->occupants * 4 >= d->capacity * 3 : s_stats.jobs_fill_pct >= 75;
    if (d->upgrade_to != B_NONE && def_building(d->upgrade_to)->variants &&
        (def_building(d->upgrade_to)->era_year <= s_date.year || s_set.all_unlocked)) {   /* M3: era of the tier */
        up = d->upgrade_to;
        /* the next-tier checklist is needed for upgrades every scan, otherwise once a month (the
           cached problem bits feed the growth overlay and the top-problems list) */
        if ((full && score > SIM_T_GROW && l->status == LOT_ACTIVE) || ((id + s_date.cycle) & 3) == 0)
            unmet_next = eval_reqs(zone, d->tier + 1, id, &ctx, 0, NULL) | industry_lot_unmet(id, 1);
        else unmet_next = l->problems & ~unmet_cur;
    }
    l->problems = unmet_cur | unmet_next;
    if (l->status == LOT_BUILDING) return;
    if (l->status == LOT_ABANDONED) {
        if (!unmet_cur && score > SIM_T_GROW && s_band_allow[dm] > 0) l->growth = (s8)IMIN(l->growth + 1, 8);
        else l->growth = 0;
        if (l->growth >= SIM_G_UP * 2) {
            world_lot_set_status(id, LOT_ACTIVE);
            l->occupants = 0; l->growth = 0;
            if (s_lot_events_month < 2) { push_event(SE_LOT_RESTORED, SEV_INFO, x0, y0, id, 0); s_lot_events_month++; }
        }
        return;
    }
    if (l->status != LOT_ACTIVE) return;
    /* residents move in */
    if (is_res_zone(zone) && l->occupants < d->capacity && !unmet_cur && score > SIM_T_DECLINE && s_band_movein > 0) {
        s32 add = IMIN((s32)(d->capacity - l->occupants), IMIN(IMAX(1, d->capacity / 2), s_band_movein));
        /* T12 fix ("homeless -1"): the month-end emigration can shrink the pool below this cycle's
           s_movein_homeless (computed earlier in phase 15), so never take more than is left */
        s32 from_h = IMIN(add, IMIN(s_movein_homeless, IMAX(0, s_homeless)));
        l->occupants = (u16)(l->occupants + add);
        s_band_movein -= add; s_movein -= add;
        s_movein_homeless -= from_h; s_homeless -= from_h;
        s_immigrated_acc += add - from_h;
        s_res_track += add;
    }
    if (unmet_cur || score < SIM_T_DECLINE) l->growth = (s8)IMAX(l->growth - 1, -8);
    else if (up && !unmet_next && full && (zone == ZONE_APARTMENTS && def_building(up)->tier >= 2
                                           ? apt_upgrade_ok(score, dm)
                                           : score > SIM_T_GROW && s_band_allow[dm] >= def_building(up)->capacity - d->capacity))
        l->growth = (s8)IMIN(l->growth + 1, 8);
    else if (l->growth > 0) l->growth--;
    else if (l->growth < 0) l->growth++;
    if (l->growth >= SIM_G_UP && up && elements_ok(zone, up, def_building(up)->capacity - d->capacity)) {
        s32 add = def_building(up)->capacity - d->capacity;
        world_lot_set_def(id, up);
        world_lot_set_variant(id, (int)(hash_xy(x0, y0, (u32)up) % (u32)IMAX(1, def_building(up)->variants)));
        if (is_res_zone(zone) && l->occupants > def_building(up)->capacity) {   /* M3: the mechanised farm sheds hands */
            displace(l->occupants - def_building(up)->capacity);
            l->occupants = (u16)def_building(up)->capacity;
        }
        s_band_allow[dm] = IMAX(0, s_band_allow[dm] - add); s_dem[dm].allowance = IMAX(0, s_dem[dm].allowance - add);
        l->growth = 0;
        count_first(zone, d->tier + 1, SE_LOT_UPGRADED, x0, y0, up);
    } else if (l->growth <= -SIM_G_DOWN) {
        l->growth = 0;
        decline_step(id, l, d, unmet_cur);
    }
}

static void growth_band(int band)
{
    int y, x, y0, y1, left, k;
    y0 = band * g_world.h / 8; y1 = (band + 1) * g_world.h / 8;
    left = 8 - s_bands_done;
    for (k = 0; k < DEM_COUNT; k++)
        s_band_allow[k] = left <= 1 ? s_dem[k].allowance : IMIN(s_dem[k].allowance, (s_dem[k].allowance * 3 + left - 1) / left);
    s_band_movein = left <= 1 ? s_movein : IMIN(s_movein, (s_movein * 3 + left - 1) / left);
    world_batch_begin();
    for (y = y0; y < y1; y++)
        for (x = 0; x < g_world.w; x++) {
            const Tile *t = WT(x, y);
            if (t->lot) {
                const Lot *l = &g_world.lots[t->lot];
                if (l->x == x && l->y == y) process_lot(t->lot);
            } else if (TILE_ZONE(t)) process_empty(x, y);
        }
    world_batch_end();
    s_bands_done++;
}

/* ------------------------------------------------------------------ stats, employment, demand */
static int pull_base(int year) { return year < 1960 ? 70 : year < 1970 ? 100 : year < 1975 ? 60 : 30; }
static int pull_rate(int year) { return year < 1960 ? 12 : year < 1970 ? 15 : year < 1975 ? 8 : 4; }  /* per mille of pop */
static int spend_pct(int era, int avg_tier10) { return 12 + era + (avg_tier10 - 10) / 5; }

static s32 s_cap_res, s_cap_com, s_cap_ind, s_jobs_other, s_tier_sum, s_res_lots;

static void aggregate(void)
{
    CityStats *st = &s_stats;
    int i, k;
    s64 lv_sum = 0, pol_sum = 0, hap_sum = 0, com_sum = 0, fire_sum = 0;
    s32 res = 0, jobs = 0, lots = 0;
    for (k = 0; k < ZONE_COUNT; k++) { st->residents_zone[k] = 0; st->jobs_zone[k] = 0; memset(st->lots_tier[k], 0, sizeof(st->lots_tier[k])); }
    st->lots_building = st->lots_abandoned = 0;
    s_cap_res = s_cap_com = s_cap_ind = s_jobs_other = 0; s_tier_sum = 0; s_res_lots = 0;
    for (k = 0; k < SVC_COUNT; k++) { st->svc_capacity[k] = 0; st->svc_load[k] = 0; }
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        int cx, cy;
        if (l->def == B_NONE) continue;
        d = def_building(l->def);
        if (d->cat != BCAT_ZONE) {
            if (l->status == LOT_ACTIVE) {
                s_jobs_other += d->jobs;
                if (d->cat == BCAT_SERVICE) st->svc_capacity[d->service] += d->capacity * g_budget.funding[d->service] / 100
                                                * (d->service == SVC_SCHOOL ? politics_mod(PM_SCHOOL_CAP_PCT) : 100) / 100;   /* M1 */
            }
            continue;
        }
        lots++;
        if (l->status == LOT_BUILDING) st->lots_building++;
        else if (l->status == LOT_ABANDONED) { st->lots_abandoned++; continue; }
        else st->lots_tier[d->zone][d->tier]++;
        if (is_res_zone(d->zone)) {
            s_cap_res += d->capacity;
            if (l->status != LOT_ACTIVE) continue;
            res += l->occupants;
            st->residents_zone[d->zone] += l->occupants;
            s_tier_sum += d->tier * l->occupants;
            if (d->zone == ZONE_FARM) s_jobs_other += l->occupants * SIM_LABOUR_PCT / 100;   /* M3: self-employed */
            s_res_lots++;
            cx = (l->x + l->x + d->w - 1) >> 1; cy = (l->y + l->y + d->h - 1) >> 1;
            lv_sum += (s64)WT(l->x, l->y)->lv * l->occupants;
            pol_sum += (s64)field_at(FLD_POLLUTION, cx, cy) * l->occupants;
            hap_sum += (s64)l->happy * l->occupants;
            fire_sum += (s64)field_at(FLD_FIRE_RISK, cx, cy) * l->occupants;
            k = traffic_commute(cx, cy);                     /* M2 */
            com_sum += (s64)(k == 255 ? 60 : k) * l->occupants;
        } else {
            if (d->zone == ZONE_COMMERCIAL) s_cap_com += d->capacity;
            else if (d->zone == ZONE_INDUSTRY) s_cap_ind += d->capacity;   /* M3: not the logging camps */
            if (l->status == LOT_ACTIVE) jobs += d->capacity;
        }
    }
    /* bulldozed homes: residents the sim did not move out themselves become homeless */
    if (res < s_res_track) s_homeless += s_res_track - res;
    s_res_track = res;
    st->population = res;
    st->jobs = jobs + s_jobs_other + complex_jobs();   /* M6: complexes (their own staffing) */
    st->workers = res * SIM_LABOUR_PCT / 100;
    st->unemployed = IMAX(0, st->workers - st->jobs);
    st->jobs_fill_pct = st->jobs ? IMIN(100, st->workers * 100 / st->jobs) : 100;
    st->homeless = s_homeless;
    st->capacity_res = s_cap_res;
    st->vacant = IMAX(0, s_cap_res - res);
    st->lots_zone = lots;
    for (k = 0; k < SVC_COUNT; k++) st->svc_load[k] = res * g_services[k].demand_pct / 100;
    if (res > 0) {
        st->avg_land_value = (s32)(lv_sum / res);
        st->avg_pollution = (s32)(pol_sum / res);
        st->avg_happiness = (s32)(hap_sum / res);
        st->avg_fire_risk = (s32)(fire_sum / res);
        st->avg_commute = (s32)(com_sum / res);
    } else {
        st->avg_land_value = field_avg(FLD_LAND_VALUE);
        st->avg_pollution = field_avg(FLD_POLLUTION);
        st->avg_happiness = 50;
        st->avg_fire_risk = field_avg(FLD_FIRE_RISK);
        st->avg_commute = 0;
    }
    {
        const NetStats *p = net_stats(NET_POWER), *w = net_stats(NET_WATER);
        st->power_supply = p->supply; st->power_demand = p->demand; st->power_short_lots = p->lots_short;
        st->water_supply = w->supply; st->water_demand = w->demand; st->water_short_lots = w->lots_short;
    }
    st->town_status = s_status_max;
    st->era = def_era(s_date.year);
}

/* employment: fill job lots city-wide in proportion (deterministic) */
static void employ(void)
{
    s32 jobs = s_stats.jobs, employed = IMIN(s_stats.workers, jobs);
    int i;
    for (i = 1; i < g_world.lot_hi; i++) {
        Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        s32 cap;
        if (l->def == B_NONE) continue;
        d = def_building(l->def);
        if (d->cat == BCAT_ZONE && is_res_zone(d->zone)) continue;
        cap = d->cat == BCAT_ZONE ? d->capacity : d->jobs;
        if (!cap) continue;
        l->occupants = (u16)((l->status == LOT_ACTIVE && jobs > 0) ? (s32)((s64)cap * employed / jobs) : 0);
    }
}

static void set_terms(Demand *dm, const s32 *vals, const u8 *ids, int n)
{
    int k;
    s32 sum = 0;
    dm->nterms = 0;
    for (k = 0; k < n; k++) {
        sum += vals[k];
        if (vals[k] && dm->nterms < MAX_TERMS) { dm->terms[dm->nterms].id = ids[k]; dm->terms[dm->nterms].amount = vals[k]; dm->nterms++; }
    }
    dm->target = sum;
}

static s32 bar_of(s32 s, s32 k) { return (s32)((s64)100 * s / (IABS(s) + k)); }

static void compute_demand(void)
{
    const CityStats *st = &s_stats;
    s32 res = st->population, W = st->workers, open_people, pull, attr_pct, tax_pct, pa;
    s32 v[8];
    u8 ids[8];
    int era = def_era(s_date.year), avg_tier10 = res ? (int)(s_tier_sum * 10 / res) : 10, k;
    s32 kk[DEM_COUNT];
    /* R */
    open_people = IMAX(0, st->jobs - W) * 100 / SIM_LABOUR_PCT;
    pull = pull_base(s_date.year) + res * pull_rate(s_date.year) / 1000;
    attr_pct = 50 + st->avg_happiness / 2;
    tax_pct = ICLAMP((g_budget.tax_res - TAX_NEUTRAL) * 4, -12, 44);
    s_stats.migration_pull = pull;
    v[0] = open_people;                         ids[0] = DT_OPEN_JOBS;
    v[1] = pull;                                ids[1] = DT_MIGRATION;
    v[2] = pull * (attr_pct - 100) / 100;       ids[2] = DT_ATTRACTIVENESS;
    v[3] = -pull * tax_pct / 100;               ids[3] = DT_TAX;
    v[4] = -st->vacant;                         ids[4] = DT_VACANT_HOMES;
    v[5] = -st->unemployed;                     ids[5] = DT_EMIGRATION;
    k = politics_mod(PM_MIGRATION_PCT);         /* M1: decrees scale the rural migration (0 when off) */
    v[6] = k == 100 ? 0 : (v[1] + v[2] + v[3]) * (k - 100) / 100;   ids[6] = DT_POLICY;
    v[7] = transit_regional_migrants(pull);     ids[7] = DT_REGIONAL;   /* M2: railway station */
    set_terms(&s_dem[DEM_R], v, ids, 8);
    pa = IMAX(0, v[1] + v[2] + v[3] + v[6] + v[7]);
    /* move-in flow for this cycle (a quarter of the monthly flow) + homeless re-housing */
    s_movein_homeless = (s_homeless + 3) / 4;
    s_movein = (pa + open_people + 3) / 4 + s_movein_homeless;
    /* C */
    tax_pct = ICLAMP((g_budget.tax_bus - TAX_NEUTRAL) * 3, -9, 33);
    v[0] = res * spend_pct(era, avg_tier10) / 100;   ids[0] = DT_SPENDING;
    v[1] = -s_cap_com;                               ids[1] = DT_COM_CAPACITY;
    v[2] = -v[0] * tax_pct / 100;                    ids[2] = DT_TAX;
    k = politics_mod(PM_COM_DEMAND_PCT);             /* M1 */
    v[3] = k == 100 ? 0 : v[0] * (k - 100) / 100;    ids[3] = DT_POLICY;
    set_terms(&s_dem[DEM_C], v, ids, 4);
    /* I */
    v[0] = s_date.year < 1990 ? 20 + res * 2 / 100 : 10 + res / 100;    ids[0] = DT_CLEARING;
    v[1] = 10 + res * (era >= ERA_STRUCTURAL ? 2 : 1) / 100;            ids[1] = DT_WESTERN;
    v[0] = POL_PCT(v[0], politics_mod(PM_CLEARING_PCT));                /* M1: Eastern relations, decrees */
    v[1] = POL_PCT(v[1], politics_mod(PM_WESTERN_PCT));                 /* M1: Western trade, decrees */
    v[0] = POL_PCT(v[0], trade_clearing_pct());                         /* M7: agreement level, oil boom, penalties */
    v[1] = POL_PCT(v[1], trade_western_pct());                          /* M7: Western price index */
    v[2] = W * 60 / 100;                                                ids[2] = DT_LOCAL_NEED;
    v[3] = -s_cap_ind;                                                  ids[3] = DT_IND_CAPACITY;
    v[4] = -(v[0] + v[1] + v[2]) * tax_pct / 100;                       ids[4] = DT_TAX;
    k = politics_mod(PM_IND_DEMAND_PCT);                                /* M1 */
    v[5] = k == 100 ? 0 : (v[0] + v[1] + v[2]) * (k - 100) / 100;       ids[5] = DT_POLICY;
    v[6] = transit_freight_jobs(v[0] + v[1]);                           ids[6] = DT_FREIGHT;   /* M2: station */
    set_terms(&s_dem[DEM_I], v, ids, 7);
    kk[DEM_R] = 60 + res / 10; kk[DEM_C] = 15 + res / 60; kk[DEM_I] = 20 + res / 50;
    for (k = 0; k < DEM_COUNT; k++) {
        Demand *dm = &s_dem[k];
        dm->allowance = IMAX(0, IMIN(dm->target, dm->smoothed));
        dm->value = bar_of(dm->smoothed, kk[k]);
    }
}

/* ------------------------------------------------------------------ month end */
static void emigrate(s32 n)
{
    /* remove n residents from homes, round robin from a moving cursor (deterministic) */
    int guard = 0;
    while (n > 0 && guard < 2 * MAX_LOTS) {
        Lot *l;
        const BuildingDef *d;
        if (s_emig_cursor >= g_world.lot_hi || s_emig_cursor < 1) s_emig_cursor = 1;
        l = &g_world.lots[s_emig_cursor];
        d = def_building(l->def);
        if (l->def != B_NONE && d->cat == BCAT_ZONE && is_res_zone(d->zone) && l->status == LOT_ACTIVE && l->occupants) {
            s32 take = IMIN(n, IMAX(1, (s32)l->occupants / 4));
            l->occupants = (u16)(l->occupants - take);
            n -= take;
            s_res_track -= take;
        }
        s_emig_cursor++;
        guard++;
        if (g_world.lot_hi <= 1) break;
    }
}

static void compute_problems(void)
{
    s32 cnt[REQ_COUNT], resd[REQ_COUNT], bestocc[REQ_COUNT];
    s16 px[REQ_COUNT], py[REQ_COUNT];
    int i, r, n = 0, a, b;
    memset(cnt, 0, sizeof(cnt)); memset(resd, 0, sizeof(resd)); memset(bestocc, 0, sizeof(bestocc));
    memset(px, 0, sizeof(px)); memset(py, 0, sizeof(py));
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d = def_building(l->def);
        if (l->def == B_NONE || d->cat != BCAT_ZONE || !l->problems) continue;
        for (r = 0; r < REQ_COUNT; r++) {
            if (!(l->problems & (1u << r))) continue;
            cnt[r]++;
            if (is_res_zone(d->zone)) resd[r] += l->occupants;
            if (l->occupants >= bestocc[r] || cnt[r] == 1) { bestocc[r] = l->occupants; px[r] = l->x; py[r] = l->y; }
        }
    }
    for (r = 0; r < REQ_COUNT; r++) {
        if (!cnt[r]) continue;
        if (n < MAX_PROBLEMS) n++;
        else if (resd[r] <= s_problems[n - 1].residents) continue;
        s_problems[n - 1].req = (u8)r; s_problems[n - 1].count = cnt[r]; s_problems[n - 1].residents = resd[r];
        s_problems[n - 1].x = px[r]; s_problems[n - 1].y = py[r];
        /* insertion sort: residents desc, then count desc */
        for (a = n - 1; a > 0; a--) {
            SimProblem *p = &s_problems[a], *q = &s_problems[a - 1], tmp;
            b = p->residents > q->residents || (p->residents == q->residents && p->count > q->count);
            if (!b) break;
            tmp = *p; *p = *q; *q = tmp;
        }
    }
    s_nproblems = n;
}

static void hist_push(void)
{
    s32 v[HIST_COUNT];
    int k;
    v[HIST_POPULATION] = s_stats.population; v[HIST_MONEY] = g_budget.money;
    v[HIST_DEMAND_R] = s_dem[DEM_R].value; v[HIST_DEMAND_C] = s_dem[DEM_C].value; v[HIST_DEMAND_I] = s_dem[DEM_I].value;
    v[HIST_JOBS] = s_stats.jobs; v[HIST_EMIGRATED] = s_stats.emigrated_month;
    for (k = 0; k < HIST_COUNT; k++) s_hist[k][s_hist_pos] = v[k];
    s_hist_pos = (s_hist_pos + 1) % SIM_HISTORY;
    if (s_hist_n < SIM_HISTORY) s_hist_n++;
}

static void statlog_row(void);

/* Month end, part 1 (phase 15 of the last cycle): construction, ageing, employment, emigration,
   demand smoothing. Part 2 (phase 0 of the next step): budget, politics, status, events, history,
   stat log, date. Split so neither step carries the whole month-end cost. */
static void month_end_population(void)
{
    int i, k;
    s32 emig, unemployed_people;
    for (i = 1; i < g_world.lot_hi; i++) {
        Lot *l = &g_world.lots[i];
        if (l->def == B_NONE) continue;
        if (l->age_months < 65535) l->age_months++;
        l->flags &= ~LF_NEW;
        if (l->status == LOT_BUILDING) {
            if (l->build_left > 1) l->build_left--;
            else {
                const BuildingDef *d = def_building(l->def);
                world_lot_set_status(i, LOT_ACTIVE);
                if (d->cat == BCAT_ZONE) count_first(d->zone, d->tier, SE_LOT_BUILT, l->x, l->y, l->def);
            }
        }
    }
    aggregate();
    employ();
    /* emigration to Sweden: 1/8 of the unemployed (as people) and 1/4 of the homeless per month */
    unemployed_people = s_stats.unemployed * 100 / SIM_LABOUR_PCT;
    emig = (unemployed_people + 7) / 8;
    emig = POL_PCT(emig, politics_mod(PM_EMIGRATION_PCT));    /* M1: decrees/events (100 when off) */
    emig = IMIN(emig, s_stats.population);
    emigrate(emig);
    {
        s32 h = (s_homeless + 3) / 4;
        s_homeless -= h;
        emig += h;
    }
    s_stats.emigrated_month = emig;
    s_stats.emigrated_total += emig;
    s_stats.immigrated_month = s_immigrated_acc;
    s_immigrated_acc = 0;
    s_stats.population = s_res_track;
    s_stats.homeless = s_homeless;
    if (emig >= 10) push_event(SE_EMIGRATION, emig >= 100 ? SEV_WARN : SEV_INFO, -1, -1, emig, s_stats.emigrated_total);
    /* demand smoothing: 1/4 of the way per month (DESIGN 5.5) */
    for (k = 0; k < DEM_COUNT; k++) {
        Demand *dm = &s_dem[k];
        s32 diff = dm->target - dm->smoothed;
        dm->smoothed += diff >= 0 ? (diff + 3) / 4 : -((-diff + 3) / 4);
    }
    s_month_pending = 1;
}

/* M7: raise the town status to st (rewards, events, ceremony page); month end and the town-rights decree */
void sim_grant_status(int st)
{
    if (st <= s_status_max || st >= TS_COUNT) return;
    while (s_status_max < st) {
        s_status_max++;
        if (g_statuses[s_status_max].reward) budget_spend(-g_statuses[s_status_max].reward, BL_GRANTS);
        push_event(SE_STATUS_UP, SEV_POLITICAL, -1, -1, s_status_max, g_statuses[s_status_max].reward);
    }
    s_stats.town_status = s_status_max;
    era_status_granted(s_status_max);
    snapshot_unlocks(1);
}

static void month_end_books(void)
{
    int i, st, k;
    s_month_pending = 0;
    k = g_budget.loans_taken;
    budget_month_end();
    if (g_budget.loans_taken > k) push_event(SE_BANKRUPT_WARN, SEV_URGENT, -1, -1, g_budget.loan, 1);
    else if (g_budget.months_negative == 1) push_event(SE_NO_MONEY, SEV_WARN, -1, -1, g_budget.money, 0);
    else if (g_budget.months_negative == 3) push_event(SE_BANKRUPT_WARN, SEV_URGENT, -1, -1, g_budget.money, 0);
    industry_month();          /* M3: regrowth, harvest, farm claims, exports (before the politics reasons) */
    complex_month();           /* M6: paper mill / Valtion Metalli: stages, owner, gifts, events, company town */
    politics_month();
    civic_month();             /* M5: element pool, heat fuel, ruins */
    /* town status + rewards (M7: with politics on only by decree, era.h) */
    st = era_status_target(def_status_for_population(s_stats.population));
    sim_grant_status(st);
    era_month();               /* M7: trade, landmarks, era events, history, the January paper */
    {
        static const s32 ms[] = { 100, 1000, 2000, 5000, 20000, 50000 };
        for (k = 0; k < COUNTOF(ms); k++)
            if (s_stats.population >= ms[k] && s_milestone < ms[k]) { s_milestone = ms[k]; push_event(SE_MILESTONE, SEV_INFO, -1, -1, ms[k], 0); }
    }
    /* shortages (warn at most every 3 months), full services (every 6 months per service) */
    if (s_warn_power > 0) s_warn_power--;
    if (s_warn_water > 0) s_warn_water--;
    if (s_stats.power_short_lots > 0 && !s_warn_power) { push_event(SE_POWER_SHORTAGE, SEV_WARN, -1, -1, s_stats.power_short_lots, s_stats.power_demand - s_stats.power_supply); s_warn_power = 3; }
    if (s_stats.water_short_lots > 0 && !s_warn_water) {   /* F1: located at the source with the most dry buildings */
        WaterSummary ws;
        const Lot *wl;
        net_water_summary(&ws);
        wl = world_lot(ws.worst_lot);
        push_event(SE_WATER_SHORTAGE, SEV_WARN, wl ? wl->x : -1, wl ? wl->y : -1, s_stats.water_short_lots, s_stats.water_demand - ws.used);
        s_warn_water = 3;
    }
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d = def_building(l->def);
        s32 load, cap;
        if (l->def == B_NONE || d->cat != BCAT_SERVICE) continue;
        if (s_warn_svc[d->service] > 0) continue;
        net_service_load(i, &load, &cap);
        if (load > cap) { push_event(SE_SERVICE_FULL, SEV_WARN, l->x, l->y, i, d->service); s_warn_svc[d->service] = 7; }
    }
    for (k = 0; k < SVC_COUNT; k++) if (s_warn_svc[k] > 0) s_warn_svc[k]--;
    compute_problems();
    hist_push();
    statlog_row();
    for (k = 0; k < SIM_STEPS_PER_CYCLE; k++) { s_phase[k].avg_ms = 0; s_phase[k].max_ms = 0; s_phase[k].count = 0; s_phase_sum[k] = 0; }
    s_lot_events_month = 0;
    push_event(SE_MONTH, SEV_INFO, -1, -1, s_date.year, s_date.month);
    s_date.total_months++;
    if (++s_date.month >= MONTHS_PER_YEAR) {
        s_date.month = 0;
        s_date.year++;
        push_event(SE_YEAR, SEV_INFO, -1, -1, s_date.year, 0);
    }
    snapshot_unlocks(1);
    s_stats.era = def_era(s_date.year);
}

/* ------------------------------------------------------------------ scheduler */
void sim_step(void)
{
    int phase = s_date.step, cycle = s_date.cycle;
    double t0 = time_ms(), dt;
    switch (phase) {
    case 0:
        s_bands_done = 0;
        if (s_month_pending) month_end_books();
        break;
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
        growth_band((phase - 1 + (int)(s_date.total_steps / SIM_STEPS_PER_CYCLE)) & 7);
        break;
    case 9:  net_step(); break;
    case 10: fields_step_pollution(); break;
    case 11: fields_step_land_value(cycle); break;
    case 12: fields_step_social(cycle); break;
    case 13: net_coverage_step(net_coverage_next()); break;
    case 14: traffic_step(); break;            /* M2: pass boundary (or a slice) */
    case 15:
        aggregate();
        employ();
        compute_demand();
        industry_cycle();          /* M3: wood/milk needs, kind demand */
        if (cycle == SIM_CYCLES_PER_MONTH - 1) month_end_population();
        break;
    }
    civic_step(phase, cycle);                  /* M5: heat/sewage solves (phase 9), fires */
    if (phase != 14) traffic_slice();          /* M2: bounded cluster work of the running traffic pass */
    dt = time_ms() - t0;
    s_phase_sum[phase] += dt;
    s_phase[phase].count++;
    s_phase[phase].avg_ms = s_phase_sum[phase] / s_phase[phase].count;
    if (dt > s_phase[phase].max_ms) s_phase[phase].max_ms = dt;
    s_phase_tot_sum[phase] += dt;
    s_phase_tot[phase].count++;
    s_phase_tot[phase].avg_ms = s_phase_tot_sum[phase] / s_phase_tot[phase].count;
    if (dt > s_phase_tot[phase].max_ms) s_phase_tot[phase].max_ms = dt;
    s_date.total_steps++;
    if (++s_date.step >= SIM_STEPS_PER_CYCLE) {
        s_date.step = 0;
        if (++s_date.cycle >= SIM_CYCLES_PER_MONTH) s_date.cycle = 0;
    }
}

int sim_frame(double dt_seconds, double budget_ms)
{
    static const int sps[SPEED_COUNT] = { 0, SIM_STEPS_PER_SEC_1, SIM_STEPS_PER_SEC_2, SIM_STEPS_PER_SEC_3 };
    double start = time_ms();
    int n = 0, rate = sps[s_speed];
    if (rate <= 0) { s_backlog = 0; return 0; }
    s_backlog += dt_seconds * rate;
    if (s_backlog > rate) s_backlog = rate;              /* at most 1 s of backlog */
    while (s_backlog >= 1.0) {
        if (n > 0 && time_ms() - start >= budget_ms) break;
        sim_step();
        s_backlog -= 1.0;
        n++;
    }
    return n;
}

void sim_run_months(int months)
{
    u32 target = s_date.total_months + (u32)months;
    while (s_date.total_months < target) sim_step();
}

/* ------------------------------------------------------------------ queries */
static int lot_capacity(const BuildingDef *d) { return d->cat == BCAT_ZONE ? d->capacity : d->jobs; }

int sim_query_lot(int lot, LotInfo *out)
{
    const Lot *l = world_lot(lot);
    const BuildingDef *d;
    int x0, y0, x1, y1, cx, cy;
    memset(out, 0, sizeof(*out));
    if (!l) return 0;
    d = def_building(l->def);
    world_lot_rect(lot, &x0, &y0, &x1, &y1);
    cx = (x0 + x1) >> 1; cy = (y0 + y1) >> 1;
    out->lot = lot; out->def = l->def; out->status = l->status; out->tier = d->tier; out->rot = l->rot;
    out->variant = l->variant; out->occupants = l->occupants; out->capacity = lot_capacity(d);
    out->age_months = l->age_months;
    out->land_value = WT(cx, cy)->lv; out->pollution = field_at(FLD_POLLUTION, cx, cy); out->happy = l->happy;
    out->growth = l->growth;
    out->growth_threshold = l->growth >= 0 ? SIM_G_UP : -SIM_G_DOWN;
    if (d->cat == BCAT_ZONE) {
        Ctx ctx;
        ctx_make(&ctx, x0, y0, x1, y1);
        out->score = score_at(d->zone, &ctx, out->terms, &out->nterms);
        if (d->upgrade_to != B_NONE && def_building(d->upgrade_to)->variants) {
            eval_reqs(d->zone, d->tier + 1, lot, &ctx, 0, &out->next);
            out->next.n += industry_lot_reqs(lot, 1, &out->next.item[out->next.n], REQ_COUNT - out->next.n);   /* M3 */
        } else if (l->status == LOT_ACTIVE || l->status == LOT_ABANDONED) {
            eval_reqs(d->zone, d->tier, lot, &ctx, 1, &out->next);    /* max tier: keep list */
            out->next.n += industry_lot_reqs(lot, 0, &out->next.item[out->next.n], REQ_COUNT - out->next.n);   /* M3 */
        }
    }
    if (d->cat == BCAT_SERVICE || (d->cat == BCAT_ZONE && d->zone == ZONE_COMMERCIAL))
        net_service_load(lot, &out->svc_load, &out->svc_capacity);
    if (d->cat == BCAT_SERVICE) out->catchment_tiles = net_catchment_tiles(lot);
    if (d->cat == BCAT_POWER) out->power_out = net_source_output(lot);
    if (d->cat == BCAT_WATER) out->water_out = net_source_output(lot);
    out->upkeep = d->cat == BCAT_SERVICE ? d->upkeep * g_budget.funding[d->service] / 100 : d->upkeep;
    return 1;
}

int sim_query_tile(int x, int y, TileInfo *out)
{
    const Tile *t;
    int k;
    memset(out, 0, sizeof(*out));
    out->x = x; out->y = y;
    if (!world_in(x, y)) return 0;
    t = WT(x, y);
    out->terrain = TER_TYPE(t); out->zone = TILE_ZONE(t); out->road = TILE_ROAD(t); out->lot = t->lot;
    out->powered = (t->zone & ZF_POWERED) ? 1 : 0;
    out->watered = (t->zone & ZF_WATERED) ? 1 : 0;
    out->land_value = t->lv;
    out->pollution = field_at(FLD_POLLUTION, x, y);
    out->traffic = traffic_flow(x, y);
    for (k = 0; k < SVC_COUNT; k++) out->coverage[k] = (u8)net_cov_quality(k, x, y);
    if (out->zone && !t->lot) {
        Ctx ctx;
        ctx_make(&ctx, x, y, x, y);
        out->score = score_at(out->zone, &ctx, out->terms, &out->nterms);
        eval_reqs(out->zone, 1, 0, &ctx, 0, &out->build);
    }
    return 1;
}

int sim_top_problems(SimProblem *out, int max)
{
    int n = IMIN(max, s_nproblems);
    if (n > 0) memcpy(out, s_problems, n * sizeof(SimProblem));
    return n;
}

static u8 net_ovv(int state) { return OVV(OVR_NET, state == NLS_OK ? NETC_OK : state == NLS_SHORT ? NETC_SHORTAGE : NETC_NOT_CONNECTED); }

static int main_problem(u32 problems)
{
    static const u8 order[] = { REQ_ROAD, REQ_POWER, REQ_WATER, REQ_WATER_TOWER, REQ_SCHOOL, REQ_SAUNA, REQ_HEALTH,
                                REQ_SHOP, REQ_PHONE, REQ_LAND_VALUE, REQ_LOW_POLLUTION, REQ_CUSTOMERS, REQ_WORKERS,
                                REQ_ELEMENTS, REQ_MAIN_ROAD, REQ_HEAT };
    int k;
    for (k = 0; k < COUNTOF(order); k++) if (problems & (1u << order[k])) return def_req_growc(order[k]);
    return GROWC_OK;
}

u8 sim_overlay_value(int ovl, int param, int x, int y)
{
    const Tile *t;
    int lot, v;
    if (!world_in(x, y)) return OVV_NONE;
    t = WT(x, y);
    lot = t->lot;
    switch (ovl) {
    case OVL_POWER:
        if (lot) {
            const BuildingDef *d = def_building(g_world.lots[lot].def);
            if (d->cat == BCAT_POWER) return OVV(OVR_NET, NETC_OK);
            if (d->flags & BDF_NO_CONDUCT) return OVV_NONE;
            return net_ovv(net_lot_power_state(lot));
        }
        if (t->road & TR_POWER_LINE) return OVV(OVR_NET, (t->zone & ZF_POWERED) ? NETC_CARRIER : NETC_NOT_CONNECTED);
        return OVV_NONE;
    case OVL_WATER:      /* F1 (WATER.md 3.6 A); param = a source lot: only its area is shown */
        if (lot) {
            const BuildingDef *d = def_building(g_world.lots[lot].def);
            if (d->cat == BCAT_WATER) return OVV(OVR_NET, net_source_output(lot) > 0 ? NETC_OK : NETC_SHORTAGE);
            if (!d->water_use && d->cat != BCAT_ZONE) return OVV_NONE;
            if (param && net_lot_water_src(lot, NULL) != param) return OVV_NONE;
            v = net_lot_water_state(lot);      /* F3: watered = blue like a wet pipe (WATER.md 3.6), not green */
            return OVV(OVR_NET, v == NLS_OK ? NETC_CARRIER : v == NLS_SHORT ? NETC_SHORTAGE : NETC_NOT_CONNECTED);
        }
        if (param && net_water_tile_src(x, y) != param) return OVV_NONE;
        if (t->flags & TF_PIPE) {
            v = net_water_tile_state(x, y);
            return OVV(OVR_NET, v == NLS_OK ? NETC_CARRIER : v == NLS_SHORT ? NETC_SHORTAGE : NETC_NOT_CONNECTED);
        }
        if (TER_IS_WATER(TER_TYPE(t)) && !TILE_ROAD(t)) {   /* M5: untreated sewage tints the lake (civic.h) */
            v = civic_lake_pollution(x, y);
            return v >= CIVIC_LAKE_TINT ? OVV(OVR_BAD, IMIN(7, 2 + v / 24)) : OVV_NONE;
        }
        if (TILE_ROAD(t)) return OVV_NONE;
        v = net_water_near(x, y);              /* wet ground: a new building here would get water */
        if (v == NLS_OK) return OVV(OVR_NET, NETC_WET);
        if (v == NLS_SHORT && TILE_ZONE(t)) return OVV(OVR_NET, NETC_SHORTAGE);
        return OVV_NONE;
    case OVL_POLLUTION:
        v = field_at(FLD_POLLUTION, x, y);
        return OVV(OVR_BAD, IMIN(7, v >> 4));                 /* 0..112+ -> 0..7 */
    case OVL_LAND_VALUE:
        if (TER_IS_WATER(TER_TYPE(t))) return OVV_NONE;
        return OVV(OVR_GOOD, IMIN(7, t->lv / 12));            /* 0..84+ -> 0..7 */
    case OVL_TRAFFIC:
        if (!TILE_ROAD(t)) return OVV_NONE;
        return OVV(OVR_BAD, traffic_flow(x, y) >> 5);
    case OVL_FIRE_RISK:
        if (TER_IS_WATER(TER_TYPE(t))) return OVV_NONE;
        return OVV(OVR_BAD, IMIN(7, field_at(FLD_FIRE_RISK, x, y) >> 4));
    case OVL_SVC_FIRE: case OVL_SVC_SCHOOL: case OVL_SVC_HEALTH: case OVL_SVC_SAUNA:
    case OVL_SVC_POLICE: case OVL_SVC_LIBRARY: case OVL_SVC_POST:
        v = net_cov_quality(ovl - OVL_SVC_FIRE, x, y);
        if (!v && !lot && !TILE_ZONE(t)) return OVV_NONE;
        return OVV(OVR_COVER, (v * 8) / 101);
    case OVL_GROWTH:
        if (lot) {
            const Lot *l = &g_world.lots[lot];
            if (def_building(l->def)->cat != BCAT_ZONE) return OVV_NONE;
            return OVV(OVR_PROBLEM, main_problem(l->problems));
        }
        if (TILE_ZONE(t)) {
            if (net_road_reach(x, y) > ZONE_ROAD_REACH) return OVV(OVR_PROBLEM, GROWC_ROAD);
            if (TILE_ZONE(t) == ZONE_APARTMENTS) {
                if (net_power_near(x, y, x, y) != NLS_OK) return OVV(OVR_PROBLEM, GROWC_POWER);
                if (net_water_near(x, y) != NLS_OK) return OVV(OVR_PROBLEM, GROWC_WATER);
            }
            if (s_dem[ZONE_DEMAND(TILE_ZONE(t))].value <= 0) return OVV(OVR_PROBLEM, GROWC_OTHER);
            return OVV(OVR_PROBLEM, GROWC_OK);
        }
        return OVV_NONE;
    case OVL_ZONES:
        if (TILE_ZONE(t)) return OVV(OVR_ZONE, TILE_ZONE(t));
        if (lot && def_building(g_world.lots[lot].def)->cat == BCAT_ZONE) return OVV(OVR_ZONE, def_building(g_world.lots[lot].def)->zone);
        return OVV_NONE;
    case OVL_HEAT:                                    /* M5 */
        return civic_overlay_value(ovl, param, x, y);
    case OVL_CATCHMENT: {
        const Lot *pl = world_lot(param);
        const BuildingDef *d;
        int kind;
        if (!pl) return OVV_NONE;
        if (lot == param) return OVV(OVR_COVER, 7);
        d = def_building(pl->def);
        kind = d->cat == BCAT_SERVICE ? d->service : (d->cat == BCAT_ZONE && d->zone == ZONE_COMMERCIAL) ? COV_SHOP : -1;
        if (kind < 0 || net_cov_lot(kind, x, y) != param) return OVV_NONE;
        return OVV(OVR_COVER, IMAX(1, net_cov_quality(kind, x, y) * 8 / 101));
    }
    default:
        return OVV_NONE;
    }
}

/* ------------------------------------------------------------------ history, hash */
int sim_history(int series, s32 *out, int max)
{
    int n, i, start;
    if (series < 0 || series >= HIST_COUNT) return 0;
    n = IMIN(max, s_hist_n);
    start = (s_hist_pos - n + SIM_HISTORY) % SIM_HISTORY;
    for (i = 0; i < n; i++) out[i] = s_hist[series][(start + i) % SIM_HISTORY];
    return n;
}

u32 sim_hash(void)
{
    u32 h = world_hash();
    int k;
    h = hash_bytes(h, &s_date, sizeof(s_date));
    h = hash_bytes(h, &s_rng, sizeof(s_rng));
    for (k = 0; k < DEM_COUNT; k++) {
        h = hash_bytes(h, &s_dem[k].value, sizeof(s32));
        h = hash_bytes(h, &s_dem[k].target, sizeof(s32));
        h = hash_bytes(h, &s_dem[k].smoothed, sizeof(s32));
    }
    h = hash_bytes(h, &g_budget.money, sizeof(Money));
    h = hash_bytes(h, &s_stats.population, sizeof(s32));
    h = hash_bytes(h, &s_stats.emigrated_total, sizeof(s32));
    h = hash_bytes(h, &s_homeless, sizeof(s32));
    h = hash_bytes(h, field_data(FLD_POLLUTION), FIELD_MAX_W * FIELD_MAX_H);
    h = hash_bytes(h, field_data(FLD_LAND_VALUE), FIELD_MAX_W * FIELD_MAX_H);
    if (politics_enabled()) h ^= politics_hash();   /* M1: off = the hash as before */
    h ^= industry_hash();                          /* M3 */
    h ^= complex_hash();                           /* M6: 0 without a complex */
    h ^= civic_hash();                             /* M5 */
    h ^= era_hash();                               /* M7: era + trade */
    return h;
}

/* ------------------------------------------------------------------ stat log + report */
const PhaseTiming *sim_phase_timing(int phase) { return &s_phase[phase & (SIM_STEPS_PER_CYCLE - 1)]; }
const PhaseTiming *sim_phase_timing_total(int phase) { return &s_phase_tot[phase & (SIM_STEPS_PER_CYCLE - 1)]; }
void sim_phase_timing_total_reset(void) { memset(s_phase_tot, 0, sizeof(s_phase_tot)); memset(s_phase_tot_sum, 0, sizeof(s_phase_tot_sum)); }

static const char *s_dt_names[DT_COUNT] = { "open_jobs", "migration", "attract", "vacant", "emigration",
    "spending", "com_cap", "clearing", "western", "local_need", "ind_cap", "tax", "policy", "regional", "freight" };

static s32 term_of(const Demand *d, int id)
{
    int k;
    for (k = 0; k < d->nterms; k++) if (d->terms[k].id == id) return d->terms[k].amount;
    return 0;
}

int sim_statlog_open(const char *path)
{
    int k, d;
    static const char *dn[DEM_COUNT] = { "R", "C", "I" };
    sim_statlog_close();
    s_statlog = fopen(path, "w");
    if (!s_statlog) { log_printf("sim: warning: cannot open stat log %s", path); return 0; }
    fprintf(s_statlog, "year,month,pop,jobs,workers,unemployed,homeless,emig_m,emig_total,immig_m,"
                       "res_houses,res_apts,cap_res,vacant,fill_pct,lots_h1,lots_h2,lots_h3,lots_a1,lots_c1,lots_c2,"
                       "lots_i1,lots_i2,building,abandoned,pwr_sup,pwr_dem,pwr_short,wat_sup,wat_dem,wat_short,"
                       "fire_load,fire_cap,school_load,school_cap,health_load,health_cap,sauna_load,sauna_cap,"
                       "avg_lv,avg_pol,avg_happy,avg_fire,avg_commute,status,money,loan,net");
    for (d = 0; d < DEM_COUNT; d++) {
        fprintf(s_statlog, ",dem%s,tgt%s,smo%s", dn[d], dn[d], dn[d]);
        for (k = 0; k < DT_COUNT; k++) fprintf(s_statlog, ",%s_%s", dn[d], s_dt_names[k]);
    }
    for (k = 0; k < BL_COUNT; k++) fprintf(s_statlog, ",bl%d", k);
    for (k = 0; k < SIM_STEPS_PER_CYCLE; k++) fprintf(s_statlog, ",p%d_avg,p%d_max", k, k);
    fprintf(s_statlog, "\n");
    fflush(s_statlog);
    return 1;
}

void sim_statlog_close(void) { if (s_statlog) { fclose(s_statlog); s_statlog = NULL; } }

static void statlog_row(void)
{
    const CityStats *st = &s_stats;
    int k, d;
    if (!s_statlog) return;
    fprintf(s_statlog, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,"
                       "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
            s_date.year, s_date.month + 1, st->population, st->jobs, st->workers, st->unemployed, st->homeless,
            st->emigrated_month, st->emigrated_total, st->immigrated_month,
            st->residents_zone[ZONE_HOUSES], st->residents_zone[ZONE_APARTMENTS], st->capacity_res, st->vacant,
            st->jobs_fill_pct,
            st->lots_tier[ZONE_HOUSES][1], st->lots_tier[ZONE_HOUSES][2], st->lots_tier[ZONE_HOUSES][3],
            st->lots_tier[ZONE_APARTMENTS][1], st->lots_tier[ZONE_COMMERCIAL][1], st->lots_tier[ZONE_COMMERCIAL][2],
            st->lots_tier[ZONE_INDUSTRY][1], st->lots_tier[ZONE_INDUSTRY][2], st->lots_building, st->lots_abandoned,
            st->power_supply, st->power_demand, st->power_short_lots, st->water_supply, st->water_demand,
            st->water_short_lots,
            st->svc_load[SVC_FIRE], st->svc_capacity[SVC_FIRE], st->svc_load[SVC_SCHOOL], st->svc_capacity[SVC_SCHOOL],
            st->svc_load[SVC_HEALTH], st->svc_capacity[SVC_HEALTH], st->svc_load[SVC_SAUNA], st->svc_capacity[SVC_SAUNA],
            st->avg_land_value, st->avg_pollution, st->avg_happiness, st->avg_fire_risk, st->avg_commute,
            st->town_status, g_budget.money, g_budget.loan, budget_net_last());
    for (d = 0; d < DEM_COUNT; d++) {
        fprintf(s_statlog, ",%d,%d,%d", s_dem[d].value, s_dem[d].target, s_dem[d].smoothed);
        for (k = 0; k < DT_COUNT; k++) fprintf(s_statlog, ",%d", term_of(&s_dem[d], k));
    }
    for (k = 0; k < BL_COUNT; k++) fprintf(s_statlog, ",%d", g_budget.last[k]);
    for (k = 0; k < SIM_STEPS_PER_CYCLE; k++) fprintf(s_statlog, ",%.3f,%.3f", s_phase[k].avg_ms, s_phase[k].max_ms);
    fprintf(s_statlog, "\n");
    fflush(s_statlog);
}

void sim_dump_report(FILE *f)
{
    const CityStats *st = &s_stats;
    int k, d;
    static const char *dn[DEM_COUNT] = { "R", "C", "I" };
    fprintf(f, "=== %s, %d-%02d (step %u) status %s era %s\n", s_set.town, s_date.year, s_date.month + 1,
            (unsigned)s_date.total_steps, g_statuses[st->town_status].name, g_eras[st->era].name);
    fprintf(f, "population %d (houses %d, apartments %d), homes %d (vacant %d), homeless %d\n", st->population,
            st->residents_zone[ZONE_HOUSES], st->residents_zone[ZONE_APARTMENTS], st->capacity_res, st->vacant,
            st->homeless);
    fprintf(f, "jobs %d (com %d lots, ind %d lots), workers %d, unemployed %d, jobs filled %d%%\n", st->jobs,
            st->lots_tier[ZONE_COMMERCIAL][1] + st->lots_tier[ZONE_COMMERCIAL][2],
            st->lots_tier[ZONE_INDUSTRY][1] + st->lots_tier[ZONE_INDUSTRY][2], st->workers, st->unemployed,
            st->jobs_fill_pct);
    fprintf(f, "migration: pull %d, immigrated %d this month, emigrated to Sweden %d (total %d)\n",
            st->migration_pull, st->immigrated_month, st->emigrated_month, st->emigrated_total);
    fprintf(f, "lots: houses T1/T2/T3 %d/%d/%d, apartments T1 %d, commercial T1/T2 %d/%d, industry T1/T2 %d/%d, "
               "building %d, abandoned %d\n",
            st->lots_tier[ZONE_HOUSES][1], st->lots_tier[ZONE_HOUSES][2], st->lots_tier[ZONE_HOUSES][3],
            st->lots_tier[ZONE_APARTMENTS][1], st->lots_tier[ZONE_COMMERCIAL][1], st->lots_tier[ZONE_COMMERCIAL][2],
            st->lots_tier[ZONE_INDUSTRY][1], st->lots_tier[ZONE_INDUSTRY][2], st->lots_building, st->lots_abandoned);
    fprintf(f, "power %d/%d (short lots %d), water %d/%d (short lots %d)\n", st->power_demand, st->power_supply,
            st->power_short_lots, st->water_demand, st->water_supply, st->water_short_lots);
    for (k = 0; k < SVC_COUNT; k++)
        if (st->svc_capacity[k]) fprintf(f, "service %s: load %d / capacity %d\n", g_services[k].name, st->svc_load[k], st->svc_capacity[k]);
    fprintf(f, "averages (residents): land value %d, pollution %d, happiness %d, fire risk %d, commute %d\n",
            st->avg_land_value, st->avg_pollution, st->avg_happiness, st->avg_fire_risk, st->avg_commute);
    for (d = 0; d < DEM_COUNT; d++) {
        fprintf(f, "demand %s: bar %d, smoothed %d, target %d =", dn[d], s_dem[d].value, s_dem[d].smoothed, s_dem[d].target);
        for (k = 0; k < s_dem[d].nterms; k++) fprintf(f, " %s %+d", s_dt_names[s_dem[d].terms[k].id], s_dem[d].terms[k].amount);
        fprintf(f, "\n");
    }
    fprintf(f, "money %d mk, loan %d, last month net %+d:", g_budget.money, g_budget.loan, budget_net_last());
    for (k = 0; k < BL_COUNT; k++) if (g_budget.last[k]) fprintf(f, " bl%d %+d", k, g_budget.last[k]);
    fprintf(f, "\n");
    for (k = 0; k < s_nproblems; k++)
        fprintf(f, "problem %d: req %d, %d lots, %d residents (near %d,%d)\n", k + 1, s_problems[k].req,
                s_problems[k].count, s_problems[k].residents, s_problems[k].x, s_problems[k].y);
}

/* ------------------------------------------------------------------ save */
/* M3: arrays whose length grows when ids are appended (zones, building defs): the saved count is read, the
   rest stays zero, extra saved entries are skipped. n_now / n_saved entries of `elem` bytes. */
static void load_arr(struct SaveFile *sf, void *dst, int n_now, int n_saved, int elem)
{
    u8 skip[64];
    int k;
    n_saved = ICLAMP(n_saved, 0, 4096);
    load_bytes(sf, dst, IMIN(n_now, n_saved) * elem);
    for (k = n_now * elem; k < n_saved * elem; k += (int)sizeof(skip))
        load_bytes(sf, skip, IMIN((int)sizeof(skip), n_saved * elem - k));
}

/* M3: CityStats has [ZONE_COUNT] arrays in the middle: written with its zone count (SIMX v2) */
static void stats_load(struct SaveFile *sf, int nz)
{
    int pre = (int)offsetof(CityStats, residents_zone);
    int post0 = (int)(offsetof(CityStats, lots_tier) + sizeof(s_stats.lots_tier));
    memset(&s_stats, 0, sizeof(s_stats));
    load_bytes(sf, &s_stats, pre);
    load_arr(sf, s_stats.residents_zone, ZONE_COUNT, nz, (int)sizeof(s32));
    load_arr(sf, s_stats.jobs_zone, ZONE_COUNT, nz, (int)sizeof(s32));
    load_arr(sf, s_stats.lots_tier, ZONE_COUNT, nz, (int)sizeof(s_stats.lots_tier[0]));
    load_bytes(sf, (u8 *)&s_stats + post0, (int)sizeof(s_stats) - post0);
}

int sim_save(struct SaveFile *sf)
{
    int k, i;
    save_begin_chunk(sf, "SIM ", 2);              /* M3: v2 = zone count before s_first_built */
    save_bytes(sf, s_set.president, NAME_LEN);
    save_bytes(sf, s_set.town, NAME_LEN);
    save_s32(sf, s_set.start_year); save_s32(sf, s_set.funds); save_s32(sf, s_set.politics);
    save_s32(sf, s_set.disasters); save_s32(sf, s_set.all_unlocked); save_u32(sf, s_set.seed);
    save_s32(sf, s_date.year); save_s32(sf, s_date.month); save_s32(sf, s_date.cycle); save_s32(sf, s_date.step);
    save_u32(sf, s_date.total_steps); save_u32(sf, s_date.total_months);
    save_u32(sf, s_rng.s);
    for (k = 0; k < DEM_COUNT; k++) { save_s32(sf, s_dem[k].value); save_s32(sf, s_dem[k].target); save_s32(sf, s_dem[k].smoothed); }
    save_s32(sf, s_homeless); save_s32(sf, s_status_max); save_s32(sf, s_stats.emigrated_total);
    save_s32(sf, s_milestone); save_s32(sf, s_emig_cursor); save_s32(sf, s_month_pending);
    save_u32(sf, ZONE_COUNT);
    save_bytes(sf, s_first_built, sizeof(s_first_built));
    save_s32(sf, s_hist_n); save_s32(sf, s_hist_pos);
    for (k = 0; k < HIST_COUNT; k++) for (i = 0; i < SIM_HISTORY; i++) save_s32(sf, s_hist[k][i]);
    save_end_chunk(sf);
    return 1;
}

int sim_load(struct SaveFile *sf)
{
    int ver, k, i;
    if (!load_find_chunk(sf, "SIM ", &ver)) return 0;
    load_bytes(sf, s_set.president, NAME_LEN); s_set.president[NAME_LEN - 1] = 0;
    load_bytes(sf, s_set.town, NAME_LEN); s_set.town[NAME_LEN - 1] = 0;
    s_set.start_year = load_s32(sf); s_set.funds = load_s32(sf); s_set.politics = load_s32(sf);
    s_set.disasters = load_s32(sf); s_set.all_unlocked = load_s32(sf); s_set.seed = load_u32(sf);
    s_date.year = load_s32(sf); s_date.month = load_s32(sf); s_date.cycle = load_s32(sf); s_date.step = load_s32(sf);
    s_date.total_steps = load_u32(sf); s_date.total_months = load_u32(sf);
    s_rng.s = load_u32(sf);
    for (k = 0; k < DEM_COUNT; k++) { s_dem[k].value = load_s32(sf); s_dem[k].target = load_s32(sf); s_dem[k].smoothed = load_s32(sf); }
    s_homeless = load_s32(sf); s_status_max = load_s32(sf); s_stats.emigrated_total = load_s32(sf);
    s_milestone = load_s32(sf); s_emig_cursor = load_s32(sf); s_month_pending = load_s32(sf);
    memset(s_first_built, 0, sizeof(s_first_built));
    load_arr(sf, s_first_built, ZONE_COUNT, ver >= 2 ? (int)load_u32(sf) : 5, TIER_MAX + 1);   /* M3: v1 had 5 zones */
    s_hist_n = load_s32(sf); s_hist_pos = load_s32(sf);
    for (k = 0; k < HIST_COUNT; k++) for (i = 0; i < SIM_HISTORY; i++) s_hist[k][i] = load_s32(sf);
    s_hist_n = ICLAMP(s_hist_n, 0, SIM_HISTORY); s_hist_pos = ICLAMP(s_hist_pos, 0, SIM_HISTORY - 1);
    s_res_track = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d = def_building(l->def);
        if (l->def && d->cat == BCAT_ZONE && is_res_zone(d->zone) && l->status == LOT_ACTIVE) s_res_track += l->occupants;
    }
    s_ev_head = s_ev_count = 0; s_backlog = 0;
    snapshot_unlocks(0);
    return load_ok(sf);
}
/* ------------------------------------------------------------------ T10: save
   Chunk "SIMX": per-cycle and aggregate state that sim_save (chunk "SIM ") leaves out but the next steps
   read (demand allowance/terms, city stats, move-in budget, growth band progress, unlock snapshot,
   warnings, problems, aggregate caches). Needed for identical continuation after load. Fixed-layout
   structs (Demand, CityStats, SimProblem: s32/int/u8 members only) are written as bytes. */
int sim_save_ext(struct SaveFile *sf)
{
    save_begin_chunk(sf, "SIMX", 2);              /* M3: v2 = zone and building-def counts first */
    save_u32(sf, ZONE_COUNT); save_u32(sf, B_COUNT);
    save_bytes(sf, s_dem, sizeof(s_dem));
    save_bytes(sf, &s_stats, sizeof(s_stats));
    save_s32(sf, s_movein); save_s32(sf, s_movein_homeless); save_s32(sf, s_res_track);
    save_s32(sf, s_bands_done); save_bytes(sf, s_band_allow, sizeof(s_band_allow)); save_s32(sf, s_band_movein);
    save_bytes(sf, s_unl_b, sizeof(s_unl_b)); save_bytes(sf, s_unl_r, sizeof(s_unl_r)); save_bytes(sf, s_unl_z, sizeof(s_unl_z));
    save_s32(sf, s_lot_events_month); save_s32(sf, s_warn_power); save_s32(sf, s_warn_water);
    save_bytes(sf, s_warn_svc, sizeof(s_warn_svc));
    save_bytes(sf, s_problems, sizeof(s_problems)); save_s32(sf, s_nproblems);
    save_s32(sf, s_emigrated_cycle_acc); save_s32(sf, s_immigrated_acc);
    save_s32(sf, s_cap_res); save_s32(sf, s_cap_com); save_s32(sf, s_cap_ind); save_s32(sf, s_jobs_other); save_s32(sf, s_tier_sum); save_s32(sf, s_res_lots);
    save_end_chunk(sf);
    return 1;
}
int sim_load_ext(struct SaveFile *sf)
{
    int ver, nz = 5, nb = 48;                     /* M3: SIMX v1 = 5 zones, 48 building defs */
    if (!load_find_chunk(sf, "SIMX", &ver)) return 0;
    if (ver >= 2) { nz = (int)load_u32(sf); nb = (int)load_u32(sf); }
    load_bytes(sf, s_dem, sizeof(s_dem));
    stats_load(sf, nz);
    s_movein = load_s32(sf); s_movein_homeless = load_s32(sf); s_res_track = load_s32(sf);
    s_bands_done = load_s32(sf); load_bytes(sf, s_band_allow, sizeof(s_band_allow)); s_band_movein = load_s32(sf);
    memset(s_unl_b, 0, sizeof(s_unl_b)); memset(s_unl_z, 0, sizeof(s_unl_z));
    load_arr(sf, s_unl_b, B_COUNT, nb, 1); load_bytes(sf, s_unl_r, sizeof(s_unl_r)); load_arr(sf, s_unl_z, ZONE_COUNT, nz, 1);
    s_lot_events_month = load_s32(sf); s_warn_power = load_s32(sf); s_warn_water = load_s32(sf);
    load_bytes(sf, s_warn_svc, sizeof(s_warn_svc));
    load_bytes(sf, s_problems, sizeof(s_problems)); s_nproblems = load_s32(sf);
    s_emigrated_cycle_acc = load_s32(sf); s_immigrated_acc = load_s32(sf);
    s_cap_res = load_s32(sf); s_cap_com = load_s32(sf); s_cap_ind = load_s32(sf); s_jobs_other = load_s32(sf); s_tier_sum = load_s32(sf); s_res_lots = load_s32(sf);
    return load_ok(sf);
}
