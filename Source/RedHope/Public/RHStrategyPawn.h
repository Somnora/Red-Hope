#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RHStrategyPawn.generated.h"

class UCameraComponent;

// Orbital <-> ground strategic camera. One zoom parameter ZoomT drives
// distance/pitch/FOV through designer-tunable ranges (CT_CameraRig curves
// replace the linear lerps in the M0 camera pass). Free 360 orbit by
// construction - the prototype's constrained band cannot exist here.
UCLASS()
class REDHOPE_API ARHStrategyPawn : public APawn
{
	GENERATED_BODY()

public:
	ARHStrategyPawn();

	virtual void Tick(float DeltaTime) override;

	// Input surface (wired to Enhanced Input in the M0 UI pass; callable from
	// BP/console meanwhile).
	UFUNCTION(BlueprintCallable, Category = "RedHope|Camera") void AddZoom(float Delta);
	UFUNCTION(BlueprintCallable, Category = "RedHope|Camera") void AddOrbit(float DeltaYawDeg);
	UFUNCTION(BlueprintCallable, Category = "RedHope|Camera") void AddPan(FVector2D PlanarDeltaCm);

	// Current camera distance (cm) - pan speed scales with it so a screen-width
	// drag covers similar ground at every register.
	float GetViewDistanceCm() const
	{
		return MinDistanceCm * FMath::Pow(MaxDistanceCm / MinDistanceCm, SmoothedZoomT);
	}

	// Sliced-floor view (M1-d): the elevator drops the camera's focus plane to
	// the active floor; orbit/zoom/pan behave identically at any depth. The
	// descent is smoothed in Tick so the ride reads as motion, not a cut
	// (director watch-through finding: "I didn't see the elevator move").
	void SetFocusZCm(float Zcm) { TargetFocusZCm = Zcm; }

	// 0 = ground register (25 m), 1 = orbital register (3000 m).
	// Default 0.45 ~= 215 m: the whole starting colony in frame with readable
	// 3D depth on the gray-boxes - the classic strategy opening shot.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RedHope|Camera", meta = (ClampMin = "0", ClampMax = "1"))
	float ZoomT = 0.45f;

	UPROPERTY(EditAnywhere, Category = "RedHope|Camera") float MinDistanceCm = 2500.f;    // 25 m
	UPROPERTY(EditAnywhere, Category = "RedHope|Camera") float MaxDistanceCm = 300000.f;  // 3000 m
	UPROPERTY(EditAnywhere, Category = "RedHope|Camera") float MinPitchDeg = 30.f;
	UPROPERTY(EditAnywhere, Category = "RedHope|Camera") float MaxPitchDeg = 84.f;
	UPROPERTY(EditAnywhere, Category = "RedHope|Camera") float GroundFovDeg = 55.f;
	UPROPERTY(EditAnywhere, Category = "RedHope|Camera") float OrbitalFovDeg = 40.f;
	UPROPERTY(EditAnywhere, Category = "RedHope|Camera") float ZoomInterpSpeed = 6.f;

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;

	FVector FocusPointCm = FVector::ZeroVector;
	float TargetFocusZCm = 0.f; // elevator destination; Tick rides toward it
	float OrbitYawDeg = 0.f;
	float SmoothedZoomT = 0.45f;
};
