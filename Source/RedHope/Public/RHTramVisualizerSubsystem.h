#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHTramVisualizerSubsystem.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class URHSimWorldSubsystem;
struct FRHRoundChain;

// Presentation of the colony's tram lines (D6d, docs/qa/2026-10-22/d6-caverns-design.md section 4). A pure listener: it reads the sim's tram
// list and tunnel chains (const accessors only), never writes, and nothing here is saved. On the floor view each line is ONE capsule that
// shuttles along its tunnel by phase (the sim simulates no car one by one; the capsule is a picture of the line, not a car): out to the far
// stop, a dwell, back, a dwell. Its tint is the line's wait (teal, amber, red against MaxWaitSec, as the lifts' bars; the visual pass's train below
// shows busy in ORANGE on its boards instead, deliberately not the lifts' amber, which is the train's own livery trim). People only ride; robots
// keep to the tunnels and the lifts, so nothing here follows a robot.
// The section draws the lines itself (its tram band), so every capsule hides while PC->IsSectionOn() (D8 R1), and a capsule shows only on the
// floor being viewed. Nothing is spawned until a line exists, so a colony that never lays one adds no actor.
// Visual pass (docs/qa/2026-10-25/visual-pass-design.md section 1): the capsule is now a TRAIN, two kit cars (RH_TramCar, the second turned
// 180 deg, so it is double-ended) and the rubber joint between them (RH_TramJoint), standing on the floor plane. Each car is the chord between
// its two bogie points 4 m apart in true arc length on the chain with its corners rounded into circular arcs (RHRoundChain.h: radius 10 m at a
// 90 deg corner, even when one leg is a single cell), so the cars bend through the L corner (about 36 deg) with the joint faces staying about
// 60 cm apart; the noses stop inside the end cells. The tunnel's carved stripe is drawn along the same rounded chain, so the train stays on it. The line's wait
// colour shows on the destination boards only (the DIRECTOR ANSWER Q-B (a)). A missing kit asset logs one warning and the line falls back to the
// capsule. "Capsule" in the accessors below keeps its meaning: one train a line; At is the train's centre on the floor plane (the fallback
// capsule's own centre is drawn 140 cm above it).
UCLASS()
class REDHOPE_API URHTramVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHTramVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// Capsules made (one a line) and capsules showing at the last tick (the viewed floor's, with the section off).
	int32 GetCapsuleCount() const { return Cars.Num(); }
	int32 GetShownCapsules() const { return ShownCount; }
	// A line's train (or fallback capsule) as last placed, for the click test and the captures: At is the train's centre on the floor plane, in
	// world cm (the fallback capsule's centre is 140 cm above it). It is updated only while the line is shown (bOutShown); a hidden line keeps the
	// last place it was drawn. False for an unknown line.
	bool GetCapsuleAt(int32 LineId, FVector& OutCm, bool* bOutShown = nullptr) const;

	// QA (RH.TramPose, the click test): hold the presentation clock so that a line stands a fraction of its one-way ride along its chain (0 at the first
	// stop, 1 in the dwell at the far one); a negative fraction lets the clock run again. False for an unknown line or no sim.
	bool Debug_PoseLine(int32 LineId, double Fraction);
	// QA (RH.TramWait, the captures): show a line's boards in wait class 0 calm teal, 1 busy orange or 2 long red whatever the line's wait;
	// -1 hands them back to the wait. False for a line with no train yet.
	bool Debug_ForceWait(int32 LineId, int32 WaitClass);

	// Pure (the click test runs it with no world): where a capsule is on a line of ArcCm along its chain, at ClockSec of the presentation clock
	// with a one-way ride of RideSec and a dwell of DwellSec at each stop. 0 at the first stop, ArcCm at the far one.
	static double ArcAt(double ClockSec, double RideSec, double DwellSec, double ArcCm);

	// Where a train stands (world cm, yaw in degrees): car 0 leads toward the far stop, car 1 is turned to face the first stop, so each Yaw is
	// that car's +X (its cab). Nose = the tip of each car's cab.
	struct FTrainPose
	{
		FVector CarAt[2] = { FVector::ZeroVector, FVector::ZeroVector };
		double CarYaw[2] = { 0.0, 0.0 };
		FVector Nose[2] = { FVector::ZeroVector, FVector::ZeroVector };
		FVector JointAt = FVector::ZeroVector;
		double JointYaw = 0.0;
	};
	// Pure (tram.fit runs it with no world, on its own L chains): the train's centre along a path of ArcCm for a capsule phase Arc (ArcAt's
	// 0..ArcCm), so the noses stop inside the end cells: HalfLen + Arc x (ArcCm - 2 HalfLen) / ArcCm. Tick passes the rounded chain's length.
	// A path no longer than the train (2 HalfLen = 12.02 m: a legal 2-cell, 10 m line) gives ArcCm / 2 whatever the phase: that train stands
	// still across both cells, its noses about 1 m past each end-cell centre (still inside the cells), while the line's ride and dwell run on.
	static double CentreArc(double Arc, double ArcCm);
	// Pure: the train posed with its centre CentreCm (true arc length) along a chain with its corners rounded, built once a line by the caller
	// (FRHRoundChain, RHRoundChain.h, from the chain's cell centres in world cm, XY; Z is set by the caller). Nose = CarAt + the contract's half
	// length along CarYaw.
	static void PoseOnRounded(const FRHRoundChain& Rounded, double CentreCm, FTrainPose& Out);
	// A line's train as last placed, for the click test (tram.fit) and the captures; its lowest point in world Z. Nose here is measured off the
	// placed car meshes (each mesh's +X bound through its component transform), not the pose's constants. False for an unknown line, a line not
	// shown at the last tick (another floor, or the section on: its pose would be stale) or a line drawn as the fallback capsule.
	bool GetTrainPose(int32 LineId, FTrainPose& OutPose, double& OutLowestZ) const;

private:
	struct FCar
	{
		int32 LineId = 0;
		TWeakObjectPtr<UProceduralMeshComponent> Mesh;     // the fallback capsule (a kit asset missing)
		TWeakObjectPtr<UStaticMeshComponent> Car[2];       // the train: two cars and the joint
		TWeakObjectPtr<UStaticMeshComponent> Joint;
		TWeakObjectPtr<UMaterialInstanceDynamic> Mid;      // the boards' (or the capsule's) tint
		int32 WaitClass = -1;
		int32 ForcedWait = -1;                             // Debug_ForceWait: -1 follows the line's wait
		FVector At = FVector::ZeroVector;
		FTrainPose Pose;
		bool bShown = false;
	};
	void EnsureHolder();
	bool LoadKit();
	void MakeCar(int32 LineId);
	void ShowCar(FCar& C, bool bShow);
	UPROPERTY() TObjectPtr<AActor> Holder;
	UPROPERTY() TObjectPtr<UStaticMesh> CarMesh;
	UPROPERTY() TObjectPtr<UStaticMesh> JointMesh;
	int32 KitState = 0;                                    // 0 not tried, 1 loaded, -1 missing (warned once; capsules)
	TArray<FCar> Cars;
	TArray<FVector2D> ChainScratch;                        // Tick's chain points, reused
	double ClockSec = 0.0;
	bool bHoldClock = false;
	int32 ShownCount = 0;
};
