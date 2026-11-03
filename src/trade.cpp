/* trade.cpp - M7: clearing trade, Western trade, oil prices, loans. PORTABLE, deterministic, integer-only.
   Owner: M7. See trade.h for the model. The Western price walk uses its own RNG (seeded from the city seed,
   saved), so the simulation RNG stream is never touched. Money is booked with budget_spend at month end
   (like politics_month): fuel on BL_UTILITIES, the hard-currency premium on BL_EXPORT, loans on BL_INTEREST,
   the materials rebate on BL_CONSTRUCTION, the machine tools / computers bonus on BL_TAX_BUS / BL_SPECIAL. */
#include "common.h"
#include "trade.h"
#include "era.h"
#include "industry.h"
#include "politics.h"
#include "transit.h"
#include "sim.h"
#include "world.h"
#include "budget.h"
#include "text.h"
#include "save.h"
#include "util.h"

/* ------------------------------------------------------------------ tunables */
#define LEVEL_PCT_MODEST     85
#define LEVEL_PCT_AMBITIOUS  120
#define QUOTA_RISE_PCT       5       /* per year of the agreement */
#define QUOTA_SUPPLEMENT_PCT 130     /* 1974 oil-for-goods supplement */
#define REVIEW_SHORT_PCT     80
#define SHORTFALL_PCT        90      /* clearing term after a shortfall review (12 months) */
#define DEFICIT_PCT          90      /* clearing term while in deficit */
#define DEFICIT_AFTER        3       /* months below -tolerance before the penalty */
#define EAST_CREDIT_PCT      110
#define WASTE_DIV            4       /* a quarter of the credit above the tolerance spoils each month */
#define OIL_PLANT_UNITS      25      /* oil units / month (credit at index 100) */
#define HEAT_PLANT_UNITS     15
#define FUEL_MK_PER_UNIT     2       /* mk per oil unit at index 100 (x upkeep index) */
#define COAL_SURCHARGE_MK    40      /* per coal plant per month x (coal index - 100) / 100 */
#define BUS_IMPORT           2       /* credit per bus per month */
#define MATERIALS_REBATE_PCT 10      /* of last month's construction spending */
#define MATERIALS_CREDIT_PCT 15
#define CONSUMER_PROG_PCT    150
#define WEST_HC_DIV          8
#define WEST_MK_DIV          40
#define TOOLS_TAX_PCT        5
#define COMPUTER_UPKEEP_PCT  8

static TradeState     T;
static TradeAgreement A;
static Rng            s_rng;
static s32            s_cx_goods[TRADE_COMPLEX_SLOTS], s_cx_east[TRADE_COMPLEX_SLOTS], s_cx_west[TRADE_COMPLEX_SLOTS];
static int            s_def_heat = -2;   /* M5's heat plant def (looked up by key; B_NONE until it exists) */
static s32            s_deliv_hist[12], s_quota_hist[12];
static int            s_hist_pos;

static int month_now(void) { return sim_date()->year * 12 + sim_date()->month; }   /* the month being closed */
static int pol_on(void) { return politics_enabled(); }

static int heat_def(void)
{
    if (s_def_heat == -2) s_def_heat = def_building_find("heat_plant");
    return s_def_heat;
}

/* ------------------------------------------------------------------ prices as functions of the date
   m = year * 12 + month of the month in force */
static int oil_index_at(int m)
{
    if (m < 1973 * 12 + 10) return 100;
    if (m < 1979 * 12) return 400;
    if (m < 1981 * 12) return 550;
    if (m < 1986 * 12 + 2) return 500;
    return 250;
}
static int coal_index_at(int m)
{
    if (m < 1974 * 12) return 100;
    if (m < 1986 * 12) return 180;
    return 130;
}
static int clearing_boom_at(int m)
{
    if (m < 1973 * 12 + 10) return 100;
    if (m < 1981 * 12) return 150;
    if (m < 1986 * 12 + 2) return 120;
    if (m < TRADE_END_YEAR * 12) return 85;
    return 100;
}
static int west_baseline_at(int m)
{
    int y = m / 12;
    if (m >= 1957 * 12 + 8 && m < 1960 * 12) return 115;   /* 1957 devaluation */
    if (m >= 1967 * 12 + 9 && m < 1970 * 12) return 115;   /* 1967 devaluation */
    if (y >= 1975 && y <= 1977) return 85;                 /* the Western recession */
    if (y >= 1980 && y <= 1984) return 110;
    if (y >= 1985) return 105;
    return 100;
}
static int oil_rate(int y) { return 8 + 2 * (IMIN(y, 1972) - 1955); }          /* per 1,000 residents / month */
static int consumer_rate(int y) { return 150 + 3 * (IMIN(y, 1975) - 1955); }   /* Ladas, televisions, sunflower oil */
static int west_era_mult(int y) { return y < 1960 ? 2 : y < 1970 ? 3 : y < 1980 ? 4 : 5; }

int trade_period_of(int year, int *from, int *to)
{
    int k;
    if (year >= TRADE_END_YEAR) { if (from) *from = 0; if (to) *to = 0; return 0; }
    k = year <= TRADE_FIRST_PERIOD ? 0 : (year - TRADE_FIRST_PERIOD) / TRADE_PERIOD_YEARS;
    if (from) *from = TRADE_FIRST_PERIOD + k * TRADE_PERIOD_YEARS;
    if (to) *to = TRADE_FIRST_PERIOD + k * TRADE_PERIOD_YEARS + TRADE_PERIOD_YEARS - 1;
    return 1;
}

/* ------------------------------------------------------------------ deliveries */
static int goods_of_kind(int ik)
{
    switch (ik) {
    case IK_WOOD:    return TG_HOUSES;
    case IK_TEXTILE: return TG_TEXTILES;
    case IK_FOOD:    return TG_DAIRY;
    case IK_ELEC:    return TG_ELECTRONICS;
    default:         return TG_MACHINERY;
    }
}

static void deliveries(s32 *east, s32 *west, s32 *east_total, s32 *west_total)
{
    const IndKinds *k = industry_kinds();
    int i;
    for (i = 0; i < TG_COUNT; i++) east[i] = west[i] = 0;
    for (i = IK_WOOD; i < IK_COUNT; i++) { east[goods_of_kind(i)] += k->export_east[i]; west[goods_of_kind(i)] += k->export_west[i]; }
    for (i = 0; i < TRADE_COMPLEX_SLOTS; i++)
        if (s_cx_goods[i] >= 0 && s_cx_goods[i] < TG_COUNT) { east[s_cx_goods[i]] += s_cx_east[i]; west[s_cx_goods[i]] += s_cx_west[i]; }
    *east_total = *west_total = 0;
    for (i = 0; i < TG_COUNT; i++) { *east_total += east[i]; *west_total += west[i]; }
}

void trade_set_complex(int slot, int goods, s32 east, s32 west)
{
    if (slot < 0 || slot >= TRADE_COMPLEX_SLOTS) return;
    s_cx_goods[slot] = (east || west) ? goods : -1;
    s_cx_east[slot] = IMAX(0, east); s_cx_west[slot] = IMAX(0, west);
}

/* ------------------------------------------------------------------ agreements */
static int level_pct(int level) { return level == TL_MODEST ? LEVEL_PCT_MODEST : level == TL_AMBITIOUS ? LEVEL_PCT_AMBITIOUS : 100; }

static void make_quota(int level)
{
    s32 east[TG_COUNT], west[TG_COUNT], et, wt, floor_m;
    int g;
    deliveries(east, west, &et, &wt);
    floor_m = 20 + sim_stats()->population / 20;
    A.quota_total = 0;
    for (g = 0; g < TG_COUNT; g++) {
        s32 base = east[g];
        if (g == TG_MACHINERY) base = IMAX(base, floor_m);
        A.quota[g] = (s32)((s64)base * level_pct(level) / 100);
        if (T.loan[LOAN_EAST].principal > 0) A.quota[g] = A.quota[g] * EAST_CREDIT_PCT / 100;
        A.quota_total += A.quota[g];
    }
}

s32 trade_quota_now(int goods)
{
    int years = sim_date()->year - A.from_year;
    s32 q;
    if (goods < 0 || goods >= TG_COUNT || A.level < 0 || T.closed) return 0;
    q = (s32)((s64)A.quota[goods] * (100 + QUOTA_RISE_PCT * IMAX(0, years)) / 100);
    if (A.supplement) q = q * QUOTA_SUPPLEMENT_PCT / 100;
    return q;
}

static s32 quota_total_now(void)
{
    s32 t = 0;
    int g;
    for (g = 0; g < TG_COUNT; g++) t += trade_quota_now(g);
    return t;
}

int trade_level_allowed(int level)
{
    if (level < 0 || level >= TL_COUNT) return 0;
    if (level == TL_MODEST && T.loan[LOAN_EAST].principal > 0) return 0;       /* the credit locks the quota */
    if (level == TL_AMBITIOUS && pol_on() && politics_meter(METER_EAST) < 40) return 0;
    return 1;
}

static void sign(int level, int automatic)
{
    A.level = level;
    A.offer = 0; A.offer_months = 0;
    make_quota(level);
    if (pol_on()) politics_meter_add(METER_EAST, level == TL_MODEST ? -2 : level == TL_AMBITIOUS ? 3 : 0);
    era_push_msg(automatic ? EM_AUTO_SIGNED : EM_SIGNED, level, A.from_year);
    log_printf("trade: agreement %d-%d signed: %s, quota %d / month", A.from_year, A.to_year, trade_level_name(level), A.quota_total);
}

int trade_sign(int level)
{
    if (!A.offer || T.closed || !trade_level_allowed(level)) return 0;
    sign(level, 0);
    return 1;
}

/* a new period starts in the month m (January) */
static void new_period(int year)
{
    int from, to;
    if (!trade_period_of(year, &from, &to)) return;
    /* settle the old account: surplus is written off, half of a deficit is carried into the new agreement */
    if (A.level >= 0) {
        s32 before = T.balance;
        if (T.balance > 0) { T.wasted_year += T.balance; T.wasted_total += T.balance; T.balance = 0; }
        else T.balance /= 2;
        era_push_msg(EM_SETTLEMENT, before, T.balance);
    }
    A.from_year = from; A.to_year = to;
    A.supplement = 0;
    A.offer = 1; A.offer_months = 0;
    era_push_msg(EM_OFFER, from, to);
}

/* ------------------------------------------------------------------ lifecycle */
void trade_reset(void)
{
    const SimSettings *ss = sim_settings();
    int i, y = sim_date()->year;
    memset(&T, 0, sizeof(T));
    memset(&A, 0, sizeof(A));
    memset(s_deliv_hist, 0, sizeof(s_deliv_hist)); memset(s_quota_hist, 0, sizeof(s_quota_hist));
    s_hist_pos = 0;
    for (i = 0; i < TRADE_COMPLEX_SLOTS; i++) { s_cx_goods[i] = -1; s_cx_east[i] = s_cx_west[i] = 0; }
    s_def_heat = -2;
    rng_seed(&s_rng, ss->seed * 2246822519u ^ 0x7a3dc1e5u);
    for (i = 0; i < LOAN_KINDS; i++) T.loan[i].project = -1;
    T.fulfil_pct = -1;
    T.oil_index = oil_index_at(y * 12);
    T.coal_index = coal_index_at(y * 12);
    T.west_index = west_baseline_at(y * 12);
    T.closed = y >= TRADE_END_YEAR;
    if (!T.closed) {
        /* the running agreement (a 1955 start joins the first one as a preliminary protocol), signed Standard */
        trade_period_of(y, &A.from_year, &A.to_year);
        if (y < TRADE_FIRST_PERIOD) A.from_year = y;
        A.level = TL_STANDARD;
        make_quota(TL_STANDARD);
    } else A.level = -1;
    if (ss->all_unlocked) T.techs = (1u << TECH_COUNT) - 1;
}

const TradeState *trade_state(void) { return &T; }
const TradeAgreement *trade_agreement(void) { return &A; }

/* ------------------------------------------------------------------ hooks */
int trade_clearing_pct(void)
{
    if (getenv("M7OFF")) return 100;
    s64 p = 100;
    int m = month_now();
    if (T.closed || sim_date()->year >= TRADE_END_YEAR) return 100;
    p = level_pct(A.level >= 0 ? A.level : TL_STANDARD);
    p = p * clearing_boom_at(m) / 100;
    if (T.shortfall_months > 0) p = p * SHORTFALL_PCT / 100;
    if (T.deficit_months >= DEFICIT_AFTER) p = p * DEFICIT_PCT / 100;
    if (T.loan[LOAN_EAST].principal > 0) p = p * EAST_CREDIT_PCT / 100;
    return (int)ICLAMP(p, 10, 400);
}
int trade_western_pct(void) { if (getenv("M7OFF")) return 100; return ICLAMP(T.west_index, 50, 150); }
int trade_emigration_pct(void) { return T.consumer && !T.closed ? 80 : 100; }
int trade_tech(int tech) { return tech >= 0 && tech < TECH_COUNT && (T.techs & (1u << tech)) != 0; }

int trade_fuel_units(int def)
{
    if (def == B_OIL_PLANT) return OIL_PLANT_UNITS;
    if (def != B_NONE && def == heat_def()) return HEAT_PLANT_UNITS;
    return 0;
}

/* ------------------------------------------------------------------ month end */
static int count_def(int def)
{
    int i, n = 0;
    if (def <= B_NONE) return 0;
    for (i = 1; i < g_world.lot_hi; i++) if (g_world.lots[i].def == def && g_world.lots[i].status == LOT_ACTIVE) n++;
    return n;
}

static int project_def_count(int p)
{
    int i, n = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_NONE) continue;
        if (p == TPROJ_POWER && def_building(l->def)->cat == BCAT_POWER) n++;
        else if (p == TPROJ_HEAT && l->def == heat_def()) n++;
        else if (p == TPROJ_ELEMENTS && l->def == B_ELEMENT_FACTORY) n++;
    }
    return n;
}

static void loans_month(void)
{
    int k;
    for (k = 0; k < LOAN_KINDS; k++) {
        TradeLoan *L = &T.loan[k];
        s32 interest, repay;
        if (L->principal <= 0) continue;
        interest = (s32)((s64)L->principal * L->rate / 1000);
        if (interest < 1) interest = 1;
        repay = L->months_left > 1 ? IMIN(L->principal, (L->principal + L->months_left - 1) / L->months_left) : L->principal;
        L->principal -= repay;
        L->months_left--;
        budget_spend(interest + repay, BL_INTEREST);
        T.loan_paid_total += repay; T.loan_interest_total += interest;
        if (k == LOAN_EAST && L->project >= 0 && L->project_months > 0) {
            if (project_def_count(L->project) > L->project_base) {
                L->project_months = 0;
                era_push_msg(EM_LOAN_DONE, L->project, 0);
            } else if (--L->project_months <= 0) {
                /* not built: the credit is called in */
                s32 rest = L->principal;
                budget_spend(rest, BL_INTEREST);
                T.loan_paid_total += rest;
                L->principal = 0; L->months_left = 0; L->project_months = 0;
                if (pol_on()) politics_meter_add(METER_EAST, -5);
                era_push_msg(EM_LOAN_CALLED, L->project, rest);
            }
        }
        if (L->principal <= 0 && L->orig > 0) { L->principal = 0; L->months_left = 0; L->project = -1; L->orig = 0; era_push_msg(EM_LOAN_REPAID, k, 0); }
    }
}

void trade_month(void)
{
    s32 east[TG_COUNT], west[TG_COUNT], et, wt, pop = sim_stats()->population, imp;
    int m = month_now(), nm = m + 1, y = sim_date()->year, g, k, oil_plants, heat_plants, coal_plants, buses = 0, ui;
    const Money *L = g_budget.last;
    /* ---- prices for the month just closed */
    T.oil_index = oil_index_at(m);
    T.coal_index = coal_index_at(m);
    ui = budget_upkeep_index();
    deliveries(east, west, &et, &wt);
    for (g = 0; g < TG_COUNT; g++) T.delivered[g] = east[g];
    oil_plants = count_def(B_OIL_PLANT);
    heat_plants = count_def(heat_def());
    coal_plants = count_def(B_COAL_PLANT);
    for (k = 0; k < BUS_MAX_LINES; k++) {
        const BusLine *bl = transit_line(k);
        if (bl && bl->used && bl->status == BLS_OK) buses += bl->buses;
    }
    /* ---- fuel (mk): oil users at the oil index, coal plants' surcharge from 1974 */
    {
        s64 fuel = (s64)(oil_plants * OIL_PLANT_UNITS + heat_plants * HEAT_PLANT_UNITS) * FUEL_MK_PER_UNIT * T.oil_index / 100;
        fuel += (s64)coal_plants * COAL_SURCHARGE_MK * (T.coal_index - 100) / 100;
        fuel = fuel * ui / 100;
        T.fuel_cost = (s32)fuel;
        if (fuel) budget_spend((Money)fuel, BL_UTILITIES);
    }
    /* ---- clearing account */
    if (!T.closed) {
        T.exports_east = et;
        T.imports[TI_OIL_TOWN] = (s32)((s64)pop * oil_rate(y) / 1000 * T.oil_index / 100);
        T.imports[TI_OIL_PLANTS] = (oil_plants * OIL_PLANT_UNITS + heat_plants * HEAT_PLANT_UNITS) * T.oil_index / 100;
        T.imports[TI_BUSES] = buses * BUS_IMPORT;
        T.imports[TI_CONSUMER] = (s32)((s64)pop * consumer_rate(y) / 1000 * (T.consumer ? CONSUMER_PROG_PCT : 100) / 100);
        if (T.oil_index > 100) T.imports[TI_CONSUMER] = T.imports[TI_CONSUMER] * 85 / 100;   /* belts tightened */
        T.imports[TI_MATERIALS] = 0;
        if (T.materials && L[BL_CONSTRUCTION] < 0) {
            s32 spent = -L[BL_CONSTRUCTION];
            T.imports[TI_MATERIALS] = spent * MATERIALS_CREDIT_PCT / 100;
            budget_spend(-(Money)(spent * MATERIALS_REBATE_PCT / 100), BL_CONSTRUCTION);
        }
        imp = 0;
        for (k = 0; k < TI_COUNT; k++) imp += T.imports[k];
        T.imports_total = imp;
        T.balance += et - imp;
        T.tolerance = 3 * et + 200;
        T.wasted_month = 0;
        if (T.balance > T.tolerance) {
            s32 w = (T.balance - T.tolerance + WASTE_DIV - 1) / WASTE_DIV;
            T.balance -= w; T.wasted_month = w; T.wasted_year += w; T.wasted_total += w;
            if (T.wasted_year - w < T.tolerance / 4 && T.wasted_year >= T.tolerance / 4) era_push_msg(EM_WASTED, T.wasted_year, 0);   /* once a year */
        }
        if (T.balance < -T.tolerance) {
            T.deficit_months++;
            if (pol_on() && (T.deficit_months & 1) == 0) politics_meter_add(METER_EAST, -1);
            if (T.deficit_months == DEFICIT_AFTER) era_push_msg(EM_DEFICIT, T.balance, T.tolerance);
        } else T.deficit_months = 0;
        /* fulfilment, rolling 12 months */
        s_deliv_hist[s_hist_pos] = et; s_quota_hist[s_hist_pos] = quota_total_now();
        s_hist_pos = (s_hist_pos + 1) % 12;
        T.deliv_12 = T.quota_12 = 0;
        for (k = 0; k < 12; k++) { T.deliv_12 += s_deliv_hist[k]; T.quota_12 += s_quota_hist[k]; }
        if (T.shortfall_months > 0) T.shortfall_months--;
        /* the offer waits; Standard by default */
        if (A.offer && ++A.offer_months >= TRADE_OFFER_MONTHS) sign(trade_level_allowed(TL_STANDARD) ? TL_STANDARD : TL_AMBITIOUS, 1);
    } else {
        T.exports_east = 0; T.imports_total = 0;
        for (k = 0; k < TI_COUNT; k++) T.imports[k] = 0;
    }
    /* ---- Western trade: price walk, premium, hard currency, licences */
    T.exports_west = wt;
    {
        int base = west_baseline_at(nm), step = rng_range(&s_rng, -3, 3);
        T.west_index += step + (base - T.west_index) / 6;
        T.west_index = ICLAMP(T.west_index, 60, 140);
    }
    T.west_income = (s32)((s64)wt * T.west_index / 100 * west_era_mult(y) / WEST_MK_DIV);
    if (T.west_income) budget_spend(-(Money)T.west_income, BL_EXPORT);
    {
        s32 h = (s32)((s64)wt * T.west_index / 100 / WEST_HC_DIV);
        T.hc += h; T.hc_earned += h;
    }
    if (!trade_tech(TECH_ELECTRONICS) && T.hc_earned >= TRADE_ELEC_HC && nm >= 1965 * 12) {
        T.techs |= 1u << TECH_ELECTRONICS;
        era_push_msg(EM_ELEC_LICENCE, 0, 0);
    }
    T.tech_bonus = 0;
    if (trade_tech(TECH_MACHINE_TOOLS) && L[BL_TAX_BUS] > 0) {
        s32 b = L[BL_TAX_BUS] * TOOLS_TAX_PCT / 100;
        T.tech_bonus += b;
        if (b) budget_spend(-(Money)b, BL_TAX_BUS);
    }
    if (trade_tech(TECH_COMPUTERS)) {
        s32 svc = 0, b;
        for (k = 0; k < SVC_COUNT; k++) svc -= L[BL_SVC(k)];
        b = IMAX(0, svc) * COMPUTER_UPKEEP_PCT / 100;
        T.tech_bonus += b;
        if (b) budget_spend(-(Money)b, BL_SPECIAL);
    }
    /* ---- loans */
    loans_month();
    /* ---- the calendar: January reviews, period starts, events (nm = the month that begins) */
    if (nm % 12 == 0 && !T.closed) {
        if (T.quota_12 > 0) {
            T.fulfil_pct = (int)((s64)T.deliv_12 * 100 / T.quota_12);
            if (T.fulfil_pct < REVIEW_SHORT_PCT) {
                T.shortfall_months = 12;
                if (pol_on()) politics_meter_add(METER_EAST, -3);
                era_push_msg(EM_SHORTFALL, T.fulfil_pct, 0);
            } else if (T.fulfil_pct >= 100) {
                if (pol_on()) politics_meter_add(METER_EAST, 1);
                era_push_msg(EM_PRAISE, T.fulfil_pct, 0);
            }
        }
        T.wasted_year = 0;
    }
    if (nm == 1973 * 12 + 10 && !T.closed) {           /* the oil shock: supplementary protocol */
        A.supplement = 1;
        era_push_msg(EM_OIL_SHOCK, 400, 0);
    }
    if (nm == TRADE_END_YEAR * 12 && !T.closed) {
        T.closed = 1;
        T.wasted_total += IMAX(0, T.balance);
        era_push_msg(EM_CLEARING_END, T.balance, T.wasted_total);
        T.balance = 0; A.level = -1; A.offer = 0;
        T.consumer = T.materials = 0;
    } else if (!T.closed && nm % 12 == 0 && (nm / 12 - TRADE_FIRST_PERIOD) % TRADE_PERIOD_YEARS == 0 && nm / 12 > TRADE_FIRST_PERIOD)
        new_period(nm / 12);
}

/* ------------------------------------------------------------------ player actions */
int trade_set_programme(int which, int on)
{
    if (T.closed) return 0;
    if (which == 0) T.consumer = on ? 1 : 0;
    else if (which == 1) T.materials = on ? 1 : 0;
    else return 0;
    return 1;
}

static s32 bond_amount(void)
{
    s32 a = 10000 + sim_stats()->population;
    a = a / 1000 * 1000;
    return IMIN(a, 60000);
}

int trade_loan_offer(int kind, s32 *amount, s32 *rate, s32 *months)
{
    s32 a = 0, r = 0, mo = 0;
    switch (kind) {
    case LOAN_BOND: a = bond_amount(); r = 6; mo = 120; break;
    case LOAN_WEST: a = IMIN(2 * bond_amount(), 120000); r = 4; mo = 120; break;
    case LOAN_EAST: a = 8000; r = 1; mo = 180; break;
    default: return 0;
    }
    if (amount) *amount = a;
    if (rate) *rate = r;
    if (months) *months = mo;
    return trade_loan_why(kind, NULL, 0) == 0;
}

int trade_loan_why(int kind, char *buf, int size)
{
    const char *why = NULL;
    if (kind < 0 || kind >= LOAN_KINDS) why = "No such loan.";
    else if (sim_settings()->all_unlocked) why = "Creative mode needs no loans.";
    else if (T.loan[kind].principal > 0) why = "Already running: repay it first.";
    else if (kind == LOAN_WEST && sim_date()->year < 1958) why = "Western banks lend from 1958.";
    else if (kind == LOAN_EAST && T.closed) why = "The clearing trade has ended.";
    else if (kind == LOAN_EAST && pol_on() && politics_meter(METER_EAST) < 35) why = "Eastern relations too cool (35 needed).";
    if (buf && size > 0) str_copy(buf, why ? why : "", size);
    return why ? 1 : 0;
}

static s32 project_amount(int p) { return p == TPROJ_ELEMENTS ? 10000 : p == TPROJ_HEAT ? 6000 : 8000; }

int trade_take_loan(int kind, int project)
{
    s32 amount, rate, months;
    TradeLoan *L;
    if (!trade_loan_offer(kind, &amount, &rate, &months)) return 0;
    L = &T.loan[kind];
    memset(L, 0, sizeof(*L));
    L->project = -1;
    if (kind == LOAN_EAST) {
        project = ICLAMP(project, 0, TPROJ_COUNT - 1);
        amount = project_amount(project);
        L->project = project;
        L->project_base = project_def_count(project);
        L->project_months = 12;
        if (A.level == TL_MODEST) A.level = TL_STANDARD;      /* the credit locks the quota */
        {   int g; A.quota_total = 0; for (g = 0; g < TG_COUNT; g++) { A.quota[g] = A.quota[g] * EAST_CREDIT_PCT / 100; A.quota_total += A.quota[g]; } }
    }
    L->principal = L->orig = amount; L->rate = rate; L->months_left = months;
    budget_spend(-(Money)amount, BL_INTEREST);
    if (kind == LOAN_WEST && pol_on()) politics_meter_add(METER_EAST, -5);
    era_push_msg(EM_LOAN, kind, amount);
    log_printf("trade: loan %s %d mk at %d per mille / month, %d months", trade_loan_name(kind), amount, rate, months);
    return 1;
}

int trade_repay_loan(int kind)
{
    TradeLoan *L;
    if (kind < 0 || kind >= LOAN_KINDS) return 0;
    L = &T.loan[kind];
    if (L->principal <= 0 || !budget_can_afford(L->principal)) return 0;
    budget_spend(L->principal, BL_INTEREST);
    T.loan_paid_total += L->principal;
    L->principal = 0; L->months_left = 0; L->project = -1; L->project_months = 0;
    era_push_msg(EM_LOAN_REPAID, kind, 1);
    return 1;
}

int trade_tech_price(int tech, s32 *hc, s32 *mk, int *year)
{
    s32 h = 0, m = 0;
    int y = 0;
    switch (tech) {
    case TECH_MACHINE_TOOLS: h = 4000; m = 5000; y = 1966; break;
    case TECH_COMPUTERS:     h = 8000; m = 12000; y = 1975; break;
    default: if (year) *year = 1965; if (hc) *hc = TRADE_ELEC_HC; if (mk) *mk = 0; return 0;
    }
    if (hc) *hc = h;
    if (mk) *mk = m;
    if (year) *year = y;
    return 1;
}

int trade_tech_state(int tech)
{
    s32 hc, mk;
    int y;
    if (trade_tech(tech)) return 0;
    if (!trade_tech_price(tech, &hc, &mk, &y)) return sim_date()->year < y ? 2 : 3;
    if (sim_date()->year < y) return 2;
    if (T.hc < hc) return 3;
    if (!budget_can_afford(mk)) return 4;
    return 1;
}

int trade_buy_tech(int tech)
{
    s32 hc, mk;
    int y;
    if (trade_tech_state(tech) != 1) return 0;
    trade_tech_price(tech, &hc, &mk, &y);
    T.hc -= hc;
    budget_spend(mk, BL_CONSTRUCTION);
    T.techs |= 1u << tech;
    era_push_msg(EM_TECH, tech, mk);
    return 1;
}

/* ------------------------------------------------------------------ text */
const char *trade_goods_name(int g)
{
    static const char *const n[TG_COUNT] = { "Machinery", "Prefab houses and timber", "Textiles and shoes", "Dairy products",
                                             "Electronics", "Paper", "Ships and paper machines" };
    return g >= 0 && g < TG_COUNT ? n[g] : "?";
}

int trade_goods_line(int g, s32 volume, char *buf, int size)
{
    /* the goods list in period units ("40,000 pairs of shoes"): volume units per month x 12 x a unit scale */
    static const char *const unit[TG_COUNT] = { "tractors and cranes", "prefabricated houses", "pairs of shoes",
                                                "tonnes of butter", "telephone exchanges", "tonnes of newsprint",
                                                "icebreakers and paper machines" };
    static const s32 per[TG_COUNT] = { 40, 25, 1, 2, 600, 1, 9000 };      /* volume units (a year) per item */
    static const s32 mul[TG_COUNT] = { 1, 1, 40, 1, 1, 3, 1 };
    char a[24];
    s32 n = (s32)((s64)volume * 12 * mul[ICLAMP(g, 0, TG_COUNT - 1)] / per[ICLAMP(g, 0, TG_COUNT - 1)]);
    if (g < 0 || g >= TG_COUNT) return str_printf(buf, size, "?");
    if (n < 1) n = 1;
    text_fmt_int(a, sizeof(a), n);
    return str_printf(buf, size, "%s %s", a, unit[g]);
}

const char *trade_level_name(int level)
{
    static const char *const n[TL_COUNT] = { "Modest", "Standard", "Ambitious" };
    return level >= 0 && level < TL_COUNT ? n[level] : "None";
}
const char *trade_import_name(int i)
{
    static const char *const n[TI_COUNT] = { "Oil for the town", "Oil for plants", "Buses", "Consumer goods", "Building materials" };
    return i >= 0 && i < TI_COUNT ? n[i] : "?";
}
const char *trade_loan_name(int kind)
{
    static const char *const n[LOAN_KINDS] = { "Domestic bond", "Western bank loan", "Eastern credit" };
    return kind >= 0 && kind < LOAN_KINDS ? n[kind] : "?";
}
const char *trade_project_name(int p)
{
    static const char *const n[TPROJ_COUNT] = { "a power plant", "a heat plant", "an element factory" };
    return p >= 0 && p < TPROJ_COUNT ? n[p] : "a project";
}
const char *trade_tech_name(int t)
{
    static const char *const n[TECH_COUNT] = { "Electronics licence", "Western machine tools", "Computers at the town hall" };
    return t >= 0 && t < TECH_COUNT ? n[t] : "?";
}

/* ------------------------------------------------------------------ debug, save */
void trade_dump(FILE *f)
{
    int g, k;
    if (!f) return;
    fprintf(f, "trade %d-%02d: agreement %d-%d %s%s%s, quota %d/month (now %d), fulfilment %d%% (12m %d/%d)\n",
            sim_date()->year, sim_date()->month + 1, A.from_year, A.to_year, trade_level_name(A.level),
            A.offer ? " (offer waiting)" : "", A.supplement ? " +supplement" : "", A.quota_total, quota_total_now(),
            T.fulfil_pct, T.deliv_12, T.quota_12);
    fprintf(f, "  clearing balance %d (tolerance %d), exports %d, imports %d:", T.balance, T.tolerance, T.exports_east, T.imports_total);
    for (k = 0; k < TI_COUNT; k++) fprintf(f, " %s %d", trade_import_name(k), T.imports[k]);
    fprintf(f, "\n  wasted month %d year %d total %d, deficit months %d, shortfall %d, programmes consumer %d materials %d%s\n",
            T.wasted_month, T.wasted_year, T.wasted_total, T.deficit_months, T.shortfall_months, T.consumer, T.materials,
            T.closed ? ", CLOSED" : "");
    fprintf(f, "  deliveries:");
    for (g = 0; g < TG_COUNT; g++) if (T.delivered[g] || A.quota[g]) fprintf(f, " %s %d/%d", trade_goods_name(g), T.delivered[g], trade_quota_now(g));
    fprintf(f, "\n  oil %d coal %d west index %d, west exports %d, premium %d mk, fuel %d mk, hc %d (earned %d), techs %x, bonus %d\n",
            T.oil_index, T.coal_index, T.west_index, T.exports_west, T.west_income, T.fuel_cost, T.hc, T.hc_earned, T.techs, T.tech_bonus);
    for (k = 0; k < LOAN_KINDS; k++)
        if (T.loan[k].principal > 0)
            fprintf(f, "  loan %s: %d of %d at %d, %d months left, project %d (%d months)\n", trade_loan_name(k), T.loan[k].principal,
                    T.loan[k].orig, T.loan[k].rate, T.loan[k].months_left, T.loan[k].project, T.loan[k].project_months);
    fprintf(f, "  clearing pct %d, western pct %d, emigration pct %d\n", trade_clearing_pct(), trade_western_pct(), trade_emigration_pct());
}

u32 trade_hash(void)
{
    u32 h = HASH_INIT;
    s32 v[16];
    int k;
    v[0] = T.balance; v[1] = T.wasted_total; v[2] = T.west_index; v[3] = T.hc; v[4] = T.hc_earned; v[5] = (s32)T.techs;
    v[6] = A.level; v[7] = A.quota_total; v[8] = A.offer; v[9] = T.deficit_months; v[10] = T.shortfall_months;
    v[11] = T.consumer * 2 + T.materials; v[12] = (s32)s_rng.s; v[13] = T.closed; v[14] = T.fulfil_pct; v[15] = A.from_year;
    h = hash_bytes(h, v, sizeof(v));
    for (k = 0; k < LOAN_KINDS; k++) { v[0] = T.loan[k].principal; v[1] = T.loan[k].months_left; v[2] = T.loan[k].project_months; h = hash_bytes(h, v, 3 * sizeof(s32)); }
    return h;
}

static void save_state_s32s(struct SaveFile *sf, const s32 *p, int n) { int i; for (i = 0; i < n; i++) save_s32(sf, p[i]); }
static void load_state_s32s(struct SaveFile *sf, s32 *p, int n) { int i; for (i = 0; i < n; i++) p[i] = load_s32(sf); }

int trade_save(struct SaveFile *sf)
{
    int k;
    save_begin_chunk(sf, "TRAD", 1);
    /* agreement */
    save_s32(sf, A.from_year); save_s32(sf, A.to_year); save_s32(sf, A.level); save_s32(sf, TG_COUNT);
    save_state_s32s(sf, A.quota, TG_COUNT);
    save_s32(sf, A.quota_total); save_s32(sf, A.offer); save_s32(sf, A.offer_months); save_s32(sf, A.supplement);
    /* state */
    save_s32(sf, T.balance); save_s32(sf, T.tolerance); save_s32(sf, T.exports_east); save_s32(sf, TI_COUNT);
    save_state_s32s(sf, T.imports, TI_COUNT);
    save_s32(sf, T.imports_total);
    save_state_s32s(sf, T.delivered, TG_COUNT);
    save_s32(sf, T.deliv_12); save_s32(sf, T.quota_12); save_s32(sf, T.fulfil_pct);
    save_s32(sf, T.wasted_month); save_s32(sf, T.wasted_year); save_s32(sf, T.wasted_total);
    save_s32(sf, T.deficit_months); save_s32(sf, T.shortfall_months); save_s32(sf, T.consumer); save_s32(sf, T.materials);
    save_s32(sf, T.oil_index); save_s32(sf, T.coal_index); save_s32(sf, T.west_index); save_s32(sf, T.exports_west);
    save_s32(sf, T.west_income); save_s32(sf, T.fuel_cost); save_s32(sf, T.hc); save_s32(sf, T.hc_earned);
    save_u32(sf, T.techs); save_s32(sf, T.tech_bonus);
    save_s32(sf, LOAN_KINDS);
    for (k = 0; k < LOAN_KINDS; k++) save_state_s32s(sf, &T.loan[k].principal, 7);
    save_s32(sf, T.loan_paid_total); save_s32(sf, T.loan_interest_total); save_s32(sf, T.closed);
    save_u32(sf, s_rng.s);
    save_state_s32s(sf, s_deliv_hist, 12); save_state_s32s(sf, s_quota_hist, 12); save_s32(sf, s_hist_pos);
    for (k = 0; k < TRADE_COMPLEX_SLOTS; k++) { save_s32(sf, s_cx_goods[k]); save_s32(sf, s_cx_east[k]); save_s32(sf, s_cx_west[k]); }
    save_end_chunk(sf);
    return 1;
}

int trade_load(struct SaveFile *sf)
{
    int ver, k, n;
    s32 tmp[32];
    if (!load_find_chunk(sf, "TRAD", &ver)) return 0;
    A.from_year = load_s32(sf); A.to_year = load_s32(sf); A.level = load_s32(sf);
    n = load_s32(sf); if (n < 0 || n > 32) return 0;
    load_state_s32s(sf, tmp, n);
    for (k = 0; k < TG_COUNT; k++) A.quota[k] = k < n ? tmp[k] : 0;
    A.quota_total = load_s32(sf); A.offer = load_s32(sf); A.offer_months = load_s32(sf); A.supplement = load_s32(sf);
    T.balance = load_s32(sf); T.tolerance = load_s32(sf); T.exports_east = load_s32(sf);
    n = load_s32(sf); if (n < 0 || n > 32) return 0;
    load_state_s32s(sf, tmp, n);
    for (k = 0; k < TI_COUNT; k++) T.imports[k] = k < n ? tmp[k] : 0;
    T.imports_total = load_s32(sf);
    load_state_s32s(sf, T.delivered, TG_COUNT);
    T.deliv_12 = load_s32(sf); T.quota_12 = load_s32(sf); T.fulfil_pct = load_s32(sf);
    T.wasted_month = load_s32(sf); T.wasted_year = load_s32(sf); T.wasted_total = load_s32(sf);
    T.deficit_months = load_s32(sf); T.shortfall_months = load_s32(sf); T.consumer = load_s32(sf); T.materials = load_s32(sf);
    T.oil_index = load_s32(sf); T.coal_index = load_s32(sf); T.west_index = load_s32(sf); T.exports_west = load_s32(sf);
    T.west_income = load_s32(sf); T.fuel_cost = load_s32(sf); T.hc = load_s32(sf); T.hc_earned = load_s32(sf);
    T.techs = load_u32(sf); T.tech_bonus = load_s32(sf);
    n = load_s32(sf); if (n < 0 || n > 8) return 0;
    for (k = 0; k < n; k++) { load_state_s32s(sf, tmp, 7); if (k < LOAN_KINDS) memcpy(&T.loan[k].principal, tmp, 7 * sizeof(s32)); }
    T.loan_paid_total = load_s32(sf); T.loan_interest_total = load_s32(sf); T.closed = load_s32(sf);
    s_rng.s = load_u32(sf);
    load_state_s32s(sf, s_deliv_hist, 12); load_state_s32s(sf, s_quota_hist, 12); s_hist_pos = ICLAMP(load_s32(sf), 0, 11);
    for (k = 0; k < TRADE_COMPLEX_SLOTS; k++) { s_cx_goods[k] = load_s32(sf); s_cx_east[k] = load_s32(sf); s_cx_west[k] = load_s32(sf); }
    s_def_heat = -2;
    UNUSED(ver);
    return load_ok(sf);
}
