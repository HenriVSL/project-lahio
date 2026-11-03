/* trade.h - M7: clearing trade with the East (5-year agreements, goods lists, the clearing balance, oil),
   Western trade (hard-currency orders, volatility, licences and purchases), oil prices (1973 and 1979 shocks,
   1986 collapse), loans (domestic bond, Western bank loan, Eastern project credit). PORTABLE, deterministic,
   integer-only. Owner: M7 (Milestone 2). Spec: DESIGN 5.9, 6, 7; DESIGN_LESSONS 4.8, 4.11.

   Model (numbers in trade.cpp; docs/HANDOFF.md M7):
   - AGREEMENTS run 1956-60, 61-65, ... 86-90 (a 1955 start joins the first one; a later start joins the running
     one). At each period start an offer waits up to TRADE_OFFER_MONTHS months: Modest / Standard / Ambitious
     (TL_*). Unanswered offers sign Standard. The level scales the clearing I-demand term (85 / 100 / 120%) and
     the promised QUOTA: per goods category the town's Eastern deliveries at signing x level (+ a small floor),
     rising 5% a year ("plan figures"). Fulfilment = deliveries of the last 12 months / quota, reviewed every
     January: below 80% the Delegation complains (Eastern relations -3, clearing term -10% for a year), 100%+ is
     praised (+1).
   - The CLEARING BALANCE (credit): + Eastern deliveries (industry_kinds()->east_total + complex exports, 1 credit
     per volume unit) - imports: the town's share of the national oil bill (residents x an era rate x the oil
     index), oil for oil plants / heat plants, buses, consumer goods (Ladas, televisions; more with the consumer
     programme) and building materials (with the materials programme). The balance should stay near zero:
     credit above the tolerance (3 months of exports + 200) spoils by a quarter each month ("wasted", a ticker
     joke); below -tolerance Eastern relations fall (1 every 2 months) and the clearing term drops 15%.
     Every 5 years the agreement settles the account: surplus written off, half of a deficit carried over.
   - OIL index (100 = 1955): 400 from November 1973 (first shock), 550 in 1979-80, 500 in 1981-85, 250 from
     1986. Clearing I-demand x150% from November 1973 to 1980 (the oil-for-goods boom), 120% 1981-85, 85%
     1986-89 (the trade shrinks with the oil price). Oil plants and heat plants pay fuel in mk (BL_UTILITIES) at
     the oil index; coal plants pay a coal surcharge in 1974-85. Clearing trade ends in January 1990: the account
     is closed, imports are paid in hard currency (mk) from then on.
   - WESTERN trade: deliveries = industry_kinds()->west_total + complex exports; a Western price index (random walk,
     60..140, mean-reverting to an era baseline with the 1957/1967 devaluations and the mid-1970s recession) scales
     the Western I-demand term and the hard-currency premium (BL_EXPORT, grows with the era). Hard currency
     (reserve) accrues from deliveries; the electronics licence (IK_ELEC) is granted once the town has earned
     TRADE_ELEC_HC, Western machine tools (business tax +5%) and computers at the town hall (service upkeep -8%)
     are bought with hard currency + mk.
   - LOANS (one of each kind at a time; payment = principal / term + interest on BL_INTEREST, proceeds on
     BL_INTEREST too): domestic bond (no strings), Western bank loan (bigger, cheaper, Eastern relations -5), Eastern
     credit (project-bound: one project kind (power plant, heat plant, element factory) must be built within 12
     months or the credit is called in; very cheap; locks the agreement at Standard or above and adds 10% quota).
     The emergency loan stays in budget.cpp.
   With politics off the Eastern relations effects are skipped; the trade-internal penalties (deficit, shortfall)
   still apply to the clearing term.

   Sim hooks (sim.cpp): trade_clearing_pct (DT_CLEARING), trade_western_pct (DT_WESTERN), trade_emigration_pct.
   Month end: era_month() calls trade_month() after politics_month. */
#ifndef TRADE_H
#define TRADE_H
#include "common.h"
#include "defs.h"

enum { TL_MODEST, TL_STANDARD, TL_AMBITIOUS, TL_COUNT };                  /* agreement levels */
enum { TG_MACHINERY, TG_HOUSES, TG_TEXTILES, TG_DAIRY, TG_ELECTRONICS,   /* goods categories (industry kinds) */
       TG_PAPER, TG_SHIPS,                                                /* M6 complexes (trade_set_complex) */
       TG_COUNT };
enum { TI_OIL_TOWN, TI_OIL_PLANTS, TI_BUSES, TI_CONSUMER, TI_MATERIALS, TI_COUNT };   /* import lines */
enum { LOAN_BOND, LOAN_WEST, LOAN_EAST, LOAN_KINDS };
enum { TPROJ_POWER, TPROJ_HEAT, TPROJ_ELEMENTS, TPROJ_COUNT };            /* Eastern credit projects */
enum { TECH_ELECTRONICS, TECH_MACHINE_TOOLS, TECH_COMPUTERS, TECH_COUNT };

#define TRADE_PERIOD_YEARS  5
#define TRADE_FIRST_PERIOD  1956
#define TRADE_END_YEAR      1990     /* clearing trade ends in January (sim.cpp's clearing term drops too) */
#define TRADE_OFFER_MONTHS  6
#define TRADE_ELEC_HC       3000     /* hard currency earned (total) before the electronics licence */
#define TRADE_COMPLEX_SLOTS 4

typedef struct {
    int   from_year, to_year;      /* inclusive */
    int   level;                   /* TL_* (-1: no agreement yet / offer pending) */
    s32   quota[TG_COUNT];         /* volume units / month at signing (before the yearly rise) */
    s32   quota_total;
    int   offer;                   /* 1 = an offer waits for the President's signature */
    int   offer_months;            /* months the offer has waited */
    int   supplement;              /* 1 = the 1974 oil-for-goods supplement (+30%) */
} TradeAgreement;

typedef struct {
    s32 principal;                 /* outstanding */
    s32 orig;                      /* amount taken */
    s32 rate;                      /* per mille per month */
    s32 months_left;
    s32 project;                   /* Eastern credit: TPROJ_*, -1 none */
    s32 project_base;              /* buildings of the project kind at signing */
    s32 project_months;            /* months left to build it (0 = built) */
} TradeLoan;

typedef struct {
    /* clearing */
    s32 balance, tolerance;
    s32 exports_east;              /* last month: credit from deliveries */
    s32 imports[TI_COUNT];         /* last month: credit spent per line */
    s32 imports_total;
    s32 delivered[TG_COUNT];       /* last month, volume units */
    s32 deliv_12, quota_12;        /* rolling 12-month sums (fulfilment) */
    int fulfil_pct;                /* last January review (-1 none yet) */
    s32 wasted_month, wasted_year, wasted_total;
    int deficit_months;            /* consecutive months below -tolerance */
    int shortfall_months;          /* clearing penalty months left (shortfall review) */
    int consumer, materials;       /* import programmes on/off */
    /* oil, west */
    int oil_index, coal_index;     /* 100 = 1955 */
    int west_index;                /* 60..140 */
    s32 exports_west;              /* last month: Western deliveries (volume) */
    s32 west_income;               /* last month: hard-currency premium (mk) */
    s32 fuel_cost;                 /* last month: fuel bill (mk) */
    s32 hc, hc_earned;             /* hard currency reserve / earned in total */
    u32 techs;                     /* 1 << TECH_* */
    s32 tech_bonus;                /* last month: machine tools + computers (mk) */
    TradeLoan loan[LOAN_KINDS];
    s32 loan_paid_total, loan_interest_total;
    int closed;                    /* 1 = clearing trade has ended (1990) */
} TradeState;

/* ------------------------------------------------------------------ lifecycle (era.cpp calls these) */
void trade_reset(void);                    /* sim_new_city (via era_reset) */
void trade_month(void);                    /* month end, after politics_month (via era_month) */
const TradeState *trade_state(void);
const TradeAgreement *trade_agreement(void);

/* ------------------------------------------------------------------ sim hooks (percent, 100 = neutral) */
int  trade_clearing_pct(void);             /* agreement level x oil boom x penalties x Eastern credit */
int  trade_western_pct(void);              /* Western price index */
int  trade_emigration_pct(void);           /* consumer goods programme: 80 */
int  trade_tech(int tech);                 /* 1 = owned (industry.cpp: TECH_ELECTRONICS) */
int  trade_fuel_units(int def);            /* oil units per month burnt by a building def (0 = none) */

/* ------------------------------------------------------------------ player actions (between sim steps) */
int  trade_sign(int level);                /* sign the waiting offer; 1 ok */
int  trade_level_allowed(int level);       /* 0 = not now (Eastern credit lock, relations too low for Ambitious) */
int  trade_set_programme(int which, int on);   /* which 0 consumer, 1 materials */
int  trade_loan_offer(int kind, s32 *amount, s32 *rate, s32 *months);   /* 1 = available now */
int  trade_loan_why(int kind, char *buf, int size);   /* why not (empty when available) */
int  trade_take_loan(int kind, int project);  /* project TPROJ_* for LOAN_EAST; 1 ok */
int  trade_repay_loan(int kind);           /* repay the rest now (needs the money); 1 ok */
int  trade_buy_tech(int tech);             /* 1 ok */
int  trade_tech_price(int tech, s32 *hc, s32 *mk, int *year);   /* 0 = not a purchase (granted) */
int  trade_tech_state(int tech);           /* 0 owned, 1 available, 2 too early, 3 no hard currency, 4 no money */

/* ------------------------------------------------------------------ M6: complex exports (paper mill, Valtion Metalli)
   Volume units per month at full output; slot 0..TRADE_COMPLEX_SLOTS-1, goods TG_*; 0/0 = none. Read every month. */
void trade_set_complex(int slot, int goods, s32 east, s32 west);
s32  trade_quota_now(int goods);           /* quota this year (after the yearly rise) */
int  trade_period_of(int year, int *from, int *to);

/* ------------------------------------------------------------------ text */
const char *trade_goods_name(int g);       /* "Machinery" */
int  trade_goods_line(int g, s32 volume, char *buf, int size);   /* "1,200 tractors and cranes" (goods list) */
const char *trade_level_name(int level);   /* "Ambitious" */
const char *trade_import_name(int i);
const char *trade_loan_name(int kind);
const char *trade_project_name(int p);
const char *trade_tech_name(int t);

/* ------------------------------------------------------------------ debug, save */
void trade_dump(FILE *f);
u32  trade_hash(void);
struct SaveFile;
int  trade_save(struct SaveFile *sf);      /* chunk "TRAD" v1 */
int  trade_load(struct SaveFile *sf);      /* 0 = no chunk: fresh state for the save's date */

#endif
