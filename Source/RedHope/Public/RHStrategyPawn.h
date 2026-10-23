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

	virtual void BeginPlay() override;
	// The opening pull-back on the Lander (also replayed when the title's NEW
	// COLONY closes the menu).
	void PlayIntro();
	virtual void Tick(float DeltaTime) override;

	// Input surface (wired to Enhanced Input in the M0 UI pass; callable from
	// BP/console meanwhile).
	UFUNCTION(BlueprintCallable, Category = "RedHope|Camera") void AddZoom(float Delta);
	UFUNCTION(BlueprintCallable, Category = "RedHope|Camera") void AddOrbit(float DeltaYawDeg);
	UFUNCTION(BlueprintCallable, Category = "RedHope|Camera") void AddPan(FVector2D PlanarDeltaCm);
	// Manual tilt on top of the zoom-coupled pitch (director: "tilt up and see
	// mountains"). Negative offset raises the gaze; the final pitch clamps just
	// above the horizon so the scenery ring and sky read, never under the world.
	UFUNCTION(BlueprintCallable, Category = "RedHope|Camera") void AddTilt(float DeltaPitchDeg);

	// Scripted-capture placement (QA contact sheets): set the whole pose at once
	// AND collapse the interpolators, so the next frame is already the final
	// framing. A camera still easing when a timed screenshot fires would make
	// the shot depend on when it was taken; these sheets must be repeatable.
	// The focus Z is deliberately untouched - the elevator owns it.
	UFUNCTION(BlueprintCallable, Category = "RedHope|Camera")
	void SetCameraPose(FVector2D FocusXYCm, float InZoomT, float InPitchOffsetDeg, float InYawDeg);
	// The getter SetCameraPose lacked (D8): the deep view remembers the floor view's pose on entry and puts it back on exit.
	void GetCameraPose(FVector2D& OutFocusXYCm, float& OutZoomT, float& OutPitchOffsetDeg, float& OutYawDeg) const;

	// D8 (the deep view): while on, Tick uses the section's own pitch (5..35 degrees; R/F move it) instead of the zoom-coupled one, and
	// the yaw eases to an exact multiple of 90. Distance and FOV still follow ZoomT, so the wheel only moves the camera nearer or farther.
	void SetSectionView(bool bOn, float PitchDeg, float YawDeg);
	bool IsSectionView() const { return bSectionView; }
	void SetSectionYaw(float YawDeg) { SectionYawDeg = FMath::UnwindDegrees(YawDeg); } // a quarter turn keeps the tilt
	// The zoom at which the frame is WidthCm wide at the focus (FOV is horizontal), solved by bisection on the distance and FOV curves.
	float SolveZoomForWidthCm(float WidthCm) const;

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
	// D2: the next-shaft move re-centres the focus on a head without touching zoom, pitch or yaw.
	void SetFocusXYCm(const FVector2D& XYCm) { FocusPointCm.X = XYCm.X; FocusPointCm.Y = XYCm.Y; }
	FVector GetFocusPointCm() const { return FocusPointCm; }

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
	// Manual tilt (R/F): 0 = classic zoom-coupled pitch; negative looks up.
	float PitchOffsetDeg = 0.f;
	float SmoothedPitchOffsetDeg = 0.f;
	// The opening shot (interface pass, 2026-10-15): the session starts low
	// and close on the Lander and slowly pulls back and swings round to the
	// classic strategy framing. Presentation only - sim time is untouched.
	// Any camera input, or a scripted pose, ends it at once.
	float IntroSecondsLeft = 0.f;
	float IntroYawOffsetDeg = 0.f;
	// D8: the section's pitch override and its eased yaw target.
	bool bSectionView = false;
	float SectionPitchDeg = 8.f;
	float SmoothedSectionPitchDeg = 8.f;
	float SectionYawDeg = 0.f;
};
