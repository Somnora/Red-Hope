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

	// Exponential distance mapping keeps ground-level zoom fine-grained while
	// orbital sweeps stay fast; linear pitch/FOV are placeholders for CT_CameraRig.
	const float Distance = MinDistanceCm * FMath::Pow(MaxDistanceCm / MinDistanceCm, SmoothedZoomT);
	const float Pitch = FMath::Lerp(MinPitchDeg, MaxPitchDeg, SmoothedZoomT);
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

void ARHStrategyPawn::AddPan(FVector2D PlanarDeltaCm)
{
	const FRotator YawOnly(0.f, OrbitYawDeg, 0.f);
	FocusPointCm += YawOnly.RotateVector(FVector(PlanarDeltaCm.X, PlanarDeltaCm.Y, 0.f));
}
