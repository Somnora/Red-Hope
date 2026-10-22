#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"

// D5 depth: depth has a price (docs/qa/2026-10-21/d5-depth-design.md; Q-A (a), Q-B (a), Q-C (a)). The dig-time curve, the
// cooling load, Warmth and its save block. The Hope terms sit in GetColonyHope and the load shedding in StepPower, beside the
// code they extend. Every branch here tests Level < -DepthCostFromFloor, so floors -1..-DepthCostFromFloor run today's paths.

namespace
{
	// The alert's hysteresis (placeholder, Gate-D wording): on at Warmth >= this, off when the floor is fully cool again.
	constexpr double WarmAlertOn = 0.25;
}

void URHSimWorldSubsystem::ApplyMaxDepthRow(int32 Row)
{
	// The light key packs one bit per floor into a uint32, and the save refuses against the same cap.
	MaxDepth = FMath::Clamp(Row, 1, RH::MaxDepthCap);
	if (MaxDepth != Row)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("MaxDepth %d is outside 1..%d - clamped to %d"), Row, RH::MaxDepthCap, MaxDepth);
	}
}

void URHSimWorldSubsystem::ApplyDepthCostRow(int32 Row)
{
	DepthCostFromFloor = FMath::Clamp(Row, 0, RH::MaxDepthCap);
	if (DepthCostFromFloor != Row)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("DepthCostFromFloor %d is outside 0..%d - clamped to %d"), Row, RH::MaxDepthCap, DepthCostFromFloor);
	}
}

int32 URHSimWorldSubsystem::GetDigPermille(int32 Level) const
{
	return 1000 + DigPermillePerDeepFloor * GetFloorsBelowLine(Level);
}

int64 URHSimWorldSubsystem::GetFloorCoolingMilliW(int32 Level) const
{
	if (Level >= -DepthCostFromFloor)
	{
		return 0;
	}
	const FRHFloor* Floor = FindFloor(Level);
	if (!Floor || !IsLevelConnected(Level))
	{
		return 0;
	}
	return (int64)Floor->GetCells().Num() * (int64)GetFloorsBelowLine(Level) * (int64)CoolMilliWPerCellPerDeepFloor;
}

int32 URHSimWorldSubsystem::GetDeepCoolingFloorCount() const
{
	int32 N = 0;
	for (const FRHFloor& Floor : FloorTable)
	{
		N += GetFloorCoolingMilliW(Floor.Level) > 0 ? 1 : 0;
	}
	return N;
}

bool URHSimWorldSubsystem::HasDeepCells() const
{
	for (const FRHFloor& Floor : FloorTable)
	{
		if (Floor.Level < -DepthCostFromFloor && Floor.GetCells().Num() > 0 && IsLevelConnected(Floor.Level))
		{
			return true;
		}
	}
	return false;
}

int32 URHSimWorldSubsystem::GetDeepResidents() const
{
	int32 N = 0;
	for (const FRHColonist& C : Colonists)
	{
		N += C.HomeLevel < -DepthCostFromFloor ? 1 : 0;
	}
	return N;
}

void URHSimWorldSubsystem::StepDepthWarmth(float SubDt, uint32 ShortMask, bool bHasCooling)
{
	if (!bHasCooling && FloorWarmth.Num() == 0)
	{
		return; // no deep floor and nothing warm: nothing to integrate, nothing to allocate
	}
	// A floor the scraping walk marked short warms at 1 / (HeatRiseSols sols); every other deep floor cools at 1 / (HeatFallSols
	// sols). Both are a rate times dt, so the integral is linear in dt between the 0 and 1 clamps (era parity).
	const double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	const double Rise = (double)SubDt / (HeatRiseSols * SolLen);
	const double Fall = (double)SubDt / (HeatFallSols * SolLen);
	for (const FRHFloor& Floor : FloorTable)
	{
		const int32 Level = Floor.Level;
		if (Level >= -DepthCostFromFloor)
		{
			continue;
		}
		const bool bShort = ((ShortMask >> (uint32)(-Level)) & 1u) != 0u;
		const double* Held = FloorWarmth.Find(Level);
		if (!Held && !bShort)
		{
			continue;
		}
		const double Old = Held ? *Held : 0.0;
		const double New = FMath::Clamp(Old + (bShort ? Rise : -Fall), 0.0, 1.0);
		if (FMath::FloorToInt(Old * 10.0) != FMath::FloorToInt(New * 10.0))
		{
			++WarmthVersion; // the floor tint's key: it moves only when a floor crosses a 10% step
		}
		if (New > 0.0)
		{
			FloorWarmth.FindOrAdd(Level) = New;
		}
		else
		{
			FloorWarmth.Remove(Level);
		}
		if (New >= WarmAlertOn && !WarmAlerted.Contains(Level))
		{
			WarmAlerted.Add(Level);
			const FString Alert = FString::Printf(TEXT("Floor %d is running warm: its cooling needs power. More generation or storage brings it back to comfort."), Level); // Gate-D placeholder copy
			UE_LOG(LogRedHopeSim, Display, TEXT("%s"), *Alert);
			OnAlert.Broadcast(Alert);
		}
		else if (New <= 0.0 && WarmAlerted.Remove(Level) > 0)
		{
			const FString Alert = FString::Printf(TEXT("Floor %d is comfortable again."), Level); // Gate-D placeholder copy
			UE_LOG(LogRedHopeSim, Display, TEXT("%s"), *Alert);
			OnAlert.Broadcast(Alert);
		}
	}
}

// --- Save v47: the warmth block, straight after the transit block ---

void URHSimWorldSubsystem::WriteWarmthBlock(FArchive& Ar) const
{
	// WarmCount, then per warm floor its RegionId (0), Level and Warmth, in floor-table order (-6 before -7); only a floor with
	// Warmth > 0 is written. The default colony writes 0: 4 bytes.
	int32 Count = 0;
	for (const FRHFloor& Floor : FloorTable)
	{
		Count += FloorWarmth.Contains(Floor.Level) ? 1 : 0;
	}
	Ar << Count;
	for (const FRHFloor& Floor : FloorTable)
	{
		if (const double* W = FloorWarmth.Find(Floor.Level))
		{
			int32 Region = Floor.RegionId, Level = Floor.Level;
			double Warmth = *W;
			Ar << Region << Level << Warmth;
		}
	}
}

bool URHSimWorldSubsystem::ReadWarmthBlock(FArchive& Ar, const TArray<FRHFloor>& Floors, TArray<TPair<int32, double>>& OutWarmth, FString& OutError) const
{
	// Structural refusals only, taken before the live colony is touched: they set OutError and log nothing at Error level.
	const auto Refuse = [&OutError](const FString& Why) { OutError = FString::Printf(TEXT("Save warmth block invalid: %s"), *Why); return false; };
	OutWarmth.Reset();
	int32 Count = 0;
	Ar << Count;
	if (Ar.IsError() || Count < 0 || Count > RH::MaxDepthCap || (int64)Count * 16 > (int64)Ar.TotalSize() - Ar.Tell())
	{
		return Refuse(FString::Printf(TEXT("warm floor count %d (the range is 0..%d, and each floor takes 16 bytes)"), Count, RH::MaxDepthCap));
	}
	int32 PrevLevel = 0;
	for (int32 n = 0; n < Count; ++n)
	{
		int32 Region = 0, Level = 0;
		double Warmth = 0.0;
		Ar << Region << Level << Warmth;
		if (Ar.IsError())
		{
			return Refuse(TEXT("the list is cut short"));
		}
		if (Region != 0)
		{
			return Refuse(FString::Printf(TEXT("a floor names region %d (only region 0 exists)"), Region));
		}
		if (Level >= 0 || Level < -MaxDepth)
		{
			return Refuse(FString::Printf(TEXT("floor level %d is outside -1..-%d"), Level, MaxDepth));
		}
		bool bLoaded = false;
		for (const FRHFloor& F : Floors)
		{
			bLoaded |= F.Level == Level && F.RegionId == Region;
		}
		if (!bLoaded)
		{
			return Refuse(FString::Printf(TEXT("floor %d is warm but the file has no such floor"), Level));
		}
		if (n > 0 && Level >= PrevLevel)
		{
			return Refuse(FString::Printf(TEXT("floors must run shallowest first, found %d after %d"), Level, PrevLevel));
		}
		PrevLevel = Level;
		if (FMath::IsNaN(Warmth) || !(Warmth > 0.0) || Warmth > 1.0)
		{
			return Refuse(FString::Printf(TEXT("floor %d has warmth %g (the range is above 0 to 1)"), Level, Warmth));
		}
		OutWarmth.Emplace(Level, Warmth);
	}
	return true;
}

void URHSimWorldSubsystem::InstallWarmth(const TArray<TPair<int32, double>>& In)
{
	FloorWarmth.Reset();
	WarmAlerted.Reset();
	for (const TPair<int32, double>& P : In)
	{
		if (P.Key >= -DepthCostFromFloor)
		{
			// The row moved between save and load: this floor is no longer past the line. Dropped, the colony kept.
			UE_LOG(LogRedHopeSim, Warning, TEXT("Load: floor %d carries warmth %.3f but is not past the cut-off (-%d) - the warmth is dropped"), P.Key, P.Value, DepthCostFromFloor);
			continue;
		}
		FloorWarmth.Add(P.Key, P.Value);
		if (P.Value >= WarmAlertOn)
		{
			WarmAlerted.Add(P.Key); // re-armed from the loaded value: no alert fires for a state the player was already told
		}
	}
	++WarmthVersion;
}
