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
	return SpawnRobotWithState(RowName, Def, PosCm, Def.Battery_Wh, 0.f);
}

FMassEntityHandle URHAgentSubsystem::SpawnRobotWithState(FName RowName, const FRHRobotRow& Def, const FVector& PosCm, float ChargeWh, float Wear)
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
		Fragments.Add(FRHWearFragment::StaticStruct());
		Fragments.Add(FRHRobotFragment::StaticStruct());
		Fragments.Add(FRHTaskFragment::StaticStruct());
		RobotArchetype = EntityManager.CreateArchetype(Fragments);
	}

	const FMassEntityHandle Entity = EntityManager.CreateEntity(RobotArchetype);

	EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetMutableTransform().SetLocation(PosCm);

	FRHBatteryFragment& Battery = EntityManager.GetFragmentDataChecked<FRHBatteryFragment>(Entity);
	Battery.CapacityWh = Def.Battery_Wh;
	Battery.ChargeWh = FMath::Clamp(ChargeWh, 0.f, Def.Battery_Wh);
	Battery.DrawMoveW = Def.DrawMove_W;

	EntityManager.GetFragmentDataChecked<FRHWearFragment>(Entity).Wear = FMath::Clamp(Wear, 0.f, 100.f);

	FRHRobotFragment& Robot = EntityManager.GetFragmentDataChecked<FRHRobotFragment>(Entity);
	Robot.DefName = RowName;
	Robot.RobotClass = Def.RobotClass;
	Robot.SpeedMps = Def.Speed_mps;
	Robot.CargoCapKg = Def.Cargo_kg;
	Robot.WorkRate = Def.WorkRate;
	Robot.DrawMoveW = Def.DrawMove_W;
	Robot.DrawWorkW = Def.DrawWork_W;
	Robot.DrawIdleW = Def.DrawIdle_W;

	RobotHandles.Add(Entity);
	++SpawnedCount;
	return Entity;
}

void URHAgentSubsystem::CollectRobotStates(TArray<FRHRobotSaveState>& OutStates) const
{
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return;
	}
	const FMassEntityManager& EntityManager = MassSubsystem->GetEntityManager();
	OutStates.Reserve(OutStates.Num() + RobotHandles.Num());

	for (const FMassEntityHandle& Entity : RobotHandles)
	{
		if (!EntityManager.IsEntityValid(Entity))
		{
			continue;
		}
		FRHRobotSaveState State;
		State.DefName = EntityManager.GetFragmentDataChecked<FRHRobotFragment>(Entity).DefName;
		State.PosCm = EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform().GetLocation();
		State.ChargeWh = EntityManager.GetFragmentDataChecked<FRHBatteryFragment>(Entity).ChargeWh;
		State.Wear = EntityManager.GetFragmentDataChecked<FRHWearFragment>(Entity).Wear;
		const FRHTaskFragment& Task = EntityManager.GetFragmentDataChecked<FRHTaskFragment>(Entity);
		State.TaskId = Task.TaskId;
		State.CargoResource = Task.CargoResource;
		State.CargoKg = Task.CargoKg;
		OutStates.Add(State);
	}
}

void URHAgentSubsystem::DespawnAllRobots()
{
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	int32 Destroyed = 0;
	for (const FMassEntityHandle& Entity : RobotHandles)
	{
		if (EntityManager.IsEntityValid(Entity))
		{
			EntityManager.DestroyEntity(Entity);
			++Destroyed;
		}
	}
	SpawnedCount = FMath::Max(0, SpawnedCount - Destroyed);
	RobotHandles.Reset();
	UE_LOG(LogRedHopeSim, Display, TEXT("Despawned %d robots (load path)"), Destroyed);
}
