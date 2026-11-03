/* menu.h - title screen, New Game panel (president name, town name, seed + reroll preview, start year,
   funds, toggles), load/save slot lists, pause menu, settings. WINDOWS SIDE. Owner: T10. DESIGN 2, 8.4.

   The menu never starts, loads or saves a game itself: it returns an MR_* result and game.cpp acts on it.
   It does change g_world while showing the title (a backdrop town) and the New Game preview (mapgen of
   the chosen preset/seed), so entering the title means the running game is gone (the menu asks first).

   ---- what game.cpp calls (T08) ----------------------------------------------------------------------
   init (after art_init + ui_init, BEFORE render_init so its 3 small images get VRAM):
       menu_init();
       then, unless the command line starts a game (-load/-seed/-simtest...):
       game_set_state(GS_TITLE); menu_enter(GS_TITLE);
   every frame while game_state() is GS_TITLE, GS_NEWGAME, GS_LOADMENU or GS_PAUSEMENU:
       game_update:  r = menu_update(input_get(), dt);     (no ui_input/tools/camera/sim this frame)
                     switch (r) {
                     case MR_NEW_GAME: game_new_city(menu_new_game_params()); game_set_state(GS_PLAY); break;
                     case MR_LOAD:     if (game_load(menu_selected_path())) game_set_state(GS_PLAY);
                                       else menu_message("Could not load the town.", save_last_error());
                                       break;
                     case MR_SAVE:     if (game_save(menu_selected_path())) game_set_state(GS_PLAY);
                                       else menu_message("Could not save the town.", "");
                                       break;
                     case MR_RESUME:   game_set_state(GS_PLAY); break;
                     case MR_TITLE:    game_set_state(GS_TITLE); break;   (menu already shows the title)
                     case MR_QUIT:     game_set_state(GS_QUIT); break;
                     case MR_SETTINGS_CHANGED: re-read g_cfg play settings (edge_scroll, scroll_speed,
                                       ticker_verbosity); fps and volumes are already applied; res/fullscreen
                                       apply at the next start. break;
                     }
       game_draw:    if (!menu_draws_world()) { draw the frozen game frame: render_update/world/flush +
                     ui_draw }  then  menu_draw();
   entering a menu from play:
       Esc (ui_key returned 0) or UC_MENU  -> sim_set_speed(SPEED_PAUSE) is optional (the sim only runs
                                              in GS_PLAY); snd_stop_all(); game_set_state(GS_PAUSEMENU);
                                              menu_enter(GS_PAUSEMENU);
       UC_SAVE                              -> game_set_state(GS_PAUSEMENU); menu_enter(MENU_SAVE);
       UC_LOAD                              -> game_set_state(GS_PAUSEMENU); menu_enter(GS_LOADMENU);
       UC_QUIT                              -> game_set_state(GS_PAUSEMENU); menu_enter(MENU_QUIT_CONFIRM);
       UC_QUICKSAVE / F5                    -> save_quicksave(&meta) (save.h), no menu
       SE_YEAR                              -> save_autosave(&meta) (save.h), no menu
   game_save(path) builds SaveMeta with the camera (cam_center_tile, g_cam.zoom) and calls
   save_game(path, &meta); game_load(path) calls load_game(path, &meta), then render_reset(), ui_reset(),
   tools/life reset, restores the camera from meta, sim_set_speed(SPEED_1).
   Back from the pause menu's sub-screens returns to the pause menu; Esc on the pause menu = MR_RESUME. */
#ifndef MENU_H
#define MENU_H
#include "common.h"
#include "plat.h"
#include "game.h"

enum { MR_NONE, MR_NEW_GAME, MR_LOAD, MR_SAVE, MR_RESUME, MR_TITLE, MR_QUIT, MR_SETTINGS_CHANGED };

void menu_init(void);
void menu_enter(int gs);                 /* GS_TITLE, GS_NEWGAME, GS_LOADMENU, GS_PAUSEMENU, or MENU_* */
int  menu_update(const InputState *in, double dt);   /* MR_* */
void menu_draw(void);
const NewGameParams *menu_new_game_params(void);     /* valid after MR_NEW_GAME */
const char *menu_selected_path(void);                /* valid after MR_LOAD / MR_SAVE */

/* ---- T10 additions */
#define MENU_SAVE          (GS_COUNT + 1) /* menu_enter: save slot list (pause menu context) */
#define MENU_SETTINGS      (GS_COUNT + 2) /* menu_enter: settings screen */
#define MENU_QUIT_CONFIRM  (GS_COUNT + 3) /* menu_enter: "Quit to Windows?" (pause menu context) */
int  menu_draws_world(void);             /* 1: menu_draw paints the whole screen (title, new game, load
                                            from the title); 0: game.cpp draws the frozen game frame first */
int  menu_screen_state(void);            /* GS_* matching the screen shown (logs, tests) */
void menu_message(const char *title, const char *detail); /* modal note with an OK button */
int  menu_selftest(void);                /* -test menu: walks every screen with scripted input, logs what
                                            was shown, writes menutest_<W>_<n>.bmp; returns failures */

#endif
