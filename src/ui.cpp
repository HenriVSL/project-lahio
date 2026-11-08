/* ui.cpp - teak & bakelite frame, tool keys + fly-outs, ticker, minimap, panels, tooltips, floating
   numbers. WINDOWS SIDE. Owner: T09. Implements ui.h; art pieces and composition rules: art/UI.md;
   frame rects: art/ui_layout.h; DESIGN 8.

   Cost model (probe 7, HANDOFF T01): ~5 us per blit call, ~5.5 us per glyph. Everything is composed into
   VRAM cache targets and only re-composed when what it shows changed:
     C_TOP     top bar (opaque, screen width x 16)
     C_SIDE    teak tool panel incl. the minimap (opaque); partial recomposition per element rect
               (a hovered key, the plaque, the minimap window) through the gfx clip rect: blits outside the
               clip are rejected on the CPU before any DirectDraw call, so a clipped recompose is cheap
     C_TICKER  news strip (opaque)
     C_WIN     the open panel (keyed: the window frame has rounded corners)
     C_POP     the fly-out or the tooltip (keyed; only one of them is shown at a time)
   Per frame ui_draw issues one blit per visible cache (3..5) plus floating money numbers.
   Composition functions draw in SCREEN coordinates: begin_compose() sets the cache as the target with
   gfx origin = the cache's screen position. If a target can't be created (VRAM full) the part is drawn
   straight onto the back buffer every frame instead (slow but correct; logged once).

   Interaction model: "immediate mode at compose time, retained hit list": composing a part records the
   rects of its clickable things (top bar fields, ticker items, panel controls); ui_input hit-tests the
   lists of the last composition. */
#include "common.h"
#include "ui.h"
#include "gfx.h"
#include "art.h"
#include "text.h"
#include "sim.h"
#include "budget.h"
#include "defs.h"
#include "tools.h"
#include "render.h"
#include "iso.h"
#include "world.h"
#include "net.h"
#include "util.h"
#include "config.h"
#include "ui_layout.h"
#include "politics.h"     /* M1 */
#include "ui_pol.h"         /* M1: politics panels */
#include "ui_m7.h"          /* M7: graphs, trade panel, newspaper */
#include "trade.h"
#include "industry.h"     /* M3 */
#include "civic.h"        /* M5 */
#include "traffic.h"      /* M2 */
#include "transit.h"      /* M2 */
#include "complex.h"      /* M6 */
#include "era.h"
#include "advice.h"       /* PL1: progression, unlock sentences, purposes, the power summary */
#include "season.h"       /* 1.1 W1: winter peak tooltips */
#include "hard.h"         /* 1.1 D1: difficulty notes, Sweden 1969-70 */

/* ================================================================== constants */
#define WIN_MAX_W   340          /* the panel target; every panel fits (600x420 rule: fits 640x480) */
#define WIN_MAX_H   320
#define POP_W       300          /* fly-out / tooltip target */
#define POP_H       216          /* PL1: 208 -> 216 (fly-out footer line: unlock sentence / purpose) */
#define CMD_QUEUE   16
#define TICK_MAX    8
#define TICK_TEXT   160
#define FLOAT_MAX   16
#define MAX_CTL     64
#define MAX_TIP     18
#define SIDE_DIRTY_MAX 8
#define TIP_DELAY   0.4
#define VALUE_RATE  0.25         /* value-driven recompositions at most every 0.25 s */
#define PANEL_CHECK 0.5          /* panel data signature checked twice a second */
#define TICK_LIFE   90.0         /* seconds a ticker item stays */
#define MM_INTERVAL 3.0          /* minimap rebuild interval while the world changes */

enum { C_TOP, C_SIDE, C_TICKER, C_WIN, C_POP, C_COUNT };
static const char *const s_cache_name[C_COUNT] = { "top", "side", "ticker", "window", "popup" };

/* hot ids (what the mouse is over) */
enum { H_NONE = 0, H_TOP = 1, H_DATE, H_FUNDS, H_POP, H_DEMAND, H_APPROVAL, H_WATER /* F1 */, H_POWER /* PL1 */,
       H_SPEED0 = 10, H_MENU = 14, H_ZOOM0 = 15 /* F3: zoom out, zoom in */,
       H_SIDE = 20, H_PLATE, H_PLAQUE, H_MMAP, H_TOOLLABEL, H_ZONEV /* F3: zone-view key */,
       H_M7K0 = 26 /* M7: graphs, trade, newspaper keys (26..28) right of the zone-view key */,
       H_KEY0 = 30, H_MINI0 = 40, H_GUIDE = 45 /* PL1: the wide Guide key under the M7 row */,
       H_TICKER = 50, H_TICK0 = 51,
       H_FLY = 60, H_FLYROW0 = 61,
       H_WIN = 100, H_CTL0 = 101 };
#define NKEYS 10
#define NMINI 5
#define FLY_MAX 12               /* M7: 10 -> 12 (landmarks in the Parks fly-out) */

enum { BS_NORMAL, BS_HOVER, BS_PRESSED, BS_DISABLED, BS_SELECTED, BS_COUNT };
static const char *const s_bs_name[BS_COUNT] = { "normal", "hover", "pressed", "disabled", "selected" };

/* ================================================================== art handles */
typedef struct { int p[9]; int l, t, r, b; } Slice9;   /* tl t tr l c r bl b br */
typedef struct { int p[3]; int a, b, vert; } Slice3;   /* l m r / t m b */

static struct {
    int bar_tile, bar_sep, dem_well, dem_bar[3];
    int speed[4][3];                 /* pause 1 2 3 x normal hover on */
    int menu[3];                     /* normal hover pressed */
    int teak, panel_edge, badge, pres_frame[2], pres[2][PRES_MOOD_COUNT];
    int mmap_cam[4];                 /* tl tr bl br */
    int ticker_tile, lamp[5];        /* info warn urgent pol off */
    int fly_arrow, fly_sep, rule, tab_line, close[3], knob[3];
    int check[2][3], radio[2][3];    /* off/on x normal hover disabled */
    int seal;
    int zoom[2][4];                  /* F3: btn_zoom_out/in x normal hover pressed disabled */
    int zonev[2];                    /* F3: icon_zoneview, _off */
    int m7k[4][2];                   /* M7: icon_graphs, icon_trade, icon_newspaper (+ _off); PL1: [3] = Services (icon_key_services) */
    int guide[2];                    /* PL1: icon_info (+ _off) on the Guide key */
} S;
static Slice9 s9_btn[BS_COUNT], s9_fly, s9_inset, s9_mmap, s9_plaque, s9_tip, s9_win;
static Slice3 s3_well, s3_cal, s3_flyhover, s3_flysel, s3_grille, s3_plate, s3_track, s3_fill, s3_tab[3];

static GfxFont s_font[FONT_COUNT];
static const char *const s_font_names[FONT_COUNT] = { "body_dark", "body_light", "head", "head_light",
    "head_red", "body_grey", "body_red", "body_green", "body_amber", "body_red_lt", "body_blue_lt",
    "body_brass", "body_green_lt", "body_shadow", "float_light", "float_green", "float_red" };

/* ================================================================== tool keys and fly-outs */
typedef struct { u8 tool, param, sep; const char *icon; } FlyEnt;
typedef struct { const char *icon, *label, *hint; int n; FlyEnt e[FLY_MAX]; } KeyDef;
static const KeyDef s_keys[NKEYS] = {
    { "key_roads", "Roads", "Roads and buses (R)", 6, { { TOOL_ROAD, ROAD_GRAVEL, 0, "r_grav" }, { TOOL_ROAD, ROAD_STREET, 0, "r_road" },
        { TOOL_ROAD, ROAD_MAIN, 0, "r_main" }, { TOOL_ROAD, ROAD_MOTORWAY, 0, "r_mway" },
        { TOOL_BUS_LINE, 0, 1, "bus_line" }, { TOOL_BUILD, B_BUS_DEPOT, 0, "b_bus_depot" } } },   /* M2 */
    { "key_homes", "Homes", "Homes: houses and apartments (H, A)", 3, { { TOOL_ZONE, ZONE_HOUSES, 0, "z_houses" },
        { TOOL_ZONE, ZONE_APARTMENTS, 0, "z_apartments" }, { TOOL_DEZONE, 0, 1, "dezone" } } },
    { "key_shops", "Shops", "Commercial zone (C)", 1, { { TOOL_ZONE, ZONE_COMMERCIAL, 0, "z_commercial" } } },
    { "key_industry", "Industry", "Industry, forestry and farms (I)", 7, { { TOOL_ZONE, ZONE_INDUSTRY, 0, "z_industry" },
        { TOOL_ZONE, ZONE_FORESTRY, 0, "z_forestry" }, { TOOL_ZONE, ZONE_FARM, 0, "z_farm" },   /* M3 */
        { TOOL_BUILD, B_ELEMENT_FACTORY, 1, "b_element_factory" },
        { TOOL_BUILD, B_PM_HALL, 1, "b_paper_mill" }, { TOOL_BUILD, B_VM_HALL, 0, "b_valtion_metalli" },   /* M6 complexes */
        { TOOL_GIFT, 0, 0, "b_row_houses" } } },
    { "key_power", "Power", "Power lines and plants (P)", 6, { { TOOL_POWER_LINE, 0, 0, "powerline" },
        { TOOL_BUILD, B_COAL_PLANT, 1, "b_coal_plant" }, { TOOL_BUILD, B_HYDRO_PLANT, 0, "b_hydro_plant" },
        { TOOL_BUILD, B_OIL_PLANT, 0, "b_oil_plant" }, { TOOL_BUILD, B_PEAT_PLANT, 0, "b_peat_plant" },
        { TOOL_BUILD, B_NUCLEAR_PLANT, 0, "b_nuclear_plant" } } },
    { "key_water", "Water", "Water, heating and sewage (W)", 6, { { TOOL_PIPE, 0, 0, "pipe" }, { TOOL_BUILD, B_PUMP, 1, "b_pump" }, { TOOL_BUILD, B_WELL, 0, "b_well" },
        { TOOL_BUILD, B_WATER_TOWER, 0, "b_water_tower" },
        { TOOL_BUILD, B_HEAT_PLANT, 1, "b_heat_plant" }, { TOOL_BUILD, B_SEWAGE_WORKS, 0, "b_sewage_works" } } },   /* M5 */
    { "key_services", "Services", "Services (S)", 8, { { TOOL_BUILD, B_FIRE_STATION, 0, "b_fire_station" },
        { TOOL_BUILD, B_SCHOOL, 0, "b_school" }, { TOOL_BUILD, B_CLINIC, 0, "b_clinic" }, { TOOL_BUILD, B_SAUNA, 0, "b_sauna" },
        { TOOL_BUILD, B_POLICE, 1, "b_police" }, { TOOL_BUILD, B_LIBRARY, 0, "b_library" }, { TOOL_BUILD, B_POST, 0, "b_post" },
        { TOOL_BUILD, B_HEALTH_CENTRE, 0, "b_health_centre" } } },
    { "key_parks", "Parks", "Parks and landmarks", 11, { { TOOL_BUILD, B_PARK, 0, "b_park" },
        { TOOL_BUILD, B_SPORTS_FIELD, 0, "b_sports_field" }, { TOOL_BUILD, B_ICE_RINK, 0, "b_ice_rink" },
        { TOOL_BUILD, B_CHURCH, 0, "b_church" }, { TOOL_BUILD, B_TOWN_HALL, 1, "b_town_hall" },
        { TOOL_BUILD, B_STATION, 0, "b_station" },
        { TOOL_BUILD, B_CONCERT_HALL, 1, "b_concert_hall" }, { TOOL_BUILD, B_TV_TOWER, 0, "b_tv_tower" },   /* M7 landmarks */
        { TOOL_BUILD, B_SKI_JUMP, 0, "b_ski_jump" }, { TOOL_BUILD, B_PRES_SAUNA, 0, "b_pres_sauna" },
        { TOOL_BUILD, B_FRIENDSHIP_STATUE, 0, "b_friendship_statue" } } },
    { "key_bulldoze", "Bulldoze", "Bulldoze (B)", 1, { { TOOL_BULLDOZE, 0, 0, "key_bulldoze" } } },
    { "key_query", "Query", "Query: what is here, and why isn't it growing? (Q)", 1, { { TOOL_QUERY, 0, 0, "key_query" } } },
};
static const char *const s_mini_icon[NMINI] = { "budget", "population", "overlays", "undo", "redo" };
static const char *const s_mini_tip[NMINI] = { "Budget and taxes", "Population and demand", "Overlays",
    "Undo (Ctrl+Z)", "Redo (Ctrl+Y)" };
static int s_key_icon[NKEYS][2], s_fly_icon[NKEYS][FLY_MAX][2], s_mini_ic[NMINI][2];
static int s_key_last[NKEYS];          /* last chosen entry per key */

/* ================================================================== state */
typedef struct {
    Img img; int ok, failed;           /* target created / creation failed (draw direct) */
    int x, y, w, h;                    /* screen rect of the cache contents */
    int dirty;                         /* full recompose needed */
    u32 sig_val, sig_hot;              /* last composed signatures */
    double last;                       /* time of the last value-driven recomposition */
} Cache;
static Cache s_c[C_COUNT];
static const UiLayout *L;
static int s_inited, s_sw, s_sh, s_restore;
static double s_now;
static UiStats s_stats;
static int s_nblit;                    /* blits + glyphs issued (counted by the draw helpers) */

static UiCmd s_cmd[CMD_QUEUE];
static int s_cmd_head, s_cmd_n;

/* input */
static int s_mx = -1, s_my = -1, s_down, s_hot, s_press_hot;
static double s_hot_t;                 /* when s_hot became the current hot */

/* tool / panels */
static int s_tool = TOOL_NONE, s_param;
static int s_fly = -1, s_fly_x, s_fly_y, s_fly_w, s_fly_h;
static int s_panel, s_tab[PANEL_COUNT];
static int s_q_lot, s_q_x, s_q_y;
static int s_win_x, s_win_y, s_win_w, s_win_h;
static double s_win_check;
static int s_ovl_local, s_ovl_seen, s_show_buildings = 1;
typedef struct { s16 x, y, w, h; u8 kind; s16 a, b; } Ctl;
enum { CK_CLOSE, CK_TAB, CK_SLIDER, CK_BUTTON, CK_CHECK, CK_OVL, CK_POL /* M1: ui_pol.cpp */ };
enum { BT_RESUME, BT_SAVE, BT_QUICKSAVE, BT_LOAD, BT_MENU, BT_QUIT, BT_CLOSE, BT_RIGHTS /* PL1: open the desk at the town-rights decree */ };
enum { CHK_BUILDINGS };
static Ctl s_ctl[MAX_CTL];
static int s_nctl;
static int s_drag_ctl = -1, s_drag_val, s_drag_mmap;
static int s_tax_res = -1, s_tax_bus = -1;   /* last values sent (shown until the budget follows) */

/* top bar hit rects (indexed by H_* - H_DATE) */
static s16 s_topr[7][4];          /* PL1: + Power */

/* side panel */
static int s_key_drawn[NKEYS], s_mini_drawn[NMINI];
static u32 s_plate_sig, s_plaque_sig, s_label_sig, s_mmap_sig;
static s16 s_side_dirty[SIDE_DIRTY_MAX][4];
static int s_nside_dirty;

/* president */
static int s_mood, s_mood_h, s_said_month = -1, s_auto_month = -1;
static s32 s_pop_prev = -1;

/* minimap */
static Img s_mm;
static int s_mm_ok, s_mm_w, s_mm_h, s_mm_ovl = -1, s_mm_ver, s_mm_force;
static u32 s_mm_serial;
static double s_mm_t = -10.0;
static int s_mm_ox, s_mm_oy;           /* minimap image pixel shown at the window's top-left */
static int s_cam_box[4];               /* bracket rect in window pixels */

/* ticker */
typedef struct { char text[TICK_TEXT]; int sev, x, y, panel, topic; double t; } TickItem;
static TickItem s_tick[TICK_MAX];
static int s_ntick;
static s16 s_tickr[TICK_MAX][4];
static int s_ntickr;
static u32 s_topic_month[SE_COUNT + SVC_COUNT + 4];

/* tooltip */
typedef struct { char l[64]; char r[28]; u8 fl, fr; } TipLine;
static TipLine s_tip[MAX_TIP];
static int s_ntip, s_tip_shown, s_tip_hot;
static u32 s_tip_sig;
static double s_tip_check;

/* floating money */
typedef struct { int x, y; Money v; double t; int on; } Float;
static Float s_float[FLOAT_MAX];

static int s_test;                     /* -test ui running */
static int s_zone_view;                /* F3: zone-view key lit (ui_set_zone_view) */
#define TOPIC_HINT (-2)                /* F3: TickItem.topic of an opening hint (click = no more hints) */

/* ================================================================== art helpers */
static const ArtSprite *SP(int id) { return art_get(ZOOM_64, id); }
static int spr_w(int id) { return SP(id)->w; }
static int spr_h(int id) { return SP(id)->h; }

static void spr_part(int id, int sx, int sy, int w, int h, int x, int y)
{
    const ArtSprite *s = SP(id);
    if (!s->img || w <= 0 || h <= 0) return;
    if (sx + w > s->w) w = s->w - sx;
    if (sy + h > s->h) h = s->h - sy;
    if (w <= 0 || h <= 0) return;
    gfx_blit_part(s->img, s->x + sx, s->y + sy, w, h, x, y);
    s_nblit++;
}

static void spr(int id, int x, int y) { const ArtSprite *s = SP(id); spr_part(id, 0, 0, s->w, s->h, x, y); }

static void spr_tile(int id, int x, int y, int w, int h)   /* tile from the sprite's origin, clip the last */
{
    const ArtSprite *s = SP(id);
    int xx, yy;
    if (s->w <= 0 || s->h <= 0) return;
    for (yy = 0; yy < h; yy += s->h)
        for (xx = 0; xx < w; xx += s->w)
            spr_part(id, 0, 0, IMIN(s->w, w - xx), IMIN(s->h, h - yy), x + xx, y + yy);
}

static void fill(int x0, int y0, int x1, int y1, PIX c) { gfx_fill(x0, y0, x1, y1, c); s_nblit++; }

static void slice9_init(Slice9 *s, const char *base)
{
    static const char *const suf[9] = { "tl", "t", "tr", "l", "c", "r", "bl", "b", "br" };
    int i;
    for (i = 0; i < 9; i++) s->p[i] = art_findf("%s_%s", base, suf[i]);
    s->l = spr_w(s->p[0]); s->t = spr_h(s->p[0]); s->r = spr_w(s->p[8]); s->b = spr_h(s->p[8]);
}

static void slice3_init(Slice3 *s, const char *base, int vert)
{
    s->vert = vert;
    s->p[0] = art_findf(vert ? "%s_t" : "%s_l", base);
    s->p[1] = art_findf("%s_m", base);
    s->p[2] = art_findf(vert ? "%s_b" : "%s_r", base);
    s->a = vert ? spr_h(s->p[0]) : spr_w(s->p[0]);
    s->b = vert ? spr_h(s->p[2]) : spr_w(s->p[2]);
}

/* fillc: colour for a flat centre (big panels: one fill instead of dozens of tiles), or -1 = tile */
static void draw9(const Slice9 *s, int x, int y, int w, int h, int fillc)
{
    int cw = w - s->l - s->r, ch = h - s->t - s->b;
    if (cw > 0 && ch > 0) {
        if (fillc >= 0) fill(x + s->l, y + s->t, x + s->l + cw, y + s->t + ch, (PIX)fillc);
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

static int icon_id(const char *name, int off)
{
    char buf[64];
    str_printf(buf, sizeof(buf), off ? "icon_%s_off" : "icon_%s", name);
    return art_exists(buf) ? art_find(buf) : art_find(off ? "icon_info_off" : "icon_info");
}

/* ================================================================== fonts and text */
static void font_build(int i)
{
    const ArtFont *af = art_font(s_font_names[i]);
    GfxFont *f = &s_font[i];
    int k, q;
    if (!af) {                          /* no art yet: widths only (layout + logs work), nothing drawn */
        gfx_font_init(f, NULL, 11, 9);
        for (k = 1; k < 256; k++) f->adv[k] = 5;
        return;
    }
    gfx_font_init(f, art_font_img(af), af->line_h, af->baseline);
    for (k = 0; k < af->count; k++) {
        const ArtGlyph *g = &af->glyphs[k];
        gfx_font_glyph(f, g->code, g->x, g->y, g->w, g->h, g->ox, g->oy, g->adv);
    }
    q = '?';                            /* codes without a glyph draw '?' (art/UI.md) */
    for (k = 1; k < 256; k++) {
        if (k == '\n' || f->adv[k] || f->gw[k]) continue;
        f->gx[k] = f->gx[q]; f->gy[k] = f->gy[q]; f->gw[k] = f->gw[q]; f->gh[k] = f->gh[q];
        f->ox[k] = f->ox[q]; f->oy[k] = f->oy[q]; f->adv[k] = f->adv[q];
    }
}

static int glyphs_in(const char *s) { int n = 0; for (; *s; s++) if (*s != ' ' && *s != '\n') n++; return n; }

static int txt(int x, int y, const char *s, int font)
{
    s_nblit += glyphs_in(s);
    return gfx_text(&s_font[font], x, y, s);
}
static int txtw(const char *s, int font) { return gfx_text_width(&s_font[font], s); }
static int txt_r(int xr, int y, const char *s, int font) { return txt(xr - txtw(s, font), y, s, font); }
static int txt_c(int x, int w, int y, const char *s, int font) { return txt(x + (w - txtw(s, font)) / 2, y, s, font); }

/* draw s cut to maxw with an ellipsis */
static int txt_fit(int x, int y, const char *s, int font, int maxw)
{
    char buf[256];
    int n;
    if (txtw(s, font) <= maxw) return txt(x, y, s, font);
    for (n = IMIN((int)strlen(s), 250); n > 0; n--) {
        memcpy(buf, s, n);
        buf[n] = (char)0x85; buf[n + 1] = 0;      /* ellipsis */
        if (txtw(buf, font) <= maxw) break;
    }
    if (n <= 0) return 0;
    return txt(x, y, buf, font);
}

/* word wrap into lines; returns the count */
static int wrap(const char *s, int font, int w, char out[][96], int maxl)
{
    char line[96], word[96];
    int n = 0, i, k;
    line[0] = 0;
    for (i = 0; ; ) {
        while (s[i] == ' ') i++;
        if (!s[i]) break;
        for (k = 0; s[i] && s[i] != ' ' && k < 94; k++, i++) word[k] = s[i];
        word[k] = 0;
        if (line[0]) {
            char t[200];
            str_printf(t, sizeof(t), "%s %s", line, word);
            if (txtw(t, font) <= w) { str_copy(line, t, sizeof(line)); continue; }
            if (n < maxl) str_copy(out[n], line, 96);
            n++;
        }
        str_copy(line, word, sizeof(line));
    }
    if (line[0]) { if (n < maxl) str_copy(out[n], line, 96); n++; }
    return n;
}

int ui_text(int x, int y, const char *s, int font) { return txt(x, y, s, ICLAMP(font, 0, FONT_COUNT - 1)); }
int ui_text_width(const char *s, int font) { return txtw(s, ICLAMP(font, 0, FONT_COUNT - 1)); }
int ui_font_height(int font) { return s_font[ICLAMP(font, 0, FONT_COUNT - 1)].line_h; }
const GfxFont *ui_font(int font) { return &s_font[ICLAMP(font, 0, FONT_COUNT - 1)]; }

static PIX col(int r, int g, int b) { return gfx_rgb(r, g, b); }
#define COL(c) col(c)

/* ================================================================== caches */
static int cache_make(Cache *c, int w, int h, int keyed)
{
    memset(&c->img, 0, sizeof(c->img));
    c->w = w; c->h = h; c->dirty = 1; c->ok = 0; c->failed = 0;
    if (!gfx_make_target(w, h, &c->img)) {
        c->failed = 1;
        log_printf("ui: warning: no VRAM for a %dx%d cache: drawing that part directly every frame", w, h);
        return 0;
    }
    if (keyed) gfx_img_set_keyed(&c->img, 1);
    c->ok = 1;
    s_stats.vram_kb += (w * h * 2 + 1023) / 1024;
    return 1;
}

/* start composing cache c (screen coordinates); clip = screen rect or NULL for the whole cache */
static double s_comp_t0;
static void begin_compose(Cache *c, int keyed, const s16 *clip)
{
    s_comp_t0 = time_ms();
    if (c->ok) {
        gfx_set_target(&c->img);
        gfx_set_origin(c->x, c->y);
        if (clip) gfx_set_clip(clip[0] - c->x, clip[1] - c->y, clip[0] + clip[2] - c->x, clip[1] + clip[3] - c->y);
        if (keyed) {
            if (clip) fill(clip[0], clip[1], clip[0] + clip[2], clip[1] + clip[3], gfx_key());
            else gfx_clear(gfx_key());
        }
    } else {
        gfx_set_target(NULL);
        if (clip) gfx_set_clip(clip[0], clip[1], clip[0] + clip[2], clip[1] + clip[3]);
    }
}

static void end_compose(int ci)
{
    gfx_set_target(NULL);
    s_stats.recompose[ci]++;
    s_stats.recompose_ms[ci] += time_ms() - s_comp_t0;
}

static u32 hash_ints(const int *v, int n) { return hash_bytes(HASH_INIT, v, n * (int)sizeof(int)); }

/* ================================================================== commands */
static void cmd(int type, int a, int b)
{
    UiCmd *c;
    if (s_cmd_n >= CMD_QUEUE) { log_printf("ui: warning: command queue full, dropped %d", type); return; }
    c = &s_cmd[(s_cmd_head + s_cmd_n) % CMD_QUEUE];
    c->type = type; c->a = a; c->b = b;
    s_cmd_n++;
}

int ui_poll_cmd(UiCmd *c)
{
    if (!s_cmd_n) return 0;
    *c = s_cmd[s_cmd_head];
    s_cmd_head = (s_cmd_head + 1) % CMD_QUEUE;
    s_cmd_n--;
    return 1;
}

/* ================================================================== game state helpers */
static int cur_year(void) { return sim_date()->year; }
static int cur_status(void) { return sim_stats()->town_status; }
static int all_unlocked(void) { return sim_settings()->all_unlocked; }

/* 1 = the entry can't be used now; hint gets "Market town" / "1965" / "Coming later" */
static int ent_locked(const FlyEnt *e, char *hint, int hsize)
{
    int y = cur_year(), st = cur_status(), all = all_unlocked();
    if (hsize > 0) hint[0] = 0;
    switch (e->tool) {
    case TOOL_ROAD:
        if (def_road_unlocked(e->param, y, st, all)) return 0;
        text_unlock_hint(hint, hsize, g_roads[e->param].status_req, g_roads[e->param].era_year, y, st);
        break;
    case TOOL_ZONE:
        if (def_zone_unlocked(e->param, y, st, all)) return 0;
        if (e->param == ZONE_APARTMENTS) text_unlock_hint(hint, hsize, TS_CHURCH_VILLAGE, 1955, y, st);
        break;
    case TOOL_GIFT:                                    /* M6: the patron's gift waiting to be placed */
        if (complex_gift_pending()) return 0;
        str_copy(hint, "None waiting", hsize);
        return 1;
    case TOOL_BUILD: {
        const BuildingDef *d = def_building(e->param);
        if (d->flags & BDF_MODULE) return complex_tool_locked(e->param, hint, hsize);   /* M6 */
        if (e->param == B_STATION && !all && !complex_map_has_rail()) { str_copy(hint, "No railway here", hsize); return 1; }   /* PL2 */
        if (def_unlocked(e->param, y, st, all)) {
            if (d->variants > 0) return 0;
            str_copy(hint, "Coming later", hsize);
            return 1;
        }
        text_unlock_hint(hint, hsize, d->status_req, d->era_year, y, st);
        break; }
    default:
        return 0;
    }
    if (!hint[0]) str_copy(hint, "Later", hsize);
    return 1;
}

static void ent_cost(const FlyEnt *e, char *buf, int size)
{
    char m[32];
    int y = cur_year();
    buf[0] = 0;
    switch (e->tool) {
    case TOOL_ROAD:       text_fmt_money(m, sizeof(m), g_roads[e->param].cost, y); str_printf(buf, size, "%s/tile", m); break;
    case TOOL_POWER_LINE: text_fmt_money(m, sizeof(m), g_misc.power_line, y); str_printf(buf, size, "%s/tile", m); break;
    case TOOL_PIPE: {     /* F1: "3 mk/tile (15 under water)" */
        char m2[32];
        text_fmt_money(m, sizeof(m), g_misc.pipe, y); text_fmt_money(m2, sizeof(m2), g_misc.pipe_water, y);
        str_printf(buf, size, "%s/tile (%s under water)", m, m2);
        break; }
    case TOOL_ZONE:
        if (g_misc.zone_tile) { text_fmt_money(m, sizeof(m), g_misc.zone_tile, y); str_printf(buf, size, "%s/tile", m); }
        else str_copy(buf, "free", size);
        break;
    case TOOL_BUILD:      text_fmt_money(buf, size, complex_tool_cost(e->param), y); break;   /* M6: = def cost except complexes */
    case TOOL_GIFT:       str_copy(buf, "free", size); break;                                /* M6 */
    case TOOL_BULLDOZE:   text_fmt_money(m, sizeof(m), g_misc.bulldoze_tile, y); str_printf(buf, size, "%s/tile", m); break;
    case TOOL_BUS_LINE: { /* M2: "300 mk + 40 mk/stop" */
        char m2[32];
        text_fmt_money(m, sizeof(m), BUS_LINE_COST, y); text_fmt_money(m2, sizeof(m2), BUS_STOP_COST, y);
        str_printf(buf, size, "%s + %s/stop", m, m2);
        break; }
    }
}

static int key_has_tool(int k, int tool, int param)
{
    int i;
    for (i = 0; i < s_keys[k].n; i++) {
        const FlyEnt *e = &s_keys[k].e[i];
        if (e->tool == tool && (e->param == param || tool == TOOL_POWER_LINE || tool == TOOL_PIPE || tool == TOOL_DEZONE ||
                                tool == TOOL_BULLDOZE || tool == TOOL_QUERY)) return 1;
    }
    return 0;
}

static int key_all_locked(int k)
{
    char h[32];
    int i;
    for (i = 0; i < s_keys[k].n; i++) if (!ent_locked(&s_keys[k].e[i], h, sizeof(h))) return 0;
    return 1;
}

/* ================================================================== geometry */
static void key_rect(int k, int *x, int *y, int *w, int *h)
{
    *w = L->keys.w; *h = L->keys.h;
    *x = L->keys.x + (k % L->key_cols) * (L->keys.w + L->key_gap_x);
    *y = L->keys.y + (k / L->key_cols) * (L->keys.h + L->key_gap_y);
}

static void mini_rect(int i, int *x, int *y, int *w, int *h)
{
    *w = L->mini.w; *h = L->mini.h;
    *x = L->mini.x + i * (L->mini.w + L->mini_gap); *y = L->mini.y;
}

static void speed_rect(int i, int *x, int *y, int *w, int *h)
{
    *w = L->speed.w; *h = L->speed.h;
    *x = L->speed.x + i * (L->speed.w + L->speed_gap); *y = L->speed.y;
}

static int in_rect(int px, int py, int x, int y, int w, int h) { return px >= x && py >= y && px < x + w && py < y + h; }

/* F3: zoom keys (UiLayout.zoom_key: zoom out, then zoom in at +w+speed_gap; UI.md top bar) */
static void zoom_rect(int i, int *x, int *y, int *w, int *h)
{
    *w = L->zoom_key.w; *h = L->zoom_key.h;
    *x = L->zoom_key.x + i * (L->zoom_key.w + L->speed_gap); *y = L->zoom_key.y;
}
static int zoom_disabled(int i) { return i == 0 ? (g_cam.zoom == ZOOM_32 || !art_zoom_available(ZOOM_32)) : (g_cam.zoom == ZOOM_64 || !art_zoom_available(ZOOM_64)); }
static int top_limit(void) { return L->zoom_key.w > 0 ? L->zoom_key.x - 6 : L->speed.x - 6; }

/* F3: zone-view key: a mini key at the top of the grille slack (a second row under the mini keys) */
static void zonev_rect(int *x, int *y, int *w, int *h)
{
    *w = L->mini.w; *h = L->mini.h;
    *x = L->mini.x; *y = L->grille.y + 1;
}
static int grille_top(void) { return L->grille.y + 2 * L->mini.h + 6; }   /* PL1: below the Guide key row */

void ui_map_rect(int *x, int *y, int *w, int *h)
{
    if (!L) { *x = 0; *y = 0; *w = plat_screen_w(); *h = plat_screen_h(); return; }
    *x = L->map.x; *y = L->map.y; *w = L->map.w; *h = L->map.h;
}

/* PL1 (PLAYTEST 2 items 1, 2c, 4): the fly-out footer for entry i: what unlocks it ("Unlocks at Market town (3,000
   residents) and 1962") and what it is for ("Lets apartment blocks grow to tier 3"); returns the lines (<= max) */
static int fly_foot_lines(int k, int i, char lines[][96], int max)
{
    const FlyEnt *e = &s_keys[k].e[i];
    char hint[48];
    const char *pur = e->tool == TOOL_BUILD ? advice_purpose(e->param) : NULL;
    int n = 0, lk = ent_locked(e, hint, sizeof(hint));
    if (lk && n < max) {
        switch (e->tool) {
        case TOOL_ROAD: advice_unlock_text(lines[n], 96, g_roads[e->param].status_req, g_roads[e->param].era_year); break;
        case TOOL_ZONE: advice_unlock_text(lines[n], 96, TS_CHURCH_VILLAGE, 1955); break;
        case TOOL_GIFT: str_copy(lines[n], "The patron's gift waits here when he sends one", 96); break;
        case TOOL_BUILD: {
            const BuildingDef *d = def_building(e->param);
            if (d->flags & BDF_MODULE) str_printf(lines[n], 96, "Needs: %s", hint);
            else if (!strcmp(hint, "Coming later")) str_copy(lines[n], "Not in this version yet", 96);
            else advice_unlock_text(lines[n], 96, d->status_req, d->era_year);
            break; }
        default: str_copy(lines[n], hint, 96); break;
        }
        if (lines[n][0]) n++;
    }
    if (pur && n < max) str_copy(lines[n++], pur, 96);
    if (!lk && e->tool == TOOL_BUILD && (def_building(e->param)->flags & BDF_MODULE) && n < max)   /* PL3 */
        str_copy(lines[n++], "Cannot be undone once placed", 96);
    return n;
}
static int s_fly_nfoot = 1;              /* footer lines of the open fly-out (fly_layout: the most any entry needs) */
static int fly_foot_n(int k) { return s_keys[k].n > 10 ? 1 : 2; }

/* fly-out rect for key k (also sets s_fly_*); content width from the rows */
static void fly_layout(int k)
{
    char cost[48], hint[48], fl[2][96];
    int i, j, wn = 0, wc = 0, wf = 0, kx, ky, kw, kh, nf, nmax = 1;
    for (i = 0; i < s_keys[k].n; i++) {
        const FlyEnt *e = &s_keys[k].e[i];
        int lk = ent_locked(e, hint, sizeof(hint));
        wn = IMAX(wn, txtw(text_tool_name(e->tool, e->param), FONT_BODY_DARK));
        if (!lk) ent_cost(e, cost, sizeof(cost));
        wc = IMAX(wc, txtw(lk ? hint : cost, FONT_BODY_DARK));
        nf = fly_foot_lines(k, i, fl, fly_foot_n(k));                 /* PL1 */
        nmax = IMAX(nmax, nf);
        for (j = 0; j < nf; j++) wf = IMAX(wf, txtw(fl[j], FONT_BODY_GREY));
    }
    s_fly_w = ICLAMP(IMAX(8 + 13 + 4 + wn + 12 + wc + 8, wf + 16), 150, POP_W - 8);
    s_fly_nfoot = nmax;
    s_fly_h = 6 + s_keys[k].n * 17 + 6 + nmax * 11 + 2;              /* PL1: footer */
    key_rect(k, &kx, &ky, &kw, &kh);
    s_fly_x = L->panel.x - 4 - s_fly_w;
    s_fly_y = ICLAMP(ky - 20, L->top.y + L->top.h + 2, s_sh - s_fly_h - 2);
}

/* ================================================================== minimap */
/* render_minimap draws the whole map as a 45-degree diamond filling the image (T05), so the image is the
   minimap window size: 128x128 at 800/1024, 88x88 at 640 (scaled, no camera-following window needed) */
static int s_mm_row = -1;              /* T13: next row of an incremental rebuild, -1 = none running */
#define MM_ROWS_PER_FRAME 8              /* ~0.1 ms per 128-px row on the P2: ~0.8 ms a frame, 16 frames */

static void mm_update(int force)
{
    int w = L->mmap.w, h = L->mmap.h, ovl = s_ovl_local;
    if (g_world.w <= 0 || g_world.h <= 0) return;
    if (s_mm_ok && (s_mm_w != w || s_mm_h != h)) { gfx_free_img(&s_mm); s_mm_ok = 0; }
    if (!s_mm_ok) {                          /* F3: normally made in caches_create (before the chunk pool) */
        static double fail_t = -100.0;
        if (s_now - fail_t < 5.0) return;    /* retry every 5 s, log once per try */
        if (!gfx_make_img(w, h, 0, &s_mm)) { fail_t = s_now; log_printf("ui: warning: no VRAM for the minimap (%d KB free)", gfx_vram_free_kb()); return; }
        s_mm_ok = 1; s_mm_w = w; s_mm_h = h; force = 1;
        s_stats.vram_kb += (w * h * 2 + 1023) / 1024;
        log_printf("ui: minimap image %dx%d created late", w, h);
    }
    if (force || ovl != s_mm_ovl) {          /* first build, overlay switch, restore: whole image now */
        render_minimap(s_mm.px, s_mm.w, w, h, ovl);
        gfx_img_update(&s_mm);
        s_mm_serial = g_world.serial; s_mm_ovl = ovl; s_mm_t = s_now; s_mm_ver++;
        s_mm_row = -1;
        return;
    }
    /* T13: world edits rebuild the image a few rows per frame (a full rebuild cost 13-24 ms in one frame);
       the VRAM copy keeps the old picture until the last row is done, then one upload. ui_update runs at
       frame start (before any blit), so the upload doesn't wait behind queued blits (T05). */
    if (s_mm_row < 0) {
        static int mm_season;                /* 1.1 W2: the art season changed: snow colours, a few rows per frame */
        if (g_world.serial == s_mm_serial && mm_season == art_season()) return;
        if (mm_season == art_season() && s_now - s_mm_t < MM_INTERVAL) return;   /* world edits: at most every 3 s */
        s_mm_row = 0;
        s_mm_serial = g_world.serial;
        mm_season = art_season();
    }
    render_minimap_rows(s_mm.px, s_mm.w, w, h, ovl, s_mm_row, s_mm_row + MM_ROWS_PER_FRAME);
    s_mm_row += MM_ROWS_PER_FRAME;
    if (s_mm_row >= h) {
        gfx_img_update(&s_mm);
        s_mm_t = s_now; s_mm_ver++;
        s_mm_row = -1;
    }
}

/* minimap window origin + camera bracket box (window pixels) for the current camera */
static void mm_view(void)
{
    int mw = L->mmap.w, mh = L->mmap.h, i, tx, ty, px, py, x0 = 1 << 20, y0 = 1 << 20, x1 = -(1 << 20), y1 = -(1 << 20);
    int cxs[4], cys[4];
    s_mm_ox = s_mm_oy = 0;
    if (g_world.w <= 0) { s_cam_box[0] = s_cam_box[1] = s_cam_box[2] = s_cam_box[3] = 0; return; }
    cxs[0] = cxs[2] = g_cam.x; cxs[1] = cxs[3] = g_cam.x + g_cam.view_w;
    cys[0] = cys[1] = g_cam.y; cys[2] = cys[3] = g_cam.y + g_cam.view_h;
    for (i = 0; i < 4; i++) {             /* the view's corners on the (diamond) minimap */
        iso_world_to_tile(g_cam.zoom, cxs[i], cys[i], &tx, &ty);
        render_minimap_pos(mw, mh, tx, ty, &px, &py);
        x0 = IMIN(x0, px); y0 = IMIN(y0, py); x1 = IMAX(x1, px); y1 = IMAX(y1, py);
    }
    s_cam_box[0] = ICLAMP(x0, 0, mw - 6);
    s_cam_box[1] = ICLAMP(y0, 0, mh - 6);
    s_cam_box[2] = ICLAMP(x1, s_cam_box[0] + 6, mw);
    s_cam_box[3] = ICLAMP(y1, s_cam_box[1] + 6, mh);
}

/* ================================================================== top bar */
static int dem_level(int dem)
{
    int v = sim_demand(dem)->value;
    int l = (v >= 0 ? v + 10 : v - 10) / 20;      /* -100..100 -> -5..5, rounded */
    return ICLAMP(l, -5, 5);
}

static void top_vals(int *v)            /* everything the top bar shows (value signature) */
{
    const SimDate *d = sim_date();
    v[0] = d->year * 12 + d->month; v[1] = g_budget.money; v[2] = budget_net_last();
    v[3] = sim_stats()->population; v[4] = dem_level(DEM_R); v[5] = dem_level(DEM_C); v[6] = dem_level(DEM_I);
    v[7] = sim_speed(); v[8] = g_budget.unlimited;
    v[9] = net_stats(NET_WATER)->used; v[10] = net_stats(NET_WATER)->supply; v[11] = net_stats(NET_WATER)->lots_short;  /* F1 */
    v[12] = politics_enabled() ? politics_popularity() * 2 + (politics_pending_event() >= 0) : -1;   /* M1 */
    v[13] = g_cam.zoom;                                                                               /* F3 */
    v[14] = net_stats(NET_POWER)->demand; v[15] = net_stats(NET_POWER)->supply * 2 + (net_stats(NET_POWER)->lots_short > 0);  /* PL1 */
    v[16] = advice_rights_waiting();
}

static int top_hotstate(void)
{
    int h = (s_hot >= H_TOP && s_hot < H_SIDE) ? s_hot : 0;
    return h * 4 + (s_down && s_press_hot == h ? 1 : 0) + (s_panel == PANEL_SETTINGS ? 2 : 0);
}

static void top_field_rect(int idx, int x, int w)
{
    s_topr[idx][0] = (s16)x; s_topr[idx][1] = 0; s_topr[idx][2] = (s16)w; s_topr[idx][3] = 16;
}

static void compose_top(void)
{
    char date[32], val[32], dl[32];
    const CityStats *cs = sim_stats();
    int x = 4, w, i, year = cur_year(), limit = top_limit(), sx, sy, sw, sh, sp = sim_speed();
    Money net = budget_net_last();
    static const int dem_ids[3] = { DEM_R, DEM_C, DEM_I };
    begin_compose(&s_c[C_TOP], 0, NULL);
    memset(s_topr, 0, sizeof(s_topr));
    spr_tile(S.bar_tile, 0, 0, s_sw, 16);
    /* date */
    text_fmt_date(date, sizeof(date), year, sim_date()->month);
    w = txtw(date, FONT_BODY_DARK) + 10;
    draw3h(&s3_cal, x, 2, w);
    txt(x + 6, 2, date, FONT_BODY_DARK);
    top_field_rect(0, x, w);
    x += w + 6;
    /* funds */
    spr(S.bar_sep, x, 2); x += 6;
    i = x;
    txt(x, 3, "Funds", FONT_BODY_BRASS); x += txtw("Funds", FONT_BODY_BRASS) + 4;
    if (g_budget.unlimited) str_copy(val, "\x97", sizeof(val));
    else text_fmt_money_short(val, sizeof(val), g_budget.money, year);
    w = txtw(val, FONT_BODY_LIGHT) + 8;
    draw3h(&s3_well, x, 2, w);
    txt(x + 4, 2, val, g_budget.money < 0 ? FONT_BODY_RED_LT : FONT_BODY_LIGHT);
    x += w + 3;
    if (!g_budget.unlimited && net != 0) {
        text_fmt_money_signed(dl, sizeof(dl), net, year);
        x += txt(x, 3, dl, net > 0 ? FONT_BODY_GREEN_LT : FONT_BODY_RED_LT) + 3;
    }
    top_field_rect(1, i, x - i);
    x += 4;
    /* population */
    spr(S.bar_sep, x, 2); x += 6;
    i = x;
    txt(x, 3, L->scr_w >= 800 ? "Population" : "Pop.", FONT_BODY_BRASS);
    x += txtw(L->scr_w >= 800 ? "Population" : "Pop.", FONT_BODY_BRASS) + 4;
    text_fmt_int(val, sizeof(val), cs->population);
    w = txtw(val, FONT_BODY_LIGHT) + 8;
    draw3h(&s3_well, x, 2, w);
    txt(x + 4, 2, val, FONT_BODY_LIGHT);
    x += w + 3;
    top_field_rect(2, i, x - i);
    x += 4;
    /* demand */
    if (x + 6 + 40 + 19 < limit) {
        const char *lab = L->scr_w >= 800 ? "Demand" : "RCI";      /* PL1: room for Power at 640 */
        spr(S.bar_sep, x, 2); x += 6;
        i = x;
        txt(x, 3, lab, FONT_BODY_BRASS); x += txtw(lab, FONT_BODY_BRASS) + 4;
        spr(S.dem_well, x, 1);
        for (w = 0; w < 3; w++) {
            int v = dem_level(dem_ids[w]);
            if (v > 0) spr_part(S.dem_bar[w], 0, 0, 4, v, x + 2 + w * 5, 1 + 6 - v);
            else if (v < 0) spr_part(S.dem_bar[w], 0, 0, 4, -v, x + 2 + w * 5, 1 + 7);
        }
        x += 19 + 4;
        top_field_rect(3, i, x - i);
        x += 2;
    }
    /* PL1 (PLAYTEST 2 item 3): power demand / supply over all networks, like Water (tooltip "Power 1,180 / 1,500 (79%)") */
    {
        const NetStats *pn = net_stats(NET_POWER);
        int pct = pn->supply > 0 ? (int)((s64)pn->demand * 100 / pn->supply) : 0;
        int fnt = pn->lots_short > 0 ? FONT_BODY_RED_LT : pct >= 90 ? FONT_BODY_AMBER : FONT_BODY_LIGHT;
        const char *lab = L->scr_w >= 800 ? "Power" : "Pow.";
        str_printf(val, sizeof(val), "%d%%", pct);
        if (x + 6 + txtw(lab, FONT_BODY_BRASS) + 4 + txtw(val, fnt) + 8 + 6 < limit) {
            spr(S.bar_sep, x, 2); x += 6;
            i = x;
            x += txt(x, 3, lab, FONT_BODY_BRASS) + 4;
            w = txtw(val, fnt) + 8;
            draw3h(&s3_well, x, 2, w);
            txt(x + 4, 2, val, fnt);
            x += w + 3;
            top_field_rect(6, i, x - i);
            x += 2;
        }
    }
    /* F1: water used / supplied over all networks (tooltip: "Water 3,400 / 4,000 (85%)") */
    {
        const NetStats *wn = net_stats(NET_WATER);
        int pct = wn->supply > 0 ? (int)((s64)wn->used * 100 / wn->supply) : 0;
        int fnt = wn->lots_short > 0 ? FONT_BODY_RED_LT : pct >= 80 ? FONT_BODY_AMBER : FONT_BODY_LIGHT;
        const char *lab = L->scr_w >= 800 ? "Water" : "Wat.";      /* PL1: short labels at 640 (room for Power) */
        str_printf(val, sizeof(val), "%d%%", pct);
        if (x + 6 + txtw(lab, FONT_BODY_BRASS) + 4 + txtw(val, fnt) + 8 + 6 < limit) {
            spr(S.bar_sep, x, 2); x += 6;
            i = x;
            x += txt(x, 3, lab, FONT_BODY_BRASS) + 4;
            w = txtw(val, fnt) + 8;
            draw3h(&s3_well, x, 2, w);
            txt(x + 4, 2, val, fnt);
            x += w + 3;
            top_field_rect(5, i, x - i);
            x += 2;
        }
    }
    /* approval (M1: popularity; hidden when politics is off) */
    if (politics_enabled() && x + 6 + txtw(L->scr_w >= 800 ? "Approval" : "Appr.", FONT_BODY_BRASS) + 30 < limit) {
        int pop = politics_popularity();
        spr(S.bar_sep, x, 2); x += 6;
        i = x;
        x += txt(x, 3, L->scr_w >= 800 ? "Approval" : "Appr.", FONT_BODY_BRASS) + 4;
        str_printf(val, sizeof(val), "%d%%", pop);
        w = txtw(val, FONT_BODY_LIGHT) + 8;
        draw3h(&s3_well, x, 2, w);
        txt(x + 4, 2, val, pop >= 60 ? FONT_BODY_GREEN_LT : pop < 40 ? FONT_BODY_RED_LT : FONT_BODY_LIGHT);
        x += w + 3;
        if (politics_pending_event() >= 0 || advice_rights_waiting()) x += txt(x, 3, "\xa7", FONT_BODY_RED_LT) + 3;   /* PL1: + town rights */
        top_field_rect(4, i, x - i);
    }
    /* speed keys + menu */
    for (i = 0; i < 4; i++) {
        int st = (sp == i) ? 2 : (s_hot == H_SPEED0 + i ? 1 : 0);
        speed_rect(i, &sx, &sy, &sw, &sh);
        spr(S.speed[i][st], sx, sy);
    }
    i = (s_hot == H_MENU && s_down) || s_panel == PANEL_SETTINGS ? 2 : (s_hot == H_MENU ? 1 : 0);
    spr(S.menu[i], L->menu_key.x, L->menu_key.y);
    for (i = 0; i < 2 && L->zoom_key.w > 0; i++) {       /* F3: zoom out / zoom in */
        int st = zoom_disabled(i) ? 3 : (s_hot == H_ZOOM0 + i ? (s_down ? 2 : 1) : 0);
        zoom_rect(i, &sx, &sy, &sw, &sh);
        spr(S.zoom[i][st], sx, sy);
    }
    end_compose(C_TOP);
}

/* ================================================================== side panel */
static int key_state(int k)
{
    if (key_all_locked(k)) return BS_DISABLED;
    if (s_down && s_press_hot == H_KEY0 + k && s_hot == s_press_hot) return BS_PRESSED;
    if (s_fly == k || key_has_tool(k, s_tool, s_param)) return BS_SELECTED;
    if (s_hot == H_KEY0 + k) return BS_HOVER;
    return BS_NORMAL;
}

/* M7: graphs / trade / newspaper keys in the zone-view row (mini positions 1..3) */
static void m7k_rect(int k, int *x, int *y, int *w, int *h)
{
    *w = L->mini.w; *h = L->mini.h;
    *x = L->mini.x + (k + 1) * (L->mini.w + L->mini_gap); *y = L->grille.y + 1;
}
static int m7k_state(int k)
{
    static const int pn[4] = { PANEL_GRAPHS, PANEL_TRADE, PANEL_NONE, PANEL_SERVICES };   /* PL1: [3] Services */
    if (s_down && s_press_hot == H_M7K0 + k && s_hot == s_press_hot) return BS_PRESSED;
    if (k != 2 && s_panel == pn[k]) return BS_SELECTED;
    if (k == 2 && uinews_active()) return BS_SELECTED;
    if (s_hot == H_M7K0 + k) return BS_HOVER;
    return BS_NORMAL;
}
static int s_m7k_drawn[4] = { -1, -1, -1, -1 };
#define NM7K 4                           /* PL1: graphs, trade, newspaper, services */

/* PL1: the wide Guide key: a third row under the zone-view / M7 row, as wide as the mini keys */
static void guide_rect(int *x, int *y, int *w, int *h)
{
    *w = L->mini_count * L->mini.w + (L->mini_count - 1) * L->mini_gap; *h = L->mini.h;
    *x = L->mini.x; *y = L->grille.y + 1 + L->mini.h + 2;
}
static int guide_state(void)
{
    if (s_down && s_press_hot == H_GUIDE && s_hot == s_press_hot) return BS_PRESSED;
    if (uiguide_active()) return BS_SELECTED;
    if (s_hot == H_GUIDE) return BS_HOVER;
    return BS_NORMAL;
}
static int s_guide_drawn = -1;

static int zonev_state(void)             /* F3 */
{
    if (s_down && s_press_hot == H_ZONEV && s_hot == s_press_hot) return BS_PRESSED;
    if (s_zone_view) return BS_SELECTED;
    if (s_hot == H_ZONEV) return BS_HOVER;
    return BS_NORMAL;
}
static int s_zonev_drawn = -1;

static int mini_state(int i)
{
    static const int panels[3] = { PANEL_BUDGET, PANEL_POPULATION, PANEL_OVERLAYS };
    if (i == 3 && !tools_can_undo()) return BS_DISABLED;
    if (i == 4 && !tools_can_redo()) return BS_DISABLED;
    if (s_down && s_press_hot == H_MINI0 + i && s_hot == s_press_hot) return BS_PRESSED;
    if (i < 3 && s_panel == panels[i]) return BS_SELECTED;
    if (s_hot == H_MINI0 + i) return BS_HOVER;
    return BS_NORMAL;
}

static void draw_key(int x, int y, int w, int h, const int *icon, const char *label, int st)
{
    int o = (st == BS_PRESSED || st == BS_SELECTED) ? 1 : 0, off = st == BS_DISABLED;
    draw9(&s9_btn[st], x, y, w, h, -1);
    if (label) {
        spr(icon[off], x + (w - 1 - 13) / 2 + o, y + 2 + (h - 24) / 3 + o);
        txt_c(x + o, w - 1, y + h - 13 + o, label, off ? FONT_BODY_GREY : FONT_BODY_DARK);
    } else spr(icon[off], x + (w - 1 - 13) / 2 + o, y + (h - 1 - 13) / 2 + o);
}

static void tool_label_text(char *buf, int size)
{
    char hint[48];
    int k;
    if (s_fly >= 0 && s_hot >= H_FLYROW0 && s_hot < H_FLYROW0 + s_keys[s_fly].n &&
        ent_locked(&s_keys[s_fly].e[s_hot - H_FLYROW0], hint, sizeof(hint))) {   /* hovering a locked row */
        if (strcmp(hint, "Coming later")) str_printf(buf, size, "Unlocks at: %s", hint);
        else str_copy(buf, "Not in this version yet", size);
        return;
    }
    if (s_tool == TOOL_NONE) { str_copy(buf, "", size); return; }
    for (k = 0; k < NKEYS; k++) if (key_has_tool(k, s_tool, s_param)) break;
    if (k < NKEYS && s_keys[k].n > 1)
        str_printf(buf, size, "%s: %s", s_keys[k].label, text_tool_name(s_tool, s_param));
    else str_copy(buf, text_tool_name(s_tool, s_param), size);
}

static void quote_text(char *buf, int size, int maxw, int maxl)
{
    char line[96], t[112], ls[4][96];
    int tries;
    for (tries = 0; tries < 8; tries++) {     /* find a line of this mood that fits the plaque */
        text_president_line(line, sizeof(line), s_mood, (u32)(s_mood_h + tries));
        str_printf(t, sizeof(t), TEXT_QUOTE "%s" TEXT_QUOTE, line);
        if (wrap(t, FONT_BODY_RED, maxw, ls, 4) <= maxl) break;
    }
    str_copy(buf, t, size);
}

/* compose the whole side panel, clipped to clip (screen rect) or everything */
static void compose_side(const s16 *clip)
{
    char buf[128], lines[4][96];
    const SimSettings *ss = sim_settings();
    int i, x, y, w, h, n, big = L->portrait_large, ns, bs;
    begin_compose(&s_c[C_SIDE], 0, clip);
    /* teak + edge */
    spr_tile(S.teak, L->panel.x, L->panel.y, L->panel.w, L->panel.h);
    spr_tile(S.panel_edge, L->panel.x, L->panel.y, spr_w(S.panel_edge), L->panel.h);
    /* name plate */
    draw3h(&s3_plate, L->plate.x, L->plate.y, L->plate.w);
    str_copy(buf, ss->town[0] ? ss->town : "Kaupunki", sizeof(buf));
    for (i = 0; buf[i]; i++) {                  /* engraved capitals */
        u8 c = (u8)buf[i];
        if (c >= 'a' && c <= 'z') buf[i] = (char)(c - 32);
        else if (c == 0xe4 || c == 0xf6 || c == 0xe5) buf[i] = (char)(c - 32);
    }
    if (txtw(buf, FONT_HEAD) <= L->plate.w - 16) txt_c(L->plate.x, L->plate.w, L->plate.y + 1, buf, FONT_HEAD);
    else txt_c(L->plate.x, L->plate.w, L->plate.y + 3, buf, FONT_BODY_DARK);
    /* tool keys */
    for (i = 0; i < NKEYS; i++) {
        key_rect(i, &x, &y, &w, &h);
        s_key_drawn[i] = key_state(i);
        draw_key(x, y, w, h, s_key_icon[i], s_keys[i].label, s_key_drawn[i]);
    }
    tool_label_text(buf, sizeof(buf));
    txt_fit(L->tool_label.x + 1, L->tool_label.y, buf, FONT_BODY_SHADOW, L->tool_label.w - 2);
    /* president plaque */
    draw9(&s9_plaque, L->plaque.x, L->plaque.y, L->plaque.w, L->plaque.h, -1);
    spr(S.pres_frame[big], L->portrait_frame.x, L->portrait_frame.y);
    spr(S.pres[big][ICLAMP(s_mood, 0, PRES_MOOD_COUNT - 1)], L->portrait.x, L->portrait.y);
    txt_fit(L->pres_name.x, L->pres_name.y, "President", FONT_BODY_DARK, L->pres_name.w);
    txt_fit(L->pres_name.x, L->pres_name.y + 10, ss->president[0] ? ss->president : "Kekkola", FONT_BODY_DARK, L->pres_name.w);
    quote_text(buf, sizeof(buf), L->quote.w, L->quote.h / 10);
    n = wrap(buf, FONT_BODY_RED, L->quote.w, lines, 4);
    for (i = 0; i < n && i < L->quote.h / 10; i++) txt_fit(L->quote.x, L->quote.y + i * 10, lines[i], FONT_BODY_RED, L->quote.w);
    /* mini keys */
    for (i = 0; i < NMINI && i < L->mini_count; i++) {
        mini_rect(i, &x, &y, &w, &h);
        s_mini_drawn[i] = mini_state(i);
        draw_key(x, y, w, h, s_mini_ic[i], NULL, s_mini_drawn[i]);
    }
    /* F3: zone-view key (top of the grille slack) */
    zonev_rect(&x, &y, &w, &h);
    s_zonev_drawn = zonev_state();
    draw_key(x, y, w, h, S.zonev, NULL, s_zonev_drawn);
    for (i = 0; i < NM7K && i + 1 < L->mini_count; i++) {       /* M7 (+ PL1 Services) */
        m7k_rect(i, &x, &y, &w, &h);
        s_m7k_drawn[i] = m7k_state(i);
        draw_key(x, y, w, h, S.m7k[i], NULL, s_m7k_drawn[i]);
    }
    {   /* PL1: Guide key (icon + "Guide") */
        int st, o, tw;
        guide_rect(&x, &y, &w, &h);
        st = s_guide_drawn = guide_state();
        o = (st == BS_PRESSED || st == BS_SELECTED) ? 1 : 0;
        draw9(&s9_btn[st], x, y, w, h, -1);
        tw = 13 + 4 + txtw("Guide", FONT_BODY_DARK);
        spr(S.guide[0], x + (w - tw) / 2 + o, y + (h - 1 - 13) / 2 + o);
        txt(x + (w - tw) / 2 + 17 + o, y + (h - 11) / 2 + o, "Guide", FONT_BODY_DARK);
    }
    /* grille (radio speaker slots) + badge */
    ns = (L->grille.y + L->grille.h - grille_top() - 4 + 3) / 8;
    bs = ns / 2;
    for (i = 0, y = grille_top() + 2; i < ns; i++, y += 8)
        if (i != bs || ns < 3) draw3h(&s3_grille, L->grille.x + 6, y, L->grille.w - 12);
    if (ns >= 3)                  /* R1: badge on top, centred on its slot (13 tall: touches the neighbours) */
        spr(S.badge, L->grille.x + (L->grille.w - spr_w(S.badge)) / 2, grille_top() + 2 + bs * 8 + 2 - spr_h(S.badge) / 2);
    /* minimap: bezel, image window, camera brackets */
    draw9(&s9_mmap, L->mmap_frame.x, L->mmap_frame.y, L->mmap_frame.w, L->mmap_frame.h, -1);
    fill(L->mmap.x, L->mmap.y, L->mmap.x + L->mmap.w, L->mmap.y + L->mmap.h, COL(UI_COL_BAR_XDK));
    if (s_mm_ok) { gfx_blit(&s_mm, L->mmap.x, L->mmap.y); s_nblit++; }
    x = L->mmap.x; y = L->mmap.y;
    spr(S.mmap_cam[0], x + s_cam_box[0], y + s_cam_box[1]);
    spr(S.mmap_cam[1], x + s_cam_box[2] - 5, y + s_cam_box[1]);
    spr(S.mmap_cam[2], x + s_cam_box[0], y + s_cam_box[3] - 5);
    spr(S.mmap_cam[3], x + s_cam_box[2] - 5, y + s_cam_box[3] - 5);
    end_compose(C_SIDE);
}

static void side_dirty(int x, int y, int w, int h)
{
    if (s_nside_dirty >= SIDE_DIRTY_MAX) { s_c[C_SIDE].dirty = 1; return; }
    s_side_dirty[s_nside_dirty][0] = (s16)x; s_side_dirty[s_nside_dirty][1] = (s16)y;
    s_side_dirty[s_nside_dirty][2] = (s16)w; s_side_dirty[s_nside_dirty][3] = (s16)h;
    s_nside_dirty++;
}

/* find what changed on the side panel since the last composition */
static void side_check(void)
{
    char buf[128];
    int i, x, y, w, h, v[8];
    u32 sig;
    const SimSettings *ss = sim_settings();
    for (i = 0; i < NKEYS; i++) if (key_state(i) != s_key_drawn[i]) { key_rect(i, &x, &y, &w, &h); side_dirty(x, y, w, h); }
    for (i = 0; i < NMINI && i < L->mini_count; i++)
        if (mini_state(i) != s_mini_drawn[i]) { mini_rect(i, &x, &y, &w, &h); side_dirty(x, y, w, h); }
    if (zonev_state() != s_zonev_drawn) { zonev_rect(&x, &y, &w, &h); side_dirty(x, y, w, h); }   /* F3 */
    for (i = 0; i < NM7K && i + 1 < L->mini_count; i++)                                           /* M7 + PL1 */
        if (m7k_state(i) != s_m7k_drawn[i]) { m7k_rect(i, &x, &y, &w, &h); side_dirty(x, y, w, h); }
    if (guide_state() != s_guide_drawn) { guide_rect(&x, &y, &w, &h); side_dirty(x, y, w, h); }       /* PL1 */
    sig = hash_bytes(HASH_INIT, ss->town, NAME_LEN);
    if (sig != s_plate_sig) { s_plate_sig = sig; side_dirty(L->plate.x, L->plate.y, L->plate.w, L->plate.h); }
    tool_label_text(buf, sizeof(buf));
    sig = hash_bytes(HASH_INIT, buf, (int)strlen(buf));
    if (sig != s_label_sig) { s_label_sig = sig; side_dirty(L->tool_label.x, L->tool_label.y, L->tool_label.w, L->tool_label.h); }
    v[0] = s_mood; v[1] = s_mood_h;
    sig = hash_bytes(hash_ints(v, 2), ss->president, NAME_LEN);
    if (sig != s_plaque_sig) { s_plaque_sig = sig; side_dirty(L->plaque.x, L->plaque.y, L->plaque.w, L->plaque.h); }
    mm_view();
    v[0] = s_mm_ver; v[1] = s_mm_ox; v[2] = s_mm_oy; v[3] = s_cam_box[0]; v[4] = s_cam_box[1];
    v[5] = s_cam_box[2]; v[6] = s_cam_box[3]; v[7] = s_mm_ok;
    sig = hash_ints(v, 8);
    if (sig != s_mmap_sig) { s_mmap_sig = sig; side_dirty(L->mmap.x, L->mmap.y, L->mmap.w, L->mmap.h); }
}

/* ================================================================== ticker */
static void compose_ticker(void)
{
    static const int lamp_font[4] = { FONT_BODY_LIGHT, FONT_BODY_AMBER, FONT_BODY_RED_LT, FONT_BODY_BLUE_LT };
    char buf[96];
    int i, x, x1, w, tx = L->ticker.x, ty = L->ticker.y;
    begin_compose(&s_c[C_TICKER], 0, NULL);
    spr_tile(S.ticker_tile, tx, ty, L->ticker.w, L->ticker.h);
    s_ntickr = 0;
    x = tx + 4; x1 = tx + L->ticker.w - 4;
    if (!s_ntick) {
        spr(S.lamp[4], x, ty + 4);
        str_printf(buf, sizeof(buf), "All quiet in %s.", sim_settings()->town[0] ? sim_settings()->town : "town");
        txt(x + 11, ty + 2, buf, FONT_BODY_GREY);
    }
    for (i = 0; i < s_ntick && x < x1 - 40; i++) {
        const TickItem *t = &s_tick[i];
        int f = lamp_font[ICLAMP(t->sev, 0, 3)];
        spr(S.lamp[ICLAMP(t->sev, 0, 3)], x, ty + 4);
        w = IMIN(txtw(t->text, f), x1 - (x + 11));
        txt_fit(x + 11, ty + 2, t->text, f, w);
        s_tickr[s_ntickr][0] = (s16)x; s_tickr[s_ntickr][1] = (s16)ty;
        s_tickr[s_ntickr][2] = (s16)(11 + w); s_tickr[s_ntickr][3] = (s16)L->ticker.h;
        s_ntickr++;
        x += 11 + w + 14;
    }
    end_compose(C_TICKER);
}

void ui_ticker_push(int severity, const char *text, int x, int y)
{
    int i;
    TickItem *t;
    if (!text || !text[0]) return;
    for (i = 0; i < s_ntick; i++) if (!strcmp(s_tick[i].text, text)) return;   /* same line already shown */
    if (s_ntick == TICK_MAX) s_ntick--;
    memmove(&s_tick[1], &s_tick[0], s_ntick * sizeof(TickItem));
    s_ntick++;
    t = &s_tick[0];
    str_copy(t->text, text, TICK_TEXT);
    t->sev = ICLAMP(severity, 0, 3); t->x = x; t->y = y; t->panel = PANEL_NONE; t->topic = -1; t->t = s_now;
    s_c[C_TICKER].dirty = 1;
}

void ui_ticker_event(const SimEvent *e)
{
    char buf[TICK_TEXT];
    int sev, topic;
    u32 month = sim_date()->total_months + 1;
    if (!text_event(buf, sizeof(buf), e, &sev)) return;
    topic = ICLAMP(text_event_topic(e), 0, COUNTOF(s_topic_month) - 1);
    if (sev != SEV_URGENT && e->type != SE_STATUS_UP && s_topic_month[topic] == month) return;  /* 1 per topic per month */
    s_topic_month[topic] = month;
    ui_ticker_push(sev, buf, e->x, e->y);
    if (s_ntick && !strcmp(s_tick[0].text, buf)) {
        TickItem *t = &s_tick[0];
        t->topic = topic;
        switch (e->type) {
        case SE_EMIGRATION: case SE_MILESTONE: case SE_STATUS_UP: t->panel = PANEL_POPULATION; break;
        case SE_NO_MONEY: case SE_BANKRUPT_WARN: t->panel = PANEL_BUDGET; break;
        case SE_POWER_SHORTAGE: case SE_WATER_SHORTAGE: case SE_SERVICE_FULL: t->panel = PANEL_SERVICES; break;
        }
    }
}

int ui_ticker_jump(void)
{
    int i;
    for (i = 0; i < s_ntick; i++)
        if (s_tick[i].x >= 0) { cmd(UC_CAMERA, s_tick[i].x, s_tick[i].y); return 1; }
    if (s_ntick && s_tick[0].panel) { ui_open_panel(s_tick[0].panel); return 1; }
    return 0;
}

static void ticker_click(int i)
{
    const TickItem *t;
    if (i < 0 || i >= s_ntick) return;
    t = &s_tick[i];
    if (t->topic == TOPIC_HINT) {             /* F3: a click on a hint stops the hint chain (this game) */
        int k, n = 0;
        cmd(UC_HINTS, 0, 0);
        for (k = 0; k < s_ntick; k++) if (s_tick[k].topic != TOPIC_HINT) s_tick[n++] = s_tick[k];
        s_ntick = n;
        s_c[C_TICKER].dirty = 1;
        return;
    }
    if (t->x >= 0 && t->y >= 0) cmd(UC_CAMERA, t->x, t->y);
    else if (t->panel) ui_open_panel(t->panel);
}

/* ================================================================== popup: fly-out */
static void compose_fly(void)
{
    char cost[48], hint[48];
    const KeyDef *kd = &s_keys[s_fly];
    Cache *c = &s_c[C_POP];
    int i, kx, ky, kw, kh, ry, lk, hot;
    fly_layout(s_fly);
    c->x = s_fly_x; c->y = s_fly_y;
    begin_compose(c, 1, NULL);
    key_rect(s_fly, &kx, &ky, &kw, &kh);
    draw9(&s9_fly, s_fly_x, s_fly_y, s_fly_w, s_fly_h, -1);
    spr(S.fly_arrow, s_fly_x + s_fly_w - 1, ICLAMP(ky + kh / 2 - 6, s_fly_y + 3, s_fly_y + s_fly_h - 14));
    for (i = 0; i < kd->n; i++) {
        const FlyEnt *e = &kd->e[i];
        ry = s_fly_y + 6 + i * 17;
        lk = ent_locked(e, hint, sizeof(hint));
        hot = s_hot == H_FLYROW0 + i;
        if (e->sep && i) spr_tile(S.fly_sep, s_fly_x + 6, ry - 1, s_fly_w - 12, 2);
        if (e->tool == s_tool && e->param == s_param) draw3h(&s3_flysel, s_fly_x + 4, ry, s_fly_w - 8);
        else if (hot && !lk) draw3h(&s3_flyhover, s_fly_x + 4, ry, s_fly_w - 8);
        spr(s_fly_icon[s_fly][i][lk], s_fly_x + 8, ry + 2);
        txt(s_fly_x + 25, ry + 3, text_tool_name(e->tool, e->param), lk ? FONT_BODY_GREY : FONT_BODY_DARK);
        if (!lk) ent_cost(e, cost, sizeof(cost));
        txt_r(s_fly_x + s_fly_w - 8, ry + 3, lk ? hint : cost, lk ? FONT_BODY_GREY : FONT_BODY_DARK);
    }
    {   /* PL1: footer for the hovered row: unlock sentence + purpose */
        char fl[2][96];
        int nf = 0, j, fy = s_fly_y + 6 + kd->n * 17 + 2, hov = (s_hot >= H_FLYROW0 && s_hot < H_FLYROW0 + kd->n) ? s_hot - H_FLYROW0 : -1;
        int red = hov >= 0 && ent_locked(&kd->e[hov], hint, sizeof(hint));
        spr_tile(S.fly_sep, s_fly_x + 6, fy - 1, s_fly_w - 12, 2);
        if (hov >= 0) nf = fly_foot_lines(s_fly, hov, fl, s_fly_nfoot);
        else { str_copy(fl[0], "Point at an item: what it needs, what it does", 96); nf = 1; }
        for (j = 0; j < nf; j++) txt_fit(s_fly_x + 8, fy + 2 + j * 11, fl[j], j == 0 && red ? FONT_BODY_RED : FONT_BODY_GREY, s_fly_w - 16);
    }
    end_compose(C_POP);
}

static void fly_open(int k)
{
    s_fly = k;
    s_tip_shown = 0;
    s_c[C_POP].dirty = 1;
}

static void fly_close(void)
{
    if (s_fly < 0) return;
    s_fly = -1;
    s_c[C_POP].dirty = 1;
}

static void select_entry(int k, int i)
{
    const FlyEnt *e = &s_keys[k].e[i];
    char hint[48];
    if (ent_locked(e, hint, sizeof(hint))) return;
    s_key_last[k] = i;
    s_tool = e->tool; s_param = e->param;
    cmd(UC_TOOL, e->tool, e->param);
    fly_close();
}

/* ================================================================== popup: tooltip */
static void tip_add(const char *l, int fl, const char *r, int fr)
{
    TipLine *t;
    if (s_ntip >= MAX_TIP) return;
    t = &s_tip[s_ntip++];
    str_copy(t->l, l ? l : "", sizeof(t->l)); str_copy(t->r, r ? r : "", sizeof(t->r));
    t->fl = (u8)fl; t->fr = (u8)fr;
}

static void tip_amount(const char *label, s32 v, int money, int invert_colour)
{
    char l[64], r[28];
    int year = cur_year(), good = invert_colour ? v < 0 : v > 0;
    str_printf(l, sizeof(l), "%s %s", v >= 0 ? GLYPH_ARROW_UP : GLYPH_ARROW_DOWN, label);
    if (money) text_fmt_money_signed(r, sizeof(r), v, year); else text_fmt_signed(r, sizeof(r), v);
    tip_add(l, FONT_BODY_DARK, r, v == 0 ? FONT_BODY_DARK : good ? FONT_BODY_GREEN : FONT_BODY_RED);
}

/* M5: "  Precast elements left (of 300 a month)" + the number in r */
static void n_elem_line(char *l, int size, char *r)
{
    char o[24];
    text_fmt_int(o, sizeof(o), civic_elements_output());
    str_printf(l, size, "  Precast elements left (of %s a month)", o);
    UNUSED(r);
}

static void tip_demand_block(int dem)
{
    const Demand *d = sim_demand(dem);
    char l[64], r[28];
    int k;
    str_printf(l, sizeof(l), "%s demand", text_demand_name(dem));
    text_fmt_signed(r, sizeof(r), d->value);
    tip_add(l, FONT_BODY_DARK, r, d->value >= 0 ? FONT_BODY_GREEN : FONT_BODY_RED);
    for (k = 0; k < d->nterms; k++) {
        if (!d->terms[k].amount) continue;
        str_printf(l, sizeof(l), "  %s", text_term_label(TERMK_DEMAND, d->terms[k].id));
        tip_amount(l + 2, d->terms[k].amount, 0, 0);
    }
    if (dem == DEM_R && civic_elements_output() > 0) {   /* M5: the element cap, never a mystery (scope review) */
        text_fmt_int(r, sizeof(r), civic_elements_left());
        n_elem_line(l, sizeof(l), r);
        tip_add(l, FONT_BODY_DARK, r, civic_elements_left() > 0 ? FONT_BODY_GREEN : FONT_BODY_RED);
    }
    if (dem == DEM_I) {      /* M3: the same total by industry kind, one line (the tooltip has 18 rows) */
        static const char *const ab[IK_COUNT] = { "", "saw", "eng", "tex", "dairy", "elec" };
        int n = str_printf(l, sizeof(l), "  by kind:");
        for (k = 1; k < IK_COUNT; k++) {
            const IndKinds *ik = industry_kinds();
            if (k == IK_ELEC && !ik->demand[k] && !ik->capacity[k]) continue;
            text_fmt_signed(r, sizeof(r), ik->demand[k]);
            n += str_printf(l + n, (int)sizeof(l) - n, " %s %s%s", ab[k], r, ik->short_jobs[k] > 0 && k != IK_ELEC ? "*" : "");   /* * = input short */
        }
        tip_add(l, FONT_BODY_GREY, NULL, 0);
    }
}

static void tip_bar(int pct);                  /* PL1 */
static void bar(int x, int y, int w, int h, int v, int signed_bar, PIX c);
/* fill s_tip for hot item h; returns 0 if h has no tooltip */
static int tip_build(int h)
{
    char a[64], b[64];
    const CityStats *cs = sim_stats();
    int year = cur_year(), k, n, i;
    s_ntip = 0;
    if (h == H_DATE) {
        const SimDate *d = sim_date();
        text_fmt_date(a, sizeof(a), d->year, d->month);
        tip_add(a, FONT_BODY_DARK, NULL, 0);
        str_printf(a, sizeof(a), "Era: %s", text_era_name(def_era(d->year)));
        tip_add(a, FONT_BODY_DARK, NULL, 0);
        str_printf(a, sizeof(a), "Status: %s", text_status_name(cs->town_status));
        tip_add(a, FONT_BODY_DARK, NULL, 0);
        hard_note(HN_LEVEL, a, sizeof(a));        /* 1.1 D1: "Difficulty: Normal" (the Guide, F4 Tab, lists the levers) */
        tip_add(a, FONT_BODY_GREY, NULL, 0);
        {   /* PL1: "Next: Market town at 3,000 (you have 2,750)" + bar */
            int pct = advice_progress(b, sizeof(b), NULL);
            tip_add(b, advice_rights_waiting() ? FONT_BODY_RED : FONT_BODY_GREY, NULL, 0);
            if (cs->town_status + 1 < TS_COUNT) tip_bar(pct);
        }
        return 1;
    }
    if (h == H_FUNDS) {
        Term t[MAX_TERMS + 8];
        Money net = 0, svc = 0;
        text_fmt_money(b, sizeof(b), g_budget.money, year);
        tip_add("Funds", FONT_BODY_DARK, b, g_budget.money < 0 ? FONT_BODY_RED : FONT_BODY_DARK);
        tip_add("Next month (projected):", FONT_BODY_GREY, NULL, 0);
        n = budget_projection(t, COUNTOF(t));
        for (k = 0; k < n; k++) {
            net += t[k].amount;
            if (t[k].id >= BL_SVC_FIRST && t[k].id < BL_SVC_FIRST + SVC_COUNT) { svc += t[k].amount; continue; }
            if (t[k].id == BL_CONSTRUCTION) continue;
            tip_amount(text_term_label(TERMK_BUDGET, t[k].id), t[k].amount, 1, 0);
        }
        if (svc) tip_amount("Service upkeep", svc, 1, 0);
        text_fmt_money_signed(a, sizeof(a), net, year);
        str_printf(b, sizeof(b), "%s / month", a);
        tip_add(GLYPH_BULLET " Projected", FONT_BODY_DARK, b, net >= 0 ? FONT_BODY_GREEN : FONT_BODY_RED);
        text_fmt_money_signed(a, sizeof(a), budget_net_last(), year);
        tip_add("Last month", FONT_BODY_GREY, a, FONT_BODY_GREY);
        if (g_budget.loan > 0) { text_fmt_money(a, sizeof(a), g_budget.loan, year); tip_add("Emergency loan", FONT_BODY_RED, a, FONT_BODY_RED); }
        if (hard_note(HN_UPKEEP, b, sizeof(b))) {     /* 1.1 D1: "Hard: upkeep x115%" + the index cap */
            char c3[48];
            if (hard_note(HN_INDEX, c3, sizeof(c3))) str_printf(b + strlen(b), (int)sizeof(b) - (int)strlen(b), ", index to %d%%", diff()->upkeep_cap);
            tip_add(b, FONT_BODY_GREY, NULL, 0);
        }
        return 1;
    }
    if (h == H_POP) {
        text_fmt_int(a, sizeof(a), cs->population);  tip_add("Residents", FONT_BODY_DARK, a, FONT_BODY_DARK);
        text_fmt_int(a, sizeof(a), cs->capacity_res); str_printf(b, sizeof(b), "%s", a);
        tip_add("Homes (places)", FONT_BODY_DARK, b, FONT_BODY_DARK);
        text_fmt_int(a, sizeof(a), cs->vacant);      tip_add("  vacant", FONT_BODY_GREY, a, FONT_BODY_GREY);
        text_fmt_int(a, sizeof(a), cs->jobs);        tip_add("Jobs", FONT_BODY_DARK, a, FONT_BODY_DARK);
        text_fmt_int(a, sizeof(a), cs->workers);     tip_add("Workers", FONT_BODY_DARK, a, FONT_BODY_DARK);
        text_fmt_int(a, sizeof(a), cs->unemployed);  tip_add("Unemployed", FONT_BODY_DARK, a, cs->unemployed ? FONT_BODY_RED : FONT_BODY_DARK);
        text_fmt_int(a, sizeof(a), cs->homeless);    tip_add("Without a home", FONT_BODY_DARK, a, cs->homeless ? FONT_BODY_RED : FONT_BODY_DARK);
        text_fmt_signed(a, sizeof(a), cs->immigrated_month); tip_add(GLYPH_ARROW_UP " Arrived this month", FONT_BODY_DARK, a, FONT_BODY_GREEN);
        text_fmt_int(a, sizeof(a), cs->emigrated_month);
        tip_add(GLYPH_ARROW_DOWN " Left for Sweden this month", FONT_BODY_DARK, a, cs->emigrated_month ? FONT_BODY_RED : FONT_BODY_DARK);
        text_fmt_int(a, sizeof(a), cs->emigrated_total);
        tip_add("Emigrated to Sweden in all", FONT_BODY_DARK, a, FONT_BODY_RED);
        {   /* PL1 (PLAYTEST 2 item 1): the next status, how far, and the decree when it waits */
            int pct = advice_progress(b, sizeof(b), NULL);
            str_printf(a, sizeof(a), "%s: %s", text_status_name(cs->town_status), b);
            tip_add(a, advice_rights_waiting() ? FONT_BODY_RED : FONT_BODY_DARK, NULL, 0);
            if (cs->town_status + 1 < TS_COUNT) tip_bar(pct);
            if (advice_rights_waiting()) tip_add("\xa7 Click Approval: the decree is on the desk", FONT_BODY_GREY, NULL, 0);
        }
        return 1;
    }
    if (h == H_DEMAND) {
        tip_demand_block(DEM_R); tip_demand_block(DEM_C); tip_demand_block(DEM_I);
        {   /* M7: what scales the clearing and Western terms */
            char c3[96], c4[80];
            trade_clearing_reason(c4, sizeof(c4));
            str_printf(c3, sizeof(c3), GLYPH_BULLET " Clearing x%d%%: %s", trade_clearing_pct(), c4);
            tip_add(c3, FONT_BODY_GREY, NULL, 0);
            str_printf(c3, sizeof(c3), GLYPH_BULLET " Western orders x%d%% (price index)", trade_western_pct());
            tip_add(c3, FONT_BODY_GREY, NULL, 0);
            if (hard_sweden()->active) { hard_sweden_reasons(c3, sizeof(c3)); tip_add(c3, FONT_BODY_RED, NULL, 0); }   /* 1.1 D1 */
            if (hard_note(HN_TAX, c3, sizeof(c3))) tip_add(c3, FONT_BODY_GREY, NULL, 0);
            if (hard_note(HN_EMIGRATION, c3, sizeof(c3))) tip_add(c3, FONT_BODY_GREY, NULL, 0);
        }
        return 1;
    }
    if (h == H_WATER) {     /* F1 (WATER.md 3.6 D) */
        const NetStats *wn = net_stats(NET_WATER);
        WaterSummary ws;
        const Lot *bl;
        net_water_summary(&ws);
        text_fmt_int(a, sizeof(a), wn->used); text_fmt_int(b, sizeof(b), wn->supply);
        { char c3[64]; str_printf(c3, sizeof(c3), "Water %s / %s (%d%%)", a, b, wn->supply > 0 ? (int)((s64)wn->used * 100 / wn->supply) : 0);
          tip_add(c3, FONT_BODY_DARK, NULL, 0); }
        str_printf(a, sizeof(a), "%d network%s " TEXT_DOT " %d short " TEXT_DOT " %d not connected", ws.networks, ws.networks == 1 ? "" : "s",
                   ws.lots_short, ws.lots_unconnected);
        tip_add(a, ws.lots_short ? FONT_BODY_RED : FONT_BODY_GREY, NULL, 0);
        bl = world_lot(ws.best_spare_lot);
        if (bl) { text_fmt_int(b, sizeof(b), ws.best_spare); str_printf(a, sizeof(a), "Largest spare: %s (%s)", def_building(bl->def)->name, b); tip_add(a, FONT_BODY_GREY, NULL, 0); }
        {   /* 1.1 W1: hard frost on pumps and wells, the heat winter peak */
            int wwarn;
            if (season_tip_line(STIP_WATER, a, sizeof(a), &wwarn)) tip_add(a, wwarn ? FONT_BODY_RED : FONT_BODY_AMBER, NULL, 0);
            if (season_tip_line(STIP_HEAT, a, sizeof(a), &wwarn)) tip_add(a, wwarn ? FONT_BODY_RED : FONT_BODY_GREY, NULL, 0);
        }
        tip_add("Click for the water overlay", FONT_BODY_GREY, NULL, 0);
        return 1;
    }
    if (h == H_POWER) {     /* PL1 (PLAYTEST 2 item 3) */
        const NetStats *pn = net_stats(NET_POWER);
        PowerNetInfo pt;
        advice_power_total(&pt);
        text_fmt_int(a, sizeof(a), pt.demand); text_fmt_int(b, sizeof(b), pt.supply);
        { char c3[64]; str_printf(c3, sizeof(c3), "Power %s / %s (%d%%)", a, b, pt.pct); tip_add(c3, FONT_BODY_DARK, NULL, 0); }
        str_printf(a, sizeof(a), "%d network%s " TEXT_DOT " %d short " TEXT_DOT " %d not connected", pn->networks, pn->networks == 1 ? "" : "s",
                   (int)pn->lots_short, (int)pn->lots_unconnected);
        tip_add(a, pn->lots_short ? FONT_BODY_RED : FONT_BODY_GREY, NULL, 0);
        text_fmt_int(b, sizeof(b), pt.spare);
        if (pn->lots_short) str_printf(a, sizeof(a), "Short by %d units: the furthest buildings go dark first", (int)(pt.demand - pt.supply));
        else str_printf(a, sizeof(a), "Spare: %s units%s", b, pt.pct >= 90 ? " - time for a new plant" : "");
        tip_add(a, pn->lots_short ? FONT_BODY_RED : pt.pct >= 90 ? FONT_BODY_AMBER : FONT_BODY_GREY, NULL, 0);
        {   /* 1.1 W1: "Winter peak (forecast): ~1,360 / 1,300" from October; the winter load Dec-Mar */
            int pwarn;
            if (season_tip_line(STIP_POWER, a, sizeof(a), &pwarn)) tip_add(a, pwarn ? FONT_BODY_RED : FONT_BODY_GREY, NULL, 0);
        }
        tip_add("Click for the power overlay", FONT_BODY_GREY, NULL, 0);
        return 1;
    }
    if (h == H_APPROVAL) {      /* M1 */
        char pl[8][64];
        n = uipol_tip(pl, 8);
        for (k = 0; k < n; k++) tip_add(pl[k], k == 0 ? FONT_BODY_DARK : FONT_BODY_GREY, NULL, 0);
        if (advice_rights_waiting()) {     /* PL1: the town-rights decree waits (\xa7) */
            str_printf(a, sizeof(a), "\xa7 %s rights await your signature", text_status_name(era_status_eligible()));
            tip_add(a, FONT_BODY_RED, NULL, 0);
        }
        return 1;
    }
    if (h >= H_SPEED0 && h < H_SPEED0 + 4) {
        static const char *const sp[4] = { "Pause (Space)", "Normal speed (F1)", "Fast (F2)", "Fastest (F3)" };
        tip_add(sp[h - H_SPEED0], FONT_BODY_DARK, NULL, 0);
        return 1;
    }
    if (h == H_MENU) { tip_add("Game: save, load, quit (Esc)", FONT_BODY_DARK, NULL, 0); return 1; }
    if (h >= H_KEY0 && h < H_KEY0 + NKEYS) {
        k = h - H_KEY0;
        tip_add(s_keys[k].hint, FONT_BODY_DARK, NULL, 0);
        if (key_all_locked(k)) {
            ent_locked(&s_keys[k].e[0], b, sizeof(b));
            str_printf(a, sizeof(a), "Unlocks at: %s", b);
            tip_add(a, FONT_BODY_GREY, NULL, 0);
        } else if (s_keys[k].n > 1) tip_add("Click for the choices", FONT_BODY_GREY, NULL, 0);
        return 1;
    }
    if (h >= H_MINI0 && h < H_MINI0 + NMINI) { tip_add(s_mini_tip[h - H_MINI0], FONT_BODY_DARK, NULL, 0); return 1; }
    if (h == H_MMAP) { tip_add("Minimap: click or drag to move the view", FONT_BODY_DARK, NULL, 0); return 1; }
    if (h == H_PLAQUE) {
        tip_add("The President", FONT_BODY_DARK, NULL, 0);
        str_printf(a, sizeof(a), "Mood: %s", text_mood_name(s_mood));
        tip_add(a, FONT_BODY_GREY, NULL, 0);
        if (politics_enabled()) tip_add("Click for the President's desk", FONT_BODY_GREY, NULL, 0);   /* M1 */
        return 1;
    }
    if (h >= H_TICK0 && h < H_TICK0 + TICK_MAX) {
        char ln[4][96];
        i = h - H_TICK0;
        if (i >= s_ntick) return 0;
        n = wrap(s_tick[i].text, FONT_BODY_DARK, POP_W - 20, ln, 4);
        for (k = 0; k < n && k < 4; k++) tip_add(ln[k], FONT_BODY_DARK, NULL, 0);
        if (s_tick[i].topic == TOPIC_HINT) tip_add("Click to hide the hints", FONT_BODY_GREY, NULL, 0);   /* F3 */
        else if (s_tick[i].x >= 0) tip_add("Click to go there", FONT_BODY_GREY, NULL, 0);
        else if (s_tick[i].panel) tip_add("Click for details", FONT_BODY_GREY, NULL, 0);
        return 1;
    }
    if (h == H_ZOOM0 || h == H_ZOOM0 + 1) {           /* F3 */
        tip_add(h == H_ZOOM0 ? "Zoom out (-)" : "Zoom in (+)", FONT_BODY_DARK, NULL, 0);
        tip_add("Z toggles the zoom; the wheel and PgUp/PgDn work too", FONT_BODY_GREY, NULL, 0);
        return 1;
    }
    if (h == H_ZONEV) {
        tip_add(s_zone_view ? "Zone view: on (V)" : "Zone view: off (V)", FONT_BODY_DARK, NULL, 0);
        tip_add("Shows zone borders whatever tool you hold", FONT_BODY_GREY, NULL, 0);
        return 1;
    }
    if (h == H_GUIDE) {                               /* PL1 */
        tip_add("The President's Handbook (F4)", FONT_BODY_DARK, NULL, 0);
        tip_add("The town ladder, what each system is for, the keys", FONT_BODY_GREY, NULL, 0);
        return 1;
    }
    if (h >= H_M7K0 && h < H_M7K0 + NM7K) {           /* M7 + PL1 */
        static const char *const t1[NM7K] = { "Graphs (G)", "Trade and the treasury (T)", "The newspaper (N)", "Services (E)" };
        static const char *const t2[NM7K] = { "Population, money, demand, work, approval, clearing",
                                              "The clearing agreement, the balance, oil, the bond", "Maakunnan Sanomat: the latest issue",
                                              "Schools, clinics, saunas... load / capacity; power and water" };
        tip_add(t1[h - H_M7K0], FONT_BODY_DARK, NULL, 0);
        tip_add(t2[h - H_M7K0], FONT_BODY_GREY, NULL, 0);
        return 1;
    }
    return 0;
}

static u32 tip_sig(void) { return hash_bytes(HASH_INIT, s_tip, s_ntip * (int)sizeof(TipLine)); }
#define TIP_BAR 0xFF                       /* PL1: a progress bar line (r[0] = percent) */
static void tip_bar(int pct) { char r[2]; r[0] = (char)ICLAMP(pct, 1, 100); r[1] = 0; tip_add("", TIP_BAR, r, TIP_BAR); }

static void compose_tip(void)
{
    Cache *c = &s_c[C_POP];
    int i, w = 0, h, x, y, lw;
    for (i = 0; i < s_ntip; i++) {
        if (s_tip[i].fl == TIP_BAR) { w = IMAX(w, 160); continue; }
        lw = txtw(s_tip[i].l, s_tip[i].fl) + (s_tip[i].r[0] ? 12 + txtw(s_tip[i].r, s_tip[i].fr) : 0);
        w = IMAX(w, lw);
    }
    w = IMIN(w + 13, POP_W); h = IMIN(s_ntip * 11 + 8, POP_H);
    /* below the mouse, inside the screen */
    x = ICLAMP(s_mx + 2, 2, s_sw - w - 2);
    y = s_my + 18;
    if (y + h > s_sh - 2) y = s_my - h - 6;
    y = IMAX(y, 2);
    c->x = x; c->y = y;
    begin_compose(c, 1, NULL);
    draw9(&s9_tip, x, y, w, h, COL(UI_COL_TIP));
    for (i = 0; i < s_ntip; i++) {
        if (s_tip[i].fl == TIP_BAR) { bar(x + 6, y + 4 + i * 11, w - 13, 7, (u8)s_tip[i].r[0], 0, COL(UI_COL_GREEN)); continue; }
        txt(x + 6, y + 3 + i * 11, s_tip[i].l, s_tip[i].fl);
        if (s_tip[i].r[0]) txt_r(x + w - 7, y + 3 + i * 11, s_tip[i].r, s_tip[i].fr);
    }
    c->w = w; c->h = h;
    end_compose(C_POP);
}

/* ================================================================== panels: widgets */
static int ctl_add(int x, int y, int w, int h, int kind, int a, int b)
{
    Ctl *c;
    if (s_nctl >= MAX_CTL) return -1;
    c = &s_ctl[s_nctl];
    c->x = (s16)x; c->y = (s16)y; c->w = (s16)w; c->h = (s16)h; c->kind = (u8)kind; c->a = (s16)a; c->b = (s16)b;
    return s_nctl++;
}

static int ctl_hot(int idx) { return idx >= 0 && s_hot == H_CTL0 + idx; }

static void tabs(int x, int y, int right, const char *const *names, int n, int sel)
{
    int i, w, idx, st;
    for (i = 0; i < n; i++) {
        w = txtw(names[i], FONT_BODY_DARK) + 12;
        idx = ctl_add(x, y, w, 14, CK_TAB, i, 0);
        st = i == sel ? 2 : ctl_hot(idx) ? 1 : 0;
        draw3h(&s3_tab[st], x, y, w);
        txt(x + 6, y + 2, names[i], FONT_BODY_DARK);
        x += w + 1;
    }
    if (right > x) spr_tile(S.tab_line, x, y + 13, right - x, 1);
}

static void rule(int x, int y, int w) { spr_tile(S.rule, x, y, w, 2); }

static void slider(int x, int y, int w, int v, int vmin, int vmax, int which)
{
    int idx = ctl_add(x - 4, y - 3, w + 8, 13, CK_SLIDER, which, 0);
    int fw, st;
    if (s_drag_ctl >= 0 && s_ctl[s_drag_ctl].a == which) v = s_drag_val;
    fw = (v - vmin) * w / IMAX(1, vmax - vmin);
    draw3h(&s3_track, x, y, w);
    if (fw > 0) draw3h(&s3_fill, x, y, fw);
    st = (s_drag_ctl >= 0 && s_ctl[s_drag_ctl].a == which) ? 2 : ctl_hot(idx) ? 1 : 0;
    spr(S.knob[st], x + fw - 4, y - 3);
}

static void button(int x, int y, int w, int h, const char *label, int action)
{
    int idx = ctl_add(x, y, w, h, CK_BUTTON, action, 0);
    int st = ctl_hot(idx) ? (s_down && s_press_hot == s_hot ? BS_PRESSED : BS_HOVER) : BS_NORMAL;
    int o = st == BS_PRESSED;
    draw9(&s9_btn[st], x, y, w, h, -1);
    txt_c(x + o, w - 1, y + (h - 11) / 2 + o, label, FONT_BODY_DARK);
}

static void checkbox(int x, int y, int on, const char *label, int what)
{
    int idx = ctl_add(x, y, 16 + txtw(label, FONT_BODY_DARK), 11, CK_CHECK, what, 0);
    spr(S.check[on ? 1 : 0][ctl_hot(idx) ? 1 : 0], x, y);
    txt(x + 16, y, label, FONT_BODY_DARK);
}

static void row_lr(int x, int y, int w, const char *l, int fl, const char *r, int fr)
{
    txt_fit(x, y, l, fl, w - (r ? txtw(r, fr) + 8 : 0));
    if (r) txt_r(x + w, y, r, fr);
}

static int money_font(Money v) { return v < 0 ? FONT_BODY_RED : v > 0 ? FONT_BODY_GREEN : FONT_BODY_DARK; }

/* horizontal bar in an inset: value -100..100 from the centre (signed) or 0..100 from the left */
static void bar(int x, int y, int w, int h, int v, int signed_bar, PIX c)
{
    int iw = w - 4, mid;
    draw9(&s9_inset, x, y, w, h, COL(UI_COL_PAPER_DK));
    if (signed_bar) {
        mid = x + 2 + iw / 2;
        v = ICLAMP(v, -100, 100) * (iw / 2) / 100;
        if (v > 0) fill(mid, y + 2, mid + v, y + h - 2, c);
        else if (v < 0) fill(mid + v, y + 2, mid, y + h - 2, c);
        fill(mid, y + 1, mid + 1, y + h - 1, COL(UI_COL_PENCIL));
    } else {
        v = ICLAMP(v, 0, 100) * iw / 100;
        if (v > 0) fill(x + 2, y + 2, x + 2 + v, y + h - 2, c);
    }
}

/* ================================================================== panels: content */
static const char *const s_terrain_name[TER_COUNT] = { "Grass", "Forest", "Rock", "Lake", "River", "Field", "Marsh" };

static void panel_budget(int x, int y, int w, int h)
{
    static const char *const tn[3] = { "Taxes", "Services", "Ledger" };
    char a[48], b[48];
    int year = cur_year(), k, n, yy, i, tab = s_tab[PANEL_BUDGET];
    UNUSED(h);
    tabs(x + 4, y + 4, x + w - 4, tn, 3, tab);
    yy = y + 26;
    if (tab == 0) {
        Term t[MAX_TERMS + 8];
        Money svc = 0, net = 0;
        int rows = 0, iy0;
        text_fmt_money(a, sizeof(a), g_budget.money, year);
        row_lr(x + 6, yy, w - 12, "Treasury", FONT_BODY_DARK, g_budget.unlimited ? "unlimited" : a,
               g_budget.money < 0 ? FONT_BODY_RED : FONT_BODY_DARK);
        yy += 15;
        for (i = 0; i < 2; i++) {
            int v = i ? g_budget.tax_bus : g_budget.tax_res;
            if (s_drag_ctl >= 0 && s_ctl[s_drag_ctl].a == i) v = s_drag_val;
            text_fmt_pct(a, sizeof(a), v);
            row_lr(x + 6, yy, w - 12, i ? "Business tax" : "Residential tax", FONT_BODY_DARK, a, FONT_BODY_DARK);
            slider(x + 6, yy + 13, w - 12 - 36, i ? g_budget.tax_bus : g_budget.tax_res, 0, 20, i);
            yy += 28;
        }
        if (!hard_note(HN_TAX, b, sizeof(b))) str_printf(b, sizeof(b), "Above %d%% taxes slow growth and drive people away.", diff()->tax_neutral);
        txt_fit(x + 6, yy, b, FONT_BODY_GREY, w - 12);   /* 1.1 D1: the level's tax tolerance / yield */
        yy += 14;
        rule(x + 6, yy, w - 12); yy += 6;
        txt(x + 6, yy, "Monthly projection", FONT_HEAD);
        str_printf(b, sizeof(b), "Upkeep index %d%%", budget_upkeep_index());      /* F3 (T12: +2.5%/year from 1960) */
        if (diff()->upkeep_cap != g_diff[DIFF_EASY].upkeep_cap)                    /* 1.1 D1: the level's cap */
            str_printf(b, sizeof(b), "Upkeep index %d%% (to %d%%)", budget_upkeep_index(), diff()->upkeep_cap);
        txt_r(x + w - 6, yy + 2, b, budget_upkeep_index() > 100 ? FONT_BODY_RED : FONT_BODY_GREY);
        yy += 16;
        n = budget_projection(t, COUNTOF(t));
        iy0 = yy;
        for (k = 0; k < n; k++) {
            net += t[k].amount;
            if (t[k].id >= BL_SVC_FIRST && t[k].id < BL_SVC_FIRST + SVC_COUNT) svc += t[k].amount;
        }
        draw9(&s9_inset, x + 6, yy, w - 12, 6 + 11 * 9, COL(UI_COL_PAPER_DK));
        yy += 3;
        for (i = 0; i < 2; i++) {               /* incomes, then service upkeep (one line), then other costs */
            if (i == 1 && svc) {
                char sl[48];
                text_fmt_money_signed(a, sizeof(a), svc, year);
                if (budget_level_upkeep_pct() != 100) str_printf(sl, sizeof(sl), "Service upkeep (%s: x%d%%)", diff()->name, budget_level_upkeep_pct());   /* 1.1 D1 */
                else str_copy(sl, "Service upkeep", sizeof(sl));
                row_lr(x + 12, yy, w - 24, sl, FONT_BODY_DARK, a, FONT_BODY_RED);
                yy += 11; rows++;
            }
            for (k = 0; k < n && rows < 8; k++) {
                int id = t[k].id;
                if ((id >= BL_SVC_FIRST && id < BL_SVC_FIRST + SVC_COUNT) || id == BL_CONSTRUCTION) continue;
                if ((i == 0) != (id < BL_INCOME_END)) continue;
                text_fmt_money_signed(a, sizeof(a), t[k].amount, year);
                row_lr(x + 12, yy, w - 24, text_term_label(TERMK_BUDGET, id), FONT_BODY_DARK, a, t[k].amount < 0 ? FONT_BODY_RED : FONT_BODY_DARK);
                yy += 11; rows++;
            }
        }
        yy = iy0 + 3 + 11 * 8;
        text_fmt_money_signed(a, sizeof(a), net, year);
        row_lr(x + 12, yy, w - 24, "Balance", FONT_BODY_DARK, a, money_font(net));
        yy = iy0 + 6 + 11 * 9 + 6;
        if (g_budget.loan > 0) {
            text_fmt_money(a, sizeof(a), g_budget.loan, year);
            str_printf(b, sizeof(b), "Emergency loan: %s at %d.%d%% a month", a, g_budget.loan_rate / 10, g_budget.loan_rate % 10);
            txt_fit(x + 6, yy, b, FONT_BODY_RED, w - 12);
        } else if (g_budget.months_negative > 0) {
            str_printf(b, sizeof(b), "In the red for %d month%s. Emergency loan after %d.", g_budget.months_negative,
                       g_budget.months_negative == 1 ? "" : "s", LOAN_AFTER_MONTHS);
            txt_fit(x + 6, yy, b, FONT_BODY_RED, w - 12);
        } else txt(x + 6, yy, "No loans. The bank manager is polite.", FONT_BODY_GREY);
        spr(S.seal, x + w - 6 - 17, y + h - 6 - 17);
    } else if (tab == 1) {
        txt(x + 6, yy, "Funding per service (50 - 120%)", FONT_BODY_GREY);
        yy += 14;
        for (i = 0; i < SVC_COUNT; i++) {
            char ic[32];
            int v = g_budget.funding[i];
            if (s_drag_ctl >= 0 && s_ctl[s_drag_ctl].a == 2 + i) v = s_drag_val;
            str_printf(ic, sizeof(ic), "s_%s", g_services[i].key);
            spr(icon_id(ic, 0), x + 6, yy - 1);
            text_fmt_pct(a, sizeof(a), v);
            text_fmt_money_signed(b, sizeof(b), g_budget.last[BL_SVC(i)], year);
            txt(x + 24, yy, g_services[i].name, FONT_BODY_DARK);
            txt_r(x + w - 50, yy, b, g_budget.last[BL_SVC(i)] < 0 ? FONT_BODY_RED : FONT_BODY_GREY);
            txt_r(x + w - 6, yy, a, FONT_BODY_DARK);
            slider(x + 24, yy + 13, w - 24 - 46, g_budget.funding[i], 50, 120, 2 + i);
            yy += 30;
        }
        txt(x + 6, yy + 2, "Less money, less coverage. More money, happier staff.", FONT_BODY_GREY);
    } else {
        Money tl = 0, ty = 0;
        txt_r(x + w - 86, yy, "Last month", FONT_BODY_GREY);
        txt_r(x + w - 8, yy, "This year", FONT_BODY_GREY);
        yy += 13;
        for (i = 0; i < BL_COUNT && yy < y + h - 30; i++) {
            if (!g_budget.last[i] && !g_budget.year[i]) continue;
            txt_fit(x + 6, yy, text_term_label(TERMK_BUDGET, i), FONT_BODY_DARK, w - 6 - 150);
            text_fmt_money_signed(a, sizeof(a), g_budget.last[i], year);
            txt_r(x + w - 86, yy, a, g_budget.last[i] < 0 ? FONT_BODY_RED : FONT_BODY_DARK);
            text_fmt_money_signed(a, sizeof(a), g_budget.year[i], year);
            txt_r(x + w - 8, yy, a, g_budget.year[i] < 0 ? FONT_BODY_RED : FONT_BODY_DARK);
            tl += g_budget.last[i]; ty += g_budget.year[i];
            yy += 11;
        }
        rule(x + 6, yy + 2, w - 12); yy += 7;
        txt(x + 6, yy, "Total", FONT_BODY_DARK);
        text_fmt_money_signed(a, sizeof(a), tl, year); txt_r(x + w - 86, yy, a, money_font(tl));
        text_fmt_money_signed(a, sizeof(a), ty, year); txt_r(x + w - 8, yy, a, money_font(ty));
    }
}

static void panel_population(int x, int y, int w, int h)
{
    static const char *const tn[2] = { "Demand", "People" };
    static const char *const unit[3] = { "people", "jobs", "jobs" };
    char a[48], b[96];
    const CityStats *cs = sim_stats();
    int yy, d, k, n, tab = s_tab[PANEL_POPULATION];
    tabs(x + 4, y + 4, x + w - 4, tn, 2, tab);
    yy = y + 26;
    if (tab == 0) {
        for (d = 0; d < DEM_COUNT; d++) {
            const Demand *dm = sim_demand(d);
            static const int rgb[3][3] = { { UI_COL_GREEN }, { UI_COL_BLUE }, { UI_COL_AMBER } };
            txt(x + 6, yy, text_demand_name(d), FONT_HEAD);
            text_fmt_signed(a, sizeof(a), dm->target);
            str_printf(b, sizeof(b), "target %s %s", a, unit[d]);
            txt_r(x + w - 6, yy + 2, b, FONT_BODY_GREY);
            yy += 15;
            bar(x + 6, yy, w - 12, 9, dm->value, 1, col(rgb[d][0], rgb[d][1], rgb[d][2]));
            yy += 12;
            for (k = 0; k < dm->nterms; k++) {
                s32 v = dm->terms[k].amount;
                if (!v) continue;
                str_printf(b, sizeof(b), "%s %s", v > 0 ? GLYPH_ARROW_UP : GLYPH_ARROW_DOWN, text_term_label(TERMK_DEMAND, dm->terms[k].id));
                text_fmt_signed(a, sizeof(a), v);
                row_lr(x + 10, yy, w - 16, b, FONT_BODY_DARK, a, v > 0 ? FONT_BODY_GREEN : FONT_BODY_RED);
                yy += 11;
            }
            if (d == DEM_R && civic_elements_output() > 0) {     /* M5: apartments T2+ wait for precast elements */
                char o[24];
                text_fmt_int(a, sizeof(a), civic_elements_left()); text_fmt_int(o, sizeof(o), civic_elements_output());
                str_printf(b, sizeof(b), "Precast elements (apartments T2+): %s / %s left", a, o);
                txt_fit(x + 10, yy, b, civic_elements_left() > 0 ? FONT_BODY_GREY : FONT_BODY_RED, w - 16);
                yy += 11;
            }
            yy += 5;
        }
    } else {
        static const char *const lab[9] = { "Residents", "Homes (places)", "Vacant homes", "Jobs", "Workers",
            "Unemployed", "Without a home", "Arrived this month", "Left for Sweden this month" };
        s32 v[9];
        SimProblem pr[3];
        v[0] = cs->population; v[1] = cs->capacity_res; v[2] = cs->vacant; v[3] = cs->jobs; v[4] = cs->workers;
        v[5] = cs->unemployed; v[6] = cs->homeless; v[7] = cs->immigrated_month; v[8] = cs->emigrated_month;
        for (k = 0; k < 9; k++) {
            text_fmt_int(a, sizeof(a), v[k]);
            row_lr(x + 6, yy, w - 12, lab[k], FONT_BODY_DARK, a, (k == 5 || k == 6 || k == 8) && v[k] ? FONT_BODY_RED : FONT_BODY_DARK);
            yy += 11;
        }
        yy += 4;
        draw9(&s9_inset, x + 6, yy, w - 12, 20, COL(UI_COL_PAPER_DK));
        text_fmt_int(a, sizeof(a), cs->emigrated_total);
        str_printf(b, sizeof(b), "Emigrated to Sweden: %s", a);
        if (diff()->sweden_permille > 0 && (hard_sweden()->active || hard_sweden()->total > 0)) {   /* 1.1 D1: 1969-70 */
            char c3[24];
            text_fmt_int(c3, sizeof(c3), hard_sweden()->total);
            str_printf(b + strlen(b), (int)sizeof(b) - (int)strlen(b), " (1969-70: %s)", c3);
        }
        txt_c(x + 6, w - 12, yy + 3, b, FONT_HEAD_RED);
        yy += 26;
        if (hard_sweden_line(b, sizeof(b))) { txt_fit(x + 6, yy - 4, b, FONT_BODY_RED, w - 12); yy += 10; }   /* 1.1 D1 */
        rule(x + 6, yy, w - 12); yy += 6;
        txt(x + 6, yy, "Top problems", FONT_HEAD); yy += 16;
        n = sim_top_problems(pr, 3);
        if (!n) { txt(x + 10, yy, "None worth a letter.", FONT_BODY_GREY); yy += 12; }
        for (k = 0; k < n; k++) {
            str_printf(b, sizeof(b), "%d. ", k + 1);
            text_problem(b + 3, sizeof(b) - 3, &pr[k]);
            txt_fit(x + 10, yy, b, FONT_BODY_RED, w - 16);
            yy += 12;
        }
        yy += 4;
        rule(x + 6, yy, w - 12); yy += 6;
        str_printf(b, sizeof(b), "Status: %s", text_status_name(cs->town_status));
        txt(x + 6, yy, b, FONT_BODY_DARK);
        {   /* PL1 (PLAYTEST 2 item 1): "Next: Market town at 3,000 (you have 2,750)" + progress bar; the decree when it waits */
            char pg[96];
            int pct = advice_progress(pg, sizeof(pg), NULL), sw = txtw(b, FONT_BODY_DARK) + 12;
            if (advice_rights_waiting()) {
                yy += 12;
                button(x + 6, yy, w - 12, 18, pg, BT_RIGHTS);
                yy += 20;
            } else {
                txt_fit(x + sw, yy, pg, FONT_BODY_GREY, w - 6 - sw);
                yy += 12;
                if (cs->town_status + 1 < TS_COUNT) { bar(x + 6, yy, w - 12, 6, pct, 0, COL(UI_COL_GREEN)); yy += 9; }
            }
        }
        str_printf(b, sizeof(b), "Lots: %d houses, %d apartments, %d shops, %d industry",
                   cs->lots_tier[ZONE_HOUSES][1] + cs->lots_tier[ZONE_HOUSES][2] + cs->lots_tier[ZONE_HOUSES][3],
                   cs->lots_tier[ZONE_APARTMENTS][1] + cs->lots_tier[ZONE_APARTMENTS][2] + cs->lots_tier[ZONE_APARTMENTS][3],
                   cs->lots_tier[ZONE_COMMERCIAL][1] + cs->lots_tier[ZONE_COMMERCIAL][2] + cs->lots_tier[ZONE_COMMERCIAL][3],
                   cs->lots_tier[ZONE_INDUSTRY][1] + cs->lots_tier[ZONE_INDUSTRY][2] + cs->lots_tier[ZONE_INDUSTRY][3]);
        if (yy < y + h - 12) txt_fit(x + 6, yy, b, FONT_BODY_GREY, w - 12);
    }
}

/* M5 (scope review: at most 5 lines per tier): up to 5 items as they are; a longer list shows the unmet items first
   (the first failing one at the top, red), then met ones, and sums up the rest in a fifth line */
#define CHECKLIST_LINES 5
static void checklist(int x, int *yy, int w, const Checklist *cl, int ymax)
{
    char b[96];
    int k, order[REQ_COUNT], n = 0, shown, rest_met = 0, rest_unmet = 0;
    if (cl->n <= CHECKLIST_LINES) for (k = 0; k < cl->n; k++) order[n++] = k;
    else {
        for (k = 0; k < cl->n; k++) if (!cl->item[k].met) order[n++] = k;
        for (k = 0; k < cl->n; k++) if (cl->item[k].met) order[n++] = k;
    }
    shown = cl->n <= CHECKLIST_LINES ? cl->n : CHECKLIST_LINES - 1;
    for (k = shown; k < n; k++) { if (cl->item[order[k]].met) rest_met++; else rest_unmet++; }
    for (k = 0; k < shown && *yy < ymax; k++) {
        text_req_line(b, sizeof(b), &cl->item[order[k]]);
        txt_fit(x, *yy, b, cl->item[order[k]].met ? FONT_BODY_GREEN : FONT_BODY_RED, w);
        *yy += 12;
    }
    if ((rest_met || rest_unmet) && *yy < ymax) {
        if (rest_unmet) str_printf(b, sizeof(b), "%s %d more missing, %d met", GLYPH_CROSS, rest_unmet, rest_met);
        else {
            int len = str_printf(b, sizeof(b), "%s", GLYPH_CHECK);
            for (k = shown; k < n && len < (int)sizeof(b) - 24; k++)
                len += str_printf(b + len, (int)sizeof(b) - len, "%s %s", k == shown ? "" : ",", text_req_label(cl->item[order[k]].req));
        }
        txt_fit(x, *yy, b, rest_unmet ? FONT_BODY_RED : FONT_BODY_GREEN, w);
        *yy += 12;
    }
}

static void score_terms(int x, int *yy, int w, s32 score, const Term *t, int n, int kind)
{
    char a[48], b[64];
    int k, col2 = w / 2, rows;
    text_fmt_signed(a, sizeof(a), score);
    str_printf(b, sizeof(b), "Growth score %s (grows above %d)", a, SIM_T_GROW);
    txt(x, *yy, b, FONT_BODY_DARK);
    *yy += 13;
    rows = (n + 1) / 2;
    if (!rows) return;
    draw9(&s9_inset, x, *yy, w, rows * 11 + 5, COL(UI_COL_PAPER_DK));
    for (k = 0; k < n; k++) {
        int cx = x + 5 + (k / rows) * col2, cy = *yy + 3 + (k % rows) * 11;
        text_fmt_signed(a, sizeof(a), t[k].amount);
        row_lr(cx, cy, col2 - 12, text_term_label(kind, t[k].id), FONT_BODY_DARK, a,
               t[k].amount > 0 ? FONT_BODY_GREEN : t[k].amount < 0 ? FONT_BODY_RED : FONT_BODY_GREY);
    }
    *yy += rows * 11 + 9;
}

/* M2: bus depot / stop query = the line panel (one row per line: stops, buses, rides a day, status) and the
   city's commute mode split; railway station: what it brings (DT_REGIONAL / DT_FREIGHT demand terms) */
static s32 demand_term(int dem, int id)
{
    const Demand *dm = sim_demand(dem);
    int k;
    for (k = 0; k < dm->nterms; k++) if (dm->terms[k].id == id) return dm->terms[k].amount;
    return 0;
}

static int query_transit(int x, int yy, int w, int ymax, int lot)
{
    char b[96];
    int i, any = 0;
    const Lot *l = world_lot(lot);
    if (!l) return yy;
    if (l->def == B_BUS_DEPOT || l->def == B_BUS_STOP) {
        TrafficStats st;
        if (yy < ymax - 16) { txt(x + 6, yy, "Bus lines", FONT_HEAD); yy += 16; }
        for (i = 0; i < BUS_MAX_LINES && yy < ymax - 11; i++) {
            if (!transit_line(i)->used) continue;
            transit_line_text(i, b, sizeof(b));
            txt_fit(x + 10, yy, b, transit_line(i)->status == BLS_OK ? FONT_BODY_DARK : FONT_BODY_RED, w - 16);
            yy += 11; any = 1;
        }
        if (!any && yy < ymax - 11) { txt_fit(x + 10, yy, "No lines yet: Roads > Bus line, click the stops.", FONT_BODY_GREY, w - 16); yy += 11; }
        traffic_stats(&st);
        if (yy < ymax - 11 && st.trips > 0) {
            str_printf(b, sizeof(b), "Commuters: %d%% by car, %d%% by bus, %d%% walk or cycle", st.car_pct, st.bus_pct, st.slow_pct);
            txt_fit(x + 6, yy, b, FONT_BODY_GREY, w - 12); yy += 12;
        }
    } else if (l->def == B_STATION && yy < ymax - 11) {
        if (!transit_stations()) str_printf(b, sizeof(b), "Not in service: platform to the railway, forecourt to a road");
        else str_printf(b, sizeof(b), "Regional migrants +%d a month " TEXT_DOT " rail freight +%d jobs",
                        (int)demand_term(DEM_R, DT_REGIONAL), (int)demand_term(DEM_I, DT_FREIGHT));
        txt_fit(x + 6, yy, b, transit_stations() ? FONT_BODY_DARK : FONT_BODY_RED, w - 12); yy += 12;
    }
    return yy;
}

static void query_title(char *buf, int size)
{
    if (s_q_lot) {
        LotInfo li;
        if (sim_query_lot(s_q_lot, &li)) { str_copy(buf, def_building(li.def)->name, size); return; }
        str_copy(buf, "Nothing here", size);
        return;
    } else {
        const Tile *t = world_tile(s_q_x, s_q_y);
        if (TILE_ROAD(t)) str_copy(buf, g_roads[TILE_ROAD(t)].name, size);
        else if (TILE_ZONE(t)) str_printf(buf, size, "%s zone", text_zone_name(TILE_ZONE(t)));
        else str_copy(buf, s_terrain_name[ICLAMP(TER_TYPE(t), 0, TER_COUNT - 1)], size);
    }
}

static int query_water(int x, int yy, int w, int ymax, int lot, const BuildingDef *d);   /* F1 */
static void panel_query(int x, int y, int w, int h)
{
    char a[48], b[128], c2[48];
    int yy = y + 5, ymax = y + h - 4, year = cur_year(), k;
    if (s_q_lot) {
        LotInfo li;
        const BuildingDef *d;
        char ic[48];
        if (!sim_query_lot(s_q_lot, &li)) { txt(x + 6, yy, "The building is gone.", FONT_BODY_GREY); return; }
        d = def_building(li.def);
        str_printf(ic, sizeof(ic), d->cat == BCAT_ZONE ? "z_%s" : "b_%s", d->cat == BCAT_ZONE ?
                   (d->zone == ZONE_HOUSES ? "houses" : d->zone == ZONE_APARTMENTS ? "apartments" : d->zone == ZONE_COMMERCIAL ? "commercial" :
                    d->zone == ZONE_FORESTRY ? "forestry" : d->zone == ZONE_FARM ? "farm" : "industry")   /* M3 */
                   : d->key);
        if (d->flags & BDF_MODULE)              /* PL3: complex modules show their complex's icon (no per-module icons) */
            str_copy(ic, complex_kind_of_def(li.def) == CPX_PAPER ? "b_paper_mill" : "b_valtion_metalli", sizeof(ic));
        spr(icon_id(ic, li.status == LOT_ABANDONED), x + 6, yy + 2);
        { const Lot *l = world_lot(s_q_lot); text_address(l ? l->x : 0, l ? l->y : 0, b, sizeof(b)); }
        txt_fit(x + 24, yy, b, FONT_BODY_DARK, w - 30);
        if (d->cat == BCAT_ZONE) {
            text_fmt_int(a, sizeof(a), li.occupants); text_fmt_int(c2, sizeof(c2), li.capacity);
            str_printf(b, sizeof(b), "Tier %d of 3 " TEXT_DOT " %s/%s %s " TEXT_DOT " %s", li.tier, a, c2,
                       ZONE_DEMAND(d->zone) == DEM_R ? "residents" : "jobs", text_lot_status(li.status));
        } else str_printf(b, sizeof(b), "%s " TEXT_DOT " built %d", text_lot_status(li.status), year - li.age_months / 12);
        txt_fit(x + 24, yy + 11, b, FONT_BODY_GREY, w - 30);
        yy += 26;
        rule(x + 6, yy, w - 12); yy += 6;
        if (d->cat != BCAT_ZONE && advice_purpose(li.def)) {     /* PL1: what it is for (heat plant: "Lets apartment blocks grow to tier 3") */
            txt_fit(x + 6, yy, advice_purpose(li.def), FONT_BODY_GREY, w - 12); yy += 12;
        }
        if (d->cat == BCAT_ZONE) {
            if (d->water_use > 0) yy = query_water(x, yy, w, ymax, s_q_lot, d);   /* F1 */
            for (k = 0; k < 4 && yy < ymax - 80; k++) {     /* M3: why this kind, its input, exports; farms, camps */
                if (!industry_lot_line(s_q_lot, k, b, sizeof(b))) break;
                txt_fit(x + 6, yy, b, k ? FONT_BODY_GREY : FONT_BODY_DARK, w - 12); yy += 11;
            }
            if (k) yy += 3;
            for (k = 0; k < 4 && yy < ymax - 80; k++) {     /* M5: heating, sewage, fire */
                if (!civic_lot_line(s_q_lot, k, b, sizeof(b))) break;
                txt_fit(x + 6, yy, b, FONT_BODY_DARK, w - 12); yy += 11;
            }
            if (k) yy += 3;
            if (li.next.n) {
                str_printf(b, sizeof(b), "To reach tier %d:", li.next.tier);
                txt(x + 6, yy, b, FONT_HEAD); yy += 16;
                checklist(x + 10, &yy, w - 16, &li.next, ymax - 60);
            } else { txt(x + 6, yy, "Top tier. Nothing more to ask for.", FONT_BODY_GREEN); yy += 14; }
            if (li.growth < 0 && li.growth_threshold < 0) {  /* 1.1 D1: the decline countdown, with the level's speed */
                int wk = IMAX(1, li.growth - li.growth_threshold);
                str_printf(b, sizeof(b), "Declining: a step down in %d week%s%s%s", wk, wk == 1 ? "" : "s",
                           diff_level() != DIFF_EASY ? " " TEXT_DOT " " : "", diff_level() != DIFF_EASY ? diff()->name : "");
                txt_fit(x + 6, yy, b, FONT_BODY_RED, w - 12); yy += 12;
            }
            yy += 4;
            score_terms(x + 6, &yy, w - 12, li.score, li.terms, li.nterms, TERMK_SCORE);
        } else {
            if (d->service != SVC_NONE) {
                text_fmt_int(a, sizeof(a), li.svc_load); text_fmt_int(c2, sizeof(c2), li.svc_capacity);
                str_printf(b, sizeof(b), "%s: %s/%s %s", g_services[d->service].name, a, c2, d->service == SVC_SCHOOL ? "pupils" : "residents served");
                txt_fit(x + 6, yy, b, li.svc_load > li.svc_capacity ? FONT_BODY_RED : FONT_BODY_DARK, w - 12); yy += 12;
                bar(x + 6, yy, w - 12, 9, li.svc_capacity ? li.svc_load * 100 / li.svc_capacity : 0, 0,
                    li.svc_load > li.svc_capacity ? COL(UI_COL_RED) : COL(UI_COL_GREEN));
                yy += 13;
                text_fmt_int(a, sizeof(a), li.catchment_tiles);
                str_printf(b, sizeof(b), "Catchment: %s tiles by road", a);
                txt(x + 6, yy, b, FONT_BODY_DARK); yy += 12;
            }
            if (d->cat == BCAT_POWER) {    /* PL1 (PLAYTEST 2 item 5): like the water source query, for the whole network */
                PowerNetInfo pn;
                if (advice_power_network(s_q_lot, &pn) && pn.supply > 0) {
                    text_fmt_int(a, sizeof(a), pn.demand); text_fmt_int(c2, sizeof(c2), pn.supply);
                    if (pn.lots_short) str_printf(b, sizeof(b), "Power %s / %s used (%d%%) - brownout", a, c2, pn.pct);
                    else { char sp[24]; text_fmt_int(sp, sizeof(sp), pn.spare); str_printf(b, sizeof(b), "Power %s / %s used (%d%%), %s spare", a, c2, pn.pct, sp); }
                    txt_fit(x + 6, yy, b, pn.lots_short ? FONT_BODY_RED : pn.pct >= 90 ? FONT_BODY_AMBER : FONT_BODY_DARK, w - 12); yy += 12;
                    bar(x + 6, yy, w - 12, 7, pn.pct, 0, pn.lots_short ? COL(UI_COL_RED) : pn.pct >= 90 ? COL(UI_COL_AMBER) : COL(UI_COL_GREEN)); yy += 10;
                    if (pn.lots_short) str_printf(b, sizeof(b), "Serves %d buildings; %d in the dark (the furthest first)", pn.lots - pn.lots_short, pn.lots_short);
                    else str_printf(b, sizeof(b), "Serves %d buildings", pn.lots);
                    txt_fit(x + 6, yy, b, pn.lots_short ? FONT_BODY_RED : FONT_BODY_DARK, w - 12); yy += 12;
                    text_fmt_int(a, sizeof(a), li.power_out);
                    if (pn.plants > 1) str_printf(b, sizeof(b), "This plant: %s units of the network's %s (%d plants)", a, c2, pn.plants);
                    else str_printf(b, sizeof(b), "This plant: %s units", a);
                    txt_fit(x + 6, yy, b, FONT_BODY_GREY, w - 12); yy += 12;
                } else if (li.power_out) {
                    text_fmt_int(a, sizeof(a), li.power_out);
                    str_printf(b, sizeof(b), "Power output: %s units (nothing connected yet)", a);
                    txt_fit(x + 6, yy, b, FONT_BODY_DARK, w - 12); yy += 12;
                } else { txt(x + 6, yy, "Power: not producing", FONT_BODY_GREY); yy += 12; }
            } else if (li.power_out) { text_fmt_int(a, sizeof(a), li.power_out); str_printf(b, sizeof(b), "Power output: %s units", a); txt(x + 6, yy, b, FONT_BODY_DARK); yy += 12; }
            yy = query_water(x, yy, w, ymax, s_q_lot, d);      /* F1 (WATER.md 3.6 B/C) */
            if (season_lot_line(s_q_lot, b, sizeof(b))) { txt_fit(x + 6, yy, b, FONT_BODY_GREY, w - 12); yy += 12; }   /* 1.1 W1 */
            if (d->jobs) { text_fmt_int(a, sizeof(a), li.occupants); text_fmt_int(c2, sizeof(c2), d->jobs);
                str_printf(b, sizeof(b), "Staff: %s/%s", a, c2); txt(x + 6, yy, b, FONT_BODY_DARK); yy += 12; }
            {   /* F3: upkeep as booked = def upkeep x funding x the upkeep index (budget.cpp) */
                int ui = budget_upkeep_index(), n2;
                int up = (d->cat == BCAT_SERVICE || d->cat == BCAT_POWER || d->cat == BCAT_WATER || li.def == B_HEAT_PLANT ||
                          li.def == B_SEWAGE_WORKS) ? budget_level_upkeep_pct() : 100;   /* 1.1 D1: the level (budget.cpp) */
                int ot = d->cat == BCAT_SERVICE ? budget_overtime_pct(d->service) : 100;   /* PL2 overtime */
                text_fmt_money(a, sizeof(a), (Money)((s64)li.upkeep * ui / 100 * up / 100 * ot / 100), year);
                n2 = str_printf(b, sizeof(b), "Upkeep: %s a month", a);
                if (ui > 100) n2 += str_printf(b + n2, (int)sizeof(b) - n2, " (index %d%%", ui);
                if (up != 100) n2 += str_printf(b + n2, (int)sizeof(b) - n2, "%s%s x%d%%", ui > 100 ? ", " : " (", diff()->name, up);
                if (ot > 100) n2 += str_printf(b + n2, (int)sizeof(b) - n2, "%sovertime +%d%%", ui > 100 || up != 100 ? ", " : " (", ot - 100);
                if (ui > 100 || up != 100 || ot > 100) str_printf(b + n2, (int)sizeof(b) - n2, ")");
            }
            txt_fit(x + 6, yy, b, FONT_BODY_DARK, w - 12); yy += 14;   /* 1.1 D1: txt_fit (longer upkeep line) */
            if (d->cat == BCAT_SERVICE && hard_note(HN_OVERTIME, a, sizeof(a))) { txt_fit(x + 6, yy - 2, a, FONT_BODY_GREY, w - 12); yy += 11; }
            yy = query_transit(x, yy, w, ymax, s_q_lot);      /* M2 */
            for (k = 0; k < 16 && yy < ymax - 22; k++) {      /* M6: complex panel (stage, jobs, wood, exports, owner, gifts) */
                static const int fonts[5] = { FONT_BODY_DARK, FONT_BODY_GREY, FONT_BODY_RED, FONT_BODY_GREEN, FONT_HEAD };
                int f = 0;
                if (!complex_lot_line(s_q_lot, k, b, sizeof(b), &f)) break;
                if (k == 0) yy += 2;
                txt_fit(x + 6, yy, b, fonts[ICLAMP(f, 0, 4)], w - 12); yy += f == 4 ? 15 : 11;
            }
            for (k = 0; k < 5 && yy < ymax - 11; k++) {       /* M5: heat plant, sewage works, element factory, heating */
                if (!civic_lot_line(s_q_lot, k, b, sizeof(b))) break;
                txt_fit(x + 6, yy, b, k ? FONT_BODY_GREY : FONT_BODY_DARK, w - 12); yy += 11;
            }
            if (k) yy += 3;
        }
        if (yy < ymax - 11) {
            str_printf(b, sizeof(b), "Land value %d " TEXT_DOT " Pollution %d " TEXT_DOT " Happiness %d", li.land_value, li.pollution, li.happy);
            txt_fit(x + 6, yy, b, FONT_BODY_GREY, w - 12);
        }
    } else {
        TileInfo ti;
        if (!sim_query_tile(s_q_x, s_q_y, &ti)) { txt(x + 6, yy, "Off the map.", FONT_BODY_GREY); return; }
        text_address(s_q_x, s_q_y, b, sizeof(b));
        txt_fit(x + 6, yy, b, FONT_BODY_DARK, w - 12);
        str_printf(b, sizeof(b), "Tile %d,%d " TEXT_DOT " %s " TEXT_DOT " %s", s_q_x, s_q_y, ti.powered ? "powered" : "no power",
                   ti.watered ? "water" : "no water");
        txt_fit(x + 6, yy + 11, b, FONT_BODY_GREY, w - 12);
        yy += 26;
        rule(x + 6, yy, w - 12); yy += 6;
        for (k = 0; k < 2; k++) {                     /* M3: forest stage, forestry / farm land */
            if (!industry_tile_line(s_q_x, s_q_y, k, b, sizeof(b))) break;
            txt_fit(x + 6, yy, b, k ? FONT_BODY_GREY : FONT_BODY_DARK, w - 12); yy += 11;
        }
        if (k) yy += 3;
        for (k = 0; k < 3; k++) {                     /* M5: lake water, ruins, crime */
            if (!civic_tile_line(s_q_x, s_q_y, k, b, sizeof(b))) break;
            txt_fit(x + 6, yy, b, FONT_BODY_DARK, w - 12); yy += 11;
        }
        if (k) yy += 3;
        if (ti.zone && !ti.lot) {
            txt(x + 6, yy, "To build here:", FONT_HEAD); yy += 16;
            checklist(x + 10, &yy, w - 16, &ti.build, ymax - 70);
            yy += 4;
            score_terms(x + 6, &yy, w - 12, ti.score, ti.terms, ti.nterms, TERMK_SCORE);
        }
        str_printf(b, sizeof(b), "Land value %d " TEXT_DOT " Pollution %d " TEXT_DOT " Traffic %d", ti.land_value, ti.pollution, ti.traffic);
        txt_fit(x + 6, yy, b, FONT_BODY_DARK, w - 12); yy += 14;
        {   /* M2: road query "Flow 1,840 / capacity 1,200 (153%)", "62% from Kivikko to the industry of Rantala" */
            char tl[3][96];
            int nl = traffic_query_text(s_q_x, s_q_y, tl, 3), i;
            for (i = 0; i < nl && yy < ymax - 11; i++) { txt_fit(x + 6, yy, tl[i], i == 0 && traffic_flow(s_q_x, s_q_y) >= 128 ? FONT_BODY_RED : FONT_BODY_DARK, w - 12); yy += 12; }
            if (nl) yy += 2;
        }
        if (yy < ymax - 24) {
            txt(x + 6, yy, "Service cover", FONT_BODY_GREY); yy += 12;
            for (k = 0; k < SVC_COUNT && yy < ymax - 11; k++) {
                int cx = x + 10 + (k % 2) * ((w - 16) / 2);
                text_fmt_pct(a, sizeof(a), ti.coverage[k]);
                row_lr(cx, yy, (w - 16) / 2 - 10, g_services[k].name, FONT_BODY_DARK, a, ti.coverage[k] >= 40 ? FONT_BODY_GREEN : FONT_BODY_RED);
                if (k % 2) yy += 11;
            }
        }
    }
}

/* one legend swatch: a flat rectangle in the average colour of the overlay diamond ovl_<R>_<L> (computed
   once from the sprite's system-memory pixels; blitting the world sheet itself depends on the zoom
   residency, art.h) with a pencil outline */
static PIX s_swatch[OVR_COUNT][8];
static u8 s_swatch_ok[OVR_COUNT][8];
static void swatch(int ramp, int level, int x, int y)
{
    if (ramp < 0 || ramp >= OVR_COUNT || level < 0 || level > 7) return;
    if (!s_swatch_ok[ramp][level]) {
        int z, pitch, px, py, r, g, b, n = 0, sr = 0, sg = 0, sb = 0, id = art_findf("ovl_%d_%d", ramp, level);
        for (z = 0; z < ZOOM_COUNT && !n; z++) {
            const PIX *p = art_pixels(z, id, &pitch);
            const ArtSprite *sp = art_get(z, id);
            if (!p) continue;
            for (py = 0; py < sp->h; py++)
                for (px = 0; px < sp->w; px++) {
                    PIX c = p[py * pitch + px];
                    if (c == gfx_key()) continue;
                    gfx_unpack(c, &r, &g, &b);
                    sr += r; sg += g; sb += b; n++;
                }
        }
        s_swatch[ramp][level] = n ? gfx_rgb(sr / n, sg / n, sb / n) : gfx_rgb(UI_COL_GREY);
        s_swatch_ok[ramp][level] = 1;
    }
    fill(x, y + 2, x + 28, y + 14, s_swatch[ramp][level]);
    gfx_rect(x, y + 2, x + 28, y + 14, gfx_rgb(UI_COL_PENCIL));
    s_nblit += 4;
}

/* F1: water lines of a lot query: a source's output/used/reach (WATER.md 3.6 B), a consumer's one line (C) */
static int query_water(int x, int yy, int w, int ymax, int lot, const BuildingDef *d)
{
    char a[32], b[96], c[32];
    WaterSrcInfo wi;
    if (d->cat == BCAT_WATER) {
        if (!net_water_source(lot, &wi)) { txt(x + 6, yy, "Water: not in use yet", FONT_BODY_GREY); return yy + 12; }
        text_fmt_int(a, sizeof(a), wi.used); text_fmt_int(c, sizeof(c), wi.output);
        if (wi.needs_power) str_printf(b, sizeof(b), "Output 0 units: needs power (%d shore tiles)", wi.shore);
        else if (wi.shore) str_printf(b, sizeof(b), "%s / %s used (%d shore tiles)%s", a, c, wi.shore, wi.at_capacity ? " - at capacity" : "");
        else if (d->capacity && !wi.output) str_printf(b, sizeof(b), "Output 0: no pump or well on its network");
        else str_printf(b, sizeof(b), "%s / %s used%s", a, c, wi.at_capacity ? " - at capacity" : "");
        txt_fit(x + 6, yy, b, wi.at_capacity || wi.needs_power ? FONT_BODY_RED : FONT_BODY_DARK, w - 12); yy += 12;
        if (yy < ymax - 12) {
            text_fmt_int(a, sizeof(a), wi.own_used); text_fmt_int(c, sizeof(c), wi.shared_used);
            str_printf(b, sizeof(b), "Serves %d buildings, reach %d tiles (own %s, shared %s)", wi.lots, wi.reach, a, c);
            txt_fit(x + 6, yy, b, FONT_BODY_DARK, w - 12); yy += 12;
        }
        if (yy < ymax - 12) {
            if (wi.lots_dry) { text_fmt_int(a, sizeof(a), wi.dry_units); str_printf(b, sizeof(b), "%d dry beyond (%s units): build a pump or well nearby", wi.lots_dry, a); }
            else { text_fmt_int(a, sizeof(a), wi.spare); str_printf(b, sizeof(b), "Spare %s units", a); }
            txt_fit(x + 6, yy, b, wi.lots_dry ? FONT_BODY_RED : FONT_BODY_GREY, w - 12); yy += 12;
        }
        if (lot && g_world.lots[lot].def == B_WATER_TOWER && yy < ymax - 12) {
            str_printf(b, sizeof(b), "Pressure zone %d tiles: %d buildings (apartments tier 2+)", TOWER_PRESSURE_REACH, wi.pressure_lots);
            txt_fit(x + 6, yy, b, FONT_BODY_DARK, w - 12); yy += 12;
        }
        return yy;
    }
    if (d->water_use > 0 && yy < ymax - 12) {
        int st = net_lot_water_state(lot), dist, src = net_lot_water_src(lot, &dist);
        const Lot *sl = world_lot(src);
        if (st == NLS_OK && sl) str_printf(b, sizeof(b), "Water: yes - %s, %d tiles along pipes", def_building(sl->def)->name, dist);
        else if (st == NLS_SHORT && sl && net_water_source(src, &wi)) {
            text_fmt_int(a, sizeof(a), wi.used); text_fmt_int(c, sizeof(c), wi.output);
            str_printf(b, sizeof(b), "Water: no - pipes reach here, %s is out of water (%s / %s)", def_building(sl->def)->name, a, c);
        } else str_printf(b, sizeof(b), "Water: no - not connected (no pipe or watered building next to it)");
        txt_fit(x + 6, yy, b, st == NLS_OK ? FONT_BODY_DARK : FONT_BODY_RED, w - 12); yy += 12;
    }
    return yy;
}

/* legend for an overlay: ramp swatches (world art ovl_<R>_<L>) with labels */
static void legend(int x, int y, int w, int ovl)
{
    static const char *const netc[4] = { "Supplied", "Not connected", "Shortage", "Line / main" };
    static const char *const watc[5] = { "Water source", "Not connected", "Short (beyond reach)", "Watered / wet pipe", "New lots get water" }; /* F1; F3: watered lots blue */
    static const char *const growc[GROWC_COUNT] = { "Growing", "Road", "Power", "Water", "Services", "Land value",
        "Pollution", "Other" };
    const char *lo = NULL, *hi = NULL;
    const char *const *cats = NULL;
    int ramp = -1, ncat = 0, k;
    switch (ovl) {
    case OVL_POWER: ramp = OVR_NET; cats = netc; ncat = 4; break;
    case OVL_WATER: ramp = OVR_NET; cats = watc; ncat = 5; break;
    case OVL_POLLUTION: case OVL_FIRE_RISK: case OVL_TRAFFIC: ramp = OVR_BAD; lo = "low"; hi = "heavy"; break;
    case OVL_LAND_VALUE: ramp = OVR_GOOD; lo = "low"; hi = "high"; break;
    case OVL_GROWTH: ramp = OVR_PROBLEM; cats = growc; ncat = GROWC_COUNT; break;
    case OVL_ZONES: ramp = OVR_ZONE; break;
    case OVL_HEAT: {                                  /* M5 */
        static const char *const heatc[5] = { "Heated", "Not connected", "Short of heat", "Heated pipe", "New lots get heat" };   /* PL3 */
        ramp = OVR_NET; cats = heatc; ncat = 5; break; }
    case OVL_NONE: break;
    default: ramp = OVR_COVER; lo = "none"; hi = "full"; break;
    }
    if (ramp < 0) { txt(x, y, "Choose an overlay to tint the map.", FONT_BODY_GREY); return; }
    if (ramp == OVR_ZONE) {
        for (k = 1; k < ZONE_COUNT; k++) {
            int cx = x + ((k - 1) % 2) * (w / 2), cy = y + ((k - 1) / 2) * 18;
            swatch(ramp, k, cx, cy);
            txt(cx + 36, cy + 3, text_zone_name(k), FONT_BODY_DARK);
        }
        return;
    }
    if (cats) {
        for (k = 0; k < ncat; k++) {
            int cx = x + (k % 2) * (w / 2), cy = y + (k / 2) * 18;
            swatch(ramp, k, cx, cy);
            txt(cx + 36, cy + 3, cats[k], FONT_BODY_DARK);
        }
        if (ovl == OVL_WATER) {                       /* M5: the lake tint in the free sixth slot */
            swatch(OVR_BAD, 5, x + w / 2, y + 2 * 18);
            txt(x + w / 2 + 36, y + 2 * 18 + 3, "Lake: untreated sewage", FONT_BODY_DARK);
        }
        return;
    }
    for (k = 0; k < 8; k++) swatch(ramp, k, x + k * 34, y);
    txt(x, y + 18, lo, FONT_BODY_GREY);
    txt_r(x + 7 * 34 + 32, y + 18, hi, FONT_BODY_GREY);
    if (ovl == OVL_FIRE_RISK) {                       /* 1.1 D1: the level's fire numbers */
        char fn[96];
        if (hard_note(HN_FIRE, fn, sizeof(fn))) txt_fit(x, y + 31, fn, FONT_BODY_GREY, w);
    }
}

/* P1 (DESIGN 14.7 "<= 9 overlays"): the list shows 9 overlays + "No overlay". District heating is a sub-view of
   Water (also the water key: water -> heat -> off) and the 7 service covers are sub-views of one "Services" entry
   (fire, schools, health, saunas, police, libraries, post); a row of sub-choices appears under the list when one of
   the two groups is active. Sewage is the lake tint inside the water view. */
static int s_svc_ovl_last = OVL_SVC_SCHOOL;
static int ovl_is_svc(int o) { return o >= OVL_SVC_FIRE && o <= OVL_SVC_POST; }
static const char *ovl_icon(int k, char *ic, int size)
{
    if (k == OVL_HEAT) { str_copy(ic, "b_heat_plant", size); return ic; }          /* M5: the building icon (13x13 too) */
    if (ovl_is_svc(k)) { str_printf(ic, size, "o_svc_%s", g_services[k - OVL_SVC_FIRE].key); return ic; }
    str_printf(ic, size, "o_%s", k == OVL_NONE ? "none" : k == OVL_POWER ? "power" : k == OVL_WATER ? "water" :
               k == OVL_POLLUTION ? "pollution" : k == OVL_LAND_VALUE ? "land_value" : k == OVL_TRAFFIC ? "traffic" :
               k == OVL_FIRE_RISK ? "fire_risk" : k == OVL_GROWTH ? "growth" : k == OVL_ZONES ? "zones" : "none");
    return ic;
}

static int ovl_radio(int cx, int cy, int cw, int k, int on, const char *label)
{
    char ic[32];
    int idx = ctl_add(cx, cy, cw - 4, 16, CK_OVL, k, 0);
    if (ctl_hot(idx)) draw3h(&s3_flyhover, cx - 2, cy - 2, cw - 2);
    spr(S.radio[on ? 1 : 0][ctl_hot(idx) ? 1 : 0], cx, cy + 1);
    spr(icon_id(ovl_icon(k, ic, sizeof(ic)), 0), cx + 15, cy);
    txt_fit(cx + 32, cy + 1, label, FONT_BODY_DARK, cw - 36);
    return idx;
}

static void panel_overlays(int x, int y, int w, int h)
{
    static const int menu[10] = { OVL_NONE, OVL_POWER, OVL_WATER, OVL_POLLUTION, OVL_LAND_VALUE, OVL_TRAFFIC,
                                  OVL_FIRE_RISK, OVL_SVC_SCHOOL, OVL_GROWTH, OVL_ZONES };
    char ic[48];
    int k, n = 0, cw = (w - 12) / 2, yy = y + 6, cur = s_ovl_local;
    int water = cur == OVL_WATER || cur == OVL_HEAT, svc = ovl_is_svc(cur);
    UNUSED(h);
    if (svc) s_svc_ovl_last = cur;
    for (k = 0; k < 10; k++) {
        int o = menu[k], on = o == cur;
        const char *label = text_overlay_name(o);
        if (o == OVL_WATER) { on = water; label = "Water and heat"; }
        if (o == OVL_SVC_SCHOOL) { o = s_svc_ovl_last; on = svc; label = "Services"; }
        ovl_radio(x + 6 + (n % 2) * cw, yy + (n / 2) * 17, cw, o, on, label);
        n++;
    }
    yy += ((n + 1) / 2) * 17 + 2;
    if (water || svc) {                                /* the group's sub-views */
        int m = 0;
        draw9(&s9_inset, x + 6, yy, w - 12, water ? 6 + 17 : 6 + 4 * 17, COL(UI_COL_PAPER_DK));
        yy += 4;
        if (water) {
            ovl_radio(x + 10, yy, cw - 2, OVL_WATER, cur == OVL_WATER, "Water pipes");
            ovl_radio(x + 10 + cw - 2, yy, cw - 2, OVL_HEAT, cur == OVL_HEAT, text_overlay_name(OVL_HEAT));
            yy += 17;
        } else {
            for (k = OVL_SVC_FIRE; k <= OVL_SVC_POST; k++, m++)
                ovl_radio(x + 10 + (m % 2) * (cw - 2), yy + (m / 2) * 17, cw - 2, k, cur == k, text_overlay_name(k));
            yy += ((m + 1) / 2) * 17;
        }
        yy += 4;
    }
    checkbox(x + 6, yy, s_show_buildings, "Show buildings (Tab)", CHK_BUILDINGS);
    yy += 16;
    rule(x + 6, yy, w - 12); yy += 6;
    str_printf(ic, sizeof(ic), "Legend: %s", text_overlay_name(s_ovl_local));
    txt(x + 6, yy, ic, FONT_HEAD); yy += 18;
    legend(x + 8, yy, w - 16, s_ovl_local);
}

static void panel_services(int x, int y, int w, int h)
{
    char a[48], b[48], ic[32];
    const CityStats *cs = sim_stats();
    int k, yy = y + 6;
    UNUSED(h);
    txt_r(x + w - 6, yy, "load / capacity", FONT_BODY_GREY);
    txt(x + 6, yy, "Service", FONT_BODY_GREY);
    yy += 13;
    for (k = 0; k < SVC_COUNT; k++) {
        s32 ld = cs->svc_load[k], cp = cs->svc_capacity[k];
        int pct = cp ? (int)((s64)ld * 100 / cp) : 0;
        PIX c = pct >= 100 ? COL(UI_COL_RED) : pct >= 80 ? COL(UI_COL_AMBER) : COL(UI_COL_GREEN);
        str_printf(ic, sizeof(ic), "s_%s", g_services[k].key);
        spr(icon_id(ic, cp == 0), x + 6, yy);
        txt(x + 24, yy + 1, g_services[k].name, cp ? FONT_BODY_DARK : FONT_BODY_GREY);
        if (cp) { text_fmt_int(a, sizeof(a), ld); text_fmt_int(b, sizeof(b), cp); str_printf(ic, sizeof(ic), "%s / %s", a, b); }
        else str_copy(ic, "no buildings", sizeof(ic));
        txt_r(x + w - 6, yy + 1, ic, cp ? (pct > 100 ? FONT_BODY_RED : FONT_BODY_DARK) : FONT_BODY_GREY);
        bar(x + 24, yy + 13, w - 30, 7, pct, 0, c);
        yy += 26;
    }
    rule(x + 6, yy, w - 12); yy += 6;
    text_fmt_int(a, sizeof(a), cs->power_demand); text_fmt_int(b, sizeof(b), cs->power_supply);
    str_printf(ic, sizeof(ic), "%s / %s units", a, b);
    row_lr(x + 6, yy, w - 12, cs->power_supply > 0 && cs->power_demand * 10 >= cs->power_supply * 9 && cs->power_demand <= cs->power_supply ?
           "Power used / supplied (nearly full)" : "Power used / supplied", FONT_BODY_DARK, ic,
           cs->power_demand > cs->power_supply ? FONT_BODY_RED : cs->power_demand * 10 >= cs->power_supply * 9 ? FONT_BODY_AMBER : FONT_BODY_DARK);
    yy += 11;
    text_fmt_int(a, sizeof(a), net_stats(NET_WATER)->used); text_fmt_int(b, sizeof(b), cs->water_supply);   /* F1: delivered */
    str_printf(ic, sizeof(ic), "%s / %s units", a, b);
    row_lr(x + 6, yy, w - 12, "Water used / supplied", FONT_BODY_DARK, ic, cs->water_short_lots > 0 ? FONT_BODY_RED : FONT_BODY_DARK);
}

static void panel_settings(int x, int y, int w, int h)
{
    static const char *const lab[6] = { "Resume", "Save game", "Quicksave (F5)", "Load game", "Main menu", "Quit to Windows" };
    int k, bw = w - 40;
    UNUSED(h);
    for (k = 0; k < 6; k++) button(x + 20, y + 8 + k * 25, bw, 20, lab[k], BT_RESUME + k);
    txt_c(x, w, y + 8 + 6 * 25 + 4, GAME_TITLE " " TEXT_DOT " Version " GAME_VERSION, FONT_BODY_GREY);
}

static void panel_size(int p, int *w, int *h)
{
    switch (p) {
    case PANEL_BUDGET:     *w = 340; *h = 320; break;
    case PANEL_POPULATION: *w = 340; *h = 316; break;
    case PANEL_QUERY:      *w = 320; *h = 300; break;
    case PANEL_OVERLAYS:   *w = 340; *h = 286; break;
    case PANEL_SERVICES:   *w = 320; *h = 262; break;
    case PANEL_SETTINGS:   *w = 200; *h = 210; break;
    case PANEL_POLITICS:   *w = 340; *h = 320; break;     /* M1 */
    case PANEL_GRAPHS: case PANEL_TRADE: *w = 340; *h = 320; break;   /* M7 */
    default:               *w = 260; *h = 100; break;
    }
    *w = IMIN(*w, WIN_MAX_W); *h = IMIN(*h, WIN_MAX_H);
}

static void panel_title(char *buf, int size)
{
    switch (s_panel) {
    case PANEL_BUDGET:     str_copy(buf, "Budget", size); break;
    case PANEL_POPULATION: str_copy(buf, "Population and demand", size); break;
    case PANEL_QUERY:      query_title(buf, size); break;
    case PANEL_OVERLAYS:   str_copy(buf, "Overlays", size); break;
    case PANEL_SERVICES:   str_copy(buf, "Services", size); break;
    case PANEL_GRAPHS: case PANEL_TRADE: uim7_title(s_panel, buf, size); break;   /* M7 */
    case PANEL_POLITICS:   uipol_title(buf, size); break;      /* M1 */
    case PANEL_SETTINGS:   str_copy(buf, "Game", size); break;
    default:               str_copy(buf, "", size); break;
    }
}

static void compose_win(void)
{
    char title[64];
    Cache *c = &s_c[C_WIN];
    int ix, iy, iw, ih, idx;
    panel_size(s_panel, &s_win_w, &s_win_h);
    s_win_x = L->map.x + L->map.w - s_win_w - 8;
    s_win_y = L->map.y + 8;
    c->x = s_win_x; c->y = s_win_y;
    begin_compose(c, 1, NULL);
    s_nctl = 0;
    draw9(&s9_win, s_win_x, s_win_y, s_win_w, s_win_h, COL(UI_COL_PAPER));
    panel_title(title, sizeof(title));
    txt_fit(s_win_x + 8, s_win_y + 3, title, FONT_HEAD_LIGHT, s_win_w - 36);
    idx = ctl_add(s_win_x + s_win_w - 19, s_win_y + 3, 13, 13, CK_CLOSE, 0, 0);
    spr(S.close[ctl_hot(idx) ? (s_down ? 2 : 1) : 0], s_win_x + s_win_w - 19, s_win_y + 3);
    ix = s_win_x + 6; iy = s_win_y + 19; iw = s_win_w - 12; ih = s_win_h - 26;
    switch (s_panel) {
    case PANEL_BUDGET:     panel_budget(ix, iy, iw, ih); break;
    case PANEL_POPULATION: panel_population(ix, iy, iw, ih); break;
    case PANEL_QUERY:      panel_query(ix, iy, iw, ih); break;
    case PANEL_OVERLAYS:   panel_overlays(ix, iy, iw, ih); break;
    case PANEL_SERVICES:   panel_services(ix, iy, iw, ih); break;
    case PANEL_SETTINGS:   panel_settings(ix, iy, iw, ih); break;
    case PANEL_POLITICS:   uipol_panel(ix, iy, iw, ih); break;  /* M1 */
    case PANEL_GRAPHS: case PANEL_TRADE: uim7_panel(s_panel, ix, iy, iw, ih); break;   /* M7 */
    default:               txt(ix + 6, iy + 8, "Graphs arrive in a later version.", FONT_BODY_GREY); break;
    }
    end_compose(C_WIN);
}

/* data signature of the open panel (checked twice a second) */
static u32 panel_data_sig(void)
{
    u32 h = HASH_INIT;
    int v[4];
    v[0] = s_panel; v[1] = s_tab[s_panel]; v[2] = cur_year() * 12 + sim_date()->month; v[3] = s_ovl_local * 2 + s_show_buildings;
    h = hash_bytes(h, v, sizeof(v));
    switch (s_panel) {
    case PANEL_BUDGET:
        h = hash_bytes(h, &g_budget, sizeof(g_budget));
        break;
    case PANEL_POPULATION: {
        int d;
        h = hash_bytes(h, sim_stats(), sizeof(CityStats));
        for (d = 0; d < DEM_COUNT; d++) h = hash_bytes(h, sim_demand(d), sizeof(Demand));
        break; }
    case PANEL_QUERY:
        if (s_q_lot) { LotInfo li; PowerNetInfo pn; memset(&li, 0, sizeof(li)); sim_query_lot(s_q_lot, &li); h = hash_bytes(h, &li, sizeof(li));
                       if (def_building(li.def)->cat == BCAT_POWER) { advice_power_network(s_q_lot, &pn); h = hash_bytes(h, &pn, sizeof(pn)); } }   /* PL1 */
        else { TileInfo ti; memset(&ti, 0, sizeof(ti)); sim_query_tile(s_q_x, s_q_y, &ti); h = hash_bytes(h, &ti, sizeof(ti)); }
        break;
    case PANEL_SERVICES:
        h = hash_bytes(h, sim_stats(), sizeof(CityStats));
        break;
    case PANEL_POLITICS:
        h = uipol_data_sig(h);      /* M1 */
        break;
    case PANEL_GRAPHS: case PANEL_TRADE:
        h = uim7_data_sig(s_panel, h);   /* M7 */
        break;
    }
    return h;
}

static u32 panel_hot_sig(void)
{
    int v[4];
    v[0] = (s_hot >= H_WIN) ? s_hot : 0;
    v[1] = s_down;
    v[2] = s_drag_ctl;
    v[3] = s_drag_val;
    return hash_ints(v, 4);
}

/* ================================================================== public panel api */
void ui_open_panel(int panel)
{
    if (panel <= PANEL_NONE || panel >= PANEL_COUNT) { ui_close_panel(); return; }
    s_panel = panel;
    s_drag_ctl = -1;
    s_nctl = 0;
    s_c[C_WIN].dirty = 1;
    fly_close();
    if (panel == PANEL_POLITICS) uipol_on_open();   /* M1 */
}

void ui_open_query(int lot, int x, int y)
{
    s_q_lot = lot; s_q_x = x; s_q_y = y;
    ui_open_panel(PANEL_QUERY);
}

void ui_close_panel(void)
{
    s_panel = PANEL_NONE;
    s_drag_ctl = -1;
    s_nctl = 0;
}

int ui_panel(void) { return s_panel; }

/* ================================================================== hit testing + input */
static int hit_test(int mx, int my)
{
    int i, x, y, w, h;
    if (mx < 0 || my < 0) return H_NONE;
    /* popups and the panel lie over everything else */
    if (s_fly >= 0) {
        if (in_rect(mx, my, s_fly_x, s_fly_y, s_fly_w, s_fly_h)) {
            i = (my - s_fly_y - 6) / 17;
            if (my >= s_fly_y + 6 && i >= 0 && i < s_keys[s_fly].n) return H_FLYROW0 + i;
            return H_FLY;
        }
    }
    if (s_panel && in_rect(mx, my, s_win_x, s_win_y, s_win_w, s_win_h)) {
        for (i = s_nctl - 1; i >= 0; i--)
            if (in_rect(mx, my, s_ctl[i].x, s_ctl[i].y, s_ctl[i].w, s_ctl[i].h)) return H_CTL0 + i;
        return H_WIN;
    }
    if (in_rect(mx, my, L->top.x, L->top.y, L->top.w, L->top.h)) {
        for (i = 0; i < 4; i++) { speed_rect(i, &x, &y, &w, &h); if (in_rect(mx, my, x, y, w, h)) return H_SPEED0 + i; }
        if (in_rect(mx, my, L->menu_key.x, L->menu_key.y, L->menu_key.w, L->menu_key.h)) return H_MENU;
        for (i = 0; i < 2 && L->zoom_key.w > 0; i++) { zoom_rect(i, &x, &y, &w, &h); if (in_rect(mx, my, x, y, w, h)) return H_ZOOM0 + i; }   /* F3 */
        for (i = 0; i < 7; i++) if (s_topr[i][2] && in_rect(mx, my, s_topr[i][0], s_topr[i][1], s_topr[i][2], s_topr[i][3])) return H_DATE + i;
        return H_TOP;
    }
    if (in_rect(mx, my, L->panel.x, L->panel.y, L->panel.w, L->panel.h)) {
        for (i = 0; i < NKEYS; i++) { key_rect(i, &x, &y, &w, &h); if (in_rect(mx, my, x, y, w - 1, h - 1)) return H_KEY0 + i; }
        for (i = 0; i < NMINI && i < L->mini_count; i++) { mini_rect(i, &x, &y, &w, &h); if (in_rect(mx, my, x, y, w - 1, h - 1)) return H_MINI0 + i; }
        zonev_rect(&x, &y, &w, &h);
        if (in_rect(mx, my, x, y, w - 1, h - 1)) return H_ZONEV;                     /* F3 */
        for (i = 0; i < NM7K && i + 1 < L->mini_count; i++) { m7k_rect(i, &x, &y, &w, &h); if (in_rect(mx, my, x, y, w - 1, h - 1)) return H_M7K0 + i; }   /* M7 + PL1 */
        guide_rect(&x, &y, &w, &h);
        if (in_rect(mx, my, x, y, w - 1, h - 1)) return H_GUIDE;                    /* PL1 */
        if (in_rect(mx, my, L->mmap.x, L->mmap.y, L->mmap.w, L->mmap.h)) return H_MMAP;
        if (in_rect(mx, my, L->plaque.x, L->plaque.y, L->plaque.w, L->plaque.h)) return H_PLAQUE;
        return H_SIDE;
    }
    if (in_rect(mx, my, L->ticker.x, L->ticker.y, L->ticker.w, L->ticker.h)) {
        for (i = 0; i < s_ntickr; i++) if (in_rect(mx, my, s_tickr[i][0], s_tickr[i][1], s_tickr[i][2], s_tickr[i][3])) return H_TICK0 + i;
        return H_TICKER;
    }
    return H_NONE;
}

static void mmap_camera(int mx, int my)
{
    int tx, ty;
    render_minimap_tile(L->mmap.w, L->mmap.h, mx - L->mmap.x, my - L->mmap.y, &tx, &ty);
    tx = ICLAMP(tx, 0, IMAX(0, g_world.w - 1)); ty = ICLAMP(ty, 0, IMAX(0, g_world.h - 1));
    cmd(UC_CAMERA, tx, ty);
}

static int slider_value(const Ctl *c, int mx)
{
    int vmin = c->a < 2 ? 0 : 50, vmax = c->a < 2 ? 20 : 120, w = c->w - 8;
    return ICLAMP(vmin + ((mx - (c->x + 4)) * (vmax - vmin) + w / 2) / IMAX(1, w), vmin, vmax);
}

static void slider_emit(int which, int v)
{
    if (which < 2) {
        int r = s_tax_res >= 0 ? s_tax_res : g_budget.tax_res, b = s_tax_bus >= 0 ? s_tax_bus : g_budget.tax_bus;
        if (which == 0) r = v; else b = v;
        s_tax_res = r; s_tax_bus = b;
        cmd(UC_SET_TAX, r, b);
    } else cmd(UC_SET_FUNDING, which - 2, v);
}

static void ctl_click(int i)
{
    const Ctl *c = &s_ctl[i];
    switch (c->kind) {
    case CK_CLOSE: ui_close_panel(); break;
    case CK_TAB:   s_tab[s_panel] = c->a; s_c[C_WIN].dirty = 1; break;
    case CK_CHECK:
        if (c->a == CHK_BUILDINGS) { s_show_buildings = !s_show_buildings; cmd(UC_TOGGLE_BUILDINGS, s_show_buildings, 0); }
        break;
    case CK_OVL:
        s_ovl_local = c->a;
        cmd(UC_OVERLAY, c->a, 0);
        break;
    case CK_POL:                                      /* M1; M7 panels reuse the uix_* controls */
        if (s_panel == PANEL_GRAPHS || s_panel == PANEL_TRADE) uim7_click(s_panel, c->a, c->b); else uipol_click(c->a, c->b);
        break;
    case CK_BUTTON:
        switch (c->a) {
        case BT_RESUME: case BT_CLOSE: ui_close_panel(); break;
        case BT_SAVE:      cmd(UC_SAVE, 0, 0); ui_close_panel(); break;
        case BT_QUICKSAVE: cmd(UC_QUICKSAVE, 0, 0); ui_close_panel(); break;
        case BT_LOAD:      cmd(UC_LOAD, 0, 0); ui_close_panel(); break;
        case BT_MENU:      cmd(UC_MENU, 0, 0); ui_close_panel(); break;
        case BT_QUIT:      cmd(UC_QUIT, 0, 0); break;
        case BT_RIGHTS:    uipol_show(DEC_TOWN_RIGHTS * 10 + 1); break;     /* PL1 */
        }
        break;
    }
}

static void click(int h)
{
    if (h >= H_SPEED0 && h < H_SPEED0 + 4) { cmd(UC_SPEED, h - H_SPEED0, 0); return; }
    if (h == H_ZOOM0 || h == H_ZOOM0 + 1) {           /* F3: zoom out / zoom in keys */
        if (!zoom_disabled(h - H_ZOOM0)) cmd(UC_ZOOM, h == H_ZOOM0 ? ZOOM_32 : ZOOM_64, 0);
        return;
    }
    if (h == H_ZONEV) { s_zone_view = !s_zone_view; cmd(UC_ZONE_VIEW, s_zone_view, 0); return; }   /* F3 */
    if (h == H_GUIDE) { cmd(UC_M7, M7A_GUIDE, 0); return; }                                        /* PL1 */
    if (h >= H_M7K0 && h < H_M7K0 + NM7K) {                                                       /* M7 + PL1 */
        int pn = h == H_M7K0 ? PANEL_GRAPHS : h == H_M7K0 + 3 ? PANEL_SERVICES : PANEL_TRADE;
        if (h == H_M7K0 + 2) { cmd(UC_M7, M7A_NEWS_LAST, 0); return; }
        if (s_panel == pn) ui_close_panel(); else ui_open_panel(pn);
        return;
    }
    if (h == H_MENU) { if (s_panel == PANEL_SETTINGS) ui_close_panel(); else ui_open_panel(PANEL_SETTINGS); return; }
    if (h == H_FUNDS) { ui_open_panel(PANEL_BUDGET); return; }
    if (h == H_WATER) { cmd(UC_OVERLAY, render_overlay() == OVL_WATER ? OVL_NONE : OVL_WATER, 0); return; }   /* F1 */
    if (h == H_POWER) { cmd(UC_OVERLAY, render_overlay() == OVL_POWER ? OVL_NONE : OVL_POWER, 0); return; }   /* PL1 */
    if (h == H_POP || h == H_DEMAND) { s_tab[PANEL_POPULATION] = h == H_DEMAND ? 0 : 1; ui_open_panel(PANEL_POPULATION); return; }
    if (h == H_DATE) { ui_open_panel(PANEL_POPULATION); s_tab[PANEL_POPULATION] = 1; return; }
    if (h == H_APPROVAL || (h == H_PLAQUE && politics_enabled())) {     /* M1 */
        if (s_panel == PANEL_POLITICS) ui_close_panel(); else ui_open_panel(PANEL_POLITICS);
        return;
    }
    if (h >= H_KEY0 && h < H_KEY0 + NKEYS) {
        int k = h - H_KEY0;
        if (key_all_locked(k)) return;
        if (s_keys[k].n == 1) { fly_close(); select_entry(k, 0); return; }
        if (s_fly == k) { fly_close(); return; }
        /* F3 (PLAYTEST 5): a key click selects its tool at once (last used entry, else the first unlocked
           one), so the previous tool never stays active behind an open fly-out; the fly-out offers the rest */
        if (!key_has_tool(k, s_tool, s_param)) {
            char hint[48];
            int e = s_key_last[k], i;
            if (ent_locked(&s_keys[k].e[e], hint, sizeof(hint)))
                for (i = 0; i < s_keys[k].n; i++) if (!ent_locked(&s_keys[k].e[i], hint, sizeof(hint))) { e = i; break; }
            select_entry(k, e);
        }
        fly_open(k);
        return;
    }
    if (h >= H_MINI0 && h < H_MINI0 + NMINI) {
        static const int panels[3] = { PANEL_BUDGET, PANEL_POPULATION, PANEL_OVERLAYS };
        int i = h - H_MINI0;
        if (i < 3) { if (s_panel == panels[i]) ui_close_panel(); else ui_open_panel(panels[i]); }
        else if (i == 3 && tools_can_undo()) cmd(UC_UNDO, 0, 0);
        else if (i == 4 && tools_can_redo()) cmd(UC_REDO, 0, 0);
        return;
    }
    if (h >= H_FLYROW0 && h < H_FLYROW0 + FLY_MAX && s_fly >= 0) { select_entry(s_fly, h - H_FLYROW0); return; }
    if (h >= H_TICK0 && h < H_TICK0 + TICK_MAX) { ticker_click(h - H_TICK0); return; }
    if (h >= H_CTL0 && h < H_CTL0 + s_nctl) { ctl_click(h - H_CTL0); return; }
}

int ui_input(const InputState *in)
{
    int h, over;
    if (!s_inited || !in) return 0;
    s_mx = in->inside ? in->mx : -1; s_my = in->inside ? in->my : -1;
    s_down = (in->buttons & MOUSE_L) != 0;
    h = hit_test(s_mx, s_my);
    if (h != s_hot) { s_hot = h; s_hot_t = s_now; }
    over = h != H_NONE;

    /* drags in progress own the mouse */
    if (s_drag_ctl >= 0) {
        if (s_drag_ctl < s_nctl) {
            int v = slider_value(&s_ctl[s_drag_ctl], in->mx);
            if (v != s_drag_val) { s_drag_val = v; slider_emit(s_ctl[s_drag_ctl].a, v); }
        }
        if (!(in->buttons & MOUSE_L)) s_drag_ctl = -1;
        return 1;
    }
    if (s_drag_mmap) {
        if (in->buttons & MOUSE_L) mmap_camera(in->mx, in->my); else s_drag_mmap = 0;
        return 1;
    }
    if (in->pressed & MOUSE_R) {
        if (s_fly >= 0) { fly_close(); return 1; }
        if (s_panel && (h >= H_WIN || over)) { ui_close_panel(); return 1; }
    }
    if (in->pressed & MOUSE_L) {
        s_press_hot = h;
        s_tip_shown = 0;
        if (s_fly >= 0 && !(h >= H_FLY && h < H_FLY + 1 + FLY_MAX) && h != H_KEY0 + s_fly) {
            fly_close();
            if (!over) return 1;                /* the click that closes a fly-out doesn't build */
        }
        if (h == H_MMAP) { s_drag_mmap = 1; mmap_camera(in->mx, in->my); return 1; }
        if (h >= H_CTL0 && h < H_CTL0 + s_nctl && s_ctl[h - H_CTL0].kind == CK_SLIDER) {
            s_drag_ctl = h - H_CTL0;
            s_drag_val = slider_value(&s_ctl[s_drag_ctl], in->mx);
            slider_emit(s_ctl[s_drag_ctl].a, s_drag_val);
            return 1;
        }
    }
    if (in->released & MOUSE_L) {
        if (h == s_press_hot && h != H_NONE) click(h);
        s_press_hot = H_NONE;
    }
    if (s_down && s_press_hot != H_NONE) return 1;  /* a press that started on the UI */
    return over;
}

int ui_wants_keyboard(void) { return 0; }

int ui_key(int vk)
{
    if (vk == VK_ESCAPE) {
        if (s_fly >= 0) { fly_close(); return 1; }
        if (s_panel) { ui_close_panel(); return 1; }
    }
    return 0;
}

void ui_set_tool(int tool, int param)
{
    s_tool = tool; s_param = param;
    if (s_fly >= 0 && tool != TOOL_NONE && !key_has_tool(s_fly, tool, param)) fly_close();   /* F3: hotkey switch */
}

/* ---- F3 */
void ui_set_zone_view(int on) { s_zone_view = on ? 1 : 0; }

void ui_ticker_hint(const char *text)
{
    ui_ticker_push(SEV_INFO, text, -1, -1);
    if (s_ntick && !strcmp(s_tick[0].text, text)) s_tick[0].topic = TOPIC_HINT;
}

int ui_lot_hover_ok(void) { return s_inited && s_fly < 0; }

/* PL3: rects overlap (the hover label must not draw over the panel window or a fly-out) */
static int rects_meet(int ax, int ay, int aw, int ah, int bx, int by, int bw, int bh)
{
    return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}
int ui_rect_covered(int x, int y, int w, int h)
{
    if (!s_inited) return 0;
    if (s_panel && rects_meet(x, y, w, h, s_win_x, s_win_y, s_win_w, s_win_h)) return 1;
    if (s_fly >= 0 && rects_meet(x, y, w, h, s_fly_x, s_fly_y, s_fly_w + 6, s_fly_h)) return 1;
    return 0;
}

void ui_president_say(int mood)
{
    s_mood = ICLAMP(mood, 0, PRES_MOOD_COUNT - 1);
    s_mood_h = (int)(hash_u32((u32)(s_now * 1000.0) + (u32)mood) & 0xffff);
    s_said_month = (int)sim_date()->total_months;
}

void ui_float_money(int x, int y, Money amount)
{
    int i, best = 0;
    for (i = 0; i < FLOAT_MAX; i++) {
        if (!s_float[i].on) { best = i; break; }
        if (s_float[i].t < s_float[best].t) best = i;
    }
    s_float[best].on = 1; s_float[best].x = x; s_float[best].y = y; s_float[best].v = amount; s_float[best].t = s_now;
}

/* ================================================================== lifecycle */
static void art_resolve(void)
{
    static const char *const sp[4] = { "pause", "1", "2", "3" };
    static const char *const sps[3] = { "normal", "hover", "on" };
    static const char *const moods[PRES_MOOD_COUNT] = { "content", "stern", "delighted", "worried" };
    static const char *const lamps[5] = { "lamp_info", "lamp_warn", "lamp_urgent", "lamp_pol", "lamp_off" };
    static const char *const cs[3] = { "normal", "hover", "disabled" };
    static const char *const bars[3] = { "dem_bar_r", "dem_bar_c", "dem_bar_i" };
    int i, j;
    S.bar_tile = art_find("bar_tile"); S.bar_sep = art_find("bar_sep"); S.dem_well = art_find("dem_well");
    for (i = 0; i < 3; i++) S.dem_bar[i] = art_find(bars[i]);
    for (i = 0; i < 4; i++) for (j = 0; j < 3; j++) S.speed[i][j] = art_findf("speed_%s_%s", sp[i], sps[j]);
    S.menu[0] = art_find("speed_menu_normal"); S.menu[1] = art_find("speed_menu_hover"); S.menu[2] = art_find("speed_menu_pressed");
    S.teak = art_find("teak_tile"); S.panel_edge = art_find("panel_edge"); S.badge = art_find("badge");
    S.pres_frame[0] = art_find("pres_frame_s"); S.pres_frame[1] = art_find("pres_frame_l");
    for (i = 0; i < PRES_MOOD_COUNT; i++) { S.pres[0][i] = art_findf("pres_s_%s", moods[i]); S.pres[1][i] = art_findf("pres_l_%s", moods[i]); }
    S.mmap_cam[0] = art_find("mmap_cam_tl"); S.mmap_cam[1] = art_find("mmap_cam_tr");
    S.mmap_cam[2] = art_find("mmap_cam_bl"); S.mmap_cam[3] = art_find("mmap_cam_br");
    S.ticker_tile = art_find("ticker_tile");
    for (i = 0; i < 5; i++) S.lamp[i] = art_find(lamps[i]);
    S.fly_arrow = art_find("fly_arrow"); S.fly_sep = art_find("fly_sep"); S.rule = art_find("rule"); S.tab_line = art_find("tab_line");
    S.close[0] = art_find("close_normal"); S.close[1] = art_find("close_hover"); S.close[2] = art_find("close_pressed");
    S.knob[0] = art_find("slider_knob_normal"); S.knob[1] = art_find("slider_knob_hover"); S.knob[2] = art_find("slider_knob_pressed");
    for (i = 0; i < 3; i++) {
        S.check[0][i] = art_findf("check_off_%s", cs[i]); S.check[1][i] = art_findf("check_on_%s", cs[i]);
        S.radio[0][i] = art_findf("radio_off_%s", cs[i]); S.radio[1][i] = art_findf("radio_on_%s", cs[i]);
    }
    S.seal = art_find("stamp_seal");
    for (i = 0; i < BS_COUNT; i++) { char b[32]; str_printf(b, sizeof(b), "btn_%s", s_bs_name[i]); slice9_init(&s9_btn[i], b); }
    slice9_init(&s9_fly, "fly"); slice9_init(&s9_inset, "inset"); slice9_init(&s9_mmap, "mmap");
    slice9_init(&s9_plaque, "plaque"); slice9_init(&s9_tip, "tip"); slice9_init(&s9_win, "win");
    slice3_init(&s3_well, "bar_well", 0); slice3_init(&s3_cal, "cal_plate", 0);
    slice3_init(&s3_flyhover, "flyrow_hover", 0); slice3_init(&s3_flysel, "flyrow_sel", 0);
    slice3_init(&s3_grille, "grille", 0); slice3_init(&s3_plate, "plate", 0);
    slice3_init(&s3_track, "slider_track", 0); slice3_init(&s3_fill, "slider_fill", 0);
    slice3_init(&s3_tab[0], "tab_normal", 0); slice3_init(&s3_tab[1], "tab_hover", 0); slice3_init(&s3_tab[2], "tab_selected", 0);
    for (i = 0; i < NKEYS; i++) {
        s_key_icon[i][0] = icon_id(s_keys[i].icon, 0); s_key_icon[i][1] = icon_id(s_keys[i].icon, 1);
        for (j = 0; j < s_keys[i].n; j++) {
            s_fly_icon[i][j][0] = icon_id(s_keys[i].e[j].icon, 0);
            s_fly_icon[i][j][1] = icon_id(s_keys[i].e[j].icon, 1);
        }
    }
    for (i = 0; i < NMINI; i++) { s_mini_ic[i][0] = icon_id(s_mini_icon[i], 0); s_mini_ic[i][1] = icon_id(s_mini_icon[i], 1); }
    {   /* F3: zoom keys (frame pieces) + zone-view icon */
        static const char *const zs[4] = { "normal", "hover", "pressed", "disabled" };
        for (j = 0; j < 4; j++) { S.zoom[0][j] = art_findf("btn_zoom_out_%s", zs[j]); S.zoom[1][j] = art_findf("btn_zoom_in_%s", zs[j]); }
        S.zonev[0] = icon_id("zoneview", 0); S.zonev[1] = icon_id("zoneview", 1);
        S.m7k[0][0] = icon_id("graphs", 0); S.m7k[0][1] = icon_id("graphs", 1);         /* M7 */
        S.m7k[1][0] = icon_id("trade", 0); S.m7k[1][1] = icon_id("trade", 1);
        S.m7k[2][0] = icon_id("newspaper", 0); S.m7k[2][1] = icon_id("newspaper", 1);
        S.m7k[3][0] = icon_id("key_services", 0); S.m7k[3][1] = icon_id("key_services", 1);   /* PL1 */
        S.guide[0] = icon_id("info", 0); S.guide[1] = icon_id("info", 1);
    }
    for (i = 0; i < FONT_COUNT; i++) font_build(i);
}

static void caches_create(void)
{
    int i;
    s_stats.vram_kb = 0;
    s_c[C_TOP].x = L->top.x; s_c[C_TOP].y = L->top.y;
    cache_make(&s_c[C_TOP], L->top.w, L->top.h, 0);
    s_c[C_SIDE].x = L->panel.x; s_c[C_SIDE].y = L->panel.y;
    cache_make(&s_c[C_SIDE], L->panel.w, L->panel.h, 0);
    s_c[C_TICKER].x = L->ticker.x; s_c[C_TICKER].y = L->ticker.y;
    cache_make(&s_c[C_TICKER], L->ticker.w, L->ticker.h, 0);
    cache_make(&s_c[C_WIN], WIN_MAX_W, WIN_MAX_H, 1);
    cache_make(&s_c[C_POP], POP_W, POP_H, 1);
    for (i = 0; i < C_COUNT; i++) s_c[i].dirty = 1;
    /* F3: the minimap image too, while VRAM is still free: made lazily in the first ui_update it came after
       render_init's chunk pool, which takes all but a small reserve (black minimap in the M1 shots) */
    if (!s_mm_ok && L->mmap.w > 0 && gfx_make_img(L->mmap.w, L->mmap.h, 0, &s_mm)) {
        s_mm_ok = 1; s_mm_w = L->mmap.w; s_mm_h = L->mmap.h;
        s_stats.vram_kb += (s_mm_w * s_mm_h * 2 + 1023) / 1024;
    }
}

static void caches_free(void)
{
    int i;
    for (i = 0; i < C_COUNT; i++) if (s_c[i].ok) gfx_free_img(&s_c[i].img);
    memset(s_c, 0, sizeof(s_c));
    if (s_mm_ok) gfx_free_img(&s_mm);
    s_mm_ok = 0;
}

int ui_init(void)
{
    int i, best = 0;
    if (s_inited) ui_shutdown();
    s_sw = plat_screen_w(); s_sh = plat_screen_h();
    for (i = 0; i < UI_LAYOUT_COUNT; i++) if (ui_layouts[i].scr_w <= s_sw && ui_layouts[i].scr_h <= s_sh) best = i;
    L = &ui_layouts[best];
    if (L->scr_w != s_sw || L->scr_h != s_sh)
        log_printf("ui: warning: no layout for %dx%d, using %dx%d", s_sw, s_sh, L->scr_w, L->scr_h);
    memset(&s_stats, 0, sizeof(s_stats));
    art_resolve();
    caches_create();
    s_restore = gfx_restore_serial();
    s_inited = 1;
    ui_reset();
    log_printf("ui: init %dx%d layout, caches %d KB VRAM (%s%s%s%s%s), fonts %s", L->scr_w, L->scr_h, s_stats.vram_kb,
               s_c[0].ok ? "" : "top! ", s_c[1].ok ? "" : "side! ", s_c[2].ok ? "" : "ticker! ", s_c[3].ok ? "" : "win! ",
               s_c[4].ok ? "" : "pop! ", art_font("body_dark") ? "ok" : "MISSING (art.cpp stub?)");
    return 1;
}

void ui_shutdown(void)
{
    if (!s_inited) return;
    caches_free();
    s_inited = 0;
}

void ui_reset(void)
{
    int i;
    s_panel = PANEL_NONE; s_fly = -1; s_ntick = 0; s_nctl = 0; s_drag_ctl = -1; s_drag_mmap = 0;
    s_tool = TOOL_NONE; s_param = 0; s_tax_res = s_tax_bus = -1;
    s_mood = PRES_MOOD_CONTENT; s_mood_h = 0; s_said_month = -1; s_auto_month = -1; s_pop_prev = -1;
    memset(s_topic_month, 0, sizeof(s_topic_month));
    memset(s_float, 0, sizeof(s_float));
    for (i = 0; i < NKEYS; i++) s_key_last[i] = 0;
    for (i = 0; i < C_COUNT; i++) s_c[i].dirty = 1;
    s_mm_serial = 0; s_mm_ovl = -1;
    s_mm_force = 1;              /* built in the next ui_update: render_minimap needs render_init's art ids */
    uipol_reset();               /* M1 */
}

void ui_get_stats(UiStats *st) { *st = s_stats; }

/* ================================================================== per frame */
static void president_auto(void)
{
    const CityStats *cs = sim_stats();
    int m = (int)sim_date()->total_months, mood = PRES_MOOD_CONTENT;
    if (m == s_auto_month) return;
    s_auto_month = m;
    if (s_said_month == m) { s_pop_prev = cs->population; return; }   /* the game spoke this month */
    if (g_budget.money < 0 || g_budget.loan > 0) mood = PRES_MOOD_WORRIED;
    else if (politics_enabled()) mood = politics_mood();     /* M1: the blocs and the East set the face */
    else if (cs->emigrated_month >= 50 || (cs->population > 200 && cs->unemployed * 10 > cs->workers)) mood = PRES_MOOD_STERN;
    else if (s_pop_prev > 100 && cs->population > s_pop_prev + s_pop_prev / 25) mood = PRES_MOOD_DELIGHTED;
    s_pop_prev = cs->population;
    if (mood != s_mood || m % 3 == 0) { s_mood = mood; s_mood_h = m; }
}

void ui_update(double dt)
{
    int i, v[20], r;
    u32 sv, sh;
    if (!s_inited) return;
    s_now += dt;
    s_nblit = 0;
    /* surfaces lost (alt-tab): everything again */
    if (gfx_restore_serial() != s_restore) {
        s_restore = gfx_restore_serial();
        for (i = 0; i < C_COUNT; i++) s_c[i].dirty = 1;
        s_mm_force = 1;
    }
    r = render_overlay();
    if (r != s_ovl_seen) { s_ovl_seen = r; s_ovl_local = r; }
    mm_update(s_mm_force);
    s_mm_force = 0;
    president_auto();
    /* ticker: drop old items */
    for (i = s_ntick - 1; i >= 0; i--)
        if (s_now - s_tick[i].t > TICK_LIFE) { s_ntick = i; s_c[C_TICKER].dirty = 1; }
    for (i = 0; i < FLOAT_MAX; i++) if (s_float[i].on && s_now - s_float[i].t > 1.6) s_float[i].on = 0;

    /* ---- top bar */
    top_vals(v);
    sv = hash_ints(v, 17);
    sh = (u32)top_hotstate();
    if (s_c[C_TOP].dirty || sh != s_c[C_TOP].sig_hot || (sv != s_c[C_TOP].sig_val && s_now - s_c[C_TOP].last >= VALUE_RATE)) {
        if (sv != s_c[C_TOP].sig_val) s_c[C_TOP].last = s_now;
        s_c[C_TOP].sig_val = sv; s_c[C_TOP].sig_hot = sh; s_c[C_TOP].dirty = 0;
        if (s_c[C_TOP].ok) compose_top();
    }
    /* ---- side panel (per element) */
    side_check();
    if (s_c[C_SIDE].dirty) {
        s_c[C_SIDE].dirty = 0; s_nside_dirty = 0;
        if (s_c[C_SIDE].ok) compose_side(NULL);
    } else if (s_nside_dirty) {
        for (i = 0; i < s_nside_dirty; i++) if (s_c[C_SIDE].ok) compose_side(s_side_dirty[i]);
        s_nside_dirty = 0;
    }
    /* ---- ticker */
    sh = (u32)((s_hot >= H_TICKER && s_hot < H_FLY) ? s_hot : 0);
    if (s_c[C_TICKER].dirty) { s_c[C_TICKER].dirty = 0; if (s_c[C_TICKER].ok) compose_ticker(); }
    UNUSED(sh);
    /* ---- panel */
    if (s_panel) {
        Cache *c = &s_c[C_WIN];
        sh = panel_hot_sig();
        if (!c->dirty && s_now - s_win_check >= PANEL_CHECK) {
            s_win_check = s_now;
            sv = panel_data_sig();
            if (sv != c->sig_val) c->dirty = 1;
        }
        if (c->dirty || sh != c->sig_hot) {
            c->sig_hot = sh;
            if (c->dirty) c->sig_val = panel_data_sig();
            c->dirty = 0;
            if (c->ok) compose_win(); else { panel_size(s_panel, &s_win_w, &s_win_h);
                s_win_x = L->map.x + L->map.w - s_win_w - 8; s_win_y = L->map.y + 8; }
        }
    }
    /* ---- popup: fly-out or tooltip */
    if (s_fly >= 0) {
        Cache *c = &s_c[C_POP];
        sh = (u32)(s_hot * 2 + s_tool * 64 + s_param * 1024);
        if (c->dirty || sh != c->sig_hot) { c->sig_hot = sh; c->dirty = 0; if (c->ok) compose_fly(); else fly_layout(s_fly); }
        s_tip_shown = 0;
    } else {
        int want = s_hot != H_NONE && !s_down && s_now - s_hot_t >= TIP_DELAY && s_drag_ctl < 0 && !s_drag_mmap;
        if (!want) s_tip_shown = 0;
        else if (!s_tip_shown || s_tip_hot != s_hot || s_now - s_tip_check >= PANEL_CHECK) {
            s_tip_check = s_now;
            if (tip_build(s_hot)) {
                u32 sg = tip_sig() ^ (u32)s_hot;
                if (!s_tip_shown || sg != s_tip_sig || s_tip_hot != s_hot) {
                    s_tip_sig = sg; s_tip_hot = s_hot; s_tip_shown = 1;
                    if (s_c[C_POP].ok) compose_tip();
                }
            } else s_tip_shown = 0;
        }
    }
}

static void blit_cache(int ci)
{
    Cache *c = &s_c[ci];
    if (!c->ok) return;
    gfx_blit_part(&c->img, 0, 0, c->w, c->h, c->x, c->y);
    s_stats.draw_blits++;
}

void ui_draw(void)
{
    int i, n0;
    if (!s_inited) return;
    s_stats.draw_blits = 0;
    gfx_set_target(NULL);
    /* parts without a cache (VRAM full) are drawn directly every frame */
    n0 = s_nblit;
    if (s_c[C_TOP].ok) blit_cache(C_TOP); else compose_top();
    if (s_c[C_SIDE].ok) blit_cache(C_SIDE); else { mm_view(); compose_side(NULL); }
    if (s_c[C_TICKER].ok) blit_cache(C_TICKER); else compose_ticker();
    if (s_panel) { if (s_c[C_WIN].ok) blit_cache(C_WIN); else compose_win(); }
    if (s_fly >= 0) {
        if (s_c[C_POP].ok) { Cache *c = &s_c[C_POP]; gfx_blit_part(&c->img, 0, 0, s_fly_w + 6, s_fly_h, c->x, c->y); s_stats.draw_blits++; }
        else compose_fly();
    } else if (s_tip_shown) {
        if (s_c[C_POP].ok) blit_cache(C_POP); else compose_tip();
    }
    s_stats.draw_blits += s_nblit - n0;
    /* floating money over the map (pre-rendered digit glyphs, a few blits while one is alive) */
    gfx_set_clip(L->map.x, L->map.y, L->map.x + L->map.w, L->map.y + L->map.h);
    for (i = 0; i < FLOAT_MAX; i++) {
        Float *f = &s_float[i];
        char b[32];
        int sx, sy, age;
        if (!f->on) continue;
        age = (int)((s_now - f->t) * 24.0);
        cam_tile_to_screen(f->x, f->y, &sx, &sy);
        text_fmt_money_delta(b, sizeof(b), f->v, cur_year());
        txt(sx - txtw(b, FONT_FLOAT_LIGHT) / 2, sy - 24 - age, b, f->v < 0 ? FONT_FLOAT_RED : FONT_FLOAT_GREEN);
    }
    gfx_reset_clip();
}

/* ================================================================== -test ui */
/* Scripted town for the test (copied from T04's tests/test_sim.cpp town_stage0/1, credit T04). */
static void t_road(int x0, int y0, int x1, int y1, int cls)
{
    int x, y;
    world_batch_begin();
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++)
            if (!TILE_ROAD(world_tile(x, y)) && !world_tile(x, y)->lot) world_set_road(x, y, cls);
    world_batch_end();
}
static void t_pline(int x0, int y0, int x1, int y1)
{
    int x, y;
    for (y = IMIN(y0, y1); y <= IMAX(y0, y1); y++)
        for (x = IMIN(x0, x1); x <= IMAX(x0, x1); x++) if (!world_tile(x, y)->lot) world_set_power_line(x, y, 1);
}
static void t_zone(int x0, int y0, int x1, int y1, int z)
{
    int x, y;
    world_batch_begin();
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            if (world_is_buildable(x, y) && world_road_within(x, y, ZONE_ROAD_REACH)) world_set_zone(x, y, z);
    world_batch_end();
}
static void t_build(int def, int x, int y)
{
    if (world_lot_place(def, x, y, ROT_0, 0, LOT_ACTIVE)) budget_spend(def_building(def)->cost, BL_CONSTRUCTION);
}
static void t_town(void)
{
    int x, y;
    for (y = 76; y <= 112; y++) for (x = 4; x <= 24; x++) world_set_terrain(x, y, TER_WATER, 0);
    t_road(0, 64, 127, 64, ROAD_STREET);
    for (x = 30; x <= 62; x += 8) t_road(x, 40, x, 88, ROAD_STREET);
    t_road(30, 40, 62, 40, ROAD_STREET); t_road(30, 48, 62, 48, ROAD_STREET); t_road(30, 56, 62, 56, ROAD_STREET);
    t_road(30, 72, 62, 72, ROAD_STREET); t_road(30, 80, 62, 80, ROAD_STREET); t_road(26, 88, 62, 88, ROAD_GRAVEL);
    t_road(26, 88, 26, 104, ROAD_GRAVEL);
    t_road(80, 44, 80, 63, ROAD_STREET); t_road(92, 44, 92, 63, ROAD_STREET); t_road(104, 44, 104, 63, ROAD_STREET);
    t_road(80, 44, 112, 44, ROAD_STREET); t_road(80, 54, 104, 54, ROAD_STREET);
    t_build(B_FIRE_STATION, 39, 57); t_build(B_CLINIC, 47, 57); t_build(B_TOWN_HALL, 63, 57);
    t_zone(31, 41, 61, 55, ZONE_HOUSES); t_zone(31, 57, 61, 60, ZONE_HOUSES);
    t_zone(31, 61, 61, 63, ZONE_COMMERCIAL); t_zone(31, 65, 61, 67, ZONE_COMMERCIAL);
    t_zone(31, 68, 61, 87, ZONE_HOUSES); t_zone(81, 45, 103, 63, ZONE_INDUSTRY);
    t_build(B_COAL_PLANT, 112, 45);
    t_pline(30, 42, 111, 42); t_pline(111, 42, 111, 60);
    for (y = 40; y <= 88; y += 8) if (y != 64) t_pline(30, y, 62, y);
    t_pline(30, 41, 30, 88);
    t_build(B_PUMP, 25, 90); t_build(B_PUMP, 25, 100);
}
static void t_town_stage1(void)
{
    t_build(B_SCHOOL, 39, 73); t_build(B_SCHOOL, 49, 44);
    t_build(B_SAUNA, 55, 57); t_build(B_SAUNA, 47, 81); t_build(B_SAUNA, 39, 49);
    t_build(B_COAL_PLANT, 112, 50); t_build(B_PUMP, 25, 95);
}

typedef struct {
    const char *name;
    double dur;
    int frames, recomp[C_COUNT], blits_max, glyph_tot;
    double recomp_ms[C_COUNT], blits_sum, upd_ms_max, upd_ms_sum, draw_ms_max, draw_ms_sum, meas_t0, meas_t;
} TPhase;

static InputState s_tin;
static void t_mouse(int x, int y) { s_tin.mx = x; s_tin.my = y; s_tin.inside = 1; }
static void t_press(void) { s_tin.buttons |= MOUSE_L; s_tin.pressed |= MOUSE_L; }
static void t_release(void) { s_tin.buttons &= ~MOUSE_L; s_tin.released |= MOUSE_L; }

static int s_tfail;
static void t_check(int ok, const char *fmt, ...)
{
    char b[256];
    va_list ap;
    va_start(ap, fmt);
    str_vprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    log_printf("ui_selftest: %s %s", ok ? "PASS" : "FAIL", b);
    if (!ok) s_tfail++;
}

static void t_exec_cmds(void)
{
    UiCmd c;
    while (ui_poll_cmd(&c)) {
        log_printf("ui_selftest:   cmd %d (%d, %d)", c.type, c.a, c.b);
        switch (c.type) {
        case UC_SET_TAX: budget_set_tax(c.a, c.b); break;
        case UC_SET_FUNDING: budget_set_funding(c.a, c.b); break;
        case UC_TOOL: ui_set_tool(c.a, c.b); break;
        case UC_SPEED: sim_set_speed(c.a); break;
        case UC_OVERLAY: render_set_overlay(c.a, c.b, s_show_buildings); break;
        case UC_CAMERA: cam_center_on_tile(c.a, c.b); break;
        }
    }
}

static int t_find_house(void)
{
    int i, best = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def == B_NONE) continue;
        if (def_building(l->def)->zone == ZONE_HOUSES) { best = i; if (def_building(l->def)->tier >= 2) return i; }
    }
    return best;
}

/* centre of an element (for simulated mouse moves) */
static void t_key_centre(int k, int *x, int *y) { int w, h; key_rect(k, x, y, &w, &h); *x += w / 2; *y += h / 2; }
static void t_mini_centre(int k, int *x, int *y) { int w, h; mini_rect(k, x, y, &w, &h); *x += w / 2; *y += h / 2; }

int ui_selftest(void)
{
    enum { P_IDLE, P_TIP_FUNDS, P_TIP_DEMAND, P_TIP_POP, P_SW_ROADS, P_SW_GRAVEL, P_SW_IND, P_SW_INDROW, P_FLY, P_FLY_PICK, P_BUDGET, P_BUDGET_DRAG, P_BUDGET_SVC,
           P_BUDGET_LEDGER, P_POP_DEMAND, P_POP_PEOPLE, P_QUERY_LOT, P_QUERY_TILE, P_OVERLAYS, P_SERVICES, P_SETTINGS,
           P_MMAP, P_RUNNING, P_IDLE_PANEL, P_CLOSE, P_COUNT };
    static TPhase ph[P_COUNT] = {
        { "idle (frame only)", 3.0 }, { "tooltip: funds", 1.5 }, { "tooltip: demand", 1.2 }, { "tooltip: population", 1.2 },
        { "F3 switch: Roads key", 0.6 }, { "F3 switch: Gravel", 0.6 }, { "F3 switch: Industry key", 0.6 }, { "F3 switch: Industry row", 0.6 },
        { "fly-out: services", 1.5 }, { "fly-out: pick school", 0.6 }, { "budget: taxes", 1.5 }, { "budget: drag tax", 1.5 },
        { "budget: services", 1.2 }, { "budget: ledger", 1.2 }, { "population: demand", 1.5 }, { "population: people", 1.2 },
        { "query: lot", 1.5 }, { "query: tile", 1.2 }, { "overlays", 1.5 }, { "services", 1.2 }, { "game menu", 1.0 },
        { "minimap drag", 1.2 }, { "running at speed 3", 4.0 }, { "idle with budget open", 4.0 }, { "esc closes", 1.0 } };
    static const int shot_phase[] = { P_IDLE, P_TIP_FUNDS, P_FLY, P_BUDGET, P_POP_DEMAND, P_POP_PEOPLE, P_QUERY_LOT, P_OVERLAYS, P_SERVICES };
    SimSettings ss;
    SimEvent e;
    UiStats st0, st1;
    double t0, tp, now, prev, dt, a, b;
    int p = 0, i, k, mx, my, house, shot_done = 0, mapx, mapy, mapw, maph, rendered = 0, phase_frame, meas_snap = 0;
    char name[64];

    s_test = 1; s_tfail = 0;
    log_printf("ui_selftest: start, screen %dx%d, backend %s", plat_screen_w(), plat_screen_h(), plat_backend_name());
    if (!art_init()) { log_printf("ui_selftest: FAIL art_init"); return 1; }
    text_init();
    world_init(); sim_init();
    world_new(128, 128);
    sim_default_settings(&ss);
    str_copy(ss.town, "Kivikoski", NAME_LEN); str_copy(ss.president, "Kekkola", NAME_LEN);
    ss.start_year = 1960; ss.seed = 7;
    sim_new_city(&ss);
    t0 = time_ms();
    t_town();
    sim_run_months(14);
    t_town_stage1();
    sim_run_months(10);
    log_printf("ui_selftest: town built + 24 months in %.0f ms: pop %d, money %d, status %d, %d/%d",
               time_ms() - t0, sim_stats()->population, g_budget.money, sim_stats()->town_status, sim_date()->month, sim_date()->year);
    /* ui_init BEFORE render_init: the UI caches (~600 KB) are allocated first and the renderer sizes its chunk
       pool from the VRAM that is left (the other order leaves no room for the panel/popup caches). The
       minimap is built lazily in the first ui_update, after render_init (its colours come from render). */
    if (!ui_init()) { log_printf("ui_selftest: FAIL ui_init"); return 1; }
    ui_map_rect(&mapx, &mapy, &mapw, &maph);
    iso_init(g_world.w, g_world.h);
    cam_set_view(mapx, mapy, mapw, maph);
    if (render_init()) { render_reset(); rendered = 1; }
    while (sim_poll_event(&e)) ui_ticker_event(&e);
    {   /* a few ticker lines in every colour */
        SimEvent f;
        memset(&f, 0, sizeof(f));
        f.type = SE_WATER_SHORTAGE; f.severity = SEV_WARN; f.x = 40; f.y = 70; f.a = 34; f.b = 120; ui_ticker_event(&f);
        f.type = SE_STATUS_UP; f.severity = SEV_POLITICAL; f.x = -1; f.y = -1; f.a = TS_CHURCH_VILLAGE; f.b = 2000; ui_ticker_event(&f);
    }
    ui_map_rect(&mapx, &mapy, &mapw, &maph);
    cam_set_view(mapx, mapy, mapw, maph);
    cam_center_on_tile(46, 64);
    house = t_find_house();
    log_printf("ui_selftest: renderer %s, query lot %d", rendered ? "on" : "off (placeholder map)", house);
    memset(&s_tin, 0, sizeof(s_tin));
    t_mouse(mapx + 100, mapy + 200);

    prev = plat_seconds(); tp = prev; phase_frame = 0;
    ui_get_stats(&st0);
    for (;;) {
        TPhase *P = &ph[p];
        double ut, dtm;
        if (!plat_pump()) break;
        now = plat_seconds(); dt = now - prev; prev = now;
        if (dt > 0.1) dt = 0.1;
        /* ---- phase script (actions on the first frame / at times within the phase) */
        a = now - tp;
        s_tin.pressed = s_tin.released = 0;
        if (phase_frame == 0) {
            switch (p) {
            case P_IDLE: t_mouse(mapx + 100, mapy + 200); break;
            case P_TIP_FUNDS: t_mouse(s_topr[1][0] + 10, 8); break;
            case P_TIP_DEMAND: t_mouse(s_topr[3][0] + s_topr[3][2] - 8, 8); break;
            case P_TIP_POP: t_mouse(s_topr[2][0] + 10, 8); break;
            /* F3 (PLAYTEST 5): Gravel road, then Industry: the road tool must not stay selected */
            case P_SW_ROADS: t_key_centre(0, &mx, &my); t_mouse(mx, my); t_press(); break;
            case P_SW_GRAVEL: t_mouse(s_fly_x + 30, s_fly_y + 6 + 8); t_press(); break;
            case P_SW_IND:
                t_check(s_tool == TOOL_ROAD && s_param == ROAD_GRAVEL && s_fly < 0, "F3: Gravel road picked (tool %d param %d, fly %d)", s_tool, s_param, s_fly);
                t_key_centre(3, &mx, &my); t_mouse(mx, my); t_press(); break;
            case P_SW_INDROW:
                t_check(s_tool == TOOL_ZONE && s_param == ZONE_INDUSTRY && s_fly == 3, "F3: the Industry key replaced the road tool at once (tool %d param %d, fly %d)", s_tool, s_param, s_fly);
                t_mouse(s_fly_x + 30, s_fly_y + 6 + 8); t_press(); break;
            case P_FLY:
                t_check(s_tool == TOOL_ZONE && s_param == ZONE_INDUSTRY && s_fly < 0, "F3: Industry zone row picked (tool %d param %d, fly %d)", s_tool, s_param, s_fly);
                t_key_centre(6, &mx, &my); t_mouse(mx, my); t_press(); break;
            case P_FLY_PICK: t_mouse(s_fly_x + 30, s_fly_y + 6 + 17 + 8); t_press(); break;
            case P_BUDGET: t_mini_centre(0, &mx, &my); t_mouse(mx, my); t_press(); break;
            case P_BUDGET_DRAG:
                for (k = 0; k < s_nctl; k++) if (s_ctl[k].kind == CK_SLIDER && s_ctl[k].a == 0) break;
                if (k < s_nctl) { t_mouse(s_ctl[k].x + 4 + (s_ctl[k].w - 8) * g_budget.tax_res / 20, s_ctl[k].y + 6); t_press(); }
                break;
            case P_BUDGET_SVC: case P_BUDGET_LEDGER: case P_POP_PEOPLE:
                for (k = 0; k < s_nctl; k++) if (s_ctl[k].kind == CK_TAB && s_ctl[k].a == (p == P_BUDGET_LEDGER ? 2 : 1)) break;
                if (k < s_nctl) { t_mouse(s_ctl[k].x + 5, s_ctl[k].y + 5); t_press(); }
                break;
            case P_POP_DEMAND: t_mini_centre(1, &mx, &my); t_mouse(mx, my); t_press(); break;
            case P_QUERY_LOT: t_mouse(mapx + 20, mapy + 300); ui_open_query(house, 0, 0); break;
            case P_QUERY_TILE: ui_open_query(0, 45, 52); break;
            case P_OVERLAYS: t_mini_centre(2, &mx, &my); t_mouse(mx, my); t_press(); break;
            case P_SERVICES:      /* PL1: through the new Services key (PLAYTEST 2 item 6: the panel was unreachable) */
                ui_close_panel(); m7k_rect(3, &mx, &my, &k, &i); t_mouse(mx + k / 2, my + i / 2); t_press(); break;
            case P_SETTINGS:
                t_check(s_panel == PANEL_SERVICES, "PL1: the Services key opened the Services panel (panel %d)", s_panel);
                t_mouse(L->menu_key.x + 5, L->menu_key.y + 5); t_press(); break;
            case P_MMAP: ui_close_panel(); t_mouse(L->mmap.x + 20, L->mmap.y + 20); t_press(); break;
            case P_RUNNING: t_mouse(mapx + 100, mapy + 200); sim_set_speed(SPEED_3); ui_float_money(46, 64, -450); break;
            case P_IDLE_PANEL: sim_set_speed(SPEED_PAUSE); ui_open_panel(PANEL_BUDGET); s_tab[PANEL_BUDGET] = 0; t_mouse(mapx + 100, mapy + 200); break;
            case P_CLOSE: t_check(ui_key(VK_ESCAPE) == 1 && s_panel == PANEL_NONE, "Esc closes the panel"); break;
            }
        } else if (phase_frame == 2 && (s_tin.buttons & MOUSE_L) && p != P_BUDGET_DRAG && p != P_MMAP) t_release();
        if (p == P_BUDGET_DRAG && (s_tin.buttons & MOUSE_L)) {
            s_tin.mx += 2;
            if (a > 1.0) t_release();
        }
        if (p == P_OVERLAYS && phase_frame == 10) {            /* pick the pollution overlay */
            for (k = 0; k < s_nctl; k++) if (s_ctl[k].kind == CK_OVL && s_ctl[k].a == OVL_POLLUTION) break;
            if (k < s_nctl) { t_mouse(s_ctl[k].x + 4, s_ctl[k].y + 4); t_press(); }
        }
        if (p == P_OVERLAYS && phase_frame == 12) t_release();
        if (p == P_MMAP && (s_tin.buttons & MOUSE_L)) { s_tin.mx = IMIN(s_tin.mx + 1, L->mmap.x + L->mmap.w - 1); if (a > 0.8) t_release(); }
        if (p == P_RUNNING) {
            sim_frame(dt, 6.0);
            while (sim_poll_event(&e)) ui_ticker_event(&e);
        }

        ui_input(&s_tin);
        t_exec_cmds();
        if (rendered) cam_update();
        ut = time_ms();
        ui_update(dt);
        ut = time_ms() - ut;

        if (rendered) { render_update(3.0); render_world(); render_flush_sprites(); }
        else {
            gfx_fill(mapx, mapy, mapx + mapw, mapy + maph, gfx_rgb(92, 104, 78));
            for (k = 0; k < 6; k++) gfx_fill(mapx + 40 + k * 70, mapy + 60 + (k % 3) * 90, mapx + 90 + k * 70, mapy + 100 + (k % 3) * 90, gfx_rgb(120, 112, 98));
        }
        dtm = time_ms();
        ui_draw();
        dtm = time_ms() - dtm;
        /* count recompositions after the phase's own change; idle phases skip the first second (updates
           still due from the previous phase: a delayed value refresh, the 3 s minimap rebuild) */
        if (phase_frame == 0 || (p == P_IDLE_PANEL && !meas_snap && a >= 1.0)) {
            ui_get_stats(&st0);
            if (phase_frame) meas_snap = 1;
            P->meas_t0 = a;
        }
        if (phase_frame > 0) {                    /* frame 0 holds the phase's deliberate change */
            P->frames++;
            P->blits_sum += s_stats.draw_blits;
            P->blits_max = IMAX(P->blits_max, s_stats.draw_blits);
            P->upd_ms_sum += ut; P->upd_ms_max = (ut > P->upd_ms_max) ? ut : P->upd_ms_max;
            P->draw_ms_sum += dtm; P->draw_ms_max = (dtm > P->draw_ms_max) ? dtm : P->draw_ms_max;
        }
        /* screenshot in the middle of selected phases */
        for (k = 0; k < COUNTOF(shot_phase); k++)
            if (shot_phase[k] == p && !(shot_done & (1 << k)) && a >= P->dur * 0.7) {
                str_printf(name, sizeof(name), "uitest_%d_%d.bmp", plat_screen_w(), k);
                gfx_save_bmp(data_path(name));
                shot_done |= 1 << k;
            }
        plat_present();
        phase_frame++;
        if (a >= P->dur) {
            ui_get_stats(&st1);
            for (i = 0; i < C_COUNT; i++) { P->recomp[i] = st1.recompose[i] - st0.recompose[i]; P->recomp_ms[i] = st1.recompose_ms[i] - st0.recompose_ms[i]; }
            st0 = st1;
            P->meas_t = a - P->meas_t0;
            p++; tp = now; phase_frame = 0; meas_snap = 0;
            if (p >= P_COUNT) break;
        }
    }
    /* ---- report */
    log_printf("ui_selftest: per phase: frames, recompositions (top side ticker window popup) + ms each, ui blits/frame avg/max, ui_update ms avg/max, ui_draw ms avg/max");
    for (p = 0; p < P_COUNT; p++) {
        TPhase *P = &ph[p];
        char rc[160];
        int nrc = 0;
        rc[0] = 0;
        for (i = 0; i < C_COUNT; i++)
            nrc += str_printf(rc + nrc, sizeof(rc) - nrc, " %s %d (%.2f ms)", s_cache_name[i], P->recomp[i],
                              P->recomp[i] ? P->recomp_ms[i] / P->recomp[i] : 0.0);
        log_printf("ui_selftest: %-24s %4d fr |%s | blits %.1f/%d | upd %.2f/%.2f | draw %.2f/%.2f", P->name, P->frames, rc,
                   P->frames ? P->blits_sum / P->frames : 0.0, P->blits_max, P->frames ? P->upd_ms_sum / P->frames : 0.0,
                   P->upd_ms_max, P->frames ? P->draw_ms_sum / P->frames : 0.0, P->draw_ms_max);
    }
    a = 0; b = 0;
    for (i = 0; i < C_COUNT; i++) { a += ph[P_IDLE].recomp[i]; b += ph[P_IDLE_PANEL].recomp[i]; }
    t_check(a / ph[P_IDLE].meas_t <= 0.5, "idle frame: %.2f recompositions/s (<= 0.5)", a / ph[P_IDLE].meas_t);
    t_check(b / ph[P_IDLE_PANEL].meas_t <= 0.5, "idle with a panel: %.2f recompositions/s over its last %.1f s (<= 0.5)",
            b / ph[P_IDLE_PANEL].meas_t, ph[P_IDLE_PANEL].meas_t);
    for (p = 0; p < P_COUNT; p++) t_check(ph[p].blits_max <= 5, "%s: ui blits per frame max %d (<= 5)", ph[p].name, ph[p].blits_max);
    t_check(ph[P_RUNNING].recomp[C_TOP] <= (int)(ph[P_RUNNING].dur * 4.0) + 2, "running: top bar %d recompositions in %.0f s (<= 4/s)",
            ph[P_RUNNING].recomp[C_TOP], ph[P_RUNNING].dur);
    t_check(g_budget.tax_res != 9 || s_tax_res >= 0, "tax slider drag sent UC_SET_TAX (res tax now %d)", g_budget.tax_res);
    t_check(s_tool == TOOL_BUILD && s_param == B_SCHOOL, "fly-out pick selected the school (tool %d param %d)", s_tool, s_param);
    t_check(s_ovl_local == OVL_POLLUTION, "overlay radio picked pollution (%d)", s_ovl_local);
    log_printf("ui_selftest: UI VRAM %d KB; %d failures", s_stats.vram_kb, s_tfail);
    ui_shutdown();
    if (rendered) render_shutdown();
    s_test = 0;
    return s_tfail;
}

/* ================================================================== M1: helpers for ui_pol.cpp (ui_pol.h) */
int  uix_text(int x, int y, const char *s, int font) { return txt(x, y, s, ICLAMP(font, 0, FONT_COUNT - 1)); }
int  uix_text_w(const char *s, int font) { return txtw(s, ICLAMP(font, 0, FONT_COUNT - 1)); }
int  uix_text_r(int xr, int y, const char *s, int font) { return txt_r(xr, y, s, ICLAMP(font, 0, FONT_COUNT - 1)); }
int  uix_text_c(int x, int w, int y, const char *s, int font) { return txt_c(x, w, y, s, ICLAMP(font, 0, FONT_COUNT - 1)); }
int  uix_text_fit(int x, int y, const char *s, int font, int maxw) { return txt_fit(x, y, s, ICLAMP(font, 0, FONT_COUNT - 1), maxw); }
int  uix_wrap(const char *s, int font, int w, char out[][96], int maxl) { return wrap(s, ICLAMP(font, 0, FONT_COUNT - 1), w, out, maxl); }
void uix_rule(int x, int y, int w) { rule(x, y, w); }
void uix_bar(int x, int y, int w, int h, int v, int signed_bar, PIX c) { bar(x, y, w, h, v, signed_bar, c); }
void uix_inset(int x, int y, int w, int h, PIX c) { draw9(&s9_inset, x, y, w, h, c); }
void uix_fill(int x0, int y0, int x1, int y1, PIX c) { fill(x0, y0, x1, y1, c); }
int  uix_ctl(int x, int y, int w, int h, int a, int b) { return ctl_add(x, y, w, h, CK_POL, a, b); }
int  uix_ctl_hot(int idx) { return ctl_hot(idx); }
void uix_button(int x, int y, int w, int h, const char *label, int a, int b, int enabled)
{
    int idx, st, o;
    if (!enabled) {
        draw9(&s9_btn[BS_DISABLED], x, y, w, h, -1);
        txt_c(x, w - 1, y + (h - 11) / 2, label, FONT_BODY_GREY);
        return;
    }
    idx = ctl_add(x, y, w, h, CK_POL, a, b);
    st = ctl_hot(idx) ? (s_down && s_press_hot == s_hot ? BS_PRESSED : BS_HOVER) : BS_NORMAL;
    o = st == BS_PRESSED;
    draw9(&s9_btn[st], x, y, w, h, -1);
    txt_c(x + o, w - 1, y + (h - 11) / 2 + o, label, FONT_BODY_DARK);
}
void uix_tabs(int x, int y, int right, const char *const *names, int n, int sel) { tabs(x, y, right, names, n, sel); }
int  uix_tab(void) { return s_tab[PANEL_POLITICS]; }
int  uix_tab_of(int panel) { return (panel > PANEL_NONE && panel < PANEL_COUNT) ? s_tab[panel] : 0; }   /* M7 */
void ui_set_panel_tab(int tab) { if (s_panel > PANEL_NONE) { s_tab[s_panel] = tab; s_c[C_WIN].dirty = 1; } }   /* M7 */
/* PL1 */
void ui_open_flyout(int key) { if (key >= 0 && key < NKEYS && s_keys[key].n > 1 && !key_all_locked(key)) fly_open(key); else fly_close(); }
void ui_advice_go(int go, int x, int y)
{
    switch (go) {
    case AGO_POWER:    cmd(UC_OVERLAY, OVL_POWER, 0); break;
    case AGO_WATER:    cmd(UC_OVERLAY, OVL_WATER, 0); break;
    case AGO_HEAT:     cmd(UC_OVERLAY, OVL_HEAT, 0); break;
    case AGO_SERVICES: ui_open_panel(PANEL_SERVICES); break;
    case AGO_PEOPLE:   s_tab[PANEL_POPULATION] = 1; ui_open_panel(PANEL_POPULATION); break;
    case AGO_DEMAND:   s_tab[PANEL_POPULATION] = 0; ui_open_panel(PANEL_POPULATION); break;
    case AGO_BUDGET:   ui_open_panel(PANEL_BUDGET); break;
    case AGO_DESK:     if (politics_enabled()) uipol_show(DEC_TOWN_RIGHTS * 10 + 1); break;
    }
    if (x >= 0 && y >= 0 && go != AGO_SERVICES && go != AGO_PEOPLE && go != AGO_DEMAND && go != AGO_BUDGET && go != AGO_DESK)
        cmd(UC_CAMERA, x, y);
}
void uix_set_tab(int t) { s_tab[PANEL_POLITICS] = t; }
void uix_sprite(int id, int x, int y) { if (id > 0) spr(id, x, y); }
void uix_portrait(int mood, int x, int y)
{
    spr(S.pres_frame[1], x - 2, y - 2);
    spr(S.pres[1][ICLAMP(mood, 0, PRES_MOOD_COUNT - 1)], x, y);
}
void uix_cmd(int type, int a, int b) { cmd(type, a, b); }
void uix_ticker(int sev, const char *text, int panel)
{
    ui_ticker_push(sev, text, -1, -1);
    if (s_ntick && !strcmp(s_tick[0].text, text)) s_tick[0].panel = panel;
}
void uix_dirty(void) { s_c[C_WIN].dirty = 1; }
