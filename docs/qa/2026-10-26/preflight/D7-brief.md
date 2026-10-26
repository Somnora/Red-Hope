# D7 atriums: BUILD BRIEF (preflight at HEAD d842dd0, save v55)

Spec: `docs/qa/2026-10-23/d7-atriums-design.md`. Authority: DIRECTOR ANSWER (2026-10-23) > REFRESH AT 31ffa6f (2026-10-25) > REVISION R1-R12 > PROFESSOR REVIEW > draft sections 0-10. This brief applies that order. It also folds in D8's "Required by D8 in D7's build" list (`docs/qa/2026-10-23/d8-deep-view-design.md:503-510` and `:377`). D8 tells the orchestrator to copy that list into D7's brief, but D7's own doc never mentions it.

**Anchor convention.** `W` = `Source/RedHopeSim/Private/RHSimWorldSubsystem.cpp`, `H` = `Source/RedHopeSim/Public/RHSimWorldSubsystem.h`, `T` = `Source/RedHopeSim/Private/RHSimTransit.cpp`, `Dist` = `Source/RedHopeSim/Private/RHSimDistricts.cpp`, `G` = `Source/RedHopeSim/Private/RHSimGrowth.cpp`, `C` = `Source/RedHopeSim/Private/RHSimCommute.cpp`, `DW` = `Source/RedHopeSim/Private/RHSimDeepWalk.cpp`, `Cav` = `Source/RedHopeSim/Private/RHSimCavern.cpp`, `SV` = `Source/RedHope/Private/RHSectionVisualizerSubsystem.cpp`, `CV` = `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp`. Every line number is at **d842dd0**. D7 builds after E5, E6 and D6e, all of which edit W, H, the save and the suites. **Re-find every anchor by its function name before you edit.** The line numbers are a map, not a promise.

---

## 0. What you are building

To make an atrium, the player converts a finished cavern of 2 or 3 floors. That cavern was dug with the Cavern brush; then "Open as atrium" goes on its card. On every floor of the box except the deepest (the "upper floors", `C.Top .. C.Bottom + 1`), the box cells are **flagged open**: no slab, not walkable, no room, no bed. **No cell is ever removed, moved or reordered.** Ordinals are keys (D1 R0), and the open flag is derived from a saved list of cavern Ids.

Homes and seats facing the hall lift Hope (up to +3, per head, counted once, stacking with Shelter). Each open cell costs 1.5x the air fill and 2x the leak, charged to its own floor's pool. The premium fill is billed up front.

## 1. The authoritative decisions (one list)

1. **Q-A (a), director:** convert a finished cavern. New verbs `OpenAtrium` and `CloseAtrium`, each with `Value` = cavern Id. D6a's carving is untouched. Log deviation 6: "Q2a's atrium brush is delivered as convert-a-finished-cavern, two steps".
2. **Q-B (a), director:** no stairs. Floors change by shaft only. D6c's walk stays one floor at a time, and no commute moves.
3. **Placeholders (director, marked "director review"):** `HopeAtriumMax` 3 (stacks with Shelter; at most +6 together), `AtriumFillPermille` 1500, `AtriumLeakPermille` 2000. No Struct bill and no power draw. Add them as rows in `docs/data/RH_Config.csv` **and** `Content/RedHope/Data/DT_Config.uasset`, synced the way D5 did it (df3e479).
4. **Open, never removed:** a derived `FRHAtriumCells` cache, never saved. It holds per level a per-ordinal flag byte (bit 0 Open, bit 1 Faces) and an `OpenCount` per level. Its key is (`AtriumsVersion`, `FloorCellsVersion`).
5. **The switch** is `Atriums.Num() > 0`. Every new branch tests it, or `O_L > 0`, first. Every new key field is `AtriumsVersion` while an atrium exists, else 0.
6. **Refusals (preflight `CanOpenAtrium`, run when queued and again at execute), in this order:**
   1. no such cavern, or cancelled;
   2. already an atrium;
   3. not finished ("finish digging it first: N cells left");
   4. one floor tall;
   5. an upper-floor box cell is in use: a room; furniture; a module; **any floor building's footprint** (tested on every footprint cell, R6); a station key; a tunnel chain cell; or an open growth target (its `Index` cell or any of its `Points`). The reason names the floor and the first such cell in ordinal order;
   6. beds: some upper floor would have fewer beds than residents ("floor -2 would have 40 beds for 46 residents: add beds or move people first"). An atrium never rehouses anyone;
   7. walking: on some upper floor, a dig holding box points would split into more than one 4-component of non-open cells, or would keep non-open cells but lose its last non-open landing. A dig that is entirely void is allowed;
   8. air bill: `PremiumKg` fails `GrowthBillFits({Oxygen: PremiumKg})`, with growth's reason text;
   9. air ceiling, per upper floor L: `(Cells_L + O_L x (AtriumLeakPermille-1000)/1000) x O2LeakKgPerCellPerSol <= O2FillRateKgPerHour x SolLengthSimSeconds / 50`. At the rows the right side is 480 kg a sol, which is 240 cell-equivalents. The floor's own `Cells` count open ones too. The hall floor is not checked;
   10. overlap: "the box shares a point with the atrium over cavern N" (3D: any shared floor and lattice point).
7. **`CloseAtrium`** is always allowed if the cavern is an atrium. The flag clears, nothing is refunded, and the extra air stays in the pool and leaks down.
8. **Air:** two helpers, `GetFloorO2RequiredKg(L)` and a new `GetFloorO2LeakKgPerSol(L)`. When `O_L == 0` each evaluates **the exact expression it replaces**: the premium branch is skipped, never added as 0. At execute the bill is taken with `TakeGrowthResource(Oxygen, PremiumKg)`, and each upper floor gets `FloorO2Kg[L] += O_L x O2FillKgPerCell x (AtriumFillPermille-1000)/1000` at once, so there is no fill window.
9. **Dig labels (R1):** while atriums exist, `EnsureDigInfo` floods non-open cells only, and open cells get `DigOf = INDEX_NONE`. Every `DigOf` reader handles INDEX_NONE. `Touching` skips open cells. `FRHDigInfo` gains `uint32 Atriums`.
10. **One step predicate (R2):** `IsWalkOrdinal(const FRHFloor&, int32 Ord)` means carved and, while atriums exist, not open. It is used by all three robot neighbour generators, the deep-net landing loop, and D6c's `BuildWalkFloor` (compartments, doors, landings).
11. **Robots on a cell that opens (R3, as amended here; see problem P2):** `Resolve` maps an open position to a non-open cell. No refusal is ever based on robot positions. The load repair treats an open ordinal as "not a cell".
12. **Home cells (section 4 + R8 + R9):** `DeriveDeepHomes` also covers every atrium floor. The legacy slots skip open cells (`Beds[i] = 0`, no slot, `k` does not advance). A floor that is **mapped** (`Geo.IsMapped`: a shape **or a tram line**) and also an atrium floor keeps its home cells in `HomeCell`, and they change. A floor that is an atrium floor and not mapped writes `AtriumHomeCell` (plus its bed list), **never** `HomeCell`.
13. **Hope:** `FRHHopeBreakdown` gains `double Atrium` and `int32 FacingHeads`, with `Atrium = HopeAtriumMax x FacingHeads / Pop`. It is the **last addend, after `Out.Shelter`**. Each head counts once (home or seat). Facing means: carved, non-open, on one of the atrium's floors, and inside or 4-adjacent to the box.
14. **Save:** a new `RHSaveFrame` frame at the end of the file. The read semantics are **option (i), repair with Warnings** (REFRESH F1, recommended there; see P1). It is not a mid-file block and not an `OutError` refusal.
15. **Inertness:** with no atrium, every battery line and the playtest are byte-identical except the save-version and file-size lines (section 6 below).
16. **Deviations to log** in `docs/design-decisions.md` (the REVISION's list of 7):
    1. a separate saved list keyed by cavern Id;
    2. facing is 4-adjacency, no line of sight;
    3. per-floor slices of the void, the premium billed up front;
    4. shaft-only floor change;
    5. a hand-made file with an occupied open cell loads with that atrium closed (Warning);
    6. convert-a-cavern, two steps;
    7. dig labels exclude open cells.

    Add 8: the save block is a frame that repairs (option (i)).

## 2. Changes per file (HEAD d842dd0 anchors)

### 2.1 Data and state (H)
- **New struct `FRHAtrium { int32 CavernId = 0; }`.** Put it near `FRHCavern` (H:406-423).
- **Subsystem members, beside `Caverns` / `CavernsVersion` (H:3391-3393):**
  - `TArray<FRHAtrium> Atriums` (ascending `CavernId`, at most one per cavern);
  - `uint32 AtriumsVersion = 0`;
  - the derived `mutable FRHAtriumCells AtriumCells` (draft section 1 layout: `bValid`, `Atriums`, `Cells`, `TMap<int32, TArray<uint8>> Flags`, `TMap<int32, int32> OpenCount`);
  - a facing cache: `FacingHeads` plus a key of `AtriumsVersion`, `FloorCellsVersion`, `ColonistSetVersion`, `FurnitureVersion`, `ZoningVersion`, `DistrictsVersion`, `BedAssignVersion`, `JobsVersion`, and a rebuild counter for AT11.
- **Const reads.** `IsCellOpen(L, Ord)`, `IsPointOpen(L, P)`, `GetFloorOpenCells(L)`, `GetAtriumFacingHeads()`. These four are D8's requirement too:
  - `bool IsCellFacing(int32 Level, int32 Ordinal) const` (bit 1);
  - `const TArray<FRHAtrium>& GetAtriums() const`;
  - `uint32 GetAtriumsVersion() const`;
  - the atrium-cells cache re-derived and compared under `-RHVerifyCaches`, with an `RHVERIFY` Error on a stale reuse.

  Also add `bool IsAtrium(int32 CavernId) const`, plus `CanOpenAtrium(CavernId, OutReason, OutPreview)` with a preview of `PremiumKg`, the extra leak a sol, the cells that would open, the facing ring, "N homes and seats would face it", and air headroom per floor.
- **Naming hazard.** `IsCavernOpen` (H:956) means "still digging". Do not reuse "Open" for cavern state. Keep atrium names explicitly `Atrium*` / `IsCellOpen`.
- **Hope breakdown** (H:1652-1690): add `double Atrium = 0; int32 FacingHeads = 0;` beside `Shelter` / `DeepResidents` (H:1663).
- **Rows:** `HopeAtriumMax`, `AtriumFillPermille`, `AtriumLeakPermille` as members beside `HopeShelterMax` (H:3349) and the O2 rows (H:3585-3588). Read them where W:255-256 reads the O2 rows.

### 2.2 Verbs and the uplink (W)
- **Dispatch.** Add `OpenAtrium` and `CloseAtrium` beside `DigCavern` / `CancelCavern` (W:9357, :9361). Both execute inside `StepUplink`, which sits at the same index in both spines (W:674 agent, W:846 era).
- **Execute (open).** Re-run `CanOpenAtrium`; on a refusal, broadcast `OnCommandRejected` with the reason. Then:
  1. bill the premium (2.5);
  2. insert into `Atriums` keeping it sorted;
  3. `++AtriumsVersion`;
  4. invalidate the atrium caches.

  Close: remove the entry and `++AtriumsVersion`.
- **Cheats (optional, for captures).** `RH.OpenAtrium <Id>` and `RH.CloseAtrium <Id>`, beside `RH.CancelCavern` (`Source/RedHope/Private/RHCheats.cpp:694-710`).

### 2.3 Cell consumers: an open cell is refused or skipped (each test written `Atriums.Num() == 0 || !IsCellOpen(...)`)
- `DesignateRoom` (W:5872): refuse "That cell is open to the atrium below". Planted cells follow rooms (W:4430-4437).
- `IsFurnishKeyValid` (W:11632): the same refusal.
- `CanPlaceBuildingSized` (W:8764): add the refusal in the **general `Level < 0` path** (W:8966-8972), not only in the module branch (W:8916-8938). Test every footprint cell: corners from `GetFootprintHalfCm`, through `CmToLattice(Shafts[0].HeadCm, ·)`, plus every lattice point in between. Reason: "That spot is open to the atrium below". A queued Build order is re-checked at execute, so it is refused with the same reason.
- `IsCellFreeForRoom` (G:399-406): gains `!IsCellOpen`, in the same pattern as the tunnel guard at G:405.
- **Footholds.**
  - `IsCavernPointEligible` (Cav:96-122): the neighbour test at Cav:106 needs a **non-open** carved neighbour. This one change also covers D6b growth, which calls it at G:781, G:1530 and G:1617.
  - `CanDigTunnel` (W:8049): `BesideCarved` (W:8082-8089) uses the non-open foothold for the A test (W:8096) and for `bTwoEnded = BesideCarved(B)` (W:8148). `Carved(P)` for "already dug" (W:8091, W:8110) stays as it is, because open cells are still carved.
- `GetCellBeds` (W:11945-11961): returns 0 for an open floor cell (`Key.Z == 0`).
- `CountFloorBeds` (W:11841-11900): subtract the floor's open cells at **both exits**:
  - the no-furniture early return (W:11846) becomes `(int32)((Carved - Open) * ColonistsPerCell)`;
  - the shared formula (W:11889) becomes `(int32)((Carved - Open - BedCells) * ColonistsPerCell) + Sum`.

  Do this only when the switch is on. With it off, keep the exact current expressions. The per-piece verify (W:11890-11898) calls the same function, so it agrees by construction. Refusal 6 needs a what-if count with hypothetical open cells; add an `ExtraOpen` parameter or a sibling helper that shares the formula (P9).

### 2.4 Dig labels and the robot walk
- **`EnsureDigInfo`** (W:5453-5516):
  - the key check (W:5456) gains `Info.Atriums == (Atriums.Num() ? AtriumsVersion : 0)`;
  - `FRHDigInfo` (H:3542-3548) gains `uint32 Atriums`;
  - the flood (W:5471-5498) skips open seeds and open neighbours while the switch is on;
  - `Touching` (W:5499-5514) skips open `O` and never indexes `Touching[INDEX_NONE]`.

  With no atrium the labelling is today's, line for line, so `DigLabelRuns` cannot move.
- **Every `DigOf` reader must handle INDEX_NONE.**
  - **Crash risk, must fix:** `ServingShaft` W:5544-5546 reads `Touching[DigOf[Ordinal]]`, which would be `Touching[-1]`.
  - `IsRouteReachable` T:597-601 and T:615 (the robot's own dig; an open own cell maps as `Resolve` does);
  - the landing `Port.Dig` T:995;
  - `Resolve` T:1128;
  - the load repair T:1994;
  - the infirmary patient test W:13115.

  In the self-review, grep `DigOf[` and `DigOf.IsValidIndex` over `Source/RedHopeSim` and list each site with its guard.
- **`IsWalkOrdinal` everywhere a robot steps:**
  - `BuildLatticeNeighbours` (T:667-701): after `Index.Find` (T:685-688), drop a non-walk ordinal. The diagonal rule (T:691-700) then sees it as absent;
  - `BuildLatticeField`'s inline path (T:735-741);
  - `LatticeAStar` (T:802-812, plus its From/To);
  - the `EnsureDeepNet` landing loop (T:976-983): the landing is the lowest-ordinal **walkable** orthogonal neighbour.

  `RunLatticeField` (T:179) uses the neighbour table, so it is covered.
- **Key fields.** `FRHDeepNet` (H:3502-3518, key fields H:3505) gains `uint32 Atriums`, with the check at T:927 and the stamp at T:939-945. **Also `FRHReserveMemo` (H:3529, check T:1582-1583, stamp T:1600-1605)**, which mirrors the deep-net key and would otherwise reuse a pre-open reserve for up to a sol (P4).
- **`Resolve`** (T:1103-1129): when `FindCarvedCellAt` returns an open ordinal, replace it with the **nearest non-open carved cell on that floor**, found by a 4-step BFS through open cells from the robot's cell (distance, then lowest ordinal). On a rim cell this is exactly R3's "lowest-ordinal non-open 4-neighbour". `Out.Dig` is that cell's label. If none exists (an all-void dig), return false: `++DeepOffLattice` at T:1134 is the existing behaviour. See P2 for why R3's wording is not enough.
- **`RepairLoadedRobots`** (T:1958-2006): at T:1990-1998 an open `Ord` is treated as `INDEX_NONE`, and the robot falls to the surface deploy ring (T:2000-2004). It runs at W:14224, so the atrium list must be installed before that line (2.10).
- **Inert check:** `DeepNetBuilds` and the D4 perf digests cannot move with no atrium.

### 2.5 Air (W, H)
- **`GetFloorO2RequiredKg`** (H:1187, inline today): `GetFloorCarvedCells(L) * O2FillKgPerCell`, and **only if** `O_L > 0`, `+= O_L * O2FillKgPerCell * (AtriumFillPermille - 1000) / 1000`.
- **New `GetFloorO2LeakKgPerSol(L)`:** `Cells * O2LeakKgPerCellPerSol`, and only if `O_L > 0`, `+ O_L * O2LeakKgPerCellPerSol * (AtriumLeakPermille - 1000) / 1000`.
- **Callers to switch, keeping each expression bit-identical when `O_L == 0`:**
  - `StepHabitability` W:2974 (`RequiredKg`) and W:2979. The leak must stay `(Cells * Leak) * (SubDt / Sol)`: the helper returns `Cells * Leak`, and the existing `* (SubDt / (double)SolLengthSimSeconds)` multiplies it;
  - `Debug_ForceRateFloor` W:3787;
  - `IsFloorSealedButSmall` W:3704 (already through the helper);
  - `GetStandingO2LeakKgPerSol` W:7323, which feeds the growth reserve at W:7343 and W:7378.
- **The up-front bill at execute:** `GrowthBillFits` (G:239) with `{NAME_GrowthOxygen: PremiumKg}` is refusal 8. Then `TakeGrowthResource(NAME_GrowthOxygen, PremiumKg)` (G:261; for Oxygen this reduces to `AddStock(-Kg)`, G:301-304), then `FloorO2Kg.FindOrAdd(L) += O_L * O2FillKgPerCell * (AtriumFillPermille - 1000) / 1000` for each upper floor. `PremiumKg` must equal the sum of those shares. No `NoteFlow`: like growth's air (G:1465, G:518), oxygen put into a floor's air is ledgered as Used only when it leaks (W:2981, W:3063). UNCERTAIN: E5 and E6 may change the growth bill or availability rule; re-read `GrowthBillFits` at build time.
- **Card:** "air headroom: N cells" per atrium floor, which is `(480 / O2LeakKgPerCellPerSol) - (Cells + O_L x (Leak-1000)/1000)` at the rows.

### 2.6 Home cells, membership and the commute (Dist, C, DW)
- **`DeriveDeepHomes`** (Dist:186-268):
  - the early return (Dist:197-200) becomes "no mapped floor **and** no atrium";
  - the level list (Dist:205-206) is the ascending union of `GetMappedLevels` and the upper and hall floors of every atrium;
  - the resident filter (Dist:210) admits atrium floors only while the switch is on;
  - the slot walk (Dist:225-241) skips open cells (`Beds[i]=0`, no slot, `K` not advanced);
  - write `HomeCell` (Dist:260, :264) for mapped floors and a new `AtriumHomeCell` (plus an `AtriumCellBeds`) for unmapped atrium floors.

  The `EnsureBedAssignments()` call at Dist:203 is reached in an atrium colony too. That is fine, since it is behind the switch.
- **Key.** `FRHDeepHomes` (H:3185-3192) gains `uint32 Atriums`. Update **both** key copies: `EnsureDeepHomes` (Dist:273-274) and `IsDeepHomesFresh` (Dist:291-292). `IsDeepHomesFresh` (Dist:287-311) also compares `AtriumHomeCell` and its bed list.
- **`MakeDistrictMembershipKey`** (Dist:133-150; struct H:3160-3173): add `uint32 Atriums`, set while a deep district (`DeepFloor.Num() > 0`, Dist:142) **and** an atrium exist, and add it to `operator==`.
- **`MakeCommuteKey`** (C:50-76; struct H:2948-2962): inside the `AnyMapped()` block (C:66-74), add `K.DeepAtriums = Atriums.Num() ? AtriumsVersion : 0`, and add the field to `operator==`.
- **The walk network's own key.** `FRHDeepWalk` (H:3090-3093) gains `uint32 Atriums`. Update `DeriveDeepWalk` (DW:332-348), `EnsureDeepWalk` (DW:353) **and** `IsDeepWalkFresh`'s copy (DW:372). The REFRESH says `IsDeepWalkFresh` "needs no change"; its key copy does (P8).
- **`BuildWalkFloor`** (DW:154-330): an open cell is not in any compartment (the flood at DW:204-226 skips it), is never a door (DW:230-246) and never a landing (DW:253-261). Guard `WalkCm` (DW:439-484): its `Doors` lambda treats `CompOf < 0` as a chain cell and reads `NodeOf[Cell]`, so an open cell must return `MAX_int64` first.
- **The commute's district-row reader** (C:376) keeps reading `HomeCell` only, which is why `AtriumHomeCell` exists. The D6c deep-trip anchor (C:441) likewise.

### 2.7 Hope (W)
- **`GetColonyHope`** (W:6451-6943): after the Shelter block (W:6910-6916):
  ```
  if (Pop > 0 && Atriums.Num() > 0) { Out.FacingHeads = GetAtriumFacingHeads(); Out.Atrium = HopeAtriumMax * (double)Out.FacingHeads / (double)Pop; }
  ```
  The Total chain (W:6939-6941) gains `+ Out.Atrium` straight after `+ Out.Shelter`.
- **`GetAtriumFacingHeads`** is one pass over `Colonists` in array order. It calls `EnsureBedAssignments()` **before** building its key (step-2 fix 2 rule). A head faces when either:
  - (home) `HomeHab == 0`, `HomeLevel < 0`, and the home cell (`HomeCell` or `AtriumHomeCell`) faces; or
  - (seat) the station in `ColonistStations` is `(L,i,0)` with cell i facing, or `(L, INDEX_NONE, BuildingId)` with `FindCarvedCellAt(L, B.LocationCm)` facing.

  `-RHVerifyCaches` re-derives it on reuse and logs an Error if it is stale.

### 2.8 Presentation, floor view (RedHope; const accessors only)
- **The void.**
  - In `ApplyViewLevel` (CV:4520), the carve tiles of open cells on an upper atrium floor (tile pass CV:4584-4591) are hidden, and their lamps (`LightByCell`, CV:4009-4026) with them.
  - The building pass (CV:4534-4550), the tiles, the room, prop and furnishing passes (`RefreshRoomVisuals` CV:3837, `RefreshFurniture` CV:2082) and the robot layer (CV:4576-4580) un-hide the floors of the box below the view, down to the hall floor, using D8's rule: `ShowsLevel(L) || (!bSectionMode && InAtriumBelowView(L, P))`. `ShowsLevel` is at `Source/RedHope/Public/RHColonyVisualizerSubsystem.h:236`.
  - Add a darkening step per floor down.
- **The railing.** A warm off-white outline around the void on each upper floor, in the cavern visualizer (`RHCavernVisualizerSubsystem.cpp`, `Rebuild` :205). Its key (:321-323) gains `GetAtriumsVersion()`.
- **Light.** One tall light per atrium at the top floor's ceiling, visible from every atrium floor, and **hidden while `bSectionMode`** (D8 R7).
- **Cutaway (R11).** The pit rig already treats every carved cell (open ones included) as open air (CV:4815-4835), so there is no ring-to-void wall today. The rock to draw is the box sides **below** the view floor down to the hall. UNCERTAIN; judge it from the captures (P13).
- **Crew wander.** `RHCrewVisualizerSubsystem.cpp:1262` picks any carved cell and must skip open cells.
- **The card.** The deck's cavern list (`Source/RedHope/Private/RHCommandDeck.cpp:2604-2700`) lists **only** caverns still being dug in the floor view (filter :2637). Finished caverns appear only in the section. Widen the filter, so a finished, non-cancelled cavern of 2 or more floors on the shown level shows **"Open as atrium"** and an atrium shows **"Close the atrium"**, then add `GetAtriumsVersion()` to the list key (:2617-2622) (P5).
  - The ghost tints the cells that would open, and the facing ring in soft gold.
  - The tip quotes `PremiumKg`, the extra leak a sol, and "N homes and seats would face it".
  - A refusal shows its reason. The card also shows the air headroom per floor.
- **City panel.** Beside the Shelter line (`Source/RedHope/Private/RHCommandDeckPanels.cpp:1084-1087`) add "Atriums: N, facing M of P people". Facing homes and seats glow softly in the overlay.
- **Copy.** All new strings are Gate-D placeholders, prevention-framed, in `docs/qa/2026-10-15/gate-d-wording.md` under the fresh prefix **`L`**. It is free at d842dd0 (D8 took `M` and reserved `L` for D7).

### 2.9 Presentation, the section view (D8 + P8c), exactly what must change
REFRESH F2 plus D8 `:100`, `:377` and `:503-510`. The P8c ladder (`RHSectionLadder.*`, `FloorAnchorCm` SV:1240-1244) anchors at the carved extent's left edge, so it needs **no change**.
1. **Rebuild key** (`Tick`, SV:687-700): mix `Sim->GetAtriumsVersion()` into `Struct` **only while `GetAtriums().Num() > 0`**, so the key value, `RH.DeepBench` and the D8 digests are untouched in a colony without one.
2. **`BuildFloor`** (SV:890-1066), `EmitCell` (SV:973-1053): for an open cell, emit **no plate** (SV:1005), no cut-face band (SV:1010), no walls, no furniture block and no pips. The `IsOpen` lambda (SV:907-910, "not rock") stays as it is, so no wall is drawn between the ring and the void. The band-2 simplified slab (SV:913-933) is one bounding box: leave the box spanning the void, or exclude open cells from `Span`. UNCERTAIN; decide by capture, since far floors are LOD only.
3. **The atrium layers** (D8 `:100`):
   - a railing line on each upper floor's ring;
   - one unlit additive light column box per atrium, from the hall floor to the top floor's ceiling (the only translucency; R3's fallback is an emissive outline);
   - a faint warm edge on facing cells (`IsCellFacing`).
4. **Pips on unmapped floors** (SV:944-961): the carve-order fallback (SV:956-959) can put a pip on an open cell. On an atrium floor, use the atrium home cell (expose a const `GetColonistAtriumHomeCell`, or have `GetColonistHomeCell` return it) or skip open cells.
5. **`LocatePlate`** (SV:626-650) must skip open cells. The click test's `LocateSectionPlate` uses it, at `RHClickTest.cpp:712` and `:2411`.
6. **`Pick`** (SV:579-625) must not return an open cell; treat it as not carved and keep scanning rows.
7. **Cut here on the atrium card** (the D8 cavern list "Cut here" at `RHCommandDeck.cpp:2652-2663`, `PC->CutSectionAtCavern`) shows on an atrium's row.
8. **`RH.DeepBench`** (`RHCheats.cpp:1748-`) opens its 8 x 8 x 3 cavern as an atrium (D8 R8: "and one atrium once D7 is built"). Record the before and after bench numbers. They will move.

### 2.10 Save (W, `RHSaveFrame.h`)
- **Version.** Take the next free version at build time. The forecast is **v59** (E5 v56, E6 v57, D6e v58). If D6e has not built, it is v58. Add an `RHSaveVersion` comment entry at W:37: "vNN: atriums - an atrium block in its own frame ('RHD7') after the last frame then written: the count, then each CavernId ascending (D7)". The strict check (W:13968) stays.
- **Tag.** Add `constexpr uint32 AtriumTag = MakeTag('R','H','D','7');` beside `MarketTag` in `Source/RedHopeSim/Public/RHSaveFrame.h`. The spec names no tag; this follows 'RHE1'-'RHE5', 'RHL6' and 'RHE8' with no clash.
- **Payload.** `int32 Count`, then `Count x int32 CavernId` ascending. An empty colony writes a 4-byte payload, so the frame is **24 bytes** (12 header + 4 + 8 footer).
- **Write.** `RHSaveFrame::Write(Ar, AtriumTag, ...)` after the last frame written at build time. At d842dd0 that is `MarketTag`, W:13933; at build time it is E5's, E6's `'RHL6'` or D6e's.
- **Read (option (i), placement matters).** Read the frame **after teardown but before `RepairLoadedRobots` (W:14224)**: right after `InstallWarmth` (W:14189), when `Caverns` (W:14174) and `FloorTable` (W:14167) are installed. Use a non-moving read: `const int64 At = Ar.Tell(); RHSaveFrame::Read(Ar, Bytes, AtriumTag, Payload, Why); Ar.Seek(At);`. `Read` falls back to the walk from the end and moves `Ar` past the frame, and the main body still has to be read from `At`. You may instead add a small `RHSaveFrame::Peek(File, Tag, OutPayload, OutWhy)` helper.
- **Structural failures drop with a Warning, never an Error and never a refusal:**
  - a lost frame (no atriums);
  - a count < 0, > `RH::MaxCavernRecords`, or past the payload (all dropped);
  - Ids not strictly ascending, an Id naming no cavern, a cancelled, unfinished or one-floor cavern (that atrium dropped);
  - an R7 overlap (the higher Id dropped).
- **Occupancy repair** after `RefreshJobs` (W:14756), when rooms, furniture, buildings, stations, tunnels and growth are loaded: an atrium with an occupied open cell is dropped with a Warning ("atrium over cavern 3 closed at load: floor -2 cell 17 is zoned"). Then `++AtriumsVersion` again and reset the atrium caches.
- **Teardown.** Reset `Atriums` at teardown (W:14027-). Bump `AtriumsVersion` at load only when the list was or becomes non-empty (the `TramVersion` pattern, W:14177-14187), so nothing moves in a colony without one.
- `FloorO2Kg` is already saved, so the premium fill survives a round trip.

### 2.11 Tests: new suite `-atriums`
- **Wiring.**
  - New `Source/RedHopeSim/Private/RHSimAtriumSuite.cpp` with `int32 RHRunAtriumsSuite(UWorld*, URHSimClockSubsystem*, URHSimWorldSubsystem*)`;
  - declare it beside `RHRunTramSuite` (`RHSimCommandlet.cpp:26`) and dispatch `-atriums` after the last suite (after `-market` at :14826-, or after E5, E6 and D6e's);
  - append `atriums` to `SUITES` in `scripts/sim/rh_battery.sh:38-39`, and update the header comment (":2 the baseline plus all 57 suites (58 runs)") and the `=== 57 suites ===` echo to the counts at build time.
- **Fixture rule.** At most 60 colonists (ramp 0) unless a case says otherwise. Every fixture that fits a lift car pins the bank to 1e9 before each step (D6 RC 9).
- **Expected values the spec gives:**
  - **AT0:** with no atrium, `Atrium == +0.0`, `O_L == 0`, the caches unbuilt and every new key field 0.
  - **AT1:** every refusal 1-10 by its reason. Refusal 5 by each kind: room, furniture, module, **circulator / non-module floor building**, station, tunnel chain cell, growth target. Refusal 6, refusal 7 (split; lost landing), refusal 8, refusal 9, refusal 10. An all-void dig is accepted.
  - **AT2:** open a 3 x 3 x 3. Every ordinal on every floor maps to the same point, `Cells.Num()` per floor is unchanged, and `FloorCellFallbacks == 0`. The same after close, after save and load, and after carving more.
  - **AT3:** **18** open cells (2 upper floors x 9). The hall's 9 box cells are not open. `IsCellOpen` and `IsPointOpen` agree.
  - **AT4:**
    - zone, furnish, module and circulator placement are refused, and a queued Build order over the void is refused at execute;
    - growth (`DeepCells` and civic inside a shape over the box) never picks an open cell;
    - `CountFloorBeds` matches a hand integer at CPC 1 and 0.5, with and without furniture, at **both exits**;
    - the `DeriveDeepHomes` slot total equals `CountFloorBeds`;
    - a queued Zone order on an open cell is refused at execute;
    - `HomeCell` equals the no-atrium twin's **only on floors with no open cell**. On a mapped atrium floor, check it against a hand derivation that skips open ordinals. Include one floor mapped by a **tram line** and no shape.
  - **AT5:** a 4 x 4 x 2 box beside a dig. The ring of the upper floor faces, the hall and its ring face, a cell two steps out does not, and a cell carved beside the ring after the open faces at once.
  - **AT6:** 40 residents; 10 face by home only, 4 by seat only, 3 by both. So `FacingHeads == 17` and `Atrium == 3 x 17 / 40 = 1.275` exactly. A head facing two atriums counts once. Total equals the no-atrium twin plus `Atrium`, to the bit. Shelter on a -6 atrium stacks (DP8's numbers plus this). Views is unchanged.
  - **AT7:**
    - hand `RequiredKg` and leak values on each atrium floor and the hall floor. For 3 x 3 x 3: premium **900 kg** (18 x 100 x 0.5) and an extra leak of 36 kg a sol. For 8 x 8 x 3: **6.4 t** and 256 kg a sol;
    - an `O_L == 0` floor in the same colony is bit-identical to the twin;
    - a **200-cell** upper floor of an 8 x 8 x 2 (264 against 240 cell-equivalents) is refused by refusal 9, and the reason says **"24 fewer"**;
    - a **170-cell** floor (234) is accepted and stays rated for 20 sols in both bands. Stock at least about 468 kg a sol x 20 of O2 plus the colonists' draw;
    - Oxygen below the bill plus the breathing reserve is refused by refusal 8;
    - the Oxygen stock falls by exactly `PremiumKg` at the open.
  - **AT8:**
    - a robot crossing an upper floor goes round the void (it never visits an open ordinal);
    - a same-dig **direct leg** between ring cells on opposite sides costs exactly the hand-counted walk around;
    - a landing in the box is replaced by the lowest non-open neighbour;
    - a robot parked on an upper-floor box cell, **rim and interior**, reaches its next site;
    - a plan point carved after the open, beside open cells only, is its own dig with no landing, and `IsRouteReachable` to it is false;
    - a commute across the ring uses the rim compartment and never anchors in the void.
  - **AT9:**
    - a round trip equals a twin that never saved (Hope, Required, FacingHeads, the flags, `FloorO2Kg`);
    - **each structural failure drops with one Warning and no Error** (option (i));
    - the occupancy repair drops the atrium with a Warning;
    - the previous version's file is refused ("Save format mismatch", "/NN");
    - a lost frame loads with no atriums;
    - doctored copies are resealed with `RHSaveFrame::Reseal`;
    - the overlap load case.
  - **AT10:** an era and an agent twin open the same atrium at the same uplink. Right after the open, each floor's `FloorO2Kg` equals its pre-open value plus its premium share, **to the bit, in both bands**. Over 10 sols the fill agrees within `O2FillRateKgPerHour x 60/50` and Hope within 5%.
  - **AT11:** `-RHVerifyCaches` is clean. The facing count rebuilds 0 times over 1,000 unchanged steps.
  - **AT12:** close. The cells are ordinary again (zonable, beds back, walkable), Hope drops to the twin's, and the premium falls away.
  - **AT13:** a Homes shape and an atrium on one floor. `-RHVerifyCaches` is clean after the open and after the close, and the shape's resident count matches a fresh derivation each time.
- **Mutations** (run by hand; each must fail its case):
  - M1 the bed count ignores open cells -> AT4;
  - M2 `IsCellFreeForRoom` ignores open cells -> AT4;
  - M3 a head counted once per role or per atrium -> AT6;
  - **M4a** `BuildLatticeNeighbours` ignores open cells -> AT8;
  - **M4b** `LatticeAStar` ignores open cells -> AT8 (the direct leg);
  - M5 the premium on every cell of an atrium floor, or on the hall floor -> AT7;
  - M6 the facing key drops `FloorCellsVersion` -> AT5 and AT11;
  - M7 atrium home cells written into `HomeCell` -> AT4's twin check on no-open floors;
  - M8 the occupancy repair as a structural refusal -> AT9 (the file must load).

  Recommended, not in the spec: drop `Atriums` from the membership key -> AT13, and from the deep-walk key -> AT13 / DC's verify.
- **`-scale`** (outside the battery; the block runs last): the 20-floor colony, four 8 x 8 x 3 atriums (one per shaft), 5,000 colonists. Budgets:
  - cold flags at most 1 ms;
  - cold `FacingHeads` at most 2 ms;
  - a warm Hope read unchanged;
  - a cold deep net with atriums within 3 ms;
  - a cold people walk within 5 ms;
  - 2,000 accepted preflights at most 100 ms. Measure this one. If it is over, cache each floor's bed count per (`FurnitureVersion`, `ZoningVersion`, `AtriumsVersion`); **a missed budget is a director finding, not a budget edit.**

  The five- and twenty-floor digests before the atrium block must be unchanged.

### 2.12 Click test and captures
- **Steps.**
  - `atrium.open` and `atrium.close` (the card buttons in the floor view);
  - **`section.cut.atrium`** (the placeholder comment at `Source/RedHope/Private/RHClickTest.cpp:2666`): Cut here on the atrium, then count **no plate on its open cells**. REFRESH F2 calls this `section.atrium`; use D8's name.

  Run `RH_CLICKTEST_RES=1440x860 bash scripts/unreal/rh_clicktest.sh` and pass it 3 times.
- **Captures:** the ghost; an atrium from its top floor looking down; the hall floor; the City panel line; D8's capture 4 (Cut here through the atrium: the void and its light column). Look at them.

## 3. Battery lines expected to move (every other line identical)
Diff against the battery baseline the gate before D7 leaves (its `Saved/baselines/battery_post_<gate>`; at d842dd0 the newest is `battery_post_s2fix`, the visual pass will leave its own). The command is `python3 scripts/sim/diff_battery.py <baseline> Saved/RHBattery`.
1. **The new `atriums.log`.**
2. **The save-version lines,** recounted literally at build time. At d842dd0 they carry 55, and E5, E6 and D6e move them first. They include CV8 ("the file says version", "a v47 file is refused ... /55"), DP11, K6, DD9, CR9, EC (the frame check), the export and market frame checks, the tram TM and transit save checks, X15, and the 12147 and 15128 checks in `RHSimCommandlet.cpp` (literals `Ver == 55` at `RHSimCavernSuite.cpp:738`, `RHSimDepthSuite.cpp:786`, `RHSimCreditSuite.cpp:609`, `RHSimDeepDistrictSuite.cpp:771`, `RHSimEconomySuite.cpp:857`, `RHSimExportSuite.cpp:530`, `RHSimMarketSuite.cpp:1316`, `RHSimTramSuite.cpp:683`, `RHSimTransitSuite.cpp:1596`, `RHSimCommandlet.cpp:10867, 12147, 12563, 14103, 15128`). Update each literal to the new version.
3. **The whole-file size lines,** each +24 bytes for an empty atrium frame. These are not masked, because they are not of the form `(N bytes`:
   - CV8 ("12059 -> 12139 bytes");
   - DP11 ("12087 -> 12119 bytes");
   - EC1 x2 ("both saves are 13100 bytes");
   - DC9 ("the 13408-byte file");
   - MX1's file size (`RHSimMarketSuite.cpp:565`) and MX9's round trip (`:1359`);
   - plus any size line E5, E6 or D6e add (E8 MX14 when it exists).
4. **Nothing else.** In particular: the two pins (`power: gen 50 W load 20 W`, `deposit Regolith_A: 40645 kg`), `DigLabelRuns`, `DeepNetBuilds`, `DeepWalkBuilds`, the C5 cache rebuild counts, `-RHCommuteAudit` margins, and every Hope and air digest. Timing digits are tolerated.

Also check that no check pins a frame as "last" in a way that the new frame breaks. E5's REFRESH bounds MX1 to the market frame; re-check E5's, E6's and D6e's own frame checks.

## 4. Playtest expectation
**Byte-identical.** No bot, suite or playtest sends `OpenAtrium`, every branch is behind the switch, and the playtest log prints no save byte counts (0 "bytes" strings in `Saved/baselines/ind_full_playtest.txt`). Compare `RH_PLAYTEST_DISTRICTS=1 bash scripts/sim/rh_playtest.sh 110 d7_full guide full` with `cmp` against the playtest baseline the gate before D7 leaves (`Saved/baselines/ind_full_playtest.txt` at d842dd0). UNCERTAIN: E5, the currency, may intentionally replace it; use the newest one the orchestrator names. Also record the **default** (districts off) run at your own HEAD **before** changing anything, and compare after; section 7 asks for it and no baseline exists.

## 5. Other gate checks
- The RedHopeSim dylib **does** change (sim gate).
- `-scale` digests unchanged before the atrium block.
- `RH.DeepBench` before and after (it now opens an atrium).
- Checkpoints: `bash scripts/unreal/rh_checkpoints.sh all --force` if the save version moves, then confirm both load.
- Log the deviations (section 1, item 16) in `docs/design-decisions.md`, the wording in `gate-d-wording.md` (`L`), and one commit per gate.

## 6. Deviations the builder must NOT take
- Do not remove, reorder or "compact" cells, and do not store the open flag in `FRHFloor` or the floor block. Do not change the cavern block (CV8's 40-byte record).
- Do not put the atrium block mid-file or read it before teardown with `OutError` refusals. Option (i) applies unless the orchestrator says otherwise (P1).
- Do not add a premium term of 0 when `O_L == 0`. Skip the branch, so the expressions stay bit-identical.
- Do not let `Resolve`'s fallback be "off-lattice" for robots in a void's interior (P2).
- Do not refuse based on robot positions (era parity).
- Do not write atrium-only floors' home cells into `HomeCell`, and do not make an atrium floor "mapped".
- Do not add stairs or any cross-floor walk edge (Q-B (a)).
- Do not move the Hope term away from last addend after Shelter, and do not sum it per floor or per atrium.
- Do not re-bill or refund the premium on carve or close.
- Do not edit a budget when `-scale` misses; report it.
- Do not mix `AtriumsVersion` into any presentation key unconditionally (the section's `StructKey`, the cavern visualizer, the deck list). Mix it only while an atrium exists, or as a 0 that leaves the hash's cadence unchanged. Keep the D8 digests still.
- Do not touch the P8c ladder.

## 7. UNCERTAIN, with evidence
- **The version number and the battery count** depend on E5, E6 and D6e landing first (orchestrator handoff 2026-10-25, queue items 1-4).
- **The band-2 simplified section slab across a void** (SV:913-933): spec silent; decide by capture.
- **The cutaway geometry** (CV:4815-4880): R11's "drop the pit wall faces around the void" is already true, because open cells count as open air. Whether the hall's lower box sides need faces is a capture call.
- **The playtest baseline file name** after E5.
- **Whether E5 or E6 change `GrowthBillFits` or `TakeGrowthResource`** (E6's "levers" rule list).

## 8. Spec problems (referenced above as Pn)
- **P1 (blocking, orchestrator; not the director).** REFRESH F1 is still open. Section 2 designs a mid-file block, read before teardown, that refuses. The house rule since v54 is a tail frame that repairs. The REFRESH recommends (i) but asks the main session to confirm, and no confirmation is logged. A second trap: `RepairLoadedRobots` runs at W:14224, well before the frame reads at W:14740-14743. If the atrium frame is read with the other frames, R3's load repair sees no atriums and keeps robots that stand on open cells. **Resolution:** take (i), with the non-moving early read in 2.10.
- **P2 (blocking, no director).** R3: "lowest-ordinal non-open 4-neighbour in the same walk component ... if none, the existing surface-ring fallback".
  - Under R1 an open cell has no walk label, so "same walk component" is vacuous.
  - The surface-ring fallback exists only in the load repair (T:2000-2004). In play, `PlanDeepRoute` returns false and counts `DeepOffLattice` (T:1134).
  - In an 8 x 8 void, the 36 interior cells of each upper floor have no non-open 4-neighbour, so a robot walking there at the open is stranded for good. AT8's "parked robot reaches its next site" would fail for it, and the lifts=1 playtest fails on `DeepOffLattice != 0` (`RHPlaytest.cpp:1041`).

  **Resolution:** a 4-step BFS through open cells to the nearest non-open cell (distance, then lowest ordinal). On a rim cell this equals R3's rule.
- **P3 (blocking, no director).** D8's "Required by D8 in D7's build" (d8 doc :503-510, :377, :100) is in neither D7's REVISION nor its REFRESH. Missing from both: `IsCellFacing`, `GetAtriums`, `GetAtriumsVersion`, the `-RHVerifyCaches` compare of the atrium cells, the un-hide rule through `ShowsLevel`/`bSectionMode`, the light hidden in the section, the section's void, railing, light column and facing edge, Cut here on the atrium row, capture 4, `section.cut.atrium`, and `RH.DeepBench`'s atrium. The REFRESH's F2 covers the plate and the key only. Folded into 2.1, 2.8, 2.9 and 2.12.
- **P4 (should fix).** `FRHReserveMemo` (H:3529, T:1582-1583) copies the deep-net key. Without an `Atriums` field, a robot's charge reserve is priced on the pre-open route for up to a sol. Not in the spec.
- **P5 (minor).** The deck's cavern list hides finished caverns in the floor view (`RHCommandDeck.cpp:2637`), so "Open as atrium on its card" has no card to sit on. Widen the filter and key it on `AtriumsVersion`.
- **P6 (minor).** These are superseded by R1 and R4, and the builder must not follow them:
  - the draft's section 1 table row "Dig labels unchanged";
  - section 6's "dig labels stay a true statement ... later carving keeps it";
  - section 5's "no up-front bill".
- **P7 (minor).** Section 7's "Save: a 4-byte block, hidden by the mask" and "only the save-version lines" are stale. It is a 24-byte frame, and the CV8, DP11, EC1 x2, DC9, MX1 and MX9 size lines move.
- **P8 (minor).** The REFRESH says `IsDeepWalkFresh` "needs no change", but it duplicates the key (DW:372). `IsDeepHomesFresh` likewise (Dist:291-292). Both copies need the new field.
- **P9 (minor).** Refusal 6 needs a what-if `CountFloorBeds` with the box cells counted open, and no API is named.
- **P10 (minor).** Refusal 9's "open N fewer cells" equals the cell-equivalent excess only at 2000 permille. In general, N = ceil(excess x 1000 / (AtriumLeakPermille - 1000)).
- **P11 (minor).** The REFRESH anchor table misattributes several bare anchors to W:
  - `W:200` -> Dist:210 (the resident filter);
  - `W:251` -> G:261 (`TakeGrowthResource`);
  - `W:371-378` -> Dist:378-389;
  - `W:401` -> Dist:411;
  - `W:1145-1151` -> T:1162-1168;
  - `W:1442` -> G:1465;
  - `W:1973-1987` -> T:1985-2004;
  - `W:4500-4516` -> CV:4534-4550;
  - `W:4542-4545` -> CV:4576-4580;
  - `W:4549-4556` -> CV:4584-4591.
- **P12 (minor).** Click-step name: REFRESH F2 says `section.atrium`, D8 and the code placeholder say `section.cut.atrium`. Use the latter.
- **P13 (minor, UNCERTAIN).** R11's cutaway is already the behaviour (2.8).
- **P14 (minor).** The `WalkCm` `Doors` lambda (DW:456-460) would read `NodeOf[INDEX_NONE]` for an open cell. Guard it.

## 9. Anchors that moved since the REFRESH (31ffa6f -> d842dd0)
- **H:**
  - `.h:378-386` -> 384-392;
  - `.h:400-417` -> 406-423 (`FRHCavern`);
  - `.h:412` -> 418;
  - `.h:1173` -> 1187 (`GetFloorO2RequiredKg`);
  - `.h:288-307` -> 289-308 (`FRHFloor`);
  - `.h:3568-3570` -> 3585-3587 (the O2 rows).
- **C:**
  - `C:957-958` -> 958-959;
  - `C:65-73` -> 66-74 (`MakeCommuteKey` `AnyMapped` block);
  - `C:336` -> 337;
  - `C:375` -> 376.
- **Dist:**
  - `Dist:142-147` -> 142-148;
  - `Dist:185-260` -> 186-268;
  - `Dist:229` -> 234;
  - `Dist:262-280` -> 267-285;
  - `Dist:282-306` -> 287-311;
  - `Dist:308-317` -> 313-322;
  - `Dist:371-385` -> 376-390;
  - early return 196-199 -> 197-200;
  - resident filter 205 -> 210.
- **W:**
  - `W:8899-8905` -> 8966-8972;
  - `W:5866` -> 5872;
  - `W:6235-6239` -> 6241-6245;
  - `W:6350` -> 6381;
  - `W:6420-6912` -> 6451-6943;
  - `W:6879-6885` -> 6910-6916;
  - `W:6908-6910` -> 6939-6941;
  - `W:7287-7302` -> 7318-7333;
  - `W:7292` -> 7323;
  - `W:7404` -> 7435;
  - `W:7447` -> 7478;
  - `W:7475` -> 7506;
  - `W:7489` -> 7520;
  - `W:7673-7679` -> 7704-7710;
  - `W:8072-8076` -> 8110-8114;
  - `W:8110` -> 8148;
  - `W:8190` -> 8228;
  - `W:8804` -> 8871;
  - `W:8849-8860` -> 8916-8927;
  - `W:9290` -> 9357;
  - `W:11352-11367` -> 11419-11434;
  - `W:11565` -> 11632;
  - `W:11774-11823` -> 11841-11890;
  - `W:11779` -> 11846;
  - `W:11790-11798` -> 11857-11865;
  - `W:11822` -> 11889;
  - `W:11823-11830` -> 11890-11897;
  - `W:11878-11893` -> 11945-11960;
  - `W:13047` -> 13114;
  - `W:13910-13952` -> 13977-14019;
  - `W:13960` -> 14027;
  - the last frame write `'RHE4'` at `W:13866` (as the REFRESH and D6e/E5 docs say) -> **W:13933**;
  - the tail reads -> W:14740-14743.
- **Cav:**
  - the carved-point skip `:356-359` (6b5653c) -> 380-383;
  - the live-cavern overlap `:367` -> 391-395;
  - the cancel of a finished cavern `:478-482` -> 510-514.
- **SV (D8, rewritten by P8c 870ba2c):**
  - `BuildFloor` :849 -> 890;
  - plate :889 -> 928-930 (band-2 slab);
  - :965 -> 1005 (`EmitCell`);
  - `StructKey` -> 687-700.
- **Unchanged since the REFRESH:**
  - everything in T, G (except as noted), DW, CV and Crew :1262;
  - the cavern visualizer :205;
  - W:2961-3067, W:3030-3033, W:5315-5320, W:5345-5397, W:5453-5516, W:5543, W:674-682, W:846-862, W:3787, W:4047, W:4430-4437.
