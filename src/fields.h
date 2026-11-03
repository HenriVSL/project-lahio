/* fields.h - field maps on the 2x2-tile grid: pollution, land value, crime, fire risk, noise, happiness,
   traffic. PORTABLE. Owner: T04. DESIGN 5.2, 5.7, DESIGN_LESSONS 4.6.

   One byte per cell (0..255; happiness 0..100), FIELD_MAX_W stride, cell = tile >> FIELD_SHIFT.
   Scales (T04): pollution 0..255 (~32 = noticeable, 128+ = heavy); land value ~15 bare land, 40-60
   serviced town, 80+ lakeside with parks; fire risk 0..255; crime 0 (M5 scope review: no crime field; police is a plain
   service, PM_CRIME_PCT is a land value term); noise 0 (traffic later). Land value is also cached
   per tile in Tile.lv (fields.cpp writes it). After a step that changes values visibly, fields.cpp calls
   world_notify(..., WCHG_FIELDS) with the changed area (render re-bakes only if that overlay is shown). */
#ifndef FIELDS_H
#define FIELDS_H
#include "common.h"
#include "defs.h"

enum { FLD_POLLUTION, FLD_LAND_VALUE, FLD_CRIME, FLD_FIRE_RISK, FLD_NOISE, FLD_HAPPINESS, FLD_TRAFFIC,
       FLD_COUNT };

void fields_init(void);
void fields_reset(void);                   /* new city: base land value from terrain, zero the rest */
void fields_step_pollution(void);          /* phase 10: emit, decay, 3x3 blur, wind drift to NE */
void fields_step_land_value(int quarter);  /* phase 11: quarter 0..3 of the map */
void fields_step_social(int quarter);      /* phase 12: crime, fire risk, happiness for that quarter */

int  field_at(int f, int x, int y);        /* by TILE coordinates; 0 outside */
int  field_cell(int f, int cx, int cy);    /* by cell coordinates */
const u8 *field_data(int f);               /* raw cells, stride FIELD_MAX_W (overlays, minimap) */
int  field_avg(int f);                     /* mean over the map (stats), updated per step */

/* Land value explained (query tooltip): ids LVT_*. Returns nterms. */
enum { LVT_BASE, LVT_LAKESIDE, LVT_FOREST_EDGE, LVT_ROCK, LVT_SERVICES, LVT_PARKS, LVT_ACCESS,
       LVT_POLLUTION, LVT_CRIME, LVT_NOISE, LVT_ABANDONED,
       LVT_LAKE,           /* M5: polluted lake water nearby (civic_lv_lake_term) */
       LVT_COUNT };
int  fields_explain_land_value(int x, int y, Term *terms, int max);

/* ---- T04 additions */
u8  *field_data_rw(int f);                 /* writable cells (traffic.cpp writes FLD_TRAFFIC) */
void fields_mark_dirty(int wchg_kinds);    /* sim's world listener: WCHG_TERRAIN -> base land value,
                                              WCHG_LOT -> lot influences (parks, plants, abandoned,
                                              wooden density) are recomputed at the next step */
int  field_happiness_at(int x, int y);     /* 0..100 (FLD_HAPPINESS stores 0..100) */
/* T13: the same with the changed tile rect: WCHG_TERRAIN/ROAD only recompute the field cells near the
   rect (terrain counts look 2 tiles around a cell), not the whole map (a full recalc was ~20 ms on the P2,
   triggered by every tree that growth cleared). Results are identical to a full recalc. */
void fields_mark_dirty_rect(int wchg_kinds, int x0, int y0, int x1, int y1);

struct SaveFile;
int  fields_save(struct SaveFile *sf);     /* chunk "FLDS" */
int  fields_load(struct SaveFile *sf);

#endif
