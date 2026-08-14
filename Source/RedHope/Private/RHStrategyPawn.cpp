#include "RHStrategyPawn.h"
#include "Camera/CameraComponent.h"

ARHStrategyPawn::ARHStrategyPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
}

void ARHStrategyPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SmoothedZoomT = FMath::FInterpTo(SmoothedZoomT, ZoomT, DeltaTime, ZoomInterpSpeed);
	SmoothedPitchOffsetDeg = FMath::FInterpTo(SmoothedPitchOffsetDeg, PitchOffsetDeg, DeltaTime, ZoomInterpSpeed);
	// The elevator ride (M1-d): the focus plane descends/ascends smoothly
	// (~1.5 s per floor) instead of cutting - the motion IS the feedback.
	FocusPointCm.Z = FMath::FInterpTo(FocusPointCm.Z, TargetFocusZCm, DeltaTime, 3.f);

	// Exponential distance mapping keeps ground-level zoom fine-grained while
	// orbital sweeps stay fast; linear pitch/FOV are placeholders for CT_CameraRig.
	// The manual tilt offset rides on top of the zoom-coupled pitch; the final
	// clamp keeps the gaze between "just above the horizon" (the mountain ring +
	// sky) and straight down.
	const float Distance = MinDistanceCm * FMath::Pow(MaxDistanceCm / MinDistanceCm, SmoothedZoomT);
	const float Pitch = FMath::Clamp(
		FMath::Lerp(MinPitchDeg, MaxPitchDeg, SmoothedZoomT) + SmoothedPitchOffsetDeg, -6.f, 86.f);
	const float Fov = FMath::Lerp(GroundFovDeg, OrbitalFovDeg, SmoothedZoomT);

	const FRotator LookRot(-Pitch, OrbitYawDeg, 0.f);
	const FVector CamPos = FocusPointCm - LookRot.Vector() * Distance;

	SetActorLocation(CamPos);
	Camera->SetWorldRotation(LookRot);
	Camera->SetFieldOfView(Fov);
}

void ARHStrategyPawn::AddZoom(float Delta)
{
	ZoomT = FMath::Clamp(ZoomT + Delta, 0.f, 1.f);
}

void ARHStrategyPawn::AddOrbit(float DeltaYawDeg)
{
	OrbitYawDeg = FMath::UnwindDegrees(OrbitYawDeg + DeltaYawDeg);
}

void ARHStrategyPawn::AddTilt(float DeltaPitchDeg)
{
	// Offset range: enough to drag the ground-register 30-degree pitch below the
	// horizon line (-6 after the Tick clamp) or push it fully top-down.
	PitchOffsetDeg = FMath::Clamp(PitchOffsetDeg + DeltaPitchDeg, -42.f, 30.f);
}

void ARHStrategyPawn::SetCameraPose(FVector2D FocusXYCm, float InZoomT, float InPitchOffsetDeg, float InYawDeg)
{
	FocusPointCm.X = FocusXYCm.X;
	FocusPointCm.Y = FocusXYCm.Y;
	ZoomT = FMath::Clamp(InZoomT, 0.f, 1.f);
	PitchOffsetDeg = FMath::Clamp(InPitchOffsetDeg, -42.f, 30.f);
	OrbitYawDeg = FMath::UnwindDegrees(InYawDeg);
	// Tick reads the smoothed mirrors, so collapsing them here is what makes
	// the pose land immediately instead of a half-second later.
	SmoothedZoomT = ZoomT;
	SmoothedPitchOffsetDeg = PitchOffsetDeg;
}

void ARHStrategyPawn::AddPan(FVector2D PlanarDeltaCm)
{
	const FRotator YawOnly(0.f, OrbitYawDeg, 0.f);
	FocusPointCm += YawOnly.RotateVector(FVector(PlanarDeltaCm.X, PlanarDeltaCm.Y, 0.f));
}
