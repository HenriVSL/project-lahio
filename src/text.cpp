/* text.cpp - English strings, sentences, formatting and name generators. PORTABLE. Owner: T09.
   Implements text.h. Tone (THEME 6.1): dry, affectionate Finnish deadpan; the joke is the system,
   never people. Finnish only for flavour words (house types, sauna, place names).

   Latin-1 escapes: a hex escape eats every following hex digit, so a letter a-f or a digit right after
   "\xe4" must start a new string literal ("M\xe4" "en..."). tests/test_text.cpp checks the name lists. */
#include "common.h"
#include "text.h"
#include "complex.h"
#include "util.h"
#include "world.h"
#include "tools.h"
#include "budget.h"
#include "fields.h"

static const char *const s_months[MONTHS_PER_YEAR] = { "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December" };

void text_init(void) {}

/* ================================================================== numbers */
int text_fmt_int64(char *buf, int size, s64 v)
{
    char tmp[40];
    int n = 0, i, len = 0, neg = v < 0;
    u64 u = neg ? (u64)(-v) : (u64)v;
    if (size <= 0) return 0;
    do {                                       /* digits backwards with a separator every 3 */
        if (n && n % 3 == 0) tmp[len++] = TEXT_THOUSANDS;
        tmp[len++] = (char)('0' + (int)(u % 10));
        u /= 10; n++;
    } while (u && len < 36);
    if (neg) tmp[len++] = TEXT_MINUS[0];
    for (i = 0; i < len && i < size - 1; i++) buf[i] = tmp[len - 1 - i];
    buf[i] = 0;
    return i;
}

int text_fmt_int(char *buf, int size, s32 v) { return text_fmt_int64(buf, size, (s64)v); }

int text_fmt_signed(char *buf, int size, s32 v)
{
    if (v > 0 && size > 1) { buf[0] = '+'; return 1 + text_fmt_int64(buf + 1, size - 1, (s64)v); }
    return text_fmt_int64(buf, size, (s64)v);
}

s64 text_money_display(Money v, int year)
{
    UNUSED(year);                  /* M7 (SCOPE_REVIEW): the old-markka x100 display is cut; only the 1963 joke stays */
    return (s64)v;
}

int text_fmt_money(char *buf, int size, Money v, int year)
{
    int n = text_fmt_int64(buf, size, text_money_display(v, year));
    return n + str_printf(buf + n, size - n, " mk");
}

int text_fmt_money_short(char *buf, int size, Money v, int year)
{
    s64 d = text_money_display(v, year), a = d < 0 ? -d : d;
    if (a >= 10000000) {                       /* "12.5M mk" */
        int tenths = (int)(a / 100000);
        return str_printf(buf, size, "%s%d.%dM mk", d < 0 ? TEXT_MINUS : "", tenths / 10, tenths % 10);
    }
    return text_fmt_money(buf, size, v, year);
}

int text_fmt_money_signed(char *buf, int size, Money v, int year)
{
    s64 d = text_money_display(v, year);
    if (d > 0 && size > 1) { buf[0] = '+'; return 1 + text_fmt_int64(buf + 1, size - 1, d); }
    return text_fmt_int64(buf, size, d);
}

int text_fmt_money_delta(char *buf, int size, Money v, int year)
{
    int n = text_fmt_money_signed(buf, size, v, year);
    return n + str_printf(buf + n, size - n, " mk");
}

int text_fmt_pct(char *buf, int size, int pct)
{
    int n = text_fmt_int(buf, size, pct);
    return n + str_printf(buf + n, size - n, "%%");
}

const char *text_month_name(int month) { return s_months[ICLAMP(month, 0, MONTHS_PER_YEAR - 1)]; }

int text_fmt_date(char *buf, int size, int year, int month)
{
    return str_printf(buf, size, "%s %d", text_month_name(month), year);
}

/* ================================================================== names of things */
const char *text_zone_name(int zone)
{
    static const char *const n[ZONE_COUNT] = { "Unzoned", "Houses", "Apartments", "Commercial", "Industry",
                                               "Forestry", "Farms" };   /* M3 */
    return (zone >= 0 && zone < ZONE_COUNT) ? n[zone] : "?";
}

const char *text_overlay_name(int ovl)
{
    static const char *const n[OVL_COUNT] = { "No overlay", "Power", "Water", "Pollution", "Land value",
        "Traffic", "Fire risk", "Fire cover", "Schools", "Health care", "Saunas", "Police", "Libraries",
        "Post and telephone", "Why not growing?", "Zones", "Catchment", "District heating" };   /* M5 */
    return (ovl >= 0 && ovl < OVL_COUNT) ? n[ovl] : "?";
}

const char *text_req_label(int req)
{
    static const char *const n[REQ_COUNT] = { "Road", "Power", "Water", "Water tower", "Precast elements",
        "School", "Sauna", "Health care", "Shop", "Telephone", "Land value", "Clean air", "Customers",
        "Workers", "Main road", "District heating", "Wood", "Milk", "Route to jobs" };   /* M3: wood, milk; M2: route */
    return (req >= 0 && req < REQ_COUNT) ? n[req] : "?";
}

const char *text_term_label(int kind, int id)
{
    static const char *const dt[DT_COUNT] = { "Open jobs", "Rural migrants", "Attractiveness", "Vacant homes",
        "Unemployed", "Shoppers' spending", "Existing shops", "Clearing trade (East)", "Western orders",
        "Local need", "Existing industry", "Taxes", "Decrees and events", "Regional migrants (railway)",
        "Rail freight" };   /* M2: station */
    static const char *const st[ST_COUNT] = { "Demand", "Land value", "Services", "Pollution", "Commute",
        "Noise", "Luck", "Decrees" };
    static const char *const lvt[LVT_COUNT] = { "Base value", "Lakeside", "Forest edge", "Rock", "Services",
        "Parks", "Road access", "Pollution", "Crime", "Noise", "Empty buildings nearby", "Polluted lake" };   /* M5 */
    static const char *const svc[SVC_COUNT] = { "Fire brigade", "Schools", "Health care", "Public saunas",
        "Police", "Libraries", "Post and telephone" };
    switch (kind) {
    case TERMK_DEMAND:     return (id >= 0 && id < DT_COUNT) ? dt[id] : "?";
    case TERMK_SCORE:      return (id >= 0 && id < ST_COUNT) ? st[id] : "?";
    case TERMK_LAND_VALUE: return (id >= 0 && id < LVT_COUNT) ? lvt[id] : "?";
    case TERMK_BUDGET:
        if (id >= BL_SVC_FIRST && id < BL_SVC_FIRST + SVC_COUNT) return svc[id - BL_SVC_FIRST];
        switch (id) {
        case BL_TAX_RES:      return "Residential tax";
        case BL_TAX_BUS:      return "Business tax";
        case BL_EXPORT:       return "Exports and trade";      /* M7: + the Western premium */
        case BL_GRANTS:       return "State grant";
        case BL_TIMBER:       return "Timber sales";
        case BL_ROADS:        return "Roads and bridges";
        case BL_NETWORKS:     return "Power lines";
        case BL_UTILITIES:    return "Plants and pumps";
        case BL_SPECIAL:      return "Town hall, parks, landmarks";
        case BL_INTEREST:     return "Loans and interest";     /* M7: loans taken and repaid */
        case BL_CONSTRUCTION: return "Construction";
        case BL_POLITICS:     return "Decrees and diplomacy";
        case BL_COMPANY:      return "Company contribution";     /* M6 */
        case BL_FUEL:         return "Fuel (oil, coal)";         /* M7 */
        }
        return "?";
    }
    return "?";
}

const char *text_place_reason(int place)
{
    switch (place) {
    case PLACE_OK:              return "OK";
    case PLACE_OUT_OF_MAP:      return "Off the map";
    case PLACE_WATER:           return "Can't build on water";
    case PLACE_ROAD:            return "A road is in the way";
    case PLACE_OCCUPIED:        return "Something is already here";
    case PLACE_PROTECTED:       return "Protected by the state";
    case PLACE_NEEDS_SHORE:     return "Must stand on a lake shore";
    case PLACE_NEEDS_RAPIDS:    return "Must stand on river rapids";
    case PLACE_UNIQUE:          return "There can be only one";
    case PLACE_NO_SLOTS:        return "Too many buildings";
    case REJ_NO_ROAD_REACH:     return "Too far from a road (3 tiles)";
    case REJ_LOCKED:            return "Not yet available";
    case REJ_NO_MONEY:          return "Not enough money";
    case REJ_BRIDGE_TOO_LONG:   return "Bridge too long (8 tiles at most)";
    case REJ_BRIDGE_NOT_STRAIGHT: return "Bridges must be straight";
    case REJ_NOTHING_TO_DO:     return "Nothing to do here";
    case REJ_PROTECTED_TILE:    return "Protected by the state";
    case REJ_WRONG_TERRAIN:     return "Wrong kind of ground";
    case REJ_NEEDS_RAIL:        return "The platform side must face the railway";   /* M2 */
    case REJ_BUS_NOT_ROAD:      return "Bus stops go on streets";
    case REJ_BUS_MAX:           return "Too many lines or stops";
    case REJ_BUS_NO_ROUTE:      return "No road route from the last stop";
    case REJ_CPX_WATER:         return "The log pond side (west) must touch a lake or river";   /* M6 */
    case REJ_CPX_RAIL:          return "Needs a railway within 2 tiles";
    case REJ_CPX_FAR:           return "Gifts go within 12 tiles of the works";
    case REJ_CPX_NO_GIFT:       return "No gift is waiting";
    case REJ_CPX_ROAD:          return "Needs a road within 2 tiles";
    }
    return "?";
}

const char *text_status_name(int ts)
{
    return (ts >= 0 && ts < TS_COUNT) ? g_statuses[ts].name : "?";
}

const char *text_era_name(int era)
{
    return (era >= 0 && era < ERA_COUNT) ? g_eras[era].name : "?";
}

const char *text_demand_name(int dem)
{
    static const char *const n[DEM_COUNT] = { "Residential", "Commercial", "Industrial" };
    return (dem >= 0 && dem < DEM_COUNT) ? n[dem] : "?";
}

const char *text_tool_name(int tool, int param)
{
    switch (tool) {
    case TOOL_NONE:       return "No tool";
    case TOOL_QUERY:      return "Query";
    case TOOL_ROAD:       return (param > 0 && param < ROAD_CLASS_COUNT) ? g_roads[param].name : "Road";
    case TOOL_POWER_LINE: return "Power line";
    case TOOL_ZONE:       return text_zone_name(param);
    case TOOL_DEZONE:     return "Dezone";
    case TOOL_BUILD:      return def_building(param)->name;
    case TOOL_BULLDOZE:   return "Bulldoze";
    case TOOL_PIPE:       return param ? "Remove pipes" : "Water pipe";   /* F1 */
    case TOOL_BUS_LINE:   return "Bus line";                             /* M2 */
    case TOOL_GIFT:       return complex_gift_tool_name();               /* M6 */
    }
    return "?";
}

const char *text_lot_status(int status)
{
    static const char *const n[LOT_STATUS_COUNT] = { "Under construction", "In use", "Abandoned", "On fire" };
    return (status >= 0 && status < LOT_STATUS_COUNT) ? n[status] : "?";
}

const char *text_mood_name(int mood)
{
    static const char *const n[PRES_MOOD_COUNT] = { "content", "stern", "delighted", "worried" };
    return n[ICLAMP(mood, 0, PRES_MOOD_COUNT - 1)];
}

int text_unlock_hint(char *buf, int size, int status_req, int era_year, int cur_year, int cur_status)
{
    int need_s = status_req > cur_status, need_y = era_year > cur_year;
    if (size > 0) buf[0] = 0;
    if (need_s && need_y) return str_printf(buf, size, "%s, %d", text_status_name(status_req), era_year);
    if (need_s) return str_printf(buf, size, "%s", text_status_name(status_req));
    if (need_y) return str_printf(buf, size, "%d", era_year);
    return 0;
}

/* ================================================================== sentences */
static int cur_year(void) { return sim_date()->year; }

/* a table name in the middle of a sentence: "Rintamamiestalo" -> "rintamamiestalo" (4 rotating buffers) */
static const char *lc(const char *name)
{
    static char buf[4][48];
    static int k;
    char *b = buf[k++ & 3];
    str_copy(b, name, 48);
    if (b[0] >= 'A' && b[0] <= 'Z') b[0] = (char)(b[0] + 32);
    else if ((u8)b[0] == 0xc4 || (u8)b[0] == 0xd6 || (u8)b[0] == 0xc5) b[0] = (char)((u8)b[0] + 32);
    return b;
}

static const char *town(void)
{
    const char *t = sim_settings()->town;
    return t[0] ? t : "the town";
}

int text_req_line(char *buf, int size, const ReqItem *it)
{
    const char *g = it->met ? GLYPH_CHECK : GLYPH_CROSS;
    const char *lab = text_req_label(it->req);
    char a[24], b[24];
    switch (it->req) {
    case REQ_ROAD:
        if (it->met) return str_printf(buf, size, "%s Road", g);
        if (it->have <= 0) return str_printf(buf, size, "%s Road (none within %d tiles)", g, (int)it->need);
        return str_printf(buf, size, "%s Road (%d tiles away, needs %d)", g, (int)it->have, (int)it->need);
    case REQ_POWER:
        if (it->met) return str_printf(buf, size, "%s Power", g);
        return str_printf(buf, size, "%s Power (%s)", g, it->why == 1 ? "brownout: plants overloaded" : "not connected");
    case REQ_WATER:      /* F1: "Water: supply short (Pumping station 440/440)" / "Water: no pipe within reach" */
        if (it->met) return str_printf(buf, size, "%s Water", g);
        if (it->why == 2) {
            const Lot *sl = world_lot(it->lot);
            text_fmt_int(a, sizeof(a), it->have);
            text_fmt_int(b, sizeof(b), it->need);
            return str_printf(buf, size, "%s Water: supply short (%s %s/%s)", g, sl ? def_building(sl->def)->name : "source", a, b);
        }
        return str_printf(buf, size, "%s Water: %s", g, it->why == 1 ? "supply short" : "no pipe within reach");
    case REQ_WATER_TOWER:
        if (it->met) return str_printf(buf, size, "%s Water tower", g);
        return str_printf(buf, size, "%s Water tower (outside a tower's pressure zone)", g);
    case REQ_ELEMENTS:      /* M5: have = left this month, need = this upgrade's share; why 0 = no factory output */
        if (it->met) return str_printf(buf, size, "%s Precast elements", g);
        if (it->why == 0) return str_printf(buf, size, "%s Precast elements (no element factory at work)", g);
        text_fmt_int(a, sizeof(a), it->have);
        text_fmt_int(b, sizeof(b), it->need);
        return str_printf(buf, size, "%s Precast elements: this month's are used up (%s left, needs %s)", g, a, b);
    case REQ_SCHOOL: case REQ_SAUNA: case REQ_HEALTH: case REQ_PHONE:
        if (it->req == REQ_PHONE && it->why == 2)               /* M5: before the telephone age */
            return str_printf(buf, size, "%s Telephone (needed from %d)", g, (int)it->need);
        if (it->req == REQ_PHONE && !it->met && !it->lot)       /* M5: the phone queue */
            return str_printf(buf, size, "%s Telephone (no exchange: the waiting list is a mere two years)", g);
        if (it->why == 1 || it->lot) {
            text_fmt_int(a, sizeof(a), it->have);
            text_fmt_int(b, sizeof(b), it->need);
            if (it->met) return str_printf(buf, size, "%s %s %s/%s", g, lab, a, b);
            if (it->have >= it->need) return str_printf(buf, size, "%s %s %s/%s (full)", g, lab, a, b);
            return str_printf(buf, size, "%s %s %s/%s (too far)", g, lab, a, b);
        }
        if (it->met) return str_printf(buf, size, "%s %s", g, lab);
        return str_printf(buf, size, "%s %s (none within reach)", g, lab);
    case REQ_SHOP:
        if (it->met) return str_printf(buf, size, "%s Shop nearby", g);
        return str_printf(buf, size, "%s Shop (none within %d tiles)", g, (int)it->need);
    case REQ_LAND_VALUE:
        return str_printf(buf, size, "%s Land value %d (needs %d)", g, (int)it->have, (int)it->need);
    case REQ_LOW_POLLUTION:
        return str_printf(buf, size, "%s Pollution %d (at most %d)", g, (int)it->have, (int)it->need);
    case REQ_CUSTOMERS:
        text_fmt_int(a, sizeof(a), it->have);
        text_fmt_int(b, sizeof(b), it->need);
        return str_printf(buf, size, "%s Customers %s/%s", g, a, b);
    case REQ_WORKERS:
        if (it->why == 0 && !it->met) return str_printf(buf, size, "%s Workers (no homes within reach)", g);
        return str_printf(buf, size, "%s Workers %d%% (needs %d%%)", g, (int)it->have, (int)it->need);
    case REQ_MAIN_ROAD:
        if (it->why == 1) {     /* M3: mechanised farms */
            if (it->met) return str_printf(buf, size, "%s Street or main road", g);
            return str_printf(buf, size, "%s Street or main road (only a %s)", g,
                              (it->have > 0 && it->have < ROAD_CLASS_COUNT) ? lc(g_roads[it->have].name) : "lane");
        }
        if (it->met) return str_printf(buf, size, "%s Main road", g);
        return str_printf(buf, size, "%s Main road (only a %s)", g,
                          (it->have > 0 && it->have < ROAD_CLASS_COUNT) ? lc(g_roads[it->have].name) : "lane");
    case REQ_HEAT:          /* M5: why 0 not connected, 2 heat plants full (have/need = used/output), 3 no spare for more */
        if (it->met) return str_printf(buf, size, "%s District heating", g);
        if (it->why == 2 || it->why == 3) {
            text_fmt_int(a, sizeof(a), it->have);
            text_fmt_int(b, sizeof(b), it->need);
            return str_printf(buf, size, "%s District heating: %s (heat plant %s/%s)", g,
                              it->why == 2 ? "short" : "no spare for a bigger block", a, b);
        }
        return str_printf(buf, size, "%s District heating (no heated pipe within reach)", g);
    case REQ_ROUTE:                 /* M2: traffic.cpp; have = commute in walk tiles */
        if (it->met) return str_printf(buf, size, "%s Route to jobs (%d tiles)", g, (int)it->have);
        return str_printf(buf, size, "%s Route to jobs (no road to the town's jobs)", g);
    case REQ_WOOD: case REQ_MILK:   /* M3: "Wood supply 82% (needs 75%)"; why 1 = after the upgrade */
        return str_printf(buf, size, "%s %s supply %d%%%s (needs %d%%)", g, lab, (int)it->have,
                          it->why == 1 ? " after the upgrade" : "", (int)it->need);
    }
    return str_printf(buf, size, "%s %s", g, lab);
}

static const char *svc_full_line(int svc)
{
    switch (svc) {
    case SVC_FIRE:    return "The fire brigade is stretched thin. Volunteers are asked to run faster.";
    case SVC_SCHOOL:  return "The school is full: pupils now learn in two shifts.";
    case SVC_HEALTH:  return "The clinic is full. The queue reaches the bus stop.";
    case SVC_SAUNA:   return "The public sauna is full. Bathers wait in their towels.";
    case SVC_POLICE:  return "The police station is overworked.";
    case SVC_LIBRARY: return "The library has a waiting list for every book.";
    case SVC_POST:    return "The telephone queue grows: new lines in eight years.";
    }
    return "A service is full.";
}

int text_event_topic(const SimEvent *e)
{
    if (e->type == SE_SERVICE_FULL) return SE_COUNT + (int)e->b;   /* one topic per service */
    return e->type;
}

int text_event(char *buf, int size, const SimEvent *e, int *severity)
{
    char a[32], b[32];
    const BuildingDef *d;
    int year = cur_year();
    *severity = e->severity;
    if (size > 0) buf[0] = 0;
    switch (e->type) {
    case SE_MONTH:
        return 0;
    case SE_YEAR:
        if (e->a == NEW_MARKKA_YEAR)
            return str_printf(buf, size, "New markka: all prices divided by 100. The coins are lighter, the "
                              "worries the same.");
        if (e->a == 1960) return str_printf(buf, size, "A new decade. Concrete is the future, says everyone with concrete.");
        return str_printf(buf, size, "The year %d begins. The New Year speech was, as ever, brief.", (int)e->a);
    case SE_LOT_BUILT:
        d = def_building(e->a);
        if (d->zone == ZONE_HOUSES) return str_printf(buf, size, "The first %s is finished. The neighbours have opinions about the paint.", lc(d->name));
        if (d->zone == ZONE_APARTMENTS) return str_printf(buf, size, "The first %s opens. Running water on every floor!", lc(d->name));
        if (d->zone == ZONE_COMMERCIAL) return str_printf(buf, size, "The first %s opens its doors. Coffee is rationed to one cup.", lc(d->name));
        if (d->zone == ZONE_INDUSTRY) return str_printf(buf, size, "The first %s starts work. The whistle can be heard across the lake.", lc(d->name));
        if (d->zone == ZONE_FARM) return str_printf(buf, size, "The first %s is worked. The milk cans stand at the gate by six.", lc(d->name));   /* M3 */
        if (d->zone == ZONE_FORESTRY) return str_printf(buf, size, "The first %s is built. Winter work in the forest, says the parish.", lc(d->name));
        return str_printf(buf, size, "A new %s is finished.", lc(d->name));
    case SE_LOT_UPGRADED:
        d = def_building(e->a);
        return str_printf(buf, size, "The first %s rises in %s. Progress, in tier %d.", lc(d->name), town(), (int)e->b);
    case SE_LOT_DOWNGRADED: {
        const Lot *l = world_lot(e->a);
        d = def_building(l ? l->def : B_NONE);
        if (l) return str_printf(buf, size, "A %s has seen better days.", lc(d->name));
        return str_printf(buf, size, "A building has seen better days.");
    }
    case SE_LOT_ABANDONED:
        return str_printf(buf, size, "A building stands empty. The family left a note: \"Gone to Sweden.\"");
    case SE_LOT_RESTORED:
        return str_printf(buf, size, "An empty building has new tenants. Curtains appear in the windows.");
    case SE_STATUS_UP:
        text_fmt_money(a, sizeof(a), e->b, year);
        if (e->b > 0)
            return str_printf(buf, size, "By presidential decree, %s is now a %s. A grant of %s follows.",
                              town(), lc(text_status_name(e->a)), a);
        return str_printf(buf, size, "By presidential decree, %s is now a %s.", town(), lc(text_status_name(e->a)));
    case SE_UNLOCK:
        if (e->b == 1) return str_printf(buf, size, "Now available: %s.", (e->a > 0 && e->a < ROAD_CLASS_COUNT) ? g_roads[e->a].name : "a new road");
        if (e->b == 2) return str_printf(buf, size, "Now available: %s zones.", text_zone_name(e->a));
        return str_printf(buf, size, "Now available: %s.", def_building(e->a)->name);
    case SE_EMIGRATION:
        text_fmt_int(a, sizeof(a), e->a);
        text_fmt_int(b, sizeof(b), e->b);
        if (e->a >= 100) return str_printf(buf, size, "%s residents left for Sweden this month (%s in all). Volvo is hiring.", a, b);
        return str_printf(buf, size, "%s residents left for Sweden this month (%s in all).", a, b);
    case SE_POWER_SHORTAGE:
        text_fmt_int(a, sizeof(a), e->a);
        if (e->b > 0) return str_printf(buf, size, "Power shortage: %s buildings in the dark. The plants are %d units short.", a, (int)e->b);
        return str_printf(buf, size, "Power shortage: %s buildings in the dark.", a);
    case SE_WATER_SHORTAGE:
        text_fmt_int(a, sizeof(a), e->a);
        if (e->b > 0) return str_printf(buf, size, "Water shortage: %s buildings have dry taps. %d units short.", a, (int)e->b);
        return str_printf(buf, size, "Water shortage: %s buildings have dry taps.", a);
    case SE_SERVICE_FULL:
        return str_printf(buf, size, "%s", svc_full_line(e->b));
    case SE_NO_MONEY:
        return str_printf(buf, size, "The treasury is empty. The bank manager has started calling.");
    case SE_BANKRUPT_WARN:
        text_fmt_money(a, sizeof(a), e->a, year);
        if (e->b == 1) return str_printf(buf, size, "Emergency loan taken: %s owed, at a rate nobody will mention.", a);
        return str_printf(buf, size, "Three months in the red. The ministry will send an emergency loan, and a lecture.");
    case SE_FIRE:           /* M5: b 0 = an outbreak starts; b >= 1: it is over, b - 1 lots lost, a residents displaced */
        if (e->b == 0) return str_printf(buf, size, "Fire! A wooden building is burning. The volunteers run for the hoses.");
        if (e->b == 1) return str_printf(buf, size, "The fire is out. No home was lost, only a good deal of sleep.");
        text_fmt_int(a, sizeof(a), e->a);
        if (e->b == 2) return str_printf(buf, size, "The fire is out. One building burned down; %s residents need a roof.", a);
        return str_printf(buf, size, "The fire is out. %d buildings burned down; %s residents need a roof. Concrete is "
                          "suddenly popular.", (int)e->b - 1, a);
    case SE_MILESTONE:
        text_fmt_int(a, sizeof(a), e->a);
        return str_printf(buf, size, "%s passes %s residents. A brass band is considered.", town(), a);
    }
    return 0;
}

static const char *problem_phrase(int req)
{
    switch (req) {
    case REQ_ROAD:          return "without road access";
    case REQ_POWER:         return "without power";
    case REQ_WATER:         return "without water";
    case REQ_WATER_TOWER:   return "short of water pressure";
    case REQ_ELEMENTS:      return "waiting for precast elements";
    case REQ_SCHOOL:        return "without a school";
    case REQ_SAUNA:         return "without a public sauna";
    case REQ_HEALTH:        return "without a clinic";
    case REQ_SHOP:          return "without a shop nearby";
    case REQ_PHONE:         return "without a telephone";
    case REQ_LAND_VALUE:    return "held back by low land value";
    case REQ_LOW_POLLUTION: return "breathing too much smoke";
    case REQ_CUSTOMERS:     return "short of customers";
    case REQ_WORKERS:       return "short of workers";
    case REQ_MAIN_ROAD:     return "without a main road";
    case REQ_HEAT:          return "without district heating";
    case REQ_WOOD:          return "short of wood";
    case REQ_MILK:          return "short of milk";
    case REQ_ROUTE:         return "with no road to the jobs";   /* M2 */
    }
    return "with a problem";
}

int text_problem(char *buf, int size, const SimProblem *p)
{
    char n[24], where[NAME_LEN];
    int len;
    if (p->residents > 0) {
        text_fmt_int(n, sizeof(n), p->residents);
        len = str_printf(buf, size, "%s residents %s", n, problem_phrase(p->req));
    } else {
        text_fmt_int(n, sizeof(n), p->count);
        len = str_printf(buf, size, "%s buildings %s", n, problem_phrase(p->req));
    }
    if (p->x >= 0 && p->y >= 0) {
        text_district_at(p->x, p->y, where, sizeof(where));
        len += str_printf(buf + len, size - len, " (%s)", where);
    }
    return len;
}

int text_president_line(char *buf, int size, int mood, u32 h)
{
    static const char *const content[] = { "Good, good.", "Carry on.", "Steady work.", "A sound plan.",
        "Proceed as planned.", "Order is a fine thing.", "The people are housed. Mostly.",
        "Fine. Now the next one." };
    static const char *const stern[] = { "The people expect results.", "This will not do.", "Explain this.",
        "I have written a letter.", "Again? Really?", "We will discuss this in the sauna." };
    static const char *const delighted[] = { "Splendid!", "Excellent work.", "The town grows!",
        "A fine day for Finland.", "Even the East is impressed.", "I may ski 30 km to celebrate." };
    static const char *const worried[] = { "Hm. The books.", "Where did the money go?", "The bank telephoned.",
        "People are leaving.", "Sweden is not a plan.", "We must not drift." };
    const char *s;
    switch (mood) {
    case PRES_MOOD_STERN:     s = stern[h % COUNTOF(stern)]; break;
    case PRES_MOOD_DELIGHTED: s = delighted[h % COUNTOF(delighted)]; break;
    case PRES_MOOD_WORRIED:   s = worried[h % COUNTOF(worried)]; break;
    default:                  s = content[h % COUNTOF(content)]; break;
    }
    return str_printf(buf, size, "%s", s);
}

/* ================================================================== name generators (THEME 8) */
static const char *const s_town_pre[] = {
    "Kuusi", "M\xe4nty", "Koivu", "Haapa", "Lepp\xe4", "Pihlaja", "Pet\xe4j\xe4", "Kataja", "Tammi", "Saarni",
    "Kivi", "Kallio", "Hiekka", "Savi", "Rauta", "Hopea", "Vaski", "Karhu", "Susi", "Hirvi", "Kettu", "Joutsen",
    "Kotka", "Haukka", "Lohi", "Hauki", "Ahven", "Mylly", "Kirkko", "Kartano", "Silta", "Ranta", "Saari", "Uusi",
    "Vanha", "Iso", "Pikku", "Ala", "Yl\xe4", "Keski", "Kylm\xe4", "Musta", "Valko", "Sini", "Puna", "Aurinko",
    "Kuu", "T\xe4hti" };
static const char *const s_town_suf[] = {
    "joki", "j\xe4rvi", "koski", "lahti", "niemi", "salmi", "saari", "vesi", "lampi", "virta", "m\xe4ki", "vaara",
    "harju", "kangas", "nummi", "suo", "kyl\xe4", "linna", "ranta", "selk\xe4", "kumpu", "kallio" };
/* real municipalities (THEME 8.1: the main town name should not be a real one) */
static const char *const s_town_real[] = {
    "Mustasaari", "Kivij\xe4rvi", "Pet\xe4j\xe4vesi", "Kirkkonummi", "Isojoki", "Saarij\xe4rvi", "Haapaj\xe4rvi",
    "Haapavesi", "Pihlajavesi", "Kylm\xe4koski", "Rautaj\xe4rvi", "Rautavaara", "Rantasalmi", "Kuusjoki",
    "Myllykoski", "Valkoj\xe4rvi", "Kotkaniemi" };

static const char *const s_dist_pre[] = {
    "Koivu", "Kallio", "Pihlaja", "M\xe4nty", "Kuusi", "Hauki", "Joutsen", "T\xe4hti", "Ruusu", "Kivi", "Mylly",
    "Hiekka", "Koti", "Kes\xe4", "Pelto", "Lehmus", "Tuomi", "Vaahtera", "Haapa", "Kanerva", "Kataja", "Rauha",
    "Lepp\xe4", "Kotka", "Peippo", "Kurki", "Aamu", "Ilta" };
static const char *const s_dist_suf[] = {
    "m\xe4ki", "lahti", "niemi", "kyl\xe4", "puisto", "rinne", "kallio", "harju", "nummi", "kangas", "mets\xe4",
    "laakso", "ranta", "saari", "kumpu", "kulma", "pelto", "kartano", "vainio", "niitty", "koti" };

/* street stems by planners' theme (THEME 8.3: compound nominative stems are always correct) */
#define STREET_THEMES 6
static const char *const s_street_theme[STREET_THEMES][8] = {
    { "Graniitti", "Gneissi", "Rapakivi", "Kvartsi", "Kiille", "Liuske", "Marmori", "Basaltti" },
    { "Koivu", "Kuusi", "M\xe4nty", "Pihlaja", "Tuomi", "Vaahtera", "Lehmus", "Haapa" },
    { "Tikka", "Peippo", "Kurki", "P\xf6ll\xf6", "Kiuru", "Sirkku", "Joutsen", "Kotka" },
    { "Kalevan", "Pohjolan", "Sammon", "Ainon", "Ilmarisen", "Kanteleen", "Tapion", "Louhen" },
    { "Kirkko", "Koulu", "Asema", "Tehtaan", "Puisto", "Tori", "Satama", "Myllyn" },
    { "Kallio", "L\xe4hde", "Puro", "Niitty", "Kanerva", "Mustikka", "Puolukka", "Sammal" } };
static const char *const s_street_suf[] = { "katu", "tie", "kuja", "polku", "rinne", "kaari", "raitti", "piha" };

static int is_real_town(const char *s)
{
    int i;
    for (i = 0; i < COUNTOF(s_town_real); i++) if (!strcmp(s, s_town_real[i])) return 1;
    return 0;
}

/* compound = prefix + suffix, rejecting doubled stems ("Kalliokallio", "Saarisaari") */
static int same_stem(const char *pre, const char *suf)
{
    int i;
    for (i = 0; pre[i] && suf[i]; i++) {
        int a = (u8)pre[i], b = (u8)suf[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (a == 0xc4) a = 0xe4;
        if (a == 0xd6) a = 0xf6;
        if (a != b) return 0;
    }
    return i >= 4;
}

void text_town_name(Rng *r, char *buf, int size)
{
    int tries;
    for (tries = 0; tries < 32; tries++) {
        const char *p = s_town_pre[rng_below(r, COUNTOF(s_town_pre))];
        const char *s = s_town_suf[rng_below(r, COUNTOF(s_town_suf))];
        if (same_stem(p, s)) continue;
        str_printf(buf, size, "%s%s", p, s);
        if (!is_real_town(buf)) return;
    }
    str_copy(buf, "Koivuniemi", size);
}

void text_district_name(u32 h, char *buf, int size)
{
    u32 a = hash_u32(h), b = hash_u32(h ^ 0x9e3779b9u);
    const char *p = s_dist_pre[a % COUNTOF(s_dist_pre)];
    const char *s = s_dist_suf[b % COUNTOF(s_dist_suf)];
    if (same_stem(p, s)) s = s_dist_suf[(b + 1) % COUNTOF(s_dist_suf)];
    str_printf(buf, size, "%s%s", p, s);
}

void text_street_name(u32 h, char *buf, int size)
{
    u32 a = hash_u32(h + 17), b = hash_u32(h * 31 + 5);
    const char *p = s_street_theme[(a >> 8) % STREET_THEMES][a % 8];
    str_printf(buf, size, "%s%s", p, s_street_suf[b % COUNTOF(s_street_suf)]);
}

static u32 district_hash(int x, int y)
{
    return hash_xy(x >> 4, y >> 4, sim_settings()->seed ^ 0xd157u);
}

void text_district_at(int x, int y, char *buf, int size)
{
    text_district_name(district_hash(x, y), buf, size);
}

void text_address(int x, int y, char *buf, int size)
{
    char dist[NAME_LEN], street[NAME_LEN];
    u32 dh = district_hash(x, y);
    u32 sh = hash_xy(x >> 2, y >> 2, dh);
    /* streets of one district share the planners' theme: pick the stem from the district's theme */
    const char *stem = s_street_theme[hash_u32(dh) % STREET_THEMES][sh % 8];
    text_district_name(dh, dist, sizeof(dist));
    str_printf(street, sizeof(street), "%s%s", stem, s_street_suf[(sh >> 4) % COUNTOF(s_street_suf)]);
    str_printf(buf, size, "%s %d, %s", street, 1 + (int)(hash_xy(x, y, 7) % 48), dist);
}

#ifdef KAUP_TESTS
/* test access to the name lists (tests/test_text.cpp checks escapes and lengths) */
int text_test_name_lists(const char *const **lists, int *counts, int max)
{
    int n = 0;
    if (max < 6) return 0;
    lists[n] = s_town_pre;  counts[n++] = COUNTOF(s_town_pre);
    lists[n] = s_town_suf;  counts[n++] = COUNTOF(s_town_suf);
    lists[n] = s_dist_pre;  counts[n++] = COUNTOF(s_dist_pre);
    lists[n] = s_dist_suf;  counts[n++] = COUNTOF(s_dist_suf);
    lists[n] = s_street_suf; counts[n++] = COUNTOF(s_street_suf);
    lists[n] = s_town_real; counts[n++] = COUNTOF(s_town_real);
    return n;
}
#endif
