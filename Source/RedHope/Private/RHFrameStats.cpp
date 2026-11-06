#include "RHFrameStats.h"
#include "RedHope.h"
#include "Containers/Ticker.h"
#include "HAL/IConsoleManager.h"

namespace RHFrameStats
{
	double GSlotSeconds[SlotCount] = { 0.0, 0.0, 0.0, 0.0 };
	double GMarketWorstRebuildMs = 0.0;
	int32 GMarketRowRebuilds = 0;
	int32 GMarketTextUpdates = 0;
	int32 GMarketOverlayRebuilds = 0;
	double GMarketRebuildTotalMs = 0.0;
	double GPowerLookSeconds = 0.0;
}

// G1 (2026-11-04): the frame check's switch. 0 (default) only zeroes the per-frame total; 1 logs its 2-second median and max.
static TAutoConsoleVariable<int32> CVarPowerLookLog(
	TEXT("rh.PowerLookLog"), 0,
	TEXT("1 = every 2 real seconds log [PowerLook] tick median/max ms: the per-frame game-thread cost of the power readers (G1 frame check)."),
	ECVF_Default);

namespace RHFrameStats
{
	void EnsurePowerLookLogger()
	{
		static bool bRegistered = false;
		if (bRegistered)
		{
			return;
		}
		bRegistered = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			// One sample per frame, kept in one list reserved once; Reset() keeps its allocation, so the sampler allocates nothing per frame.
			static TArray<float> FrameMs;
			static double WindowStartS = 0.0;
			if (FrameMs.Max() == 0)
			{
				FrameMs.Reserve(2048);
			}
			const float Ms = (float)(GPowerLookSeconds * 1000.0);
			GPowerLookSeconds = 0.0;
			if (CVarPowerLookLog.GetValueOnGameThread() == 0)
			{
				FrameMs.Reset();
				WindowStartS = 0.0;
				return true;
			}
			const double Now = FPlatformTime::Seconds();
			if (WindowStartS <= 0.0)
			{
				WindowStartS = Now;
			}
			FrameMs.Add(Ms);
			if (Now - WindowStartS >= 2.0)
			{
				FrameMs.Sort();
				const int32 N = FrameMs.Num();
				UE_LOG(LogRedHope, Display, TEXT("[PowerLook] tick median %.4f ms over %d frame(s), max %.4f ms"), FrameMs[N / 2], N, FrameMs.Last());
				FrameMs.Reset();
				WindowStartS = Now;
			}
			return true;
		}));
	}
}
