# Gate-D framing review: proposed final wording (2026-10-21)

This is a proposal for the director's Gate-D framing review. It covers every row of `docs/qa/2026-10-15/gate-d-wording.md` (D1-D166), plus three player-facing placeholders that list does not have (U1-U3).

**How it was made:**
- Each current string was found in today's source or data and is quoted verbatim, with its `file:line` as of 2026-10-21.
- The inventory's 10-15 line numbers are stale, and several rows changed since. Where a row is now built from pieces, each piece is quoted.
- Nothing has been changed in code or data. This file is the only edit.

**Totals:** 169 rows: 43 proposed, 122 kept, 4 retired (their strings no longer exist), and 9 mechanics flagged for the director.

**Answered 2026-10-21: the director approved ALL proposed rows as written.** The flagged mechanics F1-F9 are not yet ruled. Application follows D1 in one commit (strings, CSV rows with DT live-sync, and any suite assertions on alert text).

**To answer,** write something like: "approve all proposed except D21: keep STRAINED; F4: discuss". Approved text is applied exactly as written. CSV rows need a DT live-sync. Only strings change, so the battery baseline must stay identical, except suites that assert on alert text, which will be updated in the same commit.

**File key:**
- **SIM** = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`
- **SIMG** = `.../RHSimGrowth.cpp`
- **SIMD** = `.../RHSimDistricts.cpp`
- **PANELS** = `Source/RedHope/Private/RHCommandDeckPanels.cpp`
- **DECK** = `.../RHCommandDeck.cpp`
- **GUIDE** = `.../RHCommandDeckGuide.cpp`
- **CREWVIZ** = `.../RHCrewVisualizerSubsystem.cpp`
- **COLONYVIZ** = `.../RHColonyVisualizerSubsystem.cpp`
- **PC** = `.../RHPlayerController.cpp`

---

## The directive, in its own words

- `docs/design-decisions.md:11`: "Mental-health theme: prevention-focused systems, abstracted consequences, never graphic, never trivialized."
- `docs/design-decisions.md:109`: "build mechanics abstract now, review the framing before ship ... A1's only failure surface is evacuation: abstract, prevention-framed, never harm imagery."
- `docs/design-decisions.md:172`, the death-spiral guardrail: "any Hope->work->Hope feedback must be carrot-first and recoverable ... consequences abstracted, recoverable, prevention not punishment."
- `docs/m2-working-spec.md:50`: evacuation is "framed as the Program pulling people back to safety, never harm imagery."
- `docs/the-red-hope-design-brief.md:45`: "consequences and prevention systems, not graphic depiction. Prevention is the gameplay."

The five patterns at the end of the 10-15 inventory are followed here: one recall vocabulary; the fix first; systems fail, people don't; a diplomatic register; and only player words on screen.

---

## Glossary (use these words everywhere)

| Say | Never say | Notes |
|---|---|---|
| **recalled to orbit** (the act), **safe in orbit** (the state) | evacuated, returned, lost, removed, failed, rejected | The Program recalls people to keep them safe. "Recalled" is the Program's act, so it reads right for a Mars-born child too. |
| **come home to Mars** | come back | It works for people born here and for people who left. |
| **short rations** / **full rations**; **on short rations** (a count) | unsupported, needs support, starving, hungry | A shortage is a supply's state, never a person's. |
| **Hope** | morale | The game's own word. |
| Hope bands: **LOW**, **UNSETTLED**, STEADY, THRIVING, FLOURISHING | FAILING, STRAINED | The bands describe the colony's mood; they don't judge it. "Strained" is kept for machines and supplies only. |
| **at odds**, **tension** | arguing, heated words, raised voices | Words, never blows (decision 2026-07, `design-decisions.md:267`). |
| **stayed aboard** (a ship that could not land its people) | returned, turned away | Always say they are safe. |
| **the quiet colony** (everyone recalled) | collapse, abandonment, stands empty | A state you can recover from. |
| **dry spell** (the environmental crisis) | environmental strain, stress test | |
| **side with Earth**, **relations frozen**, **reassure**, **drop sharply** | defect (as a verdict), hostile, pacify, crater, "they will remember" | The diplomatic register. |
| **residents** (people in homes), **settlers** (newcomers), **crew** (the first colonists), **born on Mars** | | Use "people" in plain prose. |
| **awaiting a post** | without a post, unemployed | |
| **work pace** | how hard they work | |
| **the Program** | | Earth's agency; the CEO keeps his placeholder. |

---

## Retired rows (no longer in the code)

| ID | Was | Why retired |
|---|---|---|
| D1 | `LIFE SUPPORT: %s is unsupported — %s. Evacuation in %.1f sols unless restored.` | C2a replaced per-person alerts with D123. The string is gone from Source; only a comment mentions it (SIM:4429). |
| D2 | `EVACUATED: %s returned to orbit — life support failed for %.1f sols.` | Replaced by D125. Gone from Source. |
| D3 | `needs support - recalled in %.1f sols` | Replaced by D127. Gone from Source. |
| D31 | `Flourishing - next arrival` | Superseded by D111. Gone from Source. |

---

## 1. Alerts: rations, recall and arrivals

| ID | Where | Current (verbatim) | Proposed | Reason |
|---|---|---|---|---|
| D123 | Alert banner, once per home · SIM:4287 | `SHORT RATIONS: %s — %s is running short for %d resident%s. Restore it to bring rations back to full; if it lasts, residents are recalled to orbit a few at a time.` | `SHORT RATIONS: %s — %s is running short for %d resident%s. Restore it to bring rations back to full; if it lasts, the Program recalls residents to safety in orbit, a few at a time.` | It names the Program's protective act, so the recall reads as care, not loss. |
| D125 | Alert banner, once per batch · SIM:4357 | `RECALLED: %d resident%s of %s recalled to orbit — rations stayed short for too long.` | `RECALLED: %d resident%s of %s, now safe in orbit — rations were short too long. Restoring the supply keeps the rest at home.` | It says they are safe, drops the blame in "stayed short", and names the fix. |
| D5 | Alert banner, a Crew Pod with no beds · SIM:1932 (with a rated habitat), SIM:1934 | `CREW POD RETURNED — no certified housing (%d beds free, %d needed). Zone Living Quarters bays in your rated habitat before the next ship.` / `CREW POD RETURNED — no certified housing (%d beds free, %d needed). Certify a vault floor before the next ship.` | `CREW POD STAYED ABOARD — %d beds free, %d needed; the crew is safe and flies back with the ship. Zone Living Quarters bays in your rated habitat before the next ship.` / `CREW POD STAYED ABOARD — %d beds free, %d needed; the crew is safe and flies back with the ship. Certify a vault floor before the next ship.` | "Returned" reads as rejection. This says they are fine, then names the fix. |
| D130 | Alert banner, a Colonist Transport with no beds · SIM:1958, SIM:1960 | `COLONIST TRANSPORT RETURNED — no certified housing (%d beds free, %d needed). Zone Living Quarters bays in your rated habitat before the next ship.` / `COLONIST TRANSPORT RETURNED — no certified housing (%d beds free, %d needed). Certify a vault floor before the next ship.` | `COLONIST TRANSPORT STAYED ABOARD — %d beds free, %d needed; everyone is safe and flies back with the ship. Zone Living Quarters bays in your rated habitat before the next ship.` / `COLONIST TRANSPORT STAYED ABOARD — %d beds free, %d needed; everyone is safe and flies back with the ship. Certify a vault floor before the next ship.` | As D5. |
| D134 | Alert banner, a move-home batch · SIM:6175 | `MOVED HOME: %d resident%s moved to free beds elsewhere — their homes no longer had a bed for them.` | `MOVED HOME: %d resident%s settled into free beds elsewhere — their old home's beds were removed or lost their rating.` | It says why in terms of the building, not the people, so nobody reads as displaced. |
| D13 | Alert banner, once, when everyone has been recalled · SIM:4027-4028 | `THE COLONY STANDS EMPTY — everyone has returned to orbit. The machines keep the lights on. Restore housing and life support, and people will come back to Mars.` | `THE COLONY IS QUIET — everyone is safe in orbit. The machines keep the lights on. Restore housing and life support, and people can come home to Mars.` | "Stands empty" is bleak, and "returned" is false for a Mars-born child. "Come home" fits everyone. |
| D9 | Alert banner · SIM:3794 | `GARDEN LOST: floor %d cell %d was re-zoned — the emplaced soil is forfeit.` | `GARDEN CLEARED: floor %d cell %d was re-zoned — its soil can't be recovered.` | "Lost" and "forfeit" are punitive for a zoning click. |
| D8 | Alert banner · SIM:4418 | `WATER QUALITY LOW — the crew is drinking heavily-recycled water. Drill the ice caps to restore it.` | `WATER QUALITY LOW — most of the water is recycled now. Fresh ice-melt restores it: drill more ice.` | It puts the state on the water, not on what people drink (as in D80), and names the fix the game actually offers. |

## 2. Hope, the bands and crew moments

| ID | Where | Current (verbatim) | Proposed | Reason |
|---|---|---|---|---|
| D20 | HOPE tile, Crew panel header · SIM:6300 | `FAILING` | `LOW` | Describes the mood without judging the people. |
| D21 | Same places · SIM:6301 | `STRAINED` | `UNSETTLED` | "Strained" is a stress word aimed at people (pattern 3). |
| D148 | City panel, the growth pause (`Growth %s.` at PANELS:1427) · SIMG:579 | `paused: the colony's Hope is failing` | `paused: the colony's Hope is low` | Matches D20. See also F5. |
| D23 | Crew moment text (not shown on screen today) · SIM:4540, SIM:4542 | `Raised voices in the surface habitat — %s and %s are arguing over the shortages. Morale is wearing thin.` / `Raised voices on floor %d — %s and %s are arguing over the shortages. Morale is wearing thin.` | `Tension in the surface habitat — %s and %s are at odds over the shortages. Restocking will ease it.` / `Tension on floor %d — %s and %s are at odds over the shortages. Restocking will ease it.` | No conflict imagery; "Hope", not "morale"; the fix is named. See F1. |
| D24 | Crew emote plate over both colonists · CREWVIZ:428-429 | `! heated words !` | `~ at odds ~` | The alarm punctuation reads as a fight. |
| D99 | Crew panel, Hope prose · PANELS:702 | `Food: about %d sol%s in store - running short weighs on the crew. Grow more: zone Gardening Station cells.` | `Food: about %d sol%s in store - a thin larder lowers Hope. Grow more: zone Gardening Station cells.` | It states the system effect instead of describing how people feel. |
| D4 | CREW tile caption · PANELS:330 | `%d need support` | `%d on short rations` | The one ration vocabulary (D127, D140); "need support" sounds needy. |
| D127 | Crew roster, amber line · PANELS:889 | `needs support - %.0f%% rations` | `short rations - %.0f%% of full` | As D4, and the same length. |

## 3. Crises

| ID | Where | Current (verbatim) | Proposed | Reason |
|---|---|---|---|---|
| D34 | Earth panel crisis line · SIM:8001 | `ENVIRONMENTAL STRAIN` | `DRY SPELL` | "Strain" echoes the old mood band; the mechanic is a water drain. |
| D35 | Alert banner · SIM:8019 | `SYSTEMS MALFUNCTION — automation across the colony stutters and stalls. Units are going inert; crews are stretched. It will pass, but it bites.` | `SYSTEMS MALFUNCTION — blowback from covert work: automation stalls, units go idle, work slows. It passes in a few sols.` | "It bites" is punitive, and "crews are stretched" puts the stress on people. This names the cause, which the player can change (SIM:8010). |
| D36 | Alert banner · SIM:8024 | `ENVIRONMENTAL STRAIN — a systems-stress test on the colony's reserves. Water demand spikes; hold together and it passes.` | `DRY SPELL — the colony's water draw spikes for a few sols. Keep the tanks full and it passes.` | "Hold together" implies people might not; this gives the fix. |
| D38 | Alert banner · SIM:8058 | `WEATHERED — the environmental strain has passed; the colony held.` | `WEATHERED — the dry spell has passed; the colony held.` | Follows D34. |

## 4. Endings

| ID | Where | Current (verbatim) | Proposed | Reason |
|---|---|---|---|---|
| D14 | Earth panel `On the path to: %s`, the collapse ending · SIM:8166 | `Abandonment / Collapse` | `The Quiet Colony` | Abandonment and failure framing for a state the colony can recover from. See F7. |
| D15 | Same panel and banners · SIM:8165 | `Martian Cold War` | `A Divided Mars` | War vocabulary; there is no combat, ever. |

## 5. Earth and neighbour politics

| ID | Where | Current (verbatim) | Proposed | Reason |
|---|---|---|---|---|
| D44 | Alert banner · SIM:8227 | `YOU DEFIED EARTH — the settlements stand together. Morale rises, but your sponsor's supply priority is cut. Survive on what Mars makes.` | `YOU DEFIED EARTH — the settlements stand together. Hope rises, but your sponsor's supply priority is cut. Mars will have to provide.` | "Survive" is a threat, and it says "Morale". |
| D45 | Alert banner · SIM:7962 | `EARTH LEANS ON %s — under pressure from Earth, they have suspended trade with you. Pacify them within %.0f sols or they defect.` | `EARTH LEANS ON %s — under pressure from Earth, they have suspended trade with you. Reassure them within %.0f sols or they side with Earth.` | "Pacify" is military and condescending. |
| D47 | Embargo card button · PANELS:1837 | `PACIFY  (spend influence)` | `REASSURE  (spend influence)` | As D45. |
| D48 | Alert banner · SIM:7989 | `PACIFIED — %s sees reason and ignores Earth's order. Trade resumes; tension eases.` | `REASSURED — %s sets Earth's order aside. Trade resumes; tension eases.` | "Sees reason" talks down to the neighbour. |
| D49 | Alert banner · SIM:7924 | `DEFECTION — %s has sided with Earth. The route is closed and relations are locked hostile.` | `DEFECTION — %s has sided with Earth. The route is closed and relations are frozen.` | "Hostile" escalates. |
| D66 | Refusal notice · SIM:7240 | `%s defected to Earth - relations are hostile, no trade` | `%s has sided with Earth - relations are frozen, no trade` | Matches D49 and D50. |
| D67 | Refusal notice · SIM:7245 | `%s has embargoed you under Earth pressure - pacify them first` | `%s has embargoed you under Earth pressure - reassure them first` | Matches D45. |
| D72 | Refusal notice · SIM:7979 | `Not enough Influence to pacify (%.0f needed, %.0f on hand)` | `Not enough Influence to reassure them (%.0f needed, %.0f on hand)` | Matches D45. |
| D68 | Refusal notice · SIM:7623 | `No such settlement '%s' to move against` | `No contact with '%s'` | "Move against" is aggressive. |
| D52 | Earth panel · PANELS:1868 | `Conduct: %s` with `diplomatic` / `predatory` / `watched` | `Conduct: %s` with `diplomatic` / `opportunist` / `mixed` | "Predatory" has abuse connotations; "watched" is unclear. |
| D54 | Alert banner · SIM:7638 | `COVERT OP EXPOSED — %s caught your people in the act. Relations with %s crater; they will remember this.` | `COVERT OP EXPOSED — %s caught your team in the act. Relations with %s drop sharply.` | "They will remember this" is a menace line, and "crater" is slang. |
| D56 | SABOTAGE tooltip · PANELS:1970 | `Sabotage: disrupt their trade for a while. A hostile act.` | `Sabotage: disrupt their trade for a while. A serious breach of trust.` | The diplomatic register (pattern 4); the weight of the act stays. |
| D57 | Alert banner · SIM:7747 | `SABOTAGE EXPOSED — %s traced the blackout to you. Relations crater; the act still bit (their systems are down).` | `SABOTAGE EXPOSED — %s traced the blackout to you. Relations drop sharply; their systems are still down.` | "Crater" and "still bit" are slangy and harsh. |
| D74 | Internal verbs on screen: the confirm line PANELS:2039, the uplink list DECK:1182, the refusal prefix COLONYVIZ:2672 | `Order transmitted: %s %s` / `%s %s` / `REJECTED %s %s: %s` (filled with the sim verb and row name, e.g. `Launder Zarya`, `Solidarity Comply`, `REJECTED Pacify Zarya: …`) | The same three formats, filled with player words: Launder -> `Amends`, Pacify -> `Reassure`, Solidarity -> `Answer Earth`, Covert -> `Covert op`, PaintDistrict / EraseDistrict / RenameDistrict -> `Paint district` / `Erase district` / `Rename district`, and the target's DisplayName instead of its row name. | "Launder" suggests money laundering and contradicts the AMENDS button. A presentation-only mapping; the sim verbs are unchanged. |

## 6. Guide, tips and cards

| ID | Where | Current (verbatim) | Proposed | Reason |
|---|---|---|---|---|
| D80 | NEXT STEP, "Keep them well" · GUIDE:402 | `The water is going stale - the crew drinks it over and over. Fresh ice-melt restores it: if the Ice Drill's deposit is spent, survey for more ice and drill it.` | `The water is going stale from recycling. Fresh ice-melt restores it: if the Ice Drill's deposit is spent, survey for more ice and drill it.` | "Drinks it over and over" is a queasy image. |
| D81 | NEXT STEP default text · GUIDE:404 | `People need oxygen, water and food every sol. Underground, the ZONE row assigns rooms - Living Quarters, a Gardening Station for food, a Lab and a Workstation for work. Hope, top left, sets how hard they work. That's the first hour - hide this guide any time.` | `People need oxygen, water and food every sol. Underground, the ZONE row assigns rooms - Living Quarters, a Gardening Station for food, a Lab and a Workstation for work. Hope, top left, sets the colony's work pace. That's the first hour - hide this guide any time.` | "How hard they work" sounds like driving people; the mechanic is a pace. See F6. |
| D87 | Alert banner, the last known ice (the `%s` advice in `%s RUNNING LOW — about %.0f t of ice left. %s`, SIM:1546) · SIM:1543 | `The last known ice. Ration water: recycling stretches it, and the Water Plant restores it while ice remains.` | `The last known ice. Stretch the water: recycling makes it last, and the Water Plant restores it while ice remains.` | "Ration" collides with short rations, which means people going short. |
| U3 | Selected Infirmary card · DECK:1448 (not in the 10-15 list) | `\nINFIRMARY ONLINE - the recall window is one sol longer` | `\nINFIRMARY ONLINE - one extra sol to restore rations` | Count toward the fix, not toward the recall (pattern 2). |

## 7. City panel

| ID | Where | Current (verbatim) | Proposed | Reason |
|---|---|---|---|---|
| D138 | City panel, the Jobs bar figure · PANELS:1284 | `%d without a post` | `%d awaiting a post` | It counts seats to fill, not people lacking something. Same length. The prose lines (PANELS:1288-1293) are kept. |

## 8. Data copy (DT live-sync needed)

| ID | Where | Current (verbatim) | Proposed | Reason |
|---|---|---|---|---|
| D82 | Manifest picker, Crew Pod Effect · `docs/data/RH_ManifestItems.csv:12` | `+4 colonists into certified housing; +200 kg Food; returns aboard if no beds` | `+4 colonists into certified housing; +200 kg Food; stays aboard, safe, if no beds` | Matches D5. |
| D136 | Manifest picker, Colonist Transport Effect · `RH_ManifestItems.csv:13` | `+20 colonists into certified housing; +1000 kg Food (50 kg each); the whole transport returns aboard if the beds are not free` | `+20 colonists into certified housing; +1000 kg Food (50 kg each); everyone stays aboard, safe, if the beds are not free` | Matches D130. |

Designer-only Notes columns still say "evacuation" in places (for example `RH_Config.csv` `ColonistEvacSols`, and `RH_Rooms.csv:17`'s "evacuation countdown"). They are not shown to the player, so they are outside this review. A later sweep can align them.

---

## For the director: mechanics that may conflict with the directive

These are flagged, not redesigned. The wording above works either way.

- **F1. Named people at odds (D23, D24).** When anyone is short, or Hope is under `DisputeHopeBelow` (45), the sim pairs two named colonists in a dispute moment (SIM:4536). It puts two individuals at the centre of a shortage, and it can pair a Mars-born child. A colony-level "tension" with no names would avoid singling anyone out.
- **F2. A child born on Mars can be recalled to orbit (D123, D125, D13).** The Mars-born are recalled last but not never. "Recalled to orbit" is gentle, but a child whose only home is Mars leaving it is the hardest moment the game has.
- **F3. Who is recalled first (C2a).** Recall takes the newest arrival first (SIM:4321). That is still an order of who goes, against Q6a's "no one is singled out by roster order". A batch without an order, or households together, may read fairer.
- **F4. Births wait on Hope (D111).** Children arrive only at THRIVING (smoothed Hope 75+). Tying birth to a mood score could read as a judgment on the colony's people.
- **F5. Growth stops when Hope is lowest (D148).** At the LOW band the city stops building homes (SIMG:577-580). Homes are a Hope source, so this removes a remedy exactly when it is needed. That may conflict with the carrot-first, recoverable guardrail (`design-decisions.md:172`).
- **F6. Hope sets work pace (D30, D76, D81).** Low Hope slows colonist work (floored at 0.60). It is the approved guardrail's design, named here only so the review covers it.
- **F7. The quiet colony is an ending (D14).** An emptied colony appears in the list of endings next to the political ones, though play continues and it can recover. It might be better as a state than an ending.
- **F8. Tobacco as a comfort (D85).** The dormant Smoking Area uses tobacco as a Hope source. Keep it dormant; if it is ever activated, a `Lounge` without tobacco avoids the issue.
- **F9. The cost of recycled water (D8, D29, D80).** Drinking recycled water lowers Hope. It is a psychological cost the design log already marks as an open question (`design-decisions.md:160`).

---

## Keep as is (122 rows)

Already right under the directive. Where a row has several strings, the main ones are quoted; all were found current at the lines given.

| ID | Where | Current (verbatim) |
|---|---|---|
| D6 | SIM:1923 | `THE CREW HAS LANDED — %d colonists disembark into the vault, with %.0f kg of provisions.` |
| D7 | SIM:2436, SIM:2562 | `FLOOR %d LOST ITS HABITABILITY RATING — %s. Restore oxygen supply.` / `SURFACE HABITAT LOST ITS HABITABILITY RATING — %s. %s` |
| D10 | SIM:3887 | `A GARDEN BED IS SPENT — the soil gave all it had. Fertilizer or a fresh soil pallet brings it back.` |
| D11 | SIM:3925 | `GROW-LIGHTS DARK — the bank can't power the garden. Crops hold dormant until the grid recovers.` |
| D12 | SIM:3937 | `THE GARDEN IS DRY — no Water for the crops. Yield paused until the tanks refill.` |
| D16 | SIM:8163-8164 | `Corporate Jewel of Earth` / `Independent Mars Federation` |
| D17 | PANELS:1877 | `On the path to: %s` |
| D18 | SIM:7842 | `A PATH IS SET — the colony's course reads clearly now: %s. History will remember which Mars you chose to build.` |
| D19 | SIM:7862 | `EPILOGUE — the story of this Mars is written: %s. The colony carries it forward.` |
| D22 | SIM:6302-6304 | `STEADY` / `THRIVING` / `FLOURISHING` |
| D25 | CREWVIZ:353, 419-424 | `* cheering *` / `~ off duty ~` / `+ lending a hand` / `* working *` |
| D26 | SIM:5725 | `THE FIRST MARTIAN IS BORN — %s, the first human native to Mars. The colony is no longer a mission. It is a home.` |
| D27 | SIM:5730 | `A CHILD IS BORN — %s joins the colony (%d born on Mars).` |
| D28 | SIM:3903 | `THE FIRST MARTIAN HARVEST — food grown in OUR ground is on the table. The crew is celebrating in the mess.` |
| D29 | PANELS:693-745 | `Water is only %.0f%% drinkable.` / `Garden: %d of %d plots growing, %+.1f kg food a sol%s.` (`%s` = ` - grow-lights dark`, PANELS:708) / `Comforts reach %d of %d crew.` / `Layout: %d pair%s of rooms clash.` / `Born on Mars: %d.` / `Discoveries: %d.` |
| D30 | PANELS:678 | `work pace %.0f%%` |
| D32 | DECK:2393 | `COMFORTS UNPACKED — small joys for the crew's long nights.` |
| D33 | SIM:8000 | `SYSTEMS MALFUNCTION` |
| D37 | SIM:8057 | `RECOVERED — the malfunction is contained; automation is coming back online.` |
| D39 | SIM:7798 | `EARTH TRANSMISSION — your sponsor nation is uneasy about your ties to the other settlements. A demand is coming.` |
| D40 | PANELS:1793 | `Earth demands you cut trade with the settlements.` |
| D41 | PANELS:1797 | `Comply: the routes close for good and Earth eases off.\nDefy: keep faith with Mars - Earth's supply ships shrink.` |
| D42 | PANELS:1804, 1808 | `COMPLY` / `DEFY` |
| D43 | SIM:8206 | `YOU COMPLIED — %d trade route(s) severed to satisfy Earth. Requisitions restored; the community feels smaller tonight.` |
| D46 | PANELS:1831 | `%s has embargoed you under Earth's pressure - %.1f sols before they side with Earth.` |
| D50 | PANELS:1929-1932 | `sided with Earth` / `route closed` / `embargo` / `disrupted` |
| D51 | PANELS:1863-1864 | `The colony leans %s.   Influence %.0f` (`Martian` / `toward Earth` / `neither way`) |
| D53 | PANELS:1963 | `Covert requisition: take a share of their goods. Night makes it harder to be caught.` |
| D55 | SIM:7674 | `COVERT OP CLEAN — your people slipped away from %s with %s. No one saw a thing (tonight).` |
| D58 | SIM:7753 | `SABOTAGE LANDS — %s's production goes dark. They will be offline for a time, and no one saw your hand.` |
| D59 | SIM:7818 | `RECOVERED — %s's systems are back online.` |
| D60 | PANELS:1956 | `Make amends: send goods back to ease hidden tension and improve standing.` |
| D61 | SIM:7718 | `AMENDS MADE — a gift returned to %s (%s). Hidden tension eases; relations warm.` |
| D62 | SIM:7532 | `CONVOY HOME — cargo from %s arrived, but the relationship soured while the rover was out. Goods only; no goodwill.` |
| D63 | SIM:10746 | `SETTLEMENT FOUND — your scouts have made contact with %s (%s). Trade and diplomacy are open.` |
| D64 | SIM:7230 | `The route to %s is closed - you cut ties when you complied with Earth` |
| D65 | SIM:7235 | `%s's production is disrupted (sabotage) - no trade until they recover` |
| D69 | SIM:7733 | `No such settlement '%s' to sabotage` |
| D70 | SIM:7695, 7701, 7707 | `No contact with '%s' to make amends to` / `No hidden grievance with %s to defuse` / `Short of the peace offering (%s)` |
| D71 | SIM:7974 | `%s is not under an Earth embargo to lift` |
| D73 | SIM:8175 | `There is no demand to answer right now.` |
| D75 | DECK:1051 | `This is your slice of Mars. The robots work for you; the colony's HOPE depends on you. The NEXT STEP panel, top right, walks you through the first hour - and these tips can be skipped any time.` |
| D76 | DECK:1064 | `Colonists need oxygen, water and food every sol. Underground, the ZONE row assigns rooms - quarters, a garden, work. Watch HOPE, top left: it drives how hard the colony works, and the Crew panel shows what is moving it.` |
| D77, D110 | GUIDE:379 (one string now) | `Your vault has %d bed%s. On the next supply ship send a Crew Pod - and an Earth Soil Pallet and a Seed Vault: a garden is how they will eat, and each later ship is a whole quota away. Meet the next quota to earn it.` |
| D78 | GUIDE:397 | `Food runs out in about %.0f sols - the garden grows %.1f kg a sol against %.1f eaten. Zone more Gardening Station cells, and keep the grow-lights powered.` |
| D79 | GUIDE:398 | `Food runs out in about %.0f sols and nothing is growing. Zone a Gardening Station - it plants itself from Earth Soil and Seeds in store, which only come by supply ship.` |
| D83 | RH_ManifestItems.csv:11 | `Luxury Goods` — `+300 kg Luxury (comforts draw per colonist per sol)` |
| D84 | RH_Rooms.csv:17 | `Infirmary` |
| D85 | RH_Rooms.csv:8 (dormant) | `Smoking Area` (keep dormant; see F8) |
| D86, D88 | SIM:1537, 1540, with the frame at SIM:1546 | `%s RUNNING LOW — about %.0f t of ice left. %s` with `Survey for more ice and carry the grid to it.` / `Drill the untouched ice before this runs out.` |
| D89, D101, D114 | GUIDE:698 | `NEXT STEP  -  %s` (stages `WATER` / `FOOD` / `POWER`) |
| D90 | GUIDE:466, 476 | `Find more ice` / `%s has about %.0f t of ice left. More lies far to the north: ORDER > Survey, then click open ground about 700 m north of the Lander, a little to the east - the scout reports anything within 60 m.` |
| D91 | GUIDE:479 | `%s has about %.0f t of ice left, and no untouched ice is known. ORDER > Survey new ground - every circle a scout covers stays on your map.` |
| D92 | GUIDE:483, 488 | `Carry the grid to the new ice` / `%s: %.0f t of ice, outside the grid. BUILD > Logistics > Grid Pylon - each links to the grid within 80 m and powers 40 m around itself. Chain them out to it (%s has about %.0f t left).` |
| D93 | GUIDE:494, 498 | `Drill the new ice` / `BUILD > Production > Ice Drill, placed on %s. Its ice is hauled to the Water Plant like the first drill's.` |
| D94, D105 | GUIDE:550, 581 | `Done: %s` |
| D95 | DECK:1571 | `\ndeposit %s: %.0f t left%s` (`%s` = ` - RUNNING LOW`) |
| D96 | DECK:1833 | `  LOW` |
| D97 | GUIDE:503, 507 | `Melt it where you drill it` / `%s's ice is a long haul from the Water Plant. BUILD > Production > Water Plant beside the new drill, inside the grid - water is shared colony-wide, so it melts where it is drilled.` |
| D98 | SIM:4439 | `FOOD RUNNING LOW — about %d sol%s left at today's draw. Grow more: zone Gardening Station cells and keep their lights powered (Earth Soil and Seeds come by supply ship).` |
| D100 | PANELS:701 | `Food: about %d sol%s in store - the garden is refilling it.` |
| D102 | GUIDE:430, 444 | `Grow more food` / `Food runs out in about %d sol%s at today's draw and nothing is growing. Grow more: zone a Gardening Station - it plants itself from Earth Soil and Seeds in store, which only come by supply ship.` |
| D103 | GUIDE:443 | `Food runs out in about %d sol%s at today's draw. Grow more: zone more Gardening Station cells, and keep the grow-lights powered.` |
| D104 | GUIDE:439 | `Food in store is down to about %d sol%s at today's draw, but the garden grows %.1f kg a sol against %.1f eaten - it is refilling. Keep the grow-lights powered.` |
| D106, D107 | SIM:1788, 1790 | `CEO TRANSMISSION — quota %s renewed (round %d): deliver %s by sol %d. The program's eyes stay on Mars.` / `CEO TRANSMISSION — new quota %s: deliver %s by sol %d. The program's eyes stay on Mars.` |
| D108 | PANELS:466 | `Earth renews the quota with every bill raised. Stockpile it to earn the next supply ship.` |
| D109 | PANELS:605 | `%s - round %d, bills up %.0f%% - %s` |
| D111 | PANELS:756 | `Thriving - next arrival` (see F4) |
| D112 | PANELS:733 | `Labs research once Hope holds %.0f.` |
| D113 | PANELS:737 | `FLOURISHING at %.0f takes extra care: comforts, furnished rooms, windows.` |
| D115 | GUIDE:454, 461 | `Put machines on the day shift` / `The battery bank ran dry after sunset, so the grid is switching machines off to keep the rest running. Heavy machines can work the DAY SHIFT: they run while the sun is up and rest at night on a trickle of power. Click your %s and switch its Day shift on. More Battery Banks carry more of the night.` |
| D116 | DECK:2257 | `DAY SHIFT: ON` / `DAY SHIFT: OFF` |
| D117 | DECK:1389 | `\nDAY SHIFT - resting until sunrise (idle draw, batch paused)` |
| D118 | DECK:1392 | `\nDAY SHIFT - works while the sun is up, rests at night` |
| D119 | DECK:1469-1470 | `\nbatch -> %s: %.2f h left%s` (`%s` = ` (paused until sunrise)`) |
| D120 | PANELS:2540 | `Works the day shift by default: runs while the sun is up and rests at night on idle power. Change it on the machine's card.` |
| D121 | PANELS:2541 | `Can work the day shift - switch it on from the machine's card to save the battery for the night.` |
| D122 | PANELS:2085 | `DAY SHIFT: %d machine(s) resting until sunrise` |
| D124 | SIM:4295 | `RATIONS RESTORED: %s is back on full rations (%d resident%s).` |
| D126 | SIM:3951, 3954 | `Floor %d` / `Surface habitat #%d` |
| D128 | PANELS:727 | `Rations: the equivalent of %.0f of %d residents is short of a full ration.` |
| D129 | SIM:1950 | `A COLONIST TRANSPORT HAS LANDED — %d colonists disembark into the vault, with %.0f kg of provisions.` |
| D131 | SIM:5818 | `%d settler%s arrived with the supply ship, drawn by the colony's reputation.` |
| D132 | SIM:5865 | `THE BIG ARRIVAL — %d settlers land at once: the colony held its Hope long enough to be trusted with a city. They bring %.0f kg of provisions.` |
| D133 | SIM:5867 | `THE CEO HAS ANNOUNCED HE WILL COME TO MARS WITH THE NEXT WAVE.` |
| D135 | SIM:5740 | `THE COLONY GROWS — %d children born on Mars so far.` |
| D137 | PANELS:1259-1278 | `%d bed%s short` / `room to grow`; `Housing: nobody lives here yet.` / `Housing: there is room for everyone the next ship could bring.` / `Housing: the Big Arrival waits on %d more free bed%s (it needs %d free).` / `Housing: %s for newcomers - zone %d more Living Quarters so everyone stays well housed when %s %s.` / `Housing: %d more certified bed%s would make room for %s (%d free).%s` |
| D139 | PANELS:1300-1310 | `most wanted: %s` / `all covered`; `Services: the colony would gain most from more %s.` / `Services: every room type that lifts Hope covers the colony.` |
| D140 | PANELS:1317-1333 | `full rations` / `%d on short rations`; `Rations: every home is on a full ration; the tightest oxygen margin is %.1f sols.` / `Rations: %d home%s (%d resident%s) on short rations - %s is the scarcest. Restore it to bring rations back to full.` |
| D141 | PANELS:1167 | `%s  -  %s  -  %d resident%s  -  %d bed%s  -  %d seat%s` |
| D142 | PANELS:1504, 1494, 1460, 1475, 1107, 1136, 1077 | `CITY` / `%d DISTRICT%s` / `PAINT A DISTRICT` / `DISTRICTS  (%d)` / `Type a new name and press Enter.` / `Erase this district (its name is not reused).` / `No districts yet. Pick a kind above and drag a rectangle on the ground, or click a floor underground. Each district gets a name you can change, and its residents, beds and seats are counted here.` |
| D143 | DECK:852, 148, 128; PC:597, 655-656, 679, 1345; PANELS:1128, 1148 | `DISTRICT` / `drag on the ground, or click a floor underground` / `%s district: drag a rectangle of 10 m cells on the ground (up to 32 x 32), or click a floor underground. Right-click to finish.` / `District order transmitted: %s, %d x %d cells` / `Rename transmitted: %s` / `Erase transmitted` |
| D144 | SIMD:324-501, SIM:7364 | `'%s' is not a district kind (Homes, Industry, Farming, Research or Civic)` / `That rectangle crosses two districts (%s and %s) - paint inside one, or on open ground` / `Outside grid coverage (%d of %lld cells) - extend pylons first` / `Unknown order '%s'` (and the rest as listed in the 10-15 inventory) |
| D145 | PC:1121, 1126 | `%s district  Δ %.0fs` / `Floor %d: %s district  Δ %.0fs` |
| D146 | `Source/RedHopeSim/Private/RHDistricts.cpp:10`, `:45` | `Gale`, `Jezero`, `Holden`, ... (40 names), then Roman suffixes |
| D147 | SIMG:548-551, 171-178 | `CITY GROWTH (sol %d) - %s` joining `under way: %s` \| `finished: %s` \| `not built, the site changed, materials back in stock: %s` |
| D149 | PANELS:1059, 1186-1225 | `%s: %s` / `Growing: %s - %.0f%%` / `Growth is switched off.` / `Not growing: %s.` / `Waiting for the next sol's check.` / `GROWTH ON` / `GROWTH OFF` |
| D150 | SIMG:611-1126 | `nobody lives here yet` / `housing is not short` / `floor %d is not rated yet` / `habitat #%d is door-sealed - a new module would open new doorways, so it is not extended` / `every civic service is covered` (and the rest) |
| D151 | `RHGrowthVisualizerSubsystem.cpp:296` | `%s: %s - %.0f%%` |
| D152 | RH_Config.csv, RH_Rooms.csv:6, :17 | Designer Notes (not shown) |
| D153 | SIMD:801, 804, 817 | `The settlers are ready to come once the %s can carry %d more.` / ` The colony makes about %.0f kg of oxygen a sol and would need %.0f.` |
| D154 | SIMG:264-266 | `short of %s: needs %.0f kg, %.0f free (%.0f held - %.0f for the %s - %.0f reserve)` |
| D155 | PANELS:1389, 1398, 1377, 1358, 1408, 1372 | `CITY <-> EARTH` / `%.0f%% city  /  %.0f%% Earth` / `%d%%` / `%d%% of the colony's spare material may grow the city; %d%% is held for Earth's next quota.` |
| D156 | SIMD:857, 858, 861 | `The next ship's settlers are held to %d: they come only on what the colony already makes, and its %s production would not carry them.` / `The next ship's settlers are held to %d: the colony's %s stores are too thin to back them.` / ` Its %s stores are also too thin.` |
| D157 | RH_Config.csv | Designer Notes (not shown) |
| D158 | `RHSimRovers.cpp:269` | `A cargo rover is waiting aboard the Lander for a road.` |
| D159 | `RHSimCommute.cpp:557` | `%d people travel over %.0f m to work; homes nearer their work would give them time back.` |
| D160 | RH_ManifestItems.csv:15, RH_Robots.csv:8 | `Cargo Rover RC-R` — `+1 cargo rover (needs a road)` |
| D161 | PANELS:721 | `Long trips to work: -%.1f Hope (%d of %d people travel over %.0f m; homes nearer their work would give it back).` |
| D162 | PANELS:1021-1026 | `Commuting: %d of %d people (%.0f%%) travel over %.0f m to work; homes nearer their work would give them time back.` / `Commuting: %d people travel to work; nobody is over %.0f m.` / ` Busiest route: %s to %s, %d people.` |
| D163 | PANELS:1736-1743 | `Hauling %.0f kg` / `To a pickup` / `Standing by` / `Needs a road` / `Broken down` / `Out of charge` |
| D164 | PANELS:1659 | `%d cargo rover%s waiting aboard the Lander for a road.` |
| D165 | PANELS:2302, 2318, 2496, 2512, 2582; PC:535, 560-565, 1317; `RHSimRoads.cpp:516` | `Demolish Road` / `%.0f%% of the struct back` / `Tears a road up. Half of its Struct comes back to the Lander; a stretch still being laid gives back everything it was paid. Robots and rovers already on it finish their trip, only slower.` / `DEMOLISH ROAD #%d: %s` / `Demolish transmitted: road #%d, %.0f m - %.0f kg of Struct comes back (%s)` / `That road is already gone` |
| D166 | PANELS:1527, 1574-1604; `RHTrafficVisualizerSubsystem.cpp:130` | `Traffic` / `Traffic [on]` / `unused` / `quiet  to  busiest` / `Each road is washed by the people who ride it plus %d for every freight rover on it. The busiest road carries %d.` / `Edge #%d (%.0f m%s): %d commuter%s, %d freight rover%s` |
| U1 | SIM:2549-2551 (not in the 10-15 list) | `SURFACE HABITAT RATED LIVABLE — %d bays pressurized and circulated under a shielded hull; %d beds.%s%s` with ` Zone a bay Living Quarters to add beds.` / ` Crew can move in once the underground vault is certified.` |
| U2 | SIM:4001 (not in the 10-15 list) | `%s took charge of an empty %s seat.` |
