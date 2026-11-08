/* ui_pol.cpp - politics panels (overview, decrees, letters, event card, election night). WINDOWS SIDE.
   Owner: M1. See ui_pol.h. Everything is composed into ui.cpp's window cache (C_WIN, 340x320 max, paper area
   328x294): a recomposition costs ~100-200 glyphs (~1-2 ms on the P2) and happens on interaction or when the
   politics data signature changes (checked twice a second by ui.cpp). Period tone: THEME 6. */
#include "common.h"
#include "ui_pol.h"
#include "ui.h"
#include "art.h"
#include "text.h"
#include "sim.h"
#include "politics.h"
#include "util.h"
#include "hard.h"       /* 1.1 D1: the level's notes (the neighbour's patience, authority, card severity) */

/* colours (art/ui_layout.h values; that header is ui.cpp's) */
#define C_PAPER_DK  213, 198, 172
#define C_PAPER_LT  238, 230, 205
#define C_GREEN      74, 105,  74
#define C_BLUE       57,  85, 106
#define C_AMBER     189, 133,  41
#define C_RED       156,  56,  41
#define C_SEL       197, 186, 148

enum { V_TABS, V_EVENT, V_ELECTION };
enum { T_OVERVIEW, T_DECREES, T_LETTERS };
enum { UPA_VIEW, UPA_SEL_DECREE, UPA_SEL_LETTER, UPA_SIGN, UPA_REPEAL, UPA_SEND, UPA_CHOOSE, UPA_LATER,
       UPA_ELECTION_OK, UPA_TAB };

static int s_view = V_TABS, s_sel_dec = DEC_HOUSING, s_sel_let = LET_HOUSING, s_seen_election;

static PIX rgb(int r, int g, int b) { return gfx_rgb(r, g, b); }
#define PCOL(c) rgb(c)

static int election_unseen(void)
{
    const PolElection *e = politics_last_election();
    return e && e->year != s_seen_election;
}

void uipol_reset(void)
{
    const PolElection *e = politics_last_election();
    s_view = V_TABS;
    s_sel_dec = DEC_HOUSING;
    s_sel_let = LET_HOUSING;
    s_seen_election = e ? e->year : 0;      /* a loaded game doesn't replay its last election night */
}

void uipol_on_open(void)
{
    if (!politics_enabled()) { s_view = V_TABS; return; }
    if (election_unseen()) s_view = V_ELECTION;
    else if (politics_pending_event() >= 0) s_view = V_EVENT;
    else if (politics_decree_state(DEC_TOWN_RIGHTS) != DS_HIDDEN && !politics_decree_active(DEC_TOWN_RIGHTS)) {
        s_view = V_TABS; uix_set_tab(1); s_sel_dec = DEC_TOWN_RIGHTS;   /* PL1: the waiting town-rights decree (\xa7 on Approval) */
    } else s_view = V_TABS;
}

void uipol_title(char *buf, int size)
{
    int ev = politics_pending_event();
    if (s_view == V_EVENT && ev >= 0) str_printf(buf, size, "\xa7 %s", ev == EV_CAMPAIGN ? "The President's office" : "A matter for the President");
    else if (s_view == V_ELECTION) str_copy(buf, "Election night", size);
    else str_copy(buf, "The President's desk", size);
}

u32 uipol_data_sig(u32 h)
{
    int v[12], b;
    v[0] = s_view; v[1] = s_sel_dec; v[2] = s_sel_let; v[3] = politics_authority(); v[4] = politics_pending_event();
    v[5] = politics_meter(METER_EAST); v[6] = politics_meter(METER_WEST); v[7] = politics_popularity();
    v[8] = politics_decree_gap() * 64 + politics_letter_gap(); v[9] = politics_elections_held(); v[10] = uix_tab();
    v[11] = politics_enabled();
    h = hash_bytes(h, v, sizeof(v));
    for (b = 0; b < BLOC_COUNT; b++) { v[b] = politics_approval(b) * 1000 + politics_weight(b); }
    h = hash_bytes(h, v, BLOC_COUNT * (int)sizeof(int));
    for (b = 0; b < DEC_COUNT; b++) { v[0] = politics_decree_active(b) * 100 + politics_decree_cooldown(b); h = hash_bytes(h, v, sizeof(int)); }
    return h;
}

void uipol_click(int a, int b)
{
    switch (a) {
    case UPA_VIEW:        s_view = b; break;
    case UPA_SEL_DECREE:  s_sel_dec = b; break;
    case UPA_SEL_LETTER:  s_sel_let = b; break;
    case UPA_SIGN:        uix_cmd(UC_POLITICS, POLA_SIGN, b); break;
    case UPA_REPEAL:      uix_cmd(UC_POLITICS, POLA_REPEAL, b); break;
    case UPA_SEND:        uix_cmd(UC_POLITICS, POLA_LETTER, b); break;
    case UPA_CHOOSE:      uix_cmd(UC_POLITICS, POLA_CHOOSE, b); s_view = V_TABS; uix_set_tab(T_OVERVIEW); break;
    case UPA_LATER:       s_view = V_TABS; break;
    case UPA_ELECTION_OK: {
        const PolElection *e = politics_last_election();
        s_seen_election = e ? e->year : 0;
        s_view = politics_pending_event() >= 0 ? V_EVENT : V_TABS;
        break; }
    case UPA_TAB:         s_view = V_TABS; uix_set_tab(b); break;
    }
    uix_dirty();
}

void uipol_message(const PolMsg *m)
{
    char buf[160];
    int sev = SEV_POLITICAL;
    if (!politics_msg_text(buf, sizeof(buf), m, &sev)) return;
    uix_ticker(sev, buf, PANEL_POLITICS);
}

/* ------------------------------------------------------------------ helpers */
static int signed_font(int v) { return v > 0 ? FONT_BODY_GREEN : v < 0 ? FONT_BODY_RED : FONT_BODY_GREY; }

/* effect list as wrapped lines "Workers +6 · Rural migration -40%"; returns the new y */
static int fx_lines(int x, int y, int w, const PolFx *fx, int maxl, int font)
{
    char all[400], one[80], lines[6][96];
    int i, n, k;
    all[0] = 0;
    for (i = 0; i < POL_FX_MAX; i++) {
        if (fx[i].kind == FX_NONE) continue;
        politics_fx_text(one, sizeof(one), &fx[i]);
        if (all[0]) str_printf(all + strlen(all), (int)(sizeof(all) - strlen(all)), "  " TEXT_DOT " %s", one);
        else str_copy(all, one, sizeof(all));
    }
    if (!all[0]) str_copy(all, "No other effect", sizeof(all));
    n = uix_wrap(all, font, w, lines, 6);
    for (k = 0; k < n && k < maxl; k++) { uix_text_fit(x, y, lines[k], font, w); y += 10; }
    return y;
}

/* paragraph text with '\n' breaks, wrapped; returns the new y */
static int para(int x, int y, int w, const char *s, int font, int maxl, int lh)
{
    char buf[400], lines[10][96];
    char *p = buf, *q;
    int n, k, used = 0;
    str_copy(buf, s, sizeof(buf));
    while (p && *p && used < maxl) {
        q = strchr(p, '\n');
        if (q) *q = 0;
        n = uix_wrap(p, font, w, lines, 10);
        for (k = 0; k < n && used < maxl; k++, used++) { uix_text(x, y, lines[k], font); y += lh; }
        p = q ? q + 1 : NULL;
    }
    return y;
}

static void top_reasons(int b, char *buf, int size)
{
    Term t[POL_REASONS];
    int n = politics_reasons(b, t, POL_REASONS), k, best1 = -1, best2 = -1;
    char a[16];
    for (k = 0; k < n; k++) {
        if (t[k].id == PR_BASE || !t[k].amount) continue;
        if (best1 < 0 || IABS(t[k].amount) > IABS(t[best1].amount)) { best2 = best1; best1 = k; }
        else if (best2 < 0 || IABS(t[k].amount) > IABS(t[best2].amount)) best2 = k;
    }
    buf[0] = 0;
    if (best1 < 0) { str_copy(buf, "No strong feelings", size); return; }
    text_fmt_signed(a, sizeof(a), t[best1].amount);
    str_printf(buf, size, "%s %s %s", t[best1].amount > 0 ? GLYPH_ARROW_UP : GLYPH_ARROW_DOWN, politics_reason_label(t[best1].id), a);
    if (best2 >= 0) {
        text_fmt_signed(a, sizeof(a), t[best2].amount);
        str_printf(buf + strlen(buf), size - (int)strlen(buf), "  %s %s %s", t[best2].amount > 0 ? GLYPH_ARROW_UP : GLYPH_ARROW_DOWN,
                   politics_reason_label(t[best2].id), a);
    }
}

static void when_text(char *buf, int size, int months)
{
    if (months < 12) str_printf(buf, size, "%d month%s", months, months == 1 ? "" : "s");
    else str_printf(buf, size, "%d year%s", (months + 6) / 12, (months + 6) / 12 == 1 ? "" : "s");
}

/* ------------------------------------------------------------------ overview */
static void view_overview(int x, int y, int w, int h)
{
    char a[96], b[128], c[32];
    int yy = y, k, bw, n, ev;
    /* popularity + authority */
    str_printf(a, sizeof(a), "Popularity %d%%", politics_popularity());
    uix_text(x + 6, yy, a, FONT_HEAD);
    str_printf(a, sizeof(a), "Authority %d / %d", politics_authority(), politics_authority_cap());
    uix_text_r(x + w - 6, yy + 2, a, FONT_BODY_DARK);
    yy += 15;
    uix_bar(x + w - 126, yy, 120, 7, politics_authority() * 100 / IMAX(1, politics_authority_cap()), 0, PCOL(C_AMBER));
    when_text(c, sizeof(c), IMAX(0, politics_months_to_election() + 1));
    str_printf(b, sizeof(b), "+%d.%d a month " TEXT_DOT " election Jan %d (in %s)", politics_authority_income10() / 10,
               politics_authority_income10() % 10, politics_next_election(), c);
    uix_text_fit(x + 6, yy - 1, b, FONT_BODY_GREY, w - 140);
    yy += 12;
    uix_rule(x + 6, yy, w - 12); yy += 5;
    /* blocs */
    bw = 100;
    for (k = 0; k < BLOC_COUNT; k++) {
        int ap = politics_approval(k), wt = politics_weight(k);
        PIX col = ap >= 60 ? PCOL(C_GREEN) : ap >= 40 ? PCOL(C_AMBER) : PCOL(C_RED);
        str_printf(a, sizeof(a), "%s", politics_bloc_name(k));
        uix_text(x + 6, yy, a, FONT_BODY_DARK);
        uix_text(x + 6 + uix_text_w(a, FONT_BODY_DARK) + 4, yy, politics_bloc_finnish(k), FONT_BODY_GREY);
        str_printf(a, sizeof(a), "%d.%d%% of voters", wt / 10, wt % 10);
        uix_text_r(x + w - 6 - bw - 30, yy, a, FONT_BODY_GREY);
        uix_bar(x + w - 6 - bw - 24, yy + 1, bw, 8, ap, 0, col);
        str_printf(a, sizeof(a), "%d", ap);
        uix_text_r(x + w - 6, yy, a, FONT_BODY_DARK);
        yy += 11;
        top_reasons(k, b, sizeof(b));
        uix_text_fit(x + 14, yy, b, FONT_BODY_GREY, w - 20);
        yy += 13;
    }
    uix_rule(x + 6, yy, w - 12); yy += 5;
    /* meters */
    for (k = 0; k < METER_COUNT; k++) {
        int m = politics_meter(k);
        uix_text(x + 6, yy, politics_meter_name(k), FONT_BODY_DARK);
        uix_bar(x + 110, yy + 1, w - 150, 8, (m - 50) * 2, 1, k == METER_EAST ? PCOL(C_RED) : PCOL(C_BLUE));
        str_printf(a, sizeof(a), "%d", m);
        uix_text_r(x + w - 6, yy, a, FONT_BODY_DARK);
        yy += 11;
    }
    if (politics_meter(METER_EAST) >= 30 && hard_note(HN_EAST, b, sizeof(b))) {   /* 1.1 D1: "The neighbour's patience (Hard): drifts to 40" */
        char c2[64];
        if (hard_note(HN_AUTHORITY, c2, sizeof(c2))) str_printf(b + strlen(b), (int)sizeof(b) - (int)strlen(b), "; authority: %s", c2 + strlen(diff()->name) + 2);
        uix_text_fit(x + 6, yy, b, FONT_BODY_GREY, w - 12);
    } else
    uix_text_fit(x + 6, yy, politics_meter(METER_EAST) < 30 ? "Night frost: clearing orders shrink and oil costs more."
                                                                               : "East: clearing orders and oil prices. West: orders and loans.",
                 FONT_BODY_GREY, w - 12);
    yy += 12;
    uix_rule(x + 6, yy, w - 12); yy += 5;
    /* decrees in force */
    str_printf(a, sizeof(a), "Decrees in force (%d / %d slots)", politics_active_count(), POL_SLOTS);
    uix_text(x + 6, yy, a, FONT_HEAD);
    yy += 14;
    n = 0;
    for (k = 0; k < DEC_COUNT && yy < y + h - 34; k++) {
        if (!politics_decree_active(k)) continue;
        str_printf(a, sizeof(a), "\xa7 %s", g_decrees[k].name);
        uix_text_fit(x + 10, yy, a, FONT_BODY_DARK, w - 20);
        yy += 11; n++;
    }
    if (!n) { uix_text(x + 10, yy, "None. The cabinet awaits instructions.", FONT_BODY_GREY); yy += 11; }
    /* waiting card */
    ev = politics_pending_event();
    if (ev >= 0) {
        int by = y + h - 22;
        str_printf(a, sizeof(a), "\xa7 Waiting: %s", g_events[ev].title);
        uix_text_fit(x + 6, by + 4, a, FONT_HEAD_RED, w - 100);
        uix_button(x + w - 88, by, 82, 18, "Read it", UPA_VIEW, V_EVENT, 1);
    }
}

/* ------------------------------------------------------------------ decrees */
static void view_decrees(int x, int y, int w, int h)
{
    char a[96], b[96];
    int yy = y, k, st, rs, idx, dec = s_sel_dec;
    const DecreeDef *d;
    str_printf(a, sizeof(a), "Authority %d " TEXT_DOT " slots %d / %d", politics_authority(), politics_active_count(), POL_SLOTS);
    uix_text(x + 6, yy, a, FONT_BODY_DARK);
    if (politics_decree_gap() > 0) { str_printf(b, sizeof(b), "drafting: %d m", politics_decree_gap()); uix_text_r(x + w - 6, yy, b, FONT_BODY_GREY); }
    yy += 12;
    for (k = 0; k < DEC_COUNT; k++) {
        if (g_decrees[k].flags & DF_HIDDEN && politics_decree_state(k) == DS_HIDDEN) continue;
        st = politics_decree_state(k);
        idx = uix_ctl(x + 4, yy, w - 8, 11, UPA_SEL_DECREE, k);
        if (k == dec) uix_fill(x + 4, yy, x + w - 4, yy + 11, PCOL(C_SEL));
        else if (uix_ctl_hot(idx)) uix_fill(x + 4, yy, x + w - 4, yy + 11, PCOL(C_PAPER_LT));
        str_printf(a, sizeof(a), "\xa7 %s", g_decrees[k].name);
        uix_text_fit(x + 8, yy, a, st == DS_OK || st == DS_ACTIVE ? FONT_BODY_DARK : FONT_BODY_GREY, w - 70);
        if (st == DS_ACTIVE) uix_text_r(x + w - 8, yy, "in force", FONT_BODY_GREEN);
        else if (st == DS_COOLDOWN) { str_printf(b, sizeof(b), "%d m", politics_decree_cooldown(k)); uix_text_r(x + w - 8, yy, b, FONT_BODY_GREY); }
        else if (st == DS_TOO_EARLY) { str_printf(b, sizeof(b), "%d", g_decrees[k].year_from); uix_text_r(x + w - 8, yy, b, FONT_BODY_GREY); }
        else { str_printf(b, sizeof(b), "%d", g_decrees[k].cost); uix_text_r(x + w - 8, yy, b, st == DS_OK ? FONT_BODY_DARK : FONT_BODY_RED); }
        yy += 11;
    }
    yy += 2;
    uix_rule(x + 6, yy, w - 12); yy += 4;
    if (dec < 0 || dec >= DEC_COUNT) return;
    d = &g_decrees[dec];
    st = politics_decree_state(dec);
    rs = politics_repeal_state(dec);
    uix_text_fit(x + 6, yy, d->name, FONT_HEAD, w - 12);
    yy += 14;
    yy = para(x + 6, yy, w - 12, d->desc, FONT_BODY_GREY, 2, 10);
    yy += 1;
    if (d->sign[0].kind != FX_NONE) {
        uix_text(x + 6, yy, "When signed:", FONT_BODY_DARK);
        yy = fx_lines(x + 70, yy, w - 76, d->sign, 2, FONT_BODY_DARK);
    }
    if (d->active[0].kind != FX_NONE) {
        uix_text(x + 6, yy, "While in force:", FONT_BODY_DARK);
        yy = fx_lines(x + 82, yy, w - 88, d->active, 3, FONT_BODY_DARK);
    }
    {
        int by = y + h - 20;
        if (st == DS_ACTIVE) {
            str_printf(a, sizeof(a), "Repeal (%d authority)", (d->cost + 1) / 2);
            uix_button(x + w - 146, by, 140, 18, a, UPA_REPEAL, dec, rs == DS_OK);
            uix_text(x + 6, by + 4, "In force. Repealing frees the slot.", FONT_BODY_GREEN);
        } else {
            str_printf(a, sizeof(a), "Sign the decree (%d)", d->cost);
            uix_button(x + w - 146, by, 140, 18, a, UPA_SIGN, dec, st == DS_OK);
            politics_ds_text(b, sizeof(b), st, dec);
            uix_text_fit(x + 6, by + 4, b, st == DS_OK ? FONT_BODY_GREEN : FONT_BODY_RED, w - 160);
        }
    }
}

/* ------------------------------------------------------------------ letters */
static void view_letters(int x, int y, int w, int h)
{
    char a[96], b[96];
    const LetterDef *L = &g_letters[ICLAMP(s_sel_let, 0, LET_COUNT - 1)];
    int yy = y, k, idx, lw = 104, px = x + lw + 8, pw = w - lw - 12, st = politics_letter_state(s_sel_let);
    const SimSettings *ss = sim_settings();
    uix_text(x + 6, yy, "Write to", FONT_BODY_DARK);
    yy += 12;
    for (k = 0; k < LET_COUNT; k++) {
        idx = uix_ctl(x + 4, yy, lw, 22, UPA_SEL_LETTER, k);
        if (k == s_sel_let) uix_fill(x + 4, yy, x + 4 + lw, yy + 22, PCOL(C_SEL));
        else if (uix_ctl_hot(idx)) uix_fill(x + 4, yy, x + 4 + lw, yy + 22, PCOL(C_PAPER_LT));
        {
            char lines[2][96];
            int n = uix_wrap(g_letters[k].to, FONT_BODY_DARK, lw - 6, lines, 2), i;
            for (i = 0; i < n && i < 2; i++) uix_text_fit(x + 7, yy + 1 + i * 10, lines[i], FONT_BODY_DARK, lw - 6);
        }
        yy += 24;
    }
    str_printf(a, sizeof(a), "Authority %d", politics_authority());
    uix_text(x + 6, yy + 4, a, FONT_BODY_GREY);
    /* the typed letter */
    uix_inset(px, y, pw, h - 26, PCOL(C_PAPER_LT));
    yy = y + 6;
    str_printf(a, sizeof(a), "To: %s", L->to);
    uix_text_fit(px + 6, yy, a, FONT_BODY_DARK, pw - 12); yy += 10;
    str_printf(a, sizeof(a), "Re: %s", L->subject);
    uix_text_fit(px + 6, yy, a, FONT_BODY_DARK, pw - 12); yy += 14;
    yy = para(px + 6, yy, pw - 12, L->body, FONT_BODY_DARK, 12, 10);
    yy += 4;
    uix_text(px + 14, yy, ss->president[0] ? ss->president : "Kekkola", FONT_HEAD);
    yy += 13;
    uix_text(px + 14, yy, "President of the Republic", FONT_BODY_GREY);
    yy += 14;
    uix_rule(px + 6, yy, pw - 12); yy += 4;
    yy = fx_lines(px + 6, yy, pw - 12, L->fx, 3, FONT_BODY_DARK);
    {
        int by = y + h - 20;
        str_printf(a, sizeof(a), "Send (%d authority)", L->cost);
        uix_button(x + w - 136, by, 130, 18, a, UPA_SEND, s_sel_let, st == DS_OK);
        if (st == DS_GAP) str_printf(b, sizeof(b), "Next letter in %d months", politics_letter_gap());
        else if (st == DS_NO_AUTHORITY) str_printf(b, sizeof(b), "Needs %d authority", L->cost);
        else str_copy(b, "One letter every six months.", sizeof(b));
        uix_text_fit(x + 6, by + 4, b, st == DS_OK ? FONT_BODY_GREY : FONT_BODY_RED, w - 150);
    }
}

/* ------------------------------------------------------------------ event card */
static void view_event(int x, int y, int w, int h)
{
    char a[96];
    PolFx fx[POL_FX_MAX];
    int ev = politics_pending_event(), yy = y + 2, k;
    const EventDef *e;
    if (ev < 0) { uix_text(x + 6, yy + 6, "The desk is clear.", FONT_BODY_GREY); uix_button(x + w - 86, y + h - 20, 80, 18, "Back", UPA_LATER, 0, 1); return; }
    e = &g_events[ev];
    uix_text_fit(x + 6, yy, e->title, FONT_HEAD_RED, w - 12);
    text_fmt_date(a, sizeof(a), sim_date()->year, sim_date()->month);
    uix_text_r(x + w - 6, yy + 2, a, FONT_BODY_GREY);
    yy += 16;
    yy = para(x + 6, yy, w - 12, e->text, FONT_BODY_DARK, 6, 11);
    yy += 6;
    for (k = 0; k < e->nch; k++) {
        politics_choice_fx(ev, k, fx);
        uix_button(x + 6, yy, w - 12, 18, e->ch[k].label, UPA_CHOOSE, k, 1);
        yy += 20;
        yy = fx_lines(x + 14, yy, w - 20, fx, 2, FONT_BODY_GREY);
        yy += 4;
    }
    if (politics_event_age() > 0) {
        str_printf(a, sizeof(a), "Waiting %d month%s; the cabinet decides after %d.", politics_event_age(),
                   politics_event_age() == 1 ? "" : "s", POL_EVENT_EXPIRE);
        uix_text_fit(x + 6, y + h - 16, a, FONT_BODY_GREY, w - 110);
    } else if (hard_note(HN_CARDS, a, sizeof(a))) uix_text_fit(x + 6, y + h - 16, a, FONT_BODY_GREY, w - 110);   /* 1.1 D1 */
    uix_button(x + w - 96, y + h - 20, 90, 18, "Decide later", UPA_LATER, 0, 1);
}

/* ------------------------------------------------------------------ election night */
static void view_election(int x, int y, int w, int h)
{
    char a[128];
    const PolElection *e = politics_last_election();
    int yy = y + 2, k, bx, bw;
    if (!e) { s_view = V_TABS; return; }
    str_printf(a, sizeof(a), "January %d", e->year);
    uix_text(x + 6, yy, a, FONT_HEAD);
    yy += 18;
    uix_portrait(e->skipped ? PRES_MOOD_STERN : e->verdict > 0 ? PRES_MOOD_DELIGHTED : e->verdict < 0 ? PRES_MOOD_WORRIED : PRES_MOOD_CONTENT, x + 6, yy);
    bx = x + 80; bw = w - 86;
    if (e->skipped) {
        para(bx, yy, bw, "No election this year. The emergency powers law remains in force, and so does the President.", FONT_BODY_DARK, 5, 11);
        uix_button(x + w - 96, y + h - 20, 90, 18, "Very well", UPA_ELECTION_OK, 0, 1);
        return;
    }
    str_printf(a, sizeof(a), "%d of 300 electors", e->electors);
    uix_text(bx, yy, a, FONT_HEAD);
    yy += 16;
    /* electors bar with the majority mark at 151 */
    uix_bar(bx, yy, bw, 10, e->electors * 100 / 300, 0, e->verdict > 0 ? PCOL(C_GREEN) : e->verdict < 0 ? PCOL(C_RED) : PCOL(C_AMBER));
    uix_fill(bx + 2 + (bw - 4) * 151 / 300, yy - 2, bx + 3 + (bw - 4) * 151 / 300, yy + 12, PCOL(C_RED));
    yy += 14;
    yy = para(bx, yy, bw, politics_verdict_text(e->verdict), FONT_BODY_DARK, 3, 11);
    yy += 2;
    str_printf(a, sizeof(a), "Popularity %d, momentum %+d, campaign %+d, turnout %+d", e->share - e->momentum / 2 - e->campaign - e->turnout,
               e->momentum, e->campaign, e->turnout);
    uix_text_fit(bx, yy, a, FONT_BODY_GREY, bw);
    yy = IMAX(yy + 14, y + 20 + 84);
    uix_rule(x + 6, yy, w - 12); yy += 5;
    for (k = 0; k < BLOC_COUNT; k++) {
        str_printf(a, sizeof(a), "%s (%d.%d%%)", politics_bloc_name(k), e->bloc_weight[k] / 10, e->bloc_weight[k] % 10);
        uix_text(x + 10, yy, a, FONT_BODY_DARK);
        uix_bar(x + 150, yy + 1, w - 190, 8, e->bloc_approval[k], 0, e->bloc_approval[k] >= 60 ? PCOL(C_GREEN) : e->bloc_approval[k] >= 40 ? PCOL(C_AMBER) : PCOL(C_RED));
        str_printf(a, sizeof(a), "%d", e->bloc_approval[k]);
        uix_text_r(x + w - 8, yy, a, FONT_BODY_DARK);
        yy += 11;
    }
    yy += 4;
    str_printf(a, sizeof(a), "Authority %+d tonight. This term: %s.", e->auth_now,
               e->verdict > 0 ? "+1 a month, limit 35" : e->verdict < 0 ? "-1 a month, limit 25" : "as usual");
    uix_text_fit(x + 6, yy, a, signed_font(e->verdict), w - 12);
    uix_button(x + w - 96, y + h - 20, 90, 18, e->verdict < 0 ? "Hm." : "Very good", UPA_ELECTION_OK, 0, 1);
}

/* ------------------------------------------------------------------ entry */
void uipol_panel(int x, int y, int w, int h)
{
    static const char *const tn[3] = { "Overview", "Decrees", "Letters" };
    int t;
    if (!politics_enabled()) {
        uix_text(x + 6, y + 8, "Politics is off for this town.", FONT_HEAD);
        para(x + 6, y + 28, w - 12, "Turn it on at New Game. The President remains above politics, as the constitution intends.",
             FONT_BODY_GREY, 4, 11);
        return;
    }
    if (s_view == V_EVENT && politics_pending_event() < 0) s_view = V_TABS;
    if (s_view == V_ELECTION && !politics_last_election()) s_view = V_TABS;
    if (s_view == V_EVENT) { view_event(x, y, w, h); return; }
    if (s_view == V_ELECTION) { view_election(x, y, w, h); return; }
    t = ICLAMP(uix_tab(), 0, 2);
    uix_tabs(x + 4, y + 4, x + w - 4, tn, 3, t);
    switch (t) {
    case T_OVERVIEW: view_overview(x, y + 22, w, h - 22); break;
    case T_DECREES:  view_decrees(x, y + 22, w, h - 22); break;
    default:         view_letters(x, y + 22, w, h - 22); break;
    }
}

int uipol_tip(char lines[][64], int max)
{
    int n = 0, b;
    if (!politics_enabled()) {
        if (n < max) str_copy(lines[n++], "Politics is off for this town.", 64);
        return n;
    }
    if (n < max) str_printf(lines[n++], 64, "Popularity %d%% " TEXT_DOT " authority %d / %d", politics_popularity(),
                            politics_authority(), politics_authority_cap());
    for (b = 0; b < BLOC_COUNT && n < max; b++)
        str_printf(lines[n++], 64, "%s %d (%d.%d%% of voters)", politics_bloc_name(b), politics_approval(b),
                   politics_weight(b) / 10, politics_weight(b) % 10);
    if (n < max) str_printf(lines[n++], 64, "East %d " TEXT_DOT " West %d " TEXT_DOT " election Jan %d", politics_meter(METER_EAST),
                            politics_meter(METER_WEST), politics_next_election());
    if (politics_pending_event() >= 0 && n < max) str_printf(lines[n++], 64, "Waiting: %s", g_events[politics_pending_event()].title);
    if (n < max) str_copy(lines[n++], "Click for the President's desk", 64);
    return n;
}

void uipol_show(int what)
{
    int sel = what / 10, v = what % 10;
    ui_open_panel(PANEL_POLITICS);
    if (v <= 2) { s_view = V_TABS; uix_set_tab(v); }
    else s_view = v == 3 ? V_EVENT : V_ELECTION;
    if (v == 1 && sel < DEC_COUNT) s_sel_dec = sel;
    if (v == 2 && sel < LET_COUNT) s_sel_let = sel;
    uix_dirty();
}
