/* era.h - M7: eras and era events (1957/1967 devaluations, 1963 new markka, 1973/1979 oil shocks, 1986 oil
   collapse, 1990 end of clearing trade + the end-of-era newsreel), town status by decree (DESIGN 7 ladder),
   landmarks (concert hall, TV tower, ski jump, presidential sauna, friendship statue: authority, tourism,
   Eastern relations, sauna diplomacy), the yearly/monthly history for graphs and the newspaper, newspaper
   issues (each January: 3 headlines from the year's biggest changes + a satirical filler; ceremonies; era
   events), M7 ticker messages and script commands. PORTABLE, deterministic, integer-only. Owner: M7.
   Trade (clearing, Western, oil, loans) is trade.h; era.cpp is the single entry point the shared files call.

   Hooks in shared files (one line each):
     sim.cpp   sim_new_city -> era_reset(); month_end_books: era_month() after politics_month, the status target
               through era_status_target(); compute_demand: trade_clearing_pct / trade_western_pct;
               emigration: trade_emigration_pct; sim_hash ^= era_hash().
     save.cpp  era_save / era_load (chunks "ERA " + "TRAD").
     tools.cpp era_script_line (script "trade ..." / "era ..." / "news ...").
     politics.cpp politics_sign(DEC_TOWN_RIGHTS) -> era_town_rights_signed().
     defs.cpp  def_unlocked -> era_unlock_override (event-unlocked landmarks).
     industry.cpp electronics needs trade_tech(TECH_ELECTRONICS).
   Town status: with politics ON a status is only granted by the hidden decree DEC_TOWN_RIGHTS (offered when the
   population has reached the next step; grants every step reached); with politics OFF it is granted
   automatically at month end (as before). Either way the grant shows a ceremony page (newspaper) + the reward. */
#ifndef ERA_H
#define ERA_H
#include "common.h"
#include "defs.h"

/* ------------------------------------------------------------------ lifecycle */
void era_reset(void);                      /* sim_new_city: also trade_reset */
void era_month(void);                      /* month end after politics_month: trade, landmarks, events, history */
int  era_status_target(int st);            /* sim: the status the town may hold now (st = by population) */
void era_town_rights_signed(void);         /* politics_sign(DEC_TOWN_RIGHTS) */
int  era_status_eligible(void);            /* highest status reached by population (decree pending if > held) */
int  era_unlock_override(int def, int year, int status);   /* 1 = unlocked by an event (defs.cpp) */
void era_status_granted(int ts);           /* sim_grant_status: a new status (ceremony page, authority) */

/* ------------------------------------------------------------------ era events */
enum { EE_NONE, EE_DEVAL_1957, EE_NEW_MARKKA, EE_DEVAL_1967, EE_OIL_SHOCK, EE_OIL_SHOCK2, EE_OIL_COLLAPSE,
       EE_CLEARING_END, EE_AGREEMENT, EE_COUNT };
int  era_event_year(int ee, int *month);   /* when the event fires (EE_AGREEMENT: -1) */
int  era_event_seen(int ee);               /* 1 = has happened in this game */

/* ------------------------------------------------------------------ landmarks (defs rows appended by M7) */
int  era_landmark_count(void);             /* placed and active */
int  era_is_landmark(int def);
/* Sauna diplomacy (one button, needs the presidential sauna and politics on): Eastern relations + 8 / (1 + times
   used), at most once a year. era_sauna_state: 0 ready, 1 no sauna, 2 politics off, 3 used this year. */
int  era_sauna_state(void);
int  era_sauna_gain(void);                 /* relations the next evening would bring */
int  era_sauna_invite(void);               /* 1 ok */

/* ------------------------------------------------------------------ the agreement card (politics' one card queue) */
void era_queue_card(void);                 /* trade.cpp at a period start (politics on): queued now or retried monthly */
int  era_card_slot_free(void);             /* no card up and none in the last POL_EVENT_MIN months (M6 may use it too) */
int  era_card_pending(void);               /* 1 = the agreement card is queued or waiting to be queued */
void era_card_answered(int ev, int choice);/* politics resolve_event(EV_AGREEMENT) */

/* ------------------------------------------------------------------ history
   Monthly ring (graphs, ERA_HIST months, oldest first) of series the sim doesn't keep, and a yearly table
   (one row per January since the start, ERA_YEARS max) for the newspaper and the newsreel. */
enum { EH_APPROVAL, EH_EAST, EH_WEST, EH_UNEMPLOYED, EH_BALANCE, EH_WEST_INDEX, EH_OIL_INDEX, EH_HOMELESS,
       EH_LAND_VALUE, EH_COUNT };
#define ERA_HIST  240
#define ERA_YEARS 64
int  era_history(int series, s32 *out, int max);
typedef struct {
    s16 year;
    s32 pop, money, jobs, unemployed, emig_year, homeless, industry_jobs, east, west, balance;
    s32 lots_t3, apartments, landmarks, loan_total;
    s16 status, popularity, east_meter, west_meter, power_warn, water_warn, elections, decrees_signed;
    s16 oil_index, fulfil;
} EraYear;
int  era_years(void);                      /* rows */
const EraYear *era_year_row(int i);        /* 0 = first January */

/* ------------------------------------------------------------------ newspaper (Maakunnan Sanomat)
   era_news() is the latest issue; era_news_pending() > 0 when one waits to be shown (game.cpp pauses and the
   UI shows it; ui_news_close -> era_news_seen). Kinds: the January issue, a ceremony page (town status), the
   end-of-era newsreel (1990, also on demand). Text is plain English with Latin-1 escapes (THEME 6 tone). */
enum { NEWS_NONE, NEWS_YEAR, NEWS_CEREMONY, NEWS_NEWSREEL,
       NEWS_SPECIAL,                       /* PL1: special edition for an urgent problem (advice.h, <= 1 a game year) */
       NEWS_KIND_COUNT };
#define NEWS_HEADS 3
#define NEWS_CAB   3                       /* PL1: "From the Cabinet" lines (advice.h ADV_TEXT) */
typedef struct {
    int  kind, year, month, issue_no, status;  /* status: ceremony TS_* */
    char head[NEWS_HEADS][64];             /* headlines (head[0] = the main one) */
    char deck[NEWS_HEADS][96];             /* one line under each */
    char body[NEWS_HEADS][240];            /* a short paragraph */
    char filler_head[48], filler[200];     /* the satirical filler */
    int  topic[NEWS_HEADS];                /* NT_* (tests) */
    /* PL1 (ERA chunk v2): the Cabinet's advice (January paper, special edition), clickable in the UI (AGO_*, x, y) */
    int  ncab;
    char cab[NEWS_CAB][200];
    u8   cab_kind[NEWS_CAB], cab_go[NEWS_CAB];
    s16  cab_x[NEWS_CAB], cab_y[NEWS_CAB];
    char hind_head[48], hind[240];         /* "In hindsight" (January, replaces the filler when set) */
    int  nverdict;
    char verdict[3][120];                  /* newsreel: "What the history books will say" */
} NewsIssue;
enum { NT_NONE, NT_ERA, NT_STATUS, NT_GROWTH, NT_DECLINE, NT_EMIGRATION, NT_UNEMPLOYMENT, NT_MONEY_UP, NT_MONEY_DOWN,
       NT_LOAN, NT_ELECTION, NT_LANDMARK, NT_CLEARING_DEFICIT, NT_CLEARING_SURPLUS, NT_WEST_UP, NT_WEST_DOWN,
       NT_POWER, NT_WATER, NT_INDUSTRY, NT_HOUSING, NT_AGREEMENT, NT_DECREE, NT_QUIET,
       NT_WINTER,          /* 1.1 W1: the January paper in a hard winter (season.h) */
       NT_COUNT };
const NewsIssue *era_news(void);
int  era_news_pending(void);               /* NEWS_* waiting, 0 none */
void era_news_seen(void);
const NewsIssue *era_news_kind(int kind);  /* the last issue of a kind (NEWS_*) */
void era_news_seen_kind(int kind);
void era_news_build_year(NewsIssue *out);  /* generate the January issue for the current date (tests) */
void era_news_build_ceremony(NewsIssue *out, int ts);
void era_news_build_newsreel(NewsIssue *out);
void era_news_build_special(NewsIssue *out, int adv_kind);   /* PL1: tests / scripts ("news special"); ADV_* */
void era_news_request_newsreel(void);      /* on demand (UI) */
int  era_news_score(int topic);            /* last generation: newsworthiness per topic (tests, debug) */
int  era_newsreel_lines(char lines[][96], int max);   /* summary lines (what you built, decrees, emigration) */

/* ------------------------------------------------------------------ messages (ticker; game.cpp drains) */
enum { EM_OFFER, EM_SIGNED, EM_AUTO_SIGNED, EM_DEFICIT, EM_SHORTFALL, EM_OIL_SHOCK, EM_OIL_SHOCK2, EM_OIL_COLLAPSE,
       EM_CLEARING_END, EM_DEVALUATION, EM_LOAN, EM_LOAN_REPAID, EM_RIGHTS_PETITION, EM_SAUNA_NIGHT, EM_STATUE,
       EM_LANDMARK,
       EM_POWER_NEAR,      /* PL1: a = demand % of supply (>= 90, before a shortage) */
       EM_RIGHTS_NEAR,     /* PL1: a = TS_* within reach, b = population */
       EM_RIGHTS_REMIND,   /* PL1: a = TS_*, b = months the decree has waited */
       EM_REACTOR_ONLINE,  /* 1.2 S1: the first nuclear plant is finished; a = RX_* (power.h), b = units */
       EM_COUNT };
typedef struct { u8 type; s32 a, b; } EraMsg;
void era_push_msg(int type, s32 a, s32 b);
int  era_poll_msg(EraMsg *m);
int  era_msg_text(char *buf, int size, const EraMsg *m, int *severity);   /* SEV_*; 0 = don't show */
int  era_msg_panel(const EraMsg *m);       /* 1 = clicking the ticker opens the trade panel */

/* ------------------------------------------------------------------ scripts, debug, save */
int  era_script_line(int n, char **t, FILE *out);   /* SCR_* (tools.h) or -1 = not an M7 command:
        trade sign modest|standard|ambitious | trade bond | trade complex SLOT GOODS EAST WEST | trade dump |
        era dump | era sauna | news [year|ceremony|newsreel|special [ADV_*]|seen] | advice (PL1 dump) */
void era_dump(FILE *f);
u32  era_hash(void);
struct SaveFile;
int  era_save(struct SaveFile *sf);        /* chunks "ERA " v1 + "TRAD" v1 */
int  era_load(struct SaveFile *sf);        /* missing chunks: fresh state for the save's date */

#endif
