/* advice.cpp - PL1 guidance: the Cabinet's advice, hindsight + the 1990 verdict, progression/unlock sentences,
   building purposes, the power network summary. PORTABLE, deterministic, integer-only. Owner: PL1. See advice.h.
   Text: English with Latin-1 escapes, THEME 6.1 tone (the joke is the system: the Cabinet would never presume to
   advise the President, so it "respectfully observes"). */
#include "common.h"
#include "advice.h"
#include "sim.h"
#include "world.h"
#include "net.h"
#include "budget.h"
#include "politics.h"
#include "era.h"
#include "civic.h"
#include "complex.h"
#include "trade.h"
#include "text.h"
#include "save.h"
#include "util.h"

/* ------------------------------------------------------------------ hindsight state */
enum { HS_POWER, HS_WATER, HS_ELEMENTS, HS_TOWER, HS_HEAT, HS_DEBT, HS_RIGHTS, HS_SCHOOL, HS_JOBS, HS_COUNT };
static const u8 s_hs_weight[HS_COUNT] = { 10, 9, 8, 6, 5, 9, 7, 5, 6 };      /* per month of the missed chance */
static const u8 s_hs_min[HS_COUNT]    = {  4, 4, 6, 6, 8, 4, 6, 8, 6 };      /* months in a year before it counts */
static struct {
    s16 yr[HS_COUNT];            /* months this year */
    s32 yr_peak[HS_COUNT];       /* the biggest number behind it this year (residents, lots, mk) */
    s16 life[HS_COUNT];          /* months since the start */
    s32 life_peak[HS_COUNT];
    s16 debt_oil;                /* months in debt during the oil crisis years (1973-1975, 1979-1980) */
    s16 rights_wait;             /* months the town-rights decree has waited (now) */
    s16 special_year;            /* last year with a special edition */
    s16 last_hind_kind, last_hind_year;
    u8  power_near_latch, rights_near_done;   /* rights_near_done: highest status announced as "within reach" */
    s16 year;                    /* the year the yr[] tallies belong to */
} A;

/* ------------------------------------------------------------------ small helpers */
static const char *town(void) { return sim_settings()->town[0] ? sim_settings()->town : "the town"; }
static int cur_year(void) { return sim_date()->year; }
static int held_status(void) { return sim_stats()->town_status; }

static void fmt_i(char *b, int n, s32 v) { text_fmt_int(b, n, v); }
static void fmt_m(char *b, int n, Money v) { text_fmt_money(b, n, v, cur_year()); }

/* a table name in the middle of a sentence: "Water tower" -> "water tower", "TV tower" stays */
static void lc_name(char *dst, int size, const char *name)
{
    const char *p;
    str_copy(dst, name, size);
    if (dst[1] >= 'A' && dst[1] <= 'Z') return;                       /* "TV tower" */
    for (p = name; *p; p++) if (p[0] == ' ' && p[1] >= 'A' && p[1] <= 'Z') return;   /* "Valtion Metalli works" */
    if (dst[0] >= 'A' && dst[0] <= 'Z') dst[0] = (char)(dst[0] + 32);
}

int advice_rights_waiting(void)
{
    return politics_enabled() && era_status_eligible() > held_status();
}
int advice_rights_wait_months(void) { return A.rights_wait; }

/* apartment (or any residential) lots held back by one requirement: residents, lots, the biggest one's place */
static s32 blocked(int req, int apartments_only, int *nlots, s16 *px, s16 *py)
{
    int i, n = 0;
    s32 res = 0, best = -1;
    u32 bit = 1u << req;
    *px = *py = -1;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        if (l->def == B_NONE || !(l->problems & bit)) continue;
        d = &g_bdefs[l->def];
        if (d->cat != BCAT_ZONE) continue;
        if (apartments_only ? d->zone != ZONE_APARTMENTS : (d->zone != ZONE_APARTMENTS && d->zone != ZONE_HOUSES)) continue;
        n++; res += l->occupants;
        if ((s32)l->occupants > best) { best = l->occupants; *px = l->x; *py = l->y; }
    }
    if (nlots) *nlots = n;
    return res;
}

/* the first lot in the dark (power short) */
static void short_power_lot(s16 *px, s16 *py)
{
    int i;
    s32 best = -1;
    *px = *py = -1;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_NONE || net_lot_power_state(i) != NLS_SHORT) continue;
        if ((s32)l->occupants > best) { best = l->occupants; *px = l->x; *py = l->y; }
    }
}

static int svc_pct(int svc)
{
    const CityStats *cs = sim_stats();
    return cs->svc_capacity[svc] > 0 ? (int)((s64)cs->svc_load[svc] * 100 / cs->svc_capacity[svc]) : 0;
}

/* ------------------------------------------------------------------ the Cabinet's advice */
static int add(Advice *out, int n, int max, int kind, int go, s32 score, s16 x, s16 y, s32 a, s32 b, const char *text)
{
    Advice *v;
    int k;
    if (n >= max) return n;
    v = &out[n];
    memset(v, 0, sizeof(*v));
    v->kind = (u8)kind; v->go = (u8)go; v->score = score; v->x = x; v->y = y; v->a = a; v->b = b;
    str_copy(v->text, text, ADV_TEXT);
    for (k = n; k > 0 && out[k].score > out[k - 1].score; k--) { Advice t = out[k]; out[k] = out[k - 1]; out[k - 1] = t; }
    return n + 1;
}

const char *advice_kind_name(int kind)
{
    static const char *const nm[ADV_COUNT] = { "none", "power_short", "water_short", "money_debt", "rights_waiting",
        "money_falling", "power_near", "apt_elements", "apt_tower", "heat", "shops", "school", "health", "sauna", "jobs",
        "homes", "rights_near" };
    return (kind >= 0 && kind < ADV_COUNT) ? nm[kind] : "?";
}

static int collect_all(Advice *out, int max)
{
    const CityStats *cs = sim_stats();
    const NetStats *pw = net_stats(NET_POWER);
    char t[ADV_TEXT], a1[32], a2[32], a3[48];
    int n = 0, pct, lots, ts;
    s16 x, y;
    s32 res;
    Money net = budget_net_last();
    /* power */
    pct = pw->supply > 0 ? (int)((s64)pw->demand * 100 / pw->supply) : 0;
    if (pw->lots_short > 0) {
        short_power_lot(&x, &y);
        fmt_i(a1, sizeof(a1), pw->lots_short);
        if (pw->supply > 0)
            str_printf(t, sizeof(t), "Cabinet sources respectfully observe that the power works run at %d%% of capacity and %s "
                       "building%s sit%s in the dark. Naturally the President is already aware.", pct, a1,
                       pw->lots_short == 1 ? "" : "s", pw->lots_short == 1 ? "s" : "");
        else
            str_printf(t, sizeof(t), "Cabinet sources respectfully observe that %s building%s wait%s for power that no plant "
                       "supplies. Naturally the President is already aware.", a1, pw->lots_short == 1 ? "" : "s", pw->lots_short == 1 ? "s" : "");
        n = add(out, n, max, ADV_POWER_SHORT, AGO_POWER, 1000 + IMIN(pw->lots_short, 400), x, y, pct, pw->lots_short, t);
    } else if (pw->supply > 0 && pct >= 90) {
        str_printf(t, sizeof(t), "The Ministry of Trade and Industry notes, purely for the record, that the power works run at "
                   "%d%% of capacity. A new plant before the next cold snap would spare everyone an awkward speech.", pct);
        n = add(out, n, max, ADV_POWER_NEAR, AGO_POWER, 600 + pct, -1, -1, pct, pw->supply - pw->demand, t);
    }
    /* water */
    if (cs->water_short_lots > 0) {
        WaterSummary ws;
        const Lot *wl;
        net_water_summary(&ws);
        wl = world_lot(ws.worst_lot);
        x = wl ? wl->x : -1; y = wl ? wl->y : -1;
        fmt_i(a1, sizeof(a1), cs->water_short_lots);
        str_printf(t, sizeof(t), "The Waterworks Board reports %s building%s with dry taps; the pumps give all they have. It would "
                   "not dream of suggesting another pumping station, but has marked a spot on the map.", a1,
                   cs->water_short_lots == 1 ? "" : "s");
        n = add(out, n, max, ADV_WATER_SHORT, AGO_WATER, 950 + IMIN(cs->water_short_lots, 400), x, y, cs->water_short_lots, 0, t);
    }
    /* money */
    if (!g_budget.unlimited) {
        if (g_budget.money < 0 || g_budget.loan > 0) {
            fmt_m(a1, sizeof(a1), g_budget.money < 0 ? -g_budget.money : g_budget.loan);
            if (g_budget.money < 0)
                str_printf(t, sizeof(t), "The Treasury reports, with the greatest respect, that the town is %s in the red. The bank "
                           "manager has started sending Christmas cards. Taxes and upkeep are on the budget page.", a1);
            else
                str_printf(t, sizeof(t), "The Treasury reports, with the greatest respect, an emergency loan of %s, at a rate nobody "
                           "will mention. Taxes and upkeep are on the budget page.", a1);
            n = add(out, n, max, ADV_MONEY_DEBT, AGO_BUDGET, 900, -1, -1, g_budget.money, g_budget.loan, t);
        } else if (net < 0 && (s64)g_budget.money < (s64)(-net) * 12) {
            int months = (int)(g_budget.money / IMAX(1, -net));
            int when = cur_year() * 12 + sim_date()->month + months;
            fmt_m(a1, sizeof(a1), -net);
            str_printf(t, sizeof(t), "The Treasury observes that the town spends %s a month more than it earns; at this pace the "
                       "coffers are empty by %s %d. Taxes, upkeep or growth: the choice is, of course, the President's.", a1,
                       text_month_name(when % 12), when / 12);
            n = add(out, n, max, ADV_MONEY_FALLING, AGO_BUDGET, 700 + IMAX(0, 12 - months) * 10, -1, -1, net, months, t);
        }
    }
    /* town status */
    if (advice_rights_waiting()) {
        ts = era_status_eligible();
        if (A.rights_wait >= 2)
            str_printf(t, sizeof(t), "The %s rights decree has waited on the President's desk for %d months. The council has "
                       "polished the chain of office twice and would be honoured by a signature (\xa7 on Approval).",
                       text_status_name(ts), A.rights_wait);
        else
            str_printf(t, sizeof(t), "%s rights await the President's signature. The decree is on the desk (\xa7 on Approval); the "
                       "council has already ordered a larger flag.", text_status_name(ts));
        n = add(out, n, max, ADV_RIGHTS_WAITING, AGO_DESK, 880, -1, -1, ts, A.rights_wait, t);
    } else if ((ts = held_status() + 1) < TS_COUNT && era_status_eligible() < ts) {
        s32 need = g_statuses[ts].population, have = cs->population;
        if (have * 100 >= need * 85 && have < need) {
            fmt_i(a1, sizeof(a1), have); fmt_i(a2, sizeof(a2), need);
            str_printf(t, sizeof(t), "Statisticians count %s residents. %s rights come at %s, and the council has quietly measured "
                       "the town hall for a bigger flag.", a1, text_status_name(ts), a2);
            n = add(out, n, max, ADV_RIGHTS_NEAR, AGO_PEOPLE, 220 + (int)(have * 100 / IMAX(1, need)), -1, -1, have, need, t);
        }
    }
    /* apartment blocks held back (PLAYTEST 2 item 7: the user's 1971 town had 30 such blocks) */
    res = blocked(REQ_ELEMENTS, 1, &lots, &x, &y);
    if (res >= 100) {
        fmt_i(a1, sizeof(a1), res);
        if (!def_unlocked(B_ELEMENT_FACTORY, cur_year(), held_status(), sim_settings()->all_unlocked)) {
            advice_unlock_text(a3, sizeof(a3), g_bdefs[B_ELEMENT_FACTORY].status_req, g_bdefs[B_ELEMENT_FACTORY].era_year);
            if (a3[0] >= 'A' && a3[0] <= 'Z') a3[0] = (char)(a3[0] + 32);
            str_printf(t, sizeof(t), "The Housing Board reports %s residents in blocks that cannot grow without precast elements. "
                       "The element factory %s; the Board is counting the days.", a1, a3);
        } else if (net_element_factories() == 0)
            str_printf(t, sizeof(t), "The Housing Board reports %s residents in blocks that cannot grow without precast elements. "
                       "An element factory, it ventures, would do wonders.", a1);
        else
            str_printf(t, sizeof(t), "The Housing Board reports %s residents waiting for precast elements: the element factory "
                       "cannot keep up. A second one is, of course, the President's decision.", a1);
        n = add(out, n, max, ADV_APT_ELEMENTS, AGO_CAMERA, 400 + IMIN(res / 20, 400), x, y, res, lots, t);
    }
    res = blocked(REQ_WATER_TOWER, 1, &lots, &x, &y);
    if (res >= 100) {
        fmt_i(a1, sizeof(a1), res);
        str_printf(t, sizeof(t), "%s apartment residents wait for water pressure: no water tower within reach. The Housing Board "
                   "asks for a tower, and would prefer a tall one.", a1);
        n = add(out, n, max, ADV_APT_TOWER, AGO_WATER, 380 + IMIN(res / 20, 400), x, y, res, lots, t);
    }
    res = blocked(REQ_HEAT, 1, &lots, &x, &y);
    if (res >= 150) {
        fmt_i(a1, sizeof(a1), res);
        str_printf(t, sizeof(t), "%s residents in apartment blocks would move up to tier 3 if district heating reached them. The "
                   "Ministry suggests a heat plant on the water pipes, and has already ordered the coal.", a1);
        n = add(out, n, max, ADV_HEAT, AGO_HEAT, 350 + IMIN(res / 20, 400), x, y, res, lots, t);
    }
    /* demand */
    {
        const Demand *dc = sim_demand(DEM_C), *dr = sim_demand(DEM_R);
        if (dc->value >= 40 && dc->target > 0 && cs->population >= 200) {
            str_printf(t, sizeof(t), "The Chamber of Commerce reports queues at the co-op and bananas rationed by smile. The "
                       "housewives of %s would welcome more shops; commercial zoning, it notes, costs nothing.", town());
            n = add(out, n, max, ADV_SHOPS, AGO_DEMAND, 250 + dc->value * 2, -1, -1, dc->value, dc->target, t);
        }
        if (cs->homeless >= 50 && dr->value > 0) {
            fmt_i(a1, sizeof(a1), cs->homeless);
            str_printf(t, sizeof(t), "The Housing Board counts %s residents without a home, sleeping at relatives' and in the "
                       "sauna. It would welcome more residential zones.", a1);
            n = add(out, n, max, ADV_HOMES, AGO_DEMAND, 200 + IMIN(cs->homeless / 10, 300), -1, -1, cs->homeless, 0, t);
        }
    }
    /* services over capacity */
    pct = svc_pct(SVC_SCHOOL);
    if (pct > 100) {
        fmt_i(a1, sizeof(a1), cs->svc_load[SVC_SCHOOL]); fmt_i(a2, sizeof(a2), cs->svc_capacity[SVC_SCHOOL]);
        str_printf(t, sizeof(t), "The Board of Education counts %s pupils for %s desks. Lessons are held in shifts and, on "
                   "Thursdays, in the corridor.", a1, a2);
        n = add(out, n, max, ADV_SCHOOL, AGO_SERVICES, 300 + IMIN((pct - 100) * 5, 400), -1, -1, cs->svc_load[SVC_SCHOOL], cs->svc_capacity[SVC_SCHOOL], t);
    }
    pct = svc_pct(SVC_HEALTH);
    if (pct > 100) {
        fmt_i(a1, sizeof(a1), cs->svc_load[SVC_HEALTH]); fmt_i(a2, sizeof(a2), cs->svc_capacity[SVC_HEALTH]);
        str_printf(t, sizeof(t), "The Medical Board reports %s residents for clinics built for %s. It recommends patience, and "
                   "another clinic.", a1, a2);
        n = add(out, n, max, ADV_HEALTH, AGO_SERVICES, 280 + IMIN((pct - 100) * 5, 400), -1, -1, cs->svc_load[SVC_HEALTH], cs->svc_capacity[SVC_HEALTH], t);
    }
    pct = svc_pct(SVC_SAUNA);
    if (pct > 100) {
        fmt_i(a1, sizeof(a1), cs->svc_load[SVC_SAUNA]); fmt_i(a2, sizeof(a2), cs->svc_capacity[SVC_SAUNA]);
        str_printf(t, sizeof(t), "The public saunas serve %s bathers with benches for %s. The Sauna Society notes that the queue "
                   "is now longer than the l\xf6yly.", a1, a2);
        n = add(out, n, max, ADV_SAUNA, AGO_SERVICES, 270 + IMIN((pct - 100) * 5, 400), -1, -1, cs->svc_load[SVC_SAUNA], cs->svc_capacity[SVC_SAUNA], t);
    }
    /* work: unemployment sends people to Sweden */
    if (cs->workers > 0 && cs->unemployed * 100 >= cs->workers * 6 && cs->unemployed >= 40) {
        s32 gone = 0;
        const EraYear *r = era_years() > 0 ? era_year_row(era_years() - 1) : NULL;
        if (r) gone = r->emig_year;
        fmt_i(a1, sizeof(a1), cs->unemployed); fmt_i(a2, sizeof(a2), gone);
        if (gone > 0)
            str_printf(t, sizeof(t), "The Sweden boat leaves full: %s residents are without work, and %s left last year. Factories "
                       "and offices, the unions suggest, would keep them home.", a1, a2);
        else
            str_printf(t, sizeof(t), "%s residents are without work and have started reading Swedish newspapers. Factories and "
                       "offices, the unions suggest, would keep them home.", a1);
        n = add(out, n, max, ADV_JOBS, AGO_PEOPLE, 300 + IMIN((int)(cs->unemployed * 1000 / cs->workers), 400), -1, -1, cs->unemployed, gone, t);
    }
    return n;
}

int advice_collect(Advice *out, int max)
{
    Advice tmp[ADV_COUNT + 8];
    int n;
    if (max <= 0) return 0;
    n = collect_all(tmp, COUNTOF(tmp));
    if (n > max) n = max;
    memcpy(out, tmp, n * sizeof(Advice));
    return n;
}

int advice_find(int kind, Advice *out)
{
    Advice tmp[ADV_COUNT + 8];
    int n = collect_all(tmp, COUNTOF(tmp)), k;
    for (k = 0; k < n; k++) if (tmp[k].kind == kind) { if (out) *out = tmp[k]; return 1; }
    return 0;
}

/* urgent: in the dark / dry for 2+ months, or the treasury empty; never in December (the January paper follows),
   never twice in a game year */
int advice_special_due(Advice *main)
{
    Advice tmp[ADV_COUNT + 8];
    int n, k, m = sim_date()->month;
    if (A.special_year == cur_year() || m == 11 || sim_date()->total_months < 12) return 0;
    n = collect_all(tmp, COUNTOF(tmp));
    for (k = 0; k < n; k++) {
        const Advice *v = &tmp[k];
        int urgent = (v->kind == ADV_POWER_SHORT && v->b >= 5 && A.yr[HS_POWER] >= 2) ||
                     (v->kind == ADV_WATER_SHORT && v->a >= 5 && A.yr[HS_WATER] >= 2) ||
                     (v->kind == ADV_MONEY_DEBT && g_budget.money < 0 && A.yr[HS_DEBT] >= 2);
        if (!urgent) continue;
        if (main) *main = *v;
        A.special_year = (s16)cur_year();
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ hindsight */
void advice_reset(void)
{
    memset(&A, 0, sizeof(A));
    A.last_hind_kind = -1;
    A.year = (s16)cur_year();
    A.rights_near_done = (u8)held_status();
}

static void tally(int k, int on, s32 v)
{
    if (!on) return;
    A.yr[k]++; A.life[k]++;
    if (v > A.yr_peak[k]) A.yr_peak[k] = v;
    if (v > A.life_peak[k]) A.life_peak[k] = v;
}

void advice_month(void)
{
    const CityStats *cs = sim_stats();
    int lots, y = cur_year(), oil;
    s16 x, yy;
    s32 r;
    tally(HS_POWER, cs->power_short_lots > 0, cs->power_short_lots);
    tally(HS_WATER, cs->water_short_lots > 0, cs->water_short_lots);
    r = blocked(REQ_ELEMENTS, 1, &lots, &x, &yy);
    tally(HS_ELEMENTS, r >= 200 && net_element_factories() == 0, r);
    r = blocked(REQ_WATER_TOWER, 1, &lots, &x, &yy);
    tally(HS_TOWER, r >= 200, r);
    r = blocked(REQ_HEAT, 1, &lots, &x, &yy);
    tally(HS_HEAT, r >= 300, r);
    tally(HS_DEBT, !g_budget.unlimited && (g_budget.money < 0 || g_budget.loan > 0), g_budget.money < 0 ? -g_budget.money : g_budget.loan);
    oil = (y >= 1973 && y <= 1975) || (y >= 1979 && y <= 1980);
    if (oil && !g_budget.unlimited && (g_budget.money < 0 || g_budget.loan > 0)) A.debt_oil++;
    if (advice_rights_waiting()) A.rights_wait++; else A.rights_wait = 0;
    tally(HS_RIGHTS, A.rights_wait > 0, A.rights_wait);
    tally(HS_SCHOOL, svc_pct(SVC_SCHOOL) >= 110, cs->svc_load[SVC_SCHOOL] - cs->svc_capacity[SVC_SCHOOL]);
    tally(HS_JOBS, cs->workers > 0 && cs->unemployed * 100 >= cs->workers * 8 && cs->emigrated_month > 0, cs->unemployed);
}

void advice_year_end(void)
{
    memset(A.yr, 0, sizeof(A.yr));
    memset(A.yr_peak, 0, sizeof(A.yr_peak));
    A.year = (s16)(cur_year() + (sim_date()->month == 11 ? 1 : 0));
}

static int months_word(char *b, int size, int m)
{
    static const char *const w[13] = { "no", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
                                       "eleven", "twelve" };
    if (m >= 0 && m <= 12) return str_printf(b, size, "%s month%s", w[m], m == 1 ? "" : "s");
    return str_printf(b, size, "%d months", m);
}

static void hind_line(int k, int months, s32 peak, int year, char *head, int hs, char *body, int bs, int verdict)
{
    char a1[32], mw[32];
    fmt_i(a1, sizeof(a1), peak);
    months_word(mw, sizeof(mw), months);
    switch (k) {
    case HS_POWER:
        str_copy(head, "IN HINDSIGHT: THE CANDLES", hs);
        if (verdict) str_printf(body, bs, "That the lights went out for %d months in all, up to %s buildings at a time; a plant too few.", months, a1);
        else str_printf(body, bs, "With hindsight, one more power plant would have spared %s buildings %s in the dark in %d. "
                        "The President, naturally, had his reasons.", a1, mw, year);
        break;
    case HS_WATER:
        str_copy(head, "IN HINDSIGHT: THE TAPS", hs);
        if (verdict) str_printf(body, bs, "That the taps ran dry for %d months in all; the water carriers remember.", months);
        else str_printf(body, bs, "With hindsight, one more pumping station would have kept %s buildings in water through %s of "
                        "%d. The buckets have been returned to the fire station.", a1, mw, year);
        break;
    case HS_ELEMENTS:
        str_copy(head, "IN HINDSIGHT: THE ELEMENTS", hs);
        if (verdict) str_printf(body, bs, "That %s residents waited %d months for precast elements while the town had no element factory.", a1, months);
        else str_printf(body, bs, "With hindsight, an element factory would have let %s residents' blocks grow during %s of %d. "
                        "The cranes waited patiently; so did the residents.", a1, mw, year);
        break;
    case HS_TOWER:
        str_copy(head, "IN HINDSIGHT: THE PRESSURE", hs);
        if (verdict) str_printf(body, bs, "That apartment blocks waited %d months for a water tower, %s residents at the worst.", months, a1);
        else str_printf(body, bs, "With hindsight, a water tower would have given %s apartment residents their pressure, and "
                        "their tier, during %s of %d. Tall buildings like tall towers.", a1, mw, year);
        break;
    case HS_HEAT:
        str_copy(head, "IN HINDSIGHT: THE RADIATORS", hs);
        if (verdict) str_printf(body, bs, "That %s residents' blocks stayed below tier 3 for want of district heating, %d months in all.", a1, months);
        else str_printf(body, bs, "With hindsight, a district heat plant on the pipes would have lifted %s residents' blocks to "
                        "tier 3 in %d. The radiators were ready; the boiler was not.", a1, year);
        break;
    case HS_DEBT:
        str_copy(head, "IN HINDSIGHT: THE BOOKS", hs);
        if (verdict) {
            if (A.debt_oil >= 6) str_printf(body, bs, "That the town went into the oil crisis in debt, and stayed there %d months in all.", months);
            else str_printf(body, bs, "That the treasury spent %d months in the red; the bank manager sends his regards.", months);
        } else if (year >= 1973 && year <= 1975)
            str_printf(body, bs, "With hindsight, entering the oil crisis with %s of debt was not ideal. The Treasury has filed "
                       "the observation under \"obvious, now\".", a1);
        else str_printf(body, bs, "With hindsight, the town spent %s of %d in the red, %s at the worst. A tax point earlier, or "
                        "an upkeep less, and the bank manager would have stayed home.", mw, year, a1);
        break;
    case HS_RIGHTS:
        str_copy(head, "IN HINDSIGHT: THE DECREE", hs);
        if (verdict) str_printf(body, bs, "That the town-rights decree lay unsigned on the desk for %d months; the council has forgiven nothing.", months);
        else str_printf(body, bs, "With hindsight, the town-rights decree lay on the President's desk for %s of %d. The council "
                        "polished the chain of office every Friday, just in case.", mw, year);
        break;
    case HS_SCHOOL:
        str_copy(head, "IN HINDSIGHT: THE SCHOOL", hs);
        if (verdict) str_printf(body, bs, "That the schools ran over capacity for %d months; a generation learned in shifts.", months);
        else str_printf(body, bs, "With hindsight, another school would have saved %s pupils a year of lessons in shifts. They "
                        "learned patience instead, which is also on the curriculum.", a1);
        break;
    case HS_JOBS:
    default:
        str_copy(head, "IN HINDSIGHT: THE BOAT", hs);
        if (verdict) str_printf(body, bs, "That %d months of unemployment filled the Sweden boat; Gothenburg thanks the town.", months);
        else str_printf(body, bs, "With hindsight, a factory or two would have kept %s residents home in %d. Gothenburg sends "
                        "its thanks, and a postcard.", a1, year);
        break;
    }
}

int advice_hindsight(char *head, int hsize, char *body, int bsize)
{
    int k, best = -1;
    s32 sc, bsc = 0;
    head[0] = body[0] = 0;
    for (k = 0; k < HS_COUNT; k++) {
        if (A.yr[k] < s_hs_min[k]) continue;
        if (k == A.last_hind_kind && A.last_hind_year == A.year - 1) continue;   /* not the same lesson two years running */
        sc = (s32)A.yr[k] * s_hs_weight[k];
        if (sc > bsc) { bsc = sc; best = k; }
    }
    if (best < 0) return 0;
    hind_line(best, A.yr[best], A.yr_peak[best], A.year, head, hsize, body, bsize, 0);
    A.last_hind_kind = (s16)best; A.last_hind_year = A.year;
    return 1;
}

/* months with a power / water shortage from the era's yearly rows (they exist in saves older than the ADVC chunk) */
static int era_warn_months(int water)
{
    int i, m = 0;
    for (i = 0; i < era_years(); i++) { const EraYear *r = era_year_row(i); m += water ? r->water_warn : r->power_warn; }
    return m;
}

int advice_verdict(char lines[][120], int max)
{
    int k, j, n = 0, top[3] = { -1, -1, -1 };
    char head[48];
    if (era_warn_months(0) > A.life[HS_POWER]) A.life[HS_POWER] = (s16)era_warn_months(0);   /* old saves: the history knows */
    if (era_warn_months(1) > A.life[HS_WATER]) A.life[HS_WATER] = (s16)era_warn_months(1);
    if (A.life_peak[HS_POWER] < sim_stats()->power_short_lots) A.life_peak[HS_POWER] = sim_stats()->power_short_lots;
    for (k = 0; k < HS_COUNT; k++) {
        s32 sc = (s32)A.life[k] * s_hs_weight[k];
        if (A.life[k] < 12) continue;
        for (j = 0; j < 3; j++)
            if (top[j] < 0 || sc > (s32)A.life[top[j]] * s_hs_weight[top[j]]) { int m; for (m = 2; m > j; m--) top[m] = top[m - 1]; top[j] = k; break; }
    }
    for (j = 0; j < 3 && n < max; j++) {
        if (top[j] < 0) break;
        hind_line(top[j], A.life[top[j]], A.life_peak[top[j]], cur_year(), head, sizeof(head), lines[n], 120, 1);
        n++;
    }
    if (n < max && n < 2) {   /* a well-run town: the history books are short */
        if (A.life[HS_POWER] < 3 && sim_stats()->power_short_lots == 0)
            str_copy(lines[n++], "That the lights stayed on, which historians find suspicious but admirable.", 120);
        if (n < max && n < 2) str_copy(lines[n++], "That the President left the town better than he found it, and a little taller.", 120);
    }
    if (n < max && n < 3) {
        char a1[32];
        fmt_i(a1, sizeof(a1), sim_stats()->population);
        str_printf(lines[n++], 120, "That %s residents stayed, and most of them had a sauna.", a1);
    }
    return n;
}

int advice_power_near_due(int *pct)
{
    const NetStats *pw = net_stats(NET_POWER);
    int p = pw->supply > 0 ? (int)((s64)pw->demand * 100 / pw->supply) : 0;
    if (pct) *pct = p;
    if (pw->supply <= 0 || p < 80) { A.power_near_latch = 0; return 0; }
    if (p >= 90 && pw->lots_short == 0 && !A.power_near_latch) { A.power_near_latch = 1; return 1; }
    return 0;
}

int advice_rights_near_due(int *ts)
{
    int next = held_status() + 1;
    s32 need;
    if (next >= TS_COUNT || next <= A.rights_near_done || era_status_eligible() >= next) return 0;
    need = g_statuses[next].population;
    if (sim_stats()->population * 10 < need * 9) return 0;
    A.rights_near_done = (u8)next;
    if (ts) *ts = next;
    return 1;
}

/* ------------------------------------------------------------------ progression and unlocks */
int advice_progress(char *buf, int size, int *ts)
{
    int held = held_status(), next = held + 1;
    s32 have = sim_stats()->population, need;
    char a1[32], a2[32];
    if (ts) *ts = -1;
    if (advice_rights_waiting()) {
        if (ts) *ts = era_status_eligible();
        str_printf(buf, size, "%s rights await your signature, President", text_status_name(era_status_eligible()));
        return 100;
    }
    if (next >= TS_COUNT) { str_printf(buf, size, "%s: the top of the ladder", text_status_name(held)); return 100; }
    if (ts) *ts = next;
    need = g_statuses[next].population;
    fmt_i(a1, sizeof(a1), need); fmt_i(a2, sizeof(a2), have);
    str_printf(buf, size, "Next: %s at %s (you have %s)", text_status_name(next), a1, a2);
    return need > 0 ? ICLAMP((int)((s64)have * 100 / need), 0, 100) : 100;
}

int advice_unlock_text(char *buf, int size, int status_req, int era_year)
{
    int y = cur_year(), st = held_status(), need_s = status_req > st, need_y = era_year > y;
    char a1[32];
    if (size > 0) buf[0] = 0;
    if (!need_s && !need_y) return 0;
    fmt_i(a1, sizeof(a1), g_statuses[ICLAMP(status_req, 0, TS_COUNT - 1)].population);
    if (need_s && need_y) str_printf(buf, size, "Unlocks at %s (%s residents) and %d", text_status_name(status_req), a1, era_year);
    else if (need_s) str_printf(buf, size, "Unlocks at %s (%s residents)", text_status_name(status_req), a1);
    else str_printf(buf, size, "Unlocks in %d", era_year);
    if (need_s && advice_rights_waiting() && era_status_eligible() >= status_req)
        str_printf(buf + strlen(buf), size - (int)strlen(buf), " - sign the town rights decree");
    return (int)strlen(buf);
}

int advice_status_unlocks(int ts, char *buf, int size)
{
    char items[12][56], nm[48];
    int i, n = 0, k, len = 0, y = cur_year();
    for (i = 1; i < B_COUNT && n < 12; i++) {
        const BuildingDef *d = &g_bdefs[i];
        if (d->status_req != ts || !d->variants || d->cat == BCAT_ZONE) continue;
        if ((d->flags & BDF_MODULE) && i != B_PM_HALL && i != B_VM_HALL) continue;    /* complex pieces: the hall is the tool */
        if (i == B_BUS_STOP || i == B_PATRON_VILLA || i == B_ROW_HOUSES || i == B_SKI_JUMP || i == B_FRIENDSHIP_STATUE) continue;
        if (n) lc_name(nm, sizeof(nm), d->name); else str_copy(nm, d->name, sizeof(nm));
        if (d->era_year > y) str_printf(items[n++], 56, "%s (%d)", nm, d->era_year);
        else str_copy(items[n++], nm, 56);
    }
    for (i = 1; i < ROAD_CLASS_COUNT && n < 12; i++) {
        if (g_roads[i].status_req != ts || i == ROAD_RAIL) continue;
        if (n) lc_name(nm, sizeof(nm), g_roads[i].name); else str_copy(nm, g_roads[i].name, sizeof(nm));
        if (g_roads[i].era_year > y) str_printf(items[n++], 56, "%s (%d)", nm, g_roads[i].era_year);
        else str_copy(items[n++], nm, 56);
    }
    if (ts == TS_CHURCH_VILLAGE && n < 12) { str_copy(items[n], n ? "apartment zones" : "Apartment zones", 56); n++; }
    if (size > 0) buf[0] = 0;
    for (k = 0; k < n; k++) {
        const char *sep = k == 0 ? "" : k == n - 1 ? " and " : ", ";
        len += str_printf(buf + len, size - len, "%s%s", sep, items[k]);
        if (len >= size - 1) break;
    }
    return n;
}

/* the lowest apartment tier whose checklist has req (defs table; PL2 moves the gates): 0 = none */
static int apt_tier_needing(int req)
{
    int t, k, n;
    const TierReq *r;
    for (t = 1; t <= TIER_MAX; t++) {
        n = def_tier_reqs(ZONE_APARTMENTS, t, &r);
        for (k = 0; k < n; k++) if (r[k].req == req) return t;
    }
    return 0;
}

const char *advice_purpose(int def)
{
    static char buf[96];
    int t;
    switch (def) {
    case B_HEAT_PLANT:
        t = apt_tier_needing(REQ_HEAT);
        str_printf(buf, sizeof(buf), "Lets apartment blocks grow to tier %d (heat runs in the pipes)", t ? t : 3);
        return buf;
    case B_ELEMENT_FACTORY:
        t = apt_tier_needing(REQ_ELEMENTS);
        str_printf(buf, sizeof(buf), "Precast elements: apartment blocks need them for tier %d%s", t ? t : 3, t && t < TIER_MAX ? "+" : "");
        return buf;
    case B_WATER_TOWER:
        t = apt_tier_needing(REQ_WATER_TOWER);
        str_printf(buf, sizeof(buf), "Water pressure: apartment blocks nearby can reach tier %d%s", t ? t : 2, t && t < TIER_MAX ? "+" : "");
        return buf;
    case B_SEWAGE_WORKS:     return "Treats sewage: untreated, it spoils lakes and land value";
    case B_PUMP:             return "Water from a lake or river into the pipes; needs power";
    case B_WELL:             return "Groundwater: a small source that needs no power";
    case B_COAL_PLANT: case B_HYDRO_PLANT: case B_OIL_PLANT: case B_PEAT_PLANT: case B_NUCLEAR_PLANT:
                             return "Feeds every building its lines and neighbours touch";
    case B_SCHOOL:           return "Houses need a school for tier 2, apartments too";
    case B_SAUNA:            return "Houses need a public sauna for tier 2";
    case B_CLINIC: case B_HEALTH_CENTRE: return "Houses need health care for tier 3";
    case B_POST:             return "Telephones: houses need one for tier 3 (from 1960)";
    case B_POLICE:           return "Keeps crime down and land value up nearby";
    case B_STATION:          return "Migrants by train, freight orders for industry";
    case B_BUS_DEPOT:        return "Bus lines run only from a depot";
    }
    return NULL;
}

/* ------------------------------------------------------------------ power network summary
   A flood over the tiles the power solver marked ZF_POWER_NET, from the plant's tiles (4-neighbours, and across one
   plain road tile like net.cpp's POWER_JUMP_ROADS). Cached per (lot, sim step). */
static u16 s_pmark[MAP_MAX_W * MAP_MAX_H];
static u16 s_pgen;
static u16 s_pq[MAP_MAX_W * MAP_MAX_H];
static int s_pc_lot = -1;
static u32 s_pc_step = 0xFFFFFFFFu;
static PowerNetInfo s_pc;
static u16 s_lmark[MAX_LOTS];

static int pnet(const Tile *t) { return (t->zone & ZF_POWER_NET) != 0; }

int advice_power_network(int lot, PowerNetInfo *out)
{
    const Lot *l = world_lot(lot);
    int x0, y0, x1, y1, x, y, qh = 0, qt = 0, w = g_world.w, h = g_world.h, d;
    memset(out, 0, sizeof(*out));
    if (!l || l->def == B_NONE) return 0;
    if (lot == s_pc_lot && sim_date()->total_steps == s_pc_step) { *out = s_pc; return out->plants > 0 || out->lots > 0; }
    if (++s_pgen == 0) { memset(s_pmark, 0, sizeof(s_pmark)); memset(s_lmark, 0, sizeof(s_lmark)); s_pgen = 1; }
    world_lot_rect(lot, &x0, &y0, &x1, &y1);
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (pnet(WT(x, y))) { s_pmark[TILE_IDX(x, y)] = s_pgen; s_pq[qt++] = (u16)TILE_IDX(x, y); }
    while (qh < qt) {
        int idx = s_pq[qh++], tx = idx & (MAP_STRIDE - 1), ty = idx >> MAP_SHIFT, tl = g_world.t[idx].lot;
        if (tl && s_lmark[tl] != s_pgen) {
            const Lot *ll = &g_world.lots[tl];
            const BuildingDef *bd = &g_bdefs[ll->def];
            s_lmark[tl] = s_pgen;
            if (bd->cat == BCAT_POWER) { out->plants++; out->supply += net_source_output(tl); }
            else if (ll->status == LOT_ACTIVE) {
                s32 use = CPX_DEF_USE(ll->def, power_use, CPXU_POWER);
                if (use > 0) {
                    out->lots++; out->demand += use;
                    if (net_lot_power_state(tl) == NLS_SHORT) out->lots_short++; else out->used += use;
                }
            }
        }
        for (d = 0; d < DIR_COUNT; d++) {
            int nx = tx + g_dir_dx[d], ny = ty + g_dir_dy[d], ni;
            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
            ni = TILE_IDX(nx, ny);
            if (s_pmark[ni] == s_pgen) continue;
            if (pnet(&g_world.t[ni])) { s_pmark[ni] = s_pgen; s_pq[qt++] = (u16)ni; continue; }
            if ((g_world.t[ni].road & TR_CLASS_MASK) && !g_world.t[ni].lot) {   /* across one plain road tile */
                int jx = nx + g_dir_dx[d], jy = ny + g_dir_dy[d], ji;
                if (jx < 0 || jy < 0 || jx >= w || jy >= h) continue;
                ji = TILE_IDX(jx, jy);
                if (s_pmark[ji] != s_pgen && pnet(&g_world.t[ji])) { s_pmark[ji] = s_pgen; s_pq[qt++] = (u16)ji; }
            }
        }
    }
    out->spare = out->supply - out->used;
    if (out->spare < 0) out->spare = 0;
    out->pct = out->supply > 0 ? (int)((s64)out->demand * 100 / out->supply) : 0;
    s_pc = *out; s_pc_lot = lot; s_pc_step = sim_date()->total_steps;
    return out->plants > 0 || out->lots > 0;
}

void advice_power_total(PowerNetInfo *out)
{
    const NetStats *pw = net_stats(NET_POWER);
    memset(out, 0, sizeof(*out));
    out->supply = pw->supply; out->demand = pw->demand;
    out->lots = pw->lots_served + pw->lots_short; out->lots_short = pw->lots_short;
    out->plants = pw->networks;            /* networks with a plant */
    out->used = pw->demand;
    if (pw->lots_short > 0 && out->used > out->supply) out->used = out->supply;
    out->spare = IMAX(0, pw->supply - pw->demand);
    out->pct = pw->supply > 0 ? (int)((s64)pw->demand * 100 / pw->supply) : 0;
}

/* ------------------------------------------------------------------ save, dump */
void advice_save(struct SaveFile *sf)
{
    int k;
    save_begin_chunk(sf, "ADVC", 1);
    save_s32(sf, HS_COUNT);
    for (k = 0; k < HS_COUNT; k++) { save_s32(sf, A.yr[k]); save_s32(sf, A.yr_peak[k]); save_s32(sf, A.life[k]); save_s32(sf, A.life_peak[k]); }
    save_s32(sf, A.debt_oil); save_s32(sf, A.rights_wait); save_s32(sf, A.special_year);
    save_s32(sf, A.last_hind_kind); save_s32(sf, A.last_hind_year);
    save_s32(sf, A.power_near_latch); save_s32(sf, A.rights_near_done); save_s32(sf, A.year);
    save_end_chunk(sf);
}

void advice_load(struct SaveFile *sf)
{
    int ver, k, n;
    advice_reset();
    if (!load_find_chunk(sf, "ADVC", &ver)) return;
    n = load_s32(sf);
    for (k = 0; k < n; k++) {
        s32 a = load_s32(sf), b = load_s32(sf), c = load_s32(sf), d = load_s32(sf);
        if (k < HS_COUNT) { A.yr[k] = (s16)a; A.yr_peak[k] = b; A.life[k] = (s16)c; A.life_peak[k] = d; }
    }
    A.debt_oil = (s16)load_s32(sf); A.rights_wait = (s16)load_s32(sf); A.special_year = (s16)load_s32(sf);
    A.last_hind_kind = (s16)load_s32(sf); A.last_hind_year = (s16)load_s32(sf);
    A.power_near_latch = (u8)load_s32(sf); A.rights_near_done = (u8)load_s32(sf); A.year = (s16)load_s32(sf);
    UNUSED(ver);
}

void advice_dump(FILE *f)
{
    Advice v[ADV_COUNT];
    int n, k;
    char buf[160];
    if (!f) return;
    n = advice_collect(v, ADV_COUNT);
    fprintf(f, "advice %d-%02d: %d items\n", cur_year(), sim_date()->month + 1, n);
    for (k = 0; k < n; k++)
        fprintf(f, "advice:   %2d. %-14s score %4d go %d at %d,%d (%d, %d): %s\n", k + 1, advice_kind_name(v[k].kind), (int)v[k].score,
                v[k].go, v[k].x, v[k].y, (int)v[k].a, (int)v[k].b, v[k].text);
    fprintf(f, "advice: tallies year %d:", A.year);
    for (k = 0; k < HS_COUNT; k++) fprintf(f, " %d/%d", A.yr[k], A.life[k]);
    fprintf(f, "; rights wait %d; special year %d; debt in oil years %d\n", A.rights_wait, A.special_year, A.debt_oil);
    advice_progress(buf, sizeof(buf), NULL);
    fprintf(f, "advice: progress: %s\n", buf);
}
