#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHDistricts.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include <limits>

// D6d trams with people self-test (docs/qa/2026-10-22/d6-caverns-design.md: the DIRECTOR ANSWER Q-C (a), trams carry PEOPLE ONLY, robots never
// ride; the MERGED DESIGN AND REVISION section 4 with RC 6 and RC 9). Fails loudly. `-trams`.
// A line is one LayTramLine order on a finished tunnel of a colony with a lift car: an edge between its two stops in the people's walk network,
// priced wait-free, a load a sol (2 x its commuters x the ramp) against TramPlacesPerSol rolled in StepTransit, and a saved record (v50).
// Every fixture here fits a car, so every step pins the bank to 1e9 before it runs (RC 9). Every distance is derived by hand from the lattice, or
// by an independent oracle (a Floyd-Warshall over every carved cell with the ride added as one more edge, sharing no code with the sim).
// The fixture is D6c's: shaft 0's dig on -1 (cells 0..11), shaft 1's dig 34 cells east (cells 12..17), a 30-cell tunnel (3,0) to (32,0) between
// them (cells 18..47 in carve order). Its line is (30 / 2 + 29) s x 100 cm = 5,900 cm against 29,000 cm on foot.
//  TM0 rows, caps, a fresh colony; TM1 the six refusals (and the boundaries) through the order and the preflight, the Struct bill; TM2 the edge
//  integer, the tie, the ceiling, then a sweep of every (home, seat) pair against the oracle; TM2b two lines on one floor, a route that rides both
//  (step-2 fix 1: the pruning bound), against the oracle; TM3 riders counted by route, the ramp at 60, 61 and 160; TM4 the wait is added after the route is chosen, frozen at the roll, with no oscillation; TM5 a line maps its floor; TM6 the save
//  (the layout, the round trip, every hand-made refusal, a v49 file); TM7 the era band against the agent band, rolling a real load (step-2 fix 7)
//  in two loaded legs and one that never saved; TM8 the caches, the keys, the build order and inertness.
namespace
{
	int64 OracleOct(const FIntPoint& A, const FIntPoint& B)
	{
		const int64 Dx = FMath::Abs((int64)A.X - B.X), Dy = FMath::Abs((int64)A.Y - B.Y);
		return 1000 * FMath::Max(Dx, Dy) + 414 * FMath::Min(Dx, Dy);
	}

	struct FOracleRide { FIntPoint A, B; int64 Cm = 0; };

	// All-pairs walking cm between every carved cell of one floor (MAX_int64: none): a Floyd-Warshall over the cells themselves, the walk's rules
	// written again (chain cells are the points of every tunnel; two non-chain cells of one 4-connected component are joined by their octile line;
	// a chain cell and any 4-neighbour carved cell by 1,000 cm), plus one extra edge per ride.
	TArray<TArray<int64>> OracleWalk(const URHSimWorldSubsystem* Sim, int32 Level, const TArray<FOracleRide>& Rides)
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
		for (const FOracleRide& Ride : Rides)
		{
			const int32 A = Sim->FindFloorCell(Level, Ride.A), B = Sim->FindFloorCell(Level, Ride.B);
			if (A != INDEX_NONE && B != INDEX_NONE && Ride.Cm < D[A][B]) { D[A][B] = D[B][A] = Ride.Cm; }
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

	// Counts the Error lines LogRedHopeSim prints while it is attached: a structural refusal at load must come back through OutError, never as an Error line.
	struct FRHTramErrorCounter : public FOutputDevice
	{
		int32 Count = 0;
		virtual void Serialize(const TCHAR* /*Text*/, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Verbosity == ELogVerbosity::Error && Category == FName(TEXT("LogRedHopeSim"))) { ++Count; }
		}
	};
}

int32 RHRunTramSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== TRAMS TEST (D6d, 2026-10-23) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("TRAMS ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("TRAMS FAIL: %s"), *What); ++Fails; }
	};
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FName NStruct(TEXT("Struct")), NHomes(TEXT("Homes"));
	FString R, Why;
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	FRHTramErrorCounter ErrorCounter;
	GLog->AddOutputDevice(&ErrorCounter);
	const auto Finish = [&]() -> int32
	{
		Expect(Sim->GetFloorCellFallbacks() == 0, TEXT("TM over the whole suite not one ordinal read fell outside its floor and back to the spiral"));
		GLog->RemoveOutputDevice(&ErrorCounter);
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== TRAMS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto EraOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->Debug_SetBatteryWh(1.0e9); // every fixture here fits a car: the bank is pinned before each step (RC 9)
		Sim->EraStep((float)EraDt);
	};
	const auto AgentOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds((double)AgentDt);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->Debug_StepSim(AgentDt);
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
	const auto Rd = [](const TArray<uint8>& Bytes, int32 Off) { int32 V = INDEX_NONE; if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(&V, Bytes.GetData() + Off, 4); } return V; };
	const auto PutI = [](TArray<uint8>& Bytes, int32 Off, int32 V) { if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(Bytes.GetData() + Off, &V, 4); } };
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("trambase"), SaveErr);
	int32 LanderId = 0;
	const auto Reset = [&]()
	{
		FString E;
		const bool bOk = Sim->LoadColony(TEXT("trambase"), E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("TRAMS FAIL: loading the base failed: %s"), *E); ++Fails; }
		Rejects.Reset();
		Sim->Debug_ForgetRobotTransit();
		Sim->Debug_SetGrowthEnabled(false);
		LanderId = LastIdOf(TEXT("Lander"));
		return bOk;
	};
	const auto SolNow = [&]() { return FMath::FloorToInt32(Clock->GetSimSecondsTotal() / SolLen); };

	// --- The lattice (D6c's). Shaft 0's head is the origin (2000, 2000) cm; cell (x, y) on a floor is at origin + 1000 x (x, y). ---
	const FVector Head0(2000.0, 2000.0, 0.0);
	const FIntPoint Column1(34, 1);
	const auto Origin = [&]() { return Sim->GetShaftHeadCm(); };
	const auto PtCm = [&](int32 X, int32 Y) { return URHSimWorldSubsystem::LatticeToCm(Origin(), FIntPoint(X, Y)); };
	const auto Ord = [&](int32 Level, int32 X, int32 Y) { return Sim->FindFloorCell(Level, FIntPoint(X, Y)); };
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
	const auto LayCmd = [&](int32 TunnelId)
	{
		FRHCommand C;
		C.Verb = FName("LayTramLine");
		C.Value = TunnelId;
		return C;
	};
	const auto Build = [&](bool bTunnel, int32 Depth, bool bShape) -> bool
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
			bOk &= Sim->Debug_DigTunnel(-1, FIntPoint(3, 0), FIntPoint(32, 0), TId, Why) && TId == 1 && Sim->Debug_CarveTunnelCells(TId, 30) == 30;
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
	// One more tunnel on floor -1 from lattice point A to B, with the first Carve of its cells carved now. The id, or INDEX_NONE.
	const auto DigT = [&](const FIntPoint& A, const FIntPoint& B, int32 Carve) -> int32
	{
		int32 Id = INDEX_NONE;
		FString W;
		if (!Sim->Debug_DigTunnel(-1, A, B, Id, W)) { UE_LOG(LogRedHopeSim, Warning, TEXT("TRAMS: a tunnel was refused: %s"), *W); return INDEX_NONE; }
		return Sim->Debug_CarveTunnelCells(Id, Carve) == Carve ? Id : (int32)INDEX_NONE;
	};
	const auto AddResidents = [&](int32 N, double CellsPer, int32 HomeLevel = -1) -> bool
	{
		Sim->Debug_SetColonistsPerCell(100.0);
		const int32 Before = Sim->GetPopulation();
		const int32 Got = Sim->Debug_AddColonists(N);
		Sim->Debug_SetColonistsPerCell(CellsPer);
		const TArray<FRHColonist>& All = Sim->GetColonists();
		for (int32 i = Before; i < All.Num(); ++i) { Sim->Debug_SetColonistHome(All[i].Id, 0, HomeLevel); }
		return Got == N;
	};
	const auto ColonistAt = [&](int32 Index) { return Sim->GetColonists()[Index].Id; };
	const auto SeatOnFloor = [&](int32 Index, int32 Level, int32 Cell) { return Sim->Debug_SetColonistStation(ColonistAt(Index), FIntVector(Level, Cell, 0)); };
	const auto CommuteOf = [&](int32 Index) { return Sim->GetColonistCommuteCm(ColonistAt(Index)); };
	const auto FullRamp = [&]() { Sim->Debug_SetCommuteTuning(0.0, 1.0, 30000.0, 6000.0); };
	const auto Fit = [&](int32 S)
	{
		Sim->Debug_AddSolidAt(LanderId, NStruct, Sim->GetLiftFitBillKg(), false);
		FString W;
		return Sim->Debug_FitLift(S, W);
	};
	// Struct on hand enough for Kg (nothing added when there is already enough).
	const auto EnsureStruct = [&](double Kg) { const double Have = Sim->GetRoadStructAvailableKg(); if (Have < Kg) { Sim->Debug_AddSolidAt(LanderId, NStruct, Kg - Have, false); } };
	// A line on a tunnel through the preflight and the order, with the Struct it needs on hand.
	const auto Lay = [&](int32 TunnelId) -> bool
	{
		const FRHTunnel* T = Sim->FindTunnel(TunnelId);
		if (!T) { return false; }
		EnsureStruct((double)URHSimWorldSubsystem::GetTunnelChainLength(T->A, T->B) * Sim->GetTramStructKgPerCell());
		FString W;
		return Sim->Debug_LayTramLine(TunnelId, W);
	};
	const FIntPoint StopA(3, 0), StopB(32, 0);

	Expect(bBaseSaved, FString::Printf(TEXT("TM0 the base colony is saved for the fixtures (%s)"), *SaveErr));

	// --- TM0. The rows, the caps, a fresh colony. ---
	{
		Reset();
		struct FRowPin { const TCHAR* Row; double Value; double Got; };
		const FRowPin Rows[] = {
			{ TEXT("TramMinCells"), 12.0, (double)Sim->GetTramMinCells() },
			{ TEXT("MaxTramLines"), 8.0, (double)Sim->GetMaxTramLines() },
			{ TEXT("TramStructKgPerCell"), 20.0, Sim->GetTramStructKgPerCell() },
			{ TEXT("TramHeadwaySec"), 60.0, (double)Sim->GetTramHeadwaySec() },
			{ TEXT("TramSpeedMps"), 10.0, Sim->GetTramSpeedCmPerS() / 100.0 },
			{ TEXT("TramPlacesPerSol"), 600.0, (double)Sim->GetTramPlacesPerSol() },
		};
		for (const FRowPin& Pin : Rows)
		{
			const double Dt = DefsSub ? DefsSub->GetConfigTableScalar(FName(Pin.Row), -1.0) : -1.0;
			Expect(Dt == Pin.Value && Pin.Got == Pin.Value, FString::Printf(TEXT("TM0 %s is %g in the live DT_Config and %g in the sim (the CSV says %g; pure data: this fails on drift)"), Pin.Row, Dt, Pin.Got, Pin.Value));
		}
		Expect(RH::MaxTramLinesCap == 32 && Sim->GetMaxTramLines() <= RH::MaxTramLinesCap && Sim->GetTramPlacesPerSol() > 0 && Sim->GetTramSpeedCmPerS() > 0,
			TEXT("TM0 the cap: MaxTramLines within 1..32 (the save refuses against the same cap); the capacity and the speed are above 0 (they divide)"));
		Expect(Sim->GetTramLines().Num() == 0 && Sim->GetNextTramId() == 1 && Sim->GetTramVersion() == 0 && Sim->GetTramPeople(1) == 0 && Sim->GetTramWaitSec(1) == 0 && Sim->GetTramPeopleLoad(1) == 0,
			TEXT("TM0 a fresh colony has no line, the next Id is 1, TramVersion is 0 and every reader answers 0"));
	}

	// --- TM1. The refusals. ---
	{
		Expect(Build(true, 1, false) && Sim->GetTunnels().Num() == 1 && Sim->GetTunnels()[0].B == StopB, TEXT("TM1 setup: floor -1, a finished 30-cell tunnel (3,0) to (32,0), no shape"));
		// 1. No lift car (the Struct is enough: the car is the first test).
		FString W1;
		EnsureStruct(600.0);
		const bool bNoCar = !Sim->CanLayTramLine(1, W1) && W1.Contains(TEXT("lift car"));
		const bool bNoCarOrder = !Send(LayCmd(1)) && Rejects.Last().Contains(TEXT("lift car"));
		Expect(bNoCar && bNoCarOrder && Sim->GetTramLines().Num() == 0 && Sim->GetTramVersion() == 0,
			FString::Printf(TEXT("TM1 refusal 1: no lift car, the switch of the transit system - refused at the preflight and by the order, nothing laid (%s)"), *W1));
		Expect(Fit(0) && Sim->HasTransit(), TEXT("TM1 the colony's first car goes on shaft 0"));
		// 2. Unknown, unfinished, cancelled.
		const int32 T2 = DigT(FIntPoint(3, 2), FIntPoint(14, 2), 11);   // 12 points, one cell short
		const int32 T3 = DigT(FIntPoint(3, 1), FIntPoint(13, 1), 11);   // 11 points, finished
		const int32 T4 = DigT(FIntPoint(3, -1), FIntPoint(13, -1), 5);  // 11 points, 6 left
		const int32 T5 = DigT(FIntPoint(-2, 0), FIntPoint(-12, 0), 3);  // 11 points, cancelled with 8 left
		FRHCommand Cancel5;
		Cancel5.Verb = FName("CancelTunnel");
		Cancel5.Value = T5;
		Expect(T2 == 2 && T3 == 3 && T4 == 4 && T5 == 5 && Send(Cancel5) && Sim->FindTunnel(5) && Sim->FindTunnel(5)->bCancelled, TEXT("TM1 setup: four more tunnels on -1 - #2 of 12 points one cell short, #3 of 11 finished, #4 of 11 with 6 left, #5 of 11 cancelled with 8 left"));
		FString Wn, Wu, Wc;
		Expect(!Sim->CanLayTramLine(99, Wn) && Wn.Contains(TEXT("Unknown tunnel 99")) && !Sim->CanLayTramLine(2, Wu) && Wu.Contains(TEXT("not finished")) && Wu.Contains(TEXT("1 cell(s) left"))
			&& !Sim->CanLayTramLine(5, Wc) && Wc.Contains(TEXT("cancelled")),
			FString::Printf(TEXT("TM1 refusal 2: an unknown tunnel (%s), an unfinished one with a cell left (%s), a cancelled one (%s)"), *Wn, *Wu, *Wc));
		// 3. Under TramMinCells chain points.
		FString W11;
		Expect(!Sim->CanLayTramLine(3, W11) && W11.Contains(TEXT("11 cells")) && W11.Contains(TEXT("at least 12")), FString::Printf(TEXT("TM1 refusal 3: a finished tunnel of 11 points is under TramMinCells 12 (%s)"), *W11));
		// 6. The Struct bill: short, then exactly enough. The stock on hand is what the fixture holds after the car; the per-point row is moved for the
		// refusal (21 kg a point is 630 kg) and put back (20 kg a point is the 600 kg of a 30-point line).
		const double Have0 = Sim->GetRoadStructAvailableKg();
		Sim->Debug_SetTramStructKgPerCell(21.0);
		FString Wst;
		double BillT1 = 0.0;
		const bool bShort = !Sim->CanLayTramLine(1, Wst, &BillT1) && Wst.Contains(TEXT("Insufficient Struct")) && Wst.Contains(TEXT("630 kg needed"));
		const bool bShortOrder = !Send(LayCmd(1)) && Rejects.Last().Contains(TEXT("Insufficient Struct"));
		Expect(FMath::IsNearlyEqual(Have0, 600.0, 1.0e-6) && bShort && BillT1 == 630.0 && bShortOrder && Sim->GetTramLines().Num() == 0 && FMath::IsNearlyEqual(Sim->GetRoadStructAvailableKg(), Have0, 1.0e-6),
			FString::Printf(TEXT("TM1 refusal 6: with %.0f kg on hand against a 630 kg bill the line is refused by the preflight and by the order and the stock is untouched (%s)"), Have0, *Wst));
		Sim->Debug_SetTramStructKgPerCell(20.0);
		const double HaveExact = Sim->GetRoadStructAvailableKg();
		const uint32 Version0 = Sim->GetTramVersion();
		const uint32 Districts0 = Sim->GetDistrictsVersion();
		const bool bLaid = Send(LayCmd(1));
		Expect(bLaid && Sim->GetTramLines().Num() == 1 && Sim->GetTramLines()[0].Id == 1 && Sim->GetTramLines()[0].TunnelId == 1 && Sim->GetTramLines()[0].Level == -1
			&& Sim->GetTramLines()[0].StopA == StopA && Sim->GetTramLines()[0].StopB == StopB && Sim->GetTramLines()[0].WaitSec == 0 && Sim->GetTramLines()[0].PrevWaitSec == 0
			&& FMath::IsNearlyEqual(HaveExact, 600.0, 1.0e-6) && FMath::IsNearlyEqual(Sim->GetRoadStructAvailableKg(), 0.0, 1.0e-6)
			&& Sim->GetNextTramId() == 2 && Sim->GetTramVersion() != Version0 && Sim->GetDistrictsVersion() != Districts0,
			FString::Printf(TEXT("TM1 exactly the bill is enough: line 1 is laid on tunnel 1 by the order, its stops are the tunnel's two ends, the stock falls by 600 kg to %.0f, TramVersion and DistrictsVersion move"), Sim->GetRoadStructAvailableKg()));
		// 4. One line a tunnel.
		FString Wdup;
		Expect(!Sim->CanLayTramLine(1, Wdup) && Wdup.Contains(TEXT("already has tram line 1")) && !Send(LayCmd(1)) && Sim->GetTramLines().Num() == 1,
			FString::Printf(TEXT("TM1 refusal 4: a tunnel has one line (%s)"), *Wdup));
		// 5. At most MaxTramLines. The 12th point of tunnel 2 is carved first: 12 points is the boundary of refusal 3.
		Expect(Sim->Debug_CarveTunnelCells(2, 1) == 1, TEXT("TM1 the 12th point of tunnel #2 is carved"));
		Sim->Debug_SetMaxTramLines(1);
		FString Wcap;
		Expect(!Sim->CanLayTramLine(2, Wcap) && Wcap.Contains(TEXT("No more tram lines - 1 at most")) && !Send(LayCmd(2)) && Sim->GetTramLines().Num() == 1,
			FString::Printf(TEXT("TM1 refusal 5: past MaxTramLines (set to 1 here) the second line is refused (%s)"), *Wcap));
		Sim->Debug_SetMaxTramLines(8);
		EnsureStruct(240.0);
		double Bill12 = 0.0;
		FString W12;
		Expect(Sim->CanLayTramLine(2, W12, &Bill12) && Bill12 == 240.0, FString::Printf(TEXT("TM1 refusal 3 boundary: 12 points is enough, and the bill is 12 x 20 = 240 kg (%.0f)"), Bill12));
		Expect(Send(LayCmd(2)) && Sim->GetTramLines().Num() == 2 && Sim->GetTramLines()[1].Id == 2 && Sim->GetTramLines()[1].TunnelId == 2 && Sim->GetNextTramId() == 3,
			TEXT("TM1 with the cap back at 8 the 12-point tunnel takes line 2"));
		// Garbage in the order's Value never lays anything.
		FRHCommand Bad = LayCmd(0);
		Bad.Value = std::numeric_limits<double>::quiet_NaN();
		const int32 R0 = Rejects.Num();
		Send(Bad);
		Bad.Value = -5.0;
		Send(Bad);
		Expect(Rejects.Num() == R0 + 2 && Sim->GetTramLines().Num() == 2, TEXT("TM1 a NaN and a negative tunnel Id are rejected, nothing is laid"));
	}

	// The independent oracle on the fixture: walking with and without the ride (the line of tunnel 1: (3,0) to (32,0) at 5,900 cm).
	// --- TM2. The edge integer. ---
	{
		Expect(Build(true, 1, true) && AddResidents(18, 1.0) && Fit(0) && Lay(1), TEXT("TM2 setup: the D6c fixture with a car and a line on the 30-cell tunnel; eighteen residents live one to a cell"));
		FullRamp();
		const FRHTramLine& L = Sim->GetTramLines()[0];
		const int64 Len = Sim->GetTramLengthCm(L), Edge = Sim->GetTramEdgeCm(L);
		Expect(Len == 29000 && Edge == (60 / 2 + 29) * 100 && Edge == 5900,
			FString::Printf(TEXT("TM2 the edge integer: length (30 points - 1) x 1,000 = 29,000 cm, (60 / 2 + ceil(29,000 / 1,000)) s x 100 cm/s = 5,900 cm (%lld, %lld)"), Len, Edge));
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		// Resident 0 at (1,0) to cell 13 = (35,2): out by the door (2,0) 1,000, onto the stop (3,0) 1,000, the ride 5,900, off by the door (33,0) 1,000, then 2,828.
		const int64 Ride = 1000 + 1000 + 5900 + 1000 + (2000 + 414 * 2);
		const int64 Foot = 1000 + 1000 + 29000 + 1000 + (2000 + 414 * 2);
		Expect(Ride == 11728 && Foot == 34828 && Sim->Debug_WalkCm(-1, 0, 13) == Ride && Sim->Debug_WalkCmNoTram(-1, 0, 13) == Foot && Sim->Debug_WalkTramLines(-1, 0, 13) == 1u && CommuteOf(0) == Ride,
			FString::Printf(TEXT("TM2 resident 0 to (35,2): 1,000 + 1,000 + 5,900 + 1,000 + 2,828 = 11,728 cm with the line (sim %lld, commute %lld), 34,828 on foot (%lld), the line is bit 0"), Sim->Debug_WalkCm(-1, 0, 13), CommuteOf(0), Sim->Debug_WalkCmNoTram(-1, 0, 13)));
		// Stop to stop, and a mid-tunnel cell: the line is boarded at the stops only.
		const int32 CellA = Ord(-1, 3, 0), CellB = Ord(-1, 32, 0), Mid10 = Ord(-1, 10, 0), Mid20 = Ord(-1, 20, 0);
		Expect(CellA == 18 && CellB != INDEX_NONE && Sim->Debug_WalkCm(-1, CellA, CellB) == 5900 && Sim->Debug_WalkCmNoTram(-1, CellA, CellB) == 29000
			&& Sim->Debug_WalkCm(-1, Mid10, CellB) == 7000 + 5900 && Sim->Debug_WalkTramLines(-1, Mid10, CellB) == 1u
			&& Sim->Debug_WalkCm(-1, Mid10, Mid20) == 10000 && Sim->Debug_WalkTramLines(-1, Mid10, Mid20) == 0u,
			FString::Printf(TEXT("TM2 the stops: stop to stop 5,900 (sim %lld) against 29,000 on foot; (10,0) to the far stop walks back 7,000 and rides, 12,900 (%lld); (10,0) to (20,0) is 10,000 on foot with no ride (%lld)"),
				Sim->Debug_WalkCm(-1, CellA, CellB), Sim->Debug_WalkCm(-1, Mid10, CellB), Sim->Debug_WalkCm(-1, Mid10, Mid20)));
		// The tie is a walk, the next second a ride. 29,000 = (H / 2 + 29) x 100 at H = 522.
		Sim->Debug_SetTramTiming(522, 1000);
		const bool bTie = Sim->Debug_WalkCm(-1, CellA, CellB) == 29000 && Sim->Debug_WalkTramLines(-1, CellA, CellB) == 0u && Sim->Debug_WalkCm(-1, 0, 13) == Foot && Sim->GetTramEdgeCm(Sim->GetTramLines()[0]) == 29000;
		Sim->Debug_SetTramTiming(520, 1000);
		const bool bBelow = Sim->Debug_WalkCm(-1, CellA, CellB) == 28900 && Sim->Debug_WalkTramLines(-1, CellA, CellB) == 1u;
		Expect(bTie && bBelow, TEXT("TM2 a ride that costs exactly the walk (headway 522 s: 29,000 cm) is a walk, no line is used; at 520 s (28,900 cm) the line is ridden"));
		// The ride time rounds up: 29,000 cm at 2,000 cm/s is 14.5 s, taken as 15, so (30 + 15) x 100 = 4,500.
		Sim->Debug_SetTramTiming(60, 2000);
		const bool bCeil = Sim->GetTramEdgeCm(Sim->GetTramLines()[0]) == 4500 && Sim->Debug_WalkCm(-1, CellA, CellB) == 4500;
		Sim->Debug_SetTramTiming(61, 1000);
		const bool bFloorHalf = Sim->GetTramEdgeCm(Sim->GetTramLines()[0]) == (30 + 29) * 100; // the half-headway is an integer division
		Sim->Debug_SetTramTiming(60, 1000);
		Expect(bCeil && bFloorHalf && Sim->GetTramEdgeCm(Sim->GetTramLines()[0]) == 5900, TEXT("TM2 the ride time rounds up (14.5 s is 15: 4,500 cm) and an odd headway halves down (61 s is 30); the rows restored give 5,900"));
		// The sweep: every (home cell, seat cell) pair against the independent oracle, with and without the ride.
		const FIntPoint Pa = StopA, Pb = StopB;
		const TArray<TArray<int64>> D1 = OracleWalk(Sim, -1, { FOracleRide{ Pa, Pb, 5900 } });
		const TArray<TArray<int64>> D0 = OracleWalk(Sim, -1, {});
		const int32 Cells = Sim->GetFloorCarvedCells(-1);
		int32 Compared = 0, WrongRide = 0, WrongFoot = 0, WrongMask = 0, Riding = 0;
		FString FirstBad;
		for (int32 H = 0; H < Cells; ++H)
		{
			for (int32 W = 0; W < Cells; ++W)
			{
				++Compared;
				const int64 Got = Sim->Debug_WalkCm(-1, H, W), GotFoot = Sim->Debug_WalkCmNoTram(-1, H, W);
				const bool bRide = D1[H][W] < D0[H][W];
				Riding += bRide ? 1 : 0;
				WrongRide += Got != D1[H][W] ? 1 : 0;
				WrongFoot += GotFoot != D0[H][W] ? 1 : 0;
				WrongMask += (Sim->Debug_WalkTramLines(-1, H, W) != 0u) != bRide ? 1 : 0;
				if (Got != D1[H][W] && FirstBad.IsEmpty()) { FirstBad = FString::Printf(TEXT("%d>%d got %lld oracle %lld"), H, W, Got, D1[H][W]); }
			}
		}
		Expect(Compared == Cells * Cells && WrongRide == 0 && WrongFoot == 0 && WrongMask == 0 && Riding > 100,
			FString::Printf(TEXT("TM2 sweep: all %d (home, seat) pairs of the %d-cell floor match the independent oracle with the line (%d wrong) and without it (%d wrong); the line is named exactly where it is strictly cheaper (%d wrong, %d pairs ride) %s"),
				Compared, Cells, WrongRide, WrongFoot, WrongMask, Riding, *FirstBad));
		Expect(Sim->Debug_CommuteCachesFresh() && Sim->Debug_DeepWalkFresh() && Sim->GetCommuteOffNet() == 0, TEXT("TM2 the flows and the walk network are fresh, none off the net"));
	}

	// --- TM2b. Two lines on one floor: a route that rides both (step-2 fix 1). ---
	{
		// The review's three-dig floor (scratchpad/step2/d6d/prune.py), translated by (3, 0) so that X stands clear of shaft 0's column (0,0); the
		// distances depend only on differences, so none of its integers change. X is the 3 x 3 dig at x 1..3, y -1..1 (a cavern beside the column); Z
		// the dig at x 33..35, y -1..1; Y the dig at x 17..19, y 30..32. T0 (4,0)-(32,0), 29 cells, joins X to Z and takes no line; T1 (3,2)-(18,29), 43
		// cells, and T2 (20,31)-(34,2), 44 cells, each take one: (30 + n - 1) x 100 cm = 7,200 and 7,300 cm. Each tunnel is carved to its last cell
		// before the next is ordered, so no order crosses an open one.
		Reset();
		Sim->ExtendShaft(1, Head0);
		const auto Dig3 = [&](int32 X0, int32 Y0) -> bool
		{
			int32 CId = INDEX_NONE;
			FString W;
			return Sim->Debug_DigCavern(-1, 1, FIntPoint(X0, Y0), FIntPoint(X0 + 2, Y0 + 2), CId, W) && Sim->Debug_CarveCavernCells(CId, 9) == 9;
		};
		bool bFloor = Dig3(1, -1);
		const int32 T0 = DigT(FIntPoint(4, 0), FIntPoint(32, 0), 29);
		bFloor &= T0 != INDEX_NONE && Dig3(33, -1);
		const int32 T1 = DigT(FIntPoint(3, 2), FIntPoint(18, 29), 43);
		bFloor &= T1 != INDEX_NONE && Dig3(17, 30);
		const int32 T2 = DigT(FIntPoint(20, 31), FIntPoint(34, 2), 44);
		bFloor &= T2 != INDEX_NONE && Sim->GetFloorCarvedCells(-1) == 9 + 29 + 9 + 43 + 9 + 44;
		Expect(bFloor && T0 == 1 && T1 == 2 && T2 == 3 && Fit(0) && Lay(T1) && Lay(T2) && Sim->GetTramLines().Num() == 2 && Sim->IsFloorMapped(-1),
			FString::Printf(TEXT("TM2b setup: three 3 x 3 digs (X, Z, Y), the unlined tunnel 1 (29 cells) between X and Z, and lines on tunnels 2 (43 cells) and 3 (44 cells): %d cells on -1, two lines, the floor mapped"), Sim->GetFloorCarvedCells(-1)));
		const FRHTramLine& LineA = Sim->GetTramLines()[0];
		const FRHTramLine& LineB = Sim->GetTramLines()[1];
		const int64 EdgeA = Sim->GetTramEdgeCm(LineA), EdgeB = Sim->GetTramEdgeCm(LineB);
		Expect(EdgeA == (30 + 43 - 1) * 100 && EdgeA == 7200 && EdgeB == (30 + 44 - 1) * 100 && EdgeB == 7300 && LineA.StopA == FIntPoint(3, 2) && LineA.StopB == FIntPoint(18, 29) && LineB.StopA == FIntPoint(20, 31) && LineB.StopB == FIntPoint(34, 2),
			FString::Printf(TEXT("TM2b the two ride prices: (30 + 43 - 1) x 100 = 7,200 cm and (30 + 44 - 1) x 100 = 7,300 cm (%lld, %lld); line 1 runs (3,2) to (18,29), line 2 (20,31) to (34,2)"), EdgeA, EdgeB));
		// The review's pair: X's east middle cell (3,0) to Z's west middle cell (33,0). On foot along tunnel 1: 30,000 cm. Riding both lines: out to the stop (3,2)
		// 2,000, ride 7,200, (18,29) to (18,30) 1,000, across Y to (19,31) 1,414, onto the stop (20,31) 1,000, ride 7,300, (34,2) to (34,1) 1,000, across Z to (33,0)
		// 1,414: 22,328 cm. The bound the one-line rule gave was the cheapest of (octile to a stop + a ride + octile from the other stop) over each line and end.
		const int32 CellX = Ord(-1, 3, 0), CellZ = Ord(-1, 33, 0);
		const int64 OnFoot = Sim->Debug_WalkCmNoTram(-1, CellX, CellZ), Riding = Sim->Debug_WalkCm(-1, CellX, CellZ);
		int64 OldBound = MAX_int64;
		for (const FRHTramLine* Line : { &LineA, &LineB })
		{
			const int64 Cm = Sim->GetTramEdgeCm(*Line);
			OldBound = FMath::Min(OldBound, OracleOct(FIntPoint(3, 0), Line->StopA) + Cm + OracleOct(FIntPoint(33, 0), Line->StopB));
			OldBound = FMath::Min(OldBound, OracleOct(FIntPoint(3, 0), Line->StopB) + Cm + OracleOct(FIntPoint(33, 0), Line->StopA));
		}
		Expect(CellX != INDEX_NONE && CellZ != INDEX_NONE && OnFoot == 30000 && Riding == 22328 && Sim->Debug_WalkTramLines(-1, CellX, CellZ) == 3u && OldBound == 44410 && OldBound >= OnFoot,
			FString::Printf(TEXT("TM2b the review's pair: 30,000 cm on foot (%lld), 2,000 + 7,200 + 3,414 + 7,300 + 2,414 = 22,328 cm riding both lines (sim %lld), both lines named (mask %u); the one-line bound was %lld, which is not below the walk, so the stop search never ran"),
				OnFoot, Riding, Sim->Debug_WalkTramLines(-1, CellX, CellZ), OldBound));
		// The sweep: every (home, seat) pair of the floor against the independent oracle, with the two rides and without.
		const TArray<TArray<int64>> DBoth = OracleWalk(Sim, -1, { FOracleRide{ LineA.StopA, LineA.StopB, EdgeA }, FOracleRide{ LineB.StopA, LineB.StopB, EdgeB } });
		const TArray<TArray<int64>> DFoot = OracleWalk(Sim, -1, {});
		const int32 Cells2 = Sim->GetFloorCarvedCells(-1);
		const int32 Calls0 = Sim->GetTramWalkCalls(), Pruned0 = Sim->GetTramWalkPruned();
		int32 Compared2 = 0, Wrong2 = 0, WrongFoot2 = 0, Both = 0, Rode = 0;
		FString FirstBad2;
		for (int32 H = 0; H < Cells2; ++H)
		{
			for (int32 W = 0; W < Cells2; ++W)
			{
				++Compared2;
				const int64 Got = Sim->Debug_WalkCm(-1, H, W);
				Wrong2 += Got != DBoth[H][W] ? 1 : 0;
				WrongFoot2 += Sim->Debug_WalkCmNoTram(-1, H, W) != DFoot[H][W] ? 1 : 0;
				const uint32 Mask = Sim->Debug_WalkTramLines(-1, H, W);
				Both += Mask == 3u ? 1 : 0;
				Rode += Mask != 0u ? 1 : 0;
				if (Got != DBoth[H][W] && FirstBad2.IsEmpty()) { FirstBad2 = FString::Printf(TEXT("%d>%d got %lld oracle %lld"), H, W, Got, DBoth[H][W]); }
			}
		}
		const int32 Searched = (Sim->GetTramWalkCalls() - Calls0) - (Sim->GetTramWalkPruned() - Pruned0);
		Expect(Compared2 == Cells2 * Cells2 && Wrong2 == 0 && WrongFoot2 == 0,
			FString::Printf(TEXT("TM2b sweep: all %d (home, seat) pairs of the %d-cell floor match the independent oracle with both lines (%d wrong) and on foot (%d wrong) %s"), Compared2, Cells2, Wrong2, WrongFoot2, *FirstBad2));
		Expect(Both > 0 && Rode >= Both && Searched > 0,
			FString::Printf(TEXT("TM2b the sweep is not vacuous: %d pairs ride both lines (%d ride at least one), and the stop search ran %d times where the bound did not prune"), Both, Rode, Searched));
		Expect(Sim->Debug_CommuteCachesFresh() && Sim->Debug_DeepWalkFresh(), TEXT("TM2b the flows and the walk network are fresh"));
	}

	// --- TM3. Riders counted by route; the ramp at 60, 61 and 160. ---
	{
		Expect(Build(true, 1, true) && Fit(0) && Lay(1), TEXT("TM3 setup: the fixture with a car and a line"));
		Sim->Debug_SetCommuteTuning(60.0, 160.0, 30000.0, 6000.0); // the defaults: the ramp opens above 60 residents and is full at 160
		const TArray<TArray<int64>> D1 = OracleWalk(Sim, -1, { FOracleRide{ StopA, StopB, 5900 } });
		const TArray<TArray<int64>> D0 = OracleWalk(Sim, -1, {});
		// A resident of dig A or of the tunnel sits in cell 13 (dig B), a resident of dig B sits in cell 0 (dig A): the line is the way across for most, and not for those who live in the far half of the tunnel.
		const auto SeatAll = [&](int32 N)
		{
			for (int32 k = 0; k < N; ++k)
			{
				const int32 Home = Sim->GetColonistHomeCell(ColonistAt(k));
				SeatOnFloor(k, -1, (Home >= 0 && (Home < 12 || Home >= 18)) ? 13 : 0);
			}
		};
		const auto OracleRiders = [&](int32 N)
		{
			int32 Riders = 0;
			for (int32 k = 0; k < N; ++k)
			{
				const int32 Home = Sim->GetColonistHomeCell(ColonistAt(k));
				const int32 Seat = (Home >= 0 && (Home < 12 || Home >= 18)) ? 13 : 0;
				Riders += (Home >= 0 && D1[Home][Seat] < D0[Home][Seat]) ? 1 : 0;
			}
			return Riders;
		};
		AddResidents(60, 4.0);
		SeatAll(60);
		const int32 Rebuilds0 = Sim->GetCommuteFlowRebuilds(), Walks0 = Sim->GetDeepWalkBuilds();
		Expect(Sim->GetPopulation() == 60 && Sim->GetCommuteRamp() == 0.0 && Sim->GetTramPeople(1) == 0 && Sim->GetTramPeopleLoad(1) == 0 && Sim->GetCommuteFlowRebuilds() == Rebuilds0 && Sim->GetDeepWalkBuilds() == Walks0,
			FString::Printf(TEXT("TM3 at 60 residents the ramp is 0: no rider, no load, no flows derived and no walk network built (rebuilds %d -> %d, builds %d -> %d)"), Rebuilds0, Sim->GetCommuteFlowRebuilds(), Walks0, Sim->GetDeepWalkBuilds()));
		AddResidents(1, 4.0);
		SeatAll(61);
		const int32 Riders61 = OracleRiders(61);
		const int64 Load61 = 2 * (int64)Riders61 * 10 / 1000; // the ramp is 0.01: 10 permille
		Expect(FMath::IsNearlyEqual(Sim->GetCommuteRamp(), 0.01, 1.0e-12) && Riders61 == 61 && Sim->GetTramPeople(1) == Riders61 && Sim->GetTramPeopleLoad(1) == Load61 && Load61 == 1 && Sim->GetCommuteWalked() == 61,
			FString::Printf(TEXT("TM3 at 61 the ramp is 0.01: every one of the %d residents crosses by the line (the oracle counts %d), the load is 2 x %d x 10 / 1000 = %lld (floored once) - sim riders %d, load %lld"), 61, Riders61, Riders61, Load61, Sim->GetTramPeople(1), Sim->GetTramPeopleLoad(1)));
		AddResidents(99, 4.0);
		SeatAll(160);
		const int32 Riders160 = OracleRiders(160);
		Expect(Sim->GetPopulation() == 160 && Sim->GetCommuteRamp() == 1.0 && Riders160 == 116 && Sim->GetTramPeople(1) == Riders160 && Sim->GetTramPeopleLoad(1) == 2 * (int64)Riders160 && Sim->GetCommuteShaftPeople(0) == 0,
			FString::Printf(TEXT("TM3 at 160 the ramp is full and riders are counted by route: the oracle says %d of the 160 ride (48 of dig A, 24 of dig B and 44 of the 11 tunnel cells carved from the A end; the 44 who live in the 11 cells carved from the far end walk on), the sim says %d, load 2 x %d = %lld; no shaft is ridden (%d)"),
				Riders160, Sim->GetTramPeople(1), Riders160, Sim->GetTramPeopleLoad(1), Sim->GetCommuteShaftPeople(0)));
		// A rider who changes seats leaves the count.
		SeatOnFloor(0, -1, 5);
		Expect(Sim->GetTramPeople(1) == Riders160 - (D1[Sim->GetColonistHomeCell(ColonistAt(0))][13] < D0[Sim->GetColonistHomeCell(ColonistAt(0))][13] ? 1 : 0) && Sim->Debug_CommuteCachesFresh(),
			TEXT("TM3 a resident who takes a seat in his own dig stops riding: the count follows the flows, and -RHVerifyCaches' comparison (TramPeople) holds"));
		// The line is loaded once a ride: a resident on both legs of one trip loads it twice (a same-floor trip out by one shaft and in by another that rides the line twice).
		Expect(Sim->GetTramPeople(99) == 0 && Sim->GetTramPeopleLoad(99) == 0, TEXT("TM3 an unknown line has no riders and no load"));
	}

	// --- TM4. The wait is added after the route is chosen, frozen at the roll, with no oscillation. ---
	{
		Expect(Build(true, 1, true) && AddResidents(18, 1.0) && Fit(0) && Lay(1), TEXT("TM4 setup: eighteen residents, a car, a line"));
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, 13); }
		const int64 Ride = 11728;
		const int32 S = SolNow();
		// Twelve residents live in dig A and ride; the others are in dig B. Capacity 5: load 2 x 12 = 24, wait ceil((24 - 5) x 120 / 5) = 456 s.
		Sim->Debug_SetTramCapacity(5);
		Expect(Sim->GetTramPeople(1) == 12 && Sim->GetTramPeopleLoad(1) == 24 && Sim->GetTramWaitSec(1) == 0 && CommuteOf(0) == Ride,
			FString::Printf(TEXT("TM4 before the roll the wait is still frozen at 0 although the capacity fell to 5 and 12 ride (load %lld): resident 0 pays 11,728 cm (%lld)"), Sim->GetTramPeopleLoad(1), CommuteOf(0)));
		Sim->Debug_RollTransit(S + 1);
		const int32 W1 = Sim->GetTramWaitSec(1);
		const int64 WaitCm = (int64)W1 * Sim->GetCommuteWaitCmPerSec();
		URHSimWorldSubsystem::FRHCommutePair Pair;
		TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
		Sim->GetCommutePairs(Pairs);
		bool bPair = false;
		for (const URHSimWorldSubsystem::FRHCommutePair& P : Pairs) { if (P.HomeCell == 0 && P.WorkCell == 13) { Pair = P; bPair = true; } }
		Expect(W1 == 456 && WaitCm == 45600 && Sim->ComputeTransitWaitSec(24, 5) == 456 && Sim->GetTramLines()[0].PrevWaitSec == 0 && bPair && Pair.DistCm == Ride + WaitCm && CommuteOf(0) == 57328,
			FString::Printf(TEXT("TM4 the roll freezes the wait at ceil((24 - 5) x 120 / 5) = 456 s = 45,600 cm; the line's cost is added AFTER the route was chosen: 11,728 + 45,600 = 57,328 cm (%lld), the pair says %lld"), CommuteOf(0), bPair ? Pair.DistCm : -1));
		// The crowd is dearer than walking (34,828), yet nobody walks: a route that read waits would flip, drop the load, and flip back next sol.
		const int64 Foot = Sim->Debug_WalkCmNoTram(-1, 0, 13);
		Expect(CommuteOf(0) > Foot && Sim->Debug_WalkTramLines(-1, 0, 13) == 1u && Sim->GetTramPeople(1) == 12,
			FString::Printf(TEXT("TM4 the crowded line costs %lld cm against %lld on foot, and the route did not flip: still bit 0, still 12 riders"), CommuteOf(0), Foot));
		bool bStable = true;
		for (int32 K = 2; K <= 4; ++K)
		{
			Sim->Debug_RollTransit(S + K);
			bStable &= Sim->GetTramWaitSec(1) == 456 && Sim->GetTramPeople(1) == 12 && Sim->GetTramLines()[0].PrevWaitSec == 456;
		}
		Expect(bStable, TEXT("TM4 three more rolls: the wait stays 456 s and the riders 12 every sol - no oscillation (PrevWaitSec holds the sol before)"));
		// The crowd eases: capacity 100 puts the load under it, and the wait falls back at the next roll, not before.
		Sim->Debug_SetTramCapacity(100);
		const bool bFrozen = Sim->GetTramWaitSec(1) == 456;
		Sim->Debug_RollTransit(S + 5);
		Expect(bFrozen && Sim->GetTramWaitSec(1) == 0 && Sim->GetTramLines()[0].PrevWaitSec == 456 && CommuteOf(0) == Ride && Sim->GetTransitCheckedSol() == S + 5,
			FString::Printf(TEXT("TM4 capacity 100: the wait is frozen until the roll, then 0 (previous 456), and the trip is back to %lld cm"), CommuteOf(0)));
		// The wait caps at MaxWaitSec.
		Sim->Debug_SetTramCapacity(1);
		Sim->Debug_RollTransit(S + 6);
		Expect(Sim->GetTramWaitSec(1) == Sim->GetMaxWaitSec() && Sim->GetTramWaitSec(1) == 600, FString::Printf(TEXT("TM4 a load of 24 on 1 place waits the cap, 600 s (%d)"), Sim->GetTramWaitSec(1)));
		Expect(Sim->Debug_CommuteCachesFresh() && Sim->GetCommuteOffNet() == 0 && Sim->GetTransitUnstampedRobotWrites() == 0, TEXT("TM4 the caches are fresh, none off the net, no unstamped robot write"));
	}

	// --- TM5. A line maps its floor. ---
	{
		Expect(Build(true, 2, false) && AddResidents(18, 1.0) && Fit(0), TEXT("TM5 setup: floors -1 and -2 dug, a 30-cell tunnel on -1, NO shape anywhere; eighteen residents of -1; a car"));
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		uint32 KT0 = 7, KS0 = 7;
		Sim->Debug_DeepWalkKey(KT0, KS0);
		Expect(!Sim->IsFloorMapped(-1) && !Sim->IsFloorMapped(-2) && Sim->GetColonistHomeCell(ColonistAt(0)) == INDEX_NONE && CommuteOf(0) == 0 && Sim->GetCommuteWalked() == 0 && KT0 == 0 && KS0 == 0,
			FString::Printf(TEXT("TM5 before the line nothing is mapped: no home cell, the same-floor trip between digs is D4's 0 cm (%lld), the walk keys are 0"), CommuteOf(0)));
		const uint32 Districts0 = Sim->GetDistrictsVersion();
		Expect(Lay(1) && Sim->IsFloorMapped(-1) && !Sim->IsFloorMapped(-2), TEXT("TM5 laying a line on tunnel 1 maps floor -1 and no other"));
		uint32 KT1 = 0, KS1 = 0;
		Sim->Debug_DeepWalkKey(KT1, KS1);
		Expect(Sim->GetDistrictsVersion() != Districts0 && KT1 != 0 && KS1 != 0 && Sim->GetColonistHomeCell(ColonistAt(0)) == 0 && Sim->GetColonistHomeCell(ColonistAt(17)) == 17,
			TEXT("TM5 the districts' ground re-derives, the commute key now carries the walk's versions, and the residents of -1 have home cells (one to a cell)"));
		Expect(CommuteOf(0) == 11728 && Sim->GetCommuteWalked() == 18 && Sim->GetCommuteOffNet() == 0 && Sim->Debug_CommuteCachesFresh() && Sim->Debug_DistrictCachesFresh() && Sim->Debug_DeepWalkFresh(),
			FString::Printf(TEXT("TM5 the trip is now the walk and the ride, 11,728 cm (%lld); all 18 walked, none off the net; the caches fresh"), CommuteOf(0)));
		// A line is no shape: a whole-floor paint of the same floor is not refused, and the floor stays mapped.
		const bool bPaint = Send(ShapeCmd(NHomes, -1, 0, 0, 0));
		const int32 FloorDistrict = Sim->GetDistrictOfFloor(-1);
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); } // the steps the order took re-derived the seats: they are laid again
		Expect(bPaint && FloorDistrict != 0 && Sim->IsFloorMapped(-1) && CommuteOf(0) == 11728,
			FString::Printf(TEXT("TM5 a line is no shape: a whole-floor Homes paint of floor -1 is accepted, the floor stays mapped and the trip is unchanged (district %d, mapped %d, %lld cm, %s)"), FloorDistrict, Sim->IsFloorMapped(-1) ? 1 : 0, CommuteOf(0), Rejects.Num() > 0 ? *Rejects.Last() : TEXT("no rejection")));
	}

	// --- TM6. The save. ---
	{
		Expect(Build(true, 1, false) && AddResidents(18, 1.0) && Fit(0), TEXT("TM6 setup: the fixture with eighteen residents (one to a cell) and a car"));
		const int32 T2 = DigT(FIntPoint(3, 2), FIntPoint(14, 2), 12);
		const int32 T3 = DigT(FIntPoint(3, 1), FIntPoint(13, 1), 11);
		const int32 T4 = DigT(FIntPoint(3, -1), FIntPoint(13, -1), 5);
		const int32 T5 = DigT(FIntPoint(-2, 0), FIntPoint(-12, 0), 3);
		FRHCommand Cancel5;
		Cancel5.Verb = FName("CancelTunnel");
		Cancel5.Value = T5;
		Expect(T2 == 2 && T3 == 3 && T4 == 4 && T5 == 5 && Send(Cancel5), TEXT("TM6 setup: tunnel #2 finished (12 points), #3 finished (11), #4 with 6 left, #5 cancelled"));
		Expect(Lay(1) && Lay(2) && Sim->GetTramLines().Num() == 2, TEXT("TM6 lines 1 and 2 are laid"));
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, 13); }
		// Two rolls, so the file holds a wait and a DIFFERENT previous wait on line 1: capacity 5 gives 456 s (previous 0), then capacity 8 gives
		// ceil((24 - 8) x 120 / 8) = 240 s (previous 456).
		Sim->Debug_SetTramCapacity(5);
		Sim->Debug_RollTransit(SolNow() + 1);
		const int32 Wait1 = Sim->GetTramWaitSec(1), Prev1 = Sim->GetTramLines()[0].PrevWaitSec;
		Sim->Debug_SetTramCapacity(8);
		Sim->Debug_RollTransit(SolNow() + 2);
		const int32 WaitA = Sim->GetTramWaitSec(1), PrevA = Sim->GetTramLines()[0].PrevWaitSec;
		Sim->Debug_SetTramCapacity(600);
		Expect(Wait1 == 456 && Prev1 == 0 && WaitA == 240 && PrevA == 456 && Sim->GetTramWaitSec(2) == 0 && Sim->GetTramLines()[1].PrevWaitSec == 0,
			FString::Printf(TEXT("TM6 the waits to save: line 1 %d s (previous %d), then %d s (previous %d); line 2 0 and 0"), Wait1, Prev1, WaitA, PrevA));
		const int64 Commute0 = CommuteOf(0);
		TArray<FRHTramLine> Lines0 = Sim->GetTramLines();
		FString E0;
		Expect(Sim->SaveColony(TEXT("tramfile"), E0), FString::Printf(TEXT("TM6 the colony is saved (%s)"), *E0));
		TArray<uint8> W;
		const bool bRead = FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("tramfile")));
		uint32 Ver = 0;
		if (bRead) { FMemory::Memcpy(&Ver, W.GetData() + 4, 4); }
		const int32 Pattern[13] = { 0, 0, 0, 3, 2, 1, 0, -1, 1, 3, 0, 32, 0 };
		int32 At = INDEX_NONE;
		for (int32 I = 0; bRead && I + 52 <= W.Num() && At == INDEX_NONE; ++I)
		{
			bool bAll = true;
			for (int32 k = 0; k < 13 && bAll; ++k) { bAll &= Rd(W, I + 4 * k) == Pattern[k]; }
			if (bAll) { At = I + 12; }
		}
		Expect(bRead && Ver == 55 && At != INDEX_NONE,
			FString::Printf(TEXT("TM6 the file says version %u; the tram block starts at byte %d, straight after the 12-byte floor-shape block (NextTramId 3, 2 lines)"), Ver, At));
		if (!bRead || At == INDEX_NONE) { return Finish(); }
		Expect(Rd(W, At + 8 + 0) == 1 && Rd(W, At + 8 + 4) == 0 && Rd(W, At + 8 + 8) == -1 && Rd(W, At + 8 + 12) == 1 && Rd(W, At + 8 + 16) == 3 && Rd(W, At + 8 + 20) == 0 && Rd(W, At + 8 + 24) == 32 && Rd(W, At + 8 + 28) == 0 && Rd(W, At + 8 + 32) == WaitA && Rd(W, At + 8 + 36) == PrevA
			&& Rd(W, At + 48 + 0) == 2 && Rd(W, At + 48 + 12) == 2 && Rd(W, At + 48 + 16) == 3 && Rd(W, At + 48 + 20) == 2 && Rd(W, At + 48 + 24) == 14 && Rd(W, At + 48 + 28) == 2 && Rd(W, At + 48 + 32) == 0 && Rd(W, At + 48 + 36) == 0,
			TEXT("TM6 the layout reads back as written: Next, count, then per line Id, region, floor, tunnel, the two stops as lattice points, the frozen and the previous wait (10 ints, 40 bytes)"));
		// The default colony writes 8 bytes (NextTramId 1, no line).
		{
			Reset();
			FString ED;
			TArray<uint8> Wd;
			const bool bSavedD = Sim->SaveColony(TEXT("tramdefault"), ED) && FFileHelper::LoadFileToArray(Wd, *SaveFilePath(TEXT("tramdefault")));
			int32 AtD = INDEX_NONE;
			const int32 PatD[7] = { 1, 0, 0, 0, 0, 1, 0 };
			for (int32 I = 0; bSavedD && I + 28 <= Wd.Num() && AtD == INDEX_NONE; ++I)
			{
				bool bAll = true;
				for (int32 k = 0; k < 7 && bAll; ++k) { bAll &= Rd(Wd, I + 4 * k) == PatD[k]; }
				if (bAll) { AtD = I + 20; }
			}
			Expect(bSavedD && AtD != INDEX_NONE && Rd(Wd, AtD) == 1 && Rd(Wd, AtD + 4) == 0, TEXT("TM6 a colony with no line writes 8 bytes: NextTramId 1 and a count of 0"));
		}
		// Round trip.
		Reset();
		const uint32 TV0 = Sim->GetTramVersion();
		FString LoadErr;
		const int32 ErrorsBefore = ErrorCounter.Count;
		const bool bLoaded = Sim->LoadColony(TEXT("tramfile"), LoadErr) && LoadErr.IsEmpty();
		Sim->Debug_SetColonistsPerCell(1.0);
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, 13); }
		bool bSame = bLoaded && Sim->GetTramLines().Num() == Lines0.Num() && Sim->GetNextTramId() == 3;
		for (int32 i = 0; bSame && i < Lines0.Num(); ++i)
		{
			const FRHTramLine& A = Sim->GetTramLines()[i];
			const FRHTramLine& B = Lines0[i];
			bSame &= A.Id == B.Id && A.RegionId == B.RegionId && A.Level == B.Level && A.TunnelId == B.TunnelId && A.StopA == B.StopA && A.StopB == B.StopB && A.WaitSec == B.WaitSec && A.PrevWaitSec == B.PrevWaitSec;
		}
		Expect(bSame && ErrorCounter.Count == ErrorsBefore && Sim->GetTramVersion() != TV0 && Sim->IsFloorMapped(-1) && CommuteOf(0) == Commute0 && Commute0 == 11728 + (int64)WaitA * Sim->GetCommuteWaitCmPerSec() && Sim->Debug_CommuteCachesFresh() && Sim->Debug_DeepWalkFresh(),
			FString::Printf(TEXT("TM6 it loads back exactly with no Error line: both lines with their waits (240 s, previous 456; 0 and 0), the floor mapped, the trip %lld cm as before: the walk 11,728 plus the frozen 240 s (%s)"), CommuteOf(0), *LoadErr));
		FString Save2Err;
		TArray<uint8> W2;
		Expect(Sim->SaveColony(TEXT("tramfile2"), Save2Err) && FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("tramfile2"))) && W2.Num() == W.Num() && FMemory::Memcmp(W2.GetData() + At, W.GetData() + At, 88) == 0,
			TEXT("TM6 the block round-trips byte-equal (88 bytes: Next, count, two records)"));
		// Hand-made files: every structural refusal, the colony untouched, no Error line.
		const auto TramDigest = [&]()
		{
			FString D = FString::Printf(TEXT("%d/%d/%u/%d/%d"), Sim->GetTramLines().Num(), Sim->GetNextTramId(), Sim->GetTramVersion(), Sim->GetFloorCarvedCells(-1), Sim->GetBuildings().Num());
			for (const FRHTramLine& L : Sim->GetTramLines()) { D += FString::Printf(TEXT("|%d,%d,%d,%d"), L.Id, L.TunnelId, L.WaitSec, L.PrevWaitSec); }
			return D;
		};
		const auto TryFile = [&](const TCHAR* Slot, const TArray<uint8>& Bytes, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Bytes, *SaveFilePath(Slot));
			const FString Digest0 = TramDigest();
			const int32 E0c = ErrorCounter.Count;
			FString Err;
			const bool bOk = Sim->LoadColony(Slot, Err);
			OutErr = Err;
			return !bOk && TramDigest() == Digest0 && ErrorCounter.Count == E0c && Err.Contains(TEXT("Save tram list invalid"));
		};
		FString Err;
		const int32 L1 = At + 8, L2 = At + 48; // the two records
		{ TArray<uint8> T = W; PutI(T, At + 4, -1); Expect(TryFile(TEXT("tramr1"), T, Err) && Err.Contains(TEXT("tram line count -1")), FString::Printf(TEXT("TM6 a count below 0 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 4, 100000000); Expect(TryFile(TEXT("tramr2"), T, Err) && Err.Contains(TEXT("tram line count")), FString::Printf(TEXT("TM6 a count larger than the bytes left is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At, 40); PutI(T, At + 4, RH::MaxTramLinesCap + 1); Expect(TryFile(TEXT("tramr3"), T, Err) && Err.Contains(TEXT("tram line count 33")), FString::Printf(TEXT("TM6 more lines than the cap (32) is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At, 0); Expect(TryFile(TEXT("tramr4"), T, Err) && Err.Contains(TEXT("next id 0")), FString::Printf(TEXT("TM6 a next id below 1 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L2, 1); Expect(TryFile(TEXT("tramr5"), T, Err) && Err.Contains(TEXT("must ascend")), FString::Printf(TEXT("TM6 line Ids that do not ascend are refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L2, 3); Expect(TryFile(TEXT("tramr6"), T, Err) && Err.Contains(TEXT("stay below 3")), FString::Printf(TEXT("TM6 a line Id at or past the next id is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L1 + 4, 1); Expect(TryFile(TEXT("tramr7"), T, Err) && Err.Contains(TEXT("region 1")), FString::Printf(TEXT("TM6 a line in another region is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L1 + 12, 99); Expect(TryFile(TEXT("tramr8"), T, Err) && Err.Contains(TEXT("tunnel 99")) && Err.Contains(TEXT("does not list")), FString::Printf(TEXT("TM6 a line on an unknown tunnel is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L1 + 8, -2); Expect(TryFile(TEXT("tramr9"), T, Err) && Err.Contains(TEXT("floor -2")), FString::Printf(TEXT("TM6 a line on the wrong floor of its tunnel is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L1 + 12, 5); Expect(TryFile(TEXT("tramr10"), T, Err) && Err.Contains(TEXT("was cancelled")), FString::Printf(TEXT("TM6 a line on a cancelled tunnel is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L1 + 12, 4); PutI(T, L1 + 16, 3); PutI(T, L1 + 20, -1); PutI(T, L1 + 24, 13); PutI(T, L1 + 28, -1); Expect(TryFile(TEXT("tramr11"), T, Err) && Err.Contains(TEXT("not finished")), FString::Printf(TEXT("TM6 a line on an unfinished tunnel is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L1 + 16, 4); Expect(TryFile(TEXT("tramr12"), T, Err) && Err.Contains(TEXT("not at the ends of tunnel 1")), FString::Printf(TEXT("TM6 stops that are not the tunnel's ends are refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L2 + 12, 1); PutI(T, L2 + 16, 3); PutI(T, L2 + 20, 0); PutI(T, L2 + 24, 32); PutI(T, L2 + 28, 0); Expect(TryFile(TEXT("tramr13"), T, Err) && Err.Contains(TEXT("tunnel 1 has two tram lines")), FString::Printf(TEXT("TM6 two lines on one tunnel are refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L1 + 32, RH::MaxWaitSecCap + 1); Expect(TryFile(TEXT("tramr14"), T, Err) && Err.Contains(TEXT("waits 1201 s")), FString::Printf(TEXT("TM6 a wait past one sol (1,200 s) is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, L2 + 36, -1); Expect(TryFile(TEXT("tramr15"), T, Err) && Err.Contains(TEXT("previous -1 s")), FString::Printf(TEXT("TM6 a negative previous wait is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; T.SetNum(At + 8 + 40); Expect(TryFile(TEXT("tramr16"), T, Err) && Err.Contains(TEXT("tram line count 2")), FString::Printf(TEXT("TM6 a file cut short is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; const uint32 V49 = 49; FMemory::Memcpy(T.GetData() + 4, &V49, 4); FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("tramr17"))); FString E49; const bool bOk = Sim->LoadColony(TEXT("tramr17"), E49); Expect(!bOk && E49.Contains(TEXT("Save format mismatch")) && E49.Contains(TEXT("version 49")) && E49.Contains(TEXT("/55")), FString::Printf(TEXT("TM6 a v49 file is refused as a version error (%s)"), *E49)); }
		Expect(Sim->GetTramLines().Num() == 2 && Sim->GetTramWaitSec(1) == WaitA && Sim->IsFloorMapped(-1), TEXT("TM6 after all of that the colony is exactly as it was"));
	}

	// --- TM7. The era band against the agent band, rolling a real load (step-2 fix 7). ---
	{
		// The riders sit in REAL Lab rooms, so every RefreshJobs the step runs (and the load's own) derives their seats again and the roll finds them: six
		// Lab cells in dig B (cells 12..17), where the first six residents (the lowest Ids, who live in cells 0..5 of dig A) take the seats and ride the
		// line. At capacity 5 places the load of 2 x 6 = 12 queues. The fixture is built twice: two loaded legs from one saved file, and a third that
		// never saved.
		const auto Fixture = [&]() -> bool
		{
			bool bOk = Build(true, 1, true) && Fit(0) && Lay(1);
			Sim->AddStock(FName("Food"), 6000.0); // a minute of sim time must not recall anyone: the roster stays 40
			Sim->AddStock(FName("Water"), 600.0);
			Sim->AddStock(FName("Oxygen"), 6000.0);
			bOk &= AddResidents(40, 1.0);
			FullRamp();
			for (int32 Cell = 12; Cell <= 17; ++Cell) { bOk &= Sim->DesignateRoom(-1, Cell, FName("Lab"), R); }
			Sim->Debug_RefreshJobs();
			Sim->Debug_SetTramCapacity(1); // a load of 12 on one place: the wait caps at 600 s, frozen to be rolled on below
			Sim->Debug_RollTransit(SolNow()); // (the threshold sol stays this sol: the step that crosses the boundary rolls)
			return bOk;
		};
		const bool bFixture = Fixture() && Sim->GetTramPeople(1) >= 3 && Sim->GetTramWaitSec(1) == Sim->GetMaxWaitSec();
		if (!bFixture) { UE_LOG(LogRedHopeSim, Warning, TEXT("TM7 fixture: %d residents ride the line (load %lld places), the frozen wait is %d s"), Sim->GetTramPeople(1), Sim->GetTramPeopleLoad(1), Sim->GetTramWaitSec(1)); }
		Expect(bFixture, TEXT("TM7 setup: the fixture with a car and a line"));
		// Put the clock half a minute before a sol boundary: the next 60 s step crosses it in either band.
		const auto Lead30 = [&]() -> double
		{
			const double Boundary = (double)(SolNow() + 1) * SolLen;
			const double Lead = Boundary - 30.0 - Clock->GetSimSecondsTotal();
			if (Lead > 0.0) { Clock->Debug_AdvanceSimSeconds(Lead); }
			return Boundary;
		};
		const double Boundary = Lead30();
		const int32 SolBefore = SolNow();
		const double SavedAt = Clock->GetSimSecondsTotal();
		FString PairErr;
		Expect(Sim->SaveColony(TEXT("trampair"), PairErr) && SolNow() == SolBefore, FString::Printf(TEXT("TM7 the colony is saved half a minute before the sol boundary (sol %d; %s)"), SolBefore, *PairErr));
		FString Leg[3];
		int32 Rolls[3] = { 0, 0, 0 }, WaitAfter[3] = { 0, 0, 0 }, PeopleAfter[3] = { 0, 0, 0 };
		int64 LoadAfter[3] = { 0, 0, 0 };
		const auto Describe = [&](uint32 WaitV0, int32 Index)
		{
			const int32 Roster = Sim->GetPopulation();
			WaitAfter[Index] = Sim->GetTramWaitSec(1);
			PeopleAfter[Index] = Sim->GetTramPeople(1);
			LoadAfter[Index] = Sim->GetTramPeopleLoad(1);
			Leg[Index] = FString::Printf(TEXT("sol%d checked%d pop%d wait%d prev%d dv%u people%d load%lld walked%d |"), SolNow(), Sim->GetTransitCheckedSol(), Roster, Sim->GetTramWaitSec(1), Sim->GetTramLines()[0].PrevWaitSec,
				Sim->GetTransitWaitVersion() - WaitV0, Sim->GetTramPeople(1), Sim->GetTramPeopleLoad(1), Sim->GetCommuteWalked());
			for (int32 k = 0; k < Roster; ++k) { Leg[Index] += FString::Printf(TEXT(" %lld"), CommuteOf(k)); }
		};
		for (int32 Agent = 0; Agent < 2; ++Agent)
		{
			FString Le;
			if (!Sim->LoadColony(TEXT("trampair"), Le) || !Le.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("TRAMS: leg load failed: %s"), *Le); ++Fails; }
			Sim->Debug_SetGrowthEnabled(false);
			Sim->Debug_SetColonistsPerCell(1.0);
			FullRamp();
			Sim->Debug_SetTramCapacity(5);
			const int32 Rolls0 = Sim->GetTransitThresholds();
			const uint32 WaitV0 = Sim->GetTransitWaitVersion();
			if (Agent) { for (int32 K = 0; K < (int32)(EraDt / (double)AgentDt); ++K) { AgentOnce(); } }
			else { EraOnce(); }
			Rolls[Agent] = Sim->GetTransitThresholds() - Rolls0;
			Describe(WaitV0, Agent); // the seats are the rooms' own: no hand-laid seat to put back
		}
		// The third leg never saved: the same fixture, the same clock, one era step.
		bool bThirdOk = false;
		{
			const bool bBuilt = Fixture();
			const double Boundary3 = Lead30();
			Sim->Debug_SetTramCapacity(5);
			const int32 Rolls0 = Sim->GetTransitThresholds();
			const uint32 WaitV0 = Sim->GetTransitWaitVersion();
			EraOnce();
			Rolls[2] = Sim->GetTransitThresholds() - Rolls0;
			Describe(WaitV0, 2);
			bThirdOk = bBuilt && Boundary3 == Boundary;
		}
		const int32 WantWait = Sim->ComputeTransitWaitSec(LoadAfter[0], 5);
		Expect(Leg[0] == Leg[1] && Rolls[0] == 1 && Rolls[1] == 1 && Leg[0].Contains(TEXT("pop40 ")) && PeopleAfter[0] >= 3 && WaitAfter[0] > 0 && WaitAfter[1] > 0 && WaitAfter[0] == WantWait && WaitAfter[1] == WantWait && Leg[0].Contains(TEXT("prev600")),
			FString::Printf(TEXT("TM7 both bands roll the line once at the boundary (era %d, agent %d) under a real load: %d riders, load %lld places on 5, so a queue forms - the wait frozen for the sol is %d s, above 0 and the figure TM4's formula gives (ceil((load - 5) x 120 / 5) = %d), the sol before's 600 s kept as previous - and leave the same checked sol and the same 40 trips: %s [saved at %.0f s, boundary %.0f]%s"),
				Rolls[0], Rolls[1], PeopleAfter[0], LoadAfter[0], WaitAfter[0], WantWait, *Leg[0].Left(110), SavedAt, Boundary, Leg[0] == Leg[1] ? TEXT("") : *(FString(TEXT(" DIFFERENT: ")) + Leg[1].Left(110))));
		Expect(Leg[0].Contains(FString::Printf(TEXT("sol%d "), SolBefore + 1)), FString::Printf(TEXT("TM7 the step crossed into sol %d in both bands"), SolBefore + 1));
		Expect(bThirdOk && Rolls[2] == 1 && Leg[2] == Leg[0] && WaitAfter[2] == WantWait,
			FString::Printf(TEXT("TM7 a third leg that never saved (the fixture rebuilt on the same clock) rolls the same: one roll (%d), the same wait (%d s), riders (%d), load (%lld) and trips as the loaded legs: %s%s"), Rolls[2], WaitAfter[2], PeopleAfter[2], LoadAfter[2], *Leg[2].Left(110),
				Leg[2] == Leg[0] ? TEXT("") : *(FString(TEXT(" DIFFERENT")))));
	}

	// --- TM8. The caches, the keys, the build order, inertness. ---
	{
		// A tunnel one cell short is no line; the keys and the network of a colony that never lays one stay at 0 for ever.
		Expect(Build(true, 1, false) && AddResidents(18, 1.0), TEXT("TM8 setup: the fixture with no car, no line and no shape"));
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		uint32 KT = 7, KS = 7;
		Sim->Debug_DeepWalkKey(KT, KS);
		const int32 BuildsInert0 = Sim->GetDeepWalkBuilds();
		const uint32 TramV0 = Sim->GetTramVersion();
		const int64 Before = CommuteOf(0);
		Sim->Debug_ClearColonistStations(); // a step re-derives the seats from the real rooms (there are none): the hand-made ones go, with their version moved
		EraOnce();
		Sim->Debug_RollTransit(SolNow() + 1); // a roll with no car is a no-op
		EraOnce();
		Expect(Sim->GetTramLines().Num() == 0 && Sim->GetTramVersion() == TramV0 && KT == 0 && KS == 0 && Sim->GetDeepWalkBuilds() == BuildsInert0 && Before == 0 && Sim->GetCommuteWalked() == 0,
			TEXT("TM8 inert: with no line the tram version does not move (reads, steps and a roll), the walk keys are 0, no walk network is built and the same-floor trip is D4's 0 cm"));
		// Two assignment orders give the same trips and the same loads: riders are a function of the cells, never of the order they were laid.
		FString Digest[2];
		for (int32 Swap = 0; Swap < 2; ++Swap)
		{
			Build(true, 1, true);
			Fit(0);
			Lay(1);
			AddResidents(12, 1.0);
			FullRamp();
			for (int32 K = 0; K < 12; ++K)
			{
				const int32 k = Swap ? 11 - K : K;
				SeatOnFloor(k, -1, 13 + (k % 3));
			}
			TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
			Sim->GetCommutePairs(Pairs);
			Digest[Swap] = FString::Printf(TEXT("riders%d load%lld walked%d |"), Sim->GetTramPeople(1), Sim->GetTramPeopleLoad(1), Sim->GetCommuteWalked());
			for (const URHSimWorldSubsystem::FRHCommutePair& P : Pairs) { Digest[Swap] += FString::Printf(TEXT(" [%d>%d n%d %lld]"), P.HomeCell, P.WorkCell, P.Count, P.DistCm); }
		}
		Expect(Digest[0] == Digest[1] && Digest[0].Contains(TEXT("riders12 ")),
			FString::Printf(TEXT("TM8 the same twelve seats laid in two orders give the same riders, the same load and the same sorted trips: %s"), *Digest[0].Left(110)));
		// A tunnel cell added to a line's floor re-derives the network; a second read builds nothing.
		Build(true, 1, true);
		Fit(0);
		Lay(1);
		AddResidents(18, 1.0);
		FullRamp();
		for (int32 k = 0; k < 18; ++k) { SeatOnFloor(k, -1, k == 0 ? 13 : k); }
		const int64 First = CommuteOf(0);
		const int32 Builds1 = Sim->GetDeepWalkBuilds();
		CommuteOf(0);
		const uint32 TVNow = Sim->GetTramVersion();
		Sim->Debug_SetTramTiming(60, 1000); // the same rows: the version still moves (the edge's price is in the network)
		const int64 Second = CommuteOf(0);
		Expect(First == 11728 && Second == 11728 && Sim->GetDeepWalkBuilds() == Builds1 + 1 && Sim->GetTramVersion() != TVNow && Sim->Debug_CommuteCachesFresh() && Sim->Debug_DeepWalkFresh() && Sim->Debug_DistrictCachesFresh(),
			FString::Printf(TEXT("TM8 a second read builds no new network (%d builds); a tram-version bump rebuilds it once and the caches are all fresh (%lld, %lld)"), Builds1, First, Second));
		// Robots never ride: the robots' deep net has no tram edge, so their portals and the fitted digs are what they were without the line.
		TArray<FIntVector> PortalsWith, PortalsWithout;
		Sim->Debug_GetPortals(PortalsWith);
		Build(true, 1, true);
		Fit(0);
		Sim->Debug_GetPortals(PortalsWithout);
		Expect(PortalsWith == PortalsWithout && PortalsWith.Num() > 0, FString::Printf(TEXT("TM8 robots never ride a tram: the deep net's portals are the same with and without the line (%d)"), PortalsWith.Num()));
	}

	return Finish();
}
