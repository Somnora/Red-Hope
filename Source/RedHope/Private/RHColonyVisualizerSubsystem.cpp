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

namespace RHCanon
{
	// The director's reference palette, distilled.
	const FLinearColor BoneWhite(0.62f, 0.60f, 0.54f);
	const FLinearColor DarkSlate(0.10f, 0.11f, 0.13f);
	const FLinearColor HazYellow(0.85f, 0.62f, 0.05f);
	const FLinearColor RustBeam(0.48f, 0.16f, 0.07f);
	const FLinearColor PVBlue(0.06f, 0.12f, 0.35f);
	const FLinearColor FurnaceGlow(6.0f, 1.6f, 0.15f);   // HDR emissive
	const FLinearColor TealGlow(0.1f, 4.5f, 3.2f);
	const FLinearColor IceGlow(0.8f, 2.8f, 5.0f);
	const FLinearColor AmberGlow(4.5f, 2.2f, 0.2f);
}

FLinearColor URHColonyVisualizerSubsystem::TintFor(FName DefName) const
{
	// Function accent hue: drives labels and each machine's glow identity.
	static const TMap<FName, FLinearColor> Palette = {
		{ FName("Lander"),        FLinearColor(0.85f, 0.85f, 0.90f) },
		{ FName("SolarArray"),    FLinearColor(0.25f, 0.45f, 0.95f) },  // PV blue
		{ FName("BatteryBank"),   FLinearColor(0.10f, 0.85f, 0.65f) },  // cell teal
		{ FName("Pylon"),         FLinearColor(0.95f, 0.60f, 0.05f) },  // grid amber
		{ FName("ChargePad"),     FLinearColor(0.95f, 0.75f, 0.15f) },  // pad amber
		{ FName("Forge"),         FLinearColor(0.95f, 0.35f, 0.08f) },  // furnace orange
		{ FName("IceDrill"),      FLinearColor(0.50f, 0.85f, 0.95f) },  // ice cyan
		{ FName("WaterPlant"),    FLinearColor(0.25f, 0.55f, 0.95f) },  // water blue
		{ FName("Electrolyzer"),  FLinearColor(0.55f, 0.35f, 0.90f) },  // O2/H2 violet
		{ FName("Stockpile"),     FLinearColor(0.85f, 0.55f, 0.15f) },  // crate amber
		{ FName("ComputeModule"), FLinearColor(0.90f, 0.25f, 0.55f) },
		{ FName("Habitat"),       FLinearColor(0.95f, 0.95f, 0.95f) },
	};
	if (const FLinearColor* Found = Palette.Find(DefName))
	{
		return *Found;
	}
	return FLinearColor(0.5f, 0.5f, 0.5f);
}

FLinearColor URHColonyVisualizerSubsystem::BodyFor(FName DefName) const
{
	// Reference sheet: heavy industry sits on dark slate; habitats, power and
	// processing wear bone-white hulls.
	if (DefName == FName("Forge") || DefName == FName("IceDrill")
		|| DefName == FName("ChargePad") || DefName == FName("Stockpile"))
	{
		return RHCanon::DarkSlate;
	}
	if (DefName == FName("Pylon"))
	{
		return FLinearColor(0.22f, 0.23f, 0.26f);
	}
	if (DefName == FName("SolarArray"))
	{
		return RHCanon::PVBlue; // the tilted box IS the panel
	}
	return RHCanon::BoneWhite;
}

void URHColonyVisualizerSubsystem::ApplyTint(AStaticMeshActor* Actor, const FLinearColor& Color, const FLinearColor& Emissive) const
{
	if (Actor)
	{
		ApplyTint(Actor->GetStaticMeshComponent(), Color, Emissive);
	}
}

void URHColonyVisualizerSubsystem::ApplyTint(UStaticMeshComponent* Mesh, const FLinearColor& Color, const FLinearColor& Emissive) const
{
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
	if (!Base || !Mesh)
	{
		return;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Mesh);
	Mid->SetVectorParameterValue(FName("Tint"), Color);
	Mid->SetVectorParameterValue(FName("Emissive"), Emissive);
	Mesh->SetMaterial(0, Mid);
}

void URHColonyVisualizerSubsystem::AddAccent(AStaticMeshActor* Actor, const TCHAR* ShapePath, const FVector& WorldCm, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color, const FLinearColor& Emissive) const
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
	ApplyTint(Mesh, Color, Emissive);
}

void URHColonyVisualizerSubsystem::BuildSilhouette(AStaticMeshActor* Actor, FName DefName, const FVector& BaseCm, const FVector& ScaleM) const
{
	static const TCHAR* Cyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* Cone = TEXT("/Engine/BasicShapes/Cone.Cone");
	static const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");

	using namespace RHCanon;
	const float TopZ = ScaleM.Z * 100.f; // box top above ground (actor center at Z*50)

	if (DefName == FName("Lander"))
	{
		// Reference: bone-white hull, dark articulated legs, dome + dish.
		AddAccent(Actor, Cone, BaseCm + FVector(0, 0, TopZ), FRotator::ZeroRotator, FVector(3.2f, 3.2f, 2.8f), BoneWhite);
		AddAccent(Actor, Sphere, BaseCm + FVector(180.f, 180.f, TopZ + 60.f), FRotator::ZeroRotator, FVector(0.9f), DarkSlate);
		for (int32 Leg = 0; Leg < 4; ++Leg)
		{
			const FVector Out = FRotator(0, Leg * 90.f + 45.f, 0).Vector() * (ScaleM.X * 50.f + 60.f);
			AddAccent(Actor, Cube, BaseCm + Out + FVector(0, 0, 70.f), FRotator(0, Leg * 90.f + 45.f, 22.f), FVector(0.3f, 0.3f, 1.8f), DarkSlate);
		}
	}
	else if (DefName == FName("BatteryBank"))
	{
		// Reference: pale racks holding glowing teal cell banks.
		AddAccent(Actor, Cube, BaseCm + FVector(0, ScaleM.Y * 50.f + 6.f, ScaleM.Z * 50.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.75f, 0.08f, ScaleM.Z * 0.6f), TintFor(DefName), TealGlow);
		AddAccent(Actor, Cube, BaseCm + FVector(0, -ScaleM.Y * 50.f - 6.f, ScaleM.Z * 50.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.75f, 0.08f, ScaleM.Z * 0.6f), TintFor(DefName), TealGlow);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ + 10.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.9f, ScaleM.Y * 0.9f, 0.15f), HazYellow);
	}
	else if (DefName == FName("Pylon"))
	{
		// Grid node: amber glow cap - power made visible, on brand with the
		// coverage discs.
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, TopZ + 40.f), FRotator::ZeroRotator, FVector(0.8f), TintFor(DefName), AmberGlow);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ - 70.f), FRotator::ZeroRotator, FVector(2.4f, 0.25f, 0.25f), DarkSlate);
	}
	else if (DefName == FName("ChargePad"))
	{
		AddAccent(Actor, Cyl, BaseCm + FVector(-130.f, -130.f, 60.f), FRotator::ZeroRotator, FVector(0.35f, 0.35f, 1.2f), BoneWhite);
		AddAccent(Actor, Sphere, BaseCm + FVector(-130.f, -130.f, 130.f), FRotator::ZeroRotator, FVector(0.5f), TintFor(DefName), AmberGlow);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, 28.f), FRotator(0, 45.f, 0), FVector(ScaleM.X * 0.7f, 0.18f, 0.06f), HazYellow);
	}
	else if (DefName == FName("Forge"))
	{
		// Reference: dark smelter block, rust gantry beam across the roof on
		// posts, twin furnace mouths glowing on the south face, hazard braces.
		const float BeamZ = TopZ + 170.f;
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, BeamZ), FRotator::ZeroRotator, FVector(1.1f, ScaleM.Y + 1.6f, 0.9f), RustBeam);
		AddAccent(Actor, Cube, BaseCm + FVector(0, ScaleM.Y * 50.f + 55.f, BeamZ * 0.5f), FRotator::ZeroRotator, FVector(0.45f, 0.45f, BeamZ / 100.f), DarkSlate);
		AddAccent(Actor, Cube, BaseCm + FVector(0, -ScaleM.Y * 50.f - 55.f, BeamZ * 0.5f), FRotator::ZeroRotator, FVector(0.45f, 0.45f, BeamZ / 100.f), DarkSlate);
		AddAccent(Actor, Cube, BaseCm + FVector(-ScaleM.X * 50.f - 4.f, -110.f, 110.f), FRotator::ZeroRotator, FVector(0.1f, 1.4f, 1.6f), TintFor(DefName), FurnaceGlow);
		AddAccent(Actor, Cube, BaseCm + FVector(-ScaleM.X * 50.f - 4.f, 110.f, 110.f), FRotator::ZeroRotator, FVector(0.1f, 1.4f, 1.6f), TintFor(DefName), FurnaceGlow);
		AddAccent(Actor, Cube, BaseCm + FVector(ScaleM.X * 50.f - 30.f, ScaleM.Y * 50.f - 30.f, 90.f), FRotator::ZeroRotator, FVector(0.7f, 0.7f, 1.8f), HazYellow);
	}
	else if (DefName == FName("IceDrill"))
	{
		// Reference: slate block, drill tower, ice-glow inspection window.
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, TopZ + 150.f), FRotator::ZeroRotator, FVector(0.7f, 0.7f, 3.0f), BoneWhite);
		AddAccent(Actor, Cone, BaseCm + FVector(0, 0, TopZ + 320.f), FRotator::ZeroRotator, FVector(1.2f, 1.2f, 1.0f), DarkSlate);
		AddAccent(Actor, Cube, BaseCm + FVector(-ScaleM.X * 50.f - 4.f, 0, 120.f), FRotator::ZeroRotator, FVector(0.1f, 1.6f, 1.2f), TintFor(DefName), IceGlow);
	}
	else if (DefName == FName("WaterPlant"))
	{
		// Reference: the tank farm - domed white cylinders around the block.
		for (int32 T = 0; T < 3; ++T)
		{
			const FVector TankCm = BaseCm + FVector((T - 1) * 190.f, ScaleM.Y * 50.f + 110.f, 0);
			AddAccent(Actor, Cyl, TankCm + FVector(0, 0, 160.f), FRotator::ZeroRotator, FVector(1.4f, 1.4f, 3.2f), BoneWhite);
			AddAccent(Actor, Sphere, TankCm + FVector(0, 0, 320.f), FRotator::ZeroRotator, FVector(1.35f, 1.35f, 0.9f), BoneWhite);
		}
		AddAccent(Actor, Cube, BaseCm + FVector(-ScaleM.X * 50.f - 4.f, 0, 120.f), FRotator::ZeroRotator, FVector(0.1f, 1.6f, 1.2f), TintFor(DefName), IceGlow);
	}
	else if (DefName == FName("Electrolyzer"))
	{
		// Twin gas tanks lying along Y: O2 and H2, with a teal process strip.
		AddAccent(Actor, Cyl, BaseCm + FVector(-80.f, 0, TopZ + 55.f), FRotator(90.f, 0, 0), FVector(1.1f, 1.1f, 2.6f), BoneWhite);
		AddAccent(Actor, Cyl, BaseCm + FVector(80.f, 0, TopZ + 55.f), FRotator(90.f, 0, 0), FVector(1.1f, 1.1f, 2.6f), BoneWhite);
		AddAccent(Actor, Cube, BaseCm + FVector(0, -ScaleM.Y * 50.f - 4.f, 130.f), FRotator::ZeroRotator, FVector(1.8f, 0.1f, 0.5f), TintFor(DefName), TealGlow);
	}
	else if (DefName == FName("Stockpile"))
	{
		// Reference: amber cargo crates (the lander-bay language).
		AddAccent(Actor, Cube, BaseCm + FVector(-90.f, -60.f, 65.f), FRotator(0, 20.f, 0), FVector(0.9f), TintFor(DefName));
		AddAccent(Actor, Cube, BaseCm + FVector(70.f, 80.f, 65.f), FRotator(0, -15.f, 0), FVector(0.9f), TintFor(DefName));
		AddAccent(Actor, Cube, BaseCm + FVector(0, -10.f, 155.f), FRotator(0, 35.f, 0), FVector(0.8f), BoneWhite);
	}
	else if (DefName == FName("ComputeModule"))
	{
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ + 120.f), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 2.4f), DarkSlate);
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, TopZ + 240.f), FRotator::ZeroRotator, FVector(0.45f), TintFor(DefName), FLinearColor(3.5f, 0.9f, 2.0f));
	}
	else if (DefName == FName("Habitat"))
	{
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, TopZ + 30.f), FRotator::ZeroRotator, FVector(5.5f, 5.5f, 3.5f), BoneWhite);
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
	// Canon body color (construction sites sit dim until finished) + a ground
	// label in the function hue so "which block is the Forge" never needs asking.
	const FLinearColor Tint = TintFor(Instance.DefName);
	const FLinearColor Body = BodyFor(Instance.DefName);
	ApplyTint(Actor, Instance.bUnderConstruction ? Body * 0.3f : Body);
	AddLabel(Actor, Instance.DefName.ToString(), Tint, Scale.X * 50.f + 260.f);
	if (bPanel)
	{
		AddAccent(Actor, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), Instance.LocationCm + FVector(0, 0, 35.f),
			FRotator::ZeroRotator, FVector(0.3f, 0.3f, 0.7f), RHCanon::DarkSlate);
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
	// Reference sheet CargoLander: horizontal bone-white freighter hull on
	// dark legs, wedge cockpit, amber cargo bay glowing at the flank.
	using namespace RHCanon;
	const FVector PadCm(4000.f, -4000.f, 0.f);
	AStaticMeshActor* Ship = World->SpawnActor<AStaticMeshActor>(PadCm + FVector(0, 0, 330.f), FRotator::ZeroRotator);
	if (Ship)
	{
		Ship->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Ship->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Ship->SetActorScale3D(FVector(11.f, 4.5f, 3.2f)); // long hull, nose +X
#if WITH_EDITOR
		Ship->SetActorLabel(TEXT("Sim_SupplyShip"));
#endif
		ApplyTint(Ship, BoneWhite);
		// Wedge cockpit (dark glass) at the nose.
		AddAccent(Ship, TEXT("/Engine/BasicShapes/Cube.Cube"), PadCm + FVector(620.f, 0, 300.f),
			FRotator(0, 0, -28.f), FVector(2.2f, 3.8f, 1.6f), FLinearColor(0.05f, 0.07f, 0.09f));
		// Amber cargo bay, open on the south flank.
		AddAccent(Ship, TEXT("/Engine/BasicShapes/Cube.Cube"), PadCm + FVector(-120.f, -230.f, 260.f),
			FRotator::ZeroRotator, FVector(3.4f, 0.12f, 1.8f), TintFor(FName("Stockpile")), AmberGlow * 0.5f);
		// Dorsal dome + four dark legs.
		AddAccent(Ship, TEXT("/Engine/BasicShapes/Sphere.Sphere"), PadCm + FVector(-350.f, 0, 510.f),
			FRotator::ZeroRotator, FVector(1.6f, 1.6f, 1.0f), DarkSlate);
		for (int32 Leg = 0; Leg < 4; ++Leg)
		{
			const FVector Out((Leg % 2 ? 1.f : -1.f) * 380.f, (Leg < 2 ? 1.f : -1.f) * 220.f, 0);
			AddAccent(Ship, TEXT("/Engine/BasicShapes/Cube.Cube"), PadCm + Out + FVector(0, 0, 90.f),
				FRotator(0, 0, (Leg < 2 ? -1.f : 1.f) * 18.f), FVector(0.35f, 0.35f, 1.9f), DarkSlate);
		}
		AddLabel(Ship, TEXT("Supply Ship"), BoneWhite, 620.f);
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
