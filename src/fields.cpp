/* fields.cpp - pollution, land value, fire risk, happiness on the 2x2-tile cell grid. PORTABLE.
   Owner: T04. See fields.h. Integer-only, deterministic. Formulas: DESIGN 5.7, DESIGN_LESSONS 4.6. */
#include "common.h"
#include "fields.h"
#include "world.h"
#include "net.h"
#include "budget.h"
#include "sim.h"
#include "save.h"
#include "politics.h"
#include "complex.h"
#include "traffic.h"
#include "civic.h"

#define NCELLS      (FIELD_MAX_W * FIELD_MAX_H)
#define CELL_IDX(cx, cy) ((cy) * FIELD_MAX_W + (cx))
#define POL_FIX     16           /* pollution is kept x16 internally */

static u8  s_f[FLD_COUNT][NCELLS];
static u16 s_pol[NCELLS], s_ptmp[NCELLS];
static u16 s_emit[NCELLS];
static s8  s_base[NCELLS];       /* terrain part of land value */
static u8  s_lake[NCELLS], s_fedge[NCELLS], s_rockc[NCELLS];   /* explain: terrain components */
static s16 s_lotlv[NCELLS];      /* placed buildings' land value effects (parks +, plants -) */
static u8  s_aband[NCELLS];      /* abandoned lot tiles in the cell */
static u8  s_wood[NCELLS];       /* wooden building tiles in the cell */
static u8  s_forest[NCELLS];     /* forest tiles in the cell */
static u16 s_roadpol[NCELLS];    /* sum of road classes' pollution in the cell (x flow later) */
static int s_avg[FLD_COUNT];
static int s_dirty_terrain, s_dirty_lots;  /* terrain: 1 = whole map, 2 = only cells in s_tcell (T13) */
static u8  s_tcell[NCELLS];                 /* T13: terrain cells to recompute */
static int s_tcx0, s_tcy0, s_tcx1, s_tcy1;  /* bounding box of the s_tcell marks */
static int s_pol_runs;
static u16 s_rescell[NCELLS];               /* M5: residents per cell (crime), rebuilt at each social step */

static int cells_w(void) { return (g_world.w + 1) >> FIELD_SHIFT; }
static int cells_h(void) { return (g_world.h + 1) >> FIELD_SHIFT; }

void fields_init(void) { fields_reset(); }

void fields_reset(void)
{
    memset(s_f, 0, sizeof(s_f));
    memset(s_pol, 0, sizeof(s_pol));
    memset(s_lotlv, 0, sizeof(s_lotlv));
    memset(s_aband, 0, sizeof(s_aband));
    memset(s_wood, 0, sizeof(s_wood));
    memset(s_avg, 0, sizeof(s_avg));
    memset(s_f[FLD_HAPPINESS], 50, sizeof(s_f[FLD_HAPPINESS]));
    s_dirty_terrain = 1; s_dirty_lots = 1;
    s_pol_runs = 0;
}

void fields_mark_dirty(int k)
{
    if (k & (WCHG_TERRAIN | WCHG_ROAD)) s_dirty_terrain = 1;
    if (k & WCHG_LOT) s_dirty_lots = 1;
}

void fields_mark_dirty_rect(int k, int x0, int y0, int x1, int y1)
{
    int cx0, cy0, cx1, cy1, cx, cy, cw = (g_world.w + 1) >> FIELD_SHIFT, ch = (g_world.h + 1) >> FIELD_SHIFT;
    if (k & WCHG_LOT) s_dirty_lots = 1;
    if (!(k & (WCHG_TERRAIN | WCHG_ROAD)) || s_dirty_terrain == 1) return;
    /* cell c reads tiles 2c-2 .. 2c+3 (recalc_terrain), so tile t affects cells (t-3)/2 .. (t+2)/2 */
    cx0 = IMAX(0, FLOORDIV(x0 - 3, 2)); cx1 = IMIN(cw - 1, (x1 + 2) >> 1);
    cy0 = IMAX(0, FLOORDIV(y0 - 3, 2)); cy1 = IMIN(ch - 1, (y1 + 2) >> 1);
    if (cx0 > cx1 || cy0 > cy1) return;
    if ((cx1 - cx0 + 1) * (cy1 - cy0 + 1) * 4 > cw * ch) { s_dirty_terrain = 1; return; }   /* big: all */
    for (cy = cy0; cy <= cy1; cy++)
        for (cx = cx0; cx <= cx1; cx++) s_tcell[CELL_IDX(cx, cy)] = 1;
    if (s_dirty_terrain != 2) { s_tcx0 = cx0; s_tcy0 = cy0; s_tcx1 = cx1; s_tcy1 = cy1; s_dirty_terrain = 2; }
    else { s_tcx0 = IMIN(s_tcx0, cx0); s_tcy0 = IMIN(s_tcy0, cy0); s_tcx1 = IMAX(s_tcx1, cx1); s_tcy1 = IMAX(s_tcy1, cy1); }
}

int field_cell(int f, int cx, int cy)
{
    if (f < 0 || f >= FLD_COUNT || cx < 0 || cy < 0 || cx >= FIELD_MAX_W || cy >= FIELD_MAX_H) return 0;
    return s_f[f][CELL_IDX(cx, cy)];
}
int field_at(int f, int x, int y) { return world_in(x, y) ? field_cell(f, x >> FIELD_SHIFT, y >> FIELD_SHIFT) : 0; }
const u8 *field_data(int f) { return s_f[ICLAMP(f, 0, FLD_COUNT - 1)]; }
u8 *field_data_rw(int f) { return s_f[ICLAMP(f, 0, FLD_COUNT - 1)]; }
int field_avg(int f) { return (f >= 0 && f < FLD_COUNT) ? s_avg[f] : 0; }
int field_happiness_at(int x, int y) { return field_at(FLD_HAPPINESS, x, y); }

static void update_avg(int f)
{
    int cx, cy, cw = cells_w(), ch = cells_h();
    s32 sum = 0;
    for (cy = 0; cy < ch; cy++)
        for (cx = 0; cx < cw; cx++) sum += s_f[f][CELL_IDX(cx, cy)];
    s_avg[f] = (int)(sum / IMAX(1, cw * ch));
}

/* ------------------------------------------------------------------ terrain base + lot influences */
static void recalc_terrain(void)
{
    int cx, cy, cw = cells_w(), ch = cells_h(), i, j, part = s_dirty_terrain == 2;
    int ux0 = part ? s_tcx0 : 0, uy0 = part ? s_tcy0 : 0, ux1 = part ? s_tcx1 : cw - 1, uy1 = part ? s_tcy1 : ch - 1;
    for (cy = uy0; cy <= uy1; cy++)
        for (cx = ux0; cx <= ux1; cx++) {
            int c = CELL_IDX(cx, cy), water = 0, forest = 0, rock = 0, land = 0, ftiles = 0;
            if (part) { if (!s_tcell[c]) continue; s_tcell[c] = 0; }
            for (j = cy * 2 - 2; j <= cy * 2 + 3; j++)          /* the cell + 2 tiles around */
                for (i = cx * 2 - 2; i <= cx * 2 + 3; i++) {
                    const Tile *t;
                    int ter;
                    if (!world_in(i, j)) continue;
                    t = WT(i, j);
                    ter = TER_TYPE(t);
                    if (TER_IS_WATER(ter)) water++;
                    else if (ter == TER_FOREST) forest++;
                    else if (ter == TER_ROCK) rock++;
                    if (i >> 1 == cx && j >> 1 == cy) {
                        if (!TER_IS_WATER(ter)) land++;
                        if (ter == TER_FOREST) ftiles++;
                    }
                }
            s_lake[c] = (u8)(water && land ? 20 : 0);
            s_fedge[c] = (u8)(forest && land && forest < 30 ? 5 : 0);
            s_rockc[c] = (u8)(rock && land ? 5 : 0);
            s_base[c] = (s8)(15 + s_lake[c] + s_fedge[c] + s_rockc[c]);
            s_forest[c] = (u8)ftiles;
            {
                int rp = 0, a, b;
                for (b = cy * 2; b <= cy * 2 + 1; b++)
                    for (a = cx * 2; a <= cx * 2 + 1; a++)
                        if (world_in(a, b)) rp += g_roads[WT(a, b)->road & TR_CLASS_MASK].pollution;
                s_roadpol[c] = (u16)rp;
            }
        }
    if (!part) memset(s_tcell, 0, sizeof(s_tcell));
    s_dirty_terrain = 0;
}

static void recalc_lots(void)
{
    int i, cw = cells_w(), ch = cells_h();
    memset(s_lotlv, 0, sizeof(s_lotlv));
    memset(s_aband, 0, sizeof(s_aband));
    memset(s_wood, 0, sizeof(s_wood));
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        int x0, y0, x1, y1, x, y;
        if (l->def == B_NONE) continue;
        d = def_building(l->def);
        world_lot_rect(i, &x0, &y0, &x1, &y1);
        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++) {
                int c = CELL_IDX(x >> FIELD_SHIFT, y >> FIELD_SHIFT);
                if (l->status == LOT_ABANDONED && s_aband[c] < 255) s_aband[c]++;
                if ((d->flags & BDF_WOODEN) && l->status != LOT_BUILDING && s_wood[c] < 255) s_wood[c]++;
            }
        if (d->land_value && d->lv_radius && l->status == LOT_ACTIVE) {
            /* linear falloff over lv_radius tiles from the footprint centre, on cells */
            int ccx = ((x0 + x1) >> 1) >> FIELD_SHIFT, ccy = ((y0 + y1) >> 1) >> FIELD_SHIFT;
            int rc = (d->lv_radius + 1) >> FIELD_SHIFT, cx, cy;
            for (cy = ccy - rc; cy <= ccy + rc; cy++)
                for (cx = ccx - rc; cx <= ccx + rc; cx++) {
                    int dist, v;
                    if (cx < 0 || cy < 0 || cx >= cw || cy >= ch) continue;
                    dist = IMAX(IABS(cx - ccx), IABS(cy - ccy));
                    v = d->land_value * (rc + 1 - dist) / (rc + 1);
                    s_lotlv[CELL_IDX(cx, cy)] = (s16)ICLAMP(s_lotlv[CELL_IDX(cx, cy)] + v, -60, 60);
                }
        }
    }
    s_dirty_lots = 0;
}

/* ------------------------------------------------------------------ pollution (phase 10) */
void fields_step_pollution(void)
{
    int i, x, y, cw = cells_w(), ch = cells_h(), changed = 0;
    for (y = 0; y < ch; y++) memset(&s_emit[CELL_IDX(0, y)], 0, cw * sizeof(u16));
    /* emit: lots (pollution per month -> /4 per cycle, x16 fixed, spread over the footprint) */
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        int x0, y0, x1, y1, per;
        if (l->def == B_NONE || l->status != LOT_ACTIVE) continue;
        d = def_building(l->def);
        if (CPX_DEF_USE(l->def, pollution, CPXU_POLLUTION) <= 0) continue;   /* M6: complex modules per stage */
        world_lot_rect(i, &x0, &y0, &x1, &y1);
        per = CPX_DEF_USE(l->def, pollution, CPXU_POLLUTION) * POL_FIX / 4 / ((x1 - x0 + 1) * (y1 - y0 + 1));
        if (d->cat == BCAT_ZONE && d->zone == ZONE_INDUSTRY) per = POL_PCT(per, politics_mod(PM_IND_POLLUTION_PCT));   /* M1 */
        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++) {
                int c = CELL_IDX(x >> FIELD_SHIFT, y >> FIELD_SHIFT);
                s_emit[c] = (u16)IMIN(60000, s_emit[c] + per);
            }
    }
    /* roads: dust/exhaust per cell, scaled by the cell's traffic level (FLD_TRAFFIC, 0..255) */
    if (s_dirty_terrain) recalc_terrain();
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            int c = CELL_IDX(x, y);
            if (s_roadpol[c])
                s_emit[c] = (u16)IMIN(60000, s_emit[c] + s_roadpol[c] * POL_FIX * (32 + s_f[FLD_TRAFFIC][c]) / (32 * 4));
        }
    /* add + decay x7/8 */
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            int c = CELL_IDX(x, y);
            s32 v = (s32)s_pol[c] + s_emit[c];
            v -= v >> 3;
            s_pol[c] = (u16)IMIN(v, 255 * POL_FIX * 2);
        }
    /* 3x3 cross blur: centre 4/8, edges 1/8 (outside = 0: pollution leaves the map) */
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            int c = CELL_IDX(x, y);
            s32 v = 4 * (s32)s_pol[c];
            if (x > 0) v += s_pol[c - 1];
            if (x < cw - 1) v += s_pol[c + 1];
            if (y > 0) v += s_pol[c - FIELD_MAX_W];
            if (y < ch - 1) v += s_pol[c + FIELD_MAX_W];
            s_ptmp[c] = (u16)(v >> 3);
        }
    /* prevailing SW wind: every other cycle 1/4 of each cell comes from its SW (+y) neighbour */
    s_pol_runs++;
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            int c = CELL_IDX(x, y);
            if (s_pol_runs & 1) {
                s32 from = y < ch - 1 ? s_ptmp[c + FIELD_MAX_W] : 0;
                s_pol[c] = (u16)((3 * (s32)s_ptmp[c] + from) >> 2);
            } else s_pol[c] = s_ptmp[c];
        }
    for (y = 0; y < ch; y++)
        for (x = 0; x < cw; x++) {
            int c = CELL_IDX(x, y), v = IMIN(255, s_pol[c] / POL_FIX);
            if (s_f[FLD_POLLUTION][c] != v) { s_f[FLD_POLLUTION][c] = (u8)v; changed = 1; }
        }
    update_avg(FLD_POLLUTION);
    if (changed) world_notify_all(WCHG_FIELDS);
}

/* ------------------------------------------------------------------ land value (phase 11) */
static const u8 s_svc_lv_w[SVC_COUNT] = { 4, 10, 8, 4, 4, 4, 2 };   /* fire school health sauna police lib post */

static int lv_terms(int cx, int cy, Term *terms, int max)
{
    int c = CELL_IDX(cx, cy), tx = cx << FIELD_SHIFT, ty = cy << FIELD_SHIFT, n = 0, k, svc = 0, parks;
    int access = 0, ab = 0, i, j, jd;
    if (n < max) { terms[n].id = LVT_BASE; terms[n].amount = 15; n++; }
    if (s_lake[c] && n < max) { terms[n].id = LVT_LAKESIDE; terms[n].amount = s_lake[c]; n++; }
    if (s_fedge[c] && n < max) { terms[n].id = LVT_FOREST_EDGE; terms[n].amount = s_fedge[c]; n++; }
    if (s_rockc[c] && n < max) { terms[n].id = LVT_ROCK; terms[n].amount = s_rockc[c]; n++; }
    for (k = 0; k < SVC_COUNT; k++)
        svc += net_cov_quality(k, tx, ty) * (k == SVC_SCHOOL ? POL_PCT(s_svc_lv_w[k], politics_mod(PM_SCHOOL_LV_PCT)) : s_svc_lv_w[k]);  /* M1 */
    svc = IMIN(30, svc / 100);
    if (svc && n < max) { terms[n].id = LVT_SERVICES; terms[n].amount = svc; n++; }
    parks = ICLAMP(s_lotlv[c], -30, 20);
    if (parks && n < max) { terms[n].id = LVT_PARKS; terms[n].amount = parks; n++; }
    if (net_road_reach(tx, ty) <= ZONE_ROAD_REACH || net_road_reach(tx + 1, ty + 1) <= ZONE_ROAD_REACH) access += 8;
    jd = traffic_commute(tx, ty);                        /* M2: congested commute (walk tiles) */
    if (jd != 255) access += jd <= 20 ? 7 : jd <= 40 ? 4 : 1;
    if (access && n < max) { terms[n].id = LVT_ACCESS; terms[n].amount = access; n++; }
    if (s_f[FLD_POLLUTION][c] >= 4 && n < max) { terms[n].id = LVT_POLLUTION; terms[n].amount = -(s_f[FLD_POLLUTION][c] / 4); n++; }
    if (s_f[FLD_CRIME][c] >= 4 && n < max) { terms[n].id = LVT_CRIME; terms[n].amount = -(s_f[FLD_CRIME][c] / 4); n++; }
    if (s_f[FLD_NOISE][c] >= 4 && n < max) { terms[n].id = LVT_NOISE; terms[n].amount = -(s_f[FLD_NOISE][c] / 4); n++; }
    for (j = cy - 1; j <= cy + 1; j++)
        for (i = cx - 1; i <= cx + 1; i++)
            if (i >= 0 && j >= 0 && i < FIELD_MAX_W && j < FIELD_MAX_H) ab += s_aband[CELL_IDX(i, j)];
    if (ab && n < max) { terms[n].id = LVT_ABANDONED; terms[n].amount = -IMIN(30, ab * 3); n++; }
    ab = civic_lv_lake_term(cx, cy);                     /* M5: polluted lake nearby */
    if (ab && n < max) { terms[n].id = LVT_LAKE; terms[n].amount = ab; n++; }
    return n;
}

void fields_step_land_value(int quarter)
{
    int cx, cy, cw = cells_w(), ch = cells_h(), y0, y1, changed = 0, k, n;
    Term terms[MAX_TERMS];
    if (s_dirty_terrain) recalc_terrain();
    if (s_dirty_lots) recalc_lots();
    quarter &= 3;
    y0 = quarter * ch / 4; y1 = (quarter + 1) * ch / 4;
    for (cy = y0; cy < y1; cy++)
        for (cx = 0; cx < cw; cx++) {
            int c = CELL_IDX(cx, cy), v = 0, old = s_f[FLD_LAND_VALUE][c], nv;
            n = lv_terms(cx, cy, terms, MAX_TERMS);
            for (k = 0; k < n; k++) v += terms[k].amount;
            v = ICLAMP(v, 0, 255);
            nv = (3 * old + v + 2) >> 2;
            if (nv == old && v != old) nv += v > old ? 1 : -1;    /* converge despite rounding */
            if (nv > old && politics_mod(PM_LV_GROWTH_PCT) != 100)  /* M1: rent regulation slows rises */
                nv = old + IMAX(1, (nv - old) * politics_mod(PM_LV_GROWTH_PCT) / 100);
            if (nv != old) {
                int i, j;
                s_f[FLD_LAND_VALUE][c] = (u8)nv;
                for (j = cy << FIELD_SHIFT; j <= (cy << FIELD_SHIFT) + 1; j++)
                    for (i = cx << FIELD_SHIFT; i <= (cx << FIELD_SHIFT) + 1; i++)
                        if (world_in(i, j)) WT(i, j)->lv = (u8)nv;
                changed = 1;
            }
        }
    update_avg(FLD_LAND_VALUE);
    if (changed) world_notify(0, y0 << FIELD_SHIFT, g_world.w - 1, (y1 << FIELD_SHIFT) - 1, WCHG_FIELDS);
}

int fields_explain_land_value(int x, int y, Term *terms, int max)
{
    if (!world_in(x, y)) return 0;
    if (s_dirty_terrain) recalc_terrain();
    return lv_terms(x >> FIELD_SHIFT, y >> FIELD_SHIFT, terms, max);
}

/* ------------------------------------------------------------------ fire risk, crime, happiness (phase 12) */
/* M5: residents per cell, each lot's occupants spread over its footprint tiles */
static void residents_per_cell(void)
{
    int i;
    memset(s_rescell, 0, sizeof(s_rescell));
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        int x0, y0, x1, y1, x, y, per;
        if (l->def == B_NONE || l->status != LOT_ACTIVE || !l->occupants) continue;
        d = def_building(l->def);
        if (d->cat != BCAT_ZONE || (d->zone != ZONE_HOUSES && d->zone != ZONE_APARTMENTS)) continue;
        world_lot_rect(i, &x0, &y0, &x1, &y1);
        per = l->occupants / ((x1 - x0 + 1) * (y1 - y0 + 1));
        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++) {
                int c = CELL_IDX(x >> FIELD_SHIFT, y >> FIELD_SHIFT);
                s_rescell[c] = (u16)IMIN(60000, s_rescell[c] + per);
            }
    }
}

/* M5 crime target of a cell (DESIGN 5.7): density (3x3 mean of residents per cell) / 4, raised by unemployment and
   low land value, scaled up with the town's size (a village has its constable), cut by police cover (up to 75%),
   x politics PM_CRIME_PCT. ~5-10 in house streets, 15-25 in lamellitalo blocks, 60-80 in unpoliced point blocks. */
static int crime_target(int cx, int cy, int unemp_pct, int scale)
{
    int i, j, sum = 0, base, lv = s_f[FLD_LAND_VALUE][CELL_IDX(cx, cy)], q;
    for (j = cy - 1; j <= cy + 1; j++)
        for (i = cx - 1; i <= cx + 1; i++)
            if (i >= 0 && j >= 0 && i < FIELD_MAX_W && j < FIELD_MAX_H) sum += s_rescell[CELL_IDX(i, j)];
    if (!sum) return 0;
    base = sum / 9 / 4;
    base += base * unemp_pct / 50;
    if (lv < 30) base += (30 - lv) / 3;
    base = base * scale / 100;
    q = net_cov_quality(SVC_POLICE, cx << FIELD_SHIFT, cy << FIELD_SHIFT);
    base = base * (100 - q * 3 / 4) / 100;
    base = POL_PCT(base, politics_mod(PM_CRIME_PCT));
    return ICLAMP(base, 0, 255);
}

void fields_step_social(int quarter)
{
    int cx, cy, cw = cells_w(), ch = cells_h(), y0, y1, changed = 0, month = sim_date()->month;
    int dry = (month >= 5 && month <= 7) ? 3 : 2;           /* June-August: x1.5 */
    int taxpen = IMAX(0, g_budget.tax_res - 9) * 2;
    const CityStats *cs = sim_stats();
    int unemp_pct = cs->workers > 0 ? (int)((s64)cs->unemployed * 100 / cs->workers) : 0;
    int cscale = IMIN(100, 30 + cs->population / 60);       /* M5: crime grows with the town */
    if (s_dirty_terrain) recalc_terrain();
    if (s_dirty_lots) recalc_lots();
    residents_per_cell();
    quarter &= 3;
    y0 = quarter * ch / 4; y1 = (quarter + 1) * ch / 4;
    for (cy = y0; cy < y1; cy++)
        for (cx = 0; cx < cw; cx++) {
            int c = CELL_IDX(cx, cy), tx = cx << FIELD_SHIFT, ty = cy << FIELD_SHIFT;
            int risk, fq, h, svc, jd, cr, oc;
            /* fire risk: wooden density x (1 - fire coverage) x dry summer */
            risk = s_wood[c] * 40 + s_forest[c] * 12;
            fq = net_cov_quality(SVC_FIRE, tx, ty);
            risk = risk * (100 - fq) / 100 * dry / 2;
            risk = IMIN(255, risk);
            /* M5: crime moves 1/4 of the way to its target per update */
            oc = s_f[FLD_CRIME][c];
            cr = crime_target(cx, cy, unemp_pct, cscale);
            cr = oc + (cr > oc ? (cr - oc + 3) / 4 : -((oc - cr + 3) / 4));
            if (cr != oc) { s_f[FLD_CRIME][c] = (u8)cr; changed = 1; }
            /* happiness 0..100 */
            svc = (net_cov_quality(SVC_SCHOOL, tx, ty) + net_cov_quality(SVC_HEALTH, tx, ty) +
                   net_cov_quality(SVC_SAUNA, tx, ty) + net_cov_quality(SVC_FIRE, tx, ty)) / 20;   /* 0..20 */
            jd = traffic_commute(tx, ty);                        /* M2: congested commute (walk tiles) */
            h = 40 + svc + s_f[FLD_LAND_VALUE][c] / 5 - s_f[FLD_POLLUTION][c] / 3 - s_f[FLD_CRIME][c] / 4 - taxpen
                + politics_mod(PM_HAPPY_ADD);                     /* M1: decrees (0 when off) */
            if (jd != 255 && jd > 20) h -= IMIN(15, (jd - 20) / 2);
            h += civic_happy_lake_term(cx, cy);                   /* M5: polluted lake nearby */
            h = ICLAMP(h, 0, 100);
            if (s_f[FLD_FIRE_RISK][c] != risk || s_f[FLD_HAPPINESS][c] != h) changed = 1;
            s_f[FLD_FIRE_RISK][c] = (u8)risk;
            s_f[FLD_HAPPINESS][c] = (u8)h;
        }
    update_avg(FLD_FIRE_RISK);
    update_avg(FLD_HAPPINESS);
    update_avg(FLD_CRIME);
    if (changed) world_notify(0, y0 << FIELD_SHIFT, g_world.w - 1, (y1 << FIELD_SHIFT) - 1, WCHG_FIELDS);
}

/* ------------------------------------------------------------------ save */
int fields_save(struct SaveFile *sf)
{
    save_begin_chunk(sf, "FLDS", 1);
    save_u32(sf, (u32)s_pol_runs);
    save_rle(sf, s_f, sizeof(s_f));
    save_rle(sf, s_pol, sizeof(s_pol));
    save_end_chunk(sf);
    return 1;
}

int fields_load(struct SaveFile *sf)
{
    int ver;
    if (!load_find_chunk(sf, "FLDS", &ver)) return 0;
    s_pol_runs = (int)load_u32(sf);
    load_rle(sf, s_f, sizeof(s_f));
    load_rle(sf, s_pol, sizeof(s_pol));
    s_dirty_terrain = 1; s_dirty_lots = 1;
    return load_ok(sf);
}
