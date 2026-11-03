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
