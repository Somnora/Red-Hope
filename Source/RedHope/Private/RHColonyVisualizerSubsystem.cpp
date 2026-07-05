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
		CompletedHandle = Sim->OnBuildingCompleted.AddUObject(this, &URHColonyVisualizerSubsystem::HandleBuildingCompleted);
		RejectedHandle = Sim->OnCommandRejected.AddUObject(this, &URHColonyVisualizerSubsystem::HandleCommandRejected);
		Sim->OnQuotaMet.AddUObject(this, &URHColonyVisualizerSubsystem::HandleQuotaMet);
		Sim->OnShipArrived.AddUObject(this, &URHColonyVisualizerSubsystem::HandleShipArrived);
		Sim->OnColonyReloaded.AddUObject(this, &URHColonyVisualizerSubsystem::HandleColonyReloaded);
		// Mirror anything the sim placed before we subscribed (the Lander).
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			HandleBuildingAdded(B);
		}
		SpawnDepositMarkers();
	}
}

void URHColonyVisualizerSubsystem::HandleColonyReloaded()
{
	// Everything visual is disposable; the sim state walk rebuilds it all.
	for (auto& Pair : BuildingVisuals)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	BuildingVisuals.Reset();
	for (AStaticMeshActor* Marker : DepositMarkers)
	{
		if (Marker)
		{
			Marker->Destroy();
		}
	}
	DepositMarkers.Reset();
	if (ShipVisual)
	{
		ShipVisual->Destroy();
		ShipVisual = nullptr;
	}

	UWorld* World = GetWorld();
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		HandleBuildingAdded(B);
	}
	SpawnDepositMarkers();
	if (Sim->GetQuotaPhase() == ERHQuotaPhase::Completed)
	{
		HandleShipArrived(Sim->GetManifestItems());
	}
	UE_LOG(LogRedHope, Display, TEXT("Colony visuals rebuilt from loaded state (%d buildings)"), Sim->GetBuildings().Num());
}

void URHColonyVisualizerSubsystem::SpawnDepositMarkers()
{
	UWorld* World = GetWorld();
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (const FRHDepositState& D : Sim->GetDeposits())
	{
		// Footprint scales gently with mass: 60 t ~ 16 m across.
		const float Side = FMath::Clamp(FMath::Sqrt(D.RemainingKg) * 0.065f, 8.f, 30.f);
		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(D.LocationCm + FVector(0, 0, 20.f), FRotator::ZeroRotator);
		if (!Actor)
		{
			continue;
		}
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
		Actor->SetActorScale3D(FVector(Side, Side, 0.4f));
#if WITH_EDITOR
		Actor->SetActorLabel(FString::Printf(TEXT("Sim_Dep_%s"), *D.RowName.ToString()));
#endif
		DepositMarkers.Add(Actor);
	}
}

void URHColonyVisualizerSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			Sim->OnBuildingAdded.Remove(AddedHandle);
			Sim->OnBuildingCompleted.Remove(CompletedHandle);
			Sim->OnCommandRejected.Remove(RejectedHandle);
		}
	}
	Super::Deinitialize();
}

FVector URHColonyVisualizerSubsystem::ScaleFor(const FRHBuildingInstance& Instance) const
{
	const URHDefinitionsSubsystem* Defs = GetWorld() ? GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
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
	// Construction sites read as foundations until a fabricator finishes them.
	if (Instance.bUnderConstruction)
	{
		Scale.Z = FMath::Min(Scale.Z, 0.4f);
	}
	return Scale;
}

void URHColonyVisualizerSubsystem::HandleBuildingAdded(const FRHBuildingInstance& Instance)
{
	UWorld* World = GetWorld();
	if (!World || BuildingVisuals.Contains(Instance.Id))
	{
		return;
	}

	const FVector Scale = ScaleFor(Instance);
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

void URHColonyVisualizerSubsystem::HandleBuildingCompleted(const FRHBuildingInstance& Instance)
{
	if (TObjectPtr<AStaticMeshActor>* Found = BuildingVisuals.Find(Instance.Id))
	{
		if (AStaticMeshActor* Actor = *Found)
		{
			const FVector Scale = ScaleFor(Instance); // no longer under construction: full height
			Actor->SetActorScale3D(Scale);
			Actor->SetActorLocation(Instance.LocationCm + FVector(0, 0, Scale.Z * 50.f));
		}
	}
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

void URHColonyVisualizerSubsystem::HandleQuotaMet(int32 Sol, double AwardKg)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 12.f, FColor::Green,
			FString::Printf(TEXT("CEO TRANSMISSION: Quota met (Sol %d). Ship authorized: %.0f kg. RH.Manifest / RH.Launch"), Sol, AwardKg));
	}
}

void URHColonyVisualizerSubsystem::HandleShipArrived(const TArray<FName>& Items)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// The landing beat, gray-box edition: a second lander on the east pad.
	if (ShipVisual)
	{
		return; // already on the pad (reload path re-fires the arrival state)
	}
	AStaticMeshActor* Ship = World->SpawnActor<AStaticMeshActor>(FVector(4000.f, -4000.f, 450.f), FRotator::ZeroRotator);
	if (Ship)
	{
		Ship->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Ship->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Ship->SetActorScale3D(FVector(6.f, 6.f, 9.f));
#if WITH_EDITOR
		Ship->SetActorLabel(TEXT("Sim_SupplyShip"));
#endif
		ShipVisual = Ship;
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Cyan,
			FString::Printf(TEXT("SUPPLY SHIP LANDED: %d items transferred. The Program continues."), Items.Num()));
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
