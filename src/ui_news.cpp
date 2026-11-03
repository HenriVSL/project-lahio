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

#define C_PAPER   236, 228, 204
#define C_INK      41,  36,  32
#define C_RULE     90,  85,  82
#define C_RED     156,  56,  41
#define C_SHADE   213, 198, 172

static int s_open, s_kind, s_ids_ok;
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
    s_open = 1;
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
    ui_ticker_push(SEV_INFO, buf, -1, -1);
    era_news_seen();
}

int uinews_input(const InputState *in)
{
    if (!s_open) return 0;
    if (in->pressed & (MOUSE_L | MOUSE_R)) uinews_close();
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
    char lines[16][96];
    int n = uix_wrap(s, font, IMIN(w, 370), lines, 16), i;   /* lines hold 95 characters: wrap at <= 370 px */
    for (i = 0; i < n && y + 11 <= ybot; i++, y += 11) uix_text(x, y, lines[i], font);
    return y;
}

static int story(int x, int y, int w, int ybot, int k, int big)
{
    const NewsIssue *n = &s_issue;
    char lines[4][96];
    int i, nl;
    if (!n->head[k][0]) return y;
    nl = uix_wrap(n->head[k], FONT_HEAD, IMIN(w, 370), lines, 3);
    for (i = 0; i < nl; i++) {
        if (big) {   /* the main headline: drawn twice, 1 px apart, for a bolder face */
            uix_text_c(x, w, y, lines[i], FONT_HEAD);
            uix_text_c(x + 1, w, y, lines[i], FONT_HEAD);
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
    uix_text_c(X, W, y, s_kind == NEWS_CEREMONY ? "SPECIAL EDITION" : s_kind == NEWS_NEWSREEL ? "THE NEWSREEL" : a, FONT_BODY_DARK);
    uix_text_r(X + W - 12, y, s_kind == NEWS_YEAR ? "Price 15 p" : a, FONT_BODY_DARK);
    y += 12; hrule(X + 10, y, W - 20); y += 8;
    yb = Y + H - 18;
    if (s_kind == NEWS_NEWSREEL) {
        uix_text_c(X + 14, W - 28, y, n->head[0], FONT_HEAD); uix_text_c(X + 15, W - 28, y, n->head[0], FONT_HEAD); y += 14;
        uix_text_c(X + 14, W - 28, y, n->deck[0], FONT_BODY_RED); y += 15;   /* the lines below carry the story */
        pop_curve(X + 14, y, W - 28, 80); y += 88;
        for (k = 0; k < s_nreel && y + 11 <= yb; k++, y += 11) uix_text_fit(X + 18, y, s_reel[k], FONT_BODY_DARK, W - 36);
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
        story(X + 14, mid, cw, yb, 1, 0);
        vrule(X + 14 + cw + 9, mid, yb);
        story(X + 14 + cw + 20, mid, cw, yb, 2, 0);
        vrule(X + 14 + 2 * cw + 29, mid, yb);
        uix_text(X + 14 + 2 * (cw + 20), mid, n->filler_head, FONT_HEAD);
        column(X + 14 + 2 * (cw + 20), mid + 14, cw, yb, n->filler, FONT_BODY_DARK);
        if (s_kind == NEWS_CEREMONY && s_decree) uix_sprite(s_decree, X + W - 150, yb - 36);
    }
    uix_text_c(X, W, Y + H - 14, "Click or press Esc to put the paper down", FONT_BODY_GREY);
}
