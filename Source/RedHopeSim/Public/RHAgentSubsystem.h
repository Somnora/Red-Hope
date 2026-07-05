#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "Mass/EntityHandle.h"
#include "RHAgentSubsystem.generated.h"

// Snapshot of one robot for the sim-owned save payload. Task claims are not
// saved (robots re-claim from the board after load); cargo is recorded so the
// sim can return it to the source site in the saved copy - mass conservation.
struct FRHRobotSaveState
{
	FName DefName;
	FVector PosCm = FVector::ZeroVector;
	float ChargeWh = 0.f;
	float Wear = 0.f;
	int32 TaskId = 0;      // open claim at save time (not restored)
	FName CargoResource;
	float CargoKg = 0.f;   // in-flight cargo at save time (returned to source)
};

class UStateTree;

// Owns agent entity lifecycles: the working robot archetype plus the
// benchmark wander-dummies. Save/load round-trips robots through
// FRHRobotSaveState; the sim world owns when that happens.
// Brains (M1-a Gate B): when RH.BrainMode=1 and the ST_RobotBrain asset
// exists, robots spawn with StateTree fragments and think via
// URHRobotBrainProcessor; otherwise they fall back to the legacy switch
// processor. Same driver, two brains - the Gate B parity instrument.
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

	// Spawns one working robot with definition-derived fragment constants,
	// fully charged and unworn.
	FMassEntityHandle SpawnRobot(FName RowName, const struct FRHRobotRow& Def, const FVector& PosCm);
	// Save/load path: spawn with explicit battery + wear.
	FMassEntityHandle SpawnRobotWithState(FName RowName, const struct FRHRobotRow& Def, const FVector& PosCm, float ChargeWh, float Wear);

	// Reads every live robot's fragments into save states.
	void CollectRobotStates(TArray<FRHRobotSaveState>& OutStates) const;
	// Destroys all robot entities (load path; dummies are untouched).
	void DespawnAllRobots();

	int32 GetAgentCount() const { return SpawnedCount; }

private:
	// Resolves (once) whether robots get the StateTree brain this session.
	UStateTree* ResolveBrainTree();

	int32 SpawnedCount = 0;
	FMassArchetypeHandle DummyArchetype;
	FMassArchetypeHandle RobotArchetype;
	TArray<FMassEntityHandle> RobotHandles;
	UPROPERTY() TObjectPtr<UStateTree> BrainTree;
	bool bBrainTreeResolved = false;
};
