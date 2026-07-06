#include "RHColonyVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "DrawDebugHelpers.h"

FLinearColor URHColonyVisualizerSubsystem::TintFor(FName DefName) const
{
	// One hue per function family. Deliberately saturated: the Mars light is
	// dim and dusty, and these must read from 200 m up.
	static const TMap<FName, FLinearColor> Palette = {
		{ FName("Lander"),        FLinearColor(0.85f, 0.85f, 0.90f) },  // off-white: home
		{ FName("SolarArray"),    FLinearColor(0.08f, 0.22f, 0.70f) },  // PV blue
		{ FName("BatteryBank"),   FLinearColor(0.12f, 0.60f, 0.25f) },  // charge green
		{ FName("Pylon"),         FLinearColor(0.95f, 0.60f, 0.05f) },  // amber mast
		{ FName("ChargePad"),     FLinearColor(0.95f, 0.85f, 0.15f) },  // pad yellow
		{ FName("Forge"),         FLinearColor(0.75f, 0.15f, 0.08f) },  // furnace red
		{ FName("IceDrill"),      FLinearColor(0.50f, 0.85f, 0.95f) },  // ice cyan
		{ FName("WaterPlant"),    FLinearColor(0.15f, 0.40f, 0.85f) },  // water blue
		{ FName("Electrolyzer"),  FLinearColor(0.55f, 0.25f, 0.80f) },  // O2/H2 violet
		{ FName("Stockpile"),     FLinearColor(0.45f, 0.33f, 0.20f) },  // crate brown
		{ FName("ComputeModule"), FLinearColor(0.90f, 0.25f, 0.55f) },  // silicon magenta
		{ FName("Habitat"),       FLinearColor(0.95f, 0.95f, 0.95f) },
	};
	if (const FLinearColor* Found = Palette.Find(DefName))
	{
		return *Found;
	}
	return FLinearColor(0.5f, 0.5f, 0.5f);
}

void URHColonyVisualizerSubsystem::ApplyTint(AStaticMeshActor* Actor, const FLinearColor& Color) const
{
	if (Actor)
	{
		ApplyTint(Actor->GetStaticMeshComponent(), Color);
	}
}

void URHColonyVisualizerSubsystem::ApplyTint(UStaticMeshComponent* Mesh, const FLinearColor& Color) const
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
	if (!Base || !Mesh)
	{
		return;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
	Mid->SetVectorParameterValue(FName("Tint"), Color);
	Mesh->SetMaterial(0, Mid);
}

void URHColonyVisualizerSubsystem::AddAccent(AStaticMeshActor* Actor, const TCHAR* ShapePath, const FVector& WorldCm, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color) const
{
	UStaticMesh* Shape = LoadObject<UStaticMesh>(nullptr, ShapePath);
	if (!Actor || !Shape)
	{
		return;
	}
	UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Actor);
	Mesh->SetupAttachment(Actor->GetRootComponent());
	// Absolute: the parent carries a non-uniform footprint scale that would
	// smear any child shape. Buildings never move, so world-space is safe.
	Mesh->SetAbsolute(true, true, true);
	Mesh->RegisterComponent();
	Mesh->SetStaticMesh(Shape);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetWorldLocationAndRotation(WorldCm, Rot);
	Mesh->SetWorldScale3D(Scale);
	ApplyTint(Mesh, Color);
}

void URHColonyVisualizerSubsystem::BuildSilhouette(AStaticMeshActor* Actor, FName DefName, const FVector& BaseCm, const FVector& ScaleM) const
{
	static const TCHAR* Cyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* Cone = TEXT("/Engine/BasicShapes/Cone.Cone");
	static const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");

	const FLinearColor Tint = TintFor(DefName);
	const FLinearColor Steel(0.35f, 0.36f, 0.40f);
	const float TopZ = ScaleM.Z * 100.f; // box top above ground (actor center at Z*50)

	if (DefName == FName("Lander"))
	{
		// Return-vehicle nose cone + comms dish: the colony's monument.
		AddAccent(Actor, Cone, BaseCm + FVector(0, 0, TopZ), FRotator::ZeroRotator, FVector(3.2f, 3.2f, 2.8f), Tint);
		AddAccent(Actor, Sphere, BaseCm + FVector(180.f, 180.f, TopZ + 60.f), FRotator::ZeroRotator, FVector(0.9f), Steel);
	}
	else if (DefName == FName("BatteryBank"))
	{
		AddAccent(Actor, Cyl, BaseCm + FVector(-55.f, 0, TopZ), FRotator::ZeroRotator, FVector(0.55f, 0.55f, 0.9f), Steel);
		AddAccent(Actor, Cyl, BaseCm + FVector(55.f, 0, TopZ), FRotator::ZeroRotator, FVector(0.55f, 0.55f, 0.9f), Steel);
	}
	else if (DefName == FName("Pylon"))
	{
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, TopZ + 40.f), FRotator::ZeroRotator, FVector(0.8f), Tint);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ - 70.f), FRotator::ZeroRotator, FVector(2.4f, 0.25f, 0.25f), Steel);
	}
	else if (DefName == FName("ChargePad"))
	{
		AddAccent(Actor, Cyl, BaseCm + FVector(-130.f, -130.f, 60.f), FRotator::ZeroRotator, FVector(0.35f, 0.35f, 1.2f), Tint);
		AddAccent(Actor, Sphere, BaseCm + FVector(-130.f, -130.f, 130.f), FRotator::ZeroRotator, FVector(0.5f), Tint);
	}
	else if (DefName == FName("Forge"))
	{
		// The smelter reads by its stacks.
		AddAccent(Actor, Cyl, BaseCm + FVector(120.f, 160.f, TopZ + 150.f), FRotator::ZeroRotator, FVector(1.0f, 1.0f, 3.0f), Steel);
		AddAccent(Actor, Cyl, BaseCm + FVector(-100.f, 160.f, TopZ + 90.f), FRotator::ZeroRotator, FVector(0.7f, 0.7f, 1.8f), Steel);
	}
	else if (DefName == FName("IceDrill"))
	{
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, TopZ + 150.f), FRotator::ZeroRotator, FVector(0.7f, 0.7f, 3.0f), Tint);
		AddAccent(Actor, Cone, BaseCm + FVector(0, 0, TopZ + 320.f), FRotator::ZeroRotator, FVector(1.2f, 1.2f, 1.0f), Steel);
	}
	else if (DefName == FName("WaterPlant"))
	{
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, TopZ + 60.f), FRotator::ZeroRotator, FVector(2.6f), Tint);
	}
	else if (DefName == FName("Electrolyzer"))
	{
		// Twin gas tanks lying along Y: O2 and H2.
		AddAccent(Actor, Cyl, BaseCm + FVector(-80.f, 0, TopZ + 55.f), FRotator(90.f, 0, 0), FVector(1.1f, 1.1f, 2.6f), FLinearColor(0.75f, 0.85f, 0.95f));
		AddAccent(Actor, Cyl, BaseCm + FVector(80.f, 0, TopZ + 55.f), FRotator(90.f, 0, 0), FVector(1.1f, 1.1f, 2.6f), FLinearColor(0.9f, 0.55f, 0.75f));
	}
	else if (DefName == FName("Stockpile"))
	{
		AddAccent(Actor, Cube, BaseCm + FVector(-90.f, -60.f, 65.f), FRotator(0, 20.f, 0), FVector(0.9f), Steel);
		AddAccent(Actor, Cube, BaseCm + FVector(70.f, 80.f, 65.f), FRotator(0, -15.f, 0), FVector(0.9f), Steel);
		AddAccent(Actor, Cube, BaseCm + FVector(0, -10.f, 155.f), FRotator(0, 35.f, 0), FVector(0.8f), Steel);
	}
	else if (DefName == FName("ComputeModule"))
	{
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ + 120.f), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 2.4f), Steel);
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, TopZ + 240.f), FRotator::ZeroRotator, FVector(0.45f), Tint);
	}
	else if (DefName == FName("Habitat"))
	{
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, TopZ + 30.f), FRotator::ZeroRotator, FVector(5.5f, 5.5f, 3.5f), Tint);
	}
}

void URHColonyVisualizerSubsystem::AddLabel(AStaticMeshActor* Actor, const FString& Text, const FLinearColor& Color, float SouthOffsetCm) const
{
	if (!Actor)
	{
		return;
	}
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(Actor);
	Label->SetupAttachment(Actor->GetRootComponent());
	// Absolute: the parent actor carries a non-uniform footprint scale that
	// would smear the glyphs.
	Label->SetAbsolute(true, true, true);
	Label->RegisterComponent();
	Label->SetText(FText::FromString(Text));
	Label->SetWorldSize(220.f);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetTextRenderColor((Color * 0.4f + FLinearColor(0.6f, 0.6f, 0.6f)).ToFColor(true));
	// Lying flat on the ground, top toward +X: readable from the strategic
	// camera's default heading.
	Label->SetWorldLocationAndRotation(
		Actor->GetActorLocation() * FVector(1, 1, 0) + FVector(-SouthOffsetCm, 0, 12.f),
		FRotator(-90.f, 180.f, 0.f));
}

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
		// Ground-truth colors: regolith rust, ore slate, ice pale blue.
		FLinearColor DepColor(0.55f, 0.30f, 0.12f);
		if (D.Type == FName("Ore"))
		{
			DepColor = FLinearColor(0.30f, 0.32f, 0.40f);
		}
		else if (D.Type == FName("Ice"))
		{
			DepColor = FLinearColor(0.75f, 0.90f, 1.00f);
		}
		ApplyTint(Actor, DepColor);
		AddLabel(Actor, D.RowName.ToString(), DepColor, Side * 50.f + 260.f);
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
	// Solar arrays read as panels: tilted toward the equatorial sun, raised so
	// the low corner clears the ground.
	const bool bPanel = Instance.DefName == FName("SolarArray") && !Instance.bUnderConstruction;
	const FVector Location = Instance.LocationCm + FVector(0, 0, bPanel ? 70.f : Scale.Z * 50.f);
	const FRotator Facing = bPanel ? FRotator(0.f, 0.f, -18.f) : FRotator::ZeroRotator;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, Facing);
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
	// Legibility: family hue (construction sites sit dim until finished) + a
	// ground label so "which block is the Forge" never needs asking again.
	const FLinearColor Tint = TintFor(Instance.DefName);
	ApplyTint(Actor, Instance.bUnderConstruction ? Tint * 0.25f : Tint);
	AddLabel(Actor, Instance.DefName.ToString(), Tint, Scale.X * 50.f + 260.f);
	if (bPanel)
	{
		AddAccent(Actor, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), Instance.LocationCm + FVector(0, 0, 35.f),
			FRotator::ZeroRotator, FVector(0.3f, 0.3f, 0.7f), FLinearColor(0.35f, 0.36f, 0.40f));
	}
	else if (!Instance.bUnderConstruction)
	{
		BuildSilhouette(Actor, Instance.DefName, Instance.LocationCm, Scale);
	}
	BuildingVisuals.Add(Instance.Id, Actor);
}

void URHColonyVisualizerSubsystem::HandleBuildingCompleted(const FRHBuildingInstance& Instance)
{
	// Foundation -> finished form: rebuild the visual outright so the full
	// treatment (height, hue, tilt, silhouette accents) applies in one path.
	if (TObjectPtr<AStaticMeshActor>* Found = BuildingVisuals.Find(Instance.Id))
	{
		if (AStaticMeshActor* Actor = *Found)
		{
			Actor->Destroy();
		}
		BuildingVisuals.Remove(Instance.Id);
	}
	HandleBuildingAdded(Instance);
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
	// A rocket, not a box: cylinder hull + nose cone + landing legs.
	const FVector PadCm(4000.f, -4000.f, 0.f);
	AStaticMeshActor* Ship = World->SpawnActor<AStaticMeshActor>(PadCm + FVector(0, 0, 400.f), FRotator::ZeroRotator);
	if (Ship)
	{
		Ship->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Ship->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")));
		Ship->SetActorScale3D(FVector(4.f, 4.f, 7.f));
#if WITH_EDITOR
		Ship->SetActorLabel(TEXT("Sim_SupplyShip"));
#endif
		const FLinearColor ShipColor(0.92f, 0.92f, 0.98f);
		const FLinearColor Steel(0.35f, 0.36f, 0.40f);
		ApplyTint(Ship, ShipColor);
		AddAccent(Ship, TEXT("/Engine/BasicShapes/Cone.Cone"), PadCm + FVector(0, 0, 800.f), FRotator::ZeroRotator, FVector(4.f, 4.f, 3.f), ShipColor);
		for (int32 Leg = 0; Leg < 4; ++Leg)
		{
			const float Angle = Leg * 90.f + 45.f;
			const FVector Out = FRotator(0, Angle, 0).Vector() * 260.f;
			AddAccent(Ship, TEXT("/Engine/BasicShapes/Cube.Cube"), PadCm + Out + FVector(0, 0, 90.f),
				FRotator(0, Angle, 25.f), FVector(0.35f, 0.35f, 2.4f), Steel);
		}
		AddLabel(Ship, TEXT("Supply Ship"), ShipColor, 620.f);
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
