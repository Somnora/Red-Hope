# BM-7: a free Move order for furnishings and wall fittings (design and build brief, 2026-10-31)

The orchestrator wrote this at HEAD `9f0c971` while E8 was building; E8 is committed at `8cda69f`. E8's changes touch, among others, `RHPlayerController.cpp`, `RHCommandDeck.cpp`, `RHClickTest.cpp`, `RHCheats.cpp` and `RHSimWorldSubsystem.cpp`, so line numbers will have moved. Every anchor below is a symbol: re-anchor by symbol at the build's HEAD, never by line. An independent read-only review on 2026-10-31 found 20 items; all of them are folded in, as listed in the REVIEW section at the end.

Built after E8, by `docs/qa/2026-10-26/workflows/bm7-gate-wf.js`.

**Authority, highest first:**
1. This file's DIRECTOR ANSWER.
2. `docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md`.
3. The 2026-10-15 build-mode ruling and the 2026-10-16 bed ruling in `docs/design-decisions.md`.
4. This file's BRIEF.
5. The rest of this file.

Where the brief marks something DEFAULT, take it, and list it as a deviation if you change it.

## The director's request (2026-10-31)
The director asked whether placed furnishings, vents and lifts can be moved after they are placed, and whether walls can be extended to make rooms bigger. The orchestrator answered from the code at `9f0c971`:

- **Nothing can be moved.** No order verb moves anything.
  - Furnishings and fittings can only be sold back at 75% (`FurnitureRefundFraction` 0.75, ruling 5a) and placed again, so moving one loses 25% of its bill.
  - Modules and machines can only be demolished, for 50% salvage (`SalvageFraction`).
  - Lifts and shafts are fixed: the Borer can't be demolished once boring starts, and there is no order to remove a lift car.
  - Vents are decoration that `RHColonyVisualizerSubsystem` draws on every third wall face.
- **Walls are derived** (2026-10-15 ruling, item 3).
  - A habitat module's size is fixed when it is placed.
  - A neighbouring module joins the network through a doorway.
  - An underground room grows when more cells are excavated and zoned.

The orchestrator offered one free order that picks up a furnishing or a fitting and puts it down somewhere else, reusing the existing placement checks. Moving modules and lifts would be far bigger work, because roads, power, residents and paths all depend on where they stand.

# DIRECTOR ANSWER (2026-10-31)
"add a free move order as a gate after E8". Settled by that answer:

1. **Scope:** furnishings (in surface habitat bays and underground cells) and wall fittings (windows, doors, airlocks). Modules, machines, lifts, shafts and vents are out of scope.
2. **Free.** A move:
   - spends nothing and refunds nothing;
   - changes no stock, Lander bin or ledger leg;
   - is allowed at zero stock, for a furnishing and for a fitting.
3. **When:** it runs as its own gate after E8. The orchestrator puts it ahead of the idle robots, T1 and T2 in the queue.
4. **Launch (asked 2026-10-31, answered (a)):** start BM-7 automatically as soon as E8 passes the orchestrator's verification and is pushed. The run still stops on any director question, failed check or precondition stop. The idle robots, T1 and T2 still need the director's go.

# BRIEF

## 1. Decided (orchestrator defaults under the director's answer)
1. **One verb, `Move`, riding the uplink like every order.** It executes when it lands after `OrderLagSeconds`, with no time in between. This is ruling 5a's "installs instantly from stock after the uplink lag", minus the stock.
2. **No save change.** The order fits the fields `FRHCommand` already serializes with the queue (`Verb`, `Target`, `Level`, `Location`, `LocationB`, `Value`).
   - RHSaveVersion stays at the `saveVersion` arg (60 after E8).
   - No frame, no new saved field, no DataTable or `docs/data` change.
   - A furnishing Move:
     - `Target = "Furniture"`;
     - `Level`, `Location` and `Value` are exactly what a `Furnish` order for the destination carries: `Value = EncodeFurnish(Level == 0 ? 0 : cell, Slot, Rot)`, and a surface bay is found by `Location`, so `ResolveFurnishOrderKey` resolves it unchanged;
     - `LocationB.X` = the piece's Id, an integer held exactly in a double; `LocationB.Y` = `LocationB.Z` = 0.
   - A fitting Move:
     - `Target = "WallFeature"`, `Level = 0`;
     - `Location` = a point in the destination bay and `Value` = the side, exactly as in a `Fit` order, so `ResolveWallEdge(Location, (int32)Value, Edge)` resolves it;
     - `LocationB.X` = the fitting's Id.
   - A v60 binary that loads a save with a Move queued rejects that order as "Unknown order 'Move'". This is harmless; say it in one line of the decisions entry.
3. **The piece keeps its identity.**
   - Its Id stays the same, and so does its position in `Furniture` / `WallFeatures`: mutate `Key`, `Slot` and `Rot` (or `Edge`) in place.
   - Never remove and re-add. That would change Ids, the array order and every derivation ordered by Id. `NextFurnitureId` and `NextWallFeatureId` don't change.
   - Who sleeps in which bed is derived (`BuildBedAssignments`: bay or cell order, then piece Id), so a move can re-pair the sleepers of one home. That is accepted. Do NOT change `BuildBedAssignments`: its order is pinned by existing battery lines (DD3).
   - Const getters `GetNextFurnitureId()` and `GetNextWallFeatureId()` may be added for the suite (read-only, inert).
4. **Where a furnishing may go:** anywhere a NEW piece of its row could go.
   - That includes the same cell (a nudge or a rotation), another room, another habitat and another floor.
   - Every rule of `CanPlaceFurniture` applies, with its reason texts:
     - the catalogue and `SliceActive`;
     - `IsFurnishKeyValid`;
     - D7's open-cell refusal;
     - a zoned room;
     - `FurnitureAllowsFunction`;
     - the footprint mask;
     - overlap;
     - in transit;
     - a machine standing there.
   - Two exceptions:
     - **No cost check** (`bCheckCost = false`, the existing C5b path).
     - **The moving piece never collides with itself.** The overlap loop and the in-transit loop skip its Id, so a nudge inside its own footprint, or a rotation in place, is allowed.
   - The same cell, slot and rotation is refused: "It is already there" (placeholder).
5. **The bed guard on the end state** (the 2026-10-16 ruling, applied to a move).
   - **The two places.** S is the sleeping place the piece leaves: its habitat building if `Key.Z != 0`, else its floor `Key.X`. D is the place it joins. Both are judged per building or floor, as `GetHabResidents` (`HomeHab == Id`) and `BedGuardAllowsRemoval` count residents. If S = D, count once.
   - **The rule.** For each place:
     - Before = today's `CountHabBeds` / `CountFloorBeds`;
     - After = the same count with the piece at its destination;
     - refuse when `After < Before && After < Residents` of that place.
   - **The reason text:** "%d resident(s) sleep here - moving this would leave someone without a bed" (placeholder). %d is the residents of the place that refuses: S first, then D.
   - **What it carries over.** The ruling's surface-Living rules come with it:
     - a non-bed piece moved into a bare Living bay ends that bay's legacy beds;
     - the last piece moved out of a furnished Living bay brings them back.
   - **How to compute After.** Extend `CountHabBeds` and `CountFloorBedsImpl` (each has its own loop) with ONE optional moved-piece override ("piece Id is at Key/Slot/Rot instead"), and compute After through them. A parallel, hand-written bed rule is a code finding. `PreviewFurnishEffect` is the add-only path and is not used for a move.
   - **It replaces both existing guards.** A move does not also run `CanPlaceFurniture`'s add-only guard or `BedGuardAllowsRemoval`'s remove-only guard. The two give different answers (MV5 (vi)).
6. **Where a fitting may go:** anywhere a NEW fitting of its row could go, by every rule of `CanPlaceWallFeature`, with its reason texts:
   - `ClassifyWallEdge` by row kind: a door goes in a doorway, a window or airlock on an exterior edge;
   - the edge is free;
   - nothing is in transit for the edge;
   - an airlock's chamber is clear of machines.

   Two exceptions:
   - **No cost check.** `CanPlaceWallFeature` today has no `bCheckCost` and always checks cost at its end. Give it a defaulted `bCheckCost = true`, or a private helper with the rules and without the cost, so every existing caller behaves exactly as before.
   - **The same edge** is refused: "It is already there".

   Moving a door out of a doorway may unseal a network. That is allowed, as Sell allows it: there is no seal guard.
7. **Preflight and execution: two public const checks.** The UI and the suite call them, and `ExecuteCommand` calls them again with `bCheckQueued = false`:
   - `CanMoveFurniture(int32 Id, const FIntVector& Key, int32 Slot, int32 Rot, FString& OutReason, bool bCheckQueued = true) const`;
   - `CanMoveWallFeature(int32 Id, const FIntVector& Edge, FString& OutReason, bool bCheckQueued = true) const`.

   They share code with `CanPlaceFurniture` / `CanPlaceWallFeature`, through an extra defaulted parameter or a private helper both call. Each refuses:
   - FIRST, a missing piece, with the existing texts: "No furnishing #%d - it is already gone" / "No fitting #%d - it is already gone";
   - with `bCheckQueued`, a piece that already has a Move or Sell order in transit: "That piece already has an order in transit" (placeholder).
8. **In transit, both ways.**
   - `CanPlaceFurniture`'s queued loop also counts the destination footprints of `Move`+`"Furniture"` orders, so a Furnish can't target a spot a move is heading to. The text is unchanged: "A %s is already in transit for that spot".
   - `CanPlaceWallFeature`'s queued loop also counts the destination edges of `Move`+`"WallFeature"` orders. The text is unchanged: "A fitting is already in transit for that wall".
   - The move checks count queued Furnish / Fit orders and other queued moves the same way.
   - All of this matters only while a Move is queued, so it is inert otherwise.
   - At landing (`bCheckQueued = false`) no in-transit rule runs. If a Move and a later Furnish to the same spot were both queued without a preflight, the Move lands and the Furnish is then refused for the overlap.
9. **Executing:** a new `else if (Cmd.Verb == FName("Move"))` branch in `ExecuteCommand`, placed before the generic `Sell` branch.
   - **Check order:**
     - FIRST the target kind: any other `Target`, or a `LocationB.X` that is not a whole positive number, gets "Move needs a furnishing or a fitting" (placeholder);
     - THEN the piece exists (the "already gone" texts);
     - THEN the destination resolves and passes.
   - **A furnishing:**
     - mutate it in place, then `RebuildFurnitureCache()` and `RefreshJobs()`;
     - log `Moved %s #%d from (%d, %d, %d) slot %d rot %d to (%d, %d, %d) slot %d rot %d`;
     - `OnFurnitureChanged.Broadcast(old key)` then the new key, once if they are the same.
   - **A fitting:**
     - mutate `Edge`, then `RebuildWallFeatureIndex()`;
     - log `Moved %s #%d from wall (%d, %d, %d) to wall (%d, %d, %d)`;
     - `OnWallFeatureChanged.Broadcast(old)` then the new edge.
   - **Neither** calls `SpendTradeLot`, `CreditLander`, `AddStock`, `NoteFlowMap` or a refund, or touches `HabO2Kg`, Hope or the ledger directly.
   - **Derived state** re-derives as after any furniture or fitting change: beds, bed assignments, seats, comfort, views, seals, leak, and the home cells with everything keyed on them (districts, commute, atrium facing).
10. **Inert.** A colony that never issues a Move behaves byte for byte as before: no existing battery line moves, apart from timing digits. The playtest is cmp-identical, `-scale`'s digests are identical, and no line of the save writer or reader changes.

## 2. The player's tool (Source/RedHope)
1. **The deck.** A **"Move"** `DeckButton` right after each "Sell" button: the Furnish palette and the Walls palette in `SRHCommandDeck`, each shown only when its palette has pieces. It calls `SRHCommandDeck::HandleMove` → `ARHPlayerController::BeginMove()`.
   - The click test's widget search skips widgets clipped by a scroll box. If the Furnish row overflows at 1440x860, put Move FIRST in that palette instead, and say so; capture 7 proves it is visible.
2. **The controller.**
   - **State:** `bMoveMode` and a held piece (`MoveKind`: none / furnishing / fitting, and `MoveId`). Hold by Id and look the piece up on each use; keep no pointer across frames.
   - **Wiring.** The mode joins every place the other modes are listed:
     - `HasActiveMode` (without it, Esc opens the pause menu while moving);
     - `CancelModes` and its "no mode" test;
     - the traffic hover's `bToolArmed` list;
     - each `Begin*` that clears modes by hand (placement, dig, survey, excavate).
   - **A click with nothing held** picks by Sell's own rule: `FindFurnitureAt(Ground)` first; else, on the surface, the fitting on the nearest wall (`ComputeFitEdge` + `FindWallFeatureOnEdge`). Picking a piece that already has an order in transit is refused, with the Error cue and the reason.
   - **A held furnishing:**
     - the cursor shows the furnish ghost, from the same code as Furnish mode: its row, its current rotation, the same rotate key, and the same hint lines reading the move check's reason;
     - its current footprint is outlined in a move colour, DEFAULT violet (180, 120, 255), placeholder;
     - a click runs `CanMoveFurniture`;
     - if it passes: transmit the Move (Confirm cue, "Move order transmitted: %s #%d", placeholder), drop the hold, and keep the mode armed for the next pick;
     - if it fails: the Error cue and the reason.
   - **A held fitting:** the same flow on the lit wall, with the Fit ghost for the fitting's row and `CanMoveWallFeature`.
   - **Cancel:** right-click / Esc follow the existing mode-cancel behaviour. State in the report what a first Esc does with a piece held.
   - **Changing floor:** `SetActiveLevel` never cancels modes, so a held piece survives a floor change and cross-floor moves work in the UI. Prove it with capture 6.
3. **In-transit markers.** In the queue loop in `ARHPlayerController`'s tick that draws Furnish, Fit and Sell orders in transit:
   - a furnishing Move draws its destination like a Furnish in transit (cyan box, "Move <Row>  Δ Ns") and its source box in the move colour;
   - a fitting Move draws its destination wall line in cyan and its source wall line in the move colour.
4. **The uplink list** (`SRHCommandDeck`'s queue labels):
   - "Move <Row> #<Id>";
   - "Move a furnishing" / "Move a fitting" when the piece is gone.

   `RHOrder::VerbText` gains `Move`.
5. **Developer cheats** in `RHCheats.cpp`, beside RH.Furnish / RH.Fit / RH.Sell. They are developer-facing and stay out of the wording file:
   - `RH.MovePiece <Id> <Level> <Cell|Xm Ym> <Slot> [Rot]`, mirroring RH.Furnish's arguments (underground takes a cell index);
   - `RH.MoveFitting <Id> <Xm> <Ym> <Side>`;
   - `RH.BeginMove`.
6. **Redraw.**
   - **Colony view.** `RefreshFurniture` re-signs a piece by (row, key, slot, rot) and runs on change or at the one-second poll. Re-signing bumps `PropsVersion`. Habitat shells redraw on `OnWallFeatureChanged` → `MarkPassesDirty`.
   - **Section view.** Its structure key reads `GetPropsVersion()`. Its people key mixes the furniture COUNT and `GetBedAssignVersion()`, which follows `FurnitureVersion` only when the colony has colonists.
   - Verify it with a capture; don't assume it. A moved underground piece must redraw in the section view within a second.
   - If it doesn't, add a counter that is 0 until the first move and mix it into the key only while it is non-zero (D7's pattern), so `RH.DeepBench` and D8's digests do not move.

## 3. The `-moves` suite (new file `Source/RedHopeSim/Private/RHSimMoveSuite.cpp`; log `moves.log`; one battery run)

**Plumbing.**
- Follow `RHSimAtriumSuite.cpp`'s pattern: a declaration in `RHSimCommandlet.cpp` beside `RHRunAtriumsSuite`, and the switch `-moves`.
- Print "MOVES ok:" / "MOVES FAIL:" lines and end with "=== MOVES TEST END: N check(s), 0 failure(s) ===". The suite fails loudly.
- Print no file size and no save version anywhere. Print every check id in order, MV0 to MV11.
- Orders land only inside `EraStep` (`ExecuteCommand` is private). Queue with `EnqueueCommand` (which runs no preflight) and step, as the `-furnish` suite does.

**Comparisons are exact (`==`) unless a line says otherwise.**

**"Stock unchanged"** means, for each resource the piece's bill names (Struct for furnishings; Struct and Glass for a window; read the bill with `URHDefinitionsSubsystem::GetFurnitureCost` / `GetDoorCost`):
- `GetStock`;
- `GetTotalSolid`;
- the Lander's `InputKg` / `OutputKg` entries;
- `GetLedgerLifetimeG` / `GetLedgerOpenKg` on the Used and Made legs.

Each is exact, snapshotted just before the step the order lands in and read just after. No construction site is open in the fixture. Other resources move with life support and are not compared.

**The checks.**
- **MV0. The fixture, fail fast.**
  - `DT_Furniture` and the walls catalogue are present.
  - H1 and H2 are two SEPARATE habitat networks, each ONE multi-bay HabModule building.
  - H1 has Living bays a, b and c. Bay a holds one BunkBed (2 beds), H1's only bed piece; b and c are bare (1 legacy bed each).
  - H1 is rated, and its residents equal its beds (4).
  - H2 has Living bay x holding a Locker, a BARE Living bay y, and work bays. H2 is rated and full (residents equal to beds) whenever (vi) runs.
  - An UNRATED habitat with an exterior edge free, for MV8 (i).
  - For MV8 (v): a network with two doorways, and a second network with a free doorway.
  - One underground floor has zoned cells.
  - Print each place's beds and residents.
- **MV1. A move within a habitat.** Move a non-bed piece to another bay of the same habitat:
  - the same Id;
  - its array index unchanged;
  - `GetFurniture().Num()` unchanged;
  - `GetNextFurnitureId()` unchanged;
  - the new key, slot and rot exactly;
  - stock unchanged;
  - `OnFurnitureChanged` fired for the old key, then the new key;
  - the "Moved ..." line logged.
- **MV2. In place.**
  - (i) The same slot, rot + 1: accepted.
  - (ii) A nudge one sub-cell inside its own footprint, on a piece at least 2 sub-cells wide: accepted.
  - (iii) The same slot and rot: refused, "It is already there".
- **MV3. Far moves.**
  - (i) A piece moved from H1 to H2.
  - (ii) An underground piece moved to another cell of its floor.
  - (iii) An underground piece moved to a surface habitat.
  - (iv) A bed into a work room: refused with the exact reason `CanPlaceFurniture` gives a new bed there.
  - (v) With every resource in the piece's bill at 0 (Struct): a move succeeds and stock stays 0.
- **MV4. Every destination refusal matches Furnish.** For each destination below, the Move's reason equals `CanPlaceFurniture`'s reason for a new piece of the same row at the same spot, compared as strings:
  - an occupied footprint ("Overlaps the %s #%d");
  - a machine standing there;
  - an unzoned bay;
  - the wrong room function;
  - off the grid ("It doesn't fit there - keep it inside the room");
  - D7's open atrium cell. This case is MANDATORY: build the atrium as the `-atriums` suite does.

  No move is ever refused for cost.
- **MV5. The end-state bed guard.** Each case prints Before, After and Residents for each place it judges.
  - (i) H1's BunkBed moved to a Living bay of H2 (anywhere else it is refused for the room function, not by the guard): H1 goes 4 → 3 beds with 4 residents. Refused, with the move reason naming 4. H1's beds and the piece are unchanged.
  - (ii) The BunkBed moved to bay b: H1 stays at 4. Accepted. H1 is still 4 beds for 4 residents. H1's two lowest-Id residents read the BunkBed's Id from `GetColonistBedPiece` before and after the move, and the other two read 0 (legacy beds), as `BuildBedAssignments` derives them.
  - (iii) H2's Locker moved into H1's bare bay c, ending its legacy bed: H1 goes 4 → 3. Refused. S = H2, D = H1, so a guard that judges only S accepts it.
  - (iv) On its own state: a Rug placed in a Living bay before residents arrive, the bay holding no bed piece. Move the Rug to a Dining or Hallway bay, or to another habitat; NOT to a bare Living bay of the same habitat, which would leave the total flat. The source bay's legacy bed comes back: accepted, and the habitat's beds rise by exactly the legacy count.
  - (v) A floor whose only bed piece is a BunkBed, with residents equal to its beds. Moved to a surface habitat: refused. Moved to a bare Living cell on the same floor: accepted.
  - (vi) H2 full (residents equal beds). The Locker, the only piece in Living bay x, moved to the BARE Living bay y: the source bay gets its legacy bed back and the destination loses one, so H2's end state is unchanged. Accepted. The add-only guard alone would refuse it.
- **MV6. Seats.**
  - A work seat moved within its room: the room's `GetStationSeats` is unchanged, and the seat's colonist is still seated after `RefreshJobs`.
  - A seat moved to a non-work room: refused with Furnish's function reason.
- **MV7. In transit.**
  - (i) A second Move of a piece with a Move queued: refused by `CanMoveFurniture`, "That piece already has an order in transit".
  - (ii) A Move of a piece with a Sell queued: refused, with the same text.
  - (iii) `CanPlaceFurniture` for a new piece on a queued Move's destination: refused, "A %s is already in transit for that spot".
  - (iv) `CanMoveFurniture` onto a queued Furnish's spot: refused, with the same text.
  - (v) A Sell that lands before a Move queued with `EnqueueCommand` (no preflight): at landing the Move is rejected with "No furnishing #%d - it is already gone", and nothing else changes.
  - (vi) `CanPlaceWallFeature` for a new fitting on a queued fitting Move's edge: refused, "A fitting is already in transit for that wall".
  - (vii) `CanMoveWallFeature` onto a queued Fit's edge: refused, with the same text.
  - (viii) A Move and a later Furnish to the same spot, both queued with `EnqueueCommand`: the Move lands, and the Furnish is refused for the overlap.
- **MV8. Fittings.**
  - (i) A window moved from a RATED habitat to an exterior edge of an UNRATED one:
    - the same Id and the new edge;
    - stock unchanged;
    - `OnWallFeatureChanged` fired for both edges;
    - `GetViewsHope` drops by exactly that window's Views term, as the row and config give it (computed in the suite from the row, not from the code under test).
  - (ii) A door moved to an exterior edge: refused, with Fit's own reason.
  - (iii) A window moved onto an edge that already has a fitting: refused, "The %s #%d is already there".
  - (iv) An airlock moved to an edge whose chamber holds a machine: refused, with Fit's chamber reason.
  - (v) Seals:
    - a network with two doorways, one doored: move the door to the other doorway, and the network is still unsealed (1 of 2 doored) with the old edge free;
    - door both, so it is sealed; then move one door to a free doorway of another network: the first is unsealed (`IsHabNetworkSealed` false) and the second's doored count rises by 1;
    - move it back: sealed again;
    - move it to an exterior edge: refused, with Fit's text.
  - (vi) The same edge: "It is already there".
  - (vii) With Struct and Glass both at 0: a window move succeeds and both stay 0.
- **MV9. A queued Move survives a save and a load.**
  - Queue one furnishing Move and one fitting Move, save, load, and let them land.
  - The end state equals a twin that never saved: each piece's Id, key/slot/rot (or edge), and the beds and seats.
  - Both orders keep their `CommandId` and `LocationB`.
- **MV10. A bad order.**
  - `Target "Building"`: refused, "Move needs a furnishing or a fitting".
  - `LocationB.X` = 0 or 2.5: the same text.
  - A Move whose piece's habitat was demolished before it landed: refused, "...it is already gone".
- **MV11. Determinism.** Two fresh fixtures run the same scripted sequence of moves, including MV5 (ii) and MV8 (i). They must end with identical `GetFurniture()` and `GetWallFeatures()` arrays, bed assignments and job seats, compared field by field.

**Mutations.** Each is built, run, and must FAIL its named check (quote the failing line). Each is restored by the source file's sha256.

| # | Mutation | Must fail |
|---|---|---|
| 1 | the furnishing Move spends the piece's bill (`SpendTradeLot` as Furnish does) | MV1 stock, MV3 (v) |
| 2 | the furnishing Move is RemoveFurniture + AddFurniture (refund and a new Id) | MV1 Id and stock |
| 3 | the overlap loop does not skip the moving piece | MV2 (i) or (ii) |
| 4 | the Move skips the overlap loop entirely | MV4 overlap |
| 5 | the end-state guard judges only the place the piece leaves (S) | MV5 (iii) |
| 6 | no bed guard on a Move | MV5 (i) |
| 7 | `CanPlaceFurniture`'s queued loop ignores Move orders | MV7 (iii) |
| 8 | a fitting Move skips `ClassifyWallEdge`'s kind rule | MV8 (ii) |
| 9 | `OnFurnitureChanged` not broadcast for the old key | MV1 broadcast order |
| 10 | the fitting Move keeps `CanPlaceWallFeature`'s cost check | MV8 (vii) |
| 11 | the remove-only guard on S plus the add-only guard on D instead of the end-state guard | MV5 (ii) or (vi) |

## 4. The click test (main scene; +5 steps, exactly)

**Setup and placement.**
- The steps go before the runner's `final` block.
- The setup follows `atrium.setup`'s pattern and reports only on failure, so it is not a counted step. It builds:
  - a surface habitat of at least two zoned Living bays;
  - one bed piece and one window, placed through the harness.
- Switch tabs with `Debug_ShowFurnishTab` / `Debug_ShowWallsTab`.
- For these steps, shorten the uplink lag with `SetOrderLagSeconds`, and restore it after `move.fitting`.

**Counting.**
- Each deliberate world click does `++T->ExpectedWorld`, as `build.place` does, and its check sets `bAllowWorldClick = true`. Otherwise `leak.total` reports a leak.
- There is one report per named step. Deck presses, the rotate key and intermediate clicks get no check of their own.

**The five steps.**
1. `move.tool`: press the Furnish palette's "Move" button, located by its label. The controller is in Move mode with nothing held.
2. `move.pick`: click the bed piece. The hold is that Id, and the ghost shows its row.
3. `move.place`: click a free spot in another bay. "Move order transmitted" shows. After the lag, the piece has the same Id at the new key and slot, and the furniture count is unchanged. Freeness is the suite's job, so there is no stock check here: the demo scene's growth and sites use Struct.
4. `move.rotate`: pick it again, rotate once, and click its own spot. Its rot is +1 and its slot is the same.
5. `move.fitting`: on the Walls tab, press "Move", click the window, then click another exterior wall. After the lag, the same Id stands on the new edge.

The main click test's TOTAL rises by exactly 5. The deepbench scene is unchanged.

## 5. Captures (`Saved/RHCapture/bm7/`, 1440x860; look at every one)
1. A furnished bay before a move.
2. A held piece: the ghost at the cursor and the violet source outline.
3. The in-transit markers: the destination box, the source box and the countdown.
4. After it lands.
5. A window before and after a move, with both wall lines.
6. An underground piece moved in the section view, before and after. Include a hold carried across a floor change.
7. The deck's Furnish and Walls palettes with the new "Move" buttons, at 1440x860 and 1600x900, with no clipping.

## 6. Wording
`docs/qa/2026-10-15/gate-d-wording.md` gains "## Rows added by BM-7 moving (<date>)", with the prefix **B** (unused). Add one row per new player-facing string, each marked PLACEHOLDER:
- the "Move" button;
- "Move order transmitted: %s #%d";
- "It is already there";
- "That piece already has an order in transit";
- the bed-guard move text;
- "Move needs a furnishing or a fitting";
- the in-transit "Move <Row>" label;
- the uplink list's "Move <Row> #<Id>", "Move a furnishing" and "Move a fitting".

## 7. Do not
- Bump RHSaveVersion, add a frame or a saved field, change `FRHCommand`, or change any line of the save writer or reader.
- Touch `docs/data` or any DataTable.
- Move modules, machines, lifts, shafts or vents.
- Remove and re-add a piece, change an Id, or reorder `Furniture` / `WallFeatures`.
- Change `BuildBedAssignments`.
- Spend, refund, credit, note a ledger flow or touch air in a move.
- Change any existing refusal text, any existing order's behaviour, any existing suite, or the Sell or Demolish rules.
- Add a seal guard on doors.
- Weaken a check to make a mutation pass.
- Make Hope or any morale text depend on moving.
- Remove or weaken any existing click step.

# REVIEW (2026-10-31, an independent read-only reviewer, verdict BLOCKED; all items folded in above)
**Blocking.**
1. "Stock unchanged" was impossible as written, because life support runs in the same step. It is now the bill's resources only.
2. MV5's fixture could not make the guard decide: one bay keeps 1 legacy bed, and a Cot holds 1. It now uses single-module H1/H2, a BunkBed, and takes (iii)'s piece from H2.
3. MV8 (v)'s sealed-to-sealed case was impossible. Replaced.
4. 1.3's "the bed follows the sleeper" was false: `BuildBedAssignments` orders by bay, then Id. Corrected, and `BuildBedAssignments` is frozen.
5. A fitting moved at zero stock was untested, and `CanPlaceWallFeature` always checks cost. Added MV8 (vii) and mutation 10, and a defaulted `bCheckCost`.

**Minor.**
- The click test's leak accounting and `atrium.setup`'s pattern; the tab helpers; the lag shortened.
- Distinct click labels and a 450 s timeout (in the gate script).
- The Struct check dropped from `move.place`.
- Move goes first in the palette if the row overflows.
- MV7 (vi)-(viii) for fittings, and the landing order.
- MV5 (vi), with mutation 11.
- MV8 (i)'s Views term made falsifiable.
- MV7 (v) queued without a preflight; the check order in 1.9.
- The `Next*Id` getters.
- 1.5 names the two counters to extend.
- 2.2's full mode wiring; cross-floor holds work, because `SetActiveLevel` never cancels modes.
- RH.MovePiece mirrors RH.Furnish.
- MV4's atrium case is mandatory.
- The script gains a design-file precondition and a save-code check.

**Re-check (2026-10-31): CLEARED.** The 20 items are resolved. The MV5 arithmetic and mutation 11 were verified against `CountHabBeds`. Five text fixes followed: MV0's extra fixtures; (i)'s Living destination; (ii)'s observable bed assignment; (iv)'s destination; (vi)'s BARE destination bay, without which mutation 11 is caught only by (ii).

**Noted.**
- The section view's structure key reads `GetPropsVersion()`.
- A v60 binary rejects a queued Move as unknown.
- Home-cell re-derivation is listed in 1.9.

# DIRECTOR ANSWER (2026-11-02): BM-7's 1600x900 click run

Asked by the orchestrator on 2026-11-02 after fix round 1 (run `wf_57b13afc-1fc`; work folder `/Volumes/Unreal/red_hope/wip-backups/bm7-work/r1/`). Round 1 changed nothing outside `docs/` and rebuilt nothing; the fingerprint stayed `e936bf770f832ccd1b6e5e6d446691c0245e8f6b07f0d9d0acefc423aa77cce8`.

- **`bm7r1_click1600`** (1600x900, 12:31; 1-minute load 4.89 at the start and 14.68 mid-run; frames up to 1,013 ms) failed with no verdict at the 450 s limit. Two presses in steps older than BM-7 were dropped: `build.card` ('Solar Array') and `section.on` ('Section'). In both, the down and the up were handled at the same point and the button did nothing. Two drags moved OnClick by 0, and the section steps then failed in a cascade. No BM-7 step ran.
- **The same signature** (a press whose down and up are both handled at one point, then a failing first check) is in 46 presses in 22 logs from 2026-10-24 to 10-31, all on binaries built before BM-7: 19 of 132 main-scene runs (`r1/diag/scan_untaken2.py`, `r1/diag/untaken_presses.out`). The cause is in Slate. `SButton::OnMouseButtonUp` runs `OnClicked` only while the button is hovered and holds capture, and any window activation releases capture. BM-7's diff touches none of the press path.
- **`bm7r1_click1600b`** (12:56; loads 3.75 / 4.66) passed 114/0, with the five move steps and `leak.total` 11 of 11. The counted 1440x860 pair (`bm7r1_click1`, `bm7r1_click2`) passed 114/0, and `bm7r1_deepbench` 9/0.

**The answer: (a).** `bm7r1_click1600b` counts as the gate's 1600x900 run, and `bm7r1_click1600` stays on the record as an environment failure. The same reasoning covers round 0's `bm7_click3`, which needs no ruling for the gate because round 1's own named pair passed. Not taken: (b) one more 1600x900 run at a quieter time, counted only if it passed; (c) hold BM-7 until the harness's dropped press is fixed in its own gate.

Ruling 26 applies the same rule to later gates. The harness fix stays a follow-up.
