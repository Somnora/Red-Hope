#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "Mass/EntityHandle.h"
#include "RHAgentSubsystem.generated.h"

// Owns agent entity lifecycles. Scaffold scope: dummy-agent spawning for the
// hardware stress test; M0 replaces dummies with the real robot archetypes.
UCLASS()
class REDHOPESIM_API URHAgentSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// Spawns Count wander-dummies scattered in a box of +/-ExtentCm around
	// Center. Returns the created entity handles (presentation syncs visuals
	// from these; sim never knows about the visuals).
	TArray<FMassEntityHandle> SpawnDummyAgents(int32 Count, const FVector& CenterCm, float ExtentCm);

	int32 GetAgentCount() const { return SpawnedCount; }

private:
	int32 SpawnedCount = 0;
	FMassArchetypeHandle DummyArchetype;
};
