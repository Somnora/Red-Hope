#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "Algo/BinarySearch.h"

// C6a (living city gate 6a): commuting. Every employed colonist is measured, from their actual home to
// their actual workplace (Q2a), over roads, open ground and lifts. Districts only group the trips into the
// flow matrix; they are never an exemption. Everything here is DERIVED and never saved: the position memo
// (one Dijkstra per source position, independent of the job assignment - C4), the per-colonist distances and
// the flow matrix (keyed on a superset of every input - R1), and the summary the Hope term and the work hooks
// read. Integer centimetres throughout; ties by id or by coordinates; no container's iteration order ever
// decides an outcome (the maps below are lookups only).

namespace
{
	FIntPoint ToWholeCmCommute(const FVector& Cm)
	{
		return FIntPoint(FMath::RoundToInt(Cm.X), FMath::RoundToInt(Cm.Y));
	}

	bool PosLess(const FIntPoint& A, const FIntPoint& B)
	{
		return A.X != B.X ? A.X < B.X : A.Y < B.Y;
	}

	struct FAnchor
	{
		int32 Level = 0;                 // < 0: a floor (Pos is the shaft head); 0: the surface
		FIntPoint Pos = FIntPoint::ZeroValue;
	};
}

double URHSimWorldSubsystem::GetCommuteRamp() const
{
	const double Pop = (double)Colonists.Num();
	if (Pop <= CommuteRampStartPop)
	{
		return 0.0;
	}
	return FMath::Clamp((Pop - CommuteRampStartPop) / (CommuteRampFullPop - CommuteRampStartPop), 0.0, 1.0);
}

FRHRoadParams URHSimWorldSubsystem::CommuteParams() const
{
	FRHRoadParams P = RoadParams;
	P.SpeedMulPermille = FMath::RoundToInt(CommuteRoadSpeedMul * 1000.0);
	return P;
}

URHSimWorldSubsystem::FRHCommuteKey URHSimWorldSubsystem::MakeCommuteKey() const
{
	FRHCommuteKey K;
	K.Districts = DistrictsVersion;
	K.Colonists = ColonistSetVersion;
	K.Zoning = ZoningVersion;
	K.Furniture = FurnitureVersion;
	K.Jobs = JobsVersion;
	K.Built = RoadGraph.BuiltVersion;
	K.Cfg = CommuteCfgVersion;
	K.Shaft = ToWholeCmCommute(Shafts[0].HeadCm);
	K.ShaftReach = ShaftReachVersion; // D2: constant while there is one shaft
K.BuildingCount = Buildings.Num();
	K.NextBuilding = NextBuildingId;
	K.Lift = LiftVersion;             // D4: both stay 0 with no car, so no rebuild count can move
	K.TransitWait = TransitWaitVersion;
	if (EnsureDistrictGeo().AnyMapped())
	{
		K.DeepCells = FloorCellsVersion; // D6b (RC 6): 0 for ever while no floor shape exists
		K.DeepBeds = BedAssignVersion;
		K.DeepTunnels = TunnelsVersion;  // D6c (RC 6): the people's walk network (cells, tunnels, shafts, districts), all 0 for ever with no mapped floor
		K.DeepShafts = ShaftsVersion;
		K.DeepTram = TramVersion;        // D6d (RC 6): and the tram lines (a line maps its floor); 0 for ever with no mapped floor
	}
	return K;
}

void URHSimWorldSubsystem::ResetCommuteMemoIfStale() const
{
	FRHCommuteMemo& M = CommuteMemo;
	const int32 Permille = FMath::RoundToInt(CommuteRoadSpeedMul * 1000.0);
	const FIntPoint Shaft = ToWholeCmCommute(Shafts[0].HeadCm); // the memo never keys on the other heads: they are only more interned positions
	// The memo is a pure function of the built graph, the shaft head and the commuters' road speed - never of who
	// works where. The size cap only bounds memory: results are pure, so a flush never changes an answer.
	if (!M.bValid || M.Built != RoadGraph.BuiltVersion || M.Shaft != Shaft || M.SpeedPermille != Permille || M.Pos.Num() > 16384)
	{
		M = FRHCommuteMemo();
		M.bValid = true;
		M.Built = RoadGraph.BuiltVersion;
		M.Shaft = Shaft;
		M.SpeedPermille = Permille;
	}
}

int32 URHSimWorldSubsystem::CommuteIntern(FIntPoint P) const
{
	FRHCommuteMemo& M = CommuteMemo;
	if (const int32* Found = M.PosIndex.Find(P))
	{
		return *Found;
	}
	const int32 Idx = M.Pos.AddDefaulted();
	M.Pos[Idx].Pos = P;
	M.PosIndex.Add(P, Idx);
	return Idx;
}

int64 URHSimWorldSubsystem::CommuteSurfaceCm(FIntPoint A, FIntPoint B, TArray<int32>* OutEdges) const
{
	if (OutEdges)
	{
		OutEdges->Reset();
	}
	if (A == B)
	{
		return 0;
	}
	ResetCommuteMemoIfStale();
	FRHCommuteMemo& M = CommuteMemo;
	// O5: a pair is canonical on (low, high) by (X, Y), and the route is planned from the low end, so the overlay draws
	// one route per pair whichever way a colonist travels.
	const FIntPoint Lo = PosLess(B, A) ? B : A;
	const FIntPoint Hi = PosLess(B, A) ? A : B;
	const int32 Li = CommuteIntern(Lo);
	const int32 Hi_i = CommuteIntern(Hi);
	const uint64 Key = ((uint64)(uint32)Li << 32) | (uint64)(uint32)Hi_i;
	TPair<int64, bool> Result(0, false);
	if (const TPair<int64, bool>* Found = M.PairCm.Find(Key))
	{
		Result = *Found;
	}
	else
	{
		const int64 Dx = (int64)Hi.X - Lo.X, Dy = (int64)Hi.Y - Lo.Y;
		const int64 Straight = RHRoads::RoundSqrtCm(Dx * Dx + Dy * Dy);
		Result.Key = Straight;
		if (RoadGraph.BuiltEdgeCount > 0)
		{
			const FRHRoadParams Params = CommuteParams();
			if (M.Pos[Li].SourceIdx == INDEX_NONE)
			{
				FRHCommuteSssp S;
				RoadGraph.DistancesFrom(Lo, Params, S.Dist, S.PrevNode, S.PrevEdge);
				++CommuteDijkstraRuns;
				M.Pos[Li].SourceIdx = M.Sources.Add(MoveTemp(S));
			}
			if (!M.Pos[Hi_i].bExitsKnown)
			{
				RoadGraph.NearestBuiltNodes(Hi, Params, M.Pos[Hi_i].Exits);
				M.Pos[Hi_i].bExitsKnown = true;
			}
			const int64 Cost = FRHRoadGraph::RoadCostVia(M.Pos[Hi_i].Exits, Params, M.Sources[M.Pos[Li].SourceIdx].Dist);
			const int64 Mm = Params.SpeedMulPermille;
			if (Cost != MAX_int64 && Cost < Mm * Straight)
			{
				Result.Key = (Cost + Mm / 2) / Mm;
				Result.Value = true;
			}
		}
		M.PairCm.Add(Key, Result);
	}
	if (OutEdges && Result.Value)
	{
		const FRHRoadParams Params = CommuteParams();
		int32 ExitNode = 0;
		const FRHCommuteSssp& S = M.Sources[M.Pos[Li].SourceIdx];
		FRHRoadGraph::RoadCostVia(M.Pos[Hi_i].Exits, Params, S.Dist, &ExitNode);
		TArray<int32> Rev;
		for (int32 Cur = ExitNode; Cur != 0 && S.PrevNode[Cur - 1] != 0; Cur = S.PrevNode[Cur - 1])
		{
			Rev.Add(S.PrevEdge[Cur - 1]);
		}
		for (int32 i = Rev.Num() - 1; i >= 0; --i)
		{
			OutEdges->Add(Rev[i]);
		}
	}
	return Result.Key;
}

int32 URHSimWorldSubsystem::PickCommuteShaft(int32 Level, FIntPoint SurfacePosCm) const
{
	if (Shafts.Num() == 1)
	{
		return 0; // one shaft: today's calls, in today's order
	}
	int32 Best = INDEX_NONE;
	int64 BestCm = 0;
	for (int32 S = 0; S < Shafts.Num(); ++S)
	{
		if (!ShaftReaches(S, Level))
		{
			continue;
		}
		// D4 (O4): with transit on, a shaft's own per-floor cost is part of the pick - it changes only on a fit (LiftVersion is
		// in the key), never with a wait, so the pick cannot oscillate. With no car this is today's line.
		const int64 Cm = bHasTransit ? (int64)(-Level) * CommutePerFloorCm(S) + CommuteSurfaceCm(ToWholeCmCommute(Shafts[S].HeadCm), SurfacePosCm)
			: CommuteSurfaceCm(ToWholeCmCommute(Shafts[S].HeadCm), SurfacePosCm);
		if (Best == INDEX_NONE || Cm < BestCm) // strict: ties go to the lower Id
		{
			Best = S;
			BestCm = Cm;
		}
	}
	return Best == INDEX_NONE ? 0 : Best; // no shaft reaches Level: shaft 0's head, as the formula always read it
}

int64 URHSimWorldSubsystem::CommutePerFloorCm(int32 ShaftId) const
{
	return GetLiftCars(ShaftId) > 0 ? (int64)CommuteLiftCmPerFloor : (int64)CommuteShaftCmPerFloor;
}

int64 URHSimWorldSubsystem::CommuteWaitCm(int32 ShaftId) const
{
	return (int64)GetTransitWaitSec(ShaftId) * (int64)CommuteWaitCmPerSec;
}

void URHSimWorldSubsystem::CommuteReachList(int32 Level, TArray<int32>& Out) const
{
	Out.Reset();
	for (int32 S = 0; S < Shafts.Num(); ++S)
	{
		if (ShaftReaches(S, Level))
		{
			Out.Add(S);
		}
	}
	if (Out.Num() == 0)
	{
		Out.Add(0); // no shaft reaches it: shaft 0, as the formula always read it
	}
}

int32 URHSimWorldSubsystem::GetCommuteShaftPeople(int32 ShaftId) const
{
	if (!bHasTransit || GetCommuteRamp() <= 0.0)
	{
		return 0;
	}
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	return F.ShaftPeople.IsValidIndex(ShaftId) ? F.ShaftPeople[ShaftId] : 0;
}

int64 URHSimWorldSubsystem::GetCommuteDistanceCm(int32 HomeLevel, FIntPoint HomePosCm, int32 WorkLevel, FIntPoint WorkPosCm, int32 Ordinal) const
{
	return GetCommuteDistanceVia(HomeLevel, HomePosCm, WorkLevel, WorkPosCm, Ordinal, INDEX_NONE);
}

int64 URHSimWorldSubsystem::GetCommuteDistanceVia(int32 HomeLevel, FIntPoint HomePosCm, int32 WorkLevel, FIntPoint WorkPosCm, int32 Ordinal, int32 KnownShaft) const
{
	if (bHasTransit)
	{
		// D4 (2.6): each underground end pays its shaft - its per-floor cost and its frozen wait as walking cm. Between two floors
		// the trip is split over the shafts that reach the deeper one (ascending Id): this colonist's is R[Ordinal mod n]. The
		// choice reads no wait, so a crowd lengthens trips and never moves them.
		if (HomeLevel < 0 && WorkLevel < 0)
		{
			if (HomeLevel == WorkLevel)
			{
				return 0;
			}
			TArray<int32> Reach;
			CommuteReachList(FMath::Min(HomeLevel, WorkLevel), Reach);
			const int32 S = Reach[FMath::Max(0, Ordinal) % Reach.Num()];
			return (int64)FMath::Abs(HomeLevel - WorkLevel) * CommutePerFloorCm(S) + CommuteWaitCm(S);
		}
		if (HomeLevel < 0)
		{
			const int32 S = KnownShaft != INDEX_NONE ? KnownShaft : PickCommuteShaft(HomeLevel, WorkPosCm);
			return (int64)(-HomeLevel) * CommutePerFloorCm(S) + CommuteWaitCm(S) + CommuteSurfaceCm(ToWholeCmCommute(Shafts[S].HeadCm), WorkPosCm);
		}
		if (WorkLevel < 0)
		{
			const int32 S = KnownShaft != INDEX_NONE ? KnownShaft : PickCommuteShaft(WorkLevel, HomePosCm);
			return (int64)(-WorkLevel) * CommutePerFloorCm(S) + CommuteWaitCm(S) + CommuteSurfaceCm(HomePosCm, ToWholeCmCommute(Shafts[S].HeadCm));
		}
		return CommuteSurfaceCm(HomePosCm, WorkPosCm);
	}
	const int64 Per = CommuteShaftCmPerFloor;
	if (HomeLevel < 0 && WorkLevel < 0)
	{
		return (int64)FMath::Abs(HomeLevel - WorkLevel) * Per;
	}
	if (HomeLevel < 0)
	{
		return (int64)(-HomeLevel) * Per + CommuteSurfaceCm(ToWholeCmCommute(Shafts[KnownShaft != INDEX_NONE ? KnownShaft : PickCommuteShaft(HomeLevel, WorkPosCm)].HeadCm), WorkPosCm);
	}
	if (WorkLevel < 0)
	{
		return (int64)(-WorkLevel) * Per + CommuteSurfaceCm(HomePosCm, ToWholeCmCommute(Shafts[KnownShaft != INDEX_NONE ? KnownShaft : PickCommuteShaft(WorkLevel, HomePosCm)].HeadCm));
	}
	return CommuteSurfaceCm(HomePosCm, WorkPosCm);
}

void URHSimWorldSubsystem::DeriveCommuteFlows(FRHCommuteFlows& Out) const
{
	// Sizes only (never a value): the last derive's distinct trips tell the first insert how many to expect.
	const int32 TripHint = FMath::Max(64, Out.Trips.Num() + Out.Trips.Num() / 8);
	Out = FRHCommuteFlows();
	Out.Key = MakeCommuteKey();
	Out.bValid = true;
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();
	const FRHDistrictMembership& Mem = EnsureDistrictMembership();
	Out.Dim = Districts.Num() + 1;
	Out.Flow.Init(0, Out.Dim * Out.Dim);
	Out.LongFlow.Init(0, Out.Dim * Out.Dim);
	const FIntPoint Shaft = Out.Key.Shaft;
	// Buildings are ascending by Id (appended with a rising counter, removal keeps the order): a binary search finds a
	// surface building's position without building a map every rebuild.
	const auto SurfaceBuildingPos = [this](int32 BuildingId, FIntPoint& OutPos)
	{
		const int32 At = Algo::BinarySearchBy(Buildings, BuildingId, [](const FRHBuildingInstance& Bd) { return Bd.Id; });
		if (At == INDEX_NONE || Buildings[At].Level != 0)
		{
			return false;
		}
		OutPos = ToWholeCmCommute(Buildings[At].LocationCm);
		return true;
	};
	const auto DistrictIndex = [&Geo, &Out](int32 DistrictId)
	{
		if (DistrictId != 0)
		{
			if (const int32* Index = Geo.IndexById.Find(DistrictId))
			{
				return *Index;
			}
		}
		return Out.Dim - 1; // Unzoned: the last row and column
	};
	// A trip is a pair of anchors, keyed by (home building Id or -floor, work building Id or -floor). Everything about it
	// (distance, districts, long) is worked out once, on its first colonist; every other colonist is one map lookup.
	TMap<uint64, int32> TripIndex; // lookups only
	TripIndex.Reserve(TripHint);
	// D6b: on a floor that holds a shape a trip is also keyed by its two districts (the same floors, the same distances, a different
	// pair of rows), and the transit split rotates by the floor pair's own running count so no distance moves.
	const bool bAnyShape = Geo.AnyMapped(); // D6d: a floor is mapped by a shape or by a tram line
	TMap<TTuple<uint64, uint64>, int32> ShapeTripIndex; // lookups only
	TMap<uint64, int32> ShapePairCount;                  // lookups only
	// D6c: on a mapped floor a trip is keyed by its two ends' cells (a home cell, a seat or module cell), and measured through the deep walk.
	TMap<TTuple<uint64, uint64>, int32> DeepTripIndex;   // lookups only
	if (bAnyShape)
	{
		DeepTripIndex.Reserve(TripHint);
	}
	const FRHDeepHomes* ShapeHomes = bAnyShape ? &EnsureDeepHomes() : nullptr;
	const auto ShapeDistrictOfCell = [this, &Geo](int32 Level, int32 Ordinal) -> int32
	{
		const FIntPoint P = GetFloorCell(Level, Ordinal);
		return Geo.DeepOwner.FindRef(FIntVector(P.X, P.Y, Level));
	};
	// A floor module's cell (a module station is (Level, INDEX_NONE, Building Id)): INDEX_NONE when it stands on no carved cell.
	const auto FloorModuleCell = [this](int32 BuildingId, int32 Level)
	{
		const int32 At = Algo::BinarySearchBy(Buildings, BuildingId, [](const FRHBuildingInstance& Bd) { return Bd.Id; });
		if (At == INDEX_NONE || Buildings[At].Level != Level)
		{
			return (int32)INDEX_NONE;
		}
		return FindCarvedCellAt(Level, Buildings[At].LocationCm);
	};
	// The district rows of a trip that touches a mapped floor: the same lookups the membership uses (a whole floor's owner, else the shape
	// holding the home or work cell).
	const auto ResolveShapeDistricts = [&](const FRHColonist& Col, const FIntVector& Seat, int32& OutHome, int32& OutWork)
	{
		OutHome = 0;
		OutWork = 0;
		if (Col.HomeHab != 0)
		{
			OutHome = Mem.OfBuilding.FindRef(Col.HomeHab);
		}
		else if (const int32* WholeHome = Geo.FloorOwner.Find(Col.HomeLevel))
		{
			OutHome = *WholeHome;
		}
		else if (const int32* HomeCell = ShapeHomes->HomeCell.Find(Col.Id))
		{
			OutHome = ShapeDistrictOfCell(Col.HomeLevel, *HomeCell);
		}
		if (Seat.Y == INDEX_NONE || Seat.X >= 0)
		{
			OutWork = Mem.OfBuilding.FindRef(Seat.Z);
		}
		else if (const int32* WholeWork = Geo.FloorOwner.Find(Seat.X))
		{
			OutWork = *WholeWork;
		}
		else
		{
			OutWork = ShapeDistrictOfCell(Seat.X, Seat.Y);
		}
	};
	Out.Trips.Reserve(TripHint);
	Out.Ids.Reserve(Colonists.Num());
	Out.Dists.Reserve(Colonists.Num());
	Out.LongIds.Reserve(Colonists.Num());
	// D4: with transit on, the per-shaft commuter count (StepTransit's people load) and, for a trip between two floors, its
	// reach list, parallel to the trip slots (valid until the trip sort below).
	const bool bTransit = bHasTransit;
	TArray<TArray<int32>> TripReach;
	TArray<int64> TripDistSum;
	if (bTransit)
	{
		Out.ShaftPeople.Init(0, Shafts.Num());
		Out.TramPeople.Init(0, TramLines.Num());
		TripReach.Reserve(TripHint);
		TripDistSum.Reserve(TripHint);
	}
	for (const FRHColonist& C : Colonists)
	{
		const FIntVector* Station = ColonistStations.Find(C.Id);
		if (!Station)
		{
			continue; // no workplace: not measured
		}
		const int32 HomeKey = C.HomeHab != 0 ? C.HomeHab : (C.HomeLevel < 0 ? C.HomeLevel : 0);
		if (HomeKey == 0)
		{
			continue; // no home anchor
		}
		const int32 WorkKey = Station->X < 0 ? Station->X : Station->Z;
		const uint64 TripKey = ((uint64)(uint32)HomeKey << 32) | (uint64)(uint32)WorkKey;
		const bool bShapeTrip = bAnyShape && ((C.HomeHab == 0 && Geo.IsMapped(C.HomeLevel)) || (Station->X < 0 && Geo.IsMapped(Station->X)));
		int32 ShapeHomeDistrict = 0, ShapeWorkDistrict = 0;
		int32 TripSlot;
		const int32* Found = nullptr;
		// D6c: each end of a trip that touches a mapped floor is a surface building or a cell of a mapped floor. A trip with any other
		// end (an unmapped floor, a resident with no home cell, a module on no cell) is OFF THE NET and keeps D6b's head anchor.
		FRHDeepEnd HomeEnd, WorkEnd;
		bool bDeepTrip = false;
		if (bShapeTrip)
		{
			bool bEnds = true;
			if (C.HomeHab != 0)
			{
				bEnds &= SurfaceBuildingPos(C.HomeHab, HomeEnd.SurfacePos);
			}
			else
			{
				HomeEnd.Level = C.HomeLevel;
				const int32* HomeCellOrd = Geo.IsMapped(C.HomeLevel) ? ShapeHomes->HomeCell.Find(C.Id) : nullptr;
				bEnds &= HomeCellOrd != nullptr;
				HomeEnd.Cell = HomeCellOrd ? *HomeCellOrd : (int32)INDEX_NONE;
			}
			if (Station->X < 0)
			{
				WorkEnd.Level = Station->X;
				WorkEnd.Cell = Geo.IsMapped(Station->X) ? (Station->Y != INDEX_NONE ? Station->Y : FloorModuleCell(Station->Z, Station->X)) : (int32)INDEX_NONE;
				bEnds &= WorkEnd.Cell != INDEX_NONE;
			}
			else
			{
				bEnds &= SurfaceBuildingPos(Station->Z, WorkEnd.SurfacePos);
			}
			bDeepTrip = bEnds;
		}
		const TTuple<uint64, uint64> DeepKey(((uint64)(uint32)HomeKey << 32) | (uint64)(uint32)HomeEnd.Cell, ((uint64)(uint32)WorkKey << 32) | (uint64)(uint32)WorkEnd.Cell);
		if (bShapeTrip)
		{
			if (bDeepTrip)
			{
				Found = DeepTripIndex.Find(DeepKey); // keyed by cells: the district rows are worked out when the trip is made
			}
			else
			{
				ResolveShapeDistricts(C, *Station, ShapeHomeDistrict, ShapeWorkDistrict);
				Found = ShapeTripIndex.Find(MakeTuple(TripKey, ((uint64)(uint32)ShapeHomeDistrict << 32) | (uint64)(uint32)ShapeWorkDistrict));
			}
		}
		else
		{
			Found = TripIndex.Find(TripKey);
		}
		// D6c: a new trip on the net is measured through the deep walk; one the walk cannot reach falls through to the head anchor below.
		FRHDeepTrip Walk;
		const bool bWalked = !Found && bDeepTrip && MeasureDeepTrip(HomeEnd, WorkEnd, Walk);
		if (Found)
		{
			TripSlot = *Found;
		}
		else if (bWalked)
		{
			ResolveShapeDistricts(C, *Station, ShapeHomeDistrict, ShapeWorkDistrict);
			TripSlot = Out.Trips.AddDefaulted();
			DeepTripIndex.Add(DeepKey, TripSlot);
			FRHCommuteTrip& T = Out.Trips[TripSlot];
			T.bMeasured = true;
			T.bDeep = true;
			T.HomeLevel = HomeEnd.Level; T.HomePos = Walk.HomePos; T.WorkLevel = WorkEnd.Level; T.WorkPos = Walk.WorkPos;
			T.HomeCell = HomeEnd.Cell; T.WorkCell = WorkEnd.Cell;
			T.HomeDistrict = ShapeHomeDistrict; T.WorkDistrict = ShapeWorkDistrict;
			T.DistCm = Walk.DistCm + DeepTripWaitCm(Walk); // the route was chosen wait-free; each shaft ridden adds its own wait
			T.bLong = T.DistCm > CommuteLongCm;
			T.Cell = DistrictIndex(ShapeHomeDistrict) * Out.Dim + DistrictIndex(ShapeWorkDistrict);
			T.ShaftId = Walk.Shaft1; // set with or without a car: the readout names the shaft a walked trip rides
			T.ShaftId2 = Walk.Shaft2;
			T.Trams1 = Walk.Trams[0]; // D6d: the tram lines its two legs ride (their waits are already in DistCm)
			T.Trams2 = Walk.Trams[1];
			if (bTransit)
			{
				TripReach.SetNum(Out.Trips.Num()); // a walked trip is never split: its shafts are fixed
				TripDistSum.SetNum(Out.Trips.Num());
			}
		}
		else
		{
			TripSlot = Out.Trips.AddDefaulted();
			if (bDeepTrip)
			{
				ResolveShapeDistricts(C, *Station, ShapeHomeDistrict, ShapeWorkDistrict);
				DeepTripIndex.Add(DeepKey, TripSlot); // off the net: the head anchor, one slot a pair of cells
			}
			else if (bShapeTrip)
			{
				ShapeTripIndex.Add(MakeTuple(TripKey, ((uint64)(uint32)ShapeHomeDistrict << 32) | (uint64)(uint32)ShapeWorkDistrict), TripSlot);
			}
			else
			{
				TripIndex.Add(TripKey, TripSlot);
			}
			FRHCommuteTrip& T = Out.Trips[TripSlot];
			T.HomeCell = bDeepTrip ? HomeEnd.Cell : (int32)INDEX_NONE; // the sort's tie-break: several off-net trips share one head Pos (RC 11)
			T.WorkCell = bDeepTrip ? WorkEnd.Cell : (int32)INDEX_NONE;
			FAnchor Home, Work;
			int32 HomeDistrictId = 0, WorkDistrictId = 0;
			bool bOk = true;
			if (C.HomeHab != 0)
			{
				bOk &= SurfaceBuildingPos(C.HomeHab, Home.Pos);
				HomeDistrictId = Mem.OfBuilding.FindRef(C.HomeHab);
			}
			else
			{
				Home.Level = C.HomeLevel;
				Home.Pos = Shaft;
				HomeDistrictId = bShapeTrip ? ShapeHomeDistrict : Geo.FloorOwner.FindRef(C.HomeLevel);
			}
			if (Station->X < 0)
			{
				Work.Level = Station->X;
				Work.Pos = Shaft;
				WorkDistrictId = bShapeTrip ? ShapeWorkDistrict : Geo.FloorOwner.FindRef(Station->X);
			}
			else
			{
				bOk &= SurfaceBuildingPos(Station->Z, Work.Pos);
				WorkDistrictId = bShapeTrip ? ShapeWorkDistrict : Mem.OfBuilding.FindRef(Station->Z);
			}
			// The shaft a floor-to-surface trip uses, picked once: the anchor move below, the distance and the transit tally all asked
			// for the same pick with the same arguments (only the floor anchor moves, never the surface point it is priced to).
			int32 PickedShaft = INDEX_NONE;
			if (bOk)
			{
				if (Home.Level < 0 && Work.Level == 0) { PickedShaft = PickCommuteShaft(Home.Level, Work.Pos); }
				else if (Work.Level < 0 && Home.Level == 0) { PickedShaft = PickCommuteShaft(Work.Level, Home.Pos); }
			}
			if (bOk && Shafts.Num() > 1)
			{
				// D2: a floor anchor sits at the head of the shaft this trip uses (the cheapest that reaches its floor).
				if (Home.Level < 0 && Work.Level == 0) { Home.Pos = ToWholeCmCommute(Shafts[PickedShaft].HeadCm); }
				else if (Work.Level < 0 && Home.Level == 0) { Work.Pos = ToWholeCmCommute(Shafts[PickedShaft].HeadCm); }
			}
			T.bMeasured = bOk;
			if (bOk)
			{
				T.HomeLevel = Home.Level; T.HomePos = Home.Pos; T.WorkLevel = Work.Level; T.WorkPos = Work.Pos;
				T.HomeDistrict = HomeDistrictId; T.WorkDistrict = WorkDistrictId;
				T.DistCm = GetCommuteDistanceVia(Home.Level, Home.Pos, Work.Level, Work.Pos, 0, PickedShaft);
				T.bLong = T.DistCm > CommuteLongCm;
				T.Cell = DistrictIndex(HomeDistrictId) * Out.Dim + DistrictIndex(WorkDistrictId);
				if (bTransit)
				{
					// D4: a floor-to-surface trip uses one shaft; a trip between two floors is split (REVISION item 5).
					if (PickedShaft != INDEX_NONE) { T.ShaftId = PickedShaft; }
				}
			}
			if (bTransit)
			{
				TripReach.SetNum(Out.Trips.Num());
				TripDistSum.SetNum(Out.Trips.Num());
				if (bOk && Home.Level < 0 && Work.Level < 0 && Home.Level != Work.Level)
				{
					CommuteReachList(FMath::Min(Home.Level, Work.Level), TripReach[TripSlot]);
				}
			}
		}
		FRHCommuteTrip& Trip = Out.Trips[TripSlot];
		if (!Trip.bMeasured)
		{
			continue;
		}
		// The colonist's own distance: the trip's, except on a split trip, where colonist k of the trip (in the order this
		// loop walks the roster) rides shaft R[k mod n] and pays that shaft exactly. With no transit: the trip's, as before.
		int64 Dist = Trip.DistCm;
		bool bLong = Trip.bLong;
		if (bTransit)
		{
			const TArray<int32>& Reach = TripReach[TripSlot];
			if (Reach.Num() > 0)
			{
				// On a shaped floor the rotation counts the floor pair's colonists, not this one trip's, so the split is D4's exactly.
				int32 Turn = Trip.Count;
				if (bShapeTrip)
				{
					Turn = ShapePairCount.FindOrAdd(TripKey)++;
				}
				const int32 S = Reach[Turn % Reach.Num()];
				Dist = (int64)FMath::Abs(Trip.HomeLevel - Trip.WorkLevel) * CommutePerFloorCm(S) + CommuteWaitCm(S);
				bLong = Dist > CommuteLongCm;
				++Out.ShaftPeople[S];
				TripDistSum[TripSlot] += Dist;
			}
			else if (Trip.ShaftId != INDEX_NONE && Out.ShaftPeople.IsValidIndex(Trip.ShaftId))
			{
				++Out.ShaftPeople[Trip.ShaftId];
				if (Trip.ShaftId2 != INDEX_NONE && Out.ShaftPeople.IsValidIndex(Trip.ShaftId2))
				{
					++Out.ShaftPeople[Trip.ShaftId2]; // a walked trip that rides two shafts loads both
				}
			}
			if (Trip.bDeep && (Trip.Trams1 | Trip.Trams2) != 0)
			{
				// D6d: a walked trip loads every tram line its legs ride, one place a ride (a line it rides on both legs is loaded twice).
				for (const uint32 Leg : { Trip.Trams1, Trip.Trams2 })
				{
					for (uint32 Bits = Leg; Bits != 0; Bits &= Bits - 1)
					{
						const int32 Line = (int32)FMath::CountTrailingZeros(Bits);
						if (Out.TramPeople.IsValidIndex(Line))
						{
							++Out.TramPeople[Line];
						}
					}
				}
			}
		}
		if (Trip.bDeep)
		{
			++Out.Walked;
		}
		else if (bShapeTrip)
		{
			++Out.OffNet;
		}
		++Trip.Count;
		++Out.Employed;
		Out.Ids.Add(C.Id);
		Out.Dists.Add(Dist);
		Out.Commuters += Dist > 0 ? 1 : 0;
		Out.MaxDistCm = FMath::Max(Out.MaxDistCm, Dist);
		if (bLong)
		{
			++Out.LongCount;
			Out.LongIds.Add(C.Id);
		}
		++Out.Flow[Trip.Cell];
		Out.LongFlow[Trip.Cell] += bLong ? 1 : 0;
	}
	if (bTransit)
	{
		// For display only (GetCommutePairs): a split trip's own distance is the mean of its colonists', floored, and its
		// long flag that mean against the line. No rule reads them.
		for (int32 i = 0; i < Out.Trips.Num(); ++i)
		{
			if (TripReach.IsValidIndex(i) && TripReach[i].Num() > 0 && Out.Trips[i].Count > 0)
			{
				Out.Trips[i].DistCm = TripDistSum[i] / Out.Trips[i].Count;
				Out.Trips[i].bLong = Out.Trips[i].DistCm > CommuteLongCm;
			}
		}
	}
	// The readers binary-search Ids. Colonists are Id-ascending today (appended with a rising counter), but nothing here
	// may rely on it: when they are not, Ids and Dists are sorted together by Id.
	bool bIdsAscending = true;
	for (int32 i = 1; i < Out.Ids.Num() && bIdsAscending; ++i)
	{
		bIdsAscending = Out.Ids[i - 1] < Out.Ids[i];
	}
	if (!bIdsAscending)
	{
		TArray<int32> Order;
		Order.Reserve(Out.Ids.Num());
		for (int32 i = 0; i < Out.Ids.Num(); ++i) { Order.Add(i); }
		Order.Sort([&Out](int32 A, int32 B) { return Out.Ids[A] < Out.Ids[B]; });
		TArray<int32> SortedIds;
		TArray<int64> SortedDists;
		for (const int32 At : Order)
		{
			SortedIds.Add(Out.Ids[At]);
			SortedDists.Add(Out.Dists[At]);
		}
		Out.Ids = MoveTemp(SortedIds);
		Out.Dists = MoveTemp(SortedDists);
	}
	// Trips nobody makes (an unresolvable anchor) are dropped.
	Out.Trips.RemoveAll([](const FRHCommuteTrip& T) { return !T.bMeasured; });
	// Ids ascend in every real colony, and LongIds is appended in the same walk, so it is already ascending: sort only when not.
	bool bLongAscending = true;
	for (int32 i = 1; i < Out.LongIds.Num() && bLongAscending; ++i)
	{
		bLongAscending = Out.LongIds[i - 1] <= Out.LongIds[i];
	}
	if (!bLongAscending)
	{
		Out.LongIds.Sort();
	}
	Out.Trips.Sort([](const FRHCommuteTrip& A, const FRHCommuteTrip& B)
	{
		if (A.HomeLevel != B.HomeLevel) { return A.HomeLevel < B.HomeLevel; }
		if (A.HomePos != B.HomePos) { return PosLess(A.HomePos, B.HomePos); }
		if (A.WorkLevel != B.WorkLevel) { return A.WorkLevel < B.WorkLevel; }
		if (A.WorkPos != B.WorkPos) { return PosLess(A.WorkPos, B.WorkPos); }
		// D6b: trips of one floor pair that differ only in their districts (a shaped floor). Every other pair of trips is already
		// distinct above, so this moves nothing for a colony with no shape.
		if (A.HomeDistrict != B.HomeDistrict) { return A.HomeDistrict < B.HomeDistrict; }
		if (A.WorkDistrict != B.WorkDistrict) { return A.WorkDistrict < B.WorkDistrict; }
		// D6c (RC 11): trips on a mapped floor that share a head Pos and both districts differ only in their cells.
		if (A.HomeCell != B.HomeCell) { return A.HomeCell < B.HomeCell; }
		return A.WorkCell < B.WorkCell;
	});
}

const URHSimWorldSubsystem::FRHCommuteFlows& URHSimWorldSubsystem::EnsureCommuteFlows() const
{
	if (!CommuteFlows.bValid || !(CommuteFlows.Key == MakeCommuteKey()))
	{
		DeriveCommuteFlows(CommuteFlows);
		++CommuteFlowRebuilds;
	}
	else if (bVerifyCaches)
	{
		// The cache is about to be reused: derive it again and compare.
		FRHCommuteFlows Fresh;
		DeriveCommuteFlows(Fresh);
		if (Fresh.Flow != CommuteFlows.Flow || Fresh.LongFlow != CommuteFlows.LongFlow || Fresh.Ids != CommuteFlows.Ids
			|| Fresh.Dists != CommuteFlows.Dists || Fresh.LongIds != CommuteFlows.LongIds || Fresh.LongCount != CommuteFlows.LongCount
			|| Fresh.ShaftPeople != CommuteFlows.ShaftPeople || Fresh.TramPeople != CommuteFlows.TramPeople || Fresh.Walked != CommuteFlows.Walked || Fresh.OffNet != CommuteFlows.OffNet)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the commute flows cache is stale (%d long reused, %d re-derived; districts v%u, colonists v%u, jobs v%u, built v%u)"),
				CommuteFlows.LongCount, Fresh.LongCount, DistrictsVersion, ColonistSetVersion, JobsVersion, RoadGraph.BuiltVersion);
		}
	}
	return CommuteFlows;
}

bool URHSimWorldSubsystem::Debug_CommuteCachesFresh() const
{
	if (!CommuteFlows.bValid || !(CommuteFlows.Key == MakeCommuteKey()))
	{
		return true; // nothing is reusable: the next read re-derives
	}
	FRHCommuteFlows Fresh;
	DeriveCommuteFlows(Fresh);
	return Fresh.Flow == CommuteFlows.Flow && Fresh.LongFlow == CommuteFlows.LongFlow && Fresh.Ids == CommuteFlows.Ids
		&& Fresh.Dists == CommuteFlows.Dists && Fresh.LongIds == CommuteFlows.LongIds && Fresh.LongCount == CommuteFlows.LongCount
		&& Fresh.ShaftPeople == CommuteFlows.ShaftPeople && Fresh.TramPeople == CommuteFlows.TramPeople && Fresh.Walked == CommuteFlows.Walked && Fresh.OffNet == CommuteFlows.OffNet
		&& IsDeepWalkFresh();
}

void URHSimWorldSubsystem::Debug_ReroutePosition()
{
	CommuteMemo = FRHCommuteMemo();
	CommuteFlows = FRHCommuteFlows();
	DeepWalk = FRHDeepWalk(); // D6c: a cold start forgets the people's walk network too
}

URHSimWorldSubsystem::FRHCommuteSummary URHSimWorldSubsystem::GetCommuteSummary() const
{
	FRHCommuteSummary S;
	const double Ramp = GetCommuteRamp();
	if (Ramp <= 0.0)
	{
		return S; // the gate: the zero summary, before any cache is touched
	}
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	S.Ramp = Ramp;
	S.Pop = Colonists.Num();
	S.Employed = F.Employed;
	S.Commuters = F.Commuters;
	S.LongCommuters = F.LongCount;
	S.MaxDistCm = F.MaxDistCm;
	S.WorkMul = 1.0 - Ramp * (1.0 - CommuteWorkMul);
	if (F.LongCount > 0 && S.Pop > 0)
	{
		S.Penalty = Ramp * HopeCommuteMax * FMath::Min(1.0, HopeCommuteSteepness * (double)F.LongCount / (double)S.Pop);
	}
	return S;
}

int64 URHSimWorldSubsystem::GetColonistCommuteCm(int32 ColonistId) const
{
	if (GetCommuteRamp() <= 0.0)
	{
		return -1;
	}
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	const int32 At = Algo::BinarySearch(F.Ids, ColonistId);
	return At == INDEX_NONE ? -1 : F.Dists[At];
}

bool URHSimWorldSubsystem::IsLongCommuter(int32 ColonistId) const
{
	if (GetCommuteRamp() <= 0.0)
	{
		return false;
	}
	return Algo::BinarySearch(EnsureCommuteFlows().LongIds, ColonistId) != INDEX_NONE;
}

void URHSimWorldSubsystem::GetCommuteFlowMatrix(TArray<int32>& OutFlat, int32& OutDim) const
{
	OutFlat.Reset();
	OutDim = 0;
	if (GetCommuteRamp() <= 0.0)
	{
		return;
	}
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	OutFlat = F.Flow;
	OutDim = F.Dim;
}

void URHSimWorldSubsystem::GetCommuteFlows(TArray<FRHCommuteFlow>& Out) const
{
	Out.Reset();
	if (GetCommuteRamp() <= 0.0)
	{
		return;
	}
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	for (int32 H = 0; H < F.Dim; ++H)
	{
		for (int32 W = 0; W < F.Dim; ++W)
		{
			const int32 Count = F.Flow[H * F.Dim + W];
			if (Count > 0)
			{
				FRHCommuteFlow Flow;
				Flow.HomeDistrictId = H < Districts.Num() ? Districts[H].Id : 0;
				Flow.WorkDistrictId = W < Districts.Num() ? Districts[W].Id : 0;
				Flow.Count = Count;
				Flow.LongCount = F.LongFlow[H * F.Dim + W];
				Out.Add(Flow);
			}
		}
	}
}

void URHSimWorldSubsystem::CommuteFillEdgeLoad(const FRHCommuteFlows& F) const
{
	if (F.bEdgeLoadValid)
	{
		return;
	}
	F.TripEdges.Reset();
	F.TripEdges.SetNum(F.Trips.Num());
	TMap<int32, int32> Load; // lookups only; read out sorted
	for (int32 i = 0; i < F.Trips.Num(); ++i)
	{
		// D2: a floor anchor's Pos already holds the head of the shaft the trip uses (shaft 0's, with one shaft), so the road load
		// follows the same head as the distance.
		const FRHCommuteTrip& T = F.Trips[i];
		const FIntPoint A = T.HomePos, B = T.WorkPos;
		const bool bSurfacePart = !(T.HomeLevel < 0 && T.WorkLevel < 0);
		if (bSurfacePart)
		{
			CommuteSurfaceCm(A, B, &F.TripEdges[i]);
		}
		for (const int32 EdgeId : F.TripEdges[i])
		{
			Load.FindOrAdd(EdgeId) += T.Count;
		}
	}
	F.EdgeLoad.Reset();
	TArray<int32> Ids;
	Load.GetKeys(Ids);
	Ids.Sort();
	for (const int32 Id : Ids)
	{
		F.EdgeLoad.Emplace(Id, Load[Id]);
	}
	F.bEdgeLoadValid = true;
}

void URHSimWorldSubsystem::GetCommutePairs(TArray<FRHCommutePair>& Out) const
{
	Out.Reset();
	if (GetCommuteRamp() <= 0.0)
	{
		return;
	}
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	CommuteFillEdgeLoad(F);
	for (int32 i = 0; i < F.Trips.Num(); ++i)
	{
		const FRHCommuteTrip& T = F.Trips[i];
		FRHCommutePair P;
		P.HomeLevel = T.HomeLevel; P.WorkLevel = T.WorkLevel;
		P.HomePosCm = T.HomePos; P.WorkPosCm = T.WorkPos;
		P.HomeDistrictId = T.HomeDistrict; P.WorkDistrictId = T.WorkDistrict;
		P.Count = T.Count; P.DistCm = T.DistCm;
		P.HomeCell = T.HomeCell; P.WorkCell = T.WorkCell;
		P.ShaftId = T.bDeep ? T.ShaftId : (int32)INDEX_NONE; P.ShaftId2 = T.bDeep ? T.ShaftId2 : (int32)INDEX_NONE;
		P.EdgeIds = F.TripEdges[i];
		Out.Add(MoveTemp(P));
	}
}

void URHSimWorldSubsystem::GetCommuteEdgeLoads(TArray<TPair<int32, int32>>& Out) const
{
	Out.Reset();
	if (GetCommuteRamp() <= 0.0)
	{
		return;
	}
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	CommuteFillEdgeLoad(F);
	Out = F.EdgeLoad;
}

void URHSimWorldSubsystem::AuditCommuteStep()
{
	// -RHCommuteAudit: the flows are measured at EVERY step, even where the ramp is closed (the audit asks what the
	// distances WOULD be), and the maxima are kept - never one snapshot. Nothing here feeds the sim.
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	++CommuteAudit.Steps;
	CommuteAudit.PeakPop = FMath::Max(CommuteAudit.PeakPop, Colonists.Num());
	CommuteAudit.PeakRamp = FMath::Max(CommuteAudit.PeakRamp, GetCommuteRamp());
	CommuteAudit.MaxLong = FMath::Max(CommuteAudit.MaxLong, F.LongCount);
	CommuteAudit.MaxDistCm = FMath::Max(CommuteAudit.MaxDistCm, F.MaxDistCm);
}

void URHSimWorldSubsystem::NoteCommuteAlert()
{
	if (bCommuteAlerted || GetCommuteRamp() <= 0.0)
	{
		return;
	}
	const FRHCommuteSummary S = GetCommuteSummary();
	if (S.LongCommuters <= 0 || S.Pop <= 0 || (double)S.LongCommuters / (double)S.Pop < CommuteAlertShare)
	{
		return;
	}
	bCommuteAlerted = true; // runtime latch, reset at load (the bWaterQualityAnnounced precedent)
	const FString Alert = FString::Printf(TEXT("%d people travel over %.0f m to work; homes nearer their work would give them time back."), S.LongCommuters, (double)CommuteLongCm / 100.0); // Gate-D placeholder copy
	UE_LOG(LogRedHopeSim, Display, TEXT("%s"), *Alert);
	OnAlert.Broadcast(Alert);
}

bool URHSimWorldSubsystem::Debug_SetColonistHome(int32 ColonistId, int32 HomeHab, int32 HomeLevel)
{
	FRHColonist* C = const_cast<FRHColonist*>(FindColonist(ColonistId));
	if (!C)
	{
		return false;
	}
	C->HomeHab = HomeHab;
	C->HomeLevel = HomeHab != 0 ? 0 : HomeLevel;
	++ColonistSetVersion;
	return true;
}

bool URHSimWorldSubsystem::Debug_RemoveColonist(int32 ColonistId)
{
	if (!FindColonist(ColonistId))
	{
		return false;
	}
	Colonists.RemoveAll([ColonistId](const FRHColonist& C) { return C.Id == ColonistId; });
	bColonistIndexDirty = true;
	++ColonistSetVersion;
	RefreshJobs();
	return true;
}

bool URHSimWorldSubsystem::Debug_SetColonistStation(int32 ColonistId, const FIntVector& Key)
{
	if (!FindColonist(ColonistId))
	{
		return false;
	}
	ColonistStations.FindOrAdd(ColonistId) = Key;
	++JobsVersion;
	return true;
}
