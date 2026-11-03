/* trade.cpp - M7: clearing agreement + balance meter, oil prices, Western demand multiplier, the bond. PORTABLE,
   deterministic, integer-only. Owner: M7. See trade.h for the model. The Western price walk uses its own RNG (seeded
   from the city seed, saved), so the simulation RNG stream is never touched. Money is booked with budget_spend at
   month end: fuel on BL_FUEL ("Fuel (oil, coal)"), the bond on BL_INTEREST ("Loans and interest"). */
#include "common.h"
#include "trade.h"
#include "era.h"
#include "industry.h"
#include "politics.h"
#include "sim.h"
#include "world.h"
#include "budget.h"
#include "text.h"
#include "save.h"
#include "util.h"

#define LEVEL_PCT_MODEST     85
#define LEVEL_PCT_AMBITIOUS  120
#define SHORTFALL_PCT        90
#define DEFICIT_PCT          90
#define DEFICIT_AFTER        3
#define LOSS_DIV             4
#define BOND_RATE            6       /* per mille per month */
#define BOND_MONTHS          120

static TradeState     T;
static TradeAgreement A;
static Rng            s_rng;
static s32            s_cx_goods[TRADE_COMPLEX_SLOTS], s_cx_east[TRADE_COMPLEX_SLOTS], s_cx_west[TRADE_COMPLEX_SLOTS];
static s32            s_deliv_hist[12], s_quota_hist[12];
static int            s_hist_pos;

static int month_now(void) { return sim_date()->year * 12 + sim_date()->month; }   /* the month being closed */
static int pol_on(void) { return politics_enabled(); }

/* ------------------------------------------------------------------ prices as functions of the date (m = year*12+month) */
static int oil_index_at(int m)
{
    if (m < 1973 * 12 + 10) return 100;
    if (m < 1979 * 12) return 400;
    if (m < 1981 * 12) return 550;
    if (m < 1986 * 12 + 2) return 500;
    return 250;
}
static int coal_index_at(int m) { return m < 1974 * 12 ? 100 : m < 1986 * 12 ? 180 : 130; }
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
    if (m >= 1957 * 12 + 8 && m < 1960 * 12) return 110;   /* 1957 devaluation */
    if (m >= 1967 * 12 + 9 && m < 1970 * 12) return 110;   /* 1967 devaluation */
    if (y >= 1975 && y <= 1977) return 85;                 /* the Western recession */
    if (y >= 1980 && y <= 1984) return 110;
    if (y >= 1985) return 105;
    return 100;
}
static int oil_rate(int y) { return 120 + 4 * (ICLAMP(y, 1955, 1972) - 1955); }   /* per 1,000 residents / month */
/* the clearing price of oil follows the world price as a 5-year moving average (the real clearing rule), and only half
   of the rise reaches the town's share: 1974 ~130, 1978 ~240, 1983 ~300, then down */
static int clearing_oil_at(int m)
{
    s32 sum = 0;
    int k;
    for (k = 0; k < 60; k++) sum += oil_index_at(m - k);
    return 100 + (sum / 60 - 100) / 2;
}

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

static void deliveries(s32 *east, s32 *et, s32 *wt)
{
    const IndKinds *k = industry_kinds();
    int i;
    for (i = 0; i < TG_COUNT; i++) east[i] = 0;
    *wt = k->west_total;
    for (i = IK_WOOD; i < IK_COUNT; i++) east[goods_of_kind(i)] += k->export_east[i];
    for (i = 0; i < TRADE_COMPLEX_SLOTS; i++)
        if (s_cx_goods[i] >= 0 && s_cx_goods[i] < TG_COUNT) { east[s_cx_goods[i]] += s_cx_east[i]; *wt += s_cx_west[i]; }
    *et = 0;
    for (i = 0; i < TG_COUNT; i++) *et += east[i];
}

void trade_set_complex(int slot, int goods, s32 east, s32 west)
{
    if (slot < 0 || slot >= TRADE_COMPLEX_SLOTS) return;
    s_cx_goods[slot] = (east || west) ? goods : -1;
    s_cx_east[slot] = IMAX(0, east); s_cx_west[slot] = IMAX(0, west);
}

/* ------------------------------------------------------------------ the agreement */
static int level_pct(int level) { return level == TL_MODEST ? LEVEL_PCT_MODEST : level == TL_AMBITIOUS ? LEVEL_PCT_AMBITIOUS : 100; }

static void make_quota(int level)
{
    s32 east[TG_COUNT], et, wt;
    int g;
    deliveries(east, &et, &wt);
    A.quota_total = 0;
    for (g = 0; g < TG_COUNT; g++) {
        s32 base = east[g];
        if (g == TG_MACHINERY) base = IMAX(base, 20 + sim_stats()->population / 20);
        A.quota[g] = (s32)((s64)base * level_pct(level) / 100);
        A.quota_total += A.quota[g];
    }
}

int trade_level_allowed(int level)
{
    if (level < 0 || level >= TL_COUNT) return 0;
    if (level == TL_AMBITIOUS && pol_on() && politics_meter(METER_EAST) < 40) return 0;
    return 1;
}

static void sign(int level, int automatic)
{
    A.level = level;
    A.offer = 0; A.offer_months = 0;
    make_quota(level);
    era_push_msg(automatic ? EM_AUTO_SIGNED : EM_SIGNED, level, A.from_year);
    log_printf("trade: agreement %d-%d signed: %s, goods list %d / month", A.from_year, A.to_year, trade_level_name(level), A.quota_total);
}

int trade_sign(int level)
{
    if (!A.offer || T.closed) return 0;
    if (!trade_level_allowed(level)) level = TL_STANDARD;    /* the Delegation smiles and writes "Standard" */
    sign(level, 0);
    return 1;
}

static void new_period(int year)
{
    int from, to;
    if (!trade_period_of(year, &from, &to)) return;
    A.from_year = from; A.to_year = to;
    A.offer = 1; A.offer_months = 0;
    era_push_msg(EM_OFFER, from, to);
    if (pol_on()) era_queue_card();          /* the agreement card (era.cpp: politics' card queue) */
}

/* ------------------------------------------------------------------ lifecycle */
void trade_reset(void)
{
    int i, y = sim_date()->year;
    memset(&T, 0, sizeof(T));
    memset(&A, 0, sizeof(A));
    memset(s_deliv_hist, 0, sizeof(s_deliv_hist)); memset(s_quota_hist, 0, sizeof(s_quota_hist));
    s_hist_pos = 0;
    for (i = 0; i < TRADE_COMPLEX_SLOTS; i++) { s_cx_goods[i] = -1; s_cx_east[i] = s_cx_west[i] = 0; }
    rng_seed(&s_rng, sim_settings()->seed * 2246822519u ^ 0x7a3dc1e5u);
    T.fulfil_pct = -1;
    T.oil_index = oil_index_at(y * 12);
    T.coal_index = coal_index_at(y * 12);
    T.clearing_oil = clearing_oil_at(y * 12);
    T.west_index = west_baseline_at(y * 12);
    T.closed = y >= TRADE_END_YEAR;
    if (!T.closed) {           /* the running agreement, signed Standard (a 1955 start: a preliminary protocol) */
        trade_period_of(y, &A.from_year, &A.to_year);
        if (y < TRADE_FIRST_PERIOD) A.from_year = y;
        A.level = TL_STANDARD;
        make_quota(TL_STANDARD);
    } else A.level = -1;
}

const TradeState *trade_state(void) { return &T; }
const TradeAgreement *trade_agreement(void) { return &A; }

/* ------------------------------------------------------------------ hooks */
int trade_clearing_pct(void)
{
    s64 p;
    if (T.closed || sim_date()->year >= TRADE_END_YEAR) return 100;
    p = level_pct(A.level >= 0 ? A.level : TL_STANDARD);
    p = p * clearing_boom_at(month_now()) / 100;
    if (T.shortfall_months > 0) p = p * SHORTFALL_PCT / 100;
    if (T.deficit_months >= DEFICIT_AFTER) p = p * DEFICIT_PCT / 100;
    return (int)ICLAMP(p, (s64)10, (s64)400);
}
int trade_western_pct(void) { return ICLAMP(T.west_index, 50, 150); }

int trade_clearing_reason(char *buf, int size)
{
    int boom = clearing_boom_at(month_now()), n;
    if (T.closed || sim_date()->year >= TRADE_END_YEAR) return str_printf(buf, size, "Clearing trade has ended");
    n = str_printf(buf, size, "%s agreement", trade_level_name(A.level >= 0 ? A.level : TL_STANDARD));
    if (boom != 100) n += str_printf(buf + n, size - n, ", oil %s %s%d%%", boom > 100 ? "boom" : "slump", boom > 100 ? "+" : TEXT_MINUS, IABS(boom - 100));
    if (T.shortfall_months > 0) n += str_printf(buf + n, size - n, ", shortfall " TEXT_MINUS "10%%");
    if (T.deficit_months >= DEFICIT_AFTER) n += str_printf(buf + n, size - n, ", deficit " TEXT_MINUS "10%%");
    return n;
}

/* ------------------------------------------------------------------ month end */
static int count_def(int def)
{
    int i, n = 0;
    if (def <= B_NONE) return 0;
    for (i = 1; i < g_world.lot_hi; i++) if (g_world.lots[i].def == def && g_world.lots[i].status == LOT_ACTIVE) n++;
    return n;
}

static s32 quota_total_now(void) { return A.level >= 0 && !T.closed ? A.quota_total : 0; }

void trade_month(void)
{
    s32 east[TG_COUNT], et, wt, pop = sim_stats()->population;
    int m = month_now(), nm = m + 1, g, k, oil_plants, coal_plants;
    T.oil_index = oil_index_at(m);
    T.coal_index = coal_index_at(m);
    T.clearing_oil = clearing_oil_at(m);
    deliveries(east, &et, &wt);
    for (g = 0; g < TG_COUNT; g++) T.delivered[g] = east[g];
    oil_plants = count_def(B_OIL_PLANT);
    coal_plants = count_def(B_COAL_PLANT);
    /* ---- fuel (mk, BL_FUEL): oil plants at the oil index, coal plants' surcharge from 1974 */
    {
        s64 fuel = (s64)oil_plants * OIL_PLANT_FUEL * T.oil_index / 100;
        fuel += (s64)coal_plants * COAL_SURCHARGE_MK * (T.coal_index - 100) / 100;
        fuel = fuel * budget_upkeep_index() / 100;
        T.fuel_cost = (s32)fuel;
        if (fuel) budget_spend((Money)fuel, BL_FUEL);
    }
    /* ---- the clearing meter: exports - oil */
    if (!T.closed) {
        T.exports_east = et;
        T.oil_town = (s32)((s64)pop * oil_rate(sim_date()->year) / 1000 * T.clearing_oil / 100);
        T.oil_plants = oil_plants * OIL_PLANT_UNITS * T.clearing_oil / 100;
        T.balance += et - T.oil_town - T.oil_plants;
        T.tolerance = 3 * et + 200;
        T.lost_month = 0;
        if (T.balance > T.tolerance) {
            s32 w = (T.balance - T.tolerance + LOSS_DIV - 1) / LOSS_DIV;
            T.balance -= w; T.lost_month = w; T.lost_total += w;
        }
        if (T.balance < -3 * T.tolerance) T.balance = -3 * T.tolerance;   /* the East extends no further credit */
        if (T.balance < -T.tolerance) {
            T.deficit_months++;
            if (pol_on() && (T.deficit_months & 1) == 0) politics_meter_add(METER_EAST, -1);
            if (T.deficit_months == DEFICIT_AFTER) era_push_msg(EM_DEFICIT, T.balance, T.tolerance);
        } else T.deficit_months = 0;
        s_deliv_hist[s_hist_pos] = et; s_quota_hist[s_hist_pos] = quota_total_now();
        s_hist_pos = (s_hist_pos + 1) % 12;
        T.deliv_12 = T.quota_12 = 0;
        for (k = 0; k < 12; k++) { T.deliv_12 += s_deliv_hist[k]; T.quota_12 += s_quota_hist[k]; }
        if (T.shortfall_months > 0) T.shortfall_months--;
        if (A.offer && ++A.offer_months >= TRADE_OFFER_MONTHS && !(pol_on() && era_card_pending())) sign(TL_STANDARD, 1);
    } else { T.exports_east = T.oil_town = T.oil_plants = 0; }
    /* ---- the Western multiplier */
    T.exports_west = wt;
    T.west_index += rng_range(&s_rng, -2, 2) + (west_baseline_at(nm) - T.west_index) / 6;
    T.west_index = ICLAMP(T.west_index, 60, 140);
    /* ---- the bond */
    if (T.bond > 0) {
        s32 interest = IMAX(1, (s32)((s64)T.bond * BOND_RATE / 1000));
        s32 repay = T.bond_months > 1 ? IMIN(T.bond, (T.bond + T.bond_months - 1) / T.bond_months) : T.bond;
        T.bond -= repay; T.bond_months--;
        budget_spend(interest + repay, BL_INTEREST);
        if (T.bond <= 0) { T.bond = 0; T.bond_months = 0; era_push_msg(EM_LOAN_REPAID, 0, 0); }
    }
    /* ---- the calendar (nm = the month that begins) */
    if (nm % 12 == 0 && !T.closed && T.quota_12 > 0) {
        T.fulfil_pct = (int)((s64)T.deliv_12 * 100 / T.quota_12);
        if (T.fulfil_pct < TRADE_REVIEW_PCT) {
            T.shortfall_months = 12;
            if (pol_on()) politics_meter_add(METER_EAST, -3);
            era_push_msg(EM_SHORTFALL, T.fulfil_pct, 0);
        }
        if (A.from_year < TRADE_FIRST_PERIOD && A.level >= 0) make_quota(A.level);   /* the preliminary protocol: yearly lists */
    }
    if (nm == 1973 * 12 + 10 && !T.closed) era_push_msg(EM_OIL_SHOCK, 400, 0);
    if (nm == TRADE_END_YEAR * 12 && !T.closed) {
        T.closed = 1;
        era_push_msg(EM_CLEARING_END, T.balance, T.lost_total);
        T.balance = 0; A.level = -1; A.offer = 0;
    } else if (!T.closed && nm % 12 == 0 && nm / 12 > TRADE_FIRST_PERIOD && (nm / 12 - TRADE_FIRST_PERIOD) % TRADE_PERIOD_YEARS == 0)
        new_period(nm / 12);
}

/* ------------------------------------------------------------------ the bond */
s32 trade_bond_amount(void) { return IMIN(60000, (10000 + sim_stats()->population) / 1000 * 1000); }

int trade_issue_bond(void)
{
    s32 a;
    if (T.bond > 0 || g_budget.unlimited) return 0;
    a = trade_bond_amount();
    T.bond = T.bond_orig = a; T.bond_months = BOND_MONTHS;
    budget_spend(-(Money)a, BL_INTEREST);
    era_push_msg(EM_LOAN, 0, a);
    log_printf("trade: bond %d mk at %d per mille / month, %d months", a, BOND_RATE, BOND_MONTHS);
    return 1;
}

/* ------------------------------------------------------------------ text */
const char *trade_goods_name(int g)
{
    static const char *const n[TG_COUNT] = { "Machinery", "Prefab houses", "Textiles and shoes", "Dairy products",
                                             "Electronics", "Paper", "Ships and paper machines" };
    return g >= 0 && g < TG_COUNT ? n[g] : "?";
}

int trade_goods_line(int g, s32 volume, char *buf, int size)
{
    /* the goods list in period units ("40,000 pairs of shoes"): volume units a month x 12 x scale */
    static const char *const unit[TG_COUNT] = { "tractors and cranes", "prefabricated houses", "pairs of shoes",
                                                "tonnes of butter", "telephone exchanges", "tonnes of newsprint",
                                                "icebreakers and paper machines" };
    static const s32 per[TG_COUNT] = { 40, 25, 1, 2, 600, 1, 9000 };
    static const s32 mul[TG_COUNT] = { 1, 1, 40, 1, 1, 3, 1 };
    char a[24];
    s32 n;
    if (g < 0 || g >= TG_COUNT) return str_printf(buf, size, "?");
    n = IMAX(1, (s32)((s64)volume * 12 * mul[g] / per[g]));
    text_fmt_int(a, sizeof(a), n);
    return str_printf(buf, size, "%s %s", a, unit[g]);
}

const char *trade_level_name(int level)
{
    static const char *const n[TL_COUNT] = { "Modest", "Standard", "Ambitious" };
    return level >= 0 && level < TL_COUNT ? n[level] : "No";
}

int trade_balance_tip(char lines[][80], int max)
{
    char a[24], b[24];
    int n = 0;
    if (max <= 0) return 0;
    if (T.closed) { str_printf(lines[n++], 80, "Clearing trade ended in 1990."); return n; }
    text_fmt_signed(a, sizeof(a), T.balance); text_fmt_int(b, sizeof(b), T.tolerance);
    str_printf(lines[n++], 80, "Clearing balance %s (keep within %s either way)", a, b);
    text_fmt_int(a, sizeof(a), T.exports_east); text_fmt_int(b, sizeof(b), T.oil_town + T.oil_plants);
    if (n < max) str_printf(lines[n++], 80, "Exports %s, oil %s a month (clearing oil price %d%%)", a, b, T.clearing_oil);
    if (n < max) {
        if (T.lost_month) { text_fmt_int(a, sizeof(a), T.lost_month); str_printf(lines[n++], 80, "Surplus: %s credit lost this month", a); }
        else if (T.deficit_months >= DEFICIT_AFTER) str_printf(lines[n++], 80, "Deficit: clearing orders " TEXT_MINUS "10%%%s", pol_on() ? ", the East cools" : "");
        else str_printf(lines[n++], 80, "Balanced: the Delegation is content");
    }
    return n;
}

/* ------------------------------------------------------------------ debug, save */
void trade_dump(FILE *f)
{
    int g;
    if (!f) return;
    fprintf(f, "trade %d-%02d: agreement %d-%d %s%s, goods list %d/month, review %d%% (12m %d/%d)\n", sim_date()->year,
            sim_date()->month + 1, A.from_year, A.to_year, trade_level_name(A.level), A.offer ? " (card waiting)" : "",
            A.quota_total, T.fulfil_pct, T.deliv_12, T.quota_12);
    fprintf(f, "  clearing balance %d (tolerance %d): exports %d, oil town %d plants %d, lost %d (total %d), deficit months %d, shortfall %d%s\n",
            T.balance, T.tolerance, T.exports_east, T.oil_town, T.oil_plants, T.lost_month, T.lost_total, T.deficit_months,
            T.shortfall_months, T.closed ? ", CLOSED" : "");
    fprintf(f, "  deliveries:");
    for (g = 0; g < TG_COUNT; g++) if (T.delivered[g] || A.quota[g]) fprintf(f, " %s %d/%d", trade_goods_name(g), T.delivered[g], A.quota[g]);
    fprintf(f, "\n  clearing oil %d, oil %d coal %d west index %d (west exports %d), fuel %d mk, bond %d (%d months); clearing pct %d western pct %d\n",
            T.clearing_oil, T.oil_index, T.coal_index, T.west_index, T.exports_west, T.fuel_cost, T.bond, T.bond_months, trade_clearing_pct(), trade_western_pct());
}

u32 trade_hash(void)
{
    s32 v[14];
    v[0] = T.balance; v[1] = T.lost_total; v[2] = T.west_index; v[3] = T.bond; v[4] = T.bond_months; v[5] = A.level;
    v[6] = A.quota_total; v[7] = A.offer; v[8] = T.deficit_months; v[9] = T.shortfall_months; v[10] = (s32)s_rng.s;
    v[11] = T.closed; v[12] = T.fulfil_pct; v[13] = A.from_year;
    return hash_bytes(HASH_INIT, v, sizeof(v));
}

int trade_save(struct SaveFile *sf)
{
    int k;
    save_begin_chunk(sf, "TRAD", 1);
    save_s32(sf, A.from_year); save_s32(sf, A.to_year); save_s32(sf, A.level); save_s32(sf, TG_COUNT);
    for (k = 0; k < TG_COUNT; k++) save_s32(sf, A.quota[k]);
    save_s32(sf, A.quota_total); save_s32(sf, A.offer); save_s32(sf, A.offer_months);
    save_s32(sf, T.balance); save_s32(sf, T.tolerance); save_s32(sf, T.exports_east); save_s32(sf, T.oil_town); save_s32(sf, T.oil_plants);
    for (k = 0; k < TG_COUNT; k++) save_s32(sf, T.delivered[k]);
    save_s32(sf, T.deliv_12); save_s32(sf, T.quota_12); save_s32(sf, T.fulfil_pct); save_s32(sf, T.lost_month); save_s32(sf, T.lost_total);
    save_s32(sf, T.deficit_months); save_s32(sf, T.shortfall_months); save_s32(sf, T.oil_index); save_s32(sf, T.coal_index);
    save_s32(sf, T.west_index); save_s32(sf, T.exports_west); save_s32(sf, T.fuel_cost); save_s32(sf, T.clearing_oil);
    save_s32(sf, T.bond); save_s32(sf, T.bond_orig); save_s32(sf, T.bond_months); save_s32(sf, T.closed);
    save_u32(sf, s_rng.s);
    for (k = 0; k < 12; k++) { save_s32(sf, s_deliv_hist[k]); save_s32(sf, s_quota_hist[k]); }
    save_s32(sf, s_hist_pos);
    for (k = 0; k < TRADE_COMPLEX_SLOTS; k++) { save_s32(sf, s_cx_goods[k]); save_s32(sf, s_cx_east[k]); save_s32(sf, s_cx_west[k]); }
    save_end_chunk(sf);
    return 1;
}

int trade_load(struct SaveFile *sf)
{
    int ver, k, n;
    if (!load_find_chunk(sf, "TRAD", &ver)) return 0;
    A.from_year = load_s32(sf); A.to_year = load_s32(sf); A.level = load_s32(sf);
    n = load_s32(sf);
    if (n != TG_COUNT) return 0;
    for (k = 0; k < TG_COUNT; k++) A.quota[k] = load_s32(sf);
    A.quota_total = load_s32(sf); A.offer = load_s32(sf); A.offer_months = load_s32(sf);
    T.balance = load_s32(sf); T.tolerance = load_s32(sf); T.exports_east = load_s32(sf); T.oil_town = load_s32(sf); T.oil_plants = load_s32(sf);
    for (k = 0; k < TG_COUNT; k++) T.delivered[k] = load_s32(sf);
    T.deliv_12 = load_s32(sf); T.quota_12 = load_s32(sf); T.fulfil_pct = load_s32(sf); T.lost_month = load_s32(sf); T.lost_total = load_s32(sf);
    T.deficit_months = load_s32(sf); T.shortfall_months = load_s32(sf); T.oil_index = load_s32(sf); T.coal_index = load_s32(sf);
    T.west_index = load_s32(sf); T.exports_west = load_s32(sf); T.fuel_cost = load_s32(sf); T.clearing_oil = load_s32(sf);
    T.bond = load_s32(sf); T.bond_orig = load_s32(sf); T.bond_months = load_s32(sf); T.closed = load_s32(sf);
    s_rng.s = load_u32(sf);
    for (k = 0; k < 12; k++) { s_deliv_hist[k] = load_s32(sf); s_quota_hist[k] = load_s32(sf); }
    s_hist_pos = load_s32(sf);              /* (never a load call inside ICLAMP: it evaluates twice) */
    s_hist_pos = ICLAMP(s_hist_pos, 0, 11);
    for (k = 0; k < TRADE_COMPLEX_SLOTS; k++) { s_cx_goods[k] = load_s32(sf); s_cx_east[k] = load_s32(sf); s_cx_west[k] = load_s32(sf); }
    UNUSED(ver);
    return load_ok(sf);
}
