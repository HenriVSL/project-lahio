/* hard.h - 1.1 D1 "Harder times": the Sweden years 1969-70, the era expectations (1975 library, 1980 clinic) and the
   texts that name the difficulty levers (Guide page, tooltips, newspaper, Cabinet). PORTABLE, deterministic,
   integer-only. Owner: D1. Spec: docs/DESIGN_1_1.md section 2 (table 2.2, 2.4, 2.5). The parameter table itself is
   g_diff / diff() in defs.h.

   SWEDEN (Normal/Hard, diff()->sweden_permille > 0, January 1969 - December 1970): every cycle compute_demand calls
   hard_sweden_eval(): the R demand gets the term DT_SWEDEN = -pop x permille x (3 - reasons)^2 / 9 / 1000 people per
   month (1.1 B1: squared, was x (3 - reasons) / 3: one missing reason costs a ninth, two four ninths, all three the full
   pull; it also lowers the move-in flow), and at the month end the sim moves that many residents out (employed ones
   too, round robin like the other emigration) and books them with hard_sweden_book(). Reasons to stay: jobs
   (unemployment <= 3 % of workers), homes (no homeless and vacancy >= 2 %), contentment (average
   happiness >= 60). The emigrants count in CityStats.emigrated_month/total (the Workers bloc, the ticker) and in the
   1969-70 sub-total here (population panel, newsreel).

   ERA EXPECTATIONS (diff()->expectations): sim.cpp eval_reqs adds REQ_LIBRARY to houses T3 / apartments T3 from
   EXPECT_LIBRARY_YEAR, REQ_HEALTH to apartments T2+ from EXPECT_CLINIC_YEAR and (1.1 B1) REQ_HEALTH_CENTRE to houses T3 /
   apartments T3 from EXPECT_HCENTRE_YEAR (the town has a health centre per 20,000; defs.h): upgrades need it from the
   year on, standing lots only after EXPECT_GRACE_MONTHS; the year before it is a met, greyed checklist line ("from
   1975"). hard_expect_phase() is the shared rule. Announced in the January paper of the year before (and in force in
   the paper of the year), and by the Cabinet.

   Hooks in shared files (one line or a small block each): sim.cpp (sim_new_city -> hard_reset; compute_demand ->
   hard_sweden_eval; month_end_population -> hard_sweden_book; eval_reqs expectations), save.cpp (hard_save /
   hard_load, chunk "HARD"), era.cpp (January paper story, newsreel sub-total), advice.cpp (Cabinet lines), text.cpp
   (ticker), ui.cpp / menu.cpp (New Game radio, pause menu, tooltips, Guide page). */
#ifndef HARD_H
#define HARD_H
#include "common.h"
#include "defs.h"

/* ------------------------------------------------------------------ lifecycle */
void hard_reset(void);                     /* sim_new_city (after diff_set_level) */

/* ------------------------------------------------------------------ Sweden 1969-70 */
typedef struct {
    u8  active;                            /* this is a Sweden month on this level */
    u8  jobs, homes, content;              /* reasons to stay met (1) */
    u8  reasons;                           /* 0..3 */
    s32 pull;                              /* people this month at the current reasons (the R term is -pull) */
    s32 month;                             /* residents the recruiters took last month */
    s32 total;                             /* 1969-70 sub-total */
} HardSweden;
const HardSweden *hard_sweden(void);
int  hard_sweden_year(int year);           /* 1 = 1969 or 1970 and the level has a pull */
s32  hard_sweden_eval(void);               /* compute_demand: reasons + pull from sim_stats(); returns the R term (<= 0) */
void hard_sweden_book(s32 n);              /* month end: n residents left with the recruiters */
/* "Reasons to stay: jobs \x95 homes X contentment X" (glyphs from text.h); returns the length */
int  hard_sweden_reasons(char *buf, int size);
/* "Sweden is hiring: -36 this month. Reasons to stay: ..." or 0 when not a Sweden month */
int  hard_sweden_line(char *buf, int size);

/* ------------------------------------------------------------------ era expectations */
enum { HX_LIBRARY, HX_CLINIC, HX_HCENTRE, HX_COUNT };   /* 1.1 B1: + the 1985 health centre (town-wide) */
/* phase of expectation `which` for (zone, tier) at (year, month) on this level: 0 = none, 1 = announced (year before:
   shown met, greyed), 2 = grace (standing lots keep their tier; upgrades need it), 3 = in force */
int  hard_expect_phase(int which, int zone, int tier, int year, int month);
int  hard_expect_year(int which);          /* EXPECT_*_YEAR */
int  hard_expect_req(int which);           /* REQ_LIBRARY / REQ_HEALTH */
int  hard_expect_svc(int which);           /* SVC_LIBRARY / SVC_HEALTH */
/* 1.1 B1: the 1985 expectation is met town-wide: health centres >= 1 + population / EXPECT_HCENTRE_PER, asked only of a Town
   (the health centre's status; smaller towns always meet it). Cached per sim cycle, recomputed from the lots and stats, so a
   loaded game sees the same */
int  hard_hcentre_ok(s32 *have, s32 *need);

/* ------------------------------------------------------------------ texts (newspaper, Cabinet, Guide, tooltips) */
/* January paper (era.cpp era_news_build_year): 1 = a D1 story for the issue of `year`; *slot = where it goes (0 = the
   main headline, 1 = second) */
int  hard_news_story(int year, char *h, int hs, char *d, int ds, char *b, int bs, int *slot);
/* Cabinet lines (advice.cpp collect_all): k = 0 Sweden, 1 library, 2 clinic, 3 health centre; 1 = say it now (text, score, residents) */
int  hard_advice(int k, char *text, int size, s32 *score, s32 *residents);
/* the level's table as Guide lines ("Presidency: Normal", one lever per line, Easy values in brackets); returns count */
int  hard_level_lines(char lines[][100], int max);
/* "Difficulty: Hard (upkeep x115 %)" style notes for one place: HN_* */
enum { HN_UPKEEP, HN_TAX, HN_INDEX, HN_OVERTIME, HN_EMIGRATION, HN_DECLINE, HN_CARDS, HN_AUTHORITY, HN_EAST, HN_SHOCK,
       HN_FIRE, HN_LEVEL, HN_COUNT };
int  hard_note(int what, char *buf, int size);   /* 0 = nothing to say (Easy, or the lever is at its 1.0 value) */

/* ------------------------------------------------------------------ debug, save */
void hard_dump(FILE *f);
struct SaveFile;
int  hard_save(struct SaveFile *sf);       /* chunk "HARD" v1 */
int  hard_load(struct SaveFile *sf);       /* missing: fresh (no Sweden emigrants booked) */

#endif
