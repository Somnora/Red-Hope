#include "RHAgentVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHSimWorldSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

void URHAgentVisualizerSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// The sim's starting fleet gets visuals the same way dummies do.
	if (URHSimWorldSubsystem* Sim = InWorld.GetSubsystem<URHSimWorldSubsystem>())
	{
		Sim->OnRobotsSpawned.AddUObject(this, &URHAgentVisualizerSubsystem::TrackEntities);
		Sim->OnColonyReloaded.AddUObject(this, &URHAgentVisualizerSubsystem::ResetTracking);
	}
}

void URHAgentVisualizerSubsystem::ResetTracking()
{
	Tracked.Reset();
	if (MeshComponent)
	{
		MeshComponent->ClearInstances();
	}
}

void URHAgentVisualizerSubsystem::TrackEntities(const TArray<FMassEntityHandle>& Entities)
{
	Tracked.Append(Entities);
	EnsureMeshComponent();

	if (MeshComponent)
	{
		// Robot-ish gray box: 1 m cube stretched to 1x1x1.5 m.
		const FTransform Proto(FQuat::Identity, FVector::ZeroVector, FVector(1.f, 1.f, 1.5f));
		for (int32 i = 0; i < Entities.Num(); ++i)
		{
			MeshComponent->AddInstance(Proto, /*bWorldSpace*/ true);
		}
		UE_LOG(LogRedHope, Display, TEXT("Visualizer tracking %d agents"), Tracked.Num());
	}
}

void URHAgentVisualizerSubsystem::EnsureMeshComponent()
{
	if (MeshComponent)
	{
		return;
	}
	UWorld* World = GetWorld();
	AActor* Holder = World->SpawnActor<AActor>();
	if (!Holder)
	{
		return;
	}
#if WITH_EDITOR
	Holder->SetActorLabel(TEXT("RH_AgentVisualizer"));
#endif

	MeshComponent = NewObject<UInstancedStaticMeshComponent>(Holder, TEXT("AgentInstances"));
	MeshComponent->RegisterComponent();
	Holder->AddInstanceComponent(MeshComponent);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	MeshComponent->SetStaticMesh(Cube);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComponent->SetCastShadow(true);
}

void URHAgentVisualizerSubsystem::Tick(float DeltaTime)
{
	if (!MeshComponent || Tracked.Num() == 0)
	{
		return;
	}
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return;
	}
	const FMassEntityManager& EntityManager = MassSubsystem->GetEntityManager();

	TArray<FTransform> Transforms;
	Transforms.Reserve(Tracked.Num());
	for (const FMassEntityHandle& Entity : Tracked)
	{
		if (EntityManager.IsEntityValid(Entity))
		{
			Transforms.Add(EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform());
		}
	}
	MeshComponent->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
}
