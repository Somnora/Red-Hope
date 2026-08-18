#include "RHColonyVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHAgentVisualizerSubsystem.h"
#include "RHCrewVisualizerSubsystem.h"
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
		Sim->OnQuotaMet.AddUObject(this, &URHColonyVisualizerSubsystem::HandleQuotaMet);
		Sim->OnShipArrived.AddUObject(this, &URHColonyVisualizerSubsystem::HandleShipArrived);
		Sim->OnDepositDiscovered.AddUObject(this, &URHColonyVisualizerSubsystem::HandleDepositDiscovered);
		Sim->OnSurveyCompleted.AddUObject(this, &URHColonyVisualizerSubsystem::HandleSurveyCompleted);
		Sim->OnAlert.AddUObject(this, &URHColonyVisualizerSubsystem::HandleAlert);
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
	if (ShipVisual)
	{
		ShipVisual->Destroy();
		ShipVisual = nullptr;
	}
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

void URHColonyVisualizerSubsystem::SpawnDepositMarker(const FRHDepositState& D)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	// Footprint scales gently with mass: 60 t ~ 16 m across.
	const float Side = FMath::Clamp(FMath::Sqrt(D.RemainingKg) * 0.065f, 8.f, 30.f);
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
		D.LocationCm + FVector(0, 0, 6.f + RHMarsTerrain::GroundZCm(D.LocationCm.X, D.LocationCm.Y)),
		FRotator::ZeroRotator);
	if (!Actor)
	{
		return;
	}
	Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
	// A thin pad, not a platform: 12 cm proud of the terrain reads as ground.
	Actor->SetActorScale3D(FVector(Side, Side, 0.12f));
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
	// The director's "giant checkerboard": ApplyTint dresses the pad in
	// M_Graybox, whose blockout grid stretched across a 16-30 m cube face is
	// exactly a checkerboard. Deposits are GROUND, so wear the terrain's own
	// regolith texture, world-tiled, tinted per type - the label and tinted
	// hue still say what it is, the surface now says where it is.
	if (!ApplySurface(Actor->GetStaticMeshComponent(),
		TEXT("/Game/RedHope/Art/Mars_Regolith_Texture.Mars_Regolith_Texture"),
		340.f, DepColor * FLinearColor(1.6f, 1.5f, 1.6f), 0.95f))
	{
		ApplyTint(Actor, DepColor); // graybox fallback beats invisible
	}
	AddLabel(Actor, D.RowName.ToString(), DepColor, Side * 50.f + 260.f);
	// Surface furniture: born hidden if the player is currently underground
	// (adversarial-review finding - a discovery mid-descent used to pop the
	// marker into the pit view until the next floor change).
	Actor->SetActorHiddenInGame(IsUnderground());
	DepositMarkers.Add(Actor);
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
	LastNotice = FString::Printf(TEXT("SURVEY: %s discovered - %.0f t %s"),
		*D.RowName.ToString(), D.RemainingKg / 1000.0, *D.Type.ToString());
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
	if (!Instance.bUnderConstruction)
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
			// Re-bakes of the two worst paint offenders from the director's
			// 2026-08-18 photo notes. The old extractor2/RH_AirFilter2 assets
			// stay on disk under RealModelPaths as the rh.ModelSetV2=0
			// fallback. Borer keeps its digging-arm identity by design brief.
			{ FName("Borer"),         FString(TEXT("/Game/RedHope/Art/Models2/Borer/Borer/StaticMeshes/Borer.Borer")) },
			{ FName("AirFilter"),     FString(TEXT("/Game/RedHope/Art/Models2/AirFilter/AirFilter/StaticMeshes/AirFilter.AirFilter")) },
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
		const float TargetCm = FMath::Min(Scale.X, Scale.Y) * 100.f;
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
	if (!Instance.bUnderConstruction && !RealModel)
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
		// Surface furniture: hidden if the player is underground when the ship
		// lands (adversarial-review finding), shown again on the next SURF view.
		Ship->SetActorHiddenInGame(IsUnderground());
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
	// The scenery ring + ground scatter (one-time build; state-diffed hide when
	// the elevator descends, matching the surface ground plane's behavior).
	EnsureEnvironmentRig();
	if (EnvironmentRigActor && bEnvironmentHidden != IsUnderground())
	{
		bEnvironmentHidden = IsUnderground();
		EnvironmentRigActor->SetActorHiddenInGame(bEnvironmentHidden);
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

	// Is it working? A machine that has been shed by the power priority, or
	// switched off by the player, goes dark; a running one keeps its glow. The
	// sim already decides this per building (FRHBuildingInstance::bPowered), so
	// the visual is a pure readout - state-diffed, because writing a material
	// parameter every frame for every building would be waste. Only meshes
	// wearing the M_RH_Master family carry PoweredState; on anything else the
	// parameter simply does not exist and the write is a no-op.
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		const uint8 Want = B.bUnderConstruction ? 2 : (B.bPowered ? 1 : 0);
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
			Mid->SetScalarParameterValue(FName("PoweredState"), Want == 1 ? 1.f : 0.f);
		}
		// A Floodmast on a shed breaker goes dark with everything else.
		// (Named FoundLamp, not Found: this scope already has a `Found` for the
		// building visual, and the build is -Werror,-Wshadow.)
		if (const TWeakObjectPtr<UPointLightComponent>* FoundLamp = BuildingLights.Find(B.Id))
		{
			if (UPointLightComponent* Lamp = FoundLamp->Get())
			{
				Lamp->SetVisibility(Want == 1);
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
	// then proximity-toggle every tile label and building label.
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
		for (const auto& TP : TileByCell)
		{
			if (AStaticMeshActor* Tile = TP.Value.Get())
			{
				if (UTextRenderComponent* L = Tile->FindComponentByClass<UTextRenderComponent>())
				{
					L->SetVisibility(FVector::Dist2D(Tile->GetActorLocation(), CursorCm) < 600.f);
				}
			}
		}
		for (const auto& BP : BuildingVisuals)
		{
			if (AStaticMeshActor* B = BP.Value)
			{
				const bool bNear = FVector::Dist2D(B->GetActorLocation(), CursorCm) < 800.f;
				TArray<UTextRenderComponent*> Texts;
				B->GetComponents(Texts);
				for (UTextRenderComponent* L : Texts)
				{
					L->SetVisibility(bNear);
				}
			}
		}
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
		if (const UStaticMesh* Cage = CageComp->GetStaticMesh())
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
				Target = Crew->IsAnyCrewWithinCm(FVector(Head.X, Head.Y, FloorZ), 420.f) ? 1.f : 0.f;
			}
		}
		ElevatorWakeAlpha = FMath::FInterpTo(ElevatorWakeAlpha, Target, DeltaTime, 3.5f);
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
				UStaticMesh* Cage = LoadObject<UStaticMesh>(nullptr,
					TEXT("/Game/RedHope/Art/Machines/RH_Elevator2/StaticMeshes/RH_Elevator2.RH_Elevator2"));
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
					CageComp->SetWorldScale3D(FVector((float)(FloorH * 0.78 / FMath::Max(2.f * CB.BoxExtent.Z, 1.f))));
					ElevatorCage = CageComp;
					// No door panels (see the header note by ElevatorLamp).
					// The approach cue is this lamp inside the lattice: warm,
					// small, shadowless like every other interior light, and
					// driven from 0 in Tick so a sleeping cage stays dark.
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
			}
		}
		int32& Have = TilesSpawnedPerLevel.FindOrAdd(L);
		while (Have < Want)
		{
			const FIntPoint Cell = SpiralCell(Have);
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
	for (auto& Pair : TileByCell)
	{
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
			if (Prop) { Prop->SetVisibility(false); } // room cleared / no furniture
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
		}
	}

	// The lights track the sim EVERY tick (the loop above early-outs per cell
	// on steady state): full while that floor's circulator runs on a healthy
	// grid, dimmed hard in a brownout/shedding, dark when circulation is down.
	// Light is a readout of colony health, not decoration.
	const FRHPowerState& Power = Sim->GetPower();
	const float GridMul = (Power.bDeficit || Power.ShedCount > 0) ? 0.30f : 1.0f;
	constexpr float BaseCandela = 120.f;
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
	for (const auto& TilesPair : TilesSpawnedPerLevel)
	{
		const int32 L = TilesPair.Key;
		const int32 Carved = TilesPair.Value;
		if (Carved <= 0)
		{
			continue;
		}
		int32 Residents = 0;
		for (const FRHColonist& C : Sim->GetColonists())
		{
			if (C.HomeLevel == L)
			{
				++Residents;
			}
		}
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

FIntPoint URHColonyVisualizerSubsystem::SpiralCell(int32 Index)
{
	// Gate B moved the canonical layout sim-side (adjacency made cell geometry
	// gameplay); this forwards so existing presentation callers keep working.
	return URHSimWorldSubsystem::SpiralCell(Index);
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
	// carved cell (spiral layout - identical math to the floor tiles).
	TSet<FIntPoint> Open;
	Open.Add(FIntPoint(0, 0));
	for (int32 i = 0; i < Carved; ++i)
	{
		Open.Add(SpiralCell(i));
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
