/* advice.h - PL1 guidance: the Cabinet's advice (the newspaper as the advisor, PLAYTEST 2 items 7b/8), hindsight
   and the 1990 verdict (item 10), progression and unlock sentences (item 1), building purposes (items 2/4), the
   power network summary (items 3/5). PORTABLE, deterministic, integer-only. Owner: PL1.

   Everything here reads the city (sim_stats, sim_demand, budget, lots, era/politics state) and never changes the
   simulation. The only state is the hindsight bookkeeping (monthly tallies of missed opportunities, the special-
   edition year, the "within reach" / "near capacity" ticker latches): era.cpp calls advice_month() at month end
   (before its New Year block) and advice_year_end() after the January paper; it is saved inside era_save as chunk
   "ADVC" (missing: fresh state). Tone: THEME 6.1 (deferential, deadpan, about the system). */
#ifndef ADVICE_H
#define ADVICE_H
#include "common.h"
#include "defs.h"

/* ------------------------------------------------------------------ the Cabinet's advice */
enum { ADV_NONE, ADV_POWER_SHORT, ADV_WATER_SHORT, ADV_MONEY_DEBT, ADV_RIGHTS_WAITING, ADV_MONEY_FALLING,
       ADV_POWER_NEAR, ADV_APT_ELEMENTS, ADV_APT_TOWER, ADV_HEAT, ADV_SHOPS, ADV_SCHOOL, ADV_HEALTH, ADV_SAUNA,
       ADV_JOBS, ADV_HOMES, ADV_RIGHTS_NEAR,
       ADV_WINTER_POWER, ADV_WINTER_HEAT,   /* 1.1 W1: Oct-Nov, the winter peak exceeds supply (season.h) */
       ADV_SWEDEN, ADV_EXPECT_LIBRARY, ADV_EXPECT_CLINIC,   /* 1.1 D1: 1969-70 missing reasons; era expectations (hard.h) */
       ADV_COUNT };
/* what a click on the line does (the UI maps these): camera = centre on (x, y) when x >= 0 */
enum { AGO_NONE, AGO_CAMERA, AGO_POWER /* power overlay + camera */, AGO_WATER /* water overlay + camera */,
       AGO_HEAT /* heat overlay + camera */, AGO_SERVICES, AGO_PEOPLE, AGO_DEMAND, AGO_BUDGET, AGO_DESK, AGO_COUNT };
#define ADV_TEXT 200
typedef struct {
    u8   kind, go;            /* ADV_*, AGO_* */
    s16  x, y;                /* a place to look at (-1 = none) */
    s32  score;               /* importance (sorted descending) */
    s32  a, b;                /* the numbers behind the sentence (tests) */
    char text[ADV_TEXT];      /* the Cabinet's sentence */
} Advice;
int  advice_collect(Advice *out, int max);    /* ranked current problems, most important first; returns count */
int  advice_find(int kind, Advice *out);      /* 1 = that kind is a problem now (tests) */
const char *advice_kind_name(int kind);       /* "power_short" (tests, logs) */
int  advice_special_due(Advice *main);        /* era_month: 1 = an urgent problem deserves a special edition now
                                                 (rate limited: at most one per game year; marks the year) */

/* ------------------------------------------------------------------ hindsight and the verdict */
void advice_reset(void);                      /* era_reset */
void advice_month(void);                      /* era_month, before the New Year block */
void advice_year_end(void);                   /* era_month, after the January paper: the year's tallies -> lifetime */
int  advice_hindsight(char *head, int hsize, char *body, int bsize);   /* last year's biggest missed opportunity
                                                 (0 = none worth printing this year) */
int  advice_verdict(char lines[][120], int max);   /* "What the history books will say" (1990 newsreel): 2-3 lines */
int  advice_rights_waiting(void);             /* politics on and a town status reached but not signed */
int  advice_rights_wait_months(void);
/* ticker latches (era.cpp turns them into messages): 1 = say it now */
int  advice_power_near_due(int *pct);         /* power demand >= 90% of supply, once until it falls under 80% */
int  advice_rights_near_due(int *ts);         /* population >= 90% of the next status, once per status */

/* ------------------------------------------------------------------ progression and unlocks */
/* "Next: Market town at 3,000 (you have 2,750)"; "Town rights await your signature, President"; returns the
   progress 0..100 (100 = reached / top) and the next status in *ts (-1 at the top) */
int  advice_progress(char *buf, int size, int *ts);
/* "Unlocks at Market town (3,000 residents) and 1962" / "Unlocks at Market town (3,000 residents)" / "Unlocks in
   1962"; with the town-rights decree waiting: "... (sign the town rights decree)". 0 = already unlocked */
int  advice_unlock_text(char *buf, int size, int status_req, int era_year);
/* what a status brings: "Water tower, element factory (1962), district heat plant and sewage works" (buildings and
   roads whose status_req == ts; the year when it is still ahead); returns the number of items */
int  advice_status_unlocks(int ts, char *buf, int size);
const char *advice_purpose(int def);          /* one line on what a building is for, or NULL */

/* ------------------------------------------------------------------ power network summary (top bar, plant query) */
typedef struct {
    s32 supply, demand, used, spare;          /* units: plants' output, all consumers' use, use of the supplied ones */
    int plants, lots, lots_short;             /* power plants, consumers (use > 0), consumers in the dark */
    int pct;                                  /* demand * 100 / supply (0 without supply) */
} PowerNetInfo;
int  advice_power_network(int lot, PowerNetInfo *out);   /* the network of a plant (or any connected lot); 0 = none */
void advice_power_total(PowerNetInfo *out);   /* all networks (net_stats) */

struct SaveFile;
void advice_save(struct SaveFile *sf);        /* chunk "ADVC" v1 (era_save) */
void advice_load(struct SaveFile *sf);        /* era_load; missing chunk: advice_reset() */
void advice_dump(FILE *f);                    /* debug from text: the ranked advice + tallies */

#endif
