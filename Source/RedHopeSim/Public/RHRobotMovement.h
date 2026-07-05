#pragma once

#include "CoreMinimal.h"

// Ground movement shared by the legacy task processor and the StateTree
// tasks. One implementation so the Gate B parity bar ("same colony outcome")
// compares decisions, never integration math.
namespace RH
{
	constexpr float ArriveDistCm = 300.f;

	// Move toward target on the plain; returns true when arrived.
	inline bool MoveToward(FTransform& Transform, const FVector& TargetCm, float SpeedMps, float Dt)
	{
		FVector Pos = Transform.GetLocation();
		const FVector To = TargetCm - Pos;
		const float DistCm = To.Size2D();
		if (DistCm <= ArriveDistCm)
		{
			return true;
		}
		Pos += To.GetSafeNormal2D() * FMath::Min(SpeedMps * 100.f * Dt, DistCm);
		Transform.SetLocation(Pos);
		Transform.SetRotation(FRotationMatrix::MakeFromX(To).ToQuat());
		return false;
	}
}
