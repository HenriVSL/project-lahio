# KAUPUNKI: Theme and History Reference

A practical reference for designers and pixel artists. The setting is Finland from the post-war
reconstruction (*jälleenrakennus*) to the high tide of urbanisation (1945-1980), with an
affectionate satire of the Kekkonen era on top.

**Conventions in this document**

- Finnish terms are in *italics*. Where a word needs å/ä/ö it is spelled correctly; see section 8.4 for the font list.
- **[?]** marks something uncertain or unverified. Check it before it goes into in-game text as "fact".
- Hex colours are *approximate, art-directed* values: what reads right in pixel art on a 16-bit
  (RGB565) screen, not spectrophotometer readings. Each main colour comes as `lit / base / shade`
  where that helps.
- Sizes are real-world metres. The tile scale is a design decision, so section 2.0 only suggests one.
- The game is set in a *fictional* Finnish town. Real places are listed as references, not as game content.

---

## 0. One-paragraph pitch of the era

In 1945 Finland had lost the war but kept its independence. It had to resettle about 420,000
Karelian evacuees (roughly 11% of the population), house tens of thousands of returning veterans,
and pay heavy war reparations to the USSR in ships, machines and cables. Out of that came the
wooden *rintamamiestalo* suburbs, a state metal industry and a decade of rationing. By the 1960s
the countryside was emptying. People moved to towns in the "great migration" (*suuri muutto*), and
many went on to Swedish factories, while forests of new concrete blocks (*lähiöt*) rose on the
granite outcrops at the edge of every town. Over all of it sat President Urho Kekkonen
(1956-1982): a tall, bald, athletic former high-jump champion who skied in Lapland, wrote stern
letters to ministers, steamed in the sauna with Khrushchev and balanced between East and West.
The player is the man who stays in office forever and builds the welfare state one *ARAVA* loan
at a time.

---

## 1. Historical frame 1945-1980

### 1.1 The big forces (short)

| Theme | What happened | Why it matters for the game |
|---|---|---|
| **War reparations** (*sotakorvaukset*) | Armistice of Sept 1944: $300 million in 1938 gold dollars (later eased), paid mostly in **ships, machinery, cables, locomotives** and wood products. The last delivery was **Sept 1952**. | It forced Finland to build a metal and engineering industry (later the state company **Valmet**, the Wärtsilä shipyards). Early-game "obligation" quotas: produce X tons of goods or face penalties. |
| **Resettlement of Karelians** | About 420,000 evacuees from ceded Karelia, plus people from Salla and Petsamo. The **Land Acquisition Act** (*maanhankintalaki*, spring 1945) took land from the state, the church, companies and large owners for evacuees, veterans, war widows and invalids. Many small farms and house plots (*asuntotontti*) were formed. Only part of the rural evacuees got farms; many moved to towns and factories. | Start-of-game scenario: refugees arrive and need plots *now*. A "settler" population that wants small farms or detached houses. Orthodox churches appear in towns that took Karelians. |
| **Rintamamiestalo type plans** | Standard designs by the Finnish Association of Architects' reconstruction office (from 1942) and others. There were dozens of types. Built by owner-builders with no carpentry training; **nearly 300,000** built. | The defining low-density residential building. Variety comes from colours and small details, not from the form. |
| **Rationing ends** | Rationing ran from 1939. **Coffee** was the last food freed, on **1 Mar 1954**. | A great early event ("Coffee is free!"): a happiness spike, and cafés unlock. |
| **ARAVA** | The *ARAVA* law (**1949**) created state housing loans for flats and houses (named after *Asuntorakennustuotannon valtuuskunta*). It controlled the size and cost of flats and drove the 1950s-70s building boom. | Core housing finance: ARAVA-financed residential is cheaper and faster but has standards (minimum and maximum flat sizes) and rent caps. |
| **Baby boom** | Births peaked around **1947 (~108,000)** [?] and stayed high to about 1950. | The school-building wave of the 1950s and youth demand in the 1960s. The demographic pyramid can drive service demand over time. |
| **Suuri muutto** (great migration) | 1960s-70s: farms mechanised, and the 1969 field-reserve law (*pellonvarauslaki*) [?] paid farmers to leave fields fallow. Young people moved to southern towns, and more than **100,000 left Finland in 1969-70**, mostly for Sweden (Volvo, Saab, SKF factories; ~40,000 in 1970 alone). The population actually *fell* in 1969-70. | Migration is the main population mechanic: rural decline brings city growth, and too few jobs or flats sends people to "Ruotsi" (Sweden). The emigration ferry (Turku/Vaasa-Sweden) is a building or event. |
| **Building boom / lähiöt** | Forest suburbs from the 1950s (Tapiola), then large precast concrete estates in the 1960s-70s (Pihlajamäki, Kontula, Myllypuro, Hervanta). Housing production peaked in the early-to-mid 1970s [?]. About 1.5 million dwellings were built in the 1960s-80s. | The mid-game unlock: element factories (*elementtitehdas*) make high-density building fast and cheap but monotonous (a "drabness" stat?). |
| **Kekkonen presidency** | Elected **15 Feb 1956** by an electoral college vote of **151-149** over Fagerholm. Re-elected 1962, 1968, his term was extended by an **exceptional law in 1973** (170-28 in parliament), and he was re-elected 1978 (259/300). He resigned **Oct 1981** through illness and died 1986. | The player character, see section 6. |
| **Clearing trade with the USSR** | Bilateral *clearing* trade (1945-1990): five-year framework agreements plus yearly goods lists, with no hard currency. Finland exported **ships, paper machines, clothes, shoes, foodstuffs and construction work**, and imported **crude oil**, gas, cars and raw materials. Its share of Finnish foreign trade was about 15-16% in the 1970s and about 24% in the early 1980s. Oil price rises after 1973 made the Soviet market *bigger*, because Finland had to export more to balance. | A "Moscow trade" quota system: produce listed goods, get oil and energy at stable prices, and keep the bilateral balance near zero. |
| **Western integration (carefully)** | Associate membership of EFTA through **FINEFTA 1961**, a free-trade agreement with the **EEC in 1973**, and a matching cooperation agreement with Comecon (*KEVSOS*, 1973) to keep Moscow happy. | The balance mechanic: every Western step needs an Eastern counterweight. |
| **Welfare state** | National pension reform (1956/57), employment pensions (TEL 1962), sickness insurance (1963/64), the **Primary Health Care Act 1972** (*kansanterveyslaki*, which created health centres, *terveyskeskus*), the **comprehensive school** (*peruskoulu*, rolled out 1972-77 from Lapland southwards) and the **Day Care Act 1973**. | Service unlocks over time, and municipal duties grow (more costs, more happiness). |
| **Oil crises** | 1973 and 1979. They pushed **peat**, coal, nuclear and district heating. | Energy shocks change which power plants pay off. |

### 1.2 Key numbers (for scale and balancing)

- **Population of Finland:** ~4.0 M (1950), ~4.45 M (1960), ~4.6 M (1970), ~4.8 M (1980) [approx].
- **Urban share:** roughly a third in 1950, about 60% by 1980 [approx]. The fastest urbanisation in Western Europe of its time.
- **Workforce in farming and forestry:** roughly 45% (1950), about 20% (1970), about 13% (1980) [approx].
- **City sizes around 1970** [approx]: Helsinki ~520,000; Tampere and Turku ~155,000 each; Espoo ~90,000;
  Lahti, Oulu ~85-90,000; Pori, Vantaa ~70,000; Kuopio, Jyväskylä ~60,000. A small town is 5,000-20,000 people.
  A church village (*kirkonkylä*) is 500-3,000.
- **Municipal status until 1977:** *kaupunki* (town/city), *kauppala* (market town) and *maalaiskunta* (rural
  municipality). The *kauppalat* became towns in 1977. **Town rights were granted by government
  decree**, which gives a perfect presidential unlock.
- **Currency:** the *markka* (mk), 100 *penniä*. **The new markka of 1 Jan 1963 = 100 old marks.** In-game, divide all prices by 100 overnight.

### 1.3 Timeline 1945-1981 (in-game events, news and unlocks)

The "Gameplay idea" column is a suggestion. Dates are as precise as the sources allowed.

| Year | Event | Gameplay idea |
|---|---|---|
| 1944 | Armistice with the USSR; reparations agreed; Porkkala leased to the USSR. The Lapland War: retreating German troops burn Rovaniemi and much of Lapland. | Prologue: a burned or empty map, refugee wave, reparations quota active. |
| 1945 | Land Acquisition Act; Aalto's office drafts the **Rovaniemi "reindeer antler" plan**. | Unlock: the *rintamamiestalo* zone with free plots for settlers; "plan your town" tutorial. |
| 1947 | Paris Peace Treaty; baby-boom peak. | Birth-rate bonus; school demand rises 6-7 years later. |
| 1948 | **YYA Treaty** (Friendship, Cooperation and Mutual Assistance) with the USSR. | The Moscow Relations meter starts; a treaty landmark plaque. |
| 1949 | **ARAVA law**: state housing loans. | Unlock: ARAVA-financed apartment blocks and the Housing Board building. |
| 1950 | Kekkonen becomes Prime Minister. | News ticker flavour: "The PM from Pielavesi". |
| 1951 | State company Valmet formed; the Housing Foundation (*Asuntosäätiö*, Tapiola's developer) founded; Marimekko founded. | Unlock: the state metal works (Valmet-like); "garden city" planning policy. |
| 1952 | **Helsinki Olympics** (July-Aug); **reparations completed (Sept)**; Armi Kuusela becomes Miss Universe; Coca-Cola arrives (for the Olympics). | Event: "Olympic year" (sports field and stadium discount); reparations quota ends and frees industrial output. |
| 1953 | Tapiola garden-city construction begins [first residents 1953-54?]. | Unlock: the *rivitalo* (row house) and the "forest town" style. |
| 1954 | **Coffee rationing ends (1 Mar)**. | Happiness +; cafés and *kahvila* commercial unlocked. |
| 1955 | Finland joins the UN and the Nordic Council; *Tuntematon sotilas* (The Unknown Soldier) film premieres; Porkkala return agreed. | A "culture" event: cinema demand up. |
| 1956 | **Kekkonen elected president (151-149)**, takes office 1 Mar; **general strike** (March, ~19 days); **Porkkala returned** (Jan); national pension reform. | Game start for the "president" campaign. Strike event: industry stops unless you settle. |
| 1957 | Television starts (Tesvisio 1957, Yleisradio TV 1958); markka devaluation; Neste oil refinery at Naantali [?]. | Unlock: TV mast and broadcasting; a "TV antennas on roofs" visual upgrade. |
| 1958 | **"Night Frost" crisis** (*yöpakkaset*): Moscow freezes relations over a cabinet it dislikes and the cabinet resigns in December. The R-kioski brand name adopted. | Event: the trade freeze literally comes with a cold snap (winter palette, heating demand up) until you "fix the cabinet". Kiosk unlocked. |
| 1959 | Kekkonen settles the crisis in Leningrad/Moscow (Jan); **Pihlajamäki** construction starts (1959-65), large-scale element building. | Unlock: precast element construction (half-element). |
| 1960 | **Khrushchev attends Kekkonen's 60th birthday** (3 Sept); the famous Tamminiemi sauna night (the length of the night is legend). | Sauna Diplomacy event, see 6.3. |
| 1961 | **Note Crisis** (Oct): a Soviet note demands military consultations; the opposition candidate Honka withdraws. **FINEFTA** agreement. | Big event: choose how to answer the note. All outcomes keep you president; the price varies. Western trade unlock. |
| 1962 | Kekkonen re-elected (199/300 electors); the **World Festival of Youth and Students** in Helsinki. | Election event (always won; the margin affects approval). Festival: temporary visitors and street disturbances. |
| 1963 | **New markka** (100:1); sickness insurance law. | All money figures shrink by 100 overnight (a joke tooltip). |
| 1964 | Rautaruukki's Raahe steelworks (blast furnace) starts [?]; Marimekko *Unikko* pattern. | Unlock: steel works (state industry zone). |
| 1965 | Finland's first indoor ice hall (Hakametsä, Tampere); the Agrarian League renamed the Centre Party; Porvoo (Sköldvik) refinery [~1965-66?]. | Unlock: *jäähalli* (ice hall); oil refinery. |
| 1966 | "Popular front" government including the communists (SKDL), the first time since 1948. | Event: Moscow relations +, Western confidence -. |
| 1967 | 50 years of independence; **Sibelius Monument** unveiled; **markka devalued ~31% (Oct)**. | Monument unlock; export boom but imports dear. |
| 1968 | **Saimaa Canal reopens** (leased from the USSR); **Heikintori in Tapiola, Finland's first shopping centre**; the Haukilahti "flying saucer" water tower; Kekkonen's 3rd term; first centralised income agreement (*tupo*, Liinamaa); Soviet invasion of Czechoslovakia (Finland stays quiet). | Unlock: shopping centre (*ostoskeskus*); canal/harbour bonus; the yearly wage-settlement mechanic. |
| 1969 | New alcohol law (1 Jan): **medium beer (*keskiolut*) sold in grocery shops**, Alko shops spread to the countryside; **emigration peak**; Saab-Valmet car factory at Uusikaupunki; first electrified railway (Helsinki-Kirkkonummi); Helsinki decides to build a metro; SALT talks open in Helsinki (Nov). | Alcohol policy toggle (happiness vs order); emigration crisis event; car factory unlock; electrification of rail. |
| 1970 | ~40,000 emigrate to Sweden in a single year; Hervanta master plan (Tampere). | "Bring them home" policy; high-density *lähiö* district template. |
| 1971 | Construction of the Loviisa nuclear plant starts. | Long build project (6 years). |
| 1972 | *Peruskoulu* rollout begins; **Primary Health Care Act** (health centres). | School upgrade path; the *terveyskeskus* building replaces the scattered clinics. |
| 1973 | **Exceptional law extends Kekkonen's term (18 Jan)**; EEC free-trade agreement and KEVSOS; **oil crisis**; Day Care Act. | "Term extended!" event (no election this cycle); oil price shock; *päiväkoti* (day care) unlock; peat power gets cheaper. |
| 1975 | **CSCE summit in Helsinki** (30 Jul-1 Aug, Finlandia Hall); Kekkonen dissolves parliament; regular VR steam traction ends [~1975?]. | Prestige event: host the conference (needs a hotel and concert hall); world-fame bonus. |
| 1977 | **Loviisa 1 commissioned** (Soviet VVER-440 with Westinghouse and Siemens safety kit, nicknamed "**Eastinghouse**"); the *kauppala* status abolished. | Nuclear unlock; the town-status ladder simplifies. |
| 1978 | Kekkonen re-elected with 259/300 electors. | Landslide election event. |
| 1979 | Olkiluoto 1 (Swedish ASEA BWR); second oil crisis. | A Western nuclear option alongside the Eastern one. |
| 1980 | Kekkonen's 80th birthday; Lenin Peace Prize. | Late-game prestige; "the President skis 30 km on his birthday" news item. |
| 1981 | Kekkonen falls ill on an Iceland fishing trip; resigns 26 Oct. | Possible campaign ending (graceful "retirement" cutscene), or a sandbox that continues. |
| 1982 | Koivisto elected; **Helsinki Metro opens (Aug)**. | Post-game unlock: metro. |

---

## 2. Architecture and the built environment

### 2.0 Scale and general rules for artists

- **Suggested scale:** one tile is about one small detached-house plot. Real *rintamamiestalo* plots
  were ~1000-2000 m², so one tile is roughly 30 x 30 m and a house plus its garden fills one tile.
  Apartment slabs then take 1x2 to 2x4 tiles. If the engine uses a smaller tile, put houses on 2x2.
- **Light from the upper left (north-west)**, so south-east faces are in shade. Keep it consistent with the Neon Rooftops conventions.
- Finnish buildings are **simple boxes with honest materials**: little ornament after 1940, with
  colour, rhythm (windows, balconies, panel joints) and roofs doing the work. Pixel art should exaggerate:
  - **roof shapes** (steep gable for wooden houses, flat for 1960s-70s blocks);
  - **window rhythm** (a regular grid; this is what makes a block read as a *lähiö*);
  - **white trim** on wooden houses;
  - **context props**: flagpole, woodpile, carpet-beating rack, pines on rock.
- **Granite and pines** are the default setting of modern Finnish buildings. Blocks stand on rock *in*
  the forest, not on cleared flat lawns. Leave trees between buildings.

### 2.1 *Rintamamiestalo* (veterans' house), 1945-1960

The single most important residential sprite set.

| Spec | Value |
|---|---|
| Footprint | Nearly square or slightly rectangular, typically **~8 x 9 to 8 x 11 m**, about 70-100 m² ground floor [range approximate; dozens of type plans existed] |
| Storeys | **1½**: a full ground floor plus attic rooms under the roof, and usually a **half-sunk cellar** (*kellari*) on a visible concrete or stone plinth 0.5-1 m high |
| Roof | **Steep symmetrical gable** (*harjakatto*), ridge parallel to the long side. A municipal guide gives a pitch of 1:1.5-1:2, but many look close to 45° [?]. For art, a ridge height about **0.45 x the house width** above the eaves reads correctly. Small overhangs (30-50 cm) |
| Roof material | Originally often **bitumen felt with triangular battens** (*kolmiorimahuopa*, black/dark grey) or **sheet metal** (*peltikatto*, galvanised grey, often later painted **red-brown, black or green**); later replaced by profiled steel. The game's default: **grey metal**, with variants of black felt and red-brown painted tin |
| Chimney | **One central chimney through the ridge** (the rooms are arranged around a central masonry stove block). Red brick or rendered grey/white |
| Walls | Timber frame, sawdust insulation, **board cladding**: horizontal weatherboard (*vaakapaneeli*) **or** vertical board-and-batten (*peiterimalaudoitus*); a mix (horizontal below, vertical in the gable) is common. Some were rendered (*rapattu*) |
| Trim | **White corner boards (*nurkkalaudat*), white window surrounds, white eaves boards and bargeboards.** The trim is the signature |
| Windows | Fairly small; typically **2-light or 3-light** wooden casements (two tall panes, sometimes with a top light), **white frames**. Ground floor: 2-3 windows per long side. **Gable ends: 1-2 attic windows** high in the triangle. Originally **no dormers**; later many got a gable dormer (*frontoni*) or a box dormer, a nice "upgraded" variant |
| Entrance | A **small porch** (*kuisti*), open or glazed, on the long side or gable side, with its own little gable or shed roof and 3-5 concrete or wooden steps. A cellar door at the gable end |
| Yard | A big garden: **potato and vegetable patch, apple trees, currant and gooseberry bushes, lilac (*syreeni*), rowan (*pihlaja*), birches**. A **flagpole** (white, ~8-10 m) is near-universal. A **woodshed or outbuilding** (*liiteri/ulkorakennus*, a small gable shed, often red ochre), firewood stacks (*halkopino*), a washing line, sometimes a **separate yard sauna** (*pihasauna*). A low picket fence or spruce hedge; a gravel drive |
| Roof extras | **Roof ladder and walkway** (*kattotikkaat*, required for chimney sweeping, so the ridge carries a walkway to the chimney), snow guards, a TV antenna after 1957 |
| Later changes (variants or "decay") | 1960s: asbestos-cement *minerit* sheets over the boards; 1970s: dark brown window frames; 1980s: brick veneer and small-pane windows (the guides call these unsuitable) |

**Wall paint palette** (earth pigments and later oil paints; mixed streets were the norm):

| Name | Finnish | lit | base | shade |
|---|---|---|---|---|
| Red ochre ("Falu red") | *punamulta* | `#A44A35` | `#8E3B2A` | `#6C2B1F` |
| Yellow ochre | *keltamulta* | `#D9B25E` | `#C49A45` | `#9C7932` |
| Pale yellow / cream | *vaaleankeltainen* | `#EADCA0` | `#DCC985` | `#B7A36A` |
| Pale blue-grey | *vaaleansininen* | `#B3C4CF` | `#97ADBC` | `#768C9B` |
| Light green | *vaaleanvihreä* | `#B4C59A` | `#9CB282` | `#7C9166` |
| Grey-green | *harmaanvihreä* | `#9AA48E` | `#838E77` | `#68725E` |
| Light grey | *vaaleanharmaa* | `#CFCDC5` | `#BAB8AF` | `#96948C` |
| Brown (dark stain) | *ruskea* | `#7D5A40` | `#664730` | `#4C3424` |
| White (rarer) | *valkoinen* | `#F3F1EA` | `#E4E1D8` | `#BDBAB0` |

- Trim white `#F2F0E8` (shade `#C9C6BC`); green trim variant `#4F6B45`; dark brown trim variant `#4A3426`.
- Glass `#2E3B45`, with a sky reflection pixel `#9BB3C6`; net curtains `#E8E4D8`.
- Roofs: galvanised `#8A9096 / #757B81 / #5D6268`; painted black `#3A3B3D / #2C2D2F`; red-brown tin `#8C3A2C / #6F2D22`; felt `#45474A`.
- Plinth concrete `#A39F96 / #8A867E`; chimney brick `#9A4B36`.
- **Variation rule:** generate a street by picking a wall colour per house from a weighted list
  (red ochre 30%, yellow ochre 20%, pale yellow 15%, pale blue 10%, green tones 10%, grey 10%, others 5%)
  and keeping the form identical. Mirror the house and swap the porch side for more variety.

References: the Imatra rintamamiestalo guidelines ([imatra.fi PDF](https://www.imatra.fi/sites/default/files/atoms/files/14_08Rintamamiestalot.pdf)),
[Kaleva on ~300,000 houses](https://www.kaleva.fi/suomeen-nousi-lahes-300-000-rintamamiestaloa/1781793),
[Yle on self-build design](https://yle.fi/a/3-7622147), Helsinki's wooden-house colour schemes
([värikaava, 1950-60s puutalo](https://www.hel.fi/static/rakvv/varikaava/htm/20014.htm)). Typical districts:
Oulu's Karjasilta and Nokela, Imatra, Tampere's Messukylä and Kaleva areas, every *kirkonkylä*.

### 2.2 Older wooden town houses (*puutalokaupunki*), pre-1940, demolished in the 1960s-70s

The old town centre the player inherits, and is tempted to bulldoze.

- **Form:** 1 storey (sometimes 2), **long low facade right on the street line**, a gable or **mansard** roof (*mansardikatto*), a **wide carriage gate** (*portti*) into an enclosed courtyard with outbuildings (stables, woodsheds, saunas, privies).
- **Colours:** ochre yellow, cream, pale green, salmon pink, light grey, with **white or contrasting window surrounds**, often ornate (classicist pediments, carved neo-Renaissance trim). Roofs red tin or black.
- **Windows:** taller, **6-pane** (2 x 3) wooden windows in an even row.
- **Waterfront storehouses** (*aitat*, like Porvoo's riverside) in **red ochre**.
- **References:** Old Rauma (UNESCO), Porvoo, Kokkola Neristan, Puu-Käpylä (1920s garden suburb), Pispala (hillside workers' houses, Tampere).
- **Gameplay:** "the demolition wave" (in the 1960s-70s many towns tore down their wooden centres for concrete blocks; Turku's version was nicknamed *Turku-tauti*, "Turku disease" [nickname from the 1970s?]). Late 1970s heritage movements saved Puu-Käpylä and others. Offer **demolish for density or protect for tourism and happiness**.

### 2.3 *Rivitalo* (row house), 1955-1980

- **1950s-60s Tapiola style:** 1-2 storeys, **flat or very low mono-pitch roofs**, white render, red or yellow brick, or dark-stained wood; strong horizontal lines; a **small private garden per unit** behind a wooden fence or hedge; sometimes staggered units. Blocks of 4-10 units, ~8-10 m deep, each unit 5-7 m wide. Carports in a separate shared row.
- **1970s:** **1 storey, low gable roof**, **brown-stained horizontal boarding** `#6B4A33` or brown brick, dark window frames, a sauna in every unit (the 1970s Finnish dream), a terrace with a privacy fence.
- **Palette:** white render `#ECE9E0`, red brick `#9B4A34`, pale yellow brick `#D9C9A0`, dark stain `#4E3A2B`, black flat-roof edge `#2F2F31`.
- **Reference:** Tapiola (Espoo); Petsamo and Rantaperkiö row houses (Tampere 1950s).

### 2.4 *Lamellitalo* (slab block), 1950s-1960s

The archetype of the early *lähiö*.

| Spec | Value |
|---|---|
| Footprint | **Long and narrow**: 10-12 m deep, 30-70 m long; 2-5 stairwells (*rappu* A, B, C...), one entrance per stairwell on the "back" side |
| Storeys | **3-4** (walk-up, no lift [lift rules ?]) in the 1950s; up to 8 with lifts in the 1960s |
| Roof | 1950s: a **low-pitched gable** (*loiva harjakatto*) in tin or felt, with a small overhang. 1960s: a **flat roof** with a thin dark edge. Ventilation stacks, TV antennas |
| Walls | **Render in pale colours**: white, cream, pale yellow, light grey, sometimes salmon or pale green. Also **red brick** (very common in the late 1950s-60s, e.g. Tapiola, Herttoniemi). The plinth is dark grey concrete |
| Windows | A regular grid, horizontal **2-3-light wooden windows**, white (1950s-60s); **stairwell windows as a tall vertical strip** at each *rappu*, the key readable detail |
| Balconies | 1950s: small, **recessed or lightly projecting**, with thin steel railings or **corrugated sheet panels** (*aaltolevy*) painted a light colour or a primary accent. 1960s: projecting concrete slabs with coloured panels |
| Setting | Parallel slabs oriented for sun, **left among pines on granite**; a sandbox, swings, a **carpet-beating rack** (*mattoteline*, iconic), bike shed, rubbish shelter |

**Palette:** render white `#E9E6DC / #CFCBC0`, cream `#E8DDB5 / #CDBF93`, light grey `#CAC7BF / #A9A69E`,
salmon `#D9AA8E / #B98C72`, pale green `#C3CDB1`; red brick `#9C4B35 / #7E3A29` (mortar lines `#B9A898`);
balcony accents: signal red `#B8392B`, yellow `#E0B23A`, sky blue `#6D98BE`, green `#5F8F4E`.

Helsinki's 1950-60s colour plan ([värikaava, kivitalo](https://www.hel.fi/static/rakvv/varikaava/htm/20012.htm))
shows yellows, browns, blues, grey-greens and brick red with **white or grey-white window frames**, and
**mineral-board panel facades with light-grey render and dark-brown windows** (the "Tammi" series).

### 2.5 *Pistetalo* (point block), 1950s-1970s

- **Footprint:** compact square or cruciform, **15 x 15 to 20 x 20 m**, one central stairwell and lift, 3-5 flats per floor.
- **Storeys:** **8-13**. The Tapiola **Mäntytorni** is an 11-storey early example. Often 2-4 towers stand as a group on a hilltop, so they read as a skyline.
- **Roof:** flat, with a **lift machine room box** on top; sometimes a slightly projecting roof slab.
- **Details:** **corner balconies** (one per flat per floor, alternating colours), a vertical stairwell window strip, render or brick.
- Same palette as 2.4. They work as **landmark-density buildings on a hill**: give them a "high-ground" bonus.

### 2.6 *Elementtitalo* (precast concrete block), late 1960s-1970s

The look of Kontula, Myllypuro, Hervanta and Merihaka. Built from storey-high factory panels; the
Finnish open "BES" element system came around 1969-70 [?].

| Spec | Value |
|---|---|
| Footprint | Long slabs 12-14 m deep, 40-100+ m long; also towers, and **gallery-access blocks** (*luhtitalo*, outside walkways) |
| Storeys | **4-8** typical, 9-12 for towers |
| Roof | **Flat**, black bitumen `#353535`; lift rooms; no overhang |
| Walls | **A visible grid of panels with dark sealant joints**, the defining pixel-art feature: panels about 3 m high x 3.6-6 m wide. Finishes: **exposed-aggregate washed concrete** (*pesubetoni*, grey-beige with a speckle), **brushed or painted concrete**, **brown or red ceramic tile** (*klinkkeri*) panels, ribbed concrete |
| Windows | Regular, wide, **dark brown or teak-brown frames** (1970s) or white; the panel and window rhythm is rigid |
| Balconies | **Stacked balcony towers** (*parvekepielet*, separate concrete side walls) in front of the facade, with **coloured balcony fronts**: orange, mustard, olive green, brown, sky blue, white |
| Setting | Huge **car parks with block-heater posts** (*lämmitystolpat*, short posts with sockets, a must in winter), sparse young trees, central heating plant chimney, a 1-2-storey shopping centre (*ostari*), concrete play equipment, forest edge |

**Palette:** washed concrete `#B0A99B / #A39C8E / #847D70`, speckle dots `#7E776B` and `#CAC3B5`;
painted concrete `#C7C4BC / #A8A59D`; joints `#5E5B55`; brown tile `#7B4E36 / #5F3B29`; window frames `#4A3426`;
balcony fronts: orange `#D9792B`, mustard `#D2A62E`, olive `#76803A`, brown `#7A5236`, sky blue `#5E8DB8`, red `#B5402E`.

**Reference estates:**

| Estate | Notes |
|---|---|
| **Tapiola** (Espoo) | Garden city of the Housing Foundation (founded 1951), led by Heikki von Hertzen. A mix of detached houses, row houses, slabs and towers in forest; car-free paths; central tower (Ervi, ~1961 [?]), swimming pool, church (Ruusuvuori, 1965), **Heikintori 1968, Finland's first shopping centre**. Target ~15,000 residents. The *good* ending of planning. ([Wikipedia](https://en.wikipedia.org/wiki/Tapiola)) |
| **Pihlajamäki** (Helsinki) | 1959-65, the first large-scale element building in Finland; slabs and towers on a rocky plateau; 5 residential cells each with local shops; a red-brick church (1976). DoCoMoMo listed. ([hel.fi](https://www.hel.fi/hel2/kanslia/pihlajamaki_arkkitehtuuripolku/esittely.html)) |
| **Myllypuro** (Helsinki) | 1960s element suburb built for the migration wave; ~12,000 residents; few local jobs, so a commuter suburb |
| **Kontula** (Helsinki) | 1960s mega-*lähiö* with a big shopping centre (late 1960s [?]); later the metro (1986). The stereotype of the "concrete suburb" |
| **Hervanta** (Tampere) | Master plan 1970, built from the early 1970s; ~11,000 flats of precast blocks in rocky forest; a centre by Reima Pietilä. Today 26,000 residents. ([Wikipedia](https://en.wikipedia.org/wiki/Hervanta)) |
| **Merihaka** (Helsinki) | A 1970s waterfront "concrete jungle"; very dense, gallery decks |
| **Rovaniemi** | Aalto's **reindeer-antler plan** (1945; approved 1946): the main roads form antlers and the town centre plus the rivers form the head. Aalto Centre (library, town hall, Lappia Hall). A great "build a town shaped like X" achievement. ([Rovaniemi](https://lapinkavijat.rovaniemi.fi/aalto/jkaava_en.html)) |

### 2.7 Shops and commerce

| Building | Look | Notes |
|---|---|---|
| **Village co-op shop** (*osuuskauppa*) | 1-2 storey wooden or rendered building with a big shop window, a painted sign board across the facade, the flat above for the shopkeeper; stairs and a loading dock | Rival co-op movements (real history): **S** (SOK, the "neutral", farmer-friendly one), **E** (progressive, labour-aligned: Elanto in Helsinki, OTK), and private retailers under **Kesko (K-kauppa)**. A town often had one of each, which is great for a competition mechanic. **Use fictional brands** (see 8.6) |
| **Self-service grocery** (*valintamyymälä*) | 1960s glass shop front, flat roof, a fascia sign with letters, posters in the windows | Self-service spreads in the 1960s; supermarkets (*Valintatalo*, *Prisma*-like) come in the 1970s |
| **Kiosk** (*kioski*) | **Tiny box 2-3 m square** with a hatch window, sign on the roof, newspapers clipped to the front, a sweets display, often on a corner or at a bus stop | R-kioski name since 1958 (Rautakirja's railway bookstall chain). Also wooden *grillikioski* (1970s sausage grill) and ice-cream kiosks. Cheap 1x1 commercial |
| **Alko** (state liquor monopoly shop) | A sober shop front with an **ALKO-style plain sign**, frosted or high windows, counter service (no self-service in this era [?]) | Only in towns before 1969. **Gameplay: alcohol policy** (crime and happiness). Use a fictional sign like "VALTION VIINA" or "ALKOHOLILIIKE" (generic) |
| **Shopping centre** (*ostari*) | **1-2 storeys, flat roof, a long glazed front**, a row of fascia signs (grocery, bank, Alko, pharmacy *apteekki*, hairdresser *parturi-kampaamo*, kiosk, post office, café/grill), a **tall signpost or clock tower**, a covered walkway | The heart of every *lähiö*. First true mall: Heikintori 1968 |
| **Department store** (*tavaratalo*) | 3-6 storey city-centre box, window bands, a neon roof sign | Stockmann, Sokos, Anttila (the "people's store" with mail-order). Fictional: "Tavaratalo Pohjola" |
| **Market square** (*tori*) | An open paved or gravel square with **orange or white canvas market stalls**, a **market hall** (*kauppahalli*, brick, 1900s), coffee tents, fish boats at the harbour quay | Every Finnish town has a *tori*: give the town a central plaza tile with daily market props |
| **Petrol station** | 1950s-60s: a small kiosk with a flat cantilevered canopy and 2-4 pumps, a brand sign on a pole | Brands: Esso, Shell, Gulf, Union, Neste, Kesoil, and **Teboil** (Soviet-owned via Suomen Petrooli [?]), which is great satire. Fictional brands recommended |
| **Bank** | A rendered or stone-clad 1950s block, a granite plinth, a brass sign | KOP, SYP (Yhdyspankki), savings banks (*Säästöpankki*), co-op banks (*Osuuspankki*), Postipankki |
| **Cinema** (*elokuvateatteri*) | A plain facade with a **big neon or illuminated name** ("BIO", "KINO", "REX", "ROYAL") and a poster frame | 1950s golden age of Finnish film; TV kills many cinemas in the 1960s |
| **Hotel / restaurant** | *Seurahuone* (the classic town hotel), a 1960s modern hotel box | Restaurants with strict dress codes; the dance restaurant; the *baari* milk bar |

### 2.8 Offices and civic buildings

- **Town hall** (*kaupungintalo*): reference **Säynätsalo Town Hall** (Aalto 1952, red brick, a courtyard raised on a hill), or a 1960s concrete and glass box. A flagpole and the town coat of arms.
- **Government office building** (*virastotalo*): 1960s-70s, 4-8 storeys, **horizontal window bands**, concrete or light stone panels, a flat roof. Tax office, employment office (*työvoimatoimisto*, important during *suuri muutto*), social office.
- **Headquarters office:** reference **Rautatalo** (Aalto, 1955, copper-clad), Enso-Gutzeit HQ (1962, Aalto, white marble grid by the harbour).
- **Post office** (*posti*): an orange or yellow post-horn sign [the colour is ?], a counter hall; a mailbox on the wall.
- **Library** (*kirjasto*): every municipality must have one. References: the Aalto libraries at Seinäjoki (1965) and Rovaniemi (1965), which are fan-shaped and white.
- **Concert and congress hall:** **Finlandia Hall** (Aalto, 1971, white Carrara marble), the CSCE 1975 venue and a late-game prestige landmark.

### 2.9 Industry

| Type | Look and specs | Era notes |
|---|---|---|
| **Sawmill** (*saha*) | A long **wooden or corrugated-steel shed** with a **red-ochre or grey** finish, a tall brick chimney or sawdust burner, a lakeside **log pond with floating log booms**, huge **timber stacks** (yellow `#D8B878` boards in stickered piles), a drying kiln | Sawmills sat on lakes and river mouths; logs came by **timber floating** (*uitto*) across lakes as rafts towed by small tugs. Very visual |
| **Pulp and paper mill** (*paperitehdas*, *selluloosatehdas*) | A massive **white-rendered or red-brick** complex, **tall brick or concrete chimneys**, **white steam plumes**, a long machine hall (100-200 m, ridge skylights), round digesters and silos, mountains of logs (*tukkikasa*) or wood chips, a rail spur, rapids or a dam beside it | The "green gold". References: Aalto's **Sunila** pulp mill (Kotka, 1930s, white functionalism), Kymi (Kuusankoski), Mänttä (Serlachius), Äänekoski (famous for its smell). The company town (*tehdaskylä*) has worker housing in rows, a manager's villa, a clubhouse |
| **Metal works** (*konepaja*, Valmet-like) | Large **sawtooth-roofed** (shed-roof, north-light) halls of red brick or corrugated steel, gantry cranes, rail tracks, a water tower, a gate building with a clock | **Valmet** (state, 1951) made trains, tractors, paper machines, aircraft and rifles. Reparations built this industry |
| **Shipyard** (*telakka*) | A slipway or dry dock, **tall gantry and luffing cranes**, a hull under construction, a sheds row | Wärtsilä (Helsinki, Turku) and Valmet yards built **icebreakers** and ships for the USSR under clearing. The prime clearing export |
| **Textile mill** (*kutomo*, *puuvillatehdas*) | **Red-brick multi-storey mill** (4-6 storeys) with rows of arched windows, a brick chimney, a riverside site with rapids, worker housing (wooden *työväen asunnot*) | **Finlayson** and Tampella in Tampere ("the Manchester of Finland"), Forssa, Barker (Turku). Clothes and shoes went east under clearing |
| **Steelworks** | A blast furnace, gas holder, rolling mill sheds, ore stockpiles, a harbour | Rautaruukki **Raahe** (state, 1960s) |
| **Oil refinery** | Tank farm (white or silver cylinders `#D7D9D6`), flare stack, cracking columns, a jetty | Neste: Naantali (1957 [?]), Porvoo-Sköldvik (1965-66 [?]); **crude came from the USSR** |
| **Dairy** (*meijeri*) | A 1-2 storey **white-rendered or brick** building, a milk platform, a small chimney, milk-can trucks | **Every rural parish had a dairy** (the Valio co-op). Roadside **milk-can platforms** (*maitolaituri*) at every farm gate are a great road prop |
| **Brickworks** (*tiilitehdas*) | A long kiln shed, a tall chimney, clay pits | Supplies brick for red-brick 1950s-60s buildings |
| **Element factory** | Big flat sheds, outdoor casting beds, stacks of grey panels, cranes | **Gameplay: required for high-density elementtitalo** (unlock and production chain) |
| **Rubber, cable, electronics** | Mid-size brick or concrete factories | Nokia (rubber boots and cables in the 1960s!), Salora (TVs), Strömberg (motors) |
| **Brewery** | A brick brewhouse with a tower, malt silos | Fictional brands; ties to the alcohol policy |
| **Glassworks, ceramics** | A furnace hall, chimney | Iittala, Nuutajärvi, Riihimäki, Arabia (Helsinki). Tourism and prestige |

**Industry palette:** red brick `#93432F / #7A3626` (mortar `#BFAE9C`); white render `#E7E4DA`;
corrugated steel `#9EA4A8 / #81878C / #666B70`; rust `#8B4A2B`; soot on chimney tops `#3A3634`; steam `#EEF1F3`
(alpha-blend over the sky or use a dither); log piles bark `#6E5A44` with cut ends `#D9C08A`.

### 2.10 Harbour

- A **granite-block quay** (`#8F8A84`), wooden piers in small towns, **cargo cranes** (portal and luffing, yellow-orange or grey), **warehouses** (red-brick or corrugated), **grain or cement silos** (tall concrete cylinders), **timber and pulp-bale yards** (export).
- **Car and passenger ferries to Sweden** (from 1959 onward; Silja and Viking Line forerunners). In-game, *the emigration ferry*.
- **Icebreakers** in winter (black hull, red-white superstructure is typical [livery varies]). A frozen harbour in winter with an icebreaker channel is a strong visual.
- Fishing boats, a market-square quay with **fish-selling boats**.
- References: Kotka, Hamina, Turku, Helsinki South Harbour, Oulu, Vaasa.

### 2.11 Railway station (VR)

- **Small-town station:** a **wooden building in yellow ochre or red ochre with white trim** (standardised early-20th-century designs, e.g. by Bruno Granholm), a gable roof with decorative brackets, a platform canopy, a **water tower for steam locomotives**, a goods shed, **semaphore signals** (into the 1960s-70s), a **bilingual name sign** (*Tampere – Tammerfors*).
- **Big-town station:** stone or brick (Helsinki: Saarinen, granite, a clock tower; Tampere: functionalist with a tower, 1936). 1960s new stations: concrete and glass, flat roofs.
- **Rolling stock** (see 4.6).

### 2.12 Power and utilities

| Plant | Look | Era / reference |
|---|---|---|
| **Hydro** (*vesivoimala*) | A dam across rapids, a **power-station hall** with tall windows (classicist at Imatrankoski 1929; **modernist by Aarne Ervi on the Oulujoki river, 1940s-50s**: white concrete, big glazed halls), spillway gates, a switchyard | **Imatrankoski** (1929, the largest), the Oulujoki and **Kemijoki** chains (1940s-70s). The rapids disappear, so give a scenic or fishing penalty. Needs a river tile with a drop |
| **Coal / oil CHP** (*voimalaitos*, *lämpövoimala*) | A box hall of brick or steel, **1-2 very tall chimneys** (100-150 m), a coal yard and conveyor, an oil tank | City plants (Helsinki Salmisaari, Hanasaari). **Combined heat and power**: Finland adopted **district heating** (*kaukolämpö*) massively from the 1950s-60s. Gameplay: a heating network as a utility |
| **Peat** (*turvevoimala*) | A mid-size plant with a chimney; peat bogs nearby as **flat brown rectangles with drainage ditches and peat stacks** | Peat boomed after the 1973 oil crisis (the state fuel company). A domestic fuel that reduces Moscow-oil dependence but is ugly |
| **Nuclear** | Loviisa: a **cylindrical domed containment and rectangular turbine halls**, cooled by the **sea** (no cooling towers) [building forms approximate] | Loviisa 1 (1977), 2 (1981): a Soviet VVER-440 with Western safety systems, "**Eastinghouse**". Olkiluoto (1979) is the Western option |
| **Water tower** (*vesitorni*) | **Iconic Finnish modernist landmarks**: about half of Finland's ~500 water towers date from the 1960s-70s. Forms: **mushroom or cone** (Lauttasaari, design 1956), **flying saucer** (Haukilahti, Espoo 1968), **brutalist cylinder** (Kaanaa, Pori 1963, then the tallest), **cylinder with a viewing deck and café** (Karjaa, Riihimäki). Grey concrete `#B3B0A8` or white-painted | **Make the water tower a customisable landmark** with 4-6 forms; it provides water pressure over a radius; give it a skyline bonus. Viewing cafés are real |
| **Water works and sewage** | Low treatment basins (round clarifiers), a pump house | **Most towns lacked proper sewage treatment until the 1970s**; paper mills polluted lakes (Water Act 1961). Gameplay: lake pollution |
| **Transformer and switchyard** | A fenced gravel yard, steel frames, a transformer kiosk (small brick or concrete hut with a lightning sign) | |

### 2.13 Schools, health, welfare

- **Folk school** (*kansakoulu*, pre-1972): in villages a **2-storey wooden school** in yellow or red ochre with big windows, a teacher's flat and a yard with a flagpole; in towns 1950s **render or brick, 2-3 storeys, rows of tall classroom windows**, a gym hall.
- **Secondary** (*oppikoulu*, *lukio*): bigger, a town-centre site.
- **Comprehensive school** (*peruskoulu*, 1972-77): new 1970s **low concrete or brick school complexes**, flat roofs, wings around a yard, sports field. **Upgrade path: kansakoulu + oppikoulu become peruskoulu.**
- **Maternity and child clinic** (*neuvola*): small; famous for the **baby box** (*äitiyspakkaus*, since 1938), a nice welfare event.
- **Health centre** (*terveyskeskus*, from 1972): a 1-3 storey 1970s concrete or brick building with a **flat roof and horizontal window bands**, an ambulance bay. Before that: a municipal doctor's house (*kunnanlääkärin talo*) and a public health nurse (*terveyssisar*).
- **Hospital:** 1950s-60s **central hospitals** (*keskussairaala*) per region: 6-10 storey white slabs with long window bands and balconies (TB sanatoria had sun balconies; **Paimio Sanatorium**, Aalto 1933, is the reference).
- **Elderly home** (*vanhainkoti*, older *kunnalliskoti*): a 2-storey rendered building in a park.
- **Day care** (*päiväkoti*, from 1973 by law): a 1-storey flat-roofed pavilion with a fenced yard and play equipment.

### 2.14 Emergency services

- **Fire station** (*paloasema*): the signature detail is the **tall slender hose-drying tower** (*letkutorni*), 15-25 m, often with a clock or siren. Red garage doors (3-5 bays), a rendered or brick body. In villages, the **volunteer fire brigade hall** (*VPK-talo*): a wooden hall that doubled as a **dance and community hall**.
- **Police** (*poliisilaitos*): a plain office block with a "POLIISI" sign; in rural areas the **nimismies** (rural sheriff/police chief), a wonderfully period title for the police building ("Nimismiehen kanslia").
- **Civil defence shelters** (*väestönsuoja*): a law required shelters in large buildings; a little green door sign with an orange triangle [sign colours ?]. A hidden service: protects against "crisis" events.

### 2.15 Churches and religion

- **Old wooden church** (*puukirkko*): **cruciform or long church**, **red ochre or yellow/white**, a shingle roof (tar-black or grey), a **separate bell tower** (*kellotapuli*), a **stone wall** (*kiviaita*) around the churchyard, birches. Kerimäki is the largest. The village's tallest building.
- **Stone church** (medieval, 1300-1500s): grey fieldstone, steep roof, a stepped brick gable (Turku region). A rare landmark.
- **Modernist churches (1950s-70s)**, unique landmarks:
  - **Lakeuden Risti**, Seinäjoki (Aalto, 1960): white, a 65 m freestanding cross-tower.
  - **Vuoksenniska Church**, Imatra (Aalto, 1958): white, an asymmetric tower.
  - **Kaleva Church**, Tampere (Pietilä, 1966): tall curved concrete "fish" walls with slit windows.
  - **Temppeliaukio Rock Church**, Helsinki (Suomalainen brothers, 1969): carved into granite, a copper dome.
  - **Otaniemi Chapel** (Siren, 1957): wood and brick, a glass wall onto the forest.
  - **Hyvinkää Church** (Ruusuvuori, 1961): a sharp triangle; **Tapiola Church** (Ruusuvuori, 1965): a concrete box.
  - Many **red-brick 1950s-70s parish centres** with a slender freestanding bell tower.
- **Orthodox church** (*ortodoksinen kirkko*): evacuated Karelians founded new Orthodox parishes and churches in the 1950s, small with an **onion dome** (green or gold). A building unlocked by evacuee settlers.
- **Cemetery** (*hautausmaa*): a stone wall, a **birch-lined avenue**, a chapel. The **war heroes' section** (*sankarihauta*) has rows of identical grey or white stones or crosses and a central monument. This is a *respectful*, no-joke zone.

### 2.16 Leisure, sport, culture

- **Sauna:** the yard sauna (a small log or board cabin, red or unpainted grey-brown, a chimney pipe, by a lake with a **wooden jetty**); the **smoke sauna** (*savusauna*, no chimney, soot-black); the **public sauna** (*yleinen sauna*) in town; and in apartment blocks the shared **basement sauna with booked turns** (*saunavuoro*). Gameplay: saunas are a happiness service and a diplomacy building.
- **Outdoor dance pavilion** (*tanssilava*): a wooden **open-sided pavilion, often hexagonal or octagonal, with a big roof**, by a lake or crossroads, with a fence and ticket booth. The peak *lavatanssit* era was the 1950s-60s. A great summer-only building.
- **Ski jump** (*hyppyrimäki*): many towns had one (Lahti's are iconic), a steel or wooden in-run tower on a hill. A landmark.
- **Ice:** natural rinks (a flooded field with snow banks and floodlights) in winter; **indoor ice halls** (*jäähalli*) from 1965 (Hakametsä, Tampere): a big barrel or gable-roofed hall.
- **Sports field** (*urheilukenttä*): a cinder running track (`#A8553A` red-brown cinder), a grass infield, a wooden grandstand; a **pesäpallo field** (Finnish baseball, a distinctive zig-zag base layout); lit ski tracks (*valaistu latu*) through the forest; a swimming hall (*uimahalli*); outdoor swimming beaches with diving towers.
- **Workers' hall** (*työväentalo*) and youth society hall (*nuorisoseurantalo*): wooden halls for theatre and dances, run by the left and the agrarian movements respectively. Political-flavour buildings.
- **Allotment gardens** (*siirtolapuutarha*): tiny colourful cottages on small garden plots (Kumpula, Tampere). A charming low-density zone.
- **Summer cottages** (*kesämökki*): small red or brown log or board cabins on lake shores with saunas and jetties. A cottage boom in the 1960s-70s as cars spread.
- **Parks:** an esplanade, the *kansanpuisto* (people's park), a bandstand, playgrounds, a statue.
- **Statues:** **war memorials** (bronze soldier, 1950s-60s), poets (Runeberg, Kivi), athletes (**Paavo Nurmi**), 1960s **abstract steel** (the Sibelius Monument, 1967: a cluster of steel pipes). Twin-city gift **Lenin busts** existed (Kotka 1979, Turku 1977; both later removed [dates ?]). A satire opportunity, see 6.

---

## 3. Landscape and nature

### 3.1 Terrain types

| Terrain | Look | Hex (summer) |
|---|---|---|
| **Spruce forest** (*kuusikko*) | Dark, dense, conical trees, mossy floor, on fertile soil | crowns `#2F4A2E / #3D5C38`, dark `#1F3320`; moss `#5E7A3A` |
| **Pine forest** (*männikkö*) | On dry sand ridges (*harju*) and rock; tall **orange-brown trunks**, flat dark-green crowns, light lichen and heather floor, airy | crowns `#486A38`, trunk `#B0643A / #8A4A2A`, lichen floor `#BDBB98`, heather `#8E6B7A` |
| **Birch** (*koivikko*) | Bright light-green leaves, **white trunks with black marks**; on old fields and around houses | leaves `#7FA650 / #96B866`, trunk `#ECEAE2` + `#2A2A2A` |
| **Mixed forest** | Spruce, birch, aspen, alder; the default | mix |
| **Granite outcrop** (*kallio*) | **Rounded, glacier-polished grey-pink rock** humps with cracks, **lichen patches**, small pines in the cracks. Everywhere in the south | rock `#8E8780 / #A89F96 / #6E6862`, pink granite `#AD8C7E`, lichen `#BEB98E`, `#8A9A70` |
| **Glacial boulders** (*siirtolohkare*) | Lone huge boulders in forest or fields | `#8A847D` |
| **Lake** (*järvi*) | **Dark, brownish-blue humic water**, rocky shores, reeds, islands; "the land of a thousand lakes" (~188,000 lakes) | deep `#2F4B66`, mid `#3D5C7A`, shallow `#56738A`, reeds `#9AA060` |
| **Sea and archipelago** | Low smooth rock islets, pines, red boathouses | water `#3E5F78` |
| **Rivers and rapids** (*koski*) | White water `#E8EEF0` over rocks; the site of mills and hydro plants | |
| **Fields** (*pelto*) | Flat rectangles separated by **open drainage ditches every 15-30 m** (stripes!), **grey weathered hay barns** (*lato*) dotted in fields (iconic in Ostrobothnia), **hay drying racks** (*haasia*) in the 1950s | barley/oats `#D6C27A`, rye `#C9AE62`, hay `#A9B060`, ploughed `#5B4636`, stubble `#C8B07A`, *lato* `#8A8178` |
| **Bog** (*suo*) | Open flat wetland, hummocks, stunted pines, **cotton grass**, pools; large in the north | `#8D8256`, `#A39B6A`, pools `#4D5E55` |
| **Peat extraction field** | Flat **dark-brown rectangles** with ditches and peat stacks | `#4C3626 / #5E4430` |
| **Esker** (*harju*) | A long sand-gravel ridge with pines; gravel pits | sand `#CDBB8E` |
| **Fell** (*tunturi*, Lapland only) | Treeless rounded hills | Optional northern map |

**Typical town layout:** a church and market square near the river mouth or rapids; the old grid of
wooden houses; the railway on one side with industry; *rintamamiestalo* suburbs on the former fields
and gentle slopes; *lähiöt* on the rocky forested hills at the edge; lakeshore cottages beyond.

### 3.2 Seasons and colours

| Season | Character | Key colours |
|---|---|---|
| **Summer** (Jun-Aug) | White nights, very green, lakes blue, fields yellowing | as above; sky `#9DC0DE` |
| **Autumn** (Sep-Oct), *ruska* | Birches turn **gold**, aspens **orange-red**, rowan berries **red**, blueberry heath **wine-red**; spruces stay dark | birch `#E0B42E / #C99A22`, aspen `#C8502A`, rowan `#B0282A`, heath `#8E2F3A`, wet asphalt `#4A4C4E` |
| **Winter** (Nov/Dec-Mar/Apr) | Snow, short days, polar night in the north, frozen lakes | snow lit `#F4F6F8`, snow `#E3E9EF`, **blue shadow `#BCC8D8`**, deep shadow `#93A2B8`, ice `#CADCE6`, snowy spruce `#2C3E33` + `#E9EEF2` caps, low-sun sky `#E8B8A0` / dusk `#5A6C8E` |
| **Spring** (Apr-May) | Melt, mud, gravel dust, bare birches with a green haze, ice break-up | mud `#6B5A45`, grit `#9C9488`, first green `#A6C46A` |

### 3.3 Winter: is it worth it?

**Yes, and it should be a core feature, not decoration.** Snow is central to the Finnish identity
and the era's urban problems (heating, ploughing, cars that won't start). Facts: winter lasts
**~100 days in the south-west and ~200 in Lapland**; permanent snow cover is about **45-65 days at the
south coast**, often 4-5 months inland, and **~200 days of snow in Lapland**. In the 1950s-70s winters
were snowier than today.

**Recommendation:**

1. **Two full palettes: summer and winter.** Autumn and spring are cheap **tints** (autumn: recolour
   deciduous foliage only; spring: swap the snow for a mud/grit ground). That is exactly the kind of thing
   a deterministic Python generator does well: render each sprite with a "snow mask"
   (upward-facing roof and ground pixels go to snow colours; the roofs of heated buildings melt first,
   which looks great).
2. **Memory cost:** winter variants roughly double the terrain and building sheets. With DirectDraw
   video-memory surfaces on the Voodoo3 (16 MB), keep the *active* season in VRAM and reload the other
   at the season change (a loading moment at the "first snow" event is fine). Keep the surface width at
   640 px or less (see CLAUDE.md).
3. **Winter gameplay:** **heating demand** (district heating, oil, wood; the oil price links to
   Moscow trade), **snow ploughing** as a road service (unploughed roads slow traffic), **frozen lakes**
   (ice roads as shortcuts, harbour needs an icebreaker), **block heater posts** for car parks,
   **ski tracks and rinks** give winter happiness, more fires (wood stoves), the sauna matters more, **polar night**
   (darker palette and streetlights on all day in the north), spring **flood** risk at ice break-up.
4. **Night/dusk lighting:** street lamps as warm pools. **Mercury lamps** (bluish-white `#DDE6F0`)
   were typical in the 1950s-60s; **sodium** (orange `#F2A541`) spread in the 1970s. That is a subtle era cue.

---

## 4. Infrastructure look

### 4.1 Roads

| Type | Look |
|---|---|
| **Gravel road** (*soratie*) | The default outside towns into the 1960s-70s. **Light grey-beige** `#B3A88F` with darker wheel ruts `#9A8F78`, a crowned profile, ditches on both sides, **dust clouds** behind cars in summer, potholes in spring (*kelirikko*: spring thaw makes roads impassable, a real event) |
| **Oil-gravel** (*öljysora*) | A cheap 1960s-70s rural paving, dark grey-brown `#5C5852`, rough |
| **Asphalt street** | Towns, main roads. `#55575A` new, `#6A6B6C` worn, patch `#4A4B4D` |
| **Cobblestones** (*mukulakivi*) | Old town centres, market squares, harbour: grey `#7E7A74` with a dark grid |
| **Main road / highway** (*valtatie*) | Two lanes, asphalt, gravel shoulders, **yellow centre line** (Finland used yellow lines between opposing directions; solid yellow lines survived until the 2020 law; the exact introduction date is [?]) |
| **Motorway** (*moottoritie*) | The first sections came in the early 1960s (around Helsinki) [?]; a late-game unlock |
| **Pedestrian and cycle paths** | Separated in the new suburbs (the Tapiola ideal); light asphalt or gravel |
| **Bridges** | Steel truss (rivers), concrete beam, wooden in villages; in winter, ice roads (*jäätie*) across lakes with marker spruces stuck in the snow |

**Road markings:** white edge lines, **yellow centre lines between opposing traffic** (a distinctive
Finnish look; make it a colour detail `#E0B020`), zebra crossings white.

**Road signs:** Finnish **warning signs have a YELLOW background** (the red-bordered triangle on yellow, for
visibility against snow), and so do prohibition signs (red ring on yellow). Direction signs are blue
(ordinary roads) and green (motorways) [the motorway green is the modern standard; era use ?].
**Bilingual names** (Finnish/Swedish) in bilingual municipalities.
At pixel scale: yellow triangle and circle signs `#F2C200` with a red border `#C8102E`, blue guide signs `#1E4E9A`.

### 4.2 Street furniture

- **Street lamps:** in towns, **lamps hung from cables strung across the street** (catenary-suspended,
  very Finnish and Nordic), plus concrete or steel poles with curved arms; in villages, **wooden poles
  with a simple arm and a dish shade**.
- **Power and phone lines:** **wooden poles** (tarred and creosoted dark brown `#4E3B2C`), 3 wires on a
  crossarm for 10-20 kV, phone lines on smaller poles; transformer kiosks. **Transmission lines (110-400 kV)
  on guyed portal towers** (two-legged steel frames with guy wires) are a distinctive Finnish form.
- **Flagpoles** everywhere (white). On flag days (Independence Day 6 Dec, Midsummer, etc.) blue-cross flags fly from every yard; a fictional "President's birthday" flag day is a good satire event.
- **Mailboxes:** wall-mounted at the post office and on poles; rural **mailbox stands at road junctions** (a row of boxes on a plank).
- **Milk-can platforms** (*maitolaituri*): wooden platforms at farm road ends, with metal milk cans.
- **Bus stop:** a pole sign; a wooden or glass shelter in towns.
- **Phone booth** (*puhelinkoppi*): [colour ?]; make a generic boxy booth.
- **Carpet-beating racks** (*mattoteline*), sandboxes, laundry lines, bike racks, bin shelters in courtyards.
- **Block-heater posts** in every car park (1960s onwards).
- **Advertising columns** (*mainospylväs*) in city centres; the **neon signs** of the 1950s-60s on rooftops (Helsinki's Elanto, Hartwall, Fazer neon).

### 4.3 Buses and trams

- **Helsinki trams:** the traditional livery is **yellow upper body and green lower** (`#E8C547` / `#2F6B3E`); Valmet and Karia-built 1950s cars, the articulated Nr I from 1973 (later grey and orange, 1970s-80s). Only Helsinki (and Turku until 1972, Viipuri pre-war) had trams. **Turku closed its trams in 1972**, a good "progress?" event.
- **Buses:** Finnish **bodywork builders** (Ajokki, Wiima, Delta, Kutter, Lahden Autokori) on **Volvo, Scania-Vabis, Sisu and Leyland** chassis. Mostly **private regional bus companies with two-tone liveries** (cream plus red, blue or green bands). The 1950s rounded "bulldog" front-engined buses gave way to 1960s boxy rear-engined buses. **Postal buses** (*postiauto*) carried mail and passengers on rural routes [livery unverified].
- **Coaches:** "express bus" (*pikavuoro*) with a destination board.

### 4.4 Cars of the era (traffic sprites)

The Finnish car fleet was famously a **mix of East and West**, a satire goldmine. Soviet cars came through the importer **Konela** (founded 1947, with Avtoexport behind it).

| Car | Era | Look | Notes |
|---|---|---|---|
| **Moskvich 400/402/407/408/412** | 1950s-70s | Small, rounded (400/407) then boxy (408/412) saloons | **Finland's best-selling marque around 1956-61** (~20-23% share); cheap |
| **Pobeda** (GAZ-M20) | 1950s | A fastback; a taxi favourite | ~14% share in 1956 |
| **Volga GAZ-21 / GAZ-24** | 1956-70s | Big chrome saloon (21: deer bonnet ornament); a taxi and official car | Official cars, taxis |
| **Lada (VAZ-2101 "Zhiguli")** | 1970s | A boxy Fiat-124 clone | Sold in Finland from the early 1970s [?] |
| **Volkswagen Beetle** | 1950s-70s | The bug | Hugely popular |
| **Saab 96** | 1960s-70s | A rounded two-stroke/V4, rally fame | Built at **Uusikaupunki from 1969** (Saab-Valmet) |
| **Volvo PV444/544, Amazon** | 1950s-60s | Hump-backed / rounded | |
| **Opel Kadett/Rekord, Ford Taunus/Anglia/Cortina, Fiat, Škoda Octavia, Wartburg, Trabant** | | | **Škoda** was strong in the early 1960s |
| **Mercedes-Benz 180/190D "Ponton"** | 1950s-60s | The **taxi** (*taksi*) and doctor's car | A taxi sign on the roof |
| **Trucks:** Sisu, Vanaja (Finnish), Volvo, Scania-Vabis, GAZ/ZIL, Bedford | | Timber trucks with log bunks; milk trucks | |
| **Tractors:** **Valmet** (Finnish, orange-red?), Fordson, Massey Ferguson (grey "Fergie") | | | On field tiles |
| **Vans:** VW Transporter, Ford Transit (1970s); police "Black Maria" van (*Mustamaija*) | | | |

Car paint for sprites (1950s-70s): pale blue `#8FB0C8`, beige `#D6C79E`, cream `#EDE3C4`, red `#B33A2E`,
dark green `#3E5E46`, grey `#8D9196`, black `#262729`, white `#E8E8E4`, 1970s orange `#D07A2A`, mustard `#C9A23A`.

### 4.5 Emergency vehicles

- **Fire engines:** red (`#B8231E`), Sisu, Vanaja, Volvo or Scania chassis.
- **Police:** 1960s cars included Volvos, Saabs, VW Beetles, even Porsche 356 and Plymouth patrol cars [the livery of this era is unverified: a dark or black car with "POLIISI" lettering and a blue roof light is a safe stylisation].
- **Ambulance:** a white or cream estate car or van with a red cross [stylised].

### 4.6 Trains (VR, *Valtionrautatiet*)

- **Steam** until about 1975 (Hr1 "Ukko-Pekka" passenger engines, Tk3 "Pikku-Pässi"; black with a red buffer beam, often a green tender trim [?]).
- **Diesel:** the **Dm7 "Lättähattu" railbus** (1950s, Valmet; red with a light band [?]) on branch lines (*the* nostalgic branch-line train); Dr12, Dr13; **Dv12** (1963+, red with a grey or beige stripe [approx]).
- **Electric:** electrification from **1969**; **Sm1** commuter EMUs (1969, orange-red/brown [?]); **Sr1** locomotives (1973+, built in the USSR, red). Another East-West mix.
- **Track:** Russian broad gauge **1524 mm** (inherited from the Russian Empire). Wooden sleepers, gravel ballast `#8B857C`.
- **Goods wagons:** brown-red `#7A3A2A` boxcars, log wagons, oil tankers.

---

## 5. Society and economy, for gameplay

### 5.1 What people did for work (and zone ideas)

| Sector | 1950 | 1970s | Game zone or building |
|---|---|---|---|
| **Farming** (small dairy farms of 5-15 ha, **red barns**) | ~45% of the workforce with forestry | shrinking fast | **Agriculture zone**: fields, farmhouses (*maalaistalo*, a 2-storey wooden house, red or yellow), barns, *lato*; feeds the dairy |
| **Forestry** (winter logging by farmers, timber floating) | big | mechanising | **Forestry zone**: forest tiles harvested, feeding sawmills and paper mills by road, rail or floating |
| **Forest industry** (sawmill, pulp, paper, board) | the export backbone | bigger plants | Heavy industry I-zone, needs water and wood |
| **Metal and engineering** (state and private) | growing from reparations | big: ships, machines | **State industry** (*valtionyhtiö*) placed by decree, plus private industry |
| **Textiles, shoes, clothes** | Tampere, Forssa, Turku | clearing exports | Light industry |
| **Construction** | the reconstruction boom | the *lähiö* boom | Construction capacity as a resource (element factory) |
| **Commerce, co-ops** | co-op dominated | supermarkets | C-zones |
| **Public services** (teachers, nurses, clerks) | small | **growing fast** (welfare state) | Services create jobs |
| **Emigration to Sweden** | | the peak in 1969-70 | Population leak when jobs and flats are missing |

**Zone suggestions beyond R/C/I:**

- **R-low:** *omakotitalo*/rintamamiestalo (owner-built; cheap; slow), **R-row** (*rivitalo*), **R-mid** (lamellitalo), **R-high** (*elementtitalo*/pistetalo). Optional **ARAVA flag** on a zone (state-financed rental: cheaper, standards, rent cap).
- **Asunto-osakeyhtiö** (the Finnish housing company where flat owners own shares): a flavour detail for the "owner-occupied" mode.
- **Agriculture**, **Forestry**, **Peat bog**, **Summer cottage shore** (*mökki*, needs a lakeshore and roads; tax and happiness; a weekend traffic jam event), **Allotment garden**.
- **State industry** (only by decree; big employment; a Moscow-trade producer).
- **Garrison** (*varuskunta*): conscription army barracks; many towns had one; jobs and young men.
- **Company town** (*tehdasyhdyskunta*): a factory owner builds and runs housing; a scenario start.

### 5.2 Housing shortage (the core tension)

- In the 1940s-50s, **overcrowding** was severe: many families lived in **one room and a kitchen**, and
  shared toilets in the yard. The 1960s *lähiö* flats with **a bathroom, central heating and a basement sauna**
  felt like luxury. Show it as a "housing standard" ladder: *wooden house with a stove* → *ARAVA flat with district heating* → *own row house with a sauna*.
- Migrants arrive faster than housing; queues for ARAVA flats (*asuntojono*). If housing lags, people
  leave for Sweden. If you build too fast and drab, you get the 1970s *lähiö* stigma (social problems, "sleeping suburbs").

### 5.3 Services that mattered (municipal duties)

Water and sewage, **electricity** (the town electricity works, *sähkölaitos*, often municipally owned),
**district heating**, gas (old gasworks, *kaasulaitos*), **schools**, **neuvola**, **health centre**,
**hospital district**, **library**, **fire service** (VPK volunteers + paid), **police** (state), **elderly
care**, **day care** (1973+), **public transport**, **snow ploughing**, **waste dump** (*kaatopaikka*),
**parish** (church tax, cemeteries), **post and telephone** (local phone co-ops and the state telegraph),
**Alko** (state), **employment office**.

### 5.4 Town growth ladder (suggested)

| Stage | Population | Status | Unlocks (example) |
|---|---|---|---|
| Kylä (village) | 0-500 | rural village | farm, sawmill, co-op shop, folk school, yard saunas |
| Kirkonkylä (church village) | 500-3,000 | *maalaiskunta* centre | church, dairy, VPK hall, bank, post office, dance pavilion |
| Kauppala (market town) | 3,000-10,000 | **by decree** (until 1977) | market square, Alko, cinema, railway station, health clinic, *lamellitalo* |
| Kaupunki (town) | 10,000-40,000 | **town rights by decree** | town hall, hospital, water tower, *oppikoulu*, ice rink |
| Maakuntakeskus (regional centre) | 40,000-100,000 | | central hospital, university or college, element suburbs, shopping centre, ski jump |
| Suurkaupunki (big city) | 100,000+ | | concert hall, motorway, tram or metro plans, nuclear power, conference centre |

### 5.5 Economy flavour

- **Wage settlements** (*tupo*, from 1968): a yearly negotiation between unions (SAK), employers and the government (in-game: a slider for wages vs inflation vs strikes).
- **Devaluations** (1957, 1967, and later) are the typical tool: exports up, imports dear.
- **Agrarian politics**: the Agrarian League / Centre Party (Kekkonen's party) defends small farmers; subsidies.
- **Five-year Soviet trade framework** (see 6.2).

---

## 6. The Kekkonen satire layer

### 6.1 Tone guidance

- **Affectionate, dry, Finnish deadpan.** Think of a stern but caring headmaster who has been
  headmaster for 25 years and assumes he always will be. The joke is the *system* (Finlandization's
  self-censorship, the election everyone knows the result of, the bureaucratic paperwork, the
  sauna politics), not cruelty to people.
- Finns themselves have long joked about the era. Keep the humour **knowing and nostalgic**, like a
  family story about a formidable grandfather.
- **Historically grounded:** almost every mechanic below is a real quirk exaggerated a little.
- **No jokes about** war dead and veterans, the evacuees' loss of their homes, real victims of repression, or alcoholism as such.
  The *sankarihauta*, the evacuee arrival and the war memorials are solemn.
- **The USSR is "the big neighbour in the East"** (*itänaapuri*): a weather system the player must
  manage, not a cartoon villain or a hero. Given the post-2022 context, avoid nostalgia *for the USSR*;
  the satire targets Finnish accommodation of it.

### 6.2 Presidential mechanics (ideas)

| Mechanic | Real basis | Game idea |
|---|---|---|
| **Moscow Relations meter** (*Idänsuhteet*) | YYA treaty, Night Frost 1958, Note Crisis 1961 | 0-100. High: cheap oil, big clearing orders, stability. Low: a "frost" (trade freeze, literally colder winters as a joke). It decays if you lean West. Western trade (EFTA/EEC) raises prosperity but lowers the meter: **keep the balance** |
| **Clearing trade quota** (*clearing-kauppa*) | Five-year frame agreements, yearly goods lists, a bilateral balance | Every 5 years a **goods list** arrives ("3 icebreakers, 40,000 pairs of shoes, 2 paper machines"). Factories that fill it pay well; oil comes back. **The account must balance**: export too little and oil is cut; too much and you accumulate useless roubles (buy Ladas!) |
| **Decrees** (*asetus*, with a § symbol) | The president's wide powers, town rights by decree | A card system: "Town rights granted", "State steelworks to be built here", "Dissolve the council" (1975 parliament dissolution), "Midsummer bonfire subsidy". Each costs political capital |
| **The Letter** (*kirje*) | Kekkonen's stream of directive letters to ministers, officials and editors ("Kirjeitä myllystäni", *Letters from my mill*, published in the 1970s [?]) | Send a letter to a department or minister: an immediate effect (a project is fast-tracked, an editor softens a headline), cooldown, a small "fear" side effect. The UI is a typed letter with the presidential signature |
| **Cabinet carousel** | Finnish cabinets in this era lasted about a year on average; the president formed them | A random "government crisis" every few years: pick a coalition (Agrarian, Social Democrat, Popular Democrat, Conservative, Swedish People's...) with different buffs |
| **Elections that always go one way** | 1956 (151-149), 1962, 1968, 1973 term extension, 1978 (259/300) | The election is never lost; the **margin** is the score (approval). Low approval means a "humiliating 151-149" cutscene and weaker decrees. Rare event: "Parliament extends your term by law: no election this time" |
| **Sauna diplomacy** | The Tamminiemi sauna, Khrushchev 1960 | Build the **presidential sauna** (unique). Visiting delegations: a short choice-based "löyly" mini-event (throw water? talk trade? stay until 5 a.m.?). The outcome changes relations and trade |
| **Skiing and fishing trips** | His Lapland ski treks, fishing, hunting | Seasonal "President's ski trip": the government runs on autopilot (random events) while popularity rises; at the 80th birthday, "the President skied 30 km" (exaggeration, flagged as legend) |
| **The embassy contact** | The real slang *kotiryssä* (a politician's personal Soviet contact) | Optional, gentle: an "embassy tea" advisor who gives hints on what Moscow wants (keep it light, no spy thriller) |
| **Self-censorship meter** (*suomettuminen*) | Finlandization: removing books, softening films, editors' caution | A trade-off slider: higher self-censorship raises Moscow relations and lowers culture happiness and Western prestige. News headlines get blander as it rises |
| **Gift statues and friendship towns** | Soviet twin-city gifts | Event: "A friendship town sends a statue of a certain bald revolutionary." Place it (relations up) or "store it in the park depot" |
| **"Eastinghouse"** | Loviisa nuclear plant | Choose an Eastern reactor (cheap, relations up), a Western one (expensive, reliable), or the Loviisa compromise (Eastern core plus Western safety: best of both, slow) |
| **Agrarian heart** | Kekkonen's Agrarian League roots | Farm subsidies decree; a rural voter bloc |
| **The prestige track** | Olympics 1952, CSCE 1975, SALT 1969 | International event bids: host a summit (needs a hotel, concert hall, airport); world prestige |
| **Retirement** | Resigned 1981 aged 81 | Endgame: "The President takes a well-deserved rest". A score summary as an official honours list |

### 6.3 Presenting "the President"

- The real Kekkonen: **tall, bald dome, round glasses** (thicker frames later), athletic, ski gear and
  fur hat in winter, a dark suit otherwise. Nicknames **UKK** and **Urkki**. Residence **Tamminiemi** (a villa in Helsinki).
- **Recommended:** a **caricature in the game's pixel style**, never photographs. Consider naming the
  player character simply **"Presidentti"** or a lightly fictionalised name (e.g. "Presidentti Urho
  K.", or a pun like "Presidentti Kekkola") so the satire is clearly a *game version*. The setting and
  events can still be historical.

### 6.4 Real-person sensitivities (brief)

- Kekkonen died in 1986; he remains **admired and criticised** in Finland. Relatives are alive, and the Tamminiemi
  museum and Urho Kekkonen foundation exist. Satire of a head of state's public role is normal; avoid
  inventing private scandals, illness jokes (his final illness and dementia), or anything implying real
  criminal acts.
- Finland's Criminal Code has an offence of *insulting the memory of a deceased person* (aimed at
  false claims that hurt relatives). Affectionate period satire is very far from it, but it is one more
  reason to keep the humour about the *politics*.
- Other real people (Khrushchev, Brezhnev, Finnish ministers) should stay **unnamed or generic**
  ("the Ambassador", "the Delegation from Moscow", "the Prime Minister").
- **Trademarks and copyright:** do not use real logos or patterns (Marimekko Unikko, Moomins, Alko, Elanto,
  K, S, R-kioski, VR, Valio, Fazer). Make pastiches (section 8.6).

---

## 7. Graphic design and UI of the era

### 7.1 The look

- **Finnish modernism (1950s-60s):** **clean, geometric, functional**, with Swiss-style grids and sans-serifs
  (Helvetica, Univers and Akzidenz in the 1960s; **Futura** in the 1950s), lots of white space, **bold flat colour**.
  The "Finnish design" wave won Milan Triennale prizes in the 1950s (Wirkkala, Franck, Sarpaneva).
- **Posters:** **Erik Bruun** (bold flat-colour nature and product posters), Martti Mykkänen, Kyösti Varis;
  election posters with strong slogans and photographic faces; Finnair/Aero travel posters.
  Characteristics: flat shapes, 2-4 colours, big type, simplified nature motifs (a fish, a bird, a pine, the sun).
- **Products:** **Marimekko** (founded 1951; huge flat flowers and stripes, red/black/orange/pink;
  *Unikko* 1964), **Arabia** ceramics (Kaj Franck's plain *Kilta* in solid colours, 1953; brown
  stoneware in the 1960s-70s), **Iittala** glass (Wirkkala, Sarpaneva; *Ultima Thule* 1968), Aarikka wooden beads,
  Eero Aarnio's Ball Chair (1963) and Pastil (1967) **space-age** plastic in orange and red.
- **1970s:** **orange, brown, mustard, olive green**, rounded rectangles, rainbow stripes, chunky type.
- **State graphics:** official forms and stamps: **off-white paper, black or dark-blue type,
  red rubber stamps** ("HYVÄKSYTTY" approved, "SALAINEN" secret), the **lion coat of arms**, typewriter text
  (Courier-like) for letters and decrees. The **blue cross flag** blue.
- **Signage:** shops used **hand-painted or metal letters** on fascias and neon on roofs; bilingual station signs.

### 7.2 Suggested UI palette

Two layers: a **paper and ink** base (period documents) and **design accents** (Marimekko and 1960s posters).

| Role | Name | Hex |
|---|---|---|
| Panel background | Paper | `#F2ECDD` |
| Panel alt / inset | Aged paper | `#E4DAC2` |
| Panel border / shadow | Grey card | `#B9AE96` |
| Main text | Ink | `#1E1E1E` |
| Secondary text | Pencil | `#5A5650` |
| Headings, presidential | Flag blue | `#003580` |
| Light blue | Lake blue | `#5E8DB8` |
| Alert / stamp | Stamp red | `#C8102E` |
| Positive | Pine green | `#3E6B45` |
| Warning | Ochre | `#D2A030` |
| Accent 1 (1960s) | Marimekko orange | `#E2711D` |
| Accent 2 | Poppy pink-red | `#D63A4A` |
| Accent 3 (1970s) | Olive | `#76803A` |
| Accent 4 (1970s) | Brown | `#6B4A33` |
| Dark UI (map overlay) | Night slate | `#2B313A` |
| Moscow meter | Soviet-ish red | `#B22222` |
| West meter | EFTA blue | `#2F6DB5` |

UI ideas: **typed-letter** dialogs for decrees and letters (paper with a red stamp); **newspaper
front-page** popups with a headline in a condensed bold sans and a halftone pixel photo;
**1960s TV** frame for big events (a rounded-corner screen, scanlines); toolbar icons as **flat 2-3 colour
pictograms** in the Bruun poster style; the speed and date as a **wall calendar** or a **brass clock**.

### 7.3 Typography for the bitmap font

- A clean **geometric or grotesque sans** (Futura/Helvetica feel) for UI; a **typewriter** face for
  letters and decrees; optionally a **condensed bold** for newspaper headlines.
- Number and date format: **12 500 mk** (space as thousands separator), **3,5 %** (decimal comma),
  dates **1.3.1956**, time **18.30**. Finnish quotes use **”...”** (the same mark on both sides).
- Glyphs needed: see 8.4.

---

## 8. Naming (for procedural generation)

### 8.1 Town names

Build them as **prefix + suffix** (written as one word; some suffixes take vowel harmony, see below).

**Prefixes (nature, places, trades):**
Kuusi (spruce), Mänty* (pine), Koivu (birch), Haapa (aspen), Leppä* (alder), Pihlaja (rowan), Petäjä* (pine),
Kataja (juniper), Tammi (oak), Saarni (ash), Kivi (stone), Kallio (rock), Hiekka (sand), Savi (clay), Rauta (iron),
Kulta (gold), Hopea (silver), Vaski (copper), Karhu (bear), Susi (wolf), Hirvi (elk), Kettu (fox), Joutsen (swan),
Kotka (eagle), Haukka (hawk), Lohi (salmon), Hauki (pike), Ahven (perch), Kala (fish), Mylly (mill), Kirkko (church),
Kartano (manor), Silta (bridge), Ranta (shore), Saari (island), Uusi (new), Vanha (old), Iso (big), Pikku (little),
Ala (lower), Ylä* (upper), Keski (middle), Pohjois (north), Etelä* (south), Itä* (east), Länsi* (west), Kylmä* (cold),
Lämmin* (warm), Musta (black), Valko (white), Sini (blue), Puna (red), Kelta (yellow), Viher (green), Aurinko (sun), Kuu (moon), Tähti* (star).

**Suffixes:**
-joki (river), -järvi* (lake), -koski (rapids), -lahti (bay), -niemi (cape), -salmi (strait), -saari (island),
-vesi (water), -lampi (pond), -virta (stream), -mäki* (hill), -vaara (fell hill; northern), -harju (esker),
-kangas (heath), -nummi (moor), -suo (bog), -kylä* (village), -linna (castle), -ranta (shore), -selkä* (open lake/ridge),
-kumpu (knoll), -kallio (rock), -pää* (end), -la/-lä* (place of; e.g. Kivelä), -nen (diminutive, rare in town names).

**Examples:** Kuusijoki, Koivulahti, Mäntyharju*, Hirvikoski, Kotkaniemi, Rautavaara, Lohisalmi,
Pikkujärvi*, Haapakylä*, Kivimäki*, Joutsenlinna, Myllykoski, Keskiranta, Petäjävesi* (real!),
Kalliolampi, Uusikaupunki (real!), Ylämylly*, Lännenkoski*, Kultaranta (real; Kekkonen's summer residence! avoid), Mustasaari (real).

Note: many combinations are real places (Finland has ~300 municipalities and thousands of villages).
That's fine for districts, but the main town name should be checked against a list of real municipalities.

**Swedish coastal equivalents** (bilingual towns): -by (village), -berg (hill), -vik (bay), -näs* (cape), -holm (islet),
-sund (strait), -å* (river), -dal (valley), -backa (slope), -fors (rapids), -stad (town).
Examples: Björkby, Granvik, Ekenäs* (real), Kvarnfors, Stenholm, Ålands-* (avoid, Åland is special).

### 8.2 District names

Suffixes: **-mäki*, -lahti, -niemi, -kylä*, -puisto (park), -rinne (slope), -kallio, -harju, -nummi,
-kangas, -metsä* (forest), -laakso (valley), -ranta, -saari, -kumpu, -kulma (corner), -pelto (field), -kartano (manor),
-keskus (centre), -vainio (field), -niitty (meadow), -koti (home), -la/-lä*** (Kontula, Hervanta-style).

**Real lähiö patterns:** Pihlajamäki, Myllypuro, Kontula, Hervanta, Haukilahti, Puotila, Kannelmäki,
Lauttasaari, Tapiola, Otaniemi, Kaleva, Koivukylä, Karjasilta, Nokela, Herttoniemi, Roihuvuori.

**Fictional examples:** Koivurinne, Kalliopuisto, Pihlajakumpu, Mäntymäki*, Kuusikallio, Haukiniemi,
Joutsenlaakso, Tähtimäki*, Ruusulaakso, Kivikylä*, Myllyranta, Hiekkaharju, Kotikallio, Kesäniitty*, Peltola.

### 8.3 Street names

Suffixes: **-katu** (street, town grid), **-tie** (road), **-kuja** (lane), **-polku** (path), **-rinne** (slope),
**-kaari** (crescent), **-raitti** (village lane), **-väylä*** (artery), **-puistikko** (small park strip),
**-tori** (square), **-aukio** (plaza), **-ranta** (quay), **-portti** (gate), **-piha** (court), **-sola** (passage).

Note the genitive: names usually take a genitive first part: *Kirkko* → **Kirkkokatu**, but *Koulu* → **Koulukatu**,
*Myllymäki* → **Myllymäentie*** (consonant gradation!). Safest for generation: use **compound nominative stems**
(Koivu-tie, Kivi-katu, Pihlaja-kuja), which are correct and common.

**Thematic sets** (1960s planners loved themes; Pihlajamäki uses rock types):

- **Rocks and minerals:** Graniitti-, Gneissi-, Rapakivi-, Vuolukivi-, Kvartsi-, Maasälpä-*, Kiille-.
- **Trees:** Koivu-, Kuusi-, Mänty-*, Pihlaja-, Tuomi- (bird cherry), Vaahtera- (maple), Lehmus- (lime).
- **Birds:** Tikka- (woodpecker), Tiainen- (tit), Peippo- (finch), Rastas- (thrush), Kurki- (crane), Pöllö-* (owl), Kiuru- (lark).
- **Kalevala:** Väinämöisen-*, Ilmarisen-, Lemminkäisen-*, Kalevan-, Pohjolan-, Sammon-, Ainon-.
- **Trades (old town):** Sepän- (smith), Suutarin- (cobbler), Kauppias-, Tullin- (customs), Satama- (harbour).
- **Historic/civic:** Kirkko-, Koulu-, Raatihuoneen-, Asema- (station), Tehtaan- (factory), Sairaala-, Puisto-, Tori-.
- **Nature:** Kallio-, Lähde-* (spring), Puro- (brook), Niitty-, Kanerva- (heather), Mustikka- (blueberry), Puolukka- (lingonberry).
- **Abstract 1970s:** Rauhan- (peace), Ystävyyden-* (friendship; YYA joke), Edistyksen-* (progress), Tulevaisuuden- (future).

### 8.4 å/ä/ö: what the bitmap font must include

- **Finnish uses Ä/ä and Ö/ö constantly** (järvi, mäki, kylä, Pöllö, Ystävyys...). They are separate letters, not
  decorations, and are mandatory.
- **Å/å** appears only in **Swedish** names (Åbo = Turku, Ekenäs is ä; å in Kvarnå, Ålands-). Needed for
  bilingual signs.
- Also recommended: **é** (Café), **ü** (rare, German loans; optional), **§** (decrees), **”** (Finnish quotes),
  **–** en dash, **½** (1½-storey in tooltips), **°** (temperature: −25 °C), **−** minus, **€ not needed** (markka era; use "mk").
- **Words in this document that need ä/ö** are marked with * in the lists above. Approximately half of generated
  Finnish names will contain ä or ö.
- Vowel harmony note for generators: Finnish suffixes with a/o/u vs ä/ö/y follow the stem
  (*Kivelä* but *Kotkala*; -la/-lä). With compounds (two separate words joined), there is **no harmony issue**,
  so prefix + suffix compounding is safe.

### 8.5 People names (citizens, ministers, letters)

- **Surnames (very common):** Virtanen, Korhonen, Nieminen, Mäkinen*, Hämäläinen*, Laine, Heikkinen, Koskinen,
  Järvinen*, Lehtonen, Lehtinen, Saarinen, Salminen, Heinonen, Niemi, Heikkilä*, Kinnunen, Salonen, Turunen, Salo,
  Laaksonen, Tuominen, Rantanen, Karjalainen (typical evacuee name), Jokinen, Mattila, Savolainen, Lahtinen.
- **Men born ~1900-1930 (the adults):** Väinö*, Toivo, Eino, Lauri, Veikko, Tauno, Viljo, Arvo, Erkki, Martti, Paavo, Aarne, Urho.
- **Women born ~1900-1930:** Aino, Aili, Helmi, Lyyli, Elsa, Hilja, Martta, Saimi, Kerttu, Irja, Anna-Liisa.
- **Boom babies (born 1945-1965):** Matti, Timo, Kari, Jari, Mikko, Pekka, Juha, Markku, Seppo, Jukka, Hannu, Heikki, Pertti,
  and Tuula, Ritva, Pirjo, Sirpa, Leena, Anneli, Marjatta, Päivi*, Tarja, Merja, Riitta, Hannele, Eeva, Kaija.
- **Swedish-speaking:** Lindqvist, Johansson, Holmström*, Nyberg, Sjöberg*, Andersson; Bengt, Göran*, Gunnel, Margareta.

### 8.6 Companies, brands, institutions (fictional but believable)

Formats: **"X Oy"**, **"Oy X Ab"** (bilingual), **"X:n Osuuskauppa"**, **"Kansallinen X"**,
"Valtion X" (state), "Suomen X" (Finland's X), "X & Co", "X Tukku" (wholesale), "Kauppahuone X" (trading house).

| Category | Examples |
|---|---|
| Paper / pulp | Kuusikosken Paperi Oy, Oy Pohjolan Selluloosa Ab, Järviseudun Puuhiomo, Koskensaaren Tehtaat |
| Sawmill | Hirvisaaren Saha, Rantalan Höyrysaha*, Lohilahden Puutavara Oy |
| Metal / state | Valtion Konepajat (VKP), Suomen Valssi Oy, Pohjolan Rauta, Kalevan Telakka, Valtion Tykkitehdas (joke) |
| Textile / shoes | Koskelan Puuvilla Oy, Kotimaan Kenkä Oy, Kutomo Aalto, Hirvikosken Vaatetehdas |
| Food / drink | Järviseudun Meijeri, Lihapojat Oy, Kotkan Leipä, Pohjolan Panimo, Kultainen Kahvi (Golden Coffee) Oy, Virvoke Oy |
| Co-op shops (pastiches) | **"Osuusliike Toivo"** (E-style, progressive), **"Osuuskauppa Maakunta"** (S-style), **"M-kauppa"** (private K-pastiche), Tavaratalo Pohjola, Kauppa-Aitta |
| Kiosk | "Q-kioski" (R-kioski pastiche), Torikioski, Grilli-Pekka |
| Alcohol shop | "Valtion Alkoholiliike", "VIINAMYYMÄLÄ*" (generic) |
| Bank | Pohjolan Säästöpankki*, Kansan Osuuspankki, Maakuntapankki, Yhtymäpankki* |
| Petrol | "Esso-like" → **Essa**; Teboil-like → **Neuvo-Oil** (joke on *Neuvostoliitto*, the Soviet Union) [tone check], Kotimaan Polttoaine |
| Utilities | Kaupungin Sähkölaitos*, Vesilaitos, Lämpölaitos*, Kaasulaitos |
| Car importer | **"Konela"-pastiche: Itä-Auto Oy*** (East-Auto); Western: Länsi-Moottori Oy* |
| Bus | Järviseudun Liikenne Oy, Koiviston-pastiche: Kotkan Auto, Pohjolan Linjat, Postiliikenne |
| Media | Kaupungin Sanomat, Järviseudun Uutiset, Kansan Ääni* (the left), Maan Viesti (agrarian), "Kuvalehti" |
| State bodies | Asuntohallitus (Housing Board, ARAVA), Tie- ja vesirakennushallitus (Roads and Waterways Board), Kauppa- ja teollisuusministeriö, Valtion Rautatiet (VR) → in-game "Valtion Radat" |

---

## 9. Art direction brief

1. **Simple boxes, strong roofs.** Steep gables for wood (1940s-50s), low gables for early blocks, flat roofs for the 1960s-70s. Roof shape tells the era at a glance.
2. **White trim on coloured wood** is the signature of the *rintamamiestalo*: red ochre, yellow ochre, pale yellow, pale blue and greens with white corners and window frames, grey metal roofs, and a central chimney with a roof ladder.
3. **Same form, varied colour.** Generate streets of identical houses with weighted random wall colours, mirrored porches and garden props, as real veterans' suburbs look.
4. **Window rhythm is the lähiö.** Regular window grids, vertical stairwell strips, and (for the 1970s) visible precast panel joints and stacked balcony towers with orange, mustard and olive balcony fronts.
5. **Granite and pines everywhere.** Buildings sit on rounded grey-pink rock with lichen and orange-trunked pines left between blocks. No manicured lawns until the parks.
6. **Water is dark.** Humic blue-brown lakes, reeds, rocky shores, wooden jetties and saunas. Rapids power mills.
7. **Earthy base palette, bright accents.** The landscape and buildings are muted (ochres, greys, dark greens); accents come from balcony panels, shop signs, trams (yellow over green), flags (white and blue) and 1960s neon.
8. **Industry is red brick, white render and steam.** Tall chimneys, white plumes, log ponds and timber stacks; sawtooth roofs for engineering works.
9. **Landmarks are modernist.** Water towers (saucer, mushroom, cylinder), white Aalto-style churches and towers, hose-drying towers on fire stations, ski jumps. Make them customisable skyline pieces.
10. **Props tell the Finnish story:** flagpoles, woodpiles, carpet-beating racks, milk-can platforms, hay barns in ditched fields, block-heater posts, catenary street lamps and wooden power poles.
11. **Winter is a first-class palette.** Snow on roofs (melting first on heated buildings), blue shadows, frozen lakes with ice roads, ploughed snow banks, lit windows and street lamps in the long dusk.
12. **Era drift.** The palette and props shift over the game: gravel to asphalt, mercury-white to sodium-orange lights, white window frames to brown, Moskvich to Lada, steam to electric trains.
13. **Mixed East-West traffic.** Moskvich and Volga next to the Beetle and Saab; Soviet-built red electric locomotives on Russian-gauge track. A quiet visual joke that is also true.
14. **UI as period paperwork.** Off-white paper, ink-black and flag-blue type, red rubber stamps, typed decrees with a § sign, newspaper popups, Bruun-style flat pictogram icons, Marimekko-orange highlights.
15. **Affectionate, never cruel.** Satire targets the system (endless presidency, Moscow balancing, self-censorship), never the war dead, the evacuees or ordinary people.

---

## Sources (selected)

- Rintamamiestalo: [Imatra rintamamiestalo guidelines (PDF)](https://www.imatra.fi/sites/default/files/atoms/files/14_08Rintamamiestalot.pdf),
  [imatra.fi guide page](https://imatra.fi/print/pdf/node/4874), [Kaleva: almost 300,000 built](https://www.kaleva.fi/suomeen-nousi-lahes-300-000-rintamamiestaloa/1781793),
  [Yle on the design](https://yle.fi/a/3-7622147), [Iisalmi building guide, earth pigments (PDF)](https://iisalmi.fi/wp-content/uploads/2025/12/6b-Rakentamistapaohje-liite-1.pdf),
  [Hämeenlinna small-house inventory (PDF)](https://www.hameenlinna.fi/wp-content/uploads/2021/02/Pientalojen_ominaispiirteet_rakennusinventointi_2020.pdf)
- Colours: Helsinki colour plan, [1950-60s stone buildings](https://www.hel.fi/static/rakvv/varikaava/htm/20012.htm) and [1950-60s wooden houses](https://www.hel.fi/static/rakvv/varikaava/htm/20014.htm)
- Estates: [Pihlajamäki architecture trail](https://www.hel.fi/hel2/kanslia/pihlajamaki_arkkitehtuuripolku/esittely.html),
  [Tapiola](https://en.wikipedia.org/wiki/Tapiola), [Hervanta](https://en.wikipedia.org/wiki/Hervanta),
  [Rovaniemi reconstruction plan](https://lapinkavijat.rovaniemi.fi/aalto/jkaava_en.html), [Reindeer antler plan](https://visit.alvaraalto.fi/en/destinations/reindeer-antler-city-plan/),
  [Myllypuro area report (PDF)](https://www.hel.fi/hel2/Tietokeskus/helsinki_alueittain_2005/702Myllypuro.pdf)
- Precast facades: [BeKo facade study (Betoni, PDF)](https://betoni.com/wp-content/uploads/2015/09/BET1104_38-43.pdf)
- Water towers: [Haukilahti](https://en.wikipedia.org/wiki/Haukilahti_water_tower), [Lauttasaari](https://en.wikipedia.org/wiki/Lauttasaari_water_tower),
  [Kaanaa](https://finnisharchitecture.fi/kaanaa-water-tower/), [Riihimäki](https://www.finnisharchitecture.fi/projects/riihimaki-water-tower), [Karjaa](https://en.docomomo.fi/projects/karjaa-water-tower/)
- Kekkonen: [Wikipedia: Urho Kekkonen](https://en.wikipedia.org/wiki/Urho_Kekkonen), [Tamminiemi history (National Museum)](https://www.kansallismuseo.fi/en/tamminiemi/tamminiemen-historiaa),
  [Verkkouutiset on the Kekkonen-Khrushchev sauna night](https://www.verkkouutiset.fi/a/kekkosen-ja-hrustsovin-oinen-saunatoilailu-helsingissa-paatyivat-oikein-kunnon-hiprakkaan-54494/),
  [Countryfile on sauna diplomacy](https://www.countryfile.com/health/sauna-diplomacy)
- Migration: [Siirtolaisuusinstituutti / journal.fi on 1970s emigration](https://journal.fi/fypr/article/view/44715), [Finland Abroad centennial story 1967-77](https://finlandabroad.fi/web/usa/current-affairs/-/asset_publisher/h5w4iTUJhNne/content/centennial-story-of-finland-part-7-developing-welfare-services-and-hosting-major-helsinki-conference-1967-1977/384951)
- Resettlement: [luovutettukarjala.fi resettlement material (PDF)](https://luovutettukarjala.fi/historia/20170824_asuttaminen.pdf)
- Clearing trade: [Bank of Finland bulletin 1987/2 (PDF)](https://publications.bof.fi/bitstream/10024/48947/1/Bofbul_1987-02.pdf), [Bank of Finland bulletin 1981/9 (PDF)](https://publications.bof.fi/bitstream/10024/49821/1/Bofbul_1981-09.pdf), [Finnish trade with the USSR: why was it different?](https://urn.fi/URN:NBN:fi:bof-2014080843)
- Alcohol: [Keskiolut](https://en.wikipedia.org/wiki/Keskiolut), [Alko history](https://www.alko.fi/en/alko-inc/about-alko-history-of-alko)
- Rationing: [Moottori: 80 years since the coffee ship](https://moottori.fi/uutinen/tasan-80-vuotta-sitten-suomeen-saapui-kauan-odotettu-laiva-jonka-lasti-oli-kahvikansalle-kultaakin-arvokkaampaa/)
- Nuclear: [Loviisa Nuclear Power Plant](https://en.wikipedia.org/wiki/Loviisa_Nuclear_Power_Plant)
- Roads and signs: [Road signs in Finland](https://en.wikipedia.org/wiki/Road_signs_in_Finland), [Yle on yellow lines being repainted](https://yle.fi/a/3-11218779)
- Trams: [History of trams in Helsinki](https://en.wikipedia.org/wiki/History_of_trams_in_Helsinki), [Karia HM V](https://en.wikipedia.org/wiki/Karia_HM_V)
- Cars: [Best Selling Cars Blog, Finland 1956](https://bestsellingcarsblog.com/1957/01/finland-1956-russian-moskvitsh-and-pobeda-dominate), [Finland 1959-62](https://bestsellingcarsblog.com/1963/01/finland-1959-1962-moskvitsh-reigns-skoda-and-volkswagen-follow/), [Jalopnik on the Moskvitch in Finland](https://jalopnik.com/mighty-moscovitie-the-moskvitch-400-in-finland-208239)
- Municipal status: [Maalaiskunta (Wikipedia)](https://en.wikipedia.org/wiki/Maalaiskunta)
- Kiosks: [R-kioski](https://en.wikipedia.org/wiki/R-kioski)
- Climate: [FMI: Seasons in Finland](https://en.ilmatieteenlaitos.fi/seasons-in-finland), [FMI snow statistics](https://en.ilmatieteenlaitos.fi/snow-statistics)

### Open questions to verify before writing in-game "fact" text

- The exact dimensions and roof pitches of specific *rintamamiestalo* type plans (the Museum of Finnish Architecture, RT-cards).
- The 1950s-70s liveries of postal buses, police cars, VR diesel and electric units, and phone booths.
- When yellow centre lines were introduced; the first motorway section date.
- The exact peak year of housing production (early-to-mid 1970s).
- The opening year of Kontula's shopping centre; Tapiola central tower details.
- The publication date of "Kirjeitä myllystäni"; the date the Lenin busts were removed.
