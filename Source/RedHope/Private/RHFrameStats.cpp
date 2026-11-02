#include "RHFrameStats.h"

namespace RHFrameStats
{
	double GSlotSeconds[SlotCount] = { 0.0, 0.0, 0.0, 0.0 };
	double GMarketWorstRebuildMs = 0.0;
	int32 GMarketRowRebuilds = 0;
	int32 GMarketTextUpdates = 0;
	int32 GMarketOverlayRebuilds = 0;
	double GMarketRebuildTotalMs = 0.0;
}
