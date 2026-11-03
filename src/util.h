/* util.h - log, timing, profiling, deterministic RNG, hashing, string helpers. PORTABLE. Owner: T01
   (initial minimal implementation by T00; T01 ports the rest of SIROCCO's util.cpp).

   util.cpp is portable: the only platform code is time_ms() (QueryPerformanceCounter on Windows,
   gettimeofday elsewhere) behind #ifdef _WIN32. The simulation and the Mac tests link it.

   Logging convention (docs/ARCH.md "Logging"): one line per call, prefixed with the module name,
   e.g. log_printf("sim: month %d done, %d steps", ...). Warnings start with "warning: ", errors with
   "error: ". The log is flushed after every line so a crash keeps everything. Never log per frame or per
   tile in normal runs; per-month or per-event lines only. */
#ifndef UTIL_H
#define UTIL_H
#include "common.h"

/* ------------------------------------------------------------------ log */
void log_open(const char *path);       /* start logging to a file (truncates). NULL/"" = stderr-free no-op */
void log_close(void);
int  log_enabled(void);                /* 1 if a log file is open (skip building expensive messages) */
void log_printf(const char *fmt, ...); /* appends "fmt\n", flushes; no-op if not open */
void log_set_echo(int on);             /* also print to stdout (Mac tests, -fast runs) */

/* ------------------------------------------------------------------ time and profiling
   time_ms: monotonic wall clock in milliseconds with sub-ms resolution. Use only for budgets, logs and
   profiling, never as simulation input (DESIGN 5.1 determinism). */
double time_ms(void);

/* Profiling sections for -bench (DESIGN 11.5). begin/end pairs of one section must not nest; sections
   may overlap others. main.cpp times INPUT, TOOLS (tools + ui input), SIM (sim slice), CACHE (chunk repair),
   WORLD (chunk blits), SPRITES (dynamic sprites + occlusion fix), UI, PRESENT. bench.txt lists each one. */
enum { PROF_INPUT, PROF_TOOLS, PROF_SIM, PROF_CACHE, PROF_WORLD, PROF_SPRITES, PROF_UI, PROF_PRESENT,
       PROF_USER1, PROF_USER2, PROF_COUNT };
void        prof_begin(int sec);
void        prof_end(int sec);
void        prof_reset(void);          /* zero all sections */
double      prof_ms(int sec);          /* accumulated ms since prof_reset */
double      prof_max_ms(int sec);      /* worst single begin..end since prof_reset */
const char *prof_name(int sec);
/* Per-frame view (T01, for -bench worst-frame breakdowns): prof_frame_begin() at the top of each frame
   zeroes the per-frame accumulators; prof_frame_ms(sec) = ms spent in sec since then. */
void        prof_frame_begin(void);
double      prof_frame_ms(int sec);

/* ------------------------------------------------------------------ deterministic RNG
   xorshift32 with explicit state, so each user owns a stream: the simulation's Rng is in g_sim and in
   the save file; cosmetic code (life, ui) uses its own Rng and never touches the simulation's. */
typedef struct { u32 s; } Rng;
void rng_seed(Rng *r, u32 seed);       /* seed 0 is replaced by a fixed non-zero constant */
u32  rng_next(Rng *r);                 /* full 32 bits */
int  rng_below(Rng *r, int n);         /* 0..n-1 (n <= 0 returns 0) */
int  rng_range(Rng *r, int lo, int hi);/* lo..hi inclusive */

/* Stateless hashes for deterministic per-tile choices (building variants, grass variants, noise). */
u32  hash_u32(u32 x);                  /* good avalanche (lowbias32) */
u32  hash_xy(int x, int y, u32 salt);  /* hash of a tile coordinate */
u32  hash_bytes(u32 h, const void *data, int n); /* FNV-1a continuation; start with HASH_INIT */
#define HASH_INIT 2166136261u

/* ------------------------------------------------------------------ integer math */
int  isqrt_i(int v);                   /* floor(sqrt(v)), v >= 0 */
int  idiv_round(int a, int b);         /* a / b rounded to nearest, b > 0 */

/* ------------------------------------------------------------------ strings */
int  str_printf(char *buf, int size, const char *fmt, ...); /* always NUL-terminates; returns length */
int  str_vprintf(char *buf, int size, const char *fmt, va_list ap);
void str_copy(char *dst, const char *src, int size);        /* always NUL-terminates */
int  str_ieq(const char *a, const char *b);                 /* ASCII case-insensitive equality */

#endif
