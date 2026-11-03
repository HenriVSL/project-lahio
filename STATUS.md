# Project Lähiö: status

**Project Lähiö** ("A Finnish town, 1955-1990"; working title KAUPUNKI, folder `dev/lahio`, exe `lahio.exe`). 1960s Finland reconstruction-era isometric city builder for Win98 (VC6, DirectDraw).
Owner session: 86box-86. Started 2026-11-02.

## Phase 1: research + probes (done)
| Task | Output | State |
|---|---|---|
| Theme/history research | `docs/research/THEME.md` | done |
| City-builder design lessons | `docs/research/DESIGN_LESSONS.md` | done |
| Isometric DirectDraw render probe (uses VM lock `86box-86-probe`) | `probe/RESULTS.md` | done |
| Art style exploration (2-3 variants + mockups) | `art/explore/STYLE.md`, `art/explore/mockup_*.png` | done |

## Phase 2: design document (draft done, awaiting user review)
`docs/DESIGN.md` rev 1 + `docs/TASKS.md` (Milestone 1 = vertical slice, waves T00-T13).

## Phase 3: Milestone 1 (user approved design 2026-11-03)
- Wave 0 T00 interfaces: done.
- Wave 1 done: T01 engine, T02 world art, T03 UI art+font, T04 simulation core. T07 sound done.
- Wave 2 done: T05 renderer, T06 map generator (+ river art). T09 UI module done.

## User decisions (2026-11-02)
- **Sandbox only** for now: no story/campaign mode. Themes welcome as systems (USSR clearing trade, emigration to Sweden, etc.).
- **President name is chosen by the player** (English allowed). Use "Kekkola" as the default in testing.
- **Winter/seasons: later**, not in v1.
- **Art: muted, not too colourful** (the saturated test green was rejected). Art quality and engine quality are the top priorities.
- **Cars: Lada 1200 (VAZ-2101) shaped**, clear boxy silhouette, many period colours (cars may be more colourful than the muted terrain).
- **Art style: variant C (Nordic hybrid)**. **UI: teak & bakelite** (as in the mockups).
- **Language: English** (UI, text, news). Keep only a few Finnish flavour words, e.g. house types (rintamamiestalo, lamellitalo, pistetalo), sauna. The font still needs ä/ö/å for those.
- **Repo**: https://github.com/HenriVSL/project-lahio (public).
- **Spec**: P2-233 VM = minimum spec; real targets ThinkPad X41/T40 (GHz). 60 fps target, auto 30 fps fallback fine. **Debug from text logs**, not images.
- Wave 3/4 done: T08 integration (VERTICAL SLICE PLAYABLE), T10 menus+save, T11 cosmetic life.
- User play-test pending (game was started 2026-11-03 but the user was away; lock went stale and was handed to 86box-e8; closed it).
- T12 balance done. T13 performance done.
- **MILESTONE 1 COMPLETE (2026-11-03)**: 70/70 Mac tests, VC6 /W3 clean, 60 fps with a 24k city at all resolutions.
- User play-test 1 done (2026-11-03), feedback + decisions in docs/PLAYTEST.md.

## Phase 4: Milestone 2 (docs/TASKS_M2.md, DESIGN 14)
- Done: F1 water pipes, F2 art (pipes/yards/UI), M4 art (all M2 buildings), M1 politics. 98/98 Mac tests.
- Running: F3 UX fixes, M3 industry kinds + forestry/farms, M2 traffic congestion + buses + station.
- Then Wave M2: M5 services & housing, M6 complexes, M7 trade & eras; Wave M3: balance, perf, polish.
- Next user play-test: when Milestone 2 works as a whole.
- R1 rename (2026-11-03): KAUPUNKI -> **Project Lähiö**; `dev/lahio`, `lahio.exe`, `LAHIO.INI` (migrates `KAUPUNKI.INI` once), new title screen logo. Saves unchanged (`KAUP`, `*.KAU`).
