#include "RHColonyVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHSimWorldSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHAgentVisualizerSubsystem.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RotationMatrix.h"
#include "DrawDebugHelpers.h"

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
		|| DefName == FName("ChargePad") || DefName == FName("Stockpile")
		|| DefName == FName("ComputeModule"))
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
		&& DefName != FName("Stockpile") && DefName != FName("Pylon"))
	{
		const FLinearColor Hue = TintFor(DefName);
		AddAccent(Actor, Cube, BaseCm + FVector(0, 0, TopZ - 30.f), FRotator::ZeroRotator,
			FVector(ScaleM.X + 0.14f, ScaleM.Y + 0.14f, 0.5f), Hue, Hue * 0.9f);
	}

	if (DefName == FName("Lander"))
	{
		// A descent-stage lander: bone hull up on four splayed legs, an engine bell
		// with an ember at the throat, propellant spheres, and an ascent capsule
		// with a comms dish. The (raised) base box is the descent stage body.
		const float Lift = GLanderLiftCm;
		const float BodyTop = Lift + ScaleM.Z * 100.f;
		const float BodyMid = Lift + ScaleM.Z * 50.f;
		const float HalfX = ScaleM.X * 50.f;
		const float HalfY = ScaleM.Y * 50.f;
		// Descent engine bell (the default cone already opens downward) + throat ember.
		AddAccent(Actor, Cone, BaseCm + FVector(0, 0, (Lift + 20.f) * 0.5f), FRotator::ZeroRotator, FVector(2.4f, 2.4f, (Lift - 20.f) / 100.f), DarkSlate);
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, 45.f), FRotator::ZeroRotator, FVector(0.8f, 0.8f, 0.6f), FLinearColor(0.95f, 0.4f, 0.08f), FLinearColor(4.0f, 1.2f, 0.15f));
		// Four splayed landing legs down to footpads.
		for (int32 Leg = 0; Leg < 4; ++Leg)
		{
			const FVector Dir = FRotator(0, Leg * 90.f + 45.f, 0).Vector();
			const FVector Att = BaseCm + Dir * (HalfX * 0.7f) + FVector(0, 0, Lift + 20.f);
			const FVector Foot = BaseCm + Dir * (HalfX + 200.f);
			const FVector LegVec = Foot - Att;
			AddAccent(Actor, Cube, (Att + Foot) * 0.5f, FRotationMatrix::MakeFromZ(LegVec.GetSafeNormal()).Rotator(), FVector(0.28f, 0.28f, LegVec.Size() / 100.f), DarkSlate);
			AddAccent(Actor, Cyl, Foot + FVector(0, 0, 15.f), FRotator::ZeroRotator, FVector(1.1f, 1.1f, 0.25f), DarkSlate);
		}
		// Propellant spheres flanking the descent stage.
		AddAccent(Actor, Sphere, BaseCm + FVector(0, HalfY + 30.f, BodyMid), FRotator::ZeroRotator, FVector(1.5f, 1.5f, 1.7f), BoneWhite);
		AddAccent(Actor, Sphere, BaseCm + FVector(0, -HalfY - 30.f, BodyMid), FRotator::ZeroRotator, FVector(1.5f, 1.5f, 1.7f), BoneWhite);
		// Ascent / crew capsule: drum + dome, with a comms mast + dish.
		AddAccent(Actor, Cyl, BaseCm + FVector(0, 0, BodyTop + 70.f), FRotator::ZeroRotator, FVector(1.7f, 1.7f, 1.4f), BoneWhite);
		AddAccent(Actor, Sphere, BaseCm + FVector(0, 0, BodyTop + 150.f), FRotator::ZeroRotator, FVector(1.6f, 1.6f, 1.0f), BoneWhite);
		AddAccent(Actor, Cyl, BaseCm + FVector(HalfX * 0.5f, HalfY * 0.5f, BodyTop + 40.f), FRotator::ZeroRotator, FVector(0.12f, 0.12f, 1.6f), DarkSlate);
		AddAccent(Actor, Cyl, BaseCm + FVector(HalfX * 0.5f, HalfY * 0.5f, BodyTop + 125.f), FRotator(35.f, 0, 0), FVector(1.3f, 1.3f, 0.1f), BoneWhite);
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
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor((Color * 0.4f + FLinearColor(0.6f, 0.6f, 0.6f)).ToFColor(true));
	// Lie flat on the ground, glyph face pointing UP (+Z) so the top-down camera
	// reads the front of the text, not the mirrored back; glyph-up toward world
	// +X reads upright at the default north-looking camera heading (confirmed in
	// an oblique capture - the -Forward variant rendered it upside down). Built
	// from axes so it stays a pure rotation, never a reflection - the old Euler
	// combo faced the text into the ground and read backwards + upside down.
	Label->SetWorldLocationAndRotation(
		Actor->GetActorLocation() * FVector(1, 1, 0) + FVector(-SouthOffsetCm, 0, 12.f),
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
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(D.LocationCm + FVector(0, 0, 20.f), FRotator::ZeroRotator);
	if (!Actor)
	{
		return;
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
			// The visual body is narrower than the sim footprint - the splayed
			// legs span the rest. A boxy descent stage, not a slab on stilts.
			Scale.X *= 0.6f;
			Scale.Y *= 0.6f;
			Scale.Z = 2.2f;
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
	// Solar arrays read as panels: tilted toward the equatorial sun, raised so
	// the low corner clears the ground.
	const bool bPanel = Instance.DefName == FName("SolarArray") && !Instance.bUnderConstruction;
	// The Lander rides up on its legs so its descent engine + gear are visible.
	const bool bLander = Instance.DefName == FName("Lander") && !Instance.bUnderConstruction;
	const float LiftZ = bPanel ? 70.f : (bLander ? GLanderLiftCm + Scale.Z * 50.f : Scale.Z * 50.f);
	const FVector Location = Instance.LocationCm + FVector(0, 0, LiftZ);
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
	// The panel glass keeps a faint blue self-glow so it reads PV-blue even in
	// the orange grade / shadow, instead of washing to a flat slab.
	const FLinearColor BodyGlow = bPanel ? FLinearColor(0.05f, 0.10f, 0.30f) : FLinearColor::Black;
	ApplyTint(Actor, Instance.bUnderConstruction ? Body * 0.3f : Body, Instance.bUnderConstruction ? FLinearColor::Black : BodyGlow);
	AddLabel(Actor, Instance.DefName.ToString(), Tint, Scale.X * 50.f + 260.f);
	if (bPanel)
	{
		// A mounted PV array, not a bare slab: bone-white frame lip, dark cell-seam
		// mullions on the tilted glass, and a dark A-frame mount + torque tube.
		const TCHAR* Cube = TEXT("/Engine/BasicShapes/Cube.Cube");
		const TCHAR* Cyl = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
		const float HalfY = Scale.Y * 50.f;
		const FVector Panel = Instance.LocationCm + FVector(0, 0, 70.f);
		AddAccent(Actor, Cube, Panel + FVector(0, 0, -6.f), Facing, FVector(Scale.X + 0.35f, Scale.Y + 0.35f, 0.1f), RHCanon::BoneWhite);
		AddAccent(Actor, Cube, Panel + FVector(0, 0, 9.f), Facing, FVector(Scale.X + 0.4f, 0.14f, 0.12f), RHCanon::DarkSlate);
		AddAccent(Actor, Cube, Panel + FVector(0, HalfY * 0.5f, 9.f), Facing, FVector(Scale.X + 0.4f, 0.1f, 0.1f), RHCanon::DarkSlate);
		AddAccent(Actor, Cube, Panel + FVector(0, -HalfY * 0.5f, 9.f), Facing, FVector(Scale.X + 0.4f, 0.1f, 0.1f), RHCanon::DarkSlate);
		AddAccent(Actor, Cyl, Instance.LocationCm + FVector(0, 0, 38.f), FRotator(0, 0, 90.f), FVector(0.28f, 0.28f, Scale.Y * 0.85f), RHCanon::DarkSlate);
		AddAccent(Actor, Cube, Instance.LocationCm + FVector(0, HalfY * 0.55f, 32.f), FRotator(0, 0, -18.f), FVector(0.3f, 0.3f, 1.5f), RHCanon::DarkSlate);
		AddAccent(Actor, Cube, Instance.LocationCm + FVector(0, -HalfY * 0.55f, 26.f), FRotator(0, 0, -18.f), FVector(0.3f, 0.3f, 1.1f), RHCanon::DarkSlate);
	}
	else if (!Instance.bUnderConstruction)
	{
		BuildSilhouette(Actor, Instance.DefName, Instance.LocationCm, Scale);
	}
	else
	{
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
			AddAccent(Actor, Cube, Instance.LocationCm + Corner + FVector(0, 0, 85.f), FRotator::ZeroRotator,
				FVector(0.14f, 0.14f, 1.7f), RHCanon::HazYellow, FLinearColor(0.4f, 0.28f, 0.02f));
		}
		AddAccent(Actor, Cube, Instance.LocationCm + FVector(0, 0, 105.f), FRotator::ZeroRotator,
			FVector(0.09f, 0.09f, 2.1f), RHCanon::DarkSlate);
		AddAccent(Actor, Sphere, Instance.LocationCm + FVector(0, 0, 225.f), FRotator::ZeroRotator,
			FVector(0.28f), RHCanon::HazYellow, FLinearColor(3.2f, 2.2f, 0.35f));
	}
	// Born on whatever floor the sim says; visible only if the elevator is
	// looking at that stratum (M1-d slice view).
	Actor->SetActorHiddenInGame(Instance.Level != ViewLevel);
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

	// Shaft & carved-floor mirror (M1-d): state-diffed each frame - the counts
	// are tiny and the sim has no per-cell visual events to listen to.
	UpdateShaftVisuals();
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

	// The trunk: one dark column from the surface down to the bored depth,
	// amber-lit (canon: hazard hues mark machine interfaces).
	if (Depth > 0 && Depth != LastShaftDepthSeen)
	{
		const FVector Head = Sim->GetShaftHeadCm();
		const FVector Center(Head.X, Head.Y, -Depth * FloorH * 0.5);
		const FVector Scale(4.f, 4.f, Depth * FloorH / 100.0);
		if (!ShaftVisual)
		{
			ShaftVisual = SpawnBox(Center, Scale, RHCanon::DarkSlate, FLinearColor(0.35f, 0.25f, 0.02f));
#if WITH_EDITOR
			if (ShaftVisual) { ShaftVisual->SetActorLabel(TEXT("Sim_Shaft")); }
#endif
			// Born hidden at SURF (invariant D): a bore that completes while
			// the player is at the surface must not flash its column through
			// the ground before the next view change (mirrors carve tiles).
			if (ShaftVisual) { ShaftVisual->SetActorHiddenInGame(!IsUnderground()); }
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
	return FLinearColor(0.20f, 0.15f, 0.11f); // undesignated: the dirt
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
		FName Room = Sim->GetRoomAt(Pair.Key.X, Pair.Key.Y);
		// A planted garden reads as its own state - green means growing.
		if (Room == FName("Garden") && Sim->IsGardenPlanted(Pair.Key.X, Pair.Key.Y))
		{
			Room = FName("Garden#planted");
		}
		FName* Applied = AppliedRoomTint.Find(Pair.Key);
		if (Applied && *Applied == Room)
		{
			continue; // steady state: zero work
		}
		AppliedRoomTint.Add(Pair.Key, Room);
		ApplyTint(Tile, RoomTint(Room));
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
			Label->SetWorldLocationAndRotation(
				Tile->GetActorLocation() + FVector(0, 0, 14.f),
				FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).Rotator());
		}
		if (Label)
		{
			const FName BaseRoom = (Room == FName("Garden#planted")) ? FName("Garden") : Room;
			const FRHRoomRow* Row = Defs->GetRoom(BaseRoom);
			Label->SetText(Room.IsNone() ? FText::GetEmpty() : FText::FromString(Row ? Row->DisplayName : BaseRoom.ToString()));
			const FLinearColor T = RoomTint(Room);
			Label->SetTextRenderColor((T * 0.4f + FLinearColor(0.6f, 0.6f, 0.6f)).ToFColor(true));
			// The tile actor's hidden-in-game state (the elevator's floor cut)
			// propagates to the label component automatically.
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
	// Skirt matches the sunlit regolith so the hole reads as dug INTO the
	// same ground; walls are darker strata - the cut faces of the excavation.
	SkirtISM = MakeISM(TEXT("PitSkirt"), FLinearColor(0.48f, 0.31f, 0.19f));
	WallISM = MakeISM(TEXT("PitWalls"), FLinearColor(0.30f, 0.19f, 0.12f));
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
	const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	for (const FIntPoint& Cell : Open)
	{
		for (const FIntPoint& D : Dirs)
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
			WallISM->AddInstance(FTransform(FRotator::ZeroRotator, WallCenter, Scale), /*bWorldSpace*/ true);
		}
	}
	UE_LOG(LogRedHope, Display, TEXT("Pit rebuilt: floor %d, %d open cell(s), %d wall face(s)"),
		Pit, Open.Num(), WallISM->GetInstanceCount());
}
