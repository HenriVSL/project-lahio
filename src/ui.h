/* ui.h - teak & bakelite frame: top bar, tool panel + fly-outs, ticker, minimap, panels (budget,
   population/demand, query + checklist, overlays legend, services, game menu), tooltips, floating
   numbers, bitmap text. WINDOWS SIDE. Owner: T09. DESIGN 8; probe 7 (compose into VRAM caches only when
   dirty); art/UI.md (pieces, "Composing the frame"); art/ui_layout.h (frame rects per resolution).

   The UI reads simulation state directly (sim_stats, sim_demand, budget, sim_query_*, tools_can_undo)
   and never changes it: actions are returned as UiCmds that game.cpp executes (one place that wires
   things). Each frame costs a few cache blits (top bar, side panel incl. the minimap, ticker, open panel,
   fly-out/tooltip: <= 5); every part is recomposed into its VRAM target only when what it shows changed
   (interaction changes at once, value changes at most ~4x a second, panel data checked twice a second).

   Wiring for game.cpp (T08), per frame in GS_PLAY:
       consumed = ui_input(input_get());            mouse: 1 = don't pass buttons/wheel to tools
       for each hotkey press: if (!ui_key(vk)) ...  Esc closes the fly-out/panel first
       while (ui_poll_cmd(&c)) execute c            UC_* list below
       ui_update(dt);   ...   ui_draw();            (ui_draw inside PROF_UI after the world blits)
   and: ui_ticker_event(&e) for every drained SimEvent; ui_set_tool() when a hotkey changes the tool;
   ui_float_money() after a commit; ui_open_query() on TRES_QUERY; ui_reset() on new city/load;
   cam_set_view() with ui_map_rect(). */
#ifndef UI_H
#define UI_H
#include "common.h"
#include "plat.h"
#include "gfx.h"
#include "sim.h"

int  ui_init(void);                      /* after art_init; creates cache targets for the screen size */
void ui_shutdown(void);
void ui_reset(void);                     /* new city / load: close panels, clear ticker */
void ui_map_rect(int *x, int *y, int *w, int *h);  /* screen area left for the map view */

/* Input: returns 1 if the UI owns the mouse this frame (over the frame/panel/fly-out, or dragging a
   slider/minimap): game.cpp then doesn't pass mouse buttons to tools. */
int  ui_input(const InputState *in);
int  ui_wants_keyboard(void);            /* a text field has focus (hotkeys off) */

enum { UC_NONE,
       UC_TOOL,            /* a = TOOL_*, b = param */
       UC_SPEED,           /* a = SPEED_* */
       UC_OVERLAY,         /* a = OVL_*, b = param */
       UC_TOGGLE_BUILDINGS,/* overlay mode: buildings shown/flat; a = 1 shown, 0 flat */
       UC_UNDO, UC_REDO,
       UC_CAMERA,          /* a, b = tile to centre on (minimap click/drag, ticker jump) */
       UC_SET_TAX,         /* a = residential %, b = business % */
       UC_SET_FUNDING,     /* a = SVC_*, b = % */
       UC_MENU,            /* open the pause menu / title (T10) */
       UC_SAVE, UC_QUICKSAVE,
       UC_ZOOM,            /* a = ZOOM_* */
       /* T09 additions */
       UC_LOAD,            /* game menu: load a game (T10 shows the slots) */
       UC_QUIT,            /* game menu: quit to Windows */
       UC_POLITICS,        /* M1: a = POLA_* (ui_pol.h), b = decree / letter / choice */
       UC_ZONE_VIEW,       /* F3: a = 1 on / 0 off (zone-view key) */
       UC_HINTS,           /* F3: a = 0: the player clicked a hint (no more hints this game) */
       UC_M7,              /* M7: a = M7A_* (ui_m7.h), b = argument (game.cpp executes with trade/era) */
       UC_COUNT };
typedef struct { int type, a, b; } UiCmd;
int  ui_poll_cmd(UiCmd *c);              /* 1 = got one (queue of 16) */

void ui_update(double dt);               /* animations, tooltips, ticker, dirty-cache recompose */
void ui_draw(void);                      /* blit caches + floating numbers onto the screen */

enum { PANEL_NONE, PANEL_QUERY, PANEL_BUDGET, PANEL_POPULATION, PANEL_OVERLAYS, PANEL_SERVICES,
       PANEL_GRAPHS, PANEL_POLITICS,
       PANEL_SETTINGS,     /* T09: game menu stub (resume, save, quicksave, load, main menu, quit) */
       PANEL_TRADE,        /* M7: clearing agreement, balance meter, oil, the bond, sauna diplomacy (ui_graph.cpp) */
       PANEL_COUNT };
void ui_open_panel(int panel);
void ui_open_query(int lot, int x, int y); /* lot 0 = tile query at (x, y) */
void ui_close_panel(void);
int  ui_panel(void);
void ui_ticker_push(int severity, const char *text, int x, int y);  /* SEV_*; x,y = jump target or -1 */
void ui_float_money(int x, int y, Money amount);  /* floating "-450 mk" rising from tile (x, y) */
void ui_set_tool(int tool, int param);   /* highlight the active tool key */
void ui_president_say(int mood);         /* plaque line (text_president_line); PRES_MOOD_* */

/* Bitmap fonts (T03 metrics via art_font). Draws into the current gfx target (normally a UI cache
   target while composing; drawing glyphs straight to the screen every frame costs one blit per glyph).
   The ids follow art/UI.md's font list. */
enum { FONT_BODY_DARK, FONT_BODY_LIGHT, FONT_HEAD, FONT_HEAD_LIGHT, FONT_HEAD_RED, FONT_BODY_GREY,
       FONT_BODY_RED, FONT_BODY_GREEN, FONT_BODY_AMBER, FONT_BODY_RED_LT, FONT_BODY_BLUE_LT,
       FONT_BODY_BRASS, FONT_BODY_GREEN_LT, FONT_BODY_SHADOW, FONT_FLOAT_LIGHT, FONT_FLOAT_GREEN,
       FONT_FLOAT_RED, FONT_COUNT };
int  ui_text(int x, int y, const char *s, int font);   /* (x, y) = top of the line; returns width */
int  ui_text_width(const char *s, int font);
int  ui_font_height(int font);

/* ---- T09 additions */
int  ui_key(int vk);                     /* a key press (VK_*): 1 = consumed by the UI (Esc closes the
                                            fly-out, then the panel); game.cpp acts on it otherwise */
void ui_ticker_event(const SimEvent *e); /* text_event + severity + rate limit (1 per topic per month,
                                            urgent always) + click target; SE_MONTH is ignored */
int  ui_ticker_jump(void);               /* J: emit UC_CAMERA for the newest located item; 1 = done */
const GfxFont *ui_font(int font);        /* for other modules composing with the UI fonts (menu.cpp) */
typedef struct {                         /* -test ui / bench: UI cost counters since ui_init */
    int recompose[8];                    /* per cache: top, side, ticker, window, popup */
    double recompose_ms[8];
    int draw_blits;                      /* blits issued by the last ui_draw */
    int vram_kb;                         /* cache targets */
} UiStats;
void ui_get_stats(UiStats *st);
int  ui_selftest(void);                  /* -test ui: fake town, frame at the current -res, cycles all
                                            panels, logs recompositions/blits/ms, writes screenshots
                                            uitest_<W>_<n>.bmp; returns the number of failed checks */

/* ---- F3 additions */
void ui_set_zone_view(int on);           /* zone-view key lit (game.cpp owns the toggle; V or the key) */
void ui_ticker_hint(const char *text);   /* opening hint (info lamp); a click on it emits UC_HINTS 0 */
int  ui_lot_hover_ok(void);              /* 1 = no panel/fly-out/tooltip is in the way of a hover label */
/* ---- M7 addition */
void ui_set_panel_tab(int tab);          /* tab of the open panel (scripts: screenshots) */
/* ---- PL1 additions */
void ui_open_flyout(int key);            /* open tool key `key`'s fly-out (scripts: screenshots); -1 closes */
void ui_advice_go(int go, int x, int y); /* act on a Cabinet line (advice.h AGO_*): overlay / camera / panel */
/* ---- PL3 addition */
int  ui_rect_covered(int x, int y, int w, int h);   /* 1 = the screen rect overlaps the open panel window or
                                                       fly-out (game.cpp moves/hides the hover label) */

#endif
