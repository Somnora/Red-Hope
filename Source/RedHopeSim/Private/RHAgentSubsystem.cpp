#include "RHAgentSubsystem.h"
#include "RedHopeSim.h"
#include "RHAgentFragments.h"
#include "Data/RHRows.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"
#include "MassStateTreeFragments.h"
#include "MassStateTreeSubsystem.h"
#include "MassStateTreeProcessors.h"
#include "StateTree.h"

// 0 = legacy switch processor for every robot (A/B baseline);
// 1 = StateTree brain when /Game/RedHope/AI/ST_RobotBrain exists.
static TAutoConsoleVariable<int32> CVarRHBrainMode(
	TEXT("RH.BrainMode"), 1,
	TEXT("Robot decision layer: 0=legacy switch, 1=StateTree (falls back to legacy if the tree asset is missing)."));

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

UStateTree* URHAgentSubsystem::ResolveBrainTree()
{
	if (!bBrainTreeResolved)
	{
		bBrainTreeResolved = true;
		BrainTree = LoadObject<UStateTree>(nullptr, TEXT("/Game/RedHope/AI/ST_RobotBrain.ST_RobotBrain"));
		UE_LOG(LogRedHopeSim, Display, TEXT("Robot brain: %s"),
			BrainTree ? TEXT("StateTree (ST_RobotBrain)") : TEXT("legacy switch (no ST_RobotBrain asset)"));
	}
	return CVarRHBrainMode.GetValueOnGameThread() != 0 ? BrainTree.Get() : nullptr;
}

FMassEntityHandle URHAgentSubsystem::SpawnRobotWithState(FName RowName, const FRHRobotRow& Def, const FVector& PosCm, float ChargeWh, float Wear)
{
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return FMassEntityHandle();
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();

	// Fragment values shared by both brain paths.
	FTransformFragment TransformFrag;
	TransformFrag.GetMutableTransform().SetLocation(PosCm);

	FRHBatteryFragment BatteryFrag;
	BatteryFrag.CapacityWh = Def.Battery_Wh;
	BatteryFrag.ChargeWh = FMath::Clamp(ChargeWh, 0.f, Def.Battery_Wh);
	BatteryFrag.DrawMoveW = Def.DrawMove_W;

	FRHWearFragment WearFrag;
	WearFrag.Wear = FMath::Clamp(Wear, 0.f, 100.f);

	FRHRobotFragment RobotFrag;
	RobotFrag.DefName = RowName;
	RobotFrag.RobotClass = Def.RobotClass;
	RobotFrag.SpeedMps = Def.Speed_mps;
	RobotFrag.CargoCapKg = Def.Cargo_kg;
	RobotFrag.WorkRate = Def.WorkRate;
	RobotFrag.DrawMoveW = Def.DrawMove_W;
	RobotFrag.DrawWorkW = Def.DrawWork_W;
	RobotFrag.DrawIdleW = Def.DrawIdle_W;
	RobotFrag.WearPerSol = Def.WearPerActiveSol;

	FMassEntityHandle Entity;

	if (UStateTree* Tree = ResolveBrainTree())
	{
		// StateTree brain. The composition must EXPLICITLY carry the
		// const-shared tree fragment: CreateEntity(FragmentInstanceList, ...)
		// derives an archetype with an empty const-shared bitset and never
		// reconciles it with the passed shared values (verified 5.8 source),
		// so queries with AddConstSharedRequirement silently match nothing.
		// The engine's "already activated" tag is baked in for the same
		// reason MassAI's activation processor must never see these robots:
		// it would reallocate our instance data and Start trees on wall-clock
		// cadence. Our brain processor owns Start + Tick on the fixed sim step.
		UMassStateTreeSubsystem* StateTreeSubsystem = GetWorld()->GetSubsystem<UMassStateTreeSubsystem>();
		if (!StateTreeSubsystem)
		{
			return FMassEntityHandle();
		}
		if (!STRobotArchetype.IsValid())
		{
			FMassFragmentBitSet Fragments;
			Fragments.Add<FTransformFragment>();
			Fragments.Add<FRHBatteryFragment>();
			Fragments.Add<FRHWearFragment>();
			Fragments.Add<FRHRobotFragment>();
			Fragments.Add<FRHTaskFragment>();
			Fragments.Add<FMassStateTreeInstanceFragment>();
			FMassTagBitSet Tags;
			Tags.Add<FMassStateTreeActivatedTag>();
			FMassConstSharedFragmentBitSet ConstShared;
			ConstShared.Add<FMassStateTreeSharedFragment>();
			const FMassArchetypeCompositionDescriptor Composition(
				MoveTemp(Fragments), MoveTemp(Tags), FMassChunkFragmentBitSet(), FMassSharedFragmentBitSet(), MoveTemp(ConstShared));
			STRobotArchetype = EntityManager.CreateArchetype(Composition);
		}

		FMassStateTreeSharedFragment SharedFrag;
		SharedFrag.StateTree = Tree;
		FMassArchetypeSharedFragmentValues SharedValues;
		SharedValues.Add(EntityManager.GetOrCreateConstSharedFragment(SharedFrag));

		Entity = EntityManager.CreateEntity(STRobotArchetype, SharedValues);
		if (Entity.IsValid())
		{
			EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity) = TransformFrag;
			EntityManager.GetFragmentDataChecked<FRHBatteryFragment>(Entity) = BatteryFrag;
			EntityManager.GetFragmentDataChecked<FRHWearFragment>(Entity) = WearFrag;
			EntityManager.GetFragmentDataChecked<FRHRobotFragment>(Entity) = RobotFrag;
			FMassStateTreeInstanceFragment& InstanceFrag = EntityManager.GetFragmentDataChecked<FMassStateTreeInstanceFragment>(Entity);
			InstanceFrag.InstanceHandle = StateTreeSubsystem->AllocateInstanceData(Tree);
		}
	}
	else
	{
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
		Entity = EntityManager.CreateEntity(RobotArchetype);
		EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity) = TransformFrag;
		EntityManager.GetFragmentDataChecked<FRHBatteryFragment>(Entity) = BatteryFrag;
		EntityManager.GetFragmentDataChecked<FRHWearFragment>(Entity) = WearFrag;
		EntityManager.GetFragmentDataChecked<FRHRobotFragment>(Entity) = RobotFrag;
	}

	if (Entity.IsValid())
	{
		RobotHandles.Add(Entity);
		++SpawnedCount;
	}
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

void URHAgentSubsystem::ForEachRobotState(TFunctionRef<void(FMassEntityHandle, const FVector&, float)> Fn) const
{
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return;
	}
	const FMassEntityManager& EntityManager = MassSubsystem->GetEntityManager();
	for (const FMassEntityHandle& Entity : RobotHandles)
	{
		if (EntityManager.IsEntityValid(Entity))
		{
			Fn(Entity,
				EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform().GetLocation(),
				EntityManager.GetFragmentDataChecked<FRHWearFragment>(Entity).Wear);
		}
	}
}

bool URHAgentSubsystem::GetRobotPosition(FMassEntityHandle Entity, FVector& OutPosCm) const
{
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem || !MassSubsystem->GetEntityManager().IsEntityValid(Entity))
	{
		return false;
	}
	OutPosCm = MassSubsystem->GetEntityManager().GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform().GetLocation();
	return true;
}

float URHAgentSubsystem::RemoveRobotWear(FMassEntityHandle Entity, float Amount)
{
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem || !MassSubsystem->GetEntityManager().IsEntityValid(Entity))
	{
		return 0.f;
	}
	FRHWearFragment& Wear = MassSubsystem->GetMutableEntityManager().GetFragmentDataChecked<FRHWearFragment>(Entity);
	const float Removed = FMath::Min(Wear.Wear, Amount);
	Wear.Wear -= Removed;
	return Removed;
}

void URHAgentSubsystem::Debug_SetAllWear(float Wear)
{
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	int32 Touched = 0;
	for (const FMassEntityHandle& Entity : RobotHandles)
	{
		if (EntityManager.IsEntityValid(Entity))
		{
			EntityManager.GetFragmentDataChecked<FRHWearFragment>(Entity).Wear = FMath::Clamp(Wear, 0.f, 100.f);
			++Touched;
		}
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("[RH.Wear] set %d robots to wear %.0f (test knob)"), Touched, Wear);
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
