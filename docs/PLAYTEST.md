# Play-test feedback

## Session 1 (user, 2026-11-03, VM, Milestone 1 build)
1. **Zone visibility toggle**: zones should be a toggle (on/off) and stay visible while other tools/selections are
   active (not only while the zone tool is selected).
2. Start building: the red-brick 2x2 building is the **town hall** (auto-placed by the national road). → Make this
   obvious (name label/tooltip on hover, ticker line at start "The town hall stands ready, President").
3. **Water: switch to SimCity-style water pipes** (user preference): pumping stations feed a pipe network; buildings
   pass water on (adjacency conduction); not "mains inside roads". Research task: how SimCity (1989/2000/3000) did it.
4. **Water coverage/capacity must be visible**: how much a single pumping station covers, whether supply exceeds
   demand (spare capacity for more). User's understanding: the source feeds the network, reach is a distance, and
   higher load shrinks the covered radius. Overlay + query numbers needed.
5. **Bug: tool switching**: selecting Gravel road and then Industry did not deselect the road tool; selecting Homes did.
6. **Query tool is "handy" but the user didn't notice it at first** → discoverability: an opening ticker hint
   ("Right-click any building to see what it needs"), a hover tooltip on new lots, and maybe a short first-minutes
   hint chain (DESIGN_LESSONS 4.14).
7. **Zoom keys**: MacBook has no Page Up/Down (user plays through 86Box on a Mac). Add Mac-friendly zoom keys
   (e.g. `+`/`-` and `Z`, also numpad +/-) and on-screen zoom buttons (top bar or near the minimap). Check that the
   mouse wheel reaches the game through 86Box.
8. **Industry lot sizes leave gaps**: no 1x1 industry, so a 3-wide industrial zone leaves unbuilt tiles that break
   power/water conduction (buildings conduct, empty tiles don't). Fix: 1x1 industrial fillers (yards, timber stacks,
   workshops) and/or zoned-but-empty tiles conduct power/water (SimCity-style).
9. **Finnish industry themes**: industry should reflect the era: *Valtion Metalli* (pastiche of the state metal works
   Valmet), paper mills, sawmills; the forests (plentiful on the maps) should feed them. **Fields**: find a use
   (agriculture). **Industrial complexes / company towns**: a paper mill used a large area and the whole town and
   economy was built around it; can this be gamified, or is the current generic zone the better way?
10. User played enough for session 1 and quit (VM released for other sessions).
- Water design questions (docs/research/WATER.md, 6 questions) are still unanswered by the user.

## Decisions after session 1 (user: "do as you see fit; play-test when it's done", 2026-11-03)
All orchestrator recommendations accepted:
- Water (docs/research/WATER.md): pipes with source reach that shrinks under load, nearest first, spare flows on;
  buildings conduct; roads block unless piped; optional "lay pipes with new roads" (default off); pumping stations
  need power (wells don't); water tower = 200-unit source + pressure zone; droughts later with seasons.
- Industry (docs/research/INDUSTRY.md): 1x1 yard fillers + zoned land conducts power and water; industry kind chosen
  automatically by site (Query explains); forestry + farm zones (4-year regrowth); generic zones + optional complexes
  (paper mill first, 5,000 mk); patron gifts placed by the player, dry affectionate satire; Valtion Metalli by decree
  once politics exists (simple unlock before); company-town start option later.
- Next user play-test: when Milestone 2 works as a whole.

## Decisions 2026-11-03 (after the scope review)
- "Lay pipes with new roads" **default ON** (manual pipes still possible; option can be switched off).
- 1.0 = one presidency 1955-1990, ~30 min at speed 3 ("a sweet spot for retro play").
- **Next user play-test: after M5-M7 land, before the M8-M10 polish/balance pass.** Sound: no further work.

## Session 2 (user, 2026-11-04, VM B, pre-polish Milestone 2 build e4ef5ac)
1. **Progression requirements are invisible**: "what do I need to progress to church village? I cannot know in game what it
   requires. Maybe the newspaper should mention it?" Rule: Church village 500 / Market town 3,000 / Town 10,000 / Regional
   centre 40,000 / City 100,000 residents; with politics on, eligibility → the "Town rights" decree must be signed (2 authority).
   → Polish: population tooltip "Next: Church village at 500 (you have 412)" + progress bar; ticker + newspaper line when
   eligible ("Town rights await your signature, President"); greyed tools say the status AND its population; the decree
   should be easy to find (§ marker on the Approval field when it's waiting).
2. **Systems are undiscoverable**: the user didn't know politics existed (the President's desk) nor whether heating was
   implemented. → Polish: (a) a short "what's next" guide: the hint chain continues past the first minutes with milestone
   hints (status reached → what unlocked, e.g. "Market town! Water towers, the element factory (1962) and district heating
   are now available"; "Click Approval to open the President's desk"); (b) the status ceremony page lists unlocks and the
   NEXT goal; (c) fly-out tooltips for locked items: "Unlocks at Market town (3,000 residents) and 1960"; (d) a one-page
   in-game "Guide/Help" panel (F1?) listing the progression ladder and what each system is for.
3. **Power capacity not visible**: "where do I see the capacity for electricity?" Today only the Services panel row
   "Power used / supplied", the plant query "Power output", and the power overlay. → Polish: a top-bar Power field +
   tooltip like Water ("Power 1,180 / 1,500 (79%)"), click toggles the power overlay; ticker warning before a shortage.
4. **District heating's purpose unclear**: "what is the advantage of district heating?" Its only effect is gating
   Apartments T3. → Polish option 1 (preferred): explain it everywhere (T3 checklist, heat plant tooltip/fly-out text
   "lets apartment blocks grow to tier 3", unlock hint). Option 2 if it still feels like busywork: drop the heat plant and
   gate T3 on element factory + water-tower pressure. Decide with the user after the session.
5. **Power plant query lacks load/spare**: right-click on a plant shows only "Power output: N units", not how much is
   used or left. → Polish: plant query like the water source query: "Power 1,180 / 1,500 used (79%), 320 spare,
   serves N buildings", network total if several plants, brownout state; hover highlight of the plant's network.
6. **Services panel unreachable** (bug): no mini key or menu opens PANEL_SERVICES; only clicking a power/water/service
   shortage ticker line does. → Polish: a Services mini key (or open it from the top-bar Power/Water fields), and list it
   in the guide.
7. **Slow town** (user: 1971, ~2,750 pop (typo'd 1,750 first: the town is growing, not shrinking); autosave Jan 1971 "Mustakangas": 2,445, Church village). Diagnosis
   from the save (tests/test_zdump.cpp, KAUP_DUMPSAVE=...): power 543/500 short (7 lots), R target −49 (279 vacant homes),
   C demand +60 (unserved), I −40 (oversupplied), school 489/500, 30 apartment T1 lots (1,746 residents) blocked by
   REQ_WATER_TOWER + REQ_ELEMENTS, money −174/month, emigrated 860. → Design issues: (a) **apartment T2 gated behind Market
   town** (water tower + element factory) while a T1-apartment town struggles to reach 3,000 → lower the gates (water tower
   at Church village; element factory at Church village 1960s or T2 without elements) or make T1 apartments self-sufficient;
   (b) **the game doesn't tell the player the top actions**: a clear "Advisor" line/box: "Power short: build a plant",
   "Shops wanted: zone commercial", "School full" (top problems exist, but aren't surfaced plainly); (c) pacing: 2,750 in 1971 is far slower than the
   planner bot (3,000 by ~1958); find what the human did differently (zone mix, power, unaware of politics/decrees).
8. **User's idea, adopted: the newspaper is the advisor** ("to not make it too obvious, force the player to read it:
   'Government advises the President to do X', even though no one dared to advise Kekkonen ;)").
   → Polish design: January paper gets an "Editorial / From the Cabinet" box with the 1-3 most important problems as
   period-voice lines generated from sim_top_problems/checklist/demand/budget/status (power short, shops wanted, school
   full, money falling, town rights within reach or awaiting signature, apartment upgrades blocked by water tower/elements,
   heat needed for T3); rate-limited "special edition" for urgent issues (≤ 1 per game year); every advice line clickable
   (jump to place/panel); the President plaque quips ("Nobody advises me. But I read the papers.").
   Tone: deferential, deadpan ("Cabinet sources respectfully observe... Naturally the President is already aware.").
9. **Valtion Metalli / railway station impossible on rail-less maps** ("how can I build Valtion Metalli when there are no
   railways?"). Players can't build rail; mapgen adds a railway only on some maps. → Polish: (1) every new map gets a
   railway (mapgen option on by default, along the national road or another edge); (2) Valtion Metalli also accepts main-road
   access when no rail exists (old saves, rail-less maps); (3) locked/invalid placement tooltip states exactly what's missing.
   A player rail tool is post-1.0. **User confirmed: without a railway on the map it must work with roads.**
10. User got into debt without building the element factory ("you live and learn"). **Idea adopted: newspaper hindsight**
    ("at some point the paper should say 'the President should have done this'"). → Polish: occasional January "In
    hindsight" item on the past year's biggest missed opportunity (no element factory while apartments were blocked, power
    short for months, school overloaded, debt into the oil shock, town rights left unsigned), and a 1990 newsreel "What the
    history books will say" verdict with 2-3 such lines. Gentle, deadpan; detection from history data (era history,
    sim history, problem streaks).
11. **Positive: the humour works** ("I like the humor in the game so far. Good job. Dry finnish humor"). Keep the tone
    (THEME 6.1: affectionate, deadpan, about the system) in all new text: newspaper advice, hindsight, guide, hints.

## Session 3 (user, 2026-11-09, real hardware, 1.1 RC1)
- "It is okay. The winter is nice. Very late game is easy still but this provides good challenge; 87 and up is quite easy.
  But that is okay." → 1.1 approved for release.
- **Power plants**: the user's Koivukoski (1996, Regional centre, 58,193 pop) needed **32 coal plants** (16,000 units at 500
  each); nuclear is locked behind City (100k) and peat/nuclear have no art. Request: stronger plants. → **1.2 "power"
  update** (user chose: ship 1.1 now): large coal/oil station (3x3, ~2,000), peat plant (~1,000, domestic fuel), nuclear at
  Regional centre from 1977 (~4,000-5,000) with the "Eastinghouse" East/West/compromise choice, rebalance so a 60k city needs
  ~6-10 plants. Save: Shared/release/1.1rc1 save/.

## Session 4 (user, 2026-11-10, 1.2 RC1)
- **Bug: nothing can cross the railway** (roads, power lines refused on rail tiles; the town is cut in two). → 1.2 RC2:
  level crossings for gravel/street/main roads, motorway overpass over rail, power lines across rail, pipes under rail
  (tasks X1 sim, X2 art).
