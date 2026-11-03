/* poltab.cpp - politics data tables (decrees, letters, event cards) and politics strings. PORTABLE.
   Owner: M1. DESIGN 6, DESIGN_LESSONS 4.10, THEME 1.3 / 6 (tone: affectionate, dry, about the system;
   real people generic, no real parties or brands; the big neighbour in the East is weather, not a hero).

   Effects are explicit PolFx lists: the UI prints them with politics_fx_text before the player chooses.
   FX_MONEY values are x10 mk. Ids are stored in saves: append new rows only. */
#include "common.h"
#include "politics.h"
#include "text.h"
#include "sim.h"
#include "util.h"

#define B_W  BLOC_WORKERS
#define B_R  BLOC_RURAL
#define B_I  BLOC_INDUSTRIALISTS
#define B_S  BLOC_INTELLIGENTSIA
#define F_BLOC(b, v, m)  { FX_BLOC, (s8)(b), (s16)(v), (u8)(m) }
#define F_PM(p, v, m)    { FX_PM, (s8)(p), (s16)(v), (u8)(m) }
#define F_EAST(v)        { FX_METER, METER_EAST, (s16)(v), 0 }
#define F_WEST(v)        { FX_METER, METER_WEST, (s16)(v), 0 }
#define F_EAST_M(v)      { FX_METER_M, METER_EAST, (s16)(v), 0 }
#define F_AUTH(v)        { FX_AUTH, 0, (s16)(v), 0 }
#define F_MONEY(v10)     { FX_MONEY, 0, (s16)(v10), 0 }
#define F_MONEY_POP(v)   { FX_MONEY_POP, 0, (s16)(v), 0 }
#define F_MONEY_M(v)     { FX_MONEY_M, 0, (s16)(v), 0 }
#define F_ELECT(v)       { FX_ELECTION, 0, (s16)(v), 0 }
#define F_END            { FX_NONE, 0, 0, 0 }

/* ------------------------------------------------------------------ decrees (DESIGN_LESSONS 4.10) */
const DecreeDef g_decrees[DEC_COUNT] = {
    { "housing", "Housing production programme",
      "The state housing board finances element blocks by the hundred. Quantity first; the architects may weep later.",
      8, 1955, 0,
      { F_END },
      { F_PM(PM_APT_SCORE_ADD, 4, 0), F_PM(PM_CONSTRUCTION_PCT, -10, 0), F_PM(PM_ELEMENTS_PCT, 25, 0),
        F_BLOC(B_W, 6, 0), F_BLOC(B_S, -4, 0), F_BLOC(B_R, -3, 0) } },
    { "rent", "Rent regulation",
      "Landlords may charge what the ministry considers decent. The ministry considers very little decent.",
      6, 1955, 0,
      { F_END },
      { F_PM(PM_HAPPY_ADD, 5, 0), F_PM(PM_LV_GROWTH_PCT, -50, 0), F_PM(PM_COM_DEMAND_PCT, -5, 0),
        F_BLOC(B_W, 10, 0), F_BLOC(B_I, -6, 0), F_END } },
    { "beer", "Medium beer in grocery stores",
      "Medium beer leaves the state liquor shop for the corner grocer. The temperance society has been informed, by telegram.",
      5, 1965, 0,
      { F_END },
      { F_PM(PM_COM_DEMAND_PCT, 8, 0), F_PM(PM_TAX_INCOME_PCT, 3, 0), F_PM(PM_CRIME_PCT, 5, 0),
        F_BLOC(B_W, 4, 0), F_BLOC(B_R, -6, 0), F_END } },
    { "regional", "Regional development subsidy",
      "The countryside is paid to stay the countryside. Fewer migrants arrive, and the farmers send a thank-you card.",
      6, 1955, 0,
      { F_END },
      { F_PM(PM_MIGRATION_PCT, -40, 0), F_MONEY_M(-150), F_BLOC(B_R, 15, 0), F_BLOC(B_I, -2, 0), F_END } },
    { "school", "Comprehensive school reform",
      "One school for every child, nine years long. The teachers approve; the factory owners count the cost.",
      8, 1965, 0,
      { F_END },
      { F_PM(PM_SCHOOL_CAP_PCT, 20, 0), F_PM(PM_SCHOOL_UPKEEP_PCT, 30, 0), F_PM(PM_SCHOOL_LV_PCT, 100, 0),
        F_BLOC(B_S, 12, 0), F_BLOC(B_I, -4, 0), F_BLOC(B_W, 2, 0) } },
    { "fiveday", "Five-day work week",
      "Saturday becomes a day for the sauna, the market square and the summer cottage.",
      7, 1965, 0,
      { F_END },
      { F_PM(PM_COM_DEMAND_PCT, 10, 0), F_PM(PM_IND_DEMAND_PCT, -5, 0), F_PM(PM_EXPORT_PCT, -5, 0),
        F_BLOC(B_W, 10, 0), F_BLOC(B_I, -7, 0), F_END } },
    { "smokestack", "Smokestack standards",
      "Factories must fit filters and taller chimneys. The air improves; the order books grow thinner.",
      6, 1960, 0,
      { F_END },
      { F_PM(PM_IND_POLLUTION_PCT, -40, 0), F_PM(PM_IND_DEMAND_PCT, -15, 0), F_BLOC(B_S, 8, 0),
        F_BLOC(B_I, -12, 0), F_BLOC(B_R, 3, 0), F_END } },
    { "friendship", "Friendship treaty celebrations",
      "Brass bands, speeches and a week of films about tractors. Western visitors find the cinemas full.",
      5, 1955, 0,
      { F_EAST(10), F_WEST(-5), F_MONEY(-150), F_END },
      { F_EAST_M(1), F_MONEY_M(-60), F_BLOC(B_S, -3, 0), F_BLOC(B_W, 2, 0), F_END } },
    { "emergency", "Emergency powers law",
      "Parliament, by a qualified majority and in its wisdom, finds the next election an unnecessary inconvenience.",
      6, 1955, DF_ONESHOT,
      { { FX_SKIP_ELECTION, 0, 0, 0 }, F_AUTH(10), F_BLOC(B_S, -30, 36), F_WEST(-10), F_END },
      { F_END } },
    { "western", "Free-trade agreement with the West",
      "Tariffs fall on Western goods and on our sawn timber. Our Eastern friends read the agreement very carefully.",
      7, 1961, 0,
      { F_WEST(10), F_EAST(-6), F_END },
      { F_PM(PM_WESTERN_PCT, 25, 0), F_BLOC(B_I, 8, 0), F_BLOC(B_W, -2, 0), F_END } },
    { "homecoming", "Come home from Sweden",
      "Posters at every ferry terminal: the town needs your hands. Housing loans for returning families.",
      5, 1965, 0,
      { F_END },
      { F_PM(PM_EMIGRATION_PCT, -30, 0), F_MONEY_M(-120), F_BLOC(B_W, 4, 0), F_END } },
    { "press", "Responsible press guidelines",
      "Editors are reminded that some topics are best left to the weather page.",
      4, 1955, 0,
      { F_EAST(6), F_WEST(-3), F_END },
      { F_BLOC(B_S, -10, 0), F_END } },
    { "bonfire", "Midsummer bonfire subsidy",
      "The state contributes one cord of birch per village bonfire. A small decree for a long evening.",
      2, 1955, 0,
      { F_END },
      { F_MONEY_M(-40), F_BLOC(B_R, 4, 0), F_BLOC(B_W, 2, 0), F_END } },
    { "townrights", "Town rights by decree",
      "Hook for town status by decree (M7).",
      5, 1955, DF_HIDDEN | DF_ONESHOT,
      { F_END }, { F_END } },
    { "stateworks", "State metal works",
      "Hook for Valtion Metalli placed by decree (M6).",
      10, 1955, DF_HIDDEN | DF_ONESHOT,
      { F_EAST(5), F_END }, { F_END } },
};

/* ------------------------------------------------------------------ letters (THEME 6.2 "The Letter") */
const LetterDef g_letters[LET_COUNT] = {
    { "housing", "The Housing Board", "Construction schedules",
      "Dear Director,\nI drove past the new blocks on Sunday. They were not finished. I trust that by the time this "
      "letter reaches your desk, they will be.\nWith cordial regards,",
      3, { { FX_FASTTRACK, 0, 12, 0 }, F_BLOC(B_W, 3, 6), F_END } },
    { "editor", "The Editor, Maakunnan Sanomat", "Yesterday's leader column",
      "Dear Editor,\nI read your leader column with interest. On reflection you will surely find a calmer tone for "
      "tomorrow's edition. The weather has been fine.\nYours sincerely,",
      2, { { FX_SOFTEN, 0, 0, 0 }, F_BLOC(B_S, -2, 12), F_END } },
    { "ministry", "The Ministry of Trade and Industry", "Orders for our factories",
      "Dear Minister,\nOur town's factories stand ready and their workers are able. I am confident the ministry will "
      "find them a few orders before the autumn.\nYours,",
      3, { F_PM(PM_IND_DEMAND_PCT, 10, 6), F_BLOC(B_I, 4, 6), F_END } },
    { "finance", "The Ministry of Finance", "A modest grant",
      "Dear Minister,\nThe council informs me that the roads are muddy and the treasury dry. A modest grant would show "
      "the state's confidence in the province.\nYours,",
      4, { F_MONEY_POP(40), F_BLOC(B_S, -2, 6), F_END } },
    { "embassy", "The Ambassador", "An evening at the residence",
      "Your Excellency,\nIt would give me great pleasure to receive you at the residence on Friday. The sauna will be "
      "warm and the agenda short.\nWith respect,",
      3, { F_EAST(6), F_WEST(-2), F_END } },
    { "farmers", "The Farmers' Union", "The harvest and the town",
      "Dear Chairman,\nThe town grows, but it has not forgotten where its bread comes from. Please convey my greetings "
      "to the members, and to their cows.\nYours,",
      2, { F_BLOC(B_R, 6, 12), F_END } },
};

/* ------------------------------------------------------------------ event cards (THEME 1.3, 6.2) */
const EventDef g_events[EV_COUNT] = {
    { "frost", "Night frost in the East",
      "The big neighbour finds our new cabinet disagreeable. Trade talks are postponed indefinitely and the Ambassador "
      "has gone fishing.",
      3, { { "Ride it out", "The frost lingers. Clearing orders dry up.",
             { F_EAST(-8), F_PM(PM_CLEARING_PCT, -25, 12), F_BLOC(B_I, -5, 12), F_END } },
           { "Reshuffle the cabinet", "A new cabinet is formed by Thursday. Relations thaw.",
             { F_AUTH(-6), F_EAST(12), F_BLOC(B_S, -6, 12), F_END } },
           { "Invite the Ambassador to the sauna", "Agreements are reached at 90 degrees.",
             { F_MONEY(-80), F_EAST(6), F_END } } } },
    { "note", "A diplomatic note",
      "A note arrives proposing \"consultations\". Western newspapers print maps with arrows. Everyone looks at the "
      "President.",
      3, { { "Travel east for talks", "The consultations are postponed indefinitely. The rival candidate withdraws.",
             { F_AUTH(-4), F_EAST(10), F_WEST(-4), F_ELECT(6), F_END } },
           { "Consult the Western embassies", "Western friends express concern. Eastern friends express more.",
             { F_WEST(6), F_EAST(-12), F_END } },
           { "Say nothing and go skiing", "The President skis 30 km. The note waits on the desk.",
             { F_EAST(-3), F_BLOC(B_R, 3, 12), F_END } } } },
    { "strike", "General strike",
      "The unions have downed tools across the province. The buses stand still, the shops close at noon and the milk "
      "is late.",
      3, { { "Mediate a wage agreement", "Agreement by Sunday night. The employers sigh.",
             { F_MONEY_POP(-100), F_BLOC(B_W, 10, 12), F_BLOC(B_I, -5, 12), F_END } },
           { "Let it run its course", "Three weeks later the factories reopen, quieter than before.",
             { F_PM(PM_IND_DEMAND_PCT, -30, 3), F_PM(PM_COM_DEMAND_PCT, -10, 3), F_BLOC(B_W, -8, 12), F_END } },
           { "Back the employers", "The strike collapses. So does the mood on the factory floor.",
             { F_BLOC(B_I, 8, 12), F_BLOC(B_W, -15, 12), F_PM(PM_EMIGRATION_PCT, 20, 12), F_END } } } },
    { "delegation", "A visiting delegation",
      "Forty delegates arrive from a friendly industrial city in the East to study our progress, and our buffet.",
      3, { { "Full honours: brass band and banquet", "Toasts are proposed until the herring runs out.",
             { F_MONEY(-60), F_EAST(8), F_END } },
           { "Show them the new suburbs", "The delegation photographs every balcony.",
             { F_EAST(5), F_BLOC(B_W, 3, 12), F_BLOC(B_S, -2, 12), F_END } },
           { "A modest lunch at the town hall", "Coffee and pulla. Polite, and brief.",
             { F_EAST(2), F_END } } } },
    { "statue", "A friendship statue",
      "A twin town sends a bronze statue of a certain bald revolutionary, three metres tall, with a note: \"For your "
      "main square.\"",
      3, { { "Place it in the main square", "Unveiled in the rain. The Ambassador is visibly moved.",
             { F_EAST(10), F_BLOC(B_S, -8, 24), F_END } },
           { "Place it, discreetly, behind the bus garage", "Officially it stands \"in a prominent location\".",
             { F_EAST(3), F_BLOC(B_S, -2, 12), F_END } },
           { "Store it in the park depot", "It stands among the snow ploughs, gazing into the future.",
             { F_EAST(-6), F_BLOC(B_S, 4, 12), F_END } } } },
    { "sauna", "Sauna diplomacy",
      "The trade delegation hints that negotiations go better in the warmth. The presidential sauna is heated. "
      "(Each visit helps a little less than the last.)",
      3, { { "Heat the sauna", "The agreements were reached at 90 degrees.",
             { F_MONEY(-40), F_EAST(12), F_END } },
           { "Stay until five in the morning", "Legend says the night was long. The clearing list grew longer.",
             { F_AUTH(-3), F_EAST(16), F_PM(PM_CLEARING_PCT, 10, 12), F_END } },
           { "Decline politely", "The delegation takes the night train home.",
             { F_EAST(-2), F_END } } } },
    { "olympics", "An Olympic bid",
      "The sports federation wants the town to bid for the Games. The ski jump would be visible from three provinces.",
      3, { { "Bid for the Games", "Lost by four votes, but the world now knows where we are.",
             { F_MONEY(-500), F_WEST(10), F_BLOC(B_S, 6, 24), F_BLOC(B_W, 4, 24), F_PM(PM_COM_DEMAND_PCT, 10, 24), F_END } },
           { "Offer the ski jumping only", "A fine jump is built. Foreign journalists call the hotel \"authentic\".",
             { F_MONEY(-150), F_WEST(4), F_BLOC(B_R, 4, 12), F_END } },
           { "Decline: we have roads to build", "The federation is disappointed. The roads department is not.",
             { F_BLOC(B_S, -3, 12), F_END } } } },
    { "tv", "A television licence",
      "Two applications for a television licence: one promises Western serials and advertising, the other educational "
      "programmes about the forest industry.",
      2, { { "Licence the commercial channel", "Antennas sprout on every roof.",
             { F_WEST(6), F_EAST(-4), F_PM(PM_COM_DEMAND_PCT, 5, 24), F_BLOC(B_S, -2, 12), F_END } },
           { "State channel only", "Forty minutes of forestry every evening. The Intelligentsia approves.",
             { F_BLOC(B_S, 6, 12), F_EAST(2), F_END } },
           { NULL, NULL, { F_END } } } },
    { "scandal", "Scandal in the town council",
      "The building committee has awarded itself a summer cottage, timber included, at the town's expense.",
      3, { { "Demand resignations", "Three councillors resign; a fourth rediscovers his health.",
             { F_AUTH(-3), F_BLOC(B_S, 8, 12), F_END } },
           { "Appoint a committee to investigate", "The committee will report in due course.",
             { F_MONEY(-30), F_BLOC(B_S, -3, 12), F_END } },
           { "A letter to the editor: no story here", "The story disappears. The rumours do not.",
             { F_AUTH(2), F_BLOC(B_S, -8, 12), F_END } } } },
    { "swedenboat", "The Sweden boat is full",
      "Families leave for Swedish factories every week. \"The pay is better over there,\" says a young welder on the "
      "quay.",
      3, { { "Housing loans for young families", "Fewer suitcases on the quay this spring.",
             { F_MONEY(-200), F_PM(PM_EMIGRATION_PCT, -30, 24), F_BLOC(B_W, 5, 12), F_END } },
           { "A newspaper campaign: \"Stay, the town needs you\"", "The posters are admired, then folded into suitcases.",
             { F_PM(PM_EMIGRATION_PCT, -10, 12), F_BLOC(B_S, -2, 12), F_END } },
           { "Let them go", "Sweden thanks the town for the workers.",
             { F_BLOC(B_W, -6, 12), F_END } } } },
    { "festival", "A youth festival",
      "A world youth festival needs a host. The East offers to print the posters; the West offers to send jazz bands.",
      3, { { "Host the festival", "Ten thousand young people sing in four languages. Some windows are broken.",
             { F_MONEY(-100), F_EAST(8), F_WEST(-4), F_BLOC(B_S, -3, 12), F_END } },
           { "Hold a rival jazz festival", "The saxophones are heard across the bay.",
             { F_WEST(6), F_EAST(-6), F_BLOC(B_S, 5, 12), F_END } },
           { "Neither: it is haymaking season", "The meadows are mown on time.",
             { F_BLOC(B_R, 3, 12), F_END } } } },
    { "temperance", "The temperance society writes",
      "Two hundred signatures and an embroidered banner: medium beer has ruined the corner shop, the family and the "
      "Saturday dance.",
      2, { { "Thank them for their letter", "The letter is filed. The banner hangs in the society's hall.",
             { F_BLOC(B_R, -6, 12), F_END } },
           { "Repeal medium beer", "The grocers move the beer to the back room, then out of the shop.",
             { { FX_REPEAL, DEC_BEER, 0, 0 }, F_BLOC(B_R, 6, 12), F_BLOC(B_W, -6, 12), F_END } },
           { NULL, NULL, { F_END } } } },
    { "winter", "A hard winter",
      "Minus thirty-five for a fortnight. The boilers groan, the buses refuse to start and coal prices climb.",
      3, { { "An emergency heating fund", "Coal for every stove. The treasury feels the draught.",
             { F_MONEY_POP(-60), F_BLOC(B_W, 4, 12), F_END } },
           { "Ask the East for extra oil", "The oil arrives, with a note about friendship.",
             { F_EAST(-3), F_PM(PM_UTILITY_PCT, -20, 6), F_END } },
           { "The President's address: wear two sweaters", "The nation wears two sweaters.",
             { F_AUTH(1), F_BLOC(B_W, -4, 12), F_END } } } },
    { "tradefair", "A Western trade fair",
      "A trade fair across the Gulf invites our sawmills and machine shops. The stands are expensive; orders are not "
      "guaranteed.",
      2, { { "Send a delegation", "Orders for sawn timber and three paper machines.",
             { F_MONEY(-80), F_WEST(8), F_PM(PM_WESTERN_PCT, 15, 24), F_BLOC(B_I, 4, 12), F_END } },
           { "Stay home", "Our Eastern partners appreciate the loyalty.",
             { F_EAST(2), F_END } },
           { NULL, NULL, { F_END } } } },
    { "fishing", "The President goes fishing",
      "The President proposes a fishing trip to Lapland. The cabinet will manage, presumably.",
      2, { { "Go fishing", "A salmon of nine kilos. The cabinet managed, more or less.",
             { F_AUTH(4), F_BLOC(B_S, -2, 6), F_END } },
           { "Stay at the desk", "The desk is cleared. The ministries take note.",
             { F_BLOC(B_W, 2, 6), F_END } },
           { NULL, NULL, { F_END } } } },
    { "campaign", "The election approaches",
      "The electoral college meets in three months. The result is not in doubt; the margin is.",
      3, { { "Let the record speak", "The President cuts ribbons and says little.",
             { F_END } },
           { "Campaign across the province", "Coffee and speeches in every parish hall.",
             { F_MONEY_POP(-50), F_AUTH(-4), F_ELECT(8), F_END } },
           { "Persuade a rival to withdraw", "The rival withdraws, citing his health and his garden.",
             { F_AUTH(-8), F_ELECT(12), F_BLOC(B_S, -10, 24), F_END } } } },
};

/* ------------------------------------------------------------------ names and labels */
const char *politics_bloc_name(int bloc)
{
    static const char *const n[BLOC_COUNT] = { "Workers", "Rural", "Industrialists", "Intelligentsia" };
    return (bloc >= 0 && bloc < BLOC_COUNT) ? n[bloc] : "?";
}

const char *politics_bloc_finnish(int bloc)
{
    static const char *const n[BLOC_COUNT] = { "Ty\xf6v\xe4ki", "Maalaiset", "Patruunat", "Sivistyneist\xf6" };
    return (bloc >= 0 && bloc < BLOC_COUNT) ? n[bloc] : "?";
}

const char *politics_meter_name(int meter)
{
    return meter == METER_EAST ? "Eastern relations" : meter == METER_WEST ? "Western trade" : "?";
}

const char *politics_reason_label(int pr)
{
    static const char *const n[PR_COUNT] = { "Base", "Jobs", "Unemployment", "Families without a home",
        "Emigration to Sweden", "Resident tax", "Business tax", "Services", "New housing", "Forest cleared",
        "Fast growth", "Power supply", "Export orders", "Commuting", "Labour shortage", "Pollution", "Land value",
        "Culture and parks", "Concrete suburbs", "Western trade", "Eastern relations", "Decrees", "Recent events" };
    return (pr >= 0 && pr < PR_COUNT) ? n[pr] : "?";
}

static int pm_is_add(int pm) { return pm == PM_HAPPY_ADD || pm == PM_APT_SCORE_ADD; }

const char *politics_pm_label(int pm)
{
    static const char *const n[PM_COUNT] = { "Rural migration", "Emigration", "Commercial demand", "Industrial demand",
        "Clearing orders", "Western orders", "Happiness", "Apartment growth", "Land value rises",
        "Industrial pollution", "School capacity", "Schools in land value", "Tax income", "School upkeep",
        "Construction costs", "Export income", "Plant and pump costs", "Crime", "Element output" };
    return (pm >= 0 && pm < PM_COUNT) ? n[pm] : "?";
}

static int year_now(void) { return sim_date()->year; }

static Money money_pop(int v)
{
    s32 pop = IMAX(300, sim_stats()->population);
    return (Money)((s64)v * pop / 100);
}

int politics_fx_text(char *buf, int size, const PolFx *fx)
{
    char a[40], m[24];
    m[0] = 0;
    if (fx->months) str_printf(m, sizeof(m), " for %d months", fx->months);
    switch (fx->kind) {
    case FX_BLOC:
        text_fmt_signed(a, sizeof(a), fx->value);
        return str_printf(buf, size, "%s %s%s", politics_bloc_name(fx->target), a, m);
    case FX_METER:
        text_fmt_signed(a, sizeof(a), fx->value);
        return str_printf(buf, size, "%s %s", politics_meter_name(fx->target), a);
    case FX_METER_M:
        text_fmt_signed(a, sizeof(a), fx->value);
        return str_printf(buf, size, "%s %s a month", politics_meter_name(fx->target), a);
    case FX_AUTH:
        text_fmt_signed(a, sizeof(a), fx->value);
        return str_printf(buf, size, "Authority %s", a);
    case FX_MONEY:
        text_fmt_money_delta(a, sizeof(a), (Money)fx->value * 10, year_now());
        return str_printf(buf, size, "%s now", a);
    case FX_MONEY_POP:
        text_fmt_money_delta(a, sizeof(a), money_pop(fx->value), year_now());
        return str_printf(buf, size, "%s now", a);
    case FX_MONEY_M:
        text_fmt_money_delta(a, sizeof(a), fx->value, year_now());
        return str_printf(buf, size, "%s a month", a);
    case FX_PM:
        text_fmt_signed(a, sizeof(a), fx->value);
        return str_printf(buf, size, "%s %s%s%s", politics_pm_label(fx->target), a, pm_is_add(fx->target) ? "" : "%", m);
    case FX_ELECTION:
        text_fmt_signed(a, sizeof(a), fx->value);
        return str_printf(buf, size, "Election margin %s", a);
    case FX_SKIP_ELECTION:
        return str_printf(buf, size, "Next election postponed by %d years", POL_TERM_YEARS);
    case FX_REPEAL:
        return str_printf(buf, size, "Repeals: %s", g_decrees[ICLAMP(fx->target, 0, DEC_COUNT - 1)].name);
    case FX_FASTTRACK:
        return str_printf(buf, size, "Up to %d buildings finished at once", fx->value);
    case FX_SOFTEN:
        return str_printf(buf, size, "Softens the worst grievance");
    }
    buf[0] = 0;
    return 0;
}

const char *politics_verdict_text(int verdict)
{
    if (verdict < 0) return "A narrow win: 151 is a majority, the newspapers remind everyone.";
    if (verdict > 0) return "A landslide. The cabinet rearranges its furniture accordingly.";
    return "A comfortable majority. Business as usual.";
}

int politics_ds_text(char *buf, int size, int ds, int dec)
{
    const DecreeDef *d = &g_decrees[ICLAMP(dec, 0, DEC_COUNT - 1)];
    switch (ds) {
    case DS_OK:           return str_printf(buf, size, "Ready to sign");
    case DS_ACTIVE:       return str_printf(buf, size, "In force");
    case DS_COOLDOWN:     return str_printf(buf, size, "Cooling off: %d months", politics_decree_cooldown(dec));
    case DS_NO_AUTHORITY: return str_printf(buf, size, "Needs %d authority", d->cost);
    case DS_NO_SLOT:      return str_printf(buf, size, "All %d cabinet slots are taken", POL_SLOTS);
    case DS_TOO_EARLY:    return str_printf(buf, size, "Not before %d", d->year_from);
    case DS_GAP:          return str_printf(buf, size, "The cabinet is drafting: %d months", politics_decree_gap());
    case DS_ALREADY:      return str_printf(buf, size, "Already in force for this term");
    case DS_OFF:          return str_printf(buf, size, "Politics is off");
    default:              return str_printf(buf, size, "Not available");
    }
}

int politics_msg_text(char *buf, int size, const PolMsg *m, int *severity)
{
    int sev = SEV_POLITICAL;
    buf[0] = 0;
    switch (m->type) {
    case PMSG_EVENT:
        str_printf(buf, size, "\xa7 %s: a matter awaits the President.", g_events[ICLAMP(m->a, 0, EV_COUNT - 1)].title);
        break;
    case PMSG_EVENT_EXPIRED: {
        const EventDef *e = &g_events[ICLAMP(m->a, 0, EV_COUNT - 1)];
        str_printf(buf, size, "The cabinet decided for you: %s (%s).", e->title, e->ch[ICLAMP(m->b, 0, POL_CHOICES - 1)].label);
        break; }
    case PMSG_CAMPAIGN:
        str_printf(buf, size, "\xa7 The electoral college meets in January %d.", m->a);
        break;
    case PMSG_ELECTION:
        str_printf(buf, size, "Election night: %d of 300 electors. %s", m->a,
                   m->b < 0 ? "A narrow win." : m->b > 0 ? "A landslide!" : "A comfortable majority.");
        break;
    case PMSG_ELECTION_SKIPPED:
        str_printf(buf, size, "No election in %d: the emergency powers law remains in force.", m->a);
        break;
    case PMSG_FROST:
        str_printf(buf, size, "Night frost: relations with the East are cooling (%d).", m->a);
        sev = SEV_WARN;
        break;
    case PMSG_BLOC_ANGRY:
        str_printf(buf, size, "The %s are restless: approval %d.", politics_bloc_name(m->a), m->b);
        sev = SEV_WARN;
        break;
    case PMSG_DECREE_SIGNED:
        str_printf(buf, size, "\xa7 Decree signed: %s.", g_decrees[ICLAMP(m->a, 0, DEC_COUNT - 1)].name);
        break;
    case PMSG_DECREE_REPEALED:
        str_printf(buf, size, "Decree repealed: %s.", g_decrees[ICLAMP(m->a, 0, DEC_COUNT - 1)].name);
        break;
    case PMSG_LETTER:
        str_printf(buf, size, "A letter leaves the President's desk for %s.", g_letters[ICLAMP(m->a, 0, LET_COUNT - 1)].to);
        break;
    case PMSG_CHOICE: {
        const EventDef *e = &g_events[ICLAMP(m->a, 0, EV_COUNT - 1)];
        const char *r = e->ch[ICLAMP(m->b, 0, POL_CHOICES - 1)].result;
        str_copy(buf, r ? r : "", size);
        break; }
    case PMSG_AUTH_FULL:
        str_printf(buf, size, "Authority at its limit: the cabinet awaits decrees.");
        sev = SEV_INFO;
        break;
    default:
        return 0;
    }
    if (severity) *severity = sev;
    return (int)strlen(buf);
}
