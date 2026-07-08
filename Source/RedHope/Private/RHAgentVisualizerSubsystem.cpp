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
	for (UInstancedStaticMeshComponent* Part : { MeshComponent.Get(), CabComponent.Get(), WheelComponent.Get() })
	{
		if (Part)
		{
			Part->ClearInstances();
		}
	}
}

void URHAgentVisualizerSubsystem::TrackEntities(const TArray<FMassEntityHandle>& Entities)
{
	Tracked.Append(Entities);
	EnsureMeshComponent();

	if (MeshComponent)
	{
		const FTransform Proto(FQuat::Identity, FVector::ZeroVector, FVector(1.f, 1.f, 1.f));
		for (int32 i = 0; i < Entities.Num(); ++i)
		{
			MeshComponent->AddInstance(Proto, /*bWorldSpace*/ true);
			if (CabComponent) { CabComponent->AddInstance(Proto, /*bWorldSpace*/ true); }
			if (WheelComponent) { WheelComponent->AddInstance(Proto, /*bWorldSpace*/ true); }
		}
		UE_LOG(LogRedHope, Display, TEXT("Visualizer tracking %d agents"), Tracked.Num());
	}
}

void URHAgentVisualizerSubsystem::SetSliceHidden(bool bHidden)
{
	bSliceHidden = bHidden;
	for (UInstancedStaticMeshComponent* Part : { MeshComponent.Get(), CabComponent.Get(), WheelComponent.Get() })
	{
		if (Part)
		{
			Part->SetVisibility(!bHidden);
		}
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

	// Reference (Robots/ConstructionDrone): three layers per unit. The maker
	// binds mesh + tint; Tick composes each layer's fixed local offset onto the
	// shared entity transform.
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
	const auto MakeLayer = [&](const TCHAR* Name, const FLinearColor& Tint, const FLinearColor& Emissive) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* Part = NewObject<UInstancedStaticMeshComponent>(Holder, Name);
		Part->RegisterComponent();
		Holder->AddInstanceComponent(Part);
		Part->SetStaticMesh(Cube);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(true);
		if (Base)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Part);
			Mid->SetVectorParameterValue(FName("Tint"), Tint);
			Mid->SetVectorParameterValue(FName("Emissive"), Emissive);
			Part->SetMaterial(0, Mid);
		}
		Part->SetVisibility(!bSliceHidden); // honor an already-descended elevator
		return Part;
	};
	// Chassis: bone-white with the warm self-glow (a powered unit - the night
	// shift stays visible; 0.1 proved invisible in footage, lamps sit at ~3.2).
	MeshComponent = MakeLayer(TEXT("AgentInstances"), FLinearColor(0.62f, 0.60f, 0.54f), FLinearColor(0.60f, 0.50f, 0.28f));
	// Cab glass: near-black with the faint cool status glow of the reference visor.
	CabComponent = MakeLayer(TEXT("AgentCabs"), FLinearColor(0.03f, 0.05f, 0.07f), FLinearColor(0.05f, 0.14f, 0.18f));
	// Wheel skirt: dark running gear under the flanks.
	WheelComponent = MakeLayer(TEXT("AgentWheels"), FLinearColor(0.05f, 0.05f, 0.06f), FLinearColor::Black);
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

	// Fixed part offsets in ENTITY space (X forward): chassis mass, the raked
	// cab glass up front, and the wheel skirt under the flanks - the reference
	// drone's read at strategic zoom. ChildWorld = Local * EntityTransform.
	static const FTransform ChassisLocal(FQuat::Identity, FVector(0, 0, 0), FVector(1.2f, 0.95f, 0.85f));
	static const FTransform CabLocal(FRotator(-14.f, 0, 0).Quaternion(), FVector(30.f, 0, 62.f), FVector(0.55f, 0.78f, 0.45f));
	static const FTransform WheelLocal(FQuat::Identity, FVector(0, 0, -52.f), FVector(1.1f, 1.05f, 0.3f));

	TArray<FTransform> Bodies, Cabs, Wheels;
	Bodies.Reserve(Tracked.Num());
	Cabs.Reserve(Tracked.Num());
	Wheels.Reserve(Tracked.Num());
	for (const FMassEntityHandle& Entity : Tracked)
	{
		if (EntityManager.IsEntityValid(Entity))
		{
			const FTransform& T = EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).GetTransform();
			Bodies.Add(ChassisLocal * T);
			Cabs.Add(CabLocal * T);
			Wheels.Add(WheelLocal * T);
		}
	}
	MeshComponent->BatchUpdateInstancesTransforms(0, Bodies, /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
	if (CabComponent) { CabComponent->BatchUpdateInstancesTransforms(0, Cabs, true, true, true); }
	if (WheelComponent) { WheelComponent->BatchUpdateInstancesTransforms(0, Wheels, true, true, true); }
}
