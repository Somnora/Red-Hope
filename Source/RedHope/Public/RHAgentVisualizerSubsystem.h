#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "Mass/EntityHandle.h"
#include "RHAgentVisualizerSubsystem.generated.h"

class UInstancedStaticMeshComponent;

// Scaffold presentation for sim agents: one ISM, one instance per entity,
// transforms copied from Mass fragments each frame. Deliberately dumb - it
// proves the seam (sim knows nothing about it) and is what the stress test
// measures as "presentation cost". MassRepresentation replaces it in M0+.
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

	// Reference (Robots/ConstructionDrone): three instanced layers make each
	// unit read as the canon drone at strategic zoom - bone-white chassis, a
	// dark tinted cab glass, and a dark wheel skirt - instead of one anonymous
	// cube. All three ride the same entity transform with a fixed local offset
	// composed per part in Tick (still zero per-actor cost).
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MeshComponent;      // chassis
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CabComponent;      // glass
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WheelComponent;    // skirt
	TArray<FMassEntityHandle> Tracked;
	bool bSliceHidden = false;
};
