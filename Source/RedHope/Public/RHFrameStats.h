#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

// C3 (living-city gate 3): where the game thread's time goes in the visualizers that scale with the city (D8 adds the section's), for RH.FrameStats. Each Tick wraps
// itself in an FScope; the cheat reads and clears the per-frame totals once a
// frame. Two clock reads per tick - cheap enough to leave on in every build.
namespace RHFrameStats
{
	enum ESlot : int32 { Crew = 0, Colony = 1, Section = 2, SlotCount = 3 };

	// Game-thread seconds spent in each slot since the cheat last cleared it.
	REDHOPE_API extern double GSlotSeconds[SlotCount];

	struct FScope
	{
		explicit FScope(ESlot InSlot) : Slot(InSlot), T0(FPlatformTime::Seconds()) {}
		~FScope() { GSlotSeconds[Slot] += FPlatformTime::Seconds() - T0; }
		ESlot Slot;
		double T0;
	};
}
