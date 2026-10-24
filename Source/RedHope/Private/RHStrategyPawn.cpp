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

void ARHStrategyPawn::BeginPlay()
{
	Super::BeginPlay();
	PlayIntro();
}

void ARHStrategyPawn::PlayIntro()
{
	// Opening shot: ~40 m off the Lander (the origin), looking flatter so the
	// horizon reads, swung 35 degrees round; Tick eases all three home.
	IntroSecondsLeft = 8.f;
	SmoothedZoomT = 0.1f;
	SmoothedPitchOffsetDeg = -12.f;
	IntroYawOffsetDeg = -35.f;
}

void ARHStrategyPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// The opening pull-back runs slow (speed 0.6, ~settled in 7 s); then the
	// usual snappy zoom response resumes. Its clock is clamped to 30 fps steps:
	// the first frames after boot can take seconds each (shader and streaming
	// hitches), and an unclamped ease would land in one jump, unseen.
	const float Dt = IntroSecondsLeft > 0.f ? FMath::Min(DeltaTime, 1.f / 30.f) : DeltaTime;
	const float Speed = IntroSecondsLeft > 0.f ? 0.6f : ZoomInterpSpeed;
	IntroSecondsLeft = FMath::Max(0.f, IntroSecondsLeft - Dt);
	IntroYawOffsetDeg = IntroSecondsLeft > 0.f ? FMath::FInterpTo(IntroYawOffsetDeg, 0.f, Dt, 0.6f) : 0.f;
	SmoothedZoomT = FMath::FInterpTo(SmoothedZoomT, ZoomT, Dt, Speed);
	SmoothedPitchOffsetDeg = FMath::FInterpTo(SmoothedPitchOffsetDeg, PitchOffsetDeg, Dt, Speed);
	// The elevator ride (M1-d): the focus plane descends/ascends smoothly
	// (~1.5 s per floor) instead of cutting - the motion IS the feedback.
	FocusPointCm.Z = FMath::FInterpTo(FocusPointCm.Z, TargetFocusZCm, DeltaTime, 3.f);

	// Exponential distance mapping keeps ground-level zoom fine-grained while
	// orbital sweeps stay fast; linear pitch/FOV are placeholders for CT_CameraRig.
	// The manual tilt offset rides on top of the zoom-coupled pitch; the final
	// clamp keeps the gaze between "just above the horizon" (the mountain ring +
	// sky) and straight down.
	const float Distance = MinDistanceCm * FMath::Pow(MaxDistanceCm / MinDistanceCm, SmoothedZoomT);
	float Pitch = FMath::Clamp(
		FMath::Lerp(MinPitchDeg, MaxPitchDeg, SmoothedZoomT) + SmoothedPitchOffsetDeg, -6.f, 86.f);
	if (bSectionView)
	{
		// D8: the section's own pitch, decoupled from the zoom; the yaw eases to its exact quarter turn.
		SmoothedSectionPitchDeg = FMath::FInterpTo(SmoothedSectionPitchDeg, SectionPitchDeg, DeltaTime, ZoomInterpSpeed);
		Pitch = SmoothedSectionPitchDeg;
		const float Delta = FMath::FindDeltaAngleDegrees(OrbitYawDeg, SectionYawDeg);
		OrbitYawDeg = FMath::Abs(Delta) < 0.05f ? SectionYawDeg : FMath::UnwindDegrees(OrbitYawDeg + Delta * FMath::Min(1.f, DeltaTime * ZoomInterpSpeed));
	}
	const float Fov = FMath::Lerp(GroundFovDeg, OrbitalFovDeg, SmoothedZoomT);

	const FRotator LookRot(-Pitch, OrbitYawDeg + IntroYawOffsetDeg, 0.f);
	const FVector CamPos = FocusPointCm - LookRot.Vector() * Distance;

	SetActorLocation(CamPos);
	Camera->SetWorldRotation(LookRot);
	Camera->SetFieldOfView(Fov);
}

void ARHStrategyPawn::AddZoom(float Delta)
{
	IntroSecondsLeft = 0.f;
	ZoomT = FMath::Clamp(ZoomT + Delta, 0.f, 1.f);
}

void ARHStrategyPawn::AddOrbit(float DeltaYawDeg)
{
	IntroSecondsLeft = 0.f;
	OrbitYawDeg = FMath::UnwindDegrees(OrbitYawDeg + DeltaYawDeg);
}

void ARHStrategyPawn::AddTilt(float DeltaPitchDeg)
{
	IntroSecondsLeft = 0.f;
	if (bSectionView)
	{
		SectionPitchDeg = FMath::Clamp(SectionPitchDeg + DeltaPitchDeg, 5.f, 35.f); // D8: the section's own range
		return;
	}
	// Offset range: enough to drag the ground-register 30-degree pitch below the
	// horizon line (-6 after the Tick clamp) or push it fully top-down.
	PitchOffsetDeg = FMath::Clamp(PitchOffsetDeg + DeltaPitchDeg, -42.f, 30.f);
}

void ARHStrategyPawn::GetCameraPose(FVector2D& OutFocusXYCm, float& OutZoomT, float& OutPitchOffsetDeg, float& OutYawDeg) const
{
	OutFocusXYCm = FVector2D(FocusPointCm.X, FocusPointCm.Y);
	OutZoomT = ZoomT;
	OutPitchOffsetDeg = PitchOffsetDeg;
	OutYawDeg = OrbitYawDeg;
}

void ARHStrategyPawn::SetSectionView(bool bOn, float PitchDeg, float YawDeg)
{
	IntroSecondsLeft = 0.f;
	IntroYawOffsetDeg = 0.f;
	SectionPitchDeg = FMath::Clamp(PitchDeg, 5.f, 35.f);
	SectionYawDeg = FMath::UnwindDegrees(YawDeg);
	if (bOn && !bSectionView)
	{
		SmoothedSectionPitchDeg = SectionPitchDeg; // the entry lands at once; later tilts ease
	}
	bSectionView = bOn;
}

float ARHStrategyPawn::WidthAtZoomCm(float InZoomT) const
{
	// Width at the focus = 2 x Distance(z) x tan(Fov(z) / 2).
	const float D = MinDistanceCm * FMath::Pow(MaxDistanceCm / MinDistanceCm, InZoomT);
	const float Fov = FMath::Lerp(GroundFovDeg, OrbitalFovDeg, InZoomT);
	return 2.f * D * FMath::Tan(FMath::DegreesToRadians(Fov * 0.5f));
}

float ARHStrategyPawn::SolveZoomForWidthCm(float WidthCm) const
{
	// Both distance and FOV rise with z, so the width is monotone and bisection converges.
	float Lo = 0.f, Hi = 1.f;
	for (int32 I = 0; I < 30; ++I)
	{
		const float Mid = 0.5f * (Lo + Hi);
		(WidthAtZoomCm(Mid) < WidthCm ? Lo : Hi) = Mid;
	}
	return Hi;
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
	IntroSecondsLeft = 0.f;
	IntroYawOffsetDeg = 0.f;
}

void ARHStrategyPawn::AddPan(FVector2D PlanarDeltaCm)
{
	IntroSecondsLeft = 0.f;
	const FRotator YawOnly(0.f, OrbitYawDeg, 0.f);
	FocusPointCm += YawOnly.RotateVector(FVector(PlanarDeltaCm.X, PlanarDeltaCm.Y, 0.f));
}
