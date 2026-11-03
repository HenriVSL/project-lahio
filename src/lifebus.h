/* lifebus.h - cosmetic buses driving back and forth along the bus lines (transit.h). PORTABLE (Mac-tested in
   tests/test_traffic.cpp). Owner: M2. Cosmetic only: own state, never saved, never read by the simulation.
   Up to LB_PER_LINE buses per running line (the line's bus count, capped), spread along the route, right-hand
   lane like the cars (lifecar.h geometry: lc_car_point), a 1.5 s stop at every bus stop, U-turn at the ends.
   life.cpp calls lb_update() with the real frame time and draws the sprites bus_<ny|px|py|nx>. */
#ifndef LIFEBUS_H
#define LIFEBUS_H
#include "common.h"

#define LB_PER_LINE 3
#define LB_BUSES_MAX      24                /* BUS_MAX_LINES x LB_PER_LINE */

typedef struct {
    u8  used, line, x, y;             /* line index, tile */
    u8  dir;                          /* sprite heading DIR_* */
    u8  stopped;                      /* waiting at a stop */
    u8  pad[2];
    s32 fx, fy;                       /* position (1/256 tile, iso.h) */
} LbBus;

void lb_reset(void);
void lb_update(int dt_ms, int frozen);  /* frozen: no movement (paused); still follows line changes */
int  lb_buses(const LbBus **out);       /* LB_BUSES_MAX slots, check .used; returns the number used */

#endif
