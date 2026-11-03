/* ui_pol.h - politics panels inside the PANEL_POLITICS window: overview (blocs with reasons, meters,
   authority, decrees in force), decrees (choose/repeal, effects shown before signing), letters (typed letter
   with the President's signature), event cards and election night. WINDOWS SIDE. Owner: M1. DESIGN 6.

   ui.cpp owns the window, caches and hit testing; it calls uipol_* to compose the content and to handle
   clicks on CK_POL controls. ui_pol.cpp draws with the uix_* helpers that ui.cpp exports (end of ui.cpp).
   Player actions go out as UiCmd UC_POLITICS (a = POLA_*, b = id); game.cpp executes them with the
   politics_* API. game.cpp hands politics messages (politics_poll_msg) to uipol_message for the ticker. */
#ifndef UI_POL_H
#define UI_POL_H
#include "common.h"
#include "gfx.h"
#include "politics.h"

enum { POLA_SIGN, POLA_REPEAL, POLA_LETTER, POLA_CHOOSE, POLA_COUNT };   /* UC_POLITICS a */

/* ---- for ui.cpp / game.cpp */
void uipol_reset(void);                       /* new city / load (ui_reset) */
void uipol_on_open(void);                     /* panel opened: election result or a pending card first */
void uipol_title(char *buf, int size);
void uipol_panel(int x, int y, int w, int h); /* compose the paper area (screen coordinates) */
u32  uipol_data_sig(u32 h);                   /* panel data signature (politics state) */
void uipol_click(int a, int b);               /* a CK_POL control was clicked */
void uipol_message(const PolMsg *m);          /* ticker line (clicking it opens the politics panel) */
int  uipol_tip(char lines[][64], int max);    /* top bar "Approval" tooltip lines; returns count */
void uipol_show(int what);                    /* scripts/tests: open the panel at 0 overview, 1 decrees, 2 letters,
                                                 3 the pending card, 4 election night; +10*k selects decree/letter k */

/* ---- ui.cpp internals exported for ui_pol.cpp (draw into the current compose target) */
int  uix_text(int x, int y, const char *s, int font);
int  uix_text_w(const char *s, int font);
int  uix_text_r(int xr, int y, const char *s, int font);
int  uix_text_c(int x, int w, int y, const char *s, int font);
int  uix_text_fit(int x, int y, const char *s, int font, int maxw);
int  uix_wrap(const char *s, int font, int w, char out[][96], int maxl);
void uix_rule(int x, int y, int w);
void uix_bar(int x, int y, int w, int h, int v, int signed_bar, PIX c);
void uix_inset(int x, int y, int w, int h, PIX fill);
void uix_fill(int x0, int y0, int x1, int y1, PIX c);
int  uix_ctl(int x, int y, int w, int h, int a, int b);   /* CK_POL hit area; returns index */
int  uix_ctl_hot(int idx);
void uix_button(int x, int y, int w, int h, const char *label, int a, int b, int enabled);
void uix_tabs(int x, int y, int right, const char *const *names, int n, int sel);
int  uix_tab(void);                           /* selected tab of the politics panel */
void uix_set_tab(int t);
void uix_sprite(int id, int x, int y);
void uix_portrait(int mood, int x, int y);    /* 64x80 President */
void uix_cmd(int type, int a, int b);
void uix_ticker(int sev, const char *text, int panel);
void uix_dirty(void);                         /* recompose the window */

#endif
