#include "RHAgentSubsystem.h"
#include "RedHopeSim.h"
#include "RHAgentFragments.h"
#include "Data/RHRows.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"

TArray<FMassEntityHandle> URHAgentSubsystem::SpawnDummyAgents(int32 Count, const FVector& CenterCm, float ExtentCm)
{
	TArray<FMassEntityHandle> Handles;
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem || Count <= 0)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("SpawnDummyAgents: no MassEntitySubsystem or bad count"));
		return Handles;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();

	if (!DummyArchetype.IsValid())
	{
		TArray<const UScriptStruct*> Fragments;
		Fragments.Add(FTransformFragment::StaticStruct());
		Fragments.Add(FRHBatteryFragment::StaticStruct());
		Fragments.Add(FRHWanderFragment::StaticStruct());
		DummyArchetype = EntityManager.CreateArchetype(Fragments);
	}

	Handles.Reserve(Count);
	FRandomStream Rand(20260704); // seeded: determinism invariant applies to tests too

	for (int32 i = 0; i < Count; ++i)
	{
		const FMassEntityHandle Entity = EntityManager.CreateEntity(DummyArchetype);
		Handles.Add(Entity);

		const FVector Pos = CenterCm + FVector(Rand.FRandRange(-ExtentCm, ExtentCm), Rand.FRandRange(-ExtentCm, ExtentCm), 50.f);
		EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(Pos);

		FRHWanderFragment& Wander = EntityManager.GetFragmentDataChecked<FRHWanderFragment>(Entity);
		Wander.Target = CenterCm + FVector(Rand.FRandRange(-ExtentCm, ExtentCm), Rand.FRandRange(-ExtentCm, ExtentCm), 50.f);
		Wander.SpeedMps = 3.f;
	}

	SpawnedCount += Count;
	UE_LOG(LogRedHopeSim, Display, TEXT("Spawned %d dummy agents (total %d)"), Count, SpawnedCount);
	return Handles;
}

FMassEntityHandle URHAgentSubsystem::SpawnRobot(FName RowName, const FRHRobotRow& Def, const FVector& PosCm)
{
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return FMassEntityHandle();
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();

	if (!RobotArchetype.IsValid())
	{
		TArray<const UScriptStruct*> Fragments;
		Fragments.Add(FTransformFragment::StaticStruct());
		Fragments.Add(FRHBatteryFragment::StaticStruct());
		Fragments.Add(FRHRobotFragment::StaticStruct());
		Fragments.Add(FRHTaskFragment::StaticStruct());
		RobotArchetype = EntityManager.CreateArchetype(Fragments);
	}

	const FMassEntityHandle Entity = EntityManager.CreateEntity(RobotArchetype);

	EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(PosCm);

	FRHBatteryFragment& Battery = EntityManager.GetFragmentDataChecked<FRHBatteryFragment>(Entity);
	Battery.CapacityWh = Def.Battery_Wh;
	Battery.ChargeWh = Def.Battery_Wh;
	Battery.DrawMoveW = Def.DrawMove_W;

	FRHRobotFragment& Robot = EntityManager.GetFragmentDataChecked<FRHRobotFragment>(Entity);
	Robot.DefName = RowName;
	Robot.RobotClass = Def.RobotClass;
	Robot.SpeedMps = Def.Speed_mps;
	Robot.CargoCapKg = Def.Cargo_kg;
	Robot.WorkRate = Def.WorkRate;
	Robot.DrawMoveW = Def.DrawMove_W;
	Robot.DrawWorkW = Def.DrawWork_W;
	Robot.DrawIdleW = Def.DrawIdle_W;

	++SpawnedCount;
	return Entity;
}
