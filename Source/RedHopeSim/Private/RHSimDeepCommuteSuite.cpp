#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHDistricts.h"
#include "RHRoads.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// D6c deep commute self-test (docs/qa/2026-10-22/d6-caverns-design.md: the DIRECTOR ANSWERs Q-A (a), Q-B (a), Q-C (a), the MERGED DESIGN AND
// REVISION section 3 with RC 6, 10 and 11). Fails loudly. `-deepcommute`.
// On a MAPPED floor (one that holds a floor shape, D6b) a trip is measured from the resident's home cell to the seat's cell: through the digs
// and tunnels on foot (the octile line inside a compartment, tunnel cells cell by cell, 1,000 cm a step) and by shaft where digs are not joined.
// Every distance below is derived by hand from the lattice, or by an independent oracle (a Floyd-Warshall over every carved cell, sharing no
// code with the sim).
//  DC1 hand integers: inside one compartment, across a 30-cell tunnel, between unjoined digs through two shafts (R2: not 0 m any more), then
//      a sweep of every (home cell, seat cell) pair on two layouts against the oracle; DC2 floor to surface picks walk plus ride (a resident
//      beside shaft 1 rides it although the workplace stands nearer shaft 0's head); DC3 a crowded shaft does not flip the route, this sol or
//      the next; DC4 ShaftPeople counts each shaft ridden; DC5 off the net (no home cell, an unmapped floor) keeps the head anchor, and
//      trips that differ only in their cells sort the same in two build orders; DC6 an unmapped floor in the same colony is byte-equal to a
//      twin with no shape; DC7 the ramp at 60, 61 and 160; DC8 the era band against the agent band; DC9 the caches and keys (a tunnel cell
//      that joins two digs moves the flows; the keys are 0 with no mapped floor; nothing derived is saved).
namespace
{
	// The octile line in integer cm (the same rule as the sim's, written again here).
	int64 OracleOct(const FIntPoint& A, const FIntPoint& B)
	{
		const int64 Dx = FMath::Abs((int64)A.X - B.X), Dy = FMath::Abs((int64)A.Y - B.Y);
		return 1000 * FMath::Max(Dx, Dy) + 414 * FMath::Min(Dx, Dy);
	}

	// All-pairs walking cm between every carved cell of one floor (MAX_int64: none): a Floyd-Warshall over the cells themselves. Chain cells are
	// the points of every tunnel's line; two cells that are not chain cells and share a 4-connected component are joined by their octile line; a
	// chain cell and any 4-neighbour carved cell by 1,000 cm.
	TArray<TArray<int64>> OracleWalk(const URHSimWorldSubsystem* Sim, int32 Level)
	{
		TArray<TArray<int64>> D;
		const FRHFloor* Fl = Sim->FindFloor(Level);
		if (!Fl) { return D; }
		const TArray<FIntPoint>& Pts = Fl->GetCells();
		const int32 N = Pts.Num();
		TSet<FIntPoint> Chain;
		for (const FRHTunnel& T : Sim->GetTunnels())
		{
			if (T.Level != Level) { continue; }
			for (int32 i = 0; i < URHSimWorldSubsystem::GetTunnelChainLength(T.A, T.B); ++i) { Chain.Add(URHSimWorldSubsystem::GetTunnelChainPoint(T.A, T.B, i)); }
		}
		TArray<int32> Comp;
		Comp.Init(INDEX_NONE, N);
		int32 Comps = 0;
		for (int32 s = 0; s < N; ++s)
		{
			if (Chain.Contains(Pts[s]) || Comp[s] != INDEX_NONE) { continue; }
			TArray<int32> Work;
			Work.Add(s);
			Comp[s] = Comps;
			while (Work.Num() > 0)
			{
				const int32 Cur = Work.Pop();
				for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					const int32 O = Sim->FindFloorCell(Level, Pts[Cur] + Step);
					if (O != INDEX_NONE && !Chain.Contains(Pts[O]) && Comp[O] == INDEX_NONE) { Comp[O] = Comps; Work.Add(O); }
				}
			}
			++Comps;
		}
		D.SetNum(N);
		for (int32 i = 0; i < N; ++i)
		{
			D[i].Init(MAX_int64, N);
			D[i][i] = 0;
		}
		for (int32 i = 0; i < N; ++i)
		{
			for (int32 j = i + 1; j < N; ++j)
			{
				int64 W = MAX_int64;
				const bool bAdj = FMath::Abs(Pts[i].X - Pts[j].X) + FMath::Abs(Pts[i].Y - Pts[j].Y) == 1;
				if (Comp[i] != INDEX_NONE && Comp[i] == Comp[j]) { W = OracleOct(Pts[i], Pts[j]); }
				else if (bAdj && (Comp[i] == INDEX_NONE || Comp[j] == INDEX_NONE)) { W = 1000; }
				D[i][j] = D[j][i] = W;
			}
		}
		for (int32 k = 0; k < N; ++k)
		{
			for (int32 i = 0; i < N; ++i)
			{
				if (D[i][k] == MAX_int64) { continue; }
				for (int32 j = 0; j < N; ++j)
				{
					if (D[k][j] != MAX_int64 && D[i][k] + D[k][j] < D[i][j]) { D[i][j] = D[i][k] + D[k][j]; }
				}
			}
		}
		return D;
	}

	// Counts the Error lines LogRedHopeSim prints while it is attached (a clean run prints none).
	struct FRHDeepCommuteErrorCounter : public FOutputDevice
	{
		int32 Count = 0;
		virtual void Serialize(const TCHAR* /*Text*/, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity == ELogVerbosity::Error && Category == FName(TEXT("LogRedHopeSim"))) { ++Count; }
		}
	};
}

int32 RHRunDeepCommuteSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== DEEP COMMUTE TEST (D6c, 2026-10-23) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("DEEPCOMMUTE ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("DEEPCOMMUTE FAIL: %s"), *What); ++Fails; }
	};
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	const FName NStruct(TEXT("Struct")), NHomes(TEXT("Homes"));
	FString R, Why;
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	const auto Finish = [&]() -> int32
	{
		Expect(Sim->GetFloorCellFallbacks() == 0, TEXT("DC over the whole suite not one ordinal read fell outside its floor and back to the spiral"));
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DEEP COMMUTE TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto EraOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->Debug_SetBatteryWh(1.0e9); // every fixture here that fits a car pins the bank (RC 9)
		Sim->EraStep((float)EraDt);
	};
	const auto AgentOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds((double)AgentDt);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->Debug_StepSim(AgentDt);
	};
	const auto FindB = [&](int32 Id) -> const FRHBuildingInstance*
	{
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.Id == Id) { return &Bd; } }
		return nullptr;
	};
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto Place = [&](const TCHAR* Def, const FVector& Loc, int32 Level)
	{
		Sim->Debug_PlaceInstant(FName(Def), FVector(Loc.X, Loc.Y, 0.0), Level);
		return LastIdOf(Def);
	};
	const auto Send = [&](const FRHCommand& C)
	{
		const int32 R0 = Rejects.Num();
		Sim->EnqueueCommand(C);
		for (int32 K = 0; K < 10 && Sim->GetUplinkQueue().Num() > 0; ++K) { EraOnce(); }
		return Rejects.Num() == R0;
	};
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("deepcommutebase"), SaveErr);
	int32 LanderId = 0;
	const auto Reset = [&]()
	{
		FString E;
		const bool bOk = Sim->LoadColony(TEXT("deepcommutebase"), E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("DEEPCOMMUTE FAIL: loading the base failed: %s"), *E); ++Fails; }
		Rejects.Reset();
		Sim->Debug_ForgetRobotTransit();
		Sim->Debug_SetGrowthEnabled(false);
		LanderId = LastIdOf(TEXT("Lander"));
		return bOk;
	};
	const auto SolNow = [&]() { return FMath::FloorToInt32(Clock->GetSimSecondsTotal() / SolLen); };

	// --- The lattice. Shaft 0's head is the origin (2000, 2000) cm; cell (x, y) on a floor is at origin + 1000 x (x, y). ---
	const FVector Head0(2000.0, 2000.0, 0.0);
	const FIntPoint Column1(34, 1); // shaft 1: 34 cells east, 1 north
	const auto Origin = [&]() { return Sim->GetShaftHeadCm(); };
	const auto PtCm = [&](int32 X, int32 Y) { return URHSimWorldSubsystem::LatticeToCm(Origin(), FIntPoint(X, Y)); };
	const auto Ord = [&](int32 Level, int32 X, int32 Y) { return Sim->FindFloorCell(Level, FIntPoint(X, Y)); };
	const auto HeadInt = [&](int32 S) { const FVector H = Sim->GetShaft(S).HeadCm; return FIntPoint(FMath::RoundToInt32(H.X), FMath::RoundToInt32(H.Y)); };
	const auto SurfaceCm = [&](const FIntPoint& A, const FIntPoint& B) -> int64
	{
		if (A == B) { return 0; }
		const int64 Dx = (int64)B.X - A.X, Dy = (int64)B.Y - A.Y;
		return RHRoads::RoundSqrtCm(Dx * Dx + Dy * Dy);
	};
	const auto BuildingInt = [&](int32 Id) { const FRHBuildingInstance* Bd = FindB(Id); return Bd ? FIntPoint(FMath::RoundToInt32(Bd->LocationCm.X), FMath::RoundToInt32(Bd->LocationCm.Y)) : FIntPoint(INT32_MIN, 0); };
	const auto ShapeCmd = [&](FName Kind, int32 Level, int32 Floors, int32 X, int32 Y)
	{
		FRHCommand C;
		C.Verb = FName("PaintDistrict");
		C.Target = Kind;
		C.Level = Level;
		C.Value = Floors;
		C.Location = PtCm(X, Y);
		C.LocationB = PtCm(X, Y);
		return C;
	};
	// Everything a layout needs. Shaft 0 digs 12 cells on -1 around (0,0): ordinals 0..11 = (1,0) (1,1) (0,1) (-1,1) (-1,0) (-1,-1) (0,-1) (1,-1)
	// (2,-1) (2,0) (2,1) (2,2), one 4-connected compartment, the landing (1,0) = ordinal 0. Shaft 1 at (34,1) digs 6 cells: ordinals 12..17 =
	// (35,1) (35,2) (34,2) (33,2) (33,1) (33,0), the landing (35,1) = ordinal 12. bTunnel lays a 30-cell tunnel (3,0) to (32,0) between them and
	// carves TunnelCells of it (ordinals 18.. in carve order). Depth 2 also digs 6 cells under each shaft on -2 and rates it. bShape paints a
	// one-point Homes shape far away (60, 60), which maps every floor it spans: no other cell is in any district.
	const auto Build = [&](bool bTunnel, int32 Depth, bool bShape, int32 TunnelCells = 30) -> bool
	{
		Reset();
		Sim->ExtendShaft(Depth, Head0);
		bool bOk = Sim->ExcavateFloor(-1, 12, R, 0);
		Place(TEXT("Pylon"), PtCm(Column1.X, Column1.Y - 1), 0);
		int32 Id = INDEX_NONE;
		bOk &= Sim->Debug_SinkShaftAt(PtCm(Column1.X, Column1.Y), Depth, Id, Why) && Id == 1;
		bOk &= Sim->ExcavateFloor(-1, 6, R, 1);
		if (Depth >= 2)
		{
			bOk &= Sim->ExcavateFloor(-2, 6, R, 0) && Sim->ExcavateFloor(-2, 6, R, 1);
		}
		if (bTunnel)
		{
			int32 TId = INDEX_NONE;
			bOk &= Sim->Debug_DigTunnel(-1, FIntPoint(3, 0), FIntPoint(32, 0), TId, Why) && TId == 1 && Sim->Debug_CarveTunnelCells(TId, TunnelCells) == TunnelCells;
		}
		for (int32 L = 1; L <= Depth; ++L)
		{
			Place(TEXT("AirFilter"), PtCm(-1, -1), -L);
			Sim->Debug_ForceRateFloor(-L);
		}
		Sim->Debug_SetColonistsPerCell(100.0);
		if (bShape) { bOk &= Send(ShapeCmd(NHomes, -1, Depth, 60, 60)); }
		else { EraOnce(); EraOnce(); }
		Sim->Debug_ClearColonistStations();
		return bOk && Sim->IsFloorMapped(-1) == bShape;
	};
	// N residents of floor -1 (the base's fill houses them; every home is set by hand), CellsPer the legacy bed slots per cell once they are in: resident k
	// (by Id) lives in the cell the k-th slot is in, so at 1.0 resident k lives in cell k.
	const auto AddResidents = [&](int32 N, double CellsPer, int32 HomeLevel = -1) -> bool
	{
		Sim->Debug_SetColonistsPerCell(100.0); // beds enough to house them
		const int32 Before = Sim->GetPopulation();
		const int32 Got = Sim->Debug_AddColonists(N);
		Sim->Debug_SetColonistsPerCell(CellsPer);
		const TArray<FRHColonist>& All = Sim->GetColonists();
		for (int32 i = Before; i < All.Num(); ++i) { Sim->Debug_SetColonistHome(All[i].Id, 0, HomeLevel); }
		return Got == N;
	};
	const auto ColonistAt = [&](int32 Index) { return Sim->GetColonists()[Index].Id; };
	const auto SeatOnFloor = [&](int32 Index, int32 Level, int32 Cell) { return Sim->Debug_SetColonistStation(ColonistAt(Index), FIntVector(Level, Cell, 0)); };
	const auto SeatOnSurface = [&](int32 Index, int32 BuildingId) { return Sim->Debug_SetColonistStation(ColonistAt(Index), FIntVector(0, INDEX_NONE, BuildingId)); };
	const auto CommuteOf = [&](int32 Index) { return Sim->GetColonistCommuteCm(ColonistAt(Index)); };
	const auto FullRamp = [&]() { Sim->Debug_SetCommuteTuning(0.0, 1.0, 30000.0, 6000.0); }; // ramp 1 from the first colonist
	const auto PairOf = [&](int32 HomeCell, int32 WorkCell, URHSimWorldSubsystem::FRHCommutePair& Out) -> bool
	{
		TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
		Sim->GetCommutePairs(Pairs);
		for (const URHSimWorldSubsystem::FRHCommutePair& P : Pairs) { if (P.HomeCell == HomeCell && P.WorkCell == WorkCell) { Out = P; return true; } }
		return false;
	};
	const auto PairToSurface = [&](int32 HomeCell, URHSimWorldSubsystem::FRHCommutePair& Out) -> bool
	{
		TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
		Sim->GetCommutePairs(Pairs);
		for (const URHSimWorldSubsystem::FRHCommutePair& P : Pairs) { if (P.HomeCell == HomeCell && P.WorkLevel == 0) { Out = P; return true; } }
		return false;
	};
	const auto Fit = [&](int32 S)
	{
		Sim->Debug_AddSolidAt(LanderId, NStruct, Sim->GetLiftFitBillKg(), false);
		FString W;
		const bool bOk = Sim->Debug_FitLift(S, W);
		if (!bOk) { UE_LOG(LogRedHopeSim, Warning, TEXT("DEEPCOMMUTE: fit on shaft %d refused: %s"), S, *W); }
		return bOk;
	};
	// The lowest-ordinal carved 4-neighbour of a shaft's column on a floor (INDEX_NONE: none), found here without the sim's walk network.
	const auto OracleLanding = [&](int32 Level, int32 S) -> int32
	{
		int32 Best = INDEX_NONE;
		const FIntPoint Col = Sim->GetShaft(S).Column;
		for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
		{
			const int32 O = Sim->FindFloorCell(Level, Col + Step);
			if (O != INDEX_NONE && (Best == INDEX_NONE || O < Best)) { Best = O; }
		}
		return Best;
	};
	// The expected trips on floor -1, all wait-free and with no car (a ride costs the 6,000 cm a floor): same floor, and floor to the surface.
	const auto Up = [&](int32 Levels) { return (int64)Levels * Sim->GetCommuteShaftCmPerFloor(); };
	const auto ExpectSameFloor = [&](const TArray<TArray<int64>>& D, int32 H, int32 W) -> int64
	{
		const int64 Direct = D[H][W];
		int64 Via = MAX_int64;
		for (int32 S1 = 0; S1 < Sim->GetShaftCount(); ++S1)
		{
			for (int32 S2 = 0; S2 < Sim->GetShaftCount(); ++S2)
			{
				const int32 L1 = OracleLanding(-1, S1), L2 = OracleLanding(-1, S2);
				if (S1 == S2 || L1 == INDEX_NONE || L2 == INDEX_NONE || D[H][L1] == MAX_int64 || D[L2][W] == MAX_int64) { continue; }
				Via = FMath::Min(Via, D[H][L1] + Up(1) + SurfaceCm(HeadInt(S1), HeadInt(S2)) + Up(1) + D[L2][W]);
			}
		}
		return (Direct != MAX_int64 && Direct <= Via) ? Direct : Via;
	};
	const auto ExpectToSurface = [&](const TArray<TArray<int64>>& D, int32 H, const FIntPoint& P, int32& OutShaft) -> int64
	{
		int64 Best = MAX_int64;
		OutShaft = INDEX_NONE;
		for (int32 S = 0; S < Sim->GetShaftCount(); ++S)
		{
			const int32 L1 = OracleLanding(-1, S);
			if (L1 == INDEX_NONE || D[H][L1] == MAX_int64) { continue; }
			const int64 Cm = D[H][L1] + Up(1) + SurfaceCm(HeadInt(S), P);
			if (OutShaft == INDEX_NONE || Cm < Best) { Best = Cm; OutShaft = S; }
		}
		return Best;
	};

	Expect(bBaseSaved, FString::Printf(TEXT("DC0 the base colony is saved for the fixtures (%s)"), *SaveErr));
	{
		Reset();
		uint32 KT = 7, KS = 7;
		Sim->Debug_DeepWalkKey(KT, KS);
		uint32 MC = 7, MB = 7, CC = 7;
		Sim->Debug_DeepKeys(MC, MB, CC);
		Expect(KT == 0 && KS == 0 && MC == 0 && MB == 0 && CC == 0 && !Sim->IsFloorMapped(-1) && Sim->GetDeepWalkBuilds() == 0,
			TEXT("DC0 with no mapped floor the commute key's walk fields are 0, and no walk network was ever built (RC 6)"));
	}

	// --- DC1. Hand integers. ---
	{
		// (a) One compartment. Twelve residents live in cells 0..11 (one bed each); resident 0 at (1,0) works in cell 11 at (2,2): dx 1, dy 2.
		Expect(Build(false, 1, true) && AddResidents(12, 1.0) && Sim->GetColonistHomeCell(ColonistAt(0)) == 0 && Sim->GetColonistHomeCell(ColonistAt(11)) == 11, TEXT("DC1 layout A: two digs on -1, no tunnel, a shape maps the floor; twelve residents live one to a cell"));
		FullRamp();
		for (int32 k = 0; k < 12; ++k) { SeatOnFloor(k, -1, k == 0 || k == 4 ? 11 : k); }
		Expect(CommuteOf(0) == 1000 * 2 + 414 * 1 && CommuteOf(0) == 2414 && CommuteOf(4) == 1000 * 3 + 414 * 2 && CommuteOf(4) == 3828 && CommuteOf(7) == 0,
			FString::Printf(TEXT("DC1 inside one compartment the walk is the octile line: (1,0) to (2,2) is 1000 x 2 + 414 x 1 = 2,414 cm (%lld), (-1,0) to (2,2) is 1000 x 3 + 414 x 2 = 3,828 cm (%lld), a seat in the resident's own cell 0 (%lld)"), CommuteOf(0), CommuteOf(4), CommuteOf(7)));
		Expect(Sim->GetCommuteWalked() == 12 && Sim->GetCommuteOffNet() == 0 && Sim->Debug_WalkCm(-1, 0, 11) == 2414 && Sim->Debug_WalkCm(-1, 11, 0) == 2414,
			TEXT("DC1 all twelve trips are walked and none is off the net; the walk is symmetric"));

		// (b) Across a 30-cell tunnel. Resident 0 at (1,0) works at ordinal 13 = (35,2): out by the door (2,0) 1,000, onto the chain 1,000, 29 steps along it
		// 29,000, off it by the door (33,0) 1,000, then the octile (33,0) to (35,2): 2,000 + 414 x 2 = 2,828.
		Expect(Build(true, 1, true) && AddResidents(18, 1.0) && Ord(-1, 35, 2) == 13 && Ord(-1, 3, 0) == 18 && Ord(-1, 33, 0) == 17 && Sim->GetFloorCarvedCells(-1) == 48, TEXT("DC1 layout B: the same digs joined by a 30-cell tunnel from (3,0) to (32,0); eighteen residents live one to a cell"));
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		const int64 Across = 1000 + 1000 + 29000 + 1000 + (2000 + 414 * 2);
		const int64 ViaShafts = 0 + Up(1) + SurfaceCm(HeadInt(0), HeadInt(1)) + Up(1) + 1000;
		Expect(Across == 34828 && CommuteOf(0) == Across && ViaShafts > Across,
			FString::Printf(TEXT("DC1 across the tunnel: 1,000 + 1,000 + 29,000 + 1,000 + 2,828 = 34,828 cm (%lld); the two-shaft way (%lld) is longer, so the walk wins"), CommuteOf(0), ViaShafts));
		Expect(Sim->Debug_WalkCm(-1, Ord(-1, 2, 0), Ord(-1, 33, 0)) == 1000 + 29000 + 1000 && Sim->Debug_WalkCm(-1, Ord(-1, 3, 0), Ord(-1, 32, 0)) == 29000 && Sim->Debug_WalkCm(-1, Ord(-1, 3, 0), Ord(-1, 5, 5)) == -1,
			FString::Printf(TEXT("DC1 the chain is walked cell by cell: door to door 1,000 + 29,000 + 1,000 = 31,000 (%lld), end to end of the line 29,000 (%lld); a point in rock (5,5) is no cell (%lld)"),
				Sim->Debug_WalkCm(-1, Ord(-1, 2, 0), Ord(-1, 33, 0)), Sim->Debug_WalkCm(-1, Ord(-1, 3, 0), Ord(-1, 32, 0)), Sim->Debug_WalkCm(-1, Ord(-1, 3, 0), Ord(-1, 5, 5))));

		// (c) Between unjoined digs, through two shafts. Resident 0 at the landing (1,0) works at (35,2) in the other dig: up shaft 0 (6,000),
		// across the surface from head 0 to head 1 (34,015), down shaft 1 (6,000), then (35,1) to (35,2) (1,000). R2: D4 read this as 0 cm.
		Expect(Build(false, 1, true) && AddResidents(18, 1.0), TEXT("DC1 layout A again with eighteen residents (the second dig's cells are 12..17)"));
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		const int64 Apart = 0 + 6000 + SurfaceCm(HeadInt(0), HeadInt(1)) + 6000 + 1000;
		Expect(Sim->Debug_WalkCm(-1, 0, 13) == -1 && SurfaceCm(HeadInt(0), HeadInt(1)) == 34015 && Apart == 47015 && CommuteOf(0) == 47015,
			FString::Printf(TEXT("DC1 between unjoined digs: 0 + 6,000 + 34,015 + 6,000 + 1,000 = 47,015 cm (%lld) - no walk joins them, so it is two shafts and the surface (R2: not 0 m any more)"), CommuteOf(0)));
		Sim->Debug_SetCommuteTuning(0.0, 1.0, 30000.0, 100.0);
		Expect(CommuteOf(0) == 0 + 100 + 34015 + 100 + 1000, FString::Printf(TEXT("DC1 the ride is priced at the floor's own rate: 100 cm a floor gives 35,215 cm (%lld)"), CommuteOf(0)));
		FullRamp();
		Expect(CommuteOf(0) == 47015 && Sim->GetCommuteOffNet() == 0, TEXT("DC1 the price returns with the rate; the trip was never off the net"));
		// R2 itself: the same colony with no shape keeps D4's 0.
		Expect(Build(false, 1, false) && AddResidents(18, 1.0), TEXT("DC1 the same layout with no shape: nothing is mapped"));
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		Expect(CommuteOf(0) == 0 && CommuteOf(5) == 0 && Sim->GetCommuteWalked() == 0 && Sim->GetCommuteOffNet() == 0,
			FString::Printf(TEXT("DC1 with no shape on the floor the commute is D4's, 0 cm between digs (%lld): the switch is the first shape on a floor"), CommuteOf(0)));
	}

	// --- DC1 sweep: every (home cell, seat cell) pair of two layouts against the oracle. ---
	for (const bool bTunnel : { true, false })
	{
		Build(bTunnel, 1, true);
		AddResidents(18, 1.0);
		FullRamp();
		const TArray<TArray<int64>> D = OracleWalk(Sim, -1);
		const int32 Cells = Sim->GetFloorCarvedCells(-1);
		int32 Compared = 0, Wrong = 0, Connected = 0;
		FString FirstBad;
		for (int32 W = 0; W < Cells; ++W)
		{
			for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, W); }
			for (int32 k = 0; k < 18; ++k)
			{
				const int64 Want = ExpectSameFloor(D, k, W);
				const int64 Got = CommuteOf(k);
				++Compared;
				Connected += D[k][W] != MAX_int64 ? 1 : 0;
				if (Got != Want) { ++Wrong; if (FirstBad.IsEmpty()) { FirstBad = FString::Printf(TEXT("home %d seat %d: got %lld, oracle %lld"), k, W, Got, Want); } }
			}
		}
		Expect(Compared == 18 * Cells && Wrong == 0 && Connected > 0,
			FString::Printf(TEXT("DC1 sweep (%s): all %d (home, seat) pairs of the %d-cell floor match the independent oracle, %d of them joined on foot (%s)"), bTunnel ? TEXT("tunnelled") : TEXT("unjoined"), Compared, Cells, Connected, *FirstBad));
		Expect(Sim->GetCommuteOffNet() == 0 && Sim->Debug_CommuteCachesFresh() && Sim->Debug_DeepWalkFresh(), TEXT("DC1 sweep: none off the net, the caches fresh"));
	}

	// --- DC2. Floor to the surface picks walk plus ride. A Stockpile stands 17,000 cm east of head 0 (and 17,029 cm from head 1). ---
	{
		Build(true, 1, true);
		AddResidents(18, 1.0);
		FullRamp();
		const int32 Work = Place(TEXT("Stockpile"), FVector(Head0.X + 17000.0, Head0.Y, 0.0), 0);
		for (int32 k = 0; k < 18; ++k) { SeatOnSurface(k, Work); }
		const TArray<TArray<int64>> D = OracleWalk(Sim, -1);
		const FIntPoint P = BuildingInt(Work);
		int32 Wrong = 0, Via1 = 0, Via0 = 0;
		FString Bad;
		for (int32 k = 0; k < 18; ++k)
		{
			int32 WantShaft = INDEX_NONE;
			const int64 Want = ExpectToSurface(D, k, P, WantShaft);
			URHSimWorldSubsystem::FRHCommutePair Pair;
			const bool bPair = PairToSurface(k, Pair);
			Via1 += WantShaft == 1 ? 1 : 0;
			Via0 += WantShaft == 0 ? 1 : 0;
			if (CommuteOf(k) != Want || !bPair || Pair.ShaftId != WantShaft || Pair.ShaftId2 != INDEX_NONE) { ++Wrong; if (Bad.IsEmpty()) { Bad = FString::Printf(TEXT("resident %d: got %lld (shaft %d), want %lld (shaft %d)"), k, CommuteOf(k), bPair ? Pair.ShaftId : -9, Want, WantShaft); } }
		}
		Expect(Wrong == 0 && SurfaceCm(HeadInt(0), P) == 17000 && SurfaceCm(HeadInt(1), P) == 17029 && Via0 > 0 && Via1 > 0,
			FString::Printf(TEXT("DC2 all 18 residents ride the shaft that makes the whole trip shortest (walk + 6,000 + the surface leg): %d through shaft 0, %d through shaft 1, though the workplace is nearer head 0 than head 1 by 29 cm (%s)"), Via0, Via1, *Bad));
		const int64 By0 = D[13][OracleLanding(-1, 0)] + 6000 + 17000, By1 = D[13][OracleLanding(-1, 1)] + 6000 + 17029;
		URHSimWorldSubsystem::FRHCommutePair P13;
		Expect(By1 < By0 && PairToSurface(13, P13) && P13.ShaftId == 1 && CommuteOf(13) == By1 && P13.HomePosCm == HeadInt(1) && P13.WorkPosCm == P,
			FString::Printf(TEXT("DC2 the resident of (35,2) walks 1,000 cm and rides shaft 1: %lld cm, against %lld through shaft 0; the shuttle's anchor is head 1"), By1, By0));
		URHSimWorldSubsystem::FRHCommutePair P0;
		Expect(PairToSurface(0, P0) && P0.ShaftId == 0 && CommuteOf(0) == 0 + 6000 + 17000 && P0.HomePosCm == HeadInt(0) && P0.EdgeIds.Num() == 0,
			FString::Printf(TEXT("DC2 the resident at landing 0 rides shaft 0: 0 + 6,000 + 17,000 = 23,000 cm (%lld), anchored at head 0"), CommuteOf(0)));
		// A surface home to a floor seat is the same trip the other way: a nineteenth colonist lives in the Stockpile and sits in cell 13.
		Expect(AddResidents(1, 1.0), TEXT("DC2 a nineteenth resident"));
		for (int32 k = 0; k < 18; ++k) { SeatOnSurface(k, Work); } // adding a resident re-derives the seats: they are laid again
		Sim->Debug_SetColonistHome(ColonistAt(18), Work, 0);
		SeatOnFloor(18, -1, 13);
		URHSimWorldSubsystem::FRHCommutePair Back;
		TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
		Sim->GetCommutePairs(Pairs);
		bool bBack = false;
		for (const URHSimWorldSubsystem::FRHCommutePair& Pr : Pairs) { if (Pr.HomeLevel == 0 && Pr.WorkCell == 13) { Back = Pr; bBack = true; } }
		Expect(bBack && CommuteOf(18) == By1 && Back.ShaftId == 1 && Back.WorkPosCm == HeadInt(1) && Back.HomePosCm == P,
			FString::Printf(TEXT("DC2 a resident who lives at the surface and sits on the floor makes the same trip backwards: %lld cm (%lld), by shaft 1"), CommuteOf(18), By1));
		Expect(Sim->GetCommuteOffNet() == 0 && Sim->Debug_CommuteCachesFresh(), TEXT("DC2 none off the net, the caches fresh"));
	}

	// --- DC3. A crowded shaft does not flip the route, this sol or the next. A car on shaft 0 (2,000 cm a floor); the Stockpile stands 60 cm east of head 0. ---
	{
		Build(true, 1, true);
		AddResidents(18, 1.0);
		FullRamp();
		const int32 Work = Place(TEXT("Stockpile"), FVector(Head0.X + 60.0, Head0.Y, 0.0), 0);
		for (int32 k = 0; k < 18; ++k) { SeatOnSurface(k, Work); }
		const FIntPoint P = BuildingInt(Work);
		Expect(Fit(0) && Sim->HasTransit(), TEXT("DC3 setup: the colony's first car goes on shaft 0"));
		const int64 Per0 = Sim->GetCommuteLiftCmPerFloor(), Per1 = 6000;
		const int64 Walk13To0 = Sim->Debug_WalkCm(-1, 13, Sim->Debug_WalkLanding(-1, 0)), Walk13To1 = Sim->Debug_WalkCm(-1, 13, Sim->Debug_WalkLanding(-1, 1));
		const int64 Cost0 = Walk13To0 + Per0 + SurfaceCm(HeadInt(0), P), Cost1 = Walk13To1 + Per1 + SurfaceCm(HeadInt(1), P);
		URHSimWorldSubsystem::FRHCommutePair Before;
		Expect(Cost0 < Cost1 && Cost1 - Cost0 > 100 && PairToSurface(13, Before) && Before.ShaftId == 0 && CommuteOf(13) == Cost0 && Sim->GetTransitWaitSec(0) == 0,
			FString::Printf(TEXT("DC3 before the crowd the resident of (35,2) rides shaft 0: %lld cm against %lld through shaft 1 (the lift's 2,000 a floor wins by %lld), no wait"), Cost0, Cost1, Cost1 - Cost0));
		const int32 S = SolNow();
		Sim->Debug_SetTransitCapacity(200, 100);
		Sim->Debug_ChargeTransit(0, 5000, S * SolLen + 1.0); // a crowd on shaft 0 in sol s
		Sim->Debug_RollTransit(S + 2);
		const int32 Wait0 = Sim->GetTransitWaitSec(0);
		const int64 WaitCm = (int64)Wait0 * Sim->GetCommuteWaitCmPerSec();
		URHSimWorldSubsystem::FRHCommutePair Crowded;
		Expect(Wait0 > 0 && WaitCm > Cost1 - Cost0 && PairToSurface(13, Crowded) && Crowded.ShaftId == 0 && CommuteOf(13) == Cost0 + WaitCm,
			FString::Printf(TEXT("DC3 the crowd: shaft 0 waits %d s = %lld cm, more than the %lld cm lead - a choice that read waits would flip to shaft 1 now; this one stays on shaft 0 and pays %lld cm (%lld + the wait)"), Wait0, WaitCm, Cost1 - Cost0, CommuteOf(13), Cost0));
		Sim->Debug_RollTransit(S + 3); // the next sol: the crowd is gone from the slot it reads, the wait falls back
		const int32 Wait1 = Sim->GetTransitWaitSec(0);
		URHSimWorldSubsystem::FRHCommutePair Next;
		Expect(PairToSurface(13, Next) && Next.ShaftId == 0 && CommuteOf(13) == Cost0 + (int64)Wait1 * Sim->GetCommuteWaitCmPerSec() && Wait1 < Wait0,
			FString::Printf(TEXT("DC3 the next sol the wait is %d s and the route is the same shaft 0 (%lld cm)"), Wait1, CommuteOf(13)));
		Expect(Sim->Debug_CommuteCachesFresh() && Sim->GetCommuteOffNet() == 0, TEXT("DC3 the caches are fresh and none is off the net"));
	}

	// --- DC4. ShaftPeople counts a colonist once for each shaft ridden. Depth 2: both shafts dig under themselves on -2 as well; a car on shaft 1. ---
	{
		Build(false, 2, true);
		AddResidents(18, 1.0);
		FullRamp();
		const int32 Work = Place(TEXT("Stockpile"), FVector(Head0.X + 17000.0, Head0.Y, 0.0), 0);
		const int32 CellOnTwo = Ord(-2, 1, 0); // shaft 0's dig on -2
		// Residents 0-2: unjoined dig B on the same floor (two shafts); 3-5: the surface (one); 6-8: floor -2 under shaft 0 (one, the shaft that landed on both).
		// 9-11: dig B again. 12-17 live in dig B and sit in their own cells (no trip).
		for (int32 k = 0; k < 18; ++k)
		{
			if (k < 3 || (k >= 9 && k < 12)) { SeatOnFloor(k, -1, 13); }
			else if (k < 6) { SeatOnSurface(k, Work); }
			else if (k < 9) { SeatOnFloor(k, -2, CellOnTwo); }
			else { SeatOnFloor(k, -1, k); }
		}
		Expect(Fit(1) && Sim->HasTransit() && Sim->IsFloorMapped(-2) && CellOnTwo == 0, TEXT("DC4 setup: a car on shaft 1; floors -1 and -2 are mapped"));
		Expect(Sim->GetCommuteShaftPeople(0) == 6 + 3 + 3 && Sim->GetCommuteShaftPeople(1) == 6 && Sim->GetCommuteWalked() == 18 && Sim->GetCommuteOffNet() == 0,
			FString::Printf(TEXT("DC4 shaft 0 carries 6 (dig to dig) + 3 (the surface) + 3 (to -2) = 12 and shaft 1 the 6 who come down it: %d and %d"), Sim->GetCommuteShaftPeople(0), Sim->GetCommuteShaftPeople(1)));
		// A trip to -2 rides the one shaft that landed on both floors, once: the walk to its landing, a floor, the walk on -2.
		const int64 Want = 1000 + 414 + 6000 + 0; // by hand: (0,-1) to (1,0) is one diagonal, 1,414 cm; a floor on the hoist; the seat is the landing cell itself
		URHSimWorldSubsystem::FRHCommutePair To2;
		Expect(CommuteOf(6) == Want && PairOf(6, CellOnTwo, To2) && To2.ShaftId == 0 && To2.ShaftId2 == INDEX_NONE && To2.HomeLevel == -1 && To2.WorkLevel == -2,
			FString::Printf(TEXT("DC4 floor to floor through the shaft both floors share: the walk (0,-1) to the landing (1,0) 1,414 + 6,000 + 0 = %lld cm, one shaft, once"), CommuteOf(6)));
		URHSimWorldSubsystem::FRHCommutePair ToB;
		Expect(PairOf(0, 13, ToB) && ToB.ShaftId == 0 && ToB.ShaftId2 == 1 && ToB.Count == 1,
			TEXT("DC4 dig to dig names both shafts: up shaft 0, down shaft 1"));
	}

	// --- DC5. Off the net, and the sort. ---
	{
		// (a) Eighteen cells, twenty residents at one bed a cell: residents 18 and 19 have no home cell. They keep the head anchor with no walk.
		Build(false, 1, true);
		AddResidents(20, 1.0);
		FullRamp();
		const int32 Work = Place(TEXT("Stockpile"), FVector(Head0.X + 17000.0, Head0.Y, 0.0), 0);
		for (int32 k = 0; k < 20; ++k) { SeatOnSurface(k, Work); }
		const int64 Anchor = Sim->GetCommuteDistanceCm(-1, HeadInt(0), 0, BuildingInt(Work));
		Expect(Sim->GetColonistHomeCell(ColonistAt(18)) == INDEX_NONE && Sim->GetColonistHomeCell(ColonistAt(19)) == INDEX_NONE && Sim->GetColonistHomeCell(ColonistAt(17)) == 17
			&& Sim->GetCommuteOffNet() == 2 && Sim->GetCommuteWalked() == 18 && CommuteOf(18) == Anchor && CommuteOf(19) == Anchor && CommuteOf(0) == 23000,
			FString::Printf(TEXT("DC5 two residents past the last bed have no home cell: they keep D4's anchor trip (%lld cm: the cheapest shaft's floor plus its surface leg), 2 off the net and 18 walked (%d, %d)"), CommuteOf(18), Sim->GetCommuteOffNet(), Sim->GetCommuteWalked()));
		// (b) A seat on an unmapped floor: floor -2 is dug and rated but holds no shape (the shape spans -1 only).
		Build(false, 2, false);
		Sim->Debug_ClearColonistStations();
		Send(ShapeCmd(NHomes, -1, 1, 60, 60));
		AddResidents(6, 1.0);
		FullRamp();
		for (int32 k = 0; k < 6; ++k) { SeatOnFloor(k, -2, k % 3); }
		Expect(Sim->IsFloorMapped(-1) && !Sim->IsFloorMapped(-2) && Sim->GetCommuteOffNet() == 6 && Sim->GetCommuteWalked() == 0,
			FString::Printf(TEXT("DC5 a seat on an unmapped floor leaves the trip off the net - the whole trip, with a mapped home (%d off the net)"), Sim->GetCommuteOffNet()));
		Expect(CommuteOf(0) == Sim->GetCommuteDistanceCm(-1, HeadInt(0), -2, HeadInt(0), 0) && CommuteOf(0) == 6000 && CommuteOf(1) == Sim->GetCommuteDistanceCm(-1, HeadInt(0), -2, HeadInt(0), 0),
			FString::Printf(TEXT("DC5 and it is D4's floor-to-floor price, 6,000 cm for a floor on a hoist (%lld)"), CommuteOf(0)));

		// (c0) An island: one cell dug on floor -2 at (20,20), joined to nothing and no shaft's column (the plan order, then a carve). Its
		// resident has a home cell but no door path to any landing (D3 R6): off the net, the head anchor, D4's price, while the twelve others
		// of floor -2 walk to the surface.
		Build(false, 2, true);
		FString PaintWhy;
		const bool bPainted = Sim->Debug_PaintCarve(-2, FIntPoint(20, 20), PaintWhy);
		const bool bCarved = bPainted && Sim->ExcavateFloor(-2, 1, R, 1) && Ord(-2, 20, 20) == 12; // (20,20) is nearer shaft 1: its Borer owns the planned cell
		AddResidents(13, 1.0, -2);
		FullRamp();
		const int32 IslandWork = Place(TEXT("Stockpile"), FVector(Head0.X + 17000.0, Head0.Y, 0.0), 0);
		for (int32 k = 0; k < 13; ++k) { SeatOnSurface(k, IslandWork); }
		const int64 IslandAnchor = Sim->GetCommuteDistanceCm(-2, HeadInt(0), 0, BuildingInt(IslandWork));
		Expect(bCarved && Sim->GetColonistHomeCell(ColonistAt(12)) == 12 && Sim->Debug_WalkLanding(-2, 0) == 0 && Sim->Debug_WalkCm(-2, 12, 0) == -1,
			FString::Printf(TEXT("DC5 setup: a lone cell (20,20) is dug on -2 as ordinal 12 and the 13th resident of -2 lives in it; no walk reaches a landing (%s)"), *PaintWhy));
		Expect(Sim->GetCommuteOffNet() == 1 && Sim->GetCommuteWalked() == 12 && CommuteOf(12) == IslandAnchor && IslandAnchor == 2 * 6000 + 17000 && CommuteOf(0) == 0 + 2 * 6000 + 17000,
			FString::Printf(TEXT("DC5 the resident of the island keeps D4's anchor trip, %lld cm (two floors up and the cheapest head's surface leg), 1 off the net and 12 walked (%d, %d)"), CommuteOf(12), Sim->GetCommuteOffNet(), Sim->GetCommuteWalked()));

		// (c) The sort. Two residents share cell 0 (two beds a cell) and their seats are in cells 7 and 3 of the same dig: both walks are direct,
		// both anchors are shaft 0's head and both districts are 0, so the two trips differ ONLY in their cells. Whichever resident has which
		// seat, the trips come out in the same order.
		FString Order[2];
		FString Digest[2];
		for (int32 Swap = 0; Swap < 2; ++Swap)
		{
			Build(false, 1, true);
			AddResidents(8, 2.0);
			FullRamp();
			for (int32 k = 0; k < 8; ++k) { SeatOnFloor(k, -1, k >= 2 ? 3 + k : (k == Swap ? 7 : 3)); }
			TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
			Sim->GetCommutePairs(Pairs);
			for (const URHSimWorldSubsystem::FRHCommutePair& P : Pairs)
			{
				Order[Swap] += FString::Printf(TEXT("(%d>%d)"), P.HomeCell, P.WorkCell);
				Digest[Swap] += FString::Printf(TEXT("[%d %d %d,%d %d,%d %lld]"), P.HomeLevel, P.WorkLevel, P.HomePosCm.X, P.HomePosCm.Y, P.WorkPosCm.X, P.WorkPosCm.Y, P.DistCm);
			}
		}
		Expect(Order[0] == Order[1] && Order[0].Contains(TEXT("(0>3)(0>7)")) && Digest[0] == Digest[1],
			FString::Printf(TEXT("DC5 two trips that differ only in their cells sort by (home cell, seat cell) in both build orders: %s | %s"), *Order[0], *Order[1]));
	}

	// --- DC6. An unmapped floor in the same colony is byte-equal to a twin with no shape. ---
	{
		FString Digest[2];
		for (int32 WithShape = 0; WithShape < 2; ++WithShape)
		{
			Build(false, 2, false);
			if (WithShape) { Sim->Debug_ClearColonistStations(); Send(ShapeCmd(NHomes, -1, 1, 60, 60)); }
			else { EraOnce(); EraOnce(); }
			Sim->Debug_SetColonistsPerCell(100.0);
			// Thirty residents on floor -2 only: seats on the surface, on floor -2 and on floor -1 (the mapped one, with the shape): the last is off the net.
			const int32 Work = Place(TEXT("Stockpile"), FVector(Head0.X + 9000.0, Head0.Y, 0.0), 0);
			AddResidents(30, 100.0, -2);
			FullRamp();
			Fit(0);
			for (int32 k = 0; k < 30; ++k)
			{
				if (k % 3 == 0) { SeatOnSurface(k, Work); }
				else if (k % 3 == 1) { SeatOnFloor(k, -2, k % 6); }
				else { SeatOnFloor(k, -1, k % 6); }
			}
			const URHSimWorldSubsystem::FRHCommuteSummary Sm = Sim->GetCommuteSummary();
			Digest[WithShape] = FString::Printf(TEXT("emp%d com%d long%d max%lld pen%.12f sp%d/%d |"), Sm.Employed, Sm.Commuters, Sm.LongCommuters, Sm.MaxDistCm, Sm.Penalty, Sim->GetCommuteShaftPeople(0), Sim->GetCommuteShaftPeople(1));
			for (int32 k = 0; k < 30; ++k) { Digest[WithShape] += FString::Printf(TEXT(" %lld"), CommuteOf(k)); }
			TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
			Sim->GetCommutePairs(Pairs);
			for (const URHSimWorldSubsystem::FRHCommutePair& P : Pairs) { Digest[WithShape] += FString::Printf(TEXT(" [%d>%d %d,%d>%d,%d n%d %lld]"), P.HomeLevel, P.WorkLevel, P.HomePosCm.X, P.HomePosCm.Y, P.WorkPosCm.X, P.WorkPosCm.Y, P.Count, P.DistCm); }
			// (The flow matrix is left out: its side is the district count, and only one run has a district.)
			if (WithShape) { Expect(Sim->IsFloorMapped(-1) && !Sim->IsFloorMapped(-2) && Sim->GetCommuteOffNet() == 10 && Sim->GetCommuteWalked() == 0, FString::Printf(TEXT("DC6 the twin with the shape: floor -1 is mapped, -2 is not, the ten trips to -1 are off the net (%d)"), Sim->GetCommuteOffNet())); }
		}
		Expect(Digest[0] == Digest[1] && Digest[0].Len() > 150,
			FString::Printf(TEXT("DC6 thirty residents of an unmapped floor measure byte for byte as in the twin with no shape (summary, 30 distances, the pairs and ShaftPeople: %d characters)"), Digest[0].Len()));
	}

	// --- DC7. The ramp: the default 60 to 160. The tunnelled layout, residents in dig A, seats in dig B (34,828 cm, over the 30,000 line). ---
	{
		Build(true, 1, true);
		Sim->Debug_SetCommuteTuning(60.0, 160.0, 30000.0, 6000.0); // the defaults: the ramp opens above 60 residents and is full at 160
		AddResidents(60, 4.0);
		for (int32 k = 0; k < 60; ++k) { SeatOnFloor(k, -1, 13); }
		const int32 Rebuilds0 = Sim->GetCommuteFlowRebuilds(), Walks0 = Sim->GetDeepWalkBuilds();
		const URHSimWorldSubsystem::FRHCommuteSummary Sm60 = Sim->GetCommuteSummary();
		Expect(Sim->GetPopulation() == 60 && Sim->GetCommuteRamp() == 0.0 && Sm60.Penalty == 0.0 && Sm60.Employed == 0 && CommuteOf(0) == -1 && !Sim->IsLongCommuter(Sim->GetColonists()[0].Id)
			&& Sim->GetCommuteFlowRebuilds() == Rebuilds0 && Sim->GetDeepWalkBuilds() == Walks0,
			FString::Printf(TEXT("DC7 at 60 residents the ramp is 0: no penalty, no distance, no flows derived and no walk network built (population %d, rebuilds %d -> %d, builds %d -> %d)"), Sim->GetPopulation(), Rebuilds0, Sim->GetCommuteFlowRebuilds(), Walks0, Sim->GetDeepWalkBuilds()));
		AddResidents(1, 4.0);
		for (int32 k = 0; k < 61; ++k) { SeatOnFloor(k, -1, 13); } // adding a resident re-derives the seats: they are laid again
		const URHSimWorldSubsystem::FRHCommuteSummary Sm61 = Sim->GetCommuteSummary();
		Expect(FMath::IsNearlyEqual(Sim->GetCommuteRamp(), 0.01, 1.0e-12) && Sm61.Employed == 61 && Sm61.LongCommuters > 0 && CommuteOf(0) == 34828 && Sim->GetCommuteWalked() + Sim->GetCommuteOffNet() == 61,
			FString::Printf(TEXT("DC7 at 61 the ramp is 0.01 and every trip is measured: %d employed, %d long, resident 0 walks %lld cm"), Sm61.Employed, Sm61.LongCommuters, CommuteOf(0)));
		const double Pen61 = Sm61.Ramp * 4.0 * FMath::Min(1.0, 2.0 * (double)Sm61.LongCommuters / 61.0);
		Expect(FMath::IsNearlyEqual(Sm61.Penalty, Pen61, 1.0e-12) && Sm61.Penalty > 0.0, FString::Printf(TEXT("DC7 the Hope penalty at 61 is the ramp x the long share: %.6f"), Sm61.Penalty));
		AddResidents(99, 4.0);
		for (int32 k = 0; k < 160; ++k) { SeatOnFloor(k, -1, 13); }
		const URHSimWorldSubsystem::FRHCommuteSummary Sm160 = Sim->GetCommuteSummary();
		const double Pen160 = 1.0 * 4.0 * FMath::Min(1.0, 2.0 * (double)Sm160.LongCommuters / 160.0);
		Expect(Sim->GetPopulation() == 160 && Sim->GetCommuteRamp() == 1.0 && Sm160.Employed == 160 && FMath::IsNearlyEqual(Sm160.Penalty, Pen160, 1.0e-12) && Sm160.Penalty > Sm61.Penalty,
			FString::Printf(TEXT("DC7 at 160 the ramp is full: %d employed, %d long, penalty %.4f (at 61: %.6f)"), Sm160.Employed, Sm160.LongCommuters, Sm160.Penalty, Sm61.Penalty));
		AddResidents(1, 4.0);
		for (int32 k = 0; k < 161; ++k) { SeatOnFloor(k, -1, 13); }
		Expect(Sim->GetCommuteRamp() == 1.0 && Sim->GetCommuteSummary().Employed == 161 && Sim->Debug_CommuteCachesFresh(), TEXT("DC7 at 161 the ramp stays full and the caches are fresh"));
	}

	// --- DC8. The era band against the agent band: the same saved colony stepped a minute of sim time each way measures the same. ---
	{
		Build(true, 1, true);
		Sim->AddStock(FName("Food"), 6000.0); // a minute of sim time must not recall anyone: the roster stays 40
		Sim->AddStock(FName("Water"), 600.0);
		Sim->AddStock(FName("Oxygen"), 6000.0);
		AddResidents(40, 1.0);
		FullRamp();
		for (int32 k = 0; k < 40; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : (k * 7 + 3) % 18); }
		FString Slot = TEXT("deepcommutepair");
		FString PairErr;
		Expect(Sim->SaveColony(*Slot, PairErr), FString::Printf(TEXT("DC8 the colony is saved for both legs (%s)"), *PairErr));
		FString Leg[2];
		for (int32 Agent = 0; Agent < 2; ++Agent)
		{
			FString Le;
			if (!Sim->LoadColony(*Slot, Le) || !Le.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("DEEPCOMMUTE: leg load failed: %s"), *Le); ++Fails; }
			Sim->Debug_SetGrowthEnabled(false);
			Sim->Debug_SetColonistsPerCell(1.0);
			FullRamp();
			if (Agent) { for (int32 K = 0; K < (int32)(EraDt / (double)AgentDt); ++K) { AgentOnce(); } }
			else { EraOnce(); }
			// The step re-derives the seats from the real rooms (there are none): the hand-made seats are laid again, identically.
			Sim->Debug_ClearColonistStations();
			const int32 Roster = Sim->GetPopulation();
			for (int32 k = 0; k < Roster; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : (k * 7 + 3) % 18); }
			const URHSimWorldSubsystem::FRHCommuteSummary Sm = Sim->GetCommuteSummary();
			Leg[Agent] = FString::Printf(TEXT("pop%d emp%d com%d long%d max%lld pen%.12f walked%d off%d |"), Roster, Sm.Employed, Sm.Commuters, Sm.LongCommuters, Sm.MaxDistCm, Sm.Penalty, Sim->GetCommuteWalked(), Sim->GetCommuteOffNet());
			for (int32 k = 0; k < Sim->GetPopulation(); ++k) { Leg[Agent] += FString::Printf(TEXT(" %lld"), CommuteOf(k)); }
		}
		Expect(Leg[0] == Leg[1] && Leg[0].Contains(TEXT("pop40 ")) && Leg[0].Contains(TEXT("walked40 off0")) && Leg[0].Contains(TEXT("34828")),
			FString::Printf(TEXT("DC8 the era band and the agent band leave the same homes and measure the same 40 trips: %s"), *Leg[0].Left(120)));
	}

	// --- DC9. The caches and keys; nothing derived is saved. ---
	{
		// A tunnel one cell short of joining the digs: still two shafts. The last cell joins them, and the cached flows move with the floor's cells.
		Build(true, 1, true, 29);
		AddResidents(18, 1.0);
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		const int64 Short = CommuteOf(0);
		const uint32 CellsV = Sim->GetFloorCellsVersion();
		const int32 Walks1 = Sim->GetDeepWalkBuilds();
		CommuteOf(0);
		Expect(Short == 47015 && Sim->GetDeepWalkBuilds() == Walks1 && Walks1 >= 1,
			FString::Printf(TEXT("DC9 29 of the tunnel's 30 cells are carved and the chain stops one cell short of the second dig: still 47,015 cm (%lld), and a second read builds no new network (%d builds)"), Short, Walks1));
		const int32 CellsBefore = Sim->GetFloorCarvedCells(-1);
		const int32 Rebuilds = Sim->GetCommuteFlowRebuilds();
		const int32 Carved = Sim->Debug_CarveTunnelCells(1, 1);
		const int64 Joined = CommuteOf(0);
		Expect(Carved == 1 && Sim->GetFloorCarvedCells(-1) == CellsBefore + 1 && Sim->GetFloorCellsVersion() != CellsV && Joined == 34828 && Sim->GetCommuteFlowRebuilds() == Rebuilds + 1 && Sim->GetDeepWalkBuilds() == Walks1 + 1,
			FString::Printf(TEXT("DC9 the 30th cell is carved: the flows and the network rebuild once and the trip is the walk, 34,828 cm (%lld)"), Joined));
		Expect(Sim->Debug_CommuteCachesFresh() && Sim->Debug_DeepWalkFresh() && Sim->Debug_DistrictCachesFresh(), TEXT("DC9 the flows, the walk network and the district caches are all fresh"));
		uint32 KT = 0, KS = 0;
		Sim->Debug_DeepWalkKey(KT, KS);
		Expect(KT != 0 && KS != 0, TEXT("DC9 with a mapped floor the commute key carries the tunnels' and shafts' versions"));
		// Nothing derived is saved: the file is the same bytes before and after the commute is read.
		Build(true, 1, true);
		AddResidents(18, 1.0);
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		FString E1, E2;
		TArray<uint8> Bytes1, Bytes2;
		const int32 WalksBefore = Sim->GetDeepWalkBuilds();
		const bool bSaved1 = Sim->SaveColony(TEXT("deepcommute_a"), E1) && FFileHelper::LoadFileToArray(Bytes1, *SaveFilePath(TEXT("deepcommute_a")));
		const URHSimWorldSubsystem::FRHCommuteSummary Sm = Sim->GetCommuteSummary(); // reads the flows: builds the walk network and the trips
		TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
		Sim->GetCommutePairs(Pairs);
		const bool bSaved2 = Sim->SaveColony(TEXT("deepcommute_a"), E2) && FFileHelper::LoadFileToArray(Bytes2, *SaveFilePath(TEXT("deepcommute_a")));
		Expect(bSaved1 && bSaved2 && Bytes1.Num() > 0 && Bytes1 == Bytes2 && Sim->GetDeepWalkBuilds() > WalksBefore && Sm.Employed == 18 && Pairs.Num() > 0,
			FString::Printf(TEXT("DC9 nothing derived is saved: the %d-byte file is the same bytes after the walk network was built (%d build) and %d trips were derived (%s %s)"), Bytes1.Num(), Sim->GetDeepWalkBuilds() - WalksBefore, Pairs.Num(), *E1, *E2));
		// And the file loads back with the mapped floor and measures the same.
		const int64 Before0 = CommuteOf(0);
		FString LoadErr;
		const bool bLoaded = Sim->LoadColony(TEXT("deepcommute_a"), LoadErr) && LoadErr.IsEmpty();
		Sim->Debug_SetColonistsPerCell(1.0);
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		Expect(bLoaded && Sim->IsFloorMapped(-1) && CommuteOf(0) == Before0 && Before0 == 34828 && Sim->Debug_CommuteCachesFresh(), FString::Printf(TEXT("DC9 the colony loads back with its mapped floor and measures the same: %lld cm (%s)"), CommuteOf(0), *LoadErr));
	}

	return Finish();
}
