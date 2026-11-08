/* season.h - 1.1 W1: the winter simulation (docs/DESIGN_1_1.md section 3). PORTABLE, deterministic, integer-only.
   Owner: W1. The level's numbers come from D1's table (defs.h DiffParams: winter_load_pct, hard_winter_pct,
   hard_load_pct, hard_water_pct, plough_pct, plough_hard_pct, frost_repair10, fire_winter_pct).

   CALENDAR: winter = December-March, a pure function of the sim date (the art never feeds the sim). A "winter year" is
   the year of its December (winter 1986 = Dec 1986 - Mar 1987). 1 November: the forecast (ordinary or hard), drawn from
   the sim RNG; the winter 1986-87 is always hard on Normal/Hard; disasters off = no hard winters (normal peaks stay).
   Stacking (Normal only, DESIGN_1_1 3.1): no hard winter in the Sweden winters (1968, 1969, 1970), the first oil-shock
   winter (1973), right after a hard one, or just before the fixed 1986 one. Hard does not skip.

   WHAT IT CHANGES (Easy: nothing at all - every hook returns its neutral value, the sim path is a no-op; only the
   flavour ticker lines and the art season remain):
     power + district heat use x season_power_pct() / season_heat_pct() Dec-Mar (100 + winter load (+ hard load));
       net.cpp / civic.cpp allocate against supply * 100 / pct (exactly "use x pct <= supply") and report the scaled use;
     pumps and wells give season_water_pct() of their output in a hard winter (towers unchanged);
     ledger line BL_WINTER "Winter: ploughing and frost repairs" = road upkeep x plough % + pipe tiles / 10 x frost mk
       (hard winter) x the upkeep index, booked with the other upkeep in December-March (budget.cpp);
     Apartments T3 upgrades check heat spare against the winter peak all year (civic_heat_spare_peak: no flip-flop);
     fire ignition x season_fire_pct() in Dec-Mar (fire.cpp; D1's fire_winter_pct).
   REPORTS: ticker lines (forecast, first snow, candles/brownout, frozen pipes, cold radiators, the thaw with the winter's
   cost), the January headline in a hard winter, "In hindsight" after a hard winter that hurt, Cabinet advice in
   October-November when the winter peak exceeds supply (advice.cpp ADV_WINTER_*), tooltip lines (season_tip_line).

   ART HOOK (W2): season_is_winter() = the season to show (1 December - 31 March). season_changed() returns 1 once each
   time season_is_winter() differs from what the previous call saw (first call: compared with summer = the art loaded at
   start), so the renderer polls it every frame and swaps pages at the month tick; after loading a save it fires too.
   One poller only (the renderer). */
#ifndef SEASON_H
#define SEASON_H
#include "common.h"

/* ------------------------------------------------------------------ calendar (pure) */
int  season_month_is_winter(int month);   /* month 0..11: 1 for Dec, Jan, Feb, Mar */
int  season_winter_year(int year, int month);   /* the winter's December year (Jan-Mar: year - 1) */
int  season_is_winter(void);              /* by the current sim date (all levels: the art season) */
int  season_changed(void);                /* W2 art hook (see above) */
int  season_mechanics(void);              /* 1 = winter has simulated effects on this level (not Easy) */

/* ------------------------------------------------------------------ lifecycle (sim.cpp) */
void season_reset(void);                  /* sim_new_city (after diff_set_level) */
void season_month(void);                  /* month_end_books, after the date advance: forecast, snow, thaw, reports */

/* ------------------------------------------------------------------ the forecast and the current winter */
enum { SFC_NONE, SFC_ORDINARY, SFC_HARD };
int  season_forecast(void);               /* the announced forecast for the coming/current winter (Nov-Mar), SFC_* */
int  season_hard_now(void);               /* 1 = a hard winter is on now (Dec-Mar) */
int  season_hard_winters(void);           /* hard winters so far this game (tests, newsreel) */
int  season_hard_rule(int wyear, int *fixed);   /* 1 = a hard winter may be rolled for that winter year on this level
                                                 (stacking rules); *fixed = 1 for the fixed 1986-87 (tests) */

/* ------------------------------------------------------------------ multipliers (sim hooks; all 100 on Easy) */
int  season_winter_load(void);            /* 1.1 B1: this winter's load % (level base + electric heating; 0 on Easy) */
int  season_electric_load(void);          /* 1.1 B1: its electric-heating part (0 before ELECTRIC_HEAT_FROM and on Easy) */
int  season_power_pct(void);              /* power use x % now */
int  season_heat_pct(void);               /* district heat use x % now */
int  season_water_pct(void);              /* pumps' and wells' output % now (hard winter) */
int  season_peak_pct(void);               /* power/heat use at this winter's expected peak (Nov-Mar: with the forecast) */
int  season_fire_pct(void);               /* fire ignition x % now (Dec-Mar: diff()->fire_winter_pct) */
s32  season_eff_supply(s32 supply, int pct);  /* supply * 100 / pct (the allocation budget for raw uses) */
s32  season_scale(s32 raw_use, int pct);  /* raw use -> use at pct (display, stats) */
/* budget.cpp compute_lines: the "Winter" line for the month being booked (mk >= 0; road_mk = this month's road and bridge
   upkeep as booked, pipe_tiles = water pipe tiles, ui = upkeep index %) */
Money season_winter_upkeep(Money road_mk, s32 pipe_tiles, int ui);

/* ------------------------------------------------------------------ the peak (tooltips, advice; display numbers) */
typedef struct {
    s32 power_now, power_peak, power_supply;  /* units: use now (scaled), use at the winter peak, plants' output */
    s32 heat_now, heat_peak, heat_supply;
    s32 water_supply, water_frost;            /* water supply now / in a hard frost (pumps and wells x hard_water_pct) */
    int peak_pct, hard;                       /* season_peak_pct(); 1 = the peak is a hard winter's */
} SeasonPeak;
void season_peak(SeasonPeak *out);
enum { STIP_POWER, STIP_WATER, STIP_HEAT };
int  season_tip_line(int what, char *buf, int size, int *warn);   /* top-bar tooltip line (1 = show; *warn = over supply) */
int  season_lot_line(int lot, char *buf, int size);   /* query line for plants, pumps, wells, heat plants (1 = show) */
int  season_advice(int k, s32 *peak, s32 *supply, char *text, int size);   /* advice.cpp: k = 0 power, 1 heat;
                                                 1 = Oct-Nov and that winter peak exceeds supply */
/* newspaper hooks (era.cpp): January headline in a hard winter, "In hindsight" after a hard winter */
int  season_news_score(void);             /* 0 or the NT_WINTER score for the January paper */
void season_news_story(char *h, int hs, char *d, int ds, char *b, int bs);
int  season_hindsight(char *head, int hs, char *body, int bs);   /* the January paper (built in December): the
                                                 hard winter that ended this March hurt (dark/dry/cold lots) */
int  season_suppress_event(int se_type);  /* text.cpp: 1 = a winter report replaces this SE_* shortage line this month */

/* ------------------------------------------------------------------ ticker messages (game.cpp drains) */
enum { SM_FORECAST, SM_FIRST_SNOW, SM_THAW, SM_CANDLES, SM_FROZEN, SM_COLD,
       SM_PEAK_POWER, SM_PEAK_HEAT,     /* 1 Oct / 1 Nov: the winter peak exceeds supply (a = peak, b = supply) */
       SM_COUNT };
typedef struct { u8 type; s16 x, y; s32 a, b; } SeasonMsg;
int  season_poll_msg(SeasonMsg *m);
int  season_msg_text(char *buf, int size, const SeasonMsg *m, int *severity);   /* SEV_*; 0 = don't show */

/* ------------------------------------------------------------------ stats, debug, save */
typedef struct {
    s32 winter_cost, last_cost;           /* mk on BL_WINTER this winter / the last finished winter */
    s32 frozen_lots, frozen_max;          /* dry lots in the last hard-winter month / most in a month this game */
    s32 dark_lots, cold_lots;             /* power-short / heat-short lots in the last winter month */
    int hard_winters, last_hard_year;     /* count; winter year of the last hard one (0 none) */
    int jan_power_pct;                    /* last January's power load % (forecast line) */
} SeasonStats;
const SeasonStats *season_stats(void);
void season_statlog(FILE *f, int header); /* sim stat CSV: extra columns (header = 1: names) */
void season_dump(FILE *f);
int  season_script_line(int n, char **t, FILE *out);   /* "season dump" / "season hard on|off" (tests); -1 = not ours */
u32  season_hash(void);                   /* 0 on Easy (the 1.0 hashes) */
struct SaveFile;
int  season_save(struct SaveFile *sf);    /* chunk "SEAS" v1 */
int  season_load(struct SaveFile *sf);    /* missing: fresh state (no forecast) */
#ifdef KAUP_TESTS
void season_test_force(int forecast_year, int hard);   /* tests: set the forecast for a winter year */
#endif

#endif
