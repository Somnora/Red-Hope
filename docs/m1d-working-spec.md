# M1-d Working Spec — "The Vault" (Phase 1 exit)

Status: **agent-proposed, awaiting director OK on the gate plan.** Nothing here is started.
Anchors — all approved canon, no new invention:
- `docs/m1-underground-proposal.md` (the load-bearing design: excavation cheap + player-directed, habitability the constraint; Borer-as-tool §2, hydrogen fuel §2, habitability chain §3, sliced-ant-farm camera §4, shaft-as-vertical-trunk §5, M1-d definition §6).
- `docs/habitat-vision.md` §9 (APPROVED mapping: M1-d births the room/compartment/door/filtration **schemas dormant**; M2 activates the human layer).
- Rulings `design-decisions 2026-07-07c` (shielding build tax lands here, not M1-c) and `2026-07-07d` (RC-M retained — robots repair/fabricate robots; human purpose is an M2 theme, not a repair monopoly).
- Groundwork already banked: the **Z-model** `(X,Y,Level)` (M1-b, live), **radiation plumbing** `GetRadiationAtLevel/GetRadiationNow` + the per-floor shielding curve (M1-c, live), the save format at **v4**.

## 0. What M1-d turns the game into

M1-c made the surface hostile — storms grind the fleet, flares irradiate it, and the inspection card now tells you *"shielded underground"* you can't yet reach. M1-d makes underground real: you build a **Borer**, paint a shaft and floors down into the regolith, and the overburden that the radiation curve already models becomes **free shielding you can stand behind**. But a carved pocket is a spacesuit-only void until you've **shielded, oxygenated, and circulated** it — habitability is the constraint, excavation is not. The Phase-1 exit is a bored, shielded, sealed, trunk-powered, O₂-fed **vault rated for the first crew** — the "livable space" the director said the game was missing. The flare you learned to dread in M1-c becomes *the moment you're glad you dug.*

## 1. Gate plan (three compiles, each verified headless before the next)

- **Gate A — the Borer & the shaft** (`RedHopeSim` + data + `RedHope` camera): excavation becomes a real, player-directed tool; Level becomes buildable; the shaft trunk carries power/O₂ down; the sliced-floor camera + elevator selector. Save **v5**.
- **Gate B — the habitability chain** (`RedHopeSim` + data): per-floor **bore → shield → oxygenate → circulate**, O₂ fill scaling with carved volume, the **shielding build tax** on surface structures (deferred from M1-c — lands here where the underground contrast makes it a real tradeoff), a floor rated *livable* only when the chain completes.
- **Gate C — dormant human-layer schemas + the exit** (`RedHopeSim` data + `RedHope`): room/compartment/door/filtration **schemas born dormant** (M2 activates content, not schema), buildable-dormant treatment facilities, and the **Phase-1 exit card** on a vault rated for crew. Hand-played fresh-landing → vault run.

## 2. Gate A — the Borer & the shaft

- **Borer capability + excavation designation** (underground §2): a `Borer` building is a one-time capability unlock; then excavation is a **paint-to-size designation** on the active floor (same click-to-designate language as digging and surveying — one interaction pattern, surface and subsurface). The painted volume works down over time.
- **Level becomes buildable:** `CanPlaceBuilding` / coverage / task-target / hauling already take `Level` (M1-b) — Gate A lights the subsurface path: orders can target `Level < 0` once the shaft reaches that depth; per-level 2D queries (the plane duplicated downward, not 3D geometry).
- **Spoil → Forge loop** (underground §2): boring emits regolith spoil (extraction recipe, `DrillIce` pattern) hauled to the Forge — carving a floor yields ≈ the Struct to build it out. Every number a CSV row; scales with ambition.
- **Hydrogen fuel for heavy machines** (underground §2): the Borer can run on **H₂ instead of grid power** (`FuelType` Grid|Hydrogen + burn rate, data). Closes the ISRU loop — the Electrolyzer's H₂ byproduct becomes fuel. H₂ scarce → rationed, strategic. **Determinism:** fuel draw is a fixed per-substep integration like power; no new float pathing.
- **The shaft as vertical trunk** (underground §5): the shaft carries a power + O₂/air spine down its spine; a floor **joins the grid when the trunk reaches its depth** (bored + spine extended). "Power = territory" generalizes — a pylon pointing down.
- **Sliced-ant-farm camera** (underground §4, presentation-only, dial-gated): elevator-panel selector on the deck (`SURF · −1 … −5`) sets the active interaction plane; floors above fade; un-excavated volume renders as solid rock with a cut top face; **shaft-section HUD widget** (vertical strip: floors stacked, lift position, each floor's state). Gray-box slabs only — no art pass.
- **Save v5:** excavation state (per-cell carved bitmap or run-length per floor), shaft depth, per-floor state flags. v4 refuses loudly (established pattern).
- **Verify (headless, beats in SIM time):** commandlet/harness bores a shaft to −2, extends the trunk, places a building on −1; assert spoil hauled to Forge, building powered off the shaft tap, radiation at −1 reads 0.05 (curve already proven); save v5 round-trips the carved state.

## 3. Gate B — the habitability chain

A carved floor is **bored → shielded → oxygenated → circulated** before it is livable (underground §3), each a data-driven cost scaling with carved volume:

- **Shielding:** deep floors get radiation protection *free* from overburden (the M1-c curve). **Surface** structures pay the **shielding build tax** — `CostResources` gains a `Shielding:N` line, the `MakeShielding` recipe activates (Struct + Ore → Shielding), Ore_A finally matters (the exit-arc geography pull). Now that there's an *underground alternative*, the tax is a real surface-vs-depth tradeoff — the reason the deferral from M1-c was correct. Deliberately readable numbers, not a wall.
- **Oxygen fill + circulation:** O₂ mass per carved 10×10 cell to pressurize + an ongoing top-up for leakage, distributed down the shaft spine. Sketch (all data): a 10×10 floor ≈ ~100 kg O₂ (~2 sols of one Electrolyzer); a 40×40 hall ≈ ~1,600 kg — carve big and you need serious ISRU or it stays a suit-only void.
- **Livable rating:** a floor flips to `Livable` only when bore+shield+O₂+circulation all clear — the gate the exit condition reads.
- **Verify:** bore a floor without O₂ → stays un-livable; supply the chain → flips Livable; surface build order shows the `Shielding` line in site materials; era paired-run still passes the 5% bar with excavation + fill active.

## 4. Gate C — dormant human-layer schemas + the exit

Per the §9 discipline — **schema now, content in M2** (same as the M0 dormant rows):
- **Room schema:** rooms as data rows within a hab shell — `Function` (garden/workstation/lab/living/dining/cooking/smoking), adjacency tags, filtration/plumbing needs. Dormant: no human consumers yet.
- **Compartment / pressurized-door model:** compartments as the pressure/atmosphere unit, doors as edges — the storm-breach-retreat graph exists before breaches do. RC-M and future crew both route through door edges.
- **Buildable-dormant treatment facilities:** water-filter, septic/fertilizer, air-filtration stations (the 2026-07-05 ruling) — placeable, inert until M2's waste/water loop.
- **Phase-1 exit condition:** a bored/shielded/sealed/trunk-powered/O₂-fed vault rated for the first crew → **exit card** (the M0-c finale pattern). This is the milestone's definition-of-done (underground §6).
- **Verify:** hand-played run from fresh landing → dig → spine → Forge → Borer → shaft → shielded, oxygenated, sealed vault → exit card. Director feel-verdict is the M1-d close gate.

## 5. Explicitly out of M1-d (→ M2 unless noted)

- Humans, morale, sickness, the waste/water loop *behavior*, luxury crops/tobacco, interior human activity, suits, the drinking-water psychology — all M2 (schemas born here, dormant).
- Surface hab modules + lego connection + paint-to-size *shells* — M2 (M1-d's vault is the underground-first exit; surface habs are the human-layer expansion).
- Domes/windows, interior cutaway of surface structures — M2.
- Warehouse/garage storm shelter for vehicles — M2 (schema headroom here).
- **Human purpose** (per 2026-07-07d): the positive "what can only a human do" design — M2 theme, not a repair monopoly.
- Robot-fabricates-robot and RC-M repair are **already live** (kept per 2026-07-07d) — no M1-d work.

## 6. Risk & discipline notes

- **Determinism preserved:** Level is an integer coordinate; excavation is discrete cell designation; hydrogen fuel and O₂ fill integrate as fixed per-substep rates; era mode integrates aggregate rates regardless of floor. No float 3D pathing anywhere. The paired-run 5% divergence bar is the acceptance gate on every gate that touches the sim band.
- **Save discipline:** v5 bump, old versions refuse loudly; **commit harness/BP assets before every verification run** (the Content-trash rule).
- **Gray-box only:** sliced-rock slabs and shaft widget are gray-box gray-box; the bespoke building-art milestone stays deferred "until it makes more sense" (director).
- **Balance in data:** every borer rate, spoil yield, O₂-per-cell, shielding cost, H₂ burn rate is a CSV→DT row; no hardcoded balance.
