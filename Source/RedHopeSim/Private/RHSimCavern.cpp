#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"

// D6a caverns (docs/qa/2026-10-22/d6-caverns-design.md, MERGED DESIGN AND REVISION section 1; director answers Q-A (a), Q-B (a), Q-C (a)).
// A cavern is one DigCavern order: a lattice box on 1..CavernMaxFloors consecutive floors that the Borers carve cell by cell with the
// ordinary CarveCell recipe. Its cells are ordinary cells of each floor's table, appended through CarveLatticePoint (add-only, no new
// level, no new key). The Borer loop's branch sits in RHSimWorldSubsystem.cpp beside the tunnel branch, and every branch here tests an
// open cavern or a work record with X >= 100, so with no cavern nothing runs.

namespace
{
	const FIntPoint Neighbours4[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
}

int32 URHSimWorldSubsystem::GetCavernUncarved(const FRHCavern& C, int32 Level) const
{
	if (C.bFinished || !C.ContainsLevel(Level))
	{
		return 0;
	}
	if (!(C.bCounted && C.CountedAt == FloorCellsVersion))
	{
		// One count per FloorCellsVersion: no cell was carved anywhere since the last one. The box is at most 8 x 8 x 3 points.
		C.LeftOnFloor.Init(0, C.GetFloors());
		int32 Total = 0;
		for (int32 L = C.Top; L >= C.Bottom; --L)
		{
			const FRHFloor* Floor = FindFloor(L);
			int32 Left = 0;
			for (int32 X = C.Min.X; X <= C.Max.X; ++X)
			{
				for (int32 Y = C.Min.Y; Y <= C.Max.Y; ++Y)
				{
					if (!Floor || !Floor->IndexOf.Contains(FIntPoint(X, Y)))
					{
						++Left;
					}
				}
			}
			C.LeftOnFloor[C.Top - L] = Left;
			Total += Left;
		}
		C.bCounted = true;
		C.CountedAt = FloorCellsVersion;
		if (Total == 0)
		{
			C.bFinished = true; // cells are never removed, so a finished box stays finished
		}
	}
	return C.LeftOnFloor.IsValidIndex(C.Top - Level) ? C.LeftOnFloor[C.Top - Level] : 0;
}

int32 URHSimWorldSubsystem::GetCavernUncarved(const FRHCavern& C) const
{
	int32 Total = 0;
	for (int32 L = C.Top; L >= C.Bottom; --L)
	{
		Total += GetCavernUncarved(C, L);
	}
	return Total;
}

int32 URHSimWorldSubsystem::GetOpenCavernCount() const
{
	int32 N = 0;
	for (const FRHCavern& C : Caverns)
	{
		N += IsCavernOpen(C) ? 1 : 0;
	}
	return N;
}

int32 URHSimWorldSubsystem::GetCavernBatchesInFlight(int32 CavernId) const
{
	int32 N = 0;
	for (const auto& W : PendingBoreWork)
	{
		N += (RH::IsCavernWork(W.Value) && W.Value.Y == CavernId) ? 1 : 0;
	}
	return N;
}

int32 URHSimWorldSubsystem::GetCavernBatchesInFlightOn(int32 CavernId, int32 Level) const
{
	int32 N = 0;
	for (const auto& W : PendingBoreWork)
	{
		N += (RH::IsCavernWork(W.Value) && W.Value.Y == CavernId && W.Value.X == RH::CavernWorkBase - Level) ? 1 : 0;
	}
	return N;
}

bool URHSimWorldSubsystem::IsCavernPointEligible(int32 Level, const FIntPoint& P) const
{
	const FRHFloor* Floor = FindFloor(Level);
	if ((Floor && Floor->IndexOf.Contains(P)) || ShaftColumns.Contains(P))
	{
		return false;
	}
	for (const FIntPoint& D : Neighbours4)
	{
		const FIntPoint Q = P + D;
		if (Floor && Floor->IndexOf.Contains(Q) && (Atriums.Num() == 0 || !IsPointOpen(Level, Q)))
		{
			return true; // D7: a foothold is a carved NON-open cell (this also covers D6b growth's eligibility); inert with no atrium
		}
		if (ShaftColumns.Contains(Q))
		{
			for (const FRHShaft& Sh : Shafts)
			{
				if (Sh.Column == Q && ShaftReaches(Sh.Id, Level))
				{
					return true;
				}
			}
		}
	}
	return false;
}

bool URHSimWorldSubsystem::FindCavernPoint(const FRHCavern& C, int32 Level, FIntPoint& OutPoint) const
{
	if (!C.ContainsLevel(Level))
	{
		return false;
	}
	for (int32 X = C.Min.X; X <= C.Max.X; ++X)
	{
		for (int32 Y = C.Min.Y; Y <= C.Max.Y; ++Y)
		{
			if (IsCavernPointEligible(Level, FIntPoint(X, Y)))
			{
				OutPoint = FIntPoint(X, Y);
				return true;
			}
		}
	}
	return false;
}

const FRHTunnel* URHSimWorldSubsystem::FindLiveTunnelOnFloorPoint(int32 Level, const FIntPoint& P, int32 ExceptId) const
{
	// "Live" as FindLiveTunnelOnPoint: open, or cancelled with a batch in flight that may yet carve its next cell. Floor-specific.
	for (const FRHTunnel& T : Tunnels)
	{
		if (T.Id == ExceptId || T.Level != Level || !IsOnTunnelChain(T.A, T.B, P))
		{
			continue;
		}
		const FRHFloor* Floor = FindFloor(Level);
		if (Floor && Floor->IndexOf.Contains(P))
		{
			continue; // already carved: no longer a point it still has to carve
		}
		if (IsTunnelOpen(T) || (GetTunnelUncarved(T) > 0 && GetTunnelBatchesInFlight(T.Id) > 0))
		{
			return &T;
		}
	}
	return nullptr;
}

const FRHCavern* URHSimWorldSubsystem::FindLiveCavernOnFloorPoint(int32 Level, const FIntPoint& P, int32 ExceptId) const
{
	for (const FRHCavern& C : Caverns)
	{
		if (C.Id == ExceptId || !C.ContainsLevel(Level) || !C.ContainsPoint(P))
		{
			continue;
		}
		const FRHFloor* Floor = FindFloor(Level);
		if (Floor && Floor->IndexOf.Contains(P))
		{
			continue;
		}
		if (IsCavernOpen(C) || (GetCavernUncarved(C) > 0 && GetCavernBatchesInFlight(C.Id) > 0))
		{
			return &C;
		}
	}
	return nullptr;
}

const FRHCavern* URHSimWorldSubsystem::FindLiveCavernOnColumn(const FIntPoint& Column) const
{
	for (const FRHCavern& C : Caverns)
	{
		if (C.ContainsPoint(Column) && GetCavernUncarved(C) > 0 && (!C.bCancelled || GetCavernBatchesInFlight(C.Id) > 0))
		{
			return &C;
		}
	}
	return nullptr;
}

bool URHSimWorldSubsystem::CarveLatticePoint(int32 Level, const FIntPoint& P)
{
	// RC 3. Refuses before anything is touched: a floor that has no table yet is not created for a refused point.
	if (Level >= 0 || ShaftColumns.Contains(P) || FindFloorCell(Level, P) != INDEX_NONE)
	{
		return false;
	}
	FRHFloor& Floor = FindOrAddFloor(Level);
	Floor.Plan.Remove(P); // a painted point that is carved here leaves the plan (a carved point in the plan is a refused save)
	AppendCarvedCell(Floor, P);
	return true;
}

bool URHSimWorldSubsystem::FindCavernWork(int32 WorkShaft, FIntPoint& OutItem) const
{
	// Open caverns by Id whose work shaft is WorkShaft, on the shallowest floor that has an eligible point and more uncarved points than
	// batches in flight. A cavern starts a batch only while its uncarved points outnumber its batches in flight on that floor.
	for (const FRHCavern& C : Caverns)
	{
		if (C.bCancelled || C.ShaftId != WorkShaft)
		{
			continue;
		}
		for (int32 L = C.Top; L >= C.Bottom; --L)
		{
			if (!ShaftReaches(WorkShaft, L))
			{
				continue;
			}
			const int32 Left = GetCavernUncarved(C, L);
			if (Left == 0 || Left <= GetCavernBatchesInFlightOn(C.Id, L))
			{
				continue;
			}
			FIntPoint P;
			if (FindCavernPoint(C, L, P))
			{
				OutItem = FIntPoint(RH::CavernWorkBase - L, C.Id);
				return true;
			}
		}
	}
	return false;
}

void URHSimWorldSubsystem::CompleteCavernCell(int32 CavernId, int32 Level, int32 BorerId)
{
	// A finished cavern batch: its spoil already dropped at the Borer with the batch's outputs. The cell is the first eligible box point
	// of the batch's floor, read NOW (the floor is fixed at batch start, the point is not: a spiral, a plan point, a tunnel or another
	// batch may have carved it). Nothing to carve, a record the file no longer lists, or a floor outside its box: spoil, no cell.
	++CavernBatchesLanded;
	const FRHCavern* C = FindCavern(CavernId);
	if (!C || !C->ContainsLevel(Level))
	{
		++CavernBatchesEmpty;
		UE_LOG(LogRedHopeSim, Warning, TEXT("Borer #%d: cavern %d (floor %d) is no longer listed or has no such floor - spoil, no cell"), BorerId, CavernId, Level);
		return;
	}
	FIntPoint P;
	if (!FindCavernPoint(*C, Level, P) || !CarveLatticePoint(Level, P))
	{
		++CavernBatchesEmpty;
		UE_LOG(LogRedHopeSim, Display, TEXT("Borer #%d: cavern %d has no cell left to carve on floor %d - spoil, no cell"), BorerId, CavernId, Level);
		return;
	}
	++C->CellsCarved;
	const int32 Left = GetCavernUncarved(*C);
	if (Left == 0)
	{
		// Nit 20: the cells its Borers carved, beside the box's points (a box can take in points that were dug before it was ordered). The tally is
		// derived and never saved, so after a load it counts from the load.
		UE_LOG(LogRedHopeSim, Display, TEXT("Cavern %d carved cell (%d, %d) on floor %d - cavern complete (%d points in its box, %d cell(s) carved by its Borers since it was ordered or the colony loaded)"),
			CavernId, P.X, P.Y, Level, C->GetSizeX() * C->GetSizeY() * C->GetFloors(), C->CellsCarved);
	}
	else
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("Cavern %d carved cell (%d, %d) on floor %d (%d left)"), CavernId, P.X, P.Y, Level, Left);
	}
}

int32 URHSimWorldSubsystem::Debug_CarveCavernCells(int32 CavernId, int32 N)
{
	const FRHCavern* C = FindCavern(CavernId);
	if (!C)
	{
		return 0;
	}
	int32 Done = 0;
	for (; Done < N; ++Done)
	{
		int32 Floor = 0;
		FIntPoint P;
		for (int32 L = C->Top; L >= C->Bottom && Floor == 0; --L)
		{
			if (FindCavernPoint(*C, L, P))
			{
				Floor = L;
			}
		}
		if (Floor == 0)
		{
			break;
		}
		CompleteCavernCell(CavernId, Floor, 0);
		SpoilPileKg += SpoilKgPerCell;
	}
	return Done;
}

bool URHSimWorldSubsystem::CanDigCavern(int32 Top, int32 Floors, const FVector& ACm, const FVector& BCm, FString& OutReason, FRHCavernPreview* OutPreview) const
{
	// Refusals 1-7 of the D6 merged design, each with a reason (7 is taken before 5, so a box that is all carved says so).
	// Step-2 fix 3, taken first so the reason is the same whatever else is wrong with the box: every order stays on the list for ever (finished and
	// cancelled ones too, D7 reads a finished cavern's record) and one save holds MaxCavernRecords, so the order past the last would write a file
	// that refuses to load.
	if (Caverns.Num() >= RH::MaxCavernRecords)
	{
		OutReason = FString::Printf(TEXT("The colony has ordered %d caverns, the most one save can hold"), Caverns.Num());
		return false;
	}
	const int64 Bottom64 = (int64)Top - (int64)Floors + 1;
	// 1. The deepest floor must be reached by some shaft (every shaft starts at the surface, so the floors above it are reached too).
	if (Top >= 0)
	{
		OutReason = FString::Printf(TEXT("Floor %d is not reached - bore a shaft that deep first"), Top);
		return false;
	}
	if (Floors >= 1 && Bottom64 < -(int64)MaxDepth)
	{
		// Nit 18: no shaft can ever be bored below -MaxDepth, so "bore a shaft that deep first" would send the player after the impossible.
		OutReason = FString::Printf(TEXT("A cavern cannot go below floor -%d (this box would reach floor %lld)"), MaxDepth, Bottom64);
		return false;
	}
	if (Floors >= 1 && DeepestShaft < -Bottom64)
	{
		OutReason = FString::Printf(TEXT("Floor %lld is not reached - bore a shaft that deep first"), Bottom64);
		return false;
	}
	// 2. Size: 1..CavernMaxFloors floors, a side of at most CavernMaxSide cells (checked in int64: the order carries arbitrary points).
	if (Floors < 1 || Floors > RH::CavernMaxFloors)
	{
		OutReason = FString::Printf(TEXT("A cavern spans 1 to %d floors (this one would span %d)"), RH::CavernMaxFloors, Floors);
		return false;
	}
	const FVector Origin = Shafts[0].HeadCm;
	const FIntPoint A = CmToLattice(Origin, ACm), B = CmToLattice(Origin, BCm);
	// Step-2 fix 5: a corner past the lattice bound is a refusal before any loop runs (the side test below holds in int64 but bounds nothing, and a
	// box at the int32 edge would overflow its loops and ExecuteDigCavern's midpoint).
	if (!RH::IsLatticeInRange(A) || !RH::IsLatticeInRange(B))
	{
		OutReason = TEXT("That box is out of range");
		return false;
	}
	const int64 SideX = (int64)FMath::Abs((int64)B.X - A.X) + 1, SideY = (int64)FMath::Abs((int64)B.Y - A.Y) + 1;
	if (SideX > RH::CavernMaxSide || SideY > RH::CavernMaxSide)
	{
		OutReason = FString::Printf(TEXT("A cavern is at most %d x %d cells (this one would be %lld x %lld)"), RH::CavernMaxSide, RH::CavernMaxSide, SideX, SideY);
		return false;
	}
	const int32 Bottom = (int32)Bottom64;
	const FIntPoint Min(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), Max(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
	int32 Points = 0, Uncarved = 0;
	double Hours = 0.0;
	const FRHRecipeRow* CarveRow = Defs ? Defs->GetRecipe(FName("CarveCell")) : nullptr;
	const double HoursPerCell = CarveRow ? (double)CarveRow->BatchTime_h : 3.0;
	const double SpoilPerCell = CarveRow ? URHDefinitionsSubsystem::ParseResourceList(CarveRow->Outputs).FindRef(FName("Regolith")) : SpoilKgPerCell;
	for (int32 L = Top; L >= Bottom; --L)
	{
		const FRHFloor* Floor = FindFloor(L);
		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				const FIntPoint P(X, Y);
				// 3. Never over a shaft column (O9): a column runs through every floor.
				if (ShaftColumns.Contains(P))
				{
					OutReason = FString::Printf(TEXT("A shaft stands in the box at (%d, %d)"), P.X, P.Y);
					return false;
				}
				++Points;
				if (Floor && Floor->IndexOf.Contains(P))
				{
					continue; // an already dug point is skipped, never re-added or moved
				}
				++Uncarved;
				// 4. Never over another open tunnel's or cavern's uncarved point.
				if (const FRHTunnel* Held = FindLiveTunnelOnFloorPoint(L, P))
				{
					OutReason = FString::Printf(TEXT("The box crosses tunnel #%d at (%d, %d) on floor %d"), Held->Id, P.X, P.Y, L);
					return false;
				}
				if (const FRHCavern* Other = FindLiveCavernOnFloorPoint(L, P))
				{
					OutReason = FString::Printf(TEXT("The box overlaps cavern #%d at (%d, %d) on floor %d"), Other->Id, P.X, P.Y, L);
					return false;
				}
			}
		}
	}
	int32 UncarvedOn[RH::CavernMaxFloors] = {}; // step-2 fix 8: each floor's uncarved box points, index Top - L (Floors <= CavernMaxFloors was checked above)
	for (int32 L = Top; L >= Bottom; --L)
	{
		// The batch hours of each floor's cells, at that floor's own dig time (D5): a cavern past -5 costs more per cell.
		int32 Here = 0;
		const FRHFloor* Floor = FindFloor(L);
		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				Here += (Floor && Floor->IndexOf.Contains(FIntPoint(X, Y))) ? 0 : 1;
			}
		}
		UncarvedOn[Top - L] = Here;
		Hours += (double)Here * HoursPerCell * (double)GetDigPermille(L) / 1000.0;
	}
	// 7. Something must be left to dig.
	if (Uncarved == 0)
	{
		OutReason = TEXT("Every point of that box is already dug");
		return false;
	}
	// 5. One seed per floor: on EVERY floor that still has rock to dig in the box, some uncarved box point must be eligible. A reaching shaft's column
	// beside the box satisfies every floor at once, because the column runs through all of them. There is no vertical rule (RC 2). Step-2 fix 8 (the
	// director's answer to refusal 5): a floor with no rock left inside the box needs no seed, so a cavern may reach down through floors already dug
	// out there (the Borers skip such a floor, FindCavernWork); a box dug through on every floor was refused by 7 above.
	for (int32 L = Top; L >= Bottom; --L)
	{
		if (UncarvedOn[Top - L] == 0)
		{
			continue;
		}
		bool bSeed = false;
		for (int32 X = Min.X; X <= Max.X && !bSeed; ++X)
		{
			for (int32 Y = Min.Y; Y <= Max.Y && !bSeed; ++Y)
			{
				bSeed = IsCavernPointEligible(L, FIntPoint(X, Y));
			}
		}
		if (!bSeed)
		{
			OutReason = FString::Printf(TEXT("On floor %d the box touches no dig and no shaft - a cavern opens off a dig or a shaft on every floor"), L);
			return false;
		}
	}
	// 6. At most MaxOpenCaverns open at once.
	if (GetOpenCavernCount() >= RH::MaxOpenCaverns)
	{
		OutReason = FString::Printf(TEXT("No more open caverns - %d at a time"), RH::MaxOpenCaverns);
		return false;
	}
	if (OutPreview)
	{
		OutPreview->Min = Min;
		OutPreview->Max = Max;
		OutPreview->Top = Top;
		OutPreview->Bottom = Bottom;
		OutPreview->Cells = Uncarved;
		OutPreview->Points = Points;
		OutPreview->BorerHours = Hours;
		OutPreview->SpoilKg = (double)Uncarved * SpoilPerCell;
		OutPreview->O2Kg = (double)Uncarved * O2FillKgPerCell;
	}
	return true;
}

void URHSimWorldSubsystem::ExecuteDigCavern(const FRHCommand& Cmd)
{
	// The preflight runs here, at execute time, because the box can be dug (or a shaft sunk in it) while the order is in flight.
	const double V = Cmd.Value;
	const int32 Floors = (FMath::IsNaN(V) || V < 1.0) ? 0 : (V > 1000.0 ? 1000 : FMath::RoundToInt(V));
	FString Reason;
	FRHCavernPreview Pre;
	if (!CanDigCavern(Cmd.Level, Floors, Cmd.Location, Cmd.LocationB, Reason, &Pre))
	{
		OnCommandRejected.Broadcast(Cmd, Reason);
		return;
	}
	FRHCavern C;
	C.Id = NextCavernId++;
	C.Top = Pre.Top;
	C.Bottom = Pre.Bottom;
	C.Min = Pre.Min;
	C.Max = Pre.Max;
	// The shaft is fixed now: the nearest one that reaches the deepest floor, ties to the lower Id. A later SinkShaft cannot move the work.
	C.ShaftId = FMath::Max(0, NearestReachingShaft(C.Bottom, FIntPoint((C.Min.X + C.Max.X) / 2, (C.Min.Y + C.Max.Y) / 2)));
	Caverns.Add(C);
	++CavernsVersion;
	UE_LOG(LogRedHopeSim, Display, TEXT("Cavern %d ordered on floors %d..%d: (%d, %d) to (%d, %d), %d cells to carve, worked from shaft %d"),
		C.Id, C.Top, C.Bottom, C.Min.X, C.Min.Y, C.Max.X, C.Max.Y, Pre.Cells, C.ShaftId);
}

void URHSimWorldSubsystem::ExecuteCancelCavern(const FRHCommand& Cmd)
{
	const int32 Id = FMath::RoundToInt(Cmd.Value);
	FRHCavern* C = nullptr;
	for (FRHCavern& Each : Caverns)
	{
		if (Each.Id == Id) { C = &Each; break; }
	}
	if (!C)
	{
		OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Unknown cavern #%d"), Id));
		return;
	}
	if (C->bCancelled)
	{
		OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Cavern #%d is already cancelled"), Id));
		return;
	}
	if (GetCavernUncarved(*C) == 0)
	{
		OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Cavern #%d is already finished"), Id));
		return;
	}
	C->bCancelled = true;
	++CavernsVersion;
	UE_LOG(LogRedHopeSim, Display, TEXT("Cavern %d cancelled with %d cell(s) uncarved (%d batch(es) in flight will land)"),
		Id, GetCavernUncarved(*C), GetCavernBatchesInFlight(Id));
}

int32 URHSimWorldSubsystem::Debug_PadCavernRecords(int32 N)
{
	int32 Added = 0;
	for (; Added < N && Caverns.Num() < RH::MaxCavernRecords; ++Added)
	{
		FRHCavern C;
		C.Id = NextCavernId++;
		C.Top = -1;
		C.Bottom = -1;
		C.Min = FIntPoint(1, 0);
		C.Max = FIntPoint(1, 0);
		C.ShaftId = 0;
		C.bCancelled = true;
		Caverns.Add(C);
	}
	if (Added > 0)
	{
		++CavernsVersion;
	}
	return Added;
}

bool URHSimWorldSubsystem::Debug_DigCavern(int32 Top, int32 Floors, const FIntPoint& A, const FIntPoint& B, int32& OutId, FString& OutReason)
{
	OutId = INDEX_NONE;
	FRHCommand Cmd;
	Cmd.Verb = FName("DigCavern");
	Cmd.Level = Top;
	Cmd.Value = Floors;
	Cmd.Location = LatticeToCm(Shafts[0].HeadCm, A);
	Cmd.LocationB = LatticeToCm(Shafts[0].HeadCm, B);
	if (!CanDigCavern(Top, Floors, Cmd.Location, Cmd.LocationB, OutReason))
	{
		return false;
	}
	ExecuteDigCavern(Cmd);
	OutId = NextCavernId - 1;
	return true;
}

// --- Save v48: the cavern block, straight after the warmth block ---

void URHSimWorldSubsystem::WriteCavernBlock(FArchive& Ar) const
{
	// NextCavernId, CavernCount, then per cavern in Id order Id, RegionId, ShaftId, Top, Bottom, Min.X, Min.Y, Max.X, Max.Y, bCancelled (an
	// int32 each, a bool is four bytes: 40 bytes a record). An empty list is 8 bytes.
	int32 Next = NextCavernId, Count = Caverns.Num();
	Ar << Next << Count;
	for (const FRHCavern& C : Caverns)
	{
		int32 Id = C.Id, Region = C.RegionId, Shaft = C.ShaftId, Top = C.Top, Bottom = C.Bottom, Mnx = C.Min.X, Mny = C.Min.Y, Mxx = C.Max.X, Mxy = C.Max.Y;
		bool bCancel = C.bCancelled;
		Ar << Id << Region << Shaft << Top << Bottom << Mnx << Mny << Mxx << Mxy << bCancel;
	}
}

bool URHSimWorldSubsystem::ReadCavernBlock(FArchive& Ar, const TArray<FRHFloor>& Floors, const TArray<FRHShaft>& InShafts, const TArray<FRHTunnel>& InTunnels, TArray<FRHCavern>& OutCaverns, int32& OutNextId, FString& OutError) const
{
	// Structural refusals only, taken before the live colony is touched: they set OutError and log nothing at Error level.
	const auto Refuse = [&OutError](const FString& Why) { OutError = FString::Printf(TEXT("Save cavern list invalid: %s"), *Why); return false; };
	OutCaverns.Reset();
	OutNextId = 1;
	int32 Next = 0, Count = 0;
	Ar << Next << Count;
	if (Ar.IsError() || Next < 1 || Count < 0 || Count > RH::MaxCavernRecords || Count >= Next || (int64)Count * 40 > (int64)Ar.TotalSize() - Ar.Tell())
	{
		return Refuse(FString::Printf(TEXT("next id %d, cavern count %d"), Next, Count));
	}
	TSet<FIntPoint> Columns;
	for (const FRHShaft& Sh : InShafts)
	{
		Columns.Add(Sh.Column);
	}
	const auto FloorOf = [&Floors](int32 Level) -> const FRHFloor*
	{
		for (const FRHFloor& F : Floors) { if (F.Level == Level) { return &F; } }
		return nullptr;
	};
	// (level, x, y) of every uncarved point of an open tunnel, from the tunnel list just read.
	TSet<FIntVector> TunnelOpen;
	for (const FRHTunnel& T : InTunnels)
	{
		if (T.bCancelled)
		{
			continue;
		}
		const FRHFloor* Floor = FloorOf(T.Level);
		const int32 Len = GetTunnelChainLength(T.A, T.B);
		for (int32 i = 0; i < Len; ++i)
		{
			const FIntPoint P = GetTunnelChainPoint(T.A, T.B, i);
			if (!Floor || !Floor->IndexOf.Contains(P))
			{
				TunnelOpen.Add(FIntVector(T.Level, P.X, P.Y));
			}
		}
	}
	TSet<FIntVector> CavernOpen; // the same for every open cavern read so far
	int32 PrevId = 0;
	for (int32 n = 0; n < Count; ++n)
	{
		FRHCavern C;
		int32 Mnx = 0, Mny = 0, Mxx = 0, Mxy = 0;
		uint32 Cancel = 0; // nit 16: a UE bool is four bytes on disk, so reading it as a uint32 keeps the format and lets a value of 2 refuse here instead of logging an engine Error
		Ar << C.Id << C.RegionId << C.ShaftId << C.Top << C.Bottom << Mnx << Mny << Mxx << Mxy << Cancel;
		if (Ar.IsError())
		{
			return Refuse(TEXT("the cavern list is cut short"));
		}
		if (Cancel > 1u)
		{
			return Refuse(FString::Printf(TEXT("cavern %d has a cancelled flag of %u (it is 0 or 1)"), C.Id, Cancel));
		}
		C.Min = FIntPoint(Mnx, Mny);
		C.Max = FIntPoint(Mxx, Mxy);
		C.bCancelled = Cancel != 0u;
		if (C.Id < 1 || C.Id <= PrevId || C.Id >= Next)
		{
			return Refuse(FString::Printf(TEXT("cavern ids must ascend and stay below %d, found %d after %d"), Next, C.Id, PrevId));
		}
		PrevId = C.Id;
		if (C.RegionId != 0)
		{
			return Refuse(FString::Printf(TEXT("cavern %d names region %d (only region 0 exists)"), C.Id, C.RegionId));
		}
		const int64 FloorCount = (int64)C.Top - (int64)C.Bottom + 1;
		if (C.Top >= 0 || C.Bottom > C.Top || C.Bottom < -MaxDepth)
		{
			return Refuse(FString::Printf(TEXT("cavern %d runs from floor %d to %d, which is outside -1..-%d"), C.Id, C.Top, C.Bottom, MaxDepth));
		}
		if (!InShafts.IsValidIndex(C.ShaftId) || InShafts[C.ShaftId].Depth < -C.Bottom)
		{
			return Refuse(FString::Printf(TEXT("cavern %d is worked from shaft %d, which does not exist or does not reach floor %d"), C.Id, C.ShaftId, C.Bottom));
		}
		// Step-2 fix 5: a corner past the lattice bound is refused before any box loop runs (a hand-made box at the int32 edge never ends).
		if (!RH::IsLatticeInRange(C.Min) || !RH::IsLatticeInRange(C.Max))
		{
			return Refuse(FString::Printf(TEXT("cavern %d has a corner out of range (the lattice reaches %d cells from the shaft)"), C.Id, RH::MaxLatticeCoord));
		}
		const int64 SideX = (int64)Mxx - Mnx + 1, SideY = (int64)Mxy - Mny + 1;
		if (FloorCount > RH::CavernMaxFloors || SideX < 1 || SideY < 1 || SideX > RH::CavernMaxSide || SideY > RH::CavernMaxSide)
		{
			return Refuse(FString::Printf(TEXT("cavern %d is %lld x %lld cells on %lld floors (the limits are %d x %d on %d)"), C.Id, SideX, SideY, FloorCount, RH::CavernMaxSide, RH::CavernMaxSide, RH::CavernMaxFloors));
		}
		for (int32 X = C.Min.X; X <= C.Max.X; ++X)
		{
			for (int32 Y = C.Min.Y; Y <= C.Max.Y; ++Y)
			{
				if (!C.bCancelled && Columns.Contains(FIntPoint(X, Y)))
				{
					return Refuse(FString::Printf(TEXT("cavern %d has a shaft column (%d,%d) in its box"), C.Id, X, Y)); // a cancelled cavern's rock may be sunk on later
				}
			}
		}
		if (!C.bCancelled)
		{
			for (int32 L = C.Top; L >= C.Bottom; --L)
			{
				const FRHFloor* Floor = FloorOf(L);
				for (int32 X = C.Min.X; X <= C.Max.X; ++X)
				{
					for (int32 Y = C.Min.Y; Y <= C.Max.Y; ++Y)
					{
						if (Floor && Floor->IndexOf.Contains(FIntPoint(X, Y)))
						{
							continue;
						}
						bool bRepeat = false;
						CavernOpen.Add(FIntVector(L, X, Y), &bRepeat);
						if (bRepeat)
						{
							return Refuse(FString::Printf(TEXT("cavern %d shares the uncarved point (%d,%d) on floor %d with another open cavern"), C.Id, X, Y, L));
						}
						if (TunnelOpen.Contains(FIntVector(L, X, Y)))
						{
							return Refuse(FString::Printf(TEXT("cavern %d shares the uncarved point (%d,%d) on floor %d with an open tunnel"), C.Id, X, Y, L));
						}
					}
				}
			}
		}
		OutCaverns.Add(C);
	}
	OutNextId = Next;
	return true;
}
