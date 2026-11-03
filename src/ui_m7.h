/* ui_m7.h - M7 UI: the graphs panel and the trade panel (ui_graph.cpp: inside ui.cpp's panel window, composed with
   the uix_* helpers of ui_pol.h) and the newspaper (ui_news.cpp: the January issue, ceremony pages and the 1990
   newsreel, drawn straight onto the back buffer while it is open; the game pauses meanwhile, so the ~600 glyph
   blits a frame cost nothing a player feels and no VRAM cache is needed). WINDOWS SIDE. Owner: M7.

   Wiring (game.cpp): uinews_open() when era_news_pending() and the newspaper setting is on (else
   uinews_to_ticker()); while uinews_active(): uinews_input(input) eats all input, keys go to uinews_key(vk), and
   uinews_draw() runs after ui_draw(). UC_M7 commands (a = M7A_*) come from both panels and the newspaper key. */
#ifndef UI_M7_H
#define UI_M7_H
#include "common.h"
#include "plat.h"

enum { M7A_SIGN,        /* b = TL_*: sign the waiting agreement (politics off; on: the card does it) */
       M7A_BOND,        /* issue the bond */
       M7A_SAUNA,       /* sauna diplomacy */
       M7A_NEWS_LAST,   /* show the latest newspaper again */
       M7A_NEWSREEL,    /* show the newsreel on demand */
       M7A_CARD,        /* open the President's desk at the waiting agreement card */
       M7A_COUNT };

/* ---- panels (ui.cpp calls these for PANEL_GRAPHS and PANEL_TRADE) */
void uim7_title(int panel, char *buf, int size);
void uim7_panel(int panel, int x, int y, int w, int h);
u32  uim7_data_sig(int panel, u32 h);
void uim7_click(int panel, int a, int b);

/* ---- newspaper */
void uinews_open(void);             /* show era_news() (pending or the last one) */
void uinews_open_newsreel(void);
int  uinews_active(void);
void uinews_close(void);            /* marks the issue seen (era_news_seen) */
int  uinews_input(const InputState *in);   /* 1 while open (consumes the mouse) */
int  uinews_key(int vk);            /* 1 = consumed */
void uinews_draw(void);
void uinews_to_ticker(void);        /* newspaper off: the main headline goes to the ticker, issue marked seen */

#endif
