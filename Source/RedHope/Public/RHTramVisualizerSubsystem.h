#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHTramVisualizerSubsystem.generated.h"

class AActor;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class URHSimWorldSubsystem;

// Presentation of the colony's tram lines (D6d, docs/qa/2026-10-22/d6-caverns-design.md section 4). A pure listener: it reads the sim's tram
// list and tunnel chains (const accessors only), never writes, and nothing here is saved. On the floor view each line is ONE capsule that
// shuttles along its tunnel by phase (the sim simulates no car one by one; the capsule is a picture of the line, not a car): out to the far
// stop, a dwell, back, a dwell. Its tint is the line's wait (teal, amber, red against MaxWaitSec, as the lifts' bars). People only ride; robots
// keep to the tunnels and the lifts, so nothing here follows a robot.
// The section draws the lines itself (its tram band), so every capsule hides while PC->IsSectionOn() (D8 R1), and a capsule shows only on the
// floor being viewed. Nothing is spawned until a line exists, so a colony that never lays one adds no actor.
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
	// A line's capsule as last placed (world cm), for the click test and the captures. False for an unknown line.
	bool GetCapsuleAt(int32 LineId, FVector& OutCm, bool* bOutShown = nullptr) const;

	// Pure (the click test runs it with no world): where a capsule is on a line of ArcCm along its chain, at ClockSec of the presentation clock
	// with a one-way ride of RideSec and a dwell of DwellSec at each stop. 0 at the first stop, ArcCm at the far one.
	static double ArcAt(double ClockSec, double RideSec, double DwellSec, double ArcCm);

private:
	struct FCar
	{
		int32 LineId = 0;
		TWeakObjectPtr<UProceduralMeshComponent> Mesh;
		TWeakObjectPtr<UMaterialInstanceDynamic> Mid;
		int32 WaitClass = -1;
		FVector At = FVector::ZeroVector;
		bool bShown = false;
	};
	void EnsureHolder();
	void MakeCar(int32 LineId);
	UPROPERTY() TObjectPtr<AActor> Holder;
	TArray<FCar> Cars;
	double ClockSec = 0.0;
	int32 ShownCount = 0;
};
