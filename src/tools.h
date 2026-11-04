/* tools.h - build tools, drag preview (cost, rejected tiles, effects), validation, undo/redo.
   PORTABLE (logic only: input arrives as tile coordinates; render/ui draw the preview). Owner: T08.
   DESIGN 4 (mouse grammar, 4.1-4.6), 0.2 pillar 3.

   Mouse grammar: left-drag builds with a live preview; release commits; right-click cancels a drag;
   right-click with no tool (or TOOL_QUERY click) = query. Roads/power lines drag an L-shaped path
   (Shift = straight only); zones/dezone/bulldoze drag a rectangle; buildings follow the cursor (one
   footprint, rotation auto-faces an adjacent road unless the player rotated manually).
   Commits go through world setters inside world_batch_begin/end and charge budget_spend(BL_CONSTRUCTION).
   Undo/redo: unlimited within the current game month (inverse ops + refunds); tools_month_changed()
   (game.cpp, on SE_MONTH) clears the log because the simulation has moved on. Entries that can no longer
   be reverted cleanly (e.g. a zone tile that has grown a building since) are reverted partially and the
   rest skipped. */
#ifndef TOOLS_H
#define TOOLS_H
#include "common.h"
#include "defs.h"

enum { TOOL_NONE,
       TOOL_QUERY,
       TOOL_ROAD,          /* param = ROAD_* (drag over a lower class upgrades in place) */
       TOOL_POWER_LINE,
       TOOL_ZONE,          /* param = ZONE_* */
       TOOL_DEZONE,
       TOOL_BUILD,         /* param = B_* placeable building */
       TOOL_BULLDOZE,
       TOOL_PIPE,          /* F1: water pipe, L-path like power lines; param 0 = lay, 1 = remove (Ctrl+drag) */
       TOOL_BUS_LINE,      /* M2: click stops on roads (transit.h); right-click ends the line, Ctrl+click deletes */
       TOOL_GIFT,          /* M6: place the patron's next gift free near the complex (complex.h); param unused (0) */
       TOOL_COUNT };

typedef struct {
    int tx, ty;            /* tile under the cursor (ground pick) */
    int on_map;            /* cursor is over the map view on a valid tile */
    int down;              /* left button held */
    int pressed, released; /* left button edges this frame */
    int cancel;            /* right button pressed */
    int shift, ctrl;
} ToolInput;

/* Rejection reasons (PreviewTile.reason, ToolPreview.main_reason). 1..PLACE_COUNT-1 reuse PLACE_* (world.h). */
enum { REJ_NONE = 0, REJ_NO_ROAD_REACH = 32, REJ_LOCKED, REJ_NO_MONEY, REJ_BRIDGE_TOO_LONG,
       REJ_BRIDGE_NOT_STRAIGHT, REJ_NOTHING_TO_DO, REJ_PROTECTED_TILE, REJ_WRONG_TERRAIN,
       REJ_NEEDS_RAIL, REJ_BUS_NOT_ROAD, REJ_BUS_MAX, REJ_BUS_NO_ROUTE,   /* M2 (transit.h) */
       REJ_CPX_WATER, REJ_CPX_RAIL, REJ_CPX_FAR, REJ_CPX_NO_GIFT, REJ_CPX_ROAD,     /* M6 (complex.h) */
       REJ_NO_RAILWAY,     /* PL2: the station on a map without a railway */
       REJ_CPX_MAIN_ROAD };/* PL2: Valtion Metalli on a map without a railway: needs a main road within 2 tiles */
enum { PT_OK, PT_UPGRADE, PT_NOOP, PT_REJECT };
typedef struct { u8 x, y, state, reason; } PreviewTile;
#define TOOL_MAX_PREVIEW 2048

typedef struct {
    int   active;          /* something to draw (hover or drag) */
    int   dragging;
    int   tool, param, rot;
    int   x0, y0, x1, y1;  /* bounding tile rect of the operation */
    int   n;               /* entries in tiles[] (path tiles, or rect tiles up to the cap) */
    int   truncated;       /* rect larger than TOOL_MAX_PREVIEW: tiles[] holds only rejected ones */
    PreviewTile tiles[TOOL_MAX_PREVIEW];
    int   n_ok, n_rejected;
    int   main_reason;     /* most common rejection (REJ_* / PLACE_*) */
    Money cost;            /* total: build + clearing forest/rock + bridges + upgrade differences - refunds */
    int   affordable;
    /* projected effects shown in the tooltip */
    s32   eff_residents, eff_jobs;     /* zone capacity at tier 1 / building capacity */
    s32   eff_power, eff_water;        /* supply added (+) or demand added (-) */
    Money eff_upkeep;                  /* monthly upkeep change */
    /* building placement ghost */
    int   lot_def, lot_x, lot_y, lot_w, lot_h, lot_ok;   /* north tile + footprint at rot */
} ToolPreview;

enum { TRES_NONE, TRES_COMMITTED, TRES_DENIED, TRES_QUERY, TRES_CANCELLED };

void  tools_init(void);
void  tools_reset(void);                     /* new city / load: no tool, empty undo log */
void  tools_select(int tool, int param);
int   tools_tool(int *param);
void  tools_rotate(int delta);               /* building rotation +-1; disables auto-facing */
int   tools_update(const ToolInput *in);     /* once per frame; TRES_*; TRES_QUERY: see tools_query_tile */
const ToolPreview *tools_preview(void);
void  tools_query_tile(int *x, int *y);      /* tile of the last TRES_QUERY */
void  tools_last_commit(int *x, int *y, Money *cost);  /* for the floating "-450 mk" */

/* Scripted use (tests, -simtest town, bench city): same validation and costs as the mouse path. */
int   tools_apply_line(int tool, int param, int x0, int y0, int x1, int y1); /* L-path; tiles changed */
int   tools_apply_rect(int tool, int param, int x0, int y0, int x1, int y1); /* tiles changed */
int   tools_apply_build(int def, int x, int y, int rot);                     /* lot id or 0 */

int   tools_undo(void);                      /* 1 if something was undone */
int   tools_redo(void);
int   tools_can_undo(void);
int   tools_can_redo(void);
void  tools_month_changed(void);

/* ---- T08 additions -------------------------------------------------------------------------------
   Preview helpers (render the drag preview as real road/power pieces): autotile mask of a preview tile
   (existing links + other non-rejected preview tiles), 0..15 bits (1 << DIR_*); -1 if (x, y) is not in
   the current preview path. */
int   tools_preview_mask(int x, int y);
int   tools_rotation(void);                  /* current building rotation (auto or manual) */
int   tools_dragging(void);
/* Town hall: auto placement next to a road tile (mapgen_report()->town_x/y), facing it, free, not
   undoable. Returns the lot id or 0. tools_town_hall: front tile of the existing town hall (Home key). */
int   tools_auto_town_hall(int road_x, int road_y);
int   tools_town_hall(int *x, int *y);
/* Last commit / denial details for tooltips and sounds. */
int   tools_last_reason(void);               /* REJ_* / PLACE_* of the last TRES_DENIED (0 none) */
int   tools_last_tool(void);                 /* tool of the last commit or denial */
int   tools_undo_count(void);                /* commands in the undo log (tests) */

/* ---- script interpreter (DEBUG FROM TEXT): one command per line, same code path as the mouse.
   Coordinates are tiles, relative to the origin ("origin X Y" / "origin town"; default 0,0).
     road X0 Y0 X1 Y1 [gravel|street|main] [straight]   L-path (x first when |dx| >= |dy|)
     power X0 Y0 X1 Y1                                  power line (L-path)
     zone X0 Y0 X1 Y1 houses|apartments|commercial|industry
     dezone X0 Y0 X1 Y1 | bulldoze X0 Y0 X1 Y1
     build KEY X Y [ROT]                                north tile; ROT omitted = face the nearest road
     townhall                                           auto-place at mapgen's town site
     undo | redo | speed N | tax RES BUS | funding SVC PCT | wait MONTHS
     dump report|map|tools [FILE]                       to FILE (appended) or the script's output
     expect STAT OP VALUE                               STAT pop money jobs unemployed lots houses2
                                                        status homeless emigrated; OP < <= > >= ==
     statlog FILE | log TEXT | # comment
   tools_script_line returns SCR_WAIT with *wait_months for "wait" (the caller runs the sim: headless
   sim_run_months, in game real time at the current speed), SCR_UNKNOWN for commands the game handles
   (cam, zoom, overlay, shot, quit), SCR_ERROR for a failed build/expect/parse (message written to out). */
enum { SCR_OK, SCR_WAIT, SCR_UNKNOWN, SCR_ERROR };
int   tools_script_line(const char *line, int *wait_months, FILE *out);
/* F1 script commands: pipe X0 Y0 X1 Y1 [remove]   (L-path, like power)
                       pipes_with_roads on|off         (the option below) */
int   tools_script_run_file(const char *path, FILE *out);  /* headless; errors, -1 = can't open */
void  tools_script_reset(void);              /* origin 0,0 */
void  tools_script_set_dir(const char *dir); /* prefix for relative statlog/dump file names ("out/") */

/* ---- F1 water pipes (WATER.md 3.2) -----------------------------------------------------------------
   Pipes are underground: they run under grass, forest, rock, roads, rail and buildings (a building placed
   on a pipe keeps it); under water at g_misc.pipe_water per tile. Removal: Ctrl+drag with the pipe tool
   (param 1), or the bulldozer while tools_set_show_pipes(1) (game.cpp: the water overlay is on), so the
   bulldozer never removes pipes the player can't see. Option "lay pipes with new roads" (INI / New Game,
   default off): every new or upgraded gravel road, street or main road tile also gets a pipe (paid).
   F3 decision (WATER.md 3.2 suggested right-drag removal): NOT done. The right button keeps one meaning in every
   tool (DESIGN 4: right-click cancels a drag / drops the tool / queries), so pipe removal stays Ctrl+drag (the
   opening hint chain says so) or the bulldozer with the water overlay on. */
void  tools_set_pipes_with_roads(int on);
int   tools_pipes_with_roads(void);
void  tools_set_show_pipes(int on);
#endif
