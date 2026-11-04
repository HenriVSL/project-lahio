/* era.cpp - M7: eras and era events, town status by decree, landmarks, history, the newspaper and the newsreel,
   M7 ticker messages, script commands and the save chunks of era + trade. PORTABLE, deterministic, integer-only.
   Owner: M7. See era.h (model and hooks) and trade.h. Everything here is a function of the city state and the
   date; nothing uses a random number (the filler pick is a hash of the year and the seed). */
#include "common.h"
#include "era.h"
#include "trade.h"
#include "politics.h"
#include "industry.h"
#include "sim.h"
#include "world.h"
#include "budget.h"
#include "tools.h"
#include "text.h"
#include "save.h"
#include "util.h"
#include "advice.h"     /* PL1: the Cabinet, hindsight, the verdict */
#include "net.h"

#define MSG_RING 16

/* landmark effects: authority every N months (politics), tourism mk/month = base + pop / div (BL_SPECIAL) */
typedef struct { int def; int auth_every; s32 tour_base, tour_div; } LandmarkFx;
static const LandmarkFx s_lm[] = {
    { B_CONCERT_HALL,      3, 30, 400 },
    { B_TV_TOWER,          4, 20, 800 },
    { B_SKI_JUMP,          6, 25, 800 },
    { B_PRES_SAUNA,        0,  5, 0 },
    { B_FRIENDSHIP_STATUE, 0,  0, 0 },
};
#define NLM ((int)COUNTOF(s_lm))

static struct {
    int  granted, eligible, petitioned;
    int  lm_count[NLM];
    int  sauna_uses, sauna_year; /* sauna diplomacy button */
    int  card;                   /* agreement card: 0 none, 1 waiting for the queue, 2 queued */
    int  prev_pending;           /* politics_pending_event() at the last month end */
    s32  last_card_m;            /* total_months when the last card (any source) left the desk */
    s16  dec_months[DEC_COUNT];
    u8   dec_active[DEC_COUNT];
    s32  decrees_signed;
    /* this year's counters (reset each January) */
    int  power_warn, water_warn, status_year, landmark_year, landmark_def, loan_year;
    s32  emig_year;
    u32  seen;                   /* 1 << EE_* */
    int  elections0;             /* politics_elections_held at the last January */
    /* history */
    s32  hist[EH_COUNT][ERA_HIST];
    int  hist_pos, hist_n;
    EraYear years[ERA_YEARS];
    int  nyears;
    /* newspaper */
    NewsIssue news[NEWS_KIND_COUNT];
    u32  pending;                /* 1 << NEWS_* */
    int  last_kind, issue_no, newsreel_done;
    s32  score[NT_COUNT];
    s32  peak_pop; int peak_year;
    int  status_year_granted[TS_COUNT];
} E;

static EraMsg s_msg[MSG_RING];
static int s_msg_head, s_msg_n;

static int pol_on(void) { return politics_enabled(); }
static const char *town(void) { return sim_settings()->town[0] ? sim_settings()->town : "Kaupunki"; }
static const char *pres(void) { return sim_settings()->president[0] ? sim_settings()->president : "Kekkola"; }
static int next_month_index(void) { return sim_date()->year * 12 + sim_date()->month + 1; }

/* ------------------------------------------------------------------ messages */
void era_push_msg(int type, s32 a, s32 b)
{
    int idx;
    if (s_msg_n == MSG_RING) { s_msg_head = (s_msg_head + 1) % MSG_RING; s_msg_n--; }
    idx = (s_msg_head + s_msg_n) % MSG_RING;
    s_msg[idx].type = (u8)type; s_msg[idx].a = a; s_msg[idx].b = b;
    s_msg_n++;
}

int era_poll_msg(EraMsg *m)
{
    if (!s_msg_n) return 0;
    *m = s_msg[s_msg_head];
    s_msg_head = (s_msg_head + 1) % MSG_RING;
    s_msg_n--;
    return 1;
}

int era_msg_panel(const EraMsg *m)
{
    switch (m->type) {
    case EM_OFFER: case EM_SIGNED: case EM_AUTO_SIGNED: case EM_DEFICIT: case EM_SHORTFALL: case EM_OIL_SHOCK:
    case EM_OIL_SHOCK2: case EM_OIL_COLLAPSE: case EM_LOAN: case EM_LOAN_REPAID: case EM_CLEARING_END:
        return 1;
    }
    return 0;
}

int era_msg_text(char *buf, int size, const EraMsg *m, int *severity)
{
    char a[32];
    int year = sim_date()->year, sev = SEV_INFO;
    buf[0] = 0;
    switch (m->type) {
    case EM_OFFER:
        sev = SEV_POLITICAL;
        str_printf(buf, size, "A new five-year trade agreement (%d-%d) waits for the President's signature.", (int)m->a, (int)m->b);
        break;
    case EM_SIGNED:
        sev = SEV_POLITICAL;
        str_printf(buf, size, "Trade agreement signed: %s quota. The Delegation raises a glass.", trade_level_name((int)m->a));
        break;
    case EM_AUTO_SIGNED:
        str_printf(buf, size, "The Ministry signed the trade agreement on the town's behalf: %s quota.", trade_level_name((int)m->a));
        break;
    case EM_DEFICIT:
        sev = SEV_WARN;
        text_fmt_int(a, sizeof(a), -m->a);
        str_printf(buf, size, "Clearing deficit of %s: the Ambassador asks politely where the goods are.", a);
        break;
    case EM_SHORTFALL:
        sev = SEV_WARN;
        str_printf(buf, size, "Trade review: only %d%% of the quota delivered. The Delegation is disappointed.", (int)m->a);
        break;
    case EM_OIL_SHOCK:
        sev = SEV_URGENT;
        str_printf(buf, size, "Oil crisis: the price of oil quadruples. A supplementary protocol raises the clearing quota.");
        break;
    case EM_OIL_SHOCK2:
        sev = SEV_WARN;
        str_printf(buf, size, "Second oil crisis: oil prices climb again.");
        break;
    case EM_OIL_COLLAPSE:
        str_printf(buf, size, "Oil prices collapse. Cheap fuel, but the East can now afford fewer of our goods.");
        break;
    case EM_CLEARING_END:
        sev = SEV_POLITICAL;
        str_printf(buf, size, "The clearing trade has ended. From now on the East pays in hard currency, when it pays.");
        break;
    case EM_DEVALUATION:
        str_printf(buf, size, "The markka is devalued. Western orders pick up; coffee gets dearer.");
        break;
    case EM_LOAN:
        text_fmt_money(a, sizeof(a), m->b, year);
        str_printf(buf, size, "A municipal bond of %s is sold out by noon. The repayments start next month.", a);
        break;
    case EM_LOAN_REPAID:
        str_printf(buf, size, "The municipal bond is repaid in full.");
        break;
    case EM_RIGHTS_PETITION:
        sev = SEV_POLITICAL;
        str_printf(buf, size, "Town rights await your signature, President: the council petitions for %s rights (\xa7 on Approval).", text_status_name((int)m->a));
        break;
    case EM_SAUNA_NIGHT:
        sev = SEV_POLITICAL;
        str_printf(buf, size, "A long evening in the presidential sauna with the Ambassador. Relations warm by several degrees.");
        break;
    case EM_STATUE:
        sev = SEV_POLITICAL;
        str_printf(buf, size, "The friendship statue is unveiled. The Ambassador is visibly moved.");
        break;
    case EM_LANDMARK:
        str_printf(buf, size, "%s opens. The town has something to show its visitors.", def_building((int)m->a)->name);
        break;
    case EM_POWER_NEAR:      /* PL1: before a shortage (PLAYTEST 2 item 3) */
        sev = SEV_WARN;
        str_printf(buf, size, "The power works run at %d%% of capacity. The engineer suggests a new plant before the lights do it for him.", (int)m->a);
        break;
    case EM_RIGHTS_NEAR:     /* PL1: progression visible (item 1) */
        text_fmt_int(a, sizeof(a), g_statuses[ICLAMP((int)m->a, 0, TS_COUNT - 1)].population);
        str_printf(buf, size, "%s rights are within reach: %s residents will do it. The council is measuring the town hall for a bigger flag.",
                   text_status_name((int)m->a), a);
        break;
    case EM_RIGHTS_REMIND:
        sev = SEV_POLITICAL;
        str_printf(buf, size, "The council reminds the President, respectfully, that the %s rights decree has waited %d months (\xa7 on Approval).",
                   text_status_name((int)m->a), (int)m->b);
        break;
    default:
        return 0;
    }
    if (severity) *severity = sev;
    return (int)strlen(buf);
}

/* ------------------------------------------------------------------ status */
int era_status_eligible(void) { return E.eligible; }

int era_status_target(int st)
{
    int held = sim_stats()->town_status;
    if (st > E.eligible) E.eligible = st;
    if (held > E.granted) E.granted = held;
    if (!pol_on()) return st;
    return E.granted;
}

void era_town_rights_signed(void)
{
    if (E.eligible > E.granted) E.granted = E.eligible;
    sim_grant_status(E.granted);
    politics_enable_hook_decree(DEC_TOWN_RIGHTS, 0);
}

void era_status_granted(int ts)
{
    if (ts < 0 || ts >= TS_COUNT) return;
    E.status_year = ts;
    E.status_year_granted[ts] = sim_date()->year;
    if (ts > E.granted) E.granted = ts;
    politics_status_ceremony(ts);
    era_news_build_ceremony(&E.news[NEWS_CEREMONY], ts);
    E.pending |= 1u << NEWS_CEREMONY;
}

int era_unlock_override(int def, int year, int status)
{
    UNUSED(year); UNUSED(status);
    if (def == B_SKI_JUMP) return politics_event_count(EV_OLYMPICS) > 0;
    if (def == B_FRIENDSHIP_STATUE) return politics_event_count(EV_STATUE) > 0;
    return 0;
}

/* ------------------------------------------------------------------ events */
int era_event_year(int ee, int *month)
{
    static const s16 ym[EE_COUNT][2] = { { 0, 0 }, { 1957, 8 }, { 1963, 0 }, { 1967, 9 }, { 1973, 10 }, { 1979, 0 },
                                          { 1986, 2 }, { 1990, 0 }, { -1, 0 } };
    if (ee <= EE_NONE || ee >= EE_COUNT) return 0;
    if (month) *month = ym[ee][1];
    return ym[ee][0];
}
int era_event_seen(int ee) { return ee > 0 && ee < 32 && (E.seen & (1u << ee)) != 0; }

static void fire_events(int nm)
{
    int ee, mo, y;
    static const u8 msg[EE_COUNT] = { 0, EM_DEVALUATION, 0 /* text.cpp's 1963 ticker line */, EM_DEVALUATION, 0 /* trade sends it */, EM_OIL_SHOCK2,
                                      EM_OIL_COLLAPSE, 0 /* trade */, 0 };
    for (ee = EE_DEVAL_1957; ee < EE_AGREEMENT; ee++) {
        y = era_event_year(ee, &mo);
        if (y * 12 + mo != nm) continue;
        E.seen |= 1u << ee;
        if (msg[ee]) era_push_msg(msg[ee], ee, 0);
        log_printf("era: event %d (%d-%02d)", ee, y, mo + 1);
    }
    {
        int from, to;
        if (nm % 12 == 0 && trade_period_of(nm / 12, &from, &to) && from == nm / 12 && from > TRADE_FIRST_PERIOD) E.seen |= 1u << EE_AGREEMENT;
    }
}

/* the most recent era event within the 12 months before the month index nm (inclusive of nm) */
static int recent_event(int nm)
{
    int ee, mo, y, best = EE_NONE, bm = -1;
    for (ee = EE_DEVAL_1957; ee < EE_AGREEMENT; ee++) {
        y = era_event_year(ee, &mo);
        if (y * 12 + mo <= nm && y * 12 + mo > nm - 12 && y * 12 + mo > bm && era_event_seen(ee)) { best = ee; bm = y * 12 + mo; }
    }
    return best;
}

/* ------------------------------------------------------------------ landmarks */
int era_is_landmark(int def)
{
    int i;
    for (i = 0; i < NLM; i++) if (s_lm[i].def == def) return 1;
    return 0;
}
int era_landmark_count(void)
{
    int i, n = 0;
    for (i = 0; i < NLM; i++) n += E.lm_count[i];
    return n;
}

static void landmarks_month(void)
{
    int i, k, cnt[NLM];
    s32 tour = 0, pop = sim_stats()->population;
    u32 tm = sim_date()->total_months;
    memset(cnt, 0, sizeof(cnt));
    for (k = 1; k < g_world.lot_hi; k++) {
        const Lot *l = &g_world.lots[k];
        if (l->def == B_NONE || l->status != LOT_ACTIVE) continue;
        for (i = 0; i < NLM; i++) if (l->def == s_lm[i].def) cnt[i]++;
    }
    for (i = 0; i < NLM; i++) {
        if (cnt[i] > E.lm_count[i]) {
            E.landmark_year++; E.landmark_def = s_lm[i].def;
            era_push_msg(s_lm[i].def == B_FRIENDSHIP_STATUE ? EM_STATUE : EM_LANDMARK, s_lm[i].def, 0);
            if (s_lm[i].def == B_FRIENDSHIP_STATUE && pol_on()) politics_meter_add(METER_EAST, 4);
        }
        E.lm_count[i] = cnt[i];
        if (!cnt[i]) continue;
        if (s_lm[i].auth_every && pol_on() && (tm % (u32)s_lm[i].auth_every) == 0) politics_authority_add(1);
        tour += s_lm[i].tour_base + (s_lm[i].tour_div ? pop / s_lm[i].tour_div : 0);
    }
    if (tour) budget_spend(-(Money)(tour * budget_upkeep_index() / 100), BL_SPECIAL);
    /* the friendship statue: a little warmth every 6 months */
    if (pol_on() && E.lm_count[4] && (tm % 6) == 0) politics_meter_add(METER_EAST, 1);
}

/* sauna diplomacy (one button): +8, +4, +2, +2... Eastern relations, once a year */
int era_sauna_state(void)
{
    if (!E.lm_count[3]) return 1;
    if (!pol_on()) return 2;
    if (E.sauna_year == sim_date()->year) return 3;
    return 0;
}
int era_sauna_gain(void) { return IMAX(2, 8 / (1 + E.sauna_uses)); }
int era_sauna_invite(void)
{
    if (era_sauna_state() != 0) return 0;
    politics_meter_add(METER_EAST, era_sauna_gain());
    E.sauna_uses++;
    E.sauna_year = sim_date()->year;
    era_push_msg(EM_SAUNA_NIGHT, E.sauna_uses, 0);
    return 1;
}

/* ------------------------------------------------------------------ the agreement card */
/* one card per game year in total (docs/SCOPE_REVIEW.md): the agreement card waits until no card is up and the last
   one left the desk POL_EVENT_MIN months ago (P1: politics_card_slot_free, the one shared gate) */
int era_card_slot_free(void)
{
    return politics_agreement_slot_free();     /* P1: one shared gate (politics.cpp); E.last_card_m is bookkeeping only */
}
void era_queue_card(void) { E.card = era_card_slot_free() && politics_queue_event(EV_AGREEMENT) ? 2 : 1; }
int  era_card_pending(void) { return E.card == 2 && pol_on(); }   /* P1: the card is ON the desk (1 = waiting for the shared slot: the Ministry signs Standard after TRADE_OFFER_MONTHS) */
void era_card_answered(int ev, int choice)
{
    static const int lv[3] = { TL_STANDARD, TL_MODEST, TL_AMBITIOUS };
    if (ev != EV_AGREEMENT) return;
    E.card = 0;
    trade_sign(lv[ICLAMP(choice, 0, 2)]);
}

/* ------------------------------------------------------------------ history */
int era_history(int series, s32 *out, int max)
{
    int i, n, start;
    if (series < 0 || series >= EH_COUNT) return 0;
    n = IMIN(max, E.hist_n);
    start = (E.hist_pos - n + ERA_HIST) % ERA_HIST;
    for (i = 0; i < n; i++) out[i] = E.hist[series][(start + i) % ERA_HIST];
    return n;
}
int era_years(void) { return E.nyears; }
const EraYear *era_year_row(int i) { return (i >= 0 && i < E.nyears) ? &E.years[i] : NULL; }

static void hist_push(void)
{
    const TradeState *t = trade_state();
    const CityStats *st = sim_stats();
    s32 v[EH_COUNT];
    int k;
    v[EH_APPROVAL] = politics_popularity(); v[EH_EAST] = politics_meter(METER_EAST); v[EH_WEST] = politics_meter(METER_WEST);
    v[EH_UNEMPLOYED] = st->unemployed; v[EH_BALANCE] = t->balance; v[EH_WEST_INDEX] = t->west_index;
    v[EH_OIL_INDEX] = t->oil_index; v[EH_HOMELESS] = st->homeless; v[EH_LAND_VALUE] = st->avg_land_value;
    for (k = 0; k < EH_COUNT; k++) E.hist[k][E.hist_pos] = v[k];
    E.hist_pos = (E.hist_pos + 1) % ERA_HIST;
    if (E.hist_n < ERA_HIST) E.hist_n++;
}

static void take_year(EraYear *r, int year)
{
    const CityStats *st = sim_stats();
    const TradeState *t = trade_state();
    int z, k;
    memset(r, 0, sizeof(*r));
    r->year = (s16)year;
    r->pop = st->population; r->money = g_budget.money; r->jobs = st->jobs; r->unemployed = st->unemployed;
    r->emig_year = E.emig_year; r->homeless = st->homeless; r->industry_jobs = st->jobs_zone[ZONE_INDUSTRY];
    r->east = t->exports_east; r->west = t->exports_west; r->balance = t->balance;
    for (z = 0; z < ZONE_COUNT; z++) r->lots_t3 += st->lots_tier[z][3];
    for (k = 1; k <= TIER_MAX; k++) r->apartments += st->lots_tier[ZONE_APARTMENTS][k];
    r->landmarks = era_landmark_count();
    r->loan_total = g_budget.loan + t->bond;
    r->status = (s16)st->town_status; r->popularity = (s16)politics_popularity();
    r->east_meter = (s16)politics_meter(METER_EAST); r->west_meter = (s16)politics_meter(METER_WEST);
    r->power_warn = (s16)E.power_warn; r->water_warn = (s16)E.water_warn;
    r->elections = (s16)politics_elections_held(); r->decrees_signed = (s16)E.decrees_signed;
    r->oil_index = (s16)t->oil_index; r->fulfil = (s16)t->fulfil_pct;
}

static void push_year(int year)
{
    if (E.nyears >= ERA_YEARS) { memmove(&E.years[0], &E.years[1], (ERA_YEARS - 1) * sizeof(EraYear)); E.nyears--; }
    take_year(&E.years[E.nyears++], year);
}

/* ------------------------------------------------------------------ lifecycle */
void era_reset(void)
{
    memset(&E, 0, sizeof(E));
    s_msg_head = s_msg_n = 0;
    E.prev_pending = -1;
    E.last_card_m = -POL_EVENT_MIN;
    E.last_kind = NEWS_NONE;
    trade_reset();
    advice_reset();                          /* PL1 */
    push_year(sim_date()->year);
}

void era_month(void)
{
    const CityStats *st = sim_stats();
    int nm = next_month_index(), d;
    trade_month();
    landmarks_month();
    /* decrees in force (newsreel: the decrees that defined the era) */
    for (d = 0; d < DEC_COUNT; d++) {
        int a = politics_decree_active(d);
        if (a && E.dec_months[d] < 32000) E.dec_months[d]++;
        if (a && !E.dec_active[d]) E.decrees_signed++;
        E.dec_active[d] = (u8)a;
    }
    /* town rights by decree: offer the hidden decree while a step is reached but not granted */
    if (pol_on()) {
        int want = E.eligible > E.granted;
        politics_enable_hook_decree(DEC_TOWN_RIGHTS, want);
        if (want && E.petitioned < E.eligible) { E.petitioned = E.eligible; era_push_msg(EM_RIGHTS_PETITION, E.eligible, 0); }
    }
    if (st->power_short_lots > 0) E.power_warn++;
    if (st->water_short_lots > 0) E.water_warn++;
    E.emig_year += st->emigrated_month;
    if (st->population > E.peak_pop) { E.peak_pop = st->population; E.peak_year = sim_date()->year; }
    {   /* card bookkeeping (one card a year across all sources) */
        int pend = politics_pending_event();
        if (E.prev_pending >= 0 && pend != E.prev_pending) E.last_card_m = (s32)sim_date()->total_months;
        E.prev_pending = pend;
    }
    if (E.card == 1 && pol_on() && trade_agreement()->offer) era_queue_card();   /* retry */
    if (!trade_agreement()->offer) E.card = 0;
    fire_events(nm);
    hist_push();
    {   /* PL1: hindsight tallies, ticker warnings before trouble, a special edition for an urgent problem */
        int v = 0;
        advice_month();
        if (advice_power_near_due(&v)) era_push_msg(EM_POWER_NEAR, v, 0);
        if (advice_rights_near_due(&v)) era_push_msg(EM_RIGHTS_NEAR, v, st->population);
        if (advice_rights_waiting() && advice_rights_wait_months() >= 6 && advice_rights_wait_months() % 6 == 0)
            era_push_msg(EM_RIGHTS_REMIND, era_status_eligible(), advice_rights_wait_months());
        if (nm % 12 != 0 && !(E.pending & ~(1u << NEWS_SPECIAL))) {
            Advice main;
            if (advice_special_due(&main)) { era_news_build_special(&E.news[NEWS_SPECIAL], main.kind); E.pending |= 1u << NEWS_SPECIAL; }
        }
    }
    if (nm % 12 == 0) {                       /* New Year: the yearly row and the January paper */
        push_year(nm / 12);
        era_news_build_year(&E.news[NEWS_YEAR]);
        E.pending |= 1u << NEWS_YEAR;
        advice_year_end();                    /* PL1: the year's tallies were used by the paper's hindsight */
        if (nm == TRADE_END_YEAR * 12 && !E.newsreel_done) {
            E.newsreel_done = 1;
            era_news_build_newsreel(&E.news[NEWS_NEWSREEL]);
            E.pending |= 1u << NEWS_NEWSREEL;
        }
        E.power_warn = E.water_warn = 0; E.status_year = 0; E.landmark_year = 0; E.loan_year = 0; E.emig_year = 0;
        E.elections0 = politics_elections_held();
    }
}

/* ------------------------------------------------------------------ newspaper */
const NewsIssue *era_news(void)
{
    static const int order[4] = { NEWS_CEREMONY, NEWS_SPECIAL, NEWS_YEAR, NEWS_NEWSREEL };
    int i;
    for (i = 0; i < 4; i++) if (E.pending & (1u << order[i])) return &E.news[order[i]];
    return E.last_kind ? &E.news[E.last_kind] : NULL;
}
int era_news_pending(void)
{
    const NewsIssue *n;
    if (!E.pending) return 0;
    n = era_news();
    return n ? n->kind : 0;
}
void era_news_seen(void)
{
    const NewsIssue *n = era_news();
    if (!n || !E.pending) return;
    E.last_kind = n->kind;
    E.pending &= ~(1u << n->kind);
}
const NewsIssue *era_news_kind(int kind) { return (kind > NEWS_NONE && kind < NEWS_KIND_COUNT && E.news[kind].kind) ? &E.news[kind] : NULL; }
void era_news_seen_kind(int kind)
{
    if (kind <= NEWS_NONE || kind >= NEWS_KIND_COUNT) return;
    E.last_kind = kind;
    E.pending &= ~(1u << kind);
}
void era_news_request_newsreel(void)
{
    era_news_build_newsreel(&E.news[NEWS_NEWSREEL]);
    E.pending |= 1u << NEWS_NEWSREEL;
}
int era_news_score(int topic) { return (topic >= 0 && topic < NT_COUNT) ? E.score[topic] : 0; }

static void issue_init(NewsIssue *n, int kind)
{
    memset(n, 0, sizeof(*n));
    n->kind = kind;
    n->year = sim_date()->year; n->month = sim_date()->month;
    if (next_month_index() % 12 == 0 && kind == NEWS_YEAR) { n->year = next_month_index() / 12; n->month = 0; }
    n->issue_no = ++E.issue_no;
}

static void money_str(char *buf, int size, Money v, int year) { text_fmt_money(buf, size, v, year); }

static void put(NewsIssue *n, int slot, int topic, const char *head, const char *deck, const char *body)
{
    char *p;
    n->topic[slot] = topic;
    str_copy(n->head[slot], head, sizeof(n->head[slot]));
    for (p = n->head[slot]; *p; p++) {         /* headlines in capitals (Latin-1 lower case too) */
        u8 c = (u8)*p;
        if ((c >= 'a' && c <= 'z') || (c >= 0xe0 && c <= 0xfe && c != 0xf7)) *p = (char)(c - 32);
    }
    str_copy(n->deck[slot], deck, sizeof(n->deck[slot]));
    str_copy(n->body[slot], body, sizeof(n->body[slot]));
}

static void era_story(NewsIssue *n, int slot, int ee, const EraYear *now)
{
    char h[64], d[96], b[240], m1[32];
    switch (ee) {
    case EE_NEW_MARKKA:
        money_str(m1, sizeof(m1), now->money, 1963);
        str_printf(h, sizeof(h), "NEW MARKKA: PRICES DIVIDED BY A HUNDRED");
        str_printf(d, sizeof(d), "One new markka buys what a hundred old ones did");
        str_printf(b, sizeof(b), "The new notes are in the tills. The town treasurer, who kept the books in new markka all along "
                                 "(%s), says he saw it coming. Shopkeepers report no change in the price of coffee, only in the "
                                 "number of zeros.", m1);
        break;
    case EE_DEVAL_1957: case EE_DEVAL_1967:
        str_printf(h, sizeof(h), "MARKKA DEVALUED");
        str_printf(d, sizeof(d), "Exports cheaper abroad, coffee dearer at home");
        str_printf(b, sizeof(b), "The Bank of Finland has lowered the markka. Western buyers are already asking for our timber and "
                                 "machines; the housewives of %s are asking about the price of coffee.", town());
        break;
    case EE_OIL_SHOCK:
        str_printf(h, sizeof(h), "OIL PRICE QUADRUPLES");
        str_printf(d, sizeof(d), "The East now asks four times the goods for each barrel");
        str_printf(b, sizeof(b), "The crisis in the Middle East reaches the town's boiler rooms. Oil is paid in machines, houses and "
                                 "shoes under the clearing agreement; a supplementary protocol raises the quota by a third. "
                                 "Factories that can deliver will not lack orders.");
        break;
    case EE_OIL_SHOCK2:
        str_printf(h, sizeof(h), "SECOND OIL CRISIS");
        str_printf(d, sizeof(d), "Fuel bills climb again; the boiler men are not surprised");
        str_printf(b, sizeof(b), "Oil prices rise once more. Towns heated by oil count their markka; towns with coal, water power "
                                 "and full order books in the East count their blessings.");
        break;
    case EE_OIL_COLLAPSE:
        str_printf(h, sizeof(h), "OIL PRICE COLLAPSES");
        str_printf(d, sizeof(d), "Cheap fuel, thinner order books in the East");
        str_printf(b, sizeof(b), "With oil cheap, the East has fewer barrels to pay with. The clearing quota shrinks and the "
                                 "factories that lived on Eastern orders look West.");
        break;
    case EE_CLEARING_END:
        str_printf(h, sizeof(h), "CLEARING TRADE ENDS");
        str_printf(d, sizeof(d), "The goods lists are history");
        str_printf(b, sizeof(b), "After thirty-five years of five-year agreements the clearing account is closed. From now on the "
                                 "East pays in hard currency, when it pays. %s must find new customers.", town());
        break;
    default:
        str_printf(h, sizeof(h), "A NEW ERA BEGINS"); str_printf(d, sizeof(d), "%s", text_era_name(def_era(n->year)));
        str_printf(b, sizeof(b), "The times are changing, and so is %s.", town());
        break;
    }
    put(n, slot, NT_ERA, h, d, b);
}

static void topic_story(NewsIssue *n, int slot, int topic, const EraYear *prev, const EraYear *now)
{
    char h[64], d[96], b[240], a1[32], a2[32];
    int y = n->year, pct;
    const TradeState *t = trade_state();
    s32 dp = now->pop - prev->pop;
    pct = prev->pop > 0 ? (int)((s64)dp * 100 / prev->pop) : 100;
    h[0] = d[0] = b[0] = 0;
    switch (topic) {
    case NT_STATUS:
        str_printf(h, sizeof(h), "%s IS NOW A %s", town(), text_status_name(now->status));
        str_printf(d, sizeof(d), "Town rights by decree of President %s", pres());
        str_printf(b, sizeof(b), "The decree was read aloud on the town hall steps. The brass band played twice, the second time "
                                 "on purpose. New buildings may now be planned.");
        break;
    case NT_GROWTH:
        text_fmt_int(a1, sizeof(a1), now->pop); text_fmt_int(a2, sizeof(a2), dp);
        str_printf(h, sizeof(h), "TOWN GROWS TO %s", a1);
        str_printf(d, sizeof(d), "%s new residents in a year (+%d%%)", a2, pct);
        str_printf(b, sizeof(b), "Moving vans on every street. The housing board says the newcomers come from the countryside, "
                                 "and the countryside agrees.");
        break;
    case NT_DECLINE:
        text_fmt_int(a1, sizeof(a1), now->pop); text_fmt_int(a2, sizeof(a2), -dp);
        str_printf(h, sizeof(h), "POPULATION FALLS TO %s", a1);
        str_printf(d, sizeof(d), "%s fewer residents than a year ago", a2);
        str_printf(b, sizeof(b), "Empty windows in the new blocks. The council blames the weather, the weather blames the council.");
        break;
    case NT_EMIGRATION:
        text_fmt_int(a1, sizeof(a1), now->emig_year);
        str_printf(h, sizeof(h), "%s LEAVE FOR SWEDEN", a1);
        str_printf(d, sizeof(d), "Suitcases on the quay, jobs in Gothenburg");
        str_printf(b, sizeof(b), "The Sweden boat leaves full again. Those who stay ask for work, homes and a reason to stay. "
                                 "Factories and flats would help.");
        break;
    case NT_UNEMPLOYMENT:
        text_fmt_int(a1, sizeof(a1), now->unemployed);
        str_printf(h, sizeof(h), "%s WITHOUT WORK", a1);
        str_printf(d, sizeof(d), "The labour exchange queue reaches the corner");
        str_printf(b, sizeof(b), "Industry is not hiring fast enough. Zone for factories, the employment office says, or watch "
                                 "the young take the boat.");
        break;
    case NT_MONEY_UP:
        money_str(a1, sizeof(a1), now->money, y);
        str_printf(h, sizeof(h), "TREASURY FULL: %s", a1);
        str_printf(d, sizeof(d), "The council debates something grand");
        str_printf(b, sizeof(b), "The town has more money than plans. The President's office is said to keep a list: a concert "
                                 "hall, a television tower, perhaps a ski jump.");
        break;
    case NT_MONEY_DOWN:
        money_str(a1, sizeof(a1), now->money, y);
        str_printf(h, sizeof(h), "TREASURY EMPTY");
        str_printf(d, sizeof(d), "The town ends the year at %s", a1);
        str_printf(b, sizeof(b), "The finance committee meets daily. Taxes, savings or a loan: the ministries have offered all three.");
        break;
    case NT_LOAN:
        money_str(a1, sizeof(a1), now->loan_total, y);
        str_printf(h, sizeof(h), "TOWN BORROWS");
        str_printf(d, sizeof(d), "Loans outstanding: %s", a1);
        str_printf(b, sizeof(b), "The council signs where the bank official points. The repayments start next month.");
        break;
    case NT_ELECTION: {
        const PolElection *e = politics_last_election();
        int el = e ? e->electors : 151;
        str_printf(h, sizeof(h), "PRESIDENT %s RE-ELECTED", pres());
        str_printf(d, sizeof(d), "%d electors of 300 (%s)", el, politics_verdict_text(e ? e->verdict : 0));
        str_printf(b, sizeof(b), "The result surprised nobody, least of all the President. The margin is what the cafes discuss.");
        break; }
    case NT_LANDMARK:
        str_printf(h, sizeof(h), "%s OPENS", def_building(E.landmark_def)->name);
        str_printf(d, sizeof(d), "Visitors expected from three provinces");
        str_printf(b, sizeof(b), "The ribbon was cut by the President, who said a few words and then a few more. Land values "
                                 "nearby rose before the speech ended.");
        break;
    case NT_CLEARING_DEFICIT:
        text_fmt_int(a1, sizeof(a1), -now->balance);
        str_printf(h, sizeof(h), "CLEARING DEFICIT WORRIES MINISTRY");
        str_printf(d, sizeof(d), "The town owes the East %s in goods", a1);
        str_printf(b, sizeof(b), "Oil comes in faster than machines go out. The Ambassador asks politely, then less politely. "
                                 "More factories, or fewer imports.");
        break;
    case NT_CLEARING_SURPLUS:
        text_fmt_int(a1, sizeof(a1), t->lost_total);
        str_printf(h, sizeof(h), "UNSPENT CREDIT IN THE SAUNA");
        str_printf(d, sizeof(d), "Clearing credit lost so far: %s", a1);
        str_printf(b, sizeof(b), "The factories ship more East than the town's oil bill needs. Credit that buys only Eastern goods "
                                 "piles up unspent; the presidential sauna, the joke goes, is full of it.");
        break;
    case NT_WEST_UP: case NT_WEST_DOWN:
        str_printf(h, sizeof(h), topic == NT_WEST_UP ? "WESTERN ORDERS BOOM" : "WESTERN ORDERS SLUMP");
        str_printf(d, sizeof(d), "Western price index %d", (int)t->west_index);
        str_printf(b, sizeof(b), topic == NT_WEST_UP ? "Sawmills and works ship West at good prices; hard currency fills the bank." :
                                 "The Western buyers have gone quiet. The order books look East again.");
        break;
    case NT_POWER:
        str_printf(h, sizeof(h), "BLACKOUTS AGAIN");
        str_printf(d, sizeof(d), "Power short in %d months of the year", (int)now->power_warn);
        str_printf(b, sizeof(b), "Candles sold out at the co-op. The power company recommends a new plant and an early night.");
        break;
    case NT_WATER:
        str_printf(h, sizeof(h), "TAPS RUN DRY");
        str_printf(d, sizeof(d), "Water short in %d months of the year", (int)now->water_warn);
        str_printf(b, sizeof(b), "Residents at the end of the pipe fetch water in buckets. The waterworks asks for a pump.");
        break;
    case NT_INDUSTRY:
        text_fmt_int(a1, sizeof(a1), now->industry_jobs - prev->industry_jobs);
        str_printf(h, sizeof(h), "NEW FACTORIES: %s JOBS", a1);
        str_printf(d, sizeof(d), "Chimneys rise by the railway");
        str_printf(b, sizeof(b), "The order books are full, East and West. The foremen want workers; the workers want flats.");
        break;
    case NT_HOUSING:
        text_fmt_int(a1, sizeof(a1), now->apartments);
        str_printf(h, sizeof(h), "CONCRETE SUBURB RISES");
        str_printf(d, sizeof(d), "%s apartment blocks stand in %s", a1, town());
        str_printf(b, sizeof(b), "Bathrooms, central heating and a view of the next block. The new residents call it modern; "
                                 "their parents call it high.");
        break;
    case NT_AGREEMENT: {
        const TradeAgreement *ag = trade_agreement();
        int g, best = TG_MACHINERY;
        char gl[64];
        for (g = 0; g < TG_COUNT; g++) if (t->delivered[g] > t->delivered[best]) best = g;
        trade_goods_line(best, IMAX(1, t->delivered[best]), gl, sizeof(gl));
        str_printf(h, sizeof(h), "NEW TRADE AGREEMENT %d-%d", ag->from_year, ag->to_year);
        str_printf(d, sizeof(d), "The goods list: %s a year", gl);
        str_printf(b, sizeof(b), "The Delegation has brought the new five-year frame. Modest, standard or ambitious: the President "
                                 "decides, and the factories deliver.");
        break; }
    case NT_DECREE:
        str_printf(h, sizeof(h), "THE PEN IS BUSY");
        str_printf(d, sizeof(d), "%d decrees signed so far", (int)now->decrees_signed);
        str_printf(b, sizeof(b), "The President's office reports a shortage of ink. The decrees are, as ever, for the good of the town.");
        break;
    default:
        str_printf(h, sizeof(h), "A QUIET YEAR IN %s", town());
        str_printf(d, sizeof(d), "Nothing to report, and we report it");
        str_printf(b, sizeof(b), "The ferries ran, the shops opened, the snow came and went. The editor apologises for the lack of news.");
        topic = NT_QUIET;
        break;
    }
    put(n, slot, topic, h, d, b);
}

static void filler(NewsIssue *n)
{
    static const char *const fh[] = { "TELEPHONE", "WEATHER", "SAUNA", "LETTERS", "COFFEE", "MOTORING", "QUEUES", "TELEVISION",
        "SPORT", "COUNCIL", "CULTURE", "HOUSING", "FROM SWEDEN", "MARKET", "SCHOOLS", "HONOURS", "TRAFFIC", "LIBRARY",
        "STATISTICS", "THE ARMY OF PLANNERS" };
    static const char *const ft[] = {
        "Mr Virtanen of Koivukuja has been told his telephone will be connected in 1971. \"I have waited nine years,\" he said. \"I can wait two more.\"",
        "Snow is expected in winter. The Meteorological Institute stands by its forecast.",
        "A delegation from the twin town praised the public sauna. The minutes record the temperature as \"friendly\".",
        "A reader asks why the bus runs only when it is not needed. The bus company replies that it runs to a timetable.",
        "Coffee consumption per head rose again. The Medical Board had no comment; the Medical Board was drinking coffee.",
        "The town's newest car has been delivered from the East. Its owner reports that it starts, mostly.",
        "Forty residents queued at the co-op for bananas. Thirty-eight got some. The rest got a lecture on patience.",
        "Evening television now includes forty minutes on forestry. Viewers report feeling well informed about spruce.",
        "The ski team finished fourth. \"Fourth is the new first,\" said the coach, who is also fourth in the club's chess ladder.",
        "The council has formed a committee to study committees. It will report next year, or the year after.",
        "A cultural delegation from the East visited the library and found it in good order. The library found them punctual.",
        "A family of six moved into a two-room flat in the new suburb and declared it spacious. The neighbours agree, through the wall.",
        "A letter from Gothenburg: \"Work is good, the food is strange, we miss the lakes. Send rye bread.\"",
        "Potatoes steady, herring firm. The fishmonger's opinion of the weather is unprintable.",
        "Pupils at the new school asked for longer breaks. The headmaster asked for shorter questions.",
        "The President skied thirty kilometres on his birthday, according to legend and the President.",
        "A traffic light was installed on the main street. Drivers stopped to admire it, which helped.",
        "The library reports that its most borrowed book is the telephone directory, though few have a telephone.",
        "The Statistics Office reports that statistics are up.",
        "The planning office has drawn a new suburb, a ring road and a fountain. The fountain is the most popular." };
    u32 h = hash_u32((u32)n->year * 2654435761u ^ sim_settings()->seed ^ (u32)n->kind);
    int k = (int)(h % (u32)COUNTOF(fh));
    str_copy(n->filler_head, fh[k], sizeof(n->filler_head));
    str_copy(n->filler, ft[k], sizeof(n->filler));
}

/* PL1: "From the Cabinet": the 1-3 most important problems as the Cabinet's deferential lines (advice.h) */
static void cabinet(NewsIssue *n)
{
    Advice v[NEWS_CAB];
    int k, na = advice_collect(v, NEWS_CAB);
    n->ncab = 0;
    for (k = 0; k < na; k++) {
        str_copy(n->cab[k], v[k].text, sizeof(n->cab[k]));
        n->cab_kind[k] = v[k].kind; n->cab_go[k] = v[k].go; n->cab_x[k] = v[k].x; n->cab_y[k] = v[k].y;
        n->ncab++;
    }
    if (!n->ncab) {
        str_copy(n->cab[0], "The Cabinet has nothing to report this year, and reports it respectfully. The President's "
                            "wisdom, it notes, speaks for itself.", sizeof(n->cab[0]));
        n->cab_go[0] = AGO_NONE; n->cab_x[0] = n->cab_y[0] = -1;
        n->ncab = 1;
    }
}

void era_news_build_year(NewsIssue *n)
{
    const EraYear *now, *prev;
    int nm = next_month_index(), k, slot = 0, ee, t, used[NT_COUNT];
    s32 pct, sc[NT_COUNT];
    issue_init(n, NEWS_YEAR);
    if (E.nyears <= 0) push_year(n->year);
    now = &E.years[E.nyears - 1];
    prev = E.nyears >= 2 ? &E.years[E.nyears - 2] : now;
    memset(sc, 0, sizeof(sc)); memset(used, 0, sizeof(used));
    pct = prev->pop > 0 ? (s32)((s64)(now->pop - prev->pop) * 100 / prev->pop) : (now->pop > 0 ? 100 : 0);
    ee = recent_event(nm);
    if (ee) sc[NT_ERA] = 1000;
    if (E.status_year || now->status > prev->status) sc[NT_STATUS] = 900;
    if (now->elections > prev->elections) sc[NT_ELECTION] = 800;
    if (now->landmarks > prev->landmarks) sc[NT_LANDMARK] = 600;
    if ((E.seen & (1u << EE_AGREEMENT)) && trade_agreement()->offer) sc[NT_AGREEMENT] = 500;
    if (pct >= 5) sc[NT_GROWTH] = 100 + pct * 10;
    if (pct <= -2 && prev->pop > 300) sc[NT_DECLINE] = 300 - pct * 20;
    if (now->pop > 0 && now->emig_year * 100 >= now->pop) sc[NT_EMIGRATION] = 200 + now->emig_year * 1000 / now->pop;
    if (now->jobs > 0 && now->unemployed * 25 > now->jobs) sc[NT_UNEMPLOYMENT] = 200 + now->unemployed * 1000 / IMAX(1, now->jobs);
    if (now->money < 0) sc[NT_MONEY_DOWN] = 400;
    else if (now->money - prev->money > 10000 && now->money > 30000) sc[NT_MONEY_UP] = 150 + (now->money - prev->money) / 1000;
    if (now->loan_total > prev->loan_total + 5000) sc[NT_LOAN] = 350;
    if (trade_state()->deficit_months >= 3 && !trade_state()->closed) sc[NT_CLEARING_DEFICIT] = prev->balance < 0 ? 140 : 450;   /* news once, then a footnote */
    if (trade_state()->lost_month > 0) sc[NT_CLEARING_SURPLUS] = 120;
    if (now->west - prev->west > prev->west / 4 && prev->west > 100) sc[NT_WEST_UP] = 200;
    if (trade_state()->west_index <= 80) sc[NT_WEST_DOWN] = 220;
    if (now->power_warn >= 2) sc[NT_POWER] = 300 + now->power_warn * 10;
    if (now->water_warn >= 2) sc[NT_WATER] = 280 + now->water_warn * 10;
    if (prev->industry_jobs > 100 && (now->industry_jobs - prev->industry_jobs) * 100 >= prev->industry_jobs * 15) sc[NT_INDUSTRY] = 180;
    if (now->apartments - prev->apartments >= 5) sc[NT_HOUSING] = 150 + (now->apartments - prev->apartments) * 5;
    if (now->decrees_signed - prev->decrees_signed >= 2) sc[NT_DECREE] = 160;
    sc[NT_QUIET] = 10;
    memcpy(E.score, sc, sizeof(sc));
    while (slot < NEWS_HEADS) {
        int best = -1;
        for (t = 1; t < NT_COUNT; t++) if (!used[t] && sc[t] > 0 && (best < 0 || sc[t] > sc[best])) best = t;
        if (best < 0) break;
        used[best] = 1;
        if (best == NT_QUIET && slot > 0) break;
        if (best == NT_ERA) era_story(n, slot, ee, now);
        else topic_story(n, slot, best, prev, now);
        slot++;
    }
    for (k = slot; k < NEWS_HEADS; k++) {        /* fewer stories: the town's numbers */
        char h[64], d[96], b[240], a1[32];
        text_fmt_int(a1, sizeof(a1), now->pop);
        str_printf(h, sizeof(h), k == 1 ? "THE TOWN IN NUMBERS" : "FROM THE COUNCIL");
        str_printf(d, sizeof(d), "%s residents, %s", a1, text_status_name(now->status));
        str_printf(b, sizeof(b), "The council met %d times and agreed %d times. Minutes are available at the town hall on weekdays.",
                   12 + (int)(hash_u32((u32)n->year) % 9), 3 + (int)(hash_u32((u32)n->year * 7) % 5));
        put(n, k, NT_QUIET, h, d, b);
    }
    filler(n);
    cabinet(n);                                 /* PL1 */
    advice_hindsight(n->hind_head, sizeof(n->hind_head), n->hind, sizeof(n->hind));
}

/* PL1: special edition (urgent: in the dark, dry taps, an empty treasury); the Cabinet's column below */
void era_news_build_special(NewsIssue *n, int adv_kind)
{
    char h[64], d[96], b[240], a1[32];
    const CityStats *st = sim_stats();
    const NetStats *pw = net_stats(NET_POWER);
    int pct = pw->supply > 0 ? (int)((s64)pw->demand * 100 / pw->supply) : 0;
    issue_init(n, NEWS_SPECIAL);
    switch (adv_kind) {
    case ADV_WATER_SHORT:
        text_fmt_int(a1, sizeof(a1), st->water_short_lots);
        str_printf(h, sizeof(h), "TAPS RUN DRY IN %s", town());
        str_printf(d, sizeof(d), "%s buildings fetch water in buckets", a1);
        str_printf(b, sizeof(b), "The fire brigade lent its buckets and the dairy its churns. The waterworks engineer says the pumps "
                                 "give everything they have, and then some. The Cabinet's view is printed below.");
        put(n, 0, NT_WATER, h, d, b);
        break;
    case ADV_MONEY_DEBT:
        money_str(a1, sizeof(a1), g_budget.money < 0 ? -g_budget.money : g_budget.loan, n->year);
        str_printf(h, sizeof(h), "TREASURY EMPTY");
        str_printf(d, sizeof(d), g_budget.money < 0 ? "The town is %s in the red" : "An emergency loan of %s", a1);
        str_printf(b, sizeof(b), "The town treasurer was seen counting the petty cash twice. The bank manager, reached at his summer "
                                 "cottage, said he is in no hurry, yet. The Cabinet's view is printed below.");
        put(n, 0, NT_MONEY_DOWN, h, d, b);
        break;
    default:
        text_fmt_int(a1, sizeof(a1), st->power_short_lots);
        str_printf(h, sizeof(h), "LIGHTS OUT IN %s", town());
        str_printf(d, sizeof(d), "%s buildings in the dark; the power works at %d%%", a1, pct);
        str_printf(b, sizeof(b), "Candles sold out at the co-op by noon. The power works' engineer, reached by telephone in the dark, "
                                 "said the plants give everything they have. The Cabinet's view is printed below.");
        put(n, 0, NT_POWER, h, d, b);
        break;
    }
    put(n, 1, NT_QUIET, "THE PRESIDENT'S VIEW", "\"Nobody advises me. But I read the papers.\"",
        "Asked about the Cabinet's column, the President said he had already seen to it, and asked for the paper back.");
    text_fmt_int(a1, sizeof(a1), st->population);
    str_printf(d, sizeof(d), "%s residents, %s", a1, text_status_name(st->town_status));
    str_printf(b, sizeof(b), "The council met in emergency session and agreed that something should be done. It will meet again to "
                             "agree what.");
    put(n, 2, NT_QUIET, "FROM THE COUNCIL", d, b);
    filler(n);
    cabinet(n);
}

void era_news_build_ceremony(NewsIssue *n, int ts)
{
    char h[64], d[96], b[240], m[32], list[200], nx[64];
    int nl, y = sim_date()->year;
    issue_init(n, NEWS_CEREMONY);
    n->status = ts;
    nl = advice_status_unlocks(ts, list, sizeof(list));     /* PL1: everything the status brings (future years in brackets) */
    nx[0] = 0;
    if (ts + 1 < TS_COUNT) {                                /* PL1 (PLAYTEST 2 item 2b): and the next goal */
        char a1[32];
        text_fmt_int(a1, sizeof(a1), g_statuses[ts + 1].population);
        str_printf(nx, sizeof(nx), " Next goal: %s at %s residents.", text_status_name(ts + 1), a1);
    }
    money_str(m, sizeof(m), g_statuses[ts].reward, y);
    str_printf(h, sizeof(h), "%s IS NOW A %s", town(), text_status_name(ts));
    str_printf(d, sizeof(d), "By decree of President %s, read on the town hall steps", pres());
    str_printf(b, sizeof(b), "The brass band played, the mayor wore the chain, and the President said that the town had earned it. "
                             "The state grant of %s follows the decree.", m);
    put(n, 0, NT_STATUS, h, d, b);
    str_printf(h, sizeof(h), "WHAT THE NEW RIGHTS BRING");
    str_printf(d, sizeof(d), nl ? "New plans approved" : "Prestige, and a new letterhead");
    if (nl) str_printf(b, sizeof(b), "With the new rights the town may build: %s.%s", list, nx);
    else str_printf(b, sizeof(b), "No new buildings this time; the council orders new letterhead and a larger flag.%s", nx);
    put(n, 1, NT_STATUS, h, d, b);
    str_printf(h, sizeof(h), "THE PEOPLE SPEAK");
    str_printf(d, sizeof(d), "\"About time,\" says the shopkeeper");
    str_printf(b, sizeof(b), "Asked what the new status meant to her, a resident of %s said she hoped it meant a bus on Sundays. "
                             "The council will consider it.", town());
    put(n, 2, NT_STATUS, h, d, b);
    filler(n);
}

static int count_built(int zone, int cat, int def)
{
    int i, n = 0;
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        if (l->def == B_NONE || l->status == LOT_BUILDING) continue;
        d = def_building(l->def);
        if (def > 0) { if (l->def == def) n++; continue; }
        if (zone > 0 && d->cat == BCAT_ZONE && d->zone == zone) n++;
        else if (cat > 0 && d->cat == cat) n++;
    }
    return n;
}

int era_newsreel_lines(char lines[][96], int max)
{
    const CityStats *st = sim_stats();
    const TradeState *t = trade_state();
    char a1[32], a2[32], a3[32];
    int n = 0, y = sim_date()->year, d, k, top[3] = { -1, -1, -1 };
    int y0 = E.nyears ? E.years[0].year : y;
    if (max <= 0) return 0;
    text_fmt_int(a1, sizeof(a1), E.nyears ? E.years[0].pop : 0); text_fmt_int(a2, sizeof(a2), st->population);
    text_fmt_int(a3, sizeof(a3), E.peak_pop);
    if (n < max) {   /* PL3: the first yearly row is taken at the founding (0 residents): say so, not "0 in 1955" */
        if (E.nyears && E.years[0].pop <= 0)
            str_printf(lines[n++], 96, "Population: founded on bare land in %d, %s now (peak %s in %d).", y0, a2, a3, E.peak_year ? E.peak_year : y);
        else str_printf(lines[n++], 96, "Population: %s in %d, %s now (peak %s in %d).", a1, y0, a2, a3, E.peak_year ? E.peak_year : y);
    }
    if (n < max) {
        if (st->town_status > 0 && E.status_year_granted[st->town_status])
            str_printf(lines[n++], 96, "Status: %s, granted in %d.", text_status_name(st->town_status), E.status_year_granted[st->town_status]);
        else str_printf(lines[n++], 96, "Status: %s.", text_status_name(st->town_status));
    }
    if (n < max) str_printf(lines[n++], 96, "Built: %d homes, %d apartment blocks, %d shops, %d factories.",
                            count_built(ZONE_HOUSES, 0, 0), count_built(ZONE_APARTMENTS, 0, 0), count_built(ZONE_COMMERCIAL, 0, 0),
                            count_built(ZONE_INDUSTRY, 0, 0));
    if (n < max) str_printf(lines[n++], 96, "Services: %d schools, %d saunas, %d clinics; %d power plants; %d landmarks.",
                            count_built(0, 0, B_SCHOOL), count_built(0, 0, B_SAUNA), count_built(0, 0, B_CLINIC),
                            count_built(0, BCAT_POWER, 0), era_landmark_count());
    if (pol_on()) {
        for (d = 0; d < DEC_COUNT; d++) {
            if (E.dec_months[d] < 12) continue;
            for (k = 0; k < 3; k++)
                if (top[k] < 0 || E.dec_months[d] > E.dec_months[top[k]]) { int j; for (j = 2; j > k; j--) top[j] = top[j - 1]; top[k] = d; break; }
        }
        if (n < max) {
            if (top[0] < 0) str_printf(lines[n++], 96, "Decrees: the President governed by letters and patience.");
            else str_printf(lines[n++], 96, "Decrees that defined the era: %s (%d years)%s", g_decrees[top[0]].name, E.dec_months[top[0]] / 12,
                            top[1] >= 0 ? "," : ".");
        }
        if (n < max && top[1] >= 0)
            str_printf(lines[n++], 96, "  %s (%d years)%s%s", g_decrees[top[1]].name, E.dec_months[top[1]] / 12,
                       top[2] >= 0 ? ", " : ".", top[2] >= 0 ? g_decrees[top[2]].name : "");
        if (n < max) {
            const PolElection *e = politics_last_election();
            str_printf(lines[n++], 96, "Elections: %d held%s%d electors.", politics_elections_held(),
                       e && !e->skipped ? ", the last with " : ", ", e && !e->skipped ? e->electors : 0);
        }
    } else if (n < max) str_printf(lines[n++], 96, "Politics was left to the ministries (politics off).");
    text_fmt_int(a1, sizeof(a1), st->emigrated_total);
    if (n < max) str_printf(lines[n++], 96, "Emigration to Sweden: %s residents in all.", a1);
    text_fmt_int(a1, sizeof(a1), t->lost_total);
    if (n < max) str_printf(lines[n++], 96, "Clearing credit lost to surpluses: %s.", a1);
    money_str(a1, sizeof(a1), g_budget.money, y);
    if (n < max) str_printf(lines[n++], 96, "Treasury: %s.", a1);
    return n;
}

void era_news_build_newsreel(NewsIssue *n)
{
    char lines[12][96];
    int k, nl;
    issue_init(n, NEWS_NEWSREEL);
    nl = era_newsreel_lines(lines, 12);
    str_printf(n->head[0], sizeof(n->head[0]), "THE END OF AN ERA");
    str_printf(n->deck[0], sizeof(n->deck[0]), "%s, %d-%d: a newsreel", town(), E.nyears ? E.years[0].year : n->year, n->year);
    n->body[0][0] = 0;
    for (k = 0; k < nl && k < 3; k++) {
        if (k) str_printf(n->body[0] + strlen(n->body[0]), (int)sizeof(n->body[0]) - (int)strlen(n->body[0]), " ");
        str_printf(n->body[0] + strlen(n->body[0]), (int)sizeof(n->body[0]) - (int)strlen(n->body[0]), "%s", lines[k]);
    }
    n->topic[0] = NT_ERA;
    str_copy(n->head[1], "WHAT WE BUILT", sizeof(n->head[1]));
    str_copy(n->deck[1], nl > 3 ? lines[3] : "", sizeof(n->deck[1]));
    n->body[1][0] = 0;
    for (k = 4; k < nl && k < 7; k++) {
        if (k > 4) str_printf(n->body[1] + strlen(n->body[1]), (int)sizeof(n->body[1]) - (int)strlen(n->body[1]), " ");
        str_printf(n->body[1] + strlen(n->body[1]), (int)sizeof(n->body[1]) - (int)strlen(n->body[1]), "%s", lines[k]);
    }
    str_copy(n->head[2], "AND NOW?", sizeof(n->head[2]));
    str_copy(n->deck[2], "The sandbox continues", sizeof(n->deck[2]));
    str_printf(n->body[2], sizeof(n->body[2]), "%s %s The President, asked about the future, said he intends to keep an eye on it.",
               nl > 7 ? lines[7] : "", nl > 8 ? lines[8] : "");
    filler(n);
    n->nverdict = advice_verdict(n->verdict, 3);    /* PL1: "What the history books will say" */
}

/* ------------------------------------------------------------------ scripts */
static int word_of(const char *w, const char *const *names, int n)
{
    int i;
    for (i = 0; i < n; i++) if (str_ieq(w, names[i])) return i;
    return -1;
}

static void print_issue(FILE *out, const NewsIssue *n)
{
    int k;
    if (!out || !n) return;
    fprintf(out, "news: MAAKUNNAN SANOMAT no. %d, %s %d (kind %d)\n", n->issue_no, text_month_name(n->month), n->year, n->kind);
    for (k = 0; k < NEWS_HEADS; k++) if (n->head[k][0]) fprintf(out, "news:   [%d] %s / %s / %s\n", n->topic[k], n->head[k], n->deck[k], n->body[k]);
    fprintf(out, "news:   filler %s: %s\n", n->filler_head, n->filler);
    for (k = 0; k < n->ncab; k++) fprintf(out, "news:   cabinet %d (kind %d, go %d at %d,%d): %s\n", k + 1, n->cab_kind[k], n->cab_go[k], n->cab_x[k], n->cab_y[k], n->cab[k]);
    if (n->hind[0]) fprintf(out, "news:   %s: %s\n", n->hind_head, n->hind);
    for (k = 0; k < n->nverdict; k++) fprintf(out, "news:   verdict %d: %s\n", k + 1, n->verdict[k]);
}

int era_script_line(int n, char **t, FILE *out)
{
    static const char *const lv[TL_COUNT] = { "modest", "standard", "ambitious" };
    int k, ok = 0;
    if (n < 1) return -1;
    if (str_ieq(t[0], "trade")) {
        if (n < 2) return SCR_ERROR;
        if (str_ieq(t[1], "sign") && n >= 3 && (k = word_of(t[2], lv, TL_COUNT)) >= 0) ok = trade_sign(k);
        else if (str_ieq(t[1], "bond")) ok = trade_issue_bond();
        else if (str_ieq(t[1], "complex") && n >= 6) { trade_set_complex(atoi(t[2]), atoi(t[3]), atoi(t[4]), atoi(t[5])); ok = 1; }
        else if (str_ieq(t[1], "dump")) { trade_dump(out); ok = 1; }
        if (out) fprintf(out, "script: trade %s -> %s\n", t[1], ok ? "ok" : "refused");
        return ok ? SCR_OK : SCR_ERROR;
    }
    if (str_ieq(t[0], "era")) {
        if (n >= 2 && str_ieq(t[1], "dump")) { era_dump(out); return SCR_OK; }
        if (n >= 2 && str_ieq(t[1], "sauna")) return era_sauna_invite() ? SCR_OK : SCR_ERROR;
        return SCR_ERROR;
    }
    if (str_ieq(t[0], "advice")) { advice_dump(out); return SCR_OK; }    /* PL1 */
    if (str_ieq(t[0], "news")) {
        /* scripts (screenshots): the asked-for issue is the only one waiting */
        if (n >= 2 && str_ieq(t[1], "seen")) { era_news_seen(); return SCR_OK; }
        E.pending = 0;
        if (n >= 2 && str_ieq(t[1], "newsreel")) era_news_request_newsreel();
        else if (n >= 2 && str_ieq(t[1], "special")) { era_news_build_special(&E.news[NEWS_SPECIAL], n >= 3 ? atoi(t[2]) : ADV_POWER_SHORT); E.pending |= 1u << NEWS_SPECIAL; }
        else if (n >= 2 && str_ieq(t[1], "ceremony")) { era_news_build_ceremony(&E.news[NEWS_CEREMONY], sim_stats()->town_status); E.pending |= 1u << NEWS_CEREMONY; }
        else { era_news_build_year(&E.news[NEWS_YEAR]); E.pending |= 1u << NEWS_YEAR; }
        print_issue(out, era_news());
        return SCR_OK;
    }
    return -1;
}

/* ------------------------------------------------------------------ debug, hash, save */
void era_dump(FILE *f)
{
    int k;
    if (!f) return;
    fprintf(f, "era %d-%02d: %s; status held %d granted %d eligible %d petitioned %d; landmarks", sim_date()->year,
            sim_date()->month + 1, text_era_name(def_era(sim_date()->year)), sim_stats()->town_status, E.granted, E.eligible, E.petitioned);
    for (k = 0; k < NLM; k++) fprintf(f, " %s %d", def_building(s_lm[k].def)->key, E.lm_count[k]);
    fprintf(f, "; events seen %x; decrees signed %d; news pending %x (issues %d); years %d\n", E.seen, E.decrees_signed, E.pending,
            E.issue_no, E.nyears);
    trade_dump(f);
}

u32 era_hash(void)
{
    u32 h = trade_hash();
    s32 v[8];
    v[0] = E.granted; v[1] = E.eligible; v[2] = (s32)E.seen; v[3] = E.decrees_signed; v[4] = E.nyears;
    v[5] = E.sauna_uses * 4096 + E.card; v[6] = era_landmark_count(); v[7] = E.issue_no;
    return hash_bytes(h, v, sizeof(v));
}

static void save_issue(struct SaveFile *sf, const NewsIssue *n)
{
    int k;
    save_s32(sf, n->kind); save_s32(sf, n->year); save_s32(sf, n->month); save_s32(sf, n->issue_no); save_s32(sf, n->status);
    for (k = 0; k < NEWS_HEADS; k++) { save_str(sf, n->head[k]); save_str(sf, n->deck[k]); save_str(sf, n->body[k]); save_s32(sf, n->topic[k]); }
    save_str(sf, n->filler_head); save_str(sf, n->filler);
    save_s32(sf, n->ncab);                     /* PL1: ERA v2 */
    for (k = 0; k < NEWS_CAB; k++) { save_str(sf, n->cab[k]); save_u8(sf, n->cab_kind[k]); save_u8(sf, n->cab_go[k]); save_s32(sf, n->cab_x[k]); save_s32(sf, n->cab_y[k]); }
    save_str(sf, n->hind_head); save_str(sf, n->hind);
    save_s32(sf, n->nverdict);
    for (k = 0; k < 3; k++) save_str(sf, n->verdict[k]);
}
static void load_issue(struct SaveFile *sf, NewsIssue *n, int ver)
{
    int k;
    memset(n, 0, sizeof(*n));
    n->kind = load_s32(sf); n->year = load_s32(sf); n->month = load_s32(sf); n->issue_no = load_s32(sf); n->status = load_s32(sf);
    for (k = 0; k < NEWS_HEADS; k++) {
        load_str(sf, n->head[k], sizeof(n->head[k])); load_str(sf, n->deck[k], sizeof(n->deck[k]));
        load_str(sf, n->body[k], sizeof(n->body[k])); n->topic[k] = load_s32(sf);
    }
    load_str(sf, n->filler_head, sizeof(n->filler_head)); load_str(sf, n->filler, sizeof(n->filler));
    if (ver < 2) return;                       /* PL1 fields */
    n->ncab = ICLAMP(load_s32(sf), 0, NEWS_CAB);
    for (k = 0; k < NEWS_CAB; k++) {
        load_str(sf, n->cab[k], sizeof(n->cab[k])); n->cab_kind[k] = (u8)load_u8(sf); n->cab_go[k] = (u8)load_u8(sf);
        n->cab_x[k] = (s16)load_s32(sf); n->cab_y[k] = (s16)load_s32(sf);
    }
    load_str(sf, n->hind_head, sizeof(n->hind_head)); load_str(sf, n->hind, sizeof(n->hind));
    n->nverdict = ICLAMP(load_s32(sf), 0, 3);
    for (k = 0; k < 3; k++) load_str(sf, n->verdict[k], sizeof(n->verdict[k]));
}

#define YEAR_FIELDS 25
static void year_fields(EraYear *r, s32 *v, int store)
{
    s32 *p[] = { &r->pop, &r->money, &r->jobs, &r->unemployed, &r->emig_year, &r->homeless, &r->industry_jobs, &r->east,
                 &r->west, &r->balance, &r->lots_t3, &r->apartments, &r->landmarks, &r->loan_total };
    s16 *q[] = { &r->year, &r->status, &r->popularity, &r->east_meter, &r->west_meter, &r->power_warn, &r->water_warn,
                 &r->elections, &r->decrees_signed, &r->oil_index, &r->fulfil };
    int i, np = (int)COUNTOF(p), nq = (int)COUNTOF(q);
    for (i = 0; i < np; i++) { if (store) *p[i] = v[i]; else v[i] = *p[i]; }
    for (i = 0; i < nq; i++) { if (store) *q[i] = (s16)v[np + i]; else v[np + i] = *q[i]; }
}

int era_save(struct SaveFile *sf)
{
    int i, k;
    s32 v[YEAR_FIELDS];
    save_begin_chunk(sf, "ERA ", 2);   /* PL1: v2 = NewsIssue Cabinet/hindsight/verdict fields */
    save_s32(sf, E.granted); save_s32(sf, E.eligible); save_s32(sf, E.petitioned);
    save_s32(sf, NLM); for (i = 0; i < NLM; i++) save_s32(sf, E.lm_count[i]);
    save_s32(sf, E.sauna_uses); save_s32(sf, E.sauna_year); save_s32(sf, E.card); save_s32(sf, E.prev_pending); save_s32(sf, E.last_card_m);
    save_s32(sf, DEC_COUNT); for (i = 0; i < DEC_COUNT; i++) { save_s32(sf, E.dec_months[i]); save_u8(sf, E.dec_active[i]); }
    save_s32(sf, E.decrees_signed);
    save_s32(sf, E.power_warn); save_s32(sf, E.water_warn); save_s32(sf, E.status_year); save_s32(sf, E.landmark_year);
    save_s32(sf, E.landmark_def); save_s32(sf, E.loan_year); save_s32(sf, E.emig_year); save_u32(sf, E.seen); save_s32(sf, E.elections0);
    save_s32(sf, EH_COUNT); save_s32(sf, ERA_HIST); save_s32(sf, E.hist_pos); save_s32(sf, E.hist_n);
    for (k = 0; k < EH_COUNT; k++) for (i = 0; i < ERA_HIST; i++) save_s32(sf, E.hist[k][i]);
    save_s32(sf, E.nyears); save_s32(sf, YEAR_FIELDS);
    for (i = 0; i < E.nyears; i++) { year_fields(&E.years[i], v, 0); for (k = 0; k < YEAR_FIELDS; k++) save_s32(sf, v[k]); }
    save_s32(sf, NEWS_KIND_COUNT);
    for (i = 0; i < NEWS_KIND_COUNT; i++) save_issue(sf, &E.news[i]);
    save_u32(sf, E.pending); save_s32(sf, E.last_kind); save_s32(sf, E.issue_no); save_s32(sf, E.newsreel_done);
    save_s32(sf, E.peak_pop); save_s32(sf, E.peak_year);
    save_s32(sf, TS_COUNT); for (i = 0; i < TS_COUNT; i++) save_s32(sf, E.status_year_granted[i]);
    save_end_chunk(sf);
    advice_save(sf);                           /* PL1: chunk "ADVC" */
    return trade_save(sf);
}

int era_load(struct SaveFile *sf)
{
    int ver, i, k, n, m;
    s32 v[YEAR_FIELDS];
    int ok = 1;
    if (load_find_chunk(sf, "ERA ", &ver)) {
        E.granted = load_s32(sf); E.eligible = load_s32(sf); E.petitioned = load_s32(sf);
        n = load_s32(sf); for (i = 0; i < n; i++) { s32 c = load_s32(sf); if (i < NLM) E.lm_count[i] = c; }
        E.sauna_uses = load_s32(sf); E.sauna_year = load_s32(sf); E.card = load_s32(sf); E.prev_pending = load_s32(sf); E.last_card_m = load_s32(sf);
        n = load_s32(sf);
        for (i = 0; i < n; i++) { s32 a = load_s32(sf); u32 b = load_u8(sf); if (i < DEC_COUNT) { E.dec_months[i] = (s16)a; E.dec_active[i] = (u8)b; } }
        E.decrees_signed = load_s32(sf);
        E.power_warn = load_s32(sf); E.water_warn = load_s32(sf); E.status_year = load_s32(sf); E.landmark_year = load_s32(sf);
        E.landmark_def = load_s32(sf); E.loan_year = load_s32(sf); E.emig_year = load_s32(sf); E.seen = load_u32(sf); E.elections0 = load_s32(sf);
        n = load_s32(sf); m = load_s32(sf);
        E.hist_pos = load_s32(sf); E.hist_n = load_s32(sf);
        for (k = 0; k < n; k++) for (i = 0; i < m; i++) { s32 x = load_s32(sf); if (k < EH_COUNT && i < ERA_HIST) E.hist[k][i] = x; }
        E.hist_pos = ICLAMP(E.hist_pos, 0, ERA_HIST - 1); E.hist_n = ICLAMP(E.hist_n, 0, ERA_HIST);
        n = load_s32(sf); m = load_s32(sf);
        E.nyears = 0;
        for (i = 0; i < n; i++) {
            for (k = 0; k < m; k++) { s32 x = load_s32(sf); if (k < YEAR_FIELDS) v[k] = x; }
            for (k = m; k < YEAR_FIELDS; k++) v[k] = 0;
            if (E.nyears < ERA_YEARS) year_fields(&E.years[E.nyears++], v, 1);
        }
        n = load_s32(sf);
        for (i = 0; i < n; i++) { NewsIssue tmp; load_issue(sf, &tmp, ver); if (i < NEWS_KIND_COUNT) E.news[i] = tmp; }
        E.pending = load_u32(sf); E.last_kind = load_s32(sf); E.issue_no = load_s32(sf); E.newsreel_done = load_s32(sf);
        E.peak_pop = load_s32(sf); E.peak_year = load_s32(sf);
        n = load_s32(sf); for (i = 0; i < n; i++) { s32 x = load_s32(sf); if (i < TS_COUNT) E.status_year_granted[i] = x; }
        ok = load_ok(sf);
    } else {
        /* old save: era state from the city */
        E.granted = E.eligible = sim_stats()->town_status;
    }
    advice_load(sf);                           /* PL1 (missing: fresh) */
    if (!trade_load(sf)) {
        /* old save without TRAD: fresh trade state for the save's date (era_reset ran trade_reset at the start year) */
        trade_reset();
    }
    UNUSED(ver);
    return ok;
}
