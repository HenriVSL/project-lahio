/* fire.cpp - M5 fires: ignition from the fire-risk field in wooden districts, spread to wooden neighbours, the
   fire brigade's cover puts them out, burnt lots leave rubble (TF_RUBBLE). PORTABLE, deterministic (own RNG
   stream, saved). Owner: M5. Rules and constants: civic.h. */
#include "common.h"
#include "civic.h"
#include "world.h"
#include "sim.h"
#include "net.h"
#include "fields.h"
#include "save.h"
#include "util.h"

#define RUBBLE_MAX 64

typedef struct { u8 x, y, w, h; u8 def; } FireExt;      /* footprint + def at ignition (detects a bulldozed lot) */
typedef struct { u8 x, y, w, h; u8 months; } Rubble;

static CivicFire s_f[CIVIC_FIRE_MAX];
static FireExt   s_fx[CIVIC_FIRE_MAX];
static int       s_nf;
static Rubble    s_rb[RUBBLE_MAX];
static int       s_nrb;
static Rng       s_rng;
static FireStats s_st;
static int       s_outbreak;                /* an outbreak is running (event at its end) */
static s32       s_ob_lost, s_ob_disp, s_ob_spread;   /* this outbreak: lots lost, residents out, spreads */
static int       s_ob_x, s_ob_y;

void fire_reset(void)
{
    const SimSettings *s = sim_settings();
    memset(s_f, 0, sizeof(s_f)); memset(s_fx, 0, sizeof(s_fx)); s_nf = 0;
    memset(s_rb, 0, sizeof(s_rb)); s_nrb = 0;
    memset(&s_st, 0, sizeof(s_st));
    rng_seed(&s_rng, (s ? s->seed : 1) ^ 0xF1AE5EEDu);
    s_outbreak = 0; s_ob_lost = s_ob_disp = s_ob_spread = 0; s_ob_x = s_ob_y = -1;
}

const FireStats *civic_fire_stats(void) { s_st.burning = s_nf; return &s_st; }
int civic_fires(const CivicFire **out) { *out = s_f; return s_nf; }

static int lot_burnable(const Lot *l)
{
    const BuildingDef *d = def_building(l->def);
    return l->def != B_NONE && d->cat == BCAT_ZONE && (d->flags & BDF_WOODEN) && l->status == LOT_ACTIVE;
}

static void rect_flags(int x0, int y0, int w, int h, int set, int clear)
{
    int x, y;
    for (y = y0; y < y0 + h; y++)
        for (x = x0; x < x0 + w; x++)
            if (world_in(x, y) && ((WT(x, y)->flags & clear) || (set & ~WT(x, y)->flags))) world_set_tile_flags(x, y, set, clear);
}

static int ignite(int lot, int spread)
{
    Lot *l = world_lot(lot);
    int x0, y0, x1, y1, k;
    if (!l || l->status == LOT_BURNING || s_nf >= CIVIC_FIRE_MAX) return 0;
    world_lot_rect(lot, &x0, &y0, &x1, &y1);
    k = s_nf++;
    s_f[k].lot = (u16)lot; s_f[k].x = (u8)x0; s_f[k].y = (u8)y0; s_f[k].age = 0; s_f[k].stage = 0;
    s_fx[k].x = (u8)x0; s_fx[k].y = (u8)y0; s_fx[k].w = (u8)(x1 - x0 + 1); s_fx[k].h = (u8)(y1 - y0 + 1); s_fx[k].def = l->def;
    s_st.displaced += l->occupants; s_ob_disp += l->occupants;
    l->occupants = 0; l->growth = 0;           /* the family is out on the street (the sim counts them homeless) */
    world_batch_begin();
    world_lot_set_status(lot, LOT_BURNING);
    rect_flags(x0, y0, x1 - x0 + 1, y1 - y0 + 1, TF_BURNING, 0);
    world_batch_end();
    if (spread) { s_st.spread++; s_ob_spread++; }
    else {
        s_st.started++;
        if (!s_outbreak) {
            s_outbreak = 1; s_ob_lost = 0; s_ob_disp = 0; s_ob_spread = 0; s_ob_x = x0; s_ob_y = y0;
            s_st.outbreaks_year++;
            sim_queue_event(SE_FIRE, SEV_URGENT, x0, y0, lot, 0);
        }
    }
    return 1;
}

int civic_fire_ignite(int lot)
{
    const Lot *l = world_lot(lot);
    if (!l || l->def == B_NONE || l->status != LOT_ACTIVE) return 0;
    return ignite(lot, 0);
}

static void drop_fire(int k)
{
    s_nf--;
    if (k < s_nf) { memmove(&s_f[k], &s_f[k + 1], sizeof(CivicFire) * (s_nf - k)); memmove(&s_fx[k], &s_fx[k + 1], sizeof(FireExt) * (s_nf - k)); }
}

static void add_rubble(int x, int y, int w, int h)
{
    if (s_nrb >= RUBBLE_MAX) {                 /* full: the oldest ruins are cleared now */
        rect_flags(s_rb[0].x, s_rb[0].y, s_rb[0].w, s_rb[0].h, 0, TF_RUBBLE);
        memmove(&s_rb[0], &s_rb[1], sizeof(Rubble) * (RUBBLE_MAX - 1));
        s_nrb--;
    }
    s_rb[s_nrb].x = (u8)x; s_rb[s_nrb].y = (u8)y; s_rb[s_nrb].w = (u8)w; s_rb[s_nrb].h = (u8)h; s_rb[s_nrb].months = 0;
    s_nrb++;
}

/* one "day" (every 4 steps): put out, spread, burn down */
void fire_tick(void)
{
    int k, n0 = s_nf, month = sim_date()->month, dry = (month >= 5 && month <= 7) ? 3 : 2;
    if (!s_nf) return;
    world_batch_begin();
    for (k = 0; k < n0 && k < s_nf; ) {
        CivicFire *f = &s_f[k];
        FireExt *e = &s_fx[k];
        Lot *l = world_lot(f->lot);
        int q, cx = e->x + e->w / 2, cy = e->y + e->h / 2;
        if (!l || l->def != e->def || l->x != e->x || l->y != e->y || l->status != LOT_BURNING) {   /* bulldozed */
            rect_flags(e->x, e->y, e->w, e->h, 0, TF_BURNING);
            drop_fire(k); n0--;
            continue;
        }
        q = net_cov_quality(SVC_FIRE, cx, cy);
        f->age++;
        f->stage = (u8)IMIN(3, f->age * 4 / (CIVIC_FIRE_BURN_TICKS + 1));
        if (rng_below(&s_rng, 100) < CIVIC_FIRE_BUCKETS_PCT + q * 60 / 100) {   /* neighbours' buckets, the brigade */
            world_lot_set_status(f->lot, LOT_ACTIVE);
            rect_flags(e->x, e->y, e->w, e->h, 0, TF_BURNING);
            s_st.put_out++;
            drop_fire(k); n0--;
            continue;
        }
        {   /* spread to wooden grown neighbours sharing an edge with the footprint */
            int x, y, j, nid = 0, ids[24], chance = CIVIC_FIRE_SPREAD_PCT * (100 - q) / 100 * dry / 2;
            for (y = e->y - 1; y <= e->y + e->h; y++)
                for (x = e->x - 1; x <= e->x + e->w; x++) {
                    int nl, in_x = x >= e->x && x < e->x + e->w, in_y = y >= e->y && y < e->y + e->h;
                    if (in_x == in_y) continue;            /* inside, or a corner */
                    if (!world_in(x, y) || !(nl = WT(x, y)->lot)) continue;
                    for (j = 0; j < nid && ids[j] != nl; j++) {}
                    if (j == nid && nid < 24) ids[nid++] = nl;       /* each neighbour lot once, in ring order */
                }
            for (j = 0; j < nid && s_ob_spread < CIVIC_FIRE_SPREAD_MAX; j++)   /* never more than a few houses */
                if (lot_burnable(&g_world.lots[ids[j]]) && rng_below(&s_rng, 100) < chance) ignite(ids[j], 1);
        }
        if (f->age >= CIVIC_FIRE_BURN_TICKS) {                          /* burnt down: rubble */
            int x0 = e->x, y0 = e->y, w = e->w, h = e->h;
            world_lot_remove(f->lot);
            rect_flags(x0, y0, w, h, TF_RUBBLE, TF_BURNING);
            add_rubble(x0, y0, w, h);
            s_st.lots_lost++; s_ob_lost++;
            drop_fire(k); n0--;
            continue;
        }
        k++;
    }
    world_batch_end();
    if (!s_nf && s_outbreak) {
        s_outbreak = 0;
        sim_queue_event(SE_FIRE, s_ob_lost ? SEV_WARN : SEV_INFO, s_ob_x, s_ob_y, s_ob_disp, 1 + s_ob_lost);
    }
}

/* a wooden lot outside the fire brigade's cover: its fire risk, else 0 */
static int ignite_weight(const Lot *l)
{
    if (!lot_burnable(l) || net_cov_quality(SVC_FIRE, l->x, l->y) >= CIVIC_FIRE_COVER_SAFE) return 0;
    return field_at(FLD_FIRE_RISK, l->x, l->y);
}

/* once per cycle: maybe a new fire (New Game "disasters" on), only outside fire cover */
void fire_cycle(void)
{
    s64 total = 0, p;
    int i;
    if (!sim_settings()->disasters) return;
    for (i = 1; i < g_world.lot_hi; i++) total += ignite_weight(&g_world.lots[i]);
    if (total <= 0) return;
    p = total * CIVIC_FIRE_IGNITE_PPB;
    if (p > 500000000) p = 500000000;
    if ((s64)(rng_next(&s_rng) % 1000000000u) >= p) return;
    {
        s64 v = (s64)(rng_next(&s_rng) % (u32)total);
        for (i = 1; i < g_world.lot_hi; i++) {
            int wgt = ignite_weight(&g_world.lots[i]);
            if (!wgt) continue;
            v -= wgt;
            if (v < 0) { ignite(i, 0); break; }
        }
    }
}

/* month end: ruins age; after CIVIC_RUBBLE_MONTHS the town clears them */
void fire_month(void)
{
    int k, x, y, n = 0;
    if (sim_date()->month == 11) s_st.outbreaks_year = 0;      /* the finished month was December */
    world_batch_begin();
    for (k = 0; k < s_nrb; ) {
        if (++s_rb[k].months >= CIVIC_RUBBLE_MONTHS) {
            rect_flags(s_rb[k].x, s_rb[k].y, s_rb[k].w, s_rb[k].h, 0, TF_RUBBLE);
            s_nrb--;
            if (k < s_nrb) memmove(&s_rb[k], &s_rb[k + 1], sizeof(Rubble) * (s_nrb - k));
            continue;
        }
        k++;
    }
    world_batch_end();
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) if (WT(x, y)->flags & TF_RUBBLE) n++;
    s_st.rubble_tiles = n;
}

u32 fire_hash(void)
{
    u32 h = HASH_INIT;
    h = hash_bytes(h, &s_rng, sizeof(s_rng));
    h = hash_bytes(h, &s_nf, sizeof(s_nf));
    h = hash_bytes(h, s_f, (int)sizeof(CivicFire) * s_nf);
    h = hash_bytes(h, &s_nrb, sizeof(s_nrb));
    h = hash_bytes(h, s_rb, (int)sizeof(Rubble) * s_nrb);
    return h;
}

void fire_save(struct SaveFile *sf)
{
    save_u32(sf, s_rng.s);
    save_s32(sf, s_nf);
    save_bytes(sf, s_f, (int)sizeof(CivicFire) * s_nf);
    save_bytes(sf, s_fx, (int)sizeof(FireExt) * s_nf);
    save_s32(sf, s_nrb);
    save_bytes(sf, s_rb, (int)sizeof(Rubble) * s_nrb);
    save_bytes(sf, &s_st, sizeof(s_st));
    save_s32(sf, s_outbreak); save_s32(sf, s_ob_lost); save_s32(sf, s_ob_disp); save_s32(sf, s_ob_x); save_s32(sf, s_ob_y);
    save_s32(sf, s_ob_spread);
}

void fire_load(struct SaveFile *sf, int ver)
{
    s32 v;
    UNUSED(ver);
    s_rng.s = load_u32(sf);
    v = load_s32(sf); s_nf = ICLAMP(v, 0, CIVIC_FIRE_MAX);     /* never a call inside ICLAMP (common.h) */
    load_bytes(sf, s_f, (int)sizeof(CivicFire) * s_nf);
    load_bytes(sf, s_fx, (int)sizeof(FireExt) * s_nf);
    v = load_s32(sf); s_nrb = ICLAMP(v, 0, RUBBLE_MAX);
    load_bytes(sf, s_rb, (int)sizeof(Rubble) * s_nrb);
    load_bytes(sf, &s_st, sizeof(s_st));
    s_outbreak = load_s32(sf); s_ob_lost = load_s32(sf); s_ob_disp = load_s32(sf); s_ob_x = load_s32(sf); s_ob_y = load_s32(sf);
    s_ob_spread = load_s32(sf);
}
