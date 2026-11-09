/* common.h - KAUPUNKI shared types, constants and macros. PORTABLE (no windows.h). Owner: T00.
   DESIGN 3, 5.2, 5.3, 11.2, 11.6, 11.8. See docs/ARCH.md for the module map and conventions.

   Every header includes this file first. It must stay portable: it compiles with VC6 (/W3) and with
   clang++ -std=c++98 -Wall on the Mac (tests/Makefile.mac). Windows-only things live in plat.h.

   VC6 / C++98 rules (DESIGN 11.8, SIROCCO DESIGN 4.12), short form:
   - C-style C++: structs + free functions, file statics (s_ prefix), module globals g_*. No STL, no
     exceptions, no templates, no RTTI, no new/malloc in per-frame or per-step code (allocate at init).
   - `for (int i ...)` leaks i into the enclosing scope in VC6: declare loop variables at the top of the
     function, never redeclare one in the same function.
   - No <cstdint>, no `long long` literals in shared code (use s64 below), no snprintf (use str_printf in
     util.h), no // comments in headers shared with C tools (fine in .cpp), declarations at block top.
   - Don't name anything near, far, min, max, small, interface, ERROR, IN, OUT (windows.h macros).
   - Float -> int casts call slow _ftol on VC6: the simulation is integer-only anyway (DESIGN 5.1).
   - /W3 clean on VC6 and -Wall clean on clang. ASCII source; text uses Latin-1 escapes ("\xe4" = a-umlaut).
*/
#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ fixed-size integer types */
typedef unsigned char  u8;
typedef signed char    s8;
typedef unsigned short u16;
typedef signed short   s16;
typedef unsigned int   u32;      /* int is 32-bit on both VC6/x86 and clang/arm64 */
typedef signed int     s32;
#ifdef _MSC_VER
typedef __int64          s64;
typedef unsigned __int64 u64;
#define S64_FMT "%I64d"
#else
typedef long long          s64;
typedef unsigned long long u64;
#define S64_FMT "%lld"
#endif
typedef u16 PIX;                 /* one 16-bit screen pixel, RGB565 or RGB555 (gfx.h decides) */
typedef s32 Money;               /* WHOLE markka (mk), +-2.1e9. budget.cpp keeps sub-mk remainders
                                    internally. (DESIGN 5.9 said pennies; whole mk avoids overflow.) */

/* compile-time check: STATIC_ASSERT(sizeof(Tile) == 8, tile_size); */
#define STATIC_ASSERT(cond, name) typedef char static_assert_##name[(cond) ? 1 : -1]

/* ------------------------------------------------------------------ game */
#define GAME_TITLE    "Project L\xE4hi\xF6"   /* Latin-1 a/o umlauts (window caption; the UI fonts have them); working title was KAUPUNKI */
#define GAME_TITLE_ASCII "Project Lahio"      /* logs, bench reports, text files */
#define GAME_TAGLINE  "A Finnish town, 1955-1990"
#define GAME_VERSION  "1.1"      /* shown as "Version ..." (title, pause menu, top bar); 1.0 released 2026-11-05 */
#define PATH_LEN      260        /* = MAX_PATH; for portable headers */
#define NAME_LEN      24         /* president / town / district names incl. NUL */

/* ------------------------------------------------------------------ map (DESIGN 3, 5.2)
   Arrays are sized for the maximum map (256x256) with a fixed row stride of MAP_STRIDE, so a tile index
   is (y << MAP_SHIFT) | x. The playable size is runtime: g_world.w / g_world.h (default 128). Loops must
   use g_world.w/h, never MAP_MAX_*. Tile coordinates: x = "i" runs down-right on screen (+X, SE),
   y = "j" runs down-left (+Y, SW). Tile (0,0) is the top corner of the map. */
#define MAP_MAX_W     256
#define MAP_MAX_H     256
#define MAP_SHIFT     8
#define MAP_STRIDE    (1 << MAP_SHIFT)
#define MAP_DEF_W     128        /* default size (Milestone 1 only uses this) */
#define MAP_DEF_H     128
#define TILE_IDX(x, y) (((y) << MAP_SHIFT) | (x))
#define TILE_METRES   12         /* 1 tile = 12 m (art scale, STYLE.md 3) */

#define MAX_LOTS      16384      /* building instances incl. slot 0 (= none); 128^2 of 1x1 lots */

/* Field maps (pollution, land value, ...) are on a 2x2-tile grid (DESIGN 5.2). */
#define FIELD_SHIFT   1          /* field cell = tile >> FIELD_SHIFT */
#define FIELD_MAX_W   (MAP_MAX_W >> FIELD_SHIFT)
#define FIELD_MAX_H   (MAP_MAX_H >> FIELD_SHIFT)

/* ------------------------------------------------------------------ directions and rotations
   Directions name the tile EDGE / neighbour, clockwise from the top-right edge on screen.
   Autotile masks (roads, power lines) use bit (1 << DIR_*): NE=1, SE=2, SW=4, NW=8. */
enum { DIR_NE, DIR_SE, DIR_SW, DIR_NW, DIR_COUNT };   /* (0,-1) (+1,0) (0,+1) (-1,0) */
extern const s8 g_dir_dx[DIR_COUNT];                 /* defined in util.cpp */
extern const s8 g_dir_dy[DIR_COUNT];
#define DIR_OPPOSITE(d) (((d) + 2) & 3)
/* Lot rotation = art rotation rN (STYLE.md 4): the side the FRONT (door, street) faces.
   r0 front +Y (SW), r1 front +X (SE), r2 front -Y (NE), r3 front -X (NW).
   Rotations 1 and 3 swap the footprint: a def with size w x h occupies h x w tiles. */
enum { ROT_0, ROT_1, ROT_2, ROT_3, ROT_COUNT };
extern const u8 g_rot_front_dir[ROT_COUNT];          /* { DIR_SW, DIR_SE, DIR_NE, DIR_NW } */

/* ------------------------------------------------------------------ zoom and tiles (DESIGN 11.2)
   Two natively rendered zoom levels. Zoom 0 is the main 64x32 view, zoom 1 the 32x16 overview. */
enum { ZOOM_64, ZOOM_32, ZOOM_COUNT };
#define TILE_W(z)     (64 >> (z))        /* diamond width in px */
#define TILE_H(z)     (32 >> (z))        /* diamond height in px */
#define CHUNK_W       256                /* world-cache chunk surface size (DESIGN 11.3) */
#define CHUNK_H       128

/* ------------------------------------------------------------------ screen modes (DESIGN 11.2) */
enum { RES_640, RES_800, RES_1024, RES_COUNT };     /* all 16 bpp; default RES_800 fullscreen */
#define SCREEN_MAX_W  1024
#define SCREEN_MAX_H  768
#define ATLAS_MAX_W   640                /* art sheets are at most 640 px wide (640x480 must work) */

/* ------------------------------------------------------------------ time (DESIGN 5.3) */
#define FRAME_HZ             60          /* target frame rate (auto-fallback to 30, DESIGN 1) */
#define SIM_STEPS_PER_CYCLE  16          /* 1 step = one phase of the phase table */
#define SIM_CYCLES_PER_MONTH 4
#define SIM_STEPS_PER_MONTH  (SIM_STEPS_PER_CYCLE * SIM_CYCLES_PER_MONTH)   /* 64 */
#define MONTHS_PER_YEAR      12
enum { SPEED_PAUSE, SPEED_1, SPEED_2, SPEED_3, SPEED_COUNT };
#define SIM_STEPS_PER_SEC_1  3           /* speed 1: a month ~ 21 s */
#define SIM_STEPS_PER_SEC_2  6
#define SIM_STEPS_PER_SEC_3  16          /* speed 3: a year ~ 48 s */
#define START_YEAR_DEFAULT   1955

/* ------------------------------------------------------------------ small helpers
   (Upper-case names on purpose: min/max are windows.h macros.)
   P1: IMIN/IMAX/ICLAMP/IABS are inline FUNCTIONS, not macros, so an argument with a side effect (a call that reads
   the save, ++i, atoi(argv[++i])) is evaluated exactly once - the macros evaluated it up to three times (3 bugs, see
   docs/HANDOFF.md). Overloads for int, s64, u32 and double (all the types the code uses); u8/u16/s8/s16/enum promote
   to int. Mixed arguments (s64 with int, u32 with int) are ambiguous on purpose: cast one side. */
inline int    IMIN(int a, int b)       { return a < b ? a : b; }
inline s64    IMIN(s64 a, s64 b)       { return a < b ? a : b; }
inline u32    IMIN(u32 a, u32 b)       { return a < b ? a : b; }
inline double IMIN(double a, double b) { return a < b ? a : b; }
inline int    IMAX(int a, int b)       { return a > b ? a : b; }
inline s64    IMAX(s64 a, s64 b)       { return a > b ? a : b; }
inline u32    IMAX(u32 a, u32 b)       { return a > b ? a : b; }
inline double IMAX(double a, double b) { return a > b ? a : b; }
inline int    ICLAMP(int v, int lo, int hi)          { return v < lo ? lo : (v > hi ? hi : v); }
inline s64    ICLAMP(s64 v, s64 lo, s64 hi)          { return v < lo ? lo : (v > hi ? hi : v); }
inline u32    ICLAMP(u32 v, u32 lo, u32 hi)          { return v < lo ? lo : (v > hi ? hi : v); }
inline double ICLAMP(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline int    IABS(int a)              { return a < 0 ? -a : a; }
inline s64    IABS(s64 a)              { return a < 0 ? -a : a; }
inline double IABS(double a)           { return a < 0 ? -a : a; }
#define COUNTOF(a)        ((int)(sizeof(a) / sizeof((a)[0])))
#define UNUSED(x)         ((void)(x))
/* floor division / modulo for possibly negative numerators (chunk grids, world pixels) */
#define FLOORDIV(a, b)    (((a) >= 0) ? ((a) / (b)) : -((-(a) + (b) - 1) / (b)))
#define FLOORMOD(a, b)    ((a) - FLOORDIV(a, b) * (b))

#endif
