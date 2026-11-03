/* complex.h - M6: unique industrial complexes: the paper mill (Kuusikosken Paperi Oy, with its patruuna) and the
   state metal works Valtion Metalli. Gifts the player places, demand cards, the company-town rule, telegraphed risk
   events, exports for the trade system. PORTABLE, deterministic, integer-only. Owner: M6 (Milestone 2).
   Spec: docs/research/INDUSTRY.md 7, DESIGN 14.3; art keys ART.md 3.1 (pm_*, vm_*, row_houses, patron_villa).

   Model (numbers in complex.cpp; docs/HANDOFF.md M6):
   - A COMPLEX is a fixed-orientation set of ordinary module lots (BCAT_SPECIAL, BDF_MODULE | BDF_NO_ROTATE, rot 0,
     each <= 3x3) placed in one tool action from the module table (offsets from the north tile, ART.md 3.1):
       paper mill 5x5 (pm_logyard, pm_hall, pm_chipper, pm_boiler, pm_digester, pm_gate, pm_office), the -X side
       (log pond) must touch lake or river water on >= 2 of its 5 tiles; on rapids the mill has its own hydro
       station (no grid power needed); 5,000 mk; Church village.
       Valtion Metalli 5x4 (vm_hall, vm_yard, vm_store, vm_siding, vm_gate) needs a railway tile within 2;
       Market town; politics on: placed after the hidden decree DEC_STATE_WORKS (the state builds it, 0 mk),
       politics off: 6,000 mk.
     Both need a road within 2 tiles and are unique. The modules take 6 months to build. Bulldozing any module
     demolishes the complex at the next month end.
   - STAGES. Paper: 1 groundwood mill + PM1 (300 jobs, wood 40) -> 2 pulp mill + PM2 (600, wood 80: town >= 3,000)
     -> 3 PM3 "the big machine" (900, wood 120: year >= 1963, town >= 6,000, a railway station). A stage also needs
     the wood for it (supply left after the sawmills), the current jobs staffed (>= 90% for 6 months), the owner's
     mood > 30 and no slump/strike; it takes 6 months. Metal: product lines (tractors 1955, paper machines 1960,
     rolling stock 1965, ship sections 1970), +300 jobs each, same staffing and mood rules.
   - JOBS enter the city's job pool (sim aggregate: complex_jobs()); the complex is staffed like any employer.
     OUTPUT % = staffing x wood (paper) x events x contract; EXPORTS (volume units like industry.h: 1 = one
     engineering job) = nominal stage volume x output: paper 150 per 100 jobs, West 80/75/70% by stage; metal 100
     per 100 jobs, East 85% (the clearing quota). Valtion Metalli: each 5-year agreement (1956, 1961, ...) sets
     +-25% by Eastern relations; from 1990 (end of clearing trade) output and jobs halve.
   - OWNER MOOD 0..100 (patron / ministry; start 60) moves 1/3 of the way to its target each month. Target = 50 +
     business tax (<= 9%: +10, else -10 per point), railway station +10, wood supply +5 / -10, staffing +5 / -10,
     town services (school, health, fire capacity) +3 each, smokestack decree -20, gifts placed (+10 each, fading),
     card answers (timed). Metal: Eastern relations instead of the tax term.
   - GIFTS (mood >= 70, one per 24 months (metal 36), queue of 3): the owner OFFERS, the player PLACES it free
     within 12 tiles of the complex with the gift tool (TOOL_GIFT): paper: workers' row houses, company sauna,
     company church, factory school, the patron's villa, then row houses; metal: vocational school, ARAVA row
     houses. Each placed gift adds +10 mood that fades by 1 every 2 months. The company pays the upkeep of its
     sauna and school.
   - COMPANY TOWN: complex jobs >= 40% of all jobs -> the company pays 20% (metal: 10%) of last month's school,
     clinic/health and fire upkeep + its gift upkeep, booked on BL_COMPANY each month.
   - DEMAND CARDS (politics on only; with politics off the patron simply has no demands): at most one a year
     (12-24 months apart) via politics_queue_event: lower the business tax, a water permit, the union's workers'
     hall; mood <= 30: "the board may put PM3 in another town" (stage 3 waits until mood >= 40); metal: the
     anniversary quota. The answer comes back through politics_set_event_hook.
   - RISK EVENTS, always telegraphed 3 months ahead (ticker; politics on: a response card at the announcement,
     default answer = choice 0 when nobody answers): paper slump in London (around 1958, 1967, 1971, 1976 +-1 year:
     jobs and output -30% for 12-18 months), the general strike of March 1956 and later strikes (Workers bloc /
     unemployment: output 0 for a month, mood down), the 1970s modernisation (1972-78, stage 3: accept = 600 jobs,
     same output, -30% pollution; refuse = output -20%, and from 1985 the mill is uncompetitive: 600 jobs, 70%),
     the 1980s merger (the family sells to a Helsinki group: no more gifts, demands or contribution).
   Debug from text: complex_dump(); script "complex ...". */
#ifndef COMPLEX_H
#define COMPLEX_H
#include "common.h"
#include "defs.h"
#include "tools.h"

enum { CPX_NONE, CPX_PAPER, CPX_METAL, CPX_KIND_COUNT };
#define MAX_COMPLEX     4
#define CPX_GIFT_QUEUE  3
#define CPX_MAX_GIFTS   16
#define CPX_GIFT_RADIUS 12          /* gifts go within 12 tiles (Chebyshev) of the complex */
#define CPX_BUILD_MONTHS 6
#define CPX_DEP_PCT     40          /* company town: complex jobs >= 40% of all jobs */

enum { CF_RAPIDS = 0x01,           /* paper: own hydro on the rapids (no grid power) */
       CF_MODERNISED = 0x02,       /* paper: accepted the new machine (fewer jobs, same output) */
       CF_REFUSED = 0x04,          /* paper: refused the modernisation (output -20%; 1985+: uncompetitive) */
       CF_MERGED = 0x08,           /* paper: sold to a Helsinki group; the patron has left */
       CF_HONOURED = 0x10,         /* paper: the new owners honour the patron's promises (10% contribution) */
       CF_PERMIT = 0x20,           /* paper: water permit granted (output +10%, more effluent) */
       CF_THREAT = 0x40,           /* paper: the board threatens to put PM3 elsewhere (stage 3 waits for mood 40) */
       CF_ACTIVE = 0x80,           /* production has started (the modules are finished) */
       CF_STRUCK1956 = 0x100 };    /* paper: the 1956 strike has been scheduled */

/* risk event kinds (Complex.ev) */
enum { EVK_NONE, EVK_SLUMP, EVK_STRIKE, EVK_MODERN, EVK_MERGER, EVK_VM_STRIKE, EVK_COUNT };
enum { EVS_NONE, EVS_ANNOUNCED, EVS_RUNNING };

typedef struct {
    u8  kind;                /* CPX_* (CPX_NONE = free slot) */
    u8  x, y;                /* north tile of the template */
    u8  stage;               /* 0 = being built; paper 1..3; metal product lines 1..4 */
    u8  next;                /* stage under construction (0 = none) */
    u8  build_left;          /* months until `next` is ready */
    u8  mood, mood_target;   /* owner: patron / the Ministry, 0..100 */
    u16 flags;               /* CF_* */
    s16 jobs;                /* jobs offered this month */
    s16 employed;            /* of them filled */
    s16 output;              /* output % this month (0..150) */
    s16 wood_need;           /* wu / month (industry_set_ext_wood) */
    s16 wood_pct;            /* wood delivered last month % */
    s16 staffed;             /* consecutive months >= 90% staffed */
    s16 gift_cd, demand_cd;  /* months until the next gift offer / demand card */
    s16 gift_boost;          /* mood points from placed gifts (fades 1 per 2 months) */
    u8  gifts;               /* gifts offered so far (index into the gift sequence) */
    u8  ev, ev_state, ev_resp, ev_card;   /* risk event: EVK_*, EVS_*, response (255 = none yet), card queued */
    s16 ev_start, ev_len;    /* month index (year * 12 + month) and months */
    s16 sched[4];            /* paper: slump starts around 1958, 1967, 1971, 1976 (month index, 0 = done) */
    s16 modern_at, merger_at, strike_at;  /* scheduled starts (month index, 0 = none) */
    s16 strike_gate;         /* no new random strike before this month index */
    s16 adj[4], adj_m[4];    /* timed mood adjustments (card answers): value, months left */
    s16 out_adj, out_adj_m;  /* timed output adjustment (metal quota) */
    s16 agree;               /* metal: output % of the current 5-year agreement (100 = neutral) */
    s16 placed;              /* month index of placement */
    s32 vol, west, east;     /* export volume this month and its split */
    int nterms;
    Term terms[10];          /* mood target reasons (ids MT_*) */
} Complex;
enum { MT_BASE, MT_TAX, MT_STATION, MT_WOOD, MT_STAFF, MT_SERVICES, MT_SMOKESTACK, MT_GIFTS, MT_ANSWERS, MT_EAST,
       MT_COUNT };

typedef struct { u8 def, cpx; } CpxGift;                  /* offered, waiting for the player */
typedef struct { u8 def, cpx, x, y; } CpxPlaced;           /* placed gift lots (upkeep paid by the company) */

/* ------------------------------------------------------------------ lifecycle (sim.cpp, save.cpp) */
void complex_init(void);
void complex_reset(void);                  /* sim_new_city */
void complex_month(void);                  /* month end, after industry_month, before politics_month */

/* ------------------------------------------------------------------ module table */
int  complex_layout(int kind, int k, int *def, int *ox, int *oy);   /* module k of a kind: 1 = exists */
void complex_size(int kind, int *w, int *h);                        /* 5x5 / 5x4 */
int  complex_kind_of_def(int def);         /* CPX_* for a module def (and the tool defs pm_hall/vm_hall) */
int  complex_at_lot(int lot);              /* complex index + 1 owning this lot, 0 = none */
const Complex *complex_get(int i);         /* NULL if free */
const Complex *complex_of_kind(int kind);  /* the (unique) complex of that kind or NULL */

/* ------------------------------------------------------------------ hooks for other modules */
s32  complex_jobs(void);                   /* sim aggregate: jobs of all complexes (enter the job pool) */
s16  complex_module_use(int def, int what);/* net/fields: per-module power / water / pollution this month */
enum { CPXU_POWER, CPXU_WATER, CPXU_POLLUTION, CPXU_COUNT };
#define CPX_DEF_USE(def, field, what) ((g_bdefs[def].flags & BDF_MODULE) ? complex_module_use((def), (what)) : g_bdefs[def].field)
void complex_budget_base(s64 *bus, s32 *export_workers);   /* budget: business tax base + export volume */
/* M7 (trade): export volume units this month (same units as IndKinds.export_vol); east = clearing deliveries */
s32  complex_export_west(void);
s32  complex_export_east(void);
s32  complex_effluent(int *x, int *y);     /* M5 (lake pollution): paper mill effluent units and its pond tile */
int  complex_dependency_pct(void);         /* complex jobs per 100 jobs */
int  complex_company_town(void);           /* 1 = the company-town rule is in force */
s32  complex_contribution_last(void);      /* mk booked on BL_COMPANY last month */
int  complex_risk_ahead(int *ev, int *months);   /* M7 newspaper: an announced risk event (1 = yes) */

/* ------------------------------------------------------------------ placement (tools.cpp delegates)
   Complex tool = TOOL_BUILD with param pm_hall / vm_hall (BDF_MODULE); gift tool = TOOL_GIFT. (x, y) = north tile;
   commit 0 = check only. Returns PLACE_OK or a PLACE_* / REJ_* reason; *cost = total incl. clearing. */
int  complex_check_place(int kind, int x, int y, Money *cost);
int  complex_place(int kind, int x, int y);              /* complex index + 1, 0 = refused (no money check) */
int  complex_tool_locked(int def, char *hint, int size); /* 1 = not available now (hint "Market town", ...) */
Money complex_tool_cost(int def);          /* what the fly-out shows (VM by decree: 0) */
int  complex_gift_pending(void);           /* gifts waiting */
int  complex_gift_def(int k);              /* def of waiting gift k (0 = head), B_NONE if none */
const char *complex_gift_name(int def, int kind);   /* "workers' row houses" */
const char *complex_gift_tool_name(void);  /* fly-out row: "Patron's gift: workers' row houses" */
int  complex_check_gift(int x, int y, int rot);     /* PLACE_OK or reason (head of the queue) */
int  complex_place_gift(int x, int y, int rot);     /* lot id, 0 = refused */
int  complex_tool_update(const ToolInput *in, ToolPreview *p, int tool, int param, int rot,
                         int *lx, int *ly, Money *lcost, int *lreason);   /* TRES_* */
void complex_plan(ToolPreview *p, int tool, int param, int x, int y, int rot);  /* preview only */

/* ------------------------------------------------------------------ politics (cards come back here) */
void complex_on_event(int ev, int choice); /* registered with politics_set_event_hook */
int  complex_vm_permit(void);              /* metal works may be placed now (decree signed / politics off) */

/* ------------------------------------------------------------------ messages for the ticker (game.cpp drains) */
enum { CM_PLACED, CM_PRODUCTION, CM_STAGE_START, CM_STAGE_DONE, CM_GIFT_OFFER, CM_GIFT_PLACED, CM_COMPANY_TOWN,
       CM_EV_ANNOUNCE, CM_EV_START, CM_EV_END, CM_THREAT, CM_VM_DECREE, CM_VM_PERMIT, CM_VM_UNLOCK, CM_AGREEMENT,
       CM_CLEARING_END, CM_UNCOMPETITIVE, CM_DEMOLISHED, CM_MERGED, CM_MODERNISED, CM_COUNT };
typedef struct { u8 type, cpx; s16 a, b; } CpxMsg;
int  complex_poll_msg(CpxMsg *m);
int  complex_msg_text(char *buf, int size, const CpxMsg *m, int *severity, int *x, int *y);

/* ------------------------------------------------------------------ text (query panel), names */
const char *complex_name(int kind);        /* "Kuusikosken Paperi Oy", "Valtion Metalli" */
const char *complex_owner_name(int kind);  /* "Patruuna Albin Hellstedt", "the Ministry's director" */
int  complex_lot_line(int lot, int k, char *buf, int size, int *font);  /* line k of a module's query; 0 = none.
                                                            font: 0 normal, 1 grey, 2 red, 3 green, 4 head */
/* ------------------------------------------------------------------ tests, scripts, debug, save */
void complex_set_mood(int kind, int mood); /* tests / scripts */
int  complex_paper_stage_ok(int next, s32 pop, s32 wood_avail, int year, int station, int staffed, int mood,
                            int threat);   /* 1 = the conditions for paper stage `next` are met (pure) */
int  complex_script_line(int ntok, char **tok, int org_x, int org_y, FILE *out);   /* SCR_*; -1 not ours */
void complex_dump(FILE *f);
u32  complex_hash(void);                   /* 0 when there is no complex and nothing happened */
void complex_after_load(int had_chunk);    /* recompute the module use tables */
struct SaveFile;
int  complex_save(struct SaveFile *sf);    /* chunk "CPLX" v1 */
int  complex_load(struct SaveFile *sf);    /* 0 = no chunk */

#endif
