#pragma once

#include "CoreMinimal.h"
#include "MassEntityTypes.h"
#include "RHAgentFragments.generated.h"

// Robot state lives here as pure data - the fragment layout is the contract
// that survives even if the Mass substrate is swapped for the SoA fallback.

USTRUCT()
struct REDHOPESIM_API FRHBatteryFragment : public FMassFragment
{
	GENERATED_BODY()

	float ChargeWh = 1500.f;
	float CapacityWh = 1500.f;
	float DrawMoveW = 80.f;
};

USTRUCT()
struct REDHOPESIM_API FRHWearFragment : public FMassFragment
{
	GENERATED_BODY()

	float Wear = 0.f; // 0..100; degrades work past 50, halts at 100 (RH_Config)
};

// Scaffold/stress-test behavior: wander between random points. Replaced by
// the task system's MoveTarget in M0 proper.
USTRUCT()
struct REDHOPESIM_API FRHWanderFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector Target = FVector::ZeroVector;
	float SpeedMps = 3.f;
};
