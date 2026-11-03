/* ui_graph.cpp - M7: the graphs panel (6 tabs of monthly history: town, money, demand, work, politics, clearing)
   and the trade panel (the clearing agreement and its goods list, the balance meter, oil and the Western index,
   the bond, sauna diplomacy). WINDOWS SIDE. Owner: M7. Composed inside ui.cpp's panel window with the uix_*
   helpers; controls are CK_POL controls that ui.cpp routes to uim7_click for these two panels. */
#include "common.h"
#include "ui_m7.h"
#include "ui_pol.h"
#include "ui.h"
#include "art.h"
#include "gfx.h"
#include "text.h"
#include "sim.h"
#include "budget.h"
#include "politics.h"
#include "trade.h"
#include "era.h"
#include "util.h"
/* colours (art/ui_layout.h values; that header is ui.cpp's) */
#define UI_COL_PAPER_LT   238, 230, 205
#define UI_COL_PENCIL      90,  85,  82
#define UI_COL_GREY       139, 137, 123
#define UI_COL_RED        156,  56,  41
#define UI_COL_GREEN       74, 105,  74
#define UI_COL_BLUE        57,  85, 106
#define UI_COL_AMBER      189, 133,  41

static PIX col(int r, int g, int b) { return gfx_rgb(r, g, b); }

/* ================================================================== graphs */
enum { GT_TOWN, GT_MONEY, GT_DEMAND, GT_WORK, GT_POLITICS, GT_CLEARING, GT_COUNT };
static const char *const s_gtab[GT_COUNT] = { "Town", "Money", "Demand", "Work", "Politics", "Clearing" };

#define GMAX_SERIES 3
typedef struct { const char *name; int src; int id; int r, g, b; } GSeries;   /* src 0 sim_history, 1 era_history */
static const GSeries s_gs[GT_COUNT][GMAX_SERIES] = {
    { { "Population", 0, HIST_POPULATION, UI_COL_RED }, { "Jobs", 0, HIST_JOBS, UI_COL_BLUE }, { NULL, 0, 0, 0, 0, 0 } },
    { { "Treasury (mk)", 0, HIST_MONEY, UI_COL_GREEN }, { NULL, 0, 0, 0, 0, 0 }, { NULL, 0, 0, 0, 0, 0 } },
    { { "Residential", 0, HIST_DEMAND_R, UI_COL_GREEN }, { "Commercial", 0, HIST_DEMAND_C, UI_COL_BLUE }, { "Industry", 0, HIST_DEMAND_I, UI_COL_AMBER } },
    { { "Unemployed", 1, EH_UNEMPLOYED, UI_COL_RED }, { "To Sweden a month", 0, HIST_EMIGRATED, UI_COL_BLUE }, { "Homeless", 1, EH_HOMELESS, UI_COL_AMBER } },
    { { "Popularity", 1, EH_APPROVAL, UI_COL_RED }, { "Eastern relations", 1, EH_EAST, UI_COL_BLUE }, { "Western trade", 1, EH_WEST, UI_COL_GREEN } },
    { { "Clearing balance", 1, EH_BALANCE, UI_COL_BLUE }, { "World oil price %", 1, EH_OIL_INDEX, UI_COL_RED }, { NULL, 0, 0, 0, 0, 0 } },
};

static s32 s_gbuf[GMAX_SERIES][SIM_HISTORY > ERA_HIST ? SIM_HISTORY : ERA_HIST];

static int series_get(const GSeries *g, s32 *out)
{
    return g->src == 0 ? sim_history(g->id, out, SIM_HISTORY) : era_history(g->id, out, ERA_HIST);
}

static void plot_line(const s32 *v, int n, int px, int py, int pw, int ph, s32 lo, s32 hi, PIX c)
{
    int i, x0, x1, y0, y1, ya, yb;
    s64 span = (s64)hi - lo;
    if (n < 1 || span <= 0) return;
    for (i = 0; i < n; i++) {
        x0 = px + (n > 1 ? (int)((s64)i * (pw - 1) / (n - 1)) : 0);
        x1 = px + (n > 1 ? (int)((s64)(i + 1) * (pw - 1) / (n - 1)) : 1);
        y0 = py + ph - 1 - (int)(((s64)v[i] - lo) * (ph - 1) / span);
        y1 = i + 1 < n ? py + ph - 1 - (int)(((s64)v[i + 1] - lo) * (ph - 1) / span) : y0;
        ya = IMIN(y0, y1); yb = IMAX(y0, y1);
        if (i + 1 >= n) x1 = x0 + 1;
        uix_fill(x0, ya, IMAX(x0 + 1, x1), yb + 2, c);
    }
}

static void graphs_panel(int x, int y, int w, int h)
{
    const SimDate *d = sim_date();
    char a[48], b[48];
    int tab = ICLAMP(uix_tab_of(PANEL_GRAPHS), 0, GT_COUNT - 1), k, i, n[GMAX_SERIES], nmax = 0, px, py, pw, ph, yy;
    s32 lo = 0, hi = 1;
    uix_tabs(x + 4, y + 2, x + w - 4, s_gtab, GT_COUNT, tab);
    for (k = 0; k < GMAX_SERIES; k++) {
        n[k] = 0;
        if (!s_gs[tab][k].name) continue;
        n[k] = series_get(&s_gs[tab][k], s_gbuf[k]);
        for (i = 0; i < n[k]; i++) { if (s_gbuf[k][i] > hi) hi = s_gbuf[k][i]; if (s_gbuf[k][i] < lo) lo = s_gbuf[k][i]; }
        if (n[k] > nmax) nmax = n[k];
    }
    if (tab == GT_DEMAND) { lo = -100; hi = 100; }
    if (tab == GT_POLITICS) { lo = 0; hi = 100; }
    if (hi - lo < 10) hi = lo + 10;
    px = x + 44; py = y + 24; pw = w - 52; ph = 150;
    uix_inset(px - 2, py - 2, pw + 4, ph + 4, col(UI_COL_PAPER_LT));
    if (lo < 0 && hi > 0) {                                     /* the zero line */
        int zy = py + ph - 1 - (int)((s64)(0 - lo) * (ph - 1) / ((s64)hi - lo));
        uix_fill(px, zy, px + pw, zy + 1, col(UI_COL_GREY));
    }
    text_fmt_int(a, sizeof(a), hi); uix_text_r(px - 4, py - 2, a, FONT_BODY_GREY);
    text_fmt_int(a, sizeof(a), lo); uix_text_r(px - 4, py + ph - 9, a, FONT_BODY_GREY);
    if (tab == GT_POLITICS && !politics_enabled()) uix_text_c(px, pw, py + ph / 2 - 5, "Politics is off", FONT_BODY_GREY);
    else if (nmax < 2) uix_text_c(px, pw, py + ph / 2 - 5, "A month of history is needed", FONT_BODY_GREY);
    else {
        int last = d->year * 12 + d->month - 1;            /* month index of the newest point */
        for (i = 0; i < nmax; i++) {                        /* January ticks, a label every 5 years */
            int m = last - (nmax - 1 - i), gx = px + (int)((s64)i * (pw - 1) / (nmax - 1));
            if (m % 12) continue;
            uix_fill(gx, py + ph, gx + 1, py + ph + 3, col(UI_COL_PENCIL));
            if ((m / 12) % 5 == 0 && gx + 26 < px + pw + 10) { str_printf(a, sizeof(a), "%d", m / 12); uix_text(gx - 10, py + ph + 4, a, FONT_BODY_GREY); }
        }
        for (k = 0; k < GMAX_SERIES; k++)
            if (n[k] >= 2) plot_line(s_gbuf[k] + 0, n[k], px + (int)((s64)(nmax - n[k]) * (pw - 1) / (nmax - 1)), py,
                                     pw - (int)((s64)(nmax - n[k]) * (pw - 1) / (nmax - 1)), ph, lo, hi,
                                     col(s_gs[tab][k].r, s_gs[tab][k].g, s_gs[tab][k].b));
    }
    yy = py + ph + 18;
    for (k = 0; k < GMAX_SERIES; k++) {                     /* legend with the latest value */
        if (!s_gs[tab][k].name) continue;
        uix_fill(x + 8, yy + 3, x + 18, yy + 6, col(s_gs[tab][k].r, s_gs[tab][k].g, s_gs[tab][k].b));
        uix_text(x + 22, yy, s_gs[tab][k].name, FONT_BODY_DARK);
        if (n[k] > 0) {
            if (tab == GT_MONEY) text_fmt_money(a, sizeof(a), s_gbuf[k][n[k] - 1], d->year);
            else text_fmt_int(a, sizeof(a), s_gbuf[k][n[k] - 1]);
            uix_text_r(x + w - 8, yy, a, FONT_BODY_DARK);
        }
        yy += 12;
    }
    /* the era in one line: population since the start (yearly table) + the newsreel on demand */
    if (era_years() > 0) {
        const EraYear *r0 = era_year_row(0);
        text_fmt_int(a, sizeof(a), r0->pop); text_fmt_int(b, sizeof(b), sim_stats()->population);
        {
            char line[96];
            str_printf(line, sizeof(line), "Since %d: %s " GLYPH_ARROW_UP " %s residents", r0->year, a, b);
            uix_text_fit(x + 8, y + h - 18, line, FONT_BODY_GREY, w - 110);
        }
    }
    uix_button(x + w - 96, y + h - 22, 90, 18, "Newsreel", M7A_NEWSREEL, 0, 1);
}

/* ================================================================== trade panel */
static void trade_panel(int x, int y, int w, int h)
{
    const TradeState *t = trade_state();
    const TradeAgreement *ag = trade_agreement();
    char a[96], b[96], tip[4][80];
    int yy = y + 4, g, n, k, v;
    UNUSED(h);
    if (t->closed) {
        uix_text(x + 6, yy, "The clearing trade ended in 1990", FONT_HEAD); yy += 16;
        uix_text_fit(x + 6, yy, "The East now pays in hard currency, when it pays.", FONT_BODY_GREY, w - 12); yy += 16;
    } else {
        str_printf(a, sizeof(a), "Agreement %d-%d: %s", ag->from_year, ag->to_year, ag->offer ? "to be signed" : trade_level_name(ag->level));
        uix_text(x + 6, yy, a, FONT_HEAD); yy += 15;
        if (ag->offer) {
            if (politics_enabled()) {
                uix_text_fit(x + 6, yy, "The Delegation's card waits on the President's desk.", FONT_BODY_DARK, w - 112);
                uix_button(x + w - 104, yy - 3, 98, 16, "Open the card", M7A_CARD, 0, 1);
            } else {
                static const char *const lab[TL_COUNT] = { "Modest -15%", "Standard", "Ambitious +20%" };
                static const int order[TL_COUNT] = { TL_STANDARD, TL_MODEST, TL_AMBITIOUS };
                for (k = 0; k < TL_COUNT; k++)
                    uix_button(x + 6 + k * ((w - 12) / 3), yy - 2, (w - 12) / 3 - 4, 16, lab[order[k]], M7A_SIGN, order[k], 1);
            }
            yy += 17;
        }
        uix_text(x + 6, yy, "The goods list (a year)", FONT_BODY_GREY);
        uix_text_r(x + w - 6, yy, "delivered", FONT_BODY_GREY); yy += 11;
        for (g = 0, n = 0; g < TG_COUNT && n < 5; g++) {
            if (!ag->quota[g] || ag->offer) continue;
            trade_goods_line(g, ag->quota[g], a, sizeof(a));
            uix_text_fit(x + 10, yy, a, FONT_BODY_DARK, w - 70);
            v = (int)((s64)t->delivered[g] * 100 / IMAX(1, ag->quota[g]));
            str_printf(b, sizeof(b), "%d%%", v);
            uix_text_r(x + w - 6, yy, b, v < TRADE_REVIEW_PCT ? FONT_BODY_RED : FONT_BODY_DARK);
            yy += 11; n++;
        }
        if (!n) { uix_text(x + 10, yy, ag->offer ? "Drawn up when the agreement is signed" : "Nothing yet", FONT_BODY_GREY); yy += 11; }
        if (t->fulfil_pct >= 0) {
            str_printf(a, sizeof(a), "Last January review: %d%% of the list (%d%% expected)", t->fulfil_pct, TRADE_REVIEW_PCT);
            uix_text_fit(x + 6, yy, a, t->fulfil_pct < TRADE_REVIEW_PCT ? FONT_BODY_RED : FONT_BODY_GREY, w - 12); yy += 11;
        }
        yy += 3; uix_rule(x + 6, yy, w - 12); yy += 6;
        /* the balance meter */
        uix_text(x + 6, yy, "Clearing balance", FONT_BODY_DARK);
        v = t->tolerance > 0 ? (int)ICLAMP((s64)t->balance * 100 / t->tolerance, -100, 100) : 0;
        uix_bar(x + 110, yy + 1, w - 118, 8, v, 1, col(IABS(v) >= 100 ? 156 : 57, IABS(v) >= 100 ? 56 : 85, IABS(v) >= 100 ? 41 : 106));
        yy += 13;
        n = trade_balance_tip(tip, 3);
        for (k = 0; k < n; k++) { uix_text_fit(x + 10, yy, tip[k], k == 2 && (t->lost_month || t->deficit_months >= 3) ? FONT_BODY_RED : FONT_BODY_GREY, w - 16); yy += 11; }
        trade_clearing_reason(b, sizeof(b));
        str_printf(a, sizeof(a), "Clearing orders x%d%%: %s", trade_clearing_pct(), b);
        uix_text_fit(x + 6, yy, a, FONT_BODY_DARK, w - 12); yy += 11;
    }
    text_fmt_money(b, sizeof(b), t->fuel_cost, sim_date()->year);
    str_printf(a, sizeof(a), "World oil price %d%%, fuel %s a month", t->oil_index, b);
    uix_text_fit(x + 6, yy, a, FONT_BODY_DARK, w - 12); yy += 11;
    str_printf(a, sizeof(a), "Western orders x%d%% (price index)", trade_western_pct());
    uix_text_fit(x + 6, yy, a, FONT_BODY_DARK, w - 12); yy += 14;
    uix_rule(x + 6, yy, w - 12); yy += 6;
    /* the bond */
    if (t->bond > 0) {
        text_fmt_money(b, sizeof(b), t->bond, sim_date()->year);
        str_printf(a, sizeof(a), "Municipal bond: %s left, %d months", b, t->bond_months);
        uix_text_fit(x + 6, yy, a, FONT_BODY_DARK, w - 12); yy += 18;
    } else {
        text_fmt_money(b, sizeof(b), trade_bond_amount(), sim_date()->year);
        str_printf(a, sizeof(a), "Issue a bond: %s", b);
        uix_button(x + 6, yy - 2, 150, 16, a, M7A_BOND, 0, !g_budget.unlimited);
        uix_text_fit(x + 162, yy + 1, "0.6% a month, 10 years", FONT_BODY_GREY, w - 168);
        yy += 18;
    }
    /* sauna diplomacy */
    k = era_sauna_state();
    if (k != 1) {
        str_printf(a, sizeof(a), "Sauna evening with the Ambassador (East +%d)", era_sauna_gain());
        if (k == 0) uix_button(x + 6, yy - 2, w - 12, 16, a, M7A_SAUNA, 0, 1);
        else uix_text_fit(x + 6, yy, k == 2 ? "The presidential sauna: politics is off" : "The sauna cools until next year", FONT_BODY_GREY, w - 12);
    }
}

/* ================================================================== panel api */
void uim7_title(int panel, char *buf, int size)
{
    if (panel == PANEL_GRAPHS) str_copy(buf, "Graphs", size);
    else str_copy(buf, "Trade and the treasury", size);
}

void uim7_panel(int panel, int x, int y, int w, int h)
{
    if (panel == PANEL_GRAPHS) graphs_panel(x, y, w, h);
    else trade_panel(x, y, w, h);
}

u32 uim7_data_sig(int panel, u32 h)
{
    s32 v[6];
    if (panel == PANEL_TRADE) {
        h = hash_bytes(h, trade_state(), sizeof(TradeState));
        h = hash_bytes(h, trade_agreement(), sizeof(TradeAgreement));
    }
    v[0] = (s32)sim_date()->total_months; v[1] = era_sauna_state(); v[2] = politics_meter(METER_EAST);
    v[3] = politics_enabled(); v[4] = g_budget.unlimited; v[5] = era_years();
    return hash_bytes(h, v, sizeof(v));
}

void uim7_click(int panel, int a, int b)
{
    UNUSED(panel);
    uix_cmd(UC_M7, a, b);
    uix_dirty();
}
