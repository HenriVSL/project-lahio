/* industry.cpp - M3: industry kinds, wood/milk supply, forestry rotation + regrowth, farm claims, export
   volumes by kind. PORTABLE, deterministic, integer-only. Owner: M3. See industry.h for the model;
   numbers from docs/research/INDUSTRY.md 5-6 (deviations noted in docs/HANDOFF.md M3).

   Called by sim.cpp: industry_cycle (phase 15 after the demand), industry_month (month end, books part),
   industry_candidates / industry_grow_rural (growth bands), industry_rural_score / industry_lot_unmet /
   industry_lot_reqs (score and checklist). All state is in the structs below and the INDU save chunk. */
#include "common.h"
#include "industry.h"
#include "world.h"
#include "net.h"
#include "fields.h"
#include "save.h"
#include "util.h"

#define MAX_CAMPS 1024
#define EXT_SLOTS 4
#define WOOD_STOCK_MAX (3 * IND_TILE_WU)
#define CLAIM_BYTES (MAP_MAX_W * MAP_MAX_H / 8)

static IndWood  W;
static IndMilk  M;
static IndKinds K;
static s32  s_ext_wood[EXT_SLOTS];
static u8   s_claim[CLAIM_BYTES];          /* farm fields claimed by a farmstead (saved) */
static u16  s_owner[MAP_MAX_W * MAP_MAX_H];/* month_harvest scratch: camp ordinal harvesting a forestry tile */
static s32  s_camp_cover[MAX_CAMPS + 1];   /* scratch: tiles per camp ordinal */
static int  s_last_pick;
static int  s_transport_float;             /* this cycle: a sawmill touches water (log floating) */

/* ------------------------------------------------------------------ small helpers */
static int claimed(int x, int y) { int i = TILE_IDX(x, y); return (s_claim[i >> 3] >> (i & 7)) & 1; }
static void claim_set(int x, int y) { int i = TILE_IDX(x, y); s_claim[i >> 3] |= (u8)(1 << (i & 7)); }
int industry_tile_claimed(int x, int y) { return world_in(x, y) ? claimed(x, y) : 0; }

int industry_forest_stage(int g)
{
    if (g <= 0) return FST_MATURE;
    if (g <= 3) return FST_CUT;
    if (g <= 7) return FST_SEEDLINGS;
    return FST_YOUNG;
}

static int cheb_rect(int i, int j, int x0, int y0, int x1, int y1)
{
    int dx = i < x0 ? x0 - i : (i > x1 ? i - x1 : 0), dy = j < y0 ? y0 - j : (j > y1 ? j - y1 : 0);
    return IMAX(dx, dy);
}

static s32 term_of(const Demand *d, int id)
{
    int k;
    for (k = 0; k < d->nterms; k++) if (d->terms[k].id == id) return d->terms[k].amount;
    return 0;
}

/* ------------------------------------------------------------------ kinds */
int industry_def_kind(int def)
{
    switch (def) {
    case B_IND_WOOD_T1_3X3: case B_IND_WOOD_T1_2X2: case B_IND_WOOD_T2_3X3: case B_IND_T1_3X3:
        return IK_WOOD;
    case B_IND_METAL_T1_2X2: case B_IND_METAL_T2_2X2: case B_IND_METAL_T3_3X3:
    case B_IND_T1_2X2: case B_IND_T2_2X2: case B_IND_T3_3X3:
        return IK_METAL;
    case B_IND_TEXTILE_T1_2X2: case B_IND_TEXTILE_T2_3X3: case B_IND_T2_3X3:
        return IK_TEXTILE;
    case B_IND_DAIRY_T1_2X2: case B_IND_DAIRY_T2_2X2:
        return IK_FOOD;
    case B_IND_ELEC_T2_2X2: case B_IND_T3_2X2:
        return IK_ELEC;
    default:
        return IK_NONE;
    }
}

int industry_def_input(int def, int *milk)
{
    int k = industry_def_kind(def);
    if (milk) *milk = k == IK_FOOD;
    if (k == IK_WOOD) return def_building(def)->capacity / IND_WOOD_JOBS_PER_WU;
    if (k == IK_FOOD) return def_building(def)->capacity / IND_MILK_JOBS_PER_MU;
    return 0;
}

const char *industry_kind_name(int kind)
{
    static const char *const n[IK_COUNT] = { "Industry", "Sawmill", "Engineering works", "Textile mill", "Dairy",
                                             "Electronics works" };
    return (kind >= 0 && kind < IK_COUNT) ? n[kind] : "?";
}

const char *industry_kind_plural(int kind)
{
    static const char *const n[IK_COUNT] = { "Other", "Sawmills", "Engineering", "Textiles", "Dairies", "Electronics" };
    return (kind >= 0 && kind < IK_COUNT) ? n[kind] : "?";
}

int industry_export_weight(int def)
{
    static const int w[IK_COUNT] = { 100, 133, 100, 67, 100, 200 };
    return w[industry_def_kind(def)];
}

int industry_west_pct(int kind, int year)
{
    switch (kind) {
    case IK_WOOD: return 60;
    case IK_METAL: return 25;
    case IK_TEXTILE: return year >= 1975 ? 10 : 20;
    case IK_FOOD: return 40;
    case IK_ELEC: return 50;
    default: return 25;
    }
}

/* candidate ordering (largest first; tests) */
int industry_def_rank(int def) { const BuildingDef *d = def_building(def); return d->w * d->h * 4 + d->tier; }

/* kind shares of the three I sources (clearing, western, local need) in percent; engineering takes the
   rest of each source (INDUSTRY.md 5.3, with the Western column fixed to sum to 100) */
static void kind_shares(int year, int sh[IK_COUNT][3])
{
    int k, s;
    memset(sh, 0, sizeof(int) * IK_COUNT * 3);
    sh[IK_WOOD][0] = 20;    sh[IK_WOOD][1] = year >= 1965 ? 35 : 45; sh[IK_WOOD][2] = 15;
    sh[IK_TEXTILE][0] = 25; sh[IK_TEXTILE][1] = 10;                  sh[IK_TEXTILE][2] = 15;
    sh[IK_FOOD][0] = 10;    sh[IK_FOOD][1] = 15;                     sh[IK_FOOD][2] = 25;
    sh[IK_ELEC][0] = year >= 1970 ? 10 : 0; sh[IK_ELEC][1] = year >= 1965 ? 10 : 0;
    for (s = 0; s < 3; s++) {
        int sum = 0;
        for (k = 0; k < IK_COUNT; k++) if (k != IK_METAL) sum += sh[k][s];
        sh[IK_METAL][s] = 100 - sum;
    }
}

static int elec_unlocked(void)
{
    return sim_date()->year >= 1965 && (sim_stats()->town_status >= TS_TOWN || sim_settings()->all_unlocked);
}

static int wood_ok(s32 extra) { return W.supply * 100 >= IND_INPUT_GROW_PCT * (W.need + extra); }
static int milk_ok(s32 extra) { return M.supply * 100 >= IND_INPUT_GROW_PCT * (M.need + extra); }

/* ------------------------------------------------------------------ lifecycle */
void industry_init(void) { industry_reset(); }

void industry_reset(void)
{
    memset(&W, 0, sizeof(W)); memset(&M, 0, sizeof(M)); memset(&K, 0, sizeof(K));
    memset(s_ext_wood, 0, sizeof(s_ext_wood));
    memset(s_claim, 0, sizeof(s_claim));
    W.pct = 100; M.pct = 100;
    W.regional = IND_WOOD_REGIONAL; W.supply = IND_WOOD_REGIONAL;
    M.regional = IND_MILK_REGIONAL; M.supply = IND_MILK_REGIONAL;
    s_last_pick = IK_NONE; s_transport_float = 0;
}

void industry_set_ext_wood(int slot, s32 wu) { if (slot >= 0 && slot < EXT_SLOTS) s_ext_wood[slot] = IMAX(0, wu); }

/* water (lake or river) within 2 tiles of a lot rect: the log pond of a sawmill */
static int rect_near_water(int x0, int y0, int x1, int y1, int r)
{
    int i, j;
    for (j = y0 - r; j <= y1 + r; j++)
        for (i = x0 - r; i <= x1 + r; i++)
            if (world_in(i, j) && TER_IS_WATER(TER_TYPE(WT(i, j)))) return 1;
    return 0;
}

/* per cycle: needs, supplies, capacities by kind, kind demand */
void industry_cycle(void)
{
    const Demand *dI = sim_demand(DEM_I);
    int i, k, year = sim_date()->year, station = 0;
    int sh[IK_COUNT][3];
    s32 src[3], sum_gross = 0, other, assigned = 0, head, allowed, moved = 0;
    s32 wood_need = 0, milk_need = 0, farm_milk = 0;
    memset(K.capacity, 0, sizeof(K.capacity)); memset(K.jobs, 0, sizeof(K.jobs)); memset(K.lots, 0, sizeof(K.lots));
    memset(K.lots_tier, 0, sizeof(K.lots_tier)); memset(K.short_jobs, 0, sizeof(K.short_jobs));
    M.farm_lots = 0; M.fields = 0; M.farm_residents = 0; memset(M.farm_tier, 0, sizeof(M.farm_tier));
    W.camps = 0;
    s_transport_float = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        if (l->def == B_NONE) continue;
        d = def_building(l->def);
        if (l->def == B_STATION && l->status == LOT_ACTIVE) station = 1;
        if (d->cat != BCAT_ZONE) continue;
        if (d->zone == ZONE_INDUSTRY) {
            int kl;
            k = industry_def_kind(l->def);
            kl = k;                                     /* yards: counted as IK_NONE lots ... */
            if (k == IK_NONE) k = IK_METAL;            /* ... but their jobs/capacity go with engineering */
            if (l->status == LOT_ABANDONED) continue;
            K.capacity[k] += d->capacity;
            K.lots[kl]++;
            K.lots_tier[kl][d->tier & 3]++;
            if (l->status == LOT_ACTIVE) K.jobs[k] += l->occupants;
            if (k == IK_WOOD) {
                wood_need += d->capacity / IND_WOOD_JOBS_PER_WU;
                if (!s_transport_float && year < 1970) {
                    int x0, y0, x1, y1;
                    world_lot_rect(i, &x0, &y0, &x1, &y1);
                    if (rect_near_water(x0, y0, x1, y1, 2)) s_transport_float = 1;
                }
            } else if (k == IK_FOOD) milk_need += d->capacity / IND_MILK_JOBS_PER_MU;
        } else if (d->zone == ZONE_FARM) {
            if (l->status == LOT_ABANDONED) continue;
            M.farm_lots++;
            M.farm_tier[d->tier & 3]++;
            if (l->status != LOT_ACTIVE) continue;
            M.farm_residents += l->occupants;
            M.fields += l->spare & 0xFF;
            farm_milk += (l->spare & 0xFF) * d->tier;
        } else if (d->zone == ZONE_FORESTRY && l->status == LOT_ACTIVE) W.camps++;
    }
    /* wood */
    W.need_ext = 0;
    for (k = 0; k < EXT_SLOTS; k++) W.need_ext += s_ext_wood[k];
    W.need = wood_need + W.need_ext;
    W.regional = IND_WOOD_REGIONAL + (station ? IND_WOOD_STATION : 0) +
                 ((s_transport_float || year >= 1965) ? IND_WOOD_TRANSPORT : 0);
    W.regional = IMIN(W.regional, IND_WOOD_REGIONAL_MAX);
    W.supply = W.regional + W.forestry_cap;
    /* milk */
    M.need = milk_need;
    M.regional = IND_MILK_REGIONAL;
    M.farms = farm_milk;
    M.supply = M.regional + M.farms;
    M.food_need = 20 + sim_stats()->population / 50;
    M.farm_demand = M.food_need + M.need - M.supply;
    M.pct = M.need ? IMIN(100, M.supply * 100 / M.need) : 100;
    /* kind demand: split the I target by export category */
    kind_shares(year, sh);
    src[0] = term_of(dI, DT_CLEARING); src[1] = term_of(dI, DT_WESTERN); src[2] = term_of(dI, DT_LOCAL_NEED);
    for (k = 1; k < IK_COUNT; k++) {
        if (k == IK_METAL) continue;
        K.gross[k] = (sh[k][0] * src[0] + sh[k][1] * src[1] + sh[k][2] * src[2]) / 100;
        sum_gross += K.gross[k];
    }
    K.gross[IK_METAL] = src[0] + src[1] + src[2] - sum_gross;
    sum_gross += K.gross[IK_METAL];
    K.gross[IK_NONE] = 0;
    /* the rest of the target (tax, policy, later terms) in proportion to the gross shares */
    other = dI->target - (src[0] + src[1] + src[2]) - term_of(dI, DT_IND_CAPACITY);
    K.demand[IK_NONE] = 0;
    for (k = 1; k < IK_COUNT; k++) {
        if (k == IK_METAL) continue;
        K.demand[k] = K.gross[k] - K.capacity[k] + (sum_gross > 0 ? (s32)((s64)other * K.gross[k] / sum_gross) : 0);
        assigned += K.demand[k];
    }
    K.demand[IK_METAL] = dI->target - assigned;        /* exact sum = the I target */
    /* inputs short: the kind can't use its demand, engineering (the fallback) gets it */
    head = W.supply * 100 / IND_INPUT_GROW_PCT - W.need;            /* wu that could still be added */
    allowed = IMAX(0, head) * IND_WOOD_JOBS_PER_WU;
    if (K.demand[IK_WOOD] > allowed) { K.short_jobs[IK_WOOD] = K.demand[IK_WOOD] - allowed; K.demand[IK_WOOD] = allowed; moved += K.short_jobs[IK_WOOD]; }
    head = M.supply * 100 / IND_INPUT_GROW_PCT - M.need;
    allowed = IMAX(0, head) * IND_MILK_JOBS_PER_MU;
    if (K.demand[IK_FOOD] > allowed) { K.short_jobs[IK_FOOD] = K.demand[IK_FOOD] - allowed; K.demand[IK_FOOD] = allowed; moved += K.short_jobs[IK_FOOD]; }
    if (!elec_unlocked() && K.demand[IK_ELEC] > 0) { K.short_jobs[IK_ELEC] = K.demand[IK_ELEC]; K.demand[IK_ELEC] = 0; moved += K.short_jobs[IK_ELEC]; }
    K.demand[IK_METAL] += moved;
}

/* ------------------------------------------------------------------ site evaluation (kind choice) */
typedef struct { int d_water, d_river, d_rail, d_main, d_forestry, d_farm, d_station, n_forest; } Site;

static void site_scan(int x0, int y0, int x1, int y1, Site *s)
{
    int i, j, d;
    s->d_water = s->d_river = s->d_rail = s->d_main = s->d_forestry = s->d_farm = s->d_station = 99;
    s->n_forest = 0;
    for (j = y0 - 10; j <= y1 + 10; j++) {
        if (j < 0 || j >= g_world.h) continue;
        for (i = x0 - 10; i <= x1 + 10; i++) {
            const Tile *t;
            int ter, z;
            if (i < 0 || i >= g_world.w) continue;
            t = WT(i, j);
            d = cheb_rect(i, j, x0, y0, x1, y1);
            ter = TER_TYPE(t);
            z = TILE_ZONE(t);
            if (TER_IS_WATER(ter)) { if (d < s->d_water) s->d_water = d; if (ter == TER_RIVER && d < s->d_river) s->d_river = d; }
            if (TILE_HAS_RAIL(t)) { if (d < s->d_rail) s->d_rail = d; }         /* 1.2 X1: crossings are rail */
            if (TILE_ROAD(t) == ROAD_MAIN && d < s->d_main) s->d_main = d;
            if (ter == TER_FOREST && d <= 8) s->n_forest++;
            if (t->lot) {
                const BuildingDef *ld = def_building(g_world.lots[t->lot].def);
                if (ld->zone) z = ld->zone;
                if (g_world.lots[t->lot].def == B_STATION && d < s->d_station) s->d_station = d;
            }
            if (z == ZONE_FORESTRY && d < s->d_forestry) s->d_forestry = d;
            if (z == ZONE_FARM && d < s->d_farm) s->d_farm = d;
        }
    }
}

static int site_bonus(int kind, const Site *s)
{
    switch (kind) {
    case IK_WOOD:    return (s->d_water <= 2 ? 50 : 0) + (s->n_forest >= 6 ? 25 : 0) + (s->d_forestry <= 8 ? 25 : 0);
    case IK_METAL:   return (s->d_rail <= 4 ? 50 : 0) + (s->d_main <= 3 ? 25 : 0);
    case IK_TEXTILE: return s->d_river <= 4 ? 50 : 0;
    case IK_FOOD:    return s->d_farm <= 10 ? 25 : 0;
    case IK_ELEC:    return s->d_station <= 6 ? 50 : 0;
    default:         return 0;
    }
}

/* the kind's site rule ("must") for a footprint starting at (x0, y0) */
static int site_must(int kind, int x0, int y0, int x1, int y1)
{
    int cx = (x0 + x1) >> 1, cy = (y0 + y1) >> 1;
    switch (kind) {
    case IK_WOOD: return wood_ok(def_building(B_IND_WOOD_T1_2X2)->capacity / IND_WOOD_JOBS_PER_WU);
    case IK_FOOD: return milk_ok(def_building(B_IND_DAIRY_T1_2X2)->capacity / IND_MILK_JOBS_PER_MU);
    case IK_ELEC: return elec_unlocked() && net_cov_quality(SVC_SCHOOL, cx, cy) >= 50 && field_at(FLD_POLLUTION, cx, cy) <= 48;
    default:      return 1;
    }
}

static int rect_free_ind(int x0, int y0, int x1, int y1)
{
    int i, j;
    if (!world_in(x0, y0) || !world_in(x1, y1)) return 0;
    for (j = y0; j <= y1; j++)
        for (i = x0; i <= x1; i++)
            if (TILE_ZONE(WT(i, j)) != ZONE_INDUSTRY || !world_is_buildable(i, j)) return 0;
    return 1;
}

int industry_candidates(int x, int y, u8 *defs, int max)
{
    Site s;
    s32 w[IK_COUNT], total = 0;
    int k, kind = IK_METAL, n = 0, r;
    if (max < 2 || !rect_free_ind(x, y, x + 1, y + 1)) return 0;
    site_scan(x, y, x + 1, y + 1, &s);
    for (k = 0; k < IK_COUNT; k++) {
        w[k] = 0;
        if (k == IK_NONE || !site_must(k, x, y, x + 1, y + 1)) continue;
        w[k] = (s32)((s64)IMAX(0, K.demand[k]) * (100 + 2 * site_bonus(k, &s)) / 100);   /* site counts double */
        total += w[k];
    }
    if (total > 0) {
        r = rng_below(sim_rng(), (int)total);
        for (k = 1; k < IK_COUNT; k++) { if (r < w[k]) { kind = k; break; } r -= w[k]; }
    }
    s_last_pick = kind;
    switch (kind) {
    case IK_WOOD:
        if (wood_ok(def_building(B_IND_WOOD_T1_3X3)->capacity / IND_WOOD_JOBS_PER_WU)) defs[n++] = B_IND_WOOD_T1_3X3;
        defs[n++] = B_IND_WOOD_T1_2X2;
        break;
    case IK_TEXTILE:
        defs[n++] = B_IND_TEXTILE_T2_3X3; defs[n++] = B_IND_TEXTILE_T1_2X2;
        break;
    case IK_FOOD:
        defs[n++] = B_IND_DAIRY_T1_2X2;
        break;
    case IK_ELEC:
        defs[n++] = B_IND_ELEC_T2_2X2;
        break;
    default:
        if (sim_date()->year >= def_building(B_IND_METAL_T3_3X3)->era_year) defs[n++] = B_IND_METAL_T3_3X3;
        defs[n++] = B_IND_METAL_T1_2X2;
        break;
    }
    return n;
}

int industry_last_pick(void) { return s_last_pick; }

/* ------------------------------------------------------------------ rural zones: growth */
static int camp_near(int x, int y, int r)
{
    int i, j;
    for (j = y - r; j <= y + r; j++)
        for (i = x - r; i <= x + r; i++) {
            const Tile *t;
            if (!world_in(i, j)) continue;
            t = WT(i, j);
            if (t->lot && g_world.lots[t->lot].def == B_LOGGING_CAMP) return 1;
        }
    return 0;
}

static int is_forestry_forest(const Tile *t) { return TER_TYPE(t) == TER_FOREST && TILE_ZONE(t) == ZONE_FORESTRY && !t->lot; }

static int farm_free(int x, int y)
{
    const Tile *t;
    if (!world_in(x, y)) return 0;
    t = WT(x, y);
    return TILE_ZONE(t) == ZONE_FARM && !t->lot && !TILE_ROAD(t) && !TER_IS_WATER(TER_TYPE(t)) && !claimed(x, y);
}

/* claim up to IND_FARM_FIELDS fields for farm lot id, nearest first; grass becomes field */
static int claim_for(int id)
{
    Lot *l = &g_world.lots[id];
    int d, i, j, n = 0, x = l->x, y = l->y;
    for (d = 1; d <= IND_FARM_RADIUS && n < IND_FARM_FIELDS; d++)
        for (j = y - d; j <= y + d && n < IND_FARM_FIELDS; j++)
            for (i = x - d; i <= x + d && n < IND_FARM_FIELDS; i++) {
                if (IMAX(IABS(i - x), IABS(j - y)) != d || !farm_free(i, j)) continue;
                claim_set(i, j);
                n++;
                if (TER_TYPE(WT(i, j)) == TER_GRASS) {
                    if (WT(i, j)->flags & TF_TREE) world_set_tile_flags(i, j, 0, TF_TREE);
                    world_set_terrain(i, j, TER_FIELD, (hash_xy(i, j, 0xF1E1D) & 3) == 0 ? FIELD_PLOUGH : FIELD_CROP);
                }
            }
    l->spare = (u16)((l->spare & 0xFF00) | n);
    return n;
}

void industry_grow_rural(int x, int y, int zone)
{
    const Tile *t = WT(x, y);
    int i, j, n = 0, rot = ROT_0, id;
    if (t->lot || !world_is_buildable(x, y)) return;
    if (zone == ZONE_FARM) {
        if (M.farm_demand <= 0 || claimed(x, y)) return;
        n = net_road_reach(x, y);                /* 0 = road reach not computed yet (first cycle) */
        if (n < 1 || n > ZONE_ROAD_REACH) return;
        n = 0;
        for (j = y - IND_FARM_RADIUS; j <= y + IND_FARM_RADIUS; j++)
            for (i = x - IND_FARM_RADIUS; i <= x + IND_FARM_RADIUS; i++)
                if ((i != x || j != y) && farm_free(i, j)) n++;
        if (n < IND_FARM_MIN_FREE) return;
        if (rng_below(sim_rng(), 4)) return;
        world_footprint_road_dist(x, y, x, y, ZONE_ROAD_REACH, &rot);
        id = world_lot_place(B_FARM_T1, x, y, rot, (int)(hash_xy(x, y, B_FARM_T1) % 2u), LOT_BUILDING);
        if (!id) return;
        M.farm_demand -= claim_for(id);       /* T1: 1 mu per field; no burst of farms in one cycle */
    } else if (zone == ZONE_FORESTRY) {
        n = net_road_reach(x, y);
        if (n < 1 || n > 2) return;
        n = 0;
        if (camp_near(x, y, IND_CAMP_SPACING)) return;
        for (j = y - IND_CAMP_RADIUS; j <= y + IND_CAMP_RADIUS; j++)
            for (i = x - IND_CAMP_RADIUS; i <= x + IND_CAMP_RADIUS; i++)
                if (world_in(i, j) && is_forestry_forest(WT(i, j))) n++;
        if (n < IND_CAMP_MIN_TILES) return;
        if (rng_below(sim_rng(), 4)) return;
        world_footprint_road_dist(x, y, x, y, ZONE_ROAD_REACH, &rot);
        world_lot_place(B_LOGGING_CAMP, x, y, rot, sim_date()->year >= 1965 ? 1 : 0, LOT_BUILDING);
    }
}

s32 industry_rural_score(int zone, int x, int y)
{
    if (zone == ZONE_FARM) return 12 - field_at(FLD_POLLUTION, x, y) / 4;
    return 12;
}

/* ------------------------------------------------------------------ requirements of kind lots */
static s32 up_extra(int def)
{
    int up = def_building(def)->upgrade_to;
    if (up == B_NONE) return -1;
    return industry_def_input(up, NULL) - industry_def_input(def, NULL);
}

/* best access road class within 3 of a lot (mechanised farms need a street or a main road) */
static int lot_best_road(int lot)
{
    int x0, y0, x1, y1, i, j, best = 0;
    world_lot_rect(lot, &x0, &y0, &x1, &y1);
    for (j = y0 - ZONE_ROAD_REACH; j <= y1 + ZONE_ROAD_REACH; j++)
        for (i = x0 - ZONE_ROAD_REACH; i <= x1 + ZONE_ROAD_REACH; i++) {
            int c = TILE_ROAD(world_tile(i, j));
            if (c && (g_roads[c].flags & RDF_ZONE_ACCESS)) best = IMAX(best, c);
        }
    return best;
}

u32 industry_lot_unmet(int lot, int next_tier)
{
    const Lot *l = world_lot(lot);
    int kind;
    s32 extra;
    if (!l) return 0;
    if (l->def == B_FARM_T2 && next_tier) return lot_best_road(lot) >= ROAD_STREET ? 0 : 1u << REQ_MAIN_ROAD;
    kind = industry_def_kind(l->def);
    if (kind != IK_WOOD && kind != IK_FOOD) return 0;
    if (!next_tier) {
        if (kind == IK_WOOD) return W.pct < IND_INPUT_KEEP_PCT ? 1u << REQ_WOOD : 0;
        return M.supply * 100 < IND_INPUT_KEEP_PCT * M.need ? 1u << REQ_MILK : 0;
    }
    extra = up_extra(l->def);
    if (extra < 0) return 0;
    if (kind == IK_WOOD) return wood_ok(extra) ? 0 : 1u << REQ_WOOD;
    return milk_ok(extra) ? 0 : 1u << REQ_MILK;
}

int industry_lot_reqs(int lot, int next_tier, ReqItem *out, int max)
{
    const Lot *l = world_lot(lot);
    int kind;
    s32 extra = 0, sup, need;
    if (!l || max < 1) return 0;
    if (l->def == B_FARM_T2 && next_tier) {         /* "Street or main road (only a gravel road)" */
        memset(out, 0, sizeof(*out));
        out->req = REQ_MAIN_ROAD; out->why = 1;
        out->have = lot_best_road(lot); out->need = ROAD_STREET;
        out->met = (u8)(out->have >= ROAD_STREET);
        return 1;
    }
    kind = industry_def_kind(l->def);
    if (kind != IK_WOOD && kind != IK_FOOD) return 0;
    if (next_tier && (extra = up_extra(l->def)) < 0) next_tier = 0;
    memset(out, 0, sizeof(*out));
    out->req = (u8)(kind == IK_WOOD ? REQ_WOOD : REQ_MILK);
    sup = kind == IK_WOOD ? W.supply : M.supply;
    need = (kind == IK_WOOD ? W.need : M.need) + (next_tier ? extra : 0);
    if (next_tier) {
        out->have = need > 0 ? IMIN(999, sup * 100 / need) : 100;
        out->need = IND_INPUT_GROW_PCT;
        out->why = 1;                                  /* supply vs the need after the upgrade */
    } else {
        out->have = kind == IK_WOOD ? W.pct : M.pct;
        out->need = IND_INPUT_KEEP_PCT;
    }
    out->met = (u8)(out->have >= out->need);
    return 1;
}

/* ------------------------------------------------------------------ month end */
static void month_forest(void)
{
    int x, y, g, ng, ph = (int)(sim_date()->total_months & 3);
    W.forest_tiles = W.regrowing = W.clearcut = W.forestry_tiles = 0;
    for (y = 0; y < g_world.h; y++)
        for (x = 0; x < g_world.w; x++) {
            Tile *t = WT(x, y);
            if (TER_TYPE(t) != TER_FOREST) continue;
            if (!t->lot) W.forest_tiles++;
            if (TILE_ZONE(t) == ZONE_FORESTRY && !t->lot) W.forestry_tiles++;
            g = FOREST_REGROW(t);
            if (!g) continue;
            if (((x * 7 + y * 13 + ph) & 3) == 0) {        /* one step every 4 months, phase by tile */
                ng = g >= IND_REGROW_STEPS ? 0 : g + 1;
                t->aux = (u8)((t->aux & 15) | (ng << 4));
                if (industry_forest_stage(ng) != industry_forest_stage(g)) world_notify(x, y, x, y, WCHG_FLAGS);
                g = ng;
            }
            if (g) { W.regrowing++; if (g <= 7) W.clearcut++; }
        }
}

static void month_harvest(void)
{
    int i, c = 0, d, x, y, i2, j2;
    s32 forest_need, want, used, cut = 0, cover = 0;
    memset(s_owner, 0, sizeof(s_owner));
    /* coverage: camps in lot order take the uncovered forestry tiles within their radius */
    for (i = 1; i < g_world.lot_hi && c < MAX_CAMPS; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def != B_LOGGING_CAMP || l->status != LOT_ACTIVE) continue;
        c++;
        s_camp_cover[c] = 0;
        for (j2 = l->y - IND_CAMP_RADIUS; j2 <= l->y + IND_CAMP_RADIUS; j2++)
            for (i2 = l->x - IND_CAMP_RADIUS; i2 <= l->x + IND_CAMP_RADIUS; i2++) {
                if (!world_in(i2, j2) || s_owner[TILE_IDX(i2, j2)] || !is_forestry_forest(WT(i2, j2))) continue;
                s_owner[TILE_IDX(i2, j2)] = (u16)c;
                s_camp_cover[c]++;
            }
        cover += s_camp_cover[c];
    }
    W.harvest_tiles = cover;
    W.forestry_cap = cover * IND_TILE_WU / (IND_REGROW_STEPS * 4);
    /* cut the forest part of the need, camp by camp, nearest tiles first */
    forest_need = IMAX(0, W.need - IMIN(W.regional, W.need));
    want = forest_need - W.stock;
    W.cut_tiles_month = 0;
    c = 0;
    for (i = 1; i < g_world.lot_hi && want > 0 && c < MAX_CAMPS; i++) {
        const Lot *l = &g_world.lots[i];
        int quota;
        if (l->def != B_LOGGING_CAMP || l->status != LOT_ACTIVE) continue;
        c++;
        quota = (s_camp_cover[c] * IND_TILE_WU / (IND_REGROW_STEPS * 4) + IND_TILE_WU - 1) / IND_TILE_WU;
        x = l->x; y = l->y;
        for (d = 1; d <= IND_CAMP_RADIUS && quota > 0 && want > 0; d++)
            for (j2 = y - d; j2 <= y + d && quota > 0 && want > 0; j2++)
                for (i2 = x - d; i2 <= x + d && quota > 0 && want > 0; i2++) {
                    Tile *t;
                    if (IMAX(IABS(i2 - x), IABS(j2 - y)) != d || !world_in(i2, j2)) continue;
                    if (s_owner[TILE_IDX(i2, j2)] != c) continue;
                    t = WT(i2, j2);
                    if (FOREST_REGROW(t)) continue;
                    t->aux = (u8)((t->aux & 15) | (1 << 4));
                    world_notify(i2, j2, i2, j2, WCHG_FLAGS);
                    cut += IND_TILE_WU; want -= IND_TILE_WU; quota--;
                    W.cut_tiles_month++;
                }
    }
    W.cut_tiles_year += W.cut_tiles_month;
    W.cut_tiles_total += W.cut_tiles_month;
    W.cut_wu = cut;
    used = IMIN(forest_need, W.stock + cut);
    W.stock = IMIN(WOOD_STOCK_MAX, W.stock + cut - used);
    W.delivered = IMIN(W.regional, W.need) + used;
    W.pct = W.need ? IMIN(100, W.delivered * 100 / W.need) : 100;
    W.supply = W.regional + W.forestry_cap;
}

static void month_farms(void)
{
    int i;
    s32 milk = 0;
    memset(s_claim, 0, sizeof(s_claim));
    for (i = 1; i < g_world.lot_hi; i++) {
        Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        if (l->def == B_NONE) continue;
        d = def_building(l->def);
        if (d->cat != BCAT_ZONE || d->zone != ZONE_FARM || l->status == LOT_ABANDONED) continue;
        claim_for(i);
        if (l->status == LOT_ACTIVE) milk += (l->spare & 0xFF) * d->tier;
    }
    M.farms = milk;
    M.supply = M.regional + M.farms;
}

static void month_exports(void)
{
    static const int w[IK_COUNT] = { 100, 133, 100, 67, 100, 200 };
    int k, year = sim_date()->year;
    K.west_total = K.east_total = 0;
    for (k = 0; k < IK_COUNT; k++) {
        K.export_vol[k] = K.jobs[k] * w[k] / 100;
        K.export_west[k] = K.export_vol[k] * industry_west_pct(k, year) / 100;
        K.export_east[k] = K.export_vol[k] - K.export_west[k];
        K.west_total += K.export_west[k];
        K.east_total += K.export_east[k];
    }
}

void industry_month(void)
{
    world_batch_begin();
    month_forest();
    month_harvest();
    month_farms();
    world_batch_end();
    month_exports();
    if (sim_date()->month == MONTHS_PER_YEAR - 1) W.cut_tiles_year = 0;   /* books run before the date moves on */
}

/* ------------------------------------------------------------------ politics hook */
int industry_clearcut_pct(void) { return W.forest_tiles > 0 ? (int)(W.clearcut * 100 / W.forest_tiles) : 0; }

s32 industry_rural_forest_reason(void)
{
    s32 r = 0;
    int cc = industry_clearcut_pct();
    if (W.camps > 0) r += IMIN(4, 1 + W.camps / 3);              /* winter work in the forest */
    if (cc > 25) r -= IMIN(10, (cc - 25) / 2);                    /* "Clear-cuts: 31% of the forest" */
    return r;
}

const IndWood  *industry_wood(void)  { return &W; }
const IndMilk  *industry_milk(void)  { return &M; }
const IndKinds *industry_kinds(void) { return &K; }

/* ------------------------------------------------------------------ text */
static int kind_reason(int kind, const Site *s, char *buf, int size)
{
    char r[96];
    int n = 0;
    r[0] = 0;
#define ADD(txt) do { n += str_printf(r + n, (int)sizeof(r) - n, "%s%s", n ? ", " : "", txt); } while (0)
    switch (kind) {
    case IK_WOOD:
        if (s->d_water <= 2) ADD("lakeside log pond");
        if (s->d_forestry <= 8) ADD("forestry zone near");
        else if (s->n_forest >= 6) ADD("forest within reach");
        if (!n) ADD("wood supply");
        break;
    case IK_METAL:
        if (s->d_rail <= 4) ADD("by the railway");
        if (s->d_main <= 3) ADD("on the main road");
        if (!n) ADD("the all-round kind (no wood, milk or water power needed)");
        break;
    case IK_TEXTILE:
        if (s->d_river <= 4) ADD("water power from the river");
        else ADD("clearing orders for cloth and shoes");
        break;
    case IK_FOOD:
        if (s->d_farm <= 10) ADD("farms within reach");
        else ADD("milk from the parish farms");
        break;
    case IK_ELEC:
        ADD("good schools and clean air");
        if (s->d_station <= 6) ADD("station near");
        break;
    default:
        ADD("general industry");
        break;
    }
#undef ADD
    return str_printf(buf, size, "%s: %s", industry_kind_name(kind), r);
}

int industry_lot_line(int lot, int k, char *buf, int size)
{
    const Lot *l = world_lot(lot);
    const BuildingDef *d;
    int kind, x0, y0, x1, y1, year = sim_date()->year;
    if (!l) return 0;
    d = def_building(l->def);
    world_lot_rect(lot, &x0, &y0, &x1, &y1);
    if (d->cat == BCAT_ZONE && d->zone == ZONE_FARM) {
        int f = l->spare & 0xFF;
        if (k == 0) return str_printf(buf, size, "Fields: %d claimed, milk %d mu/month (%d per field)", f, f * d->tier, d->tier);
        if (k == 1) return str_printf(buf, size, "A self-employed farm family (Rural). Town milk %d of %d mu needed",
                                      M.supply, M.food_need + M.need);
        if (k == 2 && d->tier == 2) return str_printf(buf, size, "Mechanisation (1962+, power + main road) needs 2 fewer hands");
        return 0;
    }
    if (l->def == B_LOGGING_CAMP) {
        if (k == 0) return str_printf(buf, size, "Forest: %d forestry tiles within %d, %d wu/month in rotation",
                                      W.camps ? W.harvest_tiles / W.camps : 0, IND_CAMP_RADIUS, W.camps ? W.forestry_cap / W.camps : 0);
        if (k == 1) return str_printf(buf, size, "Town: %d tiles cut this year, wood %d of %d wu delivered (%d%%)",
                                      W.cut_tiles_year, W.delivered, W.need, W.pct);
        return 0;
    }
    kind = industry_def_kind(l->def);
    if (kind == IK_NONE) return 0;
    if (k == 0) {
        Site s;
        site_scan(x0, y0, x1, y1, &s);
        return kind_reason(kind, &s, buf, size);
    }
    if (k == 1) {
        if (kind == IK_WOOD)
            return str_printf(buf, size, "Wood %d wu/month; supply %d (forestry %d, regional %d), need %d, %d%% delivered",
                              industry_def_input(l->def, NULL), W.supply, W.forestry_cap, W.regional, W.need, W.pct);
        if (kind == IK_FOOD)
            return str_printf(buf, size, "Milk %d mu/month; supply %d (farms %d, regional %d), dairies need %d",
                              industry_def_input(l->def, NULL), M.supply, M.farms, M.regional, M.need);
        if (kind == IK_ELEC) return str_printf(buf, size, "Needs school cover 50%%+ and clean air (pollution 48 or less)");
        return str_printf(buf, size, "No raw material needed");
    }
    if (k == 2) {
        static const char *const wp[IK_COUNT] = { "", "sawn timber", "machinery", "cloth", "butter", "cables" };
        static const char *const ep[IK_COUNT] = { "", "prefab houses", "machinery", "clothes, shoes", "cheese", "TV sets" };
        int west = industry_west_pct(kind, year);
        return str_printf(buf, size, "Exports: West %d%% (%s), East %d%% (%s)", west, wp[kind], 100 - west, ep[kind]);
    }
    if (k == 3) {
        if (K.short_jobs[kind] > 0)
            return str_printf(buf, size, "Demand for %s %+d jobs; %d more wait for %s (to engineering)", industry_kind_plural(kind),
                              K.demand[kind], K.short_jobs[kind], kind == IK_WOOD ? "wood" : kind == IK_FOOD ? "milk" : "Town status");
        return str_printf(buf, size, "Demand for %s %+d jobs", industry_kind_plural(kind), K.demand[kind]);
    }
    return 0;
}

int industry_tile_line(int x, int y, int k, char *buf, int size)
{
    const Tile *t;
    int g, z;
    if (!world_in(x, y)) return 0;
    t = WT(x, y);
    z = TILE_ZONE(t);
    if (TER_TYPE(t) == TER_FOREST && !t->lot) {
        g = FOREST_REGROW(t);
        if (k == 0) {
            int left = g ? (IND_REGROW_STEPS + 1 - g) * 4 : 0;
            switch (industry_forest_stage(g)) {
            case FST_CUT: return str_printf(buf, size, "Clear-cut, %d of %d: mature in about %d months", g, IND_REGROW_STEPS, left);
            case FST_SEEDLINGS: return str_printf(buf, size, "Seedlings, %d of %d: mature in about %d months", g, IND_REGROW_STEPS, left);
            case FST_YOUNG: return str_printf(buf, size, "Young forest, %d of %d: mature in about %d months", g, IND_REGROW_STEPS, left);
            default: return str_printf(buf, size, z == ZONE_FORESTRY ? "Mature forest, ready for cutting" : "Mature forest");
            }
        }
        if (k == 1 && z == ZONE_FORESTRY) {
            if (camp_near(x, y, IND_CAMP_RADIUS)) return str_printf(buf, size, "Forestry: %d tiles cut this year, wood %d%% delivered", W.cut_tiles_year, W.pct);
            return str_printf(buf, size, "Forestry: no logging camp within %d yet (camps grow by roads)", IND_CAMP_RADIUS);
        }
        return 0;
    }
    if (z == ZONE_FARM && !t->lot) {
        if (k == 0) return str_printf(buf, size, claimed(x, y) ? "Farm field, worked by a farmstead nearby" :
                                      "Farm land: a farmstead grows when the town needs milk");
        if (k == 1) return str_printf(buf, size, "Milk %d of %d mu needed (town %d, dairies %d)", M.supply, M.food_need + M.need,
                                      M.food_need, M.need);
        return 0;
    }
    return 0;
}

int industry_demand_line(int kind, char *buf, int size, s32 *amount)
{
    if (kind <= IK_NONE || kind >= IK_COUNT) return 0;
    *amount = K.demand[kind];
    if (K.short_jobs[kind] > 0)
        return str_printf(buf, size, "%s (%s)", industry_kind_plural(kind),
                          kind == IK_WOOD ? "wood short" : kind == IK_FOOD ? "milk short" : "from 1965, Town");
    return str_printf(buf, size, "%s", industry_kind_plural(kind));
}

/* ------------------------------------------------------------------ debug, hash, save */
void industry_dump(FILE *f)
{
    int k;
    fprintf(f, "industry %d-%02d: kinds (demand / capacity / jobs / lots / short / export W E):\n", sim_date()->year, sim_date()->month + 1);
    for (k = 1; k < IK_COUNT; k++)
        fprintf(f, "  %-12s %+6d %6d %6d %4d %5d %5d %5d\n", industry_kind_plural(k), K.demand[k], K.capacity[k], K.jobs[k],
                K.lots[k], K.short_jobs[k], K.export_west[k], K.export_east[k]);
    fprintf(f, "  wood: need %d (ext %d), supply %d (regional %d, forestry %d), delivered %d (%d%%), cut %d wu, stock %d;"
               " camps %d, forestry tiles %d, harvested %d, cut %d this month / %d this year / %d total; forest %d, regrowing %d,"
               " clear-cut %d (%d%%)\n", W.need, W.need_ext, W.supply, W.regional, W.forestry_cap, W.delivered, W.pct, W.cut_wu,
            W.stock, W.camps, W.forestry_tiles, W.harvest_tiles, W.cut_tiles_month, W.cut_tiles_year, W.cut_tiles_total,
            W.forest_tiles, W.regrowing, W.clearcut, industry_clearcut_pct());
    fprintf(f, "  milk: need %d, supply %d (regional %d, farms %d), food %d, farm demand %d; farms %d (T1/T2/T3 %d/%d/%d),"
               " fields %d, residents %d\n", M.need, M.supply, M.regional, M.farms, M.food_need, M.farm_demand, M.farm_lots,
            M.farm_tier[1], M.farm_tier[2], M.farm_tier[3], M.fields, M.farm_residents);
}

u32 industry_hash(void)
{
    u32 h = HASH_INIT;
    h = hash_bytes(h, &W, sizeof(W));
    h = hash_bytes(h, &M, sizeof(M));
    h = hash_bytes(h, K.demand, sizeof(K.demand));
    h = hash_bytes(h, s_claim, sizeof(s_claim));
    return h;
}

int industry_save(struct SaveFile *sf)
{
    save_begin_chunk(sf, "INDU", 1);
    save_u32(sf, sizeof(W)); save_bytes(sf, &W, sizeof(W));
    save_u32(sf, sizeof(M)); save_bytes(sf, &M, sizeof(M));
    save_u32(sf, sizeof(K)); save_bytes(sf, &K, sizeof(K));
    save_bytes(sf, s_ext_wood, sizeof(s_ext_wood));
    save_s32(sf, s_last_pick); save_s32(sf, s_transport_float);
    save_rle(sf, s_claim, sizeof(s_claim));
    save_end_chunk(sf);
    return 1;
}

int industry_load(struct SaveFile *sf)
{
    int ver;
    if (!load_find_chunk(sf, "INDU", &ver)) return 0;
    if (load_u32(sf) != sizeof(W)) return 0; load_bytes(sf, &W, sizeof(W));
    if (load_u32(sf) != sizeof(M)) return 0; load_bytes(sf, &M, sizeof(M));
    if (load_u32(sf) != sizeof(K)) return 0; load_bytes(sf, &K, sizeof(K));
    load_bytes(sf, s_ext_wood, sizeof(s_ext_wood));
    s_last_pick = load_s32(sf); s_transport_float = load_s32(sf);
    load_rle(sf, s_claim, sizeof(s_claim));
    return load_ok(sf);
}

void industry_after_load(int had_chunk)
{
    int i;
    if (!had_chunk) {
        /* old save (Milestone 1 / before M3): generic industry lots become the kinds their art shows
           (ART.md 3.1 aliases), then the numbers are recomputed */
        for (i = 1; i < g_world.lot_hi; i++) {
            Lot *l = &g_world.lots[i];
            int nd = B_NONE, var = l->variant;
            switch (l->def) {
            case B_IND_T1_3X3: nd = B_IND_WOOD_T1_3X3; break;
            case B_IND_T1_2X2: nd = l->variant == 0 ? B_IND_WOOD_T1_2X2 : B_IND_METAL_T1_2X2; var = 0; break;
            case B_IND_T2_2X2: nd = B_IND_METAL_T2_2X2; break;
            case B_IND_T2_3X3: nd = B_IND_TEXTILE_T2_3X3; break;
            case B_IND_T3_3X3: nd = B_IND_METAL_T3_3X3; var = 0; break;
            case B_IND_T3_2X2: nd = B_IND_ELEC_T2_2X2; var = 0; break;
            default: break;
            }
            if (nd == B_NONE) continue;
            world_lot_set_def(i, nd);
            world_lot_set_variant(i, IMIN(var, IMAX(0, def_building(nd)->variants - 1)));
        }
        industry_reset();
        industry_cycle();
    }
}
