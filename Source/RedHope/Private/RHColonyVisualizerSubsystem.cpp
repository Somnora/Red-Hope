#include "RHColonyVisualizerSubsystem.h"
#include "RHHabLayout.h"
#include "RedHope.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHAgentVisualizerSubsystem.h"
#include "RHCrewVisualizerSubsystem.h"
#include "RHElevatorLayout.h"
#include "RHFrameStats.h"
#include "RHMarsTerrain.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "ProceduralMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "Misc/Crc.h"
#include "DrawDebugHelpers.h"
#include "HAL/IConsoleManager.h"

namespace RHCanon
{
	// The director's reference palette, distilled. Bone sits darker than the
	// sunlit regolith so white hulls separate from the ground instead of
	// washing into it.
	const FLinearColor BoneWhite(0.52f, 0.50f, 0.45f);
	const FLinearColor DarkSlate(0.10f, 0.11f, 0.13f);
	const FLinearColor HazYellow(0.85f, 0.62f, 0.05f);
	const FLinearColor RustBeam(0.48f, 0.16f, 0.07f);
	const FLinearColor PVBlue(0.06f, 0.12f, 0.35f);
	const FLinearColor FurnaceGlow(6.0f, 1.6f, 0.15f);   // HDR emissive
	const FLinearColor TealGlow(0.1f, 4.5f, 3.2f);
	const FLinearColor IceGlow(0.8f, 2.8f, 5.0f);
	const FLinearColor AmberGlow(4.5f, 2.2f, 0.2f);
}

// The Lander body rides this high on its legs so the descent engine + gear read.
static constexpr float GLanderLiftCm = 210.f;

// Model-set A/B. `rh.ModelSetV2 1` (default) renders the MIXED SET - the
// painted 2026-07-17 mesh for the buildings where it reads better, the original
// everywhere else (see ModelPathsV2 in HandleBuildingAdded for the per-building
// reasoning). `rh.ModelSetV2 0` restores the all-original set, so both can be
// judged in ONE boot instead of across a recompile. A building picks its mesh
// when it spawns, so re-run RH.Demo (or reload a save) after toggling.
static TAutoConsoleVariable<int32> CVarModelSetV2(
	TEXT("rh.ModelSetV2"),
	1,
	TEXT("Building models: 1 = the mixed set (painted mesh where it reads better), 0 = all originals."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarHabRoof(
	TEXT("rh.HabRoof"),
	0,
	TEXT("Surface habitats: 0 = open-top cutaway (see inside from the strategy camera), 1 = roofed."),
	ECVF_Default);

// Sims-style interior viewing. The pit has no roof to lift - it is an open
// excavation - so the thing that hides an interior is the near wall faces.
static TAutoConsoleVariable<int32> CVarCutaway(
	TEXT("rh.Cutaway"),
	0,
	TEXT("Interior view: 0 = all walls, 1 = drop the wall faces toward the camera (swaps as you orbit), 2 = floorplan, no walls."),
	ECVF_Default);

// The four wall-face outward directions, in the order their bits are packed
// into the cutaway hidden-mask.
static const FIntPoint GRHWallDirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };

static int32 RHWallDirSlot(const FIntPoint& D)
{
	for (int32 i = 0; i < 4; ++i)
	{
		if (GRHWallDirs[i] == D)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

FLinearColor URHColonyVisualizerSubsystem::TintFor(FName DefName) const
{
	// Function accent hue: drives labels and each machine's glow identity.
	static const TMap<FName, FLinearColor> Palette = {
		{ FName("Lander"),        FLinearColor(0.85f, 0.85f, 0.90f) },
		{ FName("SolarArray"),    FLinearColor(0.25f, 0.45f, 0.95f) },  // PV blue
		{ FName("BatteryBank"),   FLinearColor(0.10f, 0.85f, 0.65f) },  // cell teal
		{ FName("Pylon"),         FLinearColor(0.95f, 0.60f, 0.05f) },  // grid amber
		{ FName("Floodmast"),     FLinearColor(1.00f, 0.86f, 0.66f) },  // warm work light
		{ FName("ChargePad"),     FLinearColor(0.95f, 0.75f, 0.15f) },  // pad amber
		{ FName("Forge"),         FLinearColor(0.95f, 0.35f, 0.08f) },  // furnace orange
		{ FName("IceDrill"),      FLinearColor(0.50f, 0.85f, 0.95f) },  // ice cyan
		{ FName("WaterPlant"),    FLinearColor(0.25f, 0.55f, 0.95f) },  // water blue
		{ FName("Electrolyzer"),  FLinearColor(0.55f, 0.35f, 0.90f) },  // O2/H2 violet
		{ FName("Stockpile"),     FLinearColor(0.85f, 0.55f, 0.15f) },  // crate amber
		{ FName("ComputeModule"), FLinearColor(0.90f, 0.25f, 0.55f) },
		{ FName("Habitat"),       FLinearColor(0.95f, 0.95f, 0.95f) },
		{ FName("AirFilter"),     FLinearColor(0.30f, 0.85f, 0.80f) },  // life-support teal
		{ FName("Borer"),         FLinearColor(0.90f, 0.55f, 0.10f) },  // mining amber
		{ FName("LabModule"),     FLinearColor(0.35f, 0.70f, 1.00f) },  // research blue
		{ FName("InfirmaryModule"), FLinearColor(0.55f, 0.95f, 0.75f) }, // calm mint
		{ FName("GreenhouseDome"), FLinearColor(0.45f, 0.85f, 0.35f) }, // leaf green
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
		|| DefName == FName("ChargePad") || DefName == FName("Stockpile")
		|| DefName == FName("ComputeModule"))
	{
		return RHCanon::DarkSlate;
	}
	if (DefName == FName("Pylon") || DefName == FName("Floodmast"))
	{
		return FLinearColor(0.22f, 0.23f, 0.26f);
	}
	if (DefName == FName("SolarArray"))
	{
		// Reference (Facilities/SolarPanel): the flat deck is the anchor plate /
		// conduit pad; the white hub and radiating blue arrays sit on top.
		return FLinearColor(0.16f, 0.17f, 0.20f);
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

bool URHColonyVisualizerSubsystem::ApplySurface(UStaticMeshComponent* Mesh, const TCHAR* TexPath, float TileCm, const FLinearColor& Tint, float Rough) const
{
	UMaterialInterface* Surf = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_MarsSurface.M_MarsSurface"));
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, TexPath);
	if (!Surf || !Tex || !Mesh)
	{
		return false;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Surf, Mesh);
	Mid->SetTextureParameterValue(FName("SurfTex"), Tex);
	Mid->SetScalarParameterValue(FName("TileCm"), TileCm);
	Mid->SetVectorParameterValue(FName("Tint"), Tint);
	Mid->SetScalarParameterValue(FName("Rough"), Rough);
	// Matching normal map by convention: Foo.Foo -> Foo_Normal.Foo_Normal.
	// Absent one, zero the bump rather than let the material's default normal
	// (regolith) bump a non-regolith surface.
	FString PkgName, AssetName;
	UTexture2D* Norm = nullptr;
	if (FString(TexPath).Split(TEXT("."), &PkgName, &AssetName))
	{
		Norm = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s_Normal.%s_Normal"), *PkgName, *AssetName));
	}
	// Only trust a map RHArt has configured: a freshly imported-but-unfixed
	// normal (sRGB on, TC_Default) decodes wrong and shades WORSE than flat.
	if (Norm && Norm->CompressionSettings == TC_Normalmap)
	{
		Mid->SetTextureParameterValue(FName("NormTex"), Norm);
	}
	else
	{
		Mid->SetScalarParameterValue(FName("NormalStrength"), 0.f);
	}
	Mesh->SetMaterial(0, Mid);
	return true;
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

void URHColonyVisualizerSubsystem::BuildGreenhouseDome(AStaticMeshActor* Actor, const FRHBuildingInstance& B, const FVector& Seated) const
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	const URHDefinitionsSubsystem* Defs = GetWorld() ? GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
	if (!Actor || !Cube || !Def)
	{
		return;
	}
	const FVector2D Half = URHSimWorldSubsystem::GetFootprintHalfCm(Def, B.SizeX, B.SizeY);
	const int32 NX = FMath::Max(1, B.SizeX), NY = FMath::Max(1, B.SizeY);
	const bool bBuilt = !B.bUnderConstruction;
	const FVector Base = Seated + FVector(0, 0, 30.f); // the deck's top face

	// One ISM per material family, world-space instances (the actor's root is
	// the non-uniformly scaled deck - children must not inherit it).
	const auto MakeISM = [&](const TCHAR* Name) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(Actor, Name);
		ISM->SetupAttachment(Actor->GetRootComponent());
		ISM->SetAbsolute(true, true, true);
		ISM->SetStaticMesh(Cube);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->RegisterComponent();
		Actor->AddInstanceComponent(ISM);
		return ISM;
	};
	const auto Beam = [](UInstancedStaticMeshComponent* ISM, const FVector& A, const FVector& Bp, float ThickCm)
	{
		const FVector D = Bp - A;
		const float Len = D.Size();
		if (Len < 1.f)
		{
			return;
		}
		ISM->AddInstance(FTransform(FRotationMatrix::MakeFromX(D / Len).Rotator(), (A + Bp) * 0.5f,
			FVector(Len / 100.f, ThickCm / 100.f, ThickCm / 100.f)), /*bWorldSpace*/ true);
	};
	// A pane on the quad P00-P10-P11-P01 (U runs P00->P10), inset so the
	// struts read between panes.
	const auto Pane = [](UInstancedStaticMeshComponent* ISM, const FVector& P00, const FVector& P10, const FVector& P11, const FVector& P01)
	{
		const FVector C = (P00 + P10 + P11 + P01) * 0.25f;
		const FVector U = ((P10 + P11) - (P00 + P01)).GetSafeNormal();
		const FVector V = ((P01 + P11) - (P00 + P10)).GetSafeNormal();
		const float W = 0.5f * ((P10 - P00).Size() + (P11 - P01).Size());
		const float H = 0.5f * ((P01 - P00).Size() + (P11 - P10).Size());
		if (W < 1.f || H < 1.f || U.IsNearlyZero() || V.IsNearlyZero())
		{
			return;
		}
		ISM->AddInstance(FTransform(FRotationMatrix::MakeFromXY(U, V).Rotator(), C,
			FVector(W * 0.94f / 100.f, H * 0.94f / 100.f, 0.03f)), /*bWorldSpace*/ true);
	};

	UInstancedStaticMeshComponent* Struts = MakeISM(TEXT("DomeStruts"));
	ApplyTint(Struts, FLinearColor(0.11f, 0.12f, 0.14f), FLinearColor::Black);
	UInstancedStaticMeshComponent* Panes = bBuilt ? MakeISM(TEXT("DomePanes")) : nullptr;
	if (Panes)
	{
		Panes->SetCastShadow(false); // glass: the sun reaches the beds
		if (UMaterialInterface* Glass = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass")))
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Glass, Panes);
			Mid->SetVectorParameterValue(FName("Tint"), FLinearColor(0.58f, 0.74f, 0.80f));
			Mid->SetScalarParameterValue(FName("Opacity"), 0.2f);
			Panes->SetMaterial(0, Mid);
		}
		else
		{
			// No glass material: a pale emissive-free tint still reads as panes.
			ApplyTint(Panes, FLinearColor(0.55f, 0.70f, 0.78f), FLinearColor::Black);
			UE_LOG(LogRedHope, Warning, TEXT("M_DomeGlass missing - dome panes draw opaque (run scripts/unreal/rh_make_glass.py)"));
		}
	}

	if (NX * NY < 4)
	{
		// A small greenhouse: a flat-roofed glass house, ~2.5 m mullions.
		const float Hw = 340.f;
		const int32 SegX = FMath::Max(2, FMath::RoundToInt32(2.f * Half.X / 250.f));
		const int32 SegY = FMath::Max(2, FMath::RoundToInt32(2.f * Half.Y / 250.f));
		const auto Corner = [&](float FX, float FY, float Z) { return Base + FVector(FX * Half.X, FY * Half.Y, Z); };
		for (int32 i = 0; i <= SegX; ++i)
		{
			const float FX = -1.f + 2.f * i / SegX;
			for (const float FY : { -1.f, 1.f })
			{
				Beam(Struts, Corner(FX, FY, 0.f), Corner(FX, FY, Hw), 10.f);
			}
			Beam(Struts, Corner(FX, -1.f, Hw), Corner(FX, 1.f, Hw), 8.f); // roof rafters
		}
		for (int32 j = 0; j <= SegY; ++j)
		{
			const float FY = -1.f + 2.f * j / SegY;
			for (const float FX : { -1.f, 1.f })
			{
				Beam(Struts, Corner(FX, FY, 0.f), Corner(FX, FY, Hw), 10.f);
			}
		}
		for (const float Z : { 0.f, Hw })
		{
			Beam(Struts, Corner(-1, -1, Z), Corner(1, -1, Z), 12.f);
			Beam(Struts, Corner(-1, 1, Z), Corner(1, 1, Z), 12.f);
			Beam(Struts, Corner(-1, -1, Z), Corner(-1, 1, Z), 12.f);
			Beam(Struts, Corner(1, -1, Z), Corner(1, 1, Z), 12.f);
		}
		if (Panes)
		{
			for (int32 i = 0; i < SegX; ++i)
			{
				const float A = -1.f + 2.f * i / SegX, Bx = -1.f + 2.f * (i + 1) / SegX;
				for (const float FY : { -1.f, 1.f })
				{
					Pane(Panes, Corner(A, FY, 0.f), Corner(Bx, FY, 0.f), Corner(Bx, FY, Hw), Corner(A, FY, Hw));
				}
				for (int32 j = 0; j < SegY; ++j)
				{
					const float C0 = -1.f + 2.f * j / SegY, C1 = -1.f + 2.f * (j + 1) / SegY;
					Pane(Panes, Corner(A, C0, Hw), Corner(Bx, C0, Hw), Corner(Bx, C1, Hw), Corner(A, C1, Hw));
				}
			}
			for (int32 j = 0; j < SegY; ++j)
			{
				const float C0 = -1.f + 2.f * j / SegY, C1 = -1.f + 2.f * (j + 1) / SegY;
				for (const float FX : { -1.f, 1.f })
				{
					Pane(Panes, Corner(FX, C0, 0.f), Corner(FX, C1, 0.f), Corner(FX, C1, Hw), Corner(FX, C0, Hw));
				}
			}
		}
	}
	else
	{
		// A dome: a squircle-section cap (|x|^4 + |y|^4 ~ 1, so it covers the
		// rectangular bed grid) on a 60 cm knee wall; height scales with the
		// footprint's short side. Rings of latitude x meridians, one pane per
		// quad, struts on every edge.
		const float Knee = 60.f;
		const float H = 0.42f * 2.f * FMath::Min(Half.X, Half.Y) + 250.f;
		const int32 NTheta = FMath::Clamp(FMath::RoundToInt32(2.f * PI * FMath::Max(Half.X, Half.Y) / 320.f), 20, 48) & ~1;
		const int32 NPhi = 7;
		const auto Sq = [](float T) { return FMath::Sign(T) * FMath::Pow(FMath::Abs(T), 0.35f); };
		// The rectangle's corner beds (inset 80 cm) sit just inside the
		// rounded square at 1.04x: |cos 45|^0.35 x 1.04 = 0.92 of the half.
		const auto P = [&](int32 i, int32 k)
		{
			if (k >= NPhi)
			{
				return Base + FVector(0, 0, Knee + H);
			}
			const float Theta = 2.f * PI * i / NTheta;
			const float Phi = 0.5f * PI * k / NPhi;
			const float R = FMath::Cos(Phi) * 1.04f;
			return Base + FVector(Half.X * Sq(FMath::Cos(Theta)) * R, Half.Y * Sq(FMath::Sin(Theta)) * R, Knee + H * FMath::Sin(Phi));
		};
		for (int32 i = 0; i < NTheta; ++i)
		{
			const int32 I1 = (i + 1) % NTheta;
			// Knee wall: a post per meridian and a sill ring.
			Beam(Struts, P(i, 0) - FVector(0, 0, Knee), P(i, 0), 14.f);
			Beam(Struts, P(i, 0) - FVector(0, 0, Knee), P(I1, 0) - FVector(0, 0, Knee), 16.f);
			for (int32 k = 0; k < NPhi; ++k)
			{
				Beam(Struts, P(i, k), P(i, k + 1), 9.f);  // meridian
				Beam(Struts, P(i, k), P(I1, k), 7.f);     // ring
				if (Panes)
				{
					Pane(Panes, P(i, k), P(I1, k), P(I1, k + 1), P(i, k + 1));
				}
			}
			if (Panes)
			{
				// Knee-wall glazing.
				Pane(Panes, P(i, 0) - FVector(0, 0, Knee), P(I1, 0) - FVector(0, 0, Knee), P(I1, 0), P(i, 0));
			}
		}
	}

	// The airlock: a small opaque vestibule on the -Y face.
	AddAccent(Actor, TEXT("/Engine/BasicShapes/Cube.Cube"), Base + FVector(0, -Half.Y - 140.f, 140.f), FRotator::ZeroRotator,
		FVector(2.6f, 2.8f, 2.8f), FLinearColor(0.78f, 0.77f, 0.74f));
	AddAccent(Actor, TEXT("/Engine/BasicShapes/Cube.Cube"), Base + FVector(0, -Half.Y - 282.f, 120.f), FRotator::ZeroRotator,
		FVector(1.2f, 0.06f, 2.1f), FLinearColor(0.12f, 0.13f, 0.15f));

	if (bBuilt)
	{
		// Soil beds: one per 10 m bed, a dark tilled box with a walkway margin.
		UInstancedStaticMeshComponent* Beds = MakeISM(TEXT("DomeBeds"));
		ApplyTint(Beds, FLinearColor(0.20f, 0.13f, 0.085f), FLinearColor::Black);
		const float BedX = 2.f * Half.X / NX, BedY = 2.f * Half.Y / NY;
		for (int32 by = 0; by < NY; ++by)
		{
			for (int32 bx = 0; bx < NX; ++bx)
			{
				const FVector C = Base + FVector(-Half.X + (bx + 0.5f) * BedX, -Half.Y + (by + 0.5f) * BedY, 22.f);
				Beds->AddInstance(FTransform(FRotator::ZeroRotator, C, FVector((BedX - 160.f) / 100.f, (BedY - 160.f) / 100.f, 0.44f)), true);
			}
		}
	}
}

void URHColonyVisualizerSubsystem::RefreshHabShells()
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Sim || !Defs || !Cube)
	{
		return;
	}
	const int32 Cutaway = FMath::Clamp(CVarCutaway.GetValueOnGameThread(), 0, 2);
	const bool bRoof = CVarHabRoof.GetValueOnGameThread() != 0 && Cutaway == 0;
	static const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	// One decompression chamber per network (director 2026-10-16; see RHHabLayout.h).
	TArray<FRHAirlock> Airlocks;
	RHHabLayout::GatherAirlocks(Sim, Defs, Airlocks);
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
		if (!Def || !Def->Pressurized || B.Level != 0 || B.bUnderConstruction)
		{
			continue;
		}
		AStaticMeshActor* Actor = BuildingVisuals.FindRef(B.Id).Get();
		if (!Actor)
		{
			continue;
		}
		const FIntPoint G = Sim->GetBayGrid(B);
		const int32 Root = Sim->GetHabNetworkRoot(B.Id);
		const bool bRated = Sim->IsHabRated(B.Id);
		// The network's airlocks cut into THIS shell (the player's, BM-6, or
		// the derived entrance).
		TArray<const FRHAirlock*> Locks;
		for (const FRHAirlock& L : Airlocks)
		{
			if (L.HabId == B.Id)
			{
				Locks.Add(&L);
			}
		}
		// Edge kinds: 0 exterior wall, 1 open (same shell, same room),
		// 2 partition + door (same shell, different rooms), 3 joint + door
		// (another shell of this network).
		struct FBay { FVector C; FName Room; uint8 Edge[4]; };
		TArray<FBay> Bays;
		FString Key = FString::Printf(TEXT("%d|%d|%d|%d|"), Root, bRated ? 1 : 0, bRoof ? 1 : 0, Cutaway);
		for (int32 Bay = 0; Bay < G.X * G.Y; ++Bay)
		{
			FBay I;
			I.C = Sim->GetBayCentreCm(B, Bay);
			I.Room = Sim->GetBayRoom(B.Id, Bay);
			for (int32 d = 0; d < 4; ++d)
			{
				int32 H2 = 0, B2 = INDEX_NONE;
				uint8 Kind = 0;
				if (Sim->FindBayAt(I.C + FVector(Dirs[d].X * 1000.0, Dirs[d].Y * 1000.0, 0.0), H2, B2))
				{
					if (H2 == B.Id)
					{
						Kind = Sim->GetBayRoom(H2, B2) == I.Room ? 1 : 2;
					}
					else if (Root != 0 && Sim->GetHabNetworkRoot(H2) == Root)
					{
						Kind = 3;
					}
				}
				// BM-6: 5 = a window in an outside wall; 6 / 7 = a doorway
				// (partition / joint) with its door fitted.
				const FIntVector WEdge = URHSimWorldSubsystem::WallEdgeKey(FIntPoint(FMath::RoundToInt32(I.C.X / 1000.0), FMath::RoundToInt32(I.C.Y / 1000.0)), d);
				const FRHWallFeature* WF = Sim->FindWallFeatureOnEdge(WEdge);
				const FRHDoorRow* WRow = WF ? Defs->GetDoor(WF->Row) : nullptr;
				if (Kind == 0 && WRow && WRow->Kind == FName("Window"))
				{
					Kind = 5;
				}
				else if ((Kind == 2 || Kind == 3) && WRow && WRow->Kind == FName("Door"))
				{
					Kind += 4;
				}
				for (const FRHAirlock* L : Locks)
				{
					if (L->Bay == Bay && L->Dir == Dirs[d])
					{
						Kind = 4; // an airlock's inner door, in the exterior wall
					}
				}
				I.Edge[d] = Kind;
			}
			Key += FString::Printf(TEXT("%s:%d%d%d%d;"), *I.Room.ToString(), I.Edge[0], I.Edge[1], I.Edge[2], I.Edge[3]);
			Bays.Add(I);
		}
		FRHHabShellVis& Vis = HabShellVis.FindOrAdd(B.Id);
		if (Vis.Key == Key)
		{
			continue;
		}
		Vis.Key = Key;
		for (const TWeakObjectPtr<UActorComponent>& P : Vis.Parts)
		{
			if (UActorComponent* Comp = P.Get())
			{
				Comp->DestroyComponent();
			}
		}
		Vis.Parts.Reset();

		const float Base = (float)(Actor->GetActorLocation().Z + 3.f); // the thin deck's top
		const float WallH = Cutaway == 2 ? 30.f : 300.f;
		const float ExtH = Cutaway == 1 ? 110.f : WallH;
		const float Th = 22.f, Door = 180.f, DoorH = 220.f;
		UInstancedStaticMeshComponent* Walls = NewObject<UInstancedStaticMeshComponent>(Actor);
		Walls->SetupAttachment(Actor->GetRootComponent());
		Walls->SetAbsolute(true, true, true);
		Walls->SetStaticMesh(Cube);
		Walls->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Walls->RegisterComponent();
		if (!ApplySurface(Walls, TEXT("/Game/RedHope/Art/Surfaces/T_HabWall_Panel.T_HabWall_Panel"), 450.f, FLinearColor(0.80f, 0.79f, 0.76f), 0.55f))
		{
			ApplyTint(Walls, FLinearColor(0.80f, 0.79f, 0.76f), FLinearColor::Black);
		}
		Vis.Parts.Add(Walls);
		// One wall piece along an edge: Along = unit along the edge, Mid = the
		// edge midpoint, From/To = offsets along it (cm), Z0/Z1 = heights.
		const auto Piece = [&](const FVector& Mid, const FVector& Along, float From, float To, float Z0, float Z1)
		{
			const float Len = To - From;
			if (Len <= 1.f || Z1 - Z0 <= 1.f)
			{
				return;
			}
			const FVector C = Mid + Along * ((From + To) * 0.5f) + FVector(0, 0, Base + (Z0 + Z1) * 0.5f);
			const bool bAlongX = FMath::Abs(Along.X) > 0.5f;
			Walls->AddInstance(FTransform(FRotator::ZeroRotator, C,
				FVector(bAlongX ? Len / 100.f : Th / 100.f, bAlongX ? Th / 100.f : Len / 100.f, (Z1 - Z0) / 100.f)), true);
		};
		for (int32 Bay = 0; Bay < Bays.Num(); ++Bay)
		{
			const FBay& I = Bays[Bay];
			for (int32 d = 0; d < 4; ++d)
			{
				const uint8 Kind = I.Edge[d];
				if (Kind == 1)
				{
					continue;
				}
				// Shared interior edges are drawn once, from the +X/+Y side.
				if ((Kind == 2 || Kind == 3 || Kind == 6 || Kind == 7) && (d == 1 || d == 3))
				{
					continue;
				}
				const FVector Mid(I.C.X + Dirs[d].X * (500.f - Th * 0.5f), I.C.Y + Dirs[d].Y * (500.f - Th * 0.5f), 0.f);
				const FVector Along = Dirs[d].X != 0 ? FVector(0, 1, 0) : FVector(1, 0, 0);
				if (Kind == 0)
				{
					Piece(Mid, Along, -500.f, 500.f, 0.f, ExtH);
				}
				else if (Kind == 4)
				{
					// The exterior wall around the airlock's inner door.
					Piece(Mid, Along, -500.f, -Door * 0.5f, 0.f, ExtH);
					Piece(Mid, Along, Door * 0.5f, 500.f, 0.f, ExtH);
					Piece(Mid, Along, -Door * 0.5f, Door * 0.5f, FMath::Min(DoorH, ExtH), ExtH);
				}
				else if (Kind == 5)
				{
					// BM-6: a window - wall either side, a sill below, a header
					// above, and a glazed band between.
					const float WinW = 300.f, Sill = FMath::Min(90.f, ExtH), Head = FMath::Min(230.f, ExtH);
					Piece(Mid, Along, -500.f, -WinW * 0.5f, 0.f, ExtH);
					Piece(Mid, Along, WinW * 0.5f, 500.f, 0.f, ExtH);
					Piece(Mid, Along, -WinW * 0.5f, WinW * 0.5f, 0.f, Sill);
					Piece(Mid, Along, -WinW * 0.5f, WinW * 0.5f, Head, ExtH);
					if (Head - Sill > 1.f)
					{
						UStaticMeshComponent* Pane = NewObject<UStaticMeshComponent>(Actor);
						Pane->SetupAttachment(Actor->GetRootComponent());
						Pane->SetAbsolute(true, true, true);
						Pane->SetStaticMesh(Cube);
						Pane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						Pane->RegisterComponent();
						const bool bAlongX = FMath::Abs(Along.X) > 0.5f;
						Pane->SetWorldLocation(FVector(Mid.X, Mid.Y, Base + (Sill + Head) * 0.5f));
						Pane->SetWorldScale3D(FVector(bAlongX ? WinW / 100.f : 0.06f, bAlongX ? 0.06f : WinW / 100.f, (Head - Sill) / 100.f));
						if (UMaterialInterface* GlassMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_DomeGlass.M_DomeGlass")))
						{
							UMaterialInstanceDynamic* GlassMid = UMaterialInstanceDynamic::Create(GlassMat, Pane);
							GlassMid->SetVectorParameterValue(FName("Tint"), FLinearColor(0.55f, 0.72f, 0.82f));
							GlassMid->SetScalarParameterValue(FName("Opacity"), 0.28f);
							Pane->SetMaterial(0, GlassMid);
						}
						else
						{
							ApplyTint(Pane, FLinearColor(0.45f, 0.62f, 0.72f), FLinearColor::Black);
						}
						Vis.Parts.Add(Pane);
						// A thin frame: two mullions.
						for (const float S : { -WinW * 0.5f, WinW * 0.5f })
						{
							Piece(Mid, Along, S - 6.f, S + 6.f, Sill, Head);
						}
					}
				}
				else
				{
					// A doorway in the middle; a lintel above it.
					Piece(Mid, Along, -500.f, -Door * 0.5f, 0.f, WallH);
					Piece(Mid, Along, Door * 0.5f, 500.f, 0.f, WallH);
					Piece(Mid, Along, -Door * 0.5f, Door * 0.5f, FMath::Min(DoorH, WallH), WallH);
					if (Kind == 6 || Kind == 7)
					{
						// BM-6: a pressure door - two leaves that part for the
						// crew (AnimateDoors); slate with a hazard edge.
						FRHDoorLeaves& Leaves = DoorLeaves.FindOrAdd(URHSimWorldSubsystem::WallEdgeKey(FIntPoint(FMath::RoundToInt32(I.C.X / 1000.0), FMath::RoundToInt32(I.C.Y / 1000.0)), d));
						Leaves.Mid = FVector(I.C.X + Dirs[d].X * 500.f, I.C.Y + Dirs[d].Y * 500.f, Base + FMath::Min(DoorH, WallH) * 0.5f);
						Leaves.Along = Along;
						const bool bAlongX = FMath::Abs(Along.X) > 0.5f;
						const float LeafH = FMath::Min(DoorH, WallH) - 4.f;
						for (int32 Side = 0; Side < 2; ++Side)
						{
							UStaticMeshComponent* Leaf = NewObject<UStaticMeshComponent>(Actor);
							Leaf->SetupAttachment(Actor->GetRootComponent());
							Leaf->SetAbsolute(true, true, true);
							Leaf->SetMobility(EComponentMobility::Movable);
							Leaf->SetStaticMesh(Cube);
							Leaf->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							Leaf->RegisterComponent();
							Leaf->SetWorldScale3D(FVector(bAlongX ? (Door * 0.5f + 2.f) / 100.f : 0.08f, bAlongX ? 0.08f : (Door * 0.5f + 2.f) / 100.f, LeafH / 100.f));
							Leaf->SetWorldLocation(Leaves.Mid + Along * ((Side == 0 ? -1.f : 1.f) * Door * 0.25f));
							ApplyTint(Leaf, FLinearColor(0.36f, 0.38f, 0.42f), RHCanon::HazYellow * 0.05f);
							Vis.Parts.Add(Leaf);
							(Side == 0 ? Leaves.L : Leaves.R) = Leaf;
						}
						Leaves.Open = 0.f;
					}
				}
			}
			// The bay's floor: sealed panels in the room hue (as underground).
			UStaticMeshComponent* Tile = NewObject<UStaticMeshComponent>(Actor);
			Tile->SetupAttachment(Actor->GetRootComponent());
			Tile->SetAbsolute(true, true, true);
			Tile->SetStaticMesh(Cube);
			Tile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Tile->RegisterComponent();
			Tile->SetWorldLocation(FVector(I.C.X, I.C.Y, Base + 1.f));
			Tile->SetWorldScale3D(FVector(9.9f, 9.9f, 0.02f));
			const bool bTex = I.Room.IsNone()
				? ApplySurface(Tile, TEXT("/Game/RedHope/Art/Surfaces/T_HabFloor_Deck.T_HabFloor_Deck"), 1000.f, FLinearColor(0.42f, 0.42f, 0.44f), 0.6f)
				: ApplySurface(Tile, TEXT("/Game/RedHope/Art/Surfaces/T_HabFloor_Sealed.T_HabFloor_Sealed"), 1000.f, RoomTint(I.Room) * 0.35f + FLinearColor(0.62f, 0.62f, 0.62f), 0.45f);
			if (!bTex)
			{
				ApplyTint(Tile, RoomTint(I.Room), FLinearColor::Black);
			}
			Vis.Parts.Add(Tile);
			if (bRoof)
			{
				UStaticMeshComponent* Roof = NewObject<UStaticMeshComponent>(Actor);
				Roof->SetupAttachment(Actor->GetRootComponent());
				Roof->SetAbsolute(true, true, true);
				Roof->SetStaticMesh(Cube);
				Roof->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Roof->RegisterComponent();
				Roof->SetWorldLocation(FVector(I.C.X, I.C.Y, Base + WallH + 10.f));
				Roof->SetWorldScale3D(FVector(10.f, 10.f, 0.2f));
				ApplyTint(Roof, FLinearColor(0.70f, 0.69f, 0.66f), FLinearColor::Black);
				Vis.Parts.Add(Roof);
			}
			if (I.Room.IsNone())
			{
				continue;
			}
			// Room label, furnishing prop (crew avoid it: it rides the shared
			// prop map keyed (0, Bay, HabId)) and a soft lamp.
			const FRHRoomRow* Row = Defs->GetRoom(I.Room);
			UTextRenderComponent* Label = NewObject<UTextRenderComponent>(Actor);
			Label->SetupAttachment(Actor->GetRootComponent());
			Label->SetAbsolute(true, true, true);
			Label->RegisterComponent();
			Label->SetWorldSize(150.f);
			Label->SetHorizontalAlignment(EHTA_Center);
			Label->SetVerticalAlignment(EVRTA_TextCenter);
			Label->SetWorldLocationAndRotation(FVector(I.C.X, I.C.Y - 330.f, Base + 12.f),
				FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator());
			Label->SetText(FText::FromString(Row ? Row->DisplayName : I.Room.ToString()));
			Label->SetTextRenderColor((RoomTint(I.Room) * 0.4f + FLinearColor(0.6f, 0.6f, 0.6f)).ToFColor(true));
			Vis.Parts.Add(Label);
			const FString PropPath = RoomPropPath(I.Room);
			if (UStaticMesh* PropMesh = PropPath.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *PropPath))
			{
				UStaticMeshComponent* Prop = NewObject<UStaticMeshComponent>(Actor);
				Prop->SetupAttachment(Actor->GetRootComponent());
				Prop->SetAbsolute(true, true, true);
				Prop->SetMobility(EComponentMobility::Movable);
				Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Prop->SetStaticMesh(PropMesh);
				Prop->RegisterComponent();
				const FBoxSphereBounds MB = PropMesh->GetBounds();
				const float S = 250.f / FMath::Max(2.f * FMath::Max(MB.BoxExtent.X, MB.BoxExtent.Y), 1.f);
				const FRotator Yaw(0.f, 90.f * ((Bay * 3 + B.Id) & 3), 0.f);
				Prop->SetWorldScale3D(FVector(S));
				Prop->SetWorldLocationAndRotation(FVector(I.C.X, I.C.Y, Base) - Yaw.RotateVector(FVector(MB.Origin.X, MB.Origin.Y, MB.Origin.Z - MB.BoxExtent.Z) * S), Yaw);
				RoomPropByCell.Add(FIntVector(0, Bay, B.Id), Prop);
				Vis.Parts.Add(Prop);
			}
			UPointLightComponent* Lamp = NewObject<UPointLightComponent>(Actor);
			Lamp->SetupAttachment(Actor->GetRootComponent());
			Lamp->SetAbsolute(true, true, true);
			Lamp->SetMobility(EComponentMobility::Movable);
			Lamp->SetIntensityUnits(ELightUnits::Candelas);
			Lamp->SetIntensity(bRated ? 18.f : 3.f); // a rated (lived-in) habitat glows
			Lamp->SetLightColor(FColor(255, 228, 196));
			Lamp->SetAttenuationRadius(800.f);
			Lamp->SetCastShadows(false);
			Lamp->RegisterComponent();
			Lamp->SetWorldLocation(FVector(I.C.X, I.C.Y, Base + 260.f));
			Vis.Parts.Add(Lamp);
		}
		for (const FRHAirlock* Lock : Locks)
		{
			// The decompression chamber: a 3 x 3 m vestibule outside the door -
			// side walls, an outer wall with its own door, a hazard band and a
			// cool lamp; open-topped like the habitat so the cycle reads from
			// the strategy camera.
			const FVector N(Lock->Dir.X, Lock->Dir.Y, 0.f);
			const FVector Side = Lock->Dir.X != 0 ? FVector(0, 1, 0) : FVector(1, 0, 0);
			const float HalfW = RHHabLayout::ChamberWidthCm * 0.5f, Depth = RHHabLayout::ChamberDepthCm;
			const FVector WallMid(Lock->WallMid.X, Lock->WallMid.Y, 0.f);
			for (const float S : { -1.f, 1.f })
			{
				Piece(WallMid + Side * (S * (HalfW - Th * 0.5f)), N, 0.f, Depth, 0.f, WallH);
			}
			const FVector OuterMid = WallMid + N * (Depth - Th * 0.5f);
			Piece(OuterMid, Side, -HalfW, -Door * 0.5f, 0.f, WallH);
			Piece(OuterMid, Side, Door * 0.5f, HalfW, 0.f, WallH);
			Piece(OuterMid, Side, -Door * 0.5f, Door * 0.5f, FMath::Min(DoorH, WallH), WallH);
			UStaticMeshComponent* Floor = NewObject<UStaticMeshComponent>(Actor);
			Floor->SetupAttachment(Actor->GetRootComponent());
			Floor->SetAbsolute(true, true, true);
			Floor->SetStaticMesh(Cube);
			Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Floor->RegisterComponent();
			Floor->SetWorldLocation(FVector(Lock->Chamber.X, Lock->Chamber.Y, Base - 1.f));
			Floor->SetWorldScale3D(FVector(Lock->Dir.X != 0 ? Depth / 100.f : RHHabLayout::ChamberWidthCm / 100.f,
				Lock->Dir.X != 0 ? RHHabLayout::ChamberWidthCm / 100.f : Depth / 100.f, 0.06f));
			ApplyTint(Floor, FLinearColor(0.20f, 0.21f, 0.23f), FLinearColor::Black);
			Vis.Parts.Add(Floor);
			UStaticMeshComponent* Band = NewObject<UStaticMeshComponent>(Actor);
			Band->SetupAttachment(Actor->GetRootComponent());
			Band->SetAbsolute(true, true, true);
			Band->SetStaticMesh(Cube);
			Band->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Band->RegisterComponent();
			Band->SetWorldLocation(FVector(OuterMid.X, OuterMid.Y, Base + WallH + 6.f));
			Band->SetWorldScale3D(Lock->Dir.X != 0 ? FVector(0.3f, RHHabLayout::ChamberWidthCm / 100.f, 0.12f) : FVector(RHHabLayout::ChamberWidthCm / 100.f, 0.3f, 0.12f));
			ApplyTint(Band, RHCanon::HazYellow, RHCanon::HazYellow * 0.4f);
			Vis.Parts.Add(Band);
			UPointLightComponent* Cool = NewObject<UPointLightComponent>(Actor);
			Cool->SetupAttachment(Actor->GetRootComponent());
			Cool->SetAbsolute(true, true, true);
			Cool->SetMobility(EComponentMobility::Movable);
			Cool->SetIntensityUnits(ELightUnits::Candelas);
			Cool->SetIntensity(14.f);
			Cool->SetLightColor(FColor(196, 226, 255));
			Cool->SetAttenuationRadius(450.f);
			Cool->SetCastShadows(false);
			Cool->RegisterComponent();
			Cool->SetWorldLocation(FVector(Lock->Chamber.X, Lock->Chamber.Y, Base + 240.f));
			Vis.Parts.Add(Cool);
		}
		UE_LOG(LogRedHope, Display, TEXT("Habitat #%d drawn: %d bays, network #%d%s%s"), B.Id, Bays.Num(), Root, bRated ? TEXT(" (rated)") : TEXT(""),
			Locks.Num() > 0 ? TEXT(", airlock here") : TEXT(""));
	}
}

void URHColonyVisualizerSubsystem::RefreshDomeCrops()
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return;
	}
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
		if (!Def || !Def->DragSized || Def->Pressurized || B.bUnderConstruction)
		{
			continue;
		}
		TObjectPtr<AStaticMeshActor>* Found = BuildingVisuals.Find(B.Id);
		AStaticMeshActor* Actor = Found ? Found->Get() : nullptr;
		if (!Actor)
		{
			continue;
		}
		// Key: per bed, '-' unplanted, '*' planted without a crop entry, else
		// <family initial><stage>. Equal key = zero work.
		const int32 Beds = URHSimWorldSubsystem::GetBedCount(B);
		FString Key;
		TArray<FString> MeshPaths;
		MeshPaths.SetNum(Beds);
		for (int32 Bed = 0; Bed < Beds; ++Bed)
		{
			if (!Sim->IsDomeBedPlanted(B.Id, Bed))
			{
				Key += TEXT("-");
				continue;
			}
			const FName Crop = Sim->GetDomeBedCrop(B.Id, Bed);
			const int32 Stage = Sim->GetDomeBedCropStage(B.Id, Bed);
			const FRHCropRow* CropRow = Crop.IsNone() ? nullptr : Defs->GetCrop(Crop);
			const FString Family = (CropRow && !CropRow->VisualFamily.IsNone()) ? CropRow->VisualFamily.ToString() : TEXT("root");
			const int32 S = (CropRow && Stage >= 0) ? Stage : 1;
			Key += FString::Printf(TEXT("%c%d"), Family.IsEmpty() ? TEXT('r') : Family[0], S);
			const FString N = FString::Printf(TEXT("crop_%s_%d"), *Family, S + 1);
			MeshPaths[Bed] = FString::Printf(TEXT("/Game/RedHope/Art/Agri/%s/%s/StaticMeshes/%s.%s"), *N, *N, *N, *N);
		}
		if (const FString* Applied = AppliedDomeCrops.Find(B.Id))
		{
			if (*Applied == Key)
			{
				continue;
			}
		}
		AppliedDomeCrops.Add(B.Id, Key);
		TArray<TWeakObjectPtr<UStaticMeshComponent>>& Props = DomeCropProps.FindOrAdd(B.Id);
		for (const TWeakObjectPtr<UStaticMeshComponent>& P : Props)
		{
			if (UStaticMeshComponent* C = P.Get())
			{
				C->DestroyComponent();
			}
		}
		Props.Reset();
		// Three by three plants per 10 m bed, each fit to ~2.4 m, feet on the
		// soil; a deterministic yaw per plant so rows aren't wallpaper.
		const FVector2D Half = URHSimWorldSubsystem::GetFootprintHalfCm(Def, B.SizeX, B.SizeY);
		const int32 NX = FMath::Max(1, B.SizeX), NY = FMath::Max(1, B.SizeY);
		const float BedX = 2.f * Half.X / NX, BedY = 2.f * Half.Y / NY;
		const FVector Seated = Actor->GetActorLocation() - FVector(0, 0, 15.f);
		for (int32 Bed = 0; Bed < Beds; ++Bed)
		{
			if (MeshPaths[Bed].IsEmpty())
			{
				continue;
			}
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *MeshPaths[Bed]);
			if (!Mesh)
			{
				continue;
			}
			const FBoxSphereBounds MB = Mesh->GetBounds();
			const float S = 240.f / FMath::Max(2.f * FMath::Max(MB.BoxExtent.X, MB.BoxExtent.Y), 1.f);
			const int32 bx = Bed % NX, by = Bed / NX;
			const FVector BedC = Seated + FVector(-Half.X + (bx + 0.5f) * BedX, -Half.Y + (by + 0.5f) * BedY, 30.f + 44.f);
			for (int32 p = 0; p < 9; ++p)
			{
				const FVector At = BedC + FVector(((p % 3) - 1) * (BedX - 160.f) / 3.f, ((p / 3) - 1) * (BedY - 160.f) / 3.f, 0.f);
				UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Actor);
				C->SetupAttachment(Actor->GetRootComponent());
				C->SetAbsolute(true, true, true);
				C->SetMobility(EComponentMobility::Movable);
				C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				C->SetStaticMesh(Mesh);
				C->RegisterComponent();
				// Centre the plant's BOUNDS on its point after the yaw: the crop
				// meshes' pivots sit off-centre, and rotating about the pivot
				// swung edge plants out through the glass (first capture).
				const FRotator Yaw(0.f, 90.f * ((Bed * 7 + p * 3) & 3), 0.f);
				C->SetWorldScale3D(FVector(S));
				C->SetWorldLocationAndRotation(At - Yaw.RotateVector(FVector(MB.Origin.X, MB.Origin.Y, MB.Origin.Z - MB.BoxExtent.Z) * S), Yaw);
				Props.Add(C);
			}
		}
		UE_LOG(LogRedHope, Display, TEXT("Dome #%d crops: %s"), B.Id, *Key);
	}
}

void URHColonyVisualizerSubsystem::BuildSilhouette(AStaticMeshActor* Actor, FName DefName, const FVector& BaseCm, const FVector& ScaleM) const
{
	static const TCHAR* Cyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	static const TCHAR* Cone = TEXT("/Engine/BasicShapes/Cone.Cone");
	static const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	static const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");

	using namespace RHCanon;
	const float TopZ = ScaleM.Z * 100.f; // box top above ground (actor center at Z*50)

	// Foundation apron: every built structure sits on a dark plinth - kills the
	// floating-box-on-a-table read and ties the structure to the site. The
	// Lander stands on its own legs instead.
	if (DefName != FName("Lander"))
	{
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, 8.f), FRotator::ZeroRotator,
			FVector(ScaleM.X + 0.7f, ScaleM.Y + 0.7f, 0.16f), FLinearColor(0.055f, 0.06f, 0.075f));
	}

	// Every machine wears a band of its function color at the hull's top edge -
	// the color identity that reads at strategic zoom (canon: one saturated
	// accent per function). The strong emissive floor keeps the hue saturated
	// under midday exposure. Flat decks, the mast, and the Lander carry their
	// color elsewhere.
	if (DefName != FName("Lander") && DefName != FName("ChargePad")
		&& DefName != FName("Stockpile") && DefName != FName("Pylon")
		&& DefName != FName("Floodmast") // a mast has no hull to band
		&& DefName != FName("SolarArray")) // the radiating panels carry the PV blue
	{
		const FLinearColor Hue = TintFor(DefName);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ - 30.f), FRotator::ZeroRotator,
			FVector(ScaleM.X + 0.14f, ScaleM.Y + 0.14f, 0.5f), Hue, Hue * 0.9f);
	}

	// LIVED-IN + DUSTY (director's look note): every standing structure wears
	// the planet. Deterministic per family (a content hash picks which sides),
	// so the same building always weathers the same way.
	// - dust drifts banked against two hull faces (wind-laid, like the plain's)
	// - a grime band where hull meets regolith
	// - a thin dust film on the roof
	// - one replaced hull panel in off-tone (maintenance you can SEE)
	if (DefName != FName("Lander") && DefName != FName("Pylon") && DefName != FName("ChargePad"))
	{
		const uint32 Wear = FCrc::StrCrc32(*DefName.ToString());
		const FLinearColor DriftDust(0.52f, 0.35f, 0.22f);
		const FLinearColor Grime(0.15f, 0.115f, 0.085f);
		const float Hx = ScaleM.X * 50.f;
		const float Hy = ScaleM.Y * 50.f;
		for (int32 D = 0; D < 2; ++D)
		{
			const int32 Side = (int32)((Wear >> (D * 3)) % 4u);
			const float Sx = (Side == 0) ? 1.f : (Side == 1 ? -1.f : 0.f);
			const float Sy = (Side == 2) ? 1.f : (Side == 3 ? -1.f : 0.f);
			const FVector At = BaseCm + FVector(Sx * (Hx + 25.f), Sy * (Hy + 25.f), 6.f)
				+ FVector(Sy * (float)((Wear >> (D * 5)) % 120u) - Sy * 60.f, Sx * (float)((Wear >> (D * 7)) % 120u) - Sx * 60.f, 0.f);
			AddAccent(Actor, Sphere, At, FRotator(0, Sx != 0.f ? 90.f : 0.f, 0),
				FVector(FMath::Max(ScaleM.Y, ScaleM.X) * 0.55f + 0.8f, 1.1f, 0.42f), DriftDust);
		}
		// Grime band: four thin skirts at the hull base.
		AddAccent(Actor, Cube, BaseCm + FVector(Hx + 3.f, 0, 22.f), FRotator::ZeroRotator, FVector(0.05f, ScaleM.Y * 0.98f, 0.44f), Grime);
		AddAccent(Actor, Cube, BaseCm + FVector(-Hx - 3.f, 0, 22.f), FRotator::ZeroRotator, FVector(0.05f, ScaleM.Y * 0.98f, 0.44f), Grime);
		AddAccent(Actor, Cube, BaseCm + FVector(0, Hy + 3.f, 22.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.98f, 0.05f, 0.44f), Grime);
		AddAccent(Actor, Cube, BaseCm + FVector(0, -Hy - 3.f, 22.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.98f, 0.05f, 0.44f), Grime);
		// Roof dust film (skip open-frame silhouettes where a slab makes no sense).
		if (DefName != FName("Stockpile") && DefName != FName("SolarArray"))
		{
			AddAccent(Actor, Cube, BaseCm + FVector(Hx * 0.18f, -Hy * 0.15f, TopZ + 3.f), FRotator(0, (float)(Wear % 27u), 0),
				FVector(ScaleM.X * 0.72f, ScaleM.Y * 0.6f, 0.05f), FLinearColor(0.44f, 0.29f, 0.185f));
		}
		// The replaced panel: proud of the hull by 3 cm, in a mismatched tone.
		{
			const bool bEastFace = (Wear & 8u) != 0;
			const FLinearColor Patch = (Wear & 16u) ? FLinearColor(0.40f, 0.385f, 0.35f) : FLinearColor(0.20f, 0.21f, 0.24f);
			const float Along = ((float)((Wear >> 9) % 100u) / 100.f - 0.5f) * ScaleM.Y * 60.f;
			AddAccent(Actor, Cube, BaseCm + FVector((bEastFace ? 1.f : -1.f) * (Hx + 3.f), Along, TopZ * 0.55f),
				FRotator::ZeroRotator, FVector(0.04f, 0.85f, 0.7f), Patch);
		}
	}

	if (DefName == FName("LabModule") || DefName == FName("InfirmaryModule"))
	{
		// Prefab station shell (tiered production T4): a factory-made module
		// on skids - a low barrel-vault roof, a lit window band in the
		// function hue on both long faces, a hatch. The station is the sim's.
		const FLinearColor Hue = TintFor(DefName);
		const float Hx = ScaleM.X * 50.f, Hy = ScaleM.Y * 50.f;
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, TopZ - 6.f), FRotator(90.f, 0.f, 0.f),
			FVector(1.1f, ScaleM.Y * 0.96f, ScaleM.X * 0.96f), FLinearColor(0.80f, 0.79f, 0.76f));
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ * 0.62f), FRotator::ZeroRotator,
			FVector(ScaleM.X * 0.78f, ScaleM.Y + 0.05f, 0.32f), Hue, Hue * 1.4f);
		AddAccent(Actor, Cube, BaseCm + FVector(Hx * 0.55f, -Hy - 4.f, TopZ * 0.34f), FRotator::ZeroRotator,
			FVector(0.9f, 0.08f, 1.9f), FLinearColor(0.14f, 0.15f, 0.17f));
		for (const float Sy : { -0.62f, 0.62f })
		{
			AddAccent(Actor, Cube, BaseCm + FVector(0, Sy * Hy, 8.f), FRotator::ZeroRotator,
				FVector(ScaleM.X * 1.06f, 0.22f, 0.16f), FLinearColor(0.10f, 0.10f, 0.11f));
		}
		if (DefName == FName("LabModule"))
		{
			// A sensor mast: the lab's instruments reach outside.
			AddAccent(Actor, Cube, BaseCm + FVector(-Hx * 0.6f, Hy * 0.5f, TopZ + 90.f), FRotator::ZeroRotator,
				FVector(0.06f, 0.06f, 1.8f), FLinearColor(0.2f, 0.2f, 0.22f));
			AddAccent(Actor, Sphere, BaseCm + FVector(-Hx * 0.6f, Hy * 0.5f, TopZ + 185.f), FRotator::ZeroRotator,
				FVector(0.22f), Hue, Hue * 2.f);
		}
	}

	if (DefName == FName("Lander"))
	{
		// REFERENCE (Ships/CargoLander): a blocky, functional cargo freighter -
		// "not aerodynamic". Faceted cockpit nose toward the camera, an OPEN
		// cargo bay showing amber supply crates (the sim's actual trade depot),
		// a dome turret on the spine, twin tail fins, articulated legs. Replaces
		// the old vertical descent-rocket read, which was off-canon.
		const float Lift = GLanderLiftCm;
		const float BodyTop = Lift + ScaleM.Z * 100.f;
		const float BodyMid = Lift + ScaleM.Z * 50.f;
		const float HalfX = ScaleM.X * 50.f;
		const float HalfY = ScaleM.Y * 50.f;
		// Four articulated landing legs down to footpads (kept from v1 - they
		// match the reference's splayed struts).
		for (int32 Leg = 0; Leg < 4; ++Leg)
		{
			const FVector Dir = FRotator(0, Leg * 90.f + 45.f, 0).Vector();
			const FVector Att = BaseCm + Dir * (HalfX * 0.7f) + FVector(0, 0, Lift + 20.f);
			const FVector Foot = BaseCm + Dir * (HalfX + 160.f);
			const FVector LegVec = Foot - Att;
			AddAccent(Actor, Cube, (Att + Foot) * 0.5f, FRotationMatrix::MakeFromZ(LegVec.GetSafeNormal()).Rotator(), FVector(0.28f, 0.28f, LegVec.Size() / 100.f), DarkSlate);
			AddAccent(Actor, Cyl, Foot + FVector(0, 0, 15.f), FRotator::ZeroRotator, FVector(1.1f, 1.1f, 0.25f), DarkSlate);
		}
		// Faceted cockpit nose (south = camera-facing): a white angled nose block
		// with a dark tinted canopy raked over it.
		AddAccent(Actor, Cube, BaseCm + FVector(-HalfX - 85.f, 0, BodyMid - 25.f), FRotator(-32.f, 0, 0), FVector(2.2f, ScaleM.Y * 0.62f, ScaleM.Z * 0.62f), BoneWhite);
		AddAccent(Actor, Cube, BaseCm + FVector(-HalfX - 68.f, 0, BodyMid + 62.f), FRotator(-32.f, 0, 0), FVector(1.5f, ScaleM.Y * 0.5f, 0.28f), FLinearColor(0.03f, 0.05f, 0.07f), FLinearColor(0.05f, 0.12f, 0.15f));
		// The OPEN cargo bay on the east flank: a dark recess with three amber
		// crates glowing inside (the reference's signature), and a ramp to grade.
		AddAccent(Actor, Cube, BaseCm + FVector(0, -HalfY - 6.f, BodyMid - 20.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.5f, 0.2f, ScaleM.Z * 0.62f), FLinearColor(0.03f, 0.03f, 0.04f));
		for (int32 C = 0; C < 3; ++C)
		{
			AddAccent(Actor, Cube, BaseCm + FVector((C - 1) * 90.f, -HalfY - 24.f, BodyMid - 45.f + (C == 1 ? 55.f : 0.f)), FRotator(0, C * 14.f, 0),
				FVector(0.7f), FLinearColor(0.85f, 0.55f, 0.12f), FLinearColor(0.9f, 0.5f, 0.08f));
		}
		AddAccent(Actor, Cube, BaseCm + FVector(0, -HalfY - 150.f, (BodyMid - 40.f) * 0.5f), FRotator(0, 0, -34.f), FVector(ScaleM.X * 0.42f, 2.6f, 0.1f), DarkSlate);
		// Spine: dome turret + a comms blister; twin tail fins at the stern.
		AddAccent(Actor, Cyl, BaseCm + FVector(HalfX * 0.35f, 0, BodyTop + 22.f), FRotator::ZeroRotator, FVector(1.1f, 1.1f, 0.45f), BoneWhite);
		AddAccent(Actor, Sphere, BaseCm + FVector(HalfX * 0.35f, 0, BodyTop + 60.f), FRotator::ZeroRotator, FVector(0.95f, 0.95f, 0.7f), DarkSlate, FLinearColor(0.05f, 0.12f, 0.15f));
		AddAccent(Actor, Sphere, BaseCm + FVector(-HalfX * 0.45f, HalfY * 0.4f, BodyTop + 25.f), FRotator::ZeroRotator, FVector(0.6f), BoneWhite);
		for (int32 F = 0; F < 2; ++F)
		{
			const float Fy = (F == 0 ? 1.f : -1.f) * HalfY * 0.62f;
			AddAccent(Actor, Cube, BaseCm + FVector(HalfX + 55.f, Fy, BodyTop + 65.f), FRotator(24.f, 0, 0), FVector(1.7f, 0.12f, 1.8f), BoneWhite);
			AddAccent(Actor, Cube, BaseCm + FVector(HalfX + 88.f, Fy, BodyTop + 148.f), FRotator(24.f, 0, 0), FVector(0.55f, 0.14f, 0.5f), HazYellow);
		}
	}
	else if (DefName == FName("BatteryBank"))
	{
		// Reference: pale racks holding glowing teal cell banks.
		AddAccent(Actor, Cube, BaseCm + FVector(0, ScaleM.Y * 50.f + 6.f, ScaleM.Z * 50.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.75f, 0.08f, ScaleM.Z * 0.6f), TintFor(DefName), TealGlow);
		AddAccent(Actor, Cube, BaseCm + FVector(0, -ScaleM.Y * 50.f - 6.f, ScaleM.Z * 50.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.75f, 0.08f, ScaleM.Z * 0.6f), TintFor(DefName), TealGlow);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ + 10.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.9f, ScaleM.Y * 0.9f, 0.15f), HazYellow);
		// Greeble: dark rack rails framing the teal cells top and bottom, plus a
		// row of bone coolant fins across the roof.
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const float Sy = Side == 0 ? 1.f : -1.f;
			AddAccent(Actor, Cube, BaseCm + FVector(0, Sy * (ScaleM.Y * 50.f + 10.f), 24.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.85f, 0.14f, 0.14f), DarkSlate);
			AddAccent(Actor, Cube, BaseCm + FVector(0, Sy * (ScaleM.Y * 50.f + 10.f), TopZ - 12.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.85f, 0.14f, 0.14f), DarkSlate);
		}
		for (int32 F = 0; F < 4; ++F)
		{
			AddAccent(Actor, Cube, BaseCm + FVector((F - 1.5f) * ScaleM.X * 24.f, 0, TopZ + 34.f), FRotator::ZeroRotator, FVector(0.06f, ScaleM.Y * 0.8f, 0.5f), BoneWhite);
		}
		// Reference (Facilities/BatteryStation): the white protective shell -
		// angled corner pillars framing the cell racks - and the orange hazard
		// chevron blocks at the base corners.
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const float Sx = (Corner & 1) ? 1.f : -1.f;
			const float Sy = (Corner & 2) ? 1.f : -1.f;
			AddAccent(Actor, Cube, BaseCm + FVector(Sx * (ScaleM.X * 50.f - 8.f), Sy * (ScaleM.Y * 50.f + 14.f), TopZ * 0.5f),
				FRotator::ZeroRotator, FVector(0.55f, 0.22f, ScaleM.Z * 0.92f), BoneWhite);
			AddAccent(Actor, Cube, BaseCm + FVector(Sx * (ScaleM.X * 50.f + 8.f), Sy * (ScaleM.Y * 50.f + 8.f), 22.f),
				FRotator(0, Sx * Sy * 45.f, 0), FVector(0.45f, 0.45f, 0.3f), FLinearColor(0.9f, 0.5f, 0.05f));
		}
	}
	else if (DefName == FName("Pylon"))
	{
		// Grid node: amber glow cap - power made visible, on brand with the
		// coverage discs.
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, TopZ + 40.f), FRotator::ZeroRotator, FVector(0.8f), TintFor(DefName), AmberGlow);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ - 70.f), FRotator::ZeroRotator, FVector(2.4f, 0.25f, 0.25f), DarkSlate);
	}
	else if (DefName == FName("Floodmast"))
	{
		// A light tower and nothing else: slim mast, a cross-arm, and two
		// hooded heads whose emissive matches the real point light attached in
		// HandleBuildingAdded - so the source you SEE is the source that lights
		// the ground. Warm, because everything else out here is cold.
		const float MastZ = TopZ + 520.f;
		const FLinearColor Warm(1.0f, 0.86f, 0.66f);
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, MastZ * 0.5f), FRotator::ZeroRotator,
			FVector(0.22f, 0.22f, MastZ / 100.f), DarkSlate);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, MastZ), FRotator::ZeroRotator,
			FVector(0.22f, 1.5f, 0.16f), DarkSlate);
		for (int32 S = -1; S <= 1; S += 2)
		{
			AddAccent(Actor, Cube, BaseCm + FVector(0, S * 62.f, MastZ - 22.f), FRotator::ZeroRotator,
				FVector(0.34f, 0.42f, 0.22f), Warm, Warm * 3.2f);
		}
		// One hazard band at boot height so nobody walks into the mast.
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, 120.f), FRotator::ZeroRotator,
			FVector(0.3f, 0.3f, 0.3f), HazYellow);
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
		// Greeble: rear exhaust chimney with a rust cap, a slag chute off the
		// furnace face, and a railed catwalk down the east flank.
		AddAccent(Actor, Cyl, BaseCm + FVector(ScaleM.X * 30.f, ScaleM.Y * 50.f - 45.f, TopZ + 130.f), FRotator::ZeroRotator, FVector(0.55f, 0.55f, 3.4f), DarkSlate);
		AddAccent(Actor, Cyl, BaseCm + FVector(ScaleM.X * 30.f, ScaleM.Y * 50.f - 45.f, TopZ + 300.f), FRotator::ZeroRotator, FVector(0.75f, 0.75f, 0.3f), RustBeam);
		AddAccent(Actor, Cube, BaseCm + FVector(-ScaleM.X * 50.f - 95.f, 0.f, 60.f), FRotator(30.f, 0, 0), FVector(1.4f, 0.9f, 0.08f), DarkSlate);
		AddAccent(Actor, Cube, BaseCm + FVector(ScaleM.X * 50.f + 42.f, 0.f, TopZ * 0.62f), FRotator::ZeroRotator, FVector(0.16f, ScaleM.Y * 0.95f, 0.06f), BoneWhite);
		AddAccent(Actor, Cube, BaseCm + FVector(ScaleM.X * 50.f + 70.f, 0.f, TopZ * 0.62f + 55.f), FRotator::ZeroRotator, FVector(0.05f, ScaleM.Y * 0.95f, 0.02f), HazYellow);
		// Control annex off the north flank - a second mass so the smelter
		// reads as a works, not a single box - with a lit crew-window band.
		AddAccent(Actor, Cube, BaseCm + FVector(ScaleM.X * 25.f, ScaleM.Y * 50.f + 120.f, 105.f), FRotator::ZeroRotator, FVector(2.4f, 1.7f, 2.1f), BoneWhite);
		AddAccent(Actor, Cube, BaseCm + FVector(ScaleM.X * 25.f, ScaleM.Y * 50.f + 207.f, 140.f), FRotator::ZeroRotator, FVector(2.0f, 0.06f, 0.5f), FLinearColor(0.9f, 0.75f, 0.4f), FLinearColor(2.2f, 1.6f, 0.6f));
	}
	else if (DefName == FName("IceDrill"))
	{
		// Reference: slate block, drill tower, ice-glow inspection window.
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, TopZ + 150.f), FRotator::ZeroRotator, FVector(0.7f, 0.7f, 3.0f), BoneWhite);
		AddAccent(Actor, Cone, BaseCm + FVector(0, 0, TopZ + 320.f), FRotator::ZeroRotator, FVector(1.2f, 1.2f, 1.0f), DarkSlate);
		AddAccent(Actor, Cube, BaseCm + FVector(-ScaleM.X * 50.f - 4.f, 0, 120.f), FRotator::ZeroRotator, FVector(0.1f, 1.6f, 1.2f), TintFor(DefName), IceGlow);
		// Greeble: a 4-strut derrick leaning in around the drill mast, plus a
		// spoil auger running out to a small ice pile.
		for (int32 S = 0; S < 4; ++S)
		{
			const float Ang = S * 90.f + 45.f;
			const FVector Foot = FRotator(0, Ang, 0).Vector() * (ScaleM.X * 45.f);
			AddAccent(Actor, Cube, BaseCm + Foot + FVector(0, 0, TopZ + 120.f), FRotator(18.f, Ang, 0), FVector(0.12f, 0.12f, 3.0f), DarkSlate);
		}
		AddAccent(Actor, Cyl, BaseCm + FVector(ScaleM.X * 50.f + 80.f, -40.f, 45.f), FRotator(52.f, 0, 0), FVector(0.35f, 0.35f, 2.2f), DarkSlate);
		AddAccent(Actor, Cube, BaseCm + FVector(ScaleM.X * 50.f + 150.f, -40.f, 20.f), FRotator(0, 25.f, 0), FVector(0.9f, 0.9f, 0.35f), FLinearColor(0.75f, 0.90f, 1.00f));
		// Reference (Facilities/IceProcessor): the extraction works ships with a
		// pair of frost-white INSULATED TANKS on cradles, piped back into the
		// block, and a frost collar where the drill string enters the ground -
		// the cold made visible.
		const FLinearColor FrostWhite(0.62f, 0.66f, 0.70f);
		for (int32 T = 0; T < 2; ++T)
		{
			const FVector TankCm = BaseCm + FVector(-60.f + T * 150.f, -ScaleM.Y * 50.f - 120.f, 0);
			AddAccent(Actor, Cyl, TankCm + FVector(0, 0, 120.f), FRotator::ZeroRotator, FVector(1.15f, 1.15f, 2.4f), FrostWhite);
			AddAccent(Actor, Sphere, TankCm + FVector(0, 0, 240.f), FRotator::ZeroRotator, FVector(1.1f, 1.1f, 0.75f), FrostWhite);
			AddAccent(Actor, Cube, TankCm + FVector(0, 0, 16.f), FRotator::ZeroRotator, FVector(1.0f, 1.0f, 0.32f), DarkSlate);
			AddAccent(Actor, Cyl, TankCm + FVector(0, 60.f, 150.f), FRotator(90.f, 0, 0), FVector(0.16f, 0.16f, 1.2f), DarkSlate);
		}
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, 10.f), FRotator::ZeroRotator, FVector(1.9f, 1.9f, 0.14f), FrostWhite); // frost collar
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
		// Greeble: header pipe running along the tank tops, a riser back to the
		// block, and a valve wheel on each tank.
		AddAccent(Actor, Cyl, BaseCm + FVector(0.f, ScaleM.Y * 50.f + 110.f, 250.f), FRotator(90.f, 0, 0), FVector(0.3f, 0.3f, 4.4f), DarkSlate);
		AddAccent(Actor, Cyl, BaseCm + FVector(0.f, ScaleM.Y * 50.f + 55.f, 150.f), FRotator(0, 0, 90.f), FVector(0.28f, 0.28f, 1.2f), DarkSlate);
		for (int32 V = 0; V < 3; ++V)
		{
			AddAccent(Actor, Sphere, BaseCm + FVector((V - 1) * 190.f, ScaleM.Y * 50.f + 110.f, 305.f), FRotator::ZeroRotator, FVector(0.4f), HazYellow);
		}
		// Reference (Facilities/IceProcessor): the open MELT BASIN - a dark
		// trough with raw ice glowing pale blue inside, chunks waiting to feed
		// the works - plus the red hand-valve wheel and a breathing vent stack.
		{
			const FVector Basin = BaseCm + FVector(-ScaleM.X * 50.f - 160.f, -60.f, 0);
			AddAccent(Actor, Cube, Basin + FVector(0, 0, 30.f), FRotator(0, 8.f, 0), FVector(2.6f, 1.7f, 0.6f), DarkSlate);
			AddAccent(Actor, Cube, Basin + FVector(0, 0, 52.f), FRotator(0, 8.f, 0), FVector(2.3f, 1.4f, 0.18f),
				FLinearColor(0.55f, 0.75f, 0.9f), FLinearColor(0.25f, 0.8f, 1.4f)); // the melt, lit from within
			AddAccent(Actor, Cube, Basin + FVector(-40.f, 20.f, 78.f), FRotator(0, 33.f, 0), FVector(0.45f, 0.45f, 0.4f), FLinearColor(0.75f, 0.90f, 1.00f));
			AddAccent(Actor, Cube, Basin + FVector(55.f, -25.f, 74.f), FRotator(0, -21.f, 0), FVector(0.38f, 0.38f, 0.32f), FLinearColor(0.75f, 0.90f, 1.00f));
			AddAccent(Actor, Cyl, Basin + FVector(120.f, 60.f, 55.f), FRotator(0, 0, 90.f), FVector(0.4f, 0.4f, 0.08f), FLinearColor(0.7f, 0.12f, 0.08f)); // the red valve wheel
		}
		AddAccent(Actor, Cyl, BaseCm + FVector(ScaleM.X * 35.f, -ScaleM.Y * 30.f, TopZ + 90.f), FRotator::ZeroRotator, FVector(0.3f, 0.3f, 1.8f), BoneWhite);
		AddAccent(Actor, Sphere, BaseCm + FVector(ScaleM.X * 35.f, -ScaleM.Y * 30.f, TopZ + 205.f), FRotator::ZeroRotator, FVector(0.55f, 0.55f, 0.4f), FLinearColor(0.68f, 0.66f, 0.64f)); // the steam breath
	}
	else if (DefName == FName("Electrolyzer"))
	{
		// Gas works: the slim hall is flanked by two BIG horizontal storage
		// tanks (O2 and H2) on dark cradles with domed ends, piped back into
		// the hall. The tanks are half the building's mass - composition, not
		// roof greeble.
		const float TankLen = ScaleM.X * 0.72f;
		for (int32 T = 0; T < 2; ++T)
		{
			const float Ty = (T == 0 ? 1.f : -1.f) * (ScaleM.Y * 50.f + 105.f);
			AddAccent(Actor, Cyl, BaseCm + FVector(0, Ty, 100.f), FRotator(90.f, 0, 0), FVector(1.9f, 1.9f, TankLen), BoneWhite);
			AddAccent(Actor, Sphere, BaseCm + FVector(TankLen * 50.f, Ty, 100.f), FRotator::ZeroRotator, FVector(1.85f), BoneWhite);
			AddAccent(Actor, Sphere, BaseCm + FVector(-TankLen * 50.f, Ty, 100.f), FRotator::ZeroRotator, FVector(1.85f), BoneWhite);
			AddAccent(Actor, Cube, BaseCm + FVector(0, Ty, 24.f), FRotator::ZeroRotator, FVector(TankLen * 0.55f, 1.5f, 0.5f), DarkSlate);
			AddAccent(Actor, Cyl, BaseCm + FVector(0, Ty * 0.5f, 168.f), FRotator(0, 0, 90.f), FVector(0.24f, 0.24f, FMath::Abs(Ty) / 100.f), DarkSlate);
		}
		AddAccent(Actor, Cube, BaseCm + FVector(-ScaleM.X * 50.f - 4.f, 0, 110.f), FRotator::ZeroRotator, FVector(0.1f, ScaleM.Y * 0.7f, 0.55f), TintFor(DefName), TealGlow);
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
		// Server tower: dark rack body, three magenta status strips on the south
		// face, a bone cooling collar on the roof, and the uplink dish on a mast.
		for (int32 R = 0; R < 3; ++R)
		{
			AddAccent(Actor, Cube, BaseCm + FVector(-ScaleM.X * 50.f - 4.f, 0, 55.f + R * 60.f), FRotator::ZeroRotator,
				FVector(0.08f, ScaleM.Y * 0.72f, 0.14f), TintFor(DefName), FLinearColor(3.5f, 0.9f, 2.0f));
		}
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ + 26.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.72f, ScaleM.Y * 0.72f, 0.5f), BoneWhite);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ + 130.f), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 1.8f), DarkSlate);
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, TopZ + 215.f), FRotator(30.f, 0, 0), FVector(1.1f, 1.1f, 0.1f), BoneWhite);
	}
	else if (DefName == FName("SolarArray"))
	{
		// REFERENCE (Facilities/SolarPanel): a white central hub pod with a domed
		// cap and FOUR tilted photovoltaic clusters radiating on pedestal mounts
		// - not a flat slab. Panels tilt outward from the hub like the reference;
		// a faint PV-blue emissive keeps them reading at dusk.
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, 95.f), FRotator::ZeroRotator, FVector(1.9f, 1.9f, 1.7f), BoneWhite);
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, 190.f), FRotator::ZeroRotator, FVector(1.7f, 1.7f, 0.85f), FLinearColor(0.68f, 0.68f, 0.72f));
		AddAccent(Actor, Cube, BaseCm + FVector(0, 105.f, 70.f), FRotator::ZeroRotator, FVector(0.5f, 0.2f, 0.5f), DarkSlate); // hub louver
		for (int32 P = 0; P < 4; ++P)
		{
			const float Ang = P * 90.f + 45.f;
			const FVector Dir = FRotator(0, Ang, 0).Vector();
			const FVector PedCm = BaseCm + Dir * (ScaleM.X * 50.f + 105.f);
			// Pedestal mount, then the 2x3 panel slab tilted up and away from the hub.
			AddAccent(Actor, Cyl, PedCm + FVector(0, 0, 55.f), FRotator::ZeroRotator, FVector(0.4f, 0.4f, 1.1f), BoneWhite);
			const FVector PanelCm = PedCm + Dir * 40.f + FVector(0, 0, 135.f);
			AddAccent(Actor, Cube, PanelCm, FRotator(-26.f, Ang + 90.f, 0), FVector(2.9f, 0.08f, 1.9f), PVBlue, FLinearColor(0.04f, 0.08f, 0.28f));
			AddAccent(Actor, Cube, PanelCm + FVector(0, 0, 4.f), FRotator(-26.f, Ang + 90.f, 0), FVector(2.98f, 0.06f, 0.08f), BoneWhite); // frame spar
		}
	}
	else if (DefName == FName("Borer"))
	{
		// REFERENCE (Facilities/OreExtractor): the auger. A helical drill cone
		// biting the ground at the south face, an inclined conveyor climbing off
		// the stern to a spoil discharge, twin dark crawler tracks under the
		// body, and a glazed operator cab. The machine that digs the colony.
		const float HalfX = ScaleM.X * 50.f;
		const float HalfY = ScaleM.Y * 50.f;
		// Crawler tracks: two dark slabs under the flanks.
		AddAccent(Actor, Cube, BaseCm + FVector(0, HalfY - 10.f, 32.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.95f, 0.6f, 0.62f), FLinearColor(0.05f, 0.05f, 0.06f));
		AddAccent(Actor, Cube, BaseCm + FVector(0, -HalfY + 10.f, 32.f), FRotator::ZeroRotator, FVector(ScaleM.X * 0.95f, 0.6f, 0.62f), FLinearColor(0.05f, 0.05f, 0.06f));
		// The auger: shaft + a big steel cone pitched nose-down into the regolith.
		AddAccent(Actor, Cyl, BaseCm + FVector(-HalfX - 55.f, 0, 120.f), FRotator(0, 0, -125.f), FVector(0.55f, 0.55f, 1.6f), DarkSlate);
		AddAccent(Actor, Cone, BaseCm + FVector(-HalfX - 165.f, 0, 55.f), FRotator(0, 0, -235.f), FVector(1.5f, 1.5f, 2.3f), FLinearColor(0.38f, 0.38f, 0.42f));
		AddAccent(Actor, Cyl, BaseCm + FVector(-HalfX - 165.f, 0, 45.f), FRotator::ZeroRotator, FVector(2.3f, 2.3f, 0.35f), FLinearColor(0.32f, 0.2f, 0.12f)); // churned spoil ring
		// Inclined conveyor off the stern: dark belt, hazard side-rails, spoil pile.
		AddAccent(Actor, Cube, BaseCm + FVector(HalfX + 145.f, 40.f, 175.f), FRotator(0, 0, 26.f), FVector(3.6f, 0.75f, 0.14f), FLinearColor(0.08f, 0.08f, 0.09f));
		AddAccent(Actor, Cube, BaseCm + FVector(HalfX + 145.f, 78.f, 192.f), FRotator(0, 0, 26.f), FVector(3.6f, 0.05f, 0.06f), HazYellow);
		AddAccent(Actor, Cube, BaseCm + FVector(HalfX + 145.f, 2.f, 192.f), FRotator(0, 0, 26.f), FVector(3.6f, 0.05f, 0.06f), HazYellow);
		AddAccent(Actor, Cube, BaseCm + FVector(HalfX + 300.f, 40.f, 45.f), FRotator(0, 30.f, 0), FVector(1.1f, 1.1f, 0.8f), FLinearColor(0.28f, 0.18f, 0.11f));
		// Operator cab: white block with a dark glass face, up on the body.
		AddAccent(Actor, Cube, BaseCm + FVector(-HalfX * 0.35f, -HalfY * 0.3f, TopZ + 55.f), FRotator::ZeroRotator, FVector(1.5f, 1.3f, 1.2f), BoneWhite);
		AddAccent(Actor, Cube, BaseCm + FVector(-HalfX * 0.35f - 78.f, -HalfY * 0.3f, TopZ + 62.f), FRotator(-14.f, 0, 0), FVector(0.12f, 1.1f, 0.85f), FLinearColor(0.03f, 0.05f, 0.07f), FLinearColor(0.05f, 0.12f, 0.15f));
		// Hazard chevron block on the working face.
		AddAccent(Actor, Cube, BaseCm + FVector(-HalfX + 25.f, HalfY - 25.f, TopZ + 20.f), FRotator::ZeroRotator, FVector(0.6f, 0.6f, 0.4f), HazYellow);
	}
	else if (DefName == FName("AirFilter"))
	{
		// Life support drum: a white scrubber cylinder with dark intake louvers
		// banding it, a fan cowl on top, and a breathing-teal status light. Kept
		// clean and calm - this is the machine the crew's lives sit on.
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, TopZ + 95.f), FRotator::ZeroRotator, FVector(1.7f, 1.7f, 1.9f), BoneWhite);
		for (int32 B = 0; B < 3; ++B)
		{
			AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, TopZ + 45.f + B * 55.f), FRotator::ZeroRotator, FVector(1.74f, 1.74f, 0.12f), FLinearColor(0.09f, 0.10f, 0.12f));
		}
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, TopZ + 205.f), FRotator::ZeroRotator, FVector(1.2f, 1.2f, 0.3f), DarkSlate);
		AddAccent(Actor, Sphere, BaseCm + FVector(0, -95.f, TopZ + 165.f), FRotator::ZeroRotator, FVector(0.28f), TintFor(DefName), TealGlow);
	}
	else if (DefName == FName("Habitat"))
	{
		// REFERENCE (Facilities/ModularBlock): a walled modular compound - white
		// paneled perimeter walls with rounded corner posts, a lit cyan window
		// band on the south face, an orange door accent, and a modest central
		// dome. Replaces the single giant blob-dome.
		const float HalfX = ScaleM.X * 50.f;
		const float HalfY = ScaleM.Y * 50.f;
		const float WallZ = TopZ + 55.f;
		AddAccent(Actor, Cube, BaseCm + FVector(0, HalfY + 12.f, WallZ * 0.5f), FRotator::ZeroRotator, FVector(ScaleM.X + 0.3f, 0.35f, WallZ / 100.f), BoneWhite);
		AddAccent(Actor, Cube, BaseCm + FVector(0, -HalfY - 12.f, WallZ * 0.5f), FRotator::ZeroRotator, FVector(ScaleM.X + 0.3f, 0.35f, WallZ / 100.f), BoneWhite);
		AddAccent(Actor, Cube, BaseCm + FVector(HalfX + 12.f, 0, WallZ * 0.5f), FRotator::ZeroRotator, FVector(0.35f, ScaleM.Y + 0.3f, WallZ / 100.f), BoneWhite);
		AddAccent(Actor, Cube, BaseCm + FVector(-HalfX - 12.f, 0, WallZ * 0.5f), FRotator::ZeroRotator, FVector(0.35f, ScaleM.Y + 0.3f, WallZ / 100.f), BoneWhite);
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const float Sx = (Corner & 1) ? 1.f : -1.f;
			const float Sy = (Corner & 2) ? 1.f : -1.f;
			AddAccent(Actor, Cyl, BaseCm + FVector(Sx * (HalfX + 8.f), Sy * (HalfY + 8.f), (WallZ + 20.f) * 0.5f),
				FRotator::ZeroRotator, FVector(0.55f, 0.55f, (WallZ + 20.f) / 100.f), FLinearColor(0.68f, 0.68f, 0.72f));
		}
		// Lit crew-window band (south wall) + the airlock door accent.
		AddAccent(Actor, Cube, BaseCm + FVector(-HalfX - 32.f, 0, WallZ * 0.62f), FRotator::ZeroRotator, FVector(0.06f, ScaleM.Y * 0.7f, 0.32f), FLinearColor(0.5f, 0.85f, 0.9f), FLinearColor(0.3f, 1.1f, 1.3f));
		AddAccent(Actor, Cube, BaseCm + FVector(-HalfX - 26.f, HalfY * 0.55f, 65.f), FRotator::ZeroRotator, FVector(0.1f, 0.75f, 1.3f), FLinearColor(0.85f, 0.45f, 0.1f));
		// The central dome, sized to the compound instead of swallowing it.
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, TopZ + 25.f), FRotator::ZeroRotator, FVector(3.4f, 3.4f, 2.2f), BoneWhite);
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
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor((Color * 0.4f + FLinearColor(0.6f, 0.6f, 0.6f)).ToFColor(true));
	// Lie flat on the ground, glyph face pointing UP (+Z) so the top-down camera
	// reads the front of the text, not the mirrored back; glyph-up toward world
	// +X reads upright at the default north-looking camera heading (confirmed in
	// an oblique capture - the -Forward variant rendered it upside down). Built
	// from axes so it stays a pure rotation, never a reflection - the old Euler
	// combo faced the text into the ground and read backwards + upside down.
	const FVector AL = Actor->GetActorLocation();
	// Ride the scenery relief (12 cm above THE GROUND, not above z=0): far
	// deposit/rival labels would otherwise bury under rolling terrain.
	Label->SetWorldLocationAndRotation(
		FVector(AL.X - SouthOffsetCm, AL.Y, RHMarsTerrain::GroundZCm(AL.X - SouthOffsetCm, AL.Y) + 12.f),
		FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator());
}

void URHColonyVisualizerSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (URHSimWorldSubsystem* Sim = InWorld.GetSubsystem<URHSimWorldSubsystem>())
	{
		AddedHandle = Sim->OnBuildingAdded.AddUObject(this, &URHColonyVisualizerSubsystem::HandleBuildingAdded);
		CompletedHandle = Sim->OnBuildingCompleted.AddUObject(this, &URHColonyVisualizerSubsystem::HandleBuildingCompleted);
		RejectedHandle = Sim->OnCommandRejected.AddUObject(this, &URHColonyVisualizerSubsystem::HandleCommandRejected);
		RemovedHandle = Sim->OnBuildingRemoved.AddUObject(this, &URHColonyVisualizerSubsystem::HandleBuildingRemoved);
		Sim->OnQuotaMet.AddUObject(this, &URHColonyVisualizerSubsystem::HandleQuotaMet);
		Sim->OnShipArrived.AddUObject(this, &URHColonyVisualizerSubsystem::HandleShipArrived);
		Sim->OnDepositDiscovered.AddUObject(this, &URHColonyVisualizerSubsystem::HandleDepositDiscovered);
		Sim->OnSurveyCompleted.AddUObject(this, &URHColonyVisualizerSubsystem::HandleSurveyCompleted);
		Sim->OnAlert.AddUObject(this, &URHColonyVisualizerSubsystem::HandleAlert);
		Sim->OnColonyReloaded.AddUObject(this, &URHColonyVisualizerSubsystem::HandleColonyReloaded);
		// C3: anything that can change what the per-frame passes draw wakes them.
		Sim->OnCommandExecuted.AddWeakLambda(this, [this](const FRHCommand&) { MarkPassesDirty(); });
		Sim->OnFurnitureChanged.AddWeakLambda(this, [this](const FIntVector&) { MarkPassesDirty(); });
		Sim->OnWallFeatureChanged.AddWeakLambda(this, [this](const FIntVector&) { MarkPassesDirty(); });
		Sim->OnGrowthChanged.AddWeakLambda(this, [this]() { MarkPassesDirty(); }); // C5b: a project began or ended: rooms, cells and beds changed with no order
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
	AppliedPowerState.Reset();
	// Ids are reused across a colony reload, so a remembered depth could be
	// attributed to a different building def entirely.
	AuthoredPulseDepth.Reset();
	// The lamps died with their actors above; drop the stale weak handles.
	BuildingLights.Reset();
	for (AStaticMeshActor* Marker : DepositMarkers)
	{
		if (Marker)
		{
			Marker->Destroy();
		}
	}
	DepositMarkers.Reset();
	DepositMarkerById.Reset();
	DepositShapeKey.Reset();
	DepositClaimDressed.Reset();
	if (ShipVisual)
	{
		ShipVisual->Destroy();
		ShipVisual = nullptr;
	}
	if (LandingPadVisual)
	{
		LandingPadVisual->Destroy();
		LandingPadVisual = nullptr;
	}
	ShipBeat = 0;
	ShipLandedSimSeconds = -1.0;
	ShipEtaLanded = -1.0;
	// Shaft mirror rebuilds from the loaded counts on the next Tick.
	if (ShaftVisual)
	{
		ShaftVisual->Destroy();
		ShaftVisual = nullptr;
	}
	LastShaftDepthSeen = 0;
	for (AStaticMeshActor* Tile : CarveTileVisuals)
	{
		if (Tile)
		{
			Tile->Destroy();
		}
	}
	CarveTileVisuals.Reset();
	TilesSpawnedPerLevel.Reset();
	TileByCell.Reset();
	AppliedRoomTint.Reset();
	RoomPropByCell.Reset();       // prop components died with their tiles just above
	FurnitureVis.Reset();         // furniture died with its tiles / habitat actors
	DoorLeaves.Reset();           // door leaves died with their shells
	FurnitureCompsByCell.Reset();
	PropsHiddenForFurniture.Reset();
	LightByCell.Reset();          // cell lights died with their tiles too
	ClutterSpawnedPerLevel.Reset(); // clutter died with its tiles: re-accumulates
	for (auto& Pair : RivalMarkers)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	RivalMarkers.Reset();
	RivalMarkerState.Reset();
	if (ConvoyVisual)
	{
		ConvoyVisual->Destroy();
		ConvoyVisual = nullptr;
	}
	LastRigKey = FIntVector(-999, -999, -999); // force a pit rebuild from loaded state
	MarkPassesDirty();
	++PropsVersion;
	bLightKeyValid = false;
	HoverLabels.Reset();
	HoverGrid.Reset();
	HoverLit.Reset();

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
	RestoreSupplyShip(Sim);
	ApplyViewLevel(); // the reloaded colony honors the elevator's floor
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
	// Discovery honesty (M1-b): only surveyed ground gets a marker - the
	// undiscovered rows exist in the sim, not on the player's map.
	for (const FRHDepositState& D : Sim->GetDeposits())
	{
		if (D.bDiscovered)
		{
			SpawnDepositMarker(D);
		}
	}
}

namespace
{
	// A deposit dig site's shape (the director's pick, 2026-10-17: "deposit B"
	// on the concept sheet). Everything is derived from the sim's deposit state,
	// so it reloads and rebuilds from nothing:
	//   - an untouched deposit is a low mound of looser ground, ragged at the
	//     edge, with rocks around it (the "natural" look);
	//   - once worked (designated, claimed or anything taken out) a flat floor
	//     at ground level opens in the middle - where the diggers stand and the
	//     Ice Drill sits, so nothing drawn at ground height floats or sinks -
	//     with two terraces stepping up to the untouched deposit, a notch cut
	//     toward the Lander (the way in), and the dug pile as a heap on the floor.
	//     The floor widens as the deposit is used up; a spent deposit is all floor.
	// Sizes come from the SURVEYED mass (InitialKg), so the site no longer
	// shrinks as it is mined; the dig grows instead.
	struct FRHDepositShape
	{
		float R = 800.f;      // outer radius, cm
		float Fade = 180.f;   // the rim's slope down to the terrain
		float Rf = 0.f;       // floor radius (0 = untouched)
		float W = 70.f;       // terrace width
		float Hp = 55.f;      // untouched deposit height above the terrain
		float RampYaw = 0.f;  // radians, toward the Lander
		FVector2D PileC = FVector2D::ZeroVector;
		float PileR = 0.f, PileH = 0.f;
		bool bWorked = false;
		uint32 Seed = 1;
	};

	float SmoothStepF(float E0, float E1, float X)
	{
		const float T = FMath::Clamp((X - E0) / (E1 - E0), 0.f, 1.f);
		return T * T * (3.f - 2.f * T);
	}

	FRHDepositShape MakeDepositShape(const FRHDepositState& D, double PileCapKg)
	{
		FRHDepositShape S;
		const double Initial = D.InitialKg > 0.0 ? D.InitialKg : D.RemainingKg;
		const float Mined = Initial > 0.0 ? (float)FMath::Clamp(1.0 - D.RemainingKg / Initial, 0.0, 1.0) : 0.f;
		// 60 t ~ 8.3 m radius, the old marker's footprint; 100 t ~ 10.8 m.
		S.R = FMath::Clamp((float)FMath::Sqrt(FMath::Max(Initial, 0.0)) * 3.4f, 500.f, 1500.f);
		S.Fade = 0.22f * S.R;
		S.Hp = D.Type == FName("Ice") ? 45.f : D.Type == FName("Ore") ? 60.f : 55.f;
		S.bWorked = Mined > 0.0005f || D.bDesignated || D.DigClaims > 0;
		S.RampYaw = FMath::Atan2(-D.LocationCm.Y, -D.LocationCm.X);
		S.Seed = (uint32)D.Id * 7919u + 17u;
		if (S.bWorked)
		{
			// Ice and ore carry a drill or a rig in the middle: a wider first floor.
			// Ore is dug like regolith; a small deposit keeps its terraces.
			const float Rf0 = D.Type == FName("Ice") ? 380.f : FMath::Min(300.f, 0.4f * S.R);
			const float RfMax = FMath::Max(Rf0, S.R - S.Fade - 2.f * S.W - 30.f);
			S.Rf = FMath::Lerp(Rf0, RfMax, FMath::Pow(Mined, 0.7f));
		}
		if (D.PileKg > 1.0 && PileCapKg > 0.0)
		{
			const float F = FMath::Sqrt((float)FMath::Clamp(D.PileKg / PileCapKg, 0.0, 1.0));
			S.PileR = 50.f + 70.f * F;
			S.PileH = 20.f + 70.f * F;
			// On the floor, away from the diggers at the centre and opposite the
			// way in.
			const float Dist = FMath::Max(150.f, FMath::Min(0.62f * FMath::Max(S.Rf, 300.f), FMath::Max(S.Rf, 300.f) - 40.f));
			S.PileC = FVector2D(FMath::Cos(S.RampYaw + PI), FMath::Sin(S.RampYaw + PI)) * Dist;
		}
		return S;
	}

	float DepositNoise(float X, float Y, uint32 Seed, float Freq)
	{
		return FMath::PerlinNoise2D(FVector2D(X * Freq + (Seed % 997) * 0.37f, Y * Freq + (Seed % 613) * 0.53f));
	}

	// The rim's wobble: outside the dig the rings are pushed in and out by a
	// noise of the bearing, so the deposit meets the terrain on a ragged line
	// while the terraces stay round.
	float DepositWobble(const FRHDepositShape& S, float RingR, float Yaw)
	{
		const float Taper = SmoothStepF(S.Rf + 2.f * S.W + 60.f, 0.8f * S.R, RingR);
		return Taper * 0.09f * S.R * FMath::PerlinNoise2D(FVector2D(FMath::Cos(Yaw) * 2.3f + (S.Seed % 101) * 0.71f, FMath::Sin(Yaw) * 2.3f + (S.Seed % 89) * 0.43f));
	}

	// Height above the terrain (cm) of the deposit's own ground. R is the
	// ring's parametric radius (before the wobble) and X, Y the actual point,
	// which only the surface noise reads.
	float DepositHeight(const FRHDepositShape& S, float R, float Yaw, float X, float Y, float* OutRamp = nullptr)
	{
		float H;
		float Ramp = 0.f;
		if (R > S.R)
		{
			H = -14.f; // the skirt tucks under the terrain: no seam to z-fight
		}
		else if (!S.bWorked)
		{
			H = S.Hp * 0.8f * SmoothStepF(S.R, S.R - S.Fade - 0.25f * S.R, R)
				+ S.Hp * 0.18f * DepositNoise(X, Y, S.Seed, 0.004f) * SmoothStepF(S.R, S.R - S.Fade, R);
		}
		else
		{
			H = R <= S.Rf ? 0.f
				: S.Hp * 0.5f * SmoothStepF(S.Rf, S.Rf + 28.f, R) + S.Hp * 0.5f * SmoothStepF(S.Rf + S.W, S.Rf + S.W + 28.f, R);
			H *= SmoothStepF(S.R, S.R - S.Fade, R);
			const float DA = FMath::Abs(FMath::FindDeltaAngleRadians(S.RampYaw, Yaw));
			Ramp = SmoothStepF(0.50f, 0.28f, DA);
			H *= 1.f - 0.85f * Ramp;
			H += S.Hp * 0.12f * DepositNoise(X, Y, S.Seed, 0.004f) * SmoothStepF(S.Rf, S.Rf + 60.f, R) * SmoothStepF(S.R, S.R - S.Fade, R);
			H += 2.f * DepositNoise(X, Y, S.Seed + 5u, 0.02f) * (R <= S.Rf ? 1.f : 0.f);
		}
		if (OutRamp) { *OutRamp = Ramp; }
		return H;
	}

	// Per deposit type: the five sections' surfaces - 0 the terrain's own look
	// (unused since the rim meets the terrain by geometry), 1 untouched
	// deposit, 2 the cut, 3 the heap, 4 rocks. bWhite
	// swaps the rust regolith texture for flat white so the tint IS the colour
	// (frost and blue ice cannot be reached by tinting a rust texture).
	struct FRHDepositSurface { FLinearColor Tint; bool bWhite; float Rough; float TileCm; };
	void DepositSurfaces(FName Type, FRHDepositSurface Out[5])
	{
		Out[0] = { FLinearColor(1.f, 1.f, 1.f), false, 0.95f, 900.f };
		if (Type == FName("Ice"))
		{
			Out[1] = { FLinearColor(0.60f, 0.55f, 0.53f), true, 0.85f, 900.f };   // dusty frost
			Out[2] = { FLinearColor(0.40f, 0.60f, 0.78f), true, 0.30f, 900.f };   // exposed ice
			Out[3] = { FLinearColor(0.52f, 0.60f, 0.68f), true, 0.60f, 900.f };   // ice spoil
			Out[4] = { FLinearColor(0.85f, 0.80f, 0.80f), false, 0.90f, 500.f };  // frosted rock
		}
		else if (Type == FName("Ore"))
		{
			Out[1] = { FLinearColor(0.45f, 0.42f, 0.47f), false, 0.90f, 900.f };  // dark basalt
			Out[2] = { FLinearColor(0.030f, 0.032f, 0.040f), true, 0.65f, 900.f }; // fresh slate
			Out[3] = { FLinearColor(0.38f, 0.36f, 0.42f), false, 0.80f, 900.f };
			Out[4] = { FLinearColor(0.32f, 0.31f, 0.36f), false, 0.85f, 500.f };
		}
		else
		{
			Out[1] = { FLinearColor(1.32f, 1.16f, 1.00f), false, 0.95f, 900.f };  // loose, lighter
			Out[2] = { FLinearColor(0.58f, 0.44f, 0.38f), false, 0.95f, 900.f };  // fresh cut
			Out[3] = { FLinearColor(1.15f, 0.90f, 0.74f), false, 0.95f, 900.f };  // spoil: loose, lighter than the cut
			Out[4] = { FLinearColor(0.60f, 0.55f, 0.52f), false, 0.95f, 500.f };  // the scenery's boulder tint
		}
	}

	struct FRHDepAcc
	{
		TArray<FVector> V;
		TArray<int32> I;
		TArray<FVector> N;
		// RHMarsTerrain's winding: for (A, B, C) the face normal is
		// (C - A) x (B - A), and it must point up to face the camera.
		void Tri(const FVector& A, const FVector& B, const FVector& C)
		{
			FVector Nm = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
			if (Nm.IsNearlyZero())
			{
				return;
			}
			const bool bFlip = Nm.Z < 0.f;
			const int32 Base = V.Num();
			V.Add(A); V.Add(bFlip ? C : B); V.Add(bFlip ? B : C);
			Nm = bFlip ? -Nm : Nm;
			N.Add(Nm); N.Add(Nm); N.Add(Nm);
			I.Add(Base); I.Add(Base + 1); I.Add(Base + 2);
		}
	};
}

void URHColonyVisualizerSubsystem::SpawnDepositMarker(const FRHDepositState& D)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// The dig site is a generated mesh on a container actor at ground zero;
	// the mesh carries the terrain height itself (RHMarsTerrain::GroundZCm).
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(FVector(D.LocationCm.X, D.LocationCm.Y, 0.f), FRotator::ZeroRotator);
	if (!Actor)
	{
		return;
	}
	Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
#if WITH_EDITOR
	Actor->SetActorLabel(FString::Printf(TEXT("Sim_Dep_%s"), *D.RowName.ToString()));
#endif
	UProceduralMeshComponent* PMC = NewObject<UProceduralMeshComponent>(Actor, TEXT("DigSite"));
	PMC->SetupAttachment(Actor->GetRootComponent());
	PMC->RegisterComponent();
	PMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PMC->SetCastShadow(true);
	UMaterialInterface* Surf = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_MarsSurface.M_MarsSurface"));
	UTexture2D* Regolith = LoadObject<UTexture2D>(nullptr, TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"));
	UTexture2D* White = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	FRHDepositSurface Surfaces[5];
	DepositSurfaces(D.Type, Surfaces);
	for (int32 Sec = 0; Sec < 5; ++Sec)
	{
		if (!Surf)
		{
			break;
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Surf, PMC);
		UTexture2D* Tex = Surfaces[Sec].bWhite && White ? White : Regolith;
		if (Tex)
		{
			Mid->SetTextureParameterValue(FName("SurfTex"), Tex);
		}
		Mid->SetScalarParameterValue(FName("TileCm"), Surfaces[Sec].TileCm);
		Mid->SetVectorParameterValue(FName("Tint"), Surfaces[Sec].Tint);
		Mid->SetScalarParameterValue(FName("Rough"), Surfaces[Sec].Rough);
		PMC->SetMaterial(Sec, Mid);
	}
	BuildDepositGround(Actor, D);
	// Ground-truth label colours: regolith rust, ore slate, ice pale blue.
	FLinearColor DepColor(0.55f, 0.30f, 0.12f);
	if (D.Type == FName("Ore"))
	{
		DepColor = FLinearColor(0.30f, 0.32f, 0.40f);
	}
	else if (D.Type == FName("Ice"))
	{
		DepColor = FLinearColor(0.75f, 0.90f, 1.00f);
	}
	// Players read the material, not the data row (interface pass: no raw IDs).
	const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FRHDepositShape Shape = MakeDepositShape(D, Defs ? Defs->GetConfigScalar(FName("PilesCapacity_kg"), 500.0) : 500.0);
	AddLabel(Actor, D.Type.ToString() + TEXT(" deposit"), DepColor, Shape.R + 260.f);
	UE_LOG(LogRedHope, Display, TEXT("Dig site: %s (%s) at (%.0f, %.0f) m, %.1f m across, %s"), *D.RowName.ToString(), *D.Type.ToString(),
		D.LocationCm.X / 100.0, D.LocationCm.Y / 100.0, Shape.R / 50.f, Shape.bWorked ? TEXT("worked") : TEXT("untouched"));
	// Surface furniture: born hidden if the player is currently underground
	// (adversarial-review finding - a discovery mid-descent used to pop the
	// marker into the pit view until the next floor change).
	Actor->SetActorHiddenInGame(IsUnderground());
	DepositMarkers.Add(Actor);
	DepositMarkerById.Add(D.Id, Actor);
}

void URHColonyVisualizerSubsystem::BuildDepositGround(AStaticMeshActor* Actor, const FRHDepositState& D)
{
	UProceduralMeshComponent* PMC = Actor ? Actor->FindComponentByClass<UProceduralMeshComponent>() : nullptr;
	const URHDefinitionsSubsystem* Defs = GetWorld() ? GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!PMC)
	{
		return;
	}
	const FRHDepositShape S = MakeDepositShape(D, Defs ? Defs->GetConfigScalar(FName("PilesCapacity_kg"), 500.0) : 500.0);
	const FVector2D Origin(D.LocationCm.X, D.LocationCm.Y);
	// The ground the player SEES: the scenery's faceted relief, or the map's
	// flat MarsGround pad where the pad lies above it (the pad is a 5 km plane
	// at z=0, so it covers every dune trough and the whole colony flat). A few cm proud, as the old
	// marker sat, so the faceted terrain does not poke through.
	const auto Ground = [&](float X, float Y)
	{
		float Z = RHMarsTerrain::SceneryGroundZCm(Origin.X + X, Origin.Y + Y);
		if (bGroundPadMeasured && GroundPadBox.IsValid
			&& FMath::IsWithin(Origin.X + X, GroundPadBox.Min.X, GroundPadBox.Max.X)
			&& FMath::IsWithin(Origin.Y + Y, GroundPadBox.Min.Y, GroundPadBox.Max.Y))
		{
			Z = FMath::Max(Z, (float)GroundPadBox.Max.Z);
		}
		return Z + 4.f;
	};

	// Concentric rings, faceted like the terrain: the terrace edges come out
	// round, and every surface boundary falls on a ring. The outermost rings
	// dip under the terrain, so the deposit meets the ground on its own line.
	FRHDepAcc Acc[5];
	TArray<float> Rings;
	const float Outer = S.R + 50.f;
	for (float Rr = 25.f; Rr < Outer; Rr += 25.f)
	{
		Rings.Add(Rr);
	}
	Rings.Add(Outer);
	const float CutR = S.Rf + 2.f * S.W + 20.f;
	if (S.bWorked)
	{
		for (const float Edge : { S.Rf, S.Rf + 28.f, S.Rf + S.W, S.Rf + S.W + 28.f, CutR })
		{
			Rings.Add(Edge);
		}
	}
	Rings.Sort();
	for (int32 k = Rings.Num() - 1; k > 0; --k)
	{
		if (Rings[k] - Rings[k - 1] < 4.f)
		{
			Rings.RemoveAt(k); // near-duplicate edges make slivers
		}
	}
	const int32 Segs = FMath::Clamp(FMath::RoundToInt(2.f * PI * S.R / 30.f), 64, 256);
	const auto RingPoint = [&](float Rr, int32 j, float* OutRamp = nullptr) -> FVector
	{
		const float Yaw = 2.f * PI * j / Segs;
		const float Ra = Rr + DepositWobble(S, Rr, Yaw);
		const float X = FMath::Cos(Yaw) * Ra, Y = FMath::Sin(Yaw) * Ra;
		return FVector(X, Y, Ground(X, Y) + DepositHeight(S, Rr, Yaw, X, Y, OutRamp));
	};
	const auto SectionFor = [&](float MidR, float Ramp)
	{
		return S.bWorked && (MidR < CutR || (Ramp > 0.5f && MidR < S.R - 0.5f * S.Fade)) ? 2 : 1;
	};
	const FVector Centre(0.f, 0.f, Ground(0.f, 0.f) + DepositHeight(S, 0.f, 0.f, 0.f, 0.f));
	for (int32 j = 0; j < Segs; ++j)
	{
		Acc[SectionFor(Rings[0] * 0.5f, 0.f)].Tri(Centre, RingPoint(Rings[0], j), RingPoint(Rings[0], (j + 1) % Segs));
	}
	for (int32 k = 0; k + 1 < Rings.Num(); ++k)
	{
		for (int32 j = 0; j < Segs; ++j)
		{
			float Ramp = 0.f;
			const FVector A = RingPoint(Rings[k], j, &Ramp);
			const FVector B = RingPoint(Rings[k + 1], j);
			const FVector C = RingPoint(Rings[k + 1], (j + 1) % Segs);
			const FVector E = RingPoint(Rings[k], (j + 1) % Segs);
			const int32 Sec = SectionFor(0.5f * (Rings[k] + Rings[k + 1]), Ramp);
			Acc[Sec].Tri(A, B, C);
			Acc[Sec].Tri(A, C, E);
		}
	}

	// The dug pile: its own little dome on the floor, rim tucked under.
	if (S.PileR > 0.f)
	{
		const int32 HSegs = 28;
		const auto HeapPoint = [&](float Rr, int32 j) -> FVector
		{
			const float Yaw = 2.f * PI * j / HSegs;
			const float X = S.PileC.X + FMath::Cos(Yaw) * Rr, Y = S.PileC.Y + FMath::Sin(Yaw) * Rr;
			const float H = Rr >= S.PileR ? -2.f
				: S.PileH * FMath::Pow(1.f - Rr / S.PileR, 1.3f) * (1.f + 0.15f * DepositNoise(X, Y, S.Seed + 9u, 0.03f));
			const float Under = DepositHeight(S, FMath::Sqrt(X * X + Y * Y), FMath::Atan2(Y, X), X, Y);
			return FVector(X, Y, Ground(X, Y) + Under + H);
		};
		const float Top = S.PileH * (1.f + 0.15f * DepositNoise(S.PileC.X, S.PileC.Y, S.Seed + 9u, 0.03f));
		const FVector Peak(S.PileC.X, S.PileC.Y, Ground(S.PileC.X, S.PileC.Y) + Top);
		const float HRings[] = { 0.2f, 0.45f, 0.7f, 0.9f, 1.0f, 1.08f };
		for (int32 j = 0; j < HSegs; ++j)
		{
			Acc[3].Tri(Peak, HeapPoint(S.PileR * HRings[0], j), HeapPoint(S.PileR * HRings[0], (j + 1) % HSegs));
			for (int32 k = 0; k + 1 < (int32)UE_ARRAY_COUNT(HRings); ++k)
			{
				const FVector A = HeapPoint(S.PileR * HRings[k], j), B = HeapPoint(S.PileR * HRings[k + 1], j);
				const FVector C = HeapPoint(S.PileR * HRings[k + 1], (j + 1) % HSegs), E = HeapPoint(S.PileR * HRings[k], (j + 1) % HSegs);
				Acc[3].Tri(A, B, C);
				Acc[3].Tri(A, C, E);
			}
		}
	}

	// Rocks around the rim (the scenery's own boulders), clear of the way in;
	// a few on the untouched deposit.
	FRandomStream Rand((int32)S.Seed);
	const int32 NumRocks = FMath::Clamp(FMath::RoundToInt(S.R / 60.f), 8, 22);
	for (int32 k = 0; k < NumRocks; ++k)
	{
		// Draw everything first so the stream never depends on a skip.
		const float Yaw = Rand.FRandRange(-PI, PI);
		const float Rr = Rand.FRandRange(0.62f, 1.08f) * S.R;
		const float Sz = Rand.FRandRange(0.35f, 1.05f);
		const float Spin = Rand.FRandRange(0.f, 360.f);
		const uint32 RockSeed = (uint32)Rand.RandRange(1, 1 << 24);
		if (S.bWorked && FMath::Abs(FMath::FindDeltaAngleRadians(S.RampYaw, Yaw)) < 0.5f)
		{
			continue;
		}
		if (S.bWorked && Rr < S.Rf + 2.f * S.W + 40.f)
		{
			continue; // the dig took it
		}
		const float Ra = Rr + DepositWobble(S, Rr, Yaw);
		const float X = FMath::Cos(Yaw) * Ra, Y = FMath::Sin(Yaw) * Ra;
		const FVector Radii(Sz * 55.f * Rand.FRandRange(0.7f, 1.4f), Sz * 55.f * Rand.FRandRange(0.7f, 1.4f), Sz * 40.f);
		const float Z = Ground(X, Y) + FMath::Max(0.f, DepositHeight(S, Rr, Yaw, X, Y)) - Radii.Z * 0.35f;
		RHMarsTerrain::AppendRock(Acc[4].V, Acc[4].I, Acc[4].N, FVector(X, Y, Z), Radii, Spin, RockSeed);
	}

	for (int32 Sec = 0; Sec < 5; ++Sec)
	{
		if (Acc[Sec].I.Num() > 0)
		{
			PMC->CreateMeshSection(Sec, Acc[Sec].V, Acc[Sec].I, Acc[Sec].N,
				TArray<FVector2D>(), TArray<FColor>(), TArray<FProcMeshTangent>(), false);
			PMC->SetMeshSectionVisible(Sec, true);
		}
		else if (Sec < PMC->GetNumSections())
		{
			PMC->ClearMeshSection(Sec);
		}
	}

	// The claim: survey stakes with flags around the rim and a beacon by the
	// way in, the moment the deposit is worked. Absolute world-space accents.
	if (S.bWorked && !DepositClaimDressed.Contains(D.Id))
	{
		DepositClaimDressed.Add(D.Id);
		const TCHAR* Cyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
		const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
		const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
		const float StakeR = S.R + 35.f;
		for (int32 k = 0; k < 8; ++k)
		{
			const float Yaw = S.RampYaw + PI / 8.f + k * (PI / 4.f);
			const FVector2D At = Origin + FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw)) * StakeR;
			const float Gz = Ground(At.X - Origin.X, At.Y - Origin.Y) - 4.f;
			AddAccent(Actor, Cyl, FVector(At.X, At.Y, Gz + 70.f), FRotator::ZeroRotator, FVector(0.05f, 0.05f, 1.4f), RHCanon::BoneWhite);
			// The flag streams tangentially, so from above it reads as a tick
			// around the ring, not a dot.
			const FVector Tangent(-FMath::Sin(Yaw), FMath::Cos(Yaw), 0.f);
			AddAccent(Actor, Cube, FVector(At.X, At.Y, Gz + 124.f) + Tangent * 20.f, FRotator(0.f, FMath::RadiansToDegrees(Yaw), 0.f),
				FVector(0.02f, 0.40f, 0.26f), RHCanon::HazYellow);
		}
		const FVector2D BeaconAt = Origin + FVector2D(FMath::Cos(S.RampYaw + 0.62f), FMath::Sin(S.RampYaw + 0.62f)) * (S.R + 60.f);
		const float Bz = Ground(BeaconAt.X - Origin.X, BeaconAt.Y - Origin.Y) - 4.f;
		AddAccent(Actor, Cyl, FVector(BeaconAt.X, BeaconAt.Y, Bz + 95.f), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 1.9f), RHCanon::DarkSlate);
		AddAccent(Actor, Sphere, FVector(BeaconAt.X, BeaconAt.Y, Bz + 200.f), FRotator::ZeroRotator, FVector(0.22f), RHCanon::HazYellow, RHCanon::AmberGlow);
	}
}

void URHColonyVisualizerSubsystem::RefreshDepositMarkers(const URHSimWorldSubsystem* Sim)
{
	const URHDefinitionsSubsystem* Defs = GetWorld() ? GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	// The map's ground pad is found by the scenery pass, after the first dig
	// sites were built: measure it once, then rebuild every site on it.
	if (!bGroundPadMeasured && GroundActor.IsValid())
	{
		bGroundPadMeasured = true;
		GroundPadBox = GroundActor->GetComponentsBoundingBox(true);
		DepositShapeKey.Reset();
		UE_LOG(LogRedHope, Display, TEXT("Dig sites: ground pad spans (%.0f..%.0f, %.0f..%.0f) m, top z %.1f cm"),
			GroundPadBox.Min.X / 100.0, GroundPadBox.Max.X / 100.0, GroundPadBox.Min.Y / 100.0, GroundPadBox.Max.Y / 100.0, GroundPadBox.Max.Z);
	}
	const double PileCap = Defs ? Defs->GetConfigScalar(FName("PilesCapacity_kg"), 500.0) : 500.0;
	for (const FRHDepositState& D : Sim->GetDeposits())
	{
		const TWeakObjectPtr<AStaticMeshActor>* Found = DepositMarkerById.Find(D.Id);
		AStaticMeshActor* Actor = Found ? Found->Get() : nullptr;
		if (!Actor)
		{
			continue;
		}
		// Rebuild only when the picture would change: half-percent steps of the
		// dig, 20 kg steps of the pile, and the worked switch.
		const FRHDepositShape S = MakeDepositShape(D, PileCap);
		const double Initial = D.InitialKg > 0.0 ? D.InitialKg : D.RemainingKg;
		const uint32 Mined = Initial > 0.0 ? (uint32)FMath::RoundToInt(200.0 * FMath::Clamp(1.0 - D.RemainingKg / Initial, 0.0, 1.0)) : 0u;
		const uint32 Key = HashCombine(HashCombine(GetTypeHash(Mined), GetTypeHash((uint32)FMath::RoundToInt(D.PileKg / 20.0))), GetTypeHash(S.bWorked));
		uint32& Last = DepositShapeKey.FindOrAdd(D.Id, ~0u);
		if (Last != Key)
		{
			Last = Key;
			BuildDepositGround(Actor, D);
		}
	}
}

void URHColonyVisualizerSubsystem::HandleSurveyCompleted(const FRHSurveyRecord& Record)
{
	// Discoveries already notice per deposit; the empty result needs its own
	// voice or the player reads a quiet survey as a broken one.
	if (Record.FoundCount == 0)
	{
		LastNotice = FString::Printf(TEXT("SURVEY complete at (%.0f, %.0f) m: no deposits within %.0f m"),
			Record.PointCm.X / 100.f, Record.PointCm.Y / 100.f, Record.RadiusM);
		LastNoticeRealSeconds = FPlatformTime::Seconds();
	}
}

void URHColonyVisualizerSubsystem::HandleDepositDiscovered(const FRHDepositState& D)
{
	// The go-look payoff: the marker appears the moment the scout reports.
	SpawnDepositMarker(D);
	LastNotice = FString::Printf(TEXT("Survey found a %s deposit - %.0f t"),
		*D.Type.ToString().ToLower(), D.RemainingKg / 1000.0);
	LastNoticeRealSeconds = FPlatformTime::Seconds();
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
			Sim->OnBuildingRemoved.Remove(RemovedHandle);
		}
	}
	Super::Deinitialize();
}

void URHColonyVisualizerSubsystem::AnimateDoors(float DeltaTime)
{
	const URHCrewVisualizerSubsystem* Crew = GetWorld() ? GetWorld()->GetSubsystem<URHCrewVisualizerSubsystem>() : nullptr;
	for (auto It = DoorLeaves.CreateIterator(); It; ++It)
	{
		FRHDoorLeaves& D = It->Value;
		UStaticMeshComponent* L = D.L.Get();
		UStaticMeshComponent* R = D.R.Get();
		if (!L || !R)
		{
			It.RemoveCurrent(); // the shell redrew or the door was sold
			continue;
		}
		// The leaves part while anyone is within a couple of metres.
		const float Target = (Crew && Crew->IsAnyCrewWithinCm(D.Mid, 230.f)) ? 1.f : 0.f;
		const float Was = D.Open;
		D.Open = FMath::FInterpConstantTo(D.Open, Target, DeltaTime, 2.2f);
		if (D.Open == Was)
		{
			continue;
		}
		const float Slide = 45.f + 80.f * D.Open; // a leaf's centre from the doorway's middle
		L->SetWorldLocation(D.Mid - D.Along * Slide);
		R->SetWorldLocation(D.Mid + D.Along * Slide);
	}
}

FString URHColonyVisualizerSubsystem::FurniturePropPath(FName Row, float& OutFitCm)
{
	struct FEntry { const TCHAR* Path; float FitCm; };
	static const TMap<FName, FEntry> Paths = {
		{ FName("Cot"),         { TEXT("/Game/RedHope/Art/Furniture/furn_cot/StaticMeshes/furn_cot.furn_cot"), 0.f } },
		{ FName("BunkBed"),     { TEXT("/Game/RedHope/Art/Furniture/furn_bunkbed/StaticMeshes/furn_bunkbed.furn_bunkbed"), 0.f } },
		{ FName("Desk"),        { TEXT("/Game/RedHope/Art/Furniture/furn_desk/StaticMeshes/furn_desk.furn_desk"), 0.f } },
		{ FName("Chair"),       { TEXT("/Game/RedHope/Art/Furniture/furn_chair/StaticMeshes/furn_chair.furn_chair"), 0.f } },
		{ FName("Bench"),       { TEXT("/Game/RedHope/Art/Furniture/furn_bench/StaticMeshes/furn_bench.furn_bench"), 0.f } },
		{ FName("Sofa"),        { TEXT("/Game/RedHope/Art/Furniture/furn_sofa/StaticMeshes/furn_sofa.furn_sofa"), 0.f } },
		{ FName("Lamp"),        { TEXT("/Game/RedHope/Art/Furniture/furn_lamp/StaticMeshes/furn_lamp.furn_lamp"), 0.f } },
		{ FName("Rug"),         { TEXT("/Game/RedHope/Art/Furniture/furn_rug/StaticMeshes/furn_rug.furn_rug"), 0.f } },
		{ FName("Plant"),       { TEXT("/Game/RedHope/Art/Furniture/furn_plant/StaticMeshes/furn_plant.furn_plant"), 0.f } },
		// Room props reused as catalogue pieces, fitted like the room props.
		{ FName("LabBench"),    { TEXT("/Game/RedHope/Art/Props2/labbench/StaticMeshes/labbench.labbench"), 240.f } },
		{ FName("Workbench"),   { TEXT("/Game/RedHope/Art/Tiers/workbench_lg/StaticMeshes/workbench_lg.workbench_lg"), 240.f } },
		{ FName("DiningTable"), { TEXT("/Game/RedHope/Art/Props2/diningtable/StaticMeshes/diningtable.diningtable"), 220.f } },
		{ FName("Locker"),      { TEXT("/Game/RedHope/Art/Props2/locker/StaticMeshes/locker.locker"), 90.f } },
	};
	const FEntry* E = Paths.Find(Row);
	OutFitCm = E ? E->FitCm : 0.f;
	return E ? FString(E->Path) : FString();
}

bool URHColonyVisualizerSubsystem::GetFurnitureFrame(int32 Id, FVector& OutCentreCm, float& OutYawDeg, FVector2D& OutHalfCm) const
{
	const FRHFurnVis* Vis = FurnitureVis.Find(Id);
	if (!Vis || !Vis->Comp.IsValid())
	{
		return false;
	}
	OutCentreCm = Vis->FrameCm;
	OutYawDeg = Vis->YawDeg;
	OutHalfCm = Vis->HalfCm;
	return true;
}

void URHColonyVisualizerSubsystem::RefreshFurniture(const URHSimWorldSubsystem* Sim)
{
	if (!Sim)
	{
		return;
	}
	const URHDefinitionsSubsystem* Defs = GetWorld() ? GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	TSet<int32> Live;
	FurnitureCompsByCell.Reset();
	for (const FRHFurnitureInstance& F : Sim->GetFurniture())
	{
		Live.Add(F.Id);
		// A hash of (row, cell, slot, rotation): the same identity the old string
		// key carried, without formatting thousands of strings per pass.
		uint32 Sig = HashCombine(GetTypeHash(F.Row), GetTypeHash(F.Key));
		Sig = HashCombine(Sig, (uint32)(F.Slot * 8 + F.Rot));
		Sig = Sig == 0 ? 1u : Sig;
		FRHFurnVis& Vis = FurnitureVis.FindOrAdd(F.Id);
		UStaticMeshComponent* Comp = Vis.Comp.Get();
		if (!Comp || Vis.Sig != Sig)
		{
			++PropsVersion;
			if (Comp)
			{
				Comp->DestroyComponent();
				Comp = nullptr;
			}
			Vis.Comp = nullptr;
			Vis.Sig = 0;
			AActor* Owner = F.Key.Z == 0 ? (AActor*)TileByCell.FindRef(FIntVector(F.Key.X, F.Key.Y, 0)).Get() : (AActor*)BuildingVisuals.FindRef(F.Key.Z).Get();
			float FitCm = 0.f;
			const FString Path = FurniturePropPath(F.Row, FitCm);
			UStaticMesh* Mesh = (Owner && !Path.IsEmpty()) ? LoadObject<UStaticMesh>(nullptr, *Path) : nullptr;
			FVector Centre;
			FVector2D Half;
			if (!Mesh || !Sim->GetFurnitureFootprintCm(F.Row, F.Key, F.Slot, F.Rot, Centre, Half))
			{
				bPassesDirty = true; // its tile / habitat actor isn't up yet: try again next tick
				continue;
			}
			Comp = NewObject<UStaticMeshComponent>(Owner);
			Comp->SetupAttachment(Owner->GetRootComponent());
			Comp->SetMobility(EComponentMobility::Movable);
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Comp->SetAbsolute(true, true, true); // a tile's flat scale would squash it
			Comp->RegisterComponent();
			Comp->SetStaticMesh(Mesh);
			const FBoxSphereBounds MB = Mesh->GetBounds();
			const float S = FitCm > 0.f ? FitCm / FMath::Max(2.f * FMath::Max(MB.BoxExtent.X, MB.BoxExtent.Y), 1.f) : 1.f;
			const FRotator Yaw(0.f, 90.f * F.Rot, 0.f);
			// The code-built pieces are authored front at -Y in Blender, and the
			// glTF import flips Y (measured 2026-10-16: the chair's back came in
			// at -Y), so they are drawn a half turn round - otherwise every
			// chair sat with its back to its table. The reused room props keep
			// their old fit.
			const bool bBuiltPiece = Path.StartsWith(TEXT("/Game/RedHope/Art/Furniture/"));
			const FRotator MeshYaw(0.f, 90.f * F.Rot + (bBuiltPiece ? 180.f : 0.f), 0.f);
			const FVector Off = MeshYaw.RotateVector(FVector(MB.Origin.X, MB.Origin.Y, 0.f) * S);
			// Feet on the floor: an underground tile's top is Level x FloorHeight;
			// a habitat bay's floor tile tops out at its actor + 5 cm
			// (RefreshHabShells: Base = actor + 3, a 2 cm tile centred at Base + 1).
			const float FloorZ = F.Key.Z == 0 ? (float)(F.Key.X * Sim->GetFloorHeightCm()) : (float)(Owner->GetActorLocation().Z + 5.f);
			const float Lift = MB.BoxExtent.Z < 3.f ? 0.6f : 0.f; // a rug sits just proud of the deck
			// Seats pull up to what they face (a chair to its table): slide the
			// piece forward to the edge of its footprint. Presentation only.
			FVector Pull = FVector::ZeroVector;
			const FRHFurnitureRow* FRow = Defs ? Defs->GetFurniture(F.Row) : nullptr;
			if (FRow && FRow->Category == FName("Seating"))
			{
				const FVector Facing = Yaw.RotateVector(FVector(0.f, -1.f, 0.f));
				const double Reach = ((F.Rot & 1) ? Half.X : Half.Y) - MB.BoxExtent.Y * S - 10.0;
				Pull = Facing * (float)FMath::Max(0.0, Reach);
			}
			Comp->SetWorldScale3D(FVector(S));
			Comp->SetWorldLocationAndRotation(FVector(Centre.X - Off.X + Pull.X, Centre.Y - Off.Y + Pull.Y, FloorZ + Lift - (MB.Origin.Z - MB.BoxExtent.Z) * S), MeshYaw);
			Vis.Comp = Comp;
			Vis.Sig = Sig;
			Vis.FrameCm = FVector(Centre.X + Pull.X, Centre.Y + Pull.Y, FloorZ);
			Vis.YawDeg = 90.f * F.Rot;
			Vis.HalfCm = FVector2D(MB.BoxExtent.X * S, MB.BoxExtent.Y * S);
		}
		FurnitureCompsByCell.FindOrAdd(F.Key).Add(Comp);
	}
	for (auto It = FurnitureVis.CreateIterator(); It; ++It)
	{
		if (!Live.Contains(It->Key))
		{
			if (UStaticMeshComponent* Gone = It->Value.Comp.Get())
			{
				Gone->DestroyComponent(); // sold
			}
			++PropsVersion;
			It.RemoveCurrent();
		}
	}
	// A furnished cell drops its automatic room-prop; selling it all back
	// brings the prop back.
	for (const auto& Pair : RoomPropByCell)
	{
		UStaticMeshComponent* Prop = Pair.Value.Get();
		if (!Prop)
		{
			continue;
		}
		const bool bFurnished = Sim->GetCellFurniture(Pair.Key) > 0;
		if (bFurnished && Prop->IsVisible())
		{
			Prop->SetVisibility(false);
			PropsHiddenForFurniture.Add(Pair.Key);
			++PropsVersion;
		}
		else if (!bFurnished && PropsHiddenForFurniture.Contains(Pair.Key))
		{
			Prop->SetVisibility(true);
			PropsHiddenForFurniture.Remove(Pair.Key);
			++PropsVersion;
		}
	}
}

void URHColonyVisualizerSubsystem::HandleBuildingRemoved(const FRHBuildingInstance& Instance)
{
	MarkPassesDirty();
	++PropsVersion;
	// The actor owns everything drawn for it (shell walls, floor tiles, room
	// props and lamps, dome glass and crops, its light); neighbouring shells
	// redraw on their own next Tick because their wall keys change.
	if (AStaticMeshActor* Actor = BuildingVisuals.FindRef(Instance.Id).Get())
	{
		Actor->Destroy();
	}
	BuildingVisuals.Remove(Instance.Id);
	AppliedPowerState.Remove(Instance.Id);
	AuthoredPulseDepth.Remove(Instance.Id);
	BuildingLights.Remove(Instance.Id);
	HabShellVis.Remove(Instance.Id);
	AppliedDomeCrops.Remove(Instance.Id);
	DomeCropProps.Remove(Instance.Id);
	for (auto It = RoomPropByCell.CreateIterator(); It; ++It)
	{
		if (It->Key.X == 0 && It->Key.Z == Instance.Id)
		{
			It.RemoveCurrent(); // the crew's furniture list reads these (bay props)
		}
	}
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
		if (Def->DragSized || Def->Pressurized)
		{
			// (BM-1b: a fixed prefab shell too - a thin deck, never a block.)
			// The greenhouse dome's deck under all its beds (built separately
			// by BuildGreenhouseDome); a habitat's deck is a thin floor so the
			// crew's feet sit on it (BM-2: RefreshHabShells draws the rest).
			Scale.X *= FMath::Max(1, Instance.SizeX);
			Scale.Y *= FMath::Max(1, Instance.SizeY);
			Scale.Z = Def->Pressurized ? 0.06f : 0.3f;
		}
		else if (N == FName("SolarArray") || N == FName("ChargePad") || N == FName("Stockpile"))
		{
			Scale.Z = 0.25f; // flat decks
		}
		else if (N == FName("Pylon"))
		{
			Scale = FVector(0.6f, 0.6f, 6.f); // tall mast
		}
		else if (N == FName("Lander"))
		{
			// Reference (Ships/CargoLander): a blocky horizontal freighter - the
			// hull is LONG (nose-to-stern on X), narrower abeam, low-slung. The
			// splayed legs span the rest of the sim footprint.
			Scale.X *= 0.78f;
			Scale.Y *= 0.5f;
			Scale.Z = 1.5f;
		}
		else if (N == FName("Electrolyzer"))
		{
			// Slim process hall; the twin gas tanks alongside carry the mass.
			Scale.Y *= 0.55f;
			Scale.Z = 1.8f;
		}
		else if (N == FName("WaterPlant"))
		{
			// The tank farm is the building; the process block plays support.
			Scale.X *= 0.75f;
			Scale.Y *= 0.75f;
			Scale.Z = 2.4f;
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
	MarkPassesDirty();
	UWorld* World = GetWorld();
	if (!World || BuildingVisuals.Contains(Instance.Id))
	{
		return;
	}

	const FVector Scale = ScaleFor(Instance);
	// The Lander rides up on its legs so its descent engine + gear are visible.
	// (SolarArray used to spawn as a single tilted slab here; it now builds as a
	// hub + four radiating panel clusters in BuildSilhouette, matching the
	// reference, so it takes the normal flat-deck path.)
	const bool bLander = Instance.DefName == FName("Lander") && !Instance.bUnderConstruction;
	const float LiftZ = bLander ? GLanderLiftCm + Scale.Z * 50.f : Scale.Z * 50.f;
	// Seated on the correct plane: an UNDERGROUND building sits on its floor
	// (Level * FloorHeight), NOT on the surface terrain - the sim's LocationCm.Z
	// is unreliable (Debug_PlaceInstant passes 0), so the floor Z is derived
	// from the building's Level. Surface buildings ride the scenery relief.
	const URHSimWorldSubsystem* SeatSim = World->GetSubsystem<URHSimWorldSubsystem>();
	const double FloorZ = (Instance.Level < 0 && SeatSim)
		? Instance.Level * SeatSim->GetFloorHeightCm()
		: RHMarsTerrain::GroundZCm(Instance.LocationCm.X, Instance.LocationCm.Y);
	const FVector Seated(Instance.LocationCm.X, Instance.LocationCm.Y, FloorZ);
	const FVector Location = Seated + FVector(0, 0, LiftZ);
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(Location, FRotator::ZeroRotator);
	if (!Actor)
	{
		return;
	}
	UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	// A real imported model (director's image-to-3D pipeline -> mesh -> import)
	// replaces the whole composed-primitive treatment when one exists for this
	// building type: uniform-scaled to the footprint, grounded on its own
	// bounds. Add a row here per new model imported under
	// /Game/RedHope/Art/Models. Two material lineages coexist:
	//   - The Forge is an early vertex-color mesh (COLOR_0, no textures) and
	//     needs M_VertexColor applied below or it renders gray.
	//   - Everything from the textured pipeline (Hunyuan paint stage -> baseColor
	//     texture -> MI_<name> assigned on the StaticMesh asset) already carries
	//     its own material; leave the slots alone so the texture shows.
	UStaticMesh* RealModel = nullptr;
	bool bVertexColored = false;
	// BM-1b: a pressurized shell (the prefab Habitat too) is drawn as its
	// walls and rooms by RefreshHabShells - no model over the top of them.
	const URHDefinitionsSubsystem* ShellDefs = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FRHBuildingRow* ShellDef = ShellDefs ? ShellDefs->GetBuilding(Instance.DefName) : nullptr;
	if (!Instance.bUnderConstruction && !(ShellDef && ShellDef->Pressurized))
	{
		static const TMap<FName, FString> RealModelPaths = {
			{ FName("Forge"),       FString(TEXT("/Game/RedHope/Art/Models/forge/StaticMeshes/forge.forge")) },
			{ FName("BatteryBank"), FString(TEXT("/Game/RedHope/Art/Models/battery/battery.battery")) },
			// The IceProcessor art is a tanks-and-pipes processing plant, so it
			// renders the WaterPlant; IceDrill stays a primitive until drill art exists.
			{ FName("WaterPlant"),  FString(TEXT("/Game/RedHope/Art/Models/ice/ice.ice")) },
			// Plinth-free regenerations. The originals baked their ground IN (an
			// excavation pit, a hangar bay, a floating regolith island), which
			// meshed into a base that never seated on terrain - so the objects
			// were re-generated standalone (InstantStyle) and re-meshed; solar's
			// residual shadow slab was additionally cut in mesh-cleanup. The
			// superseded lander/ and solar/ meshes are left on disk but no longer
			// referenced. See docs/build-log.md.
			{ FName("Lander"),      FString(TEXT("/Game/RedHope/Art/Models/lander2/lander2.lander2")) },
			{ FName("SolarArray"),  FString(TEXT("/Game/RedHope/Art/Models/solar2/solar2.solar2")) },
			{ FName("Habitat"),     FString(TEXT("/Game/RedHope/Art/Models/habitat/habitat.habitat")) },
			{ FName("Stockpile"),   FString(TEXT("/Game/RedHope/Art/Models/stockpile/stockpile.stockpile")) },
			// The OreExtractor art (tracked excavator + digging arm) renders the
			// Borer - the sim's shaft/floor digging machine.
			{ FName("Borer"),       FString(TEXT("/Game/RedHope/Art/Models/extractor2/extractor2.extractor2")) },
			// Gemini design pass (2026-07-09): the life-support unit the
			// director asked to re-imagine - compact HVAC body, big intake fan.
			{ FName("AirFilter"),   FString(TEXT("/Game/RedHope/Art/Machines/RH_AirFilter2/StaticMeshes/RH_AirFilter2.RH_AirFilter2")) },
			// Agri Gate B: the climate machinery (generated with the agri batch).
			{ FName("HumidityRegulator"), FString(TEXT("/Game/RedHope/Art/Agri/humidity/humidity/StaticMeshes/humidity.humidity")) },
		};
		// The MIXED SET (premium-asset-plan section 6, resolved from the
		// 2026-08-14 side-by-side renders): keep the mesh that READS, and let
		// the material family carry the coherence instead of the mesh.
		//
		// IN - the painted 2026-07-17 mesh wins outright:
		//   Forge         the original is the vertex-colour mesh and reads as a
		//                 dark broken slab; the replacement is a real machine
		//   Habitat       geodesic dome with legible portholes
		//   ComputeModule NEW coverage - it drew composed primitives before
		//   SolarArray    a toss-up in the renders; kept here so it can be
		//                 judged in-boot against the original
		//
		// OUT - the ORIGINAL mesh reads better and stays; each loses function
		// legibility in the new batch, which matters more than fidelity here:
		//   BatteryBank   original's display panels say "power" at a glance;
		//                 the replacement is an anonymous crate
		//   Borer         original keeps its unmistakable digging arm
		//   WaterPlant    original's tanks-and-pipes says "processing plant"
		//   Lander        original's splayed descent stage is instantly a lander
		//
		// Held back entirely: ModularBlock (open face) and HeavyFreighter
		// (proportions) failed batch QA and want silhouette surgery first;
		// AirlockModule, GreenhouseDome and ScoutSpeeder are imported but have
		// no building DefName to attach to yet (the dome is agri Gate C's).
		static const TMap<FName, FString> ModelPathsV2 = {
			{ FName("Forge"),         FString(TEXT("/Game/RedHope/Art/Models2/HeavyForge/HeavyForge/StaticMeshes/HeavyForge.HeavyForge")) },
			{ FName("SolarArray"),    FString(TEXT("/Game/RedHope/Art/Models2/SolarPanel/SolarPanel/StaticMeshes/SolarPanel.SolarPanel")) },
			{ FName("Habitat"),       FString(TEXT("/Game/RedHope/Art/Models2/HabitatDome/HabitatDome/StaticMeshes/HabitatDome.HabitatDome")) },
			{ FName("ComputeModule"), FString(TEXT("/Game/RedHope/Art/Models2/CommandModule/CommandModule/StaticMeshes/CommandModule.CommandModule")) },
			// The hero-reference -> TRELLIS.2 (kept hi-poly) -> Blender-baked
			// real normal lane (2026-08-18). All three drew composed
			// primitives before - NEW coverage, so no RealModelPaths fallback
			// exists and none is wanted. These retire the last of the
			// director's "old geometrical drawings" list; IceDrill is finally
			// distinct art (mast + auger), NOT the WaterPlant's twin.
			{ FName("Electrolyzer"),  FString(TEXT("/Game/RedHope/Art/Models2/Electrolyzer/Electrolyzer/StaticMeshes/Electrolyzer.Electrolyzer")) },
			{ FName("IceDrill"),      FString(TEXT("/Game/RedHope/Art/Models2/IceDrill/IceDrill/StaticMeshes/IceDrill.IceDrill")) },
			{ FName("Pylon"),         FString(TEXT("/Game/RedHope/Art/Models2/Pylon/Pylon/StaticMeshes/Pylon.Pylon")) },
			// Code-built 2026-10-17 (scripts/blender/rh_build_chargepad.py, the
			// director's "Pad C": an induction disc, amber coil rings, a service
			// mast). It drew primitives before - NEW coverage, no fallback.
			{ FName("ChargePad"),     FString(TEXT("/Game/RedHope/Art/Models2/ChargePad/ChargePad/StaticMeshes/ChargePad.ChargePad")) },
			// Re-bakes of the two worst paint offenders from the director's
			// 2026-08-18 photo notes. The old extractor2/RH_AirFilter2 assets
			// stay on disk under RealModelPaths as the rh.ModelSetV2=0
			// fallback. Borer keeps its digging-arm identity by design brief.
			{ FName("Borer"),         FString(TEXT("/Game/RedHope/Art/Models2/Borer/Borer/StaticMeshes/Borer.Borer")) },
			{ FName("AirFilter"),     FString(TEXT("/Game/RedHope/Art/Models2/AirFilter/AirFilter/StaticMeshes/AirFilter.AirFilter")) },
			// Batch 4 (2026-08-18): the four buildings still wearing the
			// oldest Hunyuan albedos - the remainder of the "TV static"
			// family. The hero references preserved the mixed-set identity
			// verdicts that kept these on their originals: BatteryBank keeps
			// say-power charge bars, WaterPlant keeps tanks-and-pipes, Lander
			// keeps the splayed descent stage; Stockpile is one strapped
			// block per the solid-silhouette rule. Originals remain in
			// RealModelPaths as the rh.ModelSetV2=0 set.
			{ FName("WaterPlant"),    FString(TEXT("/Game/RedHope/Art/Models2/WaterPlant/WaterPlant/StaticMeshes/WaterPlant.WaterPlant")) },
			{ FName("BatteryBank"),   FString(TEXT("/Game/RedHope/Art/Models2/BatteryBank/BatteryBank/StaticMeshes/BatteryBank.BatteryBank")) },
			{ FName("Lander"),        FString(TEXT("/Game/RedHope/Art/Models2/Lander/Lander/StaticMeshes/Lander.Lander")) },
			{ FName("Stockpile"),     FString(TEXT("/Game/RedHope/Art/Models2/Stockpile/Stockpile/StaticMeshes/Stockpile.Stockpile")) },
		};
		// Meshes whose color lives in vertex colors, not a texture — these (and
		// only these) get the M_VertexColor override after the mesh is set.
		static const TSet<FName> VertexColoredModels = { FName("Forge") };
		bool bUsingV2 = CVarModelSetV2.GetValueOnGameThread() != 0;
		if (const FString* V2Path = bUsingV2 ? ModelPathsV2.Find(Instance.DefName) : nullptr)
		{
			RealModel = LoadObject<UStaticMesh>(nullptr, **V2Path);
			if (!RealModel)
			{
				// The V2 asset is missing (partial import): fall through to the
				// original rather than regress the building to primitives.
				UE_LOG(LogRedHope, Warning, TEXT("ModelSetV2: %s missing for %s - using the original model"),
					**V2Path, *Instance.DefName.ToString());
				bUsingV2 = false;
			}
		}
		else
		{
			bUsingV2 = false;
		}
		if (!RealModel)
		{
			if (const FString* Original = RealModelPaths.Find(Instance.DefName))
			{
				RealModel = LoadObject<UStaticMesh>(nullptr, **Original);
			}
		}
		// Only the ORIGINAL Forge carries its color in vertex data; its V2
		// replacement is textured and must keep the material it shipped with.
		bVertexColored = !bUsingV2 && VertexColoredModels.Contains(Instance.DefName);
	}
	if (RealModel)
	{
		Mesh->SetStaticMesh(RealModel);
		const FBoxSphereBounds MB = RealModel->GetBounds();
		// Min-side fit: the model must stay INSIDE the sim's reserved footprint
		// on both axes (max-side fit spilled a square mesh 1 m into the cells
		// flanking a rectangular footprint - review finding).
		// ScaleFor's per-type squeezes shape the PRIMITIVE stand-ins
		// (Electrolyzer's slim hall, WaterPlant's tank farm). Applied to the
		// code-built models (2026-10-14) they left both sitting small inside
		// their reserved cells, so those two fit the raw footprint. The Lander
		// keeps its squeeze: its legs are part of the model and the director
		// signed off its size.
		FVector Fit = Scale;
		if (Instance.DefName == FName("Electrolyzer") || Instance.DefName == FName("WaterPlant") || Instance.DefName == FName("ChargePad"))
		{
			const URHDefinitionsSubsystem* FitDefs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
			if (const FRHBuildingRow* FitDef = FitDefs ? FitDefs->GetBuilding(Instance.DefName) : nullptr)
			{
				Fit.X = FMath::Max(1, FitDef->FootprintX) * 2.f;
				Fit.Y = FMath::Max(1, FitDef->FootprintY) * 2.f;
			}
		}
		const float TargetCm = FMath::Min(Fit.X, Fit.Y) * 100.f;
		const float S = TargetCm / FMath::Max(2.f * FMath::Max(MB.BoxExtent.X, MB.BoxExtent.Y), 1.f);
		Actor->SetActorScale3D(FVector(S));
		// Recenter horizontally, sit the bounds bottom on the ground.
		Actor->SetActorLocation(Seated - FVector(MB.Origin.X, MB.Origin.Y, MB.Origin.Z - MB.BoxExtent.Z) * S);
		// Only the vertex-color lineage (the Forge) needs its material forced;
		// textured models keep the MI_<name> already on their slots.
		if (bVertexColored)
		{
			if (UMaterialInterface* VCMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_VertexColor.M_VertexColor")))
			{
				for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
				{
					Mesh->SetMaterial(Slot, UMaterialInstanceDynamic::Create(VCMat, Mesh));
				}
			}
		}
		UE_LOG(LogRedHope, Display, TEXT("%s renders as imported model '%s' (uniform scale %.2f, %s, mat0 %s)"),
			*Instance.DefName.ToString(), *RealModel->GetName(), S,
			bVertexColored ? TEXT("vertex-color") : TEXT("textured"),
			Mesh->GetMaterial(0) ? *Mesh->GetMaterial(0)->GetPathName() : TEXT("<none>"));
	}
	else
	{
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Actor->SetActorScale3D(Scale);
	}
#if WITH_EDITOR
	Actor->SetActorLabel(FString::Printf(TEXT("Sim_%s_%d"), *Instance.DefName.ToString(), Instance.Id));
#endif
	// Canon body color (construction sites sit dim until finished) + a ground
	// label in the function hue so "which block is the Forge" never needs asking.
	const FLinearColor Tint = TintFor(Instance.DefName);
	const FLinearColor Body = BodyFor(Instance.DefName);
	if (!RealModel)
	{
		// The imported model keeps its vertex-color material; tinting would
		// clobber it back to gray-box.
		ApplyTint(Actor, Instance.bUnderConstruction ? Body * 0.3f : Body, FLinearColor::Black);
	}
	AddLabel(Actor, Instance.DefName.ToString(), Tint, Scale.X * 50.f + 260.f);
	const URHDefinitionsSubsystem* DomeDefs = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FRHBuildingRow* DomeDef = DomeDefs ? DomeDefs->GetBuilding(Instance.DefName) : nullptr;
	if (DomeDef && DomeDef->Pressurized)
	{
		// BM-2: the shell's interior is drawn (and redrawn as rooms and
		// neighbours change) by RefreshHabShells; a fresh actor redraws.
		ApplyTint(Actor, Instance.bUnderConstruction ? FLinearColor(0.08f, 0.08f, 0.09f) : FLinearColor(0.34f, 0.33f, 0.31f), FLinearColor::Black);
		HabShellVis.Remove(Instance.Id);
	}
	else if (DomeDef && DomeDef->DragSized)
	{
		ApplyTint(Actor, Instance.bUnderConstruction ? FLinearColor(0.08f, 0.08f, 0.09f) : FLinearColor(0.30f, 0.29f, 0.28f), FLinearColor::Black);
		BuildGreenhouseDome(Actor, Instance, Seated);
		AppliedDomeCrops.Remove(Instance.Id); // a rebuilt shell redraws its crops
		DomeCropProps.Remove(Instance.Id);
	}
	else if (!Instance.bUnderConstruction && !RealModel)
	{
		BuildSilhouette(Actor, Instance.DefName, Seated, Scale);
	}
	else if (Instance.bUnderConstruction) // NOT plain else: a completed real
	{                                     // model must not wear site dressing
		// Construction site: hazard corner posts + a lit work lamp on a mast.
		// A dim foundation slab alone reads as a finished flat deck from the
		// strategic camera - and disappears entirely at night while the robots
		// are still working it.
		const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
		const TCHAR* Sphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
		const float Hx = Scale.X * 50.f;
		const float Hy = Scale.Y * 50.f;
		for (int32 C = 0; C < 4; ++C)
		{
			const FVector Corner((C & 1) ? Hx : -Hx, (C & 2) ? Hy : -Hy, 0.f);
			AddAccent(Actor, Cube, Seated + Corner + FVector(0, 0, 85.f), FRotator::ZeroRotator,
				FVector(0.14f, 0.14f, 1.7f), RHCanon::HazYellow, FLinearColor(0.4f, 0.28f, 0.02f));
		}
		AddAccent(Actor, Cube, Seated + FVector(0, 0, 105.f), FRotator::ZeroRotator,
			FVector(0.09f, 0.09f, 2.1f), RHCanon::DarkSlate);
		AddAccent(Actor, Sphere, Seated + FVector(0, 0, 225.f), FRotator::ZeroRotator,
			FVector(0.28f), RHCanon::HazYellow, FLinearColor(3.2f, 2.2f, 0.35f));
	}
	// Born on whatever floor the sim says; visible only if the elevator is
	// looking at that stratum (M1-d slice view).
	Actor->SetActorHiddenInGame(Instance.Level != ViewLevel);
	// The Floodmast exists only to light the place. It carries a real point
	// light rather than an emissive fake, so it actually throws light onto the
	// regolith and the hulls around it - which is the entire point of building
	// one. Intensity rides bPowered through the power pass below.
	// EVERY powered building gets a small lamp tinted to its own authored accent,
	// not just the Floodmast. Reason, from the 2026-08-17 fidelity pass: at the
	// distances this camera actually occupies (29 m closest, 216 m at the default
	// opening) surface detail is measurably invisible - three controlled A/Bs put
	// the whole normal-map change at 0.08-0.29% of pixels - while a BRIGHT POINT
	// still reads at any distance, because it is contrast rather than detail. A
	// night colony of small coloured pools is therefore the cheapest real gain in
	// how the place looks, and it costs nothing per asset.
	//
	// It also sidesteps the wall the emissive masks kept hitting: authored light
	// placement has NO UV dependency, so unlike a cut mask it survives every
	// future re-bake. Three separate attempts to key lit panels out of the
	// refreshed albedos failed because the paint simply does not contain them
	// (see rh_cut_masks.py's rejected readout_key for the evidence).
	//
	// Deliberately modest: 26 cd against the Floodmast's 90, a 12 m radius
	// against its 26, shadowless like every other lamp here. This is a lit
	// window, not a floodlight - the mast stays the thing that lights the yard.
	// Under-construction shells stay dark, which keeps "finished" legible at
	// night, and the whole set rides bPowered through the existing power pass
	// below, so a brownout still darkens the colony.
	const bool bWantsLamp = !Instance.bUnderConstruction
		&& Instance.DefName != FName("Pylon");       // a bare mast has no interior to light
	if (bWantsLamp && Instance.DefName != FName("Floodmast"))
	{
		UPointLightComponent* Win = NewObject<UPointLightComponent>(Actor);
		Win->SetupAttachment(Actor->GetRootComponent());
		Win->SetMobility(EComponentMobility::Movable);
		Win->SetAbsolute(true, true, true);
		Win->SetIntensityUnits(ELightUnits::Candelas);
		Win->SetIntensity(21.f);
		// The building's own accent hue, pulled 60% toward warm white so a colony
		// at night reads as inhabited rather than as a row of coloured bulbs -
		// the accent identifies the machine, the warmth says someone lives here.
		const FLinearColor Accent = TintFor(Instance.DefName);
		const FLinearColor Warm(1.00f, 0.88f, 0.72f);
		const FLinearColor Mix = Accent * 0.4f + Warm * 0.6f;
		Win->SetLightColor(Mix.ToFColor(false));
		Win->SetAttenuationRadius(1200.f);
		Win->SetCastShadows(false);
		Win->RegisterComponent();
		// DOORWAY height, deliberately - tuned from a night frame. At 150 cm the
		// lamp cleared the roof line of the SHORT buildings (IceDrill,
		// WaterPlant) and blew their roofs into flat white patches, because an
		// opaque hull does not transmit light: what you saw was the lamp
		// spilling over the top, not a lit interior. At 55 cm it sits below
		// every roof and reads as light spilling out at ground level, which is
		// also the read that carries at 216 m - a pool on the regolith.
		Win->SetWorldLocation(Seated + FVector(0, 0, 55.f));
		BuildingLights.Add(Instance.Id, Win);
	}
	if (!Instance.bUnderConstruction && Instance.DefName == FName("Floodmast"))
	{
		UPointLightComponent* Lamp = NewObject<UPointLightComponent>(Actor);
		Lamp->SetupAttachment(Actor->GetRootComponent());
		Lamp->SetMobility(EComponentMobility::Movable);
		Lamp->SetAbsolute(true, true, true);
		// Candelas, like the vault fill and the hab ceiling lights. The default
		// is Unitless, which routes through a legacy x16 path and would make a
		// value tuned in any real unit come out wrong by orders of magnitude.
		Lamp->SetIntensityUnits(ELightUnits::Candelas);
		Lamp->SetIntensity(90.f); // ~4x the 22 cd vault fill: a work light, not daylight
		Lamp->SetLightColor(FColor(255, 219, 168)); // warm work light
		Lamp->SetAttenuationRadius(2600.f);
		// Shadowless on purpose: a colony of these would otherwise cost a
		// shadow pass each, and the read we want is pooled light on the ground.
		Lamp->SetCastShadows(false);
		Lamp->RegisterComponent();
		Lamp->SetWorldLocation(Seated + FVector(0, 0, 620.f));
		BuildingLights.Add(Instance.Id, Lamp);
	}

	BuildingVisuals.Add(Instance.Id, Actor);
	// A fresh visual has no material state pushed into it yet; forget any answer
	// remembered for this Id so the next power pass re-applies from scratch.
	AppliedPowerState.Remove(Instance.Id);
	// Same reason, and it matters more than it looks: an under-construction
	// building wears M_Graybox, which has no PulseDepth at all, so a pulse read
	// taken then caches 0. Completion rebuilds the visual under this same Id
	// with the real material, and without this the building's throb would stay
	// pinned off for the rest of the session.
	AuthoredPulseDepth.Remove(Instance.Id);
}

void URHColonyVisualizerSubsystem::HandleBuildingCompleted(const FRHBuildingInstance& Instance)
{
	MarkPassesDirty();
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
	UE_LOG(LogRedHope, Warning, TEXT("ORDER REJECTED: %s %s - %s"),
		*Cmd.Verb.ToString(), *Cmd.Target.ToString(), *Reason);
	LastNotice = FString::Printf(TEXT("REJECTED %s %s: %s"),
		*Cmd.Verb.ToString(), *Cmd.Target.ToString(), *Reason);
	LastNoticeRealSeconds = FPlatformTime::Seconds();
}

FText URHColonyVisualizerSubsystem::GetNoticeText() const
{
	// Real seconds, not sim seconds: the notice must survive a pause and not
	// flash past at 8x.
	constexpr double NoticeHoldSeconds = 12.0;
	if (FPlatformTime::Seconds() - LastNoticeRealSeconds > NoticeHoldSeconds)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(LastNotice);
}

void URHColonyVisualizerSubsystem::HandleAlert(const FString& Alert)
{
	LastAlert = Alert;
	LastAlertRealSeconds = FPlatformTime::Seconds();
}

FText URHColonyVisualizerSubsystem::GetAlertText() const
{
	// Longer hold than the notice line: these are the batten-down moments.
	constexpr double AlertHoldSeconds = 15.0;
	if (FPlatformTime::Seconds() - LastAlertRealSeconds > AlertHoldSeconds)
	{
		return FText::GetEmpty();
	}
	return FText::FromString(LastAlert);
}

void URHColonyVisualizerSubsystem::HandleQuotaMet(int32 Sol, double AwardKg)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 12.f, FColor::Green,
			FString::Printf(TEXT("CEO TRANSMISSION: Quota met (Sol %d). Ship authorized: %.0f kg. RH.Manifest / RH.Launch"), Sol, AwardKg));
	}
}

namespace
{
	// The supply ship's landing site (director's pick 2026-10-17, "Ship C"):
	// the east pad, where the gray-box freighter used to stand.
	const FVector ShipPadCm(4000.f, -4000.f, 0.f);
	constexpr float ShipDropCm = 6000.f;        // where the descent starts / the climb ends
	constexpr double ShipBurnRealSeconds = 8.0; // descent and lift-off, real time at any speed
	constexpr double ShipStaySols = 1.0;        // on the pad, then it leaves
	const TCHAR* SupplyShipPath = TEXT("/Game/RedHope/Art/Models2/SupplyShip/SupplyShip/StaticMeshes/SupplyShip.SupplyShip");
	const TCHAR* LandingPadPath = TEXT("/Game/RedHope/Art/Models2/LandingPad/LandingPad/StaticMeshes/LandingPad.LandingPad");
}

void URHColonyVisualizerSubsystem::HandleShipArrived(const TArray<FName>& Items)
{
	// The landing itself is UpdateSupplyShip's: the ship comes down ahead of the
	// sim's arrival time, sits a sol, and lifts off. The arrival only lands a
	// ship that was not already on its way down (a speed jump past the descent,
	// or a load just before the touchdown).
	if (ShipBeat == 0)
	{
		const UWorld* World = GetWorld();
		const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
		SpawnSupplyShip(true);
		ShipLandedSimSeconds = Clock ? Clock->GetSimSecondsTotal() : 0.0;
	}
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Cyan,
			FString::Printf(TEXT("SUPPLY SHIP LANDED: %d items transferred. The Program continues."), Items.Num()));
	}
}

void URHColonyVisualizerSubsystem::EnsureLandingPad()
{
	UWorld* World = GetWorld();
	if (!World || LandingPadVisual)
	{
		return;
	}
	UStaticMesh* PadMesh = LoadObject<UStaticMesh>(nullptr, LandingPadPath);
	if (!PadMesh)
	{
		return; // no art, no pad: the ship still lands on the ground
	}
	AStaticMeshActor* Pad = World->SpawnActor<AStaticMeshActor>(ShipPadCm, FRotator::ZeroRotator);
	if (!Pad)
	{
		return;
	}
	Pad->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Pad->GetStaticMeshComponent()->SetStaticMesh(PadMesh);
	Pad->GetStaticMeshComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
#if WITH_EDITOR
	Pad->SetActorLabel(TEXT("Sim_LandingPad"));
#endif
	// The edge lights glow softly all the time.
	if (UMaterialInstanceDynamic* Mid = Pad->GetStaticMeshComponent()->CreateAndSetMaterialInstanceDynamic(0))
	{
		Mid->SetScalarParameterValue(FName("PoweredState"), 1.f);
	}
	Pad->SetActorHiddenInGame(IsUnderground());
	LandingPadVisual = Pad;
}

float URHColonyVisualizerSubsystem::ShipPadTopCm() const
{
	if (LandingPadVisual)
	{
		return (float)LandingPadVisual->GetComponentsBoundingBox(true).Max.Z;
	}
	return 0.f;
}

void URHColonyVisualizerSubsystem::SpawnSupplyShip(bool bLanded)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (ShipVisual)
	{
		ShipVisual->Destroy();
		ShipVisual = nullptr;
	}
	EnsureLandingPad();
	const FVector Rest = ShipPadCm + FVector(0, 0, ShipPadTopCm());
	AStaticMeshActor* Ship = World->SpawnActor<AStaticMeshActor>(Rest + FVector(0, 0, bLanded ? 0.f : ShipDropCm), FRotator::ZeroRotator);
	if (!Ship)
	{
		return;
	}
	UStaticMeshComponent* Mesh = Ship->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (UStaticMesh* ShipMesh = LoadObject<UStaticMesh>(nullptr, SupplyShipPath))
	{
		// Code-built at true scale (scripts/blender/rh_build_supplyship.py),
		// pivot at the feet: no fitting.
		Mesh->SetStaticMesh(ShipMesh);
		if (UMaterialInstanceDynamic* Mid = Mesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			Mid->SetScalarParameterValue(FName("PoweredState"), 1.f);
			Mid->SetScalarParameterValue(FName("EmissiveAmount"), 0.f);
			ShipMid = Mid;
		}
	}
	else
	{
		// Art missing: a plain deck-sized block, so the beat still plays.
		Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Mesh->SetRelativeScale3D(FVector(8.8f, 8.8f, 2.f));
		ApplyTint(Ship, RHCanon::BoneWhite);
	}
#if WITH_EDITOR
	Ship->SetActorLabel(TEXT("Sim_SupplyShip"));
#endif
	// The burn: a warm light under the deck that rides down with the ship.
	UPointLightComponent* Burn = NewObject<UPointLightComponent>(Ship);
	Burn->SetupAttachment(Ship->GetRootComponent());
	Burn->SetMobility(EComponentMobility::Movable);
	Burn->SetIntensityUnits(ELightUnits::Candelas);
	Burn->SetIntensity(0.f);
	Burn->SetLightColor(FColor(255, 168, 82));
	Burn->SetAttenuationRadius(2600.f);
	Burn->SetCastShadows(false);
	Burn->RegisterComponent();
	Burn->SetRelativeLocation(FVector(0, 0, 40.f));
	ShipBurnLight = Burn;
	Ship->SetActorHiddenInGame(IsUnderground());
	ShipVisual = Ship;
	ShipBeat = bLanded ? 2 : 1;
	UE_LOG(LogRedHope, Display, TEXT("[Ship] %s (%s)"), bLanded ? TEXT("on the pad") : TEXT("descending"),
		LoadObject<UStaticMesh>(nullptr, SupplyShipPath) ? TEXT("SupplyShip model") : TEXT("placeholder block"));
	ShipBeatStartReal = FPlatformTime::Seconds();
	if (bLanded)
	{
		AddLabel(Ship, TEXT("Supply Ship"), RHCanon::BoneWhite, 700.f);
	}
}

void URHColonyVisualizerSubsystem::SetShipBurn(float Amount)
{
	if (UMaterialInstanceDynamic* Mid = ShipMid.Get())
	{
		Mid->SetScalarParameterValue(FName("EmissiveAmount"), 2.5f * Amount);
	}
	if (UPointLightComponent* Burn = ShipBurnLight.Get())
	{
		Burn->SetIntensity(3500.f * Amount);
	}
}

void URHColonyVisualizerSubsystem::UpdateSupplyShip(const URHSimWorldSubsystem* Sim)
{
	const UWorld* World = GetWorld();
	const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	if (!Sim || !Clock)
	{
		return;
	}
	const double Now = Clock->GetSimSecondsTotal();
	const bool bInbound = Sim->GetQuotaPhase() == ERHQuotaPhase::ShipInbound;
	const double StaySim = ShipStaySols * URHSimClockSubsystem::SolLengthSimSeconds;
	// The landing site is cleared the moment the first ship is on its way.
	if (bInbound && !LandingPadVisual)
	{
		EnsureLandingPad();
	}
	const double RealNow = FPlatformTime::Seconds();
	const float T = (float)FMath::Clamp((RealNow - ShipBeatStartReal) / ShipBurnRealSeconds, 0.0, 1.0);
	const FVector Rest = ShipPadCm + FVector(0, 0, ShipPadTopCm());
	switch (ShipBeat)
	{
	case 0: // away: start down so the touchdown lands on the sim's arrival
	{
		const double Lead = ShipBurnRealSeconds * FMath::Max(Clock->GetSpeed(), 0.0001f);
		if (bInbound && Sim->GetShipEtaSimSeconds() - Now <= Lead)
		{
			SpawnSupplyShip(false);
			ShipEtaLanded = Sim->GetShipEtaSimSeconds();
		}
		break;
	}
	case 1: // descending: fast from altitude, slowing onto the pad, engines lit
		if (ShipVisual)
		{
			ShipVisual->SetActorLocation(Rest + FVector(0, 0, ShipDropCm * FMath::Pow(1.f - T, 2.2f)));
		}
		SetShipBurn(T < 0.92f ? 1.f : (1.f - T) / 0.08f);
		if (T >= 1.f)
		{
			ShipBeat = 2;
			ShipLandedSimSeconds = Now;
			SetShipBurn(0.f);
			UE_LOG(LogRedHope, Display, TEXT("[Ship] touched down (sim ETA %.0f s, now %.0f s)"), Sim->GetShipEtaSimSeconds(), Now);
			if (ShipVisual)
			{
				AddLabel(ShipVisual, TEXT("Supply Ship"), RHCanon::BoneWhite, 700.f);
			}
		}
		break;
	case 2: // on the pad for a sol; leaves early if the NEXT ship is already due
		// (this ship's own arrival can still be pending in the sim when the
		// descent ends ahead of it - it carries the ETA it came down on).
		if (Now - ShipLandedSimSeconds >= StaySim
			|| (bInbound && Sim->GetShipEtaSimSeconds() != ShipEtaLanded && Sim->GetShipEtaSimSeconds() - Now < StaySim))
		{
			ShipBeat = 3;
			ShipBeatStartReal = RealNow;
			UE_LOG(LogRedHope, Display, TEXT("[Ship] lifting off after %.2f sols on the pad"), (Now - ShipLandedSimSeconds) / URHSimClockSubsystem::SolLengthSimSeconds);
			// The label was placed at the pad (absolute); it must not hang in
			// the air once the ship climbs: drop it on lift-off.
			if (ShipVisual)
			{
				TArray<UTextRenderComponent*> Labels;
				ShipVisual->GetComponents(Labels);
				for (UTextRenderComponent* L : Labels)
				{
					L->DestroyComponent();
				}
			}
		}
		break;
	case 3: // lifting off: slow to start, accelerating away
		if (ShipVisual)
		{
			ShipVisual->SetActorLocation(Rest + FVector(0, 0, 1.5f * ShipDropCm * FMath::Pow(T, 2.2f)));
		}
		SetShipBurn(T < 0.1f ? T / 0.1f : 1.f);
		if (T >= 1.f)
		{
			if (ShipVisual)
			{
				ShipVisual->Destroy();
				ShipVisual = nullptr;
			}
			ShipBeat = 0;
			UE_LOG(LogRedHope, Display, TEXT("[Ship] gone; the pad waits for the next one"));
		}
		break;
	default:
		ShipBeat = 0;
		break;
	}
}

void URHColonyVisualizerSubsystem::RestoreSupplyShip(const URHSimWorldSubsystem* Sim)
{
	const UWorld* World = GetWorld();
	const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	// Completed is only a passing state now (the ladder repeats, D1a); a ship has
	// landed once any quota past the first has opened.
	if (!Sim || !Clock || !(Sim->GetQuotaPhase() == ERHQuotaPhase::Completed || Sim->GetQuotaOpenedSol() > 0))
	{
		return;
	}
	EnsureLandingPad();
	// Each landing opens the next quota, so its sol is the last touchdown a save
	// keeps: a ship that landed under a sol ago is still on the pad.
	const double LandedAt = Sim->GetQuotaOpenedSol() * (double)URHSimClockSubsystem::SolLengthSimSeconds;
	if (Sim->GetQuotaPhase() != ERHQuotaPhase::ShipInbound
		&& Clock->GetSimSecondsTotal() - LandedAt < ShipStaySols * URHSimClockSubsystem::SolLengthSimSeconds)
	{
		SpawnSupplyShip(true);
		ShipLandedSimSeconds = LandedAt;
	}
}

void URHColonyVisualizerSubsystem::Tick(float DeltaTime)
{
	RHFrameStats::FScope StatScope(RHFrameStats::Colony);
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
	// The scenery ring + ground scatter (one-time build; state-diffed hide when
	// the elevator descends, matching the surface ground plane's behavior).
	EnsureEnvironmentRig();
	if (EnvironmentRigActor && bEnvironmentHidden != IsUnderground())
	{
		bEnvironmentHidden = IsUnderground();
		EnvironmentRigActor->SetActorHiddenInGame(bEnvironmentHidden);
	}
	// The supply ship's beat: down ahead of the arrival, a sol on the pad, gone.
	UpdateSupplyShip(Sim);
	// Dig sites follow the dig: a cheap state-keyed rebuild, twice a second.
	if (FPlatformTime::Seconds() >= NextDepositRefreshRealSeconds)
	{
		NextDepositRefreshRealSeconds = FPlatformTime::Seconds() + 0.5;
		RefreshDepositMarkers(Sim);
	}
	// Territory made visible: one disc per coverage node, every frame - on
	// the floor the elevator shows.
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		if (B.Level != ViewLevel)
		{
			continue;
		}
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

	// C3: the key-building passes run on change (see MarkPassesDirty), plus a
	// one-second safety poll for time-driven state.
	const double RealNow = FPlatformTime::Seconds();
	if (bPassesDirty || RealNow >= NextPassPollS)
	{
		bPassesDirty = false;
		NextPassPollS = RealNow + 1.0;
		RefreshDomeCrops();
		RefreshHabShells();
		RefreshFurniture(Sim);
		bHoverDirty = true; // the passes may have added labels
	}
	AnimateDoors(DeltaTime);

	// Is it working? A machine that has been shed by the power priority, or
	// switched off by the player, goes dark; a running one keeps its glow. The
	// sim already decides this per building (FRHBuildingInstance::bPowered), so
	// the visual is a pure readout - state-diffed, because writing a material
	// parameter every frame for every building would be waste. Only meshes
	// wearing the M_RH_Master family carry PoweredState; on anything else the
	// parameter simply does not exist and the write is a no-op.
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		// 3 = a powered Charging Pad with a robot on it: same "running" look,
		// plus brighter, breathing rings (see below).
		const bool bPadInUse = B.DefName == FName("ChargePad") && B.bPowered && !B.bUnderConstruction && Sim->GetPadDockedCount(B.Id) > 0;
		const uint8 Want = B.bUnderConstruction ? 2 : (bPadInUse ? 3 : (B.bPowered ? 1 : 0));
		if (const uint8* Applied = AppliedPowerState.Find(B.Id))
		{
			if (*Applied == Want)
			{
				continue;
			}
		}
		TObjectPtr<AStaticMeshActor>* Found = BuildingVisuals.Find(B.Id);
		if (!Found || !*Found)
		{
			continue;
		}
		UStaticMeshComponent* Mesh = (*Found)->GetStaticMeshComponent();
		if (!Mesh)
		{
			continue;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
		if (!Mid)
		{
			Mid = Mesh->CreateAndSetMaterialInstanceDynamic(0);
		}
		// Record the attempt whether or not it produced a MID. A mesh whose
		// slot 0 is empty makes CreateAndSetMaterialInstanceDynamic return null
		// AND log a warning, so leaving the entry unrecorded would retry - and
		// log - every frame forever. HandleBuildingAdded clears the entry when
		// the visual is rebuilt, so a genuine retry still happens then.
		AppliedPowerState.Add(B.Id, Want);
		if (Mid)
		{
			Mid->SetScalarParameterValue(FName("PoweredState"), (Want == 1 || Want == 3) ? 1.f : 0.f);
			if (B.DefName == FName("ChargePad"))
			{
				// The rings idle low and come up, breathing, while a robot charges.
				Mid->SetScalarParameterValue(FName("EmissiveAmount"), Want == 3 ? 0.4f : 0.05f);
				Mid->SetScalarParameterValue(FName("PulseDepth"), Want == 3 ? 0.35f : 0.f);
				Mid->SetScalarParameterValue(FName("PulseSpeed"), 1.2f);
			}
		}
		// A Floodmast on a shed breaker goes dark with everything else.
		// (Named FoundLamp, not Found: this scope already has a `Found` for the
		// building visual, and the build is -Werror,-Wshadow.)
		if (const TWeakObjectPtr<UPointLightComponent>* FoundLamp = BuildingLights.Find(B.Id))
		{
			if (UPointLightComponent* Lamp = FoundLamp->Get())
			{
				Lamp->SetVisibility(Want == 1 || Want == 3);
			}
		}
	}

	// Shaft & carved-floor mirror (M1-d): state-diffed each frame - the counts
	// are tiny and the sim has no per-cell visual events to listen to.
	UpdateShaftVisuals();

	// Interior viewing: cheap unless the camera actually crossed a boundary.
	ApplyCutaway();

	// Hover-gated labels (director 2026-07-10: station and room names show
	// only under the cursor - a working town, not a diagram). One deproject,
	// then the registry's grid answers "what is near the cursor".
	{
		FVector CursorCm(1.0e12, 1.0e12, 0);
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector O, D;
			if (PC->DeprojectMousePositionToWorld(O, D) && FMath::Abs(D.Z) > 1.0e-4)
			{
				const double PlaneZ = ViewLevel * Sim->GetFloorHeightCm();
				const double T = (PlaneZ - O.Z) / D.Z;
				if (T > 0)
				{
					CursorCm = O + D * T;
				}
			}
		}
		UpdateHoverLabels(CursorCm);
	}

	// The elevator rides the open floor, and its doors ease open whenever a
	// colonist stands near the shaft head (director 2026-07-09) - the cage is
	// where crew "board" for the between-floors hard cut, so the doors parting
	// for an approaching figure is what sells the ride.
	if (UStaticMeshComponent* CageComp = ElevatorCage.Get())
	{
		const FVector Head = Sim->GetShaftHeadCm();
		const double FloorH = Sim->GetFloorHeightCm();
		const double FloorZ = ViewLevel * FloorH;
		if (bElevatorIsCab)
		{
			// The cab's pivot IS its floor centre (RHElevatorLayout.h), so it
			// sits by pivot - bounds would shift with the door casing.
			CageComp->SetWorldLocation(FVector(Head.X, Head.Y, FloorZ));
		}
		else if (const UStaticMesh* Cage = CageComp->GetStaticMesh())
		{
			const FBoxSphereBounds CB = Cage->GetBounds();
			const float S = CageComp->GetComponentScale().Z;
			CageComp->SetWorldLocation(FVector(
				Head.X - CB.Origin.X * S,
				Head.Y - CB.Origin.Y * S,
				FloorZ - (CB.Origin.Z - CB.BoxExtent.Z) * S));
		}
		float Target = 0.f;
		if (IsUnderground())
		{
			if (const URHCrewVisualizerSubsystem* Crew = World->GetSubsystem<URHCrewVisualizerSubsystem>())
			{
				// The cab wakes for riders only (the doors would otherwise
				// part for every colonist walking round it).
				const bool bNear = bElevatorIsCab ? Crew->IsAnyRiderWithinCm(FVector(Head.X, Head.Y, FloorZ), 420.f)
					: Crew->IsAnyCrewWithinCm(FVector(Head.X, Head.Y, FloorZ), 420.f);
				// Real doors dwell: held open 2 s after the last rider, so
				// riders stepping in and out of range don't make them flutter.
				const double NowS = World->GetTimeSeconds();
				if (bNear)
				{
					ElevatorHoldUntil = NowS + 2.0;
				}
				Target = NowS < ElevatorHoldUntil ? 1.f : 0.f;
			}
		}
		ElevatorWakeAlpha = FMath::FInterpTo(ElevatorWakeAlpha, Target, DeltaTime, 3.5f);
		// The doors ride the same wake: each leaf slides one leaf-width into
		// its pocket as crew approach and back as they leave. Leaf B is leaf A
		// turned 180 degrees (the leaf mesh is symmetric).
		const float Slide = RHElevator::LeafClosedY + RHElevator::LeafTravel * FMath::Clamp(ElevatorWakeAlpha, 0.f, 1.f);
		if (UStaticMeshComponent* LeafA = ElevatorDoorA.Get())
		{
			LeafA->SetWorldLocationAndRotation(
				FVector(Head.X + RHElevator::DoorPlaneX, Head.Y + Slide, FloorZ + RHElevator::LeafZ), FRotator::ZeroRotator);
		}
		if (UStaticMeshComponent* LeafB = ElevatorDoorB.Get())
		{
			LeafB->SetWorldLocationAndRotation(
				FVector(Head.X + RHElevator::DoorPlaneX, Head.Y - Slide, FloorZ + RHElevator::LeafZ), FRotator(0.f, 180.f, 0.f));
		}
		if (UPointLightComponent* Lamp = ElevatorLamp.Get())
		{
			// The cage wakes for an approaching colonist: 0 cd asleep, 18 cd
			// greeting - under the 22 cd vault fill, so it reads as a cabin
			// light, not a floodlight. Rides the viewed floor with the cage.
			Lamp->SetWorldLocation(FVector(Head.X, Head.Y, FloorZ + 150.f));
			Lamp->SetIntensity(18.f * ElevatorWakeAlpha);
		}
	}
	// Sovereignty mirror (M3/M4): rival markers + the trade rover, same pattern.
	RefreshSovereigntyVisuals();
}

void URHColonyVisualizerSubsystem::RebuildHoverRegistry()
{
	// Every tile's room label (radius 6 m round the tile) and every text
	// component on every building actor (8 m round the actor), bucketed by XY in
	// 10 m cells so a cursor query touches the 3 x 3 cells around it. Floors
	// share XY, exactly as the old scan compared XY only; the floor cut hides the
	// labels of every floor but the viewed one.
	bHoverDirty = false;
	HoverLabels.Reset();
	HoverGrid.Reset();
	HoverLit.Reset();
	const auto Add = [this](UTextRenderComponent* L, const FVector& At, float Radius)
	{
		FHoverLabel E;
		E.Label = L;
		E.Pos = FVector2D(At.X, At.Y);
		E.RadiusCm = Radius;
		const int64 Cx = (int64)FMath::FloorToInt(At.X / 1000.f) & 0xFFFFFF;
		const int64 Cy = (int64)FMath::FloorToInt(At.Y / 1000.f) & 0xFFFFFF;
		HoverGrid.FindOrAdd((Cx << 24) | Cy).Add(HoverLabels.Num());
		HoverLabels.Add(E);
		L->SetVisibility(false); // lit only while the cursor is near
	};
	for (const auto& TP : TileByCell)
	{
		if (AStaticMeshActor* Tile = TP.Value.Get())
		{
			if (UTextRenderComponent* L = Tile->FindComponentByClass<UTextRenderComponent>())
			{
				Add(L, Tile->GetActorLocation(), 600.f);
			}
		}
	}
	for (const auto& BP : BuildingVisuals)
	{
		if (AStaticMeshActor* B = BP.Value)
		{
			TArray<UTextRenderComponent*> Texts;
			B->GetComponents(Texts);
			for (UTextRenderComponent* L : Texts)
			{
				Add(L, B->GetActorLocation(), 800.f);
			}
		}
	}
}

void URHColonyVisualizerSubsystem::UpdateHoverLabels(const FVector& CursorCm)
{
	if (bHoverDirty)
	{
		RebuildHoverRegistry();
	}
	TArray<int32> Lit;
	if (CursorCm.X < 1.0e11)
	{
		const int32 Cx0 = FMath::FloorToInt(CursorCm.X / 1000.f);
		const int32 Cy0 = FMath::FloorToInt(CursorCm.Y / 1000.f);
		for (int32 Dx = -1; Dx <= 1; ++Dx)
		{
			for (int32 Dy = -1; Dy <= 1; ++Dy)
			{
				const int64 Key = (((int64)(Cx0 + Dx) & 0xFFFFFF) << 24) | ((int64)(Cy0 + Dy) & 0xFFFFFF);
				if (const TArray<int32>* Bucket = HoverGrid.Find(Key))
				{
					for (const int32 I : *Bucket)
					{
						const FHoverLabel& E = HoverLabels[I];
						if (FVector2D::Distance(E.Pos, FVector2D(CursorCm.X, CursorCm.Y)) < E.RadiusCm)
						{
							Lit.Add(I);
						}
					}
				}
			}
		}
	}
	for (const int32 I : HoverLit)
	{
		if (!Lit.Contains(I))
		{
			if (UTextRenderComponent* L = HoverLabels[I].Label.Get()) { L->SetVisibility(false); }
		}
	}
	for (const int32 I : Lit)
	{
		if (!HoverLit.Contains(I))
		{
			if (UTextRenderComponent* L = HoverLabels[I].Label.Get()) { L->SetVisibility(true); }
		}
	}
	HoverLit = MoveTemp(Lit);
}

void URHColonyVisualizerSubsystem::UpdateShaftVisuals()
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!Sim)
	{
		return;
	}
	const double FloorH = Sim->GetFloorHeightCm();
	const int32 Depth = Sim->GetShaftDepth();

	// The shaft anchor: an INVISIBLE actor at the trunk (director 2026-07-09b:
	// even slimmed, a floor-to-surface column read as a black void and blocked
	// the floor beneath it). The elevator's visible presence is now entirely
	// the cage + sliding doors + fill light, all parented to this anchor.
	if (Depth > 0 && Depth != LastShaftDepthSeen)
	{
		const FVector Head = Sim->GetShaftHeadCm();
		const FVector Center(Head.X, Head.Y, -Depth * FloorH * 0.5);
		const FVector Scale(2.2f, 2.2f, Depth * FloorH / 100.0);
		if (!ShaftVisual)
		{
			ShaftVisual = SpawnBox(Center, Scale, RHCanon::DarkSlate, FLinearColor::Black);
			if (ShaftVisual && ShaftVisual->GetStaticMeshComponent())
			{
				// Hide only the box mesh; attached children keep their own visibility.
				ShaftVisual->GetStaticMeshComponent()->SetVisibility(false);
			}
#if WITH_EDITOR
			if (ShaftVisual) { ShaftVisual->SetActorLabel(TEXT("Sim_Shaft")); }
#endif
			// Born hidden at SURF (invariant D): a bore that completes while
			// the player is at the surface must not flash its column through
			// the ground before the next view change (mirrors carve tiles).
			if (ShaftVisual) { ShaftVisual->SetActorHiddenInGame(!IsUnderground()); }
			// The underground fill: one big dim cool light riding the viewed
			// floor (repositioned by ApplyViewLevel), so an unpowered vault
			// reads dim-and-cold instead of void-black at night. Owned by the
			// shaft column - dies and rebuilds with it on reload, and inherits
			// its hidden-at-surface state.
			if (ShaftVisual)
			{
				UPointLightComponent* Fill = NewObject<UPointLightComponent>(ShaftVisual, TEXT("FloorFill"));
				Fill->SetupAttachment(ShaftVisual->GetRootComponent());
				Fill->SetMobility(EComponentMobility::Movable);
				Fill->SetAbsolute(true, true, true);
				Fill->SetIntensityUnits(ELightUnits::Candelas);
				Fill->SetIntensity(22.f);
				Fill->SetLightColor(FColor(170, 190, 215)); // cold vault ambience
				Fill->SetAttenuationRadius(4000.f);
				Fill->SetCastShadows(false);
				Fill->RegisterComponent();
				Fill->SetWorldLocation(FVector(Head.X, Head.Y, ViewLevel * FloorH + 350.0));
				FloorFill = Fill;
			}
			// The elevator CAGE at the shaft head (director 2026-07-09): the
			// real generated model, riding the open floor, with two sliding
			// door panels that ease open when a colonist approaches (animated
			// in Tick). Attached to the shaft actor: hidden at SURF, dies and
			// rebuilds with it on reload. Gemini design first, prior cage as
			// fallback; absent both, the slim column carries alone.
			if (ShaftVisual)
			{
				// The code-built cab (2026-10-14) first: true scale, real doors.
				UStaticMesh* Cage = LoadObject<UStaticMesh>(nullptr,
					TEXT("/Game/RedHope/Art/Machines/RH_ElevatorCab/StaticMeshes/RH_ElevatorCab.RH_ElevatorCab"));
				bElevatorIsCab = Cage != nullptr;
				if (!Cage)
				{
					Cage = LoadObject<UStaticMesh>(nullptr,
						TEXT("/Game/RedHope/Art/Machines/RH_Elevator2/StaticMeshes/RH_Elevator2.RH_Elevator2"));
				}
				if (!Cage)
				{
					Cage = LoadObject<UStaticMesh>(nullptr,
						TEXT("/Game/RedHope/Art/Shaft/RH_Elevator/StaticMeshes/RH_Elevator.RH_Elevator"));
				}
				if (Cage)
				{
					UStaticMeshComponent* CageComp = NewObject<UStaticMeshComponent>(ShaftVisual, TEXT("ElevatorCage"));
					CageComp->SetupAttachment(ShaftVisual->GetRootComponent());
					CageComp->SetMobility(EComponentMobility::Movable);
					CageComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					CageComp->SetAbsolute(true, true, true);
					CageComp->RegisterComponent();
					CageComp->SetStaticMesh(Cage);
					const FBoxSphereBounds CB = Cage->GetBounds();
					// The cab is built in real metres to the RHElevatorLayout.h
					// contract; only the legacy lattice is fitted to the floor.
					CageComp->SetWorldScale3D(bElevatorIsCab ? FVector(1.f)
						: FVector((float)(FloorH * 0.78 / FMath::Max(2.f * CB.BoxExtent.Z, 1.f))));
					ElevatorCage = CageComp;
					// Sliding leaves, only on the cab: its front wall has the
					// pockets they slide into (the lattice never did - see the
					// header note by ElevatorLamp). Placed every tick.
					UStaticMesh* Leaf = bElevatorIsCab ? LoadObject<UStaticMesh>(nullptr,
						TEXT("/Game/RedHope/Art/Machines/RH_ElevatorDoor/StaticMeshes/RH_ElevatorDoor.RH_ElevatorDoor")) : nullptr;
					if (Leaf)
					{
						for (int32 Side = 0; Side < 2; ++Side)
						{
							UStaticMeshComponent* LeafComp = NewObject<UStaticMeshComponent>(ShaftVisual,
								Side == 0 ? TEXT("ElevatorDoorA") : TEXT("ElevatorDoorB"));
							LeafComp->SetupAttachment(ShaftVisual->GetRootComponent());
							LeafComp->SetMobility(EComponentMobility::Movable);
							LeafComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
							LeafComp->SetAbsolute(true, true, true);
							LeafComp->RegisterComponent();
							LeafComp->SetStaticMesh(Leaf);
							LeafComp->SetWorldScale3D(FVector(1.f));
							(Side == 0 ? ElevatorDoorA : ElevatorDoorB) = LeafComp;
						}
					}
					// The approach cue: warm, small, shadowless like every other
					// interior light, driven from 0 in Tick so a sleeping cab
					// stays dark - and on the cab it rides with the doors.
					UPointLightComponent* Lamp = NewObject<UPointLightComponent>(ShaftVisual, TEXT("ElevatorLamp"));
					Lamp->SetupAttachment(ShaftVisual->GetRootComponent());
					Lamp->SetMobility(EComponentMobility::Movable);
					Lamp->SetAbsolute(true, true, true);
					Lamp->SetIntensityUnits(ELightUnits::Candelas);
					Lamp->SetIntensity(0.f);
					Lamp->SetLightColor(FColor(255, 214, 160));
					Lamp->SetAttenuationRadius(520.f);
					Lamp->SetCastShadows(false);
					Lamp->RegisterComponent();
					ElevatorLamp = Lamp;
				}
			}
		}
		else
		{
			ShaftVisual->SetActorLocation(Center);
			ShaftVisual->SetActorScale3D(Scale);
		}
		LastShaftDepthSeen = Depth;
	}

	// Carved cells: one 10x10 m floor tile per completed cell, spiraling out
	// from the shaft head. The sim tracks per-floor COUNTS (A1 model); the
	// spiral is the canonical layout - honest about how much, canonical about
	// where. Spatial painted-position carving is a flagged later-gate upgrade.
	// Tiles are exactly cell-sized and top-flush with the floor plane - the
	// dug-out base must read as ONE piece (director watch-through finding).
	for (int32 L = -1; L >= -Sim->GetMaxDepth(); --L)
	{
		const int32 Want = Sim->GetFloorCarvedCells(L);
		// A floor tile at the shaft-head cell (0,0) too - the spiral skips it (the
		// shaft column's cell), which left an un-tiled HOLE the elevator floated
		// over (director 2026-07-10). The elevator platform now sits on it. Keyed
		// at sentinel index -1 (SpiralCell never yields it), spawned once.
		if (Want > 0 && !TileByCell.Contains(FIntVector(L, -1, 0)))
		{
			const FVector Head = Sim->GetShaftHeadCm();
			const FVector Center(Head.X, Head.Y, L * FloorH - 15.0);
			if (AStaticMeshActor* HeadTile = SpawnBox(Center, FVector(10.f, 10.f, 0.3f),
				FLinearColor(0.20f, 0.15f, 0.11f), FLinearColor::Black))
			{
				// The elevator's own cell wears deck plating like every other
				// carved cell, only darker - it WAS left bare dirt, and on the
				// director's screen (2026-08-14) that read as a hole in the
				// floor around the elevator, not as rock.
				ApplySurface(HeadTile->GetStaticMeshComponent(),
					TEXT("/Game/RedHope/Art/Surfaces/T_HabFloor_Deck.T_HabFloor_Deck"),
					1000.f, FLinearColor(0.30f, 0.30f, 0.32f), 0.6f);
#if WITH_EDITOR
				HeadTile->SetActorLabel(FString::Printf(TEXT("Sim_ShaftFloor_%d"), L));
#endif
				HeadTile->SetActorHiddenInGame(ViewLevel != L);
				CarveTileVisuals.Add(HeadTile);
				TileByCell.Add(FIntVector(L, -1, 0), HeadTile);
				MarkPassesDirty();
			}
		}
		int32& Have = TilesSpawnedPerLevel.FindOrAdd(L);
		while (Have < Want)
		{
			const FIntPoint Cell = Sim->GetFloorCell(L, Have);
			const FVector Head = Sim->GetShaftHeadCm();
			const FVector Center(Head.X + Cell.X * 1000.0, Head.Y + Cell.Y * 1000.0, L * FloorH - 15.0);
			AStaticMeshActor* Tile = SpawnBox(Center, FVector(10.f, 10.f, 0.3f),
				FLinearColor(0.20f, 0.15f, 0.11f), FLinearColor::Black);
			if (!Tile)
			{
				break;
			}
#if WITH_EDITOR
			Tile->SetActorLabel(FString::Printf(TEXT("Sim_Carve_%d_%d"), L, Have));
#endif
			Tile->SetActorHiddenInGame(ViewLevel != L);
			CarveTileVisuals.Add(Tile);
			TileByCell.Add(FIntVector(L, Have, 0), Tile);
			++Have;
			MarkPassesDirty(); // the room, prop and furnishing passes dress it next
		}
	}

	RefreshRoomVisuals();

	// The pit itself (skirt + walls) tracks the carved shape; key-checked, so
	// this is a no-op except on the frame something actually changed.
	RebuildSliceRig();
}

FLinearColor URHColonyVisualizerSubsystem::RoomTint(FName RoomRowName) const
{
	// Function accents against the dug-dirt base (0.20/0.15/0.11): each active
	// room reads as its own material at a glance, gray-box discipline intact.
	if (RoomRowName == FName("LivingQuarters")) { return FLinearColor(0.46f, 0.40f, 0.30f); } // warm bone
	if (RoomRowName == FName("Dining"))         { return FLinearColor(0.10f, 0.30f, 0.27f); } // teal
	if (RoomRowName == FName("Cooking"))        { return FLinearColor(0.42f, 0.17f, 0.06f); } // furnace
	if (RoomRowName == FName("Lab"))            { return FLinearColor(0.10f, 0.20f, 0.38f); } // ice
	if (RoomRowName == FName("Workstation"))    { return FLinearColor(0.42f, 0.30f, 0.08f); } // amber
	if (RoomRowName == FName("Hallway"))        { return FLinearColor(0.30f, 0.30f, 0.32f); } // slate
	if (RoomRowName == FName("Garden"))         { return FLinearColor(0.14f, 0.26f, 0.09f); } // tilled, waiting
	if (RoomRowName == FName("Garden#planted")) { return FLinearColor(0.10f, 0.42f, 0.12f); } // growing - the only green on Mars
	// Crop-stage keys (Garden#<family>#<stage> / Greenhouse#...): same green.
	if (RoomRowName.ToString().Contains(TEXT("#"))) { return FLinearColor(0.10f, 0.42f, 0.12f); }
	// Tier and utility rooms (2026-08-18 gap-fill, professor review): these
	// eight designatable types previously fell through to the dirt default, so
	// every tier room wore the identical washed grey - the same missed-fallback
	// family as the 2026-08-14 furniture audit, which RoomPropPath got fixed
	// for and this chain never did.
	if (RoomRowName == FName("Infirmary"))      { return FLinearColor(0.30f, 0.44f, 0.36f); } // clinical mint
	if (RoomRowName == FName("Workshop"))       { return FLinearColor(0.36f, 0.24f, 0.12f); } // iron + oil
	if (RoomRowName == FName("LabFull"))        { return FLinearColor(0.08f, 0.16f, 0.34f); } // deep ice
	if (RoomRowName == FName("WorkbenchLarge")) { return FLinearColor(0.38f, 0.26f, 0.10f); } // heavy amber
	if (RoomRowName == FName("ChemTableLarge")) { return FLinearColor(0.26f, 0.16f, 0.36f); } // reagent violet
	if (RoomRowName == FName("WaterWorks"))     { return FLinearColor(0.10f, 0.28f, 0.40f); } // water blue
	if (RoomRowName == FName("Septic"))         { return FLinearColor(0.24f, 0.26f, 0.12f); } // murk olive
	if (RoomRowName == FName("Greenhouse"))     { return FLinearColor(0.14f, 0.26f, 0.09f); } // tilled, waiting
	// Unknown row: resolve its FUNCTION (mirrors RoomPropPath's fallback) so a
	// future room inherits its family hue instead of reading as dirt.
	if (!RoomRowName.IsNone())
	{
		if (const URHDefinitionsSubsystem* Defs = GetWorld() ? GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr)
		{
			if (const FRHRoomRow* Row = Defs->GetRoom(RoomRowName))
			{
				if (Row->Function != RoomRowName)
				{
					return RoomTint(Row->Function);
				}
			}
		}
	}
	return FLinearColor(0.20f, 0.15f, 0.11f); // undesignated: the dirt
}

FString URHColonyVisualizerSubsystem::RoomPropPath(FName Room) const
{
	// One hero furnishing per active room type (director's own sprite roster,
	// re-meshed). The player draws the room's SHAPE with DesignateRoom; these
	// props auto-furnish each cell it covers - prefab meeting hand-drawn. Garden
	// arrives here already split into its planted state (planter_dry -> _wet).
	// Empty string = no prop (undesignated rock, or a room with no furniture yet).
	// Props2 = the plate-free re-exports (director 2026-07-09: the baked floor
	// pad under each prop hid the deck) - same art, floor plate removed, own
	// embedded textures. Interchange layout: <name>/StaticMeshes/<name>.
	static const TMap<FName, FString> Props = {
		{ FName("LivingQuarters"), TEXT("/Game/RedHope/Art/Props2/bunk/StaticMeshes/bunk.bunk") },
		{ FName("Lab"),            TEXT("/Game/RedHope/Art/Props2/labbench/StaticMeshes/labbench.labbench") },
		{ FName("Workstation"),    TEXT("/Game/RedHope/Art/Props2/console/StaticMeshes/console.console") },
		{ FName("Dining"),         TEXT("/Game/RedHope/Art/Props2/diningtable/StaticMeshes/diningtable.diningtable") },
		{ FName("Cooking"),        TEXT("/Game/RedHope/Art/Props2/galley/StaticMeshes/galley.galley") },
		{ FName("Hallway"),        TEXT("/Game/RedHope/Art/Props2/conduit/StaticMeshes/conduit.conduit") },
		{ FName("Garden"),         TEXT("/Game/RedHope/Art/Props2/planter_dry/StaticMeshes/planter_dry.planter_dry") },
		{ FName("Garden#planted"), TEXT("/Game/RedHope/Art/Props2/planter_wet/StaticMeshes/planter_wet.planter_wet") },
		{ FName("Greenhouse"),     TEXT("/Game/RedHope/Art/Props2/planter_wet/StaticMeshes/planter_wet.planter_wet") },
		// Dormant rooms (M2 Gate C+): the fluid works read as a tank + pump.
		{ FName("WaterWorks"),     TEXT("/Game/RedHope/Art/Props2/tank/StaticMeshes/tank.tank") },
		{ FName("Septic"),         TEXT("/Game/RedHope/Art/Props2/tank/StaticMeshes/tank.tank") },
		// Tier rooms (tiers Gate A). These are SliceActive and designatable,
		// and had NO entry here - so zoning one silently STRIPPED the cell's
		// furniture and left bare deck, with no warning fired (the missing-asset
		// log only triggers on a non-empty path). Audit finding, 2026-08-14,
		// confirmed by pixel diff. Tiers meshes are still on the importer
		// material - imperfect furniture, but honest beats empty.
		{ FName("WorkbenchLarge"), TEXT("/Game/RedHope/Art/Tiers/workbench_lg/StaticMeshes/workbench_lg.workbench_lg") },
		{ FName("ChemTableLarge"), TEXT("/Game/RedHope/Art/Tiers/chemtable_lg/StaticMeshes/chemtable_lg.chemtable_lg") },
		{ FName("Infirmary"),      TEXT("/Game/RedHope/Art/Tiers/infirmary/StaticMeshes/infirmary.infirmary") },
		{ FName("LabFull"),        TEXT("/Game/RedHope/Art/Tiers/lab_full/StaticMeshes/lab_full.lab_full") },
		{ FName("Workshop"),       TEXT("/Game/RedHope/Art/Tiers/workshop/StaticMeshes/workshop.workshop") },
	};
	// Crop-stage cells (agri Gate A): the room key arrives as
	// <Garden|Greenhouse>#<family>#<stage 0-2>; the stage art is one mesh per
	// silhouette family per stage (crop_<family>_<1-3>, Interchange layout).
	{
		FString RoomStr = Room.ToString();
		FString Base, Rest;
		if (RoomStr.Split(TEXT("#"), &Base, &Rest) && Rest.Contains(TEXT("#")))
		{
			FString Family, StageStr;
			Rest.Split(TEXT("#"), &Family, &StageStr);
			const int32 Stage = FMath::Clamp(FCString::Atoi(*StageStr), 0, 2);
			// Interchange lays these out as <name>/<name>/StaticMeshes/<name>.
			const FString N = FString::Printf(TEXT("crop_%s_%d"), *Family, Stage + 1);
			return FString::Printf(TEXT("/Game/RedHope/Art/Agri/%s/%s/StaticMeshes/%s.%s"), *N, *N, *N, *N);
		}
	}
	if (const FString* P = Props.Find(Room))
	{
		return *P;
	}
	// Unknown row: fall through on its FUNCTION before giving up, so a future
	// data-added room furnishes itself with its family's furniture instead of
	// silently stripping the cell (how the tier rooms went bare for a month).
	if (!Room.IsNone())
	{
		if (const URHDefinitionsSubsystem* Defs = GetWorld() ? GetWorld()->GetSubsystem<URHDefinitionsSubsystem>() : nullptr)
		{
			if (const FRHRoomRow* Row = Defs->GetRoom(Room))
			{
				if (Row->Function != Room)
				{
					if (const FString* F = Props.Find(Row->Function))
					{
						return *F;
					}
				}
			}
		}
	}
	return FString();
}

void URHColonyVisualizerSubsystem::RefreshRoomVisuals()
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return;
	}
	// C3: the tile pass (room tint, label, prop, light per cell) runs on change
	// (see MarkPassesDirty) and on a one-second safety poll - a crop's growth
	// stage folds into the room key and fires no event.
	const double RoomsNow = FPlatformTime::Seconds();
	const bool bRoomsPass = bRoomsDirty || RoomsNow >= NextRoomPassS;
	if (bRoomsPass)
	{
		bRoomsDirty = false;
		NextRoomPassS = RoomsNow + 1.0;
	}
	for (auto& Pair : TileByCell)
	{
		if (!bRoomsPass)
		{
			break;
		}
		AStaticMeshActor* Tile = Pair.Value.Get();
		if (!Tile)
		{
			continue;
		}
		// The shaft-head floor tile (sentinel cell index -1) is never a room -
		// it just fills the hole under the elevator; leave it bare rock.
		if (Pair.Key.Y < 0)
		{
			continue;
		}
		FName Room = Sim->GetRoomAt(Pair.Key.X, Pair.Key.Y);
		// A planted garden reads as its own state - green means growing. With
		// the crop layer live (agri Gate A) the cell reads as its CROP at its
		// GROWTH STAGE: the family+stage fold into the diffed room key, so a
		// stage change refreshes the prop exactly like a redesignation. Cells
		// without a crop entry (crops dormant / legacy saves) keep the exact
		// pre-agri planter visuals.
		if ((Room == FName("Garden") || Room == FName("Greenhouse")) && Sim->IsGardenPlanted(Pair.Key.X, Pair.Key.Y))
		{
			const FName Crop = Sim->GetCellCrop(Pair.Key.X, Pair.Key.Y);
			const int32 Stage = Sim->GetCellCropStage(Pair.Key.X, Pair.Key.Y);
			const FRHCropRow* CropRow = Crop.IsNone() ? nullptr : Defs->GetCrop(Crop);
			if (CropRow && Stage >= 0)
			{
				const FString Family = CropRow->VisualFamily.IsNone() ? TEXT("root") : CropRow->VisualFamily.ToString();
				Room = FName(*FString::Printf(TEXT("%s#%s#%d"), *Room.ToString(), *Family, Stage));
			}
			else if (Room == FName("Garden"))
			{
				Room = FName("Garden#planted");
			}
		}
		FName* Applied = AppliedRoomTint.Find(Pair.Key);
		if (Applied && *Applied == Room)
		{
			continue; // steady state: zero work
		}
		AppliedRoomTint.Add(Pair.Key, Room);
		// Undesignated cells read as bare industrial deck plating - carved and
		// built, awaiting assignment (the regolith-dirt look read as "unfinished
		// floor" on the director's screen, 2026-07-09). A designated room lays
		// clean sealed panels (10 m panel = exactly one cell) with only a GENTLE
		// room-hue tint over a light base, so the at-a-glance colour language
		// survives without the floor glowing. Flat tints if assets missing.
		const bool bTextured = Room.IsNone()
			? ApplySurface(Tile->GetStaticMeshComponent(), TEXT("/Game/RedHope/Art/Surfaces/T_HabFloor_Deck.T_HabFloor_Deck"),
				1000.f, FLinearColor(0.42f, 0.42f, 0.44f), 0.6f)
			: ApplySurface(Tile->GetStaticMeshComponent(), TEXT("/Game/RedHope/Art/Surfaces/T_HabFloor_Sealed.T_HabFloor_Sealed"),
				1000.f, RoomTint(Room) * 0.35f + FLinearColor(0.62f, 0.62f, 0.62f), 0.45f);
		if (!bTextured)
		{
			ApplyTint(Tile, RoomTint(Room));
		}
		// One flat label per tile, created on first designation, retextured on
		// change. Building labels sit at Z=12 (surface); this one rides the
		// tile's own floor plane.
		UTextRenderComponent* Label = Tile->FindComponentByClass<UTextRenderComponent>();
		if (!Label && !Room.IsNone())
		{
			Label = NewObject<UTextRenderComponent>(Tile);
			Label->SetupAttachment(Tile->GetRootComponent());
			Label->SetAbsolute(true, true, true); // the tile's flat scale would smear glyphs
			Label->RegisterComponent();
			Label->SetWorldSize(150.f);
			Label->SetHorizontalAlignment(EHTA_Center);
			Label->SetVerticalAlignment(EVRTA_TextCenter);
			// 25, not 14: the tile is a 0.3 m box whose top face is at +15, so
			// a glyph plane at +14 sat one centimetre INSIDE the deck and was
			// depth-occluded from every camera above it (audit 2026-08-14).
			Label->SetWorldLocationAndRotation(
				Tile->GetActorLocation() + FVector(0, 0, 25.f),
				FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator());
		}
		if (Label)
		{
			FString BaseStr = Room.ToString();
			int32 HashIdx;
			if (BaseStr.FindChar(TEXT('#'), HashIdx)) { BaseStr.LeftInline(HashIdx); }
			const FName BaseRoom(*BaseStr);
			const FRHRoomRow* Row = Defs->GetRoom(BaseRoom);
			Label->SetText(Room.IsNone() ? FText::GetEmpty() : FText::FromString(Row ? Row->DisplayName : BaseRoom.ToString()));
			const FLinearColor T = RoomTint(Room);
			Label->SetTextRenderColor((T * 0.4f + FLinearColor(0.6f, 0.6f, 0.6f)).ToFColor(true));
			// The tile actor's hidden-in-game state (the elevator's floor cut)
			// propagates to the label component automatically.
		}
		// A furnishing prop per designated cell, attached to the tile actor so
		// the slice-view floor-cut, actor lifecycle, and this per-change diff all
		// come free (identical to the label above). One extra StaticMeshComponent
		// per designated cell; undesignated cells and rooms with no prop carry
		// none. Density is one-per-cell by design - a broad room reads furnished,
		// with a per-cell yaw so it isn't tiled wallpaper.
		UStaticMeshComponent* Prop = RoomPropByCell.FindRef(Pair.Key).Get();
		const FString PropPath = RoomPropPath(Room);
		// A missing asset must not leave the PREVIOUS room's furniture standing on
		// a retinted tile - fall through to the hide path and say so once.
		UStaticMesh* PropMesh = PropPath.IsEmpty() ? nullptr : LoadObject<UStaticMesh>(nullptr, *PropPath);
		if (!PropPath.IsEmpty() && !PropMesh)
		{
			UE_LOG(LogRedHope, Warning, TEXT("Room prop asset missing: %s (cell stays unfurnished)"), *PropPath);
		}
		if (!PropMesh)
		{
			if (Prop) { Prop->SetVisibility(false); ++PropsVersion; } // room cleared / no furniture
		}
		else
		{
			if (!Prop)
			{
				Prop = NewObject<UStaticMeshComponent>(Tile, TEXT("RoomProp"));
				Prop->SetupAttachment(Tile->GetRootComponent());
				Prop->SetMobility(EComponentMobility::Movable);
				Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Prop->SetAbsolute(true, true, true); // the tile's flat 0.3 m Z scale would squash it
				Prop->RegisterComponent();
				RoomPropByCell.Add(Pair.Key, Prop);
			}
			++PropsVersion;
			Prop->SetStaticMesh(PropMesh);
			Prop->SetVisibility(true);
			// Fit the prop's larger horizontal dimension to a human-scale
			// footprint (~2.5 m), NOT the whole 10 m cell - furniture must read
			// small beside a ~1.8 m colonist (director: earlier 7.5 m fill made
			// it ~3x too big). Feet on the tile's floor plane (tile is a 0.3 m
			// box centred at L*FloorH-15, so its top is L*FloorH = TileLoc.Z+15),
			// deterministic per-cell yaw. Tunable per-prop later if needed.
			const float TargetFootprintCm = 250.f;
			const FBoxSphereBounds MB = PropMesh->GetBounds();
			const float S = TargetFootprintCm / FMath::Max(2.f * FMath::Max(MB.BoxExtent.X, MB.BoxExtent.Y), 1.f);
			const FVector TileLoc = Tile->GetActorLocation();
			const float Yaw = 90.f * ((Pair.Key.Y * 3 + Pair.Key.X) & 3);
			Prop->SetWorldScale3D(FVector(S));
			Prop->SetWorldLocationAndRotation(
				FVector(TileLoc.X - MB.Origin.X * S,
						TileLoc.Y - MB.Origin.Y * S,
						(TileLoc.Z + 15.f) - (MB.Origin.Z - MB.BoxExtent.Z) * S),
				FRotator(0.f, Yaw, 0.f));
			// Receipt (bounded: fires only on a room CHANGE, not per tick).
			UE_LOG(LogRedHope, Display, TEXT("Room prop: L%d cell %d '%s' furnished with '%s' (scale %.2f, yaw %.0f)"),
				Pair.Key.X, Pair.Key.Y, *Room.ToString(), *PropMesh->GetName(), S, Yaw);
		}
		// Automatic hab lighting (director 2026-07-09): a designated cell gets
		// a soft ceiling light - infrastructure, never a placeable, exactly like
		// air. Created dark; the tick pass below drives its intensity from the
		// sim every frame. Owned by the tile: floor-cut hiding + lifecycle free.
		UPointLightComponent* Light = LightByCell.FindRef(Pair.Key).Get();
		if (Room.IsNone())
		{
			if (Light) { Light->SetVisibility(false); } // room cleared: fixture decommissioned
		}
		else if (!Light)
		{
			Light = NewObject<UPointLightComponent>(Tile, TEXT("CellLight"));
			Light->SetupAttachment(Tile->GetRootComponent());
			Light->SetMobility(EComponentMobility::Movable);
			Light->SetAbsolute(true, true, true);
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetIntensity(0.f);
			Light->SetAttenuationRadius(850.f);
			Light->SetCastShadows(false);
			Light->RegisterComponent();
			Light->SetWorldLocation(Tile->GetActorLocation() + FVector(0, 0, 330.f));
			LightByCell.Add(Pair.Key, Light);
		}
		else
		{
			Light->SetVisibility(true);
		}
		// Room identity in the LIGHT (2026-08-18, professor-gated): colour
		// temperature and a brightness multiplier per room type. Set on BOTH
		// the create and reuse branches - the component survives
		// redesignation (FindRef above), so a colour set only at creation
		// goes stale when a Lab becomes LivingQuarters. Brightness is a
		// per-cell MULTIPLIER folded into the tick loop below, never a
		// SetIntensity here, so brownout/shed/circulation behaviour - "light
		// is a readout of colony health" - is preserved exactly.
		if (Light)
		{
			FColor Temp(255, 236, 210); // the shipped warm interior white
			float Mul = 1.0f;
			const FName Fn = Room;
			const FString RoomStr = Room.ToString();
			if (Fn == FName("Cooking") || Fn == FName("Dining"))
			{
				Temp = FColor(255, 186, 120); Mul = 1.05f;          // galley warmth
			}
			else if (Fn == FName("Lab") || Fn == FName("LabFull") || Fn == FName("Workstation") || Fn == FName("ChemTableLarge"))
			{
				Temp = FColor(222, 234, 255); Mul = 1.0f;           // cool task light
			}
			else if (Fn == FName("Infirmary"))
			{
				Temp = FColor(255, 250, 240); Mul = 1.35f;          // bright neutral
			}
			else if (Fn == FName("LivingQuarters"))
			{
				Temp = FColor(255, 196, 136); Mul = 0.65f;          // dim amber rest
			}
			else if (Fn == FName("Garden") || Fn == FName("Greenhouse") || RoomStr.Contains(TEXT("#")))
			{
				Temp = FColor(255, 226, 238); Mul = 1.15f;          // grow-light blush
			}
			else if (Fn == FName("Hallway"))
			{
				Mul = 0.8f;
			}
			Light->SetLightColor(Temp);
			LightMulByCell.Add(Pair.Key, Mul);
			++LightMulVersion;
		}
	}

	// The lights track the sim EVERY tick (the loop above early-outs per cell
	// on steady state): full while that floor's circulator runs on a healthy
	// grid, dimmed hard in a brownout/shedding, dark when circulation is down.
	// Light is a readout of colony health, not decoration.
	const FRHPowerState& Power = Sim->GetPower();
	const float GridMul = (Power.bDeficit || Power.ShedCount > 0) ? 0.30f : 1.0f;
	constexpr float BaseCandela = 120.f;
	// C3: only a change of the inputs - the grid's state, which floors circulate,
	// which lights exist, a room's brightness multiplier - touches the lights.
	uint32 LightKey = HashCombine((uint32)(GridMul < 1.f ? 1 : 0), (uint32)LightByCell.Num());
	LightKey = HashCombine(LightKey, LightMulVersion);
	uint32 CirculationMask = 0;
	for (int32 L = 0; L <= Sim->GetMaxDepth(); ++L)
	{
		CirculationMask |= Sim->IsFloorCirculated(-L) ? (1u << FMath::Min(L, 31)) : 0u;
	}
	LightKey = HashCombine(LightKey, CirculationMask);
	if (!bLightKeyValid || LightKey != AppliedLightKey)
	{
		bLightKeyValid = true;
		AppliedLightKey = LightKey;
		for (auto& LPair : LightByCell)
		{
			UPointLightComponent* Light = LPair.Value.Get();
			if (!Light)
			{
				continue;
			}
			const float RoomMul = LightMulByCell.FindRef(LPair.Key) > 0.f ? LightMulByCell.FindRef(LPair.Key) : 1.f;
			const float Want = Sim->IsFloorCirculated(LPair.Key.X) ? BaseCandela * GridMul * RoomMul : 0.f;
			if (!FMath::IsNearlyEqual(Light->Intensity, Want, 0.5f))
			{
				Light->SetIntensity(Want);
			}
		}
	}

	// Lived-in accumulation (director 2026-07-09: "once an area gets populated,
	// over time decor and space stuff just appears"). Crates, drums, and lockers
	// collect on inhabited floors - count grows with residents and sols, capped,
	// never removed (a home only gets more lived-in). Deterministic placement
	// (seeded per floor+item) in a cell's outer band, clear of the centre prop
	// and its seat ring. Components ride tile actors: lifecycle + hiding free.
	const URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	const int32 Sol = Clock ? Clock->GetSol() : 0;
	// The locker pointed at the FLAT Props/ lineage, which still carries the
	// baked ground plate the whole Props2 lineage exists to remove - the
	// director's "white platform below it that should be transparent". Props2's
	// locker was cut to 17189 tris in July, carries MI_locker and its 2048
	// textures, and was sitting unreferenced. One in three clutter items is a
	// locker and there are up to 12 per inhabited floor, so this was the most
	// frequently drawn plate in the game. Crate and drum were already on Dress,
	// which was re-baked plinth-free 2026-08-17.
	static const TCHAR* ClutterPaths[3] = {
		TEXT("/Game/RedHope/Art/Dress/RH_crate/StaticMeshes/RH_crate.RH_crate"),
		TEXT("/Game/RedHope/Art/Dress/RH_drum/StaticMeshes/RH_drum.RH_drum"),
		TEXT("/Game/RedHope/Art/Props2/locker/StaticMeshes/locker.locker"),
	};
	TMap<int32, int32> ResidentsPerLevel;
	if (bRoomsPass)
	{
		for (const FRHColonist& C : Sim->GetColonists())
		{
			++ResidentsPerLevel.FindOrAdd(C.HomeLevel);
		}
	}
	for (const auto& TilesPair : TilesSpawnedPerLevel)
	{
		if (!bRoomsPass)
		{
			break;
		}
		const int32 L = TilesPair.Key;
		const int32 Carved = TilesPair.Value;
		if (Carved <= 0)
		{
			continue;
		}
		const int32 Residents = ResidentsPerLevel.FindRef(L);
		if (Residents <= 0)
		{
			continue; // uninhabited floors stay bare
		}
		const int32 Want = FMath::Min(12, Residents * 2 + Sol / 10);
		int32& Have = ClutterSpawnedPerLevel.FindOrAdd(L);
		while (Have < Want)
		{
			FRandomStream Rand(L * 7919 + Have * 131 + 20260709);
			AStaticMeshActor* Tile = TileByCell.FindRef(FIntVector(L, Rand.RandRange(0, Carved - 1), 0)).Get();
			UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, ClutterPaths[Have % 3]);
			if (!Tile || !Mesh)
			{
				break; // tile not spawned yet / assets absent: retry next tick
			}
			UStaticMeshComponent* Item = NewObject<UStaticMeshComponent>(Tile);
			Item->SetupAttachment(Tile->GetRootComponent());
			Item->SetMobility(EComponentMobility::Movable);
			Item->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Item->SetAbsolute(true, true, true);
			Item->RegisterComponent();
			Item->SetStaticMesh(Mesh);
			const FBoxSphereBounds MB = Mesh->GetBounds();
			const float S = Rand.FRandRange(95.f, 130.f) / FMath::Max(2.f * FMath::Max(MB.BoxExtent.X, MB.BoxExtent.Y), 1.f);
			const float Ang = Rand.FRand() * 2.f * PI;
			const float R = Rand.FRandRange(360.f, 450.f); // outer band: clear of prop + seats
			const FVector TileLoc = Tile->GetActorLocation();
			Item->SetWorldScale3D(FVector(S));
			Item->SetWorldLocationAndRotation(
				FVector(TileLoc.X + FMath::Cos(Ang) * R - MB.Origin.X * S,
						TileLoc.Y + FMath::Sin(Ang) * R - MB.Origin.Y * S,
						(TileLoc.Z + 15.f) - (MB.Origin.Z - MB.BoxExtent.Z) * S),
				FRotator(0.f, Rand.FRandRange(0.f, 360.f), 0.f));
			++Have;
		}
	}
}

FVector URHColonyVisualizerSubsystem::RivalMarkerPos(FName Rival, float DistanceKm) const
{
	// A deterministic bearing from a CONTENT hash of the name (stable across
	// runs, like the sim's covert seed) at presentation distance: real km would
	// be off-map, so the marker says "they're over there" - farther settlements
	// sit farther out, all inside the sand skirt.
	const uint32 Hash = FCrc::StrCrc32(*Rival.ToString());
	const double AngleRad = FMath::DegreesToRadians((double)(Hash % 360u));
	const double RadiusCm = 20000.0 + FMath::Clamp(DistanceKm, 0.f, 400.f) * 30.0; // 200 m + 0.3 m/km
	const double X = FMath::Cos(AngleRad) * RadiusCm, Y = FMath::Sin(AngleRad) * RadiusCm;
	return FVector(X, Y, RHMarsTerrain::GroundZCm(X, Y)); // seated on the scenery relief
}

void URHColonyVisualizerSubsystem::RefreshSovereigntyVisuals()
{
	UWorld* World = GetWorld();
	URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHDefinitionsSubsystem* Defs = World ? World->GetSubsystem<URHDefinitionsSubsystem>() : nullptr;
	if (!Sim || !Defs)
	{
		return;
	}
	using namespace RHCanon;
	const bool bSurface = ViewLevel == 0; // sovereignty is surface furniture

	// Settlement markers: one per AVAILABLE rival, tinted by diplomatic state.
	// 0 normal / 1 embargoed / 2 defected / 3 sabotaged; state-diffed so the
	// steady state does zero material work.
	Defs->ForEachRivalRow([&](FName Name, const FRHRivalRow& Row)
	{
		if (!Sim->IsRivalAvailable(Name))
		{
			return; // undiscovered dormant settlements stay off the map
		}
		TObjectPtr<AStaticMeshActor>* Found = RivalMarkers.Find(Name);
		AStaticMeshActor* Marker = Found ? Found->Get() : nullptr;
		if (!Marker)
		{
			// A distant low-slung compound: main hall + a dome, plus a name.
			const FVector Pos = RivalMarkerPos(Name, Row.DistanceKm);
			Marker = SpawnBox(Pos + FVector(0, 0, 125.f), FVector(7.f, 5.f, 2.5f), BoneWhite, FLinearColor::Black);
			if (!Marker)
			{
				return;
			}
#if WITH_EDITOR
			Marker->SetActorLabel(FString::Printf(TEXT("Sim_Rival_%s"), *Name.ToString()));
#endif
			AddAccent(Marker, TEXT("/Engine/BasicShapes/Sphere.Sphere"), Pos + FVector(-250.f, 200.f, 220.f),
				FRotator::ZeroRotator, FVector(2.6f, 2.6f, 1.8f), DarkSlate, TealGlow * 0.2f);
			AddLabel(Marker, Row.DisplayName, BoneWhite, 480.f);
			RivalMarkers.Add(Name, Marker);
			RivalMarkerState.Add(Name, 255); // force the first tint pass
		}
		// Diplomatic state -> tint (defected outranks embargo outranks sabotage).
		uint8 State = 0;
		if (Sim->HasDefected(Name))                      { State = 2; }
		else if (Sim->IsEmbargoed(Name))                 { State = 1; }
		else if (Sim->GetSabotageRemaining(Name) > 0.0)  { State = 3; }
		uint8& Applied = RivalMarkerState.FindOrAdd(Name);
		if (Applied != State)
		{
			Applied = State;
			switch (State)
			{
			case 1:  ApplyTint(Marker, HazYellow, AmberGlow * 0.25f); break;          // embargoed: amber warning
			case 2:  ApplyTint(Marker, FLinearColor(0.42f, 0.07f, 0.05f), FLinearColor(1.2f, 0.1f, 0.05f)); break; // defected: hostile red
			case 3:  ApplyTint(Marker, DarkSlate, FLinearColor::Black); break;        // sabotaged: gone dark
			default: ApplyTint(Marker, BoneWhite, TealGlow * 0.08f); break;           // normal: a living neighbor
			}
		}
		Marker->SetActorHiddenInGame(!bSurface);
	});

	// The trade rover: drives home -> settlement -> home on the sim's own
	// progress. Hidden when the convoy is idle (or underground view).
	const FName Out = Sim->GetConvoyRival();
	if (Out.IsNone())
	{
		if (ConvoyVisual)
		{
			ConvoyVisual->SetActorHiddenInGame(true);
		}
	}
	else if (const FRHRivalRow* Row = Defs->GetRival(Out))
	{
		if (!ConvoyVisual)
		{
			// REFERENCE (Robots/ConstructionDrone + Ships/ScoutSpeeder language):
			// bone-white hull, dark tinted cab glass, six dark wheels, a hazard
			// stripe, an amber running beacon, and the cargo pack on the back.
			// Root spawns at UNIFORM scale so relative children don't smear (the
			// old absolute-space label was left behind at the spawn point while
			// the rover drove away - all parts ride relative now).
			ConvoyVisual = SpawnBox(FVector(0, 0, 60.f), FVector(1.f, 1.f, 1.f), BoneWhite, FLinearColor::Black);
			if (ConvoyVisual)
			{
#if WITH_EDITOR
				ConvoyVisual->SetActorLabel(TEXT("Sim_Convoy"));
#endif
				UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
				UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
				UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
				const auto Part = [&](UStaticMesh* Shape, const FVector& RelCm, const FRotator& Rot, const FVector& Scale,
					const FLinearColor& Color, const FLinearColor& Emissive = FLinearColor::Black)
				{
					if (!Shape) { return; }
					UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(ConvoyVisual);
					Mesh->SetupAttachment(ConvoyVisual->GetRootComponent());
					Mesh->RegisterComponent();
					Mesh->SetStaticMesh(Shape);
					Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					Mesh->SetRelativeLocationAndRotation(RelCm, Rot);
					Mesh->SetRelativeScale3D(Scale);
					ApplyTint(Mesh, Color, Emissive);
				};
				Part(CubeMesh, FVector(0, 0, 10.f), FRotator::ZeroRotator, FVector(2.6f, 1.5f, 0.9f), BoneWhite);                    // hull
				Part(CubeMesh, FVector(108.f, 0, 42.f), FRotator(-16.f, 0, 0), FVector(0.7f, 1.25f, 0.62f),
					FLinearColor(0.03f, 0.05f, 0.07f), FLinearColor(0.05f, 0.12f, 0.15f));                                          // cab glass
				Part(CubeMesh, FVector(-72.f, 0, 58.f), FRotator::ZeroRotator, FVector(1.1f, 1.3f, 0.72f), DarkSlate);              // cargo pack
				Part(CubeMesh, FVector(0, 79.f, 4.f), FRotator::ZeroRotator, FVector(2.45f, 0.05f, 0.2f), HazYellow);               // stripe R
				Part(CubeMesh, FVector(0, -79.f, 4.f), FRotator::ZeroRotator, FVector(2.45f, 0.05f, 0.2f), HazYellow);              // stripe L
				for (int32 W = 0; W < 6; ++W)
				{
					const float Wx = (W % 3 - 1) * 92.f;
					const float Wy = (W < 3 ? 1.f : -1.f) * 82.f;
					Part(CylMesh, FVector(Wx, Wy, -38.f), FRotator(0, 0, 90.f), FVector(0.56f, 0.56f, 0.2f), FLinearColor(0.05f, 0.05f, 0.06f));
				}
				Part(SphereMesh, FVector(128.f, 0, 82.f), FRotator::ZeroRotator, FVector(0.16f), HazYellow, AmberGlow);             // beacon
			}
		}
		if (ConvoyVisual)
		{
			// Home = the Lander pad; destination = the rival's marker.
			FVector Home(0, 0, 0);
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName == FName("Lander")) { Home = B.LocationCm; break; }
			}
			const FVector Dest = RivalMarkerPos(Out, Row->DistanceKm);
			const double P = Sim->GetConvoyProgress(); // 0..1 round trip
			const double Alpha = Sim->IsConvoyReturning()
				? FMath::Clamp((P - 0.5) * 2.0, 0.0, 1.0)   // marker -> home
				: FMath::Clamp(P * 2.0, 0.0, 1.0);          // home -> marker
			const FVector From = Sim->IsConvoyReturning() ? Dest : Home;
			const FVector To = Sim->IsConvoyReturning() ? Home : Dest;
			// Wheels on the ground the whole road: the straight-line lerp gets
			// its Z from the terrain under each step, not the endpoints.
			FVector Loc = FMath::Lerp(From, To, Alpha);
			Loc.Z = RHMarsTerrain::GroundZCm(Loc.X, Loc.Y) + 60.f;
			ConvoyVisual->SetActorLocation(Loc);
			// The cab leads: face the rover down its road (X+ = nose).
			const FVector Travel = (To - From).GetSafeNormal2D();
			if (!Travel.IsNearlyZero())
			{
				ConvoyVisual->SetActorRotation(Travel.Rotation());
			}
			ConvoyVisual->SetActorHiddenInGame(!bSurface);
		}
	}
}

AStaticMeshActor* URHColonyVisualizerSubsystem::SpawnBox(const FVector& CenterCm, const FVector& ScaleM, const FLinearColor& Body, const FLinearColor& Emissive) const
{
	UWorld* World = GetWorld();
	AStaticMeshActor* Actor = World ? World->SpawnActor<AStaticMeshActor>(CenterCm, FRotator::ZeroRotator) : nullptr;
	if (!Actor)
	{
		return nullptr;
	}
	UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	Actor->SetActorScale3D(ScaleM);
	ApplyTint(Actor, Body, Emissive);
	return Actor;
}

void URHColonyVisualizerSubsystem::ApplyCutaway()
{
	if (!WallISM)
	{
		return;
	}
	const int32 Mode = FMath::Clamp(CVarCutaway.GetValueOnGameThread(), 0, 2);

	uint8 Hidden = 0;
	if (Mode == 2)
	{
		Hidden = 0x0F; // floorplan: every face down
	}
	else if (Mode == 1)
	{
		const UWorld* World = GetWorld();
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APlayerCameraManager* Cam = PC ? PC->PlayerCameraManager : nullptr;
		if (Cam)
		{
			const FVector CamLoc = Cam->GetCameraLocation();
			FVector2D ToCam(CamLoc.X - PitCenterCm.X, CamLoc.Y - PitCenterCm.Y);
			if (ToCam.Normalize())
			{
				const uint8 WasHidden = (AppliedCutawayKey == 0xFF) ? 0u : (uint8)(AppliedCutawayKey & 0x0F);
				for (int32 i = 0; i < 4; ++i)
				{
					// A face whose OUTWARD normal points at the camera is a near
					// wall standing between the viewer and the room. Two
					// thresholds, not one: a side already down stays down until
					// it clearly swings away, so an orbit through the boundary
					// cannot strobe walls in and out.
					// FVector2D is double-precision in UE5; the cast is explicit so no
					// narrowing warning can fire under -Werror.
					const float Dot = (float)(ToCam.X * GRHWallDirs[i].X + ToCam.Y * GRHWallDirs[i].Y);
					const bool bWas = (WasHidden & (1 << i)) != 0;
					if (bWas ? (Dot > 0.25f) : (Dot > 0.40f))
					{
						Hidden |= (uint8)(1 << i);
					}
				}
			}
		}
	}

	const uint8 Key = (uint8)((Mode << 4) | Hidden);
	if (Key == AppliedCutawayKey)
	{
		return;
	}
	AppliedCutawayKey = Key;

	WallISM->ClearInstances();
	for (int32 i = 0; i < WallFaceXf.Num(); ++i)
	{
		const int32 Slot = RHWallDirSlot(WallFaceDir[i]);
		if (Slot != INDEX_NONE && (Hidden & (1 << Slot)) != 0)
		{
			continue;
		}
		WallISM->AddInstance(WallFaceXf[i], /*bWorldSpace*/ true);
	}
	for (int32 i = 0; i < WallVents.Num(); ++i)
	{
		UStaticMeshComponent* Vent = WallVents[i].Get();
		if (!Vent)
		{
			continue;
		}
		const int32 Slot = WallVentDir.IsValidIndex(i) ? RHWallDirSlot(WallVentDir[i]) : INDEX_NONE;
		Vent->SetHiddenInGame(Slot != INDEX_NONE && (Hidden & (1 << Slot)) != 0);
	}
}

void URHColonyVisualizerSubsystem::Debug_SetPulseScale(float Scale)
{
	int32 Touched = 0;
	for (const TPair<int32, TObjectPtr<AStaticMeshActor>>& Pair : BuildingVisuals)
	{
		AStaticMeshActor* Actor = Pair.Value;
		if (!Actor)
		{
			continue;
		}
		UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
		if (!Mesh)
		{
			continue;
		}
		UMaterialInstanceDynamic* Mid = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
		if (!Mid)
		{
			Mid = Mesh->CreateAndSetMaterialInstanceDynamic(0);
		}
		if (!Mid)
		{
			continue;
		}
		// First touch reads the authored value straight through to the parent
		// instance; every later call scales that remembered number. Buildings
		// whose material is not M_RH_Master simply read 0 and stay still.
		float Authored = 0.f;
		if (const float* Remembered = AuthoredPulseDepth.Find(Pair.Key))
		{
			Authored = *Remembered;
		}
		else
		{
			Authored = Mid->K2_GetScalarParameterValue(FName("PulseDepth"));
			AuthoredPulseDepth.Add(Pair.Key, Authored);
		}
		Mid->SetScalarParameterValue(FName("PulseDepth"), Authored * Scale);
		++Touched;
	}
	UE_LOG(LogRedHope, Display, TEXT("[RH.Pulse] scale %.2f applied to %d building visual(s)"), Scale, Touched);
}

void URHColonyVisualizerSubsystem::SetViewLevel(int32 Level)
{
	if (ViewLevel == Level)
	{
		return;
	}
	ViewLevel = Level;
	ApplyViewLevel();
}

void URHColonyVisualizerSubsystem::ApplyViewLevel()
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!World || !Sim)
	{
		return;
	}

	// v3 (director recording verdict - "two distinct floors"): a floor's
	// visuals show iff you are looking at THAT floor. At SURF (ViewLevel 0)
	// that's the surface colony and nothing underground; at -N it's that
	// floor's pocket and nothing else. One rule: visible iff BLevel==ViewLevel.
	const bool bSurface = !IsUnderground();
	for (auto& Pair : BuildingVisuals)
	{
		if (!Pair.Value)
		{
			continue;
		}
		int32 BLevel = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.Id == Pair.Key)
			{
				BLevel = B.Level;
				break;
			}
		}
		Pair.Value->SetActorHiddenInGame(BLevel != ViewLevel);
	}
	// The underground fill light rides the elevator: it hovers over whichever
	// floor is open. (Its owner, the shaft column, is hidden at SURF, which
	// hides the light with it.)
	if (UPointLightComponent* Fill = FloorFill.Get())
	{
		const FVector Head = Sim->GetShaftHeadCm();
		Fill->SetWorldLocation(FVector(Head.X, Head.Y, ViewLevel * Sim->GetFloorHeightCm() + 350.0));
	}
	// Deposit markers, the ship, and the robot layer are surface furniture -
	// shown only in the surface view (underground is a separate stratum).
	for (AStaticMeshActor* Marker : DepositMarkers)
	{
		if (Marker)
		{
			Marker->SetActorHiddenInGame(!bSurface);
		}
	}
	if (ShipVisual)
	{
		ShipVisual->SetActorHiddenInGame(!bSurface);
	}
	if (LandingPadVisual)
	{
		LandingPadVisual->SetActorHiddenInGame(!bSurface);
	}
	if (URHAgentVisualizerSubsystem* Agents = World->GetSubsystem<URHAgentVisualizerSubsystem>())
	{
		Agents->SetSliceHidden(!bSurface);
	}
	// Carve tiles: only the viewed floor's pocket shows. At SURF none match
	// (tiles are all negative-Z) - the surface stays clean.
	const double FloorH = Sim->GetFloorHeightCm();
	for (AStaticMeshActor* Tile : CarveTileVisuals)
	{
		if (Tile)
		{
			const int32 TileLevel = FMath::RoundToInt32(Tile->GetActorLocation().Z / FloorH);
			Tile->SetActorHiddenInGame(TileLevel != ViewLevel);
		}
	}
	// The shaft column belongs to the underground read; at SURF the intact
	// ground covers it, so hide it to avoid poking through the surface.
	if (ShaftVisual)
	{
		ShaftVisual->SetActorHiddenInGame(bSurface);
	}

	RebuildSliceRig();
}

void URHColonyVisualizerSubsystem::EnsureSliceRig()
{
	if (SliceRigActor)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	SliceRigActor = World->SpawnActor<AActor>();
	if (!SliceRigActor)
	{
		return;
	}
	USceneComponent* Root = NewObject<USceneComponent>(SliceRigActor, TEXT("Root"));
	Root->RegisterComponent();
	SliceRigActor->SetRootComponent(Root);
#if WITH_EDITOR
	SliceRigActor->SetActorLabel(TEXT("RH_SliceRig"));
#endif
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/RedHope/Art/M_Graybox.M_Graybox"));
	const auto MakeISM = [&](const TCHAR* Name, const FLinearColor& Tint) -> UInstancedStaticMeshComponent*
	{
		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(SliceRigActor, Name);
		ISM->SetupAttachment(Root);
		ISM->RegisterComponent();
		SliceRigActor->AddInstanceComponent(ISM);
		ISM->SetStaticMesh(Cube);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCastShadow(true);
		if (Base)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, ISM);
			Mid->SetVectorParameterValue(FName("Tint"), Tint);
			ISM->SetMaterial(0, Mid);
		}
		return ISM;
	};
	// Skirt matches the sunlit regolith so the hole reads as dug INTO the same
	// ground; walls read as INSULATED habitat lining - brushed panels sealing
	// the excavation to protect the inhabitants (director 2026-07-09: bare rock
	// walls looked wrong for a lived-in vault). Flat tints if assets missing.
	SkirtISM = MakeISM(TEXT("PitSkirt"), FLinearColor(0.48f, 0.31f, 0.19f));
	WallISM = MakeISM(TEXT("PitWalls"), FLinearColor(0.55f, 0.54f, 0.52f));
	ApplySurface(SkirtISM, TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"), 900.f, FLinearColor(0.85f, 0.80f, 0.75f));
	ApplySurface(WallISM, TEXT("/Game/RedHope/Art/Surfaces/T_HabWall_Panel.T_HabWall_Panel"), 450.f, FLinearColor(0.72f, 0.71f, 0.69f), 0.55f);
}

void URHColonyVisualizerSubsystem::EnsureEnvironmentRig()
{
	if (EnvironmentRigActor)
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	EnvironmentRigActor = World->SpawnActor<AActor>();
	if (!EnvironmentRigActor)
	{
		return;
	}
	USceneComponent* Root = NewObject<USceneComponent>(EnvironmentRigActor, TEXT("Root"));
	Root->RegisterComponent();
	EnvironmentRigActor->SetRootComponent(Root);
#if WITH_EDITOR
	EnvironmentRigActor->SetActorLabel(TEXT("RH_Scenery"));
#endif

	// The heavy scenery - ground relief with carved craters, boulder fields,
	// the three mountain bands, mesas, and the hero massif - is real procedural
	// mesh now (graphics pass 3: the primitive cones and cube rocks never
	// survived the director's eye). Same seeds, same composition, real shapes.
	RHMarsTerrain::BuildScenery(*EnvironmentRigActor);

	// The authored MarsGround pad gets the real regolith texture too - it is
	// what the camera looks at most. Find it NOW (the pit view's lazy finder
	// matches on the material NAME, which this swap changes) and cache it, so
	// the underground hide keeps working off the cached pointer.
	if (!GroundActor.IsValid())
	{
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			bool bMatch = It->GetFName().ToString().Contains(TEXT("MarsGround"));
			if (!bMatch)
			{
				if (const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent())
				{
					const UMaterialInterface* Mat = Mesh->GetNumMaterials() > 0 ? Mesh->GetMaterial(0) : nullptr;
					bMatch = Mat && Mat->GetName().Contains(TEXT("MarsGround"));
				}
			}
			if (bMatch)
			{
				GroundActor = *It;
				break;
			}
		}
	}
	if (AStaticMeshActor* Pad = Cast<AStaticMeshActor>(GroundActor.Get()))
	{
		const bool bOk = ApplySurface(Pad->GetStaticMeshComponent(),
			TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"), 900.f, FLinearColor::White);
		UE_LOG(LogRedHope, Display, TEXT("Ground pad retexture: %s"), bOk ? TEXT("regolith applied") : TEXT("assets missing - flat look kept"));
	}
	else
	{
		UE_LOG(LogRedHope, Warning, TEXT("Ground pad retexture: MarsGround actor not found at rig time - pad keeps its authored material"));
	}

	// The wind-laid dust drifts (90 stretched-sphere instances) are GONE -
	// the director flagged the smooth beige ellipsoids as alien objects on
	// the terrain, twice (2026-07-17 hand-play: "strange rocks or weird oval
	// things"). The procedural boulder fields in BuildScenery carry the
	// ground interest on their own.
}

void URHColonyVisualizerSubsystem::RebuildSliceRig()
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	if (!World || !Sim)
	{
		return;
	}

	// The real ground is the SURFACE (director fix: it must never grey out at
	// SURF - the old rig hid it whenever a shaft existed and swapped in a
	// gray-box skirt that read dark at night). It yields ONLY while you are
	// underground looking into the pit; at SURF it is always the intact dirt.
	// (Material-matched search: World Partition renames actors, labels lie.)
	// Ground search/hide keys off the VIEW, not the shaft depth: the ground is
	// hidden iff we are underground looking into the pit, and shown otherwise.
	// (Descent is clamped to reached depth, so underground always implies a
	// real bored column beneath us - but gating on the view keeps this correct
	// regardless of how ViewLevel got set.)
	const int32 Depth = Sim->GetShaftDepth();
	if (!GroundActor.IsValid() && IsUnderground())
	{
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			bool bMatch = It->GetFName().ToString().Contains(TEXT("MarsGround"));
			if (!bMatch)
			{
				if (const UStaticMeshComponent* Mesh = It->GetStaticMeshComponent())
				{
					const UMaterialInterface* Mat = Mesh->GetNumMaterials() > 0 ? Mesh->GetMaterial(0) : nullptr;
					bMatch = Mat && Mat->GetName().Contains(TEXT("MarsGround"));
				}
			}
			if (bMatch)
			{
				GroundActor = *It;
				break;
			}
		}
		if (!GroundActor.IsValid() && !bGroundSearched)
		{
			UE_LOG(LogRedHope, Warning, TEXT("Pit view: no MarsGround actor/material found - the underground rim skirt is absent"));
		}
		bGroundSearched = true;
	}
	if (GroundActor.IsValid())
	{
		GroundActor->SetActorHiddenInGame(IsUnderground());
	}

	const int32 Carved = IsUnderground() ? Sim->GetFloorCarvedCells(ViewLevel) : 0;
	const FIntVector Key(Depth, ViewLevel, Carved);
	if (Key == LastRigKey)
	{
		return;
	}
	LastRigKey = Key;

	EnsureSliceRig();
	if (!SkirtISM || !WallISM)
	{
		return;
	}
	SkirtISM->ClearInstances();
	WallISM->ClearInstances();
	// Vents die with the wall layout they sat on - including on the way UP to
	// the surface, where the pit rig clears but this function returns early.
	for (const TWeakObjectPtr<UStaticMeshComponent>& V : WallVents)
	{
		if (UStaticMeshComponent* Vent = V.Get())
		{
			Vent->DestroyComponent();
		}
	}
	WallVents.Reset();
	// The cutaway's authored face list dies with them, and it MUST be cleared
	// on this side of the early return below: at the surface WallISM is emptied
	// and this function bails, so a stale face list would let ApplyCutaway
	// re-add pit walls into a view that has no pit.
	WallFaceXf.Reset();
	WallFaceDir.Reset();
	WallVentDir.Reset();
	AppliedCutawayKey = 0xFF;
	if (!IsUnderground())
	{
		return; // SURF (or above): the intact ground is the view - no pit rig
	}
	const int32 Pit = ViewLevel; // the floor being looked into

	// The open pocket on the pit floor: the shaft's own cell plus every
	// carved cell (the floor table's lattice - identical math to the floor tiles).
	TSet<FIntPoint> Open;
	Open.Add(FIntPoint(0, 0));
	for (int32 i = 0; i < Carved; ++i)
	{
		Open.Add(Sim->GetFloorCell(Pit, i));
	}

	const FVector Head = Sim->GetShaftHeadCm();
	const double FloorH = Sim->GetFloorHeightCm();
	const double PitDepthCm = -Pit * FloorH; // surface (0) down to the open floor

	// Sand skirt: the surface, minus the hole. 30 cells (~300 m) each way
	// covers the playfield at every practical zoom; beyond it the void only
	// shows at orbital register (gray-box note, not a bug).
	constexpr int32 SkirtCells = 30;
	for (int32 GX = -SkirtCells; GX <= SkirtCells; ++GX)
	{
		for (int32 GY = -SkirtCells; GY <= SkirtCells; ++GY)
		{
			if (Open.Contains(FIntPoint(GX, GY)))
			{
				continue;
			}
			const FTransform T(FRotator::ZeroRotator,
				FVector(Head.X + GX * 1000.0, Head.Y + GY * 1000.0, -20.0),
				FVector(10.f, 10.f, 0.4f));
			SkirtISM->AddInstance(T, /*bWorldSpace*/ true);
		}
	}

	// Pit walls: every open-to-rock edge gets a cut face from the surface
	// down to the open floor - the vertical faces are what make it a HOLE
	// dug in the ground instead of a shaved-off plane.
	UStaticMesh* VentMesh = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/RedHope/Art/Dress/RH_vent/StaticMeshes/RH_vent.RH_vent"));

	// The cutaway needs to know where every face is and which way it looks, so
	// the authored list is filled here and WallISM becomes a filtered view of
	// it. (The lists were cleared above, on the surface side of the early
	// return, so both paths leave them consistent with WallISM.)
	PitCenterCm = Head;

	int32 FaceIdx = 0;
	for (const FIntPoint& Cell : Open)
	{
		for (const FIntPoint& D : GRHWallDirs)
		{
			if (Open.Contains(Cell + D))
			{
				continue;
			}
			const FVector CellCenter(Head.X + Cell.X * 1000.0, Head.Y + Cell.Y * 1000.0, 0.0);
			const FVector WallCenter = CellCenter + FVector(D.X * 530.0, D.Y * 530.0, -PitDepthCm * 0.5);
			const FVector Scale = D.X != 0
				? FVector(0.6f, 10.f, (float)(PitDepthCm / 100.0))
				: FVector(10.f, 0.6f, (float)(PitDepthCm / 100.0));
			const FTransform WallXf(FRotator::ZeroRotator, WallCenter, Scale);
			WallFaceXf.Add(WallXf);
			WallFaceDir.Add(D);
			WallISM->AddInstance(WallXf, /*bWorldSpace*/ true);
			// Every third wall face carries a life-support vent at head height
			// on the open floor - the insulated lining reads SERVICED, not dead
			// space (director 2026-07-09). Faces inward at the room.
			if (VentMesh && SliceRigActor && (FaceIdx++ % 3) == 0)
			{
				UStaticMeshComponent* Vent = NewObject<UStaticMeshComponent>(SliceRigActor);
				Vent->SetupAttachment(SliceRigActor->GetRootComponent());
				Vent->SetMobility(EComponentMobility::Movable);
				Vent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Vent->RegisterComponent();
				Vent->SetStaticMesh(VentMesh);
				const FBoxSphereBounds VB = VentMesh->GetBounds();
				const float VS = 110.f / FMath::Max(2.f * VB.BoxExtent.GetMax(), 1.f); // ~1.1 m unit
				const FVector Inward(-D.X, -D.Y, 0.f);
				const FVector VentPos = CellCenter
					+ FVector(D.X * 480.0, D.Y * 480.0, Pit * FloorH + 210.0)
					- FVector(VB.Origin * VS);
				Vent->SetWorldScale3D(FVector(VS));
				Vent->SetWorldLocationAndRotation(VentPos, Inward.Rotation());
				WallVents.Add(Vent);
				WallVentDir.Add(D); // parallel to WallVents: the vent hides with its wall

			}
		}
	}
	UE_LOG(LogRedHope, Display, TEXT("Pit rebuilt: floor %d, %d open cell(s), %d wall face(s), %d vent(s)"),
		Pit, Open.Num(), WallISM->GetInstanceCount(), WallVents.Num());
}
