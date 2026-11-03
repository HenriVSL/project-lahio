/* config.cpp - defaults, LAHIO.INI (migrates KAUPUNKI.INI), command line (config.h). WINDOWS SIDE. Owner: T01.
   Ported from SIROCCO ../sandline/src/config.cpp (tokenizer, ini read/write, "don't persist one-run
   command-line overrides" rule from its T15), adapted to KAUPUNKI's Config fields and switches. */
#include "common.h"
#include "config.h"
#include "util.h"
#include "mapgen.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

Config g_cfg;
static char s_ini[PATH_LEN];
static char s_paths[8][PATH_LEN];
static int  s_next;

static int  s_migrated;                 /* settings came from the old KAUPUNKI.INI this run */

/* ------------------------------------------------------------------ INI-backed integer keys */
typedef struct { const char *name; int *val; } IniKey;
static IniKey s_keys[] = {
    { "fullscreen",       &g_cfg.fullscreen },
    { "renderer",         &g_cfg.gdi },          /* 0 = DirectDraw (auto fallback), 1 = GDI */
    { "edge_scroll",      &g_cfg.edge_scroll },
    { "scroll_speed",     &g_cfg.scroll_speed },
    { "ticker_verbosity", &g_cfg.ticker_verbosity },
    { "volume",           &g_cfg.volume },
    { "sound",            &g_cfg.sound },
    { "disasters",        &g_cfg.disasters },
    { "show_fps",         &g_cfg.show_fps },
    { "sfx_volume",       &g_cfg.sfx_volume },       /* T10 (snd.cpp reads the same keys at init) */
    { "ambient_volume",   &g_cfg.ambient_volume },
    { "pipes_with_roads", &g_cfg.pipes_with_roads }, /* F1: lay water pipes with new roads (default 0) */
    { "zone_view",        &g_cfg.zone_view },        /* F3: persistent zone borders (V) */
    { "hints",            &g_cfg.hints },            /* F3: opening hints at a new game (0 = off) */
    { "newspaper",        &g_cfg.newspaper },        /* M7: January paper opens (1) or ticker only (0) */
};
#define NUM_KEYS COUNTOF(s_keys)
/* res and fps are stored as text ("800", "auto"); handled next to the table */
static int s_iniVal[NUM_KEYS + 2], s_cmdVal[NUM_KEYS + 2], s_over[NUM_KEYS + 2];
static int s_haveCmd;

static int res_from_px(int px)
{
    if (px == 640) return RES_640;
    if (px == 1024) return RES_1024;
    if (px == 800) return RES_800;
    return -1;
}
static int res_px(int res) { return res == RES_640 ? 640 : res == RES_1024 ? 1024 : 800; }

static int fps_from_text(const char *s)
{
    if (!s || !s[0]) return -1;
    if (str_ieq(s, "auto") || !strcmp(s, "0")) return 0;
    if (!strcmp(s, "60")) return 60;
    if (!strcmp(s, "30")) return 30;
    return -1;
}

static void set_defaults(void)
{
    memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.res = RES_800; g_cfg.fullscreen = 1; g_cfg.gdi = 0; g_cfg.fps = 0;
    g_cfg.edge_scroll = 1; g_cfg.scroll_speed = 8; g_cfg.ticker_verbosity = 1;
    g_cfg.volume = 80; g_cfg.sound = 1; g_cfg.disasters = 1;
    str_copy(g_cfg.president, "Kekkola", NAME_LEN);
    g_cfg.preset = -1;
    g_cfg.shot_time = -1.0f; g_cfg.exit_after = -1.0f;
    g_cfg.start_zoom = g_cfg.start_cam_x = g_cfg.start_cam_y = g_cfg.start_overlay = g_cfg.start_speed = -1;
    g_cfg.vsync = 1;
    g_cfg.sfx_volume = 100; g_cfg.ambient_volume = 70;
    g_cfg.hints = 1;                                 /* F3 */
    g_cfg.newspaper = 1;                             /* M7 */
}

static void snapshot(int *dst)
{
    int i;
    for (i = 0; i < NUM_KEYS; i++) dst[i] = *s_keys[i].val;
    dst[NUM_KEYS] = g_cfg.res;
    dst[NUM_KEYS + 1] = g_cfg.fps;
}

static int file_exists(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void read_ini(void)
{
    char buf[64], old[PATH_LEN];
    const char *src = s_ini, *sec = CONFIG_INI_SECTION;
    int i, v;
    if (!file_exists(s_ini)) {
        /* rename migration: no LAHIO.INI yet but a KAUPUNKI.INI from the working title -> read it once,
           then write LAHIO.INI below (the old file is left alone) */
        str_printf(old, PATH_LEN, "%s\\" CONFIG_OLD_INI, g_cfg.exe_dir);
        if (!file_exists(old)) { config_save(); return; }    /* write defaults if missing */
        src = old; sec = CONFIG_OLD_SECTION; s_migrated = 1;
    }
    for (i = 0; i < NUM_KEYS; i++)
        *s_keys[i].val = GetPrivateProfileInt(sec, s_keys[i].name, *s_keys[i].val, src);
    v = res_from_px(GetPrivateProfileInt(sec, "res", res_px(g_cfg.res), src));
    if (v >= 0) g_cfg.res = v;
    GetPrivateProfileString(sec, "fps", "auto", buf, sizeof(buf), src);
    v = fps_from_text(buf);
    if (v >= 0) g_cfg.fps = v;
    GetPrivateProfileString(sec, "president", g_cfg.president, g_cfg.president, NAME_LEN, src);
    g_cfg.scroll_speed = ICLAMP(g_cfg.scroll_speed, 1, 64);
    g_cfg.volume = ICLAMP(g_cfg.volume, 0, 100);
    g_cfg.ticker_verbosity = ICLAMP(g_cfg.ticker_verbosity, 0, 2);
    g_cfg.sfx_volume = ICLAMP(g_cfg.sfx_volume, 0, 100);
    g_cfg.ambient_volume = ICLAMP(g_cfg.ambient_volume, 0, 100);
    if (s_migrated) config_save();
}

void config_save(void)
{
    char v[32];
    int i, cur[NUM_KEYS + 2];
    if (!s_ini[0]) return;
    snapshot(cur);
    for (i = 0; i < NUM_KEYS + 2; i++) {
        if (s_haveCmd && s_over[i] && cur[i] == s_cmdVal[i]) cur[i] = s_iniVal[i];
        if (s_haveCmd) s_iniVal[i] = cur[i];
    }
    for (i = 0; i < NUM_KEYS; i++) {
        str_printf(v, sizeof(v), "%d", cur[i]);
        WritePrivateProfileString(CONFIG_INI_SECTION, s_keys[i].name, v, s_ini);
    }
    str_printf(v, sizeof(v), "%d", res_px(cur[NUM_KEYS]));
    WritePrivateProfileString(CONFIG_INI_SECTION, "res", v, s_ini);
    WritePrivateProfileString(CONFIG_INI_SECTION, "fps", cur[NUM_KEYS + 1] == 60 ? "60" : cur[NUM_KEYS + 1] == 30 ? "30" : "auto", s_ini);
    WritePrivateProfileString(CONFIG_INI_SECTION, "president", g_cfg.president, s_ini);
    WritePrivateProfileString(NULL, NULL, NULL, s_ini);     /* flush the Win9x profile cache */
}

const char *config_ini_path(void) { return s_ini; }
int config_ini_migrated(void) { return s_migrated; }

const char *data_path(const char *rel)
{
    char *buf = s_paths[s_next];
    s_next = (s_next + 1) & 7;
    if (!rel) rel = "";
    if (rel[0] == '\\' || rel[0] == '/' || (rel[0] && rel[1] == ':') || !g_cfg.exe_dir[0]) str_copy(buf, rel, PATH_LEN);
    else str_printf(buf, PATH_LEN, "%s\\%s", g_cfg.exe_dir, rel);
    return buf;
}

/* ------------------------------------------------------------------ command line (supports "quoted args") */
#define MAX_ARGS 64
static char  s_argBuf[1024];
static char *s_argv[MAX_ARGS];
static int   s_argc;

static void tokenize(const char *cmd)
{
    char *p = s_argBuf;
    s_argc = 0;
    if (!cmd) return;
    str_copy(s_argBuf, cmd, sizeof(s_argBuf));
    while (*p && s_argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') {
            s_argv[s_argc++] = ++p;
            while (*p && *p != '"') p++;
        } else {
            s_argv[s_argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
        }
        if (*p) *p++ = 0;
    }
}

static int is_sw(const char *a, const char *name) { return (a[0] == '-' || a[0] == '/') && str_ieq(a + 1, name); }
static int is_val(int i) { return i < s_argc && s_argv[i][0] != '-' && s_argv[i][0] != '/'; }
static int is_num(int i) { return i < s_argc && (s_argv[i][0] == '-' || (s_argv[i][0] >= '0' && s_argv[i][0] <= '9') || s_argv[i][0] == '.'); }

/* next argument as int, clamped (ICLAMP is a macro: never pass s_argv[++i] to it directly) */
static int arg_int(int *i, int lo, int hi)
{
    int v = atoi(s_argv[++*i]);
    return ICLAMP(v, lo, hi);
}

static void parse_cmdline(void)
{
    int i, v;
    for (i = 0; i < s_argc; i++) {
        const char *a = s_argv[i];
        int more = s_argc - 1 - i;
        if (is_sw(a, "window") || is_sw(a, "windowed")) g_cfg.fullscreen = 0;
        else if (is_sw(a, "fullscreen")) g_cfg.fullscreen = 1;
        else if (is_sw(a, "gdi")) { g_cfg.gdi = 1; g_cfg.fullscreen = 0; }
        else if (is_sw(a, "res") && more >= 1) {
            v = res_from_px(atoi(s_argv[++i]));
            if (v >= 0) g_cfg.res = v;
        }
        else if (is_sw(a, "fps") && more >= 1) {
            v = fps_from_text(s_argv[++i]);
            if (v >= 0) g_cfg.fps = v;
        }
        else if (is_sw(a, "seed") && more >= 1) g_cfg.seed = (u32)strtoul(s_argv[++i], NULL, 10);
        else if (is_sw(a, "preset") && more >= 1) g_cfg.preset = mapgen_preset_from_name(s_argv[++i]);
        else if (is_sw(a, "load") && more >= 1) str_copy(g_cfg.load_path, s_argv[++i], PATH_LEN);
        else if (is_sw(a, "bench")) {
            g_cfg.bench = 20;
            if (is_val(i + 1)) g_cfg.bench = atoi(s_argv[++i]);
            if (g_cfg.bench <= 0) g_cfg.bench = 20;
        }
        else if (is_sw(a, "shot") && more >= 2) {
            g_cfg.shot_time = (float)atof(s_argv[++i]);
            str_copy(g_cfg.shot_path, s_argv[++i], PATH_LEN);
        }
        else if (is_sw(a, "exitafter") && more >= 1) g_cfg.exit_after = (float)atof(s_argv[++i]);
        else if (is_sw(a, "log")) {
            g_cfg.log = 1;
            if (is_val(i + 1)) str_copy(g_cfg.log_path, s_argv[++i], PATH_LEN);
        }
        else if (is_sw(a, "fast")) g_cfg.fast = 1;
        else if (is_sw(a, "simtest") && more >= 1) g_cfg.simtest_months = atoi(s_argv[++i]);
        else if (is_sw(a, "statlog") && more >= 1) str_copy(g_cfg.statlog_path, s_argv[++i], PATH_LEN);
        else if (is_sw(a, "test") && more >= 1) str_copy(g_cfg.test_name, s_argv[++i], sizeof(g_cfg.test_name));
        else if (is_sw(a, "zoom") && more >= 1) g_cfg.start_zoom = arg_int(&i, 0, ZOOM_COUNT - 1);
        else if (is_sw(a, "cam") && more >= 2 && is_num(i + 1)) {
            g_cfg.start_cam_x = atoi(s_argv[++i]);
            g_cfg.start_cam_y = atoi(s_argv[++i]);
        }
        else if (is_sw(a, "overlay") && more >= 1) g_cfg.start_overlay = atoi(s_argv[++i]);
        else if (is_sw(a, "speed") && more >= 1) g_cfg.start_speed = arg_int(&i, 0, SPEED_COUNT - 1);
        else if (is_sw(a, "nosound")) g_cfg.sound = 0;
        else if (is_sw(a, "showfps")) g_cfg.show_fps = 1;
        else if (is_sw(a, "novsync")) g_cfg.vsync = 0;
        else if (is_sw(a, "simload") && more >= 1) g_cfg.simload_ms = arg_int(&i, 0, 200);
        else if (is_sw(a, "direct")) g_cfg.test_direct = 1;
        else if (is_sw(a, "flipmode") && more >= 1) g_cfg.flip_mode = arg_int(&i, 0, 2);
        else if (is_sw(a, "pacetrace") && more >= 1) g_cfg.pace_trace = arg_int(&i, 0, 100000);
        else if (is_sw(a, "repairs") && more >= 1) g_cfg.max_repairs = arg_int(&i, 1, 16);
        /* unknown switches are ignored (logged by main with the whole command line) */
    }
}

void config_init(const char *cmdline)
{
    char *p;
    set_defaults();
    GetModuleFileName(NULL, g_cfg.exe_dir, PATH_LEN);
    p = strrchr(g_cfg.exe_dir, '\\');
    if (p) *p = 0; else str_copy(g_cfg.exe_dir, ".", PATH_LEN);
    str_printf(s_ini, PATH_LEN, "%s\\" CONFIG_INI_NAME, g_cfg.exe_dir);
    read_ini();
    snapshot(s_iniVal);
    tokenize(cmdline);
    parse_cmdline();
    snapshot(s_cmdVal);
    {
        int i;
        for (i = 0; i < NUM_KEYS + 2; i++) s_over[i] = s_cmdVal[i] != s_iniVal[i];
    }
    s_haveCmd = 1;
    if (g_cfg.seed == 0) g_cfg.seed = (u32)GetTickCount();
    if (g_cfg.seed == 0) g_cfg.seed = 1;
}
