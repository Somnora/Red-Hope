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
	// C6a: bRover drives it as a cargo rover - the road at its speed, everything else (the straight
	// line with no road, both end legs, a missing edge) at the rover's off-road crawl.
	// D4: TargetLevel is the floor the target is on (0 = the surface) and CargoKg what the robot carries (a ride charges
	// the shaft's ledger its places). With a fitted lift, a robot (never a rover) that is below or bound below drives the
	// deep network (URHSimWorldSubsystem::MoveAlongDeep); every other call runs the body above verbatim.
	bool MoveAlong(URHSimWorldSubsystem& Sim, FMassEntityHandle Entity, FTransform& Transform, const FVector& TargetCm, float SpeedMps, float Dt, bool bRover = false, float CargoKg = 0.f, int32 TargetLevel = 0);

	// D4 (O3): a robot's floor is read from its Z, rounded, never truncated: Z is written only when a ride completes, as
	// Level x FloorHeightCm + RobotZOffsetCm, and every robot spawns at the offset (level 0).
	constexpr double RobotZOffsetCm = 50.0;
	inline int32 LevelOfZ(double ZCm, double FloorHeightCm) { return FMath::RoundToInt((ZCm - RobotZOffsetCm) / FloorHeightCm); }

	// D4: the end of a robot's sub-step Step of a Steps-long frame ending at FrameEnd - its transit stamp (REVISION item 2(a)).
	// One formula for both processors and the -transit suite's batched-frame driver, so TR5 tests the stamp the game uses.
	inline double RobotSubStepEnd(double FrameEnd, int32 Steps, int32 Step, float SubDt)
	{
		return FrameEnd - (double)(Steps - 1 - Step) * (double)SubDt;
	}
}
