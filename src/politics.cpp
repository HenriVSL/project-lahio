/* politics.cpp - blocs, popularity, authority, decrees, letters, event cards, elections, meters.
   PORTABLE, deterministic, integer-only. Owner: M1. See politics.h for the model; tables in poltab.cpp.

   Player actions (sign/repeal/letter/choose) are called by game.cpp between simulation steps (like tool
   commits), politics_month by the sim at month end. Everything random uses the politics RNG (seeded from
   the city seed, saved), so the city simulation's own RNG stream is never touched. */
#include "common.h"
#include "politics.h"
#include "civic.h"
#include "industry.h"
#include "sim.h"
#include "world.h"
#include "budget.h"
#include "text.h"
#include "save.h"
#include "util.h"
#include "era.h"           /* M7: town rights by decree */
#include "trade.h"         /* PL2: a waiting agreement offer has the next card slot */

#define TIMED_MAX  24
#define MSG_RING   16
#define AUTH_START 100            /* tenths */

typedef struct { u8 kind; s8 target; s16 value; s16 months; } Timed;   /* FX_BLOC / FX_PM with months left */

static struct {
    int   enabled;
    Rng   rng;
    s32   approval[BLOC_COUNT], target[BLOC_COUNT], weight[BLOC_COUNT], members[BLOC_COUNT];
    int   nreason[BLOC_COUNT];
    Term  reason[BLOC_COUNT][POL_REASONS];
    s32   popularity;
    s32   meter[METER_COUNT];
    s32   auth10;
    int   term_mod;
    u8    active[DEC_COUNT], hook_on[DEC_COUNT];
    s16   cooldown[DEC_COUNT];
    s16   active_months[DEC_COUNT];
    int   decree_gap, letter_gap;
    Timed timed[TIMED_MAX];
    int   ntimed;
    int   pending, pending_age, event_next, campaign_done;
    int   card_since;                       /* P1: months since the last card (not the campaign) left the desk */
    u8    ev_count[EV_COUNT];
    int   election_year, campaign_bonus, skip_from, elections_held;
    PolElection last;
    s32   pop_hist[12], popu_hist[12];
    int   hist_pos, hist_n;
    s32   forest0;
    int   frost_warned, angry_warned[BLOC_COUNT], auth_full_warned;
    s32   mods[PM_COUNT];
} P;

static PolEventHook s_ev_hook;              /* M6 (politics_set_event_hook) */
static PolMsg s_msg[MSG_RING];
static int s_msg_head, s_msg_n;

static void push_msg(int type, int a, int b)
{
    int idx;
    if (s_msg_n == MSG_RING) { s_msg_head = (s_msg_head + 1) % MSG_RING; s_msg_n--; }
    idx = (s_msg_head + s_msg_n) % MSG_RING;
    s_msg[idx].type = (u8)type; s_msg[idx].a = (s16)a; s_msg[idx].b = (s16)b;
    s_msg_n++;
}

int politics_poll_msg(PolMsg *m)
{
    if (!s_msg_n) return 0;
    *m = s_msg[s_msg_head];
    s_msg_head = (s_msg_head + 1) % MSG_RING;
    s_msg_n--;
    return 1;
}

static int pm_is_add(int pm) { return pm == PM_HAPPY_ADD || pm == PM_APT_SCORE_ADD; }
static int month_index(void) { return sim_date()->year * 12 + sim_date()->month; }

/* ------------------------------------------------------------------ modifiers */
static void mod_apply(s32 *m, int pm, int v)
{
    if (pm < 0 || pm >= PM_COUNT) return;
    if (pm_is_add(pm)) m[pm] += v;
    else m[pm] = (s32)((s64)m[pm] * (100 + v) / 100);
}

static void recompute_mods(void)
{
    int k, d, i;
    for (k = 0; k < PM_COUNT; k++) P.mods[k] = pm_is_add(k) ? 0 : 100;
    if (!P.enabled) return;
    for (d = 0; d < DEC_COUNT; d++) {
        if (!P.active[d]) continue;
        for (i = 0; i < POL_FX_MAX; i++) {
            const PolFx *f = &g_decrees[d].active[i];
            if (f->kind == FX_PM) mod_apply(P.mods, f->target, f->value);
        }
    }
    for (i = 0; i < P.ntimed; i++) if (P.timed[i].kind == FX_PM) mod_apply(P.mods, P.timed[i].target, P.timed[i].value);
    /* meters: Eastern relations scale the clearing quota and plant/pump costs, Western trade the Western orders */
    P.mods[PM_CLEARING_PCT] = P.mods[PM_CLEARING_PCT] * (150 + P.meter[METER_EAST]) / 200;
    P.mods[PM_WESTERN_PCT] = P.mods[PM_WESTERN_PCT] * (150 + P.meter[METER_WEST]) / 200;
    P.mods[PM_UTILITY_PCT] = P.mods[PM_UTILITY_PCT] * (200 + 50 - P.meter[METER_EAST]) / 200;
    for (k = 0; k < PM_COUNT; k++) if (!pm_is_add(k)) P.mods[k] = IMAX(10, P.mods[k]);
}

int politics_mod(int pm)
{
    if (pm < 0 || pm >= PM_COUNT) return 100;
    if (!P.enabled) return pm_is_add(pm) ? 0 : 100;
    return P.mods[pm];
}

/* ------------------------------------------------------------------ lifecycle */
static s32 count_forest(void)
{
    int x, y;
    s32 n = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) if (TER_TYPE(WT(x, y)) == TER_FOREST && !WT(x, y)->lot) n++;
    return n;
}

static int first_election(int start_year)
{
    int y = start_year + 2;
    while ((y - 1956) % POL_TERM_YEARS) y++;
    return y;
}

void politics_init(void) { memset(&P, 0, sizeof(P)); s_msg_head = s_msg_n = 0; recompute_mods(); }

void politics_reset(int enabled)
{
    const SimSettings *ss = sim_settings();
    int k;
    memset(&P, 0, sizeof(P));
    s_msg_head = s_msg_n = 0;
    P.enabled = enabled ? 1 : 0;
    rng_seed(&P.rng, ss->seed * 2654435761u ^ 0x5eedf00du);
    for (k = 0; k < BLOC_COUNT; k++) { P.approval[k] = P.target[k] = 50; P.weight[k] = 250; }
    P.popularity = 50;
    P.meter[METER_EAST] = 60;
    P.meter[METER_WEST] = 40;
    P.auth10 = AUTH_START;
    P.pending = -1;
    P.event_next = 9 + rng_below(&P.rng, 8);
    P.card_since = POL_EVENT_MIN;             /* P1: no card yet: the gap is open */
    P.election_year = first_election(ss->start_year);
    P.forest0 = count_forest();
    recompute_mods();
}

int politics_enabled(void) { return P.enabled; }

/* ------------------------------------------------------------------ blocs */
static void reason(int b, int pr, s32 v)
{
    int k;
    if (!v) return;
    for (k = 0; k < P.nreason[b]; k++) if (P.reason[b][k].id == pr) { P.reason[b][k].amount += v; return; }
    if (P.nreason[b] < POL_REASONS) { P.reason[b][P.nreason[b]].id = (u8)pr; P.reason[b][P.nreason[b]].amount = v; P.nreason[b]++; }
}

/* service satisfaction 0..100: capacity / load over the given services (100 = everyone served) */
static int svc_sat(const CityStats *st, const int *svc, int n)
{
    int k, sum = 0, cnt = 0;
    for (k = 0; k < n; k++) {
        s32 load = st->svc_load[svc[k]], cap = st->svc_capacity[svc[k]];
        if (load <= 0) continue;
        sum += cap >= load ? 100 : (int)((s64)cap * 100 / load);
        cnt++;
    }
    return cnt ? sum / cnt : 100;
}

static int s_ci_lots, s_ci_dark;     /* shop/industry lots, of them without power (members_pass) */

static void members_pass(s32 *mem, int *culture)
{
    int i, b;
    s32 share[BLOC_COUNT];
    for (b = 0; b < BLOC_COUNT; b++) mem[b] = 0;
    *culture = 0;
    s_ci_lots = s_ci_dark = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        if (l->def == B_NONE) continue;
        d = def_building(l->def);
        if (d->cat != BCAT_ZONE) {
            if (l->status == LOT_ACTIVE && (l->def == B_PARK || l->def == B_LIBRARY || l->def == B_CHURCH ||
                l->def == B_SPORTS_FIELD || l->def == B_ICE_RINK)) (*culture)++;
            continue;
        }
        if (l->status != LOT_ACTIVE || !l->occupants) continue;
        if (d->zone == ZONE_FARM || d->zone == ZONE_FORESTRY) {      /* M3: farm families, forest workers */
            mem[BLOC_RURAL] += l->occupants;
            continue;
        }
        if (d->zone == ZONE_COMMERCIAL || d->zone == ZONE_INDUSTRY) {
            mem[BLOC_INDUSTRIALISTS] += l->occupants;
            s_ci_lots++;
            if (!(l->flags & LF_POWERED)) s_ci_dark++;
            continue;
        }
        share[BLOC_WORKERS] = share[BLOC_RURAL] = share[BLOC_INDUSTRIALISTS] = share[BLOC_INTELLIGENTSIA] = 0;
        if (d->zone == ZONE_HOUSES) {
            if (d->tier <= 1) { share[BLOC_RURAL] = 70; share[BLOC_WORKERS] = 30; }
            else if (d->tier == 2) { share[BLOC_RURAL] = 40; share[BLOC_WORKERS] = 45; share[BLOC_INTELLIGENTSIA] = 15; }
            else { share[BLOC_RURAL] = 20; share[BLOC_WORKERS] = 40; share[BLOC_INTELLIGENTSIA] = 30; share[BLOC_INDUSTRIALISTS] = 10; }
        } else {
            if (d->tier <= 1) { share[BLOC_WORKERS] = 85; share[BLOC_INTELLIGENTSIA] = 15; }
            else { share[BLOC_WORKERS] = 70; share[BLOC_INTELLIGENTSIA] = 25; share[BLOC_INDUSTRIALISTS] = 5; }
        }
        if (WT(l->x, l->y)->lv >= 70) {               /* good addresses lean to the Intelligentsia */
            int mv = IMIN(15, share[BLOC_WORKERS]);
            share[BLOC_WORKERS] -= mv; share[BLOC_INTELLIGENTSIA] += mv;
        }
        for (b = 0; b < BLOC_COUNT; b++) mem[b] += l->occupants * share[b] / 100;
    }
    mem[BLOC_INDUSTRIALISTS] /= 8;                    /* owners and managers: one per eight jobs */
    {   /* the countryside around the town votes too, less every year (urbanisation) */
        int y = sim_date()->year;
        mem[BLOC_RURAL] += y < 1990 ? 600 * (1990 - y) / 35 : 0;
    }
    for (b = 0; b < BLOC_COUNT; b++) mem[b] = IMAX(10, mem[b]);
}

static void compute_blocs(void)
{
    static const int svc_all[3] = { SVC_SCHOOL, SVC_HEALTH, SVC_SAUNA };
    static const int svc_edu[1] = { SVC_SCHOOL };
    const CityStats *st = sim_stats();
    s32 pop = st->population, total = 0, apt_share, sat, sat_edu, forest, lost, growth, pop12;
    int b, i, d, culture, east = P.meter[METER_EAST], west = P.meter[METER_WEST];
    members_pass(P.members, &culture);
    for (b = 0; b < BLOC_COUNT; b++) total += P.members[b];
    for (b = 0; b < BLOC_COUNT; b++) { P.weight[b] = (s32)((s64)P.members[b] * 1000 / IMAX(1, total)); P.nreason[b] = 0; }
    apt_share = pop > 0 ? st->residents_zone[ZONE_APARTMENTS] * 100 / pop : 0;
    sat = svc_sat(st, svc_all, 3);
    sat_edu = svc_sat(st, svc_edu, 1);       /* libraries count as culture (built ones only) */
    forest = count_forest();
    lost = P.forest0 > 0 ? IMAX(0, (P.forest0 - forest) * 100 / P.forest0) : 0;
    pop12 = P.hist_n >= 12 ? P.pop_hist[P.hist_pos] : (P.hist_n ? P.pop_hist[0] : pop);
    growth = (pop - pop12) * 100 / IMAX(500, pop12);

    /* Workers (Ty\xf6v\xe4ki): jobs, homes, emigration, resident tax, services, new housing */
    reason(BLOC_WORKERS, PR_BASE, 50);
    if (pop >= 200) {
        s32 u = st->unemployed * 100 / IMAX(1, st->workers);
        if (u <= 2) reason(BLOC_WORKERS, PR_JOBS, 6);
        else reason(BLOC_WORKERS, PR_UNEMPLOYMENT, -IMIN(30, u * 2));
        reason(BLOC_WORKERS, PR_HOMELESS, -IMIN(15, st->homeless * 1000 / pop / 5));
        reason(BLOC_WORKERS, PR_EMIGRATION, -IMIN(15, st->emigrated_month * 1000 / pop / 2));
    }
    reason(BLOC_WORKERS, PR_TAX_RES, (TAX_NEUTRAL - g_budget.tax_res) * 2);
    if (pop >= 300) reason(BLOC_WORKERS, PR_SERVICES, (sat - 70) / 4);
    reason(BLOC_WORKERS, PR_HOUSING, IMIN(6, apt_share / 10));
    reason(BLOC_WORKERS, PR_POLLUTION, -st->avg_pollution / 10);
    reason(BLOC_WORKERS, PR_EAST, (east - 50) / 10);

    /* Rural (Maalaiset): forest, slow growth, low taxes, no concrete */
    reason(BLOC_RURAL, PR_BASE, 55);
    reason(BLOC_RURAL, PR_FOREST, -IMIN(20, lost / 2) + industry_rural_forest_reason());   /* M3: managed forest +, clear-cuts - */
    if (growth > 10) reason(BLOC_RURAL, PR_GROWTH, -IMIN(15, (growth - 10) / 2));
    reason(BLOC_RURAL, PR_TAX_RES, TAX_NEUTRAL - g_budget.tax_res);
    if (pop >= 300) reason(BLOC_RURAL, PR_SERVICES, (sat - 70) / 6);
    if (apt_share > 30) reason(BLOC_RURAL, PR_CONCRETE, -(apt_share - 30) / 5);
    reason(BLOC_RURAL, PR_EAST, (east - 50) / 8);
    reason(BLOC_RURAL, PR_POLLUTION, -IMIN(10, civic_lake_reason() / 6));   /* M5: sewage in the fishing lakes */

    /* Industrialists (Patruunat): business tax, power, export orders, roads, labour */
    reason(BLOC_INDUSTRIALISTS, PR_BASE, 50);
    reason(BLOC_INDUSTRIALISTS, PR_TAX_BUS, (TAX_NEUTRAL - g_budget.tax_bus) * 3);
    if (st->power_short_lots > 0 || s_ci_dark * 10 > s_ci_lots) reason(BLOC_INDUSTRIALISTS, PR_POWER, -15);
    else if (st->power_demand > 0 && st->power_supply * 10 >= st->power_demand * 12) reason(BLOC_INDUSTRIALISTS, PR_POWER, 4);
    reason(BLOC_INDUSTRIALISTS, PR_EXPORTS, sim_demand(DEM_I)->value / 8);
    if (st->avg_commute > 15) reason(BLOC_INDUSTRIALISTS, PR_COMMUTE, -IMIN(10, (st->avg_commute - 15) / 2));
    if (st->jobs > 0 && st->jobs_fill_pct < 80) reason(BLOC_INDUSTRIALISTS, PR_LABOUR, -IMIN(15, (80 - st->jobs_fill_pct) / 2));
    reason(BLOC_INDUSTRIALISTS, PR_WEST, (west - 50) / 6);

    /* Intelligentsia (Sivistyneist\xf6): schools, culture, land value, clean air, the West */
    reason(BLOC_INTELLIGENTSIA, PR_BASE, 45);
    reason(BLOC_INTELLIGENTSIA, PR_SERVICES, (sat_edu - 70) / 3);
    if (culture) reason(BLOC_INTELLIGENTSIA, PR_CULTURE, IMIN(12, 1 + culture * 4000 / IMAX(1000, pop)));
    reason(BLOC_INTELLIGENTSIA, PR_LAND_VALUE, (st->avg_land_value - 45) / 4);
    reason(BLOC_INTELLIGENTSIA, PR_POLLUTION, -st->avg_pollution / 5 - IMIN(12, civic_lake_reason() / 4));   /* M5: lakes */
    if (apt_share > 40) reason(BLOC_INTELLIGENTSIA, PR_CONCRETE, -(apt_share - 40) / 4);
    reason(BLOC_INTELLIGENTSIA, PR_WEST, (west - 50) / 5);
    if (east > 75) reason(BLOC_INTELLIGENTSIA, PR_EAST, -(east - 75) / 3);

    /* decrees in force and recent events */
    for (d = 0; d < DEC_COUNT; d++) {
        if (!P.active[d]) continue;
        for (i = 0; i < POL_FX_MAX; i++) {
            const PolFx *f = &g_decrees[d].active[i];
            if (f->kind == FX_BLOC && f->target >= 0 && f->target < BLOC_COUNT) reason(f->target, PR_DECREES, f->value);
        }
    }
    for (i = 0; i < P.ntimed; i++)
        if (P.timed[i].kind == FX_BLOC && P.timed[i].target >= 0 && P.timed[i].target < BLOC_COUNT)
            reason(P.timed[i].target, PR_EVENTS, P.timed[i].value);

    for (b = 0; b < BLOC_COUNT; b++) {
        s32 sum = 0, diff;
        int k;
        for (k = 0; k < P.nreason[b]; k++) sum += P.reason[b][k].amount;
        P.target[b] = ICLAMP(sum, 0, 100);
        diff = P.target[b] - P.approval[b];
        if (diff > 0) P.approval[b] += IMAX(1, diff / 3);
        else if (diff < 0) P.approval[b] -= IMAX(1, -diff / 3);
    }
    {
        s32 s = 0;
        for (b = 0; b < BLOC_COUNT; b++) s += P.approval[b] * P.weight[b];
        P.popularity = ICLAMP(s / 1000, 0, 100);
    }
}

/* ------------------------------------------------------------------ effects */
static void auth_add10(int d10) { P.auth10 = ICLAMP(P.auth10 + d10, 0, politics_authority_cap() * 10); }

static void add_timed(int kind, int target, int value, int months)
{
    Timed *t;
    if (P.ntimed >= TIMED_MAX) {             /* full: the oldest-ending gives way */
        int i, best = 0;
        for (i = 1; i < P.ntimed; i++) if (P.timed[i].months < P.timed[best].months) best = i;
        P.timed[best] = P.timed[--P.ntimed];
    }
    t = &P.timed[P.ntimed++];
    t->kind = (u8)kind; t->target = (s8)target; t->value = (s16)value; t->months = (s16)months;
}

static void do_repeal(int dec, int free_of_charge);

static void apply_fx(const PolFx *f)
{
    int i, n;
    switch (f->kind) {
    case FX_BLOC:
        if (f->months) add_timed(FX_BLOC, f->target, f->value, f->months);
        break;
    case FX_PM:
        if (f->months) add_timed(FX_PM, f->target, f->value, f->months);
        break;
    case FX_METER:
        politics_meter_add(f->target, f->value);
        break;
    case FX_AUTH:
        auth_add10(f->value * 10);
        break;
    case FX_MONEY:
        budget_spend(-(Money)f->value * 10, BL_POLITICS);
        break;
    case FX_MONEY_POP:
        budget_spend(-(Money)((s64)f->value * IMAX(300, sim_stats()->population) / 100), BL_POLITICS);
        break;
    case FX_ELECTION:
        P.campaign_bonus += f->value;
        break;
    case FX_SKIP_ELECTION:
        P.skip_from = P.election_year;
        P.election_year += POL_TERM_YEARS;
        P.campaign_done = 0;
        break;
    case FX_REPEAL:
        if (f->target >= 0 && f->target < DEC_COUNT && P.active[f->target]) do_repeal(f->target, 1);
        break;
    case FX_FASTTRACK:
        for (i = 1, n = 0; i < g_world.lot_hi && n < f->value; i++) {
            Lot *l = &g_world.lots[i];
            if (l->def == B_NONE || l->status != LOT_BUILDING) continue;
            world_lot_set_status(i, LOT_ACTIVE);
            n++;
        }
        break;
    case FX_SOFTEN: {
        int best = -1;
        for (i = 0; i < P.ntimed; i++)
            if (P.timed[i].kind == FX_BLOC && P.timed[i].value < 0 && (best < 0 || P.timed[i].value < P.timed[best].value)) best = i;
        if (best >= 0) P.timed[best].value = (s16)(P.timed[best].value / 2);
        break; }
    }
}

static void apply_list(const PolFx *list)
{
    int i;
    for (i = 0; i < POL_FX_MAX; i++) if (list[i].kind != FX_NONE) apply_fx(&list[i]);
}

/* ------------------------------------------------------------------ getters */
int politics_popularity(void) { return P.enabled ? P.popularity : 50; }
int politics_approval(int bloc) { return (P.enabled && bloc >= 0 && bloc < BLOC_COUNT) ? P.approval[bloc] : 50; }
int politics_approval_target(int bloc) { return (P.enabled && bloc >= 0 && bloc < BLOC_COUNT) ? P.target[bloc] : 50; }
int politics_weight(int bloc) { return (bloc >= 0 && bloc < BLOC_COUNT) ? P.weight[bloc] : 0; }
s32 politics_members(int bloc) { return (bloc >= 0 && bloc < BLOC_COUNT) ? P.members[bloc] : 0; }
int politics_reasons(int bloc, Term *out, int max)
{
    int n;
    if (bloc < 0 || bloc >= BLOC_COUNT) return 0;
    n = IMIN(max, P.nreason[bloc]);
    if (n > 0) memcpy(out, P.reason[bloc], n * sizeof(Term));
    return n;
}
int politics_meter(int meter) { return (meter >= 0 && meter < METER_COUNT) ? P.meter[meter] : 50; }
int politics_authority(void) { return P.auth10 / 10; }
int politics_authority_cap(void) { return POL_AUTH_CAP + 5 * P.term_mod; }
int politics_authority_income10(void) { return P.enabled ? IMAX(0, P.popularity * 10 / diff()->auth_div + 10 * P.term_mod) : 0; }
/* 1.1 D1: popularity / 20 per month on Easy (= the 1.0 popularity / 2 tenths), / 24 Normal, / 30 Hard */
int politics_term_mod(void) { return P.term_mod; }
int politics_next_election(void) { return P.election_year; }
int politics_months_to_election(void) { return P.election_year * 12 - (month_index() + 1); }
const PolElection *politics_last_election(void) { return P.elections_held || P.last.skipped ? &P.last : NULL; }
int politics_elections_held(void) { return P.elections_held; }
int politics_decree_active(int dec) { return dec >= 0 && dec < DEC_COUNT && P.active[dec]; }
int politics_decree_cooldown(int dec) { return (dec >= 0 && dec < DEC_COUNT) ? P.cooldown[dec] : 0; }
int politics_decree_gap(void) { return P.decree_gap; }
int politics_letter_gap(void) { return P.letter_gap; }
int politics_pending_event(void) { return P.enabled ? P.pending : -1; }
int politics_event_age(void) { return P.pending_age; }
int politics_event_count(int ev) { return (ev >= 0 && ev < EV_COUNT) ? P.ev_count[ev] : 0; }
int politics_months_to_event(void) { return P.event_next; }

int politics_mood(void)
{
    int b, lo = 100;
    if (!P.enabled) return PRES_MOOD_CONTENT;
    for (b = 0; b < BLOC_COUNT; b++) if (P.weight[b] >= 100) lo = IMIN(lo, P.approval[b]);
    if (P.popularity < 35 || P.meter[METER_EAST] < 20) return PRES_MOOD_WORRIED;
    if (lo < 30) return PRES_MOOD_STERN;
    if (P.popularity >= 66) return PRES_MOOD_DELIGHTED;
    return PRES_MOOD_CONTENT;
}

int politics_active_count(void)
{
    int d, n = 0;
    for (d = 0; d < DEC_COUNT; d++) if (P.active[d]) n++;
    return n;
}

void politics_meter_add(int meter, int delta)
{
    if (meter < 0 || meter >= METER_COUNT) return;
    P.meter[meter] = ICLAMP(P.meter[meter] + delta, 0, 100);
    recompute_mods();
}

void politics_authority_add(int delta) { auth_add10(delta * 10); }

void politics_status_ceremony(int ts)
{
    if (!P.enabled) return;
    auth_add10(20 + 10 * IMIN(ts, 3));     /* M7: called by era_status_granted for every grant (decree or automatic) */
}

/* ------------------------------------------------------------------ decrees */
int politics_decree_state(int dec)
{
    const DecreeDef *d;
    if (dec < 0 || dec >= DEC_COUNT) return DS_HIDDEN;
    d = &g_decrees[dec];
    if (!P.enabled) return DS_OFF;
    if ((d->flags & DF_HIDDEN) && !P.hook_on[dec]) return DS_HIDDEN;
    if (P.active[dec]) return DS_ACTIVE;
    if (sim_date()->year < d->year_from) return DS_TOO_EARLY;
    if (dec == DEC_EMERGENCY && P.skip_from && P.skip_from + POL_TERM_YEARS == P.election_year) return DS_ALREADY;
    if (P.cooldown[dec] > 0) return DS_COOLDOWN;
    if (P.decree_gap > 0 && dec != DEC_TOWN_RIGHTS) return DS_GAP;   /* M7: a petition, not policy: no gap */
    if (politics_authority() < d->cost) return DS_NO_AUTHORITY;
    if (!(d->flags & DF_ONESHOT) && politics_active_count() >= POL_SLOTS) return DS_NO_SLOT;
    return DS_OK;
}

int politics_repeal_state(int dec)
{
    if (!P.enabled) return DS_OFF;
    if (dec < 0 || dec >= DEC_COUNT || !P.active[dec]) return DS_HIDDEN;
    if (politics_authority() < (g_decrees[dec].cost + 1) / 2) return DS_NO_AUTHORITY;
    return DS_OK;
}

int politics_sign(int dec)
{
    const DecreeDef *d;
    if (politics_decree_state(dec) != DS_OK) return 0;
    d = &g_decrees[dec];
    auth_add10(-d->cost * 10);
    apply_list(d->sign);
    if (!(d->flags & DF_ONESHOT)) { P.active[dec] = 1; P.active_months[dec] = 0; }
    if (dec != DEC_TOWN_RIGHTS) {             /* M7: town rights start no cooldown and no decree gap */
        P.cooldown[dec] = POL_COOLDOWN;
        P.decree_gap = POL_DECREE_GAP;
    }
    recompute_mods();
    push_msg(PMSG_DECREE_SIGNED, dec, 0);
    log_printf("politics: decree signed: %s (authority %d)", d->key, politics_authority());
    if (dec == DEC_TOWN_RIGHTS) era_town_rights_signed();   /* M7: grants the status reached */
    return 1;
}

static void do_repeal(int dec, int free_of_charge)
{
    if (!free_of_charge) auth_add10(-((g_decrees[dec].cost + 1) / 2) * 10);
    P.active[dec] = 0;
    P.cooldown[dec] = POL_COOLDOWN;
    recompute_mods();
    push_msg(PMSG_DECREE_REPEALED, dec, 0);
}

int politics_repeal(int dec)
{
    if (politics_repeal_state(dec) != DS_OK) return 0;
    do_repeal(dec, 0);
    log_printf("politics: decree repealed: %s (authority %d)", g_decrees[dec].key, politics_authority());
    return 1;
}

void politics_enable_hook_decree(int dec, int on) { if (dec >= 0 && dec < DEC_COUNT) P.hook_on[dec] = (u8)(on ? 1 : 0); }

/* ------------------------------------------------------------------ letters */
int politics_letter_state(int let)
{
    if (!P.enabled) return DS_OFF;
    if (let < 0 || let >= LET_COUNT) return DS_HIDDEN;
    if (P.letter_gap > 0) return DS_GAP;
    if (politics_authority() < g_letters[let].cost) return DS_NO_AUTHORITY;
    return DS_OK;
}

int politics_send_letter(int let)
{
    if (politics_letter_state(let) != DS_OK) return 0;
    auth_add10(-g_letters[let].cost * 10);
    apply_list(g_letters[let].fx);
    P.letter_gap = POL_LETTER_GAP;
    recompute_mods();
    push_msg(PMSG_LETTER, let, 0);
    log_printf("politics: letter sent: %s", g_letters[let].key);
    return 1;
}

/* ------------------------------------------------------------------ events */
void politics_choice_fx(int ev, int choice, PolFx *out)
{
    int i;
    memset(out, 0, sizeof(PolFx) * POL_FX_MAX);
    if (ev < 0 || ev >= EV_COUNT || choice < 0 || choice >= g_events[ev].nch) return;
    memcpy(out, g_events[ev].ch[choice].fx, sizeof(PolFx) * POL_FX_MAX);
    if (ev == EV_SAUNA)                       /* diminishing returns: 12, 8, 6, 4.8 ... */
        for (i = 0; i < POL_FX_MAX; i++)
            if (out[i].kind == FX_METER && out[i].value > 0) out[i].value = (s16)(out[i].value * 2 / (2 + P.ev_count[ev]));
    if (diff()->card_neg_pct != 100)          /* 1.1 D1: negative card effects by level (money, approval, meters: Normal x115 %,
                                                 Hard x140 %); the card's choice text shows the scaled numbers (ui_pol.cpp) */
        for (i = 0; i < POL_FX_MAX; i++)
            if (out[i].value < 0 && (out[i].kind == FX_MONEY || out[i].kind == FX_MONEY_POP || out[i].kind == FX_BLOC ||
                                     out[i].kind == FX_METER))
                out[i].value = (s16)((s32)out[i].value * diff()->card_neg_pct / 100);
}

static void resolve_event(int choice, int expired)
{
    PolFx fx[POL_FX_MAX];
    int ev = P.pending;
    if (ev < 0) return;
    politics_choice_fx(ev, choice, fx);
    P.pending = -1;
    P.pending_age = 0;
    apply_list(fx);
    if (P.ev_count[ev] < 255) P.ev_count[ev]++;
    if (ev != EV_CAMPAIGN)          /* 1.1 D1: the gap by level (Easy POL_EVENT_MIN-MAX 12-24, Normal 12-20, Hard 12-16) */
        P.event_next = diff()->card_gap_min + rng_below(&P.rng, diff()->card_gap_max - diff()->card_gap_min + 1);
    if (ev != EV_CAMPAIGN) P.card_since = 0;                   /* P1: the shared card gap restarts */
    recompute_mods();
    push_msg(expired ? PMSG_EVENT_EXPIRED : PMSG_CHOICE, ev, choice);
    log_printf("politics: event %s answered: %d%s", g_events[ev].key, choice, expired ? " (expired)" : "");
    if (s_ev_hook) s_ev_hook(ev, choice);     /* M6: complex cards */
    if (ev == EV_AGREEMENT) era_card_answered(ev, choice);   /* M7: sign the clearing agreement */
}

void politics_set_event_hook(PolEventHook fn) { s_ev_hook = fn; }

int politics_choose(int choice)
{
    if (!P.enabled || P.pending < 0 || choice < 0 || choice >= g_events[P.pending].nch) return 0;
    resolve_event(choice, 0);
    return 1;
}

/* P1: the card rhythm, enforced HERE for every source (random cards, the patron's demand cards and risk events, the
   clearing agreement): a card goes up only when nothing is pending, at least POL_EVENT_MIN (12) months after the last
   card left the desk (P.card_since: the hard <= 1 card per game year cap of DESIGN 14.7), and not in the POL_CARD_QUIET
   months before an election. Random and patron cards also wait for the 12-24 month timer (event_next, reset whenever a
   card leaves the desk); the Delegation's agreement card (one per 5 years) only needs the 12-month gap, otherwise the
   timer would hand most offers to the Ministry's automatic Standard signature. The election campaign card is part of
   the election (like election night), queued in the quiet window, and does not restart the gap. */
#define POL_CARD_QUIET 6
static int card_gate(int ev)
{
    int mte;
    if (!P.enabled || P.pending >= 0) return 0;
    if (ev == EV_CAMPAIGN) return 1;
    if (P.card_since < POL_EVENT_MIN) return 0;
    mte = politics_months_to_election();
    if (mte >= 0 && mte <= POL_CARD_QUIET) return 0;
    if (ev != EV_AGREEMENT && trade_agreement()->offer) return 0;   /* PL2: the Delegation waits first in line (no random,
                                                                       patron or risk card takes the slot it is waiting for) */
    return ev == EV_AGREEMENT || P.event_next <= 0;
}
int politics_card_slot_free(void) { return card_gate(-1); }
int politics_agreement_slot_free(void) { return card_gate(EV_AGREEMENT); }

int politics_queue_event(int ev)
{
    if (!card_gate(ev)) return 0;
    return politics_force_event(ev);
}

int politics_force_event(int ev)
{
    if (!P.enabled || P.pending >= 0 || ev < 0 || ev >= EV_COUNT) return 0;
    P.pending = ev;
    P.pending_age = 0;
    push_msg(ev == EV_CAMPAIGN ? PMSG_CAMPAIGN : PMSG_EVENT, ev == EV_CAMPAIGN ? P.election_year : ev, 0);
    return 1;
}

static int event_weight(int ev)
{
    const CityStats *st = sim_stats();
    int y = sim_date()->year, east = P.meter[METER_EAST], n = P.ev_count[ev], w = 0;
    switch (ev) {
    case EV_NIGHT_FROST: w = east < 45 ? 6 : 0; break;
    case EV_NOTE:        w = (y >= 1961 && !n && east < 75) ? 3 : 0; break;
    case EV_STRIKE:      w = (st->population >= 500 && st->jobs_zone[ZONE_INDUSTRY] > 0 && P.approval[BLOC_WORKERS] < 45) ? 6 : 0; break;
    case EV_DELEGATION:  w = st->population >= 300 ? 3 : 0; break;
    case EV_STATUE:      w = (st->population >= 1500 && east >= 35 && !n) ? 3 : 0; break;
    case EV_SAUNA:       w = 3; break;
    case EV_OLYMPICS:    w = (st->population >= 5000 && y >= 1960 && !n) ? 3 : 0; break;
    case EV_TV:          w = (y >= 1957 && !n) ? 3 : 0; break;
    case EV_SCANDAL:     w = st->population >= 1000 ? 2 : 0; break;
    case EV_SWEDEN_BOAT: w = (st->population >= 500 && st->emigrated_month * 1000 >= st->population * 8) ? 6 : 0; break;
    case EV_FESTIVAL:    w = (y >= 1960 && !n && st->population >= 1000) ? 2 : 0; break;
    case EV_TEMPERANCE:  w = (P.active[DEC_BEER] && n < 2) ? 6 : 0; break;
    case EV_WINTER:      w = n < 3 ? 2 : 0; break;
    case EV_TRADE_FAIR:  w = (y >= 1960 && st->jobs_zone[ZONE_INDUSTRY] > 0) ? 2 : 0; break;
    case EV_FISHING:     w = politics_authority() < 12 ? 2 : 0; break;
    default:             w = 0; break;
    }
    if (w && n) w = IMAX(1, w * 2 / (2 + n));         /* variety: seen cards come up less often */
    return w;
}

static void maybe_fire_event(void)
{
    int ev, total = 0, r, w[EV_COUNT];
    if (P.pending >= 0) return;
    if (P.event_next > 0) { P.event_next--; return; }
    if (!politics_card_slot_free()) return;            /* P1: the campaign has the stage (POL_CARD_QUIET) */
    for (ev = 0; ev < EV_COUNT; ev++) { w[ev] = event_weight(ev); total += w[ev]; }
    if (!total) { P.event_next = 3; return; }
    r = rng_below(&P.rng, total);
    for (ev = 0; ev < EV_COUNT; ev++) { if (r < w[ev]) break; r -= w[ev]; }
    if (ev >= EV_COUNT) ev = EV_SAUNA;
    politics_queue_event(ev);
}

/* ------------------------------------------------------------------ elections */
static void hold_election(void)
{
    PolElection *e = &P.last;
    int b, popu12 = P.hist_n >= 12 ? P.popu_hist[P.hist_pos] : P.popularity, turnout = 0, share, el;
    memset(e, 0, sizeof(*e));
    e->year = P.election_year;
    for (b = 0; b < BLOC_COUNT; b++) {
        e->bloc_approval[b] = P.approval[b]; e->bloc_weight[b] = P.weight[b];
        if (P.approval[b] > 60) turnout += (P.approval[b] - 60) * P.weight[b] / 1000;   /* the content turn out */
        else if (P.approval[b] < 30) turnout -= (30 - P.approval[b]) * P.weight[b] / 2000;
    }
    e->momentum = P.popularity - popu12;
    e->campaign = P.campaign_bonus;
    e->turnout = turnout;
    share = P.popularity + e->momentum / 2 + P.campaign_bonus + turnout;
    e->share = share;
    el = 151 + ICLAMP((share - 40) * 109 / 35, 0, 129);
    e->electors = el;
    e->verdict = el < 170 ? -1 : el >= 230 ? 1 : 0;
    P.term_mod = e->verdict;
    e->auth_now = e->verdict > 0 ? 5 : e->verdict == 0 ? 2 : -3;
    auth_add10(e->auth_now * 10);
    P.elections_held++;
    P.election_year += POL_TERM_YEARS;
    P.campaign_bonus = 0;
    P.campaign_done = 0;
    push_msg(PMSG_ELECTION, el, e->verdict);
    log_printf("politics: election %d: %d electors (share %d, momentum %d, campaign %d, turnout %d)", e->year, el,
               share, e->momentum, e->campaign, turnout);
}

/* ------------------------------------------------------------------ month end */
static void book_money(void)
{
    s64 delta = 0, util;
    int d, i;
    const Money *L = g_budget.last;
    if (P.mods[PM_TAX_INCOME_PCT] != 100) delta += (s64)(L[BL_TAX_RES] + L[BL_TAX_BUS]) * (P.mods[PM_TAX_INCOME_PCT] - 100) / 100;
    if (P.mods[PM_SCHOOL_UPKEEP_PCT] != 100) delta += (s64)L[BL_SVC(SVC_SCHOOL)] * (P.mods[PM_SCHOOL_UPKEEP_PCT] - 100) / 100;
    if (P.mods[PM_CONSTRUCTION_PCT] != 100) delta += (s64)IMIN(0, L[BL_CONSTRUCTION]) * (P.mods[PM_CONSTRUCTION_PCT] - 100) / 100;
    if (P.mods[PM_EXPORT_PCT] != 100) delta += (s64)L[BL_EXPORT] * (P.mods[PM_EXPORT_PCT] - 100) / 100;
    for (d = 0; d < DEC_COUNT; d++) {
        if (!P.active[d]) continue;
        for (i = 0; i < POL_FX_MAX; i++)
            if (g_decrees[d].active[i].kind == FX_MONEY_M) delta += g_decrees[d].active[i].value;
    }
    if (delta) budget_spend(-(Money)delta, BL_POLITICS);
    util = P.mods[PM_UTILITY_PCT] != 100 ? (s64)L[BL_UTILITIES] * (P.mods[PM_UTILITY_PCT] - 100) / 100 : 0;
    if (util) budget_spend(-(Money)util, BL_UTILITIES);
}

void politics_month(void)
{
    int b, d, i, mte;
    if (!P.enabled) return;
    compute_blocs();
    /* history (momentum, growth) */
    P.pop_hist[P.hist_pos] = sim_stats()->population;
    P.popu_hist[P.hist_pos] = P.popularity;
    P.hist_pos = (P.hist_pos + 1) % 12;
    if (P.hist_n < 12) P.hist_n++;
    /* authority */
    {
        int was_full = P.auth10 >= politics_authority_cap() * 10;
        auth_add10(politics_authority_income10());
        if (!was_full && P.auth10 >= politics_authority_cap() * 10 && !P.auth_full_warned) { push_msg(PMSG_AUTH_FULL, 0, 0); P.auth_full_warned = 1; }
        if (P.auth10 < politics_authority_cap() * 10 - 50) P.auth_full_warned = 0;
    }
    book_money();
    /* decrees in force: monthly meter drift; meters drift back toward 50 every 4 months */
    for (d = 0; d < DEC_COUNT; d++) {
        if (!P.active[d]) continue;
        if (P.active_months[d] < 32000) P.active_months[d]++;
        for (i = 0; i < POL_FX_MAX; i++)
            if (g_decrees[d].active[i].kind == FX_METER_M) {
                int m = g_decrees[d].active[i].target;
                if (m >= 0 && m < METER_COUNT) P.meter[m] = ICLAMP(P.meter[m] + g_decrees[d].active[i].value, 0, 100);
            }
    }
    if ((sim_date()->total_months & 3) == 3)
        for (i = 0; i < METER_COUNT; i++) {   /* 1.1 D1: the neighbour's patience: Eastern drifts to 50 (Easy) / 45 / 40 */
            int tgt = i == METER_EAST ? diff()->east_target : 50;
            P.meter[i] += P.meter[i] < tgt ? 1 : P.meter[i] > tgt ? -1 : 0;
        }
    /* timed effects run out */
    for (i = P.ntimed - 1; i >= 0; i--) if (--P.timed[i].months <= 0) P.timed[i] = P.timed[--P.ntimed];
    for (d = 0; d < DEC_COUNT; d++) if (P.cooldown[d] > 0) P.cooldown[d]--;
    if (P.decree_gap > 0) P.decree_gap--;
    if (P.card_since < 1000) P.card_since++;      /* P1 */
    if (P.letter_gap > 0) P.letter_gap--;
    recompute_mods();
    /* event cards: waiting ones resolve themselves after a year */
    if (P.pending >= 0) {
        if (++P.pending_age >= POL_EVENT_EXPIRE) resolve_event(0, 1);
    } else maybe_fire_event();
    /* elections */
    mte = politics_months_to_election();
    if (P.skip_from && P.skip_from * 12 == month_index() + 1) {
        memset(&P.last, 0, sizeof(P.last));
        P.last.year = P.skip_from; P.last.skipped = 1;
        push_msg(PMSG_ELECTION_SKIPPED, P.skip_from, 0);
    }
    if (mte >= 1 && mte <= 3 && !P.campaign_done && P.pending < 0) { P.campaign_done = 1; politics_queue_event(EV_CAMPAIGN); }
    if (mte <= 0) {
        if (P.pending == EV_CAMPAIGN) resolve_event(0, 1);
        hold_election();
    }
    /* warnings */
    if (P.meter[METER_EAST] < 25 && !P.frost_warned) { push_msg(PMSG_FROST, P.meter[METER_EAST], 0); P.frost_warned = 1; }
    if (P.meter[METER_EAST] >= 35) P.frost_warned = 0;
    for (b = 0; b < BLOC_COUNT; b++) {
        if (P.approval[b] < 25 && P.weight[b] >= 100 && !P.angry_warned[b]) { push_msg(PMSG_BLOC_ANGRY, b, P.approval[b]); P.angry_warned[b] = 1; }
        if (P.approval[b] >= 35) P.angry_warned[b] = 0;
    }
}

/* ------------------------------------------------------------------ scripts and debug */
int politics_find_decree(const char *key)
{
    int i;
    for (i = 0; i < DEC_COUNT; i++) if (str_ieq(key, g_decrees[i].key)) return i;
    return -1;
}
int politics_find_letter(const char *key)
{
    int i;
    for (i = 0; i < LET_COUNT; i++) if (str_ieq(key, g_letters[i].key)) return i;
    return -1;
}
int politics_find_event(const char *key)
{
    int i;
    for (i = 0; i < EV_COUNT; i++) if (str_ieq(key, g_events[i].key)) return i;
    return -1;
}

int politics_script(char **tok, int n, FILE *out)
{
    const char *a = n >= 2 ? tok[1] : "";
    int ok = 0, id;
    if (str_ieq(a, "enable")) { politics_reset(1); ok = 1; }
    else if (str_ieq(a, "disable")) { politics_reset(0); ok = 1; }
    else if (str_ieq(a, "sign") && n >= 3) { id = politics_find_decree(tok[2]); ok = id >= 0 && politics_sign(id); }
    else if (str_ieq(a, "repeal") && n >= 3) { id = politics_find_decree(tok[2]); ok = id >= 0 && politics_repeal(id); }
    else if (str_ieq(a, "letter") && n >= 3) { id = politics_find_letter(tok[2]); ok = id >= 0 && politics_send_letter(id); }
    else if (str_ieq(a, "choose") && n >= 3) ok = politics_choose(atoi(tok[2]));
    else if (str_ieq(a, "event") && n >= 3) { id = politics_find_event(tok[2]); ok = id >= 0 && politics_force_event(id); }   /* P1: debug, bypasses the rhythm */
    else if (str_ieq(a, "meter") && n >= 4) { politics_meter_add(str_ieq(tok[2], "west") ? METER_WEST : METER_EAST, atoi(tok[3])); ok = 1; }
    else if (str_ieq(a, "auth") && n >= 3) { politics_authority_add(atoi(tok[2])); ok = 1; }
    else if (str_ieq(a, "dump")) { if (out) politics_dump(out); ok = 1; }
    else if (str_ieq(a, "election") && P.enabled) { hold_election(); ok = 1; }   /* debug/screenshots: hold one now */
    if (out) fprintf(out, "script: pol %s%s%s -> %s\n", a, n >= 3 ? " " : "", n >= 3 ? tok[2] : "", ok ? "ok" : "refused");
    return ok;
}

void politics_dump(FILE *f)
{
    int b, k, d, n;
    Term t[POL_REASONS];
    fprintf(f, "politics %s: popularity %d, authority %d/%d (+%d.%d/month), east %d, west %d, next election %d (%d months),"
               " term %+d, event %s (age %d, next in %d)\n", P.enabled ? "on" : "off", P.popularity, politics_authority(),
            politics_authority_cap(), politics_authority_income10() / 10, politics_authority_income10() % 10,
            P.meter[METER_EAST], P.meter[METER_WEST], P.election_year, politics_months_to_election(), P.term_mod,
            P.pending >= 0 ? g_events[P.pending].key : "-", P.pending_age, P.event_next);
    for (b = 0; b < BLOC_COUNT; b++) {
        fprintf(f, "  %-14s approval %3d (target %3d) weight %3d.%d%% members %6d:", politics_bloc_name(b), P.approval[b],
                P.target[b], P.weight[b] / 10, P.weight[b] % 10, P.members[b]);
        n = politics_reasons(b, t, POL_REASONS);
        for (k = 0; k < n; k++) fprintf(f, " %s %+d;", politics_reason_label(t[k].id), t[k].amount);
        fprintf(f, "\n");
    }
    fprintf(f, "  decrees:");
    for (d = 0; d < DEC_COUNT; d++) if (P.active[d]) fprintf(f, " %s(%dm)", g_decrees[d].key, P.active_months[d]);
    fprintf(f, "\n  mods:");
    for (k = 0; k < PM_COUNT; k++) if (P.mods[k] != (pm_is_add(k) ? 0 : 100)) fprintf(f, " %s=%d", politics_pm_label(k), P.mods[k]);
    fprintf(f, "\n");
}

u32 politics_hash(void)
{
    u32 h = HASH_INIT;
    h = hash_bytes(h, &P.enabled, sizeof(P.enabled));
    h = hash_bytes(h, &P.rng, sizeof(P.rng));
    h = hash_bytes(h, P.approval, sizeof(P.approval));
    h = hash_bytes(h, P.meter, sizeof(P.meter));
    h = hash_bytes(h, &P.auth10, sizeof(P.auth10));
    h = hash_bytes(h, P.active, sizeof(P.active));
    h = hash_bytes(h, P.cooldown, sizeof(P.cooldown));
    h = hash_bytes(h, &P.pending, sizeof(P.pending));
    h = hash_bytes(h, &P.event_next, sizeof(P.event_next));
    h = hash_bytes(h, &P.card_since, sizeof(P.card_since));
    h = hash_bytes(h, &P.election_year, sizeof(P.election_year));
    h = hash_bytes(h, P.mods, sizeof(P.mods));
    return h;
}

/* ------------------------------------------------------------------ save (chunk "POLI" v1) */
int politics_save(struct SaveFile *sf)
{
    int i, k;
    if (!P.enabled) return 1;                 /* politics off: no chunk (files identical to before) */
    save_begin_chunk(sf, "POLI", 2);          /* P1: v2 appends card_since */
    save_s32(sf, P.enabled);
    save_u32(sf, P.rng.s);
    for (k = 0; k < BLOC_COUNT; k++) { save_s32(sf, P.approval[k]); save_s32(sf, P.target[k]); save_s32(sf, P.weight[k]); save_s32(sf, P.members[k]); }
    save_s32(sf, P.popularity);
    for (k = 0; k < METER_COUNT; k++) save_s32(sf, P.meter[k]);
    save_s32(sf, P.auth10); save_s32(sf, P.term_mod);
    save_s32(sf, DEC_COUNT);
    for (k = 0; k < DEC_COUNT; k++) { save_u8(sf, P.active[k]); save_u8(sf, P.hook_on[k]); save_s32(sf, P.cooldown[k]); save_s32(sf, P.active_months[k]); }
    save_s32(sf, P.decree_gap); save_s32(sf, P.letter_gap);
    save_s32(sf, P.ntimed);
    for (i = 0; i < P.ntimed; i++) { save_u8(sf, P.timed[i].kind); save_s32(sf, P.timed[i].target); save_s32(sf, P.timed[i].value); save_s32(sf, P.timed[i].months); }
    save_s32(sf, P.pending); save_s32(sf, P.pending_age); save_s32(sf, P.event_next); save_s32(sf, P.campaign_done);
    save_s32(sf, EV_COUNT);
    for (k = 0; k < EV_COUNT; k++) save_u8(sf, P.ev_count[k]);
    save_s32(sf, P.election_year); save_s32(sf, P.campaign_bonus); save_s32(sf, P.skip_from); save_s32(sf, P.elections_held);
    save_s32(sf, P.last.year); save_s32(sf, P.last.electors); save_s32(sf, P.last.share); save_s32(sf, P.last.momentum);
    save_s32(sf, P.last.campaign); save_s32(sf, P.last.turnout); save_s32(sf, P.last.verdict); save_s32(sf, P.last.skipped);
    save_s32(sf, P.last.auth_now);
    for (k = 0; k < BLOC_COUNT; k++) { save_s32(sf, P.last.bloc_approval[k]); save_s32(sf, P.last.bloc_weight[k]); }
    for (k = 0; k < 12; k++) { save_s32(sf, P.pop_hist[k]); save_s32(sf, P.popu_hist[k]); }
    save_s32(sf, P.hist_pos); save_s32(sf, P.hist_n);
    save_s32(sf, P.forest0);
    save_s32(sf, P.frost_warned); save_s32(sf, P.auth_full_warned);
    for (k = 0; k < BLOC_COUNT; k++) save_s32(sf, P.angry_warned[k]);
    save_s32(sf, P.card_since);
    save_end_chunk(sf);
    return 1;
}

int politics_load(struct SaveFile *sf)
{
    int ver, i, k, n;
    if (!load_find_chunk(sf, "POLI", &ver)) {
        politics_reset(sim_settings()->politics);   /* old save or politics off: fresh state */
        return 1;
    }
    memset(&P, 0, sizeof(P));
    s_msg_head = s_msg_n = 0;
    P.enabled = load_s32(sf);
    P.rng.s = load_u32(sf);
    for (k = 0; k < BLOC_COUNT; k++) { P.approval[k] = load_s32(sf); P.target[k] = load_s32(sf); P.weight[k] = load_s32(sf); P.members[k] = load_s32(sf); }
    P.popularity = load_s32(sf);
    for (k = 0; k < METER_COUNT; k++) P.meter[k] = load_s32(sf);
    P.auth10 = load_s32(sf); P.term_mod = load_s32(sf);
    n = load_s32(sf);
    for (k = 0; k < n; k++) {
        u8 a = (u8)load_u8(sf), h = (u8)load_u8(sf);
        s32 c = load_s32(sf), m = load_s32(sf);
        if (k < DEC_COUNT) { P.active[k] = a; P.hook_on[k] = h; P.cooldown[k] = (s16)c; P.active_months[k] = (s16)m; }
    }
    P.decree_gap = load_s32(sf); P.letter_gap = load_s32(sf);
    n = load_s32(sf);                         /* (never pass a load_* call to ICLAMP: macro) */
    P.ntimed = ICLAMP(n, 0, TIMED_MAX);
    for (i = 0; i < P.ntimed; i++) {
        P.timed[i].kind = (u8)load_u8(sf); P.timed[i].target = (s8)load_s32(sf);
        P.timed[i].value = (s16)load_s32(sf); P.timed[i].months = (s16)load_s32(sf);
    }
    P.pending = load_s32(sf); P.pending_age = load_s32(sf); P.event_next = load_s32(sf); P.campaign_done = load_s32(sf);
    n = load_s32(sf);
    for (k = 0; k < n; k++) { u8 c = (u8)load_u8(sf); if (k < EV_COUNT) P.ev_count[k] = c; }
    if (P.pending >= EV_COUNT) P.pending = -1;
    P.election_year = load_s32(sf); P.campaign_bonus = load_s32(sf); P.skip_from = load_s32(sf); P.elections_held = load_s32(sf);
    P.last.year = load_s32(sf); P.last.electors = load_s32(sf); P.last.share = load_s32(sf); P.last.momentum = load_s32(sf);
    P.last.campaign = load_s32(sf); P.last.turnout = load_s32(sf); P.last.verdict = load_s32(sf); P.last.skipped = load_s32(sf);
    P.last.auth_now = load_s32(sf);
    for (k = 0; k < BLOC_COUNT; k++) { P.last.bloc_approval[k] = load_s32(sf); P.last.bloc_weight[k] = load_s32(sf); }
    for (k = 0; k < 12; k++) { P.pop_hist[k] = load_s32(sf); P.popu_hist[k] = load_s32(sf); }
    P.hist_pos = load_s32(sf); P.hist_n = load_s32(sf);
    P.forest0 = load_s32(sf);
    P.frost_warned = load_s32(sf); P.auth_full_warned = load_s32(sf);
    for (k = 0; k < BLOC_COUNT; k++) P.angry_warned[k] = load_s32(sf);
    P.card_since = ver >= 2 ? load_s32(sf) : POL_EVENT_MIN;   /* P1 */
    recompute_mods();
    return load_ok(sf);
}
