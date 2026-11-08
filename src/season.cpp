/* season.cpp - 1.1 W1: winter simulation (season calendar, forecast + hard winters, winter peaks, frozen pipes, the
   Winter ledger line, winter lines). PORTABLE, deterministic, integer-only. Owner: W1. See season.h and
   docs/DESIGN_1_1.md section 3. Every number comes from diff() (defs.h, D1's table); on Easy every hook returns its
   neutral value and nothing here touches the sim state (no RNG draw, no dirty marks), so Easy = 1.0 bit for bit. */
#include "common.h"
#include "season.h"
#include "defs.h"
#include "sim.h"
#include "world.h"
#include "net.h"
#include "civic.h"
#include "budget.h"
#include "complex.h"
#include "text.h"
#include "save.h"
#include "util.h"
#include "tools.h"

#define SEASON_MSG_RING   16
#define FIXED_HARD_WINTER 1986     /* FMI: 2009-10 was the coldest "since the winter 1986-1987" (DESIGN_1_1 [18]) */
#define OIL_SHOCK_WINTER  1973     /* the first oil-shock winter 1973-74 */
#define SWEDEN_WINTER0    1968     /* the Sweden winters 1968-69 .. 1970-71 (SWEDEN_FROM_YEAR-1 .. SWEDEN_TO_YEAR) */
#define WINTER_NEWS_SCORE 650      /* January paper: below elections/status, above landmarks */

/* stacking rules per level (DESIGN_1_1 3.1: "Normal never rolls a hard winter in the Sweden winters or the first oil-shock
   winter, nor two in a row; Hard does not skip"); Easy rolls nothing anyway */
static const u8 s_no_stack[DIFF_COUNT] = { 1, 1, 0 };

typedef struct {
    s32 fc_year;               /* winter year of the last forecast (0 = none yet) */
    s32 fc_hard;               /* that forecast: hard */
    s32 last_hard;             /* winter year of the last hard winter (0 = none) */
    s32 hard_count;
    s32 cost, last_cost;       /* BL_WINTER this winter / the last finished one (mk, positive) */
    s32 frozen, frozen_max, dark, cold;
    s32 jan_pct;               /* power load % of the last January (0 = not seen) */
    s32 nov_spare;             /* power supply - the forecast peak at 1 November */
    s32 trouble;               /* winter months with dark, frozen or cold lots (this winter) */
    s32 hind_year, hind_trouble, hind_spare;   /* the last finished hard winter (hindsight) */
    s32 rep_power, rep_water;  /* total_months of the last winter candles / frozen report (+1; 0 = none) */
} SeasonState;
static SeasonState S;
static SeasonMsg   s_msg[SEASON_MSG_RING];
static int         s_msg_head, s_msg_n;
static int         s_seen;     /* season_changed: the season the poller saw last (0 = summer art at start); not reset */

static const DiffParams *D(void) { return diff(); }
static int cur_month(void) { return sim_date()->month; }
static int cur_year(void) { return sim_date()->year; }

/* ------------------------------------------------------------------ calendar */
int season_month_is_winter(int month) { return month == 11 || (month >= 0 && month <= 2); }
int season_winter_year(int year, int month) { return month <= 2 ? year - 1 : year; }
int season_is_winter(void) { return season_month_is_winter(cur_month()); }
int season_changed(void)
{
    int w = season_is_winter();
    if (w == s_seen) return 0;
    s_seen = w;
    return 1;
}
int season_mechanics(void)
{
    const DiffParams *d = D();
    return d->winter_load_pct > 0 || d->hard_winter_pct > 0 || d->plough_pct > 0 || d->fire_winter_pct != 100;
}

static void push_msg(int type, int x, int y, s32 a, s32 b)
{
    int idx;
    if (s_msg_n == SEASON_MSG_RING) { s_msg_head = (s_msg_head + 1) % SEASON_MSG_RING; s_msg_n--; }
    idx = (s_msg_head + s_msg_n) % SEASON_MSG_RING;
    s_msg[idx].type = (u8)type; s_msg[idx].x = (s16)x; s_msg[idx].y = (s16)y; s_msg[idx].a = a; s_msg[idx].b = b;
    s_msg_n++;
}
int season_poll_msg(SeasonMsg *m)
{
    if (!s_msg_n) return 0;
    *m = s_msg[s_msg_head];
    s_msg_head = (s_msg_head + 1) % SEASON_MSG_RING;
    s_msg_n--;
    return 1;
}

void season_reset(void)
{
    memset(&S, 0, sizeof(S));
    s_msg_head = s_msg_n = 0;
}

/* ------------------------------------------------------------------ forecast and the current winter */
int season_forecast(void)
{
    int m = cur_month();
    if (m != 10 && !season_month_is_winter(m)) return SFC_NONE;
    if (!S.fc_year || S.fc_year != season_winter_year(cur_year(), m)) return SFC_NONE;
    return S.fc_hard ? SFC_HARD : SFC_ORDINARY;
}
int season_hard_now(void) { return season_is_winter() && season_forecast() == SFC_HARD && D()->hard_winter_pct > 0; }
int season_hard_winters(void) { return S.hard_count; }

int season_hard_rule(int wy, int *fixed)
{
    int lv = diff_level();
    if (fixed) *fixed = 0;
    if (D()->hard_winter_pct <= 0 || !sim_settings()->disasters) return 0;
    if (wy < sim_settings()->start_year + 2) return 0;   /* 1.1 D1 (orchestrator): no hard winter in a game's first two winters */
    if (wy == FIXED_HARD_WINTER) { if (fixed) *fixed = 1; return 1; }
    if (lv >= 0 && lv < DIFF_COUNT && s_no_stack[lv]) {
        if (wy >= SWEDEN_WINTER0 && wy <= SWEDEN_WINTER0 + 2) return 0;
        if (wy == OIL_SHOCK_WINTER) return 0;
        if (S.last_hard && S.last_hard == wy - 1) return 0;     /* never two in a row */
        if (wy == FIXED_HARD_WINTER - 1) return 0;              /* nor just before the fixed one */
    }
    return 1;
}

/* ------------------------------------------------------------------ multipliers */
int season_power_pct(void)
{
    if (!season_is_winter()) return 100;
    return 100 + D()->winter_load_pct + (season_hard_now() ? D()->hard_load_pct : 0);
}
int season_heat_pct(void) { return season_power_pct(); }     /* the table has one winter load for power and heat */
int season_water_pct(void) { return season_hard_now() ? ICLAMP(D()->hard_water_pct, 10, 100) : 100; }
int season_peak_pct(void)
{
    int p = 100 + D()->winter_load_pct + (season_forecast() == SFC_HARD && D()->hard_winter_pct > 0 ? D()->hard_load_pct : 0);
    return IMAX(p, season_power_pct());
}
int season_fire_pct(void) { return season_is_winter() ? D()->fire_winter_pct : 100; }
s32 season_eff_supply(s32 supply, int pct) { return pct <= 100 ? supply : (s32)((s64)supply * 100 / pct); }
s32 season_scale(s32 raw, int pct) { return pct == 100 ? raw : (s32)(((s64)raw * pct + 50) / 100); }

Money season_winter_upkeep(Money road_mk, s32 pipe_tiles, int ui)
{
    const DiffParams *d = D();
    int hard = season_hard_now();
    s64 v;
    if (!season_is_winter() || road_mk < 0) return 0;
    v = (s64)road_mk * (hard ? d->plough_hard_pct : d->plough_pct) / 100;
    if (hard && pipe_tiles > 0) v += (s64)pipe_tiles * d->frost_repair10 * ui / (10 * 100);
    return (Money)v;
}

/* ------------------------------------------------------------------ the peak */
static s32 frost_supply(void)
{
    int i, wp = ICLAMP(D()->hard_water_pct, 10, 100);
    s32 s = 0;
    if (season_hard_now()) return net_stats(NET_WATER)->supply;      /* already cut */
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        s32 o;
        if (l->def != B_PUMP && l->def != B_WELL && l->def != B_WATER_TOWER) continue;
        o = net_source_output(i);
        s += l->def == B_WATER_TOWER ? o : (s32)((s64)o * wp / 100);
    }
    return s;
}

void season_peak(SeasonPeak *out)
{
    const NetStats *pn = net_stats(NET_POWER);
    UtilSummary hs;
    int cur = season_power_pct(), pk = season_peak_pct();
    s32 raw;
    memset(out, 0, sizeof(*out));
    out->peak_pct = pk;
    out->hard = season_forecast() == SFC_HARD && D()->hard_winter_pct > 0;
    out->power_now = pn->demand; out->power_supply = pn->supply;
    raw = cur == 100 ? pn->demand : (s32)((s64)pn->demand * 100 / cur);
    out->power_peak = season_scale(raw, pk);
    civic_util_summary(UT_HEAT, &hs);
    out->heat_now = hs.demand; out->heat_supply = hs.supply;
    raw = cur == 100 ? hs.demand : (s32)((s64)hs.demand * 100 / cur);
    out->heat_peak = season_scale(raw, pk);
    out->water_supply = net_stats(NET_WATER)->supply;
    out->water_frost = out->hard ? frost_supply() : out->water_supply;
}

/* ------------------------------------------------------------------ month end */
/* district (16x16 tile cell) with the most lots of a kind: 0 power-short, 1 water-short (dry), 2 heat-short */
static int lot_kind_short(int i, int kind)
{
    const Lot *l = &g_world.lots[i];
    if (l->def == B_NONE || l->status != LOT_ACTIVE) return 0;
    if (kind == 0) return net_lot_power_state(i) == NLS_SHORT;
    if (kind == 1) return net_lot_water_state(i) == NLS_SHORT && CPX_DEF_USE(l->def, water_use, CPXU_WATER) > 0;
    return civic_lot_util_state(UT_HEAT, i) == NLS_SHORT && civic_lot_util_use(UT_HEAT, i) > 0;
}
static s32 worst_district(int kind, int *px, int *py, s32 *in_district)
{
    static u16 cnt[(MAP_MAX_W >> 4) * (MAP_MAX_H >> 4)];
    int i, best = -1;
    s32 total = 0;
    memset(cnt, 0, sizeof(cnt));
    *px = *py = -1; *in_district = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        int c;
        if (!lot_kind_short(i, kind)) continue;
        c = (l->y >> 4) * (MAP_MAX_W >> 4) + (l->x >> 4);
        if (cnt[c] < 65535) cnt[c]++;
        total++;
        if (best < 0 || cnt[c] > cnt[best]) best = c;
    }
    if (best < 0) return 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (!lot_kind_short(i, kind)) continue;
        if ((l->y >> 4) * (MAP_MAX_W >> 4) + (l->x >> 4) == best) { *px = l->x; *py = l->y; break; }
    }
    *in_district = cnt[best];
    return total;
}

static int power_pct_now(void)
{
    const NetStats *pn = net_stats(NET_POWER);
    return pn->supply > 0 ? (int)((s64)pn->demand * 100 / pn->supply) : 0;
}

/* the month that just ended was a winter month: costs, tallies and (Jan-Mar) the winter reports */
static void winter_month_done(int ended, int report)
{
    const SimDate *dt = sim_date();
    int mech = season_mechanics(), hard = S.fc_hard && S.fc_year == season_winter_year(dt->year, dt->month) && D()->hard_winter_pct > 0;
    int x, y;
    s32 n, nd;
    S.cost += -g_budget.last[BL_WINTER];
    if (ended == 0) S.jan_pct = power_pct_now();
    if (!mech) return;
    S.dark = worst_district(0, &x, &y, &nd);
    if (S.dark > 0 && report) { push_msg(SM_CANDLES, x, y, S.dark, nd); S.rep_power = (s32)dt->total_months + 1; }
    S.frozen = 0;
    if (hard) {
        n = worst_district(1, &x, &y, &nd);
        S.frozen = n;
        if (n > S.frozen_max) S.frozen_max = n;
        if (n > 0 && report) { push_msg(SM_FROZEN, x, y, n, nd); S.rep_water = (s32)dt->total_months + 1; }
    }
    S.cold = worst_district(2, &x, &y, &nd);
    if (S.cold > 0 && report) push_msg(SM_COLD, x, y, S.cold, nd);
    if (S.dark > 0 || S.frozen > 0 || S.cold > 0) S.trouble++;
}

void season_month(void)
{
    const SimDate *dt = sim_date();
    int m = dt->month, ended = (m + 11) % 12, mech = season_mechanics(), fixed = 0;
    if (season_month_is_winter(ended)) winter_month_done(ended, m != 3);
    if (m == 10) {                                   /* 1 November: the forecast */
        int hard = 0;
        SeasonPeak pk;
        if (season_hard_rule(dt->year, &fixed)) {
            if (fixed) hard = 1;
            else hard = rng_below(sim_rng(), 100) < D()->hard_winter_pct;
        }
        S.fc_year = dt->year; S.fc_hard = hard;
        season_peak(&pk);
        S.nov_spare = pk.power_supply - pk.power_peak;
        push_msg(SM_FORECAST, -1, -1, hard, S.jan_pct);
    }
    if ((m == 9 || m == 10) && mech) {               /* 1 October, 1 November: the peak against supply (actionable) */
        s32 pk, sp;
        char t[8];
        if (season_advice(0, &pk, &sp, t, sizeof(t))) push_msg(SM_PEAK_POWER, -1, -1, pk, sp);
        if (season_advice(1, &pk, &sp, t, sizeof(t))) push_msg(SM_PEAK_HEAT, -1, -1, pk, sp);
    }
    if (m == 11) {                                   /* 1 December: first snow */
        S.cost = 0; S.trouble = 0; S.frozen = S.dark = S.cold = 0;
        if (season_forecast() == SFC_HARD && D()->hard_winter_pct > 0) { S.hard_count++; S.last_hard = dt->year; }
        if (mech) net_mark_dirty(NET_POWER | NET_WATER);
        push_msg(SM_FIRST_SNOW, -1, -1, mech, season_hard_now());
    } else if (m == 3) {                             /* 1 April: the ice is out */
        int was_hard = S.fc_hard && S.fc_year == dt->year - 1 && D()->hard_winter_pct > 0;
        S.last_cost = S.cost;
        if (was_hard) { S.hind_year = S.fc_year; S.hind_trouble = S.trouble; S.hind_spare = S.nov_spare; }
        if (mech) net_mark_dirty(NET_POWER | NET_WATER);
        S.dark = S.frozen = S.cold = 0;
        push_msg(SM_THAW, -1, -1, S.cost, was_hard);
    }
}

/* ------------------------------------------------------------------ text */
static void fmt_i(char *b, int n, s32 v) { text_fmt_int(b, n, v); }
static int vary(int n) { return (int)(hash_u32((u32)cur_year() * 2654435761u ^ sim_settings()->seed ^ 0x5ea5u) % (u32)n); }   /* flavour */

int season_msg_text(char *buf, int size, const SeasonMsg *m, int *severity)
{
    char a1[32], dist[NAME_LEN];
    *severity = SEV_INFO;
    if (size > 0) buf[0] = 0;
    dist[0] = 0;
    if (m->x >= 0) text_district_at(m->x, m->y, dist, sizeof(dist));
    switch (m->type) {
    case SM_FORECAST:
        if (m->a) {
            *severity = SEV_WARN;
            if (m->b >= 50)
                return str_printf(buf, size, "The Meteorological Institute expects a hard winter. The Cabinet respectfully notes that "
                                  "last January the power works ran at %d%%. Naturally the President is already aware.", (int)m->b);
            if (power_pct_now() >= 50)
                return str_printf(buf, size, "The Meteorological Institute expects a hard winter. The Cabinet respectfully notes that "
                                  "the power works run at %d%% today. Naturally the President is already aware.", power_pct_now());
            return str_printf(buf, size, "The Meteorological Institute expects a hard winter. The Cabinet notes that the power works "
                              "have room to spare, for now. Naturally the President is already aware.");
        }
        switch (vary(4)) {
        case 1:  return str_printf(buf, size, "Winter forecast: ordinary. The Institute expects snow, darkness and complaints, in that order.");
        case 2:  return str_printf(buf, size, "Winter forecast: ordinary. The Institute's barometer and the President's knee agree.");
        default: return str_printf(buf, size, "Winter forecast: ordinary. In this country that means the lakes freeze and nobody mentions it.");
        }
    case SM_FIRST_SNOW:
        if (m->b) return str_printf(buf, size, "First snow, and plenty of it. The ploughs are out, and so is the road budget.");
        if (m->a) return str_printf(buf, size, "First snow. The ploughs are out, and so is the road budget.");
        switch (vary(3)) {
        case 1:  return str_printf(buf, size, "First snow. The children are out with sledges; the adults with shovels.");
        default: return str_printf(buf, size, "First snow. The ploughs are out.");
        }
    case SM_THAW:
        if (m->a > 0) {
            text_fmt_money(a1, sizeof(a1), m->a, cur_year());
            return str_printf(buf, size, "The ice is out. The winter cost the town %s in ploughing%s.", a1, m->b ? " and repairs" : "");
        }
        switch (vary(3)) {
        case 1:  return str_printf(buf, size, "The ice is out. Somebody's car is still on the lake; the owner is said to be thinking.");
        default: return str_printf(buf, size, "The ice is out. The rivers run, and so, shortly, will the mosquitoes.");
        }
    case SM_CANDLES:
        *severity = SEV_WARN;
        fmt_i(a1, sizeof(a1), m->a);
        return str_printf(buf, size, "%s sits in the dark by candlelight (%s building%s in all). Romantic, say the poets. "
                          "Temporary, says the Cabinet.", dist[0] ? dist : "The far end of town", a1, m->a == 1 ? "" : "s");
    case SM_FROZEN:
        *severity = SEV_WARN;
        fmt_i(a1, sizeof(a1), m->b);
        return str_printf(buf, size, "Pipes frozen in %s: %s household%s fetch water from the neighbours. The neighbours keep count.",
                          dist[0] ? dist : "town", a1, m->b == 1 ? "" : "s");
    case SM_PEAK_POWER: case SM_PEAK_HEAT: {
        char a2[32];
        *severity = SEV_WARN;
        fmt_i(a1, sizeof(a1), m->a); fmt_i(a2, sizeof(a2), m->b);
        if (m->type == SM_PEAK_POWER)
            return str_printf(buf, size, "Cabinet sources observe that the winter peak (%s) exceeds the power works (%s). A plant, "
                              "perhaps.", a1, a2);
        return str_printf(buf, size, "The Heating Board observes that the winter peak (%s) exceeds the heat plants (%s). The "
                          "radiators at the far end will notice first.", a1, a2);
    }
    case SM_COLD:
        *severity = SEV_WARN;
        fmt_i(a1, sizeof(a1), m->a);
        return str_printf(buf, size, "The radiators of %s are lukewarm: %s block%s heat with the oven door open. The heat plant "
                          "gives all it has.", dist[0] ? dist : "the far blocks", a1, m->a == 1 ? "" : "s");
    }
    return 0;
}

int season_suppress_event(int se_type)
{
    s32 now = (s32)sim_date()->total_months + 1;
    if (se_type == SE_POWER_SHORTAGE) return S.rep_power == now;
    if (se_type == SE_WATER_SHORTAGE) return S.rep_water == now;
    return 0;
}

int season_tip_line(int what, char *buf, int size, int *warn)
{
    SeasonPeak pk;
    char a[32], b[32], c[48];
    int m = cur_month(), win = season_is_winter();
    if (warn) *warn = 0;
    if (size > 0) buf[0] = 0;
    if (!season_mechanics() || (m != 9 && m != 10 && !win)) return 0;
    season_peak(&pk);
    if (what == STIP_WATER) {
        if (!pk.hard || pk.water_supply <= 0) return 0;
        if (win) return str_printf(buf, size, "Hard frost: pumps and wells give %d%%", season_water_pct());
        fmt_i(a, sizeof(a), net_stats(NET_WATER)->used); fmt_i(b, sizeof(b), pk.water_frost);
        if (warn) *warn = net_stats(NET_WATER)->used > pk.water_frost;
        return str_printf(buf, size, "Hard frost ahead: pumps and wells -%d%% (~%s / %s)", 100 - ICLAMP(D()->hard_water_pct, 10, 100), a, b);
    }
    if (what == STIP_HEAT && pk.heat_supply <= 0 && pk.heat_now <= 0) return 0;
    if (what == STIP_POWER && pk.power_supply <= 0) return 0;
    if (win) {
        int load = season_power_pct() - 100;
        if (season_hard_now()) str_printf(c, sizeof(c), " (hard frost +%d%%)", D()->hard_load_pct);
        else c[0] = 0;
        return str_printf(buf, size, "Winter: %s use +%d%%%s", what == STIP_HEAT ? "heat" : "power", load - (season_hard_now() ? D()->hard_load_pct : 0), c);
    }
    if (what == STIP_HEAT) { fmt_i(a, sizeof(a), pk.heat_peak); fmt_i(b, sizeof(b), pk.heat_supply); if (warn) *warn = pk.heat_peak > pk.heat_supply; }
    else { fmt_i(a, sizeof(a), pk.power_peak); fmt_i(b, sizeof(b), pk.power_supply); if (warn) *warn = pk.power_peak > pk.power_supply; }
    return str_printf(buf, size, "%s peak (%s): ~%s / %s", what == STIP_HEAT ? "Heat winter" : "Winter",
                      m == 10 ? (pk.hard ? "forecast: hard" : "forecast: ordinary") : "expected", a, b);
}

int season_lot_line(int lot, char *buf, int size)
{
    const Lot *l = world_lot(lot);
    if (size > 0) buf[0] = 0;
    if (!l || !season_mechanics()) return 0;
    if ((l->def == B_PUMP || l->def == B_WELL) && season_hard_now())
        return str_printf(buf, size, "Hard frost -%d%% (output %d%% until April)", 100 - season_water_pct(), season_water_pct());
    if ((g_bdefs[l->def].cat == BCAT_POWER || l->def == B_HEAT_PLANT) && season_is_winter())
        return str_printf(buf, size, "Winter: the town's %s use +%d%% (December-March)", l->def == B_HEAT_PLANT ? "heat" : "power",
                          season_power_pct() - 100);
    if (g_bdefs[l->def].cat == BCAT_POWER || l->def == B_HEAT_PLANT)
        return str_printf(buf, size, "Winter peak: the town's use +%d%%%s", season_peak_pct() - 100,
                          season_forecast() == SFC_HARD ? " (hard winter forecast)" : "");
    return 0;
}

int season_advice(int k, s32 *peak, s32 *supply, char *text, int size)
{
    SeasonPeak pk;
    char a[32], b[32];
    int m = cur_month();
    if (size > 0) text[0] = 0;
    if (!season_mechanics() || (m != 9 && m != 10)) return 0;
    season_peak(&pk);
    if (k == 0) {
        if (pk.power_supply <= 0 || pk.power_peak <= pk.power_supply || net_stats(NET_POWER)->lots_short > 0) return 0;
        *peak = pk.power_peak; *supply = pk.power_supply;
        fmt_i(a, sizeof(a), pk.power_peak); fmt_i(b, sizeof(b), pk.power_supply);
        str_printf(text, size, "Cabinet sources observe that the winter peak%s (%s) exceeds the power works (%s). A plant, perhaps.",
                   pk.hard ? " of a hard winter" : "", a, b);
        return 1;
    }
    if (pk.heat_supply <= 0 || pk.heat_peak <= pk.heat_supply) return 0;
    *peak = pk.heat_peak; *supply = pk.heat_supply;
    fmt_i(a, sizeof(a), pk.heat_peak); fmt_i(b, sizeof(b), pk.heat_supply);
    str_printf(text, size, "The Heating Board observes that the winter peak%s (%s) exceeds the heat plants (%s). The radiators at "
               "the far end will notice first.", pk.hard ? " of a hard winter" : "", a, b);
    return 1;
}

int season_news_score(void) { return season_hard_now() ? WINTER_NEWS_SCORE : 0; }

void season_news_story(char *h, int hs, char *d, int ds, char *b, int bs)
{
    SeasonPeak pk;
    char a1[32], a2[32];
    season_peak(&pk);
    fmt_i(a1, sizeof(a1), pk.power_now); fmt_i(a2, sizeof(a2), pk.power_supply);
    str_copy(h, "MINUS 35 IN THE MARKET SQUARE", hs);
    str_copy(d, "Saunas report record attendance", ds);
    if (pk.power_supply > 0)
        str_printf(b, bs, "The Meteorological Institute calls it a hard winter; the power works call it %s units against %s. "
                   "Pipes are wrapped in newspaper, this one included.", a1, a2);
    else
        str_copy(b, "The Meteorological Institute calls it a hard winter. Pipes are wrapped in newspaper, this one included.", bs);
}

int season_hindsight(char *head, int hs, char *body, int bs)
{
    char a1[32];
    int wy = S.hind_year;
    if (head && hs > 0) head[0] = 0;
    if (!season_mechanics() || !wy || wy != cur_year() - 1 || S.hind_trouble <= 0) return 0;
    str_copy(head, "IN HINDSIGHT: THE HARD WINTER", hs);
    fmt_i(a1, sizeof(a1), IABS(S.hind_spare));
    if (S.hind_spare >= 0)
        str_printf(body, bs, "In hindsight, the President met the hard winter of %d-%02d with %s units of power to spare. Too few.",
                   wy, (wy + 1) % 100, a1);
    else
        str_printf(body, bs, "In hindsight, the President met the hard winter of %d-%02d %s units of power short. The candles "
                   "did the rest.", wy, (wy + 1) % 100, a1);
    return 1;
}

/* ------------------------------------------------------------------ stats, debug, script, save */
static SeasonStats s_st;
const SeasonStats *season_stats(void)
{
    s_st.winter_cost = S.cost; s_st.last_cost = S.last_cost;
    s_st.frozen_lots = S.frozen; s_st.frozen_max = S.frozen_max; s_st.dark_lots = S.dark; s_st.cold_lots = S.cold;
    s_st.hard_winters = S.hard_count; s_st.last_hard_year = S.last_hard; s_st.jan_power_pct = S.jan_pct;
    return &s_st;
}

void season_statlog(FILE *f, int header)
{
    UtilSummary hs;
    if (!f) return;
    if (header) { fprintf(f, ",winter,hard,load_pct,water_pct,heat_sup,heat_dem,heat_short,winter_mk,frozen"); return; }
    civic_util_summary(UT_HEAT, &hs);      /* the month being logged (the row is written before the date advance) */
    fprintf(f, ",%d,%d,%d,%d,%d,%d,%d,%d,%d", season_is_winter(), season_hard_now(), season_power_pct(), season_water_pct(),
            hs.supply, hs.demand, hs.lots_short, -g_budget.last[BL_WINTER], season_hard_now() ? sim_stats()->water_short_lots : 0);
}

void season_dump(FILE *f)
{
    SeasonPeak pk;
    if (!f) return;
    season_peak(&pk);
    fprintf(f, "season: %s %d-%02d, level %s, winter %d, forecast %d (winter %d hard %d), hard now %d, load %d%%, peak %d%%, water %d%%,"
               " fire %d%%\n", diff_name(diff_level()), cur_year(), cur_month() + 1, diff_name(diff_level()), season_is_winter(),
            season_forecast(), S.fc_year, S.fc_hard, season_hard_now(), season_power_pct(), season_peak_pct(), season_water_pct(),
            season_fire_pct());
    fprintf(f, "season: power now %d peak %d supply %d | heat now %d peak %d supply %d | water %d frost %d\n", pk.power_now,
            pk.power_peak, pk.power_supply, pk.heat_now, pk.heat_peak, pk.heat_supply, pk.water_supply, pk.water_frost);
    fprintf(f, "season: hard winters %d (last %d), cost this winter %d last %d, frozen %d (max %d), dark %d, cold %d, jan %d%%,"
               " trouble %d, hindsight %d/%d/%d\n", S.hard_count, S.last_hard, S.cost, S.last_cost, S.frozen, S.frozen_max, S.dark,
            S.cold, S.jan_pct, S.trouble, S.hind_year, S.hind_trouble, S.hind_spare);
}

int season_script_line(int n, char **t, FILE *out)
{
    if (n < 1 || !str_ieq(t[0], "season")) return -1;
    if (n >= 2 && str_ieq(t[1], "dump")) { season_dump(out); return SCR_OK; }
    if (n >= 3 && str_ieq(t[1], "force")) {   /* "season force hard|ordinary": the coming/current winter's forecast (tests) */
        S.fc_year = season_winter_year(cur_year(), cur_month());
        S.fc_hard = str_ieq(t[2], "hard");
        if (season_mechanics()) net_mark_dirty(NET_POWER | NET_WATER);
        if (out) fprintf(out, "season: winter %d forced %s\n", S.fc_year, S.fc_hard ? "hard" : "ordinary");
        return SCR_OK;
    }
    return SCR_ERROR;
}

u32 season_hash(void)
{
    s32 v[6];
    if (!season_mechanics()) return 0;
    v[0] = S.fc_year; v[1] = S.fc_hard; v[2] = S.last_hard; v[3] = S.hard_count; v[4] = S.cost; v[5] = S.frozen_max;
    return hash_bytes(HASH_INIT, v, sizeof(v));
}

#define SEAS_FIELDS 19
static void fields(s32 *v, int store)
{
    s32 *p[SEAS_FIELDS];
    int k;
    p[0] = &S.fc_year; p[1] = &S.fc_hard; p[2] = &S.last_hard; p[3] = &S.hard_count; p[4] = &S.cost; p[5] = &S.last_cost;
    p[6] = &S.frozen; p[7] = &S.frozen_max; p[8] = &S.dark; p[9] = &S.cold; p[10] = &S.jan_pct; p[11] = &S.nov_spare;
    p[12] = &S.trouble; p[13] = &S.hind_year; p[14] = &S.hind_trouble; p[15] = &S.hind_spare; p[16] = &S.rep_power;
    p[17] = &S.rep_water; p[18] = NULL;
    for (k = 0; k < SEAS_FIELDS; k++) {
        if (!p[k]) { if (!store) v[k] = 0; continue; }
        if (store) *p[k] = v[k]; else v[k] = *p[k];
    }
}

int season_save(struct SaveFile *sf)
{
    s32 v[SEAS_FIELDS];
    int k;
    fields(v, 0);
    save_begin_chunk(sf, "SEAS", 1);
    save_s32(sf, SEAS_FIELDS);
    for (k = 0; k < SEAS_FIELDS; k++) save_s32(sf, v[k]);
    save_end_chunk(sf);
    return 1;
}

int season_load(struct SaveFile *sf)
{
    s32 v[SEAS_FIELDS];
    int ver, n, k;
    season_reset();
    if (!load_find_chunk(sf, "SEAS", &ver)) return 0;
    n = load_s32(sf);
    memset(v, 0, sizeof(v));
    for (k = 0; k < n; k++) { s32 x = load_s32(sf); if (k < SEAS_FIELDS) v[k] = x; }
    fields(v, 1);
    UNUSED(ver);
    return load_ok(sf);
}

#ifdef KAUP_TESTS
void season_test_force(int forecast_year, int hard) { S.fc_year = forecast_year; S.fc_hard = hard; }
#endif
