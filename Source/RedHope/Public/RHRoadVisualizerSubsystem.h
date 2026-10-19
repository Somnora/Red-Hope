#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHRoadVisualizerSubsystem.generated.h"

class AActor;
class UProceduralMeshComponent;
class URHSimWorldSubsystem;

// Presentation of the road graph (C4, living city gate 4). A pure listener: it
// reads the sim's graph and rebuilds one procedural mesh whenever the graph's
// version moves (an edge laid, split or finished). A BUILT edge is a solid strip
// of compacted regolith, darker than the ground, with a round pad at every joint;
// a PLANNED or UNDER-CONSTRUCTION edge is a dashed ghost strip (the partial fill
// for a road mid-build, and edge marker posts, are a follow-up). The strips follow
// the terrain's logical ground height, like the robots. Deleting this class leaves
// the sim intact.
UCLASS()
class REDHOPE_API URHRoadVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHRoadVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

private:
	void EnsureMesh();
	void Rebuild(const URHSimWorldSubsystem& Sim);

	UPROPERTY() TObjectPtr<AActor> Holder;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;
	uint32 DrawnVersion = MAX_uint32;
	bool bHidden = false;
};
