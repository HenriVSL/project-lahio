/* test_text.cpp - text.cpp: number/money/date formatting, requirement lines, ticker sentences, president
   lines, name generators. Owner: T09. Writes tests/out/text_samples.txt (every sentence, for review). */
#include "test.h"
#include "util.h"
#include "text.h"
#include "world.h"
#include "sim.h"
#include "budget.h"
#include "tools.h"

int text_test_name_lists(const char *const **lists, int *counts, int max);

/* every byte must have a glyph in the T03 fonts (art/UI.md "Fonts") */
static int glyph_ok(int c)
{
    static const unsigned char extra[] = { 0xc4, 0xd6, 0xc5, 0xe4, 0xf6, 0xe5, 0xc9, 0xe9, 0xdc, 0xfc, 0xa7, 0xb0,
        0xbd, 0xb7, 0xd7, 0xab, 0xbb, 0x96, 0x97, 0x91, 0x92, 0x93, 0x94, 0x85, 0x95, 0 };
    int i;
    if (c >= 1 && c <= 5) return 1;
    if (c >= 32 && c <= 126) return 1;
    for (i = 0; extra[i]; i++) if (extra[i] == c) return 1;
    return 0;
}

static int font_ok(const char *s)
{
    for (; *s; s++) if (!glyph_ok((u8)*s)) return 0;
    return 1;
}

TEST(text_numbers)
{
    char b[64];
    text_fmt_int(b, sizeof(b), 0);         CHECK(!strcmp(b, "0"));
    text_fmt_int(b, sizeof(b), 999);       CHECK(!strcmp(b, "999"));
    text_fmt_int(b, sizeof(b), 12345);     CHECK(!strcmp(b, "12,345"));
    text_fmt_int(b, sizeof(b), 1000000);   CHECK(!strcmp(b, "1,000,000"));
    text_fmt_int(b, sizeof(b), -80);       CHECK(!strcmp(b, "\x96" "80"));
    text_fmt_int(b, sizeof(b), -1234);     CHECK(!strcmp(b, "\x96" "1,234"));
    text_fmt_int(b, sizeof(b), (s32)0x80000000u); CHECK(!strcmp(b, "\x96" "2,147,483,648"));
    text_fmt_signed(b, sizeof(b), 450);    CHECK(!strcmp(b, "+450"));
    text_fmt_signed(b, sizeof(b), 0);      CHECK(!strcmp(b, "0"));
    text_fmt_pct(b, sizeof(b), 85);        CHECK(!strcmp(b, "85%"));
    text_fmt_int(b, 4, 12345);             CHECK(strlen(b) == 3);   /* truncates, NUL-terminated */
}

TEST(text_money)
{
    char b[64];
    text_fmt_money(b, sizeof(b), 125, 1961);       CHECK(!strcmp(b, "125 mk"));       /* M7: no old-markka x100 display */
    text_fmt_money(b, sizeof(b), 125, 1963);       CHECK(!strcmp(b, "125 mk"));
    text_fmt_money(b, sizeof(b), -790, 1970);      CHECK(!strcmp(b, "\x96" "790 mk"));
    text_fmt_money_delta(b, sizeof(b), 450, 1970); CHECK(!strcmp(b, "+450 mk"));
    text_fmt_money_delta(b, sizeof(b), -45, 1955); CHECK(!strcmp(b, "\x96" "45 mk"));
    text_fmt_money_signed(b, sizeof(b), 12, 1955); CHECK(!strcmp(b, "+12"));
    text_fmt_money(b, sizeof(b), 2000000000, 1955); CHECK(!strcmp(b, "2,000,000,000 mk"));
    text_fmt_money_short(b, sizeof(b), 12500000, 1955); CHECK(!strcmp(b, "12.5M mk"));
    text_fmt_money_short(b, sizeof(b), 9999999, 1955); CHECK(!strcmp(b, "9,999,999 mk"));
    text_fmt_money_short(b, sizeof(b), -250000, 1970); CHECK(!strcmp(b, "\x96" "250,000 mk"));
    text_fmt_money_short(b, sizeof(b), -25000000, 1970); CHECK(!strcmp(b, "\x96" "25.0M mk"));
}

TEST(text_dates_and_labels)
{
    char b[64];
    int i, k;
    text_fmt_date(b, sizeof(b), 1961, 9);  CHECK(!strcmp(b, "October 1961"));
    CHECK(!strcmp(text_month_name(0), "January"));
    CHECK(!strcmp(text_month_name(99), "December"));
    for (i = 0; i < ZONE_COUNT; i++) CHECK(strcmp(text_zone_name(i), "?") != 0);
    for (i = 0; i < OVL_COUNT; i++) CHECK(strcmp(text_overlay_name(i), "?") != 0);
    for (i = 0; i < REQ_COUNT; i++) CHECK(strcmp(text_req_label(i), "?") != 0);
    for (i = 0; i < DT_COUNT; i++) CHECK(strcmp(text_term_label(TERMK_DEMAND, i), "?") != 0);
    for (i = 0; i < ST_COUNT; i++) CHECK(strcmp(text_term_label(TERMK_SCORE, i), "?") != 0);
    for (i = 0; i < BL_COUNT; i++) {
        CHECK(strcmp(text_term_label(TERMK_BUDGET, i), "?") != 0);
    }
    for (i = 0; i < PLACE_COUNT; i++) CHECK(strcmp(text_place_reason(i), "?") != 0);
    for (i = 0; i < TS_COUNT; i++) CHECK(strcmp(text_status_name(i), "?") != 0);
    for (i = TOOL_NONE; i < TOOL_COUNT; i++) CHECK(strcmp(text_tool_name(i, i == TOOL_ROAD ? ROAD_STREET : 1), "?") != 0);
    k = text_unlock_hint(b, sizeof(b), TS_MARKET_TOWN, 1960, 1958, TS_VILLAGE);
    CHECK(k > 0); CHECK(!strcmp(b, "Market town, 1960"));
    text_unlock_hint(b, sizeof(b), TS_MARKET_TOWN, 1955, 1958, TS_VILLAGE); CHECK(!strcmp(b, "Market town"));
    text_unlock_hint(b, sizeof(b), TS_VILLAGE, 1965, 1958, TS_VILLAGE);      CHECK(!strcmp(b, "1965"));
    CHECK(text_unlock_hint(b, sizeof(b), TS_VILLAGE, 1955, 1958, TS_TOWN) == 0);
}

TEST(text_sentences)
{
    FILE *f = fopen("out/text_samples.txt", "w");
    char b[256];
    int r, met, why, t, sev, m, k;
    SimSettings s;
    world_init();
    world_new(64, 64);
    sim_init();
    sim_default_settings(&s);
    str_copy(s.town, "Kivikoski", NAME_LEN);
    sim_new_city(&s);
    CHECK(f != NULL);
    fprintf(f, "== requirement lines (text_req_line)\n");
    for (r = 0; r < REQ_COUNT; r++) for (met = 0; met < 2; met++) for (why = 0; why < 2; why++) {
        ReqItem it;
        memset(&it, 0, sizeof(it));
        it.req = (u8)r; it.met = (u8)met; it.why = (u8)why;
        it.have = r == REQ_SCHOOL ? 312 : r == REQ_LAND_VALUE ? 31 : 2;
        it.need = r == REQ_SCHOOL ? 400 : r == REQ_LAND_VALUE ? 40 : 3;
        k = text_req_line(b, sizeof(b), &it);
        CHECK(k > 2);
        CHECK(b[0] == (met ? GLYPH_CHECK[0] : GLYPH_CROSS[0]));
        CHECK(font_ok(b));
        fprintf(f, "%s\n", b);
    }
    {
        ReqItem it;
        memset(&it, 0, sizeof(it));
        it.req = REQ_WATER; it.met = 0;
        text_req_line(b, sizeof(b), &it);
        CHECK(!strcmp(b, GLYPH_CROSS " Water: no pipe within reach"));   /* F1 */
        it.req = REQ_SCHOOL; it.met = 1; it.why = 1; it.have = 312; it.need = 400;
        text_req_line(b, sizeof(b), &it);
        CHECK(!strcmp(b, GLYPH_CHECK " School 312/400"));
        it.req = REQ_LAND_VALUE; it.met = 0; it.have = 31; it.need = 40;
        text_req_line(b, sizeof(b), &it);
        CHECK(!strcmp(b, GLYPH_CROSS " Land value 31 (needs 40)"));
    }
    fprintf(f, "\n== ticker (text_event), every SE_*\n");
    for (t = 0; t < SE_COUNT; t++) {
        for (k = 0; k < 2; k++) {
            SimEvent e;
            memset(&e, 0, sizeof(e));
            e.type = (u8)t; e.severity = SEV_WARN; e.x = -1; e.y = -1;
            switch (t) {
            case SE_YEAR: e.a = k ? 1963 : 1958; break;
            case SE_LOT_BUILT: case SE_LOT_UPGRADED: e.a = k ? B_APT_T1_3X1 : B_HOUSE_T2; e.b = 2; break;
            case SE_STATUS_UP: e.a = TS_MARKET_TOWN; e.b = k ? 5000 : 0; break;
            case SE_UNLOCK: e.a = k ? ROAD_MAIN : B_LIBRARY; e.b = k; break;
            case SE_EMIGRATION: e.a = k ? 340 : 40; e.b = 1240; break;
            case SE_POWER_SHORTAGE: case SE_WATER_SHORTAGE: e.a = 34; e.b = k ? 120 : 0; break;
            case SE_SERVICE_FULL: e.b = k ? SVC_SAUNA : SVC_SCHOOL; break;
            case SE_BANKRUPT_WARN: e.a = 12000; e.b = k; break;
            case SE_MILESTONE: e.a = 1000; break;
            }
            k += 0;
            m = text_event(b, sizeof(b), &e, &sev);
            if (t == SE_MONTH) { CHECK(m == 0); continue; }
            CHECK(m > 10);
            CHECK(font_ok(b));
            CHECK(sev == SEV_WARN);
            fprintf(f, "%-2d %s\n", t, b);
        }
    }
    fprintf(f, "\n== problems (text_problem)\n");
    for (r = 0; r < REQ_COUNT; r++) {
        SimProblem p;
        p.req = (u8)r; p.count = 12; p.residents = r == REQ_CUSTOMERS ? 0 : 340; p.x = 41; p.y = 17;
        k = text_problem(b, sizeof(b), &p);
        CHECK(k > 10);
        CHECK(font_ok(b));
        fprintf(f, "%s\n", b);
    }
    fprintf(f, "\n== president (text_president_line)\n");
    for (m = 0; m < PRES_MOOD_COUNT; m++) for (k = 0; k < 8; k++) {
        text_president_line(b, sizeof(b), m, (u32)k);
        CHECK(b[0] != 0);
        CHECK(font_ok(b));
        CHECK(strlen(b) <= 34);
        fprintf(f, "%-9s %s\n", text_mood_name(m), b);
    }
    fclose(f);
}

TEST(text_names)
{
    FILE *f = fopen("out/text_names.txt", "w");
    const char *const *lists[8];
    int counts[8], n, i, j, umlauts = 0;
    char b[64];
    Rng r;
    CHECK(f != NULL);
    n = text_test_name_lists(lists, counts, 8);
    CHECK(n == 6);
    for (i = 0; i < n; i++) for (j = 0; j < counts[i]; j++) {
        CHECK(font_ok(lists[i][j]));
        CHECK(strlen(lists[i][j]) < 14);
    }
    rng_seed(&r, 12345);
    fprintf(f, "== towns\n");
    for (i = 0; i < 300; i++) {
        text_town_name(&r, b, sizeof(b));
        CHECK(b[0] >= 'A' && b[0] <= 'Z');
        CHECK((int)strlen(b) < NAME_LEN - 1);
        CHECK(font_ok(b));
        CHECK(strcmp(b, "Mustasaari") != 0 && strcmp(b, "Kirkkonummi") != 0 && strcmp(b, "Rautavaara") != 0);
        CHECK(strcmp(b, "Kalliokallio") != 0 && strcmp(b, "Saarisaari") != 0 && strcmp(b, "Rantaranta") != 0);
        if (strchr(b, '\xe4') || strchr(b, '\xf6')) umlauts++;
        if (i < 40) fprintf(f, "%s\n", b);
    }
    test_log("town names with a/o umlauts: %d of 300", umlauts);
    CHECK(umlauts > 30);
    fprintf(f, "== districts, streets, addresses\n");
    for (i = 0; i < 40; i++) {
        text_district_name((u32)i * 7919u, b, sizeof(b));
        CHECK(font_ok(b)); CHECK(b[0] >= 'A' && b[0] <= 'Z'); CHECK((int)strlen(b) < NAME_LEN - 1);
        fprintf(f, "%-20s", b);
        text_street_name((u32)i * 104729u, b, sizeof(b));
        CHECK(font_ok(b)); CHECK((int)strlen(b) < NAME_LEN - 1);
        fprintf(f, "%-20s", b);
        text_address(i * 3, i * 5, b, sizeof(b));
        CHECK(font_ok(b));
        fprintf(f, "%s\n", b);
    }
    /* deterministic */
    {
        char a1[64], a2[64];
        Rng r1, r2;
        rng_seed(&r1, 77); rng_seed(&r2, 77);
        text_town_name(&r1, a1, sizeof(a1)); text_town_name(&r2, a2, sizeof(a2));
        CHECK(!strcmp(a1, a2));
        text_address(40, 17, a1, sizeof(a1)); text_address(40, 17, a2, sizeof(a2));
        CHECK(!strcmp(a1, a2));
    }
    fclose(f);
}
