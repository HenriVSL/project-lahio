/* budget.h - money, monthly ledger, taxes, service funding, loans (v1.0). PORTABLE. Owner: T04.
   DESIGN 5.9.

   Money is whole markka (common.h). Formulas (T04, tuned by T12: constants and details in budget.cpp and
   docs/BALANCE.md): resident tax per month = sum over homes of residents * (30 + land value) * tier weight
   * tax_res / 17000 (weights houses 1/4/6, apartments 4/6/9); business tax = workers * (50 + LV) * tax_bus
   / 3000; export share = industrial workers / 12; state grant = max(0, 400 - population / 8) (village
   support); upkeep = def upkeep (services x funding %), roads/lines per 10 tiles (bridges x3), all x the
   upkeep index (100% to 1959, +2.5%/year, max 150%); interest + repayment of the emergency loan on
   BL_INTEREST. Negative balance for LOAN_AFTER_MONTHS months -> emergency loan (cover the deficit + 10,000
   mk) at LOAN_RATE, none above 40,000 mk of debt. Construction spending is booked immediately (budget_spend);
   taxes, upkeep and grants are booked by budget_month_end(), which the sim calls at the end of each
   month. Display of old markka before 1963 (x100) is text.cpp's job; internal values never change. */
#ifndef BUDGET_H
#define BUDGET_H
#include "common.h"
#include "defs.h"

/* Ledger lines. Incomes are positive amounts, expenses negative, in cur/last/year. */
enum { BL_TAX_RES, BL_TAX_BUS, BL_EXPORT, BL_GRANTS, BL_TIMBER, BL_INCOME_END,
       BL_SVC_FIRST = BL_INCOME_END,                 /* + SVC_*: service upkeep */
       BL_ROADS = BL_SVC_FIRST + SVC_COUNT,          /* road + bridge upkeep */
       BL_NETWORKS,                                  /* power lines, water mains */
       BL_UTILITIES,                                 /* plants, pumps, towers upkeep */
       BL_SPECIAL,                                   /* town hall, parks, landmarks */
       BL_INTEREST,
       BL_CONSTRUCTION,                              /* player building/bulldozing (+ refunds) */
       BL_POLITICS,                                  /* M1: decrees, letters, event cards (booked by politics) */
       BL_COUNT };
#define BL_SVC(svc) (BL_SVC_FIRST + (svc))

typedef struct {
    Money money;                   /* treasury (may go negative: DESIGN 5.9 emergency loan after 6 months) */
    int   tax_res, tax_bus;        /* percent, 0..20 (default 9) */
    u8    funding[SVC_COUNT];      /* percent, 50..120 (default 100) */
    Money cur[BL_COUNT];           /* this month so far */
    Money last[BL_COUNT];          /* last complete month */
    Money year[BL_COUNT];          /* this year so far */
    Money last_year[BL_COUNT];
    int   unlimited;               /* creative mode: spending never fails, money shown as "-" */
    int   months_negative;
    /* T04 additions */
    Money loan;                    /* emergency loan principal outstanding (DESIGN 5.9) */
    int   loan_rate;               /* per mille per month */
    int   loans_taken;
} Budget;
extern Budget g_budget;

void  budget_init(void);
void  budget_new(Money start, int unlimited);  /* sim_new_city */
int   budget_can_afford(Money cost);
void  budget_spend(Money cost, int line);      /* books -cost on `line` now; negative cost = refund */
void  budget_month_end(void);                  /* compute + book incomes and upkeep from the world */
int   budget_projection(Term *terms, int max); /* expected next month per line (ids BL_*); tooltips */
Money budget_net_last(void);                   /* last month's total */
void  budget_set_tax(int res, int bus);
void  budget_set_funding(int svc, int pct);
/* T04 additions */
#define TAX_NEUTRAL       9          /* tax rate with no demand/attractiveness penalty */
#define LOAN_AFTER_MONTHS 6          /* months of negative balance before the emergency loan */
#define LOAN_RATE         8          /* per mille per month (~10%/year; T12: 20 made an exponential debt spiral) */
Money budget_start_funds(int funds, int start_year);   /* FUNDS_* (sim.h) -> starting money */

struct SaveFile;
int   budget_save(struct SaveFile *sf);        /* chunk "BUDG" */
int   budget_load(struct SaveFile *sf);
int   budget_save_ext(struct SaveFile *sf);    /* T10: save: chunk "BUDX" (sub-mk remainders) */
int   budget_load_ext(struct SaveFile *sf);    /* T10: save: after budget_load; 0 = no chunk */
/* F3 addition: the upkeep index in % (100 to 1959, +2.5%/year from 1960, cap 150%) for the Budget panel and
   Query upkeep lines */
int   budget_upkeep_index(void);

#endif
