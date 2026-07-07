#include "RHSimClockSubsystem.h"
#include "RedHopeSim.h"

void URHSimClockSubsystem::Tick(float DeltaTime)
{
	StepsThisFrame = 0;
	EraStepsThisFrame = 0;
	if (Speed <= 0.f)
	{
		return; // Paused: camera/UI stay live, sim time frozen, uplink countdowns frozen.
	}

	Accumulator += DeltaTime * Speed;

	if (IsEraMode())
	{
		// Era band: publish whole sim-minutes; agent sub-steps stay at zero so
		// Mass processors naturally park. The sim world integrates the ledger.
		while (Accumulator >= EraStepSimSeconds && EraStepsThisFrame < MaxEraStepsPerFrame)
		{
			Accumulator -= EraStepSimSeconds;
			++EraStepsThisFrame;
			SimSecondsTotal += EraStepSimSeconds;
		}
	}
	else
	{
		while (Accumulator >= SubStepSeconds && StepsThisFrame < MaxSubStepsPerFrame)
		{
			Accumulator -= SubStepSeconds;
			++StepsThisFrame;
			SimSecondsTotal += SubStepSeconds;
		}
	}

	// Beyond-cap debt CARRIES across frames instead of being dropped (M1-c
	// era-honesty fix: the era->agent transition used to dump up to a full
	// era step - 43 sim-s observed - and load hitches shaved sub-steps). The
	// sim fast-forwards over the next frames at the per-frame cap. Only a
	// runaway backlog is dropped: that is the true death-spiral guard, and
	// it stays loud.
	if (Accumulator > MaxCarrySimSeconds)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("SimClock backlog %.1f sim-s exceeds carry cap; dropping %.1f sim-s"),
			Accumulator, Accumulator - MaxCarrySimSeconds);
		Accumulator = MaxCarrySimSeconds;
	}

	BroadcastSolIfElapsed();
}

void URHSimClockSubsystem::SetSpeed(float NewSpeed)
{
	Speed = FMath::Clamp(NewSpeed, 0.f, 64.f);
	if (Speed > 0.f && Speed <= EraSpeedThreshold)
	{
		LastAgentSpeed = Speed;
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Sim speed set to %.0fx%s"), Speed, IsEraMode() ? TEXT(" (era mode)") : TEXT(""));
}

void URHSimClockSubsystem::Debug_AdvanceSimSeconds(double Seconds)
{
	SimSecondsTotal += Seconds;
	BroadcastSolIfElapsed();
}

void URHSimClockSubsystem::Debug_SetSimSeconds(double Seconds)
{
	SimSecondsTotal = Seconds;
	Accumulator = 0.f;
	LastSol = GetSol(); // no spurious sol broadcast on load
}

void URHSimClockSubsystem::BroadcastSolIfElapsed()
{
	const int32 Sol = GetSol();
	if (Sol != LastSol)
	{
		LastSol = Sol;
		OnSolElapsed.Broadcast(Sol);
	}
}
