#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHDistricts.h"
#include "RHSaveFrame.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// D6e lift and tram power self-test (docs/qa/2026-10-22/d6-caverns-design.md: the DIRECTOR ANSWER Q-C (a), trams carry people only; the REFRESH AT
// 31ffa6f; the MERGED DESIGN AND REVISION section 5 with RC 7, RC 8 and RC 9; docs/qa/2026-10-26/preflight/D6e-brief.md section 3). Fails loudly.
// `-transitpower`. A fitted shaft draws Cars x LiftCarW and a tram line chain points x TramWPerCell at TransitLoadPriority; the ms a sol each sat in
// the shed part of StepPower's walk set the next sol's served share, and the served share sets only the wait's capacity. Every fixture pins the
// bank to 1e9 before each step except the shortage steps themselves (RC 9). Every number below is derived by hand from the rows.
//  TP0 the rows; TP1 the entries and their order (no car: D5's numbers; the draws; the shed order at priority 6 and at 7; a shed lift is not a
//  floor); TP2 the shortage with no cooling floor (both bands) and on D5 DP6's floor; TP3 a rule-(ii) step; TP4 era parity (a paired run, D5 R2's
//  two conditions asserted); TP5 the save (mid-sol twins in both bands, the layout, the previous version, every load repair); TP6 -transit holds;
//  TP7 a dark tram (SP1).
namespace
{
	// Captures LogRedHopeSim Warning and Error lines (the load repairs must be Warnings, one each, never an Error).
	struct FTPSuiteCapture : public FOutputDevice
	{
		TArray<FString> Warnings, Errors;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category != FName(TEXT("LogRedHopeSim")))
			{
				return;
			}
			if (Verbosity == ELogVerbosity::Warning) { Warnings.Add(V); }
			if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Fatal) { Errors.Add(V); }
		}
		// Log lines reach an output device on the logging thread's schedule, so every read flushes first.
		static void Pump() { GLog->FlushThreadedLogs(); }
		int32 NumWarnings() { Pump(); return Warnings.Num(); }
		int32 NumErrors() { Pump(); return Errors.Num(); }
		FString LastWarning() { Pump(); return Warnings.Num() > 0 ? Warnings.Last() : FString(); }
		void Clear() { Pump(); Warnings.Reset(); Errors.Reset(); }
	};
}

int32 RHRunTransitPowerSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== TRANSITPOWER TEST (D6e, 2026-10-28) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("TRANSITPOWER ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("TRANSITPOWER FAIL: %s"), *What); ++Fails; }
	};
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	constexpr int32 OneSolMs = 1200000;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FName NStruct(TEXT("Struct")), NHomes(TEXT("Homes"));
	FString R, Why;
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	FTPSuiteCapture Capture;
	GLog->AddOutputDevice(&Capture);
	const auto Finish = [&]() -> int32
	{
		GLog->RemoveOutputDevice(&Capture);
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== TRANSITPOWER TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	// The pinned steps (the bank full before each: nothing scrapes), and the raw ones that leave the bank to the grid (the shortage steps only).
	const auto EraPinned = [&]()
	{
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->EraStep((float)EraDt);
	};
	const auto EraRaw = [&]()
	{
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->EraStep((float)EraDt);
	};
	const auto AgentRaw = [&]()
	{
		Clock->Debug_AdvanceSimSeconds((double)AgentDt);
		Sim->Debug_StepSim(AgentDt);
	};
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto FindB = [&](int32 Id) -> const FRHBuildingInstance*
	{
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.Id == Id) { return &Bd; } }
		return nullptr;
	};
	const auto IsOn = [&](int32 Id) { const FRHBuildingInstance* Bd = FindB(Id); return Bd && Bd->bPowered; };
	const auto Place = [&](const TCHAR* Def, double X, double Y, int32 Level = 0)
	{
		Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level);
		return LastIdOf(Def);
	};
	const auto Send = [&](const FRHCommand& C)
	{
		const int32 R0 = Rejects.Num();
		Sim->EnqueueCommand(C);
		for (int32 K = 0; K < 10 && Sim->GetUplinkQueue().Num() > 0; ++K) { EraPinned(); }
		return Rejects.Num() == R0;
	};
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	const auto Rd = [](const TArray<uint8>& Bytes, int32 Off) { int32 V = INDEX_NONE; if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(&V, Bytes.GetData() + Off, 4); } return V; };
	const auto PutI = [](TArray<uint8>& Bytes, int32 Off, int32 V) { if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(Bytes.GetData() + Off, &V, 4); } };
	const auto SolNow = [&]() { return FMath::FloorToInt32(Clock->GetSimSecondsTotal() / SolLen); };
	const auto Ms = [&](int32 Kind, int32 Id, int32 Sol) { return Sim->Debug_GetTransitShortMs(Kind, Id, Sol); };
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("tpbase"), SaveErr);
	const int32 LiftRow = Sim->GetLiftCarW(), TramRow = Sim->GetTramWPerCell(), PriorityRow = Sim->GetTransitLoadPriority();
	int32 LanderId = 0;
	const auto Reset = [&]()
	{
		FString E;
		const bool bOk = Sim->LoadColony(TEXT("tpbase"), E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("TRANSITPOWER FAIL: loading the base failed: %s"), *E); ++Fails; }
		Rejects.Reset();
		Sim->Debug_ForgetRobotTransit();
		Sim->Debug_SetExtraGenW(0.0);
		Sim->Debug_SetTransitPowerRows(LiftRow, TramRow, PriorityRow);
		LanderId = LastIdOf(TEXT("Lander"));
		return bOk;
	};
	// The lattice: shaft 0's head is the origin (2000, 2000) cm; cell (x, y) on a floor is at origin + 1000 x (x, y). Shaft 0's spiral on -1 with
	// 12 cells ends at (2, 2), so (3, 0) and (3, 2) stand in rock at the dig's edge.
	const FVector Head0(2000.0, 2000.0, 0.0);
	const auto PtCm = [&](int32 X, int32 Y) { return URHSimWorldSubsystem::LatticeToCm(Sim->GetShaftHeadCm(), FIntPoint(X, Y)); };
	const auto Fit = [&](int32 ShaftId)
	{
		Sim->Debug_AddSolidAt(LanderId, NStruct, Sim->GetLiftFitBillKg(), false);
		FString W;
		const bool bOk = Sim->Debug_FitLift(ShaftId, W);
		if (!bOk) { UE_LOG(LogRedHopeSim, Warning, TEXT("TRANSITPOWER: fit on shaft %d refused: %s"), ShaftId, *W); }
		return bOk;
	};
	const auto Tunnel = [&](int32 Level, const FIntPoint& A, const FIntPoint& B) -> int32
	{
		int32 Id = INDEX_NONE;
		FString W;
		if (!Sim->Debug_DigTunnel(Level, A, B, Id, W)) { UE_LOG(LogRedHopeSim, Warning, TEXT("TRANSITPOWER: a tunnel was refused: %s"), *W); return INDEX_NONE; }
		const int32 Len = URHSimWorldSubsystem::GetTunnelChainLength(A, B);
		return Sim->Debug_CarveTunnelCells(Id, Len) == Len ? Id : (int32)INDEX_NONE;
	};
	const auto Lay = [&](int32 TunnelId) -> bool
	{
		const FRHTunnel* T = Sim->FindTunnel(TunnelId);
		if (!T) { return false; }
		const double Kg = (double)URHSimWorldSubsystem::GetTunnelChainLength(T->A, T->B) * Sim->GetTramStructKgPerCell();
		const double Have = Sim->GetRoadStructAvailableKg();
		if (Have < Kg) { Sim->Debug_AddSolidAt(LanderId, NStruct, Kg - Have, false); }
		FString W;
		const bool bOk = Sim->Debug_LayTramLine(TunnelId, W);
		if (!bOk) { UE_LOG(LogRedHopeSim, Warning, TEXT("TRANSITPOWER: a tram line was refused: %s"), *W); }
		return bOk;
	};
	// Fixture A: shaft 0 to -3, twelve cells on each of -1..-3 (no floor past the cut-off: no cooling entry), one car. The Lander is the only
	// building and the only generator: GenBase 50 W against a load of its idle 20 W + the car's 200 W.
	const auto LiftColony = [&]() -> bool
	{
		Reset();
		Sim->ExtendShaft(3, Head0);
		bool bOk = true;
		for (int32 L = 1; L <= 3; ++L) { bOk &= Sim->ExcavateFloor(-L, 12, R, 0); }
		return bOk && Fit(0);
	};
	// Fixture B: shaft 0 to -6 (twelve cells on -1, 100 on -6: 100 W of cooling, one floor past the line), shaft 1 sunk 32 m east to -1 beside a
	// Pylon (priority 1, 10 W), a 14-point tunnel (3,0)-(16,0) on -1 with a line (70 W), a Forge (priority 2, 50 W idle), an IceDrill (6, 15 W),
	// a WaterPlant (7, 15 W). One
	// car on shaft 0, and one on shaft 1 when bShaft1 (two lifts by Id). The ids, for the walk's checks.
	struct FOrder { int32 Forge = 0, Ice = 0, Water = 0, TunnelId = INDEX_NONE; bool bOk = false; };
	const auto OrderColony = [&](bool bShaft1) -> FOrder
	{
		FOrder O;
		Reset();
		Sim->ExtendShaft(6, Head0);
		bool bOk = Sim->ExcavateFloor(-1, 12, R, 0) && Sim->ExcavateFloor(-6, 100, R, 0);
		Place(TEXT("Pylon"), PtCm(32, 0).X, PtCm(32, 0).Y);
		int32 Shaft1 = INDEX_NONE;
		bOk &= Sim->Debug_SinkShaftAt(PtCm(32, 1), 1, Shaft1, Why) && Shaft1 == 1;
		O.TunnelId = Tunnel(-1, FIntPoint(3, 0), FIntPoint(16, 0));
		O.Forge = Place(TEXT("Forge"), 8000.0, -6000.0);
		O.Ice = Place(TEXT("IceDrill"), 8000.0, 6000.0);
		O.Water = Place(TEXT("WaterPlant"), -6000.0, 6000.0);
		bOk &= O.TunnelId != INDEX_NONE && O.Forge > 0 && O.Ice > 0 && O.Water > 0 && Fit(0) && (!bShaft1 || Fit(1)) && Lay(O.TunnelId);
		O.bOk = bOk;
		return O;
	};
	// One power step on its own at the clock (no StepTransit, no population step): the bank set first. The lift and line rings' ms gained.
	struct FWalk { int32 Shed = 0, Floors = 0; double LoadW = 0.0; int32 Lift0 = 0, Lift1 = 0, Tram = 0; double Warm6 = 0.0; };
	const auto PowerStep = [&](double ExtraW, double BankWh, int32 TramId) -> FWalk
	{
		FWalk Wk;
		const int32 Sol = SolNow();
		const int32 L0 = Ms(2, 0, Sol), L1 = Ms(2, 1, Sol), T0 = Ms(3, TramId, Sol);
		const double Warm0 = Sim->GetFloorWarmth(-6);
		Sim->Debug_SetExtraGenW(ExtraW);
		Sim->Debug_SetBatteryWh(BankWh);
		Sim->Debug_StepPower((float)EraDt);
		const FRHPowerState& P = Sim->GetPower();
		Wk.Shed = P.ShedCount;
		Wk.Floors = P.ShedFloors;
		Wk.LoadW = P.LoadW;
		Wk.Lift0 = Ms(2, 0, Sol) - L0;
		Wk.Lift1 = Ms(2, 1, Sol) - L1;
		Wk.Tram = Ms(3, TramId, Sol) - T0;
		Wk.Warm6 = Sim->GetFloorWarmth(-6) - Warm0;
		Sim->Debug_SetExtraGenW(0.0);
		return Wk;
	};

	// --- TP0. The rows. ---
	{
		Expect(bBaseSaved, FString::Printf(TEXT("TP0 the fresh colony is saved as the base every case loads (%s)"), *SaveErr));
		struct FRowPin { const TCHAR* Row; double Value; double Got; };
		const FRowPin Rows[] = {
			{ TEXT("LiftCarW"), 200.0, (double)LiftRow },
			{ TEXT("TramWPerCell"), 5.0, (double)TramRow },
			{ TEXT("TransitLoadPriority"), 6.0, (double)PriorityRow },
		};
		for (const FRowPin& Pin : Rows)
		{
			const double Dt = DefsSub ? DefsSub->GetConfigTableScalar(FName(Pin.Row), -1.0) : -1.0;
			Expect(Dt == Pin.Value && Pin.Got == Pin.Value, FString::Printf(TEXT("TP0 %s is %g in the live DT_Config and %g in the sim (the CSV says %g; placeholders, director review)"), Pin.Row, Dt, Pin.Got, Pin.Value));
		}
		Expect(Sim->GetCoolingLoadPriority() == 7 && Sim->GetHoistPlacesPerSol() == 200 && Sim->GetLiftPlacesPerSol() == 1000 && Sim->GetTramPlacesPerSol() == 600 && Sim->GetMaxWaitSec() == 600,
			TEXT("TP0 the rows it reads beside them: cooling priority 7, a hoist's 200 places, a car's 1,000, a line's 600, the wait cap 600 s"));
	}

	// --- TP1. The entries and their order. ---
	{
		// (a) No car: D5 DP5's colony and numbers, unchanged (the load list, its sort and the walk are D5's when nothing is appended).
		{
			struct FShed { int32 WaterId = 0, FilterId = 0; };
			const auto ShedWorld = [&](double ExtraW, double BankWh) -> FShed
			{
				Reset();
				FShed Sh;
				Sim->ExtendShaft(10, Head0);
				Sim->ExcavateFloor(-6, 60, R, 0);   // 60 W
				Sim->ExcavateFloor(-10, 30, R, 0);  // 150 W
				Sh.WaterId = Place(TEXT("WaterPlant"), 6000.0, -3000.0);
				Sh.FilterId = Place(TEXT("AirFilter"), 6000.0, 3000.0);
				Sim->Debug_SetExtraGenW(ExtraW);
				Sim->Debug_SetBatteryWh(BankWh);
				return Sh;
			};
			const FShed A = ShedWorld(25.0, 0.0);
			EraRaw();
			const FRHPowerState& P = Sim->GetPower();
			Expect(!Sim->HasTransit() && !IsOn(A.WaterId) && IsOn(A.FilterId) && P.ShedCount == 3 && P.ShedFloors == 1 && P.LoadW == 70.0
				&& FMath::IsNearlyEqual(Sim->GetFloorWarmth(-10), EraDt / 1200.0, 1.0e-12) && Sim->GetFloorWarmth(-6) == 0.0,
				FString::Printf(TEXT("TP1 (a) no car, D5 DP5's empty-bank step: Lander, WaterPlant, then -10 shed (shed %d, floors %d), the AirFilter kept, LoadW %.1f W, Warmth -10 %.4f and -6 %.4f - D5's numbers"),
					P.ShedCount, P.ShedFloors, P.LoadW, Sim->GetFloorWarmth(-10), Sim->GetFloorWarmth(-6)));
			const FShed B = ShedWorld(25.0, 10.0);
			EraRaw();
			const FRHPowerState& P2 = Sim->GetPower();
			Expect(IsOn(B.WaterId) && P2.ShedCount == 0 && P2.LoadW == 255.0 && FMath::IsNearlyEqual(Sim->GetFloorWarmth(-10), EraDt / 1200.0, 1.0e-12) && Sim->GetFloorWarmth(-6) == 0.0,
				FString::Printf(TEXT("TP1 (a) no car, D5 DP5's rule-(ii) step: nothing shed (%d), LoadW %.1f W, and the walk still marks -10 short (Warmth %.4f) - D5's numbers"), P2.ShedCount, P2.LoadW, Sim->GetFloorWarmth(-10)));
			Sim->Debug_SetExtraGenW(0.0);
		}
		// (b) The draws: one car 200 W, four cars 800 W; a 14-point line 70 W, a 30-point line 150 W. Pinned steps at one clock: only the loads move.
		{
			Reset();
			Sim->ExtendShaft(1, Head0);
			bool bOk = Sim->ExcavateFloor(-1, 12, R, 0);
			const auto PinnedLoadW = [&]() { Sim->Debug_SetBatteryWh(1.0e9); Sim->Debug_StepPower((float)EraDt); return Sim->GetPower().LoadW; };
			const double W0 = PinnedLoadW();
			bOk &= Fit(0);
			const double W1 = PinnedLoadW();
			bOk &= Fit(0) && Fit(0) && Fit(0);
			const double W4 = PinnedLoadW();
			const int32 T14 = Tunnel(-1, FIntPoint(3, 0), FIntPoint(16, 0));
			const int32 T30 = Tunnel(-1, FIntPoint(3, 2), FIntPoint(32, 2));
			bOk &= T14 != INDEX_NONE && T30 != INDEX_NONE && Lay(T14);
			const double WT14 = PinnedLoadW();
			bOk &= Lay(T30);
			const double WT30 = PinnedLoadW();
			Expect(bOk && W1 - W0 == 200.0 && W4 - W0 == 800.0 && WT14 - W4 == 70.0 && WT30 - WT14 == 150.0 && Sim->GetPower().ShedCount == 0 && Sim->GetPower().CoolingFloors == 0,
				FString::Printf(TEXT("TP1 (b) LoadW %.1f W with no car; +%.1f W with one car (1 x 200), +%.1f W with four (4 x 200); a 14-point line adds %.1f W (14 x 5) and a 30-point line %.1f W (30 x 5)"),
					W0, W1 - W0, W4 - W0, WT14 - W4, WT30 - WT14));
		}
		// (c) The order on a shed step at the rows' priority 6: the Lander and the Pylon (1), Forge (2), IceDrill (6, a building first), lift 0,
		// lift 1 (by Id), the line, then the WaterPlant and -6's cooling (7). Full load 20 + 10 + 50 + 15 + 200 + 200 + 70 + 15 + 100 = 680 W; the
		// walk stops once the rest fits GenW (the Lander's 50 W + an extra source), so each extra cuts it after one more entry.
		{
			const FOrder O = OrderColony(true);
			const int32 Line = Sim->GetTramLines().Num() > 0 ? Sim->GetTramLines()[0].Id : 1;
			const FWalk Full = PowerStep(0.0, 1.0e9, Line);
			Expect(O.bOk && Full.LoadW == 680.0 && Full.Shed == 0 && Sim->GetPower().CoolingFloors == 1 && Full.Lift0 == 0 && Full.Tram == 0,
				FString::Printf(TEXT("TP1 (c) setup: two shafts with a car each, a 14-point line, a Forge, an IceDrill, a WaterPlant and 100 W of cooling on -6: the full load is %.1f W, and a pinned step sheds nothing and marks nothing short"), Full.LoadW));
			const FWalk W1 = PowerStep(535.0, 0.0, Line);   // GenW 585: the Lander, the Pylon, the Forge, the IceDrill
			const bool bIceShed = !IsOn(O.Ice) && IsOn(O.Water);
			const FWalk W2 = PowerStep(335.0, 0.0, Line);   // GenW 385: + lift 0
			const FWalk W3 = PowerStep(135.0, 0.0, Line);   // GenW 185: + lift 1
			const FWalk W4 = PowerStep(65.0, 0.0, Line);    // GenW 115: + the line
			const bool bWaterOnAtLine = IsOn(O.Water);
			const FWalk W5 = PowerStep(50.0, 0.0, Line);    // GenW 100: + the WaterPlant
			const bool bWaterShed = !IsOn(O.Water);
			const FWalk W6 = PowerStep(0.0, 0.0, Line);     // GenW 50: + the cooling
			const int32 Step = (int32)FMath::RoundToInt64(EraDt * 1000.0);
			Expect(bIceShed && W1.Shed == 4 && W1.Lift0 == 0 && W1.Lift1 == 0 && W1.Tram == 0 && W1.Warm6 == 0.0,
				FString::Printf(TEXT("TP1 (c) GenW 585 W: the IceDrill (priority 6, a building) sheds before any lift - shed %d, lift rings +%d / +%d ms, line +%d ms"), W1.Shed, W1.Lift0, W1.Lift1, W1.Tram));
			Expect(W2.Shed == 5 && W2.Lift0 == Step && W2.Lift1 == 0 && W2.Tram == 0 && W3.Shed == 6 && W3.Lift0 == Step && W3.Lift1 == Step && W3.Tram == 0,
				FString::Printf(TEXT("TP1 (c) GenW 385 W: lift 0 is next (+%d ms; lift 1 +%d, line +%d); GenW 185 W: then lift 1 (+%d ms), the line still not (+%d)"), W2.Lift0, W2.Lift1, W2.Tram, W3.Lift1, W3.Tram));
			Expect(W4.Shed == 7 && W4.Tram == Step && bWaterOnAtLine && W4.Warm6 == 0.0 && W5.Shed == 8 && bWaterShed && W5.Warm6 == 0.0 && W6.Shed == 9 && W6.Floors == 1 && W6.Warm6 > 0.0,
				FString::Printf(TEXT("TP1 (c) GenW 115 W: the line is next (+%d ms) with the WaterPlant still on; GenW 100 W: the WaterPlant (7) before any cooling; GenW 50 W: then -6's cooling (shed %d, %d floor, Warmth +%.4f)"), W4.Tram, W6.Shed, W6.Floors, W6.Warm6));
			// (d) The priority forced to CoolingLoadPriority (7): at 7 the WaterPlant (a building), then the cooling, then lift 0, lift 1, the line.
			Sim->Debug_SetTransitPowerRows(LiftRow, TramRow, Sim->GetCoolingLoadPriority());
			const FWalk D1 = PowerStep(420.0, 0.0, Line);   // GenW 470: through the cooling
			const FWalk D2 = PowerStep(220.0, 0.0, Line);   // GenW 270: + lift 0
			const FWalk D3 = PowerStep(20.0, 0.0, Line);    // GenW 70: + lift 1
			const FWalk D4 = PowerStep(0.0, 0.0, Line);     // GenW 50: + the line
			Sim->Debug_SetTransitPowerRows(LiftRow, TramRow, PriorityRow);
			Expect(D1.Shed == 6 && D1.Floors == 1 && D1.Warm6 > 0.0 && D1.Lift0 == 0 && D1.Lift1 == 0 && D1.Tram == 0
				&& D2.Shed == 7 && D2.Lift0 == Step && D2.Lift1 == 0 && D3.Shed == 8 && D3.Lift1 == Step && D3.Tram == 0 && D4.Shed == 9 && D4.Tram == Step,
				FString::Printf(TEXT("TP1 (d) at priority 7 (the cooling's): the cooling sheds before any lift (shed %d, %d floor, lifts +%d / +%d), then lift 0 (+%d), lift 1 (+%d, line +%d), then the line (+%d)"),
					D1.Shed, D1.Floors, D1.Lift0, D1.Lift1, D2.Lift0, D3.Lift1, D3.Tram, D4.Tram));
		}
		// (e) A shed step that cuts a lift and no floor: ShedFloors stays 0 (SP2: a lift counts in ShedCount only).
		{
			const bool bOk = LiftColony();
			const FWalk E = PowerStep(0.0, 0.0, 0);
			Expect(bOk && E.Shed == 2 && E.Floors == 0 && E.Lift0 == (int32)FMath::RoundToInt64(EraDt * 1000.0),
				FString::Printf(TEXT("TP1 (e) an empty-bank step on fixture A sheds the Lander and the lift (ShedCount %d) and no floor (ShedFloors %d); the lift's ring gains %d ms"), E.Shed, E.Floors, E.Lift0));
		}
		// (f) A lift entry does not reorder tied buildings. Four WaterPlants (7, 15 W idle each) tie; the sort is not stable, so the order among
		// them is whatever it makes of the array it is given. The Lander's 50 W against 20 + 60 W: an empty-bank step sheds the Lander and one
		// WaterPlant. Then a car at LiftCarW 0 (its entry is in the list, its draw is not): the same WaterPlant must shed, as D5's sort put it.
		{
			Reset();
			Sim->ExtendShaft(1, Head0);
			bool bOk = Sim->ExcavateFloor(-1, 12, R, 0);
			int32 Plants[4] = { 0, 0, 0, 0 };
			for (int32 K = 0; K < 4; ++K)
			{
				Plants[K] = Place(TEXT("WaterPlant"), -6000.0 - 1500.0 * K, -6000.0);
				bOk &= Plants[K] > 0 && (K == 0 || Plants[K] != Plants[K - 1]);
			}
			const auto OnMask = [&]() { FString M; for (int32 K = 0; K < 4; ++K) { M += IsOn(Plants[K]) ? TEXT("1") : TEXT("0"); } return M; };
			const bool bNoCar = !Sim->HasTransit();
			const FWalk NoCar = PowerStep(0.0, 0.0, 0);
			const FString Before = OnMask();
			Sim->Debug_SetTransitPowerRows(0, 0, PriorityRow);
			bOk &= Fit(0);
			const FWalk WithCar = PowerStep(0.0, 0.0, 0);
			const FString After = OnMask();
			Sim->Debug_SetTransitPowerRows(LiftRow, TramRow, PriorityRow);
			int32 Off = 0;
			for (const TCHAR Ch : Before) { Off += Ch == TCHAR('0') ? 1 : 0; }
			Expect(bOk && bNoCar && NoCar.Shed == 2 && WithCar.Shed == 3 && WithCar.Lift0 == (int32)FMath::RoundToInt64(EraDt * 1000.0) && Off == 1 && Before == After,
				FString::Printf(TEXT("TP1 (f) four tied WaterPlants, one shed by an empty-bank step: powered %s with no car (shed %d), %s with a car at 0 W (shed %d, the lift's ring +%d ms) - the lift's entry does not reorder them"),
					*Before, NoCar.Shed, *After, WithCar.Shed, WithCar.Lift0));
		}
	}

	// --- TP2 [RC 7]. The shortage. ---
	{
		// The bank emptied at the start of sol s-1 and the whole sol stepped unpinned; the steps start half a step before the sol, so exactly
		// one sol of steps ends inside it. Then the roll for sol s reads the slot of s-1: one sol of ms, Served 0, hoist capacity. A crowd of
		// 400 places (cargo charged in sol s-2) waits ceil(200 x 120 / 200) = 120 s, where the nominal 1,000 places would give 0 s.
		struct FShort { bool bSetup = false; int32 S = 0, MsPrev = 0, Served = -1, Rolls = 0, Wait = -1, Cooling = -1; int64 Cap = -1, Nominal = -1; };
		const auto RunShort = [&](bool bEra, bool bDeepFloor) -> FShort
		{
			FShort Out;
			if (bDeepFloor)
			{
				// D5 DP6's floor: shaft to -10, 40 cells on -10 (200 W of cooling), the Lander only; plus one car.
				Reset();
				Sim->ExtendShaft(10, Head0);
				Out.bSetup = Sim->ExcavateFloor(-10, 40, R, 0) && Fit(0);
			}
			else
			{
				Out.bSetup = LiftColony();
			}
			Out.S = SolNow() + 3;
			Sim->Debug_ChargeTransit(0, 400, (Out.S - 2) * SolLen + 1.0);
			const double Dt = bEra ? EraDt : (double)AgentDt;
			Clock->Debug_SetSimSeconds((Out.S - 1) * SolLen - 0.5 * Dt);
			Sim->Debug_SetBatteryWh(0.0);
			const int32 Rolls0 = Sim->GetTransitThresholds();
			while (Sim->GetTransitCheckedSol() < Out.S && Clock->GetSimSecondsTotal() < (Out.S + 1) * SolLen)
			{
				if (bEra) { EraRaw(); } else { AgentRaw(); }
			}
			Out.Rolls = Sim->GetTransitThresholds() - Rolls0;
			Out.MsPrev = Ms(2, 0, Out.S - 1);
			Out.Served = Sim->GetShaftServedPermille(0);
			Out.Cap = Sim->GetShaftServedCapacityPlaces(0);
			Out.Nominal = Sim->GetShaftCapacityPlaces(0);
			Out.Wait = Sim->GetTransitWaitSec(0);
			Out.Cooling = Sim->GetPower().CoolingFloors;
			return Out;
		};
		const FShort Ag = RunShort(false, false);
		const FShort Er = RunShort(true, false);
		const int32 NominalWait = Sim->ComputeTransitWaitSec(400, 1000);
		for (const FShort* X : { &Ag, &Er })
		{
			Expect(X->bSetup && X->Cooling == 0 && X->Rolls == 2 && X->MsPrev == OneSolMs && X->Served == 0 && X->Cap == 200 && X->Nominal == 1000 && X->Wait == 120 && NominalWait == 0,
				FString::Printf(TEXT("TP2 (a) %s band, no cooling floor (%d cooling entries), a Lander-only sol from an empty bank: sol %d's ring holds %d ms (one sol), Served %d, served capacity %lld places (the hoist's; GetShaftCapacityPlaces still %lld); 400 places wait %d s (nominal: %d s)"),
					X == &Ag ? TEXT("agent") : TEXT("era"), X->Cooling, X->S - 1, X->MsPrev, X->Served, X->Cap, X->Nominal, X->Wait, NominalWait));
		}
		const FShort DAg = RunShort(false, true);
		const FShort DEr = RunShort(true, true);
		for (const FShort* X : { &DAg, &DEr })
		{
			Expect(X->bSetup && X->Cooling == 1 && X->MsPrev == OneSolMs && X->Served == 0 && X->Cap == 200 && X->Wait == 120,
				FString::Printf(TEXT("TP2 (b) %s band, D5 DP6's 200 W floor on -10 plus one car: the ring holds %d ms, Served %d, capacity %lld, the crowd waits %d s - the same"),
					X == &DAg ? TEXT("agent") : TEXT("era"), X->MsPrev, X->Served, X->Cap, X->Wait));
		}
	}

	// --- TP3. A rule-(ii) step: the bank holds less than one step's deficit, nothing is shed, the lift is still short. ---
	{
		for (int32 Band = 0; Band < 2; ++Band)
		{
			const bool bEra = Band == 1;
			const bool bOk = LiftColony();
			const double Dt = bEra ? EraDt : (double)AgentDt;
			Clock->Debug_SetSimSeconds((SolNow() + 1) * SolLen + 300.0);
			EraPinned();
			const int32 Sol = FMath::FloorToInt32((Clock->GetSimSecondsTotal() + Dt) / SolLen);
			const int32 Before = Ms(2, 0, Sol);
			const double Bank = bEra ? 10.0 : 0.1; // under one step's deficit: 170 W x 60 / 50 = 204 Wh (era), 0.34 Wh (agent)
			Sim->Debug_SetBatteryWh(Bank);
			if (bEra) { EraRaw(); } else { AgentRaw(); }
			const FRHPowerState& P = Sim->GetPower();
			const int32 Gained = Ms(2, 0, Sol) - Before;
			Expect(bOk && P.ShedCount == 0 && P.LoadW == 220.0 && P.BatteryWh == 0.0 && Gained == (int32)FMath::RoundToInt64(Dt * 1000.0),
				FString::Printf(TEXT("TP3 %s band: a bank of %g Wh under one step's deficit - nothing shed (%d), LoadW %.1f W (the Lander's 20 + the car's 200), the bank clamps to 0, and the lift's ring still gains %d ms (RoundToInt64(%g x 1000))"),
					bEra ? TEXT("era") : TEXT("agent"), Bank, P.ShedCount, P.LoadW, Gained, Dt));
		}
	}

	// --- TP4. Era parity: a paired run, an agent leg and an era leg of one colony (D5 DP9's method). ---
	// Three cars (600 W) on the Lander's 2,000 Wh bank and six solar arrays: every night the bank runs dry and the lift is short; every day the
	// arrays refill it. A sol runs from midnight, so each sol's share counts that sol's evening and the end of the night before. The tolerance is two era steps of ms: 2 x 60,000 / 1200 = 100 permille. It holds under D5 R2's two conditions - a daily
	// reset (the bank is full again before every dusk) and dusk onsets within 120 s in the two bands - so both are asserted, not assumed.
	{
		constexpr int32 Sols = 8;
		const auto AdvanceTo = [&](double Frac)
		{
			const double Now = Clock->GetSimSecondsTotal();
			const double Sols0 = FMath::FloorToDouble(Now / SolLen);
			const double Target = Frac * SolLen;
			const double T = (Sols0 + (Target < Now - Sols0 * SolLen ? 1.0 : 0.0)) * SolLen + Target;
			if (T > Now) { Clock->Debug_AdvanceSimSeconds(T - Now); }
		};
		Reset();
		Sim->ExtendShaft(3, Head0);
		bool bSetup = Sim->ExcavateFloor(-1, 12, R, 0) && Fit(0) && Fit(0) && Fit(0);
		for (int32 K = 0; K < 6; ++K) { Place(TEXT("SolarArray"), 3500.0, 1000.0 + 1000.0 * K); }
		EraPinned();
		Sim->Debug_SetBatteryWh(1.0e9);
		AdvanceTo(0.5);
		FString ParErr;
		bSetup &= Sim->SaveColony(TEXT("tppar"), ParErr);
		Expect(bSetup, FString::Printf(TEXT("TP4 the paired colony (three cars on shaft 0, six arrays, the Lander's bank) is saved for both legs (%s)"), *ParErr));
		struct FPar { TArray<int32> Served, Full; TArray<double> Onset; };
		const auto RunPar = [&](bool bEra) -> FPar
		{
			FPar L;
			L.Served.Init(-1, Sols); L.Full.Init(0, Sols); L.Onset.Init(-1.0, Sols);
			FString E;
			Sim->LoadColony(TEXT("tppar"), E);
			Sim->Debug_SetExtraGenW(0.0);
			const int32 K0 = SolNow();
			const double End = (double)(K0 + Sols) * SolLen + 1.0;
			int32 LastSol = INDEX_NONE, LastMs = 0, LastChecked = Sim->GetTransitCheckedSol();
			while (Clock->GetSimSecondsTotal() < End)
			{
				if (bEra) { EraRaw(); } else { AgentRaw(); }
				const double T = Clock->GetSimSecondsTotal();
				const int32 SolEnd = FMath::FloorToInt32(T / SolLen);
				const int32 K = SolEnd - K0;
				const double Off = T - (double)SolEnd * SolLen;
				if (K >= 0 && K < Sols)
				{
					const FRHPowerState& P = Sim->GetPower();
					if (Off >= 0.25 * SolLen && Off < 0.75 * SolLen && P.BatteryWh >= P.BatteryCapWh - 1.0e-6) { L.Full[K] = 1; }
					// The evening's first short step: the ring grows after noon (the morning's ms, from the night before, are already on it).
					const int32 Now = Ms(2, 0, SolEnd);
					if (Off >= 0.5 * SolLen && L.Onset[K] < 0.0 && SolEnd == LastSol && Now > LastMs) { L.Onset[K] = Off; }
					LastSol = SolEnd;
					LastMs = Now;
				}
				// The roll's own share: the sim's GetShaftServedPermille right after the roll that read sol C - 1's slot (C the new checked sol).
				const int32 Checked = Sim->GetTransitCheckedSol();
				if (Checked != LastChecked && Checked - 1 - K0 >= 0 && Checked - 1 - K0 < Sols)
				{
					L.Served[Checked - 1 - K0] = Sim->GetShaftServedPermille(0);
				}
				LastChecked = Checked;
			}
			return L;
		};
		const FPar Ag = RunPar(false);
		const FPar Er = RunPar(true);
		bool bReset = true, bOnset = true, bFilled = true, bPartial = true;
		int32 Worst = 0;
		double WorstOnset = 0.0;
		for (int32 K = 1; K < Sols - 1; ++K)
		{
			bFilled &= Ag.Served[K] >= 0 && Er.Served[K] >= 0 && Ag.Onset[K] > 0.0 && Er.Onset[K] > 0.0;
			bReset &= Ag.Full[K] == 1 && Er.Full[K] == 1;
			bPartial &= Ag.Served[K] > 0 && Ag.Served[K] < 1000 && Er.Served[K] > 0 && Er.Served[K] < 1000;
			Worst = FMath::Max(Worst, FMath::Abs(Ag.Served[K] - Er.Served[K]));
			WorstOnset = FMath::Max(WorstOnset, FMath::Abs(Ag.Onset[K] - Er.Onset[K]));
			UE_LOG(LogRedHopeSim, Display, TEXT("TRANSITPOWER TP4 sol %d: Served agent %d, era %d | first short step after noon (seconds into the sol) agent %.1f, era %.1f | bank full by day agent %d, era %d"),
				K, Ag.Served[K], Er.Served[K], Ag.Onset[K], Er.Onset[K], Ag.Full[K], Er.Full[K]);
		}
		bOnset = WorstOnset <= 2.0 * EraDt + 1.0e-6;
		Expect(bFilled && bPartial, TEXT("TP4 every sol's served share and dusk onset were observed in both bands, and the share is partial (neither 0 nor 1000): the night is real, not vacuous"));
		Expect(bReset, TEXT("TP4 (a) the daily reset: in both bands the bank is full again by day in every sol"));
		Expect(bOnset, FString::Printf(TEXT("TP4 (b) each night's first short step falls within 120 s in the two bands (worst %.1f s)"), WorstOnset));
		Expect(Worst <= 100, FString::Printf(TEXT("TP4 |Served agent - Served era| <= 100 permille (2 x 60,000 / 1200) every sol: worst %d"), Worst));
	}

	// --- TP5. The save. ---
	{
		// (a) A colony scraping halfway through sol s-1 is saved and loaded, then run to the roll: its Served, wait and rings equal the never-saved
		// twin's, in each band. The bank starts full (2,000 Wh) at the start of s-1 and runs dry in the sol's first half; a crowd of 1,000 places
		// (charged in s-2) makes the wait read the served capacity.
		struct FTwin { int32 Served = -1, Wait = -1, MsPrev = -1, MsNow = -1; int64 Cap = -1; };
		for (int32 Band = 0; Band < 2; ++Band)
		{
			const bool bEra = Band == 1;
			const double Dt = bEra ? EraDt : (double)AgentDt;
			const bool bSetup = LiftColony();
			const int32 S = SolNow() + 3;
			Sim->Debug_ChargeTransit(0, 1000, (S - 2) * SolLen + 1.0);
			Clock->Debug_SetSimSeconds((S - 1) * SolLen - 0.5 * Dt);
			Sim->Debug_SetBatteryWh(1.0e9); // the first step clamps it to the Lander's 2,000 Wh
			const int32 Half = FMath::RoundToInt32(0.6 * SolLen / Dt);
			for (int32 K = 0; K < Half; ++K) { if (bEra) { EraRaw(); } else { AgentRaw(); } }
			const int32 MsAtSave = Ms(2, 0, S - 1);
			FString MidErr;
			const bool bSaved = Sim->SaveColony(bEra ? TEXT("tpmid_e") : TEXT("tpmid_a"), MidErr);
			const auto ToRoll = [&]() -> FTwin
			{
				while (Sim->GetTransitCheckedSol() < S && Clock->GetSimSecondsTotal() < (S + 1) * SolLen)
				{
					if (bEra) { EraRaw(); } else { AgentRaw(); }
				}
				FTwin Tw;
				Tw.Served = Sim->GetShaftServedPermille(0);
				Tw.Cap = Sim->GetShaftServedCapacityPlaces(0);
				Tw.Wait = Sim->GetTransitWaitSec(0);
				Tw.MsPrev = Ms(2, 0, S - 1);
				Tw.MsNow = Ms(2, 0, S);
				return Tw;
			};
			const FTwin Never = ToRoll();
			FString LoadErr;
			Capture.Clear();
			const bool bLoaded = Sim->LoadColony(bEra ? TEXT("tpmid_e") : TEXT("tpmid_a"), LoadErr) && LoadErr.IsEmpty();
			const bool bQuiet = Capture.NumWarnings() == 0 && Capture.NumErrors() == 0;
			const int32 MsLoaded = Ms(2, 0, S - 1);
			const FTwin Saved = ToRoll();
			Expect(bSetup && bSaved && bLoaded && bQuiet && MsAtSave > 0 && MsLoaded == MsAtSave && Never.Served > 0 && Never.Served < 1000 && Never.Wait > 0
				&& Saved.Served == Never.Served && Saved.Cap == Never.Cap && Saved.Wait == Never.Wait && Saved.MsPrev == Never.MsPrev && Saved.MsNow == Never.MsNow,
				FString::Printf(TEXT("TP5 (a) %s band: saved %.0f%% into sol %d with %d short ms already on the ring (read back %d, no Warning); at the roll the loaded colony and its never-saved twin agree: Served %d / %d, capacity %lld / %lld, wait %d / %d s, ring %d / %d ms (s-1) and %d / %d (s)"),
					bEra ? TEXT("era") : TEXT("agent"), 60.0, S - 1, MsAtSave, MsLoaded, Saved.Served, Never.Served, Saved.Cap, Never.Cap, Saved.Wait, Never.Wait, Saved.MsPrev, Never.MsPrev, Saved.MsNow, Never.MsNow));
		}
		// (b) The layout. The default colony writes Count 0 (4 payload bytes); 'RHD6' FOLLOWS 'RHL6' (found by tag, never "the last frame").
		{
			Reset();
			Expect(Sim->SaveColony(TEXT("tpdef"), SaveErr), FString::Printf(TEXT("TP5 (b) saved the default colony (%s)"), *SaveErr));
			TArray<uint8> Def;
			FFileHelper::LoadFileToArray(Def, *SaveFilePath(TEXT("tpdef")));
			uint32 Ver = 0;
			if (Def.Num() > 8) { FMemory::Memcpy(&Ver, Def.GetData() + 4, 4); }
			int32 LAt = INDEX_NONE, LBytes = 0, DAt = INDEX_NONE, DBytes = 0;
			const bool bL = RHSaveFrame::Find(Def, RHSaveFrame::LeversTag, LAt, LBytes);
			const bool bD = RHSaveFrame::Find(Def, RHSaveFrame::TransitPowerTag, DAt, DBytes);
			Expect(bL && bD && Ver == 59 && LAt + RHSaveFrame::HeaderBytes + LBytes + RHSaveFrame::FooterBytes == DAt && DBytes == 4 && Rd(Def, DAt + RHSaveFrame::HeaderBytes) == 0,
				FString::Printf(TEXT("TP5 (b) v%u: 'RHD6' sits straight after 'RHL6''s footer (found by tag), and the default colony's payload is %d bytes: Count %d"), Ver, DBytes, bD ? Rd(Def, DAt + RHSaveFrame::HeaderBytes) : -1));
		}
		// (c) A colony with a car on shaft 0, no car on shaft 1 and a line, its rings filled by four empty-bank power steps in one sol and two in the
		// next: Count 2, the lift (2, 0) then the line (3, its Id), each with both slots as the rings hold them.
		const FOrder O = OrderColony(false);
		const int32 LineId = Sim->GetTramLines().Num() > 0 ? Sim->GetTramLines()[0].Id : 1;
		const int32 S0 = SolNow() + 2;
		Clock->Debug_SetSimSeconds(S0 * SolLen + 100.0);
		for (int32 K = 0; K < 4; ++K) { PowerStep(0.0, 0.0, LineId); Clock->Debug_AdvanceSimSeconds(EraDt); }
		Clock->Debug_SetSimSeconds((S0 + 1) * SolLen + 100.0);
		for (int32 K = 0; K < 2; ++K) { PowerStep(0.0, 0.0, LineId); Clock->Debug_AdvanceSimSeconds(EraDt); }
		Sim->Debug_RollTransit(S0 + 1);
		const int32 LiftA = Ms(2, 0, S0), LiftB = Ms(2, 0, S0 + 1), TramA = Ms(3, LineId, S0), TramB = Ms(3, LineId, S0 + 1);
		Expect(Sim->SaveColony(TEXT("tpfile"), SaveErr), FString::Printf(TEXT("TP5 (c) saved the two-ring colony (%s)"), *SaveErr));
		TArray<uint8> W;
		FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("tpfile")));
		int32 At = INDEX_NONE, Bytes = 0;
		const bool bFound = RHSaveFrame::Find(W, RHSaveFrame::TransitPowerTag, At, Bytes);
		const int32 P = At + RHSaveFrame::HeaderBytes;
		// Entry e at P + 4 + 24 e: Kind, Id, then slot 0 (tag, ms) and slot 1 (tag, ms); sol S0's slot is S0 % 2.
		const auto SlotTag = [&](int32 E, int32 Sol) { return Rd(W, P + 4 + 24 * E + 8 + 8 * (Sol % 2)); };
		const auto SlotMs = [&](int32 E, int32 Sol) { return Rd(W, P + 4 + 24 * E + 12 + 8 * (Sol % 2)); };
		const bool bLayout = bFound && O.bOk && Bytes == 4 + 2 * 24 && Rd(W, P) == 2 && Rd(W, P + 4) == 2 && Rd(W, P + 8) == 0 && Rd(W, P + 28) == 3 && Rd(W, P + 32) == LineId
			&& SlotTag(0, S0) == S0 && SlotMs(0, S0) == LiftA && SlotTag(0, S0 + 1) == S0 + 1 && SlotMs(0, S0 + 1) == LiftB
			&& SlotTag(1, S0) == S0 && SlotMs(1, S0) == TramA && SlotTag(1, S0 + 1) == S0 + 1 && SlotMs(1, S0 + 1) == TramB
			&& LiftA == 4 * 60000 && LiftB == 2 * 60000 && TramA == 4 * 60000 && TramB == 2 * 60000;
		Expect(bLayout, FString::Printf(TEXT("TP5 (c) the frame reads back as written: Count %d, the lift on shaft %d (%d ms in sol %d, %d in sol %d), then line %d (%d and %d ms); shaft 1 has no car and no entry"),
			Rd(W, P), Rd(W, P + 8), LiftA, S0, LiftB, S0 + 1, Rd(W, P + 32), TramA, TramB));
		const auto TryBytes = [&](const TCHAR* Slot, const TArray<uint8>& Doc, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Doc, *SaveFilePath(Slot));
			Capture.Clear();
			return Sim->LoadColony(Slot, OutErr);
		};
		{
			FString E;
			const bool bOk = TryBytes(TEXT("tpfile2"), W, E) && E.IsEmpty() && Capture.NumWarnings() == 0 && Capture.NumErrors() == 0;
			Expect(bOk && Ms(2, 0, S0) == LiftA && Ms(2, 0, S0 + 1) == LiftB && Ms(3, LineId, S0) == TramA && Ms(3, LineId, S0 + 1) == TramB,
				FString::Printf(TEXT("TP5 (c) the file loads with no Warning and both rings read back (%s)"), *E));
		}
		// (d) The previous version is refused before anything live is touched.
		{
			TArray<uint8> T = W;
			const uint32 V57 = 57;
			FMemory::Memcpy(T.GetData() + 4, &V57, 4);
			FString E;
			const bool bOk = TryBytes(TEXT("tpv57"), T, E);
			Expect(!bOk && E.Contains(TEXT("Save format mismatch")) && E.Contains(TEXT("/59")), FString::Printf(TEXT("TP5 (d) the previous version is refused (%s)"), *E));
		}
		// (e) Doctored, resealed copies: each loads with one Warning and no Error, and repairs only what it must.
		{
			const auto Sealed = [&](TArray<uint8> T) { RHSaveFrame::Reseal(T, RHSaveFrame::TransitPowerTag); return T; };
			const int32 SlotS0 = S0 % 2;
			const int32 E0Tag = P + 4 + 8 + 8 * SlotS0, E0Ms = E0Tag + 4;
			struct FCase { const TCHAR* Slot; const TCHAR* Name; TFunction<void(TArray<uint8>&)> Edit; TFunction<bool()> Check; bool bSeal; };
			const auto LiftEmpty = [&]() { return Ms(2, 0, S0) == 0 && Ms(2, 0, S0 + 1) == 0; };
			const auto AllEmpty = [&]() { return LiftEmpty() && Ms(3, LineId, S0) == 0 && Ms(3, LineId, S0 + 1) == 0; };
			const auto LineKept = [&]() { return Ms(3, LineId, S0) == TramA && Ms(3, LineId, S0 + 1) == TramB; };
			const auto LiftKept = [&]() { return Ms(2, 0, S0) == LiftA && Ms(2, 0, S0 + 1) == LiftB; };
			// The slot as the colony now holds it, not as Ms() reads it (Ms() reads 0 for any slot whose tag is not the sol asked for): the loaded
			// colony is saved again and its 'RHD6' entry for (Kind, Id) read back as (tag, ms); (-9, -9) when there is no such entry.
			const auto RawSlot = [&](int32 Kind, int32 Id, int32 Slot) -> TPair<int32, int32>
			{
				FString RawErr;
				TArray<uint8> RawFile;
				int32 RawAt = INDEX_NONE, RawBytes = 0;
				if (!Sim->SaveColony(TEXT("tpraw"), RawErr) || !FFileHelper::LoadFileToArray(RawFile, *SaveFilePath(TEXT("tpraw")))
					|| !RHSaveFrame::Find(RawFile, RHSaveFrame::TransitPowerTag, RawAt, RawBytes))
				{
					return TPair<int32, int32>(-9, -9);
				}
				const int32 RawP = RawAt + RHSaveFrame::HeaderBytes;
				for (int32 Ix = 0; Ix < Rd(RawFile, RawP) && 4 + 24 * (Ix + 1) <= RawBytes; ++Ix)
				{
					const int32 EntryAt = RawP + 4 + 24 * Ix;
					if (Rd(RawFile, EntryAt) == Kind && Rd(RawFile, EntryAt + 4) == Id)
					{
						return TPair<int32, int32>(Rd(RawFile, EntryAt + 8 + 8 * Slot), Rd(RawFile, EntryAt + 12 + 8 * Slot));
					}
				}
				return TPair<int32, int32>(-9, -9);
			};
			const auto SlotS0Emptied = [&]() { const TPair<int32, int32> V = RawSlot(2, 0, SlotS0); return V.Key == -1 && V.Value == 0; };
			const int32 MaxCount = RH::MaxShaftsCap + RH::MaxTramLinesCap;
			const TArray<FCase> Cases = {
				{ TEXT("tpr1"), TEXT("a lost frame (its tag destroyed): every ring empty"), [&](TArray<uint8>& T) { PutI(T, At, 0x21212121); }, AllEmpty, false },
				{ TEXT("tpr2"), TEXT("a count past the payload (5): every ring empty"), [&](TArray<uint8>& T) { PutI(T, P, 5); }, AllEmpty, true },
				{ TEXT("tpr3"), TEXT("an unknown shaft (7): that entry dropped"), [&](TArray<uint8>& T) { PutI(T, P + 8, 7); }, [&]() { return LiftEmpty() && LineKept(); }, true },
				{ TEXT("tpr4"), TEXT("an unfitted shaft (1): that entry dropped"), [&](TArray<uint8>& T) { PutI(T, P + 8, 1); }, [&]() { return LiftEmpty() && Ms(2, 1, S0) == 0 && LineKept(); }, true },
				{ TEXT("tpr5"), TEXT("an unknown line (9): that entry dropped"), [&](TArray<uint8>& T) { PutI(T, P + 32, 9); }, [&]() { return LiftKept() && Ms(3, LineId, S0) == 0 && Ms(3, LineId, S0 + 1) == 0; }, true },
				{ TEXT("tpr6"), TEXT("a tag of -2: that slot emptied (re-saved as tag -1, 0 ms)"), [&](TArray<uint8>& T) { PutI(T, E0Tag, -2); PutI(T, E0Ms, 5); }, [&]() { return Ms(2, 0, S0) == 0 && Ms(2, 0, S0 + 1) == LiftB && LineKept() && SlotS0Emptied(); }, true },
				{ TEXT("tpr7"), TEXT("a tag whose parity is not its slot's: that slot emptied (re-saved as tag -1, 0 ms)"), [&](TArray<uint8>& T) { PutI(T, E0Tag, S0 - 1); }, [&]() { return Ms(2, 0, S0) == 0 && Ms(2, 0, S0 - 1) == 0 && Ms(2, 0, S0 + 1) == LiftB && LineKept() && SlotS0Emptied(); }, true },
				{ TEXT("tpr8"), TEXT("ms of 1,200,001: that slot emptied"), [&](TArray<uint8>& T) { PutI(T, E0Ms, OneSolMs + 1); }, [&]() { return Ms(2, 0, S0) == 0 && Ms(2, 0, S0 + 1) == LiftB && LineKept(); }, true },
				{ TEXT("tpr9"), TEXT("a tag past the threshold sol + 1 (its own parity): that slot emptied"), [&](TArray<uint8>& T) { PutI(T, E0Tag, S0 + 100); }, [&]() { return Ms(2, 0, S0 + 100) == 0 && Ms(2, 0, S0 + 1) == LiftB && LineKept(); }, true },
				{ TEXT("tpr10"), TEXT("a repeated Id (the line's entry made a second shaft 0, with 7 ms in sol S0's slot): the first entry kept, the repeat dropped"), [&](TArray<uint8>& T) { PutI(T, P + 28, 2); PutI(T, P + 32, 0); PutI(T, P + 28 + 12 + 8 * SlotS0, 7); }, [&]() { return LiftKept() && Ms(3, LineId, S0) == 0; }, true },
				{ TEXT("tpr11"), TEXT("a kind other than 2 or 3 (4): that entry dropped"), [&](TArray<uint8>& T) { PutI(T, P + 28, 4); }, [&]() { return LiftKept() && Ms(3, LineId, S0) == 0; }, true },
				{ TEXT("tpr12"), TEXT("a negative count (-1): every ring empty"), [&](TArray<uint8>& T) { PutI(T, P, -1); }, AllEmpty, true },
				{ TEXT("tpr13"), TEXT("a count past the cap (RH::MaxShaftsCap + RH::MaxTramLinesCap + 1): every ring empty"), [&](TArray<uint8>& T) { PutI(T, P, MaxCount + 1); }, AllEmpty, true },
			};
			bool bAll = true;
			FString Failed;
			for (const FCase& C : Cases)
			{
				TArray<uint8> T = W;
				C.Edit(T);
				FString E;
				const bool bOk = TryBytes(C.Slot, C.bSeal ? Sealed(T) : T, E) && E.IsEmpty();
				const int32 NW = Capture.NumWarnings(), NE = Capture.NumErrors();
				const FString Warn = Capture.LastWarning();
				const bool bCase = bOk && NW == 1 && NE == 0 && Warn.Contains(TEXT("LoadColony v58:")) && C.Check() && Sim->GetLiftCars(0) == 1 && Sim->GetTramLines().Num() == 1;
				if (!bCase) { Failed += FString::Printf(TEXT(" [%s: loaded %d, %d warning(s), %d error(s)]"), C.Slot, bOk ? 1 : 0, NW, NE); }
				bAll &= bCase;
				UE_LOG(LogRedHopeSim, Display, TEXT("TRANSITPOWER TP5 (e) %s -> %s"), C.Name, *Warn.Right(Warn.Len() - FMath::Max(0, Warn.Find(TEXT("LoadColony v58:")))));
			}
			Expect(bAll, FString::Printf(TEXT("TP5 (e) %d doctored copies (the brief's eight: a lost frame, a bad count, an unknown shaft, an unfitted shaft, an unknown line, a tag of -2, a wrong parity, 1,200,001 ms; and five more: a tag past the threshold, a repeated Id, a bad kind, a negative count, a count past the cap) each load with one Warning and no Error, the colony's cars and line kept%s"),
				Cases.Num(), *Failed));
		}
	}

	// --- TP6. -transit holds: with the bank pinned nothing is short, the served capacity is D4's and TR4's real roll is 60 s. ---
	{
		Reset();
		Sim->ExtendShaft(1, Head0);
		bool bOk = Sim->ExcavateFloor(-1, 12, R, 0) && Fit(0);
		for (int32 K = 0; K < 25; ++K) { EraPinned(); }
		const int64 Cap1 = Sim->GetShaftServedCapacityPlaces(0);
		const bool bOne = Cap1 == Sim->GetShaftCapacityPlaces(0) && Cap1 == 1000 && Sim->GetShaftServedPermille(0) == 1000 && Ms(2, 0, SolNow()) == 0 && Ms(2, 0, SolNow() - 1) == 0;
		bOk &= Fit(0) && Fit(0) && Fit(0);
		for (int32 K = 0; K < 25; ++K) { EraPinned(); }
		const int64 Cap4 = Sim->GetShaftServedCapacityPlaces(0);
		const bool bFour = Cap4 == Sim->GetShaftCapacityPlaces(0) && Cap4 == 4000 && Sim->GetShaftServedPermille(0) == 1000;
		const int32 S = SolNow();
		Sim->Debug_ChargeTransit(0, 6000, S * SolLen + 1.0);
		while (Sim->GetTransitCheckedSol() < S + 2 && Clock->GetSimSecondsTotal() < (S + 4) * SolLen) { EraPinned(); }
		const int32 Wait = Sim->GetTransitWaitSec(0);
		Expect(bOk && bOne && bFour && Wait == 60 && Sim->GetTransitCheckedSol() == S + 2,
			FString::Printf(TEXT("TP6 pinned: the served capacity equals GetShaftCapacityPlaces - %lld with one car, %lld with four - at Served 1000; the real roll of 6,000 places through four cars is ceil(2,000 x 120 / 4,000) = 60 s (%d)"), Cap1, Cap4, Wait));
	}

	// --- TP7 (SP1). A dark tram: a line at Served 0 keeps a capacity of 1, so its crowd waits MaxWaitSec, never 0. ---
	{
		// D6d's fixture (the -trams suite's TM4): shaft 0's dig on -1, shaft 1's dig 34 cells east, the 30-cell tunnel (3,0)-(32,0) between them, a
		// Homes shape on -1, eighteen residents, every seat in cell 13 (dig B): twelve ride, the line's load 2 x 12 = 24.
		Reset();
		Sim->Debug_SetGrowthEnabled(false);
		const FIntPoint Column1(34, 1);
		Sim->ExtendShaft(1, Head0);
		bool bOk = Sim->ExcavateFloor(-1, 12, R, 0);
		Place(TEXT("Pylon"), PtCm(Column1.X, Column1.Y - 1).X, PtCm(Column1.X, Column1.Y - 1).Y);
		int32 Shaft1 = INDEX_NONE;
		bOk &= Sim->Debug_SinkShaftAt(PtCm(Column1.X, Column1.Y), 1, Shaft1, Why) && Shaft1 == 1;
		bOk &= Sim->ExcavateFloor(-1, 6, R, 1);
		const int32 TId = Tunnel(-1, FIntPoint(3, 0), FIntPoint(32, 0));
		Place(TEXT("AirFilter"), PtCm(-1, -1).X, PtCm(-1, -1).Y, -1);
		Sim->Debug_ForceRateFloor(-1);
		Sim->Debug_SetColonistsPerCell(100.0);
		FRHCommand Paint;
		Paint.Verb = FName("PaintDistrict");
		Paint.Target = NHomes;
		Paint.Level = -1;
		Paint.Value = 1;
		Paint.Location = PtCm(60, 60);
		Paint.LocationB = PtCm(60, 60);
		bOk &= Send(Paint);
		Sim->Debug_ClearColonistStations();
		Sim->Debug_SetColonistsPerCell(100.0);
		const int32 Before = Sim->GetPopulation();
		bOk &= Sim->Debug_AddColonists(18) == 18;
		Sim->Debug_SetColonistsPerCell(1.0);
		for (int32 i = Before; i < Sim->GetColonists().Num(); ++i) { Sim->Debug_SetColonistHome(Sim->GetColonists()[i].Id, 0, -1); }
		bOk &= TId == 1 && Fit(0) && Lay(TId);
		Sim->Debug_SetCommuteTuning(0.0, 1.0, 30000.0, 6000.0);
		for (int32 k = Before; k < Sim->GetColonists().Num(); ++k) { Sim->Debug_SetColonistStation(Sim->GetColonists()[k].Id, FIntVector(-1, 13, 0)); }
		const int32 Line = 1;
		const int64 Load = Sim->GetTramPeopleLoad(Line);
		const int32 S = SolNow() + 2;
		Sim->Debug_RollTransit(S - 1);
		const int64 CapLit = Sim->GetTramServedCapacityPlaces(Line);
		const int32 WaitLit = Sim->GetTramWaitSec(Line);
		Expect(bOk && Load == 24 && CapLit == 600 && Sim->GetTramServedPermille(Line) == 1000 && WaitLit == 0,
			FString::Printf(TEXT("TP7 setup: twelve riders load the line with %lld places; at Served 1000 its capacity is exactly %lld (TramPlacesPerSol) and the wait %d s"), Load, CapLit, WaitLit));
		// The dark sol: the bank emptied before each of sol s-1's twenty power steps (the rest of the colony is not stepped).
		Clock->Debug_SetSimSeconds((S - 1) * SolLen);
		for (int32 K = 0; K < 20; ++K) { Sim->Debug_SetBatteryWh(0.0); Sim->Debug_StepPower((float)EraDt); Clock->Debug_AdvanceSimSeconds(EraDt); }
		const int32 Dark = Ms(3, Line, S - 1);
		for (int32 k = Before; k < Sim->GetColonists().Num(); ++k) { Sim->Debug_SetColonistStation(Sim->GetColonists()[k].Id, FIntVector(-1, 13, 0)); }
		Sim->Debug_RollTransit(S);
		const int32 WaitDark = Sim->GetTramWaitSec(Line);
		Expect(Dark == OneSolMs && Sim->GetTramServedPermille(Line) == 0 && Sim->GetTramServedCapacityPlaces(Line) == 1 && Sim->GetTramPeopleLoad(Line) == 24 && WaitDark == Sim->GetMaxWaitSec() && WaitDark == 600,
			FString::Printf(TEXT("TP7 after a dark sol (%d ms) the line is served 0 permille, its capacity floors at %lld place (never 0), and a load of %lld waits %d s - MaxWaitSec, not 0"),
				Dark, Sim->GetTramServedCapacityPlaces(Line), Sim->GetTramPeopleLoad(Line), WaitDark));
	}

	// The rows end where the data has them.
	Sim->Debug_SetTransitPowerRows(LiftRow, TramRow, PriorityRow);
	Sim->Debug_SetExtraGenW(0.0);
	Expect(Sim->GetLiftCarW() == 200 && Sim->GetTramWPerCell() == 5 && Sim->GetTransitLoadPriority() == 6 && Sim->GetExtraGenW() == 0.0, TEXT("TP-end the harness left the rows as the data has them and the extra generation at 0"));
	return Finish();
}
