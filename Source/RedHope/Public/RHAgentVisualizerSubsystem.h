#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "Mass/EntityHandle.h"
#include "RHPathRounding.h"
#include "RHAgentVisualizerSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
struct FRHTaskFragment;
struct FMassEntityManager;

// Scaffold presentation for sim agents: instanced meshes, transforms copied
// from Mass fragments each frame. Deliberately dumb - it proves the seam (sim
// knows nothing about it) and is what the stress test measures as
// "presentation cost". MassRepresentation replaces it in M0+.
//
// Reference (Robots/Humanoid, "reminiscent of a Tesla Bot"): each unit is a
// BIPEDAL HUMANOID composed from nine instanced layers - white armored torso /
// head / legs over matte-black joints and arms, a dark visor with a cyan
// status bar. Legs and arms swing in a procedural walk cycle driven by each
// entity's own ground speed (phase accumulates with distance covered), and the
// figure faces its direction of travel. Still zero per-actor cost: N robots =
// 9 ISMs total, one instance each.
UCLASS()
class REDHOPE_API URHAgentVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHAgentVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	void TrackEntities(const TArray<FMassEntityHandle>& Entities);
	// Load path: forget every tracked entity and instance; the reload's
	// OnRobotsSpawned broadcasts repopulate from scratch.
	void ResetTracking();
	// Sliced-floor view (M1-d): robots are surface actors until later gates
	// send them below; the whole ISM layer hides when the elevator descends.
	void SetSliceHidden(bool bHidden);
	// D4: the viewed floor. Each robot is drawn only on its own floor (its sim Z), so with no lift car - every robot on the
	// surface - this is SetSliceHidden(Level != 0) exactly. Rovers never leave the surface.
	void SetViewLevel(int32 Level);
	// D8 (the deep view): while the section is on, the surface layer is hidden (SetSliceHidden(true)) and a robot below ground is drawn
	// iff the section's slab holds it (URHSectionVisualizerSubsystem::IsInSlab), at its real position. The controller's mirror, pushed
	// through the colony visualizer. The positions drawn this frame are kept for the section's per-tunnel robot count (drawn positions
	// from the Mass fragments, not a sim accessor).
	void SetSectionMode(bool bOn);
	const TArray<FVector>& GetSectionDrawnCm() const { return SectionDrawnCm; }
	int32 GetSectionDrawnCount() const { return SectionDrawnCm.Num(); }
	// D7 (QA, RH.AtriumStatus): robots drawn this frame on a floor below the view, through an atrium (0 with none).
	int32 GetShownThroughAtrium() const { return ShownThroughAtrium; }
	int32 GetTrackedCount() const { return Tracked.Num(); } // RH.DeepBench's line: robots the visualizer follows

	// Idle robots look busy (display only; docs/qa/2026-10-28/robot-idle-brief.md). What one tracked robot was drawn doing on the last
	// tick, for RH.IdleRobots and the click test's robots.idle: a surface walker (one AdvanceDrawn draws) busy, waiting in the ring round
	// a Charging Pad (walking to its slot, standing at it, or walking back), standing in the Lander's rank, or inspecting a building.
	enum class EIdleShow : uint8
	{
		NotWalker = 0, Busy, AtPad, LanderRank, Inspecting
	};
	struct FIdleDebug
	{
		EIdleShow Show = EIdleShow::NotWalker;
		int32 PadId = 0;                      // the pad it waits at (0: none)
		int32 Slot = INDEX_NONE;              // its slot in that pad's ring: 0-7 the first ring, 8-19 the second
		bool bRingFull = false;               // its pad's ring had no open slot, so it stands in the Lander's rank
		bool bAtSlot = false;                 // it has reached its slot (the wait before an inspection runs)
		FVector PadCm = FVector::ZeroVector;  // the pad's centre
		float PadHalfCm = 0.f;                // the larger half-axis of the pad's footprint
		float ClearCm = 0.f;                  // PadHalfCm plus a robot's clearance: no waiting figure stands nearer the centre
		float RingCm = 0.f;                   // the first ring's radius (the second is 220 cm further out)
		FVector SlotCm = FVector::ZeroVector;
		FVector GoalCm = FVector::ZeroVector; // where its idle walk heads on this tick
		FVector RankCm = FVector::ZeroVector; // its slot in the Lander's rank (ParkSlotCm)
		FVector DrawnCm = FVector::ZeroVector;
		int32 InspectId = 0;                  // the building it is inspecting
		float NextInspectS = -1.f;            // sim seconds of waiting at its slot before it next inspects (-1: none to inspect, or away)
		float InspectLeftS = 0.f;             // sim seconds of Operate left at the building
		bool bInspecting = false;             // standing at the building playing Operate
	};
	bool GetIdleDebug(int32 Index, FIdleDebug& Out) const;
	// RH.IdleRobots (READ-ONLY): one line per surface walker and one per working pad in the cache.
	void LogIdleRobots() const;

private:
	void EnsureMeshComponent();

	// One ISM per body part; every tracked entity owns instance i in each.
	enum class EPart : uint8
	{
		Torso = 0, Pelvis, Head, Visor, Pack, LegL, LegR, ArmL, ArmR, COUNT
	};
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
	// Rigged robot walkers (animation phase): when RH_Walker_robot exists each
	// tracked entity gets ONE skeletal component instead of ISM instances -
	// real clips driven by ground speed + task phase (Walk/Idle plus the
	// per-task work poses: Dig/Haul/Operate/Charge/Repair). Fleet is small
	// (<20), so per-robot skeletal cost is fine; the 9-ISM cube figure stays
	// as the art-free fallback.
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> SkelBodies;
	// Suffix of the clip each robot is currently playing ("Idle", "Walk", ...).
	TArray<FName> PlayingClip;
	// Per-walker Z lift that puts the MESH'S OWN feet on the ground, derived
	// from its bounds at spawn. The old constant GroundZ (-75) assumed the
	// primitive stand-in's mid-body origin; the rigged GLBs are grounded at
	// their feet, so that constant buried every robot 75 cm - the director's
	// "stuck halfway underground".
	TArray<float> FeetLiftCm;
	// One small regolith-lump component per skeletal robot (professor-gated
	// 2026-08-18): visible while the robot's FRHTaskFragment carries CargoKg,
	// scaled by load fraction. Read from the PUBLIC Mass fragments - no sim
	// accessor, no sim recompile.
	TArray<TWeakObjectPtr<UStaticMeshComponent>> CargoLumps;
	bool bUseSkeletal = false;
	// C6b: a cargo rover (RobotClass Rover, the RC_R row) is a truck, not a walker: one static mesh (the
	// code-built Rover kit model, scripts/blender/rh_build_rover.py) driven along the sim's road path by
	// the same arc-length drawing the walkers use, plus up to three crates on its bed while the task
	// fragment carries CargoKg. Parallel to Tracked; Body is null for every other robot. Reads only the
	// public Mass fragments, never writes the sim.
	// Road legs of EVERY robot are driven this way (a walker with a smaller radius and a quicker pivot).
	// A rover turns like a heavy vehicle: its drawn path is the sim's waypoints with every corner rounded to a
	// fillet, its heading is always the tangent of that path at its drawn position (it cannot slide), it eases
	// down into tight turns within a bounded lag behind the sim's own arc-length progress, and pivots slowly
	// on the spot when a trip starts facing the wrong way.
	struct FRoverDrive
	{
		bool bInit = false;
		FVector2D Pos = FVector2D::ZeroVector;
		float Yaw = 0.f;
		bool bTrip = false, bRoadTrip = false;
		int32 Serial = -1;
		FVector2D Target = FVector2D::ZeroVector;
		float SimStartDist = 1.f;
		RHPath::FRounded Path;
		TArray<int32> LegVertex;   // raw vertex index at the end of each sim leg
		TArray<float> LegSimEnd;   // cumulative sim length at the end of each leg
		float DrawS = 0.f, SimS = 0.f;
	};
	struct FRoverVis
	{
		FRoverDrive Drive;
		TWeakObjectPtr<UStaticMeshComponent> Body;
		TWeakObjectPtr<UStaticMeshComponent> Crates[3];
		FName CrateResource;
		int32 CratesShown = 0;
		double NextLogS = 0.0; // rh.RoverTurnLog
		FVector2D LogPos = FVector2D::ZeroVector;
		bool bLogPos = false;
	};
	TArray<FRoverVis> RoverVis;
	bool IsRoverIndex(int32 i) const;
	int32 FirstRoverIndex() const;
	FVector AdvanceRover(int32 i, const FVector& SimPos, float SimSpeed, const FRHTaskFragment* Task, float DeltaTime, float& OutYawDeg, bool bHeavy);
	void UpdateRover(int32 i, const FVector& Pos, float YawDeg, float TerrainZ, const FRHTaskFragment* Task, float CapKg);
	TArray<FMassEntityHandle> Tracked;
	// Per-entity gait state, parallel to Tracked: last ground position (for
	// speed), smoothed facing yaw, and the accumulated stride phase.
	TArray<FVector> LastPosCm;
	TArray<float> FacingYawDeg;
	TArray<float> StridePhase;
	bool bSliceHidden = false;
	int32 ViewLevel = 0;
	bool bSectionMode = false;
	TArray<FVector> SectionDrawnCm;
	int32 ShownThroughAtrium = 0;
	// D4: a cab box around each robot riding a lift car, pooled (hidden when unused). Presentation only.
	TArray<TWeakObjectPtr<UStaticMeshComponent>> CabBoxes;
	UStaticMeshComponent* GetCabBox(int32 Index);

	// Alive pass 2 (c), presentation only: where a robot is DRAWN. The sim
	// walks robots in straight lines (and stays the authority on where they
	// are and when they arrive); the drawn figure walks the same trip along a
	// detour around building footprints, at the same progress fraction, so
	// it never cuts through a building and still arrives on time. Starts and
	// stops are eased, turns are smoothed, and a robot left idle walks to a
	// slot in the ring round its Charging Pad (beside the Lander with no
	// working pad) until its next task, inspecting nearby buildings meanwhile.
	struct FRobotTrip
	{
		bool bInit = false;
		FVector DrawCm = FVector::ZeroVector;       // where the figure is drawn
		bool bTrip = false;
		FVector TripTargetCm = FVector::ZeroVector; // the sim's target for this trip
		float SimStartDistCm = 0.f;                 // sim distance to it at the start
		TArray<FVector> Path;                       // drawn route, DrawCm at start -> target
		float PathLenCm = 0.f;
		FVector LastSyncedCm = FVector::ZeroVector; // where the synced trip left it
		// C4: a trip along the sim's road waypoints. Path is then the drawn
		// polyline for the WHOLE sim path (detours on the off-road legs, the exact
		// waypoints on the road legs), and progress is ARC LENGTH along the sim's
		// path mapped leg by leg onto it - never the straight-distance fraction.
		bool bRoadTrip = false;
		int32 RoadSerial = -1;                      // the sim path this was planned from
		TArray<float> LegSimEndCm;                  // cumulative sim length at the end of each leg
		TArray<float> LegDrawEndCm;                 // cumulative drawn length at the end of each leg
		double IdleS = 0.0;                         // sim-paced time idle (parking)
		// Free walks (not tied to sim progress): to a parking slot, or back to
		// the sim's position when a task begins where the robot already is.
		bool bFree = false;
		FVector FreeGoalCm = FVector::ZeroVector;
		TArray<FVector> FreePath;
		float FreeLenCm = 0.f;
		float FreeDoneCm = 0.f;
		// Idle robots look busy (display only): the pad this idle figure waits at (0: the Lander's rank) and its slot in that pad's
		// ring, chosen from the sim position against IdlePads; the inspection round; and what Tick reads, valid only on the tick in
		// IdleTick (a figure drawn some other way since - riding, below, on a road leg - holds nothing).
		uint32 WalkTick = 0;                        // the last tick AdvanceDrawn drew it (a surface walker)
		uint32 IdleTick = 0;                        // the last tick it was drawn idle
		uint8 IdleShow = 0;                         // EIdleShow on that tick
		uint32 PadChoiceSerial = 0;                 // the pad cache (IdlePadsSerial) it last chose against
		int32 IdlePadId = 0;
		int32 IdleSlot = INDEX_NONE;
		bool bRingFull = false;
		FVector IdleRankCm = FVector::ZeroVector;   // ParkSlotCm, chosen with the pad
		FVector IdleGoalCm = FVector::ZeroVector;   // where its idle walk heads
		uint8 IdlePhase = 0;                        // 0 to or at its slot, 1 walking to a building, 2 at the building, 3 walking back
		bool bAtSlot = false;
		double IdleWaitS = 0.0;                     // sim-paced seconds stood at its slot
		double IdleWaitForS = 0.0;                  // the wait before its next inspection
		int32 InspectCount = 0;                     // inspections since it went idle
		int32 InspectId = 0;
		FVector InspectAtCm = FVector::ZeroVector;  // where it stands to inspect
		FVector InspectFaceCm = FVector::ZeroVector; // the building's centre, which it faces
		double InspectS = 0.0;                      // sim-paced seconds of Operate so far
		bool bIdleFace = false;                     // standing at its slot or the building: Tick turns it to IdleYawDeg
		float IdleYawDeg = 0.f;
		bool bInspecting = false;                   // at the building: Tick plays Operate
	};
	TArray<FRobotTrip> Trips;
	uint32 TickSerial = 0; // this subsystem's ticks: what a figure was drawn doing on the last one
	// Idle robots look busy: the working surface Charging Pads (the ones FindNearestChargePad docks at), each with its ring of 20 waiting
	// slots and the buildings near it to inspect. Rebuilt once a second of real time, or at once when the building count, a surface pad's
	// state or the set of surface sites changes; a rebuild bumps IdlePadsSerial and every waiting figure chooses its pad again.
	struct FIdlePad
	{
		FVector CentreCm = FVector::ZeroVector;
		float HalfCm = 0.f;
		float RingCm = 0.f;
		TArray<FVector> Slots;         // the first ring's 8, then the second ring's 12
		TArray<bool> SlotOpen;         // false: inside a footprint, skipped
		TArray<int32> Inspect;         // the buildings near it, ascending Id
		TArray<FVector> InspectAtCm;   // where a figure stands to inspect each
		TArray<FVector> InspectFaceCm; // each one's centre
		TArray<int32> InspectDropped;  // in range, but left out: the point beside it falls inside a footprint's box (RH.IdleRobots)
		// Where the robots docked at this pad stand (the sim's Charge task, docked: its sim position), refreshed every tick: no figure waits
		// at a slot within IdleDockClearCm of one, so a waiting figure never stands in line behind the charging robot.
		TArray<FVector> DockedCm;
		bool NearDock(int32 Slot) const;
	};
	TMap<int32, FIdlePad> IdlePads;
	uint32 IdlePadsKey = 0;
	uint32 IdlePadsSerial = 0;
	double NextIdlePadsS = 0.0;
	void RefreshIdlePads();
	void GatherDocked(const FMassEntityManager& EntityManager);
	void ChooseIdlePad(int32 i, FRobotTrip& T, const FVector& SimPos);
	static void ResetIdle(FRobotTrip& T);
	TArray<FVector> SimLastPosCm; // the sim's position last frame (its own speed)
	double NextRouteLogS = 0.0;   // rh.RobotRouteLog's next report
	// Idle robots (gate tool): this subsystem's own Tick durations (ms) since the last rh.RobotRouteLog line, for that line's median.
	TArray<double> RouteLogTickMs;
	// Surface building footprints (expanded by a robot's clearance), rebuilt
	// each tick; robots route around them.
	TArray<FBox2D> Obstacles;
	void RebuildObstacles();
	// Shortest route From -> To around Obstacles (a visibility graph over the
	// expanded footprints' corners); footprints containing either end are
	// the trip's own building and are walked into. Straight when clear.
	void PlanRoute(const FVector& From, const FVector& To, TArray<FVector>& OutPath) const;
	bool SegmentBlocked(const FVector2D& A, const FVector2D& B, const TArray<int32>& Candidates) const;
	// The Nth parking slot beside the Lander (clear of footprints): the Lander's rank, for an idle robot with no working pad.
	FVector ParkSlotCm(int32 Index) const;
	// The figure's drawn position this frame (advances its trip state).
	FVector AdvanceDrawn(int32 i, const FVector& SimPosCm, float SimSpeedCmS, const FRHTaskFragment* Task, float DeltaTime);
};
