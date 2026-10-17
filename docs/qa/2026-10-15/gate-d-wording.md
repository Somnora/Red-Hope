# Gate-D wording review — 2026-10-15 (main @ 4706e5f)

This lists every player-facing string about Hope and mood, crew support and recall to orbit, the colony emptying, crises, endings, and Earth and neighbour politics. Strings come from the source and data on main. Each row gives an ID, where the player sees it, the current text verbatim (`%s`/`%d`/`%.1f` are filled in at runtime), a one-line concern, and a proposed replacement written under the mental-health directive.
Rows marked **keep** need no change. The 32 decisions are D1, D2, D3, D5, D9, D13, D14, D15, D20, D21, D23, D24, D34, D35, D36, D38, D44, D45, D47, D48, D49, D52, D54, D57, D66, D67, D68, D72, D74, D80, D82, D85.
To answer, write something like: "approve D1–D85 except D14: use 'Colony on Standby'; D52: keep 'predatory'". Approved text is applied exactly as written. CSV rows are DT-synced, and the multi-sol baseline must stay identical, since only strings change.
Nothing has been changed yet. This file is the only edit.

File key: **SIM** = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp` · **PANELS** = `Source/RedHope/Private/RHCommandDeckPanels.cpp` · **DECK** = `.../RHCommandDeck.cpp` · **GUIDE** = `.../RHCommandDeckGuide.cpp` · **CREWVIZ** = `.../RHCrewVisualizerSubsystem.cpp` · **COLONYVIZ** = `.../RHColonyVisualizerSubsystem.cpp`. "Alert banner" means the top-centre banner, which holds the latest alert for 15 s.

## 1. Life support & recall

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D1 | Alert banner, once per colonist when support drops · SIM:2420, legs SIM:2417–2418 | `LIFE SUPPORT: %s is unsupported — %s. Evacuation in %.1f sols unless restored.` The second `%s` is one of `home floor lost its air` / `water tanks empty` / `food stores empty`. Example: "LIFE SUPPORT: Freya Farid is unsupported — food stores empty. Evacuation in 2.0 sols unless restored." | Leads with the threat, and the same line is used for Mars-born children. It fires per colonist: the lean playtest got six at once, and the banner showed only the last. | `LIFE SUPPORT: %s — %s. Restore it within %.1f sols, or the Program recalls them to orbit.` (legs unchanged) |
| D2 | Alert banner · SIM:2441 | `EVACUATED: %s returned to orbit — life support failed for %.1f sols.` | "Returned" is false for a Mars-born child (playtest: Freya Farid), and "failed" frames this as failure. | `RECALLED: %s is safe in orbit — life support was down for %.1f sols.` |
| D3 | Crew panel roster, amber line per unsupported colonist · PANELS:807 | `needs support - recalled in %.1f sols` | This counts down to removal, not to the fix. It is also a bug: it reads `GetColonistEvacSols()` while the sim uses `GetEffectiveEvacSols()`, so with an Infirmary online it shows 1 sol less than the real window. | `needs support - %.1f sols to restore` (the `GetEffectiveEvacSols()` switch is already applied - only the wording awaits you) |
| D4 | CREW tile caption · PANELS:325 | `%d need support` | fine as is | keep |
| D5 | Alert banner when a Crew Pod lands with no beds · SIM:1427 | `CREW POD RETURNED — no certified housing (%d beds free, %d needed). Certify a vault floor before the next ship.` | "Returned" reads like people were rejected, and the line doesn't say they're fine. | `CREW POD STAYED ABOARD — no certified beds (%d free, %d needed); the crew flies back with the ship. Certify a vault floor before sending people.` |
| D6 | Alert banner · SIM:1420 | `THE CREW HAS LANDED — %d colonists disembark into the vault, with %.0f kg of provisions.` | fine as is | keep |
| D7 | Alert banner · SIM:1742–1743 | `FLOOR %d LOST ITS HABITABILITY RATING — %s. Restore oxygen supply.` The `%s` is `oxygen fill below requirement` or `air circulation down`. | Fine: it's the early warning before D1 and it names the fix. | keep |
| D8 | Alert banner · SIM:2472 | `WATER QUALITY LOW — the crew is drinking heavily-recycled water. Drill the ice caps to restore it.` | Fine: it names the fix. | keep |
| D9 | Alert banner · SIM:2184 | `GARDEN LOST: floor %d cell %d was re-zoned — the emplaced soil is forfeit.` | "LOST"/"forfeit" is punitive for a zoning click. | `GARDEN CLEARED: floor %d cell %d was re-zoned — its soil can't be recovered.` |
| D10 | Alert banner · SIM:2269 | `A GARDEN BED IS SPENT — the soil gave all it had. Fertilizer or a fresh soil pallet brings it back.` | fine as is | keep |
| D11 | Alert banner · SIM:2307 | `GROW-LIGHTS DARK — the bank can't power the garden. Crops hold dormant until the grid recovers.` | fine as is | keep |
| D12 | Alert banner · SIM:2319 | `THE GARDEN IS DRY — no Water for the crops. Yield paused until the tanks refill.` | fine as is | keep |

## 2. Collapse & endings

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D13 | Alert banner, once, when the last colonist is recalled · SIM:2344–2345 | `THE COLONY STANDS EMPTY — everyone has returned to orbit. The machines keep the lights on. Restore housing and life support, and people will come back to Mars.` | The recovery path is already good. "STANDS EMPTY" is bleak, and "returned" covers Mars-born children again. | `THE COLONY IS QUIET — the whole crew is safe in orbit. The machines keep the lights on. Restore housing and life support, and Mars can welcome a crew again.` |
| D14 | Earth & Neighbors panel, `On the path to: %s` (PANELS:1055) · SIM:4441 | `Abandonment / Collapse` | Abandonment/failure framing. It is also the current state, not a "path". | `The Quiet Colony` |
| D15 | Same panel, plus the D18/D19 banners · SIM:4440 | `Martian Cold War` | War vocabulary; the brief keeps war as off-screen pressure. | `A Divided Mars` |
| D16 | Same panel, plus D18/D19 · SIM:4438, SIM:4439 | `Corporate Jewel of Earth` / `Independent Mars Federation` | fine as is | keep |
| D17 | Earth & Neighbors panel · PANELS:1055 | `On the path to: %s` | Fine; D14 fixes the collapse case. | keep |
| D18 | Alert banner, once · SIM:4147 | `A PATH IS SET — the colony's course reads clearly now: %s. History will remember which Mars you chose to build.` | fine as is | keep |
| D19 | Alert banner + golden MILESTONE card · SIM:4167 | `EPILOGUE — the story of this Mars is written: %s. The colony carries it forward.` | fine as is | keep |

## 3. Hope bands & crew mood

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D20 | HOPE tile caption, title-cased to "Failing" (PANELS:336), and the Crew panel header "HOPE  FAILING" (PANELS:658) · SIM:3090 | `FAILING` | Judges the people. The lowest mood band shouldn't read as failure. | `LOW` |
| D21 | Same places · SIM:3091 | `STRAINED` | A stress word aimed at people. | `UNSETTLED` |
| D22 | Same places · SIM:3092–3094 | `STEADY` / `THRIVING` / `FLOURISHING` | fine as is | keep |
| D23 | **Not shown today.** The text goes to the crew visualizer, which ignores it. · SIM:2539 | `Raised voices on floor %d — %s and %s are arguing over the shortages. Morale is wearing thin.` | Conflict framing, and "Morale" where the game says Hope. It can pair a Mars-born child, who also gets a job and pastimes from birth. Approve it now so it's safe if it ever appears on screen. | `Tension on floor %d — %s and %s are at odds over the shortages. Restocking will settle it.` |
| D24 | Crew emote plate over both colonists (amber, 9 s) · CREWVIZ:242–243 | `! heated words !` | The alarm punctuation reads as a fight. | `~ at odds ~` |
| D25 | Crew emote plates · CREWVIZ:203, 233–234, 237–238 | `* cheering *` / `~ off duty ~` / `+ lending a hand` / `* working *` | fine as is | keep |
| D26 | Alert banner · SIM:2963 | `THE FIRST MARTIAN IS BORN — %s, the first human native to Mars. The colony is no longer a mission. It is a home.` | Fine. It shows as a banner only, not a golden MILESTONE card; your call whether it earns one. | keep |
| D27 | Alert banner · SIM:2968 | `A CHILD IS BORN — %s joins the colony (%d born on Mars).` | fine as is | keep |
| D28 | Alert banner + golden card · SIM:2285 | `THE FIRST MARTIAN HARVEST — food grown in OUR ground is on the table. The crew is celebrating in the mess.` | fine as is | keep |
| D29 | Crew panel's "what moves Hope" prose, built from pieces · PANELS:679, 684–685, 689, 693, 697, 701 | `Water is only %.0f%% drinkable.` / `Garden: %d of %d plots growing, %+.1f kg food a sol%s.` (`%s` = ` - grow-lights dark`) / `Comforts reach %d of %d crew.` / `Layout: %d pair%s of rooms clash.` / `Born on Mars: %d.` / `Discoveries: %d.` | fine as is | keep |
| D30 | Crew panel, under Hope · PANELS:664 | `work pace %.0f%%` | fine as is | keep |
| D31 | Crew panel progress bar (births) · PANELS:712 | `Flourishing - next arrival` | Fine: it calls a birth "an arrival". | keep |
| D32 | Golden RARE MATERIALS card · DECK:1676 | `COMFORTS UNPACKED — small joys for the crew's long nights.` | fine as is | keep |

## 4. Crises

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D33 | Earth & Neighbors panel line, `%s - %.1f sols` (PANELS:1050) · SIM:4305 | `SYSTEMS MALFUNCTION` | fine as is | keep |
| D34 | Same panel line · SIM:4306 | `ENVIRONMENTAL STRAIN` | "Strain" echoes the STRAINED mood band (D21). The mechanic is a water drain. | `DRY SPELL` |
| D35 | Alert banner · SIM:4324 | `SYSTEMS MALFUNCTION — automation across the colony stutters and stalls. Units are going inert; crews are stretched. It will pass, but it bites.` | "It bites" is punitive and "crews are stretched" puts the stress on people. It also doesn't say why: it is blowback from covert conduct, which the player can change. | `SYSTEMS MALFUNCTION — blowback from covert work: automation stalls, units go idle, work slows. It passes in a few sols.` |
| D36 | Alert banner · SIM:4329 | `ENVIRONMENTAL STRAIN — a systems-stress test on the colony's reserves. Water demand spikes; hold together and it passes.` | "Hold together" implies the people might not. | `DRY SPELL — the colony's water draw spikes for a few sols. Keep the tanks full and it passes.` |
| D37 | Alert banner · SIM:4362 | `RECOVERED — the malfunction is contained; automation is coming back online.` | fine as is | keep |
| D38 | Alert banner · SIM:4363 | `WEATHERED — the environmental strain has passed; the colony held.` | Follows D34. | `WEATHERED — the dry spell has passed; the colony held.` |

## 5. Earth & neighbour politics

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D39 | Alert banner · SIM:4103 | `EARTH TRANSMISSION — your sponsor nation is uneasy about your ties to the other settlements. A demand is coming.` | fine as is | keep |
| D40 | Earth panel demand card · PANELS:971 | `Earth demands you cut trade with the settlements.` | fine as is | keep |
| D41 | Earth panel demand card (two lines) · PANELS:975 | `Comply: the routes close for good and Earth eases off.` / `Defy: keep faith with Mars - Earth's supply ships shrink.` | fine as is | keep |
| D42 | Buttons · PANELS:982, 986 | `COMPLY` / `DEFY` | fine as is | keep |
| D43 | Alert banner · SIM:4481 | `YOU COMPLIED — %d trade route(s) severed to satisfy Earth. Requisitions restored; the community feels smaller tonight.` | Fine; a gentle way to deliver the Hope hit. | keep |
| D44 | Alert banner · SIM:4502 | `YOU DEFIED EARTH — the settlements stand together. Morale rises, but your sponsor's supply priority is cut. Survive on what Mars makes.` | "Survive" is a threat, and it says "Morale" where the game says Hope. | `YOU DEFIED EARTH — the settlements stand together. Hope rises, but your sponsor's supply priority is cut. Mars will have to provide.` |
| D45 | Alert banner · SIM:4267 | `EARTH LEANS ON %s — under pressure from Earth, they have suspended trade with you. Pacify them within %.0f sols or they defect.` | "Pacify" is a military/condescending verb used on a neighbour. | `EARTH LEANS ON %s — under pressure from Earth, they have suspended trade with you. Reassure them within %.0f sols or they side with Earth.` |
| D46 | Earth panel embargo card · PANELS:1009 | `%s has embargoed you under Earth's pressure - %.1f sols before they side with Earth.` | fine as is | keep |
| D47 | Button · PANELS:1015 | `PACIFY  (spend influence)` | Same as D45. | `REASSURE  (spend influence)` |
| D48 | Alert banner · SIM:4294 | `PACIFIED — %s sees reason and ignores Earth's order. Trade resumes; tension eases.` | "Sees reason" talks down to the neighbour. | `REASSURED — %s sets Earth's order aside. Trade resumes; tension eases.` |
| D49 | Alert banner · SIM:4229 | `DEFECTION — %s has sided with Earth. The route is closed and relations are locked hostile.` | "Hostile" escalates. | `DEFECTION — %s has sided with Earth. The route is closed and relations are frozen.` |
| D50 | Settlement card status chip · PANELS:1107–1110 | `sided with Earth` / `route closed` / `embargo` / `disrupted` | fine as is | keep |
| D51 | Earth panel · PANELS:1041–1042 | `The colony leans %s.   Influence %.0f` (`%s` = `Martian` / `toward Earth` / `neither way`) | fine as is | keep |
| D52 | Earth panel · PANELS:1046 | `Conduct: %s` (`%s` = `diplomatic` / `predatory` / `watched`) | "Predatory" has abuse connotations; "watched" is unclear. | `Conduct: %s` with `diplomatic` / `opportunist` / `mixed` |
| D53 | COVERT button tooltip · PANELS:1141 | `Covert requisition: take a share of their goods. Night makes it harder to be caught.` | fine as is | keep |
| D54 | Alert banner · SIM:3943 | `COVERT OP EXPOSED — %s caught your people in the act. Relations with %s crater; they will remember this.` | "They will remember this" is a menace line; "crater" is harsh. | `COVERT OP EXPOSED — %s caught your team in the act. Relations with %s drop sharply.` |
| D55 | Alert banner · SIM:3979 | `COVERT OP CLEAN — your people slipped away from %s with %s. No one saw a thing (tonight).` | Fine; the moral trap is intended. | keep |
| D56 | SABOTAGE button tooltip · PANELS:1148 | `Sabotage: disrupt their trade for a while. A hostile act.` | fine as is | keep |
| D57 | Alert banner · SIM:4052 | `SABOTAGE EXPOSED — %s traced the blackout to you. Relations crater; the act still bit (their systems are down).` | "Crater" and "still bit" are slangy and harsh. | `SABOTAGE EXPOSED — %s traced the blackout to you. Relations drop sharply; their systems are still down.` |
| D58 | Alert banner · SIM:4058 | `SABOTAGE LANDS — %s's production goes dark. They will be offline for a time, and no one saw your hand.` | Fine: it's about systems, not people. | keep |
| D59 | Alert banner · SIM:4123 | `RECOVERED — %s's systems are back online.` | fine as is | keep |
| D60 | AMENDS button tooltip · PANELS:1134 | `Make amends: send goods back to ease hidden tension and improve standing.` | fine as is | keep |
| D61 | Alert banner · SIM:4023 | `AMENDS MADE — a gift returned to %s (%s). Hidden tension eases; relations warm.` | fine as is | keep |
| D62 | Alert banner · SIM:3837 | `CONVOY HOME — cargo from %s arrived, but the relationship soured while the rover was out. Goods only; no goodwill.` | fine as is | keep |
| D63 | Alert banner · SIM:5209 | `SETTLEMENT FOUND — your scouts have made contact with %s (%s). Trade and diplomacy are open.` | fine as is | keep |

Refusals appear on the comms notice line as `REJECTED <Verb> <Target>: <reason>` (COLONYVIZ:1274) and stay up for 12 s.

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D64 | Notice line · SIM:3564 | `The route to %s is closed - you cut ties when you complied with Earth` | fine as is | keep |
| D65 | Notice line · SIM:3569 | `%s's production is disrupted (sabotage) - no trade until they recover` | fine as is | keep |
| D66 | Notice line · SIM:3574 | `%s defected to Earth - relations are hostile, no trade` | Should match D49/D50. | `%s has sided with Earth - relations are frozen, no trade` |
| D67 | Notice line · SIM:3579 | `%s has embargoed you under Earth pressure - pacify them first` | Should match D45. | `%s has embargoed you under Earth pressure - reassure them first` |
| D68 | Notice line · SIM:3928 | `No such settlement '%s' to move against` | "Move against" is aggressive. | `No contact with '%s'` |
| D69 | Notice line · SIM:4038 | `No such settlement '%s' to sabotage` | fine as is | keep |
| D70 | Notice line · SIM:4000, 4006, 4012 | `No contact with '%s' to make amends to` / `No hidden grievance with %s to defuse` / `Short of the peace offering (%s)` | fine as is | keep |
| D71 | Notice line · SIM:4279 | `%s is not under an Earth embargo to lift` | fine as is | keep |
| D72 | Notice line · SIM:4284 | `Not enough Influence to pacify (%.0f needed, %.0f on hand)` | Should match D45. | `Not enough Influence to reassure them (%.0f needed, %.0f on hand)` |
| D73 | Notice line · SIM:4450 | `There is no demand to answer right now.` | fine as is | keep |
| D74 | Internal verbs on screen, in three places: the comms confirm `Order transmitted: %s %s` (PANELS:1217), the uplink list `%s %s` (DECK:906–908), and the refusal prefix `REJECTED %s %s: %s` (COLONYVIZ:1274) | The player sees e.g. `Order transmitted: Launder Zarya`, `Solidarity Comply`, `REJECTED Pacify Zarya: …` | "Launder" suggests money-laundering and contradicts the AMENDS button. Internal verbs and row IDs leak onto the screen. | Show player words: Launder → `Amends`, Pacify → `Reassure`, Solidarity Comply/Defy → `Answer Earth: comply/defy`, Covert → `Covert op`, with the settlement's DisplayName. This is a presentation-only mapping; the sim verbs are unchanged. |

## 6. Guide, tips & panels

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D75 | Tip card "WELCOME, DIRECTOR" · DECK:805 | `This is your slice of Mars. The robots work for you; the colony's HOPE depends on you. The NEXT STEP panel, top right, walks you through the first hour - and these tips can be skipped any time.` | fine as is | keep |
| D76 | Tip card "YOUR PEOPLE" · DECK:818 | `Colonists need oxygen, water and food every sol. Underground, the ZONE row assigns rooms - quarters, a garden, work. Watch HOPE, top left: it drives how hard the colony works, and the Crew panel shows what is moving it.` | fine as is | keep |
| D77 | NEXT STEP guide, "Bring the first crew" · GUIDE:314 | `Your vault has %d bed%s. On the next supply ship send a Crew Pod - and an Earth Soil Pallet and a Seed Vault: it may be the last ship, and a garden is how they will eat. Meet the next quota to earn it.` | Fine: exactly prevention. | keep |
| D78 | NEXT STEP guide, "Keep them well" · GUIDE:332 | `Food runs out in about %.0f sols - the garden grows %.1f kg a sol against %.1f eaten. Zone more Gardening Station cells, and keep the grow-lights powered.` | fine as is | keep |
| D79 | Same step · GUIDE:333 | `Food runs out in about %.0f sols and nothing is growing. Zone a Gardening Station - it plants itself from Earth Soil and Seeds in store, which only come by supply ship.` | fine as is | keep |
| D80 | Same step · GUIDE:337 | `The water is going stale - the crew drinks it over and over. Fresh ice-melt restores it: if the Ice Drill's deposit is spent, survey for more ice and drill it.` | "Drinks it over and over" is a queasy image. | `The water is going stale from recycling. Fresh ice-melt restores it: if the Ice Drill's deposit is spent, survey for more ice and drill it.` |
| D81 | Same step (default text) · GUIDE:339 | `People need oxygen, water and food every sol. Underground, the ZONE row assigns rooms - Living Quarters, a Gardening Station for food, a Lab and a Workstation for work. Hope, top left, sets how hard they work. That's the first hour - hide this guide any time.` | fine as is | keep |

## 7. Data blurbs

Only columns the game displays are included. Notes columns are designer-only. Edits to these rows need a DT live-sync.

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D82 | Manifest picker: name, row text and tooltip (PANELS:529, 536, 549) · `docs/data/RH_ManifestItems.csv:12` DisplayName + Effect | `Crew Pod (4 colonists)` — `+4 colonists into certified housing; +200 kg Food; returns aboard if no beds` | Should match D5. | Effect: `+4 colonists into certified housing; +200 kg Food; stays aboard if no beds` |
| D83 | Same picker · `RH_ManifestItems.csv:11` | `Luxury Goods` — `+300 kg Luxury (comforts draw per colonist per sol)` | fine as is | keep |
| D84 | ZONE row button + floor cell label · `RH_Rooms.csv:17` DisplayName | `Infirmary` | Fine: it implies care, not illness. Its effect (+1 sol before recall) is shown nowhere, and D3 currently undercounts it. | keep |
| D85 | **Not shown** (SliceActive FALSE) · `RH_Rooms.csv:8` DisplayName | `Smoking Area` | A Gate-D luxury item (tobacco as a morale source in a game under a mental-health directive). It needs your call before it ever activates. | Keep dormant. If it is activated, rename it `Lounge` and drop tobacco as the comfort. |

## 8. Ice running low (D2a, added 2026-10-17)

Added with gate D2a (`docs/qa/2026-10-17/crew-phase-brief.md`). Every row is a **PLACEHOLDER** written to the prevention-first rules below (name the fix, then the consequence; no threat words) and awaits your wording review. The interrupt card appears while it can be acted on and goes away when nothing the player could do would resolve it. STEP = the `Interrupts()` array in GUIDE.

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D86 | Alert banner, once per ice deposit, while ice remains to be surveyed · SIM `StepDepositAlerts` | `%s RUNNING LOW — about %.0f t of ice left. Survey for more ice and carry the grid to it.` Example: "ICE_A RUNNING LOW — about 7 t of ice left. Survey for more ice and carry the grid to it." | PLACEHOLDER. Names the fix; `ICE_A` is a data row name in capitals, like the other deposit banners. | keep pending review |
| D87 | Alert banner, once, when the low deposit is the last ice anywhere (nothing unsurveyed, no healthy ice) · SIM `StepDepositAlerts` | `%s RUNNING LOW — about %.0f t of ice left. The last known ice. Ration water: recycling stretches it, and the Water Plant restores it while ice remains.` | PLACEHOLDER. Gives a lever that exists (recycling, the Water Plant) rather than a survey that cannot succeed. "The last known ice" is a statement of fact, not a threat. | keep pending review |
| D88 | Alert banner, once, when untouched ice is already known · SIM `StepDepositAlerts` | `%s RUNNING LOW — about %.0f t of ice left. Drill the untouched ice before this runs out.` | PLACEHOLDER. Not reachable with the shipped data order (Ice_B is found only after Ice_A is low), kept for a player who surveys first. | keep pending review |
| D89 | Guide panel header while an ice step is up · GUIDE | `NEXT STEP  -  WATER`, with the `n / N` counter blank | PLACEHOLDER. A new stage label. | keep pending review |
| D90 | Guide step, ice low and Ice_B not yet surveyed · GUIDE STEP 1 | Title `Find more ice`. Body `%s has about %.0f t of ice left. More lies far to the north: ORDER > Survey, then click open ground about 700 m north of the Lander, a little to the east - the scout reports anything within 60 m.` | PLACEHOLDER. Names where to look before it is surveyed (the same precedent as "north-east" in the first-hour step). Design call: say less ("survey farther out, to the north")? | keep pending review |
| D91 | Guide step, ice low and no named untouched ice · GUIDE STEP 1 | `%s has about %.0f t of ice left, and no untouched ice is known. ORDER > Survey new ground - every circle a scout covers stays on your map.` | PLACEHOLDER. | keep pending review |
| D92 | Guide step, healthy ice found but outside the grid · GUIDE STEP 2 | Title `Carry the grid to the new ice`. Body `%s: %.0f t of ice, outside the grid. BUILD > Logistics > Grid Pylon - each links to the grid within 80 m and powers 40 m around itself. Chain them out to it (%s has about %.0f t left).` | PLACEHOLDER. | keep pending review |
| D93 | Guide step, grid reaches healthy ice with no drill on it · GUIDE STEP 3 | Title `Drill the new ice`. Body `BUILD > Production > Ice Drill, placed on %s. Its ice is hauled to the Water Plant like the first drill's.` | PLACEHOLDER. | keep pending review |
| D94 | Toast when an ice step resolves · GUIDE | `Done: <step title>` (the existing format) | PLACEHOLDER. Also shown when the step clears because nothing can be done any more; the title then reads slightly rosy. | keep pending review |
| D95 | Selected Ice Drill card, attached-deposit line · DECK | `deposit %s: %.0f t left - RUNNING LOW` (the suffix only when low) | PLACEHOLDER. | keep pending review |
| D96 | Known Ground list, status column · DECK | `  LOW` after the tonnage when the ice deposit is low | PLACEHOLDER. | keep pending review |
| D97 | Guide step, a healthy ice deposit is drilled but no finished Water Plant stands near it · GUIDE STEP 4 | Title `Melt it where you drill it`. Body `%s's ice is a long haul from the Water Plant. BUILD > Production > Water Plant beside the new drill, inside the grid - water is shared colony-wide, so it melts where it is drilled.` | PLACEHOLDER. Same `WATER` stage label as D89. | keep pending review |

## 9. Food running low (D4a, added 2026-10-17)

All rows are PLACEHOLDER pending this review. Each is prevention-framed and names the fix: no hunger or starvation imagery, only the store and what to do about it.

| ID | Where | Current | Concern | Proposed |
|---|---|---|---|---|
| D98 | Alert banner, once per larder episode, when the food runway at today's draw falls under 10 sols (re-arms past 15) · SIM `StepPopulation` | `FOOD RUNNING LOW — about %d sol(s) left at today's draw. Grow more: zone Gardening Station cells and keep their lights powered (Earth Soil and Seeds come by supply ship).` The sols are floored, at least 1; "sol"/"sols" is chosen in code. Silent on a step where someone already failed the food draw (LIFE SUPPORT, D1, owns the banner). | PLACEHOLDER. Names the fix first. Until the supply-ship gate lands, a colony past the two-rung ladder has no next ship, so "by supply ship" is only half true. | keep pending review |
| D99 | Crew panel, "What is moving Hope" prose, while the food line costs Hope and the garden is not out-growing the crew · PANELS | `Food: about %d sol(s) in store - running short weighs on the crew. Grow more: zone Gardening Station cells.` Same floored sols as D98. | PLACEHOLDER. "weighs on the crew" is the prevention-framed way to say Hope is lower. | keep pending review |
| D100 | Same prose, while the food line costs Hope but the garden grows at least what is eaten · PANELS | `Food: about %d sol(s) in store - the garden is refilling it.` | PLACEHOLDER. Tells the player the problem is already being solved; the line stays until the store is back over 10 sols. | keep pending review |
| D101 | Guide panel header while the food step is up · GUIDE | `NEXT STEP  -  FOOD`, with the `n / N` counter blank | PLACEHOLDER. A new stage label, ranked above the WATER ones. | keep pending review |
| D102 | Guide step, runway under 10 sols and nothing planted · GUIDE | Title `Grow more food`. Body `Food runs out in about %d sol(s) at today's draw and nothing is growing. Grow more: zone a Gardening Station - it plants itself from Earth Soil and Seeds in store, which only come by supply ship.` | PLACEHOLDER. "Food runs out" is a plain supply fact, as in the existing "Keep them well" text. | keep pending review |
| D103 | Guide step, runway under 10 sols with a garden that is not keeping up · GUIDE | `Food runs out in about %d sol(s) at today's draw. Grow more: zone more Gardening Station cells, and keep the grow-lights powered.` | PLACEHOLDER. | keep pending review |
| D104 | Guide step, runway under 10 sols with a garden that out-grows the crew · GUIDE | `Food in store is down to about %d sol(s) at today's draw, but the garden grows %.1f kg a sol against %.1f eaten - it is refilling. Keep the grow-lights powered.` | PLACEHOLDER. | keep pending review |
| D105 | Toast when the food step resolves · GUIDE | `Done: Grow more food` (the existing format) | PLACEHOLDER. Also shown when the step clears because the crew is gone. | keep pending review |
| D106 | Alert banner, when a quota's ship lands and the next rung opens (reworded: the bills now read as friendly names, not the raw row string) · SIM `AdvanceQuotaLadder` | `CEO TRANSMISSION — new quota %s: deliver structural 2500 kg, Oxygen 1200 kg, Hydrogen 250 kg by sol %d. The program's eyes stay on Mars.` | PLACEHOLDER. Plain supply wording, nothing about people or mood. | keep pending review |
| D107 | Alert banner, when the last rung reopens after its ship lands (D1a) · SIM `AdvanceQuotaLadder` | `CEO TRANSMISSION — quota %s renewed (round %d): deliver structural 3125 kg, Oxygen 1500 kg, Hydrogen 313 kg by sol %d. The program's eyes stay on Mars.` | PLACEHOLDER. Says the bills went up by showing them, not by saying why. | keep pending review |
| D108 | Objective panel prose, Open phase, round above 0 · PANELS | `Earth renews the quota with every bill raised. Stockpile it to earn the next supply ship.` | PLACEHOLDER. | keep pending review |
| D109 | Objective panel header, Open phase, round above 0 · PANELS | `Q3 - round 2, bills up 56% - DUE SOL 61` (the percentage is cumulative on the authored bills) | PLACEHOLDER. | keep pending review |
| D110 | Guide step "Bring the first crew" body (it touches food; replaces the line that said it may be the last ship) · GUIDE | `Your vault has %d bed%s. On the next supply ship send a Crew Pod - and an Earth Soil Pallet and a Seed Vault: a garden is how they will eat, and each later ship is a whole quota away. Meet the next quota to earn it.` | PLACEHOLDER. Prevention-framed: the fix first, no hunger imagery. | keep pending review |

## Patterns to adopt game-wide

1. **One recall vocabulary.** People are *recalled* and *safe in orbit*. They are never evacuated, returned, lost or failed. Every line must read right for a Mars-born child, who has never been to orbit and whose home is Mars.
2. **The fix comes first, the consequence second.** A warning names what to restore and how long is left. The recall is the last clause, stated once and calmly. Countdowns count toward the fix ("sols to restore"), not toward removal.
3. **Systems fail, people don't.** Failure words (failing, strained, collapse, crater, bites) describe machines, supplies and relations, never people or their mood. Say "Hope", not "morale". The mood bands describe without judging.
4. **Politics uses a diplomatic register.** No war, menace or slang: avoid "cold war", "pacify", "hostile", "they will remember this", "crater". Use "side with Earth", "reassure", "frozen", "drop sharply". Theft and sabotage stay abstract and system-level ("their systems are down"), never about people.
5. **Only player words reach the screen.** Internal verbs and row IDs (Launder, Pacify, Solidarity Comply, Zarya) are mapped to the button labels and DisplayNames before they are displayed.
