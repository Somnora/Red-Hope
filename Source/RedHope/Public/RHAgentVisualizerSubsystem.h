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

	// Alive pass 2 (c), presentation only: where a robot is DRAWN. The sim
	// walks robots in straight lines (and stays the authority on where they
	// are and when they arrive); the drawn figure walks the same trip along a
	// detour around building footprints, at the same progress fraction, so
	// it never cuts through a building and still arrives on time. Starts and
	// stops are eased, turns are smoothed, and a robot left idle walks to a
	// parking slot beside the Lander until its next task.
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
	};
	TArray<FRobotTrip> Trips;
	TArray<FVector> SimLastPosCm; // the sim's position last frame (its own speed)
	double NextRouteLogS = 0.0;   // rh.RobotRouteLog's next report
	// Surface building footprints (expanded by a robot's clearance), rebuilt
	// each tick; robots route around them.
	TArray<FBox2D> Obstacles;
	void RebuildObstacles();
	// Shortest route From -> To around Obstacles (a visibility graph over the
	// expanded footprints' corners); footprints containing either end are
	// the trip's own building and are walked into. Straight when clear.
	void PlanRoute(const FVector& From, const FVector& To, TArray<FVector>& OutPath) const;
	bool SegmentBlocked(const FVector2D& A, const FVector2D& B, const TArray<int32>& Candidates) const;
	// The Nth parking slot beside the Lander (clear of footprints).
	FVector ParkSlotCm(int32 Index) const;
	// The figure's drawn position this frame (advances its trip state).
	FVector AdvanceDrawn(int32 i, const FVector& SimPosCm, float SimSpeedCmS, const FRHTaskFragment* Task, float DeltaTime);
};
