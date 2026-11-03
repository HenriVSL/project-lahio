/* politics.h - blocs, popularity, authority, decrees, letters, event cards, elections, Eastern relations /
   Western trade meters. PORTABLE, deterministic, integer-only. Owner: M1 (Milestone 2). DESIGN 6,
   DESIGN_LESSONS 4.10, THEME 6 (tone).

   Model (numbers in politics.cpp / poltab.cpp; docs/HANDOFF.md M1):
   - Four BLOCS. Each month every resident lot gives its occupants to the blocs by housing type, tier and
     land value (cottages mostly Rural, apartments mostly Workers, high land value Intelligentsia); business
     owners (shop + industry jobs) are the Industrialists; the offscreen countryside adds Rural voters that
     fade out by 1990. weight = members / total (per mille). APPROVAL (0..100) = base + itemised reasons
     computed from the city (jobs, homeless, emigration, taxes, services, forest loss, growth rate, power,
     export demand, pollution, land value, concrete share, meters, active decrees, timed event effects);
     it moves 1/3 of the way to that target each month. POPULARITY = weighted mean of approvals.
   - Two METERS 0..100: Eastern relations (scales the clearing I-demand term 75..125% and plant/pump upkeep
     125..75%) and Western trade (scales the Western orders term 75..125%). Decrees, letters and event
     choices move them; they drift slowly back toward 50.
   - AUTHORITY: + popularity/20 per month (kept in tenths), cap 30; a thin election win gives -1/month and
     cap 25 for the term, a landslide +1/month and cap 35.
   - DECREES (table in poltab.cpp): at most POL_SLOTS active, each costs authority, repeal costs half;
     enacting or repealing starts a 12-month cooldown for that decree, and a new decree may be signed at
     most every POL_DECREE_GAP months. Effects are simulation-parameter modifiers (politics_mod) plus bloc
     approval offsets while active, one-off meter/authority/money changes when signed, monthly money/meter
     changes while active. One-shot decrees (emergency powers law) take no slot.
   - LETTERS: cheap interventions (2-4 authority), one every POL_LETTER_GAP months.
   - EVENT CARDS: at most one pending; the next card becomes possible 12-24 months after the previous one
     was answered; never within 6 months of an election. A card waits (ticker) until the player opens it;
     unanswered cards resolve themselves with their first choice after POL_EVENT_EXPIRE months.
   - ELECTIONS every 6 years in January (1956 + 6k; the first at least 2 years after the start). Never lost:
     electors = 151 + (share - 40) * 109 / 35 (cap 280) where share = popularity + momentum/2 + campaign +
     turnout. A campaign card arrives 3 months before. The emergency powers law skips one election.
   - When politics is OFF every getter returns neutral values (politics_mod: 100 / 0), politics_month does
     nothing and the simulation is bit-identical to the game without politics.

   Sim hooks (the only places politics changes the simulation; all neutral when off):
     sim.cpp   demand terms DT_POLICY (R: rural migration, C: spending, I: total), clearing/western terms,
               emigration rate, apartment growth score term ST_POLICY;
     fields.cpp industrial pollution, land value growth, happiness, school land-value weight;
     net.cpp   school capacity;
     money     booked by politics_month on BL_POLITICS (tax income, school upkeep, construction rebate,
               export share, decree running costs) and BL_UTILITIES (Eastern relations), from last month's
               ledger lines. */
#ifndef POLITICS_H
#define POLITICS_H
#include "common.h"
#include "defs.h"

enum { BLOC_WORKERS, BLOC_RURAL, BLOC_INDUSTRIALISTS, BLOC_INTELLIGENTSIA, BLOC_COUNT };
enum { METER_EAST, METER_WEST, METER_COUNT };

#define POL_SLOTS          5      /* active decrees ("cabinet slots") */
#define POL_AUTH_CAP       30
#define POL_COOLDOWN       12     /* months per decree after signing/repealing */
#define POL_DECREE_GAP     3      /* months between two new decrees */
#define POL_LETTER_GAP     6      /* months between letters */
#define POL_EVENT_MIN      12     /* months between event cards: MIN + rng(MAX - MIN) */
#define POL_EVENT_MAX      24
#define POL_EVENT_EXPIRE   12     /* unanswered card resolves itself (first choice) */
#define POL_TERM_YEARS     6

/* ------------------------------------------------------------------ simulation parameter modifiers
   _PCT: percent, neutral 100. _ADD: additive, neutral 0. Read by sim/fields/net every time they need it
   (a table lookup). M5/M6/M7 read the hook ones (crime, elements) when their systems exist. */
enum { PM_MIGRATION_PCT,     /* rural migration pull (R demand) */
       PM_EMIGRATION_PCT,    /* emigration of the unemployed to Sweden */
       PM_COM_DEMAND_PCT,    /* commercial demand (spending) */
       PM_IND_DEMAND_PCT,    /* industrial demand (all terms) */
       PM_CLEARING_PCT,      /* clearing quota term (incl. Eastern relations 75..125%) */
       PM_WESTERN_PCT,       /* Western orders term (incl. Western trade 75..125%) */
       PM_HAPPY_ADD,         /* happiness points (fields) */
       PM_APT_SCORE_ADD,     /* apartment growth score (housing programme) */
       PM_LV_GROWTH_PCT,     /* speed of land value rises */
       PM_IND_POLLUTION_PCT, /* industrial zone pollution */
       PM_SCHOOL_CAP_PCT,    /* school capacity */
       PM_SCHOOL_LV_PCT,     /* weight of school coverage in land value */
       PM_TAX_INCOME_PCT,    /* resident + business tax income (booked by politics) */
       PM_SCHOOL_UPKEEP_PCT, /* school upkeep (booked by politics) */
       PM_CONSTRUCTION_PCT,  /* construction spending (rebate booked by politics) */
       PM_EXPORT_PCT,        /* export share income (booked by politics) */
       PM_UTILITY_PCT,       /* plant/pump upkeep (incl. Eastern relations; booked by politics) */
       PM_CRIME_PCT,         /* hook for M5 (police/crime) */
       PM_ELEMENTS_PCT,      /* hook for M5 (element factory output) */
       PM_COUNT };
int  politics_mod(int pm);
#define POL_PCT(v, m) ((m) == 100 ? (v) : (s32)((s64)(v) * (m) / 100))

/* ------------------------------------------------------------------ effects (decrees, letters, cards) */
enum { FX_NONE,
       FX_BLOC,        /* target bloc, value approval points, months (0 = while the decree is active) */
       FX_METER,       /* target meter, value now */
       FX_METER_M,     /* target meter, value per month while the decree is active */
       FX_AUTH,        /* value authority now */
       FX_MONEY,       /* value x 10 mk now (negative = cost) */
       FX_MONEY_POP,   /* value mk per 100 residents now (min 300 mk scale) */
       FX_MONEY_M,     /* value mk per month while the decree is active */
       FX_PM,          /* target PM_*, value (percent change for _PCT, points for _ADD), months (0 = while active) */
       FX_ELECTION,    /* value share points at the next election */
       FX_SKIP_ELECTION,
       FX_REPEAL,      /* target decree repealed for free */
       FX_FASTTRACK,   /* value lots under construction finished now */
       FX_SOFTEN,      /* halve the worst negative timed bloc effect */
       FX_COUNT };
typedef struct { u8 kind; s8 target; s16 value; u8 months; } PolFx;
#define POL_FX_MAX 6

/* ------------------------------------------------------------------ data tables (poltab.cpp) */
enum { DEC_HOUSING, DEC_RENT, DEC_BEER, DEC_REGIONAL, DEC_SCHOOL, DEC_FIVEDAY, DEC_SMOKESTACK, DEC_FRIENDSHIP,
       DEC_EMERGENCY, DEC_WESTERN, DEC_HOMECOMING, DEC_PRESS, DEC_BONFIRE,
       DEC_TOWN_RIGHTS,   /* hook for M7: town status granted by decree (hidden until M7 enables it) */
       DEC_STATE_WORKS,   /* hook for M6: Valtion Metalli placed by decree (hidden until M6 enables it) */
       DEC_COUNT };
enum { DF_ONESHOT = 1,    /* applies once, takes no slot, can't be repealed */
       DF_HIDDEN  = 2 };  /* not offered in the UI (hooks for later tasks) */
typedef struct {
    const char *key, *name;
    const char *desc;             /* period-tone sentence for the decree panel */
    u8    cost;                   /* authority */
    s16   year_from;
    u8    flags;                  /* DF_* */
    PolFx sign[POL_FX_MAX];       /* applied once when signed (meters, authority, money, election) */
    PolFx active[POL_FX_MAX];     /* while active: FX_PM / FX_BLOC (months 0) / FX_MONEY_M / FX_METER_M */
} DecreeDef;
extern const DecreeDef g_decrees[DEC_COUNT];

enum { LET_HOUSING, LET_EDITOR, LET_MINISTRY, LET_FINANCE, LET_EMBASSY, LET_FARMERS, LET_COUNT };
typedef struct {
    const char *key, *to, *subject, *body;   /* body: the typed letter (\n = line break) */
    u8    cost;
    PolFx fx[POL_FX_MAX];
} LetterDef;
extern const LetterDef g_letters[LET_COUNT];

enum { EV_NIGHT_FROST, EV_NOTE, EV_STRIKE, EV_DELEGATION, EV_STATUE, EV_SAUNA, EV_OLYMPICS, EV_TV, EV_SCANDAL,
       EV_SWEDEN_BOAT, EV_FESTIVAL, EV_TEMPERANCE, EV_WINTER, EV_TRADE_FAIR, EV_FISHING,
       EV_CAMPAIGN,       /* the election campaign card (not random) */
       /* M6 paper mill cards (complex.cpp queues them in the random card's slot; never drawn at random: weight 0):
          the patron's 3 demands and the 2 risk events. Answers come back via politics_set_event_hook */
       EV_CPX_TAX, EV_CPX_PERMIT, EV_CPX_UNION, EV_CPX_SLUMP, EV_CPX_MODERN,
       /* M7: the 5-year clearing agreement card (era.cpp queues it at a period start; weight 0; choice order = TL map in
          era_card_answered: 0 Standard, 1 Modest, 2 Ambitious) */
       EV_AGREEMENT,
       EV_COUNT };
#define POL_CHOICES 3
typedef struct { const char *label, *result; PolFx fx[POL_FX_MAX]; } PolChoice;
typedef struct {
    const char *key, *title, *text;
    int nch;
    PolChoice ch[POL_CHOICES];
} EventDef;
extern const EventDef g_events[EV_COUNT];

/* bloc approval reasons (Term ids) */
enum { PR_BASE, PR_JOBS, PR_UNEMPLOYMENT, PR_HOMELESS, PR_EMIGRATION, PR_TAX_RES, PR_TAX_BUS, PR_SERVICES,
       PR_HOUSING, PR_FOREST, PR_GROWTH, PR_POWER, PR_EXPORTS, PR_COMMUTE, PR_LABOUR, PR_POLLUTION,
       PR_LAND_VALUE, PR_CULTURE, PR_CONCRETE, PR_WEST, PR_EAST, PR_DECREES, PR_EVENTS, PR_COUNT };
#define POL_REASONS 10

/* ------------------------------------------------------------------ lifecycle */
void politics_init(void);
void politics_reset(int enabled);          /* sim_new_city: fresh state (seeded from sim_settings()->seed) */
void politics_month(void);                 /* sim, at month end after the budget */
int  politics_enabled(void);

/* ------------------------------------------------------------------ state (UI, tests, sim) */
int  politics_popularity(void);            /* 0..100 (off: 50) */
int  politics_approval(int bloc);          /* 0..100 smoothed (off: 50) */
int  politics_approval_target(int bloc);   /* this month's computed value */
int  politics_weight(int bloc);            /* per mille of the electorate */
s32  politics_members(int bloc);
int  politics_reasons(int bloc, Term *out, int max);   /* itemised target (ids PR_*), sum = target */
int  politics_meter(int meter);            /* 0..100 */
int  politics_authority(void);             /* whole points */
int  politics_authority_cap(void);
int  politics_authority_income10(void);    /* tenths per month */
int  politics_mood(void);                  /* PRES_MOOD_* (text.h) */
int  politics_term_mod(void);              /* -1 thin win, 0, +1 landslide */
int  politics_next_election(void);         /* year (January) */
int  politics_months_to_election(void);
typedef struct {
    int year, electors, share, momentum, campaign, turnout, verdict;   /* verdict -1 thin, 0, +1 landslide */
    int skipped;                           /* 1 = the emergency powers law skipped this one */
    int bloc_approval[BLOC_COUNT], bloc_weight[BLOC_COUNT];
    int auth_now;                          /* authority change on the night */
} PolElection;
const PolElection *politics_last_election(void);   /* NULL if none held yet */
int  politics_elections_held(void);

/* decrees */
enum { DS_OK, DS_ACTIVE, DS_COOLDOWN, DS_NO_AUTHORITY, DS_NO_SLOT, DS_TOO_EARLY, DS_GAP, DS_HIDDEN, DS_OFF,
       DS_ALREADY };
int  politics_decree_active(int dec);
int  politics_decree_cooldown(int dec);    /* months left */
int  politics_decree_gap(void);            /* months until a new decree may be signed */
int  politics_decree_state(int dec);       /* DS_* for signing (DS_ACTIVE if active) */
int  politics_repeal_state(int dec);       /* DS_OK if it can be repealed now */
int  politics_active_count(void);
int  politics_sign(int dec);               /* 1 = signed (player action: call between sim steps) */
int  politics_repeal(int dec);
void politics_enable_hook_decree(int dec, int on);   /* M6/M7: offer a DF_HIDDEN decree */
/* letters */
int  politics_letter_gap(void);            /* months until the next letter */
int  politics_letter_state(int let);       /* DS_* */
int  politics_send_letter(int let);
/* events */
int  politics_pending_event(void);         /* EV_* or -1 */
int  politics_event_age(void);             /* months pending */
int  politics_event_count(int ev);         /* times answered (sauna: diminishing returns) */
int  politics_choose(int choice);          /* answer the pending card */
int  politics_queue_event(int ev);         /* M6/M7: put a card up now if the shared card slot is free (P1: the ONE gate
                                              for every source: <= 1 card per game year, DESIGN 14.7); 0 = refused */
int  politics_card_slot_free(void);        /* P1: a random/patron card may go up now: nothing pending, >= 12 months since
                                              the last card, the 12-24 month timer has run out, no election within 6 months */
int  politics_agreement_slot_free(void);   /* P1: the same without the random timer (the 5-yearly agreement card) */
int  politics_force_event(int ev);         /* P1: tests / `pol event` debug command: bypass the rhythm (still 1 pending) */
void politics_choice_fx(int ev, int choice, PolFx *out);   /* effective effects (sauna scaling) */
int  politics_months_to_event(void);
/* meters (M3/M7 trade hooks) */
void politics_meter_add(int meter, int delta);
void politics_authority_add(int delta);
void politics_status_ceremony(int ts);     /* M7 hook: town status granted (now: small authority gift) */
/* M6: called when any event card is answered (also when it expires: choice 0), after its effects applied */
typedef void (*PolEventHook)(int ev, int choice);
void politics_set_event_hook(PolEventHook fn);

/* ------------------------------------------------------------------ messages for the ticker
   politics_month and player actions queue these; game.cpp drains them with politics_poll_msg and hands
   them to the UI (ui_pol). Ring of 16. */
enum { PMSG_EVENT, PMSG_EVENT_EXPIRED, PMSG_CAMPAIGN, PMSG_ELECTION, PMSG_ELECTION_SKIPPED, PMSG_FROST,
       PMSG_BLOC_ANGRY, PMSG_DECREE_SIGNED, PMSG_DECREE_REPEALED, PMSG_LETTER, PMSG_CHOICE, PMSG_AUTH_FULL,
       PMSG_COUNT };
typedef struct { u8 type; s16 a, b; } PolMsg;
int  politics_poll_msg(PolMsg *m);

/* ------------------------------------------------------------------ text (poltab.cpp) */
const char *politics_bloc_name(int bloc);          /* "Workers" */
const char *politics_bloc_finnish(int bloc);       /* "Ty\xf6v\xe4ki" */
const char *politics_meter_name(int meter);
const char *politics_reason_label(int pr);
const char *politics_pm_label(int pm);
int  politics_fx_text(char *buf, int size, const PolFx *fx);   /* "Workers +6", "Rural migration -40%" ... */
int  politics_msg_text(char *buf, int size, const PolMsg *m, int *severity);
const char *politics_verdict_text(int verdict);
int  politics_ds_text(char *buf, int size, int ds, int dec);   /* why a decree can't be signed now */

/* ------------------------------------------------------------------ scripts, debug, save */
int  politics_script(char **tok, int n, FILE *out); /* "pol enable|disable|sign KEY|repeal KEY|letter KEY|choose N|
                                                       event KEY|meter east|west D|auth D|dump|election"; 1 = ok */
int  politics_find_decree(const char *key);
int  politics_find_letter(const char *key);
int  politics_find_event(const char *key);
void politics_dump(FILE *f);
u32  politics_hash(void);

struct SaveFile;
int  politics_save(struct SaveFile *sf);   /* chunk "POLI" v1 (only written when enabled) */
int  politics_load(struct SaveFile *sf);   /* missing chunk: fresh state per sim_settings()->politics */

#endif
