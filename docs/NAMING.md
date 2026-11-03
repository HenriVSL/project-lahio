# Naming the game

Status: proposal, 2026-11-03. Working title KAUPUNKI. The user's suggestion is **Project Lähiö**.
Brief: easy for everyone to understand; exe and folders ASCII only.

## 1. Raw brainstorm (cut list)

- **English:** Concrete & Pine, Pine & Concrete, From Wood to Concrete, Wood Then Concrete, Precast, Prefab Republic,
  Element Town, Granite & Birch, Lakeside Republic, Thousand Lakes, Sixty North, North of Sixty, Town Rights,
  Town by Decree, By Decree §, The Long Presidency, President for Life, Mr. President's Town, Reconstruction '55,
  The Great Migration, Five-Year Town, Clearing Account, The Big Neighbour, Welfare Town, Sauna & Concrete,
  Teak & Bakelite, Paper Town, Mill Town, Hometown 1955.
- **Finnish / mixed:** Project Lähiö, Lähiö, Lähiö '55, Kaupunki, Kotikaupunki, Elementti, Rintama & Lähiö,
  Kansankoti, ARAVA, Suuri muutto, Löyly, Presidentti, Tamminiemi, Urkki, Kekkoslovakia, Suomi City, SimSuomi,
  Sisu City, Asetus §, Kaupunkioikeudet.
- **Cut quickly:** *SimSuomi* (Sim- is EA's trademark family); *Kekkoslovakia* (a loaded real epithet for
  Finlandization; foreigners read "Czechoslovakia"; THEME 6.4 sensitivities); *Sisu City* and *Thousand Lakes*
  (tourist-brochure clichés, "Sisu" is used by hundreds of products); *Suburbia* (Bézier Games' board and video game);
  *ARAVA*, *Kansankoti*, *Suuri muutto*, *Löyly* (lovely to Finns, opaque to everyone else); *President for Life*
  (reads as Tropico; the joke is better inside the game than on the box); *Teak & Bakelite* (about the UI, not the town).

## 2. Shortlist

Conflict checks (2026-11-03): web search, Steam store search, itch.io browse, GitHub repo API. MobyGames blocked
the fetch (403), and no trademark register was searched (fine for a free hobby game).

### 1. Project Lähiö (user's suggestion)
- **Pitch:** "Project Lähiö: a Finnish town, 1955-1990."
- **International:** "Project" says *plan, programme, building scheme*; in American English "the projects" are public
  housing estates, which is exactly what a lähiö is. "Lähiö" is short, two vowels with dots, looks Nordic and
  exotic in a good way; the subtitle explains the rest.
- **Finns:** reads at once as the 1960s-70s concrete estate, with its double feeling (grey stigma *and* current
  lähiö nostalgia). "Projekti" has the right official, state-programme tone; a President launching a national
  suburb project is the satire in two words. Slight mismatch: the game *starts* wooden; but the lähiö is where
  the arc goes, so it works as the destination.
- **Risks:** ä/ö (keep them in the logo, ASCII "Lahio" everywhere else); non-Finns will say "la-HEE-oh"
  (Finnish: LAH-hee-uh), which is harmless. "Project X" can sound like a codename/working title; the title-screen
  treatment (section 3) fixes that. Searchability is **excellent**: "Project Lähiö"/"Project Lahio" returns no game.
- **ASCII:** `LAHIO.EXE`, folder `lahio`, repo `project-lahio`.
- **Conflicts:** no game on Steam (0 results for "lahio"), itch.io or web. GitHub: tiny unrelated Finnish repos
  `lahiosota`, `lahion-aarre` (school/hobby games, no conflict). Helsinki ran a municipal "Lähiöprojekti"
  (suburb renewal programme, 1996-) and the state a "Lähiöohjelma": a nice echo, not a conflict.
- **Variant:** bare **LÄHIÖ** (stronger logo, but a plain common noun: it drowns in urban-research hits).

### 2. Concrete & Pine
- **Pitch:** "Concrete & Pine: build a Finnish town from wood to element blocks, 1955-1990."
- **International:** fully English, instantly evocative (forest + modernist blocks = the whole look of the game).
- **Finns:** "Betoni ja mänty"; true to the landscape (blocks on granite between pines, THEME 9.5), but says
  nothing Finnish on its own; could be a craft brewery or a furniture shop.
- **Risks:** none in spelling; "&" must become "and" in identifiers. Generic-ish; needs "Finnish" in the subtitle.
- **ASCII:** `CONCPINE.EXE`, folder `concrete-pine`, repo `concrete-and-pine`.
- **Conflicts:** no game found (web, Steam). Steam has "Pine" (2019) and "Concrete Jungle"; distinct enough.

### 3. Town by Decree
- **Pitch:** "Town by Decree: you are the President. The town rights are yours to grant."
- **International:** clear and funny; puts the President angle up front, hints at the gentle satire.
- **Finns:** echoes real town rights granted by presidential decree (*asetus*), a nod to the long presidency.
- **Risks:** "Decree" is crowded on Steam (Iron Decree, Cold Decree, Last Decree, Royal Decree!, The Decree);
  doesn't say Finland or city builder unless subtitled.
- **ASCII:** `DECREE.EXE`, folder `town-by-decree`, repo `town-by-decree`.
- **Conflicts:** exact "Town by Decree": 0 results on Steam, none on the web.

### 4. Sixty North
- **Pitch:** "Sixty North: a city builder at 60°N, Finland 1955."
- **International:** cool, atmospheric, Nordic; Helsinki sits at 60°10'N. Easy to say and spell.
- **Finns:** "60 astetta pohjoista": fine, a bit neutral; could equally be Norway or Shetland.
- **Risks:** says nothing about towns, concrete or the President; sounds like a thriller or a survival game.
- **ASCII:** `SIXTYN.EXE` (or `60NORTH.EXE`), folder `sixty-north`, repo `sixty-north`.
- **Conflicts:** no game found; GitHub org `sixty-north` is a Norwegian software consultancy (Sixty North AS).

### 5. Town Rights
- **Pitch:** "Town Rights: grow a Finnish village into a city, 1955-1990."
- **International:** understandable, maps directly onto the game's goal (town status by decree, DESIGN 7).
- **Finns:** *kaupunkioikeudet* is exactly the right period term; many towns got them in the 1960s-70s.
- **Risks:** sounds legal and dry; "rights" can read as civil rights / a political game.
- **ASCII:** `TOWNRGHT.EXE`, folder `town-rights`, repo `town-rights`.
- **Conflicts:** no game on Steam or the web; an EU5 mod "revoke town rights" exists (no conflict).

### 6. Element Town
- **Pitch:** "Element Town: precast concrete, pine forests and a President who never retires."
- **International:** "element" is the Finnish word for precast panels (*elementtitalo*), but in English it reads as
  chemistry or fantasy elements; the pun needs explaining.
- **Finns:** clever and very period (*elementtilähiö*, the element factory is a core mechanic).
- **Risks:** the meaning is lost abroad; "Element" alone is a crowded word.
- **ASCII:** `ELEMTOWN.EXE`, folder `element-town`, repo `element-town`.
- **Conflicts:** exact "Element Town": none on Steam; "Element-174" exists (unrelated).

### 7. Kaupunki (keep the working title)
- **Pitch:** "Kaupunki: a Finnish town, 1955-1990."
- **International:** means "town/city", but nobody abroad knows that; three syllables of guessing (KOW-poon-key).
- **Finns:** plain, honest, a bit like naming a game "Game".
- **Risks:** generic noun, unsearchable in Finnish, hard to remember abroad. Costs nothing to keep.
- **ASCII:** `KAUPUNKI.EXE` (current), folder `kaupunki`, repo `kaupunki`.
- **Conflicts:** 0 results on Steam; no game found.

## 3. Recommendation

**Title: Project Lähiö** (the user's suggestion is the best candidate).
It is short, unique to search, sounds like a real 1960s state programme, carries the President-as-planner satire,
and the English "housing project" reading gives non-Finns the meaning for free. The ä/ö become part of the charm in
the logo while every identifier stays ASCII.

- **Tagline:** "A Finnish town, 1955-1990" (store/readme line: "Build a Finnish town from wooden veterans' houses
  to concrete suburbs, as the President who never quite retires.")
- **Identifiers:** `LAHIO.EXE`, `LAHIO.INI`, `LAHIO.LOG`; saves `*.LAH` (or keep `*.KAU` and read both);
  project folder `dev/lahio` (rename only between milestones, as the docs and other sessions reference
  `dev/kaupunki`); GitHub repo `project-lahio`; window class/title "Project Lähiö" (the font has Ä/Ö).
- **Runner-up: Concrete & Pine** (all-English fallback if the user wants zero Finnish on the box;
  `CONCPINE.EXE`, repo `concrete-and-pine`). It also works as the subtitle of the recommendation:
  "Project Lähiö: concrete & pine, 1955-1990".

**Title screen (teak & bakelite panel):**
```
  +--------------------------------------------+
  |            P R O J E C T          [stamp]  |  small, letter-spaced, cream
  |                                            |
  |            L Ä H I Ö                       |  large period pixel font, as KAUPUNKI now
  |                                            |
  |       A FINNISH TOWN  1955-1990            |  small caps, ink on the paper strip
  +--------------------------------------------+
```
- "PROJECT" small and wide-spaced above (like a programme letterhead), or as a red rubber stamp over the panel's
  corner, so it reads as the scheme's name, not as a placeholder codename.
- "LÄHIÖ" large in the existing font. Draw the umlauts as two clear square dots with a 1-2 px gap above the
  capitals so they survive at 640x480; make sure the panel has headroom for them (they rise above cap height).
- Subtitle on the off-white paper strip in ink black, matching the period-paperwork UI (THEME 7, 9.14).

## Decision (user, 2026-11-03)
**Project Lähiö**, tagline "A Finnish town, 1955-1990". ASCII: `LAHIO.EXE`, folder `dev/lahio`, repo
`HenriVSL/project-lahio` (**public**). README credits the user by full name. Rename after the running tasks finish.
