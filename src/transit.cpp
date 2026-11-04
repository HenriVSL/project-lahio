/* transit.cpp - bus lines, stops, bus depot rule, railway station. PORTABLE, deterministic. Owner: M2.
   See transit.h. traffic.cpp reads the lines at each pass start (mode split, bus volume on the roads) and
   writes the ridership back; the cosmetic buses are lifebus.cpp (life side). */
#include "common.h"
#include "transit.h"
#include "traffic.h"
#include "tools.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "budget.h"
#include "text.h"
#include "util.h"
#include "save.h"

#define NTILES (MAP_MAX_W * MAP_MAX_H)
#define MAX_STATIONS 4

static BusLine s_lines[BUS_MAX_LINES];
static u32 s_serial = 1;
static int s_reason;
static int s_depots, s_nst;
static u16 s_st_lot[MAX_STATIONS];
static int s_edit = -1, s_tool_on;
/* BFS scratch (stamped, never cleared per search) */
static u16 s_bstamp[NTILES];
static u8  s_bpred[NTILES];
static u16 s_bq[NTILES];
static u16 s_stampv;
/* tool preview cache */
static int s_pv_x = -1, s_pv_y = -1, s_pv_edit = -2, s_pv_n;
static u32 s_pv_serial, s_pv_wserial;
static u8  s_pv_px[BUS_MAX_PATH], s_pv_py[BUS_MAX_PATH];

void transit_init(void) { transit_reset(); }

void transit_reset(void)
{
    memset(s_lines, 0, sizeof(s_lines));
    s_serial++;
    s_reason = 0; s_depots = 0; s_nst = 0; s_edit = -1;
    s_pv_x = -1;
}

const BusLine *transit_line(int i)
{
    static BusLine none;
    return (i >= 0 && i < BUS_MAX_LINES) ? &s_lines[i] : &none;
}

int transit_line_count(void)
{
    int i, n = 0;
    for (i = 0; i < BUS_MAX_LINES; i++) if (s_lines[i].used) n++;
    return n;
}

u32 transit_serial(void) { return s_serial; }
int transit_last_reason(void) { return s_reason; }
int transit_depots(void) { return s_depots; }
int transit_stations(void) { return s_nst; }
int transit_station_lot(int i) { return (i >= 0 && i < s_nst) ? s_st_lot[i] : 0; }

int transit_active_lines(void)
{
    int i, n = 0;
    for (i = 0; i < BUS_MAX_LINES; i++) if (s_lines[i].used && s_lines[i].status == BLS_OK) n++;
    return n;
}

s32 transit_total_riders(void)
{
    int i;
    s32 n = 0;
    for (i = 0; i < BUS_MAX_LINES; i++) if (s_lines[i].used) n += s_lines[i].riders;
    return n;
}

Money transit_upkeep(void)
{
    int i;
    Money m = 0;
    for (i = 0; i < BUS_MAX_LINES; i++) if (s_lines[i].used && s_lines[i].status == BLS_OK) m += (Money)s_lines[i].buses * BUS_UPKEEP;
    return m;
}

void transit_set_riders(int line, s32 riders)
{
    if (line >= 0 && line < BUS_MAX_LINES) s_lines[line].riders = riders;
}

int transit_tile_lines(int x, int y)
{
    int i, p, m = 0;
    for (i = 0; i < BUS_MAX_LINES; i++) {
        const BusLine *l = &s_lines[i];
        if (!l->used) continue;
        for (p = 0; p < l->npath; p++) if (l->px[p] == x && l->py[p] == y) { m |= 1 << i; break; }
    }
    return m;
}

int transit_stop_line_at(int x, int y)
{
    int i, s;
    for (i = 0; i < BUS_MAX_LINES; i++)
        if (s_lines[i].used)
            for (s = 0; s < s_lines[i].nstops; s++) if (s_lines[i].sx[s] == x && s_lines[i].sy[s] == y) return i;
    return -1;
}

/* ------------------------------------------------------------------ road paths (BFS, fewest tiles) */
static int stop_road(int x, int y)
{
    int c;
    if (!world_in(x, y)) return 0;
    c = TILE_ROAD(WT(x, y));
    return c && (g_roads[c].flags & RDF_CARS) && (g_roads[c].flags & RDF_ZONE_ACCESS);
}

int transit_path(int x0, int y0, int x1, int y1, u8 *px, u8 *py, int max)
{
    int head = 0, tail = 0, d, t, n, goal, start;
    if (!traffic_capacity(x0, y0) || !traffic_capacity(x1, y1)) return 0;
    start = TILE_IDX(x0, y0); goal = TILE_IDX(x1, y1);
    if (++s_stampv == 0) { memset(s_bstamp, 0, sizeof(s_bstamp)); s_stampv = 1; }
    s_bstamp[start] = s_stampv; s_bpred[start] = 0xFF;
    s_bq[tail++] = (u16)start;
    while (head < tail && s_bstamp[goal] != s_stampv) {
        int c = s_bq[head++], cx = c & (MAP_STRIDE - 1), cy = c >> MAP_SHIFT;
        for (d = 0; d < DIR_COUNT; d++) {
            int nx = cx + g_dir_dx[d], ny = cy + g_dir_dy[d], ni;
            if (!traffic_link_ok(cx, cy, d)) continue;
            ni = TILE_IDX(nx, ny);
            if (s_bstamp[ni] == s_stampv) continue;
            s_bstamp[ni] = s_stampv; s_bpred[ni] = (u8)DIR_OPPOSITE(d);
            s_bq[tail++] = (u16)ni;
        }
    }
    if (s_bstamp[goal] != s_stampv) return 0;
    for (n = 0, t = goal; ; n++) {                         /* length first */
        if (s_bpred[t] == 0xFF) break;
        t = TILE_IDX((t & (MAP_STRIDE - 1)) + g_dir_dx[s_bpred[t]], (t >> MAP_SHIFT) + g_dir_dy[s_bpred[t]]);
    }
    n++;
    if (n > max) return 0;
    for (d = n - 1, t = goal; d >= 0; d--) {
        px[d] = (u8)(t & (MAP_STRIDE - 1)); py[d] = (u8)(t >> MAP_SHIFT);
        if (s_bpred[t] == 0xFF) break;
        t = TILE_IDX((t & (MAP_STRIDE - 1)) + g_dir_dx[s_bpred[t]], (t >> MAP_SHIFT) + g_dir_dy[s_bpred[t]]);
    }
    return n;
}

/* whole route from the stops; 0 = a segment has no route (or is too long) */
static int route_line(BusLine *l)
{
    static u8 sx[BUS_MAX_PATH], sy[BUS_MAX_PATH];
    int s, n, k;
    l->npath = 0;
    if (l->nstops < 1) return 0;
    l->px[0] = l->sx[0]; l->py[0] = l->sy[0]; l->npath = 1; l->spos[0] = 0;
    for (s = 1; s < l->nstops; s++) {
        n = transit_path(l->sx[s - 1], l->sy[s - 1], l->sx[s], l->sy[s], sx, sy, BUS_MAX_PATH);
        if (!n || l->npath + n - 1 > BUS_MAX_PATH) { l->npath = 0; return 0; }
        for (k = 1; k < n; k++) { l->px[l->npath] = sx[k]; l->py[l->npath] = sy[k]; l->npath++; }
        l->spos[s] = (u16)(l->npath - 1);
    }
    return 1;
}

static int route_valid(const BusLine *l)
{
    int p, d;
    if (l->npath < 1) return 0;
    for (p = 0; p < l->npath; p++) {
        if (!traffic_capacity(l->px[p], l->py[p])) return 0;
        if (p + 1 < l->npath) {
            for (d = 0; d < DIR_COUNT; d++)
                if (l->px[p] + g_dir_dx[d] == l->px[p + 1] && l->py[p] + g_dir_dy[d] == l->py[p + 1]) break;
            if (d == DIR_COUNT || !traffic_link_ok(l->px[p], l->py[p], d)) return 0;
        }
    }
    return 1;
}

static void line_status(BusLine *l)
{
    int ok = l->npath > 0;
    if (l->nstops < 2) l->status = BLS_ONE_STOP;
    else if (!ok) l->status = BLS_BROKEN;
    else if (!s_depots) l->status = BLS_NO_DEPOT;
    else l->status = BLS_OK;
    l->buses = (u16)(l->nstops >= 2 && ok ? IMIN(BUS_MAX_BUSES, 1 + l->npath / BUS_TILES_PER_BUS) : 0);
}

static int count_depots(void)
{
    int i, n = 0;
    for (i = 1; i < g_world.lot_hi; i++)
        if (g_world.lots[i].def == B_BUS_DEPOT && g_world.lots[i].status == LOT_ACTIVE) n++;
    return n;
}

void transit_pass_prepare(void)
{
    int i, rot;
    s_depots = count_depots();
    s_nst = 0;
    for (i = 1; i < g_world.lot_hi && s_nst < MAX_STATIONS; i++) {
        const Lot *l = &g_world.lots[i];
        if (l->def != B_STATION || l->status != LOT_ACTIVE) continue;
        if (!transit_station_ok(l->x, l->y, l->rot) || !world_lot_access(i, &rot)) continue;
        s_st_lot[s_nst++] = (u16)i;
    }
    for (i = 0; i < BUS_MAX_LINES; i++) {
        BusLine *l = &s_lines[i];
        int old = l->status, on = l->npath;
        if (!l->used) continue;
        if (l->nstops >= 2 && !route_valid(l)) route_line(l);
        line_status(l);
        if (l->status != old || l->npath != on) s_serial++;
    }
}

void transit_month(void)
{
    Money m = transit_upkeep();
    if (m > 0) budget_spend(m, BL_SPECIAL);
}

/* ------------------------------------------------------------------ editing */
int transit_new_line(void)
{
    int i;
    s_reason = 0;
    for (i = 0; i < BUS_MAX_LINES; i++) if (!s_lines[i].used) break;
    if (i == BUS_MAX_LINES) { s_reason = REJ_BUS_MAX; return -1; }
    if (!budget_can_afford(BUS_LINE_COST)) { s_reason = REJ_NO_MONEY; return -1; }
    budget_spend(BUS_LINE_COST, BL_CONSTRUCTION);
    memset(&s_lines[i], 0, sizeof(s_lines[i]));
    s_lines[i].used = 1;
    s_lines[i].number = (u8)(i + 1);
    s_lines[i].status = BLS_ONE_STOP;
    s_serial++;
    return i;
}

static void place_shelter(BusLine *l, int s)
{
    static const u8 order[DIR_COUNT] = { DIR_SW, DIR_SE, DIR_NE, DIR_NW };
    int k, rot, x = l->sx[s], y = l->sy[s];
    l->slot_lot[s] = 0;
    for (k = 0; k < BUS_MAX_LINES; k++) {                   /* another line's stop here keeps its shelter */
        int j;
        if (!s_lines[k].used || &s_lines[k] == l) continue;
        for (j = 0; j < s_lines[k].nstops; j++) if (s_lines[k].sx[j] == x && s_lines[k].sy[j] == y) return;
    }
    for (k = 0; k < DIR_COUNT; k++) {
        int d = order[k], nx = x + g_dir_dx[d], ny = y + g_dir_dy[d], id;
        const Tile *t;
        if (!world_in(nx, ny) || !world_is_buildable(nx, ny)) continue;
        t = WT(nx, ny);
        if (TILE_ZONE(t) != ZONE_NONE || TER_TYPE(t) != TER_GRASS || (t->road & TR_POWER_LINE)) continue;
        for (rot = 0; rot < ROT_COUNT; rot++) if (g_rot_front_dir[rot] == DIR_OPPOSITE(d)) break;
        if (world_lot_check(B_BUS_STOP, nx, ny, rot) != PLACE_OK) continue;
        id = world_lot_place(B_BUS_STOP, nx, ny, rot, 0, LOT_ACTIVE);
        if (id) { world_lot(id)->flags |= LF_PLAYER; l->slot_lot[s] = (u16)id; }
        return;
    }
}

int transit_add_stop(int line, int x, int y)
{
    BusLine *l;
    static u8 sx[BUS_MAX_PATH], sy[BUS_MAX_PATH];
    int n = 0, k;
    s_reason = 0;
    if (line < 0 || line >= BUS_MAX_LINES || !s_lines[line].used) { s_reason = REJ_NOTHING_TO_DO; return 0; }
    l = &s_lines[line];
    if (!stop_road(x, y)) { s_reason = REJ_BUS_NOT_ROAD; return 0; }
    if (l->nstops >= BUS_MAX_STOPS) { s_reason = REJ_BUS_MAX; return 0; }
    if (l->nstops && l->sx[l->nstops - 1] == x && l->sy[l->nstops - 1] == y) { s_reason = REJ_NOTHING_TO_DO; return 0; }
    if (l->nstops) {
        if (l->npath < 1 && !route_line(l)) { s_reason = REJ_BUS_NO_ROUTE; return 0; }
        n = transit_path(l->sx[l->nstops - 1], l->sy[l->nstops - 1], x, y, sx, sy, BUS_MAX_PATH);
        if (!n) { s_reason = REJ_BUS_NO_ROUTE; return 0; }
        if (l->npath + n - 1 > BUS_MAX_PATH) { s_reason = REJ_BUS_MAX; return 0; }
    }
    if (!budget_can_afford(BUS_STOP_COST)) { s_reason = REJ_NO_MONEY; return 0; }
    budget_spend(BUS_STOP_COST, BL_CONSTRUCTION);
    l->sx[l->nstops] = (u8)x; l->sy[l->nstops] = (u8)y;
    if (!l->nstops) { l->px[0] = (u8)x; l->py[0] = (u8)y; l->npath = 1; l->spos[0] = 0; }
    else {
        for (k = 1; k < n; k++) { l->px[l->npath] = sx[k]; l->py[l->npath] = sy[k]; l->npath++; }
        l->spos[l->nstops] = (u16)(l->npath - 1);
    }
    l->nstops++;
    world_batch_begin();
    place_shelter(l, l->nstops - 1);
    world_batch_end();
    s_depots = count_depots();
    line_status(l);
    s_serial++;
    return 1;
}

int transit_delete_line(int line)
{
    BusLine *l;
    int s;
    if (line < 0 || line >= BUS_MAX_LINES || !s_lines[line].used) return 0;
    l = &s_lines[line];
    world_batch_begin();
    for (s = 0; s < l->nstops; s++) {
        Lot *lot = world_lot(l->slot_lot[s]);
        if (lot && lot->def == B_BUS_STOP) world_lot_remove(l->slot_lot[s]);
    }
    world_batch_end();
    memset(l, 0, sizeof(*l));
    if (s_edit == line) s_edit = -1;
    s_serial++;
    return 1;
}

/* ------------------------------------------------------------------ railway station */
int transit_station_ok(int x0, int y0, int rot)
{
    int w, h, back, i, x, y, n = 0;
    def_footprint(B_STATION, rot & 3, &w, &h);
    back = DIR_OPPOSITE(g_rot_front_dir[rot & 3]);
    for (i = 0; i < (back == DIR_NE || back == DIR_SW ? w : h); i++) {
        if (back == DIR_NE) { x = x0 + i; y = y0 - 1; }
        else if (back == DIR_SW) { x = x0 + i; y = y0 + h; }
        else if (back == DIR_SE) { x = x0 + w; y = y0 + i; }
        else { x = x0 - 1; y = y0 + i; }
        if (!world_in(x, y) || TILE_ROAD(WT(x, y)) != ROAD_RAIL) return 0;
        n++;
    }
    return n > 0;
}

int transit_station_rot(int x0, int y0)
{
    int r, first = -1, rr;
    for (r = 0; r < ROT_COUNT; r++) {
        int w, h;
        if (!transit_station_ok(x0, y0, r)) continue;
        if (first < 0) first = r;
        def_footprint(B_STATION, r, &w, &h);
        if (world_footprint_road_dist(x0, y0, x0 + w - 1, y0 + h - 1, ZONE_ROAD_REACH, &rr) && rr == r) return r;
    }
    return first;
}

s32 transit_regional_migrants(s32 pull) { return s_nst ? 20 + IMAX(0, pull) * 30 / 100 : 0; }
s32 transit_freight_jobs(s32 exp) { return s_nst ? 15 + IMAX(0, exp) * 25 / 100 : 0; }

/* ------------------------------------------------------------------ tool */
void transit_tool_select(int on)
{
    s_tool_on = on;
    s_edit = -1;
    s_pv_x = -1;
}

int transit_tool_editing(void) { return s_edit >= 0 && s_lines[s_edit].used; }

static void end_edit(void)
{
    if (s_edit >= 0 && s_lines[s_edit].used && s_lines[s_edit].nstops < 2) transit_delete_line(s_edit);
    s_edit = -1;
}

static void pv_add(ToolPreview *p, int x, int y, int st, int rs)
{
    if (p->n >= TOOL_MAX_PREVIEW) return;
    p->tiles[p->n].x = (u8)x; p->tiles[p->n].y = (u8)y; p->tiles[p->n].state = (u8)st; p->tiles[p->n].reason = (u8)rs;
    p->n++;
    if (x < p->x0) p->x0 = x;
    if (y < p->y0) p->y0 = y;
    if (x > p->x1) p->x1 = x;
    if (y > p->y1) p->y1 = y;
}

int transit_tool_update(const ToolInput *in, ToolPreview *p, int *lx, int *ly, Money *lcost, int *lreason)
{
    int tx = in->tx, ty = in->ty, road, k, del;
    BusLine *l;
    memset(p, 0, (size_t)((char *)&p->tiles[0] - (char *)p));
    memset(&p->n_ok, 0, sizeof(*p) - (size_t)((char *)&p->n_ok - (char *)p));
    *lreason = 0; *lcost = 0;
    if (s_edit >= 0 && !s_lines[s_edit].used) s_edit = -1;
    if (in->cancel) { end_edit(); return TRES_CANCELLED; }
    if (!in->on_map) return TRES_NONE;
    tx = ICLAMP(tx, 0, g_world.w - 1); ty = ICLAMP(ty, 0, g_world.h - 1);
    road = stop_road(tx, ty);
    del = in->ctrl ? transit_stop_line_at(tx, ty) : -1;
    l = s_edit >= 0 ? &s_lines[s_edit] : NULL;
    /* preview: the line being drawn, the segment to the cursor, the cursor tile */
    p->active = 1; p->dragging = 1; p->tool = TOOL_BUS_LINE; p->param = 0;
    p->x0 = p->y0 = 255; p->x1 = p->y1 = 0;
    if (del >= 0) {
        for (k = 0; k < s_lines[del].npath; k++) pv_add(p, s_lines[del].px[k], s_lines[del].py[k], PT_REJECT, REJ_NOTHING_TO_DO);
        p->n_rejected = p->n;
        p->main_reason = REJ_NOTHING_TO_DO;
    } else {
        if (l) for (k = 0; k < l->npath; k++) pv_add(p, l->px[k], l->py[k], PT_UPGRADE, 0);
        if (!road) { pv_add(p, tx, ty, PT_REJECT, REJ_BUS_NOT_ROAD); p->n_rejected = 1; p->main_reason = REJ_BUS_NOT_ROAD; }
        else if (l && l->nstops) {
            if (s_pv_x != tx || s_pv_y != ty || s_pv_edit != s_edit || s_pv_serial != s_serial || s_pv_wserial != world_struct_serial()) {
                s_pv_n = transit_path(l->sx[l->nstops - 1], l->sy[l->nstops - 1], tx, ty, s_pv_px, s_pv_py, BUS_MAX_PATH);
                s_pv_x = tx; s_pv_y = ty; s_pv_edit = s_edit; s_pv_serial = s_serial; s_pv_wserial = world_struct_serial();
            }
            if (!s_pv_n) { pv_add(p, tx, ty, PT_REJECT, REJ_BUS_NO_ROUTE); p->n_rejected = 1; p->main_reason = REJ_BUS_NO_ROUTE; }
            else { for (k = 1; k < s_pv_n; k++) pv_add(p, s_pv_px[k], s_pv_py[k], PT_OK, 0); p->n_ok = IMAX(1, s_pv_n - 1); }
            p->cost = BUS_STOP_COST;
        } else { pv_add(p, tx, ty, PT_OK, 0); p->n_ok = 1; p->cost = (l ? 0 : BUS_LINE_COST) + BUS_STOP_COST; }
    }
    p->affordable = p->cost <= 0 || budget_can_afford(p->cost);
    p->eff_upkeep = l ? (Money)IMIN(BUS_MAX_BUSES, 1 + (l->npath + p->n_ok) / BUS_TILES_PER_BUS) * BUS_UPKEEP : BUS_UPKEEP;
    if (!in->pressed) return TRES_NONE;
    /* click */
    *lx = tx; *ly = ty;
    if (del >= 0) { transit_delete_line(del); return TRES_COMMITTED; }
    if (!road) { *lreason = REJ_BUS_NOT_ROAD; return TRES_DENIED; }
    if (l && l->nstops && l->sx[l->nstops - 1] == tx && l->sy[l->nstops - 1] == ty) { end_edit(); return TRES_NONE; }
    if (!l) {
        int nl = transit_new_line();
        if (nl < 0) { *lreason = s_reason; return TRES_DENIED; }
        s_edit = nl; l = &s_lines[nl];
        *lcost += BUS_LINE_COST;
    }
    if (!transit_add_stop(s_edit, tx, ty)) {
        *lreason = s_reason;
        if (!l->nstops) { transit_delete_line(s_edit); budget_spend(-BUS_LINE_COST, BL_CONSTRUCTION); *lcost = 0; }
        return TRES_DENIED;
    }
    *lcost += BUS_STOP_COST;
    return TRES_COMMITTED;
}

/* ------------------------------------------------------------------ text */
const char *transit_status_text(int st)
{
    static const char *const n[BLS_COUNT] = { "running", "needs a second stop", "needs a bus depot", "no road route" };
    return (st >= 0 && st < BLS_COUNT) ? n[st] : "?";
}

int transit_line_text(int i, char *buf, int size)
{
    const BusLine *l = transit_line(i);
    char a[32];
    if (!l->used) return str_printf(buf, size, "-");
    if (l->status != BLS_OK) return str_printf(buf, size, "Line %d: %d stops, %s", l->number, l->nstops, transit_status_text(l->status));
    text_fmt_int(a, sizeof(a), l->riders);
    return str_printf(buf, size, "Line %d: %d stops, %d bus%s, %s rides a day", l->number, l->nstops, l->buses,
                      l->buses == 1 ? "" : "es", a);
}

/* ------------------------------------------------------------------ script */
int transit_script_line(int ntok, char **tok, int ox, int oy, FILE *out)
{
    int i, L;
    if (ntok < 1 || !str_ieq(tok[0], "busline")) return -1;
    if (ntok >= 5 && str_ieq(tok[1], "add")) {             /* busline add N X Y: one more stop on line N */
        int num = atoi(tok[2]);
        for (L = 0; L < BUS_MAX_LINES; L++) if (s_lines[L].used && s_lines[L].number == num) break;
        if (L == BUS_MAX_LINES || !transit_add_stop(L, atoi(tok[3]) + ox, atoi(tok[4]) + oy)) {
            if (out) fprintf(out, "error: busline add %d: %s\n", num, text_place_reason(L == BUS_MAX_LINES ? REJ_NOTHING_TO_DO : s_reason));
            return SCR_ERROR;
        }
        return SCR_OK;
    }
    if (ntok >= 3 && str_ieq(tok[1], "delete")) {
        int num = atoi(tok[2]);
        for (L = 0; L < BUS_MAX_LINES; L++) if (s_lines[L].used && s_lines[L].number == num) break;
        if (L == BUS_MAX_LINES || !transit_delete_line(L)) { if (out) fprintf(out, "error: busline delete %d: no such line\n", num); return SCR_ERROR; }
        return SCR_OK;
    }
    if (ntok < 5) { if (out) fprintf(out, "error: busline needs at least two stops\n"); return SCR_ERROR; }
    L = transit_new_line();
    if (L < 0) { if (out) fprintf(out, "error: busline: %s\n", text_place_reason(s_reason)); return SCR_ERROR; }
    for (i = 1; i + 1 < ntok; i += 2)
        if (!transit_add_stop(L, atoi(tok[i]) + ox, atoi(tok[i + 1]) + oy)) {
            if (out) fprintf(out, "error: busline stop %s %s: %s\n", tok[i], tok[i + 1], text_place_reason(s_reason));
            return SCR_ERROR;
        }
    return SCR_OK;
}

/* ------------------------------------------------------------------ save (inside TRAF v2) */
void transit_save(struct SaveFile *sf)
{
    int i, s;
    save_u32(sf, BUS_MAX_LINES);
    for (i = 0; i < BUS_MAX_LINES; i++) {
        const BusLine *l = &s_lines[i];
        save_u8(sf, l->used);
        if (!l->used) continue;
        save_u8(sf, l->nstops); save_u8(sf, l->status); save_u8(sf, l->number);
        for (s = 0; s < l->nstops; s++) { save_u8(sf, l->sx[s]); save_u8(sf, l->sy[s]); save_u16(sf, l->slot_lot[s]); save_u16(sf, l->spos[s]); }
        save_u16(sf, l->npath); save_u16(sf, l->buses);
        save_bytes(sf, l->px, l->npath); save_bytes(sf, l->py, l->npath);
        save_s32(sf, l->riders);
    }
    save_s32(sf, s_depots); save_s32(sf, s_nst);
    for (i = 0; i < MAX_STATIONS; i++) save_u16(sf, s_st_lot[i]);
}

void transit_load(struct SaveFile *sf)
{
    int i, s, v, n = (int)load_u32(sf);
    memset(s_lines, 0, sizeof(s_lines));
    for (i = 0; i < n && i < BUS_MAX_LINES; i++) {
        BusLine *l = &s_lines[i];
        l->used = (u8)load_u8(sf);
        if (!l->used) continue;
        v = (int)load_u8(sf); l->nstops = (u8)IMIN(BUS_MAX_STOPS, v); l->status = (u8)load_u8(sf); l->number = (u8)load_u8(sf);
        for (s = 0; s < l->nstops; s++) { l->sx[s] = (u8)load_u8(sf); l->sy[s] = (u8)load_u8(sf); l->slot_lot[s] = (u16)load_u16(sf); l->spos[s] = (u16)load_u16(sf); }
        v = (int)load_u16(sf); l->npath = (u16)IMIN(BUS_MAX_PATH, v); l->buses = (u16)load_u16(sf);
        load_bytes(sf, l->px, l->npath); load_bytes(sf, l->py, l->npath);
        l->riders = load_s32(sf);
    }
    s_depots = load_s32(sf); v = load_s32(sf); s_nst = ICLAMP(v, 0, MAX_STATIONS);
    for (i = 0; i < MAX_STATIONS; i++) s_st_lot[i] = (u16)load_u16(sf);
    s_serial++;
    s_edit = -1;
}

/* ------------------------------------------------------------------ PL3: undo/redo */
int transit_set_line(int line, const BusLine *l)
{
    int s, edit = s_edit;
    if (line < 0 || line >= BUS_MAX_LINES) return 0;
    if (s_lines[line].used) transit_delete_line(line);       /* removes its shelters */
    if (l && l->used) {
        s_lines[line] = *l;
        world_batch_begin();
        for (s = 0; s < s_lines[line].nstops; s++) place_shelter(&s_lines[line], s);
        world_batch_end();
        s_depots = count_depots();
        line_status(&s_lines[line]);
        if (edit == line) s_edit = edit;                       /* undoing a stop keeps drawing the same line */
    }
    s_serial++;
    return 1;
}
