/* hard.cpp - 1.1 D1 "Harder times": the Sweden years 1969-70, era expectations, and the texts that name the
   difficulty levers. PORTABLE, deterministic, integer-only. Owner: D1. See hard.h; the table is g_diff (defs.cpp). */
#include "common.h"
#include "hard.h"
#include "sim.h"
#include "world.h"
#include "net.h"
#include "text.h"
#include "save.h"
#include "util.h"
#include "budget.h"

static HardSweden s_sw;

void hard_reset(void) { memset(&s_sw, 0, sizeof(s_sw)); }

/* ------------------------------------------------------------------ Sweden 1969-70 */
const HardSweden *hard_sweden(void) { return &s_sw; }

int hard_sweden_year(int year) { return diff()->sweden_permille > 0 && year >= SWEDEN_FROM_YEAR && year <= SWEDEN_TO_YEAR; }

s32 hard_sweden_eval(void)
{
    const CityStats *st = sim_stats();
    s_sw.active = (u8)hard_sweden_year(sim_date()->year);
    if (!s_sw.active) { s_sw.pull = 0; s_sw.jobs = s_sw.homes = s_sw.content = 0; s_sw.reasons = 0; return 0; }
    s_sw.jobs = (u8)(st->unemployed * 100 <= st->workers * 3);
    s_sw.homes = (u8)(st->homeless <= 0 && st->vacant * 100 >= st->capacity_res * 2);
    s_sw.content = (u8)(st->avg_happiness >= 60);
    s_sw.reasons = (u8)(s_sw.jobs + s_sw.homes + s_sw.content);
    s_sw.pull = (s32)((s64)st->population * diff()->sweden_permille * (3 - s_sw.reasons) * (3 - s_sw.reasons) / 9000);   /* 1.1 B1:
        squared: one missing reason costs a ninth, two four ninths, none held the full pull (a careful town that lacks only
        spare flats loses ~1 %, a neglected one 5-8 %; linear thirds could not give both) */
    return -s_sw.pull;
}

void hard_sweden_book(s32 n)
{
    if (!s_sw.active) { s_sw.month = 0; return; }
    s_sw.month = n;
    s_sw.total += n;
}

int hard_sweden_reasons(char *buf, int size)
{
    return str_printf(buf, size, "Reasons to stay: jobs %s homes %s contentment %s", s_sw.jobs ? GLYPH_CHECK : GLYPH_CROSS,
                      s_sw.homes ? GLYPH_CHECK : GLYPH_CROSS, s_sw.content ? GLYPH_CHECK : GLYPH_CROSS);
}

int hard_sweden_line(char *buf, int size)
{
    char a[24], r[80];
    if (!s_sw.active) { if (size > 0) buf[0] = 0; return 0; }
    text_fmt_int(a, sizeof(a), s_sw.pull);
    hard_sweden_reasons(r, sizeof(r));
    return str_printf(buf, size, "Sweden is hiring: -%s this month. %s", a, r);
}

/* ------------------------------------------------------------------ era expectations */
int hard_expect_year(int which) { return which == HX_CLINIC ? EXPECT_CLINIC_YEAR : which == HX_HCENTRE ? EXPECT_HCENTRE_YEAR : EXPECT_LIBRARY_YEAR; }
int hard_expect_req(int which)  { return which == HX_CLINIC ? REQ_HEALTH : which == HX_HCENTRE ? REQ_HEALTH_CENTRE : REQ_LIBRARY; }
int hard_expect_svc(int which)  { return which == HX_LIBRARY ? SVC_LIBRARY : SVC_HEALTH; }

/* 1.1 B1: health centres against the town's size, cached per sim cycle (pure function of the lots and the population) */
static u32 s_hc_key = 0xffffffffu;
static s32 s_hc_have, s_hc_need;
int hard_hcentre_ok(s32 *have, s32 *need)
{
    const SimDate *dt = sim_date();
    u32 key = (u32)dt->total_months * 64u + (u32)dt->step * 8u + (u32)dt->cycle;
    if (key != s_hc_key) {
        int i;
        s_hc_have = 0;
        for (i = 1; i < g_world.lot_hi; i++)
            if (g_world.lots[i].def == B_HEALTH_CENTRE && g_world.lots[i].status == LOT_ACTIVE) s_hc_have++;
        s_hc_need = 1 + sim_stats()->population / EXPECT_HCENTRE_PER;
        if (sim_stats()->town_status < g_bdefs[B_HEALTH_CENTRE].status_req) s_hc_need = 0;   /* a Town's duty: market towns
                                                                                       are not asked for what they cannot build */
        s_hc_key = key;
    }
    if (have) *have = s_hc_have;
    if (need) *need = s_hc_need;
    return s_hc_have >= s_hc_need;
}

int hard_expect_phase(int which, int zone, int tier, int year, int month)
{
    int y0 = hard_expect_year(which), since;
    if (!diff()->expectations) return 0;
    if (which == HX_CLINIC) { if (zone != ZONE_APARTMENTS || tier < 2) return 0; }
    else if ((zone != ZONE_HOUSES && zone != ZONE_APARTMENTS) || tier != 3) return 0;
    if (year < y0 - 1) return 0;
    if (year < y0) return 1;
    since = (year - y0) * 12 + month;
    return since < EXPECT_GRACE_MONTHS ? 2 : 3;
}

/* residents (and lots) in homes the expectation applies to (at their own tier) without that service's cover */
static s32 expect_lacking(int which, int *nlots)
{
    int i, n = 0, svc = hard_expect_svc(which), town_ok = which == HX_HCENTRE && hard_hcentre_ok(NULL, NULL);
    s32 res = 0;
    const u8 *q = net_cov_q_map(svc);
    for (i = 1; i < g_world.lot_hi; i++) {
        const Lot *l = &g_world.lots[i];
        const BuildingDef *d;
        int x0, y0, x1, y1, cell;
        if (l->def == B_NONE || l->status != LOT_ACTIVE) continue;
        d = def_building(l->def);
        if (d->cat != BCAT_ZONE) continue;
        if (which == HX_CLINIC ? (d->zone != ZONE_APARTMENTS || d->tier < 2)
                               : ((d->zone != ZONE_HOUSES && d->zone != ZONE_APARTMENTS) || d->tier != 3)) continue;   /* B1: hcentre as library */
        if (which == HX_HCENTRE) { if (town_ok) continue; n++; res += l->occupants; continue; }   /* 1.1 B1: town-wide */
        world_lot_rect(i, &x0, &y0, &x1, &y1);
        cell = (((y0 + y1) >> 1) >> FIELD_SHIFT) * FIELD_MAX_W + (((x0 + x1) >> 1) >> FIELD_SHIFT);
        if (q[cell] >= 20) continue;
        n++; res += l->occupants;
    }
    if (nlots) *nlots = n;
    return res;
}

/* ------------------------------------------------------------------ texts */
static const char *town(void) { return sim_settings()->town[0] ? sim_settings()->town : "the town"; }

int hard_news_story(int year, char *h, int hs, char *d, int ds, char *b, int bs, int *slot)
{
    char a1[24], r[80];
    s32 res;
    *slot = 1;
    if (diff()->sweden_permille > 0) {
        if (year == SWEDEN_FROM_YEAR) {
            *slot = 0;
            str_printf(h, hs, "SWEDISH FACTORIES RECRUIT IN THE MARKET SQUARE");
            str_printf(d, ds, "Good wages, warm flats, and a one-way ticket to Gothenburg");
            str_printf(b, bs, "Recruiters from the Swedish car works and shipyards have a table outside the co-op. For two years "
                              "they take whoever has no reason to stay. The Cabinet counts three: work, a home, contentment. "
                              "%s has its own opinion.", town());
            return 1;
        }
        if (year == SWEDEN_TO_YEAR || year == SWEDEN_TO_YEAR + 1) {
            text_fmt_int(a1, sizeof(a1), s_sw.total);
            if (year == SWEDEN_TO_YEAR) {
                str_printf(h, hs, s_sw.total > 0 ? "THE SWEDEN BOAT LEAVES FULL" : "THE RECRUITERS DRINK THEIR COFFEE ALONE");
                str_printf(r, sizeof(r), "Reasons to stay: work %s, homes %s, contentment %s", s_sw.jobs ? "yes" : "no",
                           s_sw.homes ? "yes" : "no", s_sw.content ? "yes" : "no");   /* words: the paper has no glyphs */
                str_printf(d, ds, "%s residents went last year; the recruiters stay another year", a1);
                str_printf(b, bs, "The Swedish recruiters report a good year in %s, which the town does not. %s. The Cabinet "
                                  "suggests the President supply the missing ones before the boat sails again.", town(), r);
            } else {
                str_printf(h, hs, "THE RECRUITERS PACK UP");
                str_printf(d, ds, "%s residents left for Sweden in 1969-70", a1);
                str_printf(b, bs, "The table outside the co-op is gone. Letters from Gothenburg describe good wages and strange "
                                  "food. %s counts its residents again, and some of its neighbours twice.", town());
            }
            return 1;
        }
    }
    if (diff()->expectations) {
        int k;
        for (k = 0; k < HX_COUNT; k++) {
            int y0 = hard_expect_year(k);
            if (year != y0 - 1 && year != y0) continue;
            res = expect_lacking(k, NULL);
            text_fmt_int(a1, sizeof(a1), res);
            if (k == HX_LIBRARY && year == y0 - 1) {
                str_printf(h, hs, "FROM %d FAMILIES EXPECT A LIBRARY WITHIN REACH", y0);
                str_printf(d, ds, "The welfare state reaches the bookshelf");
                str_printf(b, bs, "The Ministry of Education writes that a modern home has a library within reach. From %d the "
                                  "best homes (tier 3) need one to be built; those standing have until %d. Today %s of their "
                                  "residents have none.", y0, y0 + 1, a1);
            } else if (k == HX_LIBRARY) {
                str_printf(h, hs, "THE WELFARE STATE: LIBRARIES NOW EXPECTED");
                str_printf(d, ds, "New tier-3 homes need a library; the others have a year");
                str_printf(b, bs, "The expectation is in force. Tier-3 homes without a library within reach can no longer be "
                                  "built, and those standing will lose patience after %d. %s residents are, for now, reading "
                                  "the telephone directory.", y0, a1);
            } else if (k == HX_HCENTRE && year == y0 - 1) {   /* 1.1 B1 */
                str_printf(h, hs, "FROM %d EVERY TOWN ITS HEALTH CENTRE", y0);
                str_printf(d, ds, "The Primary Health Care Act reaches %s", town());
                str_printf(b, bs, "The National Board of Health writes that a modern town has a health centre (one for every "
                                  "20,000 residents). From %d the best homes (tier 3) need one to be built; those standing "
                                  "have until %d. %s of their residents still queue at the clinic.", y0, y0 + 1, a1);
            } else if (k == HX_HCENTRE) {
                str_printf(h, hs, "HEALTH FOR ALL: A HEALTH CENTRE NOW EXPECTED");
                str_printf(d, ds, "Tier-3 homes need the town's health centre; the others have a year");
                str_printf(b, bs, "The expectation is in force. Without a health centre the best homes can no longer be built, "
                                  "and those standing lose patience after %d. %s residents bring sandwiches to the waiting "
                                  "room.", y0, a1);
            } else if (year == y0 - 1) {
                str_printf(h, hs, "FROM %d SUBURBS EXPECT A CLINIC", y0);
                str_printf(d, ds, "The l\xe4hi\xf6 question reaches the Ministry");
                str_printf(b, bs, "Critics call the new suburbs concrete boxes in the forest. The Ministry replies that from %d "
                                  "apartment blocks of tier 2 and up need a clinic within reach; standing blocks have until %d. "
                                  "%s of their residents have none.", y0, y0 + 1, a1);
            } else {
                str_printf(h, hs, "THE L\xc4HI\xd6 QUESTION: CLINICS NOW EXPECTED");
                str_printf(d, ds, "Blocks of tier 2 and up need a clinic; the others have a year");
                str_printf(b, bs, "The expectation is in force. Apartment blocks without a clinic within reach will not move up, "
                                  "and those standing lose patience after %d. %s residents take the bus to the doctor.", y0, a1);
            }
            return 1;
        }
    }
    return 0;
}

int hard_advice(int k, char *text, int size, s32 *score, s32 *residents)
{
    char a1[24];
    *score = 0; *residents = 0;
    if (k == 0) {
        const char *miss;
        if (!s_sw.active || s_sw.reasons >= 3 || s_sw.pull <= 0) return 0;
        text_fmt_int(a1, sizeof(a1), s_sw.pull);
        miss = !s_sw.jobs ? "work: factories and offices would keep them home"
             : !s_sw.homes ? "homes: a few empty flats (two in a hundred) and nobody sleeping in the sauna"
             : "contentment: parks, saunas and services within reach";
        str_printf(text, size, "Swedish recruiters take %s residents a month. The Cabinet notes that %s lacks a reason to stay: %s.",
                   a1, town(), miss);
        *residents = s_sw.pull;
        *score = 620 + IMIN(s_sw.pull, 300);
        return 1;
    }
    if (k == 1 || k == 2 || k == 3) {
        int which = k == 1 ? HX_LIBRARY : k == 2 ? HX_CLINIC : HX_HCENTRE, y0 = hard_expect_year(which), y = sim_date()->year, lots;
        s32 res;
        if (!diff()->expectations || y < y0 - 1 || y > y0 + 1) return 0;
        res = expect_lacking(which, &lots);
        if (res < 100) return 0;
        text_fmt_int(a1, sizeof(a1), res);
        if (which == HX_HCENTRE)   /* 1.1 B1 */
            str_printf(text, size, y < y0 ? "From %d the best homes expect the town to have a health centre. %s residents queue at "
                                             "the clinic instead; the National Board of Health suggests one (Town status)."
                                           : "Since %d the best homes expect a health centre in town. %s residents will not wait "
                                             "past the end of the year; the National Board of Health suggests one.", y0, a1);
        else if (which == HX_LIBRARY)
            str_printf(text, size, y < y0 ? "From %d families in the best homes expect a library within reach. %s residents have "
                                             "none; the Cabinet suggests a library, and perhaps a reading lamp."
                                           : "Since %d the best homes expect a library within reach. %s residents have none and "
                                             "will not wait past the end of the year; the Cabinet suggests a library.", y0, a1);
        else
            str_printf(text, size, y < y0 ? "From %d apartment blocks of tier 2 and up expect a clinic within reach. %s residents "
                                             "have none; the Medical Board would welcome a clinic."
                                           : "Since %d apartment blocks expect a clinic within reach. %s residents have none and "
                                             "will not wait past the end of the year; the Medical Board would welcome a clinic.", y0, a1);
        *residents = res;
        *score = (y < y0 ? 330 : 520) + IMIN(res / 20, 300);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ the level, named */
static int pct_note(char *buf, int size, int v, int easy) { return v == easy ? 0 : str_printf(buf, size, "%d%%", v); }
static char s_scratch[100];
static char *ln(char lines[][100], int *n, int max) { return *n < max ? lines[(*n)++] : s_scratch; }   /* no variadic macros in VC6 */

int hard_level_lines(char lines[][100], int max)
{
    const DiffParams *p = diff(), *e = &g_diff[DIFF_EASY];
    int n = 0, cap_year = 1960 + (p->upkeep_cap - 100) * 10 / 25;
    if (max <= 0) return 0;
    str_printf(ln(lines, &n, max), 100, "Presidency: %s%s", p->name, diff_level() == DIFF_EASY ? " (the 1.0 rules)" : "");
    str_printf(ln(lines, &n, max), 100, "Upkeep of services, roads and networks: x%d%% from 1965, rising from 1960 (Easy %d%%)",
               p->upkeep_pct, e->upkeep_pct);
    str_printf(ln(lines, &n, max), 100, "Upkeep index rises 2.5%%/year to %d%% in %d (Easy %d%%, %d)", p->upkeep_cap, cap_year, e->upkeep_cap,
      1960 + (e->upkeep_cap - 100) * 10 / 25);
    str_printf(ln(lines, &n, max), 100, "Tax yield x%d%% (from 1965); taxes slow growth above %d%% (Easy %d%%, %d%%)", p->tax_yield_pct,
               p->tax_neutral, e->tax_yield_pct, e->tax_neutral);
    str_printf(ln(lines, &n, max), 100, "Service overtime from %d%% load (Easy %d%%)", p->overtime_free, e->overtime_free);
    str_printf(ln(lines, &n, max), 100, "Emigration: 1/%d of the jobless, 1/%d of the homeless a month (Easy 1/%d, 1/%d)", p->emig_unemp_div,
      p->emig_homeless_div, e->emig_unemp_div, e->emig_homeless_div);
    str_printf(ln(lines, &n, max), 100, "Decline: a tier lost after %d bad weeks, %d for power/water (Easy %d, %d)", p->decline_checks, p->decline_util_checks,
      e->decline_checks, e->decline_util_checks);
    str_printf(ln(lines, &n, max), 100, "Event cards: bad effects x%d%%, every %d-%d months (Easy x%d%%, %d-%d)", p->card_neg_pct, p->card_gap_min,
      p->card_gap_max, e->card_neg_pct, e->card_gap_min, e->card_gap_max);
    str_printf(ln(lines, &n, max), 100, "Authority: popularity / %d a month (Easy / %d)", p->auth_div, e->auth_div);
    str_printf(ln(lines, &n, max), 100, "The neighbour's patience: Eastern relations drift to %d (Easy %d)", p->east_target, e->east_target);
    str_printf(ln(lines, &n, max), 100, "Oil and coal shocks x%d%%: oil %d in 1973 (Easy 400)", p->shock_pct, 100 + 300 * p->shock_pct / 100);
    str_printf(ln(lines, &n, max), 100, "Fires: ignition x%d%%, %d spreads, winter x%d%% (Easy %d%%, %d)", p->fire_ppb, p->fire_spread_max, p->fire_winter_pct,
      e->fire_ppb, e->fire_spread_max);
    if (p->winter_load_pct || p->hard_winter_pct) {
        str_printf(ln(lines, &n, max), 100, "Winter: power and heat +%d%% (+1 a year from %d); hard winters %d%% (+%d%%, water %d%%)",
          p->winter_load_pct, ELECTRIC_HEAT_FROM, p->hard_winter_pct, p->hard_load_pct, p->hard_water_pct);   /* 1.1 B1: electric heating */
        str_printf(ln(lines, &n, max), 100, "Ploughing: road upkeep +%d%% in winter (+%d%% hard); frost repairs %d mk/10 pipes", p->plough_pct, p->plough_hard_pct,
          p->frost_repair10);
    } else str_printf(ln(lines, &n, max), 100, "Winter: snow and stories only");
    if (p->loan_after != e->loan_after || p->loan_cushion100 != e->loan_cushion100 || p->loan_surplus != e->loan_surplus)   /* 1.1 B1 */
        str_printf(ln(lines, &n, max), 100, "Emergency loan after %d months in the red, +%d mk%s (Easy %d, +%d)", p->loan_after,
                   p->loan_cushion100 * 100, p->loan_surplus ? "; repaid from surpluses" : "", e->loan_after, e->loan_cushion100 * 100);
    if (p->sweden_permille) str_printf(ln(lines, &n, max), 100, "Sweden 1969-70: recruiters take up to %d per mille a month; one reason missing 1/9, two 4/9",
                              p->sweden_permille);
    else str_printf(ln(lines, &n, max), 100, "Sweden 1969-70: only the usual emigration");
    if (p->expectations) str_printf(ln(lines, &n, max), 100, "Expected: library (tier 3) %d, clinic (blocks) %d, health centre (tier 3) %d",
                           EXPECT_LIBRARY_YEAR, EXPECT_CLINIC_YEAR, EXPECT_HCENTRE_YEAR);
    else str_printf(ln(lines, &n, max), 100, "Era expectations: none");
    return n;
}

int hard_note(int what, char *buf, int size)
{
    const DiffParams *p = diff(), *e = &g_diff[DIFF_EASY];
    char a[16];
    if (size > 0) buf[0] = 0;
    switch (what) {
    case HN_LEVEL:     return str_printf(buf, size, "Difficulty: %s", p->name);
    case HN_UPKEEP:    return pct_note(a, sizeof(a), budget_level_upkeep_pct(), e->upkeep_pct) ? str_printf(buf, size, "%s: upkeep x%s", p->name, a) : 0;
    case HN_INDEX:     return p->upkeep_cap != e->upkeep_cap ? str_printf(buf, size, "%s: index up to %d%%", p->name, p->upkeep_cap) : 0;
    case HN_TAX:
        if (p->tax_yield_pct == e->tax_yield_pct && p->tax_neutral == e->tax_neutral) return 0;
        if (budget_level_tax_pct() == 100) return str_printf(buf, size, "%s: taxes slow growth above %d%%", p->name, p->tax_neutral);
        return str_printf(buf, size, "%s: tax yield x%d%%, taxes slow growth above %d%%", p->name, budget_level_tax_pct(), p->tax_neutral);
    case HN_OVERTIME:  return p->overtime_free != e->overtime_free ? str_printf(buf, size, "%s: overtime from %d%% load", p->name, p->overtime_free) : 0;
    case HN_EMIGRATION:
        return p->emig_unemp_div != e->emig_unemp_div || p->emig_homeless_div != e->emig_homeless_div
             ? str_printf(buf, size, "%s: 1/%d of the jobless and 1/%d of the homeless leave a month", p->name, p->emig_unemp_div,
                          p->emig_homeless_div) : 0;
    case HN_DECLINE:
        return p->decline_checks != e->decline_checks || p->decline_util_checks != e->decline_util_checks
             ? str_printf(buf, size, "%s: a tier is lost after %d bad weeks (%d for power/water)", p->name, p->decline_checks,
                          p->decline_util_checks) : 0;
    case HN_CARDS:     return p->card_neg_pct != e->card_neg_pct ? str_printf(buf, size, "%s: bad effects x%d%%", p->name, p->card_neg_pct) : 0;
    case HN_AUTHORITY: return p->auth_div != e->auth_div ? str_printf(buf, size, "%s: popularity / %d a month", p->name, p->auth_div) : 0;
    case HN_EAST:      return p->east_target != e->east_target ? str_printf(buf, size, "The neighbour's patience (%s): drifts to %d", p->name, p->east_target) : 0;
    case HN_SHOCK:     return p->shock_pct != e->shock_pct ? str_printf(buf, size, "%s: oil and coal shocks x%d%%", p->name, p->shock_pct) : 0;
    case HN_FIRE:
        return p->fire_ppb != e->fire_ppb || p->fire_spread_max != e->fire_spread_max
             ? str_printf(buf, size, "%s: fires x%d%%, up to %d spreads; winter stoves x%d%%", p->name, p->fire_ppb,
                          p->fire_spread_max, p->fire_winter_pct) : 0;
    }
    return 0;
}

/* ------------------------------------------------------------------ debug, save */
void hard_dump(FILE *f)
{
    int k;
    fprintf(f, "difficulty %s: sweden active %d reasons %d (jobs %d homes %d content %d) pull %d month %d total %d;",
            diff()->name, s_sw.active, s_sw.reasons, s_sw.jobs, s_sw.homes, s_sw.content, s_sw.pull, s_sw.month, s_sw.total);
    for (k = 0; k < HX_COUNT; k++) {
        int lots;
        s32 res = expect_lacking(k, &lots);
        fprintf(f, " %s lacking %d res (%d lots)", k == HX_LIBRARY ? "library" : k == HX_CLINIC ? "clinic" : "health centre", res, lots);
    }
    fprintf(f, "\n");
}

int hard_save(struct SaveFile *sf)
{
    save_begin_chunk(sf, "HARD", 1);
    save_u8(sf, s_sw.active); save_u8(sf, s_sw.jobs); save_u8(sf, s_sw.homes); save_u8(sf, s_sw.content); save_u8(sf, s_sw.reasons);
    save_s32(sf, s_sw.pull); save_s32(sf, s_sw.month); save_s32(sf, s_sw.total);
    save_end_chunk(sf);
    return 1;
}

int hard_load(struct SaveFile *sf)
{
    int ver;
    hard_reset();
    if (!load_find_chunk(sf, "HARD", &ver)) return 0;
    s_sw.active = (u8)load_u8(sf); s_sw.jobs = (u8)load_u8(sf); s_sw.homes = (u8)load_u8(sf); s_sw.content = (u8)load_u8(sf);
    s_sw.reasons = (u8)load_u8(sf);
    s_sw.pull = load_s32(sf); s_sw.month = load_s32(sf); s_sw.total = load_s32(sf);
    return load_ok(sf);
}
