# P2 design, key `meshes`: M1 the cargo lump, M2 the convoy (with two deferred findings, M1b and M1c)

Designer: read-only. Repo `/Volumes/Unreal/red_hope/red_hope`, HEAD `6df7fcc` (the working tree was clean). Every line number below is at HEAD.
Re-checked on the resumed run (2026-11-03): every anchor and quote below re-read at HEAD (`TrackEntities` `:1314-1342`, `Tick` `:1889-1920`,
`LogIdleRobots` `:690-767`, the header `:116-126`, `RefreshSovereigntyVisuals` `:4984-5153`, `SpawnBox` `:5156`); the walker's bounds
(byte 79809) and its skeleton's 14 transforms (byte 3699; bone order pelvis, spine, chest, head, upperarm_L/R, forearm_L/R, thigh, shin,
foot) re-parsed; the kit GLB's bounds (X ±2.4875, up 0..2.388, Z ±1.27 m) and its beacon lens (glTF z −0.655..−0.455, the mast at +0.5)
re-measured; `cap_cargo.cmds` diffed against `cube_day.cmds` (the one change of 2.7 only).
Display only: nothing here touches `Source/RedHopeSim`, `docs/data` or `Content/RedHope/Data`; no player-facing string is added (only
`UE_LOG` lines and a cvar's help text); no random draw; no per-frame planning or allocation.

| Item | What | Recommendation | Files |
|---|---|---|---|
| **M1** | The cargo lump rides the walker's `chest` bone, its front face on the figure's back | **GO** | `RHAgentVisualizerSubsystem.cpp`, `.h` (comment) |
| M1b | The walker's body is drawn 90.6 cm ahead of the spot it is drawn at: re-centre it | **DEFER** (this gate's builder may not move the docked robot's drawing; a look change to every robot) | `RHAgentVisualizerSubsystem.cpp`, `.h` |
| M1c | The walker's soles float 34 cm above the ground: take off the sim's 50 cm agent offset | **DEFER** (moves every robot, the docked one too; supersedes the director's 2026-08-18 plant calibration) | `RHAgentVisualizerSubsystem.cpp` |
| **M2** | The trade convoy drawn with the colony's cargo rover kit, its beacon kept, seated and pitched on the visible surface | **GO** | `RHColonyVisualizerSubsystem.cpp` |

No click step is added or changed by any item (main scene count unchanged). Two new developer log lines are the checks: an
`[RH.IdleRobots] figures:` line (M1) and a `[Convoy]` spawn line (M2).

**Read this first (it matters to the walks/ruling-28 design):** the robot walker mesh's body sits 100.37 mesh cm (90.6 cm at runtime)
off its pivot along the figure's front. Every robot figure is therefore drawn 90.6 cm ahead of `Pos`, the drawn spot that routing,
`RobotClearanceCm`, the idle ring, the docked-robot disc and the click step measure. The click step is unaffected (it reads positions),
but the presentation review that ruling 28 asks for ("no walking figure stands on a pad's disc or within 150 cm of a docked robot",
judged in pixels) sees bodies up to 90.6 cm off their spots, in the direction each faces; and a figure turning on the spot swings its
body round a 90.6 cm circle. M1b is the fix; it is deferred here only because of this gate's rules (section 3).

---

## 1. The robot walker mesh, measured (not assumed)

The walker is `RH_Walker_robot` (`RHAgentVisualizerSubsystem.cpp:30`, `RobotWalkerMesh`). Its numbers, read from the assets:

- **Bounds** (`Content/.../RobotAnim/RH_Walker_robot/SkeletalMeshes/RH_Walker_robot.uasset`, seven doubles at byte 79809):
  Origin `(0.000, 100.372, 0.100)`, BoxExtent `(43.907, 17.647, 99.708)`, SphereRadius `110.367` (mesh cm). This is what
  `Walker->GetBounds()` returns (`:1319`); there is no second bounds record (no bounds extension).
- **Vertices** (the UE glTF export of this asset, `Saved/RHLineup/glb/RobotAnim__RH_Walker_robot.glb`, 26,962 vertices, exported
  2026-10-13; the mesh asset is dated 2026-08-18, so it is the shipped mesh): UE X (side) `-43.91..43.91`, Y `82.73..118.02`,
  Z `-99.61..99.81`. The crew walkers in the same export sit at Y-up `0..1.99` m with the pelvis at `(0, 0.995, 0)` (grounded at the feet,
  centred); the robot's pelvis is at glTF `(0, 0.001, 1.0037)`: **vertically centred, and 1 m off the pivot horizontally.**
  (The axis mapping is confirmed by the asset itself: glTF z 1.0037 m = the uasset's bounds Origin.Y 100.372 cm.)
- **Reference skeleton** (`RH_Walker_robot_Skeleton.uasset`, 14 transforms from byte 3699): `pelvis` unrotated at `(0, 100.372, 0.1)`;
  `spine` +15.953 Z, `chest` +27.918, `head` +19.942, all unrotated; thighs at X ±9.659 turned 180° about X, shins 45.866, feet 43.872.
  In mesh space: spine Z 16.05, **chest `(0, 100.372, 43.971)`**, head Z 63.91, ankles Z -89.64, soles Z -99.61. The torso bones' axes are
  the component's: X the figure's side, Y its front, Z up.
- **The front is +Y**: in the soles' band (Z -100..-90) the vertices run Y 85.6..118.0 while the shins are centred near Y 95, so the toes
  point +Y. `rh.WalkerYawOffsetDeg` (default -90, `RHCrewVisualizerSubsystem.cpp:53-55`) turns +Y to the facing.
- **The clips carry the same offset**: the Idle and Haul clips hold a float 100.3724 (the pelvis's reference Y) at byte 100302; the Walk
  clip's run at bytes 100406.. alternates 100.3724 / 100.2543. No clip moves the body back over the pivot.
- **The back's surface** (min Y over the spine column |x| < 16, from the vertices): Z 25-35: Y 91.3-92.1; Z 35-45: 87.1-90.1;
  Z 45-50: 85.3; Z 50-60: 82.7-83.2. The arms (|x| 24-45) reach back to Y 83.1-85.7 at Z 25-45.

Runtime numbers at HEAD (`TrackEntities` `:1319-1325`, `Tick` `:1889-1893`):

| Quantity | Formula | Value |
|---|---|---|
| WalkerScale | 180 / (2 × 99.708) | **0.90263** |
| FeetLiftCm | (99.708 − 0.100) × 0.90263 | **89.91 cm** |
| `Pos.Z` of a surface walker | `RH::RobotZOffsetCm` (`RHRobotMovement.h:46`); every robot spawns at Z 50 (`RHSimWorldSubsystem.cpp:590`), dummies too (`RHAgentSubsystem.cpp:47`) | **50 cm** |
| Plant | `rh.RobotPlantCm` (`:71-75`) | 16 cm |
| Component origin | Pos.Z + TerrainZ + FeetLift − Plant | **TerrainZ + 123.9 cm** |
| Reference soles | 123.9 − 99.61 × 0.90263 | **TerrainZ + 34.0 cm** |
| Head top | 123.9 + 99.81 × 0.90263 | TerrainZ + 214.0 cm |
| Body centre (pelvis) horizontally | Pos + 100.372 × 0.90263 along the facing | **Pos + 90.6 cm** |
| Chest bone | Pos + 90.6 cm fwd, Z 123.9 + 43.97 × 0.90263 | TerrainZ + 163.6 cm |

---

## 2. M1. The cargo lump rides the walker's back (GO)

### 2.1 Why it floats: the offsets were written for a figure that is not the one that shipped

The lump was added in `8e6ae44` (2026-08-18) and, as that commit says, "COULD NOT be photographed" then; it was code-verified only.
Its offsets (+118 cm up, 30 cm back, `:1913-1917`) are measured from the walker **component's origin**, as if that origin were at the
soles and under the body. For `RH_Walker_robot` the origin is at mid-height and 100.37 mesh cm behind the body; on top of that the
component origin carries the sim's 50 cm agent Z (`Pos.Z`), which `FeetLiftCm` never took off.

| | Today (HEAD) | After M1 |
|---|---|---|
| Lump centre height | Pos.Z + TerrainZ + FeetLift − Plant + 118 = **TerrainZ + 241.9 cm**: 27.9 cm above the head top (214.0) | 126.0 cm above the soles (70 % of 180 cm) |
| Lump centre horizontally | Pos − 30 cm along the facing = **120.6 cm behind the body centre** (Pos + 90.6) | 25-35 cm behind the body's centre line, its front face on the back |
| Distance to the chest bone | √(120.6² + 78.3²) = **143.8 cm** | 25.3-35.2 cm (LS 0.24-0.44) |
| Centre above the soles | 241.9 − 34.0 = 207.9 cm | 126.0 cm |

**Checked against the captures** (camera `RH.Cam 8.5 20.5 0 0 0`: focus (8.5, 20.5) m, 25 m out, pitch 30°, horizontal FOV 55°, 1440×860,
`RHStrategyPawn.cpp:56-74` and `RHStrategyPawn.h:85-90`; figure #3 hauling 20 kg to Stockpile #27, `design-decisions.md:2928`):

| Shot | #3 drawn at (log) | Lump centre predicted (formula above) | Lump observed |
|---|---|---|---|
| `r2/frame2/cube/shots/cube_r2q1_09.png` | (4.9, 21.2) m, moving at about −108° | (786, 407) px | (792, 407) px |
| `cube_r2q1_11.png` | ≈ (4.03, 18.6) m, −109° | (622, 434) px | (622, 432) px |

With `Pos.Z` 0 the predicted lump in `_09` would be at y 436 (29 px off): the 50 cm is real. Figure #3's predicted head and soles with
the 90.6 cm forward offset and the 34 cm float, (709, 436) and (710, 536) in `_09`, match the observed (707, 426) and (702, 538) within
10 px; the projected drawn spot (764, 544) does not. The pad (10, 20) m projects to (694, 379) px, observed (692, 377). The sun in `_11`
is nearly overhead (the lump's shadow lies about 24 cm from the point under it: elevation about 84°), so the lump's shadow falls under
the lump, 1.2 m behind the figure's own shadow: "its shadow apart from it".

**The walk clip:** it neither causes nor fixes this (section 1: the clips keep the pelvis at Y 100.25-100.37). The clips that carry cargo
are **Walk and Idle**: `CargoKg` is set only on a Haul's Phase 1 → 2 transition (`RHRobotTaskProcessor.cpp:266`,
`RHRobotStateTree.cpp:259`) and cleared on unload, so a robot carries only in Phase 2 (moving: Walk; standing, held at a hand-off, riding
or queueing for a lift, or halted drained: Idle). Tick plays Haul only for Phase 1 or 3 (`:1935-1941`), when `CargoKg` is 0; a robot
mid-delivery is exempt from seeking a charge (`bMidDelivery`, `RHRobotTaskProcessor.cpp:83-86`), and one that does seek drops its cargo
first (`:98-111`). Attaching to the bone makes the lump follow whatever clip
plays (Walk's ±3° spine sway, Idle's 2.2° chest breath, Haul's 6° lean) all the same.

### 2.2 Current code (HEAD)

`RHAgentVisualizerSubsystem.cpp:24-39` (anonymous namespace):
```cpp
namespace
{
	// C6b: the cargo rover's kit model (scripts/blender/rh_build_rover.py, imported by rh_import_hs.py): true
	// scale (5 m long), pivot centred on the tyre soles, front at +X.
	const TCHAR* RoverMeshPath = TEXT("/Game/RedHope/Art/Models2/Rover/Rover/StaticMeshes/Rover.Rover");
	const FName RoverClassName(TEXT("Rover"));
	const TCHAR* RobotWalkerMesh = TEXT("/Game/RedHope/Art/RobotAnim/RH_Walker_robot/SkeletalMeshes/RH_Walker_robot.RH_Walker_robot");
	// Every clip lives beside the mesh as RH_Walker_robot<Suffix>. ...
	FString RobotClipPath(FName Suffix)
	{
		const FString S = Suffix.ToString();
		return FString::Printf(TEXT("/Game/RedHope/Art/RobotAnim/RH_Walker_robot/SkeletalMeshes/RH_Walker_robot%s.RH_Walker_robot%s"), *S, *S);
	}
}
```

`URHAgentVisualizerSubsystem::TrackEntities`, `:1319-1342`:
```cpp
			const FBoxSphereBounds WB = Walker->GetBounds();
			const float WalkerScale = 180.f / FMath::Max(2.f * WB.BoxExtent.Z, 1.f);
			Skel->SetWorldScale3D(FVector(WalkerScale));
			// Feet-on-ground from the bounds, not from a pivot assumption: a
			// grounded mesh (origin at soles) yields 0, a mid-body origin
			// yields +extent. Works for either without knowing which shipped.
			FeetLiftCm.Add((WB.BoxExtent.Z - WB.Origin.Z) * WalkerScale);
			// The cargo lump: what a hauling robot is CARRYING, finally
			// visible. Hidden until the task fragment says CargoKg > 0.
			UStaticMeshComponent* Lump = NewObject<UStaticMeshComponent>(Holder);
			Lump->RegisterComponent();
			Holder->AddInstanceComponent(Lump);
			Lump->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Lump->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Lump->SetAbsolute(true, true, true);
			if (UMaterialInterface* LumpBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(LumpBase, Lump);
				Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(0.44f, 0.21f, 0.10f)); // hauled regolith
				Mid->SetVectorParameterValue(FName("Emissive"), FLinearColor::Black);
				Lump->SetMaterial(0, Mid);
			}
			Lump->SetVisibility(false);
			CargoLumps.Add(Lump);
```

`URHAgentVisualizerSubsystem::Tick`, `:1889-1922`:
```cpp
				static const auto* YawOff = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("rh.WalkerYawOffsetDeg"));
				const float Plant = CVarRobotPlantCm.GetValueOnGameThread();
				Skel->SetWorldLocationAndRotation(
					FVector(Pos.X, Pos.Y, Pos.Z + TerrainZ + (FeetLiftCm.IsValidIndex(i) ? FeetLiftCm[i] : 0.f) - Plant),
					FRotator(0.f, FacingYawDeg[i] + (YawOff ? YawOff->GetValueOnGameThread() : 0.f), 0.f));
				// Cargo read from the PUBLIC sim fragments (professor: no
				// accessor - the data is already exported). GetFragmentDataPtr,
				// never Checked: the RH.Crew cheat's dummy agents share this
				// tracking list and their archetype has no task fragment.
				if (CargoLumps.IsValidIndex(i))
				{
					if (UStaticMeshComponent* Lump = CargoLumps[i].Get())
					{
						const FRHTaskFragment* Task = EntityManager.GetFragmentDataPtr<FRHTaskFragment>(Tracked[i]);
						const FRHRobotFragment* Robot = EntityManager.GetFragmentDataPtr<FRHRobotFragment>(Tracked[i]);
						const float Cargo = Task ? Task->CargoKg : 0.f;
						const bool bShow = bOnView && Cargo > 0.f;
						if (Lump->IsVisible() != bShow)
						{
							Lump->SetVisibility(bShow);
						}
						if (bShow)
						{
							const float Frac = FMath::Clamp(Cargo / FMath::Max(Robot ? Robot->CargoCapKg : 100.f, 1.f), 0.f, 1.f);
							const FVector Back = FRotator(0.f, FacingYawDeg[i], 0.f).Vector() * -30.f;
							Lump->SetWorldLocationAndRotation(
								FVector(Pos.X + Back.X, Pos.Y + Back.Y,
									Pos.Z + TerrainZ + (FeetLiftCm.IsValidIndex(i) ? FeetLiftCm[i] : 0.f) - Plant + 118.f),
								FRotator(0.f, FacingYawDeg[i] + 12.f, 0.f));
							const float LS = 0.24f + 0.20f * Frac;
							Lump->SetWorldScale3D(FVector(LS, LS * 1.25f, LS * 0.7f));
						}
					}
				}
```

`RHAgentVisualizerSubsystem.h:122-126`:
```cpp
	// One small regolith-lump component per skeletal robot (professor-gated
	// 2026-08-18): visible while the robot's FRHTaskFragment carries CargoKg,
	// scaled by load fraction. Read from the PUBLIC Mass fragments - no sim
	// accessor, no sim recompile.
	TArray<TWeakObjectPtr<UStaticMeshComponent>> CargoLumps;
```

`URHAgentVisualizerSubsystem::LogIdleRobots`, end, `:759-767`:
```cpp
		default:
			++Busy;
			UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] #%d busy, %s"), i, *At);
			break;
		}
	}
	UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] %d surface walker(s): %d busy, %d waiting at a pad, %d at the Lander rank (%d from a full ring), %d inspecting"),
		Walkers, Busy, AtPad, Rank, Full, Inspecting);
}
```

### 2.3 The exact change

**(a) `RHAgentVisualizerSubsystem.cpp`, the anonymous namespace: insert after `RobotClipPath` (after `:38`, before the closing `}` at `:39`):**
```cpp
	// P2 M1: the cargo lump rides the walker's chest bone. RH_Walker_robot's reference skeleton (RH_Walker_robot_Skeleton) leaves every
	// torso bone unrotated, so the bone's X is the figure's side, Y its front and Z up, and puts the chest at (0, 100.37, 43.97) mesh cm;
	// the back's surface on the spine column, 30-50 cm up the mesh, is near Y 86; the soles are at Z -99.61. Mesh cm: the walker's scale
	// (x0.9026 for its 180 cm) applies to them through the attachment.
	const FName LumpBone(TEXT("chest"));
	constexpr float LumpFrontYCm = -14.4f; // the lump's front face, behind the chest bone: on the back (mesh Y 86)
	constexpr float LumpDropCm = -4.0f;    // its centre, below the chest bone: mesh Z 40, 126 cm above the soles at x0.9026
```

**(b) `TrackEntities`: replace `:1326-1333`** (the two comment lines through `Lump->SetAbsolute(true, true, true);`) **with:**
```cpp
			// The cargo lump: what a hauling robot is CARRYING, finally
			// visible. Hidden until the task fragment says CargoKg > 0.
			// P2 M1: it rides the chest bone (location and rotation relative to the bone, so the walker's pivot, scale, facing and every clip
			// carry it); its scale stays absolute, in world cm, as before.
			UStaticMeshComponent* Lump = NewObject<UStaticMeshComponent>(Holder);
			Lump->RegisterComponent();
			Holder->AddInstanceComponent(Lump);
			Lump->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Lump->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Lump->SetAbsolute(false, false, true);
			if (!Skel->DoesSocketExist(LumpBone))
			{
				UE_LOG(LogRedHope, Warning, TEXT("Cargo lump: the walker mesh has no '%s' bone; the lump rides the walker's pivot"), *LumpBone.ToString());
			}
			Lump->AttachToComponent(Skel, FAttachmentTransformRules::KeepRelativeTransform, LumpBone);
```
`:1334-1342` (the material, `SetVisibility(false)`, `CargoLumps.Add`) stay as they are. `Skel` is created at `:1314`, before the lump.

**(c) `Tick`: replace the `if (bShow) { ... }` block `:1910-1920` with:**
```cpp
						if (bShow)
						{
							// P2 M1: the lump rides the chest bone (attached in TrackEntities), so the figure's pivot, scale, facing and clip carry it;
							// only its size follows the load, and its centre steps back with its depth so its front face stays on the back.
							const float Frac = FMath::Clamp(Cargo / FMath::Max(Robot ? Robot->CargoCapKg : 100.f, 1.f), 0.f, 1.f);
							const float LS = 0.24f + 0.20f * Frac;
							const FVector WantScale(LS * 1.25f, LS, LS * 0.7f); // across the back (the bone's X), its depth (Y), its height
							if (!Lump->GetComponentScale().Equals(WantScale, 1.e-3f))
							{
								const float MeshPerCm = 1.f / FMath::Max((float)Skel->GetComponentScale().Y, 0.01f);
								Lump->SetRelativeLocation(FVector(0.f, LumpFrontYCm - LS * 50.f * MeshPerCm, LumpDropCm));
								Lump->SetWorldScale3D(WantScale);
							}
						}
```
The lump's size is today's (`LS = 0.24 + 0.20 × Frac`; width 1.25 LS, depth LS, height 0.7 LS, in metres); the X and Y factors swap because
the bone's X is the figure's side (the old code yawed the lump to the facing, whose X was the depth). The old 12° skew is dropped: the
lump sits square on the back. The relative location is set only when the scale changes (a new load), so a shown lump costs one vector
compare per frame.

**(d) `RHAgentVisualizerSubsystem.h:122-125`: the comment becomes:**
```cpp
	// One small regolith-lump component per skeletal robot (professor-gated
	// 2026-08-18): visible while the robot's FRHTaskFragment carries CargoKg,
	// scaled by load fraction. Read from the PUBLIC Mass fragments - no sim
	// accessor, no sim recompile. P2 M1: attached to the walker's chest bone, its front face on the figure's back.
```

**(e) The check's read-out. `LogIdleRobots`: insert after the summary `UE_LOG` (after `:766`, before the closing `}` at `:767`):**
```cpp
	// P2 M1 (developer read-out; it also records the deferred M1b and M1c): where each surface walker's body, reference soles and cargo
	// lump are drawn against the spot it is drawn at. RH_Walker_robot at HEAD: the pelvis 91 cm from the spot, the soles 34 cm up.
	int32 Figures = 0, Lumps = 0;
	float PelvisFar = 0.f, SolesLo = 1.e9f, SolesHi = -1.e9f, LumpFar = 0.f, LumpLo = 1.e9f, LumpHi = -1.e9f;
	for (int32 i = 0; i < SkelBodies.Num(); ++i)
	{
		const USkeletalMeshComponent* Skel = SkelBodies[i];
		if (!Skel || !Skel->IsVisible() || TickSerial == 0 || !Trips.IsValidIndex(i) || Trips[i].WalkTick != TickSerial || !FeetLiftCm.IsValidIndex(i))
		{
			continue; // a surface walker AdvanceDrawn drew on the last tick, on view
		}
		++Figures;
		const FVector Spot = Trips[i].DrawCm;
		const FVector Pelvis = Skel->GetSocketLocation(FName("pelvis"));
		PelvisFar = FMath::Max(PelvisFar, (float)FVector::Dist2D(Pelvis, Spot));
		const float SolesZ = (float)Skel->GetComponentLocation().Z - FeetLiftCm[i]; // the reference pose's soles
		const float Up = SolesZ - RHMarsTerrain::GroundZCm(Spot.X, Spot.Y);
		SolesLo = FMath::Min(SolesLo, Up);
		SolesHi = FMath::Max(SolesHi, Up);
		const UStaticMeshComponent* Lump = CargoLumps.IsValidIndex(i) ? CargoLumps[i].Get() : nullptr;
		if (Lump && Lump->IsVisible())
		{
			++Lumps;
			const FVector L = Lump->GetComponentLocation();
			LumpFar = FMath::Max(LumpFar, (float)FVector::Dist2D(L, Pelvis));
			LumpLo = FMath::Min(LumpLo, (float)L.Z - SolesZ);
			LumpHi = FMath::Max(LumpHi, (float)L.Z - SolesZ);
		}
	}
	UE_LOG(LogRedHope, Display, TEXT("[RH.IdleRobots] figures: %d surface walker(s) on view%s; %d cargo lump(s) shown%s"),
		Figures,
		Figures ? *FString::Printf(TEXT(", the farthest pelvis %.0f cm from its drawn spot, reference soles %.0f to %.0f cm above the ground"), PelvisFar, SolesLo, SolesHi) : TEXT(""),
		Lumps,
		Lumps ? *FString::Printf(TEXT(", the farthest %.0f cm across from its walker's pelvis, centres %.0f to %.0f cm above their walkers' soles"), LumpFar, LumpLo, LumpHi) : TEXT(""));
```
`LogIdleRobots` is `const`; every call here is const (`GetSocketLocation`, `GetComponentLocation`, `IsVisible`, the weak pointer's `Get`).
`RHMarsTerrain.h` is already included (`:3`). The walks design adds lines inside the pad loop and after it (before the walker loop) and
appends `, %d staying put` to the summary `UE_LOG` (`:765-766`, walks.md section 7); the cache design replaces the header line `:700`.
This block goes after the summary statement, whatever its final text, and before the function's closing brace, so they do not meet;
whichever lands second anchors by that symbol, not by line number.

### 2.4 Numbers after M1 (RH_Walker_robot, scale 0.90263)

- Front face: 14.4 mesh cm = **13.0 cm** behind the chest bone's axis (the body's centre line), on the back (mesh Y 86).
- Centre: (14.4 + 55.39 × LS) mesh cm behind the axis: LS 0.24 → 25.0 cm, LS 0.26 (#3's 20 kg of a Hauler's 200, `RH_Robots.csv`
  Cargo_kg) → 26.0 cm, LS 0.44 → 35.0 cm; **3.6 cm below the chest joint = 126.0 cm above the soles**.
- Size: LS 0.24-0.44 → 30-55 cm across, 24-44 cm deep, 17-31 cm tall (unchanged).
- The read-out after M1 (M1b, M1c not built): `the farthest pelvis 91 cm from its drawn spot, reference soles 34 to 34 cm above the
  ground`; with a lump: `the farthest 25-38 cm across from its walker's pelvis, centres 120-132 cm above their walkers' soles` (the clip's
  spine and chest rotations move it a few cm).
- The figure itself does not move: the lump now rides it 90.6 cm ahead of the drawn spot and 34 cm up with it (M1b, M1c), so its shadow
  falls with the figure's.

### 2.5 Proof (the named check)

**Check `M1 cargo on the back`**: in the after capture's log (`cap_cargo`, 2.7), at least one `[RH.IdleRobots] figures:` line shows
`1` or more cargo lumps (#3 hauls from about 138.5 to 142 s; the read-outs at 138.5, 139.5, 140.2 and 140.9 s fall in it), and every such
line has `the farthest ≤ 45 cm across from its walker's pelvis` and `centres` within **110 to 140 cm** above the soles. Before (HEAD's
placement) the same numbers would be 120.6 cm and 207.9 cm. Plus the captures (2.7), judged by the presentation review: the lump on the
figure's back in `_09`, `_10` and `_12`, its shadow with the figure's.

### 2.6 Mutation

`p2_mut_m1`: delete the one line `Lump->AttachToComponent(Skel, FAttachmentTransformRules::KeepRelativeTransform, LumpBone);`. The lump
keeps its relative location, now a world one, `(0, about −29, −4)` cm, at the world origin: the read-out reports it thousands of cm
across from its walker's pelvis and about −38 cm above the soles, so `M1 cargo on the back` FAILS; the click test, the battery and the frame
check do not read it.

### 2.7 Captures: one command file, before and after

`cap_cargo.cmds` = the cube diagnostic's `r2/frame2/cube/cube_day.cmds` (itself `cap/cap_day.cmds` plus read-outs and two close-ups),
with **one change**: its `RH.After 139.6 RH.Cam 4 15 0.12 0 0` is replaced by a top-down close-up and the wide view's restore moved to
140.2 s. Every sim command (load, speed, `RH.Power`, `RH.Lag`, `RH.Build`) is identical, so the same robot (#3) hauls at the same time.
The file, verbatim (one line):

```
r.setres 1440x860w, RH.ClearTips, RH.After 2 RH.Load idle_day, RH.After 2.5 RH.SetSpeed 1, RH.After 3 RH.Floor 0, RH.After 3.5 rh.RobotRouteLog 1, RH.After 4 RH.Cam 8 14 0.12 0 0, RH.After 30 RH.IdleRobots, RH.After 30 RH.Fleet, RH.After 31 RH.Cam 10 20 0.06 0 0, RH.After 33 RH.Snapshot 0.5, RH.After 35 RH.Cam 26 -4 0.06 0 0, RH.After 37 RH.Snapshot 0.5, RH.After 39 RH.Cam 6 14 0.12 0 0, RH.After 45 RH.Snapshot 0.5, RH.After 52 RH.Snapshot 0.5, RH.After 59 RH.Snapshot 0.5, RH.After 60 RH.IdleRobots, RH.After 66 RH.Snapshot 0.5, RH.After 70 RH.Cam 6 2 0.32 0 0, RH.After 72 RH.Snapshot 0.5, RH.After 75 RH.Power 6 0, RH.After 75 RH.Power 26 0, RH.After 76 RH.Cam -6 2 0.08 0 0, RH.After 100 RH.IdleRobots, RH.After 101 RH.Snapshot 0.5, RH.After 104 RH.Cam 2 6 0.16 0 0, RH.After 106 RH.Snapshot 0.5, RH.After 110 RH.Power 6 1, RH.After 110 RH.Power 26 1, RH.After 135 RH.IdleRobots, RH.After 136 RH.Cam 4 15 0.12 0 0, RH.After 136 RH.Lag 1, RH.After 137 RH.Build Stockpile -4 12, RH.After 140 RH.Snapshot 0.5, RH.After 143 RH.Snapshot 0.5, RH.After 146 RH.Snapshot 0.5, RH.After 150 RH.Snapshot 0.5, RH.After 152 RH.IdleRobots, RH.After 153 RH.Fleet, RH.After 154 RH.Status, RH.After 136.5 RH.IdleRobots, RH.After 136.5 RH.Fleet, RH.After 136.5 RH.Rover list, RH.After 136.5 RH.Sites, RH.After 137.5 RH.IdleRobots, RH.After 137.5 RH.Fleet, RH.After 137.5 RH.Rover list, RH.After 137.5 RH.Sites, RH.After 138.5 RH.IdleRobots, RH.After 138.5 RH.Fleet, RH.After 138.5 RH.Rover list, RH.After 138.5 RH.Sites, RH.After 139.5 RH.IdleRobots, RH.After 139.5 RH.Fleet, RH.After 139.5 RH.Rover list, RH.After 139.5 RH.Sites, RH.After 140.2 RH.IdleRobots, RH.After 140.2 RH.Fleet, RH.After 140.2 RH.Rover list, RH.After 140.2 RH.Sites, RH.After 140.9 RH.IdleRobots, RH.After 140.9 RH.Fleet, RH.After 140.9 RH.Rover list, RH.After 140.9 RH.Sites, RH.After 142 RH.IdleRobots, RH.After 142 RH.Fleet, RH.After 142 RH.Rover list, RH.After 142 RH.Sites, RH.After 143.5 RH.IdleRobots, RH.After 143.5 RH.Fleet, RH.After 143.5 RH.Rover list, RH.After 143.5 RH.Sites, RH.After 138.8 RH.Cam 8.5 20.5 0 0 0, RH.After 139.0 RH.Snapshot 0.2, RH.After 139.5 RH.Cam 4.7 20.5 0 30 0, RH.After 139.6 RH.Snapshot 0.2, RH.After 140.2 RH.Cam 4 15 0.12 0 0, RH.After 141.0 RH.Cam 8.5 20.5 0 0 0, RH.After 141.2 RH.Snapshot 0.2, RH.After 141.8 RH.Cam 4 15 0.12 0 0
```

16 snapshots (the cube run had 15; it took 177 s with timeout 420). Run it with the idle gate's capture script (`idle-work/cap/idle_capture.sh`,
copied for P2; scene name `cargo`) on the before dylib and on the final one: `p2_before_cargo_NN`, `p2_after_cargo_NN`. The frames:

| NN | Time | Camera | Shows |
|---|---|---|---|
| 00-08 | 33-106 s | as `cap_day` | the ring and the Lander rank (unchanged by M1; for M1b/M1c's record) |
| **09** | ≈139.4 s | `8.5 20.5 0 0 0` (= `cube_r2q1_09`) | #3 walking along the pad's near side with its lump: before, the cube on the pad's lower-right rim; after, on its back |
| **10** | ≈139.95 s | `4.7 20.5 0 30 0` (pitch 60°; #3 near (4.7, 20.5) m) | new: from above, the lump on the shoulders' line |
| 11 | ≈140.6 s | `4 15 0.12 0 0` | the wide frame (= `cube_r2q1_10`) |
| **12** | ≈141.5 s | `8.5 20.5 0 0 0` (= `cube_r2q1_11`) | #3 further on: before, the cube over the rim and its shadow apart; after, on its back, one shadow |
| 13-15 | 143-150 s | `4 15 0.12 0 0` | the site's work (the lump gone once #3 unloads) |

Timing margins (from the cube log: a `RH.Snapshot 0.2` is taken about 0.36 s after its `RH.After`): close-up A is taken by ≈139.36 s,
the camera moves at 139.5; the top-down shot is taken by ≈139.96, the wide view returns at 140.2; the 140 s snapshot is taken ≈140.66.

### 2.8 Risk

- **Scale:** the lump's world size is unchanged (absolute scale). Its offset is in mesh cm and scales with the walker: only its half-depth
  is converted (`MeshPerCm`), so a re-fitted mesh keeps the front face on the back.
- **Collision:** none (`NoCollision`, unchanged).
- **Frame cost:** in this subsystem's `Tick`, a shown lump costs one `FVector::Equals` per frame instead of `SetWorldLocationAndRotation` +
  `SetWorldScale3D` (cheaper); the lump's transform now follows its bone in the skeletal component's own bone-transform finalize (one
  attached child per walker, at most about 20; a hidden lump is not in the scene). The `[RobotRoute]` tick median: no rise expected
  (well inside +0.2 ms).
- **Actor / HISM counts:** unchanged (one lump component per walker, as today; no HISM).
- **Look:** the lump moves with its figure, which is itself 90.6 cm ahead of its drawn spot and 34 cm up (M1b, M1c deferred); large
  loads (LS 0.44, 30.5 mesh cm half-width) meet the arms' backs (mesh Y 83-86 at |x| 24-39): the elbows cut up to about 9 cm into the
  lump's front corners on the Walk's backswing. The 12° skew is gone.
- **Other figures:** rovers own no lump (`CargoLumps.Add(nullptr)`, `:1307`); riders, figures below and road-leg walkers keep their lump
  on the back (it follows the skeletal component wherever `Tick` puts it); the docked robot and the idle figures carry nothing.
- **Save/load:** `ResetTracking` destroys each walker (`:1200-1206`) before its lump (`:1207-1214`): the lump is detached by the
  walker's destruction and destroyed next (weak pointers). `TrackEntities` re-attaches on respawn.
- **A mesh without a `chest` bone:** the lump would ride the pivot (a warning is logged at spawn). RH_Walker_robot has it (its skeleton
  and mesh name tables list `pelvis`, `spine`, `chest`, `head`).
- **Other P2 items:** walks (`LogIdleRobots`' pad loop, `AdvanceDrawn`, `RefreshIdlePads`, `GatherDocked`) and cache (`:700`,
  `:1191-1194`, `RefreshIdlePads`, `ChooseIdlePad`) touch none of these lines.

---

## 3. M1b. The walker's body drawn on its drawn spot (DEFER; exact change ready)

**Finding:** the mesh's body (bounds centre and pelvis) is at mesh `(0, 100.37)`, so every robot figure is drawn **90.6 cm ahead of
`Pos`** along its facing (`Tick` `:1891-1893` puts the pivot at `Pos`). Effects today: a figure at its ring slot facing outward stands
630 cm from the pad's centre, not 540; the docked robot stands 90.6 cm beyond its dock point; a walking figure leads its routed path by
90.6 cm (round a footprint corner its body can cut 90 cm inside the routed line); a figure turning on the spot swings its body through
a 90.6 cm-radius arc (a 180° turn moves it 181 cm); `TerrainZ` is sampled at `Pos`, not under the body. In the capture `cube_r2q1_09`,
the two waiting figures (#5, #6) and #3 sit 60-75 px right of / left of their projected spots, in the direction each faces.

**The change (for a later display-only gate):**
- `RHAgentVisualizerSubsystem.h`, after `FeetLiftCm` (`:121`):
  ```cpp
	// P2 M1b: where the walker mesh's body stands off its pivot, horizontally, at its runtime scale (its bounds' centre; RH_Walker_robot:
	// (0, 100.37) mesh cm = 90.6 cm along its front). Tick moves the pivot back by it, so the body is drawn on its drawn spot.
	FVector WalkerBodyCm = FVector::ZeroVector;
  ```
- `TrackEntities`, after `FeetLiftCm.Add(...)` (`:1325`): `WalkerBodyCm = FVector(WB.Origin.X, WB.Origin.Y, 0.0) * WalkerScale;`
- `Tick` `:1891-1893` becomes:
  ```cpp
				const FRotator BodyRot(0.f, FacingYawDeg[i] + (YawOff ? YawOff->GetValueOnGameThread() : 0.f), 0.f);
				Skel->SetWorldLocationAndRotation(
					FVector(Pos.X, Pos.Y, Pos.Z + TerrainZ + (FeetLiftCm.IsValidIndex(i) ? FeetLiftCm[i] : 0.f) - Plant) - BodyRot.RotateVector(WalkerBodyCm),
					BodyRot);
  ```
- Proof: the M1 read-out's `the farthest pelvis N cm from its drawn spot` goes from 91 to 0 (check ≤ 5 cm); mutation: drop
  `- BodyRot.RotateVector(WalkerBodyCm)` (91 cm, fails). Captures: `cap_cargo` `_02`-`_06` (the figures at their slots, 540 cm out), `_09`,
  `_12`. Frame cost: one `RotateVector` per walker per frame. The lump (M1) follows automatically (attached).

**Why DEFER here:** this gate's builder is told not to "move the docked robot's drawing on the pad", and M1b moves it 90.6 cm (as it
moves every robot's body); it also changes every robot capture, which the director should see first. The walks design measures the
keep-out and the docked-robot disc at drawn positions, so its click checks hold either way; its pixel review does not (the note at the
top). Recommendation: an orchestrator ruling to take M1b in this gate (it makes ruling 28's 150 cm and 350 cm true on screen), or its own
small gate right after.

## 4. M1c. The walker's soles on the ground (DEFER; exact change ready)

**Finding:** `FeetLiftCm` lifts the reference soles to `Pos.Z`, but `Pos.Z` is the sim's agent offset (50 cm on the surface; a floor's
Z + 50 below), so the reference soles sit at `TerrainZ + 50 − Plant` = **34 cm up**. The 2026-08-18 "floaters" fix (`1f68ffe`, `Plant` 16)
read the float as the clip's soles sitting higher than the reference pose; the Idle clip keys no leg, and the Walk's planted foot rides
only 0.7-2.3 cm higher (thigh ±17°, shin keys, `rig_colonist.py` WALK). In `cube_r2q1_09` the standing figure left of the pad has its
shadow about 14 px below its boots under a near-overhead sun (≈31 cm). A rider in a lift car floats 34 cm over the car's plate (the plate
already takes the offset off: `:1874`).

**The change:** `Tick` `:1892`: `Pos.Z` → `Pos.Z - RH::RobotZOffsetCm` (the include `RHRobotMovement.h` is at `:12`); `CVarRobotPlantCm`
(`:71-75`) default 16 → 2, help text: "Cm to press the robot walker below its bounds-derived ground fit (the sim's 50 cm agent offset is
taken off first). The fit plants the reference pose's soles; the Walk clip's planted foot rides 0.7-2.3 cm above them, so 2 keeps it
down."; the header comment `:116-120` loses "the rigged GLBs are grounded at their feet" (RH_Walker_robot is not). Net: every robot
figure (and its lump) drops 36 cm. Proof: the read-out's `reference soles` go from 34 to −2 (check −5..+5); mutation: restore `Pos.Z`
(48, fails). The primitive cube fallback (`GroundZ` −75 against `Pos.Z` 50) is left as it is (art-free only).

**Why DEFER here:** it moves every robot's drawing, the docked robot's included, and replaces the director's 2026-08-18 plant calibration
("shadows now root at the boots"); a look change for the director. Recommendation: take it with M1b.

---

## 5. M2. The convoy drawn with the colony's cargo rover kit (GO)

### 5.1 The kit

- **Mesh:** `/Game/RedHope/Art/Models2/Rover/Rover/StaticMeshes/Rover.Rover` (`RHAgentVisualizerSubsystem.cpp:28`, `RoverMeshPath`), one
  material slot `MI_Rover` (an instance of `/Game/RedHope/Art/M_RH_Master`: BaseColor, MetallicRoughness, `T_Rover_Normal`,
  `T_Rover_EmissiveMask`, `EmissiveColor`/`EmissiveAmount`, `PoweredState`), textures `Rover_BaseColor`, `Rover_MetallicRoughness`.
  Built from code by `scripts/blender/rh_build_rover.py` (C6b, 9.7k tris), imported by `rh_import_hs.py`.
- **Size and pivot** (the built GLB `Martians/gen/hs/Rover/Rover.glb`): X ±248.75, Y ±127.0, Z 0..238.8 cm (to the mast tip): 5.0 × 2.5 ×
  2.4 m, **pivot on the tyre soles, centred, front at +X**. The build script recentres by −0.1125 m in X (`:193`), so its axles
  (`AXLES = (-1.50, -0.05, 1.40)`, `:61`) are at X −161.25, −16.25 and **+128.75 cm**; the bed (`BX0, BX1`, `:65`) runs X −241..77 cm, its
  floor top at **88.8 cm** (ribs to 90.6); the headboard to 202.8 cm.
- **Its beacon:** `beacon_lens` (`:184`) at Blender `(CX0 + 0.28, +0.55)` m, 1.73-1.86 m up; in the GLB x 1.05-1.245, height
  1.758-1.888, z −0.655..−0.455 m. Interchange maps glTF z to UE Y (the mapping section 1 confirmed on the walker), so in UE the lens is
  at **(114.75, −55, 175.8-188.8) cm**, on the cab roof's left; the mast is at Y +50.
- **The code that draws a colony rover:** `TrackEntities` `:1252-1279` (one `UStaticMeshComponent`, the kit at scale 1, a MID on slot 0
  with `PoweredState` 1, a tinted slab if the art is missing) and `UpdateRover` `:1510-1572` (at `(Pos.X, Pos.Y, TerrainZ)`, yaw
  `YawDeg + rh.RoverYawOffsetDeg` (default 0, `:47-51`), up to three cube crates on the bed at `BedCentreCm −82`, `BedTopCm 86`).
  The colony visualizer's own pattern for a true-scale kit is `SpawnSupplyShip` `:3536-3567` (`SpawnActor<AStaticMeshActor>`, Movable,
  `NoCollision`, `CreateAndSetMaterialInstanceDynamic(0)`, `PoweredState` 1, a cube fallback).

### 5.2 Current code (HEAD)

`RHColonyVisualizerSubsystem.cpp:3463-3473`:
```cpp
namespace
{
	// The supply ship's landing site (director's pick 2026-10-17, "Ship C"):
	// the east pad, where the gray-box freighter used to stand.
	const FVector ShipPadCm(4000.f, -4000.f, 0.f);
	constexpr float ShipDropCm = 6000.f;        // where the descent starts / the climb ends
	constexpr double ShipBurnRealSeconds = 8.0; // descent and lift-off, real time at any speed
	constexpr double ShipStaySols = 1.0;        // on the pad, then it leaves
	const TCHAR* SupplyShipPath = TEXT("/Game/RedHope/Art/Models2/SupplyShip/SupplyShip/StaticMeshes/SupplyShip.SupplyShip");
	const TCHAR* LandingPadPath = TEXT("/Game/RedHope/Art/Models2/LandingPad/LandingPad/StaticMeshes/LandingPad.LandingPad");
}
```

`URHColonyVisualizerSubsystem::RefreshSovereigntyVisuals` (called every `Tick`, `:3968`), `:5074-5153`:
```cpp
	for (const FRHConvoy& Convoy : Sim->GetConvoys())
	{
		const FRHRivalRow* Row = Defs->GetRival(Convoy.Rival);
		if (!Row)
		{
			continue;
		}
		TObjectPtr<AStaticMeshActor>* FoundRover = ConvoyVisuals.Find(Convoy.Id);
		AStaticMeshActor* Rover = FoundRover ? FoundRover->Get() : nullptr;
		if (!Rover)
		{
			// REFERENCE (Robots/ConstructionDrone + Ships/ScoutSpeeder language):
			// bone-white hull, dark tinted cab glass, six dark wheels, a hazard
			// stripe, an amber running beacon, and the cargo pack on the back.
			// Root spawns at UNIFORM scale so relative children don't smear (the
			// old absolute-space label was left behind at the spawn point while
			// the rover drove away - all parts ride relative now).
			Rover = SpawnBox(FVector(0, 0, 60.f), FVector(1.f, 1.f, 1.f), BoneWhite, FLinearColor::Black);
			if (!Rover)
			{
				continue;
			}
#if WITH_EDITOR
			Rover->SetActorLabel(FString::Printf(TEXT("Sim_Convoy_%d"), Convoy.Id));
#endif
			UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
			UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
			const auto Part = [&](UStaticMesh* Shape, const FVector& RelCm, const FRotator& Rot, const FVector& Scale,
				const FLinearColor& Color, const FLinearColor& Emissive = FLinearColor::Black)
			{
				if (!Shape) { return; }
				UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Rover);
				Mesh->SetupAttachment(Rover->GetRootComponent());
				Mesh->RegisterComponent();
				Mesh->SetStaticMesh(Shape);
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Mesh->SetRelativeLocationAndRotation(RelCm, Rot);
				Mesh->SetRelativeScale3D(Scale);
				ApplyTint(Mesh, Color, Emissive);
			};
			Part(CubeMesh, FVector(0, 0, 10.f), FRotator::ZeroRotator, FVector(2.6f, 1.5f, 0.9f), BoneWhite);                    // hull
			Part(CubeMesh, FVector(108.f, 0, 42.f), FRotator(-16.f, 0, 0), FVector(0.7f, 1.25f, 0.62f),
				FLinearColor(0.03f, 0.05f, 0.07f), FLinearColor(0.05f, 0.12f, 0.15f));                                          // cab glass
			Part(CubeMesh, FVector(-72.f, 0, 58.f), FRotator::ZeroRotator, FVector(1.1f, 1.3f, 0.72f), DarkSlate);              // cargo pack
			Part(CubeMesh, FVector(0, 79.f, 4.f), FRotator::ZeroRotator, FVector(2.45f, 0.05f, 0.2f), HazYellow);               // stripe R
			Part(CubeMesh, FVector(0, -79.f, 4.f), FRotator::ZeroRotator, FVector(2.45f, 0.05f, 0.2f), HazYellow);              // stripe L
			for (int32 W = 0; W < 6; ++W)
			{
				const float Wx = (W % 3 - 1) * 92.f;
				const float Wy = (W < 3 ? 1.f : -1.f) * 82.f;
				Part(CylMesh, FVector(Wx, Wy, -38.f), FRotator(0, 0, 90.f), FVector(0.56f, 0.56f, 0.2f), FLinearColor(0.05f, 0.05f, 0.06f));
			}
			Part(SphereMesh, FVector(128.f, 0, 82.f), FRotator::ZeroRotator, FVector(0.16f), HazYellow, AmberGlow);             // beacon
			ConvoyVisuals.Add(Convoy.Id, Rover);
		}
		// Home = the Lander pad (E7: the colony's depot, by the sim's one depot rule; the origin with none); destination = the rival's marker.
		const FVector Home = Sim->GetColonyDepotLocationCm(Sim->GetColonyRegionId());
		const FVector Dest = RivalMarkerPos(Convoy.Rival, Row->DistanceKm);
		const double P = Sim->GetConvoyProgress(Convoy); // 0..1 round trip
		const double Alpha = Convoy.bReturning
			? FMath::Clamp((P - 0.5) * 2.0, 0.0, 1.0)   // marker -> home
			: FMath::Clamp(P * 2.0, 0.0, 1.0);          // home -> marker
		const FVector From = Convoy.bReturning ? Dest : Home;
		const FVector To = Convoy.bReturning ? Home : Dest;
		// Wheels on the ground the whole road: the straight-line lerp gets
		// its Z from the terrain under each step, not the endpoints. E8 (Zarya
		// (a)): the visible surface, as its route's ribbon, so the rover never
		// drives inside a plateau its ribbon now runs over.
		FVector Loc = FMath::Lerp(From, To, Alpha);
		Loc.Z = RHMarsTerrain::SceneryTopZCm(Loc.X, Loc.Y) + 60.f;
		Rover->SetActorLocation(Loc);
		// The cab leads: face the rover down its road (X+ = nose).
		const FVector Travel = (To - From).GetSafeNormal2D();
		if (!Travel.IsNearlyZero())
		{
			Rover->SetActorRotation(Travel.Rotation());
		}
		Rover->SetActorHiddenInGame(!bSurface);
	}
```
Today's convoy: 13 static mesh components (`SpawnBox`'s 1 m root cube, `:5156-5170`, plus 12 parts), each with its own `M_Graybox` MID;
about 2.8 × 1.8 × 1.5 m; the root at the visible surface + 60 cm; the beacon a 16 cm sphere 142 cm up at the nose.

### 5.3 The exact change

**(a) `RHColonyVisualizerSubsystem.cpp`, the namespace at `:3463-3473`: append before its closing `}` (after `:3472`):**
```cpp
	// P2 M2: the trade convoy's rover is the colony's own cargo rover kit, the mesh RHAgentVisualizerSubsystem draws a road rover with
	// (its RoverMeshPath): scripts/blender/rh_build_rover.py, true scale 5.0 x 2.5 x 2.4 m, pivot on the tyre soles, front at +X.
	const TCHAR* ConvoyRoverPath = TEXT("/Game/RedHope/Art/Models2/Rover/Rover/StaticMeshes/Rover.Rover");
	const FName ConvoyKitTag(TEXT("RH.ConvoyKit"));
	constexpr float ConvoyFrontAxleCm = 128.75f; // the kit's front axle ahead of its pivot (AXLES +1.40 m, recentred by -0.1125 m)
	constexpr float ConvoyRearAxleCm = 161.25f;  // its rear axle behind the pivot (AXLES -1.50 m)
	constexpr float ConvoyMaxPitchDeg = 30.f;    // steeper ground than this lifts one end rather than tilting further
```

**(b) `RefreshSovereigntyVisuals`: replace the spawn block `:5083-5129` (from `if (!Rover)` through its closing `}`) with:**
```cpp
		if (!Rover)
		{
			// P2 M2: the colony's cargo rover kit (ConvoyRoverPath) at true scale on its own material, lit as a road rover is (PoweredState 1),
			// with the convoy's amber running beacon round the kit's beacon lens and its cargo pack on the kit's empty bed. Missing art keeps
			// the primitive rover below, so the beat still plays.
			UStaticMesh* Kit = LoadObject<UStaticMesh>(nullptr, ConvoyRoverPath);
			if (Kit)
			{
				Rover = World->SpawnActor<AStaticMeshActor>(FVector::ZeroVector, FRotator::ZeroRotator);
				if (!Rover)
				{
					continue;
				}
				UStaticMeshComponent* Body = Rover->GetStaticMeshComponent();
				Body->SetMobility(EComponentMobility::Movable);
				Body->SetStaticMesh(Kit);
				Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				if (UMaterialInstanceDynamic* Mid = Body->CreateAndSetMaterialInstanceDynamic(0))
				{
					Mid->SetScalarParameterValue(FName("PoweredState"), 1.f);
				}
				Rover->Tags.Add(ConvoyKitTag);
			}
			else
			{
				// REFERENCE (Robots/ConstructionDrone + Ships/ScoutSpeeder language):
				// bone-white hull, dark tinted cab glass, six dark wheels, a hazard
				// stripe, an amber running beacon, and the cargo pack on the back.
				// Root spawns at UNIFORM scale so relative children don't smear (the
				// old absolute-space label was left behind at the spawn point while
				// the rover drove away - all parts ride relative now).
				Rover = SpawnBox(FVector(0, 0, 60.f), FVector(1.f, 1.f, 1.f), BoneWhite, FLinearColor::Black);
				if (!Rover)
				{
					continue;
				}
			}
#if WITH_EDITOR
			Rover->SetActorLabel(FString::Printf(TEXT("Sim_Convoy_%d"), Convoy.Id));
#endif
			UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
			UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
			const auto Part = [&](UStaticMesh* Shape, const FVector& RelCm, const FRotator& Rot, const FVector& Scale,
				const FLinearColor& Color, const FLinearColor& Emissive = FLinearColor::Black)
			{
				if (!Shape) { return; }
				UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Rover);
				Mesh->SetupAttachment(Rover->GetRootComponent());
				Mesh->RegisterComponent();
				Mesh->SetStaticMesh(Shape);
				Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Mesh->SetRelativeLocationAndRotation(RelCm, Rot);
				Mesh->SetRelativeScale3D(Scale);
				ApplyTint(Mesh, Color, Emissive);
			};
			if (Kit)
			{
				// The pack on the bed (floor top 88.8 cm; centred at a road rover's BedCentreCm), the beacon round the kit's own lens on the
				// cab roof (rh_build_rover.py beacon_lens: Blender (CX0 + 0.28, +0.55) m, 1.73-1.86 m up; UE (114.75, -55) cm).
				Part(CubeMesh, FVector(-82.f, 0.f, 126.f), FRotator::ZeroRotator, FVector(1.1f, 1.3f, 0.72f), DarkSlate);       // cargo pack
				Part(SphereMesh, FVector(115.f, -55.f, 182.f), FRotator::ZeroRotator, FVector(0.18f), HazYellow, AmberGlow);    // beacon
			}
			else
			{
				Part(CubeMesh, FVector(0, 0, 10.f), FRotator::ZeroRotator, FVector(2.6f, 1.5f, 0.9f), BoneWhite);                    // hull
				Part(CubeMesh, FVector(108.f, 0, 42.f), FRotator(-16.f, 0, 0), FVector(0.7f, 1.25f, 0.62f),
					FLinearColor(0.03f, 0.05f, 0.07f), FLinearColor(0.05f, 0.12f, 0.15f));                                          // cab glass
				Part(CubeMesh, FVector(-72.f, 0, 58.f), FRotator::ZeroRotator, FVector(1.1f, 1.3f, 0.72f), DarkSlate);              // cargo pack
				Part(CubeMesh, FVector(0, 79.f, 4.f), FRotator::ZeroRotator, FVector(2.45f, 0.05f, 0.2f), HazYellow);               // stripe R
				Part(CubeMesh, FVector(0, -79.f, 4.f), FRotator::ZeroRotator, FVector(2.45f, 0.05f, 0.2f), HazYellow);              // stripe L
				for (int32 W = 0; W < 6; ++W)
				{
					const float Wx = (W % 3 - 1) * 92.f;
					const float Wy = (W < 3 ? 1.f : -1.f) * 82.f;
					Part(CylMesh, FVector(Wx, Wy, -38.f), FRotator(0, 0, 90.f), FVector(0.56f, 0.56f, 0.2f), FLinearColor(0.05f, 0.05f, 0.06f));
				}
				Part(SphereMesh, FVector(128.f, 0, 82.f), FRotator::ZeroRotator, FVector(0.16f), HazYellow, AmberGlow);             // beacon
			}
			UE_LOG(LogRedHope, Display, TEXT("[Convoy] #%d to %s drawn as %s"), Convoy.Id, *Convoy.Rival.ToString(),
				Kit ? TEXT("the cargo rover kit (Rover, true scale)") : TEXT("the primitive rover (no Rover kit)"));
			ConvoyVisuals.Add(Convoy.Id, Rover);
		}
```
(`World` is `RefreshSovereigntyVisuals`' own local, `:4986`; `DarkSlate`, `HazYellow`, `AmberGlow`, `BoneWhite` come from
`using namespace RHCanon;`, `:4993`.)

**(c) The placement: replace `:5143-5151`** (from `FVector Loc = FMath::Lerp(From, To, Alpha);` through the closing `}` of the
`if (!Travel.IsNearlyZero())`) **with:**
```cpp
		FVector Loc = FMath::Lerp(From, To, Alpha);
		const FVector Travel = (To - From).GetSafeNormal2D();
		if (Rover->ActorHasTag(ConvoyKitTag) && !Travel.IsNearlyZero())
		{
			// P2 M2: the kit's pivot is on its tyre soles. It stands on the visible surface under its front and rear axles, pitched to them
			// (at most ConvoyMaxPitchDeg), and is lifted so neither axle nor its middle sinks into that surface.
			const float Zf = RHMarsTerrain::SceneryTopZCm(Loc.X + Travel.X * ConvoyFrontAxleCm, Loc.Y + Travel.Y * ConvoyFrontAxleCm);
			const float Zr = RHMarsTerrain::SceneryTopZCm(Loc.X - Travel.X * ConvoyRearAxleCm, Loc.Y - Travel.Y * ConvoyRearAxleCm);
			const float Zc = RHMarsTerrain::SceneryTopZCm(Loc.X, Loc.Y);
			const float PitchDeg = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Zf - Zr, ConvoyFrontAxleCm + ConvoyRearAxleCm)),
				-ConvoyMaxPitchDeg, ConvoyMaxPitchDeg);
			const float TanPitch = FMath::Tan(FMath::DegreesToRadians(PitchDeg));
			Loc.Z = FMath::Max3(Zc, Zf - ConvoyFrontAxleCm * TanPitch, Zr + ConvoyRearAxleCm * TanPitch);
			// The cab leads, as a road rover's does (rh.RoverYawOffsetDeg calibrates the kit's import axis for both).
			static const auto* RoverYawOff = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("rh.RoverYawOffsetDeg"));
			Rover->SetActorLocationAndRotation(Loc, FRotator(PitchDeg, Travel.Rotation().Yaw + (RoverYawOff ? RoverYawOff->GetValueOnGameThread() : 0.f), 0.f));
		}
		else
		{
			Loc.Z = RHMarsTerrain::SceneryTopZCm(Loc.X, Loc.Y) + 60.f;
			Rover->SetActorLocation(Loc);
			// The cab leads: face the rover down its road (X+ = nose).
			if (!Travel.IsNearlyZero())
			{
				Rover->SetActorRotation(Travel.Rotation());
			}
		}
```
`:5152` (`Rover->SetActorHiddenInGame(!bSurface);`) stays. The comment at `:5139-5142` ("Wheels on the ground the whole road ...") stays
above `Loc`. `HAL/IConsoleManager.h` is included (`:31`); `rh.RoverYawOffsetDeg` is registered by the same module (`RHAgentVisualizerSubsystem.cpp:47`).

On flat ground (the colony flat, most of the 236 m to Zarya): `Zf = Zr = Zc`, pitch 0, the soles on the surface. A slope up to 30°: both
axles on it. Steeper (Zarya's plateau flank): pitch 30° and the higher axle on the surface, the other above it, never in it.

**(d) The comment at `:5060-5061`** gains: "P2 M2: drawn with the colony's cargo rover kit (the primitive rover only with the art missing)."

### 5.4 The rival's tint: none, by decision

`FRHRivalRow` (`RHRows.h:398-426`) has no colour field, and no presentation code keys a colour on a rival: the settlement markers are
tinted by diplomatic state (`:5026-5041`), the Routes overlay by route status (`RHTradeVisualizerSubsystem`). The convoy is the colony's
own vehicle out to a rival and back (`RH.Convoy`: "dispatch the rover trade convoy to a rival"; `MaxConvoysOut` in `RH_Config.csv`:
"Cargo rovers drive convoys"), so it wears the kit's own colony livery (`MI_Rover`, `PoweredState` 1), like a road rover. A rival
colour would be a new look decision for the director; none is invented here.

### 5.5 Kept: label, beacon, movement

- **Label:** at HEAD the convoy has no world label: the "Convoy" `AddLabel` was removed in `05ea5b0` (2026-07-07; the comment at
  `:5088-5090` remembers it). What remains, the editor-only actor label `Sim_Convoy_<Id>` (`:5096-5098`), is kept as is.
- **Beacon:** the same amber sphere (`HazYellow`, `AmberGlow`), now 18 cm and round the kit's own beacon lens (the lens is 14 cm across,
  13 cm tall), 182 cm up on the cab roof (it was 16 cm at 142 cm, at the box's nose). The kit's lens, light bar, headlights and tail
  lamps glow as well (its emissive mask, `PoweredState` 1).
- **Movement:** unchanged in XY and heading: the same `Home`/`Dest`/`Alpha` lerp, the same facing down the route (plus
  `rh.RoverYawOffsetDeg`, 0 by default, as the road rover); Z from the same visible surface (`SceneryTopZCm`), now at the soles (the
  kit's pivot) instead of +60 cm, with the pitch of 5.3 (c). Hidden in the underground view as before.

### 5.6 Proof, mutation, captures

**Check `M2 convoy kit`**: the after `cap_convoy` log has `[Convoy] #1 to Zarya drawn as the cargo rover kit (Rover, true scale)` and no
`drawn as the primitive rover` line. The captures show the kit (the before shows the box rover).

**Mutation `p2_mut_m2`:** in `ConvoyRoverPath`, change `Rover.Rover` to `Rover.RoverX` (a missing asset): the convoy spawns as the
primitive rover, the log says `drawn as the primitive rover (no Rover kit)`, `M2 convoy kit` FAILS; nothing else reads the convoy's mesh
(the click test's market steps read the deck and `GetConvoys()`, `RHClickTest.cpp:2198-2214`).

**`cap_convoy.cmds`** (a fresh boot, as E8's gate captures; `wip-backups/e8-r2/cap/gatecap.sh:24-25`), verbatim:
```
r.setres 1440x860w, RH.ClearTips, RH.SetSpeed 1, RH.Lag 5, RH.Market grant 10000, RH.AddStock Hydrogen 200, RH.AddStock SpareParts 20, RH.Market order Zarya Ice:100, RH.After 3 RH.SetSpeed 60, RH.After 8 RH.SetSpeed 0, RH.After 8.5 RH.Market, RH.After 8.5 RH.Trade markers, RH.After 9 RH.Cam 12.9 26.3 0.12 0 0, RH.After 10 RH.ClearTips, RH.After 12 RH.Snapshot 0.5, RH.After 14 RH.Cam 12.9 26.3 0.02 0 0, RH.After 16 RH.Snapshot 0.5, RH.After 18 RH.Cam 12.9 26.3 0.02 25 0, RH.After 20 RH.Snapshot 0.5, RH.After 22 RH.Cam 12.9 26.3 0.02 0 334, RH.After 24 RH.Snapshot 0.5, RH.After 26 RH.Market
```
4 snapshots: `p2_before_convoy_00..03`, `p2_after_convoy_00..03` (scene name `convoy`).

Where the convoy is: the order leaves after the 5 sim-s uplink lag, about 0.04 s into the 60× burst, and the sim then pauses at 8 s.
Zarya's leg is 120 km / 60 km a sol (`ConvoySpeedKmPerSol`) = 2 sols = 2400 sim-s; its marker is at (103.5, 212.1) m, 236 m out at 64°
(`design-decisions.md`, E8 Zarya (a)); 1 sol = 1200 sim-s. So 4.96 s × 60 = 298 sim-s → 29.3 m from the depot (the Lander at (0, 0)) →
**(12.85, 26.33) m**. E8's own run agrees (`e8gO.log:335`: "convoy #1 to Zarya: outbound 7%" at 25 s = 31 m after 17 s more at 1×).
`RH.Market` at 8.5 s should print `outbound 6%` (5-7 %); with any other figure P, the focus is (103.5 × 2P, 212.1 × 2P) m, set in the one
file before either run. The boot is at sol 0 50 % (noon, `e8gO.log:300`).

| NN | Camera | Shows |
|---|---|---|
| 00 | `12.9 26.3 0.12 0 0` (44 m out, pitch 36.5°) | the convoy on its route out of the colony: before about 80 px long, after about 160 px |
| 01 | `12.9 26.3 0.02 0 0` (27.5 m, pitch 31°) | the close frame: the box rover against the kit, the bed with the pack |
| 02 | `12.9 26.3 0.02 25 0` (pitch 56°) | from above: the bed, the pack, the beacon on the cab roof |
| 03 | `12.9 26.3 0.02 0 334` (looking at the convoy's left side) | the profile; **the amber sphere round the short lens on the cab roof's left** |

Frame 03 settles the one assumption (the axis mapping that puts the lens at Y −55): the sphere must sit round the short amber lens, not
the thin mast on the roof's other side. If it sits on the mast, the single correction is `-55.f` → `55.f` in the beacon's `Part` call.

### 5.7 Risk

- **Scale:** the kit at true scale reads about 1.8× the old rover (5.0 × 2.5 × 2.4 m against 2.8 × 1.8 × 1.5), the size of a colony road
  rover; at the depot it overlaps the Lander's model a little longer (the first and last 2.5 m of each round trip).
- **Collision:** the kit's body is `NoCollision` (the old root cube kept `AStaticMeshActor`'s default); every pick in the game uses
  deprojection, not a trace (`RHPlayerController.cpp:281`, `RHColonyVisualizerSubsystem.cpp:3881`), so nothing reads it; `leak.total`
  counts world clicks, not actors.
- **Frame cost:** per convoy out, per frame, two more `SceneryTopZCm` queries (each tests one 10 m cell's triangles) and one
  `SetActorLocationAndRotation` instead of `SetActorLocation` + `SetActorRotation`; components per convoy 13 → 3 (fewer transforms and
  draw calls), triangles about +8k (the kit's 9.7k against the primitives). At most `MaxConvoysOut` = 3 convoys. No allocation added (the
  per-frame `TSet<int32> Live` at `:5063` is HEAD's). Not in the `[RobotRoute]` median (another subsystem).
- **Actor / HISM counts:** one actor per convoy out, as today; no HISM.
- **The plateau:** on the last stretch to Zarya (the marker stands 61 m above the height field on a flank, about 22 m inside a peak's
  base outline: E8's probe) the kit tilts 30° and rides with its trailing axle above the surface; today's box clipped through the same
  flank. Brief (the last ~9 % of each leg) and small at the usual zoom.
- **Other P2 items:** no other design edits this block or the namespace (walks, cache and clicktest leave `RHColonyVisualizerSubsystem.cpp`
  alone; the deferred design's shuttles live in `RHTrafficVisualizerSubsystem.cpp`).

---

## 6. Build order and the checks, in one place

1. M1 (2.3 a-e), then M2 (5.3 a-d). They share no file.
2. Checks: `M1 cargo on the back` (2.5) on `p2_after_cargo`'s log; `M2 convoy kit` (5.6) on `p2_after_convoy`'s log.
3. Mutations: `p2_mut_m1` (2.6), `p2_mut_m2` (5.6), each one line, each failing only its check.
4. Captures: `cap_cargo.cmds` (16 shots) and `cap_convoy.cmds` (4 shots), before and after, same files.
5. Click test: no step added or changed (`click_steps_added` 0 for every item); the deepbench scene is untouched.
6. Battery: no line should move (display only).

M1b and M1c are not built. Their read-out numbers are in M1's line for the record (pelvis 91 cm, soles 34 cm), and the presentation
review of ruling 28 should be told that every robot body is drawn 90.6 cm ahead of its drawn spot (the note at the top).
