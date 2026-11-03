# Project Lähiö: scope review (2026-11-03)

Reviewer brief (from the user, binding): *"Fewer features is easier to polish. I'd rather have a good polished
experience than a decades-long development project."* Also from the user: **sound is out of scope**. The user plays on
mute, so the existing sound module stays as it is, with no further sound work in the plan or in the polish list.

Inputs: DESIGN 0, 4-8 and 14, TASKS_M2, HANDOFF, PLAYTEST, BALANCE, research/DESIGN_LESSONS, INDUSTRY and WATER, the
M5-M7 headers as they stand now (`civic.h`, `complex.h`, `trade.h`, `era.h`), and new web research (sources in
section 6, cited as [n]). Where the sources are thin (a single forum thread, a fan wiki or an aggregator), the claim
is marked *(thin)*.

**Bottom line.** The core of Lähiö (zones with visible tiers, power, water, capacity services, transparent demand,
emigration to Sweden, preview and undo, overlays, the query checklist) is exactly the set of features the genre
praises. The risk is not a bad feature. It is the **number of interacting economic layers** that M5-M7 add at once:
clearing agreements, Western orders, hard currency, an oil index, three loan types, two complexes with owner moods,
heat, sewage, crime and fires. Each layer is reasonable on its own. Together they are the Cities: Skylines II lesson
(an economy players "could not understand or trust" [9]) and the Workers & Resources wall [8], and every layer
multiplies the M8 balance work. The verdict: **finish M5-M7 as thinner versions, then freeze features** and spend
the rest of 1.0 on play-tests and polish.

---

## 1. Every feature group: reception, fit, cost, verdict

Cost to polish: **S** = days of tuning and UI work; **M** = a task wave; **L** = several waves plus ongoing balance
risk. Fit = fit with a sandbox (no campaign), the Finland 1955-90 theme and the Win98 / P2-233 / 16 MB VRAM limits.

| Feature group | State | Genre reception | Fit | Cost | Verdict |
|---|---|---|---|---|---|
| Roads: gravel, street, main road | built | Good. Upgrade in place and drag preview are the modern standard (DESIGN_LESSONS 2.1) | core | S | **KEEP** |
| Motorway (junction-only links, no zone access) | built | Neutral. On a 1.5 km map it seldom matters. HANDOFF M2: the art draws side links that traffic ignores | weak (small map) | S-M | **SIMPLIFY**: fix the side-link art or hide the tool for 1.0 |
| Zones with 3 visible tiers (houses, apartments, commercial, industry) | built (+T3 in M5) | Good. Caesar III housing evolution is "a core strength" [16]; Anno tiers are the motivation engine | core, the wood-to-concrete story | M | **KEEP** |
| Power: plants + lines + conduction | built | Good, a simple and readable network | core | S | **KEEP** |
| Water pipes (SC2000 style) | built | Mixed. Edge (1993) called SC2000's "endless miles of underground water pipes" a chore [6]; CS2 moved pipes into roads and reviewers called that less busywork [7] *(thin)*. Our version (buildings conduct, reach shrinks with load, visible) avoids the worst of it | user decision | S | **KEEP**, but consider making "lay pipes with new roads" the **default ON** now that heat and sewage also run on pipes (user to decide) |
| Services with capacity (school, clinic, sauna, fire station, parks, church) | built | Good when the choice is readable. Capacity by road distance beats CS-style hearse and garbage chores [1] | core; the sauna is theme | S | **KEEP** |
| Demand bars with itemised tooltips, budget with 2 taxes, emigration to Sweden | built | Good. Opaque demand is a genre-wide complaint and ours is transparent (DESIGN_LESSONS 2.11) | the theme's best mechanic | S | **KEEP** |
| Tools with preview + undo, overlays, query checklist, hints, hover labels | built | Praised everywhere: preview before the click and undo remove fear (Islanders [17], Townscaper, Dorfromantik's "satisfying click" [18]) | core | S | **KEEP**, and make it the polish focus |
| Menus, saves, autosave, settings | built | expected | core | S | **KEEP** |
| Cosmetic cars, smoke, water shimmer | built | Good. Cosmetic life gives charm, and with no agent AI nobody can call it "stupid traffic" | core | S | **KEEP** |
| Sound | built (basic) | — | user plays muted | — | **FROZEN** as is (user) |
| Politics: 4 computed blocs, popularity, authority | built | Mixed. Tropico 6: "factions too can be more or less ignored if happiness is maintained", and elections carry "no real repercussion" [5]. Blocs work when they read the city (Tropico's good part) | strong theme | M | **KEEP**, but tune so that blocs *disagree* (M8: authority is too generous today) |
| Decrees (13, cap 5, cooldowns, repeal) | built | Good. Frostpunk's laws with cooldowns and explicit trade-offs are praised [10]; Tropico edicts turn into "checklists once you know the dominant set" | strong | S | **KEEP** the 13; add none |
| Letters (6, 1 per 6 months) | built | No direct comparable. They add a third verb next to decrees and cards | weak | S | **SIMPLIFY**: show them as one-off decrees in the Decrees tab (no separate tab); add no more |
| Event cards (15 + campaign) | built | Mixed. Tropico advisors' quips "wear thin" [5]; the CS Chirper nags (DESIGN_LESSONS 2.1) | good if rare | S | **KEEP**, with **one shared queue** for all card sources (see section 4) |
| Elections every 6 years, margin, emergency powers law | built | Mixed in Tropico (no stakes) [5]; we have no game over, so the margin must *bite* | satire centrepiece | S | **KEEP**: the margin's effect on authority must be felt |
| Eastern / Western meters | built | Good thematically; W&R's two currencies are its best idea (DESIGN_LESSONS 2.4) | strong | S | **KEEP** as two meters only (see trade) |
| Industry kinds chosen by site | built | Good. SC4's kinds-by-conditions are liked; the tax hacks were not (INDUSTRY 2) | strong | S | **KEEP** (the query explains each pick) |
| Forestry zone + regrowth | built | Good. Banished-style renewables are readable (INDUSTRY 2) | Finland = forest | S | **KEEP**, frozen |
| Farm zone (3 tiers, milk to dairies) | built | Mixed. CS Industries: "both a worthwhile and an unnecessary addition" [3]. Few farms reach T2 (HANDOFF M3) | user asked for a use for fields | S | **SIMPLIFY**: freeze it as is, no new farm features; fix the T2 rate only if trivial |
| Traffic assignment + congestion + road query | built | Good when explained. CS's car AI is the genre's top complaint, but ours is aggregate and explainable | fits the CPU budget | M (perf spikes: graph rebuild 11.8 ms) | **KEEP**; only performance work left (M9) |
| Bus lines (≤ 8 lines × 16 stops, depot) | built | Mixed. CS Mass Transit adds "layers of micromanagement" [2]; per-line budgets are "a lot of work" [2] *(thin)* | OK (Lada era) | S | **SIMPLIFY**: no per-line settings ever, make lines undoable (HANDOFF M2 open item); no trams/metro |
| Railway station (unique) | built | Neutral to good; one building, clear effect | theme | S | **KEEP** |
| Element factory caps apartment growth | M5 | Good. A construction-material cap is W&R's great planning mechanic (DESIGN_LESSONS 2.4) | pillar 5 | S | **KEEP**, but show it in the top bar or demand tooltip ("Elements 340/400") so it never reads as a mystery cap |
| Apartments T2-T3, Commercial T3 | M5 | Good (tier progression) | core | M (art in VRAM) | **KEEP** |
| Police + crime field | M5 | **Disliked.** The CS lead designer: crime "did not really create any meaningful choices for players, it just meant placing a few police stations" [1]. SC4's crime is an overlay chore [22] *(thin)* | weak (1960s Finnish town) | M (field, overlay, balance) | **SIMPLIFY**: police = a plain capacity service (land value, Workers/Intelligentsia), **no crime field, no crime overlay**. The medium-beer decree's "+crime" becomes "−land value near bars". CUT police entirely if that is easier |
| Library, post & telephone (phone queue), health centre | M5 | Neutral. Plain services; the phone queue is great satire | theme | S | **KEEP** (each is a def + checklist line) |
| District heating (on the water pipe graph) | M5 | No direct comparable (W&R heating is called fussy *(thin)*). Ours adds no new network to lay, which is what saves it | very Finnish | S-M | **KEEP**, but heat = a heat-plant source on the pipes plus flat fuel upkeep; the oil-index link may stay, with no other fuel logistics |
| Sewage works + lake pollution | M5 | Mixed. CS sewage-upstream-of-intake is a classic beginner trap [21] *(thin)*; ours has no outlet placement, so the trap mostly goes away | Water Act 1961, mill smell | M | **SIMPLIFY**: capacity + polluted lake tiles in the *water* overlay (no 4th overlay); no events or new art beyond a tint |
| Fires in wooden districts | M5 | Mixed. SC2000 players switch disasters off [20]; CS Natural Disasters "adds flavour, but not challenge" [4] | pillar 5 (wood burns) | M | **SIMPLIFY**: fires only where fire coverage is missing; spread capped (2-3 neighbours); always telegraphed by the fire-risk overlay; obeys "disasters off" |
| Paper mill complex + *patruuna* (stages, mood, gifts, demand cards, company-town rule, 5 risk events) | M6 | Good on paper: CS Industries' unique factories were liked, its logistics were not [3]. No comparable for the patron; Tropico-like satire works | user request; the best theme hook | L | **SIMPLIFY**: keep stages, mood, **gifts the player places** (charming, spatial) and the company-town rule; at most **3 demand cards** and **2 risk events** (paper slump, modernisation); cut strike, merger and mill fire (put a strike in the general card pool if wanted) |
| *Valtion Metalli* (product lines, ministry, 5-year ±25%) | M6 | No comparable; a second complex doubles test and balance work | theme | M | **SIMPLIFY**: a unique employer with jobs by decade and East exports; **no mood, gifts or own cards**; output halves in 1990 |
| Clearing trade (5-year agreements, quota, fulfilment, balance, spoiling credit, imports) | M7 | Good as an idea (W&R two currencies); risky as an economy: CS2's launch economy "not up to standard", players couldn't read it [9] | the theme | L | **SIMPLIFY**: keep the **agreement card every 5 years** (Modest/Standard/Ambitious) and **one balance meter** with a clear tooltip. Cut the consumer-goods and materials import programmes and the 5-yearly settlement rules from the UI (keep the number simple: exports − oil) |
| Western trade (price index, hard-currency reserve, licences, purchases) | M7 | No comparable; a third currency on top of mk and clearing | weak (opaque) | M | **SIMPLIFY**: Western = an I-demand multiplier + devaluation events only; **CUT the hard-currency reserve and purchases** (electronics unlocks by year + status, as before) |
| Oil shocks 1973 / 1979, oil collapse 1986 | M7 | Good. Telegraphed external shocks are the genre's late-game fuel (Against the Storm [13]; Banished's late game "let it down" without them [12]) | perfect | S | **KEEP** (1973 is the headline; 1979/1986 may be one line each) |
| Loans: domestic bond, Western bank loan, Eastern project credit | M7 | Mixed or disliked. SC2000 players: "loans are almost always a bad idea" vs. the bond-boom exploit [19] *(thin)*; the Eastern credit's "called in after 12 months" is a gotcha | weak in a sandbox | M | **SIMPLIFY**: **one** "Issue a bond" button + the existing emergency loan. Western and Eastern credit become event-card flavour |
| Town status by decree + ceremony | M7 | Good. Population milestones that unlock tools are CS's best teaching device (DESIGN_LESSONS 2.1) | strong | S | **KEEP**; the decree is offered automatically, one click |
| Eras + era events (devaluations, 1990 end of clearing) | M7 | Good, the structural cure for sameness [13] | core | S | **KEEP** |
| Old-markka display before 1963 (prices ×100) | M7 | No comparable. It hurts readability in exactly the first 8 years (the tutorial years) and doubles the width of every number | flavour only | S-M (every number path) | **CUT** the ×100 display; keep the 1963 newspaper joke |
| Newspaper each January | M7 | Good. SC2000's paper is its most fondly remembered flourish [14]; SC3000 swapped it for a ticker "that does not need clicking away" [14]; Anno 1800's paper was welcome [15] | charm | S-M | **KEEP**, but optional (INI/settings: show / ticker only); never auto-open during a drag |
| 1990 end-of-era newsreel | M7 | Good. Frostpunk-style judgement of your choices (DESIGN_LESSONS 2.6) | gives the sandbox a "finish" | S | **KEEP**, built from the newspaper renderer (no animation) |
| Graphs panel | M7 | Good, cheap readability | core | S | **KEEP** (5-6 series) |
| Landmarks (concert hall, TV tower, ski jump, presidential sauna, friendship statue) | M7 | Good. "Vanity projects" are something to want (DESIGN_LESSONS 4.14) and the money sink BALANCE asks for | strong | S | **KEEP** with plain effects; sauna diplomacy = one button with diminishing returns |
| Oil plant (coal art alias), peat, nuclear + reactor choice event | planned | — | nice | M-L (art, events) | **DEFER**: the oil plant only if it is a one-row alias; nuclear and peat post-1.0 |
| Winter/seasons, floods, frost winters | planned/stretch | — | — | L | **DEFER** (already the user's call) |
| Ageing housing / renovation, trams/metro, harbour, larger maps, scenarios, company-town start | stretch | Renovation is the classic late-game answer, but it is a new system | — | L | **DEFER** post-1.0 |

**Overlays.** The count is drifting past ten (power, water, pollution, land value, traffic, fire risk, each service,
demand, zones, wood, farms, and now crime, heat and sewage). **SIMPLIFY** to at most 9 on keys 1-9: heat and sewage
become sub-views of the water overlay (one key cycles water → heat → sewage), crime goes, and wood and farms merge into
one "resources" view.

**Checklist length.** Houses T3 and Apartments T3 now have 5-6 requirements each. Keep each tier at **5 or fewer
lines** and always show the first failing one in red. This is the most important readability rule left.

---

## 2. M5-M7 are mid-work: what to tell the running agents now

Most of the code exists already (`complex.cpp` ~1,500 lines, `era.cpp` ~1,070, `trade.cpp` ~730, `civic.cpp` ~830,
`fire.cpp` ~245). So "stop" means: **don't wire UI, art, script commands, tests or balance for the cut parts, and
delete or compile them out** rather than hiding them behind flags. Hidden code still costs save chunks, determinism
tests and M8 bot balance.

**M5: finish, minus crime.**
- Finish: element factory (+ top-bar or demand-tooltip readout), Apartments T2-3, Commercial T3, library, post &
  phone (houses T3 needs it), health centre, district heating, sewage works.
- Change: **police without a crime field** (a plain service with capacity; no crime overlay; `PM_CRIME_PCT` becomes
  land value or happiness). Lake pollution shows inside the water overlay. Fires: spread capped at 2-3 neighbours,
  only outside fire coverage, obey "disasters off".

**M6: finish the paper mill, thin out Valtion Metalli.**
- Finish: paper mill placement, stages, patron mood, the 6 gifts, the company-town rule, 3 demand cards, 2 risk
  events (slump, modernisation).
- Stop: strike, merger and mill-fire events; Valtion Metalli's mood, gifts, ministry cards and ±25% agreement swing.
  Keep it as a unique employer with jobs per decade, East exports and the 1990 halving.

**M7: finish the era spine, cut the economy branches.**
- Finish: eras and era events, 1973/1979 oil shocks, the 5-year clearing agreement card + one balance meter, town
  status by decree, the January newspaper (optional), graphs, landmarks, the 1990 newsreel.
- Cut: the old-markka ×100 display; the hard-currency reserve with its licences and purchases; the consumer-goods and
  materials import programmes; the Western and Eastern loans (keep one bond + the emergency loan). The Western price
  index stays only as the I-demand multiplier with the 1957/1967 devaluations.

**All three.** Every new card goes through one rate-limited queue (section 4, risk 3). Every new money or demand
term appears in the budget ledger or the demand tooltip by name, or it doesn't ship.

---

## 3. Proposed 1.0 scope

**1.0 = "one presidency, 1955-1990, about 1-3 hours".** At speed 3 a year is about 48 s, so a full run is roughly
30 minutes at top speed and about 2.5 hours at speed 1. That is a natural, Against-the-Storm-sized unit of play [13]
with a real ending (the 1990 newsreel), after which the sandbox simply continues.

The features: everything built in M1-M4 (roads without the motorway unless it gets fixed; zones with 3 tiers; power;
water pipes; capacity services; demand, budget and emigration; preview, undo, overlays and the checklist; politics
with blocs, 13 decrees with letters folded in, cards and elections; industry kinds, forestry and farms frozen; traffic
congestion; buses; the station) plus the thinned M5-M7: the element factory, apartment and commercial T3, library,
post & phone, health centre, police as a plain service, heating, sewage, capped fires, the paper mill with its
patron, a simple Valtion Metalli, clearing agreements + one balance meter, the oil shocks, one bond, town status
ceremonies, the era events, the newspaper, graphs, landmarks and the newsreel. **Nothing else until 1.0 ships.**

### Polish checklist (takes priority over any new system; no sound work, per the user)

**Onboarding**
- The first 10 minutes as one scripted hint chain (exists: 6 hints): road → houses → shop + workshop → plant → pipes
  + well → school + sauna, each ticked off in the ticker.
- Locked tools say "Unlocks at Market town".
- The query tool is discoverable (play-test 1).

**Feedback**
- Every money change floats ("−450 mk").
- Tier-ups get a brief visible pop (a construction stage, then a new sprite).
- The top-3 problems box names a place.
- Every ticker line jumps to its location.
- Every number shown is *delivered*, never nominal (WATER 1.4).

**Juice (visual)**
- Smoke scales with output; cars scale with car ownership.
- The water shimmer, and the President plaque's reactions.
- The patron's portrait mood.
- Snappy fly-outs; no frame hitch when a panel opens.

**Readability**
- ≤ 9 overlays, ≤ 5 checklist lines per tier.
- Each demand term named, with a single budget ledger.
- The clearing balance in one tooltip.
- Era tool unlocks announced once.
- The 640x480 panels re-checked with all M2 content.

**Performance**
- M9: split the traffic graph rebuild (11.8 ms) and the water graph change (7.3 ms).
- Hold 60 fps on BENCH20K with M5-M7 on; VRAM headroom at 800x600 (only ~0.5 MB free today).
- 1024 64-zoom art (narrower sheets) or officially "32-zoom only at 1024".

**Bugs**
- Save round-trip determinism (M1 open item).
- The windowed pause menu draws black.
- Motorway side links; bus lines not undoable.
- Politics money booked a month late.

**Balance (M8)**
- Authority too generous.
- Late-game money glut: the planner bot has 60k mk and a flat population by 1975 (BALANCE 2). Landmarks and the oil
  shock must bite.
- Each decree is worth considering at least once per game.
- Fires never wipe out more than a block.

**Play-tests**
- A user play-test **after M5-M7 land and before M8**: the user has only played the M1 build, and everything since
  is untested by a human.

---

## 4. Top 5 risks to "polished, not endless"

1. **Feature count outruns polish capacity.** About 25 interacting systems, each built by a different agent wave and
   balanced by bots, not by players. *Avoid:* declare a **feature freeze** once the trimmed M5-M7 land. New ideas go
   to a `docs/POST10.md` list, not into tasks. Each wave after that is bug, polish or balance only.
2. **An economy the player can't read** (the CS2 lesson [9], the W&R wall [8]). Clearing, Western orders, oil, the
   complexes, loans, decrees and politics all push the same I-demand and money numbers. *Avoid:* at most ~6 named
   terms in the I-demand tooltip; every money flow is a named ledger line; cut the third currency (hard currency).
   Any term that can't be explained in one tooltip line is cut.
3. **Card and notification overload** (Chirper, Tropico's quips [5]). Event cards, patron cards, trade offers,
   status decrees, letters and the January newspaper all want attention. *Avoid:* **one queue, at most 1 card per
   game year** across all sources (ceremonies and agreement offers included); the newspaper can be set to ticker
   only; nothing modal opens during a drag.
4. **The classic late game** (Banished [12], CS sameness, our own BALANCE: a flat population and a growing pile of
   money by 1975). *Avoid:* make 1990 the designed finish (newsreel + score-like summary), let the 1973 shock and the
   paper slump hit a lazy town, use the landmarks as real sinks, and accept that after 1990 it is a painting sandbox.
   Don't add renovation or ageing systems for 1.0.
5. **Minimum-spec limits erode quietly** (VRAM ~0.5 MB free at 800x600, the 1024 64-zoom art doesn't load, 7-12 ms
   graph spikes). Every new building type costs VRAM and every system costs sim time. *Avoid:* freeze art with the
   M4 set (Valtion Metalli's modules exist; no new art for cut features), give M9 a hard bar (BENCH20K with
   everything on at 60 fps at 800x600, speed 3 keeps up), and test on the X41 before calling 1.0.

---

## 5. Things deliberately *not* cut (and why)

- **Water pipes**: a user decision, and our version fixes SC2000's real problems (visible wet/dry, buildings conduct,
  more pipe never hurts). The only open question is the default of "lay pipes with roads".
- **Politics**: built, thematic, and it reads the city (Tropico's strength). Its weakness in Tropico (ignorable
  factions, no-stakes elections [5]) is a *tuning* problem for M8, not a reason to cut.
- **Paper mill and patron**: the user asked for it, it is the most distinctive thing in the game, and the
  gift-placing loop is spatial and charming. It loses only its event bloat.
- **Fires**: pillar 5 (wood burns, so concrete makes sense). They stay telegraphed, capped and switchable.

---

## 6. Sources

1. Colossal Order (K. Korppoo) on crime, GameWatcher 2015: https://www.gamewatcher.com/2015-03-09-colossal-order-is-reworking-cities-skylines-public-services-for-after-dark-expansion
2. CS Mass Transit: GameRevolution https://www.gamerevolution.com/?p=332152 ; PC Gamer https://www.pcgamer.com/uk/mass-transit-for-cities-skylines-may-be-the-most-enjoyable-expansion-yet ; per-line budgets, Steam *(thin)* https://steamcommunity.com/app/255710/discussions/0/1748980761805161788
3. CS Industries, GameSpot 7/10: https://gamespot.com/reviews/cities-skylines-industries-review-the-up-and-up/1900-6417029/
4. CS Natural Disasters: TheSixthAxis https://www.thesixthaxis.com/2016/11/29/cities-skylines-natural-disasters-review/ ; WCCFtech https://wccftech.com/review/cities-skylines-natural-disasters-review/
5. Tropico 6: TechRaptor https://techraptor.net/content/tropico-6-review ; Thumbsticks https://www.thumbsticks.com/tropico-6-review/
6. Edge (UK) #3, Dec 1993, SimCity 2000 *(single review)*: https://morguefile.wiki/wiki/images/c/c9/1993-12_Edge_%28UK%29_3_-_p32%2C33_%2836e5a82b%29.pdf
7. CS2 utilities in roads: VideoGamer review https://videogamer.com/reviews/cities-skylines-2-review ; Steam discussion *(thin)* https://steamcommunity.com/app/949230/discussions/0/3877095833474705050
8. Workers & Resources complexity, Notebookcheck: https://www.notebookcheck.net/The-Dark-Souls-of-City-Builders-Complex-strategy-game-drops-below-10-on-Steam.1379178.0.html
9. CS2 economy: PCGamesN https://www.pcgamesn.com/cities-skylines-2/broken-economy ; Colossal Order dev diary https://colossalorder.fi/news/economy-2-development-diary-1/
10. Frostpunk laws: Select Button https://selectbutton.com/reviews/frostpunk-review ; GamingBolt https://gamingbolt.com/frostpunk-review
11. Frostpunk 2 council "can get overwhelming", GamingBolt: https://gamingbolt.com/frostpunk-2-review-snowpiercer
12. Banished late game, PC Invasion: https://pcinvasion.com/banished-review/2
13. Against the Storm design, Game Developer: https://www.gamedeveloper.com/road-to-igf-2023/how-against-the-storm-managed-to-mix-city-building-and-roguelite-play
14. SimCity 2000 newspaper / SC3000 ticker: https://en.wikipedia.org/wiki/SimCity_2000 , https://en.wikipedia.org/wiki/SimCity_3000 , https://medium.com/@iain.mew/simcity-2000-3f9d933bba61
15. Anno 1800 newspaper, Checkpoint Gaming: https://checkpointgaming.net/reviews/2019/05/anno-1800-review-stunning-city-strategy/
16. Caesar III housing evolution: https://en.wikipedia.org/wiki/Caesar_III
17. Islanders, Into Indie Games: https://intoindiegames.com/reviews/islanders-review/
18. Dorfromantik, Game Informer: https://www.gameinformer.com/review/dorfromantik/falling-into-place
19. SimCity 2000 bonds *(forums, thin)*: https://www.dosgameclub.com/forums/topic/my-experience-with-sc2k/ , https://gog.com/forum/simcity_series/questions
20. SimCity 2000 disasters menu / switching off: https://www.ludo.guide/guide/simcity-2000/section-2-5-the-disasters-menu
21. CS sewage placement *(Steam threads, thin)*: https://steamcommunity.com/app/255710/discussions/0/611701360817049364
22. SC4 unused ordinances / crime abstraction *(fan wiki + essay, thin)*: https://new.tcrf.net/SimCity_4 , https://streets.mn/2020/06/11/thoughts-on-simcity/
- Earlier internal research reused: `docs/research/DESIGN_LESSONS.md` 2.1-2.11 and 4.9-4.14, `INDUSTRY.md` 2 and 7,
  `WATER.md` 1-2.
