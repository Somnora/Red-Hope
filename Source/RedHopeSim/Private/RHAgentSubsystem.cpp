#include "RHAgentSubsystem.h"
#include "RedHopeSim.h"
#include "RHAgentFragments.h"
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
