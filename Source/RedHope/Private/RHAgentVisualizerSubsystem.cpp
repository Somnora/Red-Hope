#include "RHAgentVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHSimWorldSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

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
	// Canon: bone-white body (humanoid android chassis lands with the deferred
	// art pass; this instanced block is the stand-in). Reads bright against
	// the rust regolith.
	if (UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox")))
	{
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, MeshComponent);
		Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(0.62f, 0.60f, 0.54f));
		// Warm self-glow: a powered unit - the night shift stays visible
		// instead of vanishing until sunrise. 0.1 proved invisible in footage;
		// site lamps sit at ~3.2, so this reads as a dim unit, not a light.
		Mid->SetVectorParameterValue(FName("Emissive"), FLinearColor(0.60f, 0.50f, 0.28f));
		MeshComponent->SetMaterial(0, Mid);
	}
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
