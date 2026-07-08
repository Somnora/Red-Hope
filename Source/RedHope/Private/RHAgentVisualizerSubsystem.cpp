#include "RHAgentVisualizerSubsystem.h"
#include "RedHope.h"
#include "RHMarsTerrain.h"
#include "RHSimWorldSubsystem.h"
#include "MassEntitySubsystem.h"
#include "Mass/EntityFragments.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	// The humanoid's proportions (reference: Robots/Humanoid - white armor over
	// matte-black joints, ~1.8 m tall). All offsets are in ENTITY space, X
	// forward, relative to the entity transform's origin; the prior 1.5 m-tall
	// stand-in cube put the ground at origin-75, so the feet keep that datum.
	constexpr float GroundZ = -75.f;
	constexpr float HipZ = GroundZ + 78.f;
	constexpr float ShoulderZ = GroundZ + 138.f;
	constexpr float LegLen = 76.f;   // hip to sole
	constexpr float ArmLen = 58.f;
	constexpr float HipHalfY = 11.f;
	constexpr float ShoulderHalfY = 26.f;

	// A swinging limb: leaf scale/offset hangs the box below the pivot, the
	// swing rotates about the pivot, then the whole thing rides the entity.
	FTransform Limb(const FVector& PivotEntitySpace, float SwingDeg, float LenCm, const FVector& Girth, const FTransform& Entity)
	{
		const FTransform Mesh(FQuat::Identity, FVector(0, 0, -LenCm * 0.5f), FVector(Girth.X, Girth.Y, LenCm / 100.f));
		const FTransform Swing(FRotator(SwingDeg, 0.f, 0.f).Quaternion());
		const FTransform Pivot(FQuat::Identity, PivotEntitySpace);
		return Mesh * Swing * Pivot * Entity;
	}
}

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
	LastPosCm.Reset();
	FacingYawDeg.Reset();
	StridePhase.Reset();
	for (UInstancedStaticMeshComponent* Part : Parts)
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

	const FTransform Proto(FQuat::Identity, FVector::ZeroVector, FVector(1.f, 1.f, 1.f));
	for (int32 i = 0; i < Entities.Num(); ++i)
	{
		for (UInstancedStaticMeshComponent* Part : Parts)
		{
			if (Part)
			{
				Part->AddInstance(Proto, /*bWorldSpace*/ true);
			}
		}
		LastPosCm.Add(FVector::ZeroVector);
		FacingYawDeg.Add(0.f);
		StridePhase.Add((float)((Tracked.Num() + i) % 7) * 0.9f); // desynced strides
	}
	UE_LOG(LogRedHope, Display, TEXT("Visualizer tracking %d agents"), Tracked.Num());
}

void URHAgentVisualizerSubsystem::SetSliceHidden(bool bHidden)
{
	bSliceHidden = bHidden;
	for (UInstancedStaticMeshComponent* Part : Parts)
	{
		if (Part)
		{
			Part->SetVisibility(!bHidden);
		}
	}
}

void URHAgentVisualizerSubsystem::EnsureMeshComponent()
{
	if (Parts.Num() > 0)
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

	// Reference palette: gloss-white armor plates, matte-black underlayer/
	// joints, the dark visor's cyan status bar. The white keeps the prior
	// stand-in's warm self-glow so the night shift stays visible.
	const FLinearColor ArmorWhite(0.60f, 0.58f, 0.54f);
	const FLinearColor ArmorGlow(0.45f, 0.38f, 0.22f);
	const FLinearColor JointBlack(0.05f, 0.05f, 0.06f);
	Parts.SetNum((int32)EPart::COUNT);
	Parts[(int32)EPart::Torso] = MakeLayer(TEXT("Torso"), ArmorWhite, ArmorGlow);
	Parts[(int32)EPart::Pelvis] = MakeLayer(TEXT("Pelvis"), JointBlack, FLinearColor::Black);
	Parts[(int32)EPart::Head] = MakeLayer(TEXT("Head"), ArmorWhite, ArmorGlow * 0.5f);
	Parts[(int32)EPart::Visor] = MakeLayer(TEXT("Visor"), FLinearColor(0.02f, 0.03f, 0.04f), FLinearColor(0.1f, 1.8f, 2.2f));
	Parts[(int32)EPart::Pack] = MakeLayer(TEXT("Pack"), JointBlack, FLinearColor::Black);
	Parts[(int32)EPart::LegL] = MakeLayer(TEXT("LegL"), ArmorWhite, FLinearColor::Black);
	Parts[(int32)EPart::LegR] = MakeLayer(TEXT("LegR"), ArmorWhite, FLinearColor::Black);
	Parts[(int32)EPart::ArmL] = MakeLayer(TEXT("ArmL"), JointBlack, FLinearColor::Black);
	Parts[(int32)EPart::ArmR] = MakeLayer(TEXT("ArmR"), JointBlack, FLinearColor::Black);
}

void URHAgentVisualizerSubsystem::Tick(float DeltaTime)
{
	if (Parts.Num() == 0 || Tracked.Num() == 0 || DeltaTime <= 0.f)
	{
		return;
	}
	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return;
	}
	const FMassEntityManager& EntityManager = MassSubsystem->GetEntityManager();

	TArray<TArray<FTransform>> Out;
	Out.SetNum((int32)EPart::COUNT);
	for (TArray<FTransform>& Arr : Out)
	{
		Arr.Reserve(Tracked.Num());
	}

	for (int32 i = 0; i < Tracked.Num(); ++i)
	{
		if (!EntityManager.IsEntityValid(Tracked[i]))
		{
			continue;
		}
		const FVector Pos = EntityManager.GetFragmentDataChecked<FTransformFragment>(Tracked[i]).GetTransform().GetLocation();

		// Gait from the entity's own motion: stride phase accumulates with
		// ground covered (so faster sim speeds walk faster, and a stopped
		// robot's legs settle); facing eases toward the travel direction.
		const FVector Delta = (Pos - LastPosCm[i]) * FVector(1, 1, 0);
		const float Speed = LastPosCm[i].IsNearlyZero() ? 0.f : (float)Delta.Size() / DeltaTime; // cm/s
		LastPosCm[i] = Pos;
		const float Stride = FMath::Clamp(Speed / 220.f, 0.f, 1.f); // full swing at a brisk walk
		if (Speed > 15.f)
		{
			const float TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
			FacingYawDeg[i] = FMath::FixedTurn(FacingYawDeg[i], TargetYaw, 540.f * DeltaTime);
		}
		StridePhase[i] += (Speed / 46.f) * DeltaTime; // ~one full cycle per stride length
		const float Swing = 26.f * Stride * FMath::Sin(StridePhase[i] * 2.f * PI);
		const float Bob = 2.5f * Stride * FMath::Abs(FMath::Sin(StridePhase[i] * 2.f * PI));

		// The sim walks robots on the z=0 plane; the scenery relief is purely
		// presentation, so the figure rides the terrain height at its feet
		// (zero across the colony flat, real ground out at the far deposits).
		const float TerrainZ = RHMarsTerrain::GroundZCm(Pos.X, Pos.Y);
		const FTransform Entity(FRotator(0.f, FacingYawDeg[i], 0.f).Quaternion(), Pos + FVector(0, 0, Bob + TerrainZ));

		// Rigid parts: offsets in entity space.
		const auto Fixed = [&](const FVector& At, const FVector& Scale)
		{
			return FTransform(FQuat::Identity, At, Scale) * Entity;
		};
		Out[(int32)EPart::Torso].Add(Fixed(FVector(0, 0, GroundZ + 116.f), FVector(0.30f, 0.42f, 0.52f)));
		Out[(int32)EPart::Pelvis].Add(Fixed(FVector(0, 0, HipZ + 6.f), FVector(0.24f, 0.32f, 0.20f)));
		Out[(int32)EPart::Head].Add(Fixed(FVector(0, 0, GroundZ + 160.f), FVector(0.22f, 0.24f, 0.24f)));
		Out[(int32)EPart::Visor].Add(Fixed(FVector(12.f, 0, GroundZ + 161.f), FVector(0.05f, 0.18f, 0.10f)));
		Out[(int32)EPart::Pack].Add(Fixed(FVector(-19.f, 0, GroundZ + 120.f), FVector(0.12f, 0.30f, 0.34f)));
		// Swinging limbs: legs opposite phase, arms counter-swinging their leg.
		Out[(int32)EPart::LegL].Add(Limb(FVector(0, -HipHalfY, HipZ), Swing, LegLen, FVector(0.15f, 0.13f, 0), Entity));
		Out[(int32)EPart::LegR].Add(Limb(FVector(0, HipHalfY, HipZ), -Swing, LegLen, FVector(0.15f, 0.13f, 0), Entity));
		Out[(int32)EPart::ArmL].Add(Limb(FVector(0, -ShoulderHalfY, ShoulderZ), -Swing * 0.7f, ArmLen, FVector(0.10f, 0.10f, 0), Entity));
		Out[(int32)EPart::ArmR].Add(Limb(FVector(0, ShoulderHalfY, ShoulderZ), Swing * 0.7f, ArmLen, FVector(0.10f, 0.10f, 0), Entity));
	}

	for (int32 P = 0; P < (int32)EPart::COUNT; ++P)
	{
		if (Parts[P])
		{
			Parts[P]->BatchUpdateInstancesTransforms(0, Out[P], /*bWorldSpace*/ true, /*bMarkRenderStateDirty*/ true, /*bTeleport*/ true);
		}
	}
}
