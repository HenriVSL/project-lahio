/* transit.h - bus lines and stops, bus depot rule, railway station (regional migrants, freight access).
   PORTABLE, deterministic. Owner: M2. DESIGN 5.8 (buses), 4.1 (railway station), DESIGN_LESSONS 4.7.

   Bus lines: the bus line tool (TOOL_BUS_LINE, tools.h) clicks STOPS on car-road tiles; the line follows the
   shortest road path (fewest tiles) between consecutive stops and buses run back and forth along it. Each
   new stop also gets a small "bus_stop" shelter lot on a free unzoned grass tile next to the road when there
   is one (decoration; the stop itself is the road tile). At most BUS_MAX_LINES lines, BUS_MAX_STOPS stops
   each. Cost BUS_LINE_COST per new line + BUS_STOP_COST per stop; upkeep BUS_UPKEEP per bus per month
   (buses = 1 per BUS_TILES_PER_BUS route tiles, max BUS_MAX_BUSES), booked on BL_SPECIAL. Lines run only while
   the town has an active bus depot (B_BUS_DEPOT). Ctrl+click a stop with the tool deletes its line;
   right-click ends the line being drawn.
   Riders: traffic.cpp's mode split (bus time vs car / walk time, car ownership) assigns rides to lines and
   writes the ridership back at each pass end (transit_set_riders).

   Railway station (B_STATION, unique): placed with its back (-Y edge at rot 0, the platform side) along the
   railway; the build tool auto-rotates it and refuses sites without rail behind (REJ_NEEDS_RAIL). An active
   station with road access brings regional migrants (R demand term DT_REGIONAL) and rail freight (I demand
   term DT_FREIGHT); traffic_rail_access(x,y) tells M3's industry kinds whether a site has rail freight. */
#ifndef TRANSIT_H
#define TRANSIT_H
#include "common.h"
#include "tools.h"

#define BUS_MAX_LINES     8
#define BUS_MAX_STOPS     16
#define BUS_MAX_PATH      512
#define BUS_MAX_BUSES     6
#define BUS_TILES_PER_BUS 16
#define BUS_WALK          8         /* a graph node within this Chebyshev distance of a stop walks to it */
#define BUS_LINE_COST     300
#define BUS_STOP_COST     40
#define BUS_UPKEEP        12        /* mk per bus per month */

enum { BLS_OK, BLS_ONE_STOP, BLS_NO_DEPOT, BLS_BROKEN, BLS_COUNT };   /* line status */

typedef struct {
    u8  used, nstops, status, number;   /* number = 1..BUS_MAX_LINES shown to the player */
    u8  sx[BUS_MAX_STOPS], sy[BUS_MAX_STOPS];
    u16 slot_lot[BUS_MAX_STOPS];        /* shelter lot placed for the stop (0 none) */
    u16 spos[BUS_MAX_STOPS];            /* index of each stop in the path */
    u16 npath, buses;
    u8  px[BUS_MAX_PATH], py[BUS_MAX_PATH];
    s32 riders;                         /* rides/day in the last complete traffic pass */
} BusLine;

void transit_init(void);
void transit_reset(void);                  /* new city / load: no lines */
const BusLine *transit_line(int i);        /* 0..BUS_MAX_LINES-1 (check ->used) */
int  transit_line_count(void);
u32  transit_serial(void);                 /* bumped by every line change (cosmetic buses reset) */
int  transit_active_lines(void);           /* lines with status BLS_OK */
s32  transit_total_riders(void);
Money transit_upkeep(void);                /* per month for all running buses */
int  transit_tile_lines(int x, int y);     /* bit mask of lines whose route uses the road tile */
int  transit_stop_line_at(int x, int y);   /* line index with a stop on (x,y), -1 none */

/* editing (tool, scripts, tests). Return a line index or -1. */
int  transit_new_line(void);               /* charges BUS_LINE_COST; -1 if full or no money */
int  transit_add_stop(int line, int x, int y);   /* charges BUS_STOP_COST; 1 ok, 0 refused (reason below) */
int  transit_delete_line(int line);        /* removes its shelters (not shared with other lines) */
int  transit_last_reason(void);            /* REJ_* of the last refusal */
int  transit_path(int x0, int y0, int x1, int y1, u8 *px, u8 *py, int max);  /* road path (tiles incl. both
                                              ends), 0 = no route; car roads with traffic_link_ok links */

/* traffic.cpp hooks (pass start/end). */
void transit_pass_prepare(void);           /* revalidate routes after road changes, depot/station state */
void transit_set_riders(int line, s32 riders);
int  transit_depots(void);                 /* active bus depots (at the last pass start) */
void transit_month(void);                  /* traffic_step on the last cycle of a month: bus upkeep */

/* railway station */
int  transit_station_ok(int x0, int y0, int rot);    /* footprint at rot has rail along its back edge */
int  transit_station_rot(int x0, int y0);            /* best rotation for a 2x2 station at x0,y0, -1 none */
int  transit_stations(void);               /* active stations with rail + road access (last pass start) */
int  transit_station_lot(int i);           /* lot id of active station i */
s32  transit_regional_migrants(s32 rural_pull);      /* DT_REGIONAL people/month (0 without a station) */
s32  transit_freight_jobs(s32 export_jobs);          /* DT_FREIGHT jobs (0 without a station) */

/* tool glue (tools.cpp delegates TOOL_BUS_LINE here) */
void transit_tool_select(int on);          /* tool chosen / left: ends the line being drawn */
int  transit_tool_editing(void);           /* 1 while a line is being drawn (right-click ends it) */
int  transit_tool_update(const ToolInput *in, ToolPreview *p, int *lx, int *ly, Money *lcost, int *lreason);

/* text for ui (line panel in the bus depot / stop query, tool hint) */
int  transit_line_text(int line, char *buf, int size);   /* "Line 2: 6 stops, 3 buses, 840 rides/day" */
const char *transit_status_text(int status);

/* script commands (tools_script_line falls back here): busline X0 Y0 X1 Y1 [... up to 5 stops] |
   busline add N X Y (one more stop on line N) | busline delete N */
int  transit_script_line(int ntok, char **tok, int org_x, int org_y, FILE *out);   /* SCR_*; -1 not ours */

struct SaveFile;
void transit_save(struct SaveFile *sf);    /* written inside the TRAF chunk (v2+) */
void transit_load(struct SaveFile *sf);

#endif
