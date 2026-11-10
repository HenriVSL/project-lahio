# Project Lähiö 1.2 tasks
Spec: `docs/DESIGN_1_2.md`. Rules: `docs/TASKS.md` "Rules for every task" + `docs/TASKS_M2.md` rules (shared files, **VM B
only via samba/vmb/ wrappers, vmb/vmlock for builds and runs**, VC6 reserved names, agents don't run git, debug from text).
Regression: a game that never builds the new plants must match 1.1 bit for bit (1.1 balance runs + world hashes, bots
told not to build new plants in those runs). New building ids are appended before `B_COUNT`. Notes go to `docs/HANDOFF.md`.

- **S1 Power sim (H+, owns src/ + tests/):** defs rows (coal_station new; peat/nuclear numbers, unlocks, variants = 1 once
  A1's art exists, keys are the sprite stems: `coal_station`, `peat_plant`, `nuclear_plant`, `oil_plant`), fuel rules in
  trade.cpp (station surcharge, peat immune), Rural approval for peat, nuclear BDF_NEEDS_SHORE + the reactor letter
  (DESIGN_1_2 3, saved), Cabinet advice + hindsight line, Guide page, tool menu order, old-save load test, balance bots
  that use the new plants (target: 60k needs 6-10 plants), BALANCE.md section 12.
- **A1 Power art (H+, owns art/):** coal station 3x3, peat 2x2, nuclear 3x3, oil 2x2 (stretch) per DESIGN_1_2 5; summer +
  winter, both zooms, smoke hotspots; regenerate sheets + headers; VRAM/heap delta in PERF.md; preview PNGs for the
  orchestrator in `art/preview/`.
- Then (orchestrator): integrate, VC6 build 0 warnings, visual check on VM B, RC package for the user's play-test.
