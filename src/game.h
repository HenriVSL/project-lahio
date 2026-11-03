/* game.h - game states and wiring: input -> ui -> tools -> camera; sim events -> ui/snd/tools;
   new city / load / save; headless sim test. WINDOWS SIDE. Owner: T08 (state machine + wiring; menu
   screens are T10's menu.cpp). DESIGN 2, 8, 11.5.

   main.cpp (T01) owns WinMain, the frame loop, timing, -bench/-shot/-exitafter. Per frame it calls:
       plat_pump(); input_poll();
       game_update(dt);                       PROF_TOOLS (ui input, tools, camera, sim events)
       game_draw();                           PROF_CACHE/WORLD/SPRITES/UI inside: issues ALL blits
       game_sim_slice(dt, plat_frame_budget_ms() - margin);   PROF_SIM: blitter works meanwhile
       plat_present();                        PROF_PRESENT
   If game_init() returns 0 (modules not ready yet) main.cpp runs its own test scene instead. */
#ifndef GAME_H
#define GAME_H
#include "common.h"
#include "mapgen.h"
#include "sim.h"

enum { GS_BOOT, GS_TITLE, GS_NEWGAME, GS_LOADMENU, GS_PLAY, GS_PAUSEMENU, GS_QUIT, GS_COUNT };

typedef struct {
    MapGenParams map;
    SimSettings  sim;
} NewGameParams;
void game_default_params(NewGameParams *p);   /* Lakeland, seed from config, 1955, normal funds, Kekkola */

int  game_init(void);                    /* after plat/gfx/snd init: art, render, ui, sim, tools, life,
                                            menu. Applies config start options (-load, -seed, -zoom...).
                                            0 = not ready (main shows the T01 test scene) */
void game_shutdown(void);
void game_update(double dt);             /* once per frame after input_poll */
void game_draw(void);                    /* all blits of the frame (no present) */
int  game_sim_slice(double dt, double budget_ms);  /* GS_PLAY: sim_frame; returns steps run */
int  game_state(void);
void game_set_state(int gs);
int  game_wants_quit(void);

int  game_new_city(const NewGameParams *p);  /* mapgen, sim_new_city, reset render/ui/tools/life; 1 ok */
int  game_load(const char *path);
int  game_save(const char *path);

/* Headless runs (no window; -fast -simtest N): new city from g_cfg seed, build the scripted test town
   with tools_apply_*, run N months, write the stat CSV (sim_statlog) and sim_dump_report to the log.
   Returns the process exit code (0 ok). */
int  game_simtest(int months, const char *csv_path);
/* Scripted town for simtest/bench: kind 0 = small starter town, 1 = dense 20k bench city. */
void game_build_script_town(int kind);
/* -bench camera path: position the camera for bench time t (seconds). */
void game_bench_camera(double t);

/* ---- T08 additions
   -script FILE (parsed by game.cpp from the command line): replays tool commands (tools.h script
   language) during play, frame by frame ("wait N" = N game months at the current speed), extra game
   commands: cam X Y, zoom 0|1, overlay N, shot FILE, quit. With -fast it runs headless (no window):
   game_script_headless() is main.cpp's entry, output in script_out.txt + the log.
   Start: straight into a new game with -seed/-preset/-script/-load/-bench/-play, else the title screen. */
const char *game_script_path(void);      /* "" = no -script */
int  game_script_headless(void);         /* exit code = script errors */

/* ---- T13 additions (bench report): main.cpp calls game_bench_begin() when the -bench measurement starts
   and game_bench_report(f) at the end of bench.txt (sim keep-up, per-phase timings, chunk renders). */
void game_bench_begin(void);
void game_bench_report(FILE *f, double secs);

#endif
