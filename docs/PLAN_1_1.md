# Project Lähiö 1.1 plan: "Harder times"

User feedback on 1.0 (real hardware, 2026-11-08): "works well on real hardware; it's a bit easy." What made it easy:
**no real threats** and **late-game coasting**. Chosen headline features: **difficulty levels** and **winter**.
Same discipline as 1.0: small scope, polish over features, play-test when the whole thing works, sound out of scope.

## 1. Difficulty levels (Easy / Normal / Hard)
- New Game setting; **Easy = 1.0 balance** (unchanged, for relaxed play); Normal and Hard are new.
- One parameter table (defs), every lever named in tooltips/ledger: starting funds, upkeep multiplier, tax yield,
  demand/emigration sensitivity (the Sweden valve opens sooner), decline speed (tiers drop faster when requirements fail),
  service overload penalties, event severity, Eastern-relations drift, oil-shock magnitude, fire chance, winter severity.
- **Threats that bite on Normal/Hard:** shortages cause visible decline and abandonment within months; strikes, frosts
  and shocks cost real money/residents; the 1969-70 "great migration to Sweden" pulls residents unless jobs + housing hold.
- **Against late-game coasting:** rising expectations by era (e.g. tier 3 needs more services after 1975), car growth,
  winter peaks that grow with the city, upkeep pressure; the late game must still demand decisions.
- Measured, not guessed: balance-bot targets per level (planner comfortable on Easy, challenged on Normal, struggling
  but recoverable on Hard; naive player can finish 1990 on Normal with mistakes).

## 2. Winter (December-March)
- **Look:** snow pass in the art generator (G-buffer path kept since 1.0: snow on up-facing surfaces, white roofs,
  frozen lakes, ploughed roads). Only the active season's art is resident (same mechanism as the zoom swap; budget
  check per zoom × season; swap at season change, hidden behind the month tick).
- **Mechanics (simple, readable):** power and heat demand peak in winter (capacity margins matter); **hard-frost winters**
  announced in November in the newspaper (demand spike, pipe freezes if water/heat capacity is short); lakes freeze
  (cosmetic, maybe ice-road flavour). Snow ploughing: decide in the design check (genre reception: Cities: Skylines
  Snowfall's ploughing was called busywork): default = folded into road upkeep as a winter surcharge, no new micro-service.
- Newspaper/advisor lines for winter preparation ("The Cabinet respectfully notes that last winter the power works ran
  at 112 %...").

## 3. Process
1. Design check (short research agent): winter/difficulty mechanics in comparable games (CS Snowfall, Frostpunk, Banished,
   SimCity), what players called fun vs busywork → DESIGN section 15.
2. Wave A (parallel): difficulty system + era expectations | winter simulation | winter art (snow pass, season residency).
3. Wave B: balance across difficulties (bots incl. naive and Hard targets), performance on VM + ThinkPad bench.
4. Polish → user play-test (Normal and Hard) → release 1.1.
