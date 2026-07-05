#include "RHSimClockSubsystem.h"
#include "RedHopeSim.h"

void URHSimClockSubsystem::Tick(float DeltaTime)
{
	if (Speed <= 0.f)
	{
		return; // Paused: camera/UI stay live, sim time frozen, uplink countdowns frozen.
	}

	Accumulator += DeltaTime * Speed;
	while (Accumulator >= SubStepSeconds && PendingSubSteps < MaxSubStepsPerFrame)
	{
		Accumulator -= SubStepSeconds;
		++PendingSubSteps;
		SimSecondsTotal += SubStepSeconds;
	}
	// Anything past the cap is dropped with a warning: the "sim struggling"
	// signal, preferred over a death spiral. Surfaces in UI later.
	if (Accumulator >= SubStepSeconds)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("SimClock frame budget exceeded; dropping %.1f sim-s"), Accumulator);
		Accumulator = 0.f;
	}

	const int32 Sol = GetSol();
	if (Sol != LastSol)
	{
		LastSol = Sol;
		OnSolElapsed.Broadcast(Sol);
	}
}

int32 URHSimClockSubsystem::ConsumeSubSteps()
{
	const int32 Steps = PendingSubSteps;
	PendingSubSteps = 0;
	return Steps;
}

void URHSimClockSubsystem::SetSpeed(float NewSpeed)
{
	Speed = FMath::Clamp(NewSpeed, 0.f, 64.f);
	UE_LOG(LogRedHopeSim, Display, TEXT("Sim speed set to %.0fx"), Speed);
}
