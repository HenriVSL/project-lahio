/* ui_news.cpp - M7: Maakunnan Sanomat. The January issue (3 stories + the filler), the ceremony page (town status)
   and the 1990 newsreel (summary lines + the population curve since the start), all from era.h's NewsIssue. Drawn
   straight onto the back buffer each frame while open (no VRAM cache: 64-zoom art leaves < 0.5 MB at 800x600); the
   game is paused meanwhile. A click, Esc, Enter or Space closes it. WINDOWS SIDE. Owner: M7. Art: masthead,
   rule_news, stamp_seal, stamp_decree, pres_l_* (art/UI.md). */
#include <windows.h>
#include "common.h"
#include "ui_m7.h"
#include "ui_pol.h"
#include "ui.h"
#include "art.h"
#include "gfx.h"
#include "text.h"
#include "sim.h"
#include "politics.h"
#include "era.h"
#include "util.h"
#include "advice.h"     /* PL1: the Cabinet's lines (AGO_*), progression, purposes */
#include "defs.h"
#include "hard.h"       /* 1.1 D1: the Presidency page (difficulty levers) */

#define C_PAPER   236, 228, 204
#define C_INK      41,  36,  32
#define C_RULE     90,  85,  82
#define C_RED     156,  56,  41
#define C_SHADE   213, 198, 172

static int s_open, s_kind, s_ids_ok;
/* PL1: the Cabinet box's clickable lines (rects from the last draw) and the mouse */
static s16 s_cabr[NEWS_CAB][4];
static int s_ncabr, s_mx = -1, s_my = -1;
static int s_guide;                       /* the Guide page is open */
static int s_guide_page;                  /* 1.1 D1: 0 = the handbook, 1 = the Presidency (difficulty) */
static int s_masthead, s_rule, s_seal, s_decree;
static NewsIssue s_issue;                 /* a copy: the era may build the next issue while this one is shown */
static char s_reel[12][96];
static int  s_nreel;

static PIX col(int r, int g, int b) { return gfx_rgb(r, g, b); }

static void ids(void)
{
    if (s_ids_ok) return;
    s_masthead = art_exists("masthead") ? art_find("masthead") : 0;
    s_rule = art_exists("rule_news") ? art_find("rule_news") : 0;
    s_seal = art_exists("stamp_seal") ? art_find("stamp_seal") : 0;
    s_decree = art_exists("stamp_decree") ? art_find("stamp_decree") : 0;
    s_ids_ok = 1;
}

void uinews_open(void)
{
    const NewsIssue *n = era_news();
    if (!n) return;
    s_issue = *n;
    s_kind = n->kind;
    if (s_kind == NEWS_NEWSREEL) s_nreel = era_newsreel_lines(s_reel, 12);
    s_open = 1; s_ncabr = 0;
}

void uinews_open_newsreel(void)
{
    const NewsIssue *n;
    era_news_request_newsreel();
    n = era_news_kind(NEWS_NEWSREEL);
    if (!n) return;
    s_issue = *n;
    s_kind = NEWS_NEWSREEL;
    s_nreel = era_newsreel_lines(s_reel, 12);
    s_open = 1;
}

int uinews_active(void) { return s_open; }

void uinews_close(void)
{
    if (!s_open) return;
    s_open = 0;
    era_news_seen_kind(s_kind);
}

void uinews_to_ticker(void)
{
    const NewsIssue *n = era_news();
    char buf[160];
    if (!n || !era_news_pending()) return;
    str_printf(buf, sizeof(buf), "Maakunnan Sanomat: %s. %s.", n->head[0], n->deck[0]);
    ui_ticker_push(n->kind == NEWS_SPECIAL ? SEV_WARN : SEV_INFO, buf, -1, -1);
    if (n->ncab > 0 && n->cab_go[0] != AGO_NONE) {      /* PL1: newspaper=0: the Cabinet's first line goes to the ticker too */
        char c[160];
        str_printf(c, sizeof(c), "From the Cabinet: %s", n->cab[0]);
        ui_ticker_push(SEV_POLITICAL, c, n->cab_x[0], n->cab_y[0]);
    }
    era_news_seen();
}

static int cab_hit(int mx, int my)
{
    int k;
    for (k = 0; k < s_ncabr; k++)
        if (mx >= s_cabr[k][0] && my >= s_cabr[k][1] && mx < s_cabr[k][0] + s_cabr[k][2] && my < s_cabr[k][1] + s_cabr[k][3]) return k;
    return -1;
}

int uinews_input(const InputState *in)
{
    if (!s_open) return 0;
    s_mx = in->inside ? in->mx : -1; s_my = in->inside ? in->my : -1;
    if (in->pressed & MOUSE_L) {          /* PL1: a click on a Cabinet line goes there (after the paper closes) */
        int k = cab_hit(s_mx, s_my);
        if (k >= 0 && s_issue.cab_go[k] != AGO_NONE) ui_advice_go(s_issue.cab_go[k], s_issue.cab_x[k], s_issue.cab_y[k]);
        uinews_close();
    } else if (in->pressed & MOUSE_R) uinews_close();
    return 1;
}

int uinews_key(int vk)
{
    if (!s_open) return 0;
    if (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_SPACE) uinews_close();
    return 1;
}

/* wrapped text in a column; returns the y below it */
static int column(int x, int y, int w, int ybot, const char *s, int font)
{
    char lines[16][96], cut[100];
    int n = IMIN(16, uix_wrap(s, font, IMIN(w, 370), lines, 16)), i;   /* lines hold 95 characters: wrap at <= 370 px */
    for (i = 0; i < n && y + 11 <= ybot; i++, y += 11) {
        if (i + 1 < n && y + 22 > ybot) {            /* PL3: the story goes on below the page: end with an ellipsis */
            str_printf(cut, sizeof(cut), "%s \x85", lines[i]);
            uix_text_fit(x, y, cut, font, IMIN(w, 370));
        } else uix_text(x, y, lines[i], font);
    }
    return y;
}

/* PL3: heights the column stories need (the Cabinet box gives way to them; PL1 left it cutting them off) */
static int column_h(const char *s, int font, int w)
{
    char lines[16][96];
    return s[0] ? IMIN(16, uix_wrap(s, font, IMIN(w, 370), lines, 16)) * 11 : 0;
}
static int story_h(int w, int k)
{
    const NewsIssue *n = &s_issue;
    char lines[4][96];
    if (!n->head[k][0]) return 0;
    return IMIN(3, uix_wrap(n->head[k], FONT_HEAD, IMIN(w, 370), lines, 3)) * 14 + column_h(n->deck[k], FONT_BODY_RED, w) + 2 +
           column_h(n->body[k], FONT_BODY_DARK, w) + 6;
}

/* PL1: word wrap into lines of up to 159 characters (the shared helper stops at 95), by pixel width */
static int wrap_wide(const char *s, int font, int w, char out[][160], int maxl)
{
    char line[160], t[320];
    int n = 0, i = 0, k;
    line[0] = 0;
    for (;;) {
        char word[96];
        while (s[i] == ' ') i++;
        if (!s[i]) break;
        for (k = 0; s[i] && s[i] != ' ' && k < 94; k++, i++) word[k] = s[i];
        word[k] = 0;
        if (line[0]) {
            str_printf(t, sizeof(t), "%s %s", line, word);
            if (uix_text_w(t, font) <= w && (int)strlen(t) < 159) { str_copy(line, t, sizeof(line)); continue; }
            if (n < maxl) str_copy(out[n], line, 160);
            n++;
        }
        str_copy(line, word, sizeof(line));
    }
    if (line[0]) { if (n < maxl) str_copy(out[n], line, 160); n++; }
    return IMIN(n, maxl);
}

/* PL1: "FROM THE CABINET" box (January paper, special edition) at the bottom of the page; returns its top y.
   PL3: at most `room` px high: lower-ranked lines are left out (the first one always shows) so the column
   stories above it keep their space. */
static int cabinet_box(int X, int W, int ybot, int room)
{
    const NewsIssue *n = &s_issue;
    char ln[NEWS_CAB][3][160];
    int nl[NEWS_CAB], k, j, h, y, bx = X + 12, bw = W - 24, tw = bw - 26, hot, ncab = 0;
    if (n->ncab <= 0) return ybot;
    h = 6 + 14 + 12;
    for (k = 0; k < n->ncab; k++) {
        nl[k] = wrap_wide(n->cab[k], FONT_BODY_DARK, tw, ln[k], 3);
        if (k > 0 && h + nl[k] * 11 + 3 > room) break;
        h += nl[k] * 11 + 3; ncab = k + 1;
    }
    y = ybot - h;
    uix_fill(bx, y, bx + bw, y + h, col(C_SHADE));
    uix_fill(bx, y, bx + bw, y + 1, col(C_INK)); uix_fill(bx, y + h - 1, bx + bw, y + h, col(C_INK));
    uix_fill(bx, y, bx + 1, y + h, col(C_INK)); uix_fill(bx + bw - 1, y, bx + bw, y + h, col(C_INK));
    uix_text(bx + 8, y + 4, "FROM THE CABINET", FONT_HEAD);
    uix_text_r(bx + bw - 8, y + 6, "respectfully submitted to the President", FONT_BODY_GREY);
    hot = cab_hit(s_mx, s_my);
    y += 6 + 14;
    s_ncabr = 0;
    for (k = 0; k < ncab; k++) {
        int f = (k == hot && n->cab_go[k] != AGO_NONE) ? FONT_BODY_RED : FONT_BODY_DARK;
        s_cabr[k][0] = (s16)(bx + 4); s_cabr[k][1] = (s16)(y - 1); s_cabr[k][2] = (s16)(bw - 8); s_cabr[k][3] = (s16)(nl[k] * 11 + 2);
        s_ncabr = k + 1;
        uix_text(bx + 10, y, GLYPH_BULLET, FONT_BODY_RED);
        for (j = 0; j < nl[k]; j++) uix_text(bx + 20, y + j * 11, ln[k][j], f);
        y += nl[k] * 11 + 3;
    }
    uix_text_r(bx + bw - 8, y, "Click a line to see for yourself", FONT_BODY_GREY);
    return ybot - h;
}

static int story(int x, int y, int w, int ybot, int k, int big)
{
    const NewsIssue *n = &s_issue;
    char lines[4][96];
    int i, nl;
    if (!n->head[k][0]) return y;
    nl = uix_wrap(n->head[k], FONT_HEAD, IMIN(w, 370), lines, 3);
    for (i = 0; i < nl; i++) {
        if (big) {   /* the main headline: drawn twice, 1 px apart, for a bolder face (PL1: red in an extra edition) */
            int hf = s_kind == NEWS_SPECIAL ? FONT_HEAD_RED : FONT_HEAD;
            uix_text_c(x, w, y, lines[i], hf);
            uix_text_c(x + 1, w, y, lines[i], hf);
        } else uix_text(x, y, lines[i], FONT_HEAD);
        y += 14;
    }
    if (big && w > 370) { x += (w - 370) / 2; w = 370; }    /* the main story's text block is centred */
    y = column(x, y, w, ybot, n->deck[k], FONT_BODY_RED) + 2;
    return column(x, y, w, ybot, n->body[k], FONT_BODY_DARK) + 6;
}

static void vrule(int x, int y0, int y1) { uix_fill(x, y0, x + 1, y1, col(C_RULE)); }
static void hrule(int x, int y, int w)
{
    if (s_rule) { int xx; const ArtSprite *a = art_get(ZOOM_64, s_rule); for (xx = 0; xx < w; xx += a->w) uix_sprite(s_rule, x + xx, y); }
    else { uix_fill(x, y, x + w, y + 2, col(C_INK)); uix_fill(x, y + 3, x + w, y + 4, col(C_INK)); }
}

static void pop_curve(int x, int y, int w, int h)
{
    int i, n = era_years();
    s32 hi = 100;
    uix_fill(x, y, x + w, y + h, col(C_SHADE));
    for (i = 0; i < n; i++) if (era_year_row(i)->pop > hi) hi = era_year_row(i)->pop;
    for (i = 0; i < n; i++) {
        const EraYear *r = era_year_row(i);
        int bx = x + 2 + i * (w - 4) / IMAX(1, n), bw = IMAX(1, (w - 4) / IMAX(1, n) - 1), bh = (int)((s64)r->pop * (h - 14) / hi);
        uix_fill(bx, y + h - 2 - bh, bx + bw, y + h - 2, col(C_INK));
        if (r->year % 10 == 0) { char a[8]; str_printf(a, sizeof(a), "%d", r->year); uix_text(bx, y + 1, a, FONT_BODY_DARK); }
    }
}

void uinews_draw(void)
{
    const NewsIssue *n = &s_issue;
    char a[96];
    int sw = gfx_screen_w(), sh = gfx_screen_h(), W, H, X, Y, y, cw, yb, k, mid;
    if (!s_open) return;
    ids();
    gfx_set_target(NULL);
    W = IMIN(620, sw - 16); H = IMIN(sh - 40, 420);
    X = (sw - W) / 2; Y = (sh - H) / 2;
    uix_fill(X + 4, Y + 4, X + W + 4, Y + H + 4, col(24, 24, 24));          /* shadow */
    uix_fill(X, Y, X + W, Y + H, col(C_PAPER));
    uix_fill(X, Y, X + W, Y + 1, col(C_RULE)); uix_fill(X, Y + H - 1, X + W, Y + H, col(C_RULE));
    uix_fill(X, Y, X + 1, Y + H, col(C_RULE)); uix_fill(X + W - 1, Y, X + W, Y + H, col(C_RULE));
    y = Y + 8;
    if (s_masthead) { const ArtSprite *m = art_get(ZOOM_64, s_masthead); uix_sprite(s_masthead, X + (W - m->w) / 2, y); y += m->h + 2; }
    else { uix_text_c(X, W, y + 10, "MAAKUNNAN SANOMAT", FONT_HEAD); y += 30; }
    str_printf(a, sizeof(a), "No. %d", n->issue_no);
    uix_text(X + 12, y, a, FONT_BODY_DARK);
    str_printf(a, sizeof(a), "%s %d", text_month_name(n->month), n->year);
    uix_text_c(X, W, y, s_kind == NEWS_CEREMONY ? "SPECIAL EDITION" : s_kind == NEWS_NEWSREEL ? "THE NEWSREEL" :
               s_kind == NEWS_SPECIAL ? "EXTRA EDITION" : a, s_kind == NEWS_SPECIAL ? FONT_BODY_RED : FONT_BODY_DARK);
    uix_text_r(X + W - 12, y, s_kind == NEWS_YEAR ? "Price 15 p" : a, FONT_BODY_DARK);
    y += 12; hrule(X + 10, y, W - 20); y += 8;
    yb = Y + H - 18;
    if (s_kind == NEWS_NEWSREEL) {
        uix_text_c(X + 14, W - 28, y, n->head[0], FONT_HEAD); uix_text_c(X + 15, W - 28, y, n->head[0], FONT_HEAD); y += 14;
        uix_text_c(X + 14, W - 28, y, n->deck[0], FONT_BODY_RED); y += 15;   /* the lines below carry the story */
        pop_curve(X + 14, y, W - 28, 80); y += 88;
        for (k = 0; k < s_nreel && y + 11 <= yb - (n->nverdict ? 16 + n->nverdict * 11 : 0); k++, y += 11)
            uix_text_fit(X + 18, y, s_reel[k], FONT_BODY_DARK, W - 36);
        if (n->nverdict) {        /* PL1 (PLAYTEST 2 item 10): "What the history books will say" */
            y += 4;
            hrule(X + 10, y, W - 20); y += 6;
            uix_text(X + 14, y, "WHAT THE HISTORY BOOKS WILL SAY", FONT_HEAD); y += 15;
            for (k = 0; k < n->nverdict && y + 11 <= yb; k++, y += 11) {
                uix_text(X + 18, y, GLYPH_BULLET, FONT_BODY_RED);
                uix_text_fit(X + 28, y, n->verdict[k], FONT_BODY_DARK, W - 46);
            }
        }
    } else {
        int top = y;
        y = story(X + 14, y, W - (s_kind == NEWS_CEREMONY ? 120 : 28), Y + H / 2, 0, 1);
        if (s_kind == NEWS_CEREMONY) {
            uix_portrait(politics_mood(), X + W - 90, top);
            if (s_seal) uix_sprite(s_seal, X + W - 100, top + 70);
            if (y < top + 96) y = top + 96;
        }
        hrule(X + 10, y, W - 20); y += 8;
        cw = (W - 28 - 40) / 3; mid = y;
        if (s_kind == NEWS_YEAR || s_kind == NEWS_SPECIAL) {   /* PL1: the Cabinet's column; PL3: under the stories */
            int need = IMAX(story_h(cw, 1), story_h(cw, 2)), third;
            if (s_kind == NEWS_YEAR && n->hind[0]) {
                char hl[3][96];
                third = IMIN(3, uix_wrap(n->hind_head, FONT_HEAD, cw, hl, 3)) * 14 + column_h(n->hind, FONT_BODY_DARK, cw);
            } else third = 14 + column_h(n->filler, FONT_BODY_DARK, cw);
            need = IMAX(need, third);
            yb = cabinet_box(X, W, yb, yb - mid - 6 - need) - 6;
        } else s_ncabr = 0;
        story(X + 14, mid, cw, yb, 1, 0);
        vrule(X + 14 + cw + 9, mid, yb);
        story(X + 14 + cw + 20, mid, cw, yb, 2, 0);
        vrule(X + 14 + 2 * cw + 29, mid, yb);
        if (s_kind == NEWS_YEAR && n->hind[0]) {    /* PL1 (item 10): "In hindsight" replaces the filler */
            int hy = mid;
            char hl[3][96];
            int nh = uix_wrap(n->hind_head, FONT_HEAD, cw, hl, 3), i;
            for (i = 0; i < nh; i++, hy += 14) uix_text(X + 14 + 2 * (cw + 20), hy, hl[i], FONT_HEAD);
            column(X + 14 + 2 * (cw + 20), hy, cw, yb, n->hind, FONT_BODY_DARK);
        } else {
            uix_text(X + 14 + 2 * (cw + 20), mid, n->filler_head, FONT_HEAD);
            column(X + 14 + 2 * (cw + 20), mid + 14, cw, yb, n->filler, FONT_BODY_DARK);
        }
        if (s_kind == NEWS_CEREMONY && s_decree) uix_sprite(s_decree, X + W - 150, yb - 36);
    }
    uix_text_c(X, W, Y + H - 14, "Click or press Esc to put the paper down", FONT_BODY_GREY);
}

/* ================================================================== PL1: the Guide ("The President's Handbook")
   One page in the newspaper's style (PLAYTEST 2 item 2d): the town ladder (population, what each status brings, where
   the town stands), what each system is for, the keys. F4 or the Guide key; the game pauses while it is open. */
void uiguide_open(void) { s_guide = 1; s_guide_page = 0; }
int  uiguide_active(void) { return s_guide; }
void uiguide_close(void) { s_guide = 0; }
int  uiguide_input(const InputState *in)
{
    if (!s_guide) return 0;
    if (in->pressed & (MOUSE_L | MOUSE_R)) s_guide = 0;
    return 1;
}
int  uiguide_key(int vk)
{
    if (!s_guide) return 0;
    if (vk == VK_ESCAPE || vk == VK_RETURN || vk == VK_SPACE || vk == VK_F4) s_guide = 0;
    if (vk == VK_TAB || vk == VK_RIGHT || vk == VK_LEFT || vk == VK_NEXT || vk == VK_PRIOR) s_guide_page ^= 1;   /* 1.1 D1 */
    return 1;
}

/* a heading and wrapped lines in a column; returns the y below */
static int guide_par(int x, int y, int w, int ybot, const char *head, const char *body)
{
    char ln[4][160];
    int n, i;
    if (head) { uix_text(x, y, head, FONT_BODY_RED); y += 11; }
    n = wrap_wide(body, FONT_BODY_DARK, w, ln, 4);
    for (i = 0; i < n && y + 11 <= ybot; i++, y += 11) uix_text(x, y, ln[i], FONT_BODY_DARK);
    return y + 2;
}

void uiguide_draw(void)
{
    static const char *const keys[4] = {
        "R roads  H/A homes  C shops  I industry  P power  W water and pipes  S services  B bulldoze  Q query  Ctrl+Z undo",
        "1-9 overlays (1 power, 2 water/heat)  Tab buildings in an overlay  V zone borders  + / - or Z zoom  Home town hall",
        "Space pause  F1-F3 speed  F5 quicksave  G graphs  T trade  N newspaper  E services  J jump to the latest news",
        "F4 this handbook  Right-click a building: what it needs  Esc closes panels, then the game menu" };
    char a[200], b[200], c[32], d[32];
    int sw = gfx_screen_w(), sh = gfx_screen_h(), W, H, X, Y, y, yl, yr, cw, ts, k, held = sim_stats()->town_status, ybot;
    if (!s_guide) return;
    gfx_set_target(NULL);
    W = IMIN(620, sw - 16); H = IMIN(sh - 40, 430);
    X = (sw - W) / 2; Y = (sh - H) / 2;
    uix_fill(X + 4, Y + 4, X + W + 4, Y + H + 4, col(24, 24, 24));
    uix_fill(X, Y, X + W, Y + H, col(C_PAPER));
    uix_fill(X, Y, X + W, Y + 1, col(C_RULE)); uix_fill(X, Y + H - 1, X + W, Y + H, col(C_RULE));
    uix_fill(X, Y, X + 1, Y + H, col(C_RULE)); uix_fill(X + W - 1, Y, X + W, Y + H, col(C_RULE));
    y = Y + 8;
    uix_text_c(X, W, y, "THE PRESIDENT'S HANDBOOK", FONT_HEAD); uix_text_c(X + 1, W, y, "THE PRESIDENT'S HANDBOOK", FONT_HEAD);
    y += 14;
    uix_text_c(X, W, y, "Issued by the Cabinet. For the President's eyes only; the President, naturally, knows all this already.", FONT_BODY_GREY);
    y += 12; hrule(X + 10, y, W - 20); y += 8;
    if (s_guide_page) {     /* 1.1 D1: "Presidency: Normal" - the level's table (DESIGN_1_1 2.1), every lever named */
        char ln[20][100];
        int n = hard_level_lines(ln, 20), i;
        str_printf(a, sizeof(a), "THE PRESIDENCY: %s", diff()->name);
        for (k = 0; a[k]; k++) if (a[k] >= 'a' && a[k] <= 'z') a[k] = (char)(a[k] - 32);
        uix_text(X + 14, y, a, FONT_HEAD); y += 15;
        uix_text_fit(X + 14, y, "Chosen at New Game and fixed for the term. The numbers in brackets are Easy's (the 1.0 rules).",
                     FONT_BODY_GREY, W - 28);
        y += 14;
        for (i = 1; i < n && y + 11 <= Y + H - 30; i++, y += 12) {
            str_printf(b, sizeof(b), "%s %s", GLYPH_BULLET, ln[i]);
            uix_text_fit(X + 14, y, b, FONT_BODY_DARK, W - 28);
        }
        uix_text_c(X, W, Y + H - 14, "Tab: back to the handbook  " TEXT_DOT "  Click or Esc to put it down", FONT_BODY_GREY);
        return;
    }
    ybot = Y + H - 18 - 4 * 11 - 10;
    cw = (W - 28 - 20) / 2;
    /* ---- left: the ladder */
    yl = y;
    uix_text(X + 14, yl, "THE LADDER", FONT_HEAD); yl += 15;
    for (ts = TS_CHURCH_VILLAGE; ts < TS_COUNT && yl + 22 <= ybot; ts++) {
        char ln[3][160];
        int n, i;
        text_fmt_int(c, sizeof(c), g_statuses[ts].population);
        str_printf(a, sizeof(a), "%s %s, %s residents", ts <= held ? GLYPH_CHECK : GLYPH_BULLET, text_status_name(ts), c);
        uix_text(X + 14, yl, a, ts == held + 1 ? FONT_BODY_RED : FONT_BODY_DARK);
        yl += 11;
        if (advice_status_unlocks(ts, b, sizeof(b)) <= 0) str_copy(b, "Prestige, and a larger flag", sizeof(b));
        n = wrap_wide(b, FONT_BODY_GREY, cw - 12, ln, 3);
        for (i = 0; i < n && yl + 11 <= ybot; i++, yl += 11) uix_text(X + 26, yl, ln[i], FONT_BODY_GREY);
        yl += 2;
    }
    k = advice_progress(a, sizeof(a), NULL);
    if (yl + 24 <= ybot) {
        uix_text_fit(X + 14, yl + 2, a, advice_rights_waiting() ? FONT_BODY_RED : FONT_BODY_DARK, cw);
        uix_bar(X + 14, yl + 14, cw - 4, 6, k, 0, col(74, 105, 74));
        yl += 24;
    }
    if (politics_enabled() && yl + 22 <= ybot)
        yl = guide_par(X + 14, yl, cw, ybot, NULL, "With politics on, each status needs the town-rights decree: \xa7 on Approval, 2 authority.");
    vrule(X + 14 + cw + 9, y, ybot);
    /* ---- right: what each system is for */
    yr = y;
    k = X + 14 + cw + 20;
    uix_text(k, yr, "WHAT EACH SYSTEM IS FOR", FONT_HEAD); yr += 15;
    yr = guide_par(k, yr, cw, ybot, "Power (P, overlay 1)", "Plants feed every building their lines and neighbours touch. Watch Power % on the top bar: past 100 the furthest go dark.");
    yr = guide_par(k, yr, cw, ybot, "Water and pipes (W, overlay 2)", "Pumps (need power) and wells feed the pipes; buildings and zoned land pass water on, roads need a pipe across.");
    str_copy(d, "", sizeof(d));
    str_copy(b, advice_purpose(B_HEAT_PLANT), sizeof(b));
    if (b[0] >= 'A' && b[0] <= 'Z') b[0] = (char)(b[0] + 32);
    str_printf(a, sizeof(a), "%s. The heat plant %s.", advice_purpose(B_WATER_TOWER), b);
    yr = guide_par(k, yr, cw, ybot, "Towers and district heat", a);
    str_printf(b, sizeof(b), "%s. Sewage works keep the lakes clean.", advice_purpose(B_ELEMENT_FACTORY));
    yr = guide_par(k, yr, cw, ybot, "Element factory, sewage", b);
    yr = guide_par(k, yr, cw, ybot, "Services (S; loads with E)", "Schools and saunas let houses reach tier 2; clinics, shops and telephones tier 3. Each has a capacity.");
    yr = guide_par(k, yr, cw, ybot, "The President's desk", politics_enabled() ? "Click Approval: decrees (\xa7 town rights), letters, the cards that wait. Authority is the currency."
                                                                            : "Politics is off in this town: the ministries decide, and town rights come by themselves.");
    yr = guide_par(k, yr, cw, ybot, "Trade (T) and the newspaper (N)", "The East's five-year clearing agreements fill the factories. Each January the paper's Cabinet column says what is wrong.");
    UNUSED(yr); UNUSED(d);
    /* ---- keys */
    y = ybot + 4;
    hrule(X + 10, y, W - 20); y += 6;
    for (k = 0; k < 4; k++, y += 11) uix_text_fit(X + 14, y, keys[k], FONT_BODY_DARK, W - 28);
    str_printf(a, sizeof(a), "Tab: the Presidency (%s)  " TEXT_DOT "  Click or press Esc to put the handbook down", diff()->name);   /* 1.1 D1 */
    uix_text_c(X, W, Y + H - 14, a, FONT_BODY_GREY);
}
