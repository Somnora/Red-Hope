#pragma once

#include "CoreMinimal.h"

class UWorld;

// UI polish P6: the flat world labels (the cavern, tunnel, district and growth layers) lay their text on the ground with the tops of the
// letters toward world +X. That reads at the opening yaw and sideways or upside down after a turn of the camera. Every layer turns its labels
// with this one helper: the camera's yaw snapped to a quarter turn, so a label is re-aimed only when the quarter changes, never every frame.
namespace RHLabelTurn
{
	// The camera's yaw snapped to a quarter turn: 0..3, in steps of 90 degrees. Quarter 0 is the opening yaw, where world +X runs up the
	// screen (and +Y to the right). A world with no camera reads as 0.
	REDHOPE_API int32 CameraQuarter(const UWorld* World);

	// Up the screen along the ground at a quarter: (1, 0), (0, 1), (-1, 0), (0, -1). It is also the direction the tops of the letters point.
	REDHOPE_API FVector2D QuarterUp(int32 Quarter);

	// The rotation of a flat label (its face up) whose letter tops point up the screen at a quarter. Quarter 0 is the rotation the layers
	// always used.
	REDHOPE_API FRotator FlatRotation(int32 Quarter);
}
