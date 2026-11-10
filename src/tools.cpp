/* tools.cpp - build tools, drag preview, validation, costs, undo/redo, script interpreter. PORTABLE.
   Owner: T08. DESIGN 4 (mouse grammar, 4.1-4.6), 0.2 pillar 3. See tools.h for the contract.

   Structure:
   - eval_*(): one tile (or one footprint) -> state (PT_*), reason, cost, timber. Used by the preview AND
     by the commit, so what the player sees is what gets built.
   - plan(): fills s_prev for a tool + drag (path for roads/lines incl. bridge-run validation, rect for
     zones/bulldoze, footprint for buildings), with totals, affordability and projected effects.
   - commit(): re-plans, checks money/unlock, applies the changes through world setters inside one
     world batch, records every primitive change as an undo op, pays once per command.
   - Undo log: ops (tile before/after states, lot place/remove with the full Lot) grouped in commands.
     Undo applies inverse ops in reverse order and refunds the command's money exactly; redo re-applies
     and charges again. A field is reverted only if it still has the value the command gave it (the
     simulation may have grown a building on a zoned tile since: that part is skipped).
     Cleared at month change (tools_month_changed) and on reset.
   Determinism: no RNG; building variants come from hash_xy, so scripted towns are reproducible. */
#include "common.h"
#include "tools.h"
#include "industry.h"
#include "util.h"
#include "world.h"
#include "defs.h"
#include "sim.h"
#include "budget.h"
#include "mapgen.h"
#include "politics.h"
#include "transit.h"
#include "complex.h"
#include "civic.h"
#include "era.h"           /* M7 */
#include "season.h"        /* 1.1 W1 */
#include "power.h"         /* 1.2 S1: reactor price, build months, script */

/* ------------------------------------------------------------------ state */
static ToolPreview s_prev;
static int s_tool, s_param, s_rot, s_rot_manual;
static int s_drag, s_ax, s_ay, s_bx, s_by, s_xfirst, s_xfirst_set;
static int s_query_x, s_query_y;
static int s_last_x, s_last_y, s_last_reason, s_last_tool;
static Money s_last_cost;
static u32 s_plan_key, s_plan_serial;
static Money s_plan_money;
static int s_plan_valid;
static int s_pipes_roads, s_show_pipes;     /* F1 options (tools.h) */

/* path of the current line plan (roads, power lines) */
#define PATH_MAX_LEN 520
static u8  s_px[PATH_MAX_LEN], s_py[PATH_MAX_LEN], s_pstate[PATH_MAX_LEN], s_preason[PATH_MAX_LEN];
static int s_pn;
static u16 s_inpath[MAP_MAX_W * MAP_MAX_H];      /* stamp per tile: index+1 in path when == s_path_stamp */
static u16 s_inpath_stamp[MAP_MAX_W * MAP_MAX_H];
static u16 s_path_stamp;

/* lots already counted in a bulldoze plan/commit */
static u16 s_lot_seen[MAX_LOTS];
static u16 s_lot_stamp;

/* roads/lines built this month (bulldoze refund) */
static u8 s_new_tile[MAP_MAX_W * MAP_MAX_H / 8];
#define NEW_GET(x, y) (s_new_tile[TILE_IDX(x, y) >> 3] & (1 << (TILE_IDX(x, y) & 7)))
#define NEW_SET(x, y) (s_new_tile[TILE_IDX(x, y) >> 3] |= (u8)(1 << (TILE_IDX(x, y) & 7)))

/* ------------------------------------------------------------------ undo log */
enum { OP_TILE, OP_LOT_PLACE, OP_LOT_REMOVE,
       OP_BUS,                 /* PL3: a bus line edit: x = line, id = snapshot slot, gen = its generation */
       OP_GIFT };              /* PL3: a patron's gift placed (lot = the gift lot) */
typedef struct {            /* the parts of a tile a tool changes */
    u8 ter;                 /* full terrain byte */
    u8 road;                /* class | TR_POWER_LINE (no mask) */
    u8 zone;                /* ZONE_* */
    u8 tree;                /* TF_TREE bit (M5: | TF_RUBBLE: the bulldozer clears ruins, undo restores them) */
    u8 aux;
    u8 pipe;                /* F1: TF_PIPE bit */
} TState;
typedef struct {
    u8  kind, x, y, pad;
    TState t0, t1;          /* OP_TILE: before / after */
    u16 id;                 /* lot ops: slot at record time */
    u16 gen;                /* PL3: OP_BUS snapshot generation */
    Lot lot;                /* OP_LOT_PLACE: after; OP_LOT_REMOVE: before */
} Op;
typedef struct {
    int   op0, nops;
    Money con, timber;      /* booked: -con on BL_CONSTRUCTION, +timber on BL_TIMBER */
    u8    tool;
} Cmd;
#define MAX_OPS  16384
#define MAX_CMDS 512
static Op  s_ops[MAX_OPS];
static Cmd s_cmds[MAX_CMDS];
static int s_nops, s_ncmds, s_cur;          /* s_cur = commands done (undo stack top); > s_cur = redo */
static int s_rec, s_rec_overflow, s_rec_op0;
static Money s_rec_con, s_rec_timber;

static void log_clear(void) { s_nops = s_ncmds = s_cur = 0; }

static void log_drop_oldest(void)
{
    int k, n;
    if (s_ncmds <= 0) return;
    n = s_cmds[0].nops;
    memmove(&s_ops[0], &s_ops[n], (size_t)(s_nops - n) * sizeof(Op));
    s_nops -= n;
    memmove(&s_cmds[0], &s_cmds[1], (size_t)(s_ncmds - 1) * sizeof(Cmd));
    s_ncmds--;
    if (s_cur > 0) s_cur--;
    for (k = 0; k < s_ncmds; k++) s_cmds[k].op0 -= n;
}

static void rec_begin(void)
{
    /* a new command drops the redo branch, keeps room for one big command */
    if (s_cur < s_ncmds) { s_ncmds = s_cur; s_nops = s_ncmds ? s_cmds[s_ncmds - 1].op0 + s_cmds[s_ncmds - 1].nops : 0; }
    while (s_ncmds > 0 && (s_nops > MAX_OPS / 2 || s_ncmds >= MAX_CMDS)) log_drop_oldest();
    s_rec = 1; s_rec_overflow = 0; s_rec_op0 = s_nops; s_rec_con = 0; s_rec_timber = 0;
}

static Op *rec_op(void)
{
    if (!s_rec) return NULL;
    if (s_nops >= MAX_OPS) { s_rec_overflow = 1; return NULL; }
    return &s_ops[s_nops++];
}

static void rec_end(int tool)
{
    Cmd *c;
    s_rec = 0;
    if (s_rec_con) budget_spend(s_rec_con, BL_CONSTRUCTION);
    if (s_rec_timber) budget_spend(-s_rec_timber, BL_TIMBER);
    if (s_rec_overflow) { log_printf("tools: warning: command too big for the undo log, log cleared"); log_clear(); return; }
    if (s_nops == s_rec_op0) return;                     /* nothing changed */
    c = &s_cmds[s_ncmds++];
    c->op0 = s_rec_op0; c->nops = s_nops - s_rec_op0; c->con = s_rec_con; c->timber = s_rec_timber;
    c->tool = (u8)tool;
    s_cur = s_ncmds;
}

/* ------------------------------------------------------------------ tile state helpers */
static void tstate_get(int x, int y, TState *s)
{
    const Tile *t = WT(x, y);
    s->ter = t->terrain;
    s->road = (u8)(t->road & (TR_CLASS_MASK | TR_POWER_LINE));
    s->zone = (u8)TILE_ZONE(t);
    s->tree = (u8)(t->flags & (TF_TREE | TF_RUBBLE));
    s->aux = t->aux;
    s->pipe = (u8)(t->flags & TF_PIPE);
}

/* move a tile to state `to`; if `from` is given, a field is changed only while it still equals from's
   value (partial undo/redo when the simulation changed the tile meanwhile) */
static void tstate_apply(int x, int y, const TState *to, const TState *from)
{
    TState c;
    tstate_get(x, y, &c);
    if ((c.road & TR_CLASS_MASK) != (to->road & TR_CLASS_MASK) &&
        (!from || (c.road & TR_CLASS_MASK) == (from->road & TR_CLASS_MASK)) &&
        (!(to->road & TR_CLASS_MASK) || !WT(x, y)->lot))
        world_set_road(x, y, to->road & TR_CLASS_MASK);
    tstate_get(x, y, &c);
    if ((c.road & TR_POWER_LINE) != (to->road & TR_POWER_LINE) &&
        (!from || (c.road & TR_POWER_LINE) == (from->road & TR_POWER_LINE)) &&
        (!(to->road & TR_POWER_LINE) || !WT(x, y)->lot))
        world_set_power_line(x, y, (to->road & TR_POWER_LINE) != 0);
    tstate_get(x, y, &c);
    if (c.ter != to->ter && (!from || c.ter == from->ter))
        world_set_terrain(x, y, to->ter & 15, to->ter >> 4);
    tstate_get(x, y, &c);
    if (c.tree != to->tree && (!from || c.tree == from->tree) && (!to->tree || !WT(x, y)->lot))
        world_set_tile_flags(x, y, to->tree, (TF_TREE | TF_RUBBLE) & ~to->tree);
    if (c.zone != to->zone && (!from || c.zone == from->zone) && !WT(x, y)->lot)
        world_set_zone(x, y, to->zone);
    if (c.pipe != to->pipe && (!from || c.pipe == from->pipe))    /* F1: pipes may run under lots */
        world_set_pipe(x, y, to->pipe != 0);
    if (!TILE_ROAD(WT(x, y))) WT(x, y)->aux = to->aux;  /* forest density / water depth back */
}

/* change a tile and record it */
static void op_tile(int x, int y, const TState *to)
{
    Op *o;
    TState before;
    tstate_get(x, y, &before);
    tstate_apply(x, y, to, NULL);
    o = rec_op();
    if (!o) return;
    memset(o, 0, sizeof(*o));
    o->kind = OP_TILE; o->x = (u8)x; o->y = (u8)y;
    o->t0 = before;
    tstate_get(x, y, &o->t1);
}

static int variant_for(int def, int x, int y)
{
    const BuildingDef *d = def_building(def);
    return d->variants ? (int)(hash_xy(x, y, (u32)def * 7919u) % d->variants) : 0;
}

static int op_lot_place(int def, int x, int y, int rot)
{
    Op *o;
    Lot *l;
    int bm = power_build_months(def);           /* 1.2 S1: a reactor is built over months (0 = ready at once) */
    int id = world_lot_place(def, x, y, rot, variant_for(def, x, y), bm ? LOT_BUILDING : LOT_ACTIVE);
    if (!id) return 0;
    l = world_lot(id);
    if (bm) l->build_left = (u8)ICLAMP(bm, 1, 255);
    l->flags |= LF_NEW | LF_PLAYER;
    o = rec_op();
    if (o) {
        memset(o, 0, sizeof(*o));
        o->kind = OP_LOT_PLACE; o->x = (u8)x; o->y = (u8)y; o->id = (u16)id; o->lot = *l;
    }
    return id;
}

static void op_lot_remove(int id)
{
    Op *o;
    Lot *l = world_lot(id);
    if (!l) return;
    o = rec_op();
    if (o) {
        memset(o, 0, sizeof(*o));
        o->kind = OP_LOT_REMOVE; o->x = l->x; o->y = l->y; o->id = (u16)id; o->lot = *l;
    }
    world_lot_remove(id);
}

static int lot_matching(const Lot *saved)
{
    int id = world_lot_at(saved->x, saved->y);
    const Lot *l = world_lot(id);
    if (!l || l->def != saved->def || l->x != saved->x || l->y != saved->y) return 0;
    return id;
}

static void lot_restore(const Lot *saved)
{
    int id = world_lot_place(saved->def, saved->x, saved->y, saved->rot, saved->variant, saved->status);
    Lot *l = world_lot(id);
    if (!l) { log_printf("tools: undo: lot %s at %d,%d cannot be restored (%d)", def_building(saved->def)->key,
                         saved->x, saved->y, world_lot_check(saved->def, saved->x, saved->y, saved->rot)); return; }
    /* not drawn: may be written directly (world.h); the visible fields match already */
    l->flags = saved->flags; l->build_left = saved->build_left; l->occupants = saved->occupants;
    l->age_months = saved->age_months; l->problems = saved->problems; l->growth = saved->growth;
    l->happy = saved->happy; l->spare = saved->spare;
}

/* ---- PL3: bus line edits and gift placements in the undo log. A bus command keeps before/after snapshots of the
   lines it changed in a small ring (an op whose slot was reused since is skipped); a gift command records the
   footprint's tile changes + OP_GIFT (complex.cpp takes the gift back into its queue / places it again). */
#define BUS_SNAPS 32
typedef struct { u16 gen; BusLine before, after; } BusSnap;
static BusSnap s_bsnap[BUS_SNAPS];
static int s_bsnap_next;
static u16 s_bsnap_gen;
static BusLine s_bus_before[BUS_MAX_LINES];

static int bus_same(const BusLine *a, const BusLine *b)
{
    if (a->used != b->used) return 0;
    if (!a->used) return 1;
    return a->nstops == b->nstops && a->npath == b->npath && !memcmp(a->sx, b->sx, a->nstops) && !memcmp(a->sy, b->sy, a->nstops);
}

static void bus_apply(const Op *o, int redo)
{
    const BusSnap *b = &s_bsnap[o->id % BUS_SNAPS];
    const BusLine *from, *to;
    if (b->gen != o->gen) { log_printf("tools: undo: bus line %d snapshot gone", o->x + 1); return; }
    from = redo ? &b->before : &b->after; to = redo ? &b->after : &b->before;
    if (!bus_same(transit_line(o->x), from)) { log_printf("tools: undo: bus line %d changed meanwhile", o->x + 1); return; }
    transit_set_line(o->x, to);
}

static void gift_apply(const Op *o, int redo)
{
    int id = lot_matching(&o->lot);
    if (!redo) { if (id && !complex_gift_undo(id)) log_printf("tools: undo: gift at %d,%d cannot be taken back", o->lot.x, o->lot.y); return; }
    if (!id && complex_gift_def(0) == o->lot.def && !complex_place_gift(o->lot.x, o->lot.y, o->lot.rot))
        log_printf("tools: redo: gift at %d,%d cannot be placed again", o->lot.x, o->lot.y);
}

static void op_undo(const Op *o)
{
    int id;
    switch (o->kind) {
    case OP_TILE: tstate_apply(o->x, o->y, &o->t0, &o->t1); break;
    case OP_LOT_PLACE: id = lot_matching(&o->lot); if (id) world_lot_remove(id); break;
    case OP_LOT_REMOVE: if (!lot_matching(&o->lot)) lot_restore(&o->lot); break;
    case OP_BUS: bus_apply(o, 0); break;
    case OP_GIFT: gift_apply(o, 0); break;
    }
}

static void op_redo(const Op *o)
{
    int id;
    switch (o->kind) {
    case OP_TILE: tstate_apply(o->x, o->y, &o->t1, &o->t0); break;
    case OP_LOT_PLACE: if (!lot_matching(&o->lot)) lot_restore(&o->lot); break;
    case OP_LOT_REMOVE: id = lot_matching(&o->lot); if (id) world_lot_remove(id); break;
    case OP_BUS: bus_apply(o, 1); break;
    case OP_GIFT: gift_apply(o, 1); break;
    }
}

/* a command whose world/transit changes are already done (the money too: `spent` is only logged for the refund) */
static void rec_done(int tool, Money spent)
{
    int n0 = s_ncmds;
    s_rec_con = 0; s_rec_timber = 0;
    rec_end(tool);
    if (s_ncmds > n0) s_cmds[s_ncmds - 1].con = spent;
}

static void bus_record(Money spent)
{
    int k;
    rec_begin();
    for (k = 0; k < BUS_MAX_LINES; k++) {
        const BusLine *now = transit_line(k);
        BusSnap *b;
        Op *o;
        if (bus_same(&s_bus_before[k], now)) continue;
        o = rec_op();
        if (!o) break;
        b = &s_bsnap[s_bsnap_next]; s_bsnap_next = (s_bsnap_next + 1) % BUS_SNAPS;
        if (++s_bsnap_gen == 0) s_bsnap_gen = 1;
        b->gen = s_bsnap_gen; b->before = s_bus_before[k]; b->after = *now;
        memset(o, 0, sizeof(*o));
        o->kind = OP_BUS; o->x = (u8)k; o->id = (u16)(b - s_bsnap); o->gen = b->gen;
    }
    rec_done(TOOL_BUS_LINE, spent);
}

#define GIFT_SNAP 4                 /* tiles around the cursor saved before a gift click (footprints <= 4x4) */
static TState s_gift_ts[(2 * GIFT_SNAP + 1) * (2 * GIFT_SNAP + 1)];
static int s_gift_cx, s_gift_cy;

static void gift_snap(int cx, int cy)
{
    int x, y, k = 0;
    s_gift_cx = cx; s_gift_cy = cy;
    for (y = cy - GIFT_SNAP; y <= cy + GIFT_SNAP; y++)
        for (x = cx - GIFT_SNAP; x <= cx + GIFT_SNAP; x++, k++) if (world_in(x, y)) tstate_get(x, y, &s_gift_ts[k]);
}

static void gift_record(int lot)
{
    const Lot *l = world_lot(lot);
    int x, y, k = 0;
    TState now;
    Op *o;
    if (!l) return;
    rec_begin();
    for (y = s_gift_cy - GIFT_SNAP; y <= s_gift_cy + GIFT_SNAP; y++)
        for (x = s_gift_cx - GIFT_SNAP; x <= s_gift_cx + GIFT_SNAP; x++, k++) {
            if (!world_in(x, y)) continue;
            tstate_get(x, y, &now);
            if (!memcmp(&now, &s_gift_ts[k], sizeof(now)) || !(o = rec_op())) continue;
            memset(o, 0, sizeof(*o));
            o->kind = OP_TILE; o->x = (u8)x; o->y = (u8)y; o->t0 = s_gift_ts[k]; o->t1 = now;
        }
    if ((o = rec_op()) != NULL) {
        memset(o, 0, sizeof(*o));
        o->kind = OP_GIFT; o->x = l->x; o->y = l->y; o->id = (u16)lot; o->lot = *l;
    }
    rec_done(TOOL_GIFT, 0);
}

int tools_undo(void)
{
    Cmd *c;
    int i;
    if (s_cur <= 0) return 0;
    if (s_drag) s_drag = 0;
    c = &s_cmds[--s_cur];
    world_batch_begin();
    for (i = c->op0 + c->nops - 1; i >= c->op0; i--) op_undo(&s_ops[i]);
    world_batch_end();
    if (c->con) budget_spend(-c->con, BL_CONSTRUCTION);
    if (c->timber) budget_spend(c->timber, BL_TIMBER);
    s_plan_valid = 0;
    log_printf("tools: undo command %d (%d ops, %d mk back)", s_cur, c->nops, c->con - c->timber);
    return 1;
}

int tools_redo(void)
{
    Cmd *c;
    int i;
    if (s_cur >= s_ncmds) return 0;
    c = &s_cmds[s_cur];
    if (c->con - c->timber > 0 && !budget_can_afford(c->con - c->timber)) return 0;
    s_cur++;
    world_batch_begin();
    for (i = c->op0; i < c->op0 + c->nops; i++) op_redo(&s_ops[i]);
    world_batch_end();
    if (c->con) budget_spend(c->con, BL_CONSTRUCTION);
    if (c->timber) budget_spend(-c->timber, BL_TIMBER);
    s_plan_valid = 0;
    return 1;
}

int  tools_can_undo(void) { return s_cur > 0; }
int  tools_can_redo(void) { return s_cur < s_ncmds; }
int  tools_undo_count(void) { return s_cur; }
void tools_month_changed(void)
{
    log_clear();
    memset(s_new_tile, 0, sizeof(s_new_tile));
}

/* ------------------------------------------------------------------ lifecycle */
void tools_init(void)
{
    memset(&s_prev, 0, sizeof(s_prev));
    tools_reset();
}

void tools_reset(void)
{
    s_tool = TOOL_NONE; s_param = 0; s_rot = 0; s_rot_manual = 0; s_drag = 0;
    memset(&s_prev, 0, sizeof(s_prev));
    memset(s_new_tile, 0, sizeof(s_new_tile));
    log_clear();
    s_plan_valid = 0;
    s_last_reason = 0; s_last_tool = 0; s_last_cost = 0; s_last_x = s_last_y = 0;
    tools_script_reset();
}

void tools_set_pipes_with_roads(int on) { s_pipes_roads = on ? 1 : 0; s_plan_valid = 0; }
int  tools_pipes_with_roads(void) { return s_pipes_roads; }
void tools_set_show_pipes(int on) { if (!s_show_pipes != !on) s_plan_valid = 0; s_show_pipes = on ? 1 : 0; }

void tools_select(int tool, int param)
{
    if (tool != s_tool || param != s_param) { s_rot_manual = 0; s_drag = 0; }
    s_tool = ICLAMP(tool, 0, TOOL_COUNT - 1); s_param = param;
    s_plan_valid = 0;
    transit_tool_select(s_tool == TOOL_BUS_LINE);           /* M2: (re)selecting ends a line being drawn */
    if (s_tool == TOOL_NONE) memset(&s_prev, 0, sizeof(s_prev));
}

int  tools_tool(int *param) { if (param) *param = s_param; return s_tool; }
int  tools_rotation(void) { return s_rot; }
int  tools_dragging(void) { return s_drag; }
void tools_rotate(int delta) { s_rot = (s_rot + delta + 4) & 3; s_rot_manual = 1; s_plan_valid = 0; }
const ToolPreview *tools_preview(void) { return &s_prev; }
void tools_query_tile(int *x, int *y) { *x = s_query_x; *y = s_query_y; }
void tools_last_commit(int *x, int *y, Money *cost) { *x = s_last_x; *y = s_last_y; *cost = s_last_cost; }
int  tools_last_reason(void) { return s_last_reason; }
int  tools_last_tool(void) { return s_last_tool; }

/* ------------------------------------------------------------------ evaluation */
typedef struct { int state, reason; Money cost, timber; } TEval;

static int cur_year(void) { return sim_date()->year; }
static int cur_status(void) { return sim_stats()->town_status; }
static int all_unlocked(void) { return sim_settings()->all_unlocked; }

/* vegetation/rock under new construction: clearing and blasting costs */
static void eval_ground(const Tile *t, TEval *e, int blast)
{
    if (TER_TYPE(t) == TER_FOREST) { e->cost += g_misc.clear_forest; e->timber += g_misc.timber_income; }
    if (blast && TER_TYPE(t) == TER_ROCK) e->cost += g_misc.blast_rock;
}

/* F1: "lay pipes with new roads" */
static int road_gets_pipe(const Tile *t, int cls)
{
    return s_pipes_roads && (g_roads[cls].flags & RDF_CARRIES_WATER) && !(t->flags & TF_PIPE);
}

static void eval_road(int x, int y, int cls, TEval *e)
{
    const Tile *t;
    int rc, water;
    memset(e, 0, sizeof(*e));
    if (!world_in(x, y)) { e->state = PT_REJECT; e->reason = PLACE_OUT_OF_MAP; return; }
    t = WT(x, y);
    rc = TILE_ROAD(t);
    water = TER_IS_WATER(TER_TYPE(t));
    if (rc == ROAD_RAIL) { e->state = PT_REJECT; e->reason = REJ_PROTECTED_TILE; return; }
    if (rc) {
        if (rc >= cls) { e->state = PT_NOOP; return; }
        if (t->flags & TF_PROTECTED) { e->state = PT_NOOP; return; }
        e->state = PT_UPGRADE;
        e->cost = water ? g_roads[cls].bridge_cost - g_roads[rc].bridge_cost : g_roads[cls].cost - g_roads[rc].cost;
        if (e->cost < 0) e->cost = 0;
        if (road_gets_pipe(t, cls)) e->cost += water ? g_misc.pipe_water : g_misc.pipe;
        return;
    }
    if (t->flags & TF_PROTECTED) { e->state = PT_REJECT; e->reason = REJ_PROTECTED_TILE; return; }
    if (t->lot) { e->state = PT_REJECT; e->reason = PLACE_OCCUPIED; return; }
    if (water) {
        if (!(g_roads[cls].flags & RDF_BRIDGE_OK)) { e->state = PT_REJECT; e->reason = PLACE_WATER; return; }
        e->state = PT_OK; e->cost = g_roads[cls].bridge_cost;
        if (road_gets_pipe(t, cls)) e->cost += g_misc.pipe_water;
        return;
    }
    e->state = PT_OK;
    e->cost = g_roads[cls].cost;
    if (road_gets_pipe(t, cls)) e->cost += g_misc.pipe;
    eval_ground(t, e, 1);
}

/* F1: a pipe (param 0) or its removal (param 1). Underground: anything may be on the tile. */
static void eval_pipe(int x, int y, int remove, TEval *e)
{
    const Tile *t;
    memset(e, 0, sizeof(*e));
    if (!world_in(x, y)) { e->state = PT_REJECT; e->reason = PLACE_OUT_OF_MAP; return; }
    t = WT(x, y);
    if (remove) {
        if (!(t->flags & TF_PIPE)) { e->state = PT_NOOP; return; }
        e->state = PT_OK;
        e->cost = g_misc.bulldoze_tile;
        if (NEW_GET(x, y)) e->cost -= (TER_IS_WATER(TER_TYPE(t)) ? g_misc.pipe_water : g_misc.pipe) * g_misc.refund_pct / 100;
        return;
    }
    if (t->flags & TF_PIPE) { e->state = PT_NOOP; return; }
    e->state = PT_OK;
    e->cost = TER_IS_WATER(TER_TYPE(t)) ? g_misc.pipe_water : g_misc.pipe;
}

static void eval_power(int x, int y, int corner, TEval *e)
{
    const Tile *t;
    memset(e, 0, sizeof(*e));
    if (!world_in(x, y)) { e->state = PT_REJECT; e->reason = PLACE_OUT_OF_MAP; return; }
    t = WT(x, y);
    if (t->lot || (t->road & TR_POWER_LINE)) { e->state = PT_NOOP; return; }   /* lots conduct anyway */
    if (TILE_ROAD(t) == ROAD_RAIL) { e->state = PT_REJECT; e->reason = REJ_PROTECTED_TILE; return; }
    if (TILE_ROAD(t) && corner) { e->state = PT_REJECT; e->reason = REJ_WRONG_TERRAIN; return; }  /* no art */
    e->state = PT_OK;
    e->cost = g_misc.power_line;
    eval_ground(t, e, 0);
}

static void eval_zone(int x, int y, int zone, TEval *e)
{
    const Tile *t;
    memset(e, 0, sizeof(*e));
    if (!world_in(x, y)) { e->state = PT_REJECT; e->reason = PLACE_OUT_OF_MAP; return; }
    t = WT(x, y);
    if (TER_IS_WATER(TER_TYPE(t))) { e->state = PT_REJECT; e->reason = PLACE_WATER; return; }
    if (TILE_ROAD(t)) { e->state = PT_NOOP; return; }        /* dragging across streets is normal */
    if (t->flags & TF_PROTECTED) { e->state = PT_REJECT; e->reason = PLACE_PROTECTED; return; }
    if (t->lot) {
        const BuildingDef *d = def_building(g_world.lots[t->lot].def);
        if (d->cat == BCAT_ZONE && d->zone == zone) { e->state = PT_NOOP; return; }
        e->state = PT_REJECT; e->reason = PLACE_OCCUPIED; return;
    }
    if (t->road & TR_POWER_LINE) { e->state = PT_REJECT; e->reason = PLACE_OCCUPIED; return; }
    if (TILE_ZONE(t) == zone) { e->state = PT_NOOP; return; }
    if (TER_TYPE(t) == TER_ROCK) { e->state = PT_REJECT; e->reason = REJ_WRONG_TERRAIN; return; }
    if (zone == ZONE_FORESTRY) {     /* M3: forest only, no road reach limit (camps grow by the roads) */
        if (TER_TYPE(t) != TER_FOREST) { e->state = PT_REJECT; e->reason = REJ_WRONG_TERRAIN; return; }
        e->state = PT_OK; e->cost = g_misc.zone_tile; return;
    }
    if (zone == ZONE_FARM) {         /* M3: grass or fields; fields may lie up to 8 tiles from a road */
        if (TER_TYPE(t) != TER_GRASS && TER_TYPE(t) != TER_FIELD) { e->state = PT_REJECT; e->reason = REJ_WRONG_TERRAIN; return; }
        if (!world_road_within(x, y, 8)) { e->state = PT_REJECT; e->reason = REJ_NO_ROAD_REACH; return; }
        e->state = PT_OK; e->cost = g_misc.zone_tile; return;
    }
    if (!world_road_within(x, y, ZONE_ROAD_REACH)) { e->state = PT_REJECT; e->reason = REJ_NO_ROAD_REACH; return; }
    e->state = PT_OK;
    e->cost = g_misc.zone_tile;
}

static void eval_dezone(int x, int y, TEval *e)
{
    const Tile *t;
    memset(e, 0, sizeof(*e));
    if (!world_in(x, y)) { e->state = PT_REJECT; e->reason = PLACE_OUT_OF_MAP; return; }
    t = WT(x, y);
    if (!TILE_ZONE(t)) { e->state = PT_NOOP; return; }
    if (t->lot) { e->state = PT_REJECT; e->reason = PLACE_OCCUPIED; return; }
    e->state = PT_OK;
}

static Money lot_refund(const Lot *l)
{
    if ((l->flags & (LF_NEW | LF_PLAYER)) != (LF_NEW | LF_PLAYER)) return 0;
    return power_build_cost(l->def) * g_misc.refund_pct / 100;   /* 1.2 S1: the price paid (the reactor letter) */
}

/* bulldoze; count_lot = 1 the first time a lot is met in this plan (its cost counted once) */
static void eval_bulldoze(int x, int y, TEval *e, int *lot_out)
{
    const Tile *t;
    memset(e, 0, sizeof(*e));
    *lot_out = 0;
    if (!world_in(x, y)) { e->state = PT_REJECT; e->reason = PLACE_OUT_OF_MAP; return; }
    t = WT(x, y);
    if (t->lot) {
        const Lot *l = &g_world.lots[t->lot];
        e->state = PT_OK;
        if (s_lot_seen[t->lot] != s_lot_stamp) {
            int w, h;
            s_lot_seen[t->lot] = s_lot_stamp;
            def_footprint(l->def, l->rot, &w, &h);
            e->cost = g_misc.bulldoze_tile * w * h - lot_refund(l);
            *lot_out = t->lot;
        }
        return;
    }
    if (TILE_ROAD(t) && (t->flags & TF_PROTECTED)) {
        if (t->road & TR_POWER_LINE) { e->state = PT_OK; e->cost = g_misc.bulldoze_tile; return; }
        e->state = PT_REJECT; e->reason = REJ_PROTECTED_TILE; return;
    }
    if (TILE_ROAD(t) || (t->road & TR_POWER_LINE) || (s_show_pipes && (t->flags & TF_PIPE))) {
        e->state = PT_OK;
        e->cost = g_misc.bulldoze_tile;
        if (NEW_GET(x, y)) {           /* built this month: 25% back */
            int rc = TILE_ROAD(t);
            Money base = rc ? (TER_IS_WATER(TER_TYPE(t)) ? g_roads[rc].bridge_cost : g_roads[rc].cost) : 0;
            if (t->road & TR_POWER_LINE) base += g_misc.power_line;
            if (s_show_pipes && (t->flags & TF_PIPE)) base += TER_IS_WATER(TER_TYPE(t)) ? g_misc.pipe_water : g_misc.pipe;
            e->cost -= base * g_misc.refund_pct / 100;
        }
        return;
    }
    if (TER_TYPE(t) == TER_FOREST) { e->state = PT_OK; e->cost = g_misc.clear_forest; e->timber = g_misc.timber_income; return; }
    if (t->flags & (TF_TREE | TF_RUBBLE)) { e->state = PT_OK; e->cost = g_misc.bulldoze_tile; return; }   /* M5: ruins */
    e->state = PT_NOOP;
}

/* ------------------------------------------------------------------ plan (preview) */
static void prev_add(ToolPreview *p, int x, int y, int state, int reason)
{
    PreviewTile *pt;
    if (p->n >= TOOL_MAX_PREVIEW) return;
    if (p->truncated && state != PT_REJECT) return;
    pt = &p->tiles[p->n++];
    pt->x = (u8)ICLAMP(x, 0, 255); pt->y = (u8)ICLAMP(y, 0, 255); pt->state = (u8)state; pt->reason = (u8)reason;
}

static int s_reason_count[64];
static void reason_note(int r) { if (r > 0 && r < 64) s_reason_count[r]++; }
static int reason_main(void)
{
    int i, best = 0;
    for (i = 1; i < 64; i++) if (s_reason_count[i] > s_reason_count[best]) best = i;
    return s_reason_count[best] ? best : 0;
}

static int make_path(int ax, int ay, int bx, int by, int shift, int xfirst)
{
    int x = ax, y = ay, sx, sy, n = 0;
    if (shift) { if (IABS(bx - ax) >= IABS(by - ay)) by = ay; else bx = ax; }
    sx = bx > ax ? 1 : -1; sy = by > ay ? 1 : -1;
    s_px[n] = (u8)x; s_py[n] = (u8)y; n++;
    if (xfirst) {
        while (x != bx && n < PATH_MAX_LEN) { x += sx; s_px[n] = (u8)x; s_py[n] = (u8)y; n++; }
        while (y != by && n < PATH_MAX_LEN) { y += sy; s_px[n] = (u8)x; s_py[n] = (u8)y; n++; }
    } else {
        while (y != by && n < PATH_MAX_LEN) { y += sy; s_px[n] = (u8)x; s_py[n] = (u8)y; n++; }
        while (x != bx && n < PATH_MAX_LEN) { x += sx; s_px[n] = (u8)x; s_py[n] = (u8)y; n++; }
    }
    return n;
}

static int path_is_new_water(int i)
{
    const Tile *t = WT(s_px[i], s_py[i]);
    return s_pstate[i] == PT_OK && TER_IS_WATER(TER_TYPE(t)) && !TILE_ROAD(t);
}

/* bridges: every run of new water tiles must be straight, <= BRIDGE_MAX_LEN, and land on both ends */
static void validate_bridges(void)
{
    int i = 0, j, k, straight, bad;
    while (i < s_pn) {
        if (!path_is_new_water(i)) { i++; continue; }
        j = i;
        while (j + 1 < s_pn && path_is_new_water(j + 1)) j++;
        bad = 0;
        if (i == 0 || j == s_pn - 1) bad = REJ_BRIDGE_NOT_STRAIGHT;      /* must end on land */
        else {
            straight = 1;
            for (k = i - 1; k <= j + 1; k++)
                if (s_px[k] != s_px[i - 1] && s_py[k] != s_py[i - 1]) straight = 0;
            for (k = i - 1; k <= j + 1; k++)
                if (!(s_px[k] == s_px[i - 1] && s_px[k] == s_px[j + 1]) && !(s_py[k] == s_py[i - 1] && s_py[k] == s_py[j + 1])) straight = 0;
            if (!straight) bad = REJ_BRIDGE_NOT_STRAIGHT;
            else if (j - i + 1 > BRIDGE_MAX_LEN) bad = REJ_BRIDGE_TOO_LONG;
            else if (s_pstate[i - 1] == PT_REJECT || s_pstate[j + 1] == PT_REJECT) bad = REJ_BRIDGE_NOT_STRAIGHT;
        }
        if (bad) for (k = i; k <= j; k++) { s_pstate[k] = PT_REJECT; s_preason[k] = (u8)bad; }
        i = j + 1;
    }
}

static int locked_now(int tool, int param)
{
    int y = cur_year(), st = cur_status(), all = all_unlocked();
    switch (tool) {
    case TOOL_ROAD: return !def_road_unlocked(param, y, st, all);
    case TOOL_ZONE: return !def_zone_unlocked(param, y, st, all);
    case TOOL_BUILD: return !def_unlocked(param, y, st, all) || def_building(param)->variants == 0;
    }
    return 0;
}

static void footprint_at(int def, int cx, int cy, int centred, int *x0, int *y0, int *w, int *h, int *rot)
{
    const BuildingDef *d = def_building(def);
    int r = s_rot, rr;
    def_footprint(def, r, w, h);
    *x0 = centred ? cx - (*w - 1) / 2 : cx;
    *y0 = centred ? cy - (*h - 1) / 2 : cy;
    if (d->flags & BDF_NO_ROTATE) r = 0;
    else if (def == B_STATION && !s_rot_manual) {               /* M2: platform (back) side to the railway */
        rr = transit_station_rot(*x0, *y0);
        if (rr >= 0) r = rr;
    } else if (!s_rot_manual && d->w == d->h) {
        if (world_footprint_road_dist(*x0, *y0, *x0 + *w - 1, *y0 + *h - 1, 3, &rr)) r = rr;
    }
    *rot = r;
    def_footprint(def, r, w, h);
}

static void plan(int tool, int param, int ax, int ay, int bx, int by, int shift, int xfirst, int centred)
{
    ToolPreview *p = &s_prev;
    TEval e;
    int i, x, y, x0, y0, x1, y1, lot, total;
    Money timber = 0;
    memset(p, 0, (size_t)((char *)&p->tiles[0] - (char *)p));
    memset(&p->n_ok, 0, sizeof(*p) - (size_t)((char *)&p->n_ok - (char *)p));
    memset(s_reason_count, 0, sizeof(s_reason_count));
    p->active = 1; p->dragging = s_drag; p->tool = tool; p->param = param; p->rot = s_rot;
    s_pn = 0;
    if (++s_path_stamp == 0) { memset(s_inpath_stamp, 0, sizeof(s_inpath_stamp)); s_path_stamp = 1; }
    if (++s_lot_stamp == 0) { memset(s_lot_seen, 0, sizeof(s_lot_seen)); s_lot_stamp = 1; }
    switch (tool) {
    case TOOL_ROAD:
    case TOOL_POWER_LINE:
    case TOOL_PIPE:
        s_pn = make_path(ax, ay, bx, by, shift, xfirst);
        for (i = 0; i < s_pn; i++) {
            int corner = i > 0 && i < s_pn - 1 && s_px[i - 1] != s_px[i + 1] && s_py[i - 1] != s_py[i + 1];
            if (tool == TOOL_ROAD) eval_road(s_px[i], s_py[i], param, &e);
            else if (tool == TOOL_PIPE) eval_pipe(s_px[i], s_py[i], param, &e);
            else eval_power(s_px[i], s_py[i], corner, &e);
            s_pstate[i] = (u8)e.state; s_preason[i] = (u8)e.reason;
        }
        if (tool == TOOL_ROAD) validate_bridges();
        x0 = 255; y0 = 255; x1 = 0; y1 = 0;
        for (i = 0; i < s_pn; i++) {
            x = s_px[i]; y = s_py[i];
            if (s_pstate[i] == PT_OK || s_pstate[i] == PT_UPGRADE) {
                if (tool == TOOL_ROAD) eval_road(x, y, param, &e);
                else if (tool == TOOL_PIPE) eval_pipe(x, y, param, &e);
                else { e.cost = g_misc.power_line; e.timber = 0;
                    if (TER_TYPE(WT(x, y)) == TER_FOREST) { e.cost += g_misc.clear_forest; e.timber = g_misc.timber_income; } }
                p->cost += e.cost; timber += e.timber; p->n_ok++;
            } else if (s_pstate[i] == PT_REJECT) { p->n_rejected++; reason_note(s_preason[i]); }
            prev_add(p, x, y, s_pstate[i], s_preason[i]);
            s_inpath[TILE_IDX(x, y)] = (u16)(i + 1); s_inpath_stamp[TILE_IDX(x, y)] = s_path_stamp;
            x0 = IMIN(x0, x); y0 = IMIN(y0, y); x1 = IMAX(x1, x); y1 = IMAX(y1, y);
        }
        p->x0 = x0; p->y0 = y0; p->x1 = x1; p->y1 = y1;
        if (tool == TOOL_ROAD) p->eff_upkeep = (Money)((p->n_ok * g_roads[param].upkeep10 + 9) / 10);
        else if (tool == TOOL_PIPE) p->eff_upkeep = (Money)((p->n_ok * g_misc.pipe_upkeep10 + 9) / 10) * (param ? -1 : 1);
        else p->eff_upkeep = (Money)((p->n_ok * g_misc.power_line_upkeep10 + 9) / 10);
        break;
    case TOOL_ZONE:
    case TOOL_DEZONE:
    case TOOL_BULLDOZE:
        x0 = IMAX(0, IMIN(ax, bx)); x1 = IMIN(g_world.w - 1, IMAX(ax, bx));
        y0 = IMAX(0, IMIN(ay, by)); y1 = IMIN(g_world.h - 1, IMAX(ay, by));
        p->x0 = x0; p->y0 = y0; p->x1 = x1; p->y1 = y1;
        total = (x1 - x0 + 1) * (y1 - y0 + 1);
        p->truncated = total > TOOL_MAX_PREVIEW;
        for (y = y0; y <= y1; y++)
            for (x = x0; x <= x1; x++) {
                if (tool == TOOL_ZONE) eval_zone(x, y, param, &e);
                else if (tool == TOOL_DEZONE) eval_dezone(x, y, &e);
                else eval_bulldoze(x, y, &e, &lot);
                if (e.state == PT_OK || e.state == PT_UPGRADE) { p->n_ok++; p->cost += e.cost; timber += e.timber; }
                else if (e.state == PT_REJECT) { p->n_rejected++; reason_note(e.reason); }
                prev_add(p, x, y, e.state, e.reason);
            }
        if (tool == TOOL_ZONE) {
            const BuildingDef *d = def_building(def_zone_building(param, 1, 1, 1));
            s32 per = 0;
            if (param == ZONE_HOUSES) per = d->capacity;
            else if (param == ZONE_APARTMENTS) per = def_building(B_APT_T1_2X2)->capacity / 4;
            else if (param == ZONE_COMMERCIAL) per = d->capacity;
            else if (param == ZONE_INDUSTRY) per = def_building(B_IND_T1_2X2)->capacity / 4;
            if (param == ZONE_HOUSES || param == ZONE_APARTMENTS) p->eff_residents = per * p->n_ok;
            else p->eff_jobs = per * p->n_ok;
        }
        break;
    case TOOL_BUILD: {
        const BuildingDef *d = def_building(param);
        int w, h, rot, r;
        footprint_at(param, ax, ay, centred, &x0, &y0, &w, &h, &rot);
        p->rot = rot;
        p->lot_def = param; p->lot_x = x0; p->lot_y = y0; p->lot_w = w; p->lot_h = h;
        p->x0 = x0; p->y0 = y0; p->x1 = x0 + w - 1; p->y1 = y0 + h - 1;
        r = world_lot_check(param, x0, y0, rot);
        if (param == B_STATION && r == PLACE_OK && !transit_station_ok(x0, y0, rot))
            r = complex_map_has_rail() ? REJ_NEEDS_RAIL : REJ_NO_RAILWAY;   /* M2; PL2: say when the map has no track at all */
        if (r == PLACE_OK && param == B_NUCLEAR_PLANT && power_reactor_choice() == RX_NONE) r = REJ_REACTOR;   /* 1.2 S1 */
        p->cost = power_build_cost(param);   /* 1.2 S1: = d->cost except the reactor (the letter's price) */
        for (y = y0; y < y0 + h; y++)
            for (x = x0; x < x0 + w; x++) {
                int st = PT_OK, rs = 0;
                if (world_in(x, y)) {
                    const Tile *t = WT(x, y);
                    memset(&e, 0, sizeof(e));
                    eval_ground(t, &e, 1);
                    p->cost += e.cost; timber += e.timber;
                    if (TER_IS_WATER(TER_TYPE(t))) { st = PT_REJECT; rs = PLACE_WATER; }
                    else if (TILE_ROAD(t)) { st = PT_REJECT; rs = PLACE_ROAD; }
                    else if (t->flags & TF_PROTECTED) { st = PT_REJECT; rs = PLACE_PROTECTED; }
                    else if (t->lot || (t->road & TR_POWER_LINE)) { st = PT_REJECT; rs = PLACE_OCCUPIED; }
                } else { st = PT_REJECT; rs = PLACE_OUT_OF_MAP; }
                if (st == PT_OK && r != PLACE_OK) { st = PT_REJECT; rs = r; }
                prev_add(p, x, y, st, rs);
                if (st == PT_REJECT) { p->n_rejected++; reason_note(rs); } else p->n_ok++;
            }
        if (r != PLACE_OK) { reason_note(r); if (!p->n_rejected) p->n_rejected = 1; }
        p->lot_ok = r == PLACE_OK;
        if (d->cat == BCAT_POWER) p->eff_power = d->capacity;
        else p->eff_power = -d->power_use;
        if (d->cat == BCAT_WATER) p->eff_water = d->capacity; else p->eff_water = -d->water_use;
        if (d->cat == BCAT_SERVICE) p->eff_residents = d->capacity;
        p->eff_jobs = d->jobs;
        p->eff_upkeep = d->upkeep;
        break; }
    default:
        p->active = 0;
        break;
    }
    if (p->active && tool != TOOL_BULLDOZE && locked_now(tool, param)) {
        p->main_reason = REJ_LOCKED; p->lot_ok = 0;
        p->n_rejected += p->n_ok; p->n_ok = 0;
    } else p->main_reason = reason_main();
    UNUSED(timber);     /* timber is income on its own ledger line; cost is what is charged */
    p->affordable = p->cost <= 0 || budget_can_afford(p->cost);
}

int tools_preview_mask(int x, int y)
{
    int d, m = 0, i, idx;
    if (!world_in(x, y)) return -1;
    idx = TILE_IDX(x, y);
    if (s_inpath_stamp[idx] != s_path_stamp) return -1;
    i = s_inpath[idx] - 1;
    if (i < 0 || i >= s_pn) return -1;
    for (d = 0; d < DIR_COUNT; d++) {
        int nx = x + g_dir_dx[d], ny = y + g_dir_dy[d], ni;
        const Tile *n;
        if (!world_in(nx, ny)) { if (s_prev.tool == TOOL_ROAD && (WT(x, y)->flags & TF_EDGE_LINK)) m |= 1 << d; continue; }
        n = WT(nx, ny);
        ni = TILE_IDX(nx, ny);
        if (s_inpath_stamp[ni] == s_path_stamp && s_pstate[s_inpath[ni] - 1] != PT_REJECT) { m |= 1 << d; continue; }
        if (s_prev.tool == TOOL_ROAD) { if (TILE_ROAD(n) && TILE_ROAD(n) != ROAD_RAIL) m |= 1 << d; }
        else if (s_prev.tool == TOOL_PIPE) { if (n->flags & TF_PIPE) m |= 1 << d; }
        else if ((n->road & TR_POWER_LINE) || (n->lot && def_building(g_world.lots[n->lot].def)->cat == BCAT_POWER)) m |= 1 << d;
    }
    return m;
}

/* ------------------------------------------------------------------ commit */
static void state_cleared(int x, int y, TState *s)   /* current state with vegetation/rock removed */
{
    tstate_get(x, y, s);
    if ((s->ter & 15) == TER_FOREST) s->ter = (u8)(TER_GRASS | (((s->ter >> 4) % 3) << 4));
    if ((s->ter & 15) == TER_ROCK) s->ter = TER_GRASS;
    s->tree = 0;
}

static int deny(int reason)
{
    s_last_reason = reason ? reason : REJ_NOTHING_TO_DO;
    return 0;
}

/* plan + apply; returns tiles/lots changed (0 = denied, s_last_reason says why) */
static int commit(int tool, int param, int ax, int ay, int bx, int by, int shift, int xfirst, int centred)
{
    ToolPreview *p = &s_prev;
    TState s;
    TEval e;
    int i, x, y, lot, n = 0;
    plan(tool, param, ax, ay, bx, by, shift, xfirst, centred);
    s_last_tool = tool;
    if (p->main_reason == REJ_LOCKED) return deny(REJ_LOCKED);
    if (p->n_ok == 0 || (tool == TOOL_BUILD && !p->lot_ok)) return deny(p->main_reason);
    if (!p->affordable) return deny(REJ_NO_MONEY);
    rec_begin();
    world_batch_begin();
    switch (tool) {
    case TOOL_ROAD:
    case TOOL_POWER_LINE:
    case TOOL_PIPE:
        for (i = 0; i < s_pn; i++) {
            if (s_pstate[i] != PT_OK && s_pstate[i] != PT_UPGRADE) continue;
            x = s_px[i]; y = s_py[i];
            if (tool == TOOL_ROAD) {
                int pipe = road_gets_pipe(WT(x, y), param);
                eval_road(x, y, param, &e);
                state_cleared(x, y, &s);
                if (s_pstate[i] == PT_UPGRADE) tstate_get(x, y, &s);
                s.road = (u8)((s.road & TR_POWER_LINE) | param);
                s.zone = 0;
                if (pipe) s.pipe = TF_PIPE;
            } else if (tool == TOOL_PIPE) {
                eval_pipe(x, y, param, &e);
                tstate_get(x, y, &s);
                s.pipe = (u8)(param ? 0 : TF_PIPE);
            } else {
                eval_power(x, y, 0, &e);
                state_cleared(x, y, &s);
                if (TER_TYPE(WT(x, y)) == TER_ROCK) s.ter = WT(x, y)->terrain;   /* poles stand on rock */
                s.road |= TR_POWER_LINE;
            }
            op_tile(x, y, &s);
            if (tool != TOOL_PIPE || !param) NEW_SET(x, y);
            s_rec_con += e.cost; s_rec_timber += e.timber;
            n++;
        }
        break;
    case TOOL_ZONE:
    case TOOL_DEZONE:
        for (y = p->y0; y <= p->y1; y++)
            for (x = p->x0; x <= p->x1; x++) {
                if (tool == TOOL_ZONE) eval_zone(x, y, param, &e); else eval_dezone(x, y, &e);
                if (e.state != PT_OK) continue;
                tstate_get(x, y, &s);
                s.zone = (u8)(tool == TOOL_ZONE ? param : ZONE_NONE);
                op_tile(x, y, &s);
                s_rec_con += e.cost;
                n++;
            }
        break;
    case TOOL_BULLDOZE:
        if (++s_lot_stamp == 0) { memset(s_lot_seen, 0, sizeof(s_lot_seen)); s_lot_stamp = 1; }
        for (y = p->y0; y <= p->y1; y++)
            for (x = p->x0; x <= p->x1; x++) {
                eval_bulldoze(x, y, &e, &lot);
                if (e.state != PT_OK) continue;
                s_rec_con += e.cost; s_rec_timber += e.timber;
                if (WT(x, y)->lot) { if (lot) { op_lot_remove(lot); n++; } continue; }
                tstate_get(x, y, &s);
                if (!(WT(x, y)->flags & TF_PROTECTED)) s.road &= (u8)~TR_CLASS_MASK;
                s.road &= (u8)~TR_POWER_LINE;
                if (s_show_pipes) s.pipe = 0;
                if ((s.ter & 15) == TER_FOREST) s.ter = (u8)(TER_GRASS | (((s.ter >> 4) % 3) << 4));
                s.tree = 0;
                op_tile(x, y, &s);
                n++;
            }
        break;
    case TOOL_BUILD:
        for (y = p->lot_y; y < p->lot_y + p->lot_h; y++)
            for (x = p->lot_x; x < p->lot_x + p->lot_w; x++) {
                TState c;
                tstate_get(x, y, &c);
                state_cleared(x, y, &s);
                s.zone = 0;                       /* player buildings sit on unzoned land */
                if (memcmp(&c, &s, sizeof(s))) op_tile(x, y, &s);
            }
        lot = op_lot_place(param, p->lot_x, p->lot_y, p->rot);
        if (lot) n = 1;
        else log_printf("tools: error: lot %s at %d,%d failed after a valid check", def_building(param)->key, p->lot_x, p->lot_y);
        s_rec_con = p->cost;
        break;
    }
    world_batch_end();
    if (tool == TOOL_BUILD) {
        /* timber: forest tiles cleared under the footprint (recorded tile ops of this command) */
        for (i = s_rec_op0; i < s_nops; i++)
            if (s_ops[i].kind == OP_TILE && (s_ops[i].t0.ter & 15) == TER_FOREST) s_rec_timber += g_misc.timber_income;
    }
    s_last_cost = s_rec_con - s_rec_timber;
    s_last_x = (p->x0 + p->x1) / 2; s_last_y = (p->y0 + p->y1) / 2;
    if (tool == TOOL_ROAD || tool == TOOL_POWER_LINE || tool == TOOL_PIPE) { s_last_x = bx; s_last_y = by; }
    rec_end(tool);
    s_last_reason = 0;
    s_plan_valid = 0;
    return n;
}

int tools_apply_line(int tool, int param, int x0, int y0, int x1, int y1)
{
    int r;
    if (tool != TOOL_ROAD && tool != TOOL_POWER_LINE && tool != TOOL_PIPE) return 0;
    r = commit(tool, param, x0, y0, x1, y1, 0, IABS(x1 - x0) >= IABS(y1 - y0), 0);
    return r;
}

int tools_apply_rect(int tool, int param, int x0, int y0, int x1, int y1)
{
    if (tool != TOOL_ZONE && tool != TOOL_DEZONE && tool != TOOL_BULLDOZE) return 0;
    return commit(tool, param, x0, y0, x1, y1, 0, 1, 0);
}

int tools_apply_build(int def, int x, int y, int rot)
{
    int sr = s_rot, sm = s_rot_manual, n, id = 0;
    if (def_building(def)->flags & BDF_MODULE) {      /* M6: "build pm_hall X Y" = the whole complex (north tile) */
        Money c = 0;
        int kind = complex_kind_of_def(def), r = complex_check_place(kind, x, y, &c);
        s_last_tool = TOOL_BUILD;
        if (r == PLACE_OK && !budget_can_afford(c)) r = REJ_NO_MONEY;
        if (r != PLACE_OK || !complex_place(kind, x, y)) { s_last_reason = r != PLACE_OK ? r : REJ_NOTHING_TO_DO; return 0; }
        s_last_cost = c; s_last_reason = 0;
        return world_lot_at(x + (kind == CPX_PAPER ? 2 : 0), y);    /* the hall */
    }
    if (rot >= 0) { s_rot = rot & 3; s_rot_manual = 1; } else s_rot_manual = 0;
    n = commit(TOOL_BUILD, def, x, y, x, y, 0, 1, 0);
    if (n) id = world_lot_at(s_prev.lot_x, s_prev.lot_y);
    s_rot = sr; s_rot_manual = sm;
    return id;
}

/* ------------------------------------------------------------------ town hall */
int tools_auto_town_hall(int rx, int ry)
{
    int r, best = 0, bestd = 1 << 30, bx = 0, by = 0, brot = 0, x, y, rot, id;
    for (r = 1; r <= 12 && !best; r++)
        for (y = ry - r - 1; y <= ry + r; y++)
            for (x = rx - r - 1; x <= rx + r; x++) {
                int d;
                if (world_lot_check(B_TOWN_HALL, x, y, 0) != PLACE_OK) continue;
                if (!world_footprint_road_dist(x, y, x + 1, y + 1, 1, &rot)) continue;
                d = (x - rx) * (x - rx) + (y - ry) * (y - ry);
                if (d < bestd) { bestd = d; bx = x; by = y; brot = rot; best = 1; }
            }
    if (!best) { log_printf("tools: warning: no town hall site near %d,%d", rx, ry); return 0; }
    world_batch_begin();
    for (y = by; y <= by + 1; y++)
        for (x = bx; x <= bx + 1; x++) {
            TState s;
            state_cleared(x, y, &s);
            s.zone = 0;
            tstate_apply(x, y, &s, NULL);
        }
    id = world_lot_place(B_TOWN_HALL, bx, by, brot, 0, LOT_ACTIVE);
    world_batch_end();
    if (id) world_lot(id)->flags |= LF_PLAYER;
    log_printf("tools: town hall at %d,%d rot %d (road %d,%d)", bx, by, brot, rx, ry);
    return id;
}

int tools_town_hall(int *x, int *y)
{
    int i;
    for (i = 1; i < g_world.lot_hi; i++)
        if (g_world.lots[i].def == B_TOWN_HALL) { *x = g_world.lots[i].x + 1; *y = g_world.lots[i].y + 1; return i; }
    return 0;
}

/* ------------------------------------------------------------------ per-frame mouse grammar */
static u32 plan_key(int tool, int param, int ax, int ay, int bx, int by, int shift)
{
    int v[10];
    v[0] = tool; v[1] = param; v[2] = ax; v[3] = ay; v[4] = bx; v[5] = by; v[6] = shift;
    v[7] = s_rot | (s_rot_manual << 2); v[8] = s_drag; v[9] = s_xfirst;
    return hash_bytes(HASH_INIT, v, sizeof(v));
}

static void plan_cached(int tool, int param, int ax, int ay, int bx, int by, int shift, int xfirst, int centred)
{
    u32 k = plan_key(tool, param, ax, ay, bx, by, shift);
    if (s_plan_valid && k == s_plan_key && s_plan_serial == g_world.serial && s_plan_money == g_budget.money) return;
    plan(tool, param, ax, ay, bx, by, shift, xfirst, centred);
    s_plan_valid = 1; s_plan_key = k; s_plan_serial = g_world.serial; s_plan_money = g_budget.money;
}

int tools_update(const ToolInput *in)
{
    int res = TRES_NONE, tx = in->tx, ty = in->ty, n;
    int line = s_tool == TOOL_ROAD || s_tool == TOOL_POWER_LINE || s_tool == TOOL_PIPE;
    int prm = (s_tool == TOOL_PIPE && in->ctrl) ? 1 : s_param;      /* F1: Ctrl+drag removes pipes */
    int rect = s_tool == TOOL_ZONE || s_tool == TOOL_DEZONE || s_tool == TOOL_BULLDOZE;
    if (in->on_map) { tx = ICLAMP(tx, 0, g_world.w - 1); ty = ICLAMP(ty, 0, g_world.h - 1); }
    if (s_tool == TOOL_BUS_LINE && (!in->cancel || transit_tool_editing())) {    /* M2: transit.cpp owns it */
        Money c = 0, m0 = g_budget.money;
        int k;
        if (in->pressed) for (k = 0; k < BUS_MAX_LINES; k++) s_bus_before[k] = *transit_line(k);   /* PL3: undo */
        res = transit_tool_update(in, &s_prev, &s_last_x, &s_last_y, &c, &s_last_reason);
        if (res == TRES_COMMITTED || res == TRES_DENIED) { s_last_tool = TOOL_BUS_LINE; s_last_cost = c; }
        if (res == TRES_COMMITTED) { s_last_reason = 0; if (in->pressed) bus_record(m0 - g_budget.money); }
        return res;
    }
    if ((s_tool == TOOL_GIFT || (s_tool == TOOL_BUILD && (def_building(s_param)->flags & BDF_MODULE))) && !in->cancel) {
        Money c = 0;                                   /* M6: complex / gift placement lives in complex.cpp */
        int gift = s_tool == TOOL_GIFT && in->pressed && in->on_map;
        if (gift) gift_snap(tx, ty);                   /* PL3: gifts are undoable (complexes are not: preview says so) */
        res = complex_tool_update(in, &s_prev, s_tool, s_param, s_rot, &s_last_x, &s_last_y, &c, &s_last_reason);
        if (res == TRES_COMMITTED || res == TRES_DENIED) { s_last_tool = s_tool; s_last_cost = c; }
        if (res == TRES_COMMITTED) { s_last_reason = 0; if (gift) gift_record(world_lot_at(s_last_x, s_last_y)); }
        return res;
    }
    if (in->cancel) {
        if (s_drag) { s_drag = 0; s_plan_valid = 0; s_prev.active = 0; s_prev.n = 0; return TRES_CANCELLED; }
        if (s_tool != TOOL_NONE && s_tool != TOOL_QUERY) { tools_select(TOOL_NONE, 0); return TRES_CANCELLED; }
        if (in->on_map) { s_query_x = tx; s_query_y = ty; return TRES_QUERY; }
        return TRES_NONE;
    }
    if (s_tool == TOOL_NONE || s_tool == TOOL_QUERY) {
        s_prev.active = 0;
        if (in->pressed && in->on_map) { s_query_x = tx; s_query_y = ty; return TRES_QUERY; }
        return TRES_NONE;
    }
    if (s_tool == TOOL_BUILD) {
        if (!in->on_map) { s_prev.active = 0; s_plan_valid = 0; return TRES_NONE; }
        if (in->pressed) {
            n = commit(TOOL_BUILD, s_param, tx, ty, tx, ty, 0, 1, 1);
            res = n ? TRES_COMMITTED : TRES_DENIED;
        }
        plan_cached(TOOL_BUILD, s_param, tx, ty, tx, ty, 0, 1, 1);
        return res;
    }
    if (!line && !rect) return TRES_NONE;
    if (in->pressed && in->on_map && !s_drag) {
        s_drag = 1; s_ax = s_bx = tx; s_ay = s_by = ty; s_xfirst = 1; s_xfirst_set = 0;
    }
    if (s_drag) {
        if (in->on_map) { s_bx = tx; s_by = ty; }
        if (!s_xfirst_set && (s_bx != s_ax || s_by != s_ay)) { s_xfirst = IABS(s_bx - s_ax) >= IABS(s_by - s_ay); s_xfirst_set = 1; }
        if (in->released || !in->down) {
            s_drag = 0;
            n = commit(s_tool, prm, s_ax, s_ay, s_bx, s_by, in->shift, s_xfirst, 0);
            res = n ? TRES_COMMITTED : TRES_DENIED;
            s_plan_valid = 0;
            if (in->on_map) plan_cached(s_tool, prm, tx, ty, tx, ty, 0, 1, 0);
            else s_prev.active = 0;
            return res;
        }
        plan_cached(s_tool, prm, s_ax, s_ay, s_bx, s_by, in->shift, s_xfirst, 0);
        return TRES_NONE;
    }
    if (in->on_map) plan_cached(s_tool, prm, tx, ty, tx, ty, 0, 1, 0);
    else { s_prev.active = 0; s_plan_valid = 0; }
    return TRES_NONE;
}

/* ================================================================== script interpreter */
static int s_org_x, s_org_y;
static char s_dir[PATH_LEN];
void tools_script_reset(void) { s_org_x = s_org_y = 0; }
void tools_script_set_dir(const char *dir) { str_copy(s_dir, dir ? dir : "", PATH_LEN); }

static const char *out_path(const char *name)
{
    static char buf[PATH_LEN];
    if (name[0] == '/' || name[0] == '\\' || (name[0] && name[1] == ':')) return name;
    str_printf(buf, sizeof(buf), "%s%s", s_dir, name);
    return buf;
}

static int split(char *s, char **tok, int max)
{
    int n = 0;
    while (*s && n < max) {
        while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' || *s == ',') s++;
        if (!*s || *s == '#') break;
        tok[n++] = s;
        while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n' && *s != ',') s++;
        if (*s) *s++ = 0;
    }
    return n;
}

static int road_class_name(const char *s)
{
    if (str_ieq(s, "gravel") || str_ieq(s, "grav")) return ROAD_GRAVEL;
    if (str_ieq(s, "street") || str_ieq(s, "road")) return ROAD_STREET;
    if (str_ieq(s, "main")) return ROAD_MAIN;
    if (str_ieq(s, "motorway") || str_ieq(s, "mway")) return ROAD_MOTORWAY;
    return -1;
}

static int zone_name(const char *s)
{
    if (str_ieq(s, "houses") || str_ieq(s, "h")) return ZONE_HOUSES;
    if (str_ieq(s, "apartments") || str_ieq(s, "a")) return ZONE_APARTMENTS;
    if (str_ieq(s, "commercial") || str_ieq(s, "c")) return ZONE_COMMERCIAL;
    if (str_ieq(s, "industry") || str_ieq(s, "i")) return ZONE_INDUSTRY;
    if (str_ieq(s, "forestry") || str_ieq(s, "f")) return ZONE_FORESTRY;   /* M3 */
    if (str_ieq(s, "farm") || str_ieq(s, "farms")) return ZONE_FARM;
    return -1;
}

static s32 stat_value(const char *s, int *ok)
{
    const CityStats *st = sim_stats();
    *ok = 1;
    if (str_ieq(s, "pop")) return st->population;
    if (str_ieq(s, "money")) return g_budget.money;
    if (str_ieq(s, "jobs")) return st->jobs;
    if (str_ieq(s, "unemployed")) return st->unemployed;
    if (str_ieq(s, "lots")) return st->lots_zone;
    if (str_ieq(s, "houses2")) return st->lots_tier[ZONE_HOUSES][2];
    if (str_ieq(s, "status")) return st->town_status;
    if (str_ieq(s, "homeless")) return st->homeless;
    if (str_ieq(s, "emigrated")) return st->emigrated_total;
    if (str_ieq(s, "loan")) return g_budget.loan;
    if (str_ieq(s, "undo")) return tools_undo_count();
    if (str_ieq(s, "farms")) return industry_milk()->farm_lots;                 /* M3 */
    if (str_ieq(s, "camps")) return industry_wood()->camps;
    if (str_ieq(s, "sawmills")) return industry_kinds()->lots[IK_WOOD];
    if (str_ieq(s, "dairies")) return industry_kinds()->lots[IK_FOOD];
    if (str_ieq(s, "wood_pct")) return industry_wood()->pct;
    if (str_ieq(s, "wood_cut")) return industry_wood()->cut_tiles_total;
    return civic_stat_value(s, ok);                                              /* M5: heated, fires, apt3 ... */
}

static void dump_tools(FILE *f)
{
    int r[ROAD_CLASS_COUNT], b[ROAD_CLASS_COUNT], lines, i, z;
    world_road_stats(r, b, &lines);
    fprintf(f, "tools: roads");
    for (i = 1; i < ROAD_CLASS_COUNT; i++) fprintf(f, " %s %d (bridges %d)", g_roads[i].key, r[i], b[i]);
    fprintf(f, ", power lines %d, pipes %d, zones", lines, world_pipe_tiles(NULL));
    for (z = 1; z < ZONE_COUNT; z++) fprintf(f, " %d", world_count_zone(z));
    fprintf(f, ", lots %d, money %d, undo %d/%d, world %08x\n", g_world.lot_count, g_budget.money, s_cur, s_ncmds, world_hash());
}

static int out_err(FILE *out, const char *line, const char *why)
{
    if (out) fprintf(out, "script: error: %s: %s\n", why, line);
    log_printf("script: error: %s: %s", why, line);
    return SCR_ERROR;
}

int tools_script_line(const char *line, int *wait_months, FILE *out)
{
    char buf[256], *t[12], msg[96];
    int n, a[6], i, r;
    *wait_months = 0;
    str_copy(buf, line, sizeof(buf));
    n = split(buf, t, 12);
    if (n == 0) return SCR_OK;
    for (i = 0; i < 6; i++) a[i] = (i + 1 < n) ? atoi(t[i + 1]) : 0;
#define XY(k) (a[k] + ((k) % 2 ? s_org_y : s_org_x))
    if (str_ieq(t[0], "road")) {
        int cls = ROAD_STREET, straight = 0, bx, by;
        if (n < 5) return out_err(out, line, "road needs X0 Y0 X1 Y1");
        for (i = 5; i < n; i++) { if (str_ieq(t[i], "straight")) straight = 1; else if (road_class_name(t[i]) > 0) cls = road_class_name(t[i]); }
        bx = XY(2); by = XY(3);
        if (straight) { if (IABS(bx - XY(0)) >= IABS(by - XY(1))) by = XY(1); else bx = XY(0); }
        r = tools_apply_line(TOOL_ROAD, cls, XY(0), XY(1), bx, by);
        if (out) fprintf(out, "script: %s -> %d tiles (%d rejected, reason %d), %d mk\n", line, r, s_prev.n_rejected, s_prev.main_reason, r ? s_last_cost : 0);
        if (!r && (s_last_reason == REJ_NO_MONEY || s_last_reason == REJ_LOCKED)) return out_err(out, line, "denied");
        return SCR_OK;
    }
    if (str_ieq(t[0], "power") || str_ieq(t[0], "line")) {
        if (n < 5) return out_err(out, line, "power needs X0 Y0 X1 Y1");
        r = tools_apply_line(TOOL_POWER_LINE, 0, XY(0), XY(1), XY(2), XY(3));
        if (out) fprintf(out, "script: %s -> %d tiles (%d rejected, reason %d), %d mk\n", line, r, s_prev.n_rejected, s_prev.main_reason, r ? s_last_cost : 0);
        if (!r && (s_last_reason == REJ_NO_MONEY || s_last_reason == REJ_LOCKED)) return out_err(out, line, "denied");
        return SCR_OK;
    }
    if (str_ieq(t[0], "pipe")) {                      /* F1 */
        int rm = n >= 6 && str_ieq(t[5], "remove");
        if (n < 5) return out_err(out, line, "pipe needs X0 Y0 X1 Y1");
        r = tools_apply_line(TOOL_PIPE, rm, XY(0), XY(1), XY(2), XY(3));
        if (out) fprintf(out, "script: %s -> %d tiles (%d rejected, reason %d), %d mk\n", line, r, s_prev.n_rejected, s_prev.main_reason, r ? s_last_cost : 0);
        if (!r && (s_last_reason == REJ_NO_MONEY || s_last_reason == REJ_LOCKED)) return out_err(out, line, "denied");
        return SCR_OK;
    }
    if (str_ieq(t[0], "pipes_with_roads")) {          /* F1 */
        tools_set_pipes_with_roads(n >= 2 && (str_ieq(t[1], "on") || str_ieq(t[1], "1")));
        return SCR_OK;
    }
    if (str_ieq(t[0], "zone") || str_ieq(t[0], "dezone") || str_ieq(t[0], "bulldoze")) {
        int tool = str_ieq(t[0], "zone") ? TOOL_ZONE : str_ieq(t[0], "dezone") ? TOOL_DEZONE : TOOL_BULLDOZE, z = 0;
        if (n < 5) return out_err(out, line, "rect needs X0 Y0 X1 Y1");
        if (tool == TOOL_ZONE) { if (n < 6 || (z = zone_name(t[5])) < 0) return out_err(out, line, "unknown zone"); }
        r = tools_apply_rect(tool, z, XY(0), XY(1), XY(2), XY(3));
        if (out) fprintf(out, "script: %s -> %d tiles (%d rejected, reason %d), %d mk\n", line, r, s_prev.n_rejected, s_prev.main_reason, r ? s_last_cost : 0);
        if (!r && (s_last_reason == REJ_NO_MONEY || s_last_reason == REJ_LOCKED)) return out_err(out, line, "denied");
        return SCR_OK;
    }
    if (str_ieq(t[0], "build")) {
        int def, x, y, rot = -1;
        if (n < 4) return out_err(out, line, "build needs KEY X Y");
        def = def_building_find(t[1]);
        if (def == B_NONE) return out_err(out, line, "unknown building");
        x = atoi(t[2]) + s_org_x; y = atoi(t[3]) + s_org_y;
        if (n >= 5) rot = atoi(t[4]);
        r = tools_apply_build(def, x, y, rot);
        if (out) fprintf(out, "script: %s -> lot %d rot %d, %d mk\n", line, r, s_prev.rot, r ? s_last_cost : 0);
        if (!r) { str_printf(msg, sizeof(msg), "build failed (reason %d)", s_last_reason); return out_err(out, line, msg); }
        return SCR_OK;
    }
    if (str_ieq(t[0], "townhall")) {
        const MapGenReport *mr = mapgen_report();
        int tx, ty;
        if (tools_town_hall(&tx, &ty)) return SCR_OK;
        r = tools_auto_town_hall(mr->town_x, mr->town_y);
        if (out) fprintf(out, "script: townhall -> lot %d\n", r);
        return r ? SCR_OK : out_err(out, line, "no town hall site");
    }
    if (str_ieq(t[0], "origin")) {
        if (n >= 2 && str_ieq(t[1], "town")) { s_org_x = mapgen_report()->town_x; s_org_y = mapgen_report()->town_y; }
        else if (n >= 3) { s_org_x = a[0]; s_org_y = a[1]; }
        if (out) fprintf(out, "script: origin %d,%d\n", s_org_x, s_org_y);
        return SCR_OK;
    }
    if (str_ieq(t[0], "undo")) { r = tools_undo(); if (out) fprintf(out, "script: undo -> %d\n", r); return SCR_OK; }
    if (str_ieq(t[0], "redo")) { r = tools_redo(); if (out) fprintf(out, "script: redo -> %d\n", r); return SCR_OK; }
    if (str_ieq(t[0], "speed")) { sim_set_speed(a[0]); return SCR_OK; }
    if (str_ieq(t[0], "tax")) { budget_set_tax(a[0], n >= 3 ? a[1] : a[0]); return SCR_OK; }
    if (str_ieq(t[0], "funding")) {
        int svc = -1;
        for (i = 0; i < SVC_COUNT; i++) if (n >= 2 && str_ieq(t[1], g_services[i].key)) svc = i;
        if (svc < 0 || n < 3) return out_err(out, line, "funding SVC PCT");
        budget_set_funding(svc, atoi(t[2]));
        return SCR_OK;
    }
    if (str_ieq(t[0], "wait")) { *wait_months = IMAX(1, a[0]); return SCR_WAIT; }
    if (str_ieq(t[0], "pol")) return politics_script(t, n, out) ? SCR_OK : out_err(out, line, "politics refused");   /* M1 */
    if (str_ieq(t[0], "statlog")) { if (n >= 2) sim_statlog_open(out_path(t[1])); return SCR_OK; }
    if (str_ieq(t[0], "log")) { if (out) fprintf(out, "script: %s\n", line); log_printf("script: %s", line); return SCR_OK; }
    if (str_ieq(t[0], "dump")) {
        FILE *f = out, *own = NULL;
        if (n >= 3) { own = fopen(out_path(t[2]), "a"); if (own) f = own; }
        if (f) {
            if (n >= 2 && str_ieq(t[1], "map")) world_dump(f, 0, 0, g_world.w - 1, g_world.h - 1);
            else if (n >= 2 && str_ieq(t[1], "tools")) dump_tools(f);
            else if (n >= 2 && str_ieq(t[1], "industry")) industry_dump(f);    /* M3 */
            else if (n >= 2 && str_ieq(t[1], "complex")) complex_dump(f);      /* M6 */
            else if (n >= 2 && str_ieq(t[1], "civic")) civic_dump(f);          /* M5 */
            else if (n >= 2 && !strncmp(t[1], "ovl", 3)) {                    /* P1: `dump ovl17 FILE` = overlay values */
                int ov = atoi(t[1] + 3), x, y;
                fprintf(f, "overlay %d values (. none, else the level / category digit)\n", ov);
                for (y = 0; y < g_world.h; y++) {
                    for (x = 0; x < g_world.w; x++) {
                        u8 v = sim_overlay_value(ov, 0, x, y);
                        fputc(v == OVV_NONE ? '.' : '0' + OVV_LEVEL(v), f);
                    }
                    fputc('\n', f);
                }
            }
            else sim_dump_report(f);
            fflush(f);
        }
        if (own) fclose(own);
        return SCR_OK;
    }
    if (str_ieq(t[0], "expect")) {
        s32 v, want;
        int ok, pass = 0;
        if (n < 4) return out_err(out, line, "expect STAT OP VALUE");
        v = stat_value(t[1], &ok);
        if (!ok) return out_err(out, line, "unknown stat");
        want = atoi(t[3]);
        if (!strcmp(t[2], "<")) pass = v < want;
        else if (!strcmp(t[2], "<=")) pass = v <= want;
        else if (!strcmp(t[2], ">")) pass = v > want;
        else if (!strcmp(t[2], ">=")) pass = v >= want;
        else if (!strcmp(t[2], "==")) pass = v == want;
        else return out_err(out, line, "bad operator");
        if (out) fprintf(out, "script: expect %s %s %d: %d %s\n", t[1], t[2], want, v, pass ? "ok" : "FAILED");
        if (!pass) { str_printf(msg, sizeof(msg), "expectation failed (%s = %d)", t[1], v); return out_err(out, line, msg); }
        return SCR_OK;
    }
#undef XY
    r = complex_script_line(n, t, s_org_x, s_org_y, out);         /* M6: complex place|gift|mood|dump */
    if (r >= 0) return r == SCR_ERROR ? out_err(out, line, "complex") : r;
    r = transit_script_line(n, t, s_org_x, s_org_y, out);         /* M2: busline ... */
    if (r >= 0) return r == SCR_ERROR ? out_err(out, line, "busline") : r;
    r = civic_script_line(n, t, s_org_x, s_org_y, out);            /* M5: fire X Y */
    if (r >= 0) return r == SCR_ERROR ? out_err(out, line, "fire") : r;
    r = era_script_line(n, t, out);                                /* M7: trade ... / era dump / news ... */
    if (r >= 0) return r == SCR_ERROR ? out_err(out, line, "trade/era") : r;
    r = season_script_line(n, t, out);                             /* 1.1 W1: season dump | season force hard|ordinary */
    if (r >= 0) return r == SCR_ERROR ? out_err(out, line, "season") : r;
    r = power_script_line(n, t, out);                              /* 1.2 S1: reactor east|west|loviisa | reactor dump */
    if (r >= 0) return r == SCR_ERROR ? out_err(out, line, "reactor") : r;
    return SCR_UNKNOWN;
}

int tools_script_run_file(const char *path, FILE *out)
{
    FILE *f = fopen(path, "r");
    char line[256];
    int errors = 0, wait, r, lineno = 0;
    if (!f) { log_printf("script: error: cannot open %s", path); return -1; }
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        r = tools_script_line(line, &wait, out);
        if (r == SCR_WAIT) {
            SimEvent ev;
            sim_run_months(wait);
            while (sim_poll_event(&ev)) {}     /* headless: nobody shows them */
            tools_month_changed();             /* what game.cpp does on SE_MONTH */
        }
        else if (r == SCR_ERROR) errors++;
        else if (r == SCR_UNKNOWN) { if (out) fprintf(out, "script: line %d ignored headless: %s", lineno, line); }
    }
    fclose(f);
    log_printf("script: %s done, %d lines, %d errors", path, lineno, errors);
    return errors;
}
