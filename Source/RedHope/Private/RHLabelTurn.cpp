#include "RHLabelTurn.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

int32 RHLabelTurn::CameraQuarter(const UWorld* World)
{
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->PlayerCameraManager)
	{
		return 0;
	}
	// The camera's forward on the ground is (cos yaw, sin yaw): yaw 0 looks along +X, so +X is up the screen.
	const float Yaw = PC->PlayerCameraManager->GetCameraRotation().Yaw;
	return ((FMath::RoundToInt32(Yaw / 90.f) % 4) + 4) % 4;
}

FVector2D RHLabelTurn::QuarterUp(int32 Quarter)
{
	switch (((Quarter % 4) + 4) % 4)
	{
	case 0: return FVector2D(1.0, 0.0);
	case 1: return FVector2D(0.0, 1.0);
	case 2: return FVector2D(-1.0, 0.0);
	default: return FVector2D(0.0, -1.0);
	}
}

FRotator RHLabelTurn::FlatRotation(int32 Quarter)
{
	// The layers' own rotation (glyph face up, glyph-up toward +X), then turned about the vertical by the quarter.
	const FQuat Flat = FRotationMatrix::MakeFromXZ(FVector::UpVector, FVector::ForwardVector).ToQuat();
	const FQuat Turn = FRotator(0.0, 90.0 * (double)(((Quarter % 4) + 4) % 4), 0.0).Quaternion();
	return (Turn * Flat).Rotator();
}
