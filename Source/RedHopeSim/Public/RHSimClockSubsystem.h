#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHSimClockSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnSolElapsed, int32 /*NewSol*/);

// The single authoritative timeline. Two integration bands, one clock:
//  - Agent band (speed <= EraSpeedThreshold): real dt x speed accumulates into
//    fixed 0.1 s sub-steps; Mass processors and the sim world consume whole
//    sub-steps, so acceleration changes how fast we watch, never what happens.
//  - Era band (speed > threshold, M1): sub-steps stop publishing (agents park)
//    and the clock publishes whole sim-minutes instead; the sim world's ledger
//    integrator consumes those. Same timeline, coarser integrator.
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
	// Speeds above this are era mode (tier 60). Between the agent tiers and
	// this line nothing is offered in UI; the constant just splits the bands.
	static constexpr float EraSpeedThreshold = 16.f;
	static constexpr float EraStepSimSeconds = 60.f;
	static constexpr int32 MaxEraStepsPerFrame = 4;

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
	// consumed) so multiple sim systems advance the same timeline. Zero while
	// paused or in era mode - parked Mass processors need no special casing.
	int32 GetStepsThisFrame() const { return StepsThisFrame; }
	// Whole era steps (1 sim-minute each) accumulated this frame. Zero in the
	// agent band. Consumed only by the sim world's ledger integrator.
	int32 GetEraStepsThisFrame() const { return EraStepsThisFrame; }
	bool IsEraMode() const { return Speed > EraSpeedThreshold; }

	void SetSpeed(float NewSpeed);
	float GetSpeed() const { return Speed; }
	double GetSimSecondsTotal() const { return SimSecondsTotal; }
	int32 GetSol() const { return static_cast<int32>(SimSecondsTotal / SolLengthSimSeconds); }
	// 0..1 through the current sol; drives sun angle + TimeOfSol MPC scalar.
	float GetSolFraction() const
	{
		return static_cast<float>(FMath::Fmod(SimSecondsTotal, (double)SolLengthSimSeconds) / SolLengthSimSeconds);
	}

	// Headless/save-load drivers only: advance or set the timeline directly
	// (fires sol events; publishes no steps - the caller integrates).
	void Debug_AdvanceSimSeconds(double Seconds);
	void Debug_SetSimSeconds(double Seconds);

	FRHOnSolElapsed OnSolElapsed;

private:
	void BroadcastSolIfElapsed();

	float Speed = 1.f;
	float Accumulator = 0.f;
	double SimSecondsTotal = 0.0;
	int32 StepsThisFrame = 0;
	int32 EraStepsThisFrame = 0;
	int32 LastSol = 0;
};
