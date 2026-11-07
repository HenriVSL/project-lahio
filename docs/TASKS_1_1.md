# Project Lähiö 1.1 tasks
Spec: `docs/DESIGN_1_1.md` (+ DESIGN 15). Rules: `docs/TASKS.md` "Rules for every task" + `docs/TASKS_M2.md` rules (shared
files, **VM B only via samba/vmb/ wrappers with vmb/vmlock for builds and runs**, VC6 reserved names, agents don't run git,
debug from text, Easy must stay bit-identical to 1.0: the 1.0 balance runs and world hashes are the regression test).
- **D1 Difficulty (H+):** level setting (New Game, save, default Normal), parameter table, all hooks named in UI, era
  expectations (1975/1980, announced + grace), Sweden 1969-70, bot targets per level, Easy == 1.0 regression tests.
- **W1 Winter simulation (H+):** season calendar, demand peaks, November forecast + hard winters (1986-87 fixed), frozen pipes,
  T3 heat on winter peak, Winter ledger line (ploughing surcharge, frost repairs), newspaper/Cabinet winter lines, season hook
  for art swap (`season_changed`), Easy cosmetic only.
- **W2 Winter art (H+):** snow pass in gen_art (same page layout, piece table), both zooms; season residency swap in art.cpp
  (re-upload into the same surfaces at the month tick, 0 KB extra VRAM); INI winter_art=0; **mockups first** (summer vs
  winter, with overlays and zone view on snow) for the user.
- Then: B1 balance across levels (bots incl. winter forecast reading), P perf/real-hardware bench, polish → user play-test.
