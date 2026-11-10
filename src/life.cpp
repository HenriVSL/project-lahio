/* life.cpp - cosmetic life: cars (logic in lifecar.cpp), chimney smoke, water shimmer timing, drawing,
   -test life. WINDOWS SIDE. Owner: T11. See life.h.
   Smoke spawn rule from art/ART.md 6 and T05's demo (render.cpp demo_update/demo_draw). */
#include "common.h"
#include "plat.h"
#include "gfx.h"
#include "life.h"
#include "lifecar.h"
#include "lifebus.h"
#include "render.h"
#include "art.h"
#include "iso.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "budget.h"
#include "traffic.h"
#include "politics.h"
#include "config.h"
#include "util.h"
#include "ui_layout.h"
#include "civic.h"

#define SMOKE_SRC_MAX   48
#define SMOKE_SCAN_LOTS 768          /* lots examined per frame by the smoke-source scan */
#define SHIMMER_MS      300

typedef struct {
    u8  used, zoom;
    u8  dtx, dty;                     /* depth tile (lot front tile) */
    s32 bx, by;                       /* chimney hotspot, world px at `zoom` */
    s32 age_ms, life_ms;
    s16 wind;                         /* drift px/s x16 at zoom 64 */
    s16 phase;
} Puff;
typedef struct {
    u16 lot;
    s32 next_ms;                      /* smoke clock time of the next puff */
    s32 interval;                     /* ms between puffs */
} SmokeSrc;

static int      s_inited, s_carsOn = 1, s_smokeOn = 1;
static int      s_carIds[LC_COLOURS][DIR_COUNT];
static int      s_busIds[DIR_COUNT];      /* M2: bus_<dir> (lifebus.cpp) */
static int      s_smokeIds[6];
static int      s_fireIds[4];         /* M5: fire0..3 flame frames (ART.md 3.1), drawn on burning lots */
static s32      s_fireT, s_fireSmokeT;
static Puff     s_puffs[LIFE_MAX_SMOKE];
static int      s_npuffs;
static SmokeSrc s_src[SMOKE_SRC_MAX], s_scan[SMOKE_SRC_MAX];
static int      s_nsrc, s_nscan, s_scanLot = 1, s_scanZoom = -1;
static s32      s_smokeT;             /* smoke clock, ms (stops while paused) */
static double   s_msAcc, s_animAcc;
static int      s_animFrame;
static Rng      s_rng;
static LifeStats s_ls;
static u32      s_resets;
static int      s_stress;             /* -test life stress mode: forced flow, 1972, big population */

/* ------------------------------------------------------------------ init / reset */
void life_init(void)
{
    static const char *const dn[DIR_COUNT] = { "ny", "px", "py", "nx" };   /* DIR_NE, SE, SW, NW (STYLE.md 9) */
    int i, d;
    for (i = 0; i < LC_COLOURS; i++)
        for (d = 0; d < DIR_COUNT; d++) s_carIds[i][d] = art_findf("car%d_%s", i, dn[d]);
    for (i = 0; i < 6; i++) s_smokeIds[i] = art_findf("smoke%d", i);
    for (d = 0; d < DIR_COUNT; d++) s_busIds[d] = art_findf("bus_%s", dn[d]);     /* M2 */
    for (i = 0; i < 4; i++) s_fireIds[i] = art_findf("fire%d", i);                 /* M5 */
    lc_init();
    s_inited = 1;
    life_reset();
}

void life_reset(void)
{
    s_resets++;
    lc_reset(0x5eed0000u ^ (g_world.serial * 2654435761u) ^ s_resets);
    lb_reset();                                                                   /* M2: buses */
    rng_seed(&s_rng, 0xc0ffeeu ^ s_resets);
    memset(s_puffs, 0, sizeof(s_puffs));
    s_npuffs = 0; s_nsrc = 0; s_nscan = 0; s_scanLot = 1; s_scanZoom = -1;
    s_smokeT = 0; s_msAcc = 0;
    memset(&s_ls, 0, sizeof(s_ls));
}

void life_set_enabled(int cars, int smoke)
{
    s_carsOn = !!cars; s_smokeOn = !!smoke;
    if (!s_carsOn) lc_reset(s_resets);
    if (!s_smokeOn) { memset(s_puffs, 0, sizeof(s_puffs)); s_npuffs = 0; }
}

int life_car_count(void) { return lc_count(); }

/* ------------------------------------------------------------------ smoke */
static void lot_front_sv(int lot, int z, int *fx, int *fy, int *wx, int *wy)
{
    int x0, y0, x1, y1;
    world_lot_rect(lot, &x0, &y0, &x1, &y1);
    *fx = x1; *fy = y1;
    iso_tile_to_world(z, x1, y1, wx, wy);
    *wy += TILE_H(z);                 /* south vertex of the front tile = sprite anchor */
}

/* puff interval for an active smoke lot, 0 = no smoke now */
static s32 smoke_interval(const Lot *l)
{
    const BuildingDef *d = def_building(l->def);
    int cap = d->cat == BCAT_ZONE ? d->capacity : d->jobs, act;
    s32 iv;
    act = cap > 0 ? (int)l->occupants * 100 / cap : 100;
    if (d->cat != BCAT_ZONE) act = IMAX(act, 40);       /* plants and saunas burn anyway */
    if (act < 10) return 0;
    act = IMIN(act, 100);
    iv = 380 * 100 / act;                               /* 0.38 s at full activity .. 3.8 s */
    if (d->w * d->h >= 4) iv = iv * 2 / 3;              /* big chimneys smoke more */
    else if (d->w * d->h == 1) iv = iv * 2;             /* a sauna stove: a thin wisp */
    if (l->def == B_COAL_STATION) iv = iv / 2;          /* 1.2 S1: two chimneys (smoke_emit alternates the hotspots) */
    else if (l->def == B_PEAT_PLANT) iv = iv * 3 / 2;   /* 1.2 S1: light smoke */
    return iv;
}

/* incremental scan of all lots for active smoke sources near the view; the list is swapped in per pass */
static void smoke_scan(void)
{
    int z = g_cam.zoom, n, margin = 2 * TILE_W(z);
    int vx0 = g_cam.x - margin, vy0 = g_cam.y - margin, vx1 = g_cam.x + g_cam.view_w + margin;
    int vy1 = g_cam.y + g_cam.view_h + margin + (256 >> z);   /* chimneys of lots below the view rise into it */
    if (s_scanZoom != z) { s_scanZoom = z; s_scanLot = 1; s_nscan = 0; s_nsrc = 0; }
    for (n = 0; n < SMOKE_SCAN_LOTS; n++) {
        const Lot *l;
        int tx, ty, wx, wy, i;
        s32 iv;
        if (s_scanLot >= g_world.lot_hi) {                    /* pass done: swap, keep timers */
            for (i = 0; i < s_nscan; i++) {
                int j;
                for (j = 0; j < s_nsrc; j++) if (s_src[j].lot == s_scan[i].lot) { s_scan[i].next_ms = s_src[j].next_ms; break; }
            }
            memcpy(s_src, s_scan, sizeof(SmokeSrc) * s_nscan);
            s_nsrc = s_nscan; s_nscan = 0; s_scanLot = 1;
            return;
        }
        l = &g_world.lots[s_scanLot];
        if (l->def && (def_building(l->def)->flags & BDF_SMOKE) && l->status == LOT_ACTIVE && s_nscan < SMOKE_SRC_MAX &&
            (iv = smoke_interval(l)) > 0) {
            lot_front_sv(s_scanLot, z, &tx, &ty, &wx, &wy);
            if (wx >= vx0 && wx < vx1 && wy >= vy0 && wy < vy1 && art_get(z, render_lot_art(s_scanLot))->hx >= 0) {
                s_scan[s_nscan].lot = (u16)s_scanLot;
                s_scan[s_nscan].interval = iv;
                s_scan[s_nscan].next_ms = s_smokeT + rng_below(&s_rng, (int)iv);
                s_nscan++;
            }
        }
        s_scanLot++;
    }
}

/* 1.2 S1: the second-chimney alias of a sprite id ("<name>_hot2"), looked up once per sprite (silent: art_exists) and
   cached; 0 = none. Only the coal power station asks (4 rotations). */
static int s_hot2_flip, s_h2_base[8], s_h2_id[8], s_h2_n;
static int hot2_of(int base)
{
    char nm[96];
    int k, id = 0;
    for (k = 0; k < s_h2_n; k++) if (s_h2_base[k] == base) return s_h2_id[k];
    str_printf(nm, sizeof(nm), "%s_hot2", art_name(base));
    if (art_exists(nm)) id = art_find(nm);
    if (s_h2_n < 8) { s_h2_base[s_h2_n] = base; s_h2_id[s_h2_n] = id; s_h2_n++; }
    return id;
}

static void smoke_emit(int lot)
{
    const Lot *l = world_lot(lot);
    const ArtSprite *sp;
    int z = g_cam.zoom, i, tx, ty, wx, wy;
    Puff *p;
    if (!l || l->status != LOT_ACTIVE) return;
    sp = art_get(z, render_lot_art(lot));
    if (sp->hx < 0) return;
    if (l->def == B_COAL_STATION && (s_hot2_flip ^= 1)) {   /* 1.2 S1: every other puff from the second chimney (A1's zero-cost
                                                               alias <sprite>_hot2: same pieces, hotspot on chimney two) */
        int h2 = hot2_of(render_lot_art(lot));
        if (h2 > 0) { const ArtSprite *s2 = art_get(z, h2); if (s2->hx >= 0) sp = s2; }
    }
    for (i = 0; i < LIFE_MAX_SMOKE && s_puffs[i].used; i++) {}
    if (i == LIFE_MAX_SMOKE) return;                    /* capped */
    p = &s_puffs[i];
    lot_front_sv(lot, z, &tx, &ty, &wx, &wy);
    p->used = 1; p->zoom = (u8)z;
    p->dtx = (u8)tx; p->dty = (u8)ty;
    p->bx = wx + sp->hx - sp->ax + rng_range(&s_rng, -1, 1);
    p->by = wy + sp->hy - sp->ay;
    p->age_ms = 0;
    p->life_ms = rng_range(&s_rng, 2300, 3100);
    if (def_building(l->def)->w * def_building(l->def)->h == 1) p->life_ms = p->life_ms * 2 / 3;   /* short column */
    p->wind = (s16)rng_range(&s_rng, 110, 170);          /* 7..10.6 px/s toward the right (NE wind) */
    p->phase = (s16)rng_below(&s_rng, 1024);
    s_npuffs++;
}

/* M5: thick smoke from burning lots: a puff from each burning tile's flames every ~300 ms (capped by the pool) */
static void fire_smoke(int dms)
{
    const CivicFire *f;
    int n = civic_fires(&f), i, j, z = g_cam.zoom;
    s_fireT += dms;
    if (!n || (s_fireSmokeT += dms) < 300) return;
    s_fireSmokeT = 0;
    for (i = 0; i < n; i++) {
        int x0, y0, x1, y1, wx, wy;
        Puff *p;
        if (!world_lot(f[i].lot)) continue;
        world_lot_rect(f[i].lot, &x0, &y0, &x1, &y1);
        iso_tile_to_world(z, x1, y1, &wx, &wy);
        for (j = 0; j < LIFE_MAX_SMOKE && s_puffs[j].used; j++) {}
        if (j == LIFE_MAX_SMOKE) return;
        p = &s_puffs[j];
        p->used = 1; p->zoom = (u8)z; p->dtx = (u8)x1; p->dty = (u8)y1;
        p->bx = wx + rng_range(&s_rng, -6, 6) / (1 + z);
        p->by = wy + TILE_H(z) / 2 - (40 >> z);
        p->age_ms = 0; p->life_ms = rng_range(&s_rng, 2600, 3400);
        p->wind = (s16)rng_range(&s_rng, 110, 170);
        p->phase = (s16)rng_below(&s_rng, 1024);
        s_npuffs++;
    }
}

static void smoke_update(int dms)
{
    int i;
    fire_smoke(dms);
    if (s_scanZoom != g_cam.zoom) {                     /* zoom change: puffs are in the old zoom's pixels */
        memset(s_puffs, 0, sizeof(s_puffs)); s_npuffs = 0;
    }
    smoke_scan();
    if (!dms) return;
    s_smokeT += dms;
    for (i = 0; i < LIFE_MAX_SMOKE; i++)
        if (s_puffs[i].used && (s_puffs[i].age_ms += dms) >= s_puffs[i].life_ms) { s_puffs[i].used = 0; s_npuffs--; }
    for (i = 0; i < s_nsrc; i++) {
        SmokeSrc *s = &s_src[i];
        if (s_smokeT < s->next_ms) continue;
        s->next_ms = s_smokeT + s->interval * rng_range(&s_rng, 80, 120) / 100;
        smoke_emit(s->lot);
    }
}

/* ------------------------------------------------------------------ update */
void life_update(double dt)
{
    double t0 = time_ms();
    int dms, frozen = sim_speed() == SPEED_PAUSE;
    if (!s_inited) return;
    if (dt < 0) dt = 0;
    if (dt > 0.1) dt = 0.1;
    /* water shimmer runs on the wall clock (also while paused); renderer re-renders water chunks in budget */
    s_animAcc += dt * 1000.0;
    if (s_animAcc >= SHIMMER_MS) {
        s_animAcc -= SHIMMER_MS;
        if (s_animAcc > SHIMMER_MS) s_animAcc = 0;
        s_animFrame = (s_animFrame + 1) % 3;
        render_set_anim_frame(s_animFrame);
    }
    s_msAcc += dt * 1000.0;
    dms = (int)s_msAcc;
    s_msAcc -= dms;
    if (frozen) dms = 0;
    if (s_carsOn) {
        LcView v;
        LcParams p;
        v.zoom = g_cam.zoom;
        v.vx0 = g_cam.x; v.vy0 = g_cam.y;
        v.vx1 = g_cam.x + g_cam.view_w; v.vy1 = g_cam.y + g_cam.view_h;
        v.margin = 2 * TILE_W(g_cam.zoom);
        p.year = s_stress ? 1972 : sim_date()->year;
        p.population = s_stress ? 100000 : sim_stats()->population;
        p.frozen = dms == 0;
        lc_update(dms, &v, &p);
        lb_update(dms, dms == 0);                                                 /* M2: buses on the lines */
    }
    if (s_smokeOn) smoke_update(dms);
    s_ls.update_ms = time_ms() - t0;
}

/* 1.2 X1: the motorway overpass over the railway (mway_rail_MM, X2) is a hump: 6 px at z64 (3 at z32) along the
   motorway, ramps over the first and last 7.5 of the tile's 32 units. Position in 1/256 tile (fx, fy): u along the
   motorway axis, 0..255; ramp = 60 u. */
static int overpass_lift(int z, s32 fx, s32 fy)
{
    int tx = (int)(fx >> 8), ty = (int)(fy >> 8), u, h;
    const Tile *t;
    if (!world_in(tx, ty)) return 0;
    t = WT(tx, ty);
    if (TILE_ROAD(t) != ROAD_MOTORWAY || !(t->flags & TF_RAIL)) return 0;
    u = (int)((world_rail_axis(tx, ty) == 5 ? fx : fy) & 255);       /* rail along Y: the motorway runs along X */
    if (u < 60) h = (u * 6 + 30) / 60;
    else if (u <= 196) h = 6;
    else h = ((256 - u) * 6 + 30) / 60;
    return z == ZOOM_64 ? h : (h + 1) / 2;
}

/* ------------------------------------------------------------------ draw */
void life_draw(void)
{
    double t0 = time_ms();
    int z = g_cam.zoom, i, n, wx, wy;
    int cx0 = g_cam.x - 24, cy0 = g_cam.y - 16, cx1 = g_cam.x + g_cam.view_w + 24, cy1 = g_cam.y + g_cam.view_h + 16;
    if (!s_inited) return;
    if (s_carsOn) {
        const LcCar *cars = lc_cars();
        const u8 *order;
        n = lc_draw_order(&order);
        for (i = 0; i < n; i++) {                       /* depth order: render's insertion sort stays cheap */
            const LcCar *c = &cars[order[i]];
            const Tile *t;
            if (!c->used) continue;
            iso_pos_to_world(z, c->fx, c->fy, &wx, &wy);
            if (wx < cx0 || wx >= cx1 || wy < cy0 || wy >= cy1) continue;
            t = WT(c->x, c->y);
            if (z == ZOOM_64 && TER_IS_WATER(TER_TYPE(t))) wy--;    /* bridge deck is 1 px above the road */
            wy -= overpass_lift(z, c->fx, c->fy);                     /* 1.2 X1 */
            render_sprite(s_carIds[c->col][c->dir & 3], wx, wy, c->x, c->y, 0);
        }
        {   /* M2: buses (lifebus.cpp) */
            const LbBus *b;
            int nb = lb_buses(&b);
            for (i = 0; i < nb; i++) {
                if (!b[i].used) continue;
                iso_pos_to_world(z, b[i].fx, b[i].fy, &wx, &wy);
                if (wx < cx0 || wx >= cx1 || wy < cy0 || wy >= cy1) continue;
                if (z == ZOOM_64 && TER_IS_WATER(TER_TYPE(WT(b[i].x, b[i].y)))) wy--;
                wy -= overpass_lift(z, b[i].fx, b[i].fy);             /* 1.2 X1 */
                render_sprite(s_busIds[b[i].dir & 3], wx, wy, b[i].x, b[i].y, 0);
            }
        }
    }
    {   /* M5: flames on burning lots: per footprint tile 2 offset copies of fire0..3, frame cycle ~130 ms, the
           stage (fire.cpp) picks how many tiles of a big lot burn */
        const CivicFire *f;
        int nf = civic_fires(&f), k, tx, ty;
        for (k = 0; k < nf; k++) {
            int x0, y0, x1, y1;
            if (!world_lot(f[k].lot)) continue;
            world_lot_rect(f[k].lot, &x0, &y0, &x1, &y1);
            for (ty = y0; ty <= y1; ty++)
                for (tx = x0; tx <= x1; tx++) {
                    int c, fr;
                    u32 hh = hash_xy(tx, ty, 0xF1AE);
                    if ((int)(hh % 4u) > f[k].stage + 1 && (tx != x1 || ty != y1)) continue;   /* small fires: front tile */
                    iso_tile_to_world(z, tx, ty, &wx, &wy);
                    wy += TILE_H(z) / 2;                    /* tile centre */
                    if (wx < cx0 || wx >= cx1 || wy < cy0 || wy >= cy1 + 48) continue;
                    for (c = 0; c < 2; c++) {
                        int ox = (c ? 7 : -6) >> z, oy = (c ? 3 : -2) >> z;
                        fr = (int)((s_fireT / 130 + (s32)(hh >> (8 + c * 4)) + c * 2) & 3);
                        render_sprite(s_fireIds[fr], wx + ox, wy + oy, x1, y1, 0);
                    }
                }
        }
    }
    if (s_smokeOn) {
        for (i = 0; i < LIFE_MAX_SMOKE; i++) {
            const Puff *p = &s_puffs[i];
            int t, f, ox, oy, wob;
            if (!p->used || p->zoom != z) continue;
            t = p->age_ms;                              /* ms */
            f = IMIN(5, t * 6 / p->life_ms);
            /* rise 24 px/s slowing down, drift right with the wind, a little sideways wobble (64-zoom px) */
            oy = (24 * t) / 1000 - (5 * (t / 10) * (t / 10)) / 20000;
            wob = ((t + p->phase) & 1023) < 512 ? ((t + p->phase) & 511) : 511 - ((t + p->phase) & 511);
            ox = (p->wind * t) / 16000 + (wob - 256) / 200;
            wx = p->bx + (ox >> z);
            wy = p->by - (oy >> z);
            if (wx < cx0 || wx >= cx1 || wy < cy0 || wy >= cy1 + 32) continue;
            render_sprite(s_smokeIds[f], wx, wy, p->dtx, p->dty, 0);
        }
    }
    s_ls.draw_ms = time_ms() - t0;
}

/* ------------------------------------------------------------------ stats */
void life_stats(LifeStats *out)
{
    LcStats st;
    lc_stats(&st);
    s_ls.cars = st.alive; s_ls.cars_target = st.target; s_ls.cars_visible = st.visible;
    s_ls.puffs = s_npuffs; s_ls.smoke_lots = s_nsrc;
    s_ls.spawned = (int)st.spawned; s_ls.despawned = (int)st.despawned;
    *out = s_ls;
}

int life_traffic_level(void)
{
    LcStats st;
    lc_stats(&st);
    return IMIN(100, st.visible * 100 / 60);
}

/* ------------------------------------------------------------------ -test life */
#define LB_MAX 9000
static int fcmp(const void *a, const void *b)
{
    float d = *(const float *)a - *(const float *)b;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

int life_selftest(void)
{
    const UiLayout *L;
    int sw, li, running = 1, fails = 0, secFrames = 0, nlb = 0, frames = 0, maxCars = 0, maxPuffs = 0;
    u32 lastSpawn = 0, lastDespawn = 0;
    double t0, now, prev, runT, nextSec = 1.0, benchT0 = -1, lastPresent;
    double secUpd = 0, secUpdMax = 0, secDraw = 0, secDrawMax = 0, secSpr = 0, secSprMax = 0;
    double sumUpd = 0, sumDraw = 0, sumSpr = 0, maxUpd = 0, maxDraw = 0, maxSpr = 0, sumCars = 0;
    float *iv = NULL;
    PIX bg;
    if (!gfx_screen_w()) return 1;
    if (!art_init()) return 1;
    world_init(); budget_init(); net_init(); fields_init(); traffic_init(); politics_init(); sim_init();
    render_build_test_town(g_cfg.seed);
    sw = gfx_screen_w();
    li = sw <= 640 ? 0 : sw <= 800 ? 1 : 2;
    L = &ui_layouts[li];
    g_cam.zoom = (g_cfg.start_zoom == 1) ? ZOOM_32 : ZOOM_64;
    cam_set_view(L->map.x, L->map.y, L->map.w, L->map.h);
    if (!render_init()) return 1;
    if (g_cfg.start_cam_x >= 0 && g_cfg.start_cam_y >= 0) cam_center_on_tile(g_cfg.start_cam_x, g_cfg.start_cam_y);
    else cam_center_on_tile(58, 64);
    sim_set_speed(g_cfg.start_speed >= 0 ? g_cfg.start_speed : SPEED_1);   /* the sim itself is not run */
    life_init();
    s_stress = g_cfg.bench > 0 || g_cfg.start_speed == 3;      /* perf runs: ~150 cars (flow forced to 150) */
    if (s_stress) lc_debug_force_flow(150);
    bg = gfx_rgb(UI_COL_BAR);
    if (g_cfg.bench) iv = (float *)calloc(LB_MAX, sizeof(float));
    log_printf("life: selftest: town ready (%d lots, pop %d, year %d, speed %d, zoom %d%s), entering the loop",
               g_world.lot_count, sim_stats()->population, sim_date()->year, sim_speed(), g_cam.zoom,
               s_stress ? ", STRESS: flow 150, 1972, pop cap off" : "");
    t0 = prev = lastPresent = plat_seconds();
    while (running) {
        const InputState *in;
        double dt, sprMs, b;
        LifeStats ls;
        RenderStats rs;
        prof_frame_begin();
        if (!plat_pump()) break;
        input_poll();
        in = input_get();
        now = plat_seconds(); runT = now - t0;
        dt = now - prev; prev = now;
        if (g_cfg.exit_after >= 0 && runT >= g_cfg.exit_after && !(g_cfg.shot_time >= 0 && runT < g_cfg.exit_after + 5)) break;
        if (key_hit(VK_ESCAPE)) break;
        if (key_hit(VK_PRIOR) || in->wheel > 0) render_set_zoom(ZOOM_64, in->mx, in->my);
        if (key_hit(VK_NEXT) || in->wheel < 0) render_set_zoom(ZOOM_32, in->mx, in->my);
        if (key_hit('C')) life_set_enabled(!s_carsOn, s_smokeOn);
        if (key_hit('P')) sim_set_speed(sim_speed() == SPEED_PAUSE ? SPEED_1 : SPEED_PAUSE);
        if (g_cfg.bench) {
            if (runT >= 2.0 && benchT0 < 0) benchT0 = now;
            render_bench_camera(runT);
        } else {
            int ex = 0, ey = 0;
            if (key_down(VK_LEFT)) ex--;
            if (key_down(VK_RIGHT)) ex++;
            if (key_down(VK_UP)) ey--;
            if (key_down(VK_DOWN)) ey++;
            cam_push(ex * 8, ey * 8);
            cam_update();
        }
        life_update(dt);
        gfx_fill(0, 0, sw, gfx_screen_h(), bg);
        render_update(3.0);
        render_world();
        life_draw();
        b = time_ms();
        render_flush_sprites();
        sprMs = time_ms() - b;
        if (g_cfg.shot_time >= 0 && runT >= g_cfg.shot_time) {
            gfx_save_bmp(data_path(g_cfg.shot_path)); g_cfg.shot_time = -1.0f;
            log_printf("life: screenshot %s at %.1f s (zoom %d, %d cars)", g_cfg.shot_path, runT, g_cam.zoom, lc_count());
        }
        plat_present();
        now = plat_seconds();
        if (iv && benchT0 >= 0 && nlb < LB_MAX) iv[nlb++] = (float)((now - lastPresent) * 1000.0);
        lastPresent = now;
        life_stats(&ls);
        render_stats(&rs);
        frames++; secFrames++;
        secUpd += ls.update_ms; if (ls.update_ms > secUpdMax) secUpdMax = ls.update_ms;
        secDraw += ls.draw_ms; if (ls.draw_ms > secDrawMax) secDrawMax = ls.draw_ms;
        secSpr += sprMs; if (sprMs > secSprMax) secSprMax = sprMs;
        if (runT >= 2.0) {
            sumUpd += ls.update_ms; sumDraw += ls.draw_ms; sumSpr += sprMs; sumCars += ls.cars;
            if (ls.update_ms > maxUpd) maxUpd = ls.update_ms;
            if (ls.draw_ms > maxDraw) maxDraw = ls.draw_ms;
            if (sprMs > maxSpr) maxSpr = sprMs;
        }
        if (ls.cars > maxCars) maxCars = ls.cars;
        if (ls.puffs > maxPuffs) maxPuffs = ls.puffs;
        if (runT >= nextSec) {
            LcStats cs;
            lc_stats(&cs);
            log_printf("life: t %4.1f fps %d zoom %d cars %d (target %d, visible %d, blocked %d) +%u -%u/s [offview %u life %u road %u edge %u excess %u] "
                       "puffs %d src %d | update %.3f/%.3f draw %.3f/%.3f flush %.3f/%.3f ms avg/max | sprites %d occl %d dropped %d",
                       runT, secFrames, g_cam.zoom, ls.cars, ls.cars_target, ls.cars_visible, cs.blocked,
                       cs.spawned - lastSpawn, cs.despawned - lastDespawn, cs.gone_offview, cs.gone_life, cs.gone_road, cs.gone_edge, cs.gone_excess,
                       ls.puffs, ls.smoke_lots, secUpd / secFrames, secUpdMax, secDraw / secFrames, secDrawMax, secSpr / secFrames, secSprMax,
                       rs.sprites, rs.occlusion_blits, rs.sprites_dropped);
            lastSpawn = cs.spawned; lastDespawn = cs.despawned;
            if (nextSec == 3.0) {                       /* smoke sources once (debug from text) */
                int k;
                for (k = 0; k < s_nsrc; k++) {
                    const Lot *l = &g_world.lots[s_src[k].lot];
                    log_printf("life: smoke source lot %d %s at %d,%d occupants %d interval %d ms", s_src[k].lot,
                               def_building(l->def)->key, l->x, l->y, l->occupants, (int)s_src[k].interval);
                }
            }
            secFrames = 0; secUpd = secUpdMax = secDraw = secDrawMax = secSpr = secSprMax = 0;
            nextSec += 1.0;
        }
        if (iv && benchT0 >= 0 && now - benchT0 >= g_cfg.bench) running = 0;
    }
    {
        int n;
        LcStats cs;
        lc_stats(&cs);
        n = IMAX(1, frames - 120);            /* averages over the frames after the 2 s fill-up (approx.) */
        log_printf("life: summary: %d frames, cars avg %.1f max %d, puffs max %d, spawned %u despawned %u, turns %u uturns %u, tiles %u, traffic_live %d",
                   frames, sumCars / n, maxCars, maxPuffs, cs.spawned, cs.despawned, cs.turns, cs.uturns, cs.tiles, cs.traffic_live);
        log_printf("life: summary ms (after 2 s): update avg %.3f max %.3f, draw (submit) avg %.3f max %.3f, update+draw avg %.3f; render_flush_sprites avg %.3f max %.3f",
                   sumUpd / n, maxUpd, sumDraw / n, maxDraw, (sumUpd + sumDraw) / n, sumSpr / n, maxSpr);
        if (iv && nlb > 0) {
            FILE *f = fopen(data_path("bench.txt"), "w");
            qsort(iv, nlb, sizeof(float), fcmp);
            if (f) {
                fprintf(f, GAME_TITLE_ASCII " life bench (T11, -test life, render_bench_camera path)\n");
                fprintf(f, "zoom %d, view %dx%d, frames %d, avg fps %.2f\n", g_cam.zoom, g_cam.view_w, g_cam.view_h, nlb, nlb / (double)g_cfg.bench);
                fprintf(f, "frame interval ms: median %.2f p95 %.2f p99 %.2f max %.2f\n", iv[nlb / 2], iv[nlb * 95 / 100], iv[nlb * 99 / 100], iv[nlb - 1]);
                fprintf(f, "cars avg %.1f max %d, puffs max %d\n", sumCars / n, maxCars, maxPuffs);
                fprintf(f, "life_update avg %.3f max %.3f ms; life_draw avg %.3f max %.3f ms; render_flush_sprites avg %.3f max %.3f ms\n",
                        sumUpd / n, maxUpd, sumDraw / n, maxDraw, sumSpr / n, maxSpr);
                fclose(f);
            }
        }
        if (maxCars == 0) { fails++; log_printf("life: FAIL no cars spawned"); }
        if (maxPuffs == 0) { fails++; log_printf("life: FAIL no smoke puffs"); }
    }
    if (iv) free(iv);
    s_stress = 0;
    lc_debug_force_flow(-1);
    render_shutdown();
    return fails;
}
