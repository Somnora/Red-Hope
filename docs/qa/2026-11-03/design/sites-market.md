# p2 design, key `sites-market`: S1 the Solar Array's cut hold, S2 RH.Market's synced region bound

Read-only design at HEAD `6df7fcc` (working tree clean under `Source/` and `docs/`). Every line number below is at that HEAD.
Both items are display or developer-facing only: nothing under `Source/RedHopeSim`, `docs/data` or `Content/RedHope/Data` changes, no
player-facing string is added, no random draw, no per-frame work.

| Item | Recommendation | Files | Click steps added |
|---|---|---|---|
| S1 per-model cut hold (Solar Array, plus Battery Bank, found by the model sweep) and `RH.SitePose` for its capture | **GO** | `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp`, `Source/RedHope/Public/RHColonyVisualizerSubsystem.h`, `Source/RedHope/Private/RHCheats.cpp` | 0 |
| S2 RH.Market reads its region count after a syncing getter | **GO** (the log proves no regression only; see S2.2) | `Source/RedHope/Private/RHCheats.cpp` | 0 |

---

## S1. Construction sites: the per-model cut hold

### S1.1 The code today

The cut is written in `URHColonyVisualizerSubsystem::RefreshSites` (`RHColonyVisualizerSubsystem.cpp:2983`). It runs on the
one-second pass poll, or sooner when `bPassesDirty` is set (`:3790-3797`). It writes a MID only when the 5 % step moves:

```cpp
3045		const double Fraction = bWaiting ? (BillKg > 0.0 ? HeldKg / BillKg : 0.0) : (BuildS > 0.0 ? 1.0 - B.BuildRemaining_s / BuildS : 1.0);
3046		const int32 Step = FMath::Clamp(FMath::FloorToInt32(FMath::Clamp(Fraction, 0.0, 1.0) * RHSiteKit::Steps + 1.0e-6), 0, RHSiteKit::Steps);
3047		if (Step == Site.Step && Site.bFabricating == !bWaiting)
3048		{
3049			continue; // the 5 % step has not moved: nothing to write
3050		}
...
3068		if (UStaticMeshComponent* Mesh = Site.RevealMesh.Get())
3069		{
3070			// At 0 % the cut sits on the model's bottom and every pixel clips: drawing it would only cost the masked
3071			// passes (the tutorial's three Solar sites sit there). It shows from the first 5 % step.
3072			Mesh->SetVisibility(Site.bFabricating && Step > 0, /*bPropagateToChildren*/ false);
3073			const float CutZ = Site.RevealBottomCm + Site.RevealHeightCm * Step / RHSiteKit::Steps;
3074			for (const TWeakObjectPtr<UMaterialInstanceDynamic>& Mid : Site.RevealMids)
3075			{
3076				if (UMaterialInstanceDynamic* M = Mid.Get())
3077				{
3078					M->SetScalarParameterValue(FName("BuildCutZ"), CutZ);
3079				}
3080			}
3081		}
```

The reveal is set up once per site in `URHColonyVisualizerSubsystem::AddSite` (`:2843`), inside `if (Reveal)` (`:2905`).
The hidden mask is already looked up there by the static mesh's name (`:2914-2915`, `T_%s_HiddenMask` from
`Mesh->GetStaticMesh()->GetName()`). The block ends:

```cpp
2955			Site.RevealBottomCm = BottomZ;
2956			Site.RevealHeightCm = ModelHeightCm;
2957			Site.RevealMesh = Mesh;
2958		}
```

`RevealHeightCm` is the model's drawn height, `2 * MB.BoxExtent.Z * S` (`:2529`). `RevealBottomCm` is the seat plus
`PadTopCm` (`:2910`). So the cut at step k sits at k/20 of the mesh-bounds height. The per-site state is
`FRHSiteVis` (`RHColonyVisualizerSubsystem.h:318-341`; reveal fields `:332-335`). The step constants are in
`namespace RHSiteKit` (`.cpp:2757-2782`; `Steps = 20` at `:2776`). There is no per-model data anywhere in the reveal today.

`LogSites` (`.cpp:3397-3414`) prints the reveal as `"%.0f cm model"` only (`:3411`). `GetSiteRevealCut` (`:3381`) reads
`BuildCutZ` back from the model's first MID. `GetSiteRevealFraction` (`:3375-3379`) returns `Step / Steps`.

No cheat or command sets a site's progress. The `RHCheats.cpp` command list has `RH.Build`, `RH.Sites` (`:2581`) and
`RH.Deliver`, but nothing writes `BuildRemaining_s`, and the sim exposes no debug setter for it (`RHSimWorldSubsystem.h`,
the `Debug_*` list). The sites gate staged its 50 % frame from saves (`Saved/RHCapture/sites/capcmds.sh`: `sx_9`, `sx_13`).
Those saves are v55 (`Saved/SaveGames/RH_sx_13.sav` header `31534852 37000000`), and HEAD refuses any save that is not v60
(`RHSimWorldSubsystem.cpp:14525`). The precedent for a presentation-only pose cheat is `RH.TramPose` / `RH.TramWait`
(`RHCheats.cpp:2548`, `:2564`; `URHTramVisualizerSubsystem::Debug_PoseLine` / `Debug_ForceWait`).

### S1.2 The fault, measured on the imported mesh

**Mesh.** The Solar Array draws `/Game/RedHope/Art/Models2/SolarPanel/.../SolarPanel` (`ModelPathsV2`, `.cpp:2432`;
rh.ModelSetV2 defaults to 1, `:58-61`). The uasset's import data names
`Martians/gen/buildings-refresh/final/SolarPanel.glb`, FileMD5 `89a5754095c2f4899986c70435d66c4a`, and the file on disk
matches. The sites gate's logs confirm the scale: `reveal 481 cm model` and `SolarArray renders as imported model 'SolarPanel'
(uniform scale 4.81 ...)` (`Saved/RHCapture/logs`).

**Method.** This is the appendix script, run read-only over the GLB. glTF is Y-up, so Unreal's height is glTF Y, and the cut
fraction maps one to one. At each cut, the geometry below it is voxelised, dilated by one voxel (26-connected; gaps up to
about two voxels bridge) and flood-filled from the bottom 2 % of the height. Voxels not reached are **floating**. It was run at
160, 240 and 320 voxels across the largest side, which is 3.0, 2.0 and 1.5 cm at game scale for the Solar Array. The finished
model (cut 100 %) has no floating voxel at any resolution, so what floats at a cut floats because of the cut.

**SolarPanel (481 cm drawn).** The shape is a tripod mast with a hub. A two-panel array is tilted forward on a hinge at the
mast's top, and the panels' bottom rail hangs forward of the mast.
- The panels' bottom rail enters the cut at **46.6 %** (left panel) and 47.2 % (right) of the height, about 224 cm.
  (At 3 cm voxels both read 46.2 %.)
- The panels join the mast-top hinge at **64.6 %** (left) and **65.2 %** (right), 311-313 cm, at 1.5 cm voxels. At 2-3 cm
  voxels both have joined by 65.0 %.
- Effect on the game's 5 % steps:

| step | cut | floating (3 cm voxels) | floating (1.5 cm voxels) |
|---|---|---|---|
| 9 (45 %) | 216 cm | 0 | a 3 cm rod inside the mast apex, 41.6-45 % (379 voxels; joined at 2 cm) |
| **10 (50 %)** | 240 cm | both panels' bottom strip, 16 cm tall (3010 voxels) | both (14658) |
| **11 (55 %)** | 264 cm | both panels, 40 cm (7310) | both |
| **12 (60 %)** | 288 cm | both panels, 64 cm (11912) | both (59138) |
| **13 (65 %)** | 312 cm | 0 | the right panel (38330), 3-4 cm short of the hinge |
| 14 (70 %) | 337 cm | 0 | 0 |

This matches the director's "about 45-60 %" and capture `Saved/RHCapture/sites/after/sites_after_01.png` (the 50 % site).
In that capture the teal bar stands to the right of the half-built mast.

### S1.3 Every other model's reveal (the same fault: a part drawn below the cut whose support is above it)

Each source was checked against its uasset's FileMD5 (all match). The same scan was run at 5 % steps.

**The default set (rh.ModelSetV2 1), every model that reveals:**

| Building → mesh | drawn height | result |
|---|---|---|
| SolarArray → SolarPanel | 481 cm | **FAULT, steps 10-13** (S1.2) |
| BatteryBank → BatteryBank | 361 cm | **FAULT, step 2 (10 %) only.** Two 1.8 m tubes run across the two ends under the body's overhang, at 7.6-12.9 % of the height (27-47 cm). At the 10 % cut (36 cm) they hover about 15-20 cm outboard of the plinth with nothing above them drawn. They float from about 8.5 % to 12.9 % and join the body's underside by 13 % at 2.5 cm voxels. On the finished model they sit 3-5 cm under the body (apart at 1.7 cm voxels, joined at 2.5 cm), which is invisible at strategy zoom. |
| Pylon → Pylon | 196 cm | none |
| ChargePad → ChargePad | 326 cm | none |
| Forge → HeavyForge | 921 cm | none |
| IceDrill → IceDrill | 930 cm | none |
| WaterPlant → WaterPlant | 345 cm | Not the fault. A lid about 27 x 80 cm (40 % step) and a 25 cm fitting (50 %) sit 1-3 cm over their cabinet's top face, inside its outline. They show only at 2 cm voxels (10 and 45 voxels at 3.8 cm). |
| Electrolyzer → Electrolyzer | 645 cm | none |
| Stockpile → Stockpile | 316 cm | none |
| ComputeModule → CommandModule | 193 cm | Not the fault: a 5 cm bit on a top face at 65 % (20 voxels). |
| Borer → Borer | 416 cm | Not the fault: a 10 cm bit on the track top at 20 % (19 voxels). |
| AirFilter → AirFilter | 393 cm | none |
| HumidityRegulator → humidity | 231 cm | Not the fault. At the 5 % step, the bottom rings of the tank, a box and two posts show inside the open base plinth about 1 cm under its top face (joined by 6 %). They are inside the plinth's walls, which is the ordinary open-cut look. |

These buildings do not reveal, so they need no check:
- The Lander: `BuildTime_s` 0, placed instantly (`RHSimWorldSubsystem.cpp:9321`). Its mesh would float four engine pieces at 10-15 %, but it is never a site.
- The pressurized shells: Habitat, GreenhouseDome, HabModule, HabCorridor (`.cpp:2378`, no model).
- Floodmast, LabModule, InfirmaryModule: no real model, so `RevealHeightCm` is 0.

**The fallback set (rh.ModelSetV2 0, not what a player sees).** For the record, no hold proposed:
- `extractor2` (Borer): **the same fault, large.** The digging arm's stick and bucket hang from the boom: steps 20-75 % (16.5-75.4 % of 433 cm).
- `solar2` (SolarArray): the same fault, small. A pod floats at 8-16 %, steps 10-15 %.
- `RH_AirFilter2` (`buildings-refresh/final/AirFilter.glb`, MD5 match): a piece is apart on the finished model too. That is a mesh gap, not reveal order.
- `stockpile`: none.
- `battery`, `ice`: their import sources were scratchpad files that are gone. The files left on disk have different MD5s, so these are not checked.

Holding `extractor2` would freeze 60 % of its build. If the fallback set is ever shipped, that is an art fix, not a hold.

### S1.4 The design

**Recommendation: one per-model table, keyed by the static mesh's name, carrying the Solar Array row and the Battery Bank row.**
- **Keyed by mesh, not DefName.** The fault belongs to the geometry. Under rh.ModelSetV2 0 the SolarArray draws `solar2`, whose fault is at 10-15 %, and a DefName key would wrongly hold it at 45-70 %. The hidden mask already keys by mesh name (`:2914`).
- **The Battery Bank row is my recommendation, beyond the Solar ask.** It is the same fault, one step, one table row, and it rides the same capture. Drop the row and its three frames if the orchestrator wants the Solar alone.

**Semantics.** A row is `{ Mesh, HoldStep, ReleaseStep }`. While a site's step is after `HoldStep` and before `ReleaseStep`,
the model's cut is drawn at `HoldStep`. At `ReleaseStep` the cut jumps to the real step.
- **SolarPanel `{9, 14}`.** Steps 10-13 (50-65 %) draw the 45 % cut, 216 cm. That is 8 cm under the rail's first voxel at 224 cm; the rod in S1.2 is inside the apex and not visible. At 70 % (337 cm) the panels' lower half and the hinge appear together, 24 cm above the right panel's join.
  - Releasing at 13 is rejected: at exactly 65.0 % the right panel is 3-4 cm short of the hinge (1.5 cm scan), and the director's words are "until the hinge is built".
  - Holding at 8 is rejected: it lengthens the stall for an invisible rod.
- **BatteryBank `{1, 3}`.** Step 2 (10 %) draws the 5 % cut, 18 cm, under the tubes' 27 cm bottom. 15 % (54 cm) shows tubes and body underside together.

**Other places the change reaches:**
- The scaffold keeps following the real step (`BuiltCm` at `:3060` uses `Step`), so it stays at least a metre above the held cut. For the Solar it is already at its 2 full courses from 45 %.
- `GetSiteRevealFraction` keeps returning the progress step. The click test's `build.site` reads it for "non-decreasing" (`RHClickTest.cpp:1908`).
- `build.site` reads the cut once at the first reading of 10 % or more and checks only `CutZ > BottomZ + 1` and that the model is drawn (`:1920-1928`). A held 45 % cut passes that check.

**Rejected alternatives:**
- A progress remap with no stall. Every single-plane cut between 46.6 % and 65.2 % shows a floating panel, so a remap only moves the jump, and the ruling says "hold".
- A runtime scan of the mesh. Static data does not need runtime cost or risk.
- An art fix such as a strut to the rail. Possible later, but it is an art job, not this gate.

#### S1.4.1 `RHColonyVisualizerSubsystem.cpp`, `namespace RHSiteKit`: add before its closing brace (`:2782`)

```cpp
	// Per-model cut holds (the director's answer of 2026-10-27, docs/qa/2026-10-25/visual-pass-design.md, DIRECTOR ANSWER): a model
	// with a part that comes into the cut before what holds it up is built keeps its cut under that part until the support is in.
	// Keyed by the static mesh's name (as the hidden masks, T_<Mesh>_HiddenMask), in 5 % steps of the model's drawn height: a step
	// after HoldStep and before ReleaseStep draws HoldStep's cut. Measured on the imported source meshes (floating-piece scan,
	// 2026-11-03).
	struct FCutHold { const TCHAR* Mesh; int32 HoldStep; int32 ReleaseStep; };
	const FCutHold CutHolds[] = {
		// The Solar Array: its two panels' bottom rail enters the cut at 46.6 % of the height and the panels join the mast-top
		// hinge at 64.6 % and 65.2 %. Steps 10-13 (50-65 %) draw the 45 % cut; 70 % shows the panels and the hinge together.
		{ TEXT("SolarPanel"), 9, 14 },
		// The Battery Bank: the tubes under its two overhanging ends (7.6-12.9 % of the height) join the body's underside at 13 %.
		// Step 2 (10 %) draws the 5 % cut.
		{ TEXT("BatteryBank"), 1, 3 },
	};
	static_assert(Steps == 20, "CutHolds are written in 5 % steps");
```

#### S1.4.2 `RHColonyVisualizerSubsystem.h`, `FRHSiteVis`: add after `RevealHeightCm` (`:333`)

```cpp
		int32 CutHoldStep = -1;                // a per-model cut hold (RHSiteKit::CutHolds): a step after CutHoldStep and before
		int32 CutReleaseStep = -1;             // CutReleaseStep draws CutHoldStep's cut (-1 / -1: no hold)
		int32 PoseStep = -1;                   // RH.SitePose: the step drawn instead of the sim's, as fabrication (-1 follows the sim)
```

#### S1.4.3 `AddSite`: inside `if (Reveal)`, right after `Site.RevealMesh = Mesh;` (`:2957`)

`using namespace RHSiteKit;` is already at `:2845`.

```cpp
			// A per-model cut hold (RHSiteKit::CutHolds), by the mesh's name.
			if (const UStaticMesh* Model = Mesh->GetStaticMesh())
			{
				for (const FCutHold& Hold : CutHolds)
				{
					if (Model->GetName() == Hold.Mesh)
					{
						Site.CutHoldStep = Hold.HoldStep;
						Site.CutReleaseStep = Hold.ReleaseStep;
					}
				}
			}
```

This runs once per site, when it is created or rebuilt from a load. It is not a per-frame loop.

#### S1.4.4 `RefreshSites`

(a) Replace `:3046` (`const int32 Step = ...`) with the same expression made non-const, followed by the pose:

```cpp
		int32 Step = FMath::Clamp(FMath::FloorToInt32(FMath::Clamp(Fraction, 0.0, 1.0) * RHSiteKit::Steps + 1.0e-6), 0, RHSiteKit::Steps);
		if (Site.PoseStep >= 0)
		{
			// RH.SitePose (a capture's pose, presentation only): drawn as fabrication at the posed step, whatever the sim's progress.
			bWaiting = false;
			Step = Site.PoseStep;
		}
```

`bWaiting` is a non-const local (`:3034`), and `Fraction` (`:3045`) is computed before the override. Every later use reads the
overridden pair, so a posed site gets its crates, courses and kit rebuild exactly as a real site at that step would. These uses
are `:3047`, `:3051`, `:3055`, `:3057` and `:3061`.

(b) Replace `:3073` with:

```cpp
			// A per-model cut hold (RHSiteKit::CutHolds): the cut waits under a part whose support is not built yet.
			const int32 CutStep = (Step > Site.CutHoldStep && Step < Site.CutReleaseStep) ? Site.CutHoldStep : Step;
			const float CutZ = Site.RevealBottomCm + Site.RevealHeightCm * CutStep / RHSiteKit::Steps;
```

With no hold (-1 / -1) the condition is always false, so every other site draws exactly what it does today.

#### S1.4.5 `LogSites`: the drawn cut, and whether it is held or posed

Replace `:3405-3406` (Stage) and the reveal argument at `:3411`. Build `Reveal` before the `UE_LOG` and pass `*Reveal` where
`:3411`'s expression was; the rest of the line is unchanged.

```cpp
		const FString Stage = S.Step < 0 ? FString(TEXT("not polled yet"))
			: FString::Printf(TEXT("%s %d%%%s"), S.bFabricating ? TEXT("fabrication") : TEXT("waiting for materials"), S.Step * 100 / RHSiteKit::Steps,
				S.PoseStep >= 0 ? TEXT(" (posed)") : TEXT(""));
		// The cut as drawn, read back from the model's own MID (GetSiteRevealCut), while the model is drawn; "(held)" when a
		// per-model cut hold keeps it under the step.
		FString Reveal = S.RevealHeightCm > 0.f ? FString::Printf(TEXT("%.0f cm model"), S.RevealHeightCm) : FString(TEXT("none"));
		float CutZ = 0.f, BottomZ = 0.f;
		bool bDrawn = false;
		if (S.RevealHeightCm > 0.f && S.bFabricating && GetSiteRevealCut(Pair.Key, CutZ, BottomZ, bDrawn))
		{
			Reveal += FString::Printf(TEXT(", cut %d%%%s"), FMath::RoundToInt32(100.f * (CutZ - BottomZ) / S.RevealHeightCm),
				(S.Step > S.CutHoldStep && S.Step < S.CutReleaseStep) ? TEXT(" (held)") : TEXT(""));
		}
```

Nothing in `scripts/`, `Source/` or the gate workflows parses `[RH.Sites]` lines (`git grep` at HEAD). The header comment at
`.h:103` notes that people pick capture frames by these lines. Extend that comment to "(stage, step, the drawn cut, crates,
courses, instances)". Extend `RH.Sites`' help text (`RHCheats.cpp:2583`) after "its 5 % step" with "the model's cut as drawn
(held when a per-model cut hold keeps it lower)". Both are developer-facing.

#### S1.4.6 The pose: `Debug_PoseSite` and `RH.SitePose` (the capture's tool; developer-facing)

**Header, public, after `void LogSites() const;` (`.h:104`):**

```cpp
	// RH.SitePose (captures): draw site BuildingId (-1 = every site) as fabrication at Percent, rounded down to a 5 % step, instead
	// of the sim's progress; Percent < 0 follows the sim again. Presentation only: the sim's progress and completion are untouched.
	// False when no site matched.
	bool Debug_PoseSite(int32 BuildingId, int32 Percent);
```

**`.cpp`, after `LogSites` (`:3414`):**

```cpp
bool URHColonyVisualizerSubsystem::Debug_PoseSite(int32 BuildingId, int32 Percent)
{
	const int32 Pose = Percent < 0 ? -1 : FMath::Clamp(Percent * RHSiteKit::Steps / 100, 0, RHSiteKit::Steps);
	bool bAny = false;
	for (TPair<int32, FRHSiteVis>& Pair : SiteVis)
	{
		if (BuildingId < 0 || Pair.Key == BuildingId)
		{
			Pair.Value.PoseStep = Pose;
			bAny = true;
		}
	}
	bPassesDirty = true; // RefreshSites draws it on the next tick, not the next poll
	return bAny;
}
```

Set `bPassesDirty` only, not `MarkPassesDirty()` (`.h:146`), which would also dirty the hover and the rooms.

**`RHCheats.cpp`, straight after `GRHSites` (`:2581-2591`):**

```cpp
// Sites follow-up (the cut hold's capture): pose a construction site, as RH.TramPose poses a tram. The visualizer draws the site at
// that fabrication percent instead of the sim's; the sim is not touched.
static FAutoConsoleCommandWithWorldAndArgs GRHSitePose(
	TEXT("RH.SitePose"),
	TEXT("RH.SitePose <BuildingId|all> <Percent 0..100|off> - DEBUG: draw the construction site (or every site) as fabrication at that percent, in 5 % steps, instead of its real progress; off follows the sim again. Presentation only."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHColonyVisualizerSubsystem* Viz = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
		if (!Viz || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.SitePose <BuildingId|all> <Percent 0..100|off>"));
			return;
		}
		const int32 Id = Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase) ? -1 : FCString::Atoi(*Args[0]);
		const int32 Percent = Args[1].Equals(TEXT("off"), ESearchCase::IgnoreCase) ? -1 : FMath::Clamp(FCString::Atoi(*Args[1]), 0, 100);
		UE_LOG(LogRedHope, Display, TEXT("[RH.SitePose] %s %s: %s"), *Args[0], *Args[1], Viz->Debug_PoseSite(Id, Percent) ? TEXT("posed") : TEXT("no such site"));
	}));
```

`RHCheats.cpp` already includes `RHColonyVisualizerSubsystem.h` (`:10`). A pose lives in `FRHSiteVis` and goes with the site
when the site finishes or is forgotten. This is my recommendation over capturing real progress, because:
- real progress cannot be timed to exact 5 % steps (it depends on how many robots work the site);
- the sites gate's staged saves no longer load at v60.

#### S1.4.7 Comment touch-ups (no behaviour)

- `.h:100-101` (`GetSiteRevealFraction`): add "(the step the site is drawn at; a per-model cut hold can keep the model's cut lower, which `GetSiteRevealCut` reads)".
- `.cpp:3058-3059`: "a course ahead of the cut" stays true; the scaffold follows the step, and the step is never below the held cut.

### S1.5 How a capture proves it

**Harness.** `scripts/unreal/rh_capture.sh` at 1440x860 on a fresh world (L_Slice, no `RH.Load`). Use the same command file for
the final binary and a mutant binary, with PNGs to `Saved/RHCapture/sites_hold/`:

```
RH_SHOT_DIR=$P/Saved/RHCapture/sites_hold bash $P/scripts/unreal/rh_capture.sh sites_hold_final "$(cat cap_sites_hold.cmds)" 26 420 1440 860
```

**The command file `cap_sites_hold.cmds`** (one line in the file; broken here for reading).
- **Setup.** A fresh world has 6 Solar packs and 2 Battery packs (`RHSimWorldSubsystem.cpp:567-568`). `(-10, -10)` is the click test's own `build.place` spot: `FindBuildSpot`'s first ring-1 spot, `RHClickTest.cpp`. The uplink lag is 45 sim-s (`OrderLagTier0_s`, `RH_Config.csv:7`), so at 1x the orders land at about 48-50 s, and the 54 s pause leaves at most about 5 s of real work.

```
r.setres 1440x860w, RH.ClearTips, RH.After 2 RH.SetSpeed 1, RH.After 3 RH.Market, RH.After 3.5 RH.Market history,
RH.After 4 RH.Build SolarArray -10 -10, RH.After 4.5 RH.Build BatteryBank -10 0,
RH.After 54 RH.SetSpeed 0, RH.After 55 RH.Floor 0, RH.After 56 RH.Sites, RH.After 57 RH.Cam -10 -10 0.07 10 0,
RH.After 58 RH.SitePose all 40, RH.After 60 RH.Sites, RH.After 60 RH.Snapshot 0.5,
RH.After 62 RH.SitePose all 45, RH.After 64 RH.Sites, RH.After 64 RH.Snapshot 0.5,
RH.After 66 RH.SitePose all 50, RH.After 68 RH.Sites, RH.After 68 RH.Snapshot 0.5,
RH.After 70 RH.SitePose all 55, RH.After 72 RH.Sites, RH.After 72 RH.Snapshot 0.5,
RH.After 74 RH.SitePose all 60, RH.After 76 RH.Sites, RH.After 76 RH.Snapshot 0.5,
RH.After 78 RH.SitePose all 65, RH.After 80 RH.Sites, RH.After 80 RH.Snapshot 0.5,
RH.After 82 RH.SitePose all 70, RH.After 84 RH.Sites, RH.After 84 RH.Snapshot 0.5,
RH.After 86 RH.Cam -10 -10 0.07 10 90, RH.After 86 RH.SitePose all 50, RH.After 88 RH.Sites, RH.After 88 RH.Snapshot 0.5,
RH.After 90 RH.SitePose all 70, RH.After 92 RH.Sites, RH.After 92 RH.Snapshot 0.5,
RH.After 94 RH.Cam -10 0 0.07 10 0,
RH.After 95 RH.SitePose all 5, RH.After 97 RH.Sites, RH.After 97 RH.Snapshot 0.5,
RH.After 99 RH.SitePose all 10, RH.After 101 RH.Sites, RH.After 101 RH.Snapshot 0.5,
RH.After 103 RH.SitePose all 15, RH.After 105 RH.Sites, RH.After 105 RH.Snapshot 0.5,
RH.After 107 RH.SitePose all off, RH.After 107 RH.Cam -10 -10 0.07 10 0,
then for k = 0..13, t = 108 + 4k:  RH.After t RH.SetSpeed 1, RH.After t+2.5 RH.SetSpeed 0, RH.After t+3 RH.Sites, RH.After t+3 RH.Snapshot 0.3
```

That gives 26 snapshots:

| Frames | What |
|---|---|
| 00-06 | the Solar posed at 40 / 45 / 50 / 55 / 60 / 65 / 70 % |
| 07-08 | the same at yaw 90, 50 % and 70 % |
| 09-11 | the Battery posed at 5 / 10 / 15 % |
| 12-25 | supplementary, unposed: real progress, paused for each frame, judged by its own `RH.Sites` line |

**A run is void** if the `RH.Sites` at 56 s does not list exactly two sites, `SolarArray ... at (-10.0, -10.0) m` and
`BatteryBank ... at (-10.0, 0.0) m`, or if any `[RH.SitePose]` line says `no such site`. In that case move the pause later or
re-place, and rerun.

**Pass, final binary: every posed `RH.Sites` line carries `(posed)` and these tokens.**

| frames | Solar `reveal` field | Battery `reveal` field |
|---|---|---|
| 00 | `481 cm model, cut 40%` | |
| 01 | `481 cm model, cut 45%` | |
| 02-05 | `481 cm model, cut 45% (held)` | |
| 06 | `481 cm model, cut 70%` | |
| 09 | | `361 cm model, cut 5%` |
| 10 | | `361 cm model, cut 5% (held)` |
| 11 | | `361 cm model, cut 15%` |

- **Supplementary frames:** a Solar line at fabrication 50-65 % reads `cut 45% (held)`, and every other line's cut equals its step.
- **PNGs:**
  - **02-05:** the same model, the tripod to the 45 % line with the teal cut at its apex, and no teal bar or panel strip forward of it. Compare `sites_after_01.png`, where the bar stands at about x 760, y 290-410.
  - **06 and 08:** the panels' lower half joined to the hinge, with no gap.
  - **10:** only the plinth, with no tubes hovering at the ends.
  - **11:** tubes and body underside together.

**Mutation (the check can fail).** Build a mutant whose `:3073` replacement reads `const int32 CutStep = Step;`. Run the same
file with label `sites_hold_mut`.
- **Expected:** the `RH.Sites` lines read `cut 50%` / `55%` / `60%` / `65%` and the Battery's `cut 10%`, with no `(held)`. Frames 02-04 show the floating panel strip growing 16 / 40 / 64 cm, as in `sites_after_01.png`. Frame 10 shows the two hovering tubes.
- Restore the source by sha256 and check the final dylib's hash before counting the final run.

**What else the gate runs:**
- **Battery and playtest:** unchanged, since `Source/RedHopeSim` is untouched. The battery should stay line-identical against `battery_post_idle` except timing digits, and the 110-sol playtest byte-identical.
- **Click test:** three back-to-back passes at 115 steps (unchanged). `build.site` still passes; see S1.4.
- **Frame check:** no per-frame code is added. Whether the gate still runs its standard frame check is the orchestrator's call.

---

## S2. RH.Market: read the region count after a syncing getter

### S2.1 The code today

There are two region loops in the `RH.Market` lambda (`RHCheats.cpp:1481`), both bounded by the unsynced count.

The `history` branch (`:1582-1600`):
```cpp
1584			// E8: log only (console output is not discovery-gated, as E5's join line is not).
1585			for (int32 m = 0; m < Sim->GetMarketRegionCount(); ++m)
1586			{
1587				const int32 Region = Sim->GetMarketRegionIdAt(m);
```

The default branch (`:1624-1632`):
```cpp
1624		UE_LOG(LogRedHope, Display, TEXT("[RH.Market] %s | open sol %d | credit ..."),
1625			Sim->IsMarketLive() ? TEXT("live") : TEXT("off or no neighbor trades"), Sim->GetMarketCheckedSol(), ...
1627		// E7: one block per market region, each read with its own region; the "region N" line prints only when there is more than one, so
1628		// one region prints the lines it always did.
1629		for (int32 m = 0; m < Sim->GetMarketRegionCount(); ++m)
1630		{
1631			const int32 Region = Sim->GetMarketRegionIdAt(m);
1632			if (Sim->GetMarketRegionCount() > 1) { UE_LOG(LogRedHope, Display, TEXT("  region %d"), Region); }
```

The sim's getters, which are read-only here:
- `int32 GetMarketRegionCount() const { return Markets.Num(); }` (`RHSimWorldSubsystem.h:1907`) does not sync.
- `GetMarketRegionIdAt` (`RHSimMarket.cpp:534-538`) calls `SyncRivalEconomies(false)` first.
- `SyncRivalEconomies` (`:380-392`) rebuilds `Markets` when the rivals generation moved, then stamps `SyncedRivalsGeneration` (`MAX_uint32` = never synced, `.h:4518`).
- `IsMarketLive` (`:428-436`) syncs only when `bMarketEnabled`.

### S2.2 Reachability: what a log line can and cannot show

**When the bug bites.** The loop's condition is re-read on every iteration, and the first body statement
`GetMarketRegionIdAt(m)` syncs. So a stale count of 1 or more heals itself after the first pass. Only a stale **0**, with Defs
that would give one or more regions, skips the block. That is the E7 reviewer's "never-synced market prints no resource lines".

**Why a fresh world cannot show it.** In a world that has begun play, `Markets` is never stale at 0 with the shipped data:
- `OnWorldBeginPlay` → `InitMarket()` (`RHSimWorldSubsystem.cpp:525`) force-syncs (`RHSimMarket.cpp:120`). `RH_Rivals.csv` puts Zarya and Meridian, both with economy columns, in region 0, so there is one market from the first frame (`BuildMarketState`: one market per region with an economy rival, `:297`).
- A load resets `Markets` and stamps never-synced (`:1705-1713`), but the same reader force-syncs before it returns (`:2017`, or `:1830` when the block is lost).
- `RH.Market inject-region` syncs explicitly (`RHCheats.cpp:1535`).
- `RH.InjectRivals` bumps the generation, but only re-adds region-0 rows, so the count stays at 1 or more.
- In the default branch, the header's `Sim->IsMarketLive()` (`:1625`) syncs before the loop anyway, because `bMarketEnabled` is only switched off by suites (`RHSimCurrencySuite.cpp`, `RHSimLeversSuite.cpp`).

**So the fix is defensive.** It removes the loop's reliance on a side effect inside a log argument (`:1625`) and on the first
iteration's sync, and it closes the `history` branch, which has no syncing read before its bound at all. On a fresh world the
log is the same before and after, which is the E7 contract that one region prints the lines it always did. A log line here
proves non-regression, not the fix. I recommend GO anyway: two call sites, no output change, and it retires the reviewer's
minor. The fix itself is verified by reading.

### S2.3 The change (`RHCheats.cpp` only)

`history` branch: replace `:1585` with:
```cpp
			// E7 review: GetMarketRegionCount() is Markets.Num() as last synced and does not sync; GetMarketRegionIdAt syncs first
			// (SyncRivalEconomies), so it is read before the count bounds the loop (-1: no market at all).
			const int32 Regions = Sim->GetMarketRegionIdAt(0) >= 0 ? Sim->GetMarketRegionCount() : 0;
			for (int32 m = 0; m < Regions; ++m)
```

Default branch: replace `:1629` and `:1632` with:
```cpp
		// (E7 review: the bound is read after GetMarketRegionIdAt, which syncs; GetMarketRegionCount alone is Markets.Num() as last synced.)
		const int32 Regions = Sim->GetMarketRegionIdAt(0) >= 0 ? Sim->GetMarketRegionCount() : 0;
		for (int32 m = 0; m < Regions; ++m)
		{
			const int32 Region = Sim->GetMarketRegionIdAt(m);
			if (Regions > 1) { UE_LOG(LogRedHope, Display, TEXT("  region %d"), Region); }
```

**Why reading the bound once is equivalent.** After the first sync, `GetMarketRegionIdAt(0)` is -1 exactly when `Markets` is
empty. Nothing in the loop bodies bumps the rivals generation, so the count cannot move mid-loop. The name `Regions` is unused
elsewhere in the lambda. No string changes.

**Leave unchanged:**
- `:1536` (`inject-region`), which prints the count after its own explicit sync.
- The deck's `MarketRegionsOf` (`RHCommandDeckPanels.cpp:4143-4156`), which has the same latent bound. It is E8's player-facing panel under its own click steps, and no reachable state differs. Record it in the handoff's Follow-ups rather than widening this gate.

### S2.4 Proof

`cap_market.cmds`:
```
r.setres 1440x860w, RH.ClearTips, RH.After 3 RH.Market, RH.After 3.5 RH.Market history, RH.After 4 RH.Snapshot 0.3
```

`rh_capture.sh` needs at least one snapshot. Run it on the **before** dylib, built at `6df7fcc` and copied before the first
build, and on the **final** dylib, each with a fresh world. The final run is also covered by S1's command file, at 3 / 3.5 s.

To compare, keep the lines from `[RH.Market] live` through the last `  rival ...` line, and the `[RH.Market] history, region 0`
block, with the timestamp prefix stripped:
```
grep -E 'LogRedHope: Display: (\[RH\.Market\]|  )' <log> | sed -E 's/^\[[^]]*\]\[ *[0-9]+\]//'
```

**Pass:**
- The two blocks are identical before and after.
- There is at least one resource line (`  Ice: ... cr/kg (Earth value ...) | the colony sold 0 kg and bought 0 kg over the last 5 sols`). With the shipped `RH_Rivals.csv` I expect 7, the StoreCap keys of Zarya and Meridian, but the count only has to match.
- `history, region 0:` is present.
- There is no `  region` line (one region).

**Mutation.** None can fail in a reachable state, for the reasons in S2.2. State this in the gate report rather than inventing one.

---

## Gate notes (both items)

- **Files:** `Source/RedHope/Private/RHColonyVisualizerSubsystem.cpp`, `Source/RedHope/Public/RHColonyVisualizerSubsystem.h` and `Source/RedHope/Private/RHCheats.cpp`. Nothing else in `Source/`; `Source/RedHopeSim`, `docs/data` and `Content/RedHope/Data` are untouched.
- **Strings and randomness:** no player-facing string. The new strings are the `RH.SitePose` help and log text and the `RH.Sites` / `RH.Market` comments; `RH.Sites` gains `, cut N%`, ` (held)` and ` (posed)`. No `FMath::Rand*` or `FRandomStream`.
- **Cost:**
  - The table lookup runs once per site, at `AddSite`.
  - `RefreshSites` adds two int compares and one branch when a site's step moves.
  - `LogSites`, `RH.SitePose` and `RH.Market` run only when someone types them.
  - No per-frame work or allocation.
- **Docs for the builder** (none under `docs/data`):
  - `docs/design-decisions.md`: one entry with the S1.2/S1.3 numbers, the two table rows, the release-at-70 % reason and the mutation result, plus the S2 reachability finding.
  - `docs/qa/2026-10-26/orchestrator-handoff.md` Follow-ups: close the Sites item "Solar Array at about 45-60 %" and E7's `RH.Market` minor, citing the commit. Add the fallback-set findings (`extractor2` 20-75 %, `solar2` 10-15 %, `RH_AirFilter2`'s finished-model gap, `battery`/`ice` unverifiable) and the deck's `MarketRegionsOf` bound.
  - `gate-d-wording.md`: no block (no player-facing string).
- **Risks:**
  - The Solar's reveal now stalls at 45 % for four steps (12 s of its 60 s with one robot at 1x) and then jumps 120 cm. That is the ruled look.
  - A mesh rename would silently drop a hold. The `(held)` token in the capture log is the guard.
  - A posed site stays posed until `off` or completion. The pose is a cheat and is never saved.

---

## Appendix: the floating-piece scan (read-only; numpy + scipy)

This script produced every number in S1.2 and S1.3. Run it as
`python3 -I rh_reveal_float.py <model.glb> [res=160] [cuts in %, comma-separated]`.
For example, `... SolarPanel.glb 320 45,46.7,50,60,64.6,65,65.2,70` reproduces the S1.2 boundary rows.

```python
"""Floating-piece scan of a building model's bottom-up reveal (sites follow-up S1, read-only).
A glTF is Y-up; Unreal's height axis is glTF Y, and the reveal's cut is at (bottom + fraction x height) of the mesh bounds.
Each cut: the geometry below it is voxelised (res voxels across the model's largest side), dilated by one voxel (26-connected,
so gaps up to about two voxels bridge) and flood-filled from the bottom 2 % of the height. Voxels not reached are FLOATING."""
import json, struct, sys
import numpy as np
from scipy import ndimage

CT = {5126: 'f4', 5125: 'u4', 5123: 'u2', 5121: 'u1'}
NC = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4}

def load(path):
    data = open(path, 'rb').read()
    _, _, length = struct.unpack_from('<4sII', data, 0)
    off, js, b = 12, None, None
    while off < length:
        clen, ctype = struct.unpack_from('<II', data, off); off += 8
        chunk = data[off:off + clen]; off += clen
        if ctype == 0x4E4F534A: js = json.loads(chunk)
        elif ctype == 0x004E4942: b = chunk
    return js, b

def acc(js, b, i):
    a = js['accessors'][i]; bv = js['bufferViews'][a['bufferView']]
    dt = np.dtype('<' + CT[a['componentType']]); n = NC[a['type']]
    start = bv.get('byteOffset', 0) + a.get('byteOffset', 0); stride = bv.get('byteStride', dt.itemsize * n)
    if stride == dt.itemsize * n:
        return np.frombuffer(b, dtype=dt, count=a['count'] * n, offset=start).reshape(a['count'], n).astype(np.float64)
    return np.array([np.frombuffer(b, dtype=dt, count=n, offset=start + k * stride) for k in range(a['count'])], dtype=np.float64)

def qmat(q):
    x, y, z, w = q
    return np.array([[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)], [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)], [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]])

def local(n):
    if 'matrix' in n: return np.array(n['matrix']).reshape(4, 4).T
    T = np.eye(4); T[:3, 3] = n.get('translation', [0, 0, 0])
    R = np.eye(4); R[:3, :3] = qmat(n.get('rotation', [0, 0, 0, 1]))
    return T @ R @ np.diag(list(n.get('scale', [1, 1, 1])) + [1])

def gather(path):
    js, b = load(path); Ps, Is, off = [], [], 0
    def walk(ni, M):
        nonlocal off
        n = js['nodes'][ni]; W = M @ local(n)
        for p in (js['meshes'][n['mesh']]['primitives'] if 'mesh' in n else []):
            if p.get('mode', 4) != 4: continue
            P = acc(js, b, p['attributes']['POSITION']); P = (W[:3, :3] @ P.T).T + W[:3, 3]
            I = acc(js, b, p['indices']).astype(np.int64).reshape(-1, 3) if 'indices' in p else np.arange(len(P)).reshape(-1, 3)
            Ps.append(P); Is.append(I + off); off += len(P)
        for c in n.get('children', []): walk(c, W)
    for r in js['scenes'][js.get('scene', 0)]['nodes']: walk(r, np.eye(4))
    return np.vstack(Ps), np.vstack(Is)

def main():
    path = sys.argv[1]; res = int(sys.argv[2]) if len(sys.argv) > 2 else 160
    cuts = [float(c) for c in sys.argv[3].split(',')] if len(sys.argv) > 3 else [5.0 * k for k in range(1, 20)]
    P, I = gather(path); lo, hi = P.min(0), P.max(0); H = hi[1] - lo[1]; v = max(hi - lo) / res
    pts = []
    for tri in P[I]:
        e = max(np.linalg.norm(tri[1] - tri[0]), np.linalg.norm(tri[2] - tri[1]), np.linalg.norm(tri[0] - tri[2]))
        n = max(1, int(np.ceil(e / (v * 0.5))))
        i, j = np.meshgrid(np.arange(n + 1), np.arange(n + 1), indexing='ij'); m = (i + j) <= n
        a, bb = i[m] / n, j[m] / n
        pts.append(np.outer(1 - a - bb, tri[0]) + np.outer(a, tri[1]) + np.outer(bb, tri[2]))
    pts = np.vstack(pts)
    idx = np.clip(np.floor((pts - lo) / v).astype(int), 0, None); dims = idx.max(0) + 1
    miny = np.full(int(np.prod(dims)), np.inf); np.minimum.at(miny, np.ravel_multi_index(idx.T, dims), pts[:, 1]); miny = miny.reshape(dims)
    st = ndimage.generate_binary_structure(3, 3)
    ground = np.zeros(dims, bool); ground[:, :max(1, int(np.ceil(0.02 * H / v))), :] = True
    print(f'{path.split("/")[-1]}: height {H:.3f} (glTF Y), voxel {v:.4f} = {100 * v / H:.2f} % of the height')
    for pc in [100.0] + cuts:
        occ = miny < lo[1] + pc / 100.0 * H
        d = ndimage.binary_dilation(occ, st); lab, _ = ndimage.label(d, st)
        sup = set(np.unique(lab[d & ground & occ])) - {0}
        fl = occ & ~np.isin(lab, list(sup))
        parts = []
        for L in sorted(set(np.unique(lab[fl])) - {0}):
            w = np.argwhere(fl & (lab == L))
            parts.append(f'{len(w)} vox at {100 * w[:, 1].min() * v / H:.1f}-{100 * (w[:, 1].max() + 1) * v / H:.1f} %')
        print(f'  cut {pc:5.1f} %: floating {fl.sum():6d} of {occ.sum():6d} voxels' + ('  [' + '; '.join(parts[:4]) + ']' if parts else ''))

main()
```

**Sources scanned** (all under `Martians/gen/`, each matching its uasset's FileMD5):

| Set | Files |
|---|---|
| default | `buildings-refresh/final/SolarPanel.glb`; `hs/{BatteryBank,ChargePad,HeavyForge,IceDrill,WaterPlant,Electrolyzer,Stockpile,CommandModule,Borer,AirFilter,humidity,Lander}/<same>.glb`; `pylon2/Pylon.glb` |
| fallback | `finished_20260814/finished2/solar2.glb`, `plate-cut/{stockpile,extractor2}.glb`, `buildings-refresh/final/AirFilter.glb` |
