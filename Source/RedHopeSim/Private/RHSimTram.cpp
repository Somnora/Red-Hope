#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"

// D6d (trams with people, docs/qa/2026-10-22/d6-caverns-design.md MERGED DESIGN AND REVISION section 4, under the director's Q-C (a): trams
// carry PEOPLE ONLY; robots never ride and the robots' deep net has no tram edge). One line is one LayTramLine order on a finished tunnel of a
// colony with a lift car. A line is aggregate: no car is simulated one by one. It is
//  - an edge between its two stops in the people's walk network (RHSimDeepWalk.cpp: WalkTramCm), priced wait-free, so a crowd lengthens trips
//    and never flips a route from sol to sol; the line's frozen WaitSec is added after the route is chosen (DeepTripWaitCm);
//  - a load a sol (2 x the commuters riding it x the commute ramp, D4 item 8) against TramPlacesPerSol, rolled with the lifts' waits in
//    ascending line Id at StepTransit's sol threshold (RollTransit) - the same step at the same index in both spines;
//  - a saved record (save v50), whose floor becomes mapped.
// Nothing here runs in a colony that never lays a line: no field, no key and no loop moves.

int64 URHSimWorldSubsystem::GetTramEdgeCm(const FRHTramLine& L) const
{
	const int64 Speed = FMath::Max(1, TramSpeedCmPerS);
	const int64 RideSec = (GetTramLengthCm(L) + Speed - 1) / Speed; // ceil, in int64
	return ((int64)(TramHeadwaySec / 2) + RideSec) * (int64)CommuteWaitCmPerSec;
}

int32 URHSimWorldSubsystem::GetTramPeople(int32 LineId) const
{
	if (!bHasTransit || TramLines.Num() == 0 || GetCommuteRamp() <= 0.0)
	{
		return 0;
	}
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	for (int32 i = 0; i < TramLines.Num(); ++i)
	{
		if (TramLines[i].Id == LineId)
		{
			return F.TramPeople.IsValidIndex(i) ? F.TramPeople[i] : 0;
		}
	}
	return 0;
}

int64 URHSimWorldSubsystem::GetTramPeopleLoad(int32 LineId) const
{
	// The lifts' rule (GetShaftPeopleLoad): 2 x the line's commuters x the ramp in permille, floored once per line; 0 at 60 people or fewer.
	if (!bHasTransit || TramLines.Num() == 0)
	{
		return 0;
	}
	const double Ramp = GetCommuteRamp();
	if (Ramp <= 0.0)
	{
		return 0;
	}
	const int64 RampPermille = FMath::RoundToInt64(Ramp * 1000.0);
	return 2 * (int64)GetTramPeople(LineId) * RampPermille / 1000;
}

int64 URHSimWorldSubsystem::TramWaitCm(uint32 Lines) const
{
	int64 Cm = 0;
	for (uint32 Bits = Lines; Bits != 0; Bits &= Bits - 1)
	{
		const int32 Index = (int32)FMath::CountTrailingZeros(Bits);
		if (TramLines.IsValidIndex(Index))
		{
			Cm += (int64)TramLines[Index].WaitSec * (int64)CommuteWaitCmPerSec;
		}
	}
	return Cm;
}

// --- The order ---

bool URHSimWorldSubsystem::CanLayTramLine(int32 TunnelId, FString& OutReason, double* OutBillKg) const
{
	const FRHTunnel* T = FindTunnel(TunnelId);
	const int32 Chain = T ? GetTunnelChainLength(T->A, T->B) : 0;
	const double Bill = (double)Chain * TramStructKgPerCell;
	if (OutBillKg)
	{
		*OutBillKg = Bill;
	}
	// 1. The lift car is the one switch of the transit system (D4); a tram is part of it.
	if (!bHasTransit)
	{
		OutReason = TEXT("A tram line needs a lift car first - fit one on a shaft (the lifts and the trams are one transit system)");
		return false;
	}
	// 2. A finished tunnel.
	if (!T)
	{
		OutReason = FString::Printf(TEXT("Unknown tunnel %d"), TunnelId);
		return false;
	}
	if (T->bCancelled)
	{
		OutReason = FString::Printf(TEXT("Tunnel %d was cancelled - a tram line goes on a finished tunnel"), TunnelId);
		return false;
	}
	const int32 Left = GetTunnelUncarved(*T);
	if (Left > 0)
	{
		OutReason = FString::Printf(TEXT("Tunnel %d is not finished - %d cell(s) left to carve"), TunnelId, Left);
		return false;
	}
	// 3. Long enough to be worth a car.
	if (Chain < TramMinCells)
	{
		OutReason = FString::Printf(TEXT("Tunnel %d is %d cells long - a tram line needs at least %d"), TunnelId, Chain, TramMinCells);
		return false;
	}
	// 4. One line a tunnel.
	if (const FRHTramLine* Have = FindTramOnTunnel(TunnelId))
	{
		OutReason = FString::Printf(TEXT("Tunnel %d already has tram line %d"), TunnelId, Have->Id);
		return false;
	}
	// 5. At most MaxTramLines in the colony.
	if (TramLines.Num() >= MaxTramLines)
	{
		OutReason = FString::Printf(TEXT("No more tram lines - %d at most"), MaxTramLines);
		return false;
	}
	// 6. The Struct bill.
	const double Have = GetRoadStructAvailableKg();
	if (Have + 1.0e-6 < Bill)
	{
		OutReason = FString::Printf(TEXT("Insufficient Struct (%.0f kg needed, %.0f on hand)"), Bill, Have);
		return false;
	}
	return true;
}

void URHSimWorldSubsystem::ExecuteLayTramLine(const FRHCommand& Cmd)
{
	// The preflight runs again here, at execute time: the Struct may have gone, or another order laid the last line.
	const double V = Cmd.Value;
	const int32 TunnelId = (FMath::IsNaN(V) || V < 0.0 || V > 1.0e9) ? 0 : FMath::RoundToInt(V);
	FString Reason;
	double Bill = 0.0;
	if (!CanLayTramLine(TunnelId, Reason, &Bill))
	{
		OnCommandRejected.Broadcast(Cmd, Reason);
		return;
	}
	const FRHTunnel* T = FindTunnel(TunnelId);
	if (!TakeStructForBill(Bill, TEXT("tram line")))
	{
		OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Insufficient Struct (%.0f kg needed)"), Bill));
		return;
	}
	const bool bFirst = TramLines.Num() == 0;
	FRHTramLine L;
	L.Id = NextTramId++;
	L.Level = T->Level;
	L.TunnelId = T->Id;
	L.StopA = T->A;
	L.StopB = T->B;
	TramLines.Add(L);
	++TramVersion;
	++DistrictsVersion; // a line maps its floor: the ground maps, the home cells and the commute's deep trips re-derive on first read
	UE_LOG(LogRedHopeSim, Display, TEXT("Tram line %d laid on tunnel %d (floor %d), %d stops apart, %.0f kg Struct paid%s"),
		L.Id, L.TunnelId, L.Level, GetTunnelChainLength(L.StopA, L.StopB), Bill, bFirst ? TEXT(" - the colony's first") : TEXT(""));
	if (bFirst)
	{
		// Gate-D placeholder copy (row N1 of docs/qa/2026-10-15/gate-d-wording.md), said plainly.
		// Each rider takes two places a sol (a line's load is 2 x its commuters x the ramp), so a line carries half its places in people. Laying a line maps its floor, so every
		// commute there is measured by the walk from then on.
		const FString Alert = FString::Printf(TEXT("Tram line %d is running on tunnel %d. People ride it between its two ends; each line carries %d people a sol before a queue forms. Laying a line maps its floor, so commutes there are now measured by walking. Robots keep to the tunnels and the lifts."),
			L.Id, L.TunnelId, TramPlacesPerSol / 2);
		OnAlert.Broadcast(Alert);
	}
}

bool URHSimWorldSubsystem::Debug_LayTramLine(int32 TunnelId, FString& OutReason)
{
	FRHCommand Cmd;
	Cmd.Verb = FName("LayTramLine");
	Cmd.Value = TunnelId;
	if (!CanLayTramLine(TunnelId, OutReason))
	{
		return false;
	}
	ExecuteLayTramLine(Cmd);
	return true;
}

// --- Save v50: the tram block, straight after the floor-shape block ---

void URHSimWorldSubsystem::WriteTramBlock(FArchive& Ar) const
{
	// NextTramId, TramCount, then per line in Id order Id, RegionId, Level, TunnelId, StopA.X, StopA.Y, StopB.X, StopB.Y, WaitSec, PrevWaitSec (an
	// int32 each: 40 bytes a record). An empty list is 8 bytes.
	int32 Next = NextTramId, Count = TramLines.Num();
	Ar << Next << Count;
	for (const FRHTramLine& L : TramLines)
	{
		int32 Id = L.Id, Region = L.RegionId, Level = L.Level, Tunnel = L.TunnelId, Ax = L.StopA.X, Ay = L.StopA.Y, Bx = L.StopB.X, By = L.StopB.Y, Wait = L.WaitSec, Prev = L.PrevWaitSec;
		Ar << Id << Region << Level << Tunnel << Ax << Ay << Bx << By << Wait << Prev;
	}
}

bool URHSimWorldSubsystem::ReadTramBlock(FArchive& Ar, const TArray<FRHFloor>& Floors, const TArray<FRHTunnel>& InTunnels, const FRHLoadedTransit& InTransit, FRHLoadedTram& Out, FString& OutError) const
{
	// Structural refusals only, taken before the live colony is touched: they set OutError and log nothing at Error level.
	const auto Refuse = [&OutError](const FString& Why) { OutError = FString::Printf(TEXT("Save tram list invalid: %s"), *Why); return false; };
	Out = FRHLoadedTram();
	int32 Next = 0, Count = 0;
	Ar << Next << Count;
	if (Ar.IsError() || Next < 1 || Count < 0 || Count > RH::MaxTramLinesCap || Count >= Next || (int64)Count * 40 > (int64)Ar.TotalSize() - Ar.Tell())
	{
		return Refuse(FString::Printf(TEXT("next id %d, tram line count %d (at most %d)"), Next, Count, RH::MaxTramLinesCap));
	}
	if (Count > 0 && InTransit.Cars.Num() == 0)
	{
		return Refuse(FString::Printf(TEXT("%d tram line(s) in a colony with no lift car"), Count));
	}
	const auto FloorOf = [&Floors](int32 Level) -> const FRHFloor*
	{
		for (const FRHFloor& F : Floors) { if (F.Level == Level) { return &F; } }
		return nullptr;
	};
	TSet<int32> Tunnelled;
	int32 PrevId = 0;
	for (int32 n = 0; n < Count; ++n)
	{
		FRHTramLine L;
		int32 Ax = 0, Ay = 0, Bx = 0, By = 0;
		Ar << L.Id << L.RegionId << L.Level << L.TunnelId << Ax << Ay << Bx << By << L.WaitSec << L.PrevWaitSec;
		if (Ar.IsError())
		{
			return Refuse(TEXT("the tram list is cut short"));
		}
		L.StopA = FIntPoint(Ax, Ay);
		L.StopB = FIntPoint(Bx, By);
		if (L.Id < 1 || L.Id <= PrevId || L.Id >= Next)
		{
			return Refuse(FString::Printf(TEXT("tram line ids must ascend and stay below %d, found %d after %d"), Next, L.Id, PrevId));
		}
		PrevId = L.Id;
		if (L.RegionId != 0)
		{
			return Refuse(FString::Printf(TEXT("tram line %d names region %d (only region 0 exists)"), L.Id, L.RegionId));
		}
		const FRHTunnel* T = nullptr;
		for (const FRHTunnel& Each : InTunnels)
		{
			if (Each.Id == L.TunnelId) { T = &Each; break; }
		}
		if (!T || T->Level != L.Level)
		{
			return Refuse(FString::Printf(TEXT("tram line %d is on tunnel %d of floor %d, which the file does not list"), L.Id, L.TunnelId, L.Level));
		}
		if (T->bCancelled)
		{
			return Refuse(FString::Printf(TEXT("tram line %d is on tunnel %d, which was cancelled"), L.Id, L.TunnelId));
		}
		if (L.StopA != T->A || L.StopB != T->B)
		{
			return Refuse(FString::Printf(TEXT("tram line %d stops at (%d,%d) and (%d,%d), not at the ends of tunnel %d"), L.Id, Ax, Ay, Bx, By, L.TunnelId));
		}
		const FRHFloor* Floor = FloorOf(T->Level);
		const int32 Len = GetTunnelChainLength(T->A, T->B);
		for (int32 i = 0; i < Len; ++i)
		{
			if (!Floor || !Floor->IndexOf.Contains(GetTunnelChainPoint(T->A, T->B, i)))
			{
				return Refuse(FString::Printf(TEXT("tram line %d is on tunnel %d, which is not finished"), L.Id, L.TunnelId));
			}
		}
		bool bRepeat = false;
		Tunnelled.Add(L.TunnelId, &bRepeat);
		if (bRepeat)
		{
			return Refuse(FString::Printf(TEXT("tunnel %d has two tram lines"), L.TunnelId));
		}
		if (L.WaitSec < 0 || L.WaitSec > RH::MaxWaitSecCap || L.PrevWaitSec < 0 || L.PrevWaitSec > RH::MaxWaitSecCap)
		{
			return Refuse(FString::Printf(TEXT("tram line %d waits %d s (previous %d s; the range is 0..%d)"), L.Id, L.WaitSec, L.PrevWaitSec, RH::MaxWaitSecCap));
		}
		Out.Lines.Add(L);
	}
	Out.NextId = Next;
	return true;
}
