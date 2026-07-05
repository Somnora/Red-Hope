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

	void TrackEntities(const TArray<FMassEntityHandle>& Entities);

private:
	void EnsureMeshComponent();

	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> MeshComponent;
	TArray<FMassEntityHandle> Tracked;
};
