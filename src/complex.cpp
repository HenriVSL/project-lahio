/* complex.cpp - M6: the paper mill complex with its patruuna, Valtion Metalli, gifts, demand cards, the company-town
   rule and telegraphed risk events. PORTABLE, deterministic, integer-only. Owner: M6. See complex.h for the model.

   Determinism: the monthly update runs inside sim's month end (complex_month, after industry_month). Random draws
   use sim_rng() only while a complex exists (a town without one keeps the old stream and hash); the placement-time
   schedule (slump years, modernisation, merger) comes from a local Rng seeded by the site and the map seed, so a
   tool action never touches the sim stream. Player actions (placing, answering cards) happen between steps. */
#include "common.h"
#include "complex.h"
#include "world.h"
#include "sim.h"
#include "budget.h"
#include "industry.h"
#include "politics.h"
#include "transit.h"
#include "save.h"
#include "text.h"
#include "tools.h"
#include "util.h"

/* ------------------------------------------------------------------ tables */
typedef struct { u8 def, ox, oy; } Mod;
/* offsets from the north tile (ART.md 3.1), listed back to front (painter's order of the footprints' front tiles) */
static const Mod s_pm[] = { { B_PM_LOGYARD, 0, 0 }, { B_PM_CHIPPER, 0, 2 }, { B_PM_BOILER, 0, 3 }, { B_PM_HALL, 2, 0 },
                            { B_PM_DIGESTER, 2, 3 }, { B_PM_GATE, 4, 3 }, { B_PM_OFFICE, 4, 4 } };
static const Mod s_vm[] = { { B_VM_HALL, 0, 0 }, { B_VM_YARD, 3, 0 }, { B_VM_SIDING, 0, 3 }, { B_VM_STORE, 3, 2 },
                            { B_VM_GATE, 3, 3 } };
#define NPM ((int)COUNTOF(s_pm))
#define NVM ((int)COUNTOF(s_vm))

/* paper stages (INDUSTRY.md 7.2): jobs, wood, power, water, air pollution, West share of exports */
static const s16 s_pjobs[4] = { 0, 300, 600, 900 };
static const s16 s_pwood[4] = { 0, 40, 80, 120 };
static const s16 s_ppow[4]  = { 0, 40, 70, 100 };
static const s16 s_pwat[4]  = { 0, 30, 60, 90 };
static const s16 s_ppol[4]  = { 0, 250, 400, 500 };
static const s16 s_pwest[4] = { 80, 80, 75, 70 };
#define PAPER_VOL_PER100   150     /* export volume per 100 nominal jobs (engineering = 100) */
#define PAPER_MODERN_POL   350
#define PAPER_PERMIT_POL   50
/* Valtion Metalli product lines (INDUSTRY.md 7.4) */
#define VM_LINES           4
#define VM_LINE_JOBS       300
static const s16 s_vmyear[VM_LINES] = { 1955, 1960, 1965, 1970 };
static const char *const s_vmline[VM_LINES] = { "tractors", "paper machines", "rolling stock", "ship sections" };
#define VM_POW_LINE        15
#define VM_WAT_LINE        5
#define VM_POL_LINE        50
#define VM_EAST_PCT        85

/* gift sequences */
static const u8 s_gift_pm[] = { B_ROW_HOUSES, B_SAUNA, B_CHURCH, B_SCHOOL, B_PATRON_VILLA };
static const u8 s_gift_vm[] = { B_SCHOOL, B_ROW_HOUSES };
#define GIFT_MOOD      70
#define GIFT_CD_PM     24
#define GIFT_CD_VM     36
#define GIFT_BOOST     10

enum { CF_CLEAR_WARNED = 0x200, CF_CLEAR_SAID = 0x400, CF_UNCOMP_SAID = 0x800 };

/* ------------------------------------------------------------------ state */
static struct {
    Complex c[MAX_COMPLEX];
    CpxGift q[CPX_GIFT_QUEUE];
    int nq;
    CpxPlaced g[CPX_MAX_GIFTS];
    int ng;
    int vm_hook, vm_permit, vm_said;
    int company_town, dep_pct;
    s32 contrib, contrib_rate;
    int any;                       /* a complex was ever placed (hash / save content) */
} X;
static s16 s_use[CPXU_COUNT][B_COUNT];
#define MSG_RING 16
static CpxMsg s_msg[MSG_RING];
static int s_msg_head, s_msg_n;

static void push_msg(int type, int cpx, int a, int b)
{
    int idx;
    if (s_msg_n == MSG_RING) { s_msg_head = (s_msg_head + 1) % MSG_RING; s_msg_n--; }
    idx = (s_msg_head + s_msg_n) % MSG_RING;
    s_msg[idx].type = (u8)type; s_msg[idx].cpx = (u8)cpx; s_msg[idx].a = (s16)a; s_msg[idx].b = (s16)b;
    s_msg_n++;
}

int complex_poll_msg(CpxMsg *m)
{
    if (!s_msg_n) return 0;
    *m = s_msg[s_msg_head];
    s_msg_head = (s_msg_head + 1) % MSG_RING;
    s_msg_n--;
    return 1;
}

/* the month that begins after this month end (complex_month runs before the date advances) */
static int now_mi(void) { return sim_date()->year * 12 + sim_date()->month + 1; }
static int mi_year(int mi) { return mi / 12; }

/* ------------------------------------------------------------------ module table */
static const Mod *mods(int kind, int *n)
{
    if (kind == CPX_PAPER) { *n = NPM; return s_pm; }
    if (kind == CPX_METAL) { *n = NVM; return s_vm; }
    *n = 0; return NULL;
}

int complex_layout(int kind, int k, int *def, int *ox, int *oy)
{
    int n;
    const Mod *m = mods(kind, &n);
    if (!m || k < 0 || k >= n) return 0;
    *def = m[k].def; *ox = m[k].ox; *oy = m[k].oy;
    return 1;
}

void complex_size(int kind, int *w, int *h)
{
    *w = kind ? 5 : 0;
    *h = kind == CPX_PAPER ? 5 : kind == CPX_METAL ? 4 : 0;
}

int complex_kind_of_def(int def)
{
    if (def >= B_PM_LOGYARD && def <= B_PM_OFFICE) return CPX_PAPER;
    if (def >= B_VM_HALL && def <= B_VM_GATE) return CPX_METAL;
    return CPX_NONE;
}

static int hall_def(int kind) { return kind == CPX_PAPER ? B_PM_HALL : kind == CPX_METAL ? B_VM_HALL : B_NONE; }

const Complex *complex_get(int i) { return (i >= 0 && i < MAX_COMPLEX && X.c[i].kind) ? &X.c[i] : NULL; }

const Complex *complex_of_kind(int kind)
{
    int i;
    for (i = 0; i < MAX_COMPLEX; i++) if (X.c[i].kind == kind && kind) return &X.c[i];
    return NULL;
}

static Complex *of_kind(int kind) { return (Complex *)complex_of_kind(kind); }

static int lot_of_module(const Complex *c, int k)
{
    int def, ox, oy, id;
    const Lot *l;
    if (!complex_layout(c->kind, k, &def, &ox, &oy)) return 0;
    id = world_lot_at(c->x + ox, c->y + oy);
    l = world_lot(id);
    if (!l || l->def != def || l->x != c->x + ox || l->y != c->y + oy) return 0;
    return id;
}

static int hall_lot(const Complex *c)
{
    int n, k;
    const Mod *m = mods(c->kind, &n);
    for (k = 0; k < n; k++) if (m[k].def == hall_def(c->kind)) return lot_of_module(c, k);
    return 0;
}

int complex_at_lot(int lot)
{
    const Lot *l = world_lot(lot);
    int i, w, h;
    if (!l || !(g_bdefs[l->def].flags & BDF_MODULE)) return 0;
    for (i = 0; i < MAX_COMPLEX; i++) {
        const Complex *c = &X.c[i];
        if (!c->kind || complex_kind_of_def(l->def) != c->kind) continue;
        complex_size(c->kind, &w, &h);
        if (l->x >= c->x && l->y >= c->y && l->x < c->x + w && l->y < c->y + h) return i + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ names */
const char *complex_name(int kind)
{
    return kind == CPX_PAPER ? "Kuusikosken Paperi Oy" : kind == CPX_METAL ? "Valtion Metalli" : "?";
}

const char *complex_owner_name(int kind)
{
    return kind == CPX_PAPER ? "Patruuna Albin Hellstedt" : kind == CPX_METAL ? "The Ministry's director" : "?";
}

const char *complex_gift_name(int def, int kind)
{
    switch (def) {
    case B_ROW_HOUSES:   return kind == CPX_METAL ? "ARAVA row houses" : "workers' row houses";
    case B_SAUNA:        return "a company sauna";
    case B_CHURCH:       return "a company church";
    case B_SCHOOL:       return kind == CPX_METAL ? "a vocational school" : "a factory school";
    case B_PATRON_VILLA: return "the patron's villa";
    }
    return def_building(def)->name;
}

static int gift_paid(int def) { return def == B_SAUNA || def == B_SCHOOL; }

/* the owner is still around to give, demand and pay */
static int owner_present(const Complex *c)
{
    if (c->kind == CPX_PAPER) return !(c->flags & CF_MERGED);
    return c->kind == CPX_METAL;
}

/* pays for its gifts' upkeep */
static int owner_pays(const Complex *c)
{
    if (c->kind == CPX_PAPER) return !(c->flags & CF_MERGED) || ((c->flags & CF_HONOURED) && sim_date()->year < 1990);
    return c->kind == CPX_METAL;
}

/* ------------------------------------------------------------------ use tables (net, fields) */
s16 complex_module_use(int def, int what)
{
    if (def <= 0 || def >= B_COUNT || what < 0 || what >= CPXU_COUNT) return 0;
    return s_use[what][def];
}

static void set_use(int def, int pw, int wa, int pol)
{
    s_use[CPXU_POWER][def] = (s16)pw; s_use[CPXU_WATER][def] = (s16)wa; s_use[CPXU_POLLUTION][def] = (s16)pol;
}

/* this month's module values of one complex */
static void fill_use(const Complex *c)
{
    int pw = 0, wa = 0, pol = 0, out = ICLAMP(c->output, 0, 100);
    if (c->kind == CPX_PAPER) {
        if (c->flags & CF_ACTIVE) {
            pw = (c->flags & CF_RAPIDS) ? 0 : s_ppow[c->stage];
            wa = s_pwat[c->stage];
            pol = (c->stage == 3 && (c->flags & CF_MODERNISED)) ? PAPER_MODERN_POL : s_ppol[c->stage];
            if (c->flags & CF_PERMIT) pol += PAPER_PERMIT_POL;
            pol = pol * IMAX(30, out) / 100;
        }
        set_use(B_PM_HALL, pw, wa, pol / 2);              /* hall 50%, boiler 30%, digesters 20% of the smoke */
        set_use(B_PM_BOILER, 0, 0, pol * 3 / 10);
        set_use(B_PM_DIGESTER, 0, 0, pol - pol / 2 - pol * 3 / 10);
    } else if (c->kind == CPX_METAL) {
        if (c->flags & CF_ACTIVE) {
            pw = VM_POW_LINE * c->stage;
            wa = VM_WAT_LINE * c->stage;
            pol = VM_POL_LINE * c->stage * IMAX(30, out) / 100;
        }
        set_use(B_VM_HALL, pw, wa, pol * 6 / 10);
        set_use(B_VM_YARD, 0, 0, pol - pol * 6 / 10);
    }
}

/* recompute one complex's module values; returns 1 when power or water use changed (networks must re-solve) */
static int update_use(const Complex *c)
{
    int hd = hall_def(c->kind), hall, changed;
    s16 old_pw = s_use[CPXU_POWER][hd], old_wa = s_use[CPXU_WATER][hd];
    fill_use(c);
    changed = old_pw != s_use[CPXU_POWER][hd] || old_wa != s_use[CPXU_WATER][hd];
    if (changed && (hall = hall_lot(c)) != 0) {
        int x0, y0, x1, y1;
        world_lot_rect(hall, &x0, &y0, &x1, &y1);
        world_notify(x0, y0, x1, y1, WCHG_LOT);      /* sim marks the networks dirty */
    }
    return changed;
}

static void clear_use(int kind)
{
    int n, k;
    const Mod *m = mods(kind, &n);
    for (k = 0; k < n; k++) set_use(m[k].def, 0, 0, 0);
}

/* ------------------------------------------------------------------ lifecycle */
void complex_init(void)
{
    complex_reset();
}

void complex_reset(void)
{
    memset(&X, 0, sizeof(X));
    memset(s_use, 0, sizeof(s_use));
    s_msg_head = s_msg_n = 0;
    politics_set_event_hook(complex_on_event);
}

/* ------------------------------------------------------------------ hooks */
s32 complex_jobs(void)
{
    int i;
    s32 j = 0;
    for (i = 0; i < MAX_COMPLEX; i++) if (X.c[i].kind && (X.c[i].flags & CF_ACTIVE)) j += X.c[i].jobs;
    return j;
}

void complex_budget_base(s64 *bus, s32 *export_workers)
{
    int i, lot;
    for (i = 0; i < MAX_COMPLEX; i++) {
        const Complex *c = &X.c[i];
        if (!c->kind || !(c->flags & CF_ACTIVE)) continue;
        lot = hall_lot(c);
        /* like a placed building: half the zone rate (the company has its own arrangement with the tax office) */
        if (bus) *bus += (s64)c->employed * (50 + (lot ? WT(world_lot(lot)->x, world_lot(lot)->y)->lv : 0)) / 2;
        if (export_workers) *export_workers += c->vol;
    }
}

s32 complex_export_west(void)
{
    int i;
    s32 v = 0;
    for (i = 0; i < MAX_COMPLEX; i++) if (X.c[i].kind) v += X.c[i].west;
    return v;
}

s32 complex_export_east(void)
{
    int i;
    s32 v = 0;
    for (i = 0; i < MAX_COMPLEX; i++) if (X.c[i].kind) v += X.c[i].east;
    return v;
}

s32 complex_effluent(int *x, int *y)
{
    const Complex *c = complex_of_kind(CPX_PAPER);
    if (!c || !(c->flags & CF_ACTIVE)) return 0;
    if (x) *x = c->x - 1;
    if (y) *y = c->y + 2;
    return (30 * c->stage + ((c->flags & CF_PERMIT) ? 40 : 0)) * ICLAMP(c->output, 0, 100) / 100;
}

int complex_dependency_pct(void) { return X.dep_pct; }
int complex_company_town(void) { return X.company_town; }
s32 complex_contribution_last(void) { return X.contrib; }

int complex_risk_ahead(int *ev, int *months)
{
    int i;
    for (i = 0; i < MAX_COMPLEX; i++) {
        const Complex *c = &X.c[i];
        if (c->kind && c->ev_state == EVS_ANNOUNCED) {
            if (ev) *ev = c->ev;
            if (months) *months = IMAX(0, c->ev_start - now_mi() + 1);
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ placement */
static int water_side(int x, int y, int *rapids)
{
    int k, n = 0;
    *rapids = 0;
    for (k = 0; k < 5; k++) {
        const Tile *t = world_tile(x - 1, y + k);
        if (!world_in(x - 1, y + k) || !TER_IS_WATER(TER_TYPE(t))) continue;
        n++;
        if (TER_TYPE(t) == TER_RIVER && (TER_VAR(t) & RIVER_RAPIDS_BIT)) *rapids = 1;
    }
    return n;
}

static int rail_near(int x0, int y0, int x1, int y1, int d)
{
    int x, y;
    for (y = y0 - d; y <= y1 + d; y++)
        for (x = x0 - d; x <= x1 + d; x++)
            if (world_in(x, y) && TILE_ROAD(WT(x, y)) == ROAD_RAIL) return 1;
    return 0;
}

int complex_vm_permit(void)
{
    if (!politics_enabled()) return 1;
    return X.vm_permit || (X.vm_hook && politics_decree_cooldown(DEC_STATE_WORKS) > 0);
}

static int status_now(void) { return sim_stats()->town_status; }

int complex_tool_locked(int def, char *hint, int size)
{
    int kind = complex_kind_of_def(def), y = sim_date()->year, all = sim_settings()->all_unlocked;
    const BuildingDef *d = def_building(hall_def(kind));
    if (size > 0) hint[0] = 0;
    if (!kind) { str_copy(hint, "Later", size); return 1; }
    if (complex_of_kind(kind)) { str_copy(hint, "Built", size); return 1; }
    if (!def_unlocked(hall_def(kind), y, status_now(), all)) {
        text_unlock_hint(hint, size, d->status_req, d->era_year, y, status_now());
        if (!hint[0]) str_copy(hint, "Later", size);
        return 1;
    }
    if (kind == CPX_METAL && !all && !complex_vm_permit()) { str_copy(hint, "Needs a decree", size); return 1; }
    return 0;
}

Money complex_tool_cost(int def)
{
    int kind = complex_kind_of_def(def);
    if (kind == CPX_METAL && politics_enabled()) return 0;        /* the state builds it */
    return def_building(def)->cost;
}

int complex_check_place(int kind, int x, int y, Money *cost)
{
    int n, k, w, h, xx, yy, rapids, rr;
    const Mod *m = mods(kind, &n);
    Money c;
    char hint[32];
    if (cost) *cost = 0;
    if (!m) return REJ_LOCKED;
    if (complex_of_kind(kind)) return PLACE_UNIQUE;
    if (complex_tool_locked(hall_def(kind), hint, sizeof(hint))) return REJ_LOCKED;
    complex_size(kind, &w, &h);
    if (x < 0 || y < 0 || x + w > g_world.w || y + h > g_world.h) return PLACE_OUT_OF_MAP;
    c = complex_tool_cost(hall_def(kind));
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++) {
            const Tile *t = WT(xx, yy);
            if (TER_IS_WATER(TER_TYPE(t))) return PLACE_WATER;
            if (TILE_ROAD(t)) return PLACE_ROAD;
            if (t->flags & TF_PROTECTED) return PLACE_PROTECTED;
            if (t->lot || (t->road & TR_POWER_LINE)) return PLACE_OCCUPIED;
            if (TER_TYPE(t) == TER_FOREST) c += g_misc.clear_forest;
            else if (TER_TYPE(t) == TER_ROCK) c += g_misc.blast_rock;
        }
    for (k = 0; k < n; k++) {
        int r = world_lot_check(m[k].def, x + m[k].ox, y + m[k].oy, ROT_0);
        if (r != PLACE_OK) return r;
    }
    if (kind == CPX_PAPER && water_side(x, y, &rapids) < 2) return REJ_CPX_WATER;
    if (kind == CPX_METAL && !rail_near(x, y, x + w - 1, y + h - 1, 2)) return REJ_CPX_RAIL;
    if (!world_footprint_road_dist(x, y, x + w - 1, y + h - 1, 2, &rr)) return REJ_CPX_ROAD;
    if (cost) *cost = c;
    return PLACE_OK;
}

static void schedule_paper(Complex *c)
{
    static const s16 slump[4] = { 1958, 1967, 1971, 1976 };
    Rng r;
    int k, s;
    rng_seed(&r, hash_xy(c->x, c->y, sim_settings()->seed ^ 0x6d696c6cu));
    for (k = 0; k < 4; k++) {
        s = (slump[k] + rng_range(&r, -1, 1)) * 12 + rng_below(&r, 12);
        c->sched[k] = (s16)(s >= c->placed + CPX_BUILD_MONTHS + 3 ? s : 0);
    }
    s = (1972 + rng_below(&r, 7)) * 12 + rng_below(&r, 12);
    c->modern_at = (s16)(s >= c->placed + CPX_BUILD_MONTHS + 3 ? s : 0);
    s = (1980 + rng_below(&r, 9)) * 12 + rng_below(&r, 12);
    c->merger_at = (s16)(s >= c->placed + CPX_BUILD_MONTHS + 3 ? s : 0);
    if (c->placed + CPX_BUILD_MONTHS + 3 <= 1956 * 12 + 2) { c->strike_at = 1956 * 12 + 2; c->flags |= CF_STRUCK1956; }
}

int complex_place(int kind, int x, int y)
{
    int n, k, i, w, h, xx, yy, rapids = 0, slot = -1;
    const Mod *m = mods(kind, &n);
    Money cost;
    Complex *c;
    if (complex_check_place(kind, x, y, &cost) != PLACE_OK) return 0;
    for (i = 0; i < MAX_COMPLEX; i++) if (!X.c[i].kind) { slot = i; break; }
    if (slot < 0) return 0;
    complex_size(kind, &w, &h);
    world_batch_begin();
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++) {
            const Tile *t = WT(xx, yy);
            if (TER_TYPE(t) == TER_FOREST) world_set_terrain(xx, yy, TER_GRASS, TER_VAR(t) % 3);
            else if (TER_TYPE(t) == TER_ROCK) world_set_terrain(xx, yy, TER_GRASS, 0);
            if (TILE_ZONE(WT(xx, yy))) world_set_zone(xx, yy, ZONE_NONE);
        }
    for (k = 0; k < n; k++) {
        int id = world_lot_place(m[k].def, x + m[k].ox, y + m[k].oy, ROT_0, 0, LOT_BUILDING);
        Lot *l = world_lot(id);
        if (l) { l->build_left = CPX_BUILD_MONTHS; l->flags |= LF_PLAYER; }
    }
    world_batch_end();
    budget_spend(cost, BL_CONSTRUCTION);
    c = &X.c[slot];
    memset(c, 0, sizeof(*c));
    c->kind = (u8)kind; c->x = (u8)x; c->y = (u8)y;
    c->mood = c->mood_target = 60;
    c->agree = 100;
    c->ev_resp = 255;
    c->gift_cd = 12; c->demand_cd = 12;
    c->placed = (s16)(sim_date()->year * 12 + sim_date()->month);
    c->wood_pct = 100;
    if (kind == CPX_PAPER) {
        water_side(x, y, &rapids);
        if (rapids) c->flags |= CF_RAPIDS;
        schedule_paper(c);
    }
    if (kind == CPX_METAL) { X.vm_permit = 1; if (X.vm_hook) politics_enable_hook_decree(DEC_STATE_WORKS, 0); }
    X.any = 1;
    push_msg(CM_PLACED, slot, rapids, 0);
    log_printf("complex: %s placed at %d,%d (%d mk%s)", complex_name(kind), x, y, cost, rapids ? ", on the rapids" : "");
    return slot + 1;
}

/* ------------------------------------------------------------------ gifts */
int complex_gift_pending(void) { return X.nq; }
int complex_gift_def(int k) { return (k >= 0 && k < X.nq) ? X.q[k].def : B_NONE; }

const char *complex_gift_tool_name(void)
{
    static char buf[64];
    if (!X.nq) return "Patron's gift";
    str_printf(buf, sizeof(buf), "Gift: %s", complex_gift_name(X.q[0].def, X.c[X.q[0].cpx].kind));
    return buf;
}

int complex_check_gift(int x, int y, int rot)
{
    const Complex *c;
    int def, w, h, cw, ch, gap, xx, yy, rr, r;
    if (!X.nq) return REJ_CPX_NO_GIFT;
    def = X.q[0].def;
    c = complex_get(X.q[0].cpx);
    if (!c) return REJ_CPX_NO_GIFT;
    def_footprint(def, rot, &w, &h);
    r = world_lot_check(def, x, y, rot);
    if (r != PLACE_OK) return r;
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            if (WT(xx, yy)->road & TR_POWER_LINE) return PLACE_OCCUPIED;
    complex_size(c->kind, &cw, &ch);
    gap = IMAX(IMAX(c->x - (x + w - 1), x - (c->x + cw - 1)), IMAX(c->y - (y + h - 1), y - (c->y + ch - 1)));
    if (gap > CPX_GIFT_RADIUS) return REJ_CPX_FAR;
    if (!world_footprint_road_dist(x, y, x + w - 1, y + h - 1, 2, &rr)) return REJ_CPX_ROAD;
    return PLACE_OK;
}

int complex_place_gift(int x, int y, int rot)
{
    int def, cpx, w, h, xx, yy, id, k;
    Lot *l;
    if (complex_check_gift(x, y, rot) != PLACE_OK) return 0;
    def = X.q[0].def; cpx = X.q[0].cpx;
    def_footprint(def, rot, &w, &h);
    world_batch_begin();
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++) {
            const Tile *t = WT(xx, yy);
            if (TER_TYPE(t) == TER_FOREST) world_set_terrain(xx, yy, TER_GRASS, TER_VAR(t) % 3);
            else if (TER_TYPE(t) == TER_ROCK) world_set_terrain(xx, yy, TER_GRASS, 0);
            if (TILE_ZONE(WT(xx, yy))) world_set_zone(xx, yy, ZONE_NONE);
        }
    id = world_lot_place(def, x, y, rot, def_building(def)->variants ? (int)(hash_xy(x, y, (u32)def * 7919u) % def_building(def)->variants) : 0,
                         LOT_ACTIVE);
    world_batch_end();
    if (!id) return 0;
    l = world_lot(id);
    l->flags |= LF_PLAYER;
    for (k = 1; k < X.nq; k++) X.q[k - 1] = X.q[k];
    X.nq--;
    if (X.ng < CPX_MAX_GIFTS) {
        CpxPlaced *g = &X.g[X.ng++];
        g->def = (u8)def; g->cpx = (u8)cpx; g->x = (u8)x; g->y = (u8)y;
    }
    X.c[cpx].gift_boost = (s16)IMIN(30, X.c[cpx].gift_boost + GIFT_BOOST);
    push_msg(CM_GIFT_PLACED, cpx, def, 0);
    log_printf("complex: gift %s placed at %d,%d", def_building(def)->key, x, y);
    return id;
}

/* ------------------------------------------------------------------ tool (tools.cpp delegates) */
static void prev_tile(ToolPreview *p, int x, int y, int st, int reason)
{
    if (p->n < TOOL_MAX_PREVIEW && world_in(x, y)) {
        PreviewTile *t = &p->tiles[p->n++];
        t->x = (u8)x; t->y = (u8)y; t->state = (u8)st; t->reason = (u8)reason;
    }
    if (st == PT_REJECT) p->n_rejected++; else p->n_ok++;
}

static int gift_rot(int def, int x, int y, int rot)
{
    const BuildingDef *d = def_building(def);
    int w, h, rr;
    if (d->w != d->h) return rot;
    def_footprint(def, 0, &w, &h);
    return world_footprint_road_dist(x, y, x + w - 1, y + h - 1, 3, &rr) ? rr : rot;
}

void complex_plan(ToolPreview *p, int tool, int param, int cx, int cy, int rot)
{
    int x0, y0, w, h, r, xx, yy, def;
    Money cost = 0;
    memset(p, 0, (size_t)((char *)&p->tiles[0] - (char *)p));
    memset(&p->n_ok, 0, sizeof(*p) - (size_t)((char *)&p->n_ok - (char *)p));
    p->active = 1; p->tool = TOOL_BUILD;
    if (tool == TOOL_GIFT) {
        def = complex_gift_def(0);
        if (def == B_NONE) { p->param = B_NONE; p->main_reason = REJ_CPX_NO_GIFT; p->active = 0; return; }
        def_footprint(def, rot, &w, &h);
        x0 = cx - (w - 1) / 2; y0 = cy - (h - 1) / 2;
        rot = gift_rot(def, x0, y0, rot);
        def_footprint(def, rot, &w, &h);
        r = complex_check_gift(x0, y0, rot);
        p->rot = rot;
    } else {
        int kind = complex_kind_of_def(param);
        def = hall_def(kind);
        complex_size(kind, &w, &h);
        x0 = cx - (w - 1) / 2; y0 = cy - (h - 1) / 2;
        r = complex_check_place(kind, x0, y0, &cost);
        if (r != PLACE_OK) {   /* show the price anyway */
            cost = complex_tool_cost(def);
        }
        p->rot = 0;
        p->eff_jobs = kind == CPX_PAPER ? s_pjobs[1] : VM_LINE_JOBS;
        p->eff_power = kind == CPX_PAPER ? -s_ppow[1] : -VM_POW_LINE;
        p->eff_water = kind == CPX_PAPER ? -s_pwat[1] : -VM_WAT_LINE;
    }
    p->param = def; p->lot_def = def;
    p->lot_x = x0; p->lot_y = y0; p->lot_w = w; p->lot_h = h;
    p->x0 = x0; p->y0 = y0; p->x1 = x0 + w - 1; p->y1 = y0 + h - 1;
    for (yy = y0; yy < y0 + h; yy++)
        for (xx = x0; xx < x0 + w; xx++) {
            int st = PT_OK, rs = 0;
            if (!world_in(xx, yy)) { st = PT_REJECT; rs = PLACE_OUT_OF_MAP; }
            else {
                const Tile *t = WT(xx, yy);
                if (TER_IS_WATER(TER_TYPE(t))) { st = PT_REJECT; rs = PLACE_WATER; }
                else if (TILE_ROAD(t)) { st = PT_REJECT; rs = PLACE_ROAD; }
                else if (t->flags & TF_PROTECTED) { st = PT_REJECT; rs = PLACE_PROTECTED; }
                else if (t->lot || (t->road & TR_POWER_LINE)) { st = PT_REJECT; rs = PLACE_OCCUPIED; }
            }
            prev_tile(p, xx, yy, st, rs);
        }
    p->lot_ok = r == PLACE_OK;
    p->main_reason = r == PLACE_OK ? 0 : r;
    if (r != PLACE_OK && !p->n_rejected) p->n_rejected = 1;
    p->cost = cost;
    p->affordable = cost <= 0 || budget_can_afford(cost);
}

int complex_tool_update(const ToolInput *in, ToolPreview *p, int tool, int param, int rot,
                        int *lx, int *ly, Money *lcost, int *lreason)
{
    if (!in->on_map) { p->active = 0; return TRES_NONE; }
    complex_plan(p, tool, param, in->tx, in->ty, rot);
    if (!in->pressed) return TRES_NONE;
    if (!p->lot_ok) { *lreason = p->main_reason ? p->main_reason : REJ_NOTHING_TO_DO; return TRES_DENIED; }
    if (!p->affordable) { *lreason = REJ_NO_MONEY; return TRES_DENIED; }
    if (tool == TOOL_GIFT) {
        if (!complex_place_gift(p->lot_x, p->lot_y, p->rot)) { *lreason = REJ_NOTHING_TO_DO; return TRES_DENIED; }
        *lcost = 0;
    } else {
        if (!complex_place(complex_kind_of_def(param), p->lot_x, p->lot_y)) { *lreason = REJ_NOTHING_TO_DO; return TRES_DENIED; }
        *lcost = p->cost;
    }
    *lx = (p->x0 + p->x1) / 2; *ly = (p->y0 + p->y1) / 2;
    *lreason = 0;
    complex_plan(p, tool, param, in->tx, in->ty, rot);
    return TRES_COMMITTED;
}

/* ------------------------------------------------------------------ mood */
static void add_adj(Complex *c, int v, int months)
{
    int k, best = 0;
    for (k = 0; k < 4; k++) {
        if (!c->adj_m[k]) { best = k; break; }
        if (IABS(c->adj[k]) < IABS(c->adj[best])) best = k;
    }
    c->adj[best] = (s16)v; c->adj_m[best] = (s16)months;
}

static void term(Complex *c, int id, int v)
{
    if (v && c->nterms < (int)COUNTOF(c->terms)) { c->terms[c->nterms].id = (u8)id; c->terms[c->nterms].amount = v; c->nterms++; }
}

static void mood_step(Complex *c, int fill)
{
    const CityStats *st = sim_stats();
    int t = 50, v, k, svc = 0;
    static const u8 sv[3] = { SVC_SCHOOL, SVC_HEALTH, SVC_FIRE };
    c->nterms = 0;
    term(c, MT_BASE, 50);
    if (c->kind == CPX_PAPER) {
        v = g_budget.tax_bus <= TAX_NEUTRAL ? 10 : -10 * (g_budget.tax_bus - TAX_NEUTRAL);
        term(c, MT_TAX, v); t += v;
        v = c->wood_pct >= 100 ? 5 : c->wood_pct < 75 ? -10 : 0;
        term(c, MT_WOOD, v); t += v;
    } else if (politics_enabled()) {
        v = (politics_meter(METER_EAST) - 50) / 3;
        term(c, MT_EAST, v); t += v;
    }
    v = transit_stations() > 0 ? 10 : 0;
    term(c, MT_STATION, v); t += v;
    v = fill >= 95 ? 5 : fill < 80 ? -10 : 0;
    term(c, MT_STAFF, v); t += v;
    for (k = 0; k < 3; k++) if (st->svc_load[sv[k]] > 0 && st->svc_capacity[sv[k]] >= st->svc_load[sv[k]]) svc += 3;
    term(c, MT_SERVICES, svc); t += svc;
    v = (c->kind == CPX_PAPER && politics_decree_active(DEC_SMOKESTACK)) ? -20 : 0;
    term(c, MT_SMOKESTACK, v); t += v;
    term(c, MT_GIFTS, c->gift_boost); t += c->gift_boost;
    for (v = 0, k = 0; k < 4; k++) if (c->adj_m[k] > 0) v += c->adj[k];
    term(c, MT_ANSWERS, v); t += v;
    c->mood_target = (u8)ICLAMP(t, 0, 100);
    v = c->mood_target - c->mood;
    if (v) c->mood = (u8)ICLAMP(c->mood + (IABS(v) >= 3 ? v / 3 : (v > 0 ? 1 : -1)), 0, 100);
    for (k = 0; k < 4; k++) if (c->adj_m[k] > 0 && --c->adj_m[k] == 0) c->adj[k] = 0;
    if (c->gift_boost > 0 && (now_mi() & 1)) c->gift_boost--;
}

/* ------------------------------------------------------------------ risk events */
static int ev_card(int ev, int kind)
{
    switch (ev) {
    case EVK_SLUMP:     return EV_CPX_SLUMP;
    case EVK_STRIKE:    return kind == CPX_METAL ? EV_VM_STRIKE : EV_CPX_STRIKE;
    case EVK_VM_STRIKE: return EV_VM_STRIKE;
    case EVK_MODERN:    return EV_CPX_MODERN;
    case EVK_MERGER:    return EV_CPX_MERGER;
    }
    return -1;
}

static int resp(const Complex *c) { return c->ev_resp == 255 ? 0 : c->ev_resp; }

/* complex-side effect of an answer (the card's own PolFx are applied by politics) */
static void apply_answer_mood(Complex *c)
{
    int r = resp(c);
    if (c->ev == EVK_STRIKE) add_adj(c, r == 0 ? -10 : r == 1 ? -5 : -20, 24);
    else if (c->ev == EVK_VM_STRIKE) add_adj(c, r == 0 ? -10 : 0, 24);
}

static void ev_announce(Complex *c, int idx, int ev, int start)
{
    int card;
    c->ev = (u8)ev; c->ev_state = EVS_ANNOUNCED; c->ev_start = (s16)start; c->ev_resp = 255; c->ev_card = 0; c->ev_len = 0;
    card = ev_card(ev, c->kind);
    if (politics_enabled() && card >= 0 && politics_queue_event(card)) c->ev_card = 1;
    push_msg(CM_EV_ANNOUNCE, idx, ev, IMAX(1, start - now_mi()));
    log_printf("complex: %s: risk event %d announced for %d-%02d", complex_name(c->kind), ev, mi_year(start), start % 12 + 1);
}

static void ev_begin(Complex *c, int idx)
{
    int fresh = c->ev_resp == 255;
    if (fresh) c->ev_resp = 0;              /* nobody answered: the default */
    if (fresh) apply_answer_mood(c);
    switch (c->ev) {
    case EVK_SLUMP:
        c->ev_len = (s16)(resp(c) == 2 ? 8 : 12 + rng_below(sim_rng(), 7));
        break;
    case EVK_STRIKE: case EVK_VM_STRIKE:
        c->ev_len = 1;
        break;
    case EVK_MODERN:
        if (resp(c) == 0) c->flags |= CF_MODERNISED; else c->flags |= CF_REFUSED;
        push_msg(CM_MODERNISED, idx, resp(c), 0);
        c->ev = EVK_NONE; c->ev_state = EVS_NONE;
        return;
    case EVK_MERGER:
        c->flags |= CF_MERGED;
        if (resp(c) == 1) c->flags |= CF_HONOURED;
        push_msg(CM_MERGED, idx, resp(c), 0);
        c->ev = EVK_NONE; c->ev_state = EVS_NONE;
        return;
    }
    c->ev_state = EVS_RUNNING;
    push_msg(CM_EV_START, idx, c->ev, c->ev == EVK_SLUMP ? s_pjobs[c->stage] * (resp(c) == 1 ? 15 : 30) / 100 : 0);
}

static int strike_chance(void)
{
    const CityStats *st = sim_stats();
    if (politics_enabled()) {
        int a = politics_approval(BLOC_WORKERS);
        return a < 40 ? 30 : a < 50 ? 10 : 0;
    }
    return (st->workers > 0 && st->unemployed * 100 / st->workers > 8) ? 20 : 5;
}

static void ev_step(Complex *c, int idx)
{
    int mi = now_mi(), k, best = 0, bk = -1, ev = EVK_NONE;
    if (c->ev_state == EVS_RUNNING) {
        if (mi >= c->ev_start + c->ev_len) {
            push_msg(CM_EV_END, idx, c->ev, 0);
            c->ev = EVK_NONE; c->ev_state = EVS_NONE; c->ev_resp = 255;
        }
        return;
    }
    if (c->ev_state == EVS_ANNOUNCED) {
        int card = ev_card(c->ev, c->kind);
        if (!c->ev_card && politics_enabled() && card >= 0 && mi < c->ev_start && c->ev_resp == 255 && politics_queue_event(card)) c->ev_card = 1;
        if (mi >= c->ev_start) ev_begin(c, idx);
        return;
    }
    /* random strikes: decided each January from 1960, at most one in 5 years, announced 3 months ahead */
    if (mi % 12 == 0 && mi_year(mi) >= 1960 && !c->strike_at && mi >= c->strike_gate &&
        rng_below(sim_rng(), 100) < strike_chance())
        c->strike_at = (s16)(mi + 3);
    /* the next scheduled start */
    if (c->kind == CPX_PAPER) {
        for (k = 0; k < 4; k++) if (c->sched[k] && (bk < 0 || c->sched[k] < best)) { best = c->sched[k]; bk = k; ev = EVK_SLUMP; }
        if (c->modern_at && (bk < 0 || c->modern_at < best)) { best = c->modern_at; bk = 10; ev = EVK_MODERN; }
        if (c->merger_at && (bk < 0 || c->merger_at < best)) { best = c->merger_at; bk = 11; ev = EVK_MERGER; }
    }
    if (c->strike_at && (bk < 0 || c->strike_at < best)) { best = c->strike_at; bk = 12; ev = c->kind == CPX_METAL ? EVK_VM_STRIKE : EVK_STRIKE; }
    if (bk < 0 || mi < best - 3) return;
    if (bk >= 0 && bk < 4) c->sched[bk] = 0;
    else if (bk == 10) {
        if (c->stage < 3 || (c->flags & (CF_MODERNISED | CF_REFUSED))) {      /* only the big machine is modernised */
            c->modern_at = (s16)(mi_year(c->modern_at) < 1978 ? c->modern_at + 12 : 0);
            return;
        }
        c->modern_at = 0;
    } else if (bk == 11) {
        c->merger_at = 0;
        if (c->flags & CF_MERGED) return;
    } else if (bk == 12) {
        c->strike_at = 0;
        c->strike_gate = (s16)(best + 60);           /* no new random strike within 5 years */
    }
    if (!(c->flags & CF_ACTIVE)) return;    /* not producing yet: the event passes the town by */
    ev_announce(c, idx, ev, IMAX(best, mi + 1));
}

/* event factors on jobs / output (percent) */
static void ev_factors(const Complex *c, int *jobs_pct, int *out_pct, int *wood_pct)
{
    *jobs_pct = *out_pct = *wood_pct = 100;
    if (c->ev_state != EVS_RUNNING) return;
    switch (c->ev) {
    case EVK_SLUMP:
        *jobs_pct = resp(c) == 1 ? 85 : 70; *out_pct = 70; *wood_pct = 70;
        break;
    case EVK_STRIKE: case EVK_VM_STRIKE:
        *out_pct = resp(c) == 1 ? 50 : 0; *wood_pct = *out_pct;
        break;
    }
}

/* ------------------------------------------------------------------ cards (politics hook) */
void complex_on_event(int ev, int choice)
{
    Complex *c = of_kind(ev == EV_VM_QUOTA || ev == EV_VM_STRIKE ? CPX_METAL : CPX_PAPER);
    int want = EVK_NONE;
    if (ev < EV_CPX_TAX || ev > EV_VM_STRIKE || !c) return;
    switch (ev) {
    case EV_CPX_TAX:
        if (choice == 0) { budget_set_tax(g_budget.tax_res, IMAX(0, g_budget.tax_bus - 1)); add_adj(c, 15, 24); }
        else add_adj(c, -10, 12);
        break;
    case EV_CPX_PERMIT:
        if (choice == 0) { c->flags |= CF_PERMIT; add_adj(c, 10, 24); } else add_adj(c, -5, 12);
        break;
    case EV_CPX_UNION:
        add_adj(c, choice == 0 ? -10 : 5, 24);
        break;
    case EV_CPX_THREAT:
        if (choice == 0) { budget_set_tax(g_budget.tax_res, IMAX(0, g_budget.tax_bus - 1)); add_adj(c, 10, 24); }
        break;
    case EV_VM_QUOTA:
        if (choice == 0) { c->out_adj = 10; c->out_adj_m = 12; add_adj(c, 10, 12); } else add_adj(c, -5, 12);
        break;
    case EV_CPX_SLUMP:  want = EVK_SLUMP; break;
    case EV_CPX_STRIKE: want = EVK_STRIKE; break;
    case EV_CPX_MODERN: want = EVK_MODERN; break;
    case EV_CPX_MERGER: want = EVK_MERGER; break;
    case EV_VM_STRIKE:  want = EVK_VM_STRIKE; break;
    }
    if (want != EVK_NONE && c->ev == want && c->ev_state != EVS_NONE && c->ev_resp == 255) {
        c->ev_resp = (u8)choice;
        apply_answer_mood(c);
        if (c->ev == EVK_SLUMP && choice == 2 && c->ev_state == EVS_RUNNING) c->ev_len = (s16)IMIN(c->ev_len, 8);
    }
    log_printf("complex: card %d answered %d (mood %d)", ev, choice, c->mood);
}

static void demand_step(Complex *c, int idx)
{
    int cand[4], n = 0, ev;
    const CityStats *st = sim_stats();
    UNUSED(idx);
    if (c->demand_cd > 0) { c->demand_cd--; return; }
    if (!politics_enabled() || !owner_present(c) || politics_pending_event() >= 0) return;
    if (c->kind == CPX_PAPER) {
        if (g_budget.tax_bus >= TAX_NEUTRAL && g_budget.tax_bus > 3) cand[n++] = EV_CPX_TAX;
        if (!(c->flags & CF_PERMIT) && c->stage >= 2) cand[n++] = EV_CPX_PERMIT;
        if (st->population >= 2000 && politics_event_count(EV_CPX_UNION) < 2) cand[n++] = EV_CPX_UNION;
    } else cand[n++] = EV_VM_QUOTA;
    if (!n) { c->demand_cd = 6; return; }
    ev = cand[rng_below(sim_rng(), n)];
    if (politics_queue_event(ev)) c->demand_cd = (s16)((c->kind == CPX_METAL ? 24 : 12) + rng_below(sim_rng(), 13));
}

/* ------------------------------------------------------------------ the monthly update */
int complex_paper_stage_ok(int next, s32 pop, s32 wood_avail, int year, int station, int staffed, int mood, int threat)
{
    if (next == 2) return pop >= 3000 && wood_avail >= s_pwood[2] && staffed && mood > 30;
    if (next == 3) return year >= 1963 && pop >= 6000 && wood_avail >= s_pwood[3] && station && staffed && mood > 30 && !threat;
    return 0;
}

static int intact(const Complex *c)
{
    int n, k;
    mods(c->kind, &n);
    for (k = 0; k < n; k++) if (!lot_of_module(c, k)) return 0;
    return 1;
}

static void demolish(Complex *c, int idx)
{
    int k, def, ox, oy, id, kind = c->kind;
    for (k = 0; complex_layout(kind, k, &def, &ox, &oy); k++) {
        id = world_lot_at(c->x + ox, c->y + oy);
        if (id && world_lot(id)->def == def) world_lot_remove(id);
    }
    for (k = 0; k < X.nq; ) {
        if (X.q[k].cpx == idx) { int j; for (j = k + 1; j < X.nq; j++) X.q[j - 1] = X.q[j]; X.nq--; }
        else k++;
    }
    industry_set_ext_wood(idx, 0);
    clear_use(kind);
    push_msg(CM_DEMOLISHED, idx, kind, 0);
    log_printf("complex: %s demolished", complex_name(kind));
    memset(c, 0, sizeof(*c));
}

static void update(Complex *c, int idx)
{
    const CityStats *st = sim_stats();
    const IndWood *W = industry_wood();
    int mi = now_mi(), year = mi_year(mi), fill = st->jobs_fill_pct, hall, nominal = 0, jp, op, wp, out, wood_f;
    s32 vol_nom = 0, avail;
    Lot *hl;
    if (!intact(c)) { demolish(c, idx); return; }
    hall = hall_lot(c);
    hl = world_lot(hall);
    if (!(c->flags & CF_ACTIVE)) {
        if (!hl || hl->status != LOT_ACTIVE) { c->jobs = c->employed = 0; c->output = 0; c->vol = c->west = c->east = 0; return; }
        c->flags |= CF_ACTIVE; c->stage = 1;
        push_msg(CM_PRODUCTION, idx, 0, 0);
        log_printf("complex: %s starts production", complex_name(c->kind));
    }
    if (c->next) {
        if (c->build_left > 1) c->build_left--;
        else {
            c->stage = c->next; c->next = 0; c->build_left = 0;
            push_msg(CM_STAGE_DONE, idx, c->stage, 0);
            log_printf("complex: %s stage %d running", complex_name(c->kind), c->stage);
        }
    }
    ev_step(c, idx);
    ev_factors(c, &jp, &op, &wp);
    c->wood_pct = (s16)(W->need > 0 ? W->pct : 100);
    out = 100;
    if (c->kind == CPX_PAPER) {
        if ((c->flags & CF_REFUSED) && year >= 1985 && !(c->flags & CF_UNCOMP_SAID)) {
            c->flags |= CF_UNCOMP_SAID;
            push_msg(CM_UNCOMPETITIVE, idx, 0, 0);
        }
        nominal = s_pjobs[c->stage];
        if (c->stage == 3 && ((c->flags & CF_MODERNISED) || ((c->flags & CF_REFUSED) && year >= 1985))) nominal = s_pjobs[2];
        vol_nom = (s32)s_pjobs[c->stage] * PAPER_VOL_PER100 / 100;
        wood_f = ICLAMP(c->wood_pct + 25, 50, 100);
        out = out * IMIN(100, c->wood_pct) / 100;
        if (c->flags & CF_PERMIT) out = out * 110 / 100;
        if (c->flags & CF_REFUSED) out = out * (year >= 1985 ? 70 : 80) / 100;
        {   /* "our own machine": Valtion Metalli's paper machine line (1960) */
            const Complex *vm = complex_of_kind(CPX_METAL);
            if (vm && (vm->flags & CF_ACTIVE) && vm->stage >= 2) out = out * 110 / 100;
        }
        c->wood_need = (s16)(s_pwood[c->stage] * wp / 100);
        c->west = 0;
    } else {
        if (mi % 12 == 0 && mi_year(mi) >= 1956 && (mi_year(mi) - 1956) % 5 == 0) {     /* 5-year agreement */
            int e = politics_enabled() ? politics_meter(METER_EAST) : 50;
            c->agree = (s16)(e >= 60 ? 125 : e <= 40 ? 75 : 100);
            push_msg(CM_AGREEMENT, idx, c->agree, mi_year(mi));
        }
        if (mi == 1989 * 12 + 10 && !(c->flags & CF_CLEAR_WARNED)) { c->flags |= CF_CLEAR_WARNED; push_msg(CM_CLEARING_END, idx, 0, 0); }
        if (year >= 1990 && !(c->flags & CF_CLEAR_SAID)) { c->flags |= CF_CLEAR_SAID; push_msg(CM_CLEARING_END, idx, 1, 0); }
        nominal = VM_LINE_JOBS * c->stage;
        vol_nom = nominal;
        wood_f = 100;
        out = out * c->agree / 100;
        if (c->out_adj_m > 0) { out = out * (100 + c->out_adj) / 100; if (--c->out_adj_m == 0) c->out_adj = 0; }
        if (year >= 1990) { out /= 2; nominal /= 2; }
        c->wood_need = 0;
    }
    out = out * op / 100;
    out = out * IMIN(100, fill) / 100;
    if (hl && !(hl->flags & LF_WATERED)) out = 0;
    if (hl && !(c->flags & CF_RAPIDS) && !(hl->flags & LF_POWERED)) out = 0;
    c->output = (s16)ICLAMP(out, 0, 150);
    c->jobs = (s16)(nominal * jp / 100 * wood_f / 100);
    c->employed = (s16)(c->jobs * IMIN(100, fill) / 100);
    if (hl) hl->occupants = (u16)c->employed;
    c->vol = vol_nom * c->output / 100;
    if (c->kind == CPX_PAPER) c->west = c->vol * s_pwest[c->stage] / 100;
    else c->west = c->vol * (year >= 1990 ? 50 : 100 - VM_EAST_PCT) / 100;
    c->east = c->vol - c->west;
    industry_set_ext_wood(idx, c->wood_need);
    update_use(c);
    /* owner */
    if (owner_present(c)) mood_step(c, fill);
    /* stages */
    if (fill >= 90 && c->jobs > 0) c->staffed++; else c->staffed = 0;
    if (c->staffed > 120) c->staffed = 120;
    avail = W->supply - (W->need - c->wood_need);
    if (c->kind == CPX_PAPER) {
        if (c->stage == 2 && c->mood <= 30 && !(c->flags & CF_THREAT) && owner_present(c)) {
            c->flags |= CF_THREAT;
            push_msg(CM_THREAT, idx, c->mood, 0);
            if (politics_enabled() && politics_pending_event() < 0) politics_queue_event(EV_CPX_THREAT);
        }
        if ((c->flags & CF_THREAT) && c->mood >= 40) c->flags &= ~CF_THREAT;
        if (!c->next && c->stage < 3 && (c->ev_state != EVS_RUNNING) &&
            complex_paper_stage_ok(c->stage + 1, st->population, avail, year, transit_stations() > 0, c->staffed >= 6, c->mood,
                                   (c->flags & CF_THREAT) != 0)) {
            c->next = (u8)(c->stage + 1); c->build_left = CPX_BUILD_MONTHS;
            push_msg(CM_STAGE_START, idx, c->next, 0);
        }
    } else if (!c->next && c->stage < VM_LINES && year >= s_vmyear[c->stage] && year < 1990 && c->staffed >= 6 &&
               c->mood > 30 && c->ev_state != EVS_RUNNING) {
        c->next = (u8)(c->stage + 1); c->build_left = CPX_BUILD_MONTHS;
        push_msg(CM_STAGE_START, idx, c->next, 0);
    }
    /* gifts */
    if (c->gift_cd > 0) c->gift_cd--;
    if (owner_present(c) && c->mood >= GIFT_MOOD && !c->gift_cd && X.nq < CPX_GIFT_QUEUE) {
        int def = c->kind == CPX_PAPER ? (c->gifts < (int)COUNTOF(s_gift_pm) ? s_gift_pm[c->gifts] : B_ROW_HOUSES)
                                       : (c->gifts < (int)COUNTOF(s_gift_vm) ? s_gift_vm[c->gifts] : B_ROW_HOUSES);
        X.q[X.nq].def = (u8)def; X.q[X.nq].cpx = (u8)idx; X.nq++;
        if (c->gifts < 255) c->gifts++;
        c->gift_cd = c->kind == CPX_PAPER ? GIFT_CD_PM : GIFT_CD_VM;
        push_msg(CM_GIFT_OFFER, idx, def, 0);
        log_printf("complex: %s offers %s (mood %d)", complex_name(c->kind), def_building(def)->key, c->mood);
    }
    demand_step(c, idx);
}

static Money gift_upkeep(void)
{
    int k, id;
    s64 m = 0;
    for (k = 0; k < X.ng; ) {
        const CpxPlaced *g = &X.g[k];
        const Lot *l;
        id = world_lot_at(g->x, g->y);
        l = world_lot(id);
        if (!l || l->def != g->def || l->x != g->x || l->y != g->y) {      /* bulldozed: forget it */
            int j;
            for (j = k + 1; j < X.ng; j++) X.g[j - 1] = X.g[j];
            X.ng--;
            continue;
        }
        if (gift_paid(g->def) && l->status == LOT_ACTIVE && X.c[g->cpx].kind && owner_pays(&X.c[g->cpx])) {
            const BuildingDef *d = def_building(g->def);
            m += (s64)d->upkeep * (d->service != SVC_NONE ? g_budget.funding[d->service] : 100) / 100;
        }
        k++;
    }
    return (Money)(m * budget_upkeep_index() / 100);
}

void complex_month(void)
{
    const CityStats *st = sim_stats();
    int i, rate = 0, ct, mi = now_mi();
    s32 cj = 0;
    Money svc, contrib;
    /* Valtion Metalli unlock (DESIGN 14.3): a hidden decree with politics, a Market-town unlock without */
    if (!complex_of_kind(CPX_METAL) && (st->town_status >= TS_MARKET_TOWN || sim_settings()->all_unlocked)) {
        if (politics_enabled()) {
            if (!X.vm_hook && !X.vm_permit) { X.vm_hook = 1; politics_enable_hook_decree(DEC_STATE_WORKS, 1); push_msg(CM_VM_DECREE, 0, 0, 0); }
            if (X.vm_hook && !X.vm_permit && politics_decree_cooldown(DEC_STATE_WORKS) > 0) {
                X.vm_permit = 1;
                politics_enable_hook_decree(DEC_STATE_WORKS, 0);
                push_msg(CM_VM_PERMIT, 0, 0, 0);
            }
        } else if (!X.vm_said) { X.vm_said = 1; push_msg(CM_VM_UNLOCK, 0, 0, 0); }
    }
    UNUSED(mi);
    if (!X.any) return;
    for (i = 0; i < MAX_COMPLEX; i++) if (X.c[i].kind) update(&X.c[i], i);
    /* the company-town rule */
    for (i = 0; i < MAX_COMPLEX; i++) {
        const Complex *c = &X.c[i];
        if (!c->kind || !(c->flags & CF_ACTIVE)) continue;
        cj += c->jobs;
        if (c->kind == CPX_PAPER) rate = IMAX(rate, !(c->flags & CF_MERGED) ? 20 : ((c->flags & CF_HONOURED) && sim_date()->year < 1990) ? 10 : 0);
        else rate = IMAX(rate, 10);
    }
    X.dep_pct = st->jobs > 0 ? (int)((s64)cj * 100 / st->jobs) : 0;
    ct = X.dep_pct >= CPX_DEP_PCT && rate > 0;
    if (ct != X.company_town) push_msg(CM_COMPANY_TOWN, 0, X.dep_pct, ct);
    X.company_town = ct;
    X.contrib_rate = ct ? rate : 0;
    svc = -(g_budget.last[BL_SVC(SVC_SCHOOL)] + g_budget.last[BL_SVC(SVC_HEALTH)] + g_budget.last[BL_SVC(SVC_FIRE)]);
    contrib = ct ? (Money)((s64)IMAX(0, svc) * rate / 100) : 0;
    contrib += gift_upkeep();
    X.contrib = contrib;
    if (contrib > 0) budget_spend(-contrib, BL_COMPANY);
}

/* ------------------------------------------------------------------ messages */
int complex_msg_text(char *buf, int size, const CpxMsg *m, int *severity, int *x, int *y)
{
    const Complex *c = complex_get(m->cpx);
    int kind = c ? c->kind : (m->type == CM_DEMOLISHED ? m->a : CPX_PAPER), sev = SEV_INFO, w, h;
    const char *nm = complex_name(kind);
    const char *desk = politics_enabled() ? " A card waits on the President's desk." : "";
    *x = *y = -1;
    if (c) { complex_size(c->kind, &w, &h); *x = c->x + w / 2; *y = c->y + h / 2; }
    buf[0] = 0;
    switch (m->type) {
    case CM_PLACED:
        if (kind == CPX_METAL) str_printf(buf, size, "Valtion Metalli breaks ground by the railway. The Ministry promises tractors within six months.");
        else str_printf(buf, size, "%s breaks ground by the water%s. Production starts in six months.", nm, m->a ? ", on the rapids: its own turbines will power it" : "");
        sev = SEV_POLITICAL; break;
    case CM_PRODUCTION:
        if (kind == CPX_METAL) str_copy(buf, "The first VM Kontio 35 tractor rolls out of the Valtion Metalli works. 300 jobs.", size);
        else str_printf(buf, size, "The first reel leaves PM1 at %s: 300 jobs. Residents call the smell 'the smell of money'.", nm);
        sev = SEV_POLITICAL; break;
    case CM_STAGE_START:
        if (kind == CPX_METAL) str_printf(buf, size, "Valtion Metalli starts a new line: %s. 300 more jobs in six months.", s_vmline[ICLAMP(m->a - 1, 0, VM_LINES - 1)]);
        else if (m->a == 2) str_printf(buf, size, "%s builds a pulp mill and PM2: 300 more jobs in six months.", nm);
        else str_printf(buf, size, "Work begins on PM3, 'the big machine': 300 more jobs in six months.");
        break;
    case CM_STAGE_DONE:
        if (kind == CPX_METAL) str_printf(buf, size, "The %s line is running at Valtion Metalli.", s_vmline[ICLAMP(m->a - 1, 0, VM_LINES - 1)]);
        else if (m->a == 2) str_printf(buf, size, "PM2 is running at %s. The mill now employs 600.", nm);
        else str_printf(buf, size, "PM3, the big machine, is running. The town hears it at night and sleeps better for it.");
        sev = SEV_POLITICAL; break;
    case CM_GIFT_OFFER:
        if (kind == CPX_METAL) str_printf(buf, size, "The Ministry grants the town %s. Place it near the works: Industry > Gift.", complex_gift_name(m->a, kind));
        else str_printf(buf, size, "Patruuna Hellstedt offers the town %s. Place it near the mill: Industry > Gift.", complex_gift_name(m->a, kind));
        sev = SEV_POLITICAL; break;
    case CM_GIFT_PLACED:
        str_printf(buf, size, "%s%s opens with a speech by %s. The speech is longer than the ribbon.",
                   m->a == B_PATRON_VILLA ? "The patron's villa" : "The gift: ", m->a == B_PATRON_VILLA ? "" : complex_gift_name(m->a, kind),
                   kind == CPX_METAL ? "the director" : "the patron");
        break;
    case CM_COMPANY_TOWN:
        if (m->b) str_printf(buf, size, "Company town: %d%% of the jobs are at the works. The company now pays part of the school, clinic and fire upkeep.", m->a);
        else str_printf(buf, size, "No longer a company town: %d%% of the jobs are at the works.", m->a);
        sev = SEV_POLITICAL; break;
    case CM_EV_ANNOUNCE:
        sev = SEV_WARN;
        switch (m->a) {
        case EVK_SLUMP:  str_printf(buf, size, "Paper prices are falling in London. The mill goes on short shifts in %d months.%s", m->b, desk); break;
        case EVK_STRIKE:
            if (sim_date()->year <= 1956) str_printf(buf, size, "The unions call a general strike for March. The mill will stand still.%s", desk);
            else str_printf(buf, size, "Wage talks have failed. A strike is called at the mill in %d months.%s", m->b, desk);
            break;
        case EVK_MODERN: str_printf(buf, size, "%s plans a new paper machine with 300 fewer men. The decision falls in %d months.%s", nm, m->b, desk); break;
        case EVK_MERGER: str_printf(buf, size, "The patron's family is said to be selling the mill to a Helsinki group. Papers in %d months.%s", m->b, desk); break;
        case EVK_VM_STRIKE: str_printf(buf, size, "The metal workers at Valtion Metalli will strike in %d months.%s", m->b, desk); break;
        }
        break;
    case CM_EV_START:
        sev = SEV_WARN;
        if (m->a == EVK_SLUMP) str_printf(buf, size, "Short shifts begin at the mill: %d jobs fewer until London buys again.", m->b);
        else if (m->a == EVK_VM_STRIKE) str_copy(buf, "Valtion Metalli is on strike. Deliveries to the East stop.", size);
        else str_copy(buf, "The strike begins. The mill is silent; the patron is not.", size);
        break;
    case CM_EV_END:
        if (m->a == EVK_SLUMP) str_copy(buf, "London buys paper again. Full shifts return to the mill.", size);
        else str_copy(buf, "The strike is over. The machines start again.", size);
        break;
    case CM_THREAT:
        str_printf(buf, size, "The board may put PM3 in another town: relations with the patron are chilly (mood %d).%s", m->a, desk);
        sev = SEV_WARN; break;
    case CM_VM_DECREE:
        str_copy(buf, "The Ministry seeks a site for a state metal works. A decree would bring Valtion Metalli here: President's desk > Decrees.", size);
        sev = SEV_POLITICAL; break;
    case CM_VM_PERMIT:
        str_copy(buf, "Decree signed: Valtion Metalli builds its works here at the state's expense. Pick a site by the railway: Industry > Valtion Metalli.", size);
        sev = SEV_POLITICAL; break;
    case CM_VM_UNLOCK:
        str_copy(buf, "As a market town we may now host a state metal works: Industry > Valtion Metalli (by the railway).", size);
        break;
    case CM_AGREEMENT:
        str_printf(buf, size, "The %d trade agreement with the East: Valtion Metalli's deliveries %s.", m->b,
                   m->a > 100 ? "rise by a quarter" : m->a < 100 ? "fall by a quarter" : "stay as they were");
        sev = SEV_POLITICAL; break;
    case CM_CLEARING_END:
        if (!m->a) str_copy(buf, "The clearing trade with the East ends in January. Valtion Metalli expects to halve its output.", size);
        else str_copy(buf, "The clearing trade has ended. Valtion Metalli halves its output and its jobs. The newsreel calls it the end of an era.", size);
        sev = SEV_WARN; break;
    case CM_UNCOMPETITIVE:
        str_printf(buf, size, "The old machines at %s can no longer compete. PM1 is shut down: 300 jobs go.", nm);
        sev = SEV_WARN; break;
    case CM_DEMOLISHED:
        str_printf(buf, size, "%s is demolished. The smell lingers for a while.", nm);
        sev = SEV_WARN; break;
    case CM_MERGED:
        if (m->a) str_copy(buf, "The family has sold the mill. The new Helsinki owners will honour the patron's promises until 1990, in moderation.", size);
        else str_copy(buf, "The family has sold the mill to a Helsinki group. The patron leaves; the gifts and the company contribution end.", size);
        sev = SEV_POLITICAL; break;
    case CM_MODERNISED:
        if (!m->a) str_copy(buf, "The new paper machine is installed: 300 fewer jobs, the same paper, less smoke.", size);
        else str_copy(buf, "The mill keeps its old machines and its men. Output falls by a fifth.", size);
        break;
    default:
        return 0;
    }
    if (severity) *severity = sev;
    return buf[0] != 0;
}

/* ------------------------------------------------------------------ query panel */
static const char *mood_word(int m)
{
    return m >= 80 ? "delighted" : m >= 70 ? "generous" : m >= 50 ? "content" : m >= 31 ? "cool" : "chilly";
}

static const char *mt_label(int id)
{
    static const char *const n[MT_COUNT] = { "base", "tax", "station", "wood", "staff", "services", "smokestacks",
                                             "gifts", "answers", "East" };
    return (id >= 0 && id < MT_COUNT) ? n[id] : "?";
}

int complex_lot_line(int lot, int k, char *buf, int size, int *font)
{
    int ci = complex_at_lot(lot), year = sim_date()->year, line = 0, n;
    const Complex *c;
    const IndWood *W = industry_wood();
    char a[32], b[32];
    if (!ci) return 0;
    c = &X.c[ci - 1];
    *font = 0;
    buf[0] = 0;
#define LINE(f) if (line++ == k) { *font = (f);
#define END return 1; }
    LINE(4)
        if (!(c->flags & CF_ACTIVE)) str_printf(buf, size, "%s: under construction", complex_name(c->kind));
        else if (c->kind == CPX_PAPER) str_printf(buf, size, "%s: stage %d of 3%s", complex_name(c->kind), c->stage,
                                                  c->stage == 1 ? " (groundwood mill + PM1)" : c->stage == 2 ? " (pulp mill + PM2)" : " (PM3)");
        else str_printf(buf, size, "%s: %d of %d product lines", complex_name(c->kind), c->stage, VM_LINES);
    END
    if (!(c->flags & CF_ACTIVE)) {
        int hall = hall_lot(c);
        LINE(1) str_printf(buf, size, "Production starts in %d months", hall ? IMAX(1, world_lot(hall)->build_left) : CPX_BUILD_MONTHS); END
        return 0;
    }
    LINE(c->output < 60 ? 2 : 0)
        str_printf(buf, size, "Jobs %d (%d filled) " TEXT_DOT " output %d%%", c->jobs, c->employed, c->output);
    END
    if (c->next) { LINE(3) str_printf(buf, size, "Building %s: ready in %d months", c->kind == CPX_PAPER ? (c->next == 2 ? "PM2" : "PM3") :
                                      s_vmline[ICLAMP(c->next - 1, 0, VM_LINES - 1)], c->build_left); END }
    if (c->kind == CPX_PAPER) {
        LINE(c->wood_pct < 75 ? 2 : 1)
            str_printf(buf, size, "Wood %d wu a month " TEXT_DOT " delivered %d%% " TEXT_DOT " forestry %d + regional %d",
                       c->wood_need, c->wood_pct, W->forestry_cap, W->regional);
        END
    }
    LINE(1)
        text_fmt_int(a, sizeof(a), c->vol);
        str_printf(buf, size, "Exports %s: West %d%%, East %d%%%s", a, c->vol ? (int)(c->west * 100 / c->vol) : 0,
                   c->vol ? (int)(c->east * 100 / c->vol) : 0, c->kind == CPX_METAL ? " (the clearing quota)" : " (paper for London)");
    END
    if (owner_present(c)) {
        LINE(c->mood <= 30 ? 2 : c->mood >= GIFT_MOOD ? 3 : 0)
            str_printf(buf, size, "%s: mood %d (%s), heading for %d", complex_owner_name(c->kind), c->mood, mood_word(c->mood), c->mood_target);
        END
        LINE(1)
            int i, len = 0;
            for (i = 1; i < c->nterms && len < size - 24; i++) {
                text_fmt_signed(a, sizeof(a), c->terms[i].amount);
                len += str_printf(buf + len, size - len, "%s%s %s", len ? ", " : "", mt_label(c->terms[i].id), a);
            }
            if (!len) str_copy(buf, "No particular opinions this month", size);
        END
    } else {
        LINE(1) str_copy(buf, "Owned by a Helsinki group: no patron, no gifts, no demands", size); END
    }
    LINE(X.company_town ? 3 : 1)
        text_fmt_money(b, sizeof(b), X.contrib, year);
        if (X.company_town) str_printf(buf, size, "Company town: %d%% of all jobs here; the company pays %d%% of school, clinic "
                                       "and fire upkeep (%s last month)", X.dep_pct, X.contrib_rate, b);
        else str_printf(buf, size, "%d%% of all jobs here (company town from %d%%)%s%s", X.dep_pct, CPX_DEP_PCT,
                        X.contrib > 0 ? "; gift upkeep paid " : "", X.contrib > 0 ? b : "");
    END
    n = 0;
    { int i; for (i = 0; i < X.nq; i++) if (X.q[i].cpx == ci - 1) n++; }
    if (n) { LINE(3) str_printf(buf, size, "Gift waiting: %s (Industry > Gift)", complex_gift_name(complex_gift_def(0), c->kind)); END }
    else if (owner_present(c)) {
        LINE(1)
            if (c->mood < GIFT_MOOD) str_printf(buf, size, "Next gift when the mood reaches %d", GIFT_MOOD);
            else str_printf(buf, size, "Next gift in %d months", IMAX(1, c->gift_cd));
        END
    }
    if (c->ev_state != EVS_NONE) {
        LINE(2)
            static const char *const en[EVK_COUNT] = { "", "Paper slump", "Strike", "Modernisation decision", "Sale of the mill", "Strike" };
            if (c->ev_state == EVS_ANNOUNCED) str_printf(buf, size, "%s from %d/%d", en[c->ev], c->ev_start % 12 + 1, mi_year(c->ev_start));
            else str_printf(buf, size, "%s until %d/%d", en[c->ev], (c->ev_start + c->ev_len) % 12 + 1, mi_year(c->ev_start + c->ev_len));
        END
    }
    if (c->kind == CPX_PAPER && c->stage < 3 && !c->next) {
        LINE(1)
            s32 avail = W->supply - (W->need - c->wood_need);
            if (c->stage == 1) str_printf(buf, size, "PM2 needs: town 3,000 (%d), wood %d for the mill (%d), staff, mood 31+",
                                          sim_stats()->population, s_pwood[2], avail);
            else str_printf(buf, size, "PM3 needs: 1963, town 6,000 (%d), wood %d (%d), a railway station%s",
                            sim_stats()->population, s_pwood[3], avail, (c->flags & CF_THREAT) ? ", the board's goodwill" : "");
        END
    }
    {
        int hall = hall_lot(c);
        const Lot *hl = world_lot(hall);
        if (c->flags & CF_RAPIDS) { LINE(3) str_copy(buf, "Own turbines on the rapids: no power needed from the grid", size); END }
        else if (hl && !(hl->flags & LF_POWERED)) { LINE(2) str_copy(buf, "No power: connect the mill to a plant", size); END }
        if (hl && !(hl->flags & LF_WATERED)) { LINE(2) str_copy(buf, "No water: lay a pipe to the mill", size); END }
    }
    if (c->kind == CPX_METAL && year >= 1990) { LINE(2) str_copy(buf, "Clearing trade has ended: output and jobs halved", size); END }
#undef LINE
#undef END
    return 0;
}

/* ------------------------------------------------------------------ tests, scripts */
void complex_set_mood(int kind, int mood)
{
    Complex *c = of_kind(kind);
    if (c) c->mood = c->mood_target = (u8)ICLAMP(mood, 0, 100);
}

int complex_script_line(int ntok, char **tok, int org_x, int org_y, FILE *out)
{
    int kind, x, y, r, id;
    Money cost = 0;
    if (ntok < 1 || !str_ieq(tok[0], "complex")) return -1;
    if (ntok >= 5 && str_ieq(tok[1], "place")) {
        kind = str_ieq(tok[2], "paper") ? CPX_PAPER : str_ieq(tok[2], "metal") ? CPX_METAL : CPX_NONE;
        x = atoi(tok[3]) + org_x; y = atoi(tok[4]) + org_y;
        r = complex_check_place(kind, x, y, &cost);
        if (r == PLACE_OK && !budget_can_afford(cost)) r = REJ_NO_MONEY;
        id = r == PLACE_OK ? complex_place(kind, x, y) : 0;
        if (out) fprintf(out, "script: complex place %s %d %d -> %d (%s), %d mk\n", tok[2], x, y, id, id ? "ok" : text_place_reason(r), id ? cost : 0);
        return id ? SCR_OK : SCR_ERROR;
    }
    if (ntok >= 4 && str_ieq(tok[1], "gift")) {
        x = atoi(tok[2]) + org_x; y = atoi(tok[3]) + org_y;
        r = ntok >= 5 ? atoi(tok[4]) & 3 : 0;
        if (ntok < 5 && X.nq) r = gift_rot(X.q[0].def, x, y, 0);
        kind = complex_check_gift(x, y, r);
        id = kind == PLACE_OK ? complex_place_gift(x, y, r) : 0;
        if (out) fprintf(out, "script: complex gift %d %d rot %d -> lot %d (%s)\n", x, y, r, id, id ? "ok" : text_place_reason(kind));
        return id ? SCR_OK : SCR_ERROR;
    }
    if (ntok >= 4 && str_ieq(tok[1], "mood")) {
        complex_set_mood(str_ieq(tok[2], "metal") ? CPX_METAL : CPX_PAPER, atoi(tok[3]));
        return SCR_OK;
    }
    if (ntok >= 2 && str_ieq(tok[1], "dump")) {
        FILE *f = out, *own = NULL;
        if (ntok >= 3) { own = fopen(tok[2], "a"); if (own) f = own; }
        if (f) complex_dump(f);
        if (own) fclose(own);
        return SCR_OK;
    }
    if (out) fprintf(out, "script: complex: place paper|metal X Y | gift X Y [ROT] | mood paper|metal N | dump [FILE]\n");
    return SCR_ERROR;
}

void complex_dump(FILE *f)
{
    int i, k;
    const SimDate *d = sim_date();
    fprintf(f, "complexes %d-%02d: dependency %d%%, company town %d (rate %d%%), contribution %d mk, gifts waiting %d, placed %d,"
               " VM hook %d permit %d; exports West %d East %d\n",
            d->year, d->month + 1, X.dep_pct, X.company_town, X.contrib_rate, X.contrib, X.nq, X.ng, X.vm_hook, X.vm_permit,
            complex_export_west(), complex_export_east());
    for (i = 0; i < MAX_COMPLEX; i++) {
        const Complex *c = &X.c[i];
        if (!c->kind) continue;
        fprintf(f, "  [%d] %s at %d,%d: stage %d next %d (%d months) flags %03x | jobs %d employed %d output %d%% | wood need %d"
                   " delivered %d%% | vol %d W %d E %d | mood %d -> %d, gifts %d (cd %d, boost %d), demand cd %d, staffed %d"
                   " | event %d state %d resp %d start %d-%02d len %d | agree %d\n",
                i, complex_name(c->kind), c->x, c->y, c->stage, c->next, c->build_left, c->flags, c->jobs, c->employed,
                c->output, c->wood_need, c->wood_pct, c->vol, c->west, c->east, c->mood, c->mood_target, c->gifts, c->gift_cd,
                c->gift_boost, c->demand_cd, c->staffed, c->ev, c->ev_state, c->ev_resp == 255 ? -1 : c->ev_resp,
                mi_year(c->ev_start), c->ev_start % 12 + 1, c->ev_len, c->agree);
        fprintf(f, "      schedule: slumps");
        for (k = 0; k < 4; k++) if (c->sched[k]) fprintf(f, " %d-%02d", mi_year(c->sched[k]), c->sched[k] % 12 + 1);
        if (c->modern_at) fprintf(f, ", modernisation %d-%02d", mi_year(c->modern_at), c->modern_at % 12 + 1);
        if (c->merger_at) fprintf(f, ", merger %d-%02d", mi_year(c->merger_at), c->merger_at % 12 + 1);
        if (c->strike_at) fprintf(f, ", strike %d-%02d", mi_year(c->strike_at), c->strike_at % 12 + 1);
        fprintf(f, "; mood terms");
        for (k = 0; k < c->nterms; k++) fprintf(f, " %s %+d", mt_label(c->terms[k].id), c->terms[k].amount);
        fprintf(f, "\n");
    }
    for (k = 0; k < X.nq; k++) fprintf(f, "  waiting gift %d: %s from [%d]\n", k, def_building(X.q[k].def)->key, X.q[k].cpx);
    for (k = 0; k < X.ng; k++) fprintf(f, "  placed gift: %s at %d,%d from [%d]\n", def_building(X.g[k].def)->key, X.g[k].x, X.g[k].y, X.g[k].cpx);
}

/* ------------------------------------------------------------------ hash, save */
/* fields in a fixed order (save and hash share it) */
#define CPX_FIELDS(F) \
    F(kind) F(x) F(y) F(stage) F(next) F(build_left) F(mood) F(mood_target) F(flags) F(jobs) F(employed) F(output) \
    F(wood_need) F(wood_pct) F(staffed) F(gift_cd) F(demand_cd) F(gift_boost) F(gifts) F(ev) F(ev_state) F(ev_resp) \
    F(ev_card) F(ev_start) F(ev_len) F(sched[0]) F(sched[1]) F(sched[2]) F(sched[3]) F(modern_at) F(merger_at) \
    F(strike_at) F(strike_gate) F(adj[0]) F(adj[1]) F(adj[2]) F(adj[3]) F(adj_m[0]) F(adj_m[1]) F(adj_m[2]) F(adj_m[3]) F(out_adj) \
    F(out_adj_m) F(agree) F(placed) F(vol) F(west) F(east)

u32 complex_hash(void)
{
    u32 h = HASH_INIT;
    int i, k;
    s32 v;
    if (!X.any) return 0;
#define HF(f) v = (s32)c->f; h = hash_bytes(h, &v, sizeof(v));
    for (i = 0; i < MAX_COMPLEX; i++) { const Complex *c = &X.c[i]; CPX_FIELDS(HF) }
#undef HF
    for (k = 0; k < X.nq; k++) { v = X.q[k].def | (X.q[k].cpx << 8); h = hash_bytes(h, &v, sizeof(v)); }
    for (k = 0; k < X.ng; k++) { v = X.g[k].def | (X.g[k].cpx << 8) | (X.g[k].x << 16) | (X.g[k].y << 24); h = hash_bytes(h, &v, sizeof(v)); }
    v = X.company_town | (X.vm_permit << 1) | (X.dep_pct << 2); h = hash_bytes(h, &v, sizeof(v));
    h = hash_bytes(h, &X.contrib, sizeof(X.contrib));
    return h;
}

int complex_save(SaveFile *sf)
{
    int i, k;
    save_begin_chunk(sf, "CPLX", 1);
    save_s32(sf, MAX_COMPLEX);
#define SF(f) save_s32(sf, (s32)c->f);
    for (i = 0; i < MAX_COMPLEX; i++) {
        const Complex *c = &X.c[i];
        CPX_FIELDS(SF)
        save_s32(sf, c->nterms);
        for (k = 0; k < (int)COUNTOF(c->terms); k++) { save_u8(sf, c->terms[k].id); save_s32(sf, c->terms[k].amount); }
    }
#undef SF
    save_s32(sf, X.nq);
    for (k = 0; k < X.nq; k++) { save_u8(sf, X.q[k].def); save_u8(sf, X.q[k].cpx); }
    save_s32(sf, X.ng);
    for (k = 0; k < X.ng; k++) { save_u8(sf, X.g[k].def); save_u8(sf, X.g[k].cpx); save_u8(sf, X.g[k].x); save_u8(sf, X.g[k].y); }
    save_s32(sf, X.vm_hook); save_s32(sf, X.vm_permit); save_s32(sf, X.vm_said);
    save_s32(sf, X.company_town); save_s32(sf, X.dep_pct); save_s32(sf, X.contrib); save_s32(sf, X.contrib_rate);
    save_s32(sf, X.any);
    save_end_chunk(sf);
    return 1;
}

int complex_load(SaveFile *sf)
{
    int i, k, n, ver;
    Complex dummy;
    if (!load_find_chunk(sf, "CPLX", &ver)) return 0;
    n = load_s32(sf);
    if (n < 0 || n > 64) return 0;
#define LF(f) c->f = load_s32(sf);
    for (i = 0; i < n; i++) {
        Complex *c = i < MAX_COMPLEX ? &X.c[i] : &dummy;
        memset(c, 0, sizeof(*c));
        CPX_FIELDS(LF)
        c->nterms = load_s32(sf);
        for (k = 0; k < (int)COUNTOF(c->terms); k++) { c->terms[k].id = (u8)load_u8(sf); c->terms[k].amount = load_s32(sf); }
        c->nterms = ICLAMP(c->nterms, 0, (int)COUNTOF(c->terms));
    }
#undef LF
    X.nq = ICLAMP(load_s32(sf), 0, CPX_GIFT_QUEUE);
    for (k = 0; k < X.nq; k++) { X.q[k].def = (u8)load_u8(sf); X.q[k].cpx = (u8)load_u8(sf); }
    X.ng = ICLAMP(load_s32(sf), 0, CPX_MAX_GIFTS);
    for (k = 0; k < X.ng; k++) { X.g[k].def = (u8)load_u8(sf); X.g[k].cpx = (u8)load_u8(sf); X.g[k].x = (u8)load_u8(sf); X.g[k].y = (u8)load_u8(sf); }
    X.vm_hook = load_s32(sf); X.vm_permit = load_s32(sf); X.vm_said = load_s32(sf);
    X.company_town = load_s32(sf); X.dep_pct = load_s32(sf); X.contrib = load_s32(sf); X.contrib_rate = load_s32(sf);
    X.any = load_s32(sf);
    return load_ok(sf);
}

void complex_after_load(int had_chunk)
{
    int i;
    UNUSED(had_chunk);
    memset(s_use, 0, sizeof(s_use));
    s_msg_head = s_msg_n = 0;
    politics_set_event_hook(complex_on_event);
    /* the use tables as they were when saved (no network notification: the NET chunk holds the solved state) */
    for (i = 0; i < MAX_COMPLEX; i++) {
        if (!X.c[i].kind) continue;
        fill_use(&X.c[i]);
        industry_set_ext_wood(i, X.c[i].wood_need);
    }
}
