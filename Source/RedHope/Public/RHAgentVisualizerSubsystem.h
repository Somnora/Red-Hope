#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MassEntityTypes.h"
#include "Mass/EntityHandle.h"
#include "RHAgentVisualizerSubsystem.generated.h"

class UInstancedStaticMeshComponent;
class USkeletalMeshComponent;

// Scaffold presentation for sim agents: instanced meshes, transforms copied
// from Mass fragments each frame. Deliberately dumb - it proves the seam (sim
// knows nothing about it) and is what the stress test measures as
// "presentation cost". MassRepresentation replaces it in M0+.
//
// Reference (Robots/Humanoid, "reminiscent of a Tesla Bot"): each unit is a
// BIPEDAL HUMANOID composed from nine instanced layers - white armored torso /
// head / legs over matte-black joints and arms, a dark visor with a cyan
// status bar. Legs and arms swing in a procedural walk cycle driven by each
// entity's own ground speed (phase accumulates with distance covered), and the
// figure faces its direction of travel. Still zero per-actor cost: N robots =
// 9 ISMs total, one instance each.
UCLASS()
class REDHOPE_API URHAgentVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHAgentVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	void TrackEntities(const TArray<FMassEntityHandle>& Entities);
	// Load path: forget every tracked entity and instance; the reload's
	// OnRobotsSpawned broadcasts repopulate from scratch.
	void ResetTracking();
	// Sliced-floor view (M1-d): robots are surface actors until later gates
	// send them below; the whole ISM layer hides when the elevator descends.
	void SetSliceHidden(bool bHidden);

private:
	void EnsureMeshComponent();

	// One ISM per body part; every tracked entity owns instance i in each.
	enum class EPart : uint8
	{
		Torso = 0, Pelvis, Head, Visor, Pack, LegL, LegR, ArmL, ArmR, COUNT
	};
	UPROPERTY() TArray<TObjectPtr<UInstancedStaticMeshComponent>> Parts;
	// Rigged robot walkers (animation phase): when RH_Walker_robot exists each
	// tracked entity gets ONE skeletal component instead of ISM instances -
	// real Walk/Idle clips driven by ground speed. Fleet is small (<20), so
	// per-robot skeletal cost is fine; the 9-ISM cube figure stays as the
	// art-free fallback.
	UPROPERTY() TArray<TObjectPtr<USkeletalMeshComponent>> SkelBodies;
	TArray<bool> bWalkPlaying;
	// Per-walker Z lift that puts the MESH'S OWN feet on the ground, derived
	// from its bounds at spawn. The old constant GroundZ (-75) assumed the
	// primitive stand-in's mid-body origin; the rigged GLBs are grounded at
	// their feet, so that constant buried every robot 75 cm - the director's
	// "stuck halfway underground".
	TArray<float> FeetLiftCm;
	// One small regolith-lump component per skeletal robot (professor-gated
	// 2026-08-18): visible while the robot's FRHTaskFragment carries CargoKg,
	// scaled by load fraction. Read from the PUBLIC Mass fragments - no sim
	// accessor, no sim recompile.
	TArray<TWeakObjectPtr<UStaticMeshComponent>> CargoLumps;
	bool bUseSkeletal = false;
	TArray<FMassEntityHandle> Tracked;
	// Per-entity gait state, parallel to Tracked: last ground position (for
	// speed), smoothed facing yaw, and the accumulated stride phase.
	TArray<FVector> LastPosCm;
	TArray<float> FacingYawDeg;
	TArray<float> StridePhase;
	bool bSliceHidden = false;
};
