/* trade.h - M7: clearing trade with the East (a 5-year agreement card + one balance meter), oil prices (1973 and 1979
   shocks, 1986 collapse), the Western demand multiplier (1957/1967 devaluations), the domestic bond. PORTABLE,
   deterministic, integer-only. Owner: M7 (Milestone 2). Spec: DESIGN 5.9, 6, 7; docs/SCOPE_REVIEW.md (the thin
   version: no hard currency, no import programmes, one bond).

   Model (numbers in trade.cpp; docs/HANDOFF.md M7):
   - AGREEMENTS run 1956-60, 61-65, ... 86-90 (a 1955 start joins the first one, signed Standard). At each period start
     the Delegation's agreement card (EV_AGREEMENT, politics' card queue) offers Standard / Modest / Ambitious; with
     politics off, or unanswered after TRADE_OFFER_MONTHS, the Ministry signs Standard. The level scales the clearing
     I-demand term (100 / 85 / 120%) and the GOODS LIST: per goods category the town's Eastern deliveries at signing x
     level (+ a small machinery floor). Every January the review compares the last 12 months' deliveries with the list:
     below 80% the Delegation complains (Eastern relations -3, clearing term x90% for a year).
   - The CLEARING BALANCE (one meter): + Eastern deliveries (industry_kinds()->east_total + complex exports, 1 credit per
     volume unit) - oil (the town's share of the national oil bill: residents x an era rate x the oil index, + oil for
     oil plants), oil at the CLEARING price (a 5-year moving average of the world price, half of the rise). Credit above the tolerance (3 months of exports + 200) is lost (a quarter a month: it buys nothing);
     below -tolerance for 3 months the clearing term drops to 90% and (politics on) Eastern relations fall 1 every 2
     months; the deficit stops at -3 x tolerance (the East extends no further credit). Clearing trade ends in January 1990 (the account closes; sim.cpp's clearing term drops too).
   - OIL index (100 = 1955): 400 from November 1973, 550 in 1979-80, 500 in 1981-85, 250 from March 1986. Clearing
     I-demand x150% from November 1973 to 1980 (oil for goods), 120% 1981-85, 85% 1986-89. Fuel (ledger line BL_FUEL):
     oil plants pay OIL_PLANT_FUEL mk x oil index / 100, coal plants a surcharge in 1974-85 (both x the upkeep index).
   - WESTERN demand multiplier: a price index (random walk 60..140 around an era baseline: 1957 and 1967 devaluations
     110, the 1975-77 recession 85, the 1980s 105-110) scales the Western I-demand term. Nothing else.
   - BOND: one button. Amount 10,000 + residents (max 60,000), 6 per mille a month, 120 months; payments on
     BL_INTEREST. One bond at a time. The emergency loan stays in budget.cpp.
   Sim hooks (sim.cpp): trade_clearing_pct (DT_CLEARING), trade_western_pct (DT_WESTERN). Month end: era_month() calls
   trade_month() after politics_month. */
#ifndef TRADE_H
#define TRADE_H
#include "common.h"
#include "defs.h"

enum { TL_MODEST, TL_STANDARD, TL_AMBITIOUS, TL_COUNT };                  /* agreement levels */
enum { TG_MACHINERY, TG_HOUSES, TG_TEXTILES, TG_DAIRY, TG_ELECTRONICS,   /* goods categories (industry kinds) */
       TG_PAPER, TG_SHIPS,                                                /* M6 complexes (trade_set_complex) */
       TG_COUNT };

#define TRADE_PERIOD_YEARS  5
#define TRADE_FIRST_PERIOD  1956
#define TRADE_END_YEAR      1990     /* clearing trade ends in January */
#define TRADE_OFFER_MONTHS  6
#define TRADE_OFFER_WAIT_POL 24      /* PL2: with politics on the offer waits this long for its card (the one-card-a-year gap and
                                        the election quiet months used to hand 3 of 6 agreements to the Ministry) */
#define TRADE_COMPLEX_SLOTS 4
#define TRADE_REVIEW_PCT    80
#define OIL_PLANT_UNITS     25       /* oil units / month of an oil plant (clearing credit at index 100) */
#define OIL_PLANT_FUEL      50       /* mk / month at index 100 */
#define COAL_SURCHARGE_MK   20       /* per coal plant per month x (coal index - 100) / 100 */

typedef struct {
    int   from_year, to_year;      /* inclusive */
    int   level;                   /* TL_* (-1: none) */
    s32   quota[TG_COUNT];         /* goods list: volume units / month */
    s32   quota_total;
    int   offer;                   /* 1 = the agreement card waits */
    int   offer_months;
} TradeAgreement;

typedef struct {
    s32 balance, tolerance;        /* the clearing meter */
    s32 exports_east;              /* last month: credit from deliveries */
    s32 oil_town, oil_plants;      /* last month: credit spent on oil */
    s32 delivered[TG_COUNT];       /* last month, volume units */
    s32 deliv_12, quota_12;        /* rolling 12-month sums (review) */
    int fulfil_pct;                /* last January review (-1 none yet) */
    s32 lost_month, lost_total;    /* credit lost above the tolerance */
    int deficit_months;            /* consecutive months below -tolerance */
    int shortfall_months;          /* review penalty months left */
    int oil_index, coal_index;     /* world prices, 100 = 1955 (fuel in mk) */
    int clearing_oil;              /* the clearing price of oil (5-year average, half the rise): the meter's oil */
    int west_index;                /* 60..140 */
    s32 exports_west;              /* last month: Western deliveries (volume) */
    s32 fuel_cost;                 /* last month: BL_FUEL (mk) */
    s32 bond, bond_orig, bond_months;   /* outstanding, amount, months left */
    int closed;                    /* 1 = clearing trade has ended (1990) */
} TradeState;

/* ------------------------------------------------------------------ lifecycle (era.cpp calls these) */
void trade_reset(void);                    /* sim_new_city (via era_reset) */
void trade_month(void);                    /* month end, after politics_month (via era_month) */
const TradeState *trade_state(void);
const TradeAgreement *trade_agreement(void);

/* ------------------------------------------------------------------ sim hooks (percent, 100 = neutral) */
int  trade_clearing_pct(void);             /* agreement level x oil boom x review / deficit penalties */
int  trade_western_pct(void);              /* Western price index */
int  trade_clearing_reason(char *buf, int size);   /* demand tooltip line: "Clearing: Standard agreement, oil boom +50%" */

/* ------------------------------------------------------------------ player actions (between sim steps) */
int  trade_sign(int level);                /* sign the waiting agreement; 1 ok (the card's choice calls it) */
int  trade_level_allowed(int level);       /* Ambitious needs Eastern relations 40+ (politics on) */
s32  trade_bond_amount(void);
int  trade_issue_bond(void);               /* 1 ok (none running) */

/* ------------------------------------------------------------------ M6: complex exports (paper mill, Valtion Metalli)
   Volume units per month; slot 0..TRADE_COMPLEX_SLOTS-1, goods TG_*; 0/0 = none. Read every month. */
void trade_set_complex(int slot, int goods, s32 east, s32 west);
int  trade_period_of(int year, int *from, int *to);

/* ------------------------------------------------------------------ text */
const char *trade_goods_name(int g);       /* "Machinery" */
int  trade_goods_line(int g, s32 volume, char *buf, int size);   /* "1,200 tractors and cranes" (goods list, a year) */
const char *trade_level_name(int level);   /* "Ambitious" */
int  trade_balance_tip(char lines[][80], int max);   /* the one tooltip of the clearing meter */

/* ------------------------------------------------------------------ debug, save */
void trade_dump(FILE *f);
u32  trade_hash(void);
struct SaveFile;
int  trade_save(struct SaveFile *sf);      /* chunk "TRAD" v1 */
int  trade_load(struct SaveFile *sf);      /* 0 = no chunk */

#endif
