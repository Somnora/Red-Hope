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

	// G1 (2026-11-04), the frame check: game-thread seconds spent this frame in the power readers (the colony visualizer's power pass and
	// room lights, the machine hum, and the shared power look's own tick). A core-ticker sampler moves it into a per-frame list and zeroes
	// it every frame; while rh.PowerLookLog is 1 it logs "[PowerLook] tick median ... ms" every 2 real seconds.
	REDHOPE_API extern double GPowerLookSeconds;
	struct FPowerLookScope
	{
		double T0;
		FPowerLookScope() : T0(FPlatformTime::Seconds()) {}
		~FPowerLookScope() { GPowerLookSeconds += FPlatformTime::Seconds() - T0; }
	};
	// Registers that sampler once (the colony visualizer's OnWorldBeginPlay calls it).
	REDHOPE_API void EnsurePowerLookLogger();
}
