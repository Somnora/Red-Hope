#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHSimClockSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnSolElapsed, int32 /*NewSol*/);

// The single authoritative timeline. Accumulates real dt x speed into fixed
// 0.1 s sim sub-steps; sim systems consume whole sub-steps so acceleration
// changes how fast we watch, never what happens (determinism invariant).
// No global time dilation anywhere - presentation runs at native frame time.
UCLASS()
class REDHOPESIM_API URHSimClockSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static constexpr float SubStepSeconds = 0.1f;
	// From RH_Config SolLengthSimSeconds; hardcoded default until the
	// definition registry loads config at Initialize (scaffold TODO M0).
	static constexpr float SolLengthSimSeconds = 1200.f;
	// Frame-budget guard: cap sub-steps executed per rendered frame so a slow
	// frame can't spiral. 32 = 3.2 sim-s per frame >> 8x at 30 fps needs ~2.7.
	static constexpr int32 MaxSubStepsPerFrame = 32;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHSimClockSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// Whole sub-steps accumulated this rendered frame, published (not
	// consumed) so multiple sim systems advance the same timeline. Readers
	// that tick before the clock in a frame see the previous frame's count -
	// acceptable jitter until the M0-b single-driver refactor.
	int32 GetStepsThisFrame() const { return StepsThisFrame; }

	void SetSpeed(float NewSpeed);
	float GetSpeed() const { return Speed; }
	double GetSimSecondsTotal() const { return SimSecondsTotal; }
	int32 GetSol() const { return static_cast<int32>(SimSecondsTotal / SolLengthSimSeconds); }
	// 0..1 through the current sol; drives sun angle + TimeOfSol MPC scalar.
	float GetSolFraction() const
	{
		return static_cast<float>(FMath::Fmod(SimSecondsTotal, (double)SolLengthSimSeconds) / SolLengthSimSeconds);
	}

	FRHOnSolElapsed OnSolElapsed;

private:
	float Speed = 1.f;
	float Accumulator = 0.f;
	double SimSecondsTotal = 0.0;
	int32 StepsThisFrame = 0;
	int32 LastSol = 0;
};
