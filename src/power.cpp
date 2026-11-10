/* power.cpp - 1.2 S1 "Power": the reactor letter, plant prices and build months, the Eastern reactor's clearing import,
   query / Guide / Cabinet / newspaper lines. PORTABLE. Owner: S1. See power.h; numbers and reasons in docs/BALANCE.md 12.
   Tone: THEME 6 (dry, deferential, about the system; the big neighbour is weather; no new Finnish words). */
#include "common.h"
#include "power.h"
#include "defs.h"
#include "world.h"
#include "sim.h"
#include "budget.h"
#include "politics.h"
#include "trade.h"
#include "era.h"
#include "text.h"
#include "tools.h"
#include "save.h"
#include "util.h"

static struct {
    int choice;               /* RX_* */
    int online_year;          /* the first nuclear plant finished (0 none) */
    int choice_year;          /* when the letter was answered */
} S;

void power_reset(void) { memset(&S, 0, sizeof(S)); S.choice = RX_NONE; }

static int cur_year(void) { return sim_date()->year; }
static int clearing_open(void) { return !trade_state()->closed && cur_year() < TRADE_END_YEAR; }

/* ------------------------------------------------------------------ the letter */
#define FB(b, v, m) { FX_BLOC, (s8)(b), (s16)(v), (u8)(m) }
#define FE(v)       { FX_METER, METER_EAST, (s16)(v), 0 }
#define FA(v)       { FX_AUTH, 0, (s16)(v), 0 }
#define F0          { FX_NONE, 0, 0, 0 }
static const ReactorCard s_card = {
    "The reactor question",
    "The Ministry of Trade and Industry has found the town a lake cold enough for a reactor. Two suppliers have called: "
    "one from the East, one from Pittsburgh. Both bring brochures. One also brings a delegation.",
    { { "Eastern reactor (price -30%, part paid in clearing)",
        "The friendly neighbour sends engineers, a reactor and an invoice in clearing roubles. The Workers approve; the "
        "Industrialists count the pipes.", "Eastern reactor",
        { FE(8), FB(BLOC_WORKERS, 6, 24), FB(BLOC_INDUSTRIALISTS, -4, 24), F0, F0, F0 } },
      { "Western reactor (\"Eastinghouse Electric\", full price, quickest)",
        "Pittsburgh sends a reactor and a man in a hat. Moscow expresses interest, which is worse than expressing displeasure.",
        "Western reactor",
        { FB(BLOC_INDUSTRIALISTS, 6, 24), FB(BLOC_INTELLIGENTSIA, 3, 24), FE(-6), FA(-2), F0, F0 } },
      { "The Loviisa compromise (price -15%, slowest)",
        "An Eastern reactor in a Western containment with a Western control room. The newspaper calls it a very Finnish "
        "solution; both delegations claim the credit.", "Loviisa compromise",
        { FB(BLOC_WORKERS, 2, 24), FB(BLOC_INDUSTRIALISTS, 2, 24), FE(2), F0, F0, F0 } } }
};
#undef FB
#undef FE
#undef FA
#undef F0

const ReactorCard *power_reactor_card(void) { return &s_card; }
int power_reactor_choice(void) { return S.choice; }

static int nuclear_open(void)
{
    return def_unlocked(B_NUCLEAR_PLANT, cur_year(), sim_stats()->town_status, sim_settings()->all_unlocked) &&
           g_bdefs[B_NUCLEAR_PLANT].variants > 0;
}
int power_reactor_needed(void) { return S.choice == RX_NONE && nuclear_open(); }

int power_reactor_cost_pct(int c)
{
    switch (c) {
    case RX_EAST:    return clearing_open() ? 70 : 90;     /* after 1990 the neighbour wants markka */
    case RX_LOVIISA: return clearing_open() ? 85 : 95;
    default:         return 100;
    }
}
int power_reactor_months(int c) { return c == RX_EAST ? 18 : c == RX_LOVIISA ? 24 : 12; }
static s32 import_of(int c) { return !clearing_open() ? 0 : c == RX_EAST ? POWER_REACTOR_IMPORT : c == RX_LOVIISA ? POWER_REACTOR_IMPORT / 2 : 0; }

int power_reactor_terms(int c, char *buf, int size)
{
    char a[32], b[32];
    int n;
    text_fmt_money(a, sizeof(a), (Money)((s64)g_bdefs[B_NUCLEAR_PLANT].cost * power_reactor_cost_pct(c) / 100), cur_year());
    n = str_printf(buf, size, "%s, %d months to build", a, power_reactor_months(c));
    if (import_of(c)) { text_fmt_int(b, sizeof(b), import_of(c)); n += str_printf(buf + n, size - n, ", %s clearing credit a month", b); }
    return n;
}

int power_reactor_choose(int c)
{
    if (S.choice != RX_NONE || c < 0 || c >= RX_COUNT) return 0;
    S.choice = c;
    S.choice_year = cur_year();
    politics_apply_fx_list(s_card.ch[c].fx);       /* nothing when politics is off */
    log_printf("power: reactor letter answered: %d (%s)", c, s_card.ch[c].short_name);
    return 1;
}

/* ------------------------------------------------------------------ prices, construction, fuel */
int power_count(int def, int active_only)
{
    int i, n = 0;
    for (i = 1; i < g_world.lot_hi; i++)
        if (g_world.lots[i].def == def && (!active_only || g_world.lots[i].status == LOT_ACTIVE)) n++;
    return n;
}

Money power_build_cost(int def)
{
    const BuildingDef *d = def_building(def);
    if (def == B_NUCLEAR_PLANT && S.choice != RX_NONE) return (Money)((s64)d->cost * power_reactor_cost_pct(S.choice) / 100);
    return d->cost;
}

int power_build_months(int def)
{
    if (def == B_NUCLEAR_PLANT && S.choice != RX_NONE) return power_reactor_months(S.choice);
    return 0;
}

s32 power_clearing_import(void)
{
    int i, n = 0;
    s32 per = S.choice == RX_NONE ? 0 : import_of(S.choice);
    if (!per) return 0;
    for (i = 1; i < g_world.lot_hi; i++)
        if (g_world.lots[i].def == B_NUCLEAR_PLANT && g_world.lots[i].status == LOT_BUILDING) n++;
    return per * n;
}

int power_peat_rural(void)
{
    int n = power_count(B_PEAT_PLANT, 1);
    return n ? IMIN(POWER_PEAT_RURAL_MAX, n * POWER_PEAT_RURAL) : 0;
}

s32 power_station_surcharge(void)
{
    const TradeState *t = trade_state();
    s64 v = (s64)POWER_STATION_COAL_X * COAL_SURCHARGE_MK * IMAX(0, t->coal_index - 100) / 100;
    return (s32)(v * budget_upkeep_index() / 100);
}

void power_month(void)
{
    if (!S.online_year && power_count(B_NUCLEAR_PLANT, 1) > 0) {
        S.online_year = cur_year();
        era_push_msg(EM_REACTOR_ONLINE, S.choice, g_bdefs[B_NUCLEAR_PLANT].capacity);
    }
}
int power_online_year(void) { return S.online_year; }

/* ------------------------------------------------------------------ text */
int power_lot_line(int lot, int k, char *buf, int size, int *font)
{
    const Lot *l = world_lot(lot);
    const TradeState *t = trade_state();
    char a[32];
    int line = 0;
    *font = 1;
    buf[0] = 0;
    if (!l || l->def == B_NONE || !DEF_IS_PLANT(l->def)) return 0;
    switch (l->def) {
    case B_NUCLEAR_PLANT:
        if (k == line++) {
            *font = 0;
            if (S.choice == RX_NONE) return str_printf(buf, size, "Reactor: not yet decided");
            return str_printf(buf, size, "Reactor: %s (%d%% of the price, decided %d)", s_card.ch[S.choice].short_name,
                              power_reactor_cost_pct(S.choice), S.choice_year);
        }
        if (l->status == LOT_BUILDING && k == line++) {
            *font = 2;
            return str_printf(buf, size, "Under construction: %d month%s to go, no power yet", l->build_left, l->build_left == 1 ? "" : "s");
        }
        if (l->status == LOT_BUILDING && import_of(S.choice) && k == line++) {
            text_fmt_int(a, sizeof(a), import_of(S.choice));
            return str_printf(buf, size, "Clearing: %s credit a month goes East while it is built", a);
        }
        if (S.choice != RX_NONE && k == line++) {
            static const char *const tag[RX_COUNT] = { "Paid partly in clearing; the East is pleased",
                "Paid in markka; Moscow expressed interest", "East inside, West outside: a very Finnish solution" };
            return str_printf(buf, size, "%s", tag[S.choice]);
        }
        if (k == line++) return str_printf(buf, size, "Cooled by the lake; no smoke, no fuel bill");
        return 0;
    case B_PEAT_PLANT:
        if (k == line++) return str_printf(buf, size, "Fuel: peat from the bogs (no oil or coal prices)");
        if (k == line++) { *font = 3; return str_printf(buf, size, "Rural bloc: +%d for the town's peat plants", power_peat_rural()); }
        return 0;
    case B_COAL_STATION:
        if (k == line++) {
            if (t->coal_index > 100) {
                text_fmt_money(a, sizeof(a), power_station_surcharge(), cur_year());
                return str_printf(buf, size, "Fuel: coal; surcharge %s a month (four small plants' worth)", a);
            }
            return str_printf(buf, size, "Fuel: coal (from 1974 a surcharge, four small plants' worth)");
        }
        return 0;
    case B_COAL_PLANT:
        if (k == line++) return str_printf(buf, size, t->coal_index > 100 ? "Fuel: coal, at the surcharged price" : "Fuel: coal");
        return 0;
    case B_OIL_PLANT:
        if (k == line++) return str_printf(buf, size, "Fuel: oil at index %d (1955 = 100)", t->oil_index);
        return 0;
    case B_HYDRO_PLANT:
        if (k == line++) return str_printf(buf, size, "Fuel: the river, free of charge");
        return 0;
    }
    return 0;
}

/* the Power menu order (DESIGN_1_2 4): coal plant, coal station, oil, peat, hydro, nuclear */
static const int s_order[6] = { B_COAL_PLANT, B_COAL_STATION, B_OIL_PLANT, B_PEAT_PLANT, B_HYDRO_PLANT, B_NUCLEAR_PLANT };
static const char *const s_note[6] = {
    "The workhorse. Smoky; coal surcharge 1974-85",
    "Four small plants in one, cheaper to run; four surcharges",
    "Fuel at the oil price: 1973 and 1979 hurt",
    "Domestic fuel, no import prices; the Rural bloc approves",
    "Needs river rapids. Free fuel, little upkeep",
    "No smoke; needs a shore. The reactor letter" };

int power_guide_rows(PowerGuideRow *out, int max)
{
    int k, n = 0, y = cur_year(), st = sim_stats()->town_status, all = sim_settings()->all_unlocked;
    for (k = 0; k < 6 && n < max; k++) {
        const BuildingDef *d = def_building(s_order[k]);
        PowerGuideRow *r = &out[n];
        if (!d->variants && s_order[k] != B_HYDRO_PLANT) continue;   /* no art in this build */
        memset(r, 0, sizeof(*r));
        r->def = s_order[k];
        str_copy(r->name, d->name, sizeof(r->name));
        str_printf(r->size, sizeof(r->size), "%dx%d", d->w, d->h);
        text_fmt_int(r->units, sizeof(r->units), d->capacity);
        text_fmt_money(r->cost, sizeof(r->cost), power_build_cost(s_order[k]), y);
        text_fmt_money(r->upkeep, sizeof(r->upkeep), d->upkeep, y);
        r->unlocked = def_unlocked(s_order[k], y, st, all) && (d->variants || s_order[k] == B_HYDRO_PLANT);
        r->built = power_count(s_order[k], 0);
        if (r->unlocked) str_printf(r->unlock, sizeof(r->unlock), r->built ? "built: %d" : "available", r->built);
        else {
            char h[40];
            text_unlock_hint(h, sizeof(h), d->status_req, d->era_year, y, st);
            str_printf(r->unlock, sizeof(r->unlock), "%s", h);
        }
        str_copy(r->note, s_note[k], sizeof(r->note));
        n++;
    }
    return n;
}

int power_guide_note(char *buf, int size)
{
    if (S.choice != RX_NONE) {
        char t[96];
        power_reactor_terms(S.choice, t, sizeof(t));
        return str_printf(buf, size, "The reactor question is settled: %s (%s).", s_card.ch[S.choice].short_name, t);
    }
    {   /* units per 100 mk of monthly upkeep, from the table (fuel extra) */
        static const int d4[4] = { B_COAL_PLANT, B_COAL_STATION, B_PEAT_PLANT, B_NUCLEAR_PLANT };
        int u[4], k;
        for (k = 0; k < 4; k++) u[k] = (int)((s64)g_bdefs[d4[k]].capacity * 100 / IMAX(1, (int)g_bdefs[d4[k]].upkeep));
        return str_printf(buf, size, "Units per 100 mk of upkeep: small coal %d, station %d, peat %d, nuclear %d (fuel extra). "
                                     "The first reactor brings a letter.", u[0], u[1], u[2], u[3]);
    }
}

int power_advice_pick(void)
{
    int y = cur_year(), st = sim_stats()->town_status, all = sim_settings()->all_unlocked;
    Money m = g_budget.money;
#define OPEN(d) (def_unlocked(d, y, st, all) && g_bdefs[d].variants > 0)
    if (OPEN(B_NUCLEAR_PLANT) && m >= (Money)((s64)g_bdefs[B_NUCLEAR_PLANT].cost * (S.choice == RX_NONE ? 70 : power_reactor_cost_pct(S.choice)) / 100))
        return B_NUCLEAR_PLANT;
    if (OPEN(B_PEAT_PLANT) && era_event_seen(EE_OIL_SHOCK) && trade_state()->coal_index >= 150 && m >= g_bdefs[B_PEAT_PLANT].cost)
        return B_PEAT_PLANT;                    /* after the oil shock, while coal is dear (1974-85) */
    if (OPEN(B_COAL_STATION) && m >= g_bdefs[B_COAL_STATION].cost && power_count(B_COAL_PLANT, 0) >= 2) return B_COAL_STATION;
#undef OPEN
    return B_NONE;
}

int power_news_filler(int year, char *head, int hs, char *body, int bs)
{
    static const char *const fb[RX_COUNT] = {
        "The new reactor hums in two languages. Its manual was translated into Finnish, and the Finnish back again, to be sure.",
        "The engineers from Pittsburgh have gone home. They left a reactor, a coffee machine and an address. The coffee machine is admired.",
        "Visitors ask whose reactor it is. Officials reply that it is a very Finnish solution, and offer coffee." };
    if (!S.online_year || year != S.online_year + 1 || S.choice == RX_NONE) return 0;
    str_copy(head, "ENERGY", hs);
    str_copy(body, fb[S.choice], bs);
    return 1;
}

/* ------------------------------------------------------------------ scripts, debug, save */
int power_script_line(int n, char **t, FILE *out)
{
    int c = -1;
    if (n < 1 || !str_ieq(t[0], "reactor")) return -1;
    if (n >= 2 && str_ieq(t[1], "dump")) { power_dump(out); return SCR_OK; }
    if (n >= 2) {
        if (str_ieq(t[1], "east") || str_ieq(t[1], "0")) c = RX_EAST;
        else if (str_ieq(t[1], "west") || str_ieq(t[1], "1")) c = RX_WEST;
        else if (str_ieq(t[1], "loviisa") || str_ieq(t[1], "2")) c = RX_LOVIISA;
    }
    if (c < 0) return SCR_ERROR;
    if (!power_reactor_choose(c)) { if (out) fprintf(out, "script: reactor: already decided (%d)\n", S.choice); return SCR_ERROR; }
    if (out) fprintf(out, "script: reactor %s\n", s_card.ch[c].short_name);
    return SCR_OK;
}

void power_dump(FILE *f)
{
    int k;
    if (!f) return;
    fprintf(f, "power %d-%02d: reactor %d (%s, decided %d), online %d, clearing import %d, peat rural %+d; plants:",
            cur_year(), sim_date()->month + 1, S.choice, S.choice == RX_NONE ? "none" : s_card.ch[S.choice].short_name, S.choice_year,
            S.online_year, (int)power_clearing_import(), power_peat_rural());
    for (k = 0; k < 6; k++) fprintf(f, " %s %d/%d", g_bdefs[s_order[k]].key, power_count(s_order[k], 1), power_count(s_order[k], 0));
    fprintf(f, "\n");
}

u32 power_hash(void)
{
    s32 v[3];
    if (S.choice == RX_NONE && !S.online_year) return 0;
    v[0] = S.choice; v[1] = S.online_year; v[2] = S.choice_year;
    return hash_bytes(HASH_INIT, v, sizeof(v));
}

void power_save(struct SaveFile *sf)
{
    if (S.choice == RX_NONE && !S.online_year) return;     /* nothing decided: the file is byte-identical to 1.1 */
    save_begin_chunk(sf, "POWR", 1);
    save_s32(sf, S.choice); save_s32(sf, S.online_year); save_s32(sf, S.choice_year);
    save_end_chunk(sf);
}

void power_load(struct SaveFile *sf)
{
    int ver;
    power_reset();
    if (!load_find_chunk(sf, "POWR", &ver)) return;
    S.choice = load_s32(sf); S.online_year = load_s32(sf); S.choice_year = load_s32(sf);
    if (S.choice < RX_NONE || S.choice >= RX_COUNT) S.choice = RX_NONE;
    UNUSED(ver);
}
