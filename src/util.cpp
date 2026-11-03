/* util.cpp - log, timing, profiling, RNG, hashing, strings. PORTABLE. Owner: T01.
   Base by T00; T01 added the per-frame profiling view (prof_frame_*) used by -bench. SIROCCO's util.cpp
   (../sandline/src) was the model; its RDTSC profiling was not needed (QPC is enough at frame granularity).
   Only time_ms() is platform-specific (QueryPerformanceCounter / gettimeofday). */
#include "common.h"
#include "util.h"

#ifdef _WIN32
/* kernel32 QPC, declared here so this portable file never includes <windows.h> (its macros min/max/
   near/far/ERROR leak into portable code). Signatures match winbase.h (LARGE_INTEGER = 64-bit). */
extern "C" __declspec(dllimport) int __stdcall QueryPerformanceCounter(s64 *count);
extern "C" __declspec(dllimport) int __stdcall QueryPerformanceFrequency(s64 *freq);
#else
#include <sys/time.h>
#endif

const s8 g_dir_dx[DIR_COUNT] = { 0, 1, 0, -1 };     /* NE, SE, SW, NW */
const s8 g_dir_dy[DIR_COUNT] = { -1, 0, 1, 0 };
const u8 g_rot_front_dir[ROT_COUNT] = { DIR_SW, DIR_SE, DIR_NE, DIR_NW };

/* ------------------------------------------------------------------ log */
static FILE *s_log;
static int   s_echo;

void log_open(const char *path)
{
    log_close();
    if (path && path[0]) s_log = fopen(path, "w");
}

void log_close(void)
{
    if (s_log) { fclose(s_log); s_log = NULL; }
}

int log_enabled(void) { return s_log != NULL || s_echo; }

void log_set_echo(int on) { s_echo = on; }

void log_printf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    if (!s_log && !s_echo) return;
    va_start(ap, fmt);
    str_vprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (s_log) { fputs(buf, s_log); fputc('\n', s_log); fflush(s_log); }
    if (s_echo) { fputs(buf, stdout); fputc('\n', stdout); }
}

/* ------------------------------------------------------------------ time */
double time_ms(void)
{
#ifdef _WIN32
    static s64 freq;
    static double inv;
    s64 now;
    if (!freq) { QueryPerformanceFrequency(&freq); if (!freq) freq = 1; inv = 1000.0 / (double)freq; }
    QueryPerformanceCounter(&now);
    return (double)now * inv;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
#endif
}

/* ------------------------------------------------------------------ profiling */
static double s_prof_t0[PROF_COUNT], s_prof_ms[PROF_COUNT], s_prof_max[PROF_COUNT], s_prof_frame[PROF_COUNT];
static const char *const s_prof_names[PROF_COUNT] = {
    "input", "tools", "sim", "cache", "world", "sprites", "ui", "present", "user1", "user2" };

void prof_begin(int sec) { if (sec >= 0 && sec < PROF_COUNT) s_prof_t0[sec] = time_ms(); }

void prof_end(int sec)
{
    double d;
    if (sec < 0 || sec >= PROF_COUNT) return;
    d = time_ms() - s_prof_t0[sec];
    s_prof_ms[sec] += d;
    s_prof_frame[sec] += d;
    if (d > s_prof_max[sec]) s_prof_max[sec] = d;
}

void prof_frame_begin(void) { memset(s_prof_frame, 0, sizeof(s_prof_frame)); }
double prof_frame_ms(int sec) { return (sec >= 0 && sec < PROF_COUNT) ? s_prof_frame[sec] : 0.0; }

void prof_reset(void)
{
    memset(s_prof_ms, 0, sizeof(s_prof_ms));
    memset(s_prof_max, 0, sizeof(s_prof_max));
}

double prof_ms(int sec) { return (sec >= 0 && sec < PROF_COUNT) ? s_prof_ms[sec] : 0.0; }
double prof_max_ms(int sec) { return (sec >= 0 && sec < PROF_COUNT) ? s_prof_max[sec] : 0.0; }
const char *prof_name(int sec) { return (sec >= 0 && sec < PROF_COUNT) ? s_prof_names[sec] : "?"; }

/* ------------------------------------------------------------------ RNG (xorshift32) */
void rng_seed(Rng *r, u32 seed) { r->s = seed ? seed : 0x9E3779B9u; }

u32 rng_next(Rng *r)
{
    u32 x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    r->s = x;
    return x;
}

int rng_below(Rng *r, int n) { return n > 0 ? (int)(rng_next(r) % (u32)n) : 0; }

int rng_range(Rng *r, int lo, int hi) { return hi > lo ? lo + rng_below(r, hi - lo + 1) : lo; }

/* ------------------------------------------------------------------ hashing */
u32 hash_u32(u32 x)
{
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

u32 hash_xy(int x, int y, u32 salt) { return hash_u32((u32)x * 73856093u ^ (u32)y * 19349663u ^ hash_u32(salt)); }

u32 hash_bytes(u32 h, const void *data, int n)
{
    const u8 *p = (const u8 *)data;
    int i;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* ------------------------------------------------------------------ integer math */
int isqrt_i(int v)
{
    int r = 0, b = 1 << 30;
    if (v <= 0) return 0;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) { v -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return r;
}

int idiv_round(int a, int b) { return a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b); }

/* ------------------------------------------------------------------ strings */
int str_vprintf(char *buf, int size, const char *fmt, va_list ap)
{
    int n;
    if (size <= 0) return 0;
#ifdef _MSC_VER
    n = _vsnprintf(buf, size - 1, fmt, ap);
#else
    n = vsnprintf(buf, size, fmt, ap);
#endif
    buf[size - 1] = 0;
    if (n < 0 || n >= size) n = (int)strlen(buf);
    return n;
}

int str_printf(char *buf, int size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = str_vprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

void str_copy(char *dst, const char *src, int size)
{
    if (size <= 0) return;
    strncpy(dst, src ? src : "", size - 1);
    dst[size - 1] = 0;
}

int str_ieq(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)*a, cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return 0;
        if (!ca) return 1;
    }
}
