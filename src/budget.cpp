/* budget.cpp - money, monthly ledger, taxes, service funding, emergency loan. PORTABLE. Owner: T04.
   See budget.h for the formulas. Integer-only; sub-markka remainders carried in file statics. */
#include "common.h"
#include "civic.h"
#include "budget.h"
#include "season.h"          /* 1.1 W1: the Winter ledger line */
#include "industry.h"
#include "complex.h"
#include "world.h"
#include "sim.h"
#include "save.h"
#include "util.h"

/* T12 balance constants (BALANCE.md). Resident tax per month = sum over homes of
   occupants * (RES_TAX_LV_BASE + land value) * tier weight * tax% / RES_TAX_DIV with the weights below:
   at 9% a cottage resident pays ~0.03 mk (LV 35), a rintamamiestalo resident ~0.17 (LV 48), a tier-3
   resident ~0.27 (LV 55). Cottage villages live on grants and business tax; services pay back through
   upgrades (BALANCE.md). */
static const int s_tax_weight[2][4] = { { 0, 1, 4, 6 },      /* houses T1..T3 */
                                        { 0, 3, 4, 5 } };    /* apartments T1..T3 (P1: was 4/6/9 - with T2/T3 flats now
                                                                growing on their own, a precast lahio paid 1.5x a tier-3
                                                                house per resident and dense towns piled up 180k+ by 1975;
                                                                PL2: 4/5/7 -> 3/4/5: rented flats, working-class incomes, and
                                                                a block needs half the power, pipes and streets per resident of
                                                                a house; with T2 no longer waiting for elements the dense bot
                                                                reached 358k by 1990) */
/* Upkeep index (wage/price level, %): 100 until UPKEEP_INFL_FROM, then +UPKEEP_INFL_PCT per year (1970: 125%), capped
   at UPKEEP_INFL_CAP (P1: 130% from 1972; was 150% from 1980). Applied to service, road, network, utility and special
   upkeep: the welfare-state years get dearer, so a built-out town doesn't pile up money (T04 note) unless it keeps
   developing (tiers, land value) or raises taxes. */
#define UPKEEP_INFL_FROM 1960
#define UPKEEP_INFL_PCT  25          /* per mille per year */
#define UPKEEP_INFL_CAP  130         /* P1: was 150. With M5-M7's services, fuel and landmarks a built-out 1980s town at
                                        150% lost ~900 mk a month (planner bot -101k by 1990, 3 emergency loans); at 130%
                                        it runs about even and the oil-shock fuel bill (1974-86) is what tips it negative */
static int upkeep_index(void)
{
    int y = sim_date()->year - UPKEEP_INFL_FROM;
    return y <= 0 ? 100 : IMIN((int)diff()->upkeep_cap, 100 + y * UPKEEP_INFL_PCT / 10);   /* 1.1 D1: cap by level
                                                                   (Easy = UPKEEP_INFL_CAP 130, Normal 145, Hard 160) */
}
int budget_upkeep_index(void) { return upkeep_index(); }    /* F3: shown in the Budget panel and Query */

/* 1.1 D1: the level's upkeep surcharge and tax-yield cut phase in with the welfare state (like the upkeep index): none
   before 1960, a fifth more each year, in full from 1965. The village years stay as on Easy (the bots' 1.0 early game
   already runs within a few hundred markka of the emergency loan); the pressure lands on the built-up town (coasting). */
static int phase_in(int pct)
{
    int y = sim_date()->year - UPKEEP_INFL_FROM;
    if (pct == 100 || y <= 0) return 100;
    return 100 + (pct - 100) * IMIN(y, 5) / 5;
}
int budget_level_upkeep_pct(void) { return phase_in(diff()->upkeep_pct); }
int budget_level_tax_pct(void) { return phase_in(diff()->tax_yield_pct); }

/* PL2: overtime. A service running above OVERTIME_FREE_PCT of its capacity (town-wide load / capacity, sim stats) pays
   its staff overtime: the line's upkeep grows with the load beyond that, up to OVERTIME_MAX_PCT. Without it a town that
   never built a second fire station or clinic served 19,000 residents for the price of 10,000 and the skimping bots
   (dense, naive) piled up 130-250k by 1990, while the planner that built enough broke even. */
#define OVERTIME_FREE_PCT 125
#define OVERTIME_MAX_PCT  300
int budget_overtime_pct(int svc)
{
    const CityStats *st = sim_stats();
    s32 cap, load;
    if (svc < 0 || svc >= SVC_COUNT) return 100;
    cap = st->svc_capacity[svc]; load = st->svc_load[svc];
    if (cap <= 0) return 100;
    return (int)ICLAMP((s64)load * 100 / cap - (diff()->overtime_free - 100), (s64)100, (s64)OVERTIME_MAX_PCT);   /* 1.1 D1:
                                                                   overtime from OVERTIME_FREE_PCT (Easy) / 115 / 105 % */
}
Money budget_overtime_mk(int svc)
{
    s64 sum = 0;
    int i, ot = budget_overtime_pct(svc);
    if (ot <= 100 || svc < 0 || svc >= SVC_COUNT) return 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        if (l->def == B_NONE) continue;
        d = def_building(l->def);
        if (d->cat == BCAT_SERVICE && d->service == svc) sum += d->upkeep;
    }
    return (Money)(sum * upkeep_index() * (ot - 100) / 100 * budget_level_upkeep_pct() / 100 * g_budget.funding[svc] / (100 * 100));
}
#define RES_TAX_LV_BASE 30
#define RES_TAX_DIV     17000
#define GRANT_BASE      400         /* state grant to a village: GRANT_BASE - pop / GRANT_FADE_DIV */
#define GRANT_FADE_DIV  8
#define LOAN_MAX        40000       /* no further emergency loans above this debt: the treasury stays
                                       negative (construction denied) instead of an interest spiral */
#define LOAN_INSTALMENT_FLOOR 2000  /* PL2: instalments only while the treasury keeps this much */
#define LOAN_INSTALMENT_MIN   100   /* PL2: smallest monthly instalment (mk) */

Budget g_budget;
static s64 s_rem[BL_COUNT];       /* sub-mk remainders per line (numerator units of each formula) */

void budget_init(void) { memset(&g_budget, 0, sizeof(g_budget)); memset(s_rem, 0, sizeof(s_rem)); }

void budget_new(Money start, int unlimited)
{
    int i;
    budget_init();
    g_budget.money = start;
    g_budget.unlimited = unlimited;
    g_budget.tax_res = diff()->tax_neutral;   /* 1.1 D1: start at the level's tolerance (TAX_NEUTRAL 9 on Easy, Hard 8) */
    g_budget.tax_bus = diff()->tax_neutral;
    g_budget.loan_rate = LOAN_RATE;
    for (i = 0; i < SVC_COUNT; i++) g_budget.funding[i] = 100;
}

Money budget_start_funds(int funds, int start_year)
{
    /* FUNDS_GENEROUS/NORMAL/AUSTERE (T12: Normal 20,000 -> 25,000, Austere 8,000 -> 10,000: the first
       neighbourhood + plant + water cost ~10,000; the rest carries a careful town to market-town status) */
    Money base = funds == 0 ? 50000 : funds == 2 ? 10000 : 25000;
    if (start_year > 1955) base += (Money)((start_year - 1955) / 5) * 5000;
    return base;
}

int budget_can_afford(Money cost) { return g_budget.unlimited || cost <= g_budget.money; }

void budget_spend(Money cost, int line)
{
    if (line < 0 || line >= BL_COUNT) line = BL_CONSTRUCTION;
    g_budget.cur[line] -= cost;
    if (!g_budget.unlimited) g_budget.money -= cost;
}

static Money div_rem(s64 num, s64 den, int line, int book)
{
    s64 v = num + (book ? s_rem[line] : 0);
    Money q = (Money)(v / den);
    if (book) s_rem[line] = v - (s64)q * den;
    return q;
}

/* Compute next month's lines from the world (incomes +, expenses -). book: carry remainders. */
static void compute_lines(Money *out, int book)
{
    s64 res = 0, bus = 0, roads10 = 0, lines10 = 0, svc[SVC_COUNT];
    s32 ind_workers = 0, pop = 0;
    Money util = 0, special = 0;
    int i, x, ui;
    memset(svc, 0, sizeof(svc));
    for (i = 0; i < BL_COUNT; i++) out[i] = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        int lv;
        if (l->def == B_NONE) continue;
        d = def_building(l->def);
        lv = WT(l->x, l->y)->lv;
        if (d->cat == BCAT_ZONE) {
            if (l->status != LOT_ACTIVE) continue;
            if (d->zone == ZONE_HOUSES || d->zone == ZONE_APARTMENTS || d->zone == ZONE_FARM) {
                /* T12: tax weight by tier (better homes, higher incomes; apartments +1): a cottage
                   village pays little, services pay back through upgrades + land value.
                   M3: farm families pay the tier-1 house weight (INDUSTRY.md 6.2) */
                int tw = s_tax_weight[d->zone == ZONE_APARTMENTS][d->zone == ZONE_FARM ? 1 : d->tier & 3];
                res += (s64)l->occupants * (RES_TAX_LV_BASE + lv) * tw;
                pop += l->occupants;
            } else {
                bus += (s64)l->occupants * (50 + lv);
                if (d->zone == ZONE_INDUSTRY) ind_workers += l->occupants * industry_export_weight(l->def) / 100;   /* M3: rate by kind */
            }
            continue;
        }
        if (d->cat == BCAT_SERVICE) svc[d->service] += d->upkeep;
        else if (l->def == B_HEAT_PLANT) util += d->upkeep * civic_fuel_pct() / 100;   /* M5: coal included; M7 fuel price */
        else if (d->cat == BCAT_POWER || d->cat == BCAT_WATER || l->def == B_SEWAGE_WORKS) util += d->upkeep;   /* M5 */
        else special += d->upkeep;
        if (d->jobs && l->status == LOT_ACTIVE) bus += (s64)l->occupants * (50 + lv) / 2;
    }
    {
        int rn[ROAD_CLASS_COUNT], bn[ROAD_CLASS_COUNT], ln;
        world_road_stats(rn, bn, &ln);
        for (x = 1; x < ROAD_CLASS_COUNT; x++) roads10 += (s64)g_roads[x].upkeep10 * (rn[x] + 2 * bn[x]);
        lines10 = (s64)ln * g_misc.power_line_upkeep10;
        lines10 += (s64)world_pipe_tiles(NULL) * g_misc.pipe_upkeep10;     /* F1: water pipes (BL_NETWORKS) */
    }
    ui = upkeep_index();
    for (i = 0; i < SVC_COUNT; i++) svc[i] = svc[i] * ui * budget_overtime_pct(i) / 100;   /* PL2: overtime */
    roads10 *= ui; lines10 *= ui;
    util = (Money)((s64)util * ui / 100); special = (Money)((s64)special * ui / 100);
    if (budget_level_upkeep_pct() != 100) {   /* 1.1 D1: service, road, network and utility upkeep by level (Easy: untouched) */
        int up = budget_level_upkeep_pct();
        for (i = 0; i < SVC_COUNT; i++) svc[i] = svc[i] * up / 100;
        roads10 = roads10 * up / 100; lines10 = lines10 * up / 100;
        util = (Money)((s64)util * up / 100);
    }
    if (budget_level_tax_pct() != 100) res = res * budget_level_tax_pct() / 100;   /* 1.1 D1: tax yield by level (Hard 90 %) */
    out[BL_TAX_RES] = div_rem(res * g_budget.tax_res, RES_TAX_DIV, BL_TAX_RES, book);
    complex_budget_base(&bus, &ind_workers);   /* M6: mill / state works staff (half rate) + export volume */
    if (budget_level_tax_pct() != 100) bus = bus * budget_level_tax_pct() / 100;   /* 1.1 D1 */
    out[BL_TAX_BUS] = div_rem(bus * g_budget.tax_bus, 3000, BL_TAX_BUS, book);
    out[BL_EXPORT] = div_rem(ind_workers, 12, BL_EXPORT, book);
    out[BL_GRANTS] = IMAX(0, GRANT_BASE - pop / GRANT_FADE_DIV);   /* village support, fades out by 3,200 */
    for (i = 0; i < SVC_COUNT; i++)
        out[BL_SVC(i)] = -div_rem(svc[i] * g_budget.funding[i], 100 * 100, BL_SVC(i), book);
    out[BL_ROADS] = -div_rem(roads10, 10 * 100, BL_ROADS, book);
    out[BL_WINTER] = -season_winter_upkeep(-out[BL_ROADS], world_pipe_tiles(NULL), ui);   /* 1.1 W1: ploughing + frost (Easy 0) */
    out[BL_NETWORKS] = -div_rem(lines10, 10 * 100, BL_NETWORKS, book);
    out[BL_UTILITIES] = -util;
    out[BL_SPECIAL] = -special;
    if (g_budget.loan > 0) {
        Money interest = (Money)((s64)g_budget.loan * g_budget.loan_rate / 1000);
        out[BL_INTEREST] = -IMAX(1, interest);
    }
}

void budget_month_end(void)
{
    Money lines[BL_COUNT];
    Money total = 0;
    int i, month = sim_date()->month;
    compute_lines(lines, 1);
    for (i = 0; i < BL_COUNT; i++) {
        if (i == BL_CONSTRUCTION || i == BL_TIMBER) continue;     /* booked when spent */
        g_budget.cur[i] += lines[i];
        total += lines[i];
    }
    if (!g_budget.unlimited) g_budget.money += total;
    /* loan repayment: when the treasury is healthy again, pay back what exceeds 15,000 mk. PL2 (PLAYTEST 2 item 10):
       plus an instalment of 1/60 of the debt (at least LOAN_INSTALMENT_MIN) whenever the treasury holds more than
       LOAN_INSTALMENT_FLOOR: a town hovering under 15,000 never paid anything back and the interest (8 per mille of
       30-45k) ate its whole surplus for 20 years (the mill bot: 4 loans in the 1960s, still 32k owed in 1990) */
    if (diff()->loan_surplus && g_budget.loan > 0 && g_budget.money > LOAN_INSTALMENT_FLOOR && !g_budget.unlimited) {
        /* 1.1 B1 (Normal): the bank takes its instalment (1/60, as PL2, while the treasury holds 2,000) only out of the month's
           operating surplus, never out of the till: the PL2 rule took 1/60 even in a losing month, so a struggling town sat
           at 2,000 mk and the next bad winter pushed it under zero and into the next emergency loan */
        Money opnet = 0, pay;
        for (i = 0; i < BL_COUNT; i++) if (i != BL_CONSTRUCTION && i != BL_TIMBER) opnet += g_budget.cur[i];   /* the month's
                                     operating balance: the books' lines + what politics booked (decrees, the neighbour's
                                     utility premium), without construction and timber */
        pay = IMIN(IMAX(LOAN_INSTALMENT_MIN, (g_budget.loan + 59) / 60), IMAX(0, opnet));
        if (g_budget.money > 15000) pay = IMAX(pay, g_budget.money - 15000);
        pay = IMIN(IMIN(pay, g_budget.loan), g_budget.money - LOAN_INSTALMENT_FLOOR);
        g_budget.loan -= pay;
        g_budget.money -= pay;
        g_budget.cur[BL_INTEREST] -= pay;
    } else if (!diff()->loan_surplus && g_budget.loan > 0 && g_budget.money > LOAN_INSTALMENT_FLOOR && !g_budget.unlimited) {
        Money pay = IMAX(LOAN_INSTALMENT_MIN, (g_budget.loan + 59) / 60);
        if (g_budget.money > 15000) pay = IMAX(pay, g_budget.money - 15000);
        pay = IMIN(IMIN(pay, g_budget.loan), g_budget.money - LOAN_INSTALMENT_FLOOR);
        g_budget.loan -= pay;
        g_budget.money -= pay;
        g_budget.cur[BL_INTEREST] -= pay;
    }
    /* negative balance -> emergency loan after LOAN_AFTER_MONTHS */
    if (g_budget.money < 0 && !g_budget.unlimited) {
        g_budget.months_negative++;
        if (g_budget.months_negative >= diff()->loan_after && g_budget.loan < LOAN_MAX) {   /* 1.1 B1: by level (Easy 6) */
            Money amount = -g_budget.money + (Money)diff()->loan_cushion100 * 100;           /* 1.1 B1: Easy 10,000 */
            g_budget.loan += amount;
            g_budget.money += amount;
            g_budget.loans_taken++;
            g_budget.months_negative = 0;
            log_printf("budget: emergency loan %d mk (outstanding %d)", amount, g_budget.loan);
        }
    } else g_budget.months_negative = 0;
    for (i = 0; i < BL_COUNT; i++) {
        g_budget.last[i] = g_budget.cur[i];
        g_budget.year[i] += g_budget.cur[i];
        g_budget.cur[i] = 0;
    }
    if (month == MONTHS_PER_YEAR - 1) {
        memcpy(g_budget.last_year, g_budget.year, sizeof(g_budget.year));
        memset(g_budget.year, 0, sizeof(g_budget.year));
    }
}

int budget_projection(Term *terms, int max)
{
    Money lines[BL_COUNT];
    int i, n = 0;
    compute_lines(lines, 0);
    for (i = 0; i < BL_COUNT && n < max; i++) {
        if (!lines[i]) continue;
        terms[n].id = (u8)i; terms[n].amount = lines[i]; n++;
    }
    return n;
}

Money budget_net_last(void)
{
    Money t = 0;
    int i;
    for (i = 0; i < BL_COUNT; i++) t += g_budget.last[i];
    return t;
}

void budget_set_tax(int res, int bus)
{
    g_budget.tax_res = ICLAMP(res, 0, 20);
    g_budget.tax_bus = ICLAMP(bus, 0, 20);
}

void budget_set_funding(int svc, int pct)
{
    if (svc < 0 || svc >= SVC_COUNT) return;
    g_budget.funding[svc] = (u8)ICLAMP(pct, 50, 120);
}

int budget_save(struct SaveFile *sf)
{
    int i;
    save_begin_chunk(sf, "BUDG", 1);
    save_s32(sf, g_budget.money);
    save_s32(sf, g_budget.tax_res);
    save_s32(sf, g_budget.tax_bus);
    for (i = 0; i < SVC_COUNT; i++) save_u8(sf, g_budget.funding[i]);
    save_s32(sf, BL_COUNT);
    for (i = 0; i < BL_COUNT; i++) {
        save_s32(sf, g_budget.cur[i]); save_s32(sf, g_budget.last[i]);
        save_s32(sf, g_budget.year[i]); save_s32(sf, g_budget.last_year[i]);
    }
    save_s32(sf, g_budget.unlimited);
    save_s32(sf, g_budget.months_negative);
    save_s32(sf, g_budget.loan);
    save_s32(sf, g_budget.loan_rate);
    save_s32(sf, g_budget.loans_taken);
    save_end_chunk(sf);
    return 1;
}

int budget_load(struct SaveFile *sf)
{
    int ver, i, n;
    if (!load_find_chunk(sf, "BUDG", &ver)) return 0;
    g_budget.money = load_s32(sf);
    g_budget.tax_res = load_s32(sf);
    g_budget.tax_bus = load_s32(sf);
    for (i = 0; i < SVC_COUNT; i++) g_budget.funding[i] = (u8)load_u8(sf);
    n = load_s32(sf);
    for (i = 0; i < n; i++) {
        Money a = load_s32(sf), b = load_s32(sf), c = load_s32(sf), d = load_s32(sf);
        if (i < BL_COUNT) { g_budget.cur[i] = a; g_budget.last[i] = b; g_budget.year[i] = c; g_budget.last_year[i] = d; }
    }
    g_budget.unlimited = load_s32(sf);
    g_budget.months_negative = load_s32(sf);
    g_budget.loan = load_s32(sf);
    g_budget.loan_rate = load_s32(sf);
    g_budget.loans_taken = load_s32(sf);
    memset(s_rem, 0, sizeof(s_rem));
    return load_ok(sf);
}

/* ------------------------------------------------------------------ T10: save
   Chunk "BUDX": the sub-mk remainders (s_rem) that budget_load resets; without them the month-end
   books differ by a markka after load. Call budget_load_ext AFTER budget_load. */
int budget_save_ext(struct SaveFile *sf)
{
    save_begin_chunk(sf, "BUDX", 2);           /* M1: v2 = line count first (BL_POLITICS appended) */
    save_s32(sf, BL_COUNT);
    save_bytes(sf, s_rem, sizeof(s_rem));
    save_end_chunk(sf);
    return 1;
}

int budget_load_ext(struct SaveFile *sf)
{
    int ver;
    int n;
    if (!load_find_chunk(sf, "BUDX", &ver)) return 0;
    memset(s_rem, 0, sizeof(s_rem));
    n = ver >= 2 ? load_s32(sf) : BL_POLITICS;  /* v1: the lines before M1 appended BL_POLITICS */
    if (n < 0 || n > BL_COUNT) return 0;
    load_bytes(sf, s_rem, n * (int)sizeof(s64));
    return load_ok(sf);
}
