#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "MassArchetypeTypes.h"
#include "Mass/EntityHandle.h"
#include "RHSimTypes.h"
#include "RHAgentSubsystem.generated.h"

// Snapshot of one robot for the sim-owned save payload. Task claims are not
// saved (robots re-claim from the board after load); cargo is recorded so the
// sim can return it to the source site in the saved copy - mass conservation.
struct FRHRobotSaveState
{
	FName DefName;
	FVector PosCm = FVector::ZeroVector;
	// Z-model (M1-b): dormant until the shaft lets robots descend (M1-d);
	// serialized now so the v2 save format survives that milestone unchanged.
	int32 Level = 0;
	float ChargeWh = 0.f;
	float Wear = 0.f;
	int32 TaskId = 0;      // open claim at save time (not restored)
	FName CargoResource;
	float CargoKg = 0.f;   // in-flight cargo at save time (returned to source)
};

class UStateTree;

// One fleet-panel line (M1-b Gate C): everything the deck shows per robot,
// snapshotted in one pass so the UI never touches the entity manager.
struct FRHRobotPanelRow
{
	FName DefName;
	FName RobotClass;
	uint8 TaskType = 0;    // ERHTaskType
	float ChargeFrac = 0.f;
	float Wear = 0.f;
	double PosZCm = 50.0;  // D4: the robot's Z, which carries its floor (URHSimWorldSubsystem::LevelOfZ reads it)
};

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

	// Fleet reality (M1-b): the sim's repair loop scans and mutates robot
	// wear through these - the entity manager stays this subsystem's business.
	void ForEachRobotState(TFunctionRef<void(FMassEntityHandle, const FVector&, float /*Wear*/)> Fn) const;
	bool GetRobotPosition(FMassEntityHandle Entity, FVector& OutPosCm) const;
	// D4 (sim only): the load repair moves a robot that cannot stay below to the surface deploy ring, or sets the Z its saved
	// level names. False when the entity is gone.
	bool SetRobotPosition(FMassEntityHandle Entity, const FVector& PosCm);
	// D4: one robot's wear (the halted test of a cross-level repair). False when the entity is gone.
	bool GetRobotWear(FMassEntityHandle Entity, float& OutWear) const;
	// Test knobs (-transit): one robot's wear and charge.
	void Debug_SetRobotWear(FMassEntityHandle Entity, float Wear);
	void Debug_SetRobotChargeWh(FMassEntityHandle Entity, float ChargeWh);
	// Reduces wear by up to Amount; returns what actually came off.
	float RemoveRobotWear(FMassEntityHandle Entity, float Amount);
	// Test knob (RH.Wear): set every robot's wear directly.
	void Debug_SetAllWear(float Wear);
	// Fleet panel snapshot (M1-b Gate C), stable spawn order.
	void CollectPanelRows(TArray<FRHRobotPanelRow>& OutRows) const;
	// C6a: robots of a class that can work now - wear under the halt line and charge left. The task
	// board counts the active cargo rovers once a step through this (a robot's battery and wear
	// are fragments, which ForEachRobotState does not read).
	int32 CountActiveRobots(FName RobotClass, float WearHalt) const;
	// W1: the brain's own halt test for one robot: it ticks only while its wear is under the halt line
	// and it has charge, or is docked at a pad (a Charge task in phase 1), which can refill a dead
	// battery. A halted robot keeps its claim without working, so the outpost lane asks this.
	bool IsRobotLive(FMassEntityHandle Entity, float WearHalt) const;
	// C6a: every robot of a class, stable spawn order (cargo rovers: the readout for C6b and the cheats).
	void CollectRobotsOfClass(FName RobotClass, TArray<FRHRoverState>& OutStates) const;
// Test knob (RH.Rover drain): set every robot of a class to a fraction of its battery.
void Debug_SetChargeFrac(FName RobotClass, float Frac);

	int32 GetAgentCount() const { return SpawnedCount; }

private:
	// Resolves (once) whether robots get the StateTree brain this session.
	UStateTree* ResolveBrainTree();

	int32 SpawnedCount = 0;
	FMassArchetypeHandle DummyArchetype;
	FMassArchetypeHandle RobotArchetype;
	// StateTree-brained variant: same fragments plus the ST instance fragment,
	// with the const-shared tree fragment and the engine's activated tag baked
	// into the composition (a query's AddConstSharedRequirement only matches
	// archetypes whose composition carries the bit - Gate B lesson).
	FMassArchetypeHandle STRobotArchetype;
	TArray<FMassEntityHandle> RobotHandles;
	UPROPERTY() TObjectPtr<UStateTree> BrainTree;
	bool bBrainTreeResolved = false;
};
