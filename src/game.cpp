/* game.cpp - game states and wiring: input -> ui -> tools -> camera; sim events -> ui ticker / sounds /
   undo log / autosave; new city, load, save; -script replay; headless runs. WINDOWS SIDE. Owner: T08.
   DESIGN 2, 4, 8, 11.5; ARCH.md 2; HANDOFF (T01 loop, T05 renderer, T07 sound, T09 UI order, T10 menus,
   T11 life).

   Per frame in GS_PLAY (main.cpp calls game_update, game_draw, game_sim_slice):
     game_update: script step -> consumed = ui_input -> hotkeys (ui_key first for Esc) -> UiCmds ->
                  camera (arrows, edge scroll, middle drag, wheel/PgUp/PgDn zoom) -> tools_update with the
                  picked tile (mouse withheld when the UI owns it, except to finish a drag) -> results
                  (sound, floating money, query panel) -> drain SimEvents (ticker, sounds, undo-log clear
                  on SE_MONTH, autosave on SE_YEAR) -> ambient layers 4x a second -> ui_update (the minimap
                  upload happens here, i.e. at frame start, before any blit) -> life_update.
     game_draw:   render_update (3 ms) -> render_world -> life_draw + tool preview marks/ghost ->
                  render_flush_sprites -> ui_draw -> cost label -> pending screenshot.
     game_sim_slice: sim_frame in GS_PLAY only.
   Menus (T10): title / new game / load / pause menu run through menu_update/menu_draw; the sim does not
   run outside GS_PLAY.

   Town hall: auto-placed next to mapgen's town site (the national road) at a new city, so Home has a
   target and the first street can start from it; the player can bulldoze/move it (unique building). */
#include "common.h"
#include "plat.h"
#include "game.h"
#include "config.h"
#include "util.h"
#include "gfx.h"
#include "art.h"
#include "iso.h"
#include "render.h"
#include "world.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "traffic.h"
#include "politics.h"
#include "ui_pol.h"       /* M1 */
#include "tools.h"
#include "text.h"
#include "ui.h"
#include "snd.h"
#include "life.h"
#include "menu.h"
#include "save.h"
#include "mapgen.h"
#include <math.h>

/* F3: the '+'/'=' and '-' keys of the main row (VK_OEM_PLUS / VK_OEM_MINUS are missing from VC6's headers) */
#define VK_OEM_PLUS_F3  0xBB
#define VK_OEM_MINUS_F3 0xBD

static int  s_state = GS_BOOT;
static int  s_ready;                    /* art/ui/render up (not headless) */
static int  s_play_direct;              /* command line asked for a game straight away */
static char s_script[PATH_LEN];
static int  s_politics_cmd;          /* M1: -politics (direct starts; the New Game menu has its own toggle) */
static int  s_ovl, s_ovl_param, s_show_buildings;
static int  s_speed_before_pause = SPEED_2;
static int  s_road_cls = ROAD_STREET;
static int  s_ui_tool = -1, s_ui_param = -1;
static int  s_mid, s_mid_x, s_mid_y;
static double s_amb_t, s_now;
static char s_shot_pending[PATH_LEN];
static int  s_shot_n;
static int  s_consumed;
static int  s_cycle_s, s_cycle_w, s_cycle_p;

/* F3: persistent zone view, opening hints, lot hover label, water-source hover highlight */
static int    s_zone_view;
static int    s_hint_on, s_hint_next, s_hint_done;  /* chain running, next hint, HINT_* bits the player already knows */
static double s_hint_t;                             /* seconds until the next hint */
static int    s_hover_lot, s_hover_mx = -1, s_hover_my = -1, s_hover_cx, s_hover_cy, s_hover_moved;
static double s_hover_t, s_hover_pick_t;            /* when the cursor came onto s_hover_lot / last pick */
static int    s_src_sel;                            /* water source chosen by a query click (water overlay param) */
enum { HINT_QUERY = 1, HINT_ZONEV = 2, HINT_PIPE = 4, HINT_ZOOM = 8, HINT_OVERLAY = 16, HINT_SPEED = 32 };

/* art ids for the preview */
static int s_id_road[ROAD_CLASS_COUNT][16], s_id_bridge[ROAD_CLASS_COUNT][2], s_id_power[16];
static int s_id_pipe[16];             /* F1: pipe_<MM>_wet for the pipe tool preview (cur_ok if missing) */
static int s_pipe_prev_ovl = -1;      /* F1: overlay before the pipe tool switched the water overlay on */
static int s_id_ok, s_id_bad, s_id_sel, s_id_query, s_id_zone[ZONE_COUNT];

/* cost label (composed into a small VRAM target only when the text changes) */
static Img  s_label;
static int  s_label_ok, s_label_w, s_label_serial = -1;
static char s_label_text[96];

/* script replay */
#define SCR_MAX_LINES 1024
static char  *s_scr_buf;
static char  *s_scr_line[SCR_MAX_LINES];
static int    s_scr_n, s_scr_pos, s_scr_wait, s_scr_errors, s_scr_quit;
static u32    s_scr_target;
static FILE  *s_scr_out;
static int    game_script_load(const char *path);
static void   autosave_finish(int wait);   /* T13 */
static void   enter_menu(int which);       /* F3: script "menu" */

/* ================================================================== command line extras */
static void parse_cmdline(void)
{
    char buf[1024], *p, *tok[64];
    int n = 0, i;
    str_copy(buf, GetCommandLineA(), sizeof(buf));
    p = buf;
    while (*p && n < 64) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '"') { p++; tok[n++] = p; while (*p && *p != '"') p++; }
        else { tok[n++] = p; while (*p && *p != ' ' && *p != '\t') p++; }
        if (*p) *p++ = 0;
    }
    for (i = 1; i < n; i++) {
        const char *a = tok[i];
        if (a[0] != '-' && a[0] != '/') continue;
        a++;
        if (str_ieq(a, "script") && i + 1 < n) { str_copy(s_script, tok[++i], PATH_LEN); s_play_direct = 1; }
        else if (str_ieq(a, "seed") || str_ieq(a, "preset") || str_ieq(a, "play") || str_ieq(a, "bench")) s_play_direct = 1;
        else if (str_ieq(a, "politics")) s_politics_cmd = 1;      /* M1: direct starts with politics on */
    }
    if (g_cfg.load_path[0]) s_play_direct = 1;
}

const char *game_script_path(void) { return s_script; }

/* ================================================================== params, new city, load, save */
void game_default_params(NewGameParams *p)
{
    Rng r;
    mapgen_defaults(&p->map);
    sim_default_settings(&p->sim);
    if (g_cfg.seed) { p->map.seed = g_cfg.seed; p->sim.seed = g_cfg.seed; }
    if (g_cfg.preset >= 0) p->map.preset = g_cfg.preset;
    if (g_cfg.president[0]) str_copy(p->sim.president, g_cfg.president, NAME_LEN);
    rng_seed(&r, p->map.seed * 2654435761u + 17);
    text_town_name(&r, p->sim.town, NAME_LEN);
    p->sim.disasters = g_cfg.disasters;
    if (s_politics_cmd) p->sim.politics = 1;   /* M1 */
}

static void set_overlay(int ovl, int param)
{
    s_ovl = ovl; s_ovl_param = param;
    if (s_ready) render_set_overlay(ovl, param, s_show_buildings);
}

static void center_on_town_hall(void)
{
    int x, y;
    if (!s_ready) return;
    if (tools_town_hall(&x, &y)) cam_center_on_tile(x, y);
    else cam_center_on_tile(g_world.w / 2, g_world.h / 2);
}

/* ---- F3: zone view (V / zone-view key), saved in the INI */
static void hint_learnt(int bit) { s_hint_done |= bit; }

static void set_zone_view(int on)
{
    s_zone_view = on ? 1 : 0;
    hint_learnt(HINT_ZONEV);
    if (!s_ready) return;
    render_set_zone_view(s_zone_view);
    ui_set_zone_view(s_zone_view);
    if (g_cfg.zone_view != s_zone_view) { g_cfg.zone_view = s_zone_view; config_save(); }
}

/* ---- F3: opening hint chain (PLAYTEST 6, DESIGN_LESSONS 4.14): one ticker line every HINT_GAP seconds of play
   for the first minutes of a NEW town; hints the player has already acted on are skipped; a click on a hint
   ends the chain (UC_HINTS); INI hints=0 turns them off. Not in script/bench runs. */
#define HINT_FIRST 12.0
#define HINT_GAP   35.0
static const struct { int bit; const char *text; } s_hints[] = {
    { HINT_QUERY,   "Right-click any building to see what it needs." },
    { HINT_ZONEV,   "Press V to show zone borders, whatever tool you hold." },
    { HINT_PIPE,    "W lays water pipes (Ctrl+drag removes them). Buildings and zoned land pass water on." },
    { HINT_ZOOM,    "+ and - zoom in and out; Z toggles the zoom." },
    { HINT_OVERLAY, "Number keys show overlays: 1 power, 2 water. Tab shows the buildings in an overlay." },
    { HINT_SPEED,   "Space pauses the town; F1-F3 set the speed." },
};

static void hints_start(void)
{
    int x, y;
    s_hint_on = 0; s_hint_next = 0; s_hint_done = 0; s_hint_t = HINT_FIRST;
    if (!s_ready) return;
    if (tools_town_hall(&x, &y)) ui_ticker_push(SEV_INFO, "The town hall stands ready, President.", x, y);
    s_hint_on = g_cfg.hints && !s_script[0] && !g_cfg.bench;
    log_printf("game: hints %s", s_hint_on ? "on" : "off");
}

static void hints_tick(double dt)
{
    if (!s_hint_on) return;
    s_hint_t -= dt;
    if (s_hint_t > 0) return;
    while (s_hint_next < COUNTOF(s_hints) && (s_hint_done & s_hints[s_hint_next].bit)) s_hint_next++;
    if (s_hint_next >= COUNTOF(s_hints)) { s_hint_on = 0; return; }
    ui_ticker_hint(s_hints[s_hint_next].text);
    log_printf("game: hint %d: %s", s_hint_next, s_hints[s_hint_next].text);
    s_hint_next++;
    s_hint_t = HINT_GAP;
}

static void after_world_change(void)
{
    tools_reset();
    tools_set_pipes_with_roads(g_cfg.pipes_with_roads);   /* F1 preference (INI / New Game); a script may change it */
    s_ui_tool = -1;
    if (!s_ready) return;
    render_reset();
    render_invalidate_all();
    ui_reset();
    life_reset();
    set_overlay(OVL_NONE, 0);
    s_pipe_prev_ovl = -1;
    s_hint_on = 0; s_src_sel = 0; s_hover_lot = 0; s_hover_mx = -1;     /* F3 */
    snd_stop_all();
}

int game_new_city(const NewGameParams *p)
{
    const MapGenReport *mr;
    if (!mapgen_generate(&p->map)) { log_printf("game: error: mapgen failed"); return 0; }
    sim_new_city(&p->sim);
    after_world_change();
    mr = mapgen_report();
    tools_auto_town_hall(mr->town_x, mr->town_y);
    if (s_ready) center_on_town_hall();
    hints_start();                              /* F3: "The town hall stands ready" + the hint chain */
    sim_set_speed(SPEED_2);
    log_printf("game: new city \"%s\", president %s, %s seed %u, %d, town site %d,%d, money %d", p->sim.town,
               p->sim.president, mapgen_preset_name(p->map.preset), p->map.seed, p->sim.start_year, mr->town_x,
               mr->town_y, g_budget.money);
    return 1;
}

int game_load(const char *path)
{
    SaveMeta m;
    if (!load_game(path, &m)) { log_printf("game: load %s failed: %s", path, save_last_error()); return 0; }
    after_world_change();
    if (s_ready) {
        render_set_zoom(ICLAMP(m.zoom, 0, ZOOM_COUNT - 1), g_cam.view_x + g_cam.view_w / 2, g_cam.view_y + g_cam.view_h / 2);
        cam_center_on_tile(m.cam_x, m.cam_y);
    }
    sim_set_speed(SPEED_1);
    log_printf("game: loaded %s (%s, %d-%02d, pop %d)", path, m.town, m.year, m.month + 1, m.population);
    return 1;
}

static void fill_meta(SaveMeta *m)
{
    save_meta_from_game(m);
    if (s_ready) { cam_center_tile(&m->cam_x, &m->cam_y); m->zoom = g_cam.zoom; }
}

int game_save(const char *path)
{
    SaveMeta m;
    fill_meta(&m);
    return save_game(path, &m);
}

/* ================================================================== init / shutdown */
static void resolve_ids(void)
{
    int c, m;
    for (c = 1; c < ROAD_CLASS_COUNT; c++) {
        for (m = 0; m < 16; m++) s_id_road[c][m] = art_findf("%s_%02d", g_roads[c].key, m);
        s_id_bridge[c][0] = art_findf("%s_bridge_05", g_roads[c].key);
        s_id_bridge[c][1] = art_findf("%s_bridge_10", g_roads[c].key);
    }
    for (m = 0; m < 16; m++) s_id_power[m] = art_findf("power_%02d", m);
    for (m = 0; m < 16; m++) { char pn[24]; str_printf(pn, sizeof(pn), "pipe_%02d_wet", m); s_id_pipe[m] = art_exists(pn) ? art_find(pn) : -1; }
    s_id_ok = art_find("cur_ok"); s_id_bad = art_find("cur_bad"); s_id_sel = art_find("cur_sel");
    s_id_query = art_find("cur_query");
    for (m = 1; m < ZONE_COUNT; m++) s_id_zone[m] = art_findf("zone_%d", m);
}

static void fatal(const char *msg)
{
    log_printf("game: error: %s", msg);
    MessageBox(plat_hwnd(), msg, GAME_TITLE, MB_OK | MB_ICONERROR);
    s_state = GS_QUIT;
}

int game_init(void)
{
    int x, y, w, h;
    char dir[PATH_LEN];
    world_init(); budget_init(); net_init(); fields_init(); traffic_init(); politics_init();
    sim_init(); tools_init(); text_init();
    parse_cmdline();
    str_printf(dir, sizeof(dir), "%s\\", g_cfg.exe_dir);
    tools_script_set_dir(dir);
    save_set_dir(g_cfg.exe_dir);
    if (g_cfg.fast) return 1;                          /* headless: main runs simtest/script */
    if (!art_init()) { fatal("Could not load the game art (art\\*.bmp missing, or not enough video memory)."); return 1; }
    ui_init();
    menu_init();                                       /* after ui_init, before render_init (T10) */
    ui_map_rect(&x, &y, &w, &h);
    g_cam.zoom = g_cfg.start_zoom == 1 ? ZOOM_32 : ZOOM_64;
    if (!art_zoom_available(g_cam.zoom)) g_cam.zoom = art_resident_zoom();   /* F3: 64-zoom art did not fit */
    cam_set_view(x, y, w, h);
    if (!render_init()) { fatal("Could not create the world view (not enough video memory)."); return 1; }
    life_init();
    resolve_ids();
    s_label_ok = gfx_make_target(260, 15, &s_label);
    s_ready = 1;
    set_zone_view(g_cfg.zone_view);            /* F3: INI zone_view */
    {   /* F3: VRAM budget at start (DESIGN 14.5; one zoom's world art resident at every resolution) */
        UiStats us;
        RenderStats rs;
        ui_get_stats(&us);
        render_stats(&rs);
        log_printf("vram: budget %dx%d: total %d KB, art %d KB (world 64-zoom %d KB, 32-zoom %d KB, resident zoom %d), "
                   "ui caches + minimap %d KB, chunk pool %d x 64 = %d KB, label %d KB, gfx accounted %d KB, free %d KB",
                   gfx_screen_w(), gfx_screen_h(), gfx_vram_total_kb(), art_vram_kb(), art_world_vram_kb(ZOOM_64),
                   art_world_vram_kb(ZOOM_32), art_resident_zoom(), us.vram_kb, rs.chunks_pool, rs.chunks_pool * 64,
                   s_label_ok ? 8 : 0, gfx_vram_used_kb(), gfx_vram_free_kb());
    }
    if (g_cfg.statlog_path[0]) sim_statlog_open(data_path(g_cfg.statlog_path));
    if (s_play_direct) {
        NewGameParams p;
        int ok = 0;
        if (g_cfg.load_path[0]) {
            ok = game_load(data_path(g_cfg.load_path));
            if (ok && g_cfg.start_zoom == 1) render_set_zoom(ZOOM_32, g_cam.view_x + g_cam.view_w / 2, g_cam.view_y + g_cam.view_h / 2);   /* T13: -zoom 1 beats the save's zoom */
        }
        if (!ok) {
            game_default_params(&p);
            if (g_cfg.bench && !s_script[0]) {     /* -bench without a script: T05's dense test town */
                render_build_test_town(g_cfg.seed);
                after_world_change();
                cam_center_on_tile(60, 64);
                ok = 1;
            } else ok = game_new_city(&p);
        }
        if (!ok) { fatal("Could not start a new town."); return 1; }
        s_state = GS_PLAY;
        if (g_cfg.start_cam_x >= 0 && g_cfg.start_cam_y >= 0) cam_center_on_tile(g_cfg.start_cam_x, g_cfg.start_cam_y);
        if (g_cfg.start_overlay > 0) { s_show_buildings = g_cfg.start_overlay >= 100; set_overlay(g_cfg.start_overlay % 100, 0); }
        if (g_cfg.start_speed >= 0) sim_set_speed(g_cfg.start_speed);
    } else {
        s_state = GS_TITLE;
        menu_enter(GS_TITLE);
    }
    if (s_script[0] && !game_script_load(s_script)) log_printf("game: warning: script %s not loaded", s_script);
    log_printf("game: init done, state %d, script %s", s_state, s_script[0] ? s_script : "-");
    return 1;
}

void game_shutdown(void)
{
    autosave_finish(1);                        /* T13: let a running autosave write finish */
    if (s_scr_out) { fclose(s_scr_out); s_scr_out = NULL; }
    if (s_scr_buf) { free(s_scr_buf); s_scr_buf = NULL; }
    sim_statlog_close();
    if (s_label_ok) { gfx_free_img(&s_label); s_label_ok = 0; }
    if (s_ready) { render_shutdown(); ui_shutdown(); s_ready = 0; }
}

int  game_state(void) { return s_state; }
void game_set_state(int gs) { s_state = gs; }
int  game_wants_quit(void) { return s_state == GS_QUIT; }

/* ================================================================== script replay */
static int game_script_load(const char *path)
{
    FILE *f = fopen(data_path(path), "rb");
    long size;
    char *p;
    if (!f) { log_printf("game: error: cannot open script %s", data_path(path)); return 0; }
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    s_scr_buf = (char *)malloc((size_t)size + 1);
    if (!s_scr_buf) { fclose(f); return 0; }
    size = (long)fread(s_scr_buf, 1, (size_t)size, f);
    fclose(f);
    s_scr_buf[size] = 0;
    s_scr_n = 0;
    p = s_scr_buf;
    while (*p && s_scr_n < SCR_MAX_LINES) {
        s_scr_line[s_scr_n++] = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = 0;
    }
    s_scr_pos = 0; s_scr_wait = 0; s_scr_errors = 0;
    s_scr_out = fopen(data_path("script_out.txt"), "w");
    log_printf("game: script %s: %d lines", path, s_scr_n);
    return 1;
}

static void request_shot(const char *name)
{
    str_copy(s_shot_pending, data_path(name), PATH_LEN);
}

/* commands the portable interpreter leaves to the game */
static int script_game_cmd(const char *line)
{
    char cmd[32];
    int a = 0, b = 0, n;
    char arg[PATH_LEN];
    arg[0] = 0;
    n = sscanf(line, "%31s %d %d", cmd, &a, &b);
    if (n < 1) return 0;
    if (str_ieq(cmd, "cam") && n >= 3) { if (s_ready) cam_center_on_tile(a, b); return 1; }
    if (str_ieq(cmd, "zoom") && n >= 2) { if (s_ready) render_set_zoom(a ? ZOOM_32 : ZOOM_64, g_cam.view_x + g_cam.view_w / 2, g_cam.view_y + g_cam.view_h / 2); return 1; }
    if (str_ieq(cmd, "overlay") && n >= 2) { s_show_buildings = a >= 100; set_overlay(a % 100, 0); return 1; }
    if (str_ieq(cmd, "shot")) { if (sscanf(line, "%31s %259s", cmd, arg) == 2 && s_ready) request_shot(arg); return 1; }
    if (str_ieq(cmd, "tool") && n >= 2) { tools_select(a, b); return 1; }
    if (str_ieq(cmd, "quit")) { s_scr_quit = 1; return 1; }
    if (str_ieq(cmd, "polpanel") && n >= 2) { if (s_ready) uipol_show(a); return 1; }   /* M1: screenshots */
    /* F3 (screenshots / VM checks): zoneview 0|1, mouse SX SY (client px; the hover label), menu (pause menu) */
    if (str_ieq(cmd, "zoneview") && n >= 2) { set_zone_view(a); return 1; }
    if (str_ieq(cmd, "mouse") && n >= 3) {
        POINT pt;
        pt.x = a; pt.y = b;
        if (s_ready) { ClientToScreen(plat_hwnd(), &pt); SetCursorPos(pt.x, pt.y); }
        return 1;
    }
    if (str_ieq(cmd, "menu")) { if (s_ready) enter_menu(GS_PAUSEMENU); return 1; }
    return 0;
}

static void script_tick(void)
{
    int lines = 0, wait, r;
    if (s_scr_pos >= s_scr_n || !s_scr_n) return;
    if (s_scr_wait) {
        if (sim_date()->total_months < s_scr_target) return;
        s_scr_wait = 0;
    }
    while (s_scr_pos < s_scr_n && lines < 16) {
        const char *line = s_scr_line[s_scr_pos++];
        lines++;
        r = tools_script_line(line, &wait, s_scr_out);
        if (r == SCR_ERROR) s_scr_errors++;
        else if (r == SCR_UNKNOWN && !script_game_cmd(line)) {
            if (s_scr_out) fprintf(s_scr_out, "script: unknown command: %s\n", line);
            s_scr_errors++;
        } else if (r == SCR_WAIT) {
            s_scr_target = sim_date()->total_months + (u32)wait;
            s_scr_wait = 1;
            if (sim_speed() == SPEED_PAUSE) sim_set_speed(SPEED_3);
            break;
        }
        if (s_scr_quit) break;
    }
    if (s_scr_out) fflush(s_scr_out);
    if (s_scr_pos >= s_scr_n || s_scr_quit) {
        log_printf("game: script done: %d errors, pop %d, money %d (%d-%02d)", s_scr_errors, sim_stats()->population,
                   g_budget.money, sim_date()->year, sim_date()->month + 1);
        if (s_scr_out) { fprintf(s_scr_out, "script: done, %d errors\n", s_scr_errors); fflush(s_scr_out); }
        if (s_scr_quit) s_state = GS_QUIT;
        s_scr_pos = s_scr_n;
    }
}

int game_script_headless(void)
{
    NewGameParams p;
    FILE *out;
    int errors;
    game_default_params(&p);
    if (!game_new_city(&p)) return 1;
    out = fopen(data_path("script_out.txt"), "w");
    errors = tools_script_run_file(data_path(s_script), out);
    if (out) { sim_dump_report(out); fclose(out); }
    if (log_enabled()) {
        log_printf("game: headless script %s: %d errors, pop %d, money %d", s_script, errors, sim_stats()->population, g_budget.money);
    }
    return errors < 0 ? 1 : errors;
}

/* ================================================================== headless simtest */
static const char *const s_town0[] = {
    "origin town", "townhall",
    "road -24 4 24 4", "road -24 10 24 10", "road -24 16 24 16", "road -24 -4 24 -4", "road -24 -10 24 -10",
    "road -24 -10 -24 16", "road -12 -10 -12 16", "road 0 -10 0 16", "road 12 -10 12 16", "road 24 -10 24 16",
    "build coal_plant 26 -14", "power 26 -12 26 16", "power -24 4 24 4", "power -24 10 24 10", "power -24 -4 24 -4",
    "build well -10 5", "build well 2 5", "build well 14 5", "build well -10 -3",
    "build school -6 5", "build sauna -2 5", "build fire_station 6 5", "build clinic 8 5", "build sauna -18 11",
    "zone -23 -9 23 15 houses", "zone -23 -3 23 -1 commercial", "zone 13 11 23 15 industry",
    "speed 3",
};

void game_build_script_town(int kind)
{
    int i, wait;
    if (kind == 1) { if (s_ready) { render_build_test_town(g_cfg.seed); after_world_change(); } return; }
    for (i = 0; i < COUNTOF(s_town0); i++) tools_script_line(s_town0[i], &wait, NULL);
}

int game_simtest(int months, const char *csv_path)
{
    NewGameParams p;
    FILE *f;
    game_default_params(&p);
    if (!game_new_city(&p)) return 1;
    game_build_script_town(0);
    sim_statlog_open(data_path(csv_path && csv_path[0] ? csv_path : "simtest.csv"));
    sim_run_months(months);
    sim_statlog_close();
    f = fopen(data_path("simtest_report.txt"), "w");
    if (f) { sim_dump_report(f); fclose(f); }
    log_printf("game: simtest %d months: pop %d, money %d, world %08x sim %08x", months, sim_stats()->population,
               g_budget.money, world_hash(), sim_hash());
    return 0;
}

void game_bench_camera(double t)
{
    int x, y, wx, wy, cx, cy;
    if (!s_ready) return;
    if (g_cfg.start_cam_x >= 0 && g_cfg.start_cam_y >= 0) { x = g_cfg.start_cam_x; y = g_cfg.start_cam_y; }   /* T13: -cam X Y = bench centre */
    else if (!tools_town_hall(&x, &y)) { render_bench_camera(t); return; }
    iso_tile_to_world(g_cam.zoom, x, y, &wx, &wy);         /* same path as render_bench_camera, around the town hall */
    cx = wx + (int)(1200.0 * sin(t * 0.27)) / (1 + g_cam.zoom);
    cy = wy + (int)(650.0 * sin(t * 0.43 + 0.6)) / (1 + g_cam.zoom);
    cam_scroll(cx - g_cam.view_w / 2 - g_cam.x, cy - g_cam.view_h / 2 - g_cam.y);
}

/* ================================================================== T13 bench report */
static u32    s_bench_steps0, s_bench_months0;
static int    s_bench_speed0;

void game_bench_begin(void)
{
    s_bench_steps0 = sim_date()->total_steps;
    s_bench_months0 = sim_date()->total_months;
    s_bench_speed0 = sim_speed();
    sim_phase_timing_total_reset();
    if (s_ready) render_totals(NULL, 1);
}

void game_bench_report(FILE *f, double secs)
{
    static const char *const rr[RR_COUNT] = { "visible", "dirty", "overlay", "prefetch", "shimmer" };
    static const int sps[SPEED_COUNT] = { 0, SIM_STEPS_PER_SEC_1, SIM_STEPS_PER_SEC_2, SIM_STEPS_PER_SEC_3 };
    RenderTotals rt;
    u32 steps = sim_date()->total_steps - s_bench_steps0;
    int k, sp = sim_speed();
    double due = secs * sps[sp];
    fprintf(f, "\nT13 sim: speed %d (start %d), %u steps in %.1f s = %.2f steps/s (speed wants %d/s: %s), %u months, pop %d\n",
            sp, s_bench_speed0, steps, secs, steps / secs, sps[sp], steps + 2 >= due ? "keeps up" : "BEHIND",
            sim_date()->total_months - s_bench_months0, sim_stats()->population);
    fprintf(f, "T13 phase ms:   n      avg      max\n");
    for (k = 0; k < SIM_STEPS_PER_CYCLE; k++) {
        const PhaseTiming *pt = sim_phase_timing_total(k);
        fprintf(f, "  phase %2d %6u %8.3f %8.3f\n", k, pt->count, pt->avg_ms, pt->max_ms);
    }
    {   /* one forced full power flood + water solve (a structural change), timed: the worst net_step case */
        double t0, t1, t2;
        world_set_power_line(0, 0, !(WT(0, 0)->road & TR_POWER_LINE));
        world_set_power_line(0, 0, !(WT(0, 0)->road & TR_POWER_LINE));
        t0 = time_ms(); net_step(); t1 = time_ms(); net_step(); t2 = time_ms();
        fprintf(f, "T13 forced structural net solve: %.2f ms + %.2f ms (power flood / water, alternate steps)\n", t1 - t0, t2 - t1);
        {   /* F1: water solves (min of 5): graph changed (a pipe toggled far away -> BFS) and demand only (fast) */
            double tb = 1e9, tf = 1e9, a;
            int k2;
            for (k2 = 0; k2 < 5; k2++) {
                world_set_pipe(0, 0, !(WT(0, 0)->flags & TF_PIPE));
                net_mark_dirty(NET_WATER); a = time_ms(); net_step(); a = time_ms() - a; if (a < tb) tb = a;
                net_mark_dirty(NET_WATER); a = time_ms(); net_step(); a = time_ms() - a; if (a < tf) tf = a;
            }
            if (WT(0, 0)->flags & TF_PIPE) { world_set_pipe(0, 0, 0); net_mark_dirty(NET_WATER); net_step(); }
            fprintf(f, "F1 water solve: %.2f ms graph changed (BFS), %.2f ms demand only (fast path)\n", tb, tf);
        }
    }
    if (!s_ready) return;
    render_totals(&rt, 0);
    fprintf(f, "T13 render: %u frames, sprites %.1f/frame, occlusion blits %.1f/frame, overlay hash checks %u\n",
            rt.frames, rt.frames ? (double)rt.sprites / rt.frames : 0.0, rt.frames ? (double)rt.occlusion_blits / rt.frames : 0.0, rt.ovl_checks);
    fprintf(f, "T13 chunks: slowest chunk %.2f ms, most chunk ms in one frame %.2f\n", rt.chunk_ms_max, rt.frame_chunk_ms_max);
    for (k = 0; k < RR_COUNT; k++)
        fprintf(f, "  %-9s %6u renders (%.2f/s), avg %.3f ms\n", rr[k], rt.chunks[k], rt.chunks[k] / secs,
                rt.chunks[k] ? rt.chunk_ms[k] / rt.chunks[k] : 0.0);
    {
        LifeStats ls;
        life_stats(&ls);
        fprintf(f, "T13 life: cars %d (target %d, visible %d), puffs %d\n", ls.cars, ls.cars_target, ls.cars_visible, ls.puffs);
    }
}

/* ================================================================== play: helpers */
static void sync_ui_tool(void)
{
    int param, tool = tools_tool(&param);
    if (tool != s_ui_tool || param != s_ui_param) { s_ui_tool = tool; s_ui_param = param; ui_set_tool(tool, param); }
}

static void select_tool(int tool, int param)
{
    tools_select(tool, param);
    if (tool == TOOL_ROAD) s_road_cls = param;
    sync_ui_tool();
    snd_play(SND_KEY, 70);
}

static void enter_menu(int which)
{
    snd_stop_all();
    s_state = GS_PAUSEMENU;
    menu_enter(which);
}

static void quicksave(void)
{
    SaveMeta m;
    fill_meta(&m);
    if (save_quicksave(&m)) { ui_ticker_push(SEV_INFO, "Quicksaved.", -1, -1); snd_play(SND_PAPER, 60); }
    else { ui_ticker_push(SEV_WARN, "Quicksave failed.", -1, -1); snd_play(SND_DENY, 70); }
}

static void do_undo(int redo)
{
    if (redo ? tools_redo() : tools_undo()) snd_play(SND_CLICK, 70);
    else snd_play(SND_DENY, 50);
}

static void zoom_to(int z, int sx, int sy)
{
    if (z == g_cam.zoom) return;
    render_set_zoom(z, sx, sy);
}

static void screen_of_tile(int x, int y, int *sx)
{
    int sy;
    cam_tile_to_screen(x, y, sx, &sy);
}

static void exec_cmd(const UiCmd *c)
{
    switch (c->type) {
    case UC_TOOL: tools_select(c->a, c->b); if (c->a == TOOL_ROAD) s_road_cls = c->b; sync_ui_tool(); break;
    case UC_SPEED: sim_set_speed(c->a); hint_learnt(HINT_SPEED); break;
    case UC_OVERLAY: set_overlay(c->a, c->b); hint_learnt(HINT_OVERLAY); break;
    case UC_TOGGLE_BUILDINGS: s_show_buildings = c->a; set_overlay(s_ovl, s_ovl_param); break;
    case UC_UNDO: do_undo(0); break;
    case UC_REDO: do_undo(1); break;
    case UC_CAMERA: cam_center_on_tile(c->a, c->b); break;
    case UC_SET_TAX: budget_set_tax(c->a, c->b); break;
    case UC_SET_FUNDING: budget_set_funding(c->a, c->b); break;
    case UC_MENU: enter_menu(GS_PAUSEMENU); break;
    case UC_SAVE: enter_menu(MENU_SAVE); break;
    case UC_QUICKSAVE: quicksave(); break;
    case UC_ZOOM: zoom_to(c->a, g_cam.view_x + g_cam.view_w / 2, g_cam.view_y + g_cam.view_h / 2); hint_learnt(HINT_ZOOM); break;
    case UC_ZONE_VIEW: set_zone_view(c->a); snd_play(SND_CLICK, 50); break;          /* F3 */
    case UC_HINTS: s_hint_on = 0; log_printf("game: hints dismissed"); snd_play(SND_PAPER, 40); break;   /* F3 */
    case UC_LOAD: enter_menu(GS_LOADMENU); break;
    case UC_QUIT: enter_menu(MENU_QUIT_CONFIRM); break;
    case UC_POLITICS:     /* M1: player actions between sim steps (like tool commits) */
        if (c->a == POLA_SIGN) { if (politics_sign(c->b)) snd_play(SND_STAMP, 80); else snd_play(SND_DENY, 60); }
        else if (c->a == POLA_REPEAL) { if (politics_repeal(c->b)) snd_play(SND_PAPER, 70); else snd_play(SND_DENY, 60); }
        else if (c->a == POLA_LETTER) { if (politics_send_letter(c->b)) snd_play(SND_TYPE, 80); else snd_play(SND_DENY, 60); }
        else if (c->a == POLA_CHOOSE) { if (politics_choose(c->b)) snd_play(SND_STAMP, 70); }
        break;
    }
}

static int cycle(int *idx, const int *list, int n)
{
    int tool, param;
    tool = tools_tool(&param);
    if (tool == TOOL_BUILD) { int i; for (i = 0; i < n; i++) if (list[i] == param) { *idx = (i + 1) % n; return list[*idx]; } }
    *idx = 0;
    return list[0];
}

static void hotkeys(const InputState *in)
{
    static const int ovl_keys[10] = { OVL_NONE, OVL_POWER, OVL_WATER, OVL_POLLUTION, OVL_LAND_VALUE, OVL_TRAFFIC,
                                      OVL_FIRE_RISK, OVL_SVC_SCHOOL, OVL_SVC_HEALTH, OVL_GROWTH };
    static const int svc_list[4] = { B_FIRE_STATION, B_SCHOOL, B_CLINIC, B_SAUNA };
    static const int wat_list[3] = { B_PUMP, B_WELL, B_WATER_TOWER };
    int k, param, tool = tools_tool(&param), mx = in->mx, my = in->my;
    if (ui_wants_keyboard()) return;
    if (key_hit(VK_ESCAPE) && !ui_key(VK_ESCAPE)) {
        if (tools_dragging()) tools_select(tool, param);
        else if (tool != TOOL_NONE) select_tool(TOOL_NONE, 0);
        else enter_menu(GS_PAUSEMENU);
        return;
    }
    if (in->ctrl) {
        if (key_pressed('Z')) do_undo(0);
        if (key_pressed('Y')) do_undo(1);
        if (key_hit('S')) enter_menu(MENU_SAVE);
        return;
    }
    if (key_hit('R')) {
        if (tool == TOOL_BUILD && !(def_building(param)->flags & BDF_NO_ROTATE)) { tools_rotate(1); snd_play(SND_CLICK, 50); }
        else select_tool(TOOL_ROAD, s_road_cls);
    }
    if (key_hit('H')) select_tool(TOOL_ZONE, ZONE_HOUSES);
    if (key_hit('A')) select_tool(TOOL_ZONE, ZONE_APARTMENTS);
    if (key_hit('C')) select_tool(TOOL_ZONE, ZONE_COMMERCIAL);
    if (key_hit('I')) select_tool(TOOL_ZONE, ZONE_INDUSTRY);
    if (key_hit('B')) select_tool(TOOL_BULLDOZE, 0);
    if (key_hit('Q')) select_tool(TOOL_QUERY, 0);
    if (key_hit('P')) {
        if (tool == TOOL_POWER_LINE) select_tool(TOOL_BUILD, B_COAL_PLANT);
        else select_tool(TOOL_POWER_LINE, 0);
    }
    if (key_hit('W')) {         /* F1: pipe first, then W cycles pump / well / tower and back to the pipe */
        if (tool == TOOL_PIPE) select_tool(TOOL_BUILD, wat_list[0]);
        else if (tool == TOOL_BUILD && param == wat_list[2]) select_tool(TOOL_PIPE, 0);
        else if (tool == TOOL_BUILD && (param == wat_list[0] || param == wat_list[1])) select_tool(TOOL_BUILD, cycle(&s_cycle_w, wat_list, 3));
        else select_tool(TOOL_PIPE, 0);
    }
    if (key_hit('S')) select_tool(TOOL_BUILD, cycle(&s_cycle_s, svc_list, 4));
    UNUSED(s_cycle_p);
    for (k = 0; k <= 9; k++)
        if (key_hit('0' + k)) {
            int o = ovl_keys[k];
            set_overlay(o == s_ovl ? OVL_NONE : o, 0);
            hint_learnt(HINT_OVERLAY);
        }
    if (key_hit(VK_TAB) && s_ovl != OVL_NONE) { s_show_buildings = !s_show_buildings; set_overlay(s_ovl, s_ovl_param); }
    if (key_hit(VK_SPACE)) {
        hint_learnt(HINT_SPEED);
        if (sim_speed() == SPEED_PAUSE) sim_set_speed(s_speed_before_pause);
        else { s_speed_before_pause = sim_speed(); sim_set_speed(SPEED_PAUSE); }
        snd_play(SND_CLICK, 60);
    }
    if (key_hit(VK_F1)) sim_set_speed(SPEED_1);
    if (key_hit(VK_F2)) sim_set_speed(SPEED_2);
    if (key_hit(VK_F3)) sim_set_speed(SPEED_3);
    if (key_hit(VK_F5)) quicksave();
    if (key_hit(VK_F12)) {
        char name[32];
        str_printf(name, sizeof(name), "shot%03d.bmp", s_shot_n++);
        request_shot(name);
    }
    if (key_hit(VK_PRIOR)) zoom_to(ZOOM_64, mx, my);
    if (key_hit(VK_NEXT)) zoom_to(ZOOM_32, mx, my);
    {   /* F3: Mac-friendly zoom (no PgUp/PgDn on a MacBook): + / - (main row and numpad), Z toggles; around the
           cursor when it is over the map, else the view centre */
        int zx = mx, zy = my;
        if (s_consumed || !in->inside) { zx = g_cam.view_x + g_cam.view_w / 2; zy = g_cam.view_y + g_cam.view_h / 2; }
        if (key_hit(VK_OEM_PLUS_F3) || key_hit(VK_ADD)) { zoom_to(ZOOM_64, zx, zy); hint_learnt(HINT_ZOOM); }
        if (key_hit(VK_OEM_MINUS_F3) || key_hit(VK_SUBTRACT)) { zoom_to(ZOOM_32, zx, zy); hint_learnt(HINT_ZOOM); }
        if (key_hit('Z')) { zoom_to(g_cam.zoom == ZOOM_64 ? ZOOM_32 : ZOOM_64, zx, zy); hint_learnt(HINT_ZOOM); }
    }
    if (key_hit('V')) { set_zone_view(!s_zone_view); snd_play(SND_CLICK, 50); }        /* F3: zone view */
    if (key_hit(VK_HOME)) center_on_town_hall();
    if (key_hit('J')) ui_ticker_jump();
}

static void camera(const InputState *in)
{
    int dx = 0, dy = 0, ex = 0, ey = 0, sp = g_cfg.scroll_speed > 0 ? g_cfg.scroll_speed : 8;
    if (g_cfg.bench) return;                         /* main drives the camera (game_bench_camera) */
    if (!ui_wants_keyboard()) {
        if (key_down(VK_LEFT)) dx--;
        if (key_down(VK_RIGHT)) dx++;
        if (key_down(VK_UP)) dy--;
        if (key_down(VK_DOWN)) dy++;
    }
    if (!dx && !dy && g_cfg.fullscreen && g_cfg.edge_scroll && in->inside) input_edge_scroll(3, &ex, &ey);
    dx += ex; dy += ey;
    if (in->shift) sp *= 2;
    cam_push(dx * sp, dy * sp);
    /* middle-drag pans 1:1 */
    if (in->buttons & MOUSE_M) {
        if (s_mid) cam_scroll(s_mid_x - in->mx, s_mid_y - in->my);
        s_mid = 1; s_mid_x = in->mx; s_mid_y = in->my;
    } else s_mid = 0;
    if (!s_consumed && in->wheel > 0) zoom_to(ZOOM_64, in->mx, in->my);
    if (!s_consumed && in->wheel < 0) zoom_to(ZOOM_32, in->mx, in->my);
    cam_update();
}

static void tool_results(int res, const InputState *in)
{
    int x, y, sx, tool = tools_last_tool();
    Money cost;
    switch (res) {
    case TRES_COMMITTED:
        tools_last_commit(&x, &y, &cost);
        screen_of_tile(x, y, &sx);
        snd_play_sx(tool == TOOL_ROAD ? SND_ROAD : tool == TOOL_BULLDOZE ? SND_BULLDOZE :
                    (tool == TOOL_ZONE || tool == TOOL_DEZONE) ? SND_CLICK : SND_BUILD, 80, sx, gfx_screen_w());
        if (cost) ui_float_money(x, y, -cost);
        break;
    case TRES_DENIED:
        snd_play(SND_DENY, 70);
        if (tools_last_reason() == REJ_NO_MONEY) ui_ticker_push(SEV_WARN, "Not enough money for that.", -1, -1);
        break;
    case TRES_QUERY: {
        int lot = render_pick_lot(in->mx, in->my);
        tools_query_tile(&x, &y);
        if (!lot) lot = world_lot_at(x, y);
        ui_open_query(lot, x, y);
        hint_learnt(HINT_QUERY);               /* F3 */
        if (s_ovl == OVL_WATER) {              /* F1: a water source's area is highlighted in the overlay */
            const Lot *ql = world_lot(lot);
            s_src_sel = ql && def_building(ql->def)->cat == BCAT_WATER ? lot : 0;    /* F3: hover falls back to it */
            set_overlay(OVL_WATER, s_src_sel);
        }
        snd_play(SND_CLICK, 60);
        break; }
    case TRES_CANCELLED:
        snd_play(SND_CLICK, 40);
        break;
    }
    sync_ui_tool();
}

/* T13: the yearly autosave (~0.35 s on the P2: one long frame) waits for a calm moment: the camera has not
   moved for AS_CALM seconds and no tool drag is running, but at most AS_MAX_WAIT seconds after the new year. */
#define AS_CALM     0.75
#define AS_MAX_WAIT 20.0
static int    s_as_pending, s_as_camx, s_as_camy, s_as_camz;
static double s_as_since, s_as_moved;

/* The file part (rotate AUTO3 <- AUTO2 <- AUTO1, write AUTO1 via .tmp; ~0.7 s over the VM's SMB share) runs
   on a worker thread with Win32 file calls only (the VC6 static CRT is single-threaded: no fopen/malloc/log
   there). The main thread serialises (consistent state), then polls for completion and frees the buffer. */
static SaveFile     *s_as_sf;
static HANDLE        s_as_thread;
static volatile LONG s_as_done, s_as_ok;
static char          s_as_path[SAVE_AUTOS][PATH_LEN], s_as_tmp[PATH_LEN];
static double        s_as_t0;

static DWORD WINAPI autosave_thread(LPVOID arg)
{
    const u8 *p = save_mem_data(s_as_sf);
    DWORD left = (DWORD)save_mem_size(s_as_sf), done;
    HANDLE h;
    int i, ok = 1;
    UNUSED(arg);
    DeleteFile(s_as_path[SAVE_AUTOS - 1]);
    for (i = SAVE_AUTOS - 2; i >= 0; i--) MoveFile(s_as_path[i], s_as_path[i + 1]);
    h = CreateFile(s_as_tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) ok = 0;
    while (ok && left) {
        if (!WriteFile(h, p, left, &done, NULL) || !done) ok = 0;
        else { p += done; left -= done; }
    }
    if (h != INVALID_HANDLE_VALUE && !CloseHandle(h)) ok = 0;
    if (ok) { DeleteFile(s_as_path[0]); ok = MoveFile(s_as_tmp, s_as_path[0]) != 0; }
    else DeleteFile(s_as_tmp);
    InterlockedExchange((LONG *)&s_as_ok, ok);
    InterlockedExchange((LONG *)&s_as_done, 1);
    return 0;
}

static void autosave_finish(int wait)
{
    if (!s_as_thread) return;
    if (!s_as_done && !(wait && WaitForSingleObject(s_as_thread, 10000) == WAIT_OBJECT_0)) return;
    log_printf("save: autosave %s %s (%d bytes, file write on a thread %.0f ms)", s_as_ok ? "written" : "error: failed to write",
               s_as_path[0], save_mem_size(s_as_sf), time_ms() - s_as_t0);
    CloseHandle(s_as_thread); s_as_thread = NULL;
    save_mem_close(s_as_sf); s_as_sf = NULL;
}

static void autosave_now(void)
{
    SaveMeta m;
    DWORD tid;
    int i;
    double t0 = time_ms();
    fill_meta(&m);
    s_as_sf = save_mem_open();
    if (!s_as_sf || !save_game_to(s_as_sf, &m)) {
        if (s_as_sf) { save_mem_close(s_as_sf); s_as_sf = NULL; }
        save_autosave(&m);                                  /* fallback: the synchronous path */
        return;
    }
    for (i = 0; i < SAVE_AUTOS; i++) save_auto_path(i, s_as_path[i], PATH_LEN);
    str_printf(s_as_tmp, PATH_LEN, "%s.tmp", s_as_path[0]);
    s_as_done = 0; s_as_ok = 0; s_as_t0 = time_ms();
    s_as_thread = CreateThread(NULL, 0, autosave_thread, NULL, 0, &tid);
    log_printf("save: autosave serialised in %.1f ms (%d bytes), writing %s", s_as_t0 - t0, save_mem_size(s_as_sf),
               s_as_thread ? "on a thread" : "now");
    if (!s_as_thread) { autosave_thread(NULL); s_as_thread = NULL; log_printf("save: autosave %s", s_as_ok ? "written" : "failed"); save_mem_close(s_as_sf); s_as_sf = NULL; }
}

static void autosave_tick(void)
{
    if (g_cam.x != s_as_camx || g_cam.y != s_as_camy || g_cam.zoom != s_as_camz) {
        s_as_camx = g_cam.x; s_as_camy = g_cam.y; s_as_camz = g_cam.zoom; s_as_moved = s_now;
    }
    autosave_finish(0);
    if (!s_as_pending || s_as_thread) return;
    if (s_now - s_as_since < AS_MAX_WAIT && (s_now - s_as_moved < AS_CALM || tools_dragging())) return;
    s_as_pending = 0;
    autosave_now();
    log_printf("game: autosave %.1f s after the new year", s_now - s_as_since);
}

static void drain_events(void)
{
    SimEvent e;
    PolMsg pm;
    int x, y, sx;
    while (politics_poll_msg(&pm)) {        /* M1: politics messages -> ticker (click opens the President's desk) */
        uipol_message(&pm);
        if (pm.type == PMSG_EVENT || pm.type == PMSG_CAMPAIGN) snd_play(SND_PAPER, 70);
        else if (pm.type == PMSG_ELECTION) { snd_play(SND_FANFARE, 80); ui_president_say(pm.b > 0 ? PRES_MOOD_DELIGHTED : pm.b < 0 ? PRES_MOOD_WORRIED : PRES_MOOD_CONTENT); }
        else if (pm.type == PMSG_FROST || pm.type == PMSG_BLOC_ANGRY) snd_play(SND_WARN, 50);
    }
    while (sim_poll_event(&e)) {
        ui_ticker_event(&e);
        switch (e.type) {
        case SE_MONTH:
            tools_month_changed();
            if (budget_net_last() > 0 && tools_town_hall(&x, &y)) {
                ui_float_money(x, y, budget_net_last());
                snd_play(SND_CASH, 30);
            }
            break;
        case SE_YEAR:
            if (!s_scr_n && !g_cfg.bench && !s_as_pending) { s_as_pending = 1; s_as_since = s_now; }   /* T13: autosave_tick */
            break;
        case SE_STATUS_UP: case SE_MILESTONE:
            snd_play(SND_FANFARE, 80);
            break;
        case SE_UNLOCK:
            break;
        case SE_FIRE:
            snd_play(SND_FIREBELL, 80);
            break;
        default:
            if (e.severity == SEV_URGENT) snd_play(SND_WARN, 70);
            else if (e.severity == SEV_WARN && e.x >= 0) { screen_of_tile(e.x, e.y, &sx); snd_play_sx(SND_CHIME, 40, sx, gfx_screen_w()); }
            break;
        }
    }
}

static void ambient(double dt)
{
    const CityStats *st = sim_stats();
    int birds, traffic, industry;
    s_amb_t -= dt;
    if (s_amb_t > 0) return;
    s_amb_t = 0.25;
    if (sim_speed() == SPEED_PAUSE) { snd_set_ambient(25, 0, 0); return; }
    birds = ICLAMP(70 - st->population / 150, 20, 70);
    traffic = ICLAMP(life_traffic_level(), 0, 100);
    if (traffic < st->population / 200) traffic = ICLAMP(st->population / 200, 0, 60);
    industry = ICLAMP(st->jobs_zone[ZONE_INDUSTRY] / 15, 0, 80);
    snd_set_ambient(birds, traffic, industry);
}

static void menu_frame(double dt)
{
    int r = menu_update(input_get(), dt);
    switch (r) {
    case MR_NEW_GAME:
        if (game_new_city(menu_new_game_params())) s_state = GS_PLAY;
        else menu_message("Could not create the town.", "");
        break;
    case MR_LOAD:
        if (game_load(menu_selected_path())) s_state = GS_PLAY;
        else menu_message("Could not load the town.", save_last_error());
        break;
    case MR_SAVE:
        if (game_save(menu_selected_path())) s_state = GS_PLAY;
        else menu_message("Could not save the town.", "");
        break;
    case MR_RESUME: s_state = GS_PLAY; break;
    case MR_TITLE: s_state = GS_TITLE; break;
    case MR_QUIT: s_state = GS_QUIT; break;
    default: break;
    }
}

/* F3: the lot under the resting cursor (hover label after 0.4 s; water source hover highlight). Picks at most
   every 0.1 s and only when the cursor or the camera moved (render_pick_lot tests sprite pixels). */
static void hover_update(const InputState *in)
{
    int lot, t = tools_tool(NULL), want;
    if (s_consumed || !in->inside || tools_dragging()) { s_hover_lot = 0; s_hover_mx = -1; }
    else {
        if (in->mx != s_hover_mx || in->my != s_hover_my || g_cam.x != s_hover_cx || g_cam.y != s_hover_cy) {
            s_hover_mx = in->mx; s_hover_my = in->my; s_hover_cx = g_cam.x; s_hover_cy = g_cam.y; s_hover_moved = 1;
        }
        if (s_hover_moved && s_now - s_hover_pick_t >= 0.1) {
            s_hover_moved = 0; s_hover_pick_t = s_now;
            lot = render_pick_lot(in->mx, in->my);
            if (lot != s_hover_lot) { s_hover_lot = lot; s_hover_t = s_now; }
        }
    }
    /* water overlay: hovering a source highlights its area (WATER.md 3.6 A); else the query-selected source */
    if (s_ovl != OVL_WATER) { s_src_sel = 0; return; }
    want = s_src_sel;
    if (s_hover_lot && (t == TOOL_NONE || t == TOOL_QUERY || t == TOOL_PIPE)) {
        const Lot *l = world_lot(s_hover_lot);
        if (l && def_building(l->def)->cat == BCAT_WATER) want = s_hover_lot;
    }
    if (want != s_ovl_param) set_overlay(OVL_WATER, want);
}

/* ================================================================== per frame */
void game_update(double dt)
{
    const InputState *in = input_get();
    ToolInput ti;
    UiCmd c;
    int res, drag;
    s_now += dt;
    if (!s_ready) return;
    if (s_state == GS_TITLE || s_state == GS_NEWGAME || s_state == GS_LOADMENU || s_state == GS_PAUSEMENU) {
        menu_frame(dt);
        if (s_state == GS_PLAY) sync_ui_tool();
        return;
    }
    if (s_state != GS_PLAY) return;
    script_tick();
    s_consumed = ui_input(in);
    hotkeys(in);
    if (s_state != GS_PLAY) return;                  /* Esc opened the pause menu */
    while (ui_poll_cmd(&c)) exec_cmd(&c);
    if (s_state != GS_PLAY) return;
    {   /* F1: the pipe tool shows the water overlay (and restores the previous one after); the bulldozer removes
           pipes only while they are visible; the "pipes with new roads" preference */
        int t = tools_tool(NULL);
        if (t == TOOL_PIPE) hint_learnt(HINT_PIPE);                                   /* F3 */
        if (t == TOOL_PIPE && s_pipe_prev_ovl < 0) { s_pipe_prev_ovl = s_ovl; if (s_ovl != OVL_WATER) set_overlay(OVL_WATER, 0); }
        else if (t != TOOL_PIPE && s_pipe_prev_ovl >= 0) { if (s_ovl == OVL_WATER) set_overlay(s_pipe_prev_ovl, 0); s_pipe_prev_ovl = -1; }
        tools_set_show_pipes(s_ovl == OVL_WATER);
    }
    camera(in);
    hover_update(in);                                /* F3 */
    hints_tick(dt);                                  /* F3 */
    /* tools: mouse goes to the tools unless the UI owns it (a running drag still gets its release) */
    memset(&ti, 0, sizeof(ti));
    drag = tools_dragging();
    ti.shift = in->shift; ti.ctrl = in->ctrl;
    if (!s_consumed || drag) {
        ti.on_map = !s_consumed && in->inside && cam_pick(in->mx, in->my, &ti.tx, &ti.ty);
        ti.down = (in->buttons & MOUSE_L) != 0;
        ti.pressed = !s_consumed && (in->pressed & MOUSE_L) != 0;
        ti.released = (in->released & MOUSE_L) != 0;
        ti.cancel = !s_consumed && (in->pressed & MOUSE_R) != 0;
    }
    res = tools_update(&ti);
    tool_results(res, in);
    drain_events();
    autosave_tick();
    ambient(dt);
    ui_update(dt);
    life_update(dt);
}

/* ---- drawing */
static void preview_marks(void)
{
    const ToolPreview *p = tools_preview();
    const InputState *in = input_get();
    int i, tool = p->tool, budget = 240, x, y;
    if (!p->active) {
        int t = tools_tool(NULL);
        if ((t == TOOL_NONE || t == TOOL_QUERY) && !s_consumed && in->inside && cam_pick(in->mx, in->my, &x, &y))
            render_tile_mark(t == TOOL_QUERY ? s_id_query : s_id_sel, x, y);
        return;
    }
    if (tool == TOOL_BUILD) {
        const BuildingDef *d = def_building(p->lot_def);
        int id = art_lot_sprite(d->key, 0, p->rot, ART_LOT_NORMAL, p->lot_w, p->lot_h);
        for (i = 0; i < p->n; i++) render_tile_mark(p->tiles[i].state == PT_REJECT || !p->lot_ok ? s_id_bad : s_id_ok, p->tiles[i].x, p->tiles[i].y);
        render_lot_ghost(id, p->lot_x, p->lot_y, p->lot_w, p->lot_h);
        return;
    }
    for (i = 0; i < p->n && budget > 0; i++) {
        const PreviewTile *pt = &p->tiles[i];
        int id = -1;
        x = pt->x; y = pt->y;
        if (pt->state == PT_REJECT) id = s_id_bad;
        else if (pt->state == PT_NOOP) continue;
        else if (tool == TOOL_ROAD) {
            int m = tools_preview_mask(x, y);
            const Tile *t = world_tile(x, y);
            if (m < 0) m = 0;
            if (TER_IS_WATER(TER_TYPE(t)) && (m == 5 || m == 10)) id = s_id_bridge[p->param][m == 10];
            else id = s_id_road[p->param][m & 15];
        } else if (tool == TOOL_POWER_LINE) {
            int m = tools_preview_mask(x, y);
            id = s_id_power[m < 0 ? 0 : m & 15];
        } else if (tool == TOOL_PIPE) {        /* F1: laying = the wet pipe piece, removing = the selection mark */
            int m = tools_preview_mask(x, y);
            id = p->param ? s_id_sel : s_id_pipe[m < 0 ? 0 : m & 15];
            if (id < 0) id = s_id_ok;
        } else if (tool == TOOL_ZONE) {
            if (p->n > 240 && x != p->x0 && x != p->x1 && y != p->y0 && y != p->y1) continue;   /* big rect: outline */
            id = s_id_zone[p->param];
        } else if (tool == TOOL_BULLDOZE || tool == TOOL_DEZONE) {
            if (p->n > 240 && x != p->x0 && x != p->x1 && y != p->y0 && y != p->y1) continue;
            id = s_id_sel;
        } else id = s_id_ok;
        render_tile_mark(id, x, y);
        budget--;
    }
}

static void label_text(char *buf, int size)
{
    const ToolPreview *p = tools_preview();
    char m[32];
    int year = sim_date()->year;
    buf[0] = 0;
    if (!p->active || (!p->dragging && p->tool != TOOL_BUILD)) {
        /* F3: hover label on lots after ~0.4 s at rest: "Town hall", "Rintamamiestalo, tier 2" */
        const Lot *l = world_lot(s_hover_lot);
        const BuildingDef *d;
        if (!s_hover_lot || !l || s_now - s_hover_t < 0.4 || !ui_lot_hover_ok()) return;
        d = def_building(l->def);
        if (l->status == LOT_BUILDING) str_printf(buf, size, "%s (under construction)", d->name);
        else if (l->status == LOT_ABANDONED) str_printf(buf, size, "%s (abandoned)", d->name);
        else if (d->cat == BCAT_ZONE) str_printf(buf, size, "%s, tier %d", d->name, d->tier);
        else str_copy(buf, d->name, size);
        return;
    }
    if (p->main_reason == REJ_LOCKED) { str_printf(buf, size, "%s: not available yet", text_tool_name(p->tool, p->param)); return; }
    if (p->n_ok == 0 || (p->tool == TOOL_BUILD && !p->lot_ok)) {
        if (p->main_reason) str_printf(buf, size, "%s", text_place_reason(p->main_reason));
        return;
    }
    text_fmt_money(m, sizeof(m), p->cost, year);
    if (p->tool == TOOL_BUILD) str_printf(buf, size, "%s  %s%s", text_tool_name(p->tool, p->param), m, p->affordable ? "" : "  (not enough money)");
    else if (p->tool == TOOL_ZONE && (p->eff_residents || p->eff_jobs))
        str_printf(buf, size, "%d tiles  +%d %s%s", p->n_ok, p->eff_residents ? p->eff_residents : p->eff_jobs,
                   p->eff_residents ? "homes" : "jobs", p->n_rejected ? "  (some too far from a road)" : "");
    else str_printf(buf, size, "%d tiles  %s%s", p->n_ok, m, p->affordable ? "" : "  (not enough money)");
}

static void draw_label(void)
{
    const InputState *in = input_get();
    char t[96];
    int x, y;
    if (!s_label_ok || s_consumed || !in->inside) return;
    label_text(t, sizeof(t));
    if (!t[0]) return;
    if (strcmp(t, s_label_text) || gfx_restore_serial() != s_label_serial) {
        PIX bg = gfx_rgb(43, 33, 25), edge = gfx_rgb(120, 96, 60);
        str_copy(s_label_text, t, sizeof(s_label_text));
        gfx_set_target(&s_label);
        gfx_clear(bg);
        s_label_w = IMIN(260, ui_text(4, 2, t, FONT_BODY_LIGHT) + 8);
        gfx_rect(0, 0, s_label_w, 15, edge);
        gfx_set_target(NULL);
        s_label_serial = gfx_restore_serial();
    }
    x = ICLAMP(in->mx + 16, g_cam.view_x, g_cam.view_x + g_cam.view_w - s_label_w);
    y = ICLAMP(in->my + 20, g_cam.view_y, g_cam.view_y + g_cam.view_h - 15);
    gfx_blit_part(&s_label, 0, 0, s_label_w, 15, x, y);
}

static void draw_play(int interactive)
{
    prof_begin(PROF_CACHE); render_update(3.0); prof_end(PROF_CACHE);
    prof_begin(PROF_WORLD); render_world(); prof_end(PROF_WORLD);
    prof_begin(PROF_SPRITES);
    life_draw();
    if (interactive) preview_marks();
    render_flush_sprites();
    prof_end(PROF_SPRITES);
    prof_begin(PROF_UI);
    ui_draw();
    if (interactive) draw_label();
    prof_end(PROF_UI);
}

void game_draw(void)
{
    if (!s_ready) return;
    if (s_state == GS_TITLE || s_state == GS_NEWGAME || s_state == GS_LOADMENU || s_state == GS_PAUSEMENU) {
        if (!menu_draws_world()) draw_play(0);
        menu_draw();
    } else if (s_state == GS_PLAY) draw_play(1);
    if (s_shot_pending[0]) {
        gfx_save_bmp(s_shot_pending);
        log_printf("game: screenshot %s", s_shot_pending);
        s_shot_pending[0] = 0;
    }
}

int game_sim_slice(double dt, double budget_ms)
{
    if (s_state != GS_PLAY) return 0;
    return sim_frame(dt, budget_ms);
}
