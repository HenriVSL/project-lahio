/* lifebus.cpp - cosmetic buses on the bus lines. PORTABLE. Owner: M2. See lifebus.h. */
#include "common.h"
#include "lifebus.h"
#include "lifecar.h"
#include "transit.h"
#include "world.h"
#include "defs.h"

#define LB_SPEED  (210 << 8)          /* fx*256 per second (a bit slower than a street car) */
#define LB_DWELL  1500

typedef struct {
    s16 idx;                          /* path index of the tile */
    s8  step;                         /* +1 / -1 along the path */
    u8  stop_pending;
    u16 dwell;
    LcCar c;                          /* geometry only: x, y, din, dout, lat0, lat, s, len */
} LbState;

static LbBus   s_bus[LB_BUSES_MAX];
static LbState s_st[LB_BUSES_MAX];
static u32     s_serial;
static int     s_n;

static s32 arc_len(s32 r) { return (r * 402 + 128) >> 8; }
static s32 semi_len(s32 r) { return (r * 804 + 128) >> 8; }

static int dir_between(int x0, int y0, int x1, int y1)
{
    int d;
    for (d = 0; d < DIR_COUNT; d++) if (x0 + g_dir_dx[d] == x1 && y0 + g_dir_dy[d] == y1) return d;
    return DIR_SE;
}

static int is_stop(const BusLine *l, int idx)
{
    int s;
    for (s = 0; s < l->nstops; s++) if (l->spos[s] == idx) return 1;
    return 0;
}

static void enter_tile(LbState *b, const BusLine *l)
{
    int i = b->idx, prev = i - b->step, next = i + b->step, cls;
    LcCar *c = &b->c;
    c->x = l->px[i]; c->y = l->py[i];
    if (prev >= 0 && prev < l->npath) c->din = (u8)dir_between(l->px[prev], l->py[prev], c->x, c->y);
    else if (next >= 0 && next < l->npath) c->din = (u8)dir_between(c->x, c->y, l->px[next], l->py[next]);
    if (next >= 0 && next < l->npath) c->dout = (u8)dir_between(c->x, c->y, l->px[next], l->py[next]);
    else c->dout = (u8)DIR_OPPOSITE(c->din);
    cls = world_in(c->x, c->y) ? TILE_ROAD(WT(c->x, c->y)) : ROAD_STREET;
    c->lat = c->lat0 = (s16)lc_lane_offset(cls ? cls : ROAD_STREET, 0);
    if (c->dout == c->din) c->len = 256 << 8;
    else if (c->dout == DIR_OPPOSITE(c->din)) c->len = (256 + semi_len(c->lat)) << 8;
    else c->len = arc_len(c->dout == ((c->din + 1) & 3) ? 128 - c->lat : 128 + c->lat) << 8;
    b->stop_pending = (u8)is_stop(l, i);
}

static void respawn(void)
{
    int L, j;
    memset(s_bus, 0, sizeof(s_bus));
    s_n = 0;
    for (L = 0; L < BUS_MAX_LINES; L++) {
        const BusLine *l = transit_line(L);
        int nb;
        if (!l->used || l->status != BLS_OK || l->npath < 2) continue;
        nb = IMIN(LB_PER_LINE, IMAX(1, l->buses));
        for (j = 0; j < nb && s_n < LB_BUSES_MAX; j++) {
            LbState *b = &s_st[s_n];
            memset(b, 0, sizeof(*b));
            b->idx = (s16)(j * (l->npath - 1) / nb);
            b->step = (s8)((j & 1) ? -1 : 1);
            if (b->idx == 0) b->step = 1;
            enter_tile(b, l);
            b->c.s = 0;
            s_bus[s_n].used = 1; s_bus[s_n].line = (u8)L;
            s_n++;
        }
    }
}

void lb_reset(void) { s_serial = transit_serial() - 1; s_n = 0; memset(s_bus, 0, sizeof(s_bus)); }

void lb_update(int dt_ms, int frozen)
{
    int i;
    if (s_serial != transit_serial()) { s_serial = transit_serial(); respawn(); }
    for (i = 0; i < s_n; i++) {
        LbState *b = &s_st[i];
        LbBus *o = &s_bus[i];
        const BusLine *l = transit_line(o->line);
        int dir;
        if (!o->used) continue;
        if (!frozen && dt_ms > 0) {
            if (b->dwell) b->dwell = (u16)(b->dwell > dt_ms ? b->dwell - dt_ms : 0);
            else {
                b->c.s += (s32)((s64)LB_SPEED * dt_ms / 1000);
                if (b->stop_pending && b->c.s >= b->c.len / 2) { b->stop_pending = 0; b->dwell = LB_DWELL; b->c.s = b->c.len / 2; }
                while (b->c.s >= b->c.len) {
                    b->c.s -= b->c.len;
                    if (b->c.dout == DIR_OPPOSITE(b->c.din)) b->step = (s8)-b->step;
                    b->idx = (s16)(b->idx + b->step);
                    if (b->idx < 0 || b->idx >= l->npath) { b->idx = (s16)ICLAMP(b->idx, 0, l->npath - 1); b->step = (s8)-b->step; }
                    enter_tile(b, l);
                }
            }
        }
        lc_car_point(&b->c, &o->fx, &o->fy, &dir);
        o->x = b->c.x; o->y = b->c.y; o->dir = (u8)dir; o->stopped = (u8)(b->dwell > 0);
    }
}

int lb_buses(const LbBus **out) { *out = s_bus; return s_n; }
