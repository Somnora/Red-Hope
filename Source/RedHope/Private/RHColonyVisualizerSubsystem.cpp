#include "RHColonyVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"

void URHColonyVisualizerSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (URHSimWorldSubsystem* Sim = InWorld.GetSubsystem<URHSimWorldSubsystem>())
	{
		AddedHandle = Sim->OnBuildingAdded.AddUObject(this, &URHColonyVisualizerSubsystem::HandleBuildingAdded);
		RejectedHandle = Sim->OnCommandRejected.AddUObject(this, &URHColonyVisualizerSubsystem::HandleCommandRejected);
		// Mirror anything the sim placed before we subscribed (the Lander).
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			HandleBuildingAdded(B);
		}
	}
}

void URHColonyVisualizerSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			Sim->OnBuildingAdded.Remove(AddedHandle);
			Sim->OnCommandRejected.Remove(RejectedHandle);
		}
	}
	Super::Deinitialize();
}

void URHColonyVisualizerSubsystem::HandleBuildingAdded(const FRHBuildingInstance& Instance)
{
	UWorld* World = GetWorld();
	if (!World || BuildingVisuals.Contains(Instance.Id))
	{
		return;
	}

	const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(Instance.DefName) : nullptr;

	// Gray-box shape from footprint (2 m cells / 1 m base cube => scale = cells x 2).
	FVector Scale(2.f, 2.f, 2.f);
	if (Def)
	{
		Scale.X = FMath::Max(1, Def->FootprintX) * 2.f;
		Scale.Y = FMath::Max(1, Def->FootprintY) * 2.f;
		const FName N = Instance.DefName;
		if (N == FName("SolarArray") || N == FName("ChargePad") || N == FName("Stockpile"))
		{
			Scale.Z = 0.25f; // flat decks
		}
		else if (N == FName("Pylon"))
		{
			Scale = FVector(0.6f, 0.6f, 6.f); // tall mast
		}
		else
		{
			Scale.Z = 2.f + (Def->FootprintX + Def->FootprintY) * 0.25f;
		}
	}

	const FVector Location = Instance.LocationCm + FVector(0, 0, Scale.Z * 50.f);
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
	if (!Actor)
	{
		return;
	}
	UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Actor->SetActorScale3D(Scale);
#if WITH_EDITOR
	Actor->SetActorLabel(FString::Printf(TEXT("Sim_%s_%d"), *Instance.DefName.ToString(), Instance.Id));
#endif
	BuildingVisuals.Add(Instance.Id, Actor);
}

void URHColonyVisualizerSubsystem::HandleCommandRejected(const FRHCommand& Cmd, const FString& Reason)
{
	// Toast stand-in until the UI pass.
	UE_LOG(LogRedHope, Warning, TEXT("ORDER REJECTED: %s %s - %s"),
		*Cmd.Verb.ToString(), *Cmd.Target.ToString(), *Reason);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 6.f, FColor::Orange,
			FString::Printf(TEXT("Order rejected: %s (%s)"), *Cmd.Target.ToString(), *Reason));
	}
}

void URHColonyVisualizerSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
	const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
	if (!Sim || !Defs)
	{
		return;
	}
	// Territory made visible: one disc per coverage node, every frame.
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		if (const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName))
		{
			if (Def->CoverageRadius_m > 0.f)
			{
				DrawDebugCircle(World, B.LocationCm + FVector(0, 0, 30.f), Def->CoverageRadius_m * 100.f,
					64, FColor(255, 140, 40), false, -1.f, 0, 12.f,
					FVector(1, 0, 0), FVector(0, 1, 0), false);
			}
		}
	}
}
