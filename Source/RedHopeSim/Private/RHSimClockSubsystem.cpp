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
		if (Accumulator >= EraStepSimSeconds)
		{
			Accumulator = 0.f; // drop silently: era mode has no fidelity promise below 1 sim-min
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
		// Anything past the cap is dropped with a warning: the "sim struggling"
		// signal, preferred over a death spiral. Surfaces in UI later.
		if (Accumulator >= SubStepSeconds)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("SimClock frame budget exceeded; dropping %.1f sim-s"), Accumulator);
			Accumulator = 0.f;
		}
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
