/* config.h - LAHIO.INI settings + command line. WINDOWS SIDE implementation, portable header.
   Owner: T01. DESIGN 8.4, 11.1. Port of SIROCCO config.cpp.

   Order: defaults, then LAHIO.INI (next to the exe; if it is missing, an old KAUPUNKI.INI [Kaupunki] is
   read once and written out as LAHIO.INI), then the command line.
   Command line (all optional):
     -window            windowed (development); default fullscreen
     -gdi               software GDI backend
     -res 640|800|1024  screen mode (default 800)
     -fps 60|30|auto    frame-rate policy (default auto)
     -seed N            map/sim seed for a quick new game (0 = time based)
     -preset NAME       map preset: lakeland|river|coast|forest
     -load FILE         load a save at start
     -bench N           N seconds of benchmark (scripted camera), writes bench.txt
     -shot T FILE       save the back buffer as BMP at T seconds of run time
     -exitafter T       quit after T seconds of run time (0 = right after init)
     -log               write lahio.log
     -fast              headless: no window, no rendering (with -simtest)
     -simtest MONTHS    run the simulation headless for MONTHS months on a generated town; writes the
                        per-month stat CSV (-statlog, default simtest.csv) and a text report to the log
     -statlog FILE      per-month stat CSV during normal play too (sim_statlog_open)
     -test NAME         self-test: art|snd|render|... (module selftests; exit code = failures)
     -zoom 0|1          start zoom; -cam X Y  start camera centre tile; -overlay N  start overlay
     -speed 0..3        start speed (screenshot/bench setups)
     -nosound
   T01 additions:
     -log [FILE]        log file name (default lahio.log next to the exe)
     -showfps           debug overlay (fps, frame/work ms, VRAM); F11 toggles it at run time
     -novsync           timer pacing instead of display-locked pacing (WaitForVerticalBlank)
     -simload MS        test scene: busy MS ms in the sim slice every frame (with -bench: only in the
                        middle third of the run) to exercise the automatic 60 -> 30 fps drop and recovery
     -direct            test scene: blit every diamond each frame instead of the 256x128 chunk targets
     -flipmode 0|1|2    fullscreen present order (debug): 0 wait blank + Flip, 1 Flip ahead + wait,
                        2 wait blank + Flip(NOVSYNC)
     -pacetrace N       log per-frame pacing data for N frames (debug only)
   INI (section [Lahio]): res=640|800|1024 fullscreen=0|1 renderer=0|1 (1 = GDI) fps=60|30|auto
     edge_scroll scroll_speed ticker_verbosity volume sound disasters president show_fps
     sfx_volume ambient_volume (T10).
     Written with defaults when missing. config_save() writes the INI keys back; a value that is still
     equal to its one-run command-line override is saved with its INI value (SIROCCO T15 rule).
*/
#ifndef CONFIG_H
#define CONFIG_H
#include "common.h"

typedef struct {
    /* display */
    int   res;                   /* RES_* (default RES_800) */
    int   fullscreen;            /* 1 default; -window = 0 */
    int   gdi;                   /* force the GDI backend */
    int   fps;                   /* 60, 30, 0 = auto (default) */
    /* play settings (INI) */
    int   edge_scroll;           /* 1 default */
    int   scroll_speed;          /* px per frame at zoom 0 (default 8) */
    int   ticker_verbosity;      /* 0 urgent only, 1 normal, 2 all */
    int   volume;                /* 0..100 */
    int   sound;                 /* 0 off (-nosound) */
    int   disasters;             /* default for New Game */
    char  president[NAME_LEN];   /* last used president name (default "Kekkola") */
    /* run control (command line) */
    u32   seed;
    int   preset;                /* MAP_PRESET_* (mapgen.h), -1 = default */
    char  load_path[PATH_LEN];
    int   bench;                 /* seconds, 0 = off */
    float shot_time;             /* < 0 = off */
    char  shot_path[PATH_LEN];
    float exit_after;            /* < 0 = never; 0 = right after init */
    int   log;
    int   fast;
    int   simtest_months;        /* 0 = off */
    char  statlog_path[PATH_LEN];
    char  test_name[32];         /* "" = off */
    int   start_zoom, start_cam_x, start_cam_y, start_overlay, start_speed; /* -1 = default */
    char  exe_dir[PATH_LEN];
    /* T01 additions */
    char  log_path[PATH_LEN];    /* -log FILE ("" = lahio.log) */
    int   show_fps;              /* debug overlay (INI show_fps, -showfps, F11) */
    int   vsync;                 /* 1 = display-locked pacing when the refresh allows it (-novsync = 0) */
    int   simload_ms;            /* test scene synthetic sim load */
    int   test_direct;           /* test scene without chunk targets */
    int   flip_mode;             /* -flipmode: fullscreen present order (plat.cpp), default 0 */
    int   pace_trace;            /* -pacetrace N: log N frames of pacing data (debug) */
    /* T10 additions (INI, settings menu) */
    int   sfx_volume;            /* 0..100 (default 100): snd group SND_GROUP_SFX */
    int   ambient_volume;        /* 0..100 (default 70): snd group SND_GROUP_AMBIENT */
    /* T13 addition (perf experiments) */
    int   max_repairs;           /* -repairs N: non-mandatory chunk renders per frame (0 = renderer default) */
    /* F1 addition */
    int   pipes_with_roads;      /* INI pipes_with_roads (New Game checkbox): new roads get water pipes (0) */
    /* F3 additions (INI) */
    int   zone_view;             /* INI zone_view: persistent zone borders on (V / zone-view key), default 0 */
    int   hints;                 /* INI hints: opening hint chain in the ticker at a new game (default 1) */
} Config;

extern Config g_cfg;
void        config_init(const char *cmdline);  /* defaults, INI, command line */
void        config_save(void);                 /* writes the INI settings back */
const char *config_ini_path(void);
int         config_ini_migrated(void);         /* 1: this run read the old KAUPUNKI.INI (logged by main) */
#define CONFIG_INI_NAME    "LAHIO.INI"
#define CONFIG_INI_SECTION "Lahio"
#define CONFIG_OLD_INI     "KAUPUNKI.INI"     /* working-title INI, migrated once */
#define CONFIG_OLD_SECTION "Kaupunki"
const char *data_path(const char *rel);        /* exe_dir + "\\" + rel (absolute paths unchanged);
                                                  rotating static buffers (8) */

#endif
