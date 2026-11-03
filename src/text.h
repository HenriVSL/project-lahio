#ifndef TEXT_H
#define TEXT_H
/* text.h - English strings, sentences, number/money/date formatting, name generators. PORTABLE.
   Owner: T09. DESIGN 0.1 (language), 5.9 (markka), 8.3, THEME 6.1 (tone), THEME 8 (names).

   Encoding: Latin-1 (cp1252). Write non-ASCII as escapes in source: "\xe4" a-umlaut, "\xf6" o-umlaut,
   "\xe5" a-ring, "\xc4" "\xd6" "\xc5" capitals, "\xa7" section sign. The bitmap fonts (T03, art.h) map
   byte codes 1:1 to glyphs, with these extra symbols on control codes: */
#define GLYPH_CHECK   "\x01"     /* check mark (requirement met) */
#define GLYPH_CROSS   "\x02"     /* cross (requirement missing) */
#define GLYPH_BULLET  "\x03"
#define GLYPH_ARROW_UP   "\x04"  /* trend up / down (tooltips) */
#define GLYPH_ARROW_DOWN "\x05"
/* T09 additions: cp1252 punctuation the fonts have */
#define TEXT_MINUS    "\x96"     /* en dash, used as the minus sign in displayed numbers */
#define TEXT_QUOTE    "\x94"     /* Finnish quotes use the right double quote on both sides */
#define TEXT_DOT      "\xb7"     /* middle dot separator */
/* Functions writing into buf always NUL-terminate and return the length. Nothing here draws: ui.h
   (ui_text) draws strings with the bitmap fonts. */
#include "common.h"
#include "defs.h"
#include "sim.h"

void text_init(void);

/* ---- numbers. Thousands are grouped with TEXT_THOUSANDS (","; the approved UI previews use
   "12,500 mk"). Negative numbers are written with TEXT_MINUS (en dash), not '-'. */
#define TEXT_THOUSANDS ','
#define NEW_MARKKA_YEAR 1963     /* the new markka (newspaper/ticker joke only: M7 cut the x100 display, SCOPE_REVIEW) */
int  text_fmt_int(char *buf, int size, s32 v);              /* "12,345", "\x96" "80" */
int  text_fmt_money(char *buf, int size, Money v, int year); /* "1,250 mk" (year unused since M7) */
int  text_fmt_money_delta(char *buf, int size, Money v, int year); /* "+450 mk" / "-450 mk" */
int  text_fmt_pct(char *buf, int size, int pct);            /* "85%" */
int  text_fmt_date(char *buf, int size, int year, int month); /* "October 1961" */
const char *text_month_name(int month);                     /* 0..11 */
/* T09 additions */
int  text_fmt_int64(char *buf, int size, s64 v);            /* grouped, en-dash minus */
int  text_fmt_signed(char *buf, int size, s32 v);           /* "+450" / en-dash "450" / "0" */
int  text_fmt_money_short(char *buf, int size, Money v, int year); /* like text_fmt_money, but from
                                                               10 million on "12.5M mk" (top bar) */
int  text_fmt_money_signed(char *buf, int size, Money v, int year); /* "+1,240" (no unit; ledgers) */
s64  text_money_display(Money v, int year);                 /* the number shown (x100 before 1963) */

/* ---- names of things (data names like building names come from defs.h tables) */
const char *text_zone_name(int zone);
const char *text_overlay_name(int ovl);
const char *text_req_label(int req);                        /* "Water", "Land value" */
enum { TERMK_DEMAND, TERMK_SCORE, TERMK_LAND_VALUE, TERMK_BUDGET };
const char *text_term_label(int kind, int id);              /* DT_*, ST_*, LVT_*, BL_* */
const char *text_place_reason(int place);                   /* PLACE_* (world.h); also REJ_* (tools.h) */
/* T09 additions */
const char *text_status_name(int ts);                       /* TS_*: "Market town" */
const char *text_era_name(int era);                         /* ERA_* */
const char *text_demand_name(int dem);                      /* DEM_*: "Residential" */
const char *text_tool_name(int tool, int param);            /* "Street", "Houses", "Bulldoze", building name */
const char *text_lot_status(int status);                    /* LOT_*: "Under construction" */
const char *text_mood_name(int mood);                       /* PRES_MOOD_* */
/* "Unlocks at: Market town" detail: the status name, the year, or both ("Market town, 1960").
   Returns 0 (empty buf) if (status_req, era_year) is already reached. */
int  text_unlock_hint(char *buf, int size, int status_req, int era_year, int cur_year, int cur_status);

/* ---- sentences */
int  text_req_line(char *buf, int size, const ReqItem *it); /* GLYPH_CROSS "Water (no main within reach)" */
int  text_event(char *buf, int size, const SimEvent *e, int *severity); /* ticker line; 0 = don't show */
int  text_problem(char *buf, int size, const SimProblem *p); /* "340 residents without water (Koivurinne)" */
int  text_president_line(char *buf, int size, int mood, u32 h); /* plaque quote ("Good, good.") */
/* President moods (art pres_s_<mood>, T03 order). */
enum { PRES_MOOD_CONTENT, PRES_MOOD_STERN, PRES_MOOD_DELIGHTED, PRES_MOOD_WORRIED, PRES_MOOD_COUNT };
int  text_event_topic(const SimEvent *e);                   /* rate-limit topic for the ticker (T09) */

/* ---- name generators (THEME 8), deterministic from the RNG/hash given */
void text_town_name(Rng *r, char *buf, int size);
void text_district_name(u32 h, char *buf, int size);
/* T09 additions */
void text_street_name(u32 h, char *buf, int size);          /* "Pihlajakuja", "Graniittitie" */
void text_district_at(int x, int y, char *buf, int size);   /* district of a tile (16x16 tile cells,
                                                               seeded by sim_settings()->seed) */
void text_address(int x, int y, char *buf, int size);       /* "Koivukuja 14, M\xe4ntyrinne" */

#endif
