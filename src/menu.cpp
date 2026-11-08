/* menu.cpp - title screen, New Game panel, load/save slot lists, settings, pause menu. WINDOWS SIDE.
   Owner: T10. Implements menu.h; DESIGN 2, 8.4; art pieces and conventions: art/UI.md (T03), widget look
   copied from ui.cpp (T09) so menus and in-game panels match.

   Look: the game's own frame. Title / New Game / Load-from-title draw the world through the renderer in
   the map rect (title: a test town slowly panning; New Game: the actual map being previewed), the teak
   side panel with brass plate, grille and the President's plaque, the dark top bar and the ticker, and a
   bakelite window with paper interior in the middle of the map. The pause menu and its sub-screens draw
   over the frozen game frame (game.cpp draws it first; menu_draws_world() == 0) with a 50% dim.

   Drawing is immediate mode straight to the back buffer every frame (menus only: the sim is not running,
   ~150 blits + ~400 glyphs ~ 3 ms on the P2-233), no VRAM caches. Three small art images are made at
   menu_init: the 4x "LAHIO" (with umlauts) title (head font scaled in software; R1 rename, was "KAUPUNKI"), the New Game minimap preview and
   the load-screen thumbnail (both 128x128).

   Widgets: one function per screen lays out AND draws (s_drawing) the same widgets, so menu_update
   (layout pass, hit tests) and menu_draw (drawing pass) can never disagree about rectangles. */
#include "common.h"
#include "menu.h"
#include "ui.h"
#include "gfx.h"
#include "art.h"
#include "render.h"
#include "iso.h"
#include "text.h"
#include "save.h"
#include "config.h"
#include "snd.h"
#include "mapgen.h"
#include "world.h"
#include "sim.h"
#include "budget.h"
#include "net.h"
#include "fields.h"
#include "traffic.h"
#include "util.h"
#include "ui_layout.h"
#include <time.h>

/* ================================================================== constants */
#define TITLE_WORD    "L\xC4HI\xD6"   /* R1: logo word (Latin-1 umlauts); "PROJECT" is the stamp_project art above it */
#define TITLE_SCALE   4
#define TITLE_SPACING 1               /* extra px between the head-font glyphs before scaling */
enum { SC_TITLE, SC_NEWGAME, SC_LOAD, SC_SAVE, SC_SETTINGS, SC_PAUSE, SC_COUNT };
static const char *const s_sc_name[SC_COUNT] = { "title", "new game", "load", "save", "settings", "pause" };

enum { WK_BUTTON, WK_CHECK, WK_RADIO, WK_FIELD, WK_ROW, WK_SLIDER, WK_ARROW, WK_CLOSE };
enum { BS_NORMAL, BS_HOVER, BS_PRESSED, BS_DISABLED, BS_SELECTED, BS_COUNT };
static const char *const s_bs_name[BS_COUNT] = { "normal", "hover", "pressed", "disabled", "selected" };

/* widget ids (one space for all screens) */
enum {
    W_NONE,
    /* title */
    W_T_NEW, W_T_LOAD, W_T_SETTINGS, W_T_QUIT,
    /* new game */
    W_N_PRES, W_N_TOWN, W_N_TOWN_REROLL, W_N_PRESET_L, W_N_PRESET_R, W_N_SEED, W_N_SEED_REROLL,
    W_N_YEAR0, W_N_YEAR3 = W_N_YEAR0 + 3, W_N_FUNDS0, W_N_FUNDS2 = W_N_FUNDS0 + 2,
    W_N_POLITICS, W_N_DISASTERS, W_N_ALLTOOLS, W_N_RAILWAY, W_N_BACK, W_N_START, W_N_PIPES /* F1 */,
    W_N_DIFF0, W_N_DIFF2 = W_N_DIFF0 + 2,      /* 1.1 D1: Easy / Normal / Hard */
    /* load / save */
    W_L_ROW0, W_L_ROW_LAST = W_L_ROW0 + SAVE_LIST_COUNT - 1, W_L_BACK, W_L_GO,
    /* settings */
    W_S_RES0, W_S_RES2 = W_S_RES0 + 2, W_S_FULL, W_S_WINDOW, W_S_FPS60, W_S_FPS30, W_S_FPSAUTO,
    W_S_EDGE, W_S_SPEED, W_S_SFX, W_S_AMB, W_S_TICK0, W_S_TICK2 = W_S_TICK0 + 2, W_S_BACK,
    /* pause */
    W_P_RESUME, W_P_SAVE, W_P_LOAD, W_P_SETTINGS, W_P_TITLE, W_P_QUIT,
    /* dialog */
    W_D_YES, W_D_NO,
    W_CLOSE,
    W_COUNT };

#define MAXW 96
typedef struct { s16 x, y, w, h; u8 kind, disabled; s16 id; s32 vmin, vmax; } Widget;

/* dialog actions */
enum { DA_NONE, DA_OVERWRITE, DA_QUIT, DA_TITLE, DA_LOAD_INGAME };

/* ================================================================== art (pieces per art/UI.md) */
typedef struct { int p[9]; int l, t, r, b; } Slice9;
typedef struct { int p[3]; int a, b; } Slice3;
static Slice9 s9_win, s9_btn[BS_COUNT], s9_inset, s9_mmap, s9_plaque;
static Slice3 s3_plate, s3_grille, s3_track, s3_fill, s3_flysel, s3_flyhover;
static struct {
    int teak, panel_edge, bar_tile, ticker_tile, badge, bar_sep, lamp_info, rule, seal, stamp_project;
    int pres_frame_l, pres_frame_s, pres_l[4], pres_s[4];
    int check[2][3], radio[2][3];          /* off/on x normal hover disabled */
    int arrow_l[3], arrow_r[3];            /* normal pressed disabled */
    int close[3], knob[3];
} A;

/* ================================================================== state */
static int s_inited, s_screen, s_back_screen, s_ingame, s_drawing, s_result;
static int s_sw, s_sh;
static const UiLayout *L;
static Widget s_w[MAXW];
static int s_nw, s_dlg_first;
static int s_mx, s_my, s_down, s_hot, s_press, s_focus, s_drag;
static double s_now, s_caret_t;
static NewGameParams s_ng;
static char s_path[PATH_LEN];
static char s_seed_txt[16];
static Rng s_name_rng;
static int s_preview_pending, s_preview_ok, s_backdrop;   /* backdrop: 0 none, 1 title town, 2 preview map */
static double s_pan_acc;
static int s_pan_dir = 1;
static double s_pan_t;
static int s_settings_dirty;

/* load/save list */
static SaveMeta s_meta[SAVE_LIST_COUNT];
static int s_have[SAVE_LIST_COUNT];
static int s_sel = -1, s_thumb_sel = -2;
static double s_last_click_t;
static int s_last_click_row = -1;

/* dialog */
static struct { int on, yesno, action; char title[80], detail[200]; } s_dlg;

/* images */
static Img s_title_img, s_mm_img, s_th_img;
static int s_title_ok, s_mm_ok, s_th_ok;

/* ticker headlines for the title backdrop (period flavour) */
static const char *const s_heads[] = {
    "Rural families keep arriving: the towns must build homes",
    "Reconstruction: the veterans' houses rise on new streets",
    "The President opens the new pumping station",
    "Sawmills report a record export year",
    "Concrete element factory planned for the province",
    "Sauna co-operative asks the town for a site",
};

/* ================================================================== helpers */
static const ArtSprite *SP(int id) { return art_get(ZOOM_64, id); }
static int spr_w(int id) { return SP(id)->w; }
static int spr_h(int id) { return SP(id)->h; }
static PIX col(int r, int g, int b) { return gfx_rgb(r, g, b); }
#define COL(c) col(c)

static void spr_part(int id, int sx, int sy, int w, int h, int x, int y)
{
    const ArtSprite *s = SP(id);
    if (!s->img || w <= 0 || h <= 0) return;
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    if (w <= 0 || h <= 0) return;
    gfx_blit_part(s->img, s->x + sx, s->y + sy, w, h, x, y);
}
static void spr(int id, int x, int y) { const ArtSprite *s = SP(id); spr_part(id, 0, 0, s->w, s->h, x, y); }
static void spr_tile(int id, int x, int y, int w, int h)
{
    const ArtSprite *s = SP(id);
    int xx, yy;
    if (s->w <= 0 || s->h <= 0) return;
    for (yy = 0; yy < h; yy += s->h)
        for (xx = 0; xx < w; xx += s->w)
            spr_part(id, 0, 0, IMIN(s->w, w - xx), IMIN(s->h, h - yy), x + xx, y + yy);
}

/* 9-/3-slices: copied from ui.cpp (T09); ui.h exports no frame helpers */
static void slice9_init(Slice9 *s, const char *base)
{
    static const char *const suf[9] = { "tl", "t", "tr", "l", "c", "r", "bl", "b", "br" };
    int i;
    for (i = 0; i < 9; i++) s->p[i] = art_findf("%s_%s", base, suf[i]);
    s->l = spr_w(s->p[0]); s->t = spr_h(s->p[0]); s->r = spr_w(s->p[8]); s->b = spr_h(s->p[8]);
}
static void slice3_init(Slice3 *s, const char *base)
{
    s->p[0] = art_findf("%s_l", base); s->p[1] = art_findf("%s_m", base); s->p[2] = art_findf("%s_r", base);
    s->a = spr_w(s->p[0]); s->b = spr_w(s->p[2]);
}
static void draw9(const Slice9 *s, int x, int y, int w, int h, int fillc)
{
    int cw = w - s->l - s->r, ch = h - s->t - s->b;
    if (cw > 0 && ch > 0) {
        if (fillc >= 0) gfx_fill(x + s->l, y + s->t, x + s->l + cw, y + s->t + ch, (PIX)fillc);
        else spr_tile(s->p[4], x + s->l, y + s->t, cw, ch);
    }
    if (cw > 0) { spr_tile(s->p[1], x + s->l, y, cw, s->t); spr_tile(s->p[7], x + s->l, y + h - s->b, cw, s->b); }
    if (ch > 0) { spr_tile(s->p[3], x, y + s->t, s->l, ch); spr_tile(s->p[5], x + w - s->r, y + s->t, s->r, ch); }
    spr(s->p[0], x, y); spr(s->p[2], x + w - s->r, y);
    spr(s->p[6], x, y + h - s->b); spr(s->p[8], x + w - s->r, y + h - s->b);
}
static void draw3h(const Slice3 *s, int x, int y, int w)
{
    int h = spr_h(s->p[1]);
    if (w <= 0) return;
    if (w < s->a + s->b) { spr_part(s->p[0], 0, 0, w, h, x, y); return; }
    spr(s->p[0], x, y);
    spr_tile(s->p[1], x + s->a, y, w - s->a - s->b, h);
    spr(s->p[2], x + w - s->b, y);
}

static int txt(int x, int y, const char *s, int font) { return ui_text(x, y, s, font); }
static int txtw(const char *s, int font) { return ui_text_width(s, font); }
static void txt_c(int x, int w, int y, const char *s, int font) { txt(x + (w - txtw(s, font)) / 2, y, s, font); }
static void txt_r(int xr, int y, const char *s, int font) { txt(xr - txtw(s, font), y, s, font); }
/* letter-spaced text (title lettering): sp extra px between glyphs */
static int txtw_sp(const char *s, int font, int sp)
{
    char c[2];
    int w = 0;
    c[1] = 0;
    for (; *s; s++) { c[0] = *s; w += txtw(c, font) + (s[1] ? sp : 0); }
    return w;
}
static void txt_sp(int x, int y, const char *s, int font, int sp)
{
    char c[2];
    c[1] = 0;
    for (; *s; s++) { c[0] = *s; txt(x, y, c, font); x += txtw(c, font) + sp; }
}

static int in_rect(int px, int py, int x, int y, int w, int h) { return px >= x && py >= y && px < x + w && py < y + h; }

static void click_snd(void) { snd_play(SND_CLICK, 100); }

/* word wrap a short text into max 3 lines of width w */
static int wrap3(const char *s, int font, int w, char out[3][100])
{
    char line[100], t[200];
    int n = 0, i = 0, k;
    line[0] = 0;
    for (;;) {
        char word[100];
        while (s[i] == ' ') i++;
        if (!s[i]) break;
        for (k = 0; s[i] && s[i] != ' ' && k < 98; k++, i++) word[k] = s[i];
        word[k] = 0;
        if (line[0]) {
            str_printf(t, sizeof(t), "%s %s", line, word);
            if (txtw(t, font) <= w) { str_copy(line, t, sizeof(line)); continue; }
            if (n < 3) str_copy(out[n], line, 100);
            n++;
        }
        str_copy(line, word, sizeof(line));
    }
    if (line[0]) { if (n < 3) str_copy(out[n], line, 100); n++; }
    return IMIN(n, 3);
}

/* ================================================================== widgets */
static int w_add(int id, int kind, int x, int y, int w, int h, int disabled)
{
    Widget *wd;
    if (s_nw >= MAXW) return -1;
    wd = &s_w[s_nw];
    wd->x = (s16)x; wd->y = (s16)y; wd->w = (s16)w; wd->h = (s16)h; wd->kind = (u8)kind;
    wd->disabled = (u8)disabled; wd->id = (s16)id; wd->vmin = 0; wd->vmax = 0;
    return s_nw++;
}
static int w_state(int id, int disabled)
{
    if (disabled) return BS_DISABLED;
    if (s_hot == id) return (s_down && s_press == id) ? BS_PRESSED : BS_HOVER;
    return BS_NORMAL;
}

static void w_button(int id, int x, int y, int w, int h, const char *label, int disabled)
{
    int st, o;
    w_add(id, WK_BUTTON, x, y, w, h, disabled);
    if (!s_drawing) return;
    st = w_state(id, disabled);
    o = st == BS_PRESSED;
    draw9(&s9_btn[st], x, y, w, h, -1);
    txt_c(x + o, w - 1, y + (h - 11) / 2 + o, label, disabled ? FONT_BODY_GREY : FONT_BODY_DARK);
}

static void w_check(int id, int x, int y, int on, const char *label, int disabled)
{
    int st;
    w_add(id, WK_CHECK, x, y, 16 + txtw(label, FONT_BODY_DARK), 11, disabled);
    if (!s_drawing) return;
    st = disabled ? 2 : s_hot == id ? 1 : 0;
    spr(A.check[on ? 1 : 0][st], x, y);
    txt(x + 16, y, label, disabled ? FONT_BODY_GREY : FONT_BODY_DARK);
}

static int w_radio(int id, int x, int y, int on, const char *label)
{
    int w = 16 + txtw(label, FONT_BODY_DARK);
    w_add(id, WK_RADIO, x, y, w, 11, 0);
    if (s_drawing) {
        spr(A.radio[on ? 1 : 0][s_hot == id ? 1 : 0], x, y);
        txt(x + 16, y, label, FONT_BODY_DARK);
    }
    return w;
}

static void w_field(int id, int x, int y, int w, const char *text)
{
    w_add(id, WK_FIELD, x, y, w, 15, 0);
    if (!s_drawing) return;
    draw9(&s9_inset, x, y, w, 15, s_focus == id ? COL(UI_COL_PAPER_LT) : COL(UI_COL_PAPER_DK));
    {
        int tw = txt(x + 4, y + 2, text, FONT_BODY_DARK);
        if (s_focus == id && ((int)(s_caret_t * 2.0) & 1) == 0) gfx_fill(x + 5 + tw, y + 3, x + 6 + tw, y + 12, COL(UI_COL_INK));
    }
}

static void w_arrow(int id, int x, int y, int left)
{
    int st;
    w_add(id, WK_ARROW, x, y, 11, 11, 0);
    if (!s_drawing) return;
    st = (s_hot == id && s_down && s_press == id) ? 1 : 0;
    spr(left ? A.arrow_l[st] : A.arrow_r[st], x, y);
}

static void w_slider(int id, int x, int y, int w, int v, int vmin, int vmax)
{
    int i = w_add(id, WK_SLIDER, x - 4, y - 3, w + 8, 13, 0), fw, st;
    if (i >= 0) { s_w[i].vmin = vmin; s_w[i].vmax = vmax; }
    if (!s_drawing) return;
    fw = (v - vmin) * w / IMAX(1, vmax - vmin);
    draw3h(&s3_track, x, y, w);
    if (fw > 0) draw3h(&s3_fill, x, y, fw);
    st = s_drag == id ? 2 : s_hot == id ? 1 : 0;
    spr(A.knob[st], x + fw - 4, y - 3);
}

/* list row: highlight only; caller draws the content */
static void w_row(int id, int x, int y, int w, int h, int selected)
{
    w_add(id, WK_ROW, x, y, w, h, 0);
    if (!s_drawing) return;
    if (selected) gfx_fill(x, y, x + w, y + h, COL(UI_COL_LIT));
    else if (s_hot == id) gfx_fill(x, y, x + w, y + h, COL(UI_COL_PAPER_LT));
}

/* bakelite window with paper interior; returns the interior rect */
static void win(int x, int y, int w, int h, const char *title, int close_id, int *ix, int *iy, int *iw, int *ih)
{
    if (s_drawing) {
        draw9(&s9_win, x, y, w, h, COL(UI_COL_PAPER));
        txt(x + 8, y + 3, title, FONT_HEAD_LIGHT);
    }
    if (close_id) {
        w_add(close_id, WK_CLOSE, x + w - 19, y + 3, 13, 13, 0);
        if (s_drawing) spr(A.close[s_hot == close_id ? (s_down ? 2 : 1) : 0], x + w - 19, y + 3);
    }
    *ix = x + 6; *iy = y + 19; *iw = w - 12; *ih = h - 26;
}

static void win_centre(int w, int h, int *x, int *y)
{
    *x = L->map.x + (L->map.w - w) / 2;
    *y = L->map.y + (L->map.h - h) / 2;
    if (*y < L->map.y + 2) *y = L->map.y + 2;
}

static void rule(int x, int y, int w) { if (s_drawing) spr_tile(A.rule, x, y, w, 2); }

/* ================================================================== images */
/* R1 title lettering: "LAHIO" (A/O with umlauts) in the head font, letter-spaced, software-scaled S x. The font's umlaut dots are
   2x1 on line row 0 with a 1 px gap above the caps; here they grow upwards to 2x2 squares (2 rows of headroom),
   and the image is cropped to the ink rows, so at 4x: 8x8 dots, 4 px gap, 36 px caps. */
static void make_title_image(void)
{
    Img tmp;
    int tw, th, x, y, n = 0, S = TITLE_SCALE, HR = 2, top = -1, bot = -1, h;
    PIX key;
    memset(&tmp, 0, sizeof(tmp));
    memset(&s_title_img, 0, sizeof(s_title_img));
    tw = txtw_sp(TITLE_WORD, FONT_HEAD, TITLE_SPACING) + 2;
    th = ui_font_height(FONT_HEAD) + HR;
    if (tw <= 2 || !gfx_make_img(tw, th, 1, &tmp)) return;
    if (!tmp.px) { gfx_free_img(&tmp); return; }
    gfx_set_target(&tmp);
    txt_sp(1, HR, TITLE_WORD, FONT_HEAD, TITLE_SPACING);
    gfx_set_target(NULL);
    key = gfx_key();
    for (x = 0; x < tw; x++)                       /* dot on line row 0, gap below -> square it upwards */
        if (tmp.px[HR * tw + x] != key && tmp.px[(HR + 1) * tw + x] == key) tmp.px[(HR - 1) * tw + x] = tmp.px[HR * tw + x];
    for (y = 0; y < th; y++)
        for (x = 0; x < tw; x++)
            if (tmp.px[y * tw + x] != key) { n++; if (top < 0) top = y; bot = y; }
    h = bot - top + 1;
    if (n && gfx_make_img(tw * S, h * S, 1, &s_title_img) && s_title_img.px) {
        for (y = 0; y < h * S; y++)
            for (x = 0; x < tw * S; x++) s_title_img.px[y * tw * S + x] = tmp.px[(top + y / S) * tw + x / S];
        gfx_img_update(&s_title_img);
        s_title_ok = 1;
    }
    gfx_free_img(&tmp);
    log_printf("menu: title image %dx%d %s (%d ink px)", tw * S, h * S, s_title_ok ? "ok" : "failed", n);
}

/* F3: the 128x128 preview / thumbnail images are made on first use (from the VRAM reserve render_init leaves),
   not at menu_init: made before render_init they cost the chunk pool one 64 KB chunk at every start */
static int menu_img(Img *im, int *ok, int keyed)
{
    if (!*ok) {
        *ok = gfx_make_img(128, 128, keyed, im) && im->px;
        if (!*ok) log_printf("menu: warning: no VRAM for a 128x128 preview image (%d KB free)", gfx_vram_free_kb());
    }
    return *ok;
}

static void build_minimap(void)
{
    if (!menu_img(&s_mm_img, &s_mm_ok, 0)) return;
    render_minimap(s_mm_img.px, s_mm_img.w, s_mm_img.w, s_mm_img.h, OVL_NONE);
    gfx_img_update(&s_mm_img);
}

/* thumbnail classes -> colours (muted, like the art) */
static PIX thumb_col(int c)
{
    switch (c) {
    case THUMB_GRASS:      return col(122, 134, 90);
    case THUMB_FOREST:     return col(66, 88, 60);
    case THUMB_ROCK:       return col(132, 128, 118);
    case THUMB_FIELD:      return col(170, 156, 104);
    case THUMB_WATER:      return col(82, 110, 128);
    case THUMB_ROAD:       return col(62, 60, 56);
    case THUMB_RAIL:       return col(96, 74, 58);
    case THUMB_HOUSES:     return col(160, 96, 74);
    case THUMB_APARTMENTS: return col(196, 190, 176);
    case THUMB_COMMERCIAL: return col(96, 120, 160);
    case THUMB_INDUSTRY:   return col(180, 150, 82);
    case THUMB_CIVIC:      return col(230, 222, 197);
    case THUMB_ZONED:      return col(150, 156, 112);
    default:               return col(213, 198, 172);
    }
}

static void build_thumb(int idx)
{
    static u8 th[SAVE_THUMB * SAVE_THUMB];
    int px, py, W, H, ok;
    PIX key;
    if (!menu_img(&s_th_img, &s_th_ok, 1)) return;
    s_thumb_sel = idx;
    W = s_th_img.w; H = s_th_img.h;
    key = gfx_key();
    ok = 0;
    if (idx >= 0 && s_have[idx]) {
        char p[PATH_LEN];
        save_any_path(idx, p, sizeof(p));
        ok = save_read_thumb(p, th);
    }
    for (py = 0; py < H; py++)
        for (px = 0; px < W; px++) {
            /* 45-degree diamond like the minimap: tile x runs down-right, y down-left */
            int fx = (2 * px - W) * SAVE_THUMB / (2 * W) + py * SAVE_THUMB / H;
            int fy = py * SAVE_THUMB / H - (2 * px - W) * SAVE_THUMB / (2 * W);
            PIX c = key;
            if (ok && fx >= 0 && fy >= 0 && fx < SAVE_THUMB && fy < SAVE_THUMB) c = thumb_col(th[fy * SAVE_THUMB + fx]);
            s_th_img.px[py * W + px] = c;
        }
    gfx_img_update(&s_th_img);
}

/* ================================================================== backdrop (renderer) */
static void backdrop_title(void)
{
    render_build_test_town(g_cfg.seed);
    render_reset();
    cam_center_on_tile(60, 66);
    s_backdrop = 1;
    s_pan_t = 0; s_pan_dir = 1; s_pan_acc = 0;
}

static void gen_preview(void)
{
    double t0 = time_ms();
    s_ng.map.seed = (u32)strtoul(s_seed_txt, NULL, 10);
    if (!s_ng.map.seed) s_ng.map.seed = 1;
    s_ng.sim.seed = s_ng.map.seed;
    s_preview_ok = mapgen_generate(&s_ng.map);
    render_reset();
    {
        const MapGenReport *r = mapgen_report();
        if (r->town_x > 0) cam_center_on_tile(r->town_x, r->town_y); else cam_center_on_tile(g_world.w / 2, g_world.h / 2);
    }
    build_minimap();
    s_backdrop = 2;
    s_preview_pending = 0;
    log_printf("menu: preview %s seed %u: %s in %.0f ms", mapgen_preset_name(s_ng.map.preset), s_ng.map.seed,
               s_preview_ok ? "ok" : "failed", time_ms() - t0);
}

static void draw_frame_decor(void)
{
    char buf[96];
    int x, y, i, by = -1;
    /* top bar */
    spr_tile(A.bar_tile, L->top.x, L->top.y, L->top.w, L->top.h);
    txt(L->top.x + 8, L->top.y + 3, GAME_TITLE " " TEXT_DOT " Version " GAME_VERSION, FONT_BODY_BRASS);
    str_printf(buf, sizeof(buf), "A town-builder of the Finnish reconstruction");
    txt_r(L->top.x + L->top.w - 8, L->top.y + 3, buf, FONT_BODY_LIGHT);
    /* teak panel */
    spr_tile(A.teak, L->panel.x, L->panel.y, L->panel.w, L->panel.h);
    spr_tile(A.panel_edge, L->panel.x, L->panel.y, 3, L->panel.h);
    draw3h(&s3_plate, L->plate.x, L->plate.y, L->plate.w);
    txt_c(L->plate.x, L->plate.w, L->plate.y + 1, s_screen == SC_NEWGAME ? s_ng.sim.town : TITLE_WORD, FONT_HEAD);
    /* plaque with the President */
    x = L->panel.x + 6; y = L->plate.y + L->plate.h + 10;
    {
        int pw = L->panel.w - 12, ph = 120, large = L->panel.w >= 140;
        int fr = large ? A.pres_frame_l : A.pres_frame_s, pr = large ? A.pres_l[0] : A.pres_s[0];
        draw9(&s9_plaque, x, y, pw, ph, -1);
        spr(fr, x + (pw - spr_w(fr)) / 2, y + 6);
        spr(pr, x + (pw - spr_w(fr)) / 2 + 2, y + 8);
        txt_c(x, pw, y + 10 + spr_h(fr), "President", FONT_BODY_DARK);
        txt_c(x, pw, y + 21 + spr_h(fr), s_screen == SC_NEWGAME ? s_ng.sim.president : g_cfg.president, FONT_BODY_DARK);
        y += ph + 8;
    }
    /* grille slots + badge */
    for (i = 0; y + 5 < L->panel.y + L->panel.h - 8 && i < 40; i++, y += 8) {
        if (i == 4) by = y;
        else draw3h(&s3_grille, L->panel.x + 6, y, L->panel.w - 12);
    }
    if (by >= 0) spr(A.badge, L->panel.x + (L->panel.w - spr_w(A.badge)) / 2, by + 2 - spr_h(A.badge) / 2);
    /* ticker */
    spr_tile(A.ticker_tile, L->ticker.x, L->ticker.y, L->ticker.w, L->ticker.h);
    spr(A.lamp_info, L->ticker.x + 6, L->ticker.y + 4);
    txt(L->ticker.x + 18, L->ticker.y + 2, s_heads[((int)(s_now / 6.0)) % COUNTOF(s_heads)], FONT_BODY_LIGHT);
}

static void draw_backdrop(void)
{
    if (s_backdrop) {
        render_update(3.0);
        render_world();
        render_flush_sprites();
    } else gfx_fill(L->map.x, L->map.y, L->map.x + L->map.w, L->map.y + L->map.h, COL(UI_COL_TEAK_XDK));
    draw_frame_decor();
}

/* ================================================================== load/save list */
static void scan_slots(void)
{
    int i;
    char p[PATH_LEN];
    for (i = 0; i < SAVE_LIST_COUNT; i++) {
        save_any_path(i, p, sizeof(p));
        s_have[i] = save_read_meta(p, &s_meta[i]);
    }
    s_thumb_sel = -2;
}

static void slot_name(int i, char *buf, int size)
{
    if (i < SAVE_SLOTS) str_printf(buf, size, "Slot %d", i + 1);
    else if (i == SAVE_SLOTS) str_copy(buf, "Quicksave", size);
    else str_printf(buf, size, "Autosave %d", i - SAVE_SLOTS);
}

/* ================================================================== screens */
static void enter_screen(int sc)
{
    s_screen = sc;
    s_focus = W_NONE; s_drag = W_NONE; s_press = W_NONE;
    s_last_click_row = -1;
    if (sc == SC_LOAD || sc == SC_SAVE) {
        scan_slots();
        s_sel = -1;
        if (sc == SC_LOAD) { int i; for (i = 0; i < SAVE_LIST_COUNT; i++) if (s_have[i]) { s_sel = i; break; } }
    }
    if (sc == SC_NEWGAME) { s_focus = W_N_PRES; if (s_backdrop != 2) s_preview_pending = 1; }
    if (sc == SC_SETTINGS) s_settings_dirty = 0;
    log_printf("menu: screen %s%s", s_sc_name[sc], s_ingame ? " (in game)" : "");
}

static void dialog(int yesno, int action, const char *title, const char *detail)
{
    s_dlg.on = 1; s_dlg.yesno = yesno; s_dlg.action = action;
    str_copy(s_dlg.title, title, sizeof(s_dlg.title));
    str_copy(s_dlg.detail, detail ? detail : "", sizeof(s_dlg.detail));
    if (s_dlg.detail[0] >= 'a' && s_dlg.detail[0] <= 'z') s_dlg.detail[0] = (char)(s_dlg.detail[0] - 32);
    log_printf("menu: dialog \"%s\" %s", s_dlg.title, s_dlg.detail);
}

/* ---- title */
static void screen_title(void)
{
    int x, y, ix, iy, iw, ih, bx, by, W = 330, H = 292, fr = A.pres_frame_l, ty = 118;
    win_centre(W, H, &x, &y);
    win(x, y, W, H, "Welcome, President", 0, &ix, &iy, &iw, &ih);
    if (s_drawing) {
        /* R1 logo (docs/NAMING.md 3): red "PROJECT" rubber stamp, big letter-spaced LAHIO with square umlaut
           dots, the tagline as small spaced capitals in ink */
        spr(A.stamp_project, ix + (iw - spr_w(A.stamp_project)) / 2, iy + 6);
        if (s_title_ok) gfx_blit(&s_title_img, ix + (iw - s_title_img.w) / 2, iy + 36);
        else txt_c(ix, iw, iy + 48, TITLE_WORD, FONT_HEAD);
        txt_sp(ix + (iw - txtw_sp("A FINNISH TOWN, 1955-1990", FONT_BODY_DARK, 1)) / 2, iy + 92,
               "A FINNISH TOWN, 1955-1990", FONT_BODY_DARK, 1);
        rule(ix + 10, iy + ty - 14, iw - 20);
        spr(fr, ix + 14, iy + ty);
        spr(A.pres_l[2], ix + 16, iy + ty + 2);
        txt_c(ix + 14, spr_w(fr), iy + ty + 6 + spr_h(fr), g_cfg.president, FONT_BODY_DARK);
        txt(ix + 8, iy + ih - 13, "Version " GAME_VERSION, FONT_BODY_GREY);
        spr(A.seal, ix + iw - 24, iy + ih - 22);
    }
    bx = ix + 14 + spr_w(fr) + 22; by = iy + ty;
    w_button(W_T_NEW, bx, by, ix + iw - 14 - bx, 22, "New town", 0); by += 28;
    w_button(W_T_LOAD, bx, by, ix + iw - 14 - bx, 22, "Load town", 0); by += 28;
    w_button(W_T_SETTINGS, bx, by, ix + iw - 14 - bx, 22, "Settings", 0); by += 28;
    w_button(W_T_QUIT, bx, by, ix + iw - 14 - bx, 22, "Quit to Windows", 0);
}

/* ---- new game */
static const char *const s_funds_name[FUNDS_COUNT] = { "Generous", "Normal", "Austere" };
static const int s_years[4] = { 1955, 1960, 1965, 1970 };

static void screen_newgame(void)
{
    int x, y, ix, iy, iw, ih, lx, cx, yy, k, rx, W = 520, H = 378;   /* 1.1 D1: + the difficulty row (was 346) */
    char buf[64];
    win_centre(W, H, &x, &y);
    win(x, y, W, H, "Found a new town", W_CLOSE, &ix, &iy, &iw, &ih);
    lx = ix + 10; cx = ix + 96; yy = iy + 10;
    /* names */
    if (s_drawing) txt(lx, yy + 2, "President", FONT_BODY_DARK);
    w_field(W_N_PRES, cx, yy, 170, s_ng.sim.president);
    yy += 22;
    if (s_drawing) txt(lx, yy + 2, "Town", FONT_BODY_DARK);
    w_field(W_N_TOWN, cx, yy, 170, s_ng.sim.town);
    w_button(W_N_TOWN_REROLL, cx + 176, yy - 2, 60, 19, "Reroll", 0);
    yy += 26;
    rule(lx, yy, 326); yy += 8;
    /* map */
    if (s_drawing) txt(lx, yy + 2, "Landscape", FONT_BODY_DARK);
    w_arrow(W_N_PRESET_L, cx, yy + 2, 1);
    if (s_drawing) txt_c(cx + 12, 100, yy + 2, mapgen_preset_name(s_ng.map.preset), FONT_BODY_DARK);
    w_arrow(W_N_PRESET_R, cx + 113, yy + 2, 0);
    yy += 22;
    if (s_drawing) txt(lx, yy + 2, "Map seed", FONT_BODY_DARK);
    w_field(W_N_SEED, cx, yy, 110, s_seed_txt);
    w_button(W_N_SEED_REROLL, cx + 116, yy - 2, 60, 19, "Reroll", 0);
    yy += 22;
    w_check(W_N_RAILWAY, cx, yy + 1, s_ng.map.railway, "Railway along an edge", 0);
    yy += 22;
    rule(lx, yy, 326); yy += 8;
    /* year + funds */
    if (s_drawing) txt(lx, yy, "Start year", FONT_BODY_DARK);
    rx = cx;
    for (k = 0; k < 4; k++) {
        str_printf(buf, sizeof(buf), "%d", s_years[k]);
        rx += w_radio(W_N_YEAR0 + k, rx, yy, s_ng.sim.start_year == s_years[k], buf) + 10;
    }
    yy += 20;
    if (s_drawing) txt(lx, yy, "Treasury", FONT_BODY_DARK);
    rx = cx;
    for (k = 0; k < FUNDS_COUNT; k++) rx += w_radio(W_N_FUNDS0 + k, rx, yy, s_ng.sim.funds == k, s_funds_name[k]) + 10;
    yy += 14;
    if (s_drawing) {
        Money m = budget_start_funds(s_ng.sim.funds, s_ng.sim.start_year);
        char mb[32];
        text_fmt_money(mb, sizeof(mb), m, s_ng.sim.start_year);
        str_printf(buf, sizeof(buf), "Starting funds: %s", s_ng.sim.all_unlocked ? "unlimited" : mb);
        txt(cx, yy, buf, FONT_BODY_GREY);
    }
    yy += 18;
    /* 1.1 D1: difficulty, fixed for the game (DESIGN_1_1 2.1); the Guide (F4) lists every lever */
    if (s_drawing) txt(lx, yy, "Difficulty", FONT_BODY_DARK);
    rx = cx;
    for (k = 0; k < DIFF_COUNT; k++) rx += w_radio(W_N_DIFF0 + k, rx, yy, s_ng.sim.difficulty == k, diff_name(k)) + 10;
    yy += 14;
    if (s_drawing) {
        static const char *const dd[DIFF_COUNT] = { "The 1.0 rules: winter is scenery",
                                                    "Winter peaks, Sweden 1969-70, expectations",
                                                    "Dearer upkeep, faster decline, hard winters" };
        const char *t = dd[ICLAMP(s_ng.sim.difficulty, 0, DIFF_COUNT - 1)];
        if (ui_text_width(t, FONT_BODY_GREY) <= ix + iw - 10 - 128 - 14 - cx) txt(cx, yy, t, FONT_BODY_GREY);   /* clear of the preview */
    }
    yy += 18;
    rule(lx, yy, 326); yy += 8;
    /* toggles */
    w_check(W_N_POLITICS, cx, yy, s_ng.sim.politics, "Politics: blocs, decrees, elections", 0); yy += 17;   /* M1 */
    w_check(W_N_DISASTERS, cx, yy, s_ng.sim.disasters, "Disasters (fires)", 0); yy += 17;
    w_check(W_N_ALLTOOLS, cx, yy, s_ng.sim.all_unlocked, "All tools unlocked, unlimited money", 0); yy += 17;
    w_check(W_N_PIPES, cx, yy, g_cfg.pipes_with_roads, "Lay water pipes with new roads", 0); yy += 17;   /* F1 */
    /* preview on the right */
    {
        int px = ix + iw - 10 - 128 - 6, py = iy + 10;
        if (s_drawing) {
            const MapGenReport *r = mapgen_report();
            draw9(&s9_mmap, px - 3, py - 3, 134, 134, COL(UI_COL_BAR_DK));
            if (s_preview_pending) txt_c(px, 128, py + 58, "Surveying" "\x85", FONT_BODY_LIGHT);
            else if (s_mm_ok) gfx_blit(&s_mm_img, px, py);
            yy = py + 140;
            if (!s_preview_pending && s_preview_ok) {
                str_printf(buf, sizeof(buf), "Water %d%%  Forest %d%%", r->water_pct, r->forest_pct);
                txt(px - 2, yy, buf, FONT_BODY_DARK); yy += 12;
                str_printf(buf, sizeof(buf), "Buildable land %d%%", r->buildable_pct);
                txt(px - 2, yy, buf, FONT_BODY_DARK); yy += 12;
                str_printf(buf, sizeof(buf), "%s%s", r->river ? "River with rapids" : "No river", r->railway ? ", railway" : "");
                txt(px - 2, yy, buf, FONT_BODY_DARK); yy += 12;
                str_printf(buf, sizeof(buf), "National road, %d bridges", r->bridge_tiles);
                txt(px - 2, yy, buf, FONT_BODY_GREY);
            }
        }
    }
    w_button(W_N_BACK, ix + 10, iy + ih - 30, 90, 22, "Back", 0);
    w_button(W_N_START, ix + iw - 10 - 150, iy + ih - 30, 150, 22, "Found the town", s_preview_pending || !s_preview_ok);
}

/* ---- load / save */
static void screen_slots(int saving)
{
    int x, y, ix, iy, iw, ih, i, n = saving ? SAVE_SLOTS : SAVE_LIST_COUNT, rowh = 22, lw = 300, yy, W = 520, H = saving ? 300 : 372;
    char a[64], b[96], d[40];
    win_centre(W, H, &x, &y);
    win(x, y, W, H, saving ? "Save the town" : "Load a town", W_CLOSE, &ix, &iy, &iw, &ih);
    if (s_drawing) draw9(&s9_inset, ix + 8, iy + 8, lw + 4, n * rowh + 4, COL(UI_COL_PAPER_DK));
    for (i = 0; i < n; i++) {
        int ry = iy + 10 + i * rowh, rx = ix + 10;
        w_row(W_L_ROW0 + i, rx, ry, lw, rowh, s_sel == i);
        if (!s_drawing) continue;
        slot_name(i, a, sizeof(a));
        txt(rx + 4, ry + 1, a, FONT_BODY_DARK);
        if (s_have[i]) {
            const SaveMeta *m = &s_meta[i];
            str_printf(b, sizeof(b), "%s", m->town);
            txt(rx + 74, ry + 1, b, FONT_BODY_DARK);
            text_fmt_date(d, sizeof(d), m->year, m->month);
            txt_r(rx + lw - 4, ry + 1, d, FONT_BODY_DARK);
            text_fmt_int(d, sizeof(d), m->population);
            str_printf(b, sizeof(b), "President %s " TEXT_DOT " %s residents", m->president, d);
            txt(rx + 74, ry + 11, b, FONT_BODY_GREY);
        } else txt(rx + 74, ry + 6, "\x97 empty \x97", FONT_BODY_GREY);
        if (i < n - 1) gfx_fill(rx, ry + rowh - 1, rx + lw, ry + rowh, COL(UI_COL_CARD));
    }
    /* details of the selection */
    if (s_drawing) {
        int px = ix + lw + 30, pw = ix + iw - 10 - px;
        draw9(&s9_mmap, px + (pw - 134) / 2, iy + 8, 134, 134, COL(UI_COL_BAR_DK));
        if (s_sel >= 0 && s_have[s_sel] && s_th_ok) {
            if (s_thumb_sel != s_sel) build_thumb(s_sel);
            gfx_blit(&s_th_img, px + (pw - 134) / 2 + 3, iy + 11);
        }
        yy = iy + 150;
        if (s_sel >= 0 && s_have[s_sel]) {
            const SaveMeta *m = &s_meta[s_sel];
            txt(px, yy, m->town, FONT_HEAD); yy += 16;
            str_printf(b, sizeof(b), "President %s", m->president); txt(px, yy, b, FONT_BODY_DARK); yy += 12;
            text_fmt_date(d, sizeof(d), m->year, m->month); txt(px, yy, d, FONT_BODY_DARK); yy += 12;
            text_fmt_int(d, sizeof(d), m->population); str_printf(b, sizeof(b), "%s residents", d); txt(px, yy, b, FONT_BODY_DARK); yy += 12;
            txt(px, yy, text_status_name(ICLAMP(m->town_status, 0, TS_COUNT - 1)), FONT_BODY_DARK); yy += 12;
            text_fmt_money(d, sizeof(d), m->money, m->year); txt(px, yy, d, m->money < 0 ? FONT_BODY_RED : FONT_BODY_DARK); yy += 12;
            if (m->saved_time) {
                time_t tt = (time_t)m->saved_time;
                struct tm *lt = localtime(&tt);
                if (lt) {
                    str_printf(b, sizeof(b), "Saved %d.%d.%d %02d:%02d", lt->tm_mday, lt->tm_mon + 1, lt->tm_year + 1900, lt->tm_hour, lt->tm_min);
                    txt(px, yy, b, FONT_BODY_GREY);
                }
            }
        } else if (s_sel >= 0) txt(px, yy, saving ? "An empty slot." : "Nothing saved here.", FONT_BODY_GREY);
        else txt(px, yy, saving ? "Choose a slot." : "No saved towns yet.", FONT_BODY_GREY);
    }
    w_button(W_L_BACK, ix + 10, iy + ih - 30, 90, 22, "Back", 0);
    w_button(W_L_GO, ix + iw - 10 - 120, iy + ih - 30, 120, 22, saving ? "Save here" : "Load",
             s_sel < 0 || (!saving && !s_have[s_sel]));
}

/* ---- settings */
static void screen_settings(void)
{
    static const char *const res_name[3] = { "640x480", "800x600", "1024x768" };
    static const char *const tick_name[3] = { "Urgent only", "Normal", "Everything" };
    int x, y, ix, iy, iw, ih, lx, cx, yy, k, rx, W = 420, H = 300;
    char buf[32];
    win_centre(W, H, &x, &y);
    win(x, y, W, H, "Settings", W_CLOSE, &ix, &iy, &iw, &ih);
    lx = ix + 10; cx = ix + 120; yy = iy + 12;
    if (s_drawing) txt(lx, yy, "Screen", FONT_BODY_DARK);
    rx = cx;
    for (k = 0; k < 3; k++) rx += w_radio(W_S_RES0 + k, rx, yy, g_cfg.res == k, res_name[k]) + 10;
    yy += 18;
    if (s_drawing) txt(lx, yy, "Display", FONT_BODY_DARK);
    rx = cx;
    rx += w_radio(W_S_FULL, rx, yy, g_cfg.fullscreen, "Full screen") + 10;
    w_radio(W_S_WINDOW, rx, yy, !g_cfg.fullscreen, "Window");
    yy += 14;
    if (s_drawing) txt(cx, yy, "Screen and display change at the next start.", FONT_BODY_GREY);
    yy += 20;
    if (s_drawing) txt(lx, yy, "Frame rate", FONT_BODY_DARK);
    rx = cx;
    rx += w_radio(W_S_FPS60, rx, yy, g_cfg.fps == 60, "60") + 10;
    rx += w_radio(W_S_FPS30, rx, yy, g_cfg.fps == 30, "30") + 10;
    w_radio(W_S_FPSAUTO, rx, yy, g_cfg.fps == 0, "Automatic");
    yy += 22;
    rule(lx, yy, iw - 20); yy += 10;
    if (s_drawing) txt(lx, yy, "Scrolling", FONT_BODY_DARK);
    w_check(W_S_EDGE, cx, yy, g_cfg.edge_scroll, "Scroll at the screen edge", 0);
    yy += 20;
    if (s_drawing) txt(lx, yy, "Scroll speed", FONT_BODY_DARK);
    w_slider(W_S_SPEED, cx + 4, yy + 2, 160, g_cfg.scroll_speed, 1, 32);
    if (s_drawing) { str_printf(buf, sizeof(buf), "%d", g_cfg.scroll_speed); txt(cx + 180, yy, buf, FONT_BODY_DARK); }
    yy += 22;
    rule(lx, yy, iw - 20); yy += 10;
    if (s_drawing) txt(lx, yy, "Effects volume", FONT_BODY_DARK);
    w_slider(W_S_SFX, cx + 4, yy + 2, 160, g_cfg.sfx_volume, 0, 100);
    if (s_drawing) { str_printf(buf, sizeof(buf), "%d%%", g_cfg.sfx_volume); txt(cx + 180, yy, buf, FONT_BODY_DARK); }
    yy += 20;
    if (s_drawing) txt(lx, yy, "Town ambience", FONT_BODY_DARK);
    w_slider(W_S_AMB, cx + 4, yy + 2, 160, g_cfg.ambient_volume, 0, 100);
    if (s_drawing) { str_printf(buf, sizeof(buf), "%d%%", g_cfg.ambient_volume); txt(cx + 180, yy, buf, FONT_BODY_DARK); }
    yy += 22;
    rule(lx, yy, iw - 20); yy += 10;
    if (s_drawing) txt(lx, yy, "News ticker", FONT_BODY_DARK);
    rx = cx;
    for (k = 0; k < 3; k++) rx += w_radio(W_S_TICK0 + k, rx, yy, g_cfg.ticker_verbosity == k, tick_name[k]) + 10;
    w_button(W_S_BACK, ix + iw - 10 - 100, iy + ih - 30, 100, 22, "Done", 0);
}

/* ---- pause */
static void screen_pause(void)
{
    static const char *const lab[6] = { "Resume", "Save town", "Load town", "Settings", "Quit to title", "Quit to Windows" };
    int x, y, ix, iy, iw, ih, k, W = 220, H = 242;   /* 1.1 D1: + the difficulty line (was 230) */
    char buf[64], d[32];
    win_centre(W, H, &x, &y);
    win(x, y, W, H, "Paused", W_CLOSE, &ix, &iy, &iw, &ih);
    if (s_drawing) {
        txt_c(ix, iw, iy + 6, sim_settings()->town, FONT_HEAD);
        text_fmt_date(d, sizeof(d), sim_date()->year, sim_date()->month);
        str_printf(buf, sizeof(buf), "%s " TEXT_DOT " President %s", d, sim_settings()->president);
        txt_c(ix, iw, iy + 21, buf, FONT_BODY_GREY);
        str_printf(buf, sizeof(buf), "Difficulty: %s", diff()->name);     /* 1.1 D1: fixed for the game */
        txt_c(ix, iw, iy + 33, buf, FONT_BODY_GREY);
    }
    for (k = 0; k < 6; k++) w_button(W_P_RESUME + k, ix + 20, iy + 52 + k * 26, iw - 40, 22, lab[k], 0);
}

/* ---- dialog (drawn last; while open only its widgets are hot) */
static void screen_dialog(void)
{
    int x, y, ix, iy, iw, ih, W = 300, H = 112, n, k;
    char lines[3][100];
    if (!s_dlg.on) return;
    s_dlg_first = s_nw;
    win_centre(W, H, &x, &y);
    win(x, y, W, H, s_dlg.title, 0, &ix, &iy, &iw, &ih);
    if (s_drawing) {
        n = wrap3(s_dlg.detail, FONT_BODY_DARK, iw - 20, lines);
        for (k = 0; k < n; k++) txt(ix + 10, iy + 8 + k * 12, lines[k], FONT_BODY_DARK);
    }
    if (s_dlg.yesno) {
        w_button(W_D_YES, ix + iw / 2 - 100, iy + ih - 28, 90, 22, "Yes", 0);
        w_button(W_D_NO, ix + iw / 2 + 10, iy + ih - 28, 90, 22, "No", 0);
    } else w_button(W_D_YES, ix + (iw - 90) / 2, iy + ih - 28, 90, 22, "OK", 0);
}

static void layout(int drawing)
{
    s_drawing = drawing;
    s_nw = 0; s_dlg_first = 0;
    switch (s_screen) {
    case SC_TITLE:    screen_title(); break;
    case SC_NEWGAME:  screen_newgame(); break;
    case SC_LOAD:     screen_slots(0); break;
    case SC_SAVE:     screen_slots(1); break;
    case SC_SETTINGS: screen_settings(); break;
    default:          screen_pause(); break;
    }
    screen_dialog();
    s_drawing = 0;
}

/* ================================================================== actions */
static void go_back(void)
{
    if (s_screen == SC_SETTINGS && s_settings_dirty) { config_save(); s_result = MR_SETTINGS_CHANGED; }
    if (s_ingame) {
        if (s_screen == SC_PAUSE) { s_result = MR_RESUME; return; }
        enter_screen(SC_PAUSE);
    } else {
        if (s_screen == SC_TITLE) return;
        if (s_screen == SC_NEWGAME) backdrop_title();
        enter_screen(SC_TITLE);
    }
}

static void reroll_town(void) { text_town_name(&s_name_rng, s_ng.sim.town, NAME_LEN); }

static void start_new_game(void)
{
    if (!s_ng.sim.president[0]) str_copy(s_ng.sim.president, "Kekkola", NAME_LEN);
    if (!s_ng.sim.town[0]) reroll_town();
    s_ng.map.seed = (u32)strtoul(s_seed_txt, NULL, 10);
    if (!s_ng.map.seed) s_ng.map.seed = 1;
    s_ng.sim.seed = s_ng.map.seed;
    str_copy(g_cfg.president, s_ng.sim.president, NAME_LEN);
    g_cfg.disasters = s_ng.sim.disasters;
    config_save();
    log_printf("menu: new game: president \"%s\", town \"%s\", %s seed %u, %d, funds %d, disasters %d, all %d, railway %d, %s",
               s_ng.sim.president, s_ng.sim.town, mapgen_preset_name(s_ng.map.preset), s_ng.map.seed,
               s_ng.sim.start_year, s_ng.sim.funds, s_ng.sim.disasters, s_ng.sim.all_unlocked, s_ng.map.railway,
               diff_name(s_ng.sim.difficulty));
    s_result = MR_NEW_GAME;              /* s_ingame is re-derived from game_state() at the next menu_enter */
}

static void do_load_selected(void)
{
    if (s_sel < 0 || !s_have[s_sel]) return;
    save_any_path(s_sel, s_path, sizeof(s_path));
    log_printf("menu: load %s", s_path);
    s_result = MR_LOAD;                  /* a failed load keeps this screen (menu_message) */
}

static void do_save_selected(int confirmed)
{
    if (s_sel < 0 || s_sel >= SAVE_SLOTS) return;
    if (s_have[s_sel] && !confirmed) {
        char d[160];
        str_printf(d, sizeof(d), "Slot %d holds %s (%d). Replace it with %s?", s_sel + 1, s_meta[s_sel].town,
                   s_meta[s_sel].year, sim_settings()->town);
        dialog(1, DA_OVERWRITE, "Overwrite the save?", d);
        return;
    }
    save_slot_path(s_sel, s_path, sizeof(s_path));
    log_printf("menu: save %s", s_path);
    s_result = MR_SAVE;
}

static void set_cfg(int *field, int v)
{
    if (*field == v) return;
    *field = v;
    s_settings_dirty = 1;
}

static void apply_slider(int id, int v)
{
    switch (id) {
    case W_S_SPEED: set_cfg(&g_cfg.scroll_speed, v); break;
    case W_S_SFX:   set_cfg(&g_cfg.sfx_volume, v); snd_set_group_volume(SND_GROUP_SFX, v); break;
    case W_S_AMB:   set_cfg(&g_cfg.ambient_volume, v); snd_set_group_volume(SND_GROUP_AMBIENT, v); break;
    }
}

static int slider_value(const Widget *w, int mx)
{
    int x0 = w->x + 4, ww = w->w - 8;
    int v = w->vmin + ((mx - x0) * (w->vmax - w->vmin) + ww / 2) / IMAX(1, ww);
    return ICLAMP(v, w->vmin, w->vmax);
}

static void activate(int id)
{
    if (s_dlg.on) {
        int act = s_dlg.action;
        s_dlg.on = 0;
        click_snd();
        if (id != W_D_YES) return;
        switch (act) {
        case DA_OVERWRITE: do_save_selected(1); break;
        case DA_QUIT:      s_result = MR_QUIT; break;
        case DA_TITLE:     s_ingame = 0; backdrop_title(); enter_screen(SC_TITLE); s_result = MR_TITLE; break;
        }
        return;
    }
    if (id != W_N_PRES && id != W_N_TOWN && id != W_N_SEED) click_snd();
    switch (id) {
    case W_CLOSE: go_back(); break;
    /* title */
    case W_T_NEW:      enter_screen(SC_NEWGAME); break;
    case W_T_LOAD:     enter_screen(SC_LOAD); break;
    case W_T_SETTINGS: s_back_screen = SC_TITLE; enter_screen(SC_SETTINGS); break;
    case W_T_QUIT:     s_result = MR_QUIT; break;
    /* new game */
    case W_N_PRES: case W_N_TOWN: case W_N_SEED: s_focus = id; break;
    case W_N_TOWN_REROLL: reroll_town(); break;
    case W_N_PRESET_L: s_ng.map.preset = (s_ng.map.preset + MAP_PRESET_COUNT - 1) % MAP_PRESET_COUNT; s_preview_pending = 1; break;
    case W_N_PRESET_R: s_ng.map.preset = (s_ng.map.preset + 1) % MAP_PRESET_COUNT; s_preview_pending = 1; break;
    case W_N_SEED_REROLL:
        str_printf(s_seed_txt, sizeof(s_seed_txt), "%u", (rng_next(&s_name_rng) % 999999u) + 1);
        s_preview_pending = 1;
        break;
    case W_N_RAILWAY:   s_ng.map.railway = !s_ng.map.railway; s_preview_pending = 1; break;
    case W_N_DISASTERS: s_ng.sim.disasters = !s_ng.sim.disasters; break;
    case W_N_POLITICS:  s_ng.sim.politics = !s_ng.sim.politics; break;    /* M1 */
    case W_N_PIPES: g_cfg.pipes_with_roads = !g_cfg.pipes_with_roads; break;          /* F1 (saved at Start) */
    case W_N_ALLTOOLS:  s_ng.sim.all_unlocked = !s_ng.sim.all_unlocked; break;
    case W_N_BACK:      go_back(); break;
    case W_N_START:     if (!s_preview_pending && s_preview_ok) start_new_game(); break;
    /* load / save */
    case W_L_BACK: go_back(); break;
    case W_L_GO:   if (s_screen == SC_SAVE) do_save_selected(0); else do_load_selected(); break;
    /* settings */
    case W_S_FULL:    set_cfg(&g_cfg.fullscreen, 1); break;
    case W_S_WINDOW:  set_cfg(&g_cfg.fullscreen, 0); break;
    case W_S_FPS60:   set_cfg(&g_cfg.fps, 60); plat_set_fps(60); break;
    case W_S_FPS30:   set_cfg(&g_cfg.fps, 30); plat_set_fps(30); break;
    case W_S_FPSAUTO: set_cfg(&g_cfg.fps, 0); plat_set_fps(0); break;
    case W_S_EDGE:    set_cfg(&g_cfg.edge_scroll, !g_cfg.edge_scroll); break;
    case W_S_BACK:    go_back(); break;
    /* pause */
    case W_P_RESUME:   s_result = MR_RESUME; break;
    case W_P_SAVE:     enter_screen(SC_SAVE); break;
    case W_P_LOAD:     enter_screen(SC_LOAD); break;
    case W_P_SETTINGS: s_back_screen = SC_PAUSE; enter_screen(SC_SETTINGS); break;
    case W_P_TITLE:    dialog(1, DA_TITLE, "Quit to the title?", "The town is not saved automatically. Unsaved progress since the last save is lost."); break;
    case W_P_QUIT:     dialog(1, DA_QUIT, "Quit to Windows?", "Unsaved progress since the last save is lost."); break;
    default:
        if (id >= W_N_YEAR0 && id <= W_N_YEAR3) s_ng.sim.start_year = s_years[id - W_N_YEAR0];
        else if (id >= W_N_FUNDS0 && id <= W_N_FUNDS2) s_ng.sim.funds = id - W_N_FUNDS0;
        else if (id >= W_N_DIFF0 && id <= W_N_DIFF2) s_ng.sim.difficulty = id - W_N_DIFF0;   /* 1.1 D1 */
        else if (id >= W_S_RES0 && id <= W_S_RES2) set_cfg(&g_cfg.res, id - W_S_RES0);
        else if (id >= W_S_TICK0 && id <= W_S_TICK2) set_cfg(&g_cfg.ticker_verbosity, id - W_S_TICK0);
        else if (id >= W_L_ROW0 && id <= W_L_ROW_LAST) {
            int row = id - W_L_ROW0;
            int dbl = row == s_last_click_row && s_now - s_last_click_t < 0.45;
            s_sel = row;
            s_last_click_row = row; s_last_click_t = s_now;
            if (dbl) { if (s_screen == SC_SAVE) do_save_selected(0); else do_load_selected(); }
        }
        break;
    }
}

/* typed characters into the focused field */
static int name_char_ok(int c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == ' ' || c == '-' || c == '\'' || c == '.' ||
           c == 0xE4 || c == 0xF6 || c == 0xE5 || c == 0xC4 || c == 0xD6 || c == 0xC5;
}

static void type_text(const char *t)
{
    char *buf;
    int size, n;
    if (s_focus == W_N_PRES) { buf = s_ng.sim.president; size = NAME_LEN; }
    else if (s_focus == W_N_TOWN) { buf = s_ng.sim.town; size = NAME_LEN; }
    else if (s_focus == W_N_SEED) { buf = s_seed_txt; size = 10; }
    else return;
    for (; *t; t++) {
        int c = (u8)*t;
        n = (int)strlen(buf);
        if (c == '\b') { if (n > 0) buf[n - 1] = 0; if (s_focus == W_N_SEED) s_preview_pending = 1; continue; }
        if (c == '\t') {
            s_focus = s_focus == W_N_PRES ? W_N_TOWN : s_focus == W_N_TOWN ? W_N_SEED : W_N_PRES;
            return;
        }
        if (c == '\r') continue;
        if (s_focus == W_N_SEED ? (c < '0' || c > '9') : !name_char_ok(c)) continue;
        if (n >= size - 1) continue;
        if (s_focus != W_N_SEED && c == ' ' && n == 0) continue;     /* no leading space */
        buf[n] = (char)c; buf[n + 1] = 0;
        if (s_focus == W_N_SEED) s_preview_pending = 1;
    }
    snd_play(SND_TYPE, 40);
}

/* ================================================================== public */
void menu_init(void)
{
    int i, k;
    static const char *const moods[4] = { "content", "stern", "delighted", "worried" };
    static const char *const ck[3] = { "normal", "hover", "disabled" };
    static const char *const ar[3] = { "normal", "pressed", "disabled" };
    s_sw = gfx_screen_w(); s_sh = gfx_screen_h();
    L = &ui_layouts[s_sw <= 640 ? 0 : s_sw <= 800 ? 1 : 2];
    save_set_dir(g_cfg.exe_dir);
    game_default_params(&s_ng);
    rng_seed(&s_name_rng, (u32)time(NULL) ^ g_cfg.seed);
    if (!strcmp(s_ng.sim.town, "Kaupunki") || !s_ng.sim.town[0]) reroll_town();
    str_printf(s_seed_txt, sizeof(s_seed_txt), "%u", s_ng.map.seed ? s_ng.map.seed : 1);
    s_ng.sim.disasters = g_cfg.disasters;
    s_ng.sim.politics = 1;                     /* M1: politics on by default in the New Game panel */
    s_ng.sim.difficulty = DIFF_NORMAL;         /* 1.1 D1: Normal by default in the New Game panel (direct starts: Easy) */
    /* art */
    slice9_init(&s9_win, "win"); slice9_init(&s9_inset, "inset"); slice9_init(&s9_mmap, "mmap"); slice9_init(&s9_plaque, "plaque");
    for (i = 0; i < BS_COUNT; i++) { char b[24]; str_printf(b, sizeof(b), "btn_%s", s_bs_name[i]); slice9_init(&s9_btn[i], b); }
    slice3_init(&s3_plate, "plate"); slice3_init(&s3_grille, "grille"); slice3_init(&s3_track, "slider_track");
    slice3_init(&s3_fill, "slider_fill"); slice3_init(&s3_flysel, "flyrow_sel"); slice3_init(&s3_flyhover, "flyrow_hover");
    A.teak = art_find("teak_tile"); A.panel_edge = art_find("panel_edge"); A.bar_tile = art_find("bar_tile");
    A.ticker_tile = art_find("ticker_tile"); A.badge = art_find("badge"); A.bar_sep = art_find("bar_sep");
    A.lamp_info = art_find("lamp_info"); A.rule = art_find("rule"); A.seal = art_find("stamp_seal");
    A.stamp_project = art_find("stamp_project");
    A.pres_frame_l = art_find("pres_frame_l"); A.pres_frame_s = art_find("pres_frame_s");
    for (i = 0; i < 4; i++) { A.pres_l[i] = art_findf("pres_l_%s", moods[i]); A.pres_s[i] = art_findf("pres_s_%s", moods[i]); }
    for (i = 0; i < 2; i++)
        for (k = 0; k < 3; k++) {
            A.check[i][k] = art_findf("check_%s_%s", i ? "on" : "off", ck[k]);
            A.radio[i][k] = art_findf("radio_%s_%s", i ? "on" : "off", ck[k == 2 ? 2 : k]);
        }
    for (k = 0; k < 3; k++) { A.arrow_l[k] = art_findf("arrow_left_%s", ar[k]); A.arrow_r[k] = art_findf("arrow_right_%s", ar[k]); }
    A.close[0] = art_find("close_normal"); A.close[1] = art_find("close_hover"); A.close[2] = art_find("close_pressed");
    A.knob[0] = art_find("slider_knob_normal"); A.knob[1] = art_find("slider_knob_hover"); A.knob[2] = art_find("slider_knob_pressed");
    /* images */
    make_title_image();
    memset(&s_mm_img, 0, sizeof(s_mm_img)); memset(&s_th_img, 0, sizeof(s_th_img));
    s_mm_ok = s_th_ok = 0;                 /* F3: made on first use (menu_img) */
    s_inited = 1;
    log_printf("menu: init (%dx%d, save dir %s)", s_sw, s_sh, g_cfg.exe_dir);
}

void menu_enter(int gs)
{
    if (!s_inited) return;
    s_result = MR_NONE;
    s_dlg.on = 0;
    s_hot = s_press = W_NONE;
    input_clear();
    if (gs == GS_TITLE) {
        s_ingame = 0;
        backdrop_title();
        enter_screen(SC_TITLE);
        return;
    }
    s_ingame = game_state() == GS_PLAY || game_state() == GS_PAUSEMENU;
    if (s_ingame) s_backdrop = 0;
    switch (gs) {
    case GS_NEWGAME:     enter_screen(SC_NEWGAME); break;
    case GS_LOADMENU:    enter_screen(SC_LOAD); break;
    case MENU_SAVE:      enter_screen(SC_SAVE); break;
    case MENU_SETTINGS:  s_back_screen = s_ingame ? SC_PAUSE : SC_TITLE; enter_screen(SC_SETTINGS); break;
    case MENU_QUIT_CONFIRM:
        enter_screen(SC_PAUSE);
        dialog(1, DA_QUIT, "Quit to Windows?", "Unsaved progress since the last save is lost.");
        break;
    default:             enter_screen(SC_PAUSE); break;
    }
    snd_play(SND_PANEL_OPEN, 80);
}

static int hit(int mx, int my)
{
    int i;
    for (i = s_nw - 1; i >= s_dlg_first; i--) {
        const Widget *w = &s_w[i];
        if (in_rect(mx, my, w->x, w->y, w->w, w->h)) return w->disabled ? -1 : i;
    }
    return -1;
}

int menu_update(const InputState *in, double dt)
{
    int hi, r;
    if (!s_inited) return MR_NONE;
    s_now += dt; s_caret_t += dt;
    s_result = MR_NONE;
    if (s_preview_pending == 2) gen_preview();         /* the "Surveying" frame has been shown */
    else if (s_preview_pending == 1 && s_screen == SC_NEWGAME) s_preview_pending = 2;
    /* title backdrop: slow pan */
    if (s_backdrop == 1 && s_screen == SC_TITLE) {
        s_pan_t += dt;
        if (s_pan_t > 40.0) { s_pan_t = 0; s_pan_dir = -s_pan_dir; }
        s_pan_acc += dt * 8.0;
        if (s_pan_acc >= 1.0) { int px = (int)s_pan_acc; s_pan_acc -= px; cam_scroll(px * s_pan_dir, 0); }
    }
    layout(0);
    s_mx = in->inside ? in->mx : -10000; s_my = in->inside ? in->my : -10000;
    s_down = (in->buttons & MOUSE_L) != 0;
    hi = hit(s_mx, s_my);
    s_hot = hi >= 0 ? s_w[hi].id : W_NONE;
    if (s_drag) {
        int i;
        for (i = 0; i < s_nw; i++) if (s_w[i].id == s_drag) apply_slider(s_drag, slider_value(&s_w[i], in->mx));
        if (!s_down) s_drag = W_NONE;
    } else if (in->pressed & MOUSE_L) {
        s_press = s_hot;
        if (hi >= 0 && s_w[hi].kind == WK_SLIDER) { s_drag = s_hot; apply_slider(s_drag, slider_value(&s_w[hi], in->mx)); }
        else if (hi >= 0 && s_w[hi].kind == WK_FIELD) activate(s_hot);
        else if (hi < 0 && s_screen == SC_NEWGAME && !s_dlg.on) s_focus = W_NONE;
    } else if (in->released & MOUSE_L) {
        if (s_press != W_NONE && s_press == s_hot && hi >= 0 && s_w[hi].kind != WK_FIELD) activate(s_hot);
        s_press = W_NONE;
    }
    if ((in->pressed & MOUSE_R) && !s_dlg.on) go_back();
    if (in->wheel && (s_screen == SC_LOAD || s_screen == SC_SAVE)) {
        int n = s_screen == SC_SAVE ? SAVE_SLOTS : SAVE_LIST_COUNT;
        s_sel = ICLAMP(s_sel - in->wheel, 0, n - 1);
    }
    /* keyboard */
    if (in->text[0] && s_focus && !s_dlg.on) type_text(in->text);
    if (key_hit(VK_ESCAPE)) {
        if (s_dlg.on) activate(W_D_NO);
        else if (s_focus && s_screen != SC_NEWGAME) s_focus = W_NONE;
        else go_back();
    }
    if (key_hit(VK_RETURN)) {
        if (s_dlg.on) activate(W_D_YES);
        else if (s_screen == SC_NEWGAME) activate(W_N_START);
        else if (s_screen == SC_LOAD || s_screen == SC_SAVE) activate(W_L_GO);
        else if (s_screen == SC_PAUSE) activate(W_P_RESUME);
    }
    if ((s_screen == SC_LOAD || s_screen == SC_SAVE) && !s_dlg.on) {
        int n = s_screen == SC_SAVE ? SAVE_SLOTS : SAVE_LIST_COUNT;
        if (key_pressed(VK_DOWN)) s_sel = ICLAMP(s_sel + 1, 0, n - 1);
        if (key_pressed(VK_UP)) s_sel = ICLAMP(s_sel - 1, 0, n - 1);
    }
    r = s_result;
    s_result = MR_NONE;
    if (r != MR_NONE) log_printf("menu: result %d", r);
    return r;
}

void menu_draw(void)
{
    if (!s_inited) return;
    gfx_set_target(NULL);
    if (menu_draws_world()) draw_backdrop();
    else gfx_fill_half(L->map.x, L->map.y, L->map.x + L->map.w, L->map.y + L->map.h, COL(UI_COL_BAR_XDK));
    layout(1);
}

int menu_draws_world(void) { return !s_ingame; }

int menu_screen_state(void)
{
    switch (s_screen) {
    case SC_TITLE: return GS_TITLE;
    case SC_NEWGAME: return GS_NEWGAME;
    case SC_LOAD: return GS_LOADMENU;
    default: return GS_PAUSEMENU;
    }
}

void menu_message(const char *title, const char *detail)
{
    dialog(0, DA_NONE, title, detail);
}

const NewGameParams *menu_new_game_params(void) { return &s_ng; }
const char *menu_selected_path(void) { return s_path; }

/* ================================================================== -test menu
   Self-contained (main.cpp calls it before game_init): inits art, ui, the world/sim modules and the
   renderer like render_selftest, then walks the screens with scripted mouse/keyboard input, logs every
   screen + result and saves menutest_<W>_<n>.bmp. Writes and deletes slot 8 (SAVE8.KAU). */
static InputState s_tin;
static int s_tfail, s_tshot;

static void t_check(int ok, const char *fmt, ...)
{
    char b[200];
    va_list ap;
    va_start(ap, fmt);
    str_vprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    log_printf("menu_selftest: %s %s", ok ? "PASS" : "FAIL", b);
    if (!ok) s_tfail++;
}

static int t_frame(void)            /* one frame with the scripted input; returns the menu result */
{
    int r;
    if (!plat_pump()) return -1;
    input_poll();
    r = menu_update(&s_tin, 1.0 / 60.0);
    s_tin.pressed = s_tin.released = 0; s_tin.text[0] = 0; s_tin.wheel = 0;
    if (game_state() == GS_PLAY || game_state() == GS_PAUSEMENU) {   /* in game: a frozen world behind */
        render_update(3.0); render_world(); render_flush_sprites();
    }
    menu_draw();
    plat_present();
    return r;
}

static int t_frames(int n) { int r = MR_NONE, k, x; for (k = 0; k < n; k++) { x = t_frame(); if (x != MR_NONE) r = x; } return r; }

static void t_shot(const char *what)
{
    char name[64];
    if (!plat_pump()) return;
    if (game_state() == GS_PLAY || game_state() == GS_PAUSEMENU) { render_update(3.0); render_world(); render_flush_sprites(); }
    menu_draw();
    str_printf(name, sizeof(name), "menutest_%d_%d.bmp", s_sw, ++s_tshot);
    gfx_save_bmp(data_path(name));
    plat_present();
    log_printf("menu_selftest: screenshot %s: %s", name, what);
}

static int t_widget_centre(int id, int *x, int *y)
{
    int i;
    layout(0);
    for (i = 0; i < s_nw; i++)
        if (s_w[i].id == id) { *x = s_w[i].x + s_w[i].w / 2; *y = s_w[i].y + s_w[i].h / 2; return 1; }
    return 0;
}

static int t_click(int id, const char *what)  /* move, press, release on a widget; returns the result */
{
    int x, y, r;
    if (!t_widget_centre(id, &x, &y)) { t_check(0, "widget for \"%s\" is on screen %s", what, s_sc_name[s_screen]); return MR_NONE; }
    s_tin.mx = x; s_tin.my = y; s_tin.inside = 1;
    t_frame();
    s_tin.buttons |= MOUSE_L; s_tin.pressed |= MOUSE_L;
    t_frame();
    s_tin.buttons &= ~MOUSE_L; s_tin.released |= MOUSE_L;
    r = t_frame();
    log_printf("menu_selftest: click \"%s\" -> screen %s, result %d%s", what, s_sc_name[s_screen], r, s_dlg.on ? ", dialog open" : "");
    return r;
}

static void t_type(const char *s) { str_copy(s_tin.text, s, sizeof(s_tin.text)); t_frame(); }

static void t_wait_preview(void) { int k; for (k = 0; k < 10 && s_preview_pending; k++) t_frame(); }

int menu_selftest(void)
{
    int r, k;
    char shots_note[64];
    s_tfail = 0; s_tshot = 0;
    memset(&s_tin, 0, sizeof(s_tin));
    if (!gfx_screen_w()) { log_printf("menu_selftest: needs a window"); return 1; }
    if (!art_init()) return 1;
    world_init(); budget_init(); net_init(); fields_init(); traffic_init(); sim_init(); text_init();
    if (!ui_init()) { log_printf("menu_selftest: ui_init failed"); return 1; }
    menu_init();
    {
        int mx, my, mw, mh;
        ui_map_rect(&mx, &my, &mw, &mh);
        cam_set_view(mx, my, mw, mh);
    }
    if (!render_init()) { log_printf("menu_selftest: render_init failed"); return 1; }
    game_set_state(GS_TITLE);
    menu_enter(GS_TITLE);
    t_frames(30);
    t_check(s_screen == SC_TITLE && s_backdrop == 1, "title screen with the town backdrop (%d lots)", g_world.lot_count);
    t_shot("title");

    /* settings from the title: toggle edge scroll twice, move a slider, Done */
    t_click(W_T_SETTINGS, "Settings");
    t_check(s_screen == SC_SETTINGS, "settings screen");
    k = g_cfg.edge_scroll;
    t_click(W_S_EDGE, "edge scroll");
    t_check(g_cfg.edge_scroll == !k, "edge scroll toggled to %d", g_cfg.edge_scroll);
    t_click(W_S_EDGE, "edge scroll");
    t_frames(2);
    t_shot("settings");
    r = t_click(W_S_BACK, "Done");
    t_check(s_screen == SC_TITLE, "back on the title (result %d)", r);

    /* new game: names, preset, seed, toggles, preview */
    t_click(W_T_NEW, "New town");
    t_wait_preview();
    t_check(s_screen == SC_NEWGAME && s_preview_ok, "new game screen with a preview (seed %s, %s)", s_seed_txt, mapgen_preset_name(s_ng.map.preset));
    t_check(!strcmp(s_ng.sim.president, g_cfg.president), "president defaults to \"%s\"", s_ng.sim.president);
    t_click(W_N_PRES, "president field");
    for (k = 0; k < NAME_LEN; k++) t_type("\b");
    t_type("Kekkola");
    t_type("\xe4!7");                         /* a-umlaut allowed, '!' and '7' rejected */
    t_check(!strcmp(s_ng.sim.president, "Kekkola\xe4"), "typed president \"%s\" (filtering)", s_ng.sim.president);
    t_type("\b");
    t_click(W_N_TOWN_REROLL, "town reroll");
    t_check(s_ng.sim.town[0] != 0, "town name rerolled to \"%s\"", s_ng.sim.town);
    t_click(W_N_PRESET_R, "next landscape");
    t_wait_preview();
    t_check(s_ng.map.preset == MAP_PRESET_RIVER && s_preview_ok, "preset %s regenerated", mapgen_preset_name(s_ng.map.preset));
    t_click(W_N_SEED, "seed field");
    for (k = 0; k < 10; k++) t_type("\b");
    t_type("1234");
    t_wait_preview();
    t_check(s_ng.map.seed == 1234, "seed 1234 regenerated (report water %d%%, buildable %d%%)", mapgen_report()->water_pct, mapgen_report()->buildable_pct);
    t_click(W_N_YEAR0 + 1, "1960");
    t_click(W_N_FUNDS0 + 2, "Austere");
    t_click(W_N_POLITICS, "politics toggle (on -> off)");
    t_check(s_ng.sim.start_year == 1960 && s_ng.sim.funds == FUNDS_AUSTERE && !s_ng.sim.politics, "year %d funds %d politics %d",
            s_ng.sim.start_year, s_ng.sim.funds, s_ng.sim.politics);
    t_frames(2);
    t_shot("new game (River town, seed 1234, 1960, austere)");
    r = t_click(W_N_START, "Found the town");
    t_check(r == MR_NEW_GAME, "Found the town returns MR_NEW_GAME (%d)", r);
    t_check(!strcmp(menu_new_game_params()->sim.president, "Kekkola") && menu_new_game_params()->map.seed == 1234,
            "params: president %s seed %u", menu_new_game_params()->sim.president, menu_new_game_params()->map.seed);

    /* act as game.cpp: start the city from the params (map + sim), then play state */
    mapgen_generate(&menu_new_game_params()->map);
    sim_new_city(&menu_new_game_params()->sim);
    render_reset();
    cam_center_on_tile(mapgen_report()->town_x, mapgen_report()->town_y);
    game_set_state(GS_PLAY);
    sim_run_months(3);

    /* pause menu -> save to slot 8 */
    game_set_state(GS_PAUSEMENU);
    menu_enter(GS_PAUSEMENU);
    t_frames(3);
    t_check(s_screen == SC_PAUSE && !menu_draws_world(), "pause menu over the frozen game");
    t_shot("pause menu");
    t_click(W_P_SAVE, "Save town");
    t_check(s_screen == SC_SAVE, "save slot list");
    t_click(W_L_ROW0 + SAVE_SLOTS - 1, "slot 8");
    r = t_click(W_L_GO, "Save here");
    if (s_dlg.on) { t_shot("overwrite dialog"); r = t_click(W_D_YES, "Yes (overwrite)"); }
    t_check(r == MR_SAVE, "Save here returns MR_SAVE (%d), path %s", r, menu_selected_path());
    if (r == MR_SAVE) t_check(save_game(menu_selected_path(), NULL), "saved %s", menu_selected_path());
    game_set_state(GS_PLAY);

    /* load list from the pause menu */
    game_set_state(GS_PAUSEMENU);
    menu_enter(GS_LOADMENU);
    t_frames(2);
    t_check(s_screen == SC_LOAD && s_have[SAVE_SLOTS - 1], "load list shows slot 8 (%s, pop %d)", s_meta[SAVE_SLOTS - 1].town, s_meta[SAVE_SLOTS - 1].population);
    t_click(W_L_ROW0 + SAVE_SLOTS - 1, "slot 8");
    t_frames(2);
    t_shot("load list (slot 8 selected, thumbnail)");
    r = t_click(W_L_GO, "Load");
    t_check(r == MR_LOAD, "Load returns MR_LOAD (%d)", r);
    if (r == MR_LOAD) {
        SaveMeta m;
        t_check(load_game(menu_selected_path(), &m) && !strcmp(m.president, "Kekkola"), "loaded %s (%s)", menu_selected_path(), m.town);
        render_reset();
    }
    game_set_state(GS_PLAY);

    /* message dialog, quit dialog */
    game_set_state(GS_PAUSEMENU);
    menu_enter(GS_PAUSEMENU);
    menu_message("Could not load the town.", "save file is corrupt (checksum mismatch)");
    t_frames(2);
    t_shot("message dialog");
    t_click(W_D_YES, "OK");
    t_check(!s_dlg.on, "message closed");
    t_click(W_P_QUIT, "Quit to Windows");
    t_check(s_dlg.on, "quit asks first");
    r = t_click(W_D_NO, "No");
    t_check(r == MR_NONE && s_screen == SC_PAUSE, "No keeps the pause menu");
    r = t_click(W_P_RESUME, "Resume");
    t_check(r == MR_RESUME, "Resume returns MR_RESUME (%d)", r);

    /* quit to title, then the load list from the title */
    menu_enter(GS_PAUSEMENU);
    t_click(W_P_TITLE, "Quit to title");
    r = t_click(W_D_YES, "Yes");
    t_check(r == MR_TITLE && s_screen == SC_TITLE && menu_draws_world(), "quit to title (%d)", r);
    game_set_state(GS_TITLE);
    t_click(W_T_LOAD, "Load town");
    t_frames(2);
    t_shot("load list from the title");
    t_click(W_L_BACK, "Back");
    t_check(s_screen == SC_TITLE, "back to the title");
    r = t_click(W_T_QUIT, "Quit to Windows");
    t_check(r == MR_QUIT, "title Quit returns MR_QUIT (%d)", r);
    {
        char p[PATH_LEN];
        save_slot_path(SAVE_SLOTS - 1, p, sizeof(p));
        remove(p);
    }
    str_printf(shots_note, sizeof(shots_note), "%d screenshots", s_tshot);
    log_printf("menu_selftest: %s, %d failures", shots_note, s_tfail);
    render_shutdown();
    ui_shutdown();
    return s_tfail;
}
