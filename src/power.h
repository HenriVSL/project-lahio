/* power.h - 1.2 S1 "Power" (docs/DESIGN_1_2.md 2-4): the reactor question (one decision letter per game, saved), plant
   prices / build months by that choice, the Eastern reactor's clearing import, the plants' query and Guide lines, the
   Cabinet's plant recommendation and the newspaper's reactor filler. PORTABLE, deterministic, integer-only. Owner: S1.

   The plants themselves are ordinary BCAT_POWER rows in defs.cpp (coal plant, hydro, oil, peat, nuclear, and the coal
   power station appended as B_COAL_STATION). Fuel stays in trade.cpp (the station pays four small plants' coal
   surcharge; peat pays no import price); the peat plants' Rural approval is a reason in politics.cpp.

   THE REACTOR LETTER (DESIGN_1_2 3): the first time the President opens the nuclear plant tool once it is unlocked, the
   letter waits on the desk (ui_pol.cpp V_REACTOR, also with politics off); the tool cannot place a reactor until it is
   answered. The choice holds for every nuclear plant of the game:
     RX_EAST     price x70 %, 18 months to build, while it is built 1,500 clearing credits a month go East (the meter in
                 trade.cpp); Workers + / Industrialists -, Eastern relations +.
     RX_WEST     full price in markka, 12 months; Industrialists and Intelligentsia +, Eastern relations - and authority -2
                 ("Moscow expresses interest").
     RX_LOVIISA  price x85 %, 24 months, half the Eastern import; both blocs a little +, relations a little +.
   After clearing trade ends (1990) the Eastern parts are paid in markka: East x90 %, Loviisa x95 %, no import.
   Politics effects go through politics_apply_fx_list (nothing when politics is off). A nuclear plant is placed under
   construction (LOT_BUILDING, build_left = the months) and produces nothing until it is finished; power plants under
   construction pay no upkeep (budget.cpp).

   Regression: with no choice made and no reactor online every function here is neutral (power_hash() == 0, no POWR
   chunk), so a 1.1 game that never builds the new plants plays and saves exactly as before. */
#ifndef POWER_H
#define POWER_H
#include "common.h"
#include "defs.h"
#include "politics.h"

enum { RX_NONE = -1, RX_EAST = 0, RX_WEST = 1, RX_LOVIISA = 2, RX_COUNT = 3 };

#define POWER_STATION_COAL_X   4      /* the station's coal surcharge in small-plant units (2,000 / 500) */
#define POWER_PEAT_RURAL       2      /* Rural approval per peat plant ... */
#define POWER_PEAT_RURAL_MAX   4      /* ... at most (small and steady) */
#define POWER_REACTOR_IMPORT   1500   /* clearing credits a month per Eastern reactor under construction (Loviisa: half) */

/* ------------------------------------------------------------------ lifecycle */
void power_reset(void);                    /* sim_new_city */
void power_month(void);                    /* era_month, after trade_month: the first reactor online (ticker) */

/* ------------------------------------------------------------------ the reactor letter */
typedef struct {
    const char *label;        /* the button: "Eastern reactor (price -30%, paid in clearing)" */
    const char *result;       /* what happens (shown after the choice, and in the query panel) */
    const char *short_name;   /* "Eastern reactor" (query panel, Guide) */
    PolFx fx[POL_FX_MAX];     /* blocs / meters / authority (politics_apply_fx_list) */
} ReactorChoice;
typedef struct {
    const char *title, *text;
    ReactorChoice ch[RX_COUNT];
} ReactorCard;
const ReactorCard *power_reactor_card(void);
int  power_reactor_choice(void);           /* RX_* (RX_NONE = not decided) */
int  power_reactor_needed(void);           /* 1 = the nuclear plant is unlocked and the letter is unanswered */
int  power_reactor_choose(int choice);     /* answer the letter (once); 1 ok. Player action between sim steps */
int  power_reactor_cost_pct(int choice);   /* price % for a choice now (clearing open or closed) */
int  power_reactor_months(int choice);     /* build months for a choice */
int  power_reactor_terms(int choice, char *buf, int size);   /* "Price 21,000 mk, 18 months, 1,500 clearing credits a month" */

/* ------------------------------------------------------------------ prices and construction (tools.cpp) */
Money power_build_cost(int def);           /* the def's price (nuclear: by the choice; others: g_bdefs cost) */
int  power_build_months(int def);          /* months under construction when the player places it (0 = at once) */
s32  power_clearing_import(void);          /* this month's clearing import (trade_month subtracts it) */
int  power_count(int def, int active_only);/* lots of a def (active only: LOT_ACTIVE) */
int  power_peat_rural(void);               /* Rural approval points from peat plants (politics.cpp) */
s32  power_station_surcharge(void);        /* coal surcharge of one coal power station this month, mk (query panel) */

/* ------------------------------------------------------------------ text */
/* query panel lines for a power plant lot (k = 0, 1, ...; 0 = no more); *font 0 dark, 1 grey, 2 red, 3 green */
int  power_lot_line(int lot, int k, char *buf, int size, int *font);
/* the Guide's power page, one row per plant in the Power menu order */
typedef struct {
    int  def;
    char name[40], size[8], units[16], cost[24], upkeep[16], unlock[48], note[72];
    int  unlocked, built;
} PowerGuideRow;
int  power_guide_rows(PowerGuideRow *out, int max);
int  power_guide_note(char *buf, int size);   /* one line under the table (the reactor choice, or what it will ask) */
/* the Cabinet's plant: the bigger plant to build now (unlocked, has art, affordable), B_NONE if the small coal plant is
   still the answer. Nuclear at Regional centre, peat after the 1973 oil shock, else the coal power station. */
int  power_advice_pick(void);
/* the January paper's filler the year after the first reactor went online; 0 = none */
int  power_news_filler(int year, char *head, int hs, char *body, int bs);
int  power_online_year(void);              /* the first reactor finished (0 = none yet) */

/* ------------------------------------------------------------------ scripts, debug, save */
int  power_script_line(int n, char **t, FILE *out);   /* "reactor east|west|loviisa|0|1|2" | "reactor dump"; -1 = not ours */
void power_dump(FILE *f);
u32  power_hash(void);                     /* 0 while nothing has been decided (old hashes unchanged) */
struct SaveFile;
void power_save(struct SaveFile *sf);      /* chunk "POWR" v1, written only once something was decided */
void power_load(struct SaveFile *sf);      /* missing chunk (1.0/1.1 saves): not decided */

#endif
