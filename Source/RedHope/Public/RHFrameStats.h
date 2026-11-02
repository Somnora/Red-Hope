#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

// C3 (living-city gate 3): where the game thread's time goes in the visualizers that scale with the city (D8 adds the section's, E8 the market's:
// the MARKET panel's refresh and the Routes overlay), for RH.FrameStats. Each Tick wraps
// itself in an FScope; the cheat reads and clears the per-frame totals once a
// frame. Two clock reads per tick - cheap enough to leave on in every build.
namespace RHFrameStats
{
	enum ESlot : int32 { Crew = 0, Colony = 1, Section = 2, Market = 3, SlotCount = 4 };

	// Game-thread seconds spent in each slot since the cheat last cleared it.
	REDHOPE_API extern double GSlotSeconds[SlotCount];
	// E8: the worst single rebuild (ms) of the MARKET panel or the Routes overlay since the cheat's window started (it resets it). A panel
	// "rebuild" is any refresh pass that did work: one that re-created a section's rows, or one that only rewrote their texts in place.
	REDHOPE_API extern double GMarketWorstRebuildMs;
	// E8 (Rebuild (a)): the window's rebuilds by kind - panel passes that re-created rows, panel passes that only rewrote texts, Routes
	// overlay rebuilds - and their summed cost (ms), so [FrameStats] reports the cost per rebuild beside the worst.
	REDHOPE_API extern int32 GMarketRowRebuilds;
	REDHOPE_API extern int32 GMarketTextUpdates;
	REDHOPE_API extern int32 GMarketOverlayRebuilds;
	REDHOPE_API extern double GMarketRebuildTotalMs;

	struct FScope
	{
		explicit FScope(ESlot InSlot) : Slot(InSlot), T0(FPlatformTime::Seconds()) {}
		~FScope() { GSlotSeconds[Slot] += FPlatformTime::Seconds() - T0; }
		ESlot Slot;
		double T0;
	};
}
