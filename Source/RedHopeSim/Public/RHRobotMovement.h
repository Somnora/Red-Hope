#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"

class URHSimWorldSubsystem;

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

	// C4: the one movement call BOTH brains use for every drive but Repair's (whose
	// target moves every step). With no built road it is MoveToward, call for call,
	// so a colony without roads moves exactly as it always did. With roads it plans
	// the trip once per target (straight to an entry node, along the graph, straight
	// from an exit node) and drives it, carrying the step's leftover time across
	// waypoints. True on arrival.
	bool MoveAlong(URHSimWorldSubsystem& Sim, FMassEntityHandle Entity, FTransform& Transform, const FVector& TargetCm, float SpeedMps, float Dt);
}
