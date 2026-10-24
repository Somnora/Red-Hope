#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// D5 depth self-test (depth has a price; docs/qa/2026-10-21/d5-depth-design.md: the DIRECTOR ANSWERs Q-A (a), Q-B (a), Q-C (a),
// the PROFESSOR RE-REVIEW conditions 1-5, the REVISION items 1-10, the PROFESSOR REVIEW, the design). Fails loudly. `-depth`.
// Every number below is derived from the rows; the headless world ticks no Mass robot and nothing here needs one.
//  DP0 the rows, the caps, the cut-off below MaxDepth, the radiation profile; DP1 a twin with one carved -6 floor (the cut-off:
//  the literal Cells x 1 W check and the absolute -5 pins); DP2 bore -6 / -7 / -8 (hours, fuel); DP3 carve, a tunnel front, a
//  growth floor-homes project; DP4 the cooling load; DP5 the shed prefix on a scraping step (rule (i) and rule (ii)); DP6 Warmth
//  through the real grid (a Lander-only night, recovery by generation); DP7 no harm (twins, Hope only); DP8 Shelter;
//  DP9 era parity (a paired 10-sol run); DP10 the limits; DP11 the save; DP12 depth is far (the commute, pinned).
int32 RHRunDepthSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== DEPTH TEST (D5 depth, 2026-10-22) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("DEPTH ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("DEPTH FAIL: %s"), *What); ++Fails; }
	};
	const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	const FName NOx(TEXT("Oxygen")), NWater(TEXT("Water")), NFood(TEXT("Food")), NHyd(TEXT("Hydrogen")), NStruct(TEXT("Struct"));
	const FName NLiving(TEXT("LivingQuarters"));
	FString R, Why;
	TArray<FString> Alerts, Rejects;
	const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& Text) { Alerts.Add(Text); });
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	const auto CountAlerts = [&](const TCHAR* Part, int32 Level) { int32 N = 0; for (const FString& A : Alerts) { N += (A.Contains(Part) && A.Contains(FString::Printf(TEXT("Floor %d "), Level))) ? 1 : 0; } return N; };
	const auto Finish = [&]() -> int32
	{
		Sim->OnAlert.Remove(AlertH);
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== DEPTH TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	// The healthy-bank step (the bank pinned full: nothing sheds); the raw steps leave the bank to the grid.
	const auto Step = [&]()
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
	const auto FindB = [&](int32 Id) -> const FRHBuildingInstance*
	{
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.Id == Id) { return &Bd; } }
		return nullptr;
	};
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto Place = [&](const TCHAR* Def, double X, double Y, int32 Level = 0)
	{
		Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), Level);
		return LastIdOf(Def);
	};
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	const FVector Head0(2000.0, 2000.0, 0.0);
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("depthbase"), SaveErr);
	int32 LanderId = 0;
	const auto Reset = [&]()
	{
		FString E;
		const bool bOk = Sim->LoadColony(TEXT("depthbase"), E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("DEPTH FAIL: loading the base failed: %s"), *E); ++Fails; }
		Rejects.Reset();
		Alerts.Reset();
		Sim->Debug_SetExtraGenW(0.0);
		Sim->Debug_SetHopeWarmMax(Sim->GetHopeWarmMax() > 0.0 ? Sim->GetHopeWarmMax() : 5.0);
		LanderId = LastIdOf(TEXT("Lander"));
		return bOk;
	};
	const double HopeWarmRow = Sim->GetHopeWarmMax();
	// A Borer, power and a bank (CanBore is patched in memory as -shafts does, so this proves the code path).
	const auto Kit = [&]()
	{
		FRHBuildingRow* BorerRow = const_cast<FRHBuildingRow*>(DefsSub ? DefsSub->GetBuilding(FName("Borer")) : nullptr);
		if (BorerRow) { BorerRow->CanBore = true; BorerRow->H2BurnKgPerHour = 1.5f; }
		const int32 Id = Place(TEXT("Borer"), 2000.0, 2000.0);
		Place(TEXT("SolarArray"), 3500.0, 1000.0);
		Place(TEXT("BatteryBank"), 1000.0, 3500.0);
		return Id;
	};
	const auto Send = [&](const FRHCommand& C)
	{
		Sim->EnqueueCommand(C);
		for (int32 K = 0; K < 10 && Sim->GetUplinkQueue().Num() > 0; ++K) { Step(); }
	};
	const auto BoreOrder = [&](int32 To) { FRHCommand C; C.Verb = FName("Bore"); C.Value = To; Send(C); };
	const auto ExcOrder = [&](int32 Level, int32 N) { FRHCommand C; C.Verb = FName("Excavate"); C.Level = Level; C.Value = N; Send(C); };
	const auto UntilDepth = [&](int32 D, int32 Max) { for (int32 K = 0; K < Max && Sim->GetShaftDepth(0) < D; ++K) { Step(); } return Sim->GetShaftDepth(0) >= D; };
	const auto UntilIdle = [&](int32 BorerId, int32 Max) { for (int32 K = 0; K < Max; ++K) { const FRHBuildingInstance* B = FindB(BorerId); if (B && B->BatchRemaining_h <= 0.0 && B->ActiveRecipe.IsNone()) { return true; } Step(); } return false; };
	// The hours a batch STARTED with, read after the step that started it: that step spent one era budget (SubDt / 50 sol-hours) at once.
	const auto StartedHours = [&](int32 BorerId) { const FRHBuildingInstance* B = FindB(BorerId); return B ? B->BatchRemaining_h + EraDt / 50.0 : -1.0; };
	const auto Permille = [&](int32 Level) { return 1000 + 150 * FMath::Max(0, -Level - 5); };
	const auto SumBuildingW = [&]()
	{
		// StepPower's Pass 1 re-derived: each building's wanted draw, summed in building order.
		double Sum = 0.0;
		const double StepEnd = Clock->GetSimSecondsTotal();
		const double DayFrac = Sim->GetDaylightFraction(StepEnd - EraDt, StepEnd);
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings())
		{
			if (Bd.bUnderConstruction || Bd.bManualOff) { continue; }
			const FRHBuildingRow* Def = DefsSub->GetBuilding(Bd.DefName);
			if (!Def) { continue; }
			const bool bGridBatch = Bd.BatchRemaining_h > 0.0 && !Bd.bBatchOnH2;
			Sum += !bGridBatch ? Def->PowerIdle_W : (Bd.bDayShift ? Def->PowerIdle_W + (Def->PowerDraw_W - Def->PowerIdle_W) * DayFrac : Def->PowerDraw_W);
		}
		return Sum;
	};
	// A rated, populated deep floor: a shaft to -10, Cells carved on -10, an air filter, Living cells, colonists to capacity, deep stocks.
	const auto Populated = [&](int32 Cells, int32 LivingCells)
	{
		Reset();
		Sim->AddStock(NOx, 2.0e6);
		Sim->AddStock(NWater, 2.0e6);
		Sim->AddStock(NFood, 2.0e6);
		Sim->ExtendShaft(10, Head0);
		Sim->ExcavateFloor(-10, Cells, R, 0);
		Place(TEXT("AirFilter"), Head0.X, Head0.Y + 500.0, -10);
		Step();
		Sim->Debug_ForceRateFloor(-10);
		for (int32 I = 0; I < LivingCells; ++I) { Sim->DesignateRoom(-10, I, NLiving, R); }
		Sim->Debug_SetColonistsPerCell(1.0);
		Step();
		return Sim->Debug_AddColonists(Sim->GetHousingCapacity());
	};

	// --- DP0. The rows, the caps, the cut-off, the radiation profile. ---
	{
		Expect(bBaseSaved, FString::Printf(TEXT("DP0 the fresh colony is saved as the base every case loads (%s)"), *SaveErr));
		struct FRowPin { const TCHAR* Row; double Value; double Got; };
		const FRowPin Rows[] = {
			{ TEXT("MaxDepth"), 20.0, (double)Sim->GetMaxDepth() },
			{ TEXT("DepthCostFromFloor"), 5.0, (double)Sim->GetDepthCostFromFloor() },
			{ TEXT("DigPermillePerDeepFloor"), 150.0, (double)Sim->GetDigPermillePerDeepFloor() },
			{ TEXT("CoolMilliWPerCellPerDeepFloor"), 1000.0, (double)Sim->GetCoolMilliWPerCellPerDeepFloor() },
			{ TEXT("CoolingLoadPriority"), 7.0, (double)Sim->GetCoolingLoadPriority() },
			{ TEXT("HeatRiseSols"), 1.0, Sim->GetHeatRiseSols() },
			{ TEXT("HeatFallSols"), 0.5, Sim->GetHeatFallSols() },
			{ TEXT("HopeWarmMax"), 5.0, Sim->GetHopeWarmMax() },
			{ TEXT("HopeShelterMax"), 3.0, Sim->GetHopeShelterMax() },
		};
		for (const FRowPin& Pin : Rows)
		{
			const double Dt = DefsSub ? DefsSub->GetConfigTableScalar(FName(Pin.Row), -1.0) : -1.0;
			Expect(Dt == Pin.Value && Pin.Got == Pin.Value, FString::Printf(TEXT("DP0 %s is %g in the live DT_Config and %g in the sim (the CSV says %g; pure data: this fails on drift)"), Pin.Row, Dt, Pin.Got, Pin.Value));
		}
		Expect(Sim->GetDepthCostFromFloor() < Sim->GetMaxDepth() && Sim->GetDepthCostFromFloor() >= 0 && Sim->GetCoolingLoadPriority() > 0 && Sim->GetHeatRiseSols() > 0.0 && Sim->GetHeatFallSols() > 0.0,
			TEXT("DP0 the line sits below MaxDepth and every new row is above 0 (the two heat rates divide)"));
		Expect(Sim->GetDigPermille(0) == 1000 && Sim->GetDigPermille(-1) == 1000 && Sim->GetDigPermille(-5) == 1000 && Sim->GetDigPermille(-6) == 1150 && Sim->GetDigPermille(-7) == 1300 && Sim->GetDigPermille(-10) == 1750 && Sim->GetDigPermille(-20) == 3250,
			FString::Printf(TEXT("DP0 DigPermille: 0 and -1..-5 are 1000, -6 is %d, -7 %d, -10 %d, -20 %d (1000 + 150 x floors past -5)"), Sim->GetDigPermille(-6), Sim->GetDigPermille(-7), Sim->GetDigPermille(-10), Sim->GetDigPermille(-20)));
		Expect(RH::MaxDepthCap == 30 && Sim->Debug_ApplyMaxDepthRow(40) == 30 && Sim->Debug_ApplyMaxDepthRow(0) == 1 && Sim->Debug_ApplyMaxDepthRow(20) == 20,
			TEXT("DP0 a MaxDepth row of 40 clamps to the cap of 30 (and 0 to 1); the row's own 20 is restored"));
		Expect(Sim->Debug_ApplyDepthCostRow(99) == 30 && Sim->Debug_ApplyDepthCostRow(-3) == 0 && Sim->Debug_ApplyDepthCostRow(5) == 5,
			TEXT("DP0 a DepthCostFromFloor row of 99 clamps to 30 and -3 to 0 (O6); the row's own 5 is restored"));
		FString Profile;
		bool bMonotone = true;
		float Prev = 1.0e9f;
		for (int32 L = 0; L >= -Sim->GetMaxDepth(); --L)
		{
			const float Rad = Sim->GetRadiationAtLevel(L);
			bMonotone &= Rad <= Prev && Rad >= 0.f;
			Prev = Rad;
			Profile += FString::Printf(TEXT("%s%d:%.3e"), L == 0 ? TEXT("") : TEXT(" "), L, (double)Rad);
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("DEPTH radiation profile 0..-%d (%%.3e, the same 0.05 ^ depth the baseline prints at %%.4f): %s"), Sim->GetMaxDepth(), *Profile);
		Expect(bMonotone && Sim->GetRadiationAtLevel(-20) < 1.0e-20f, TEXT("DP0 the radiation index never rises with depth and is below 1e-20 at -20: it cannot be the payoff for going deep (so shielding pays in Hope)"));
	}

	// --- DP1. A twin with one carved -6 floor: -1..-5 do not move; the literal Cells x 1 W check and the absolute -5 pins. ---
	{
		struct FTwin
		{
			TArray<double> Hours;       // each -1..-5 batch's started hours
			TArray<double> LoadW;       // LoadW each step
			TArray<double> Trace;       // FloorO2Kg and rating of -1..-5 each step
			double CoolingW = 0.0;
			int32 CoolingFloors = 0;
			bool bBuildingSumEqual = true;
			bool bDayShift = false;
		};
		const auto RunTwin = [&](bool bDeepFloor) -> FTwin
		{
			FTwin T;
			Reset();
			const int32 BorerId = Kit();
			Sim->AddStock(NOx, 5000.0);
			Sim->ExtendShaft(6, Head0);
			for (int32 L = 1; L <= 5; ++L)
			{
				Sim->ExcavateFloor(-L, 6, R, 0);
				Place(TEXT("AirFilter"), Head0.X, Head0.Y + 500.0, -L);
			}
			if (bDeepFloor) { Sim->ExcavateFloor(-6, 40, R, 0); }
			const FRHBuildingInstance* Borer = FindB(BorerId);
			T.bDayShift = Borer && Borer->bDayShift;
			const auto Record = [&]()
			{
				T.LoadW.Add(Sim->GetPower().LoadW);
				for (int32 L = 1; L <= 5; ++L) { T.Trace.Add(Sim->GetFloorO2Kg(-L)); T.Trace.Add(Sim->IsFloorRated(-L) ? 1.0 : 0.0); }
				T.CoolingW = Sim->GetPower().CoolingW;
				T.CoolingFloors = Sim->GetPower().CoolingFloors;
				if (!bDeepFloor) { T.bBuildingSumEqual &= Near(Sim->GetPower().LoadW, SumBuildingW(), 1.0e-9); }
			};
			// Each of -1..-5 is ordered on its own with the Borer idle, so the step that executes the order starts the batch.
			for (int32 L = 1; L <= 5; ++L)
			{
				ExcOrder(-L, 1);
				T.Hours.Add(StartedHours(BorerId));
				Record();
				UntilIdle(BorerId, 40);
			}
			for (int32 K = 0; K < 60; ++K) { Step(); Record(); } // three sols in all, with the batches above
			return T;
		};
		const FTwin A = RunTwin(true);
		const FTwin B = RunTwin(false);
		Expect(!A.bDayShift && !B.bDayShift, TEXT("DP1 the Borer is not a day-shift machine (so a batch's first step spends the whole era budget)"));
		bool bHoursEqual = A.Hours.Num() == 5 && B.Hours.Num() == 5;
		for (int32 I = 0; bHoursEqual && I < 5; ++I) { bHoursEqual &= Near(A.Hours[I], B.Hours[I], 1.0e-12); }
		Expect(bHoursEqual, FString::Printf(TEXT("DP1 every -1..-5 batch started with the same hours in both twins (A: %.4f %.4f %.4f %.4f %.4f)"), A.Hours.Num() > 4 ? A.Hours[0] : -1.0, A.Hours.Num() > 4 ? A.Hours[1] : -1.0, A.Hours.Num() > 4 ? A.Hours[2] : -1.0, A.Hours.Num() > 4 ? A.Hours[3] : -1.0, A.Hours.Num() > 4 ? A.Hours[4] : -1.0));
		bool bAbs = A.Hours.Num() == 5 && B.Hours.Num() == 5;
		for (int32 I = 0; bAbs && I < 5; ++I) { bAbs &= Near(A.Hours[I], 3.0, 1.0e-9) && Near(B.Hours[I], 3.0, 1.0e-9); }
		Expect(bAbs, FString::Printf(TEXT("DP1 absolute pins: a CarveCell on each of -1..-5 starts at exactly 3.0 h in BOTH twins (-5 reads %.9f and %.9f: BatchRemaining_h + SubDt / 50)"), A.Hours.Num() > 4 ? A.Hours[4] : -1.0, B.Hours.Num() > 4 ? B.Hours[4] : -1.0));
		Expect(A.Trace == B.Trace && A.Trace.Num() > 0, FString::Printf(TEXT("DP1 FloorO2Kg and the rating of each of -1..-5 are equal in both twins at every one of %d steps"), A.LoadW.Num()));
		bool bLoadDiff = A.LoadW.Num() == B.LoadW.Num() && A.LoadW.Num() > 0;
		double Worst = 0.0;
		for (int32 I = 0; bLoadDiff && I < A.LoadW.Num(); ++I) { Worst = FMath::Max(Worst, FMath::Abs(A.LoadW[I] - B.LoadW[I] - 40.0)); }
		bLoadDiff &= Worst <= 1.0e-9;
		Expect(bLoadDiff && A.CoolingFloors == 1 && Near(A.CoolingW, 40.0, 1.0e-12), FString::Printf(TEXT("DP1 Cells x 1 W: A's LoadW exceeds B's by exactly 40 cells x 1 W = 40.000 W at every step (worst error %.3g W); A has %d cooling floor (%.3f W)"), Worst, A.CoolingFloors, A.CoolingW));
		Expect(B.bBuildingSumEqual && B.CoolingFloors == 0 && B.CoolingW == 0.0, FString::Printf(TEXT("DP1 B has no cooling entry (%d floors, %.3f W) and its LoadW equals the sum of its building entries at every step"), B.CoolingFloors, B.CoolingW));
	}

	// --- DP2. Bore: the hours and the fuel bill scale with the floor. ---
	{
		Reset();
		const int32 BorerId = Kit();
		Sim->ExtendShaft(5, Head0);
		BoreOrder(6);
		const FRHBuildingInstance* Borer = FindB(BorerId);
		Expect(Borer && Borer->ActiveRecipe == FName("BoreFloor") && Near(StartedHours(BorerId), 13.8, 1.0e-9),
			FString::Printf(TEXT("DP2 BoreFloor on -6 starts at 13.8 h (12 h x 1150 per mille): %.9f"), StartedHours(BorerId)));
		Expect(UntilDepth(6, 60), TEXT("DP2 the Borer bored -6"));
		UntilIdle(BorerId, 10);
		BoreOrder(7);
		Expect(Near(StartedHours(BorerId), 15.6, 1.0e-9), FString::Printf(TEXT("DP2 BoreFloor on -7 starts at 15.6 h (12 h x 1300 per mille, exactly): %.9f"), StartedHours(BorerId)));
		Expect(UntilDepth(7, 60), TEXT("DP2 the Borer bored -7"));
		UntilIdle(BorerId, 10);
		Sim->AddStock(NHyd, 100.0);
		const double H2Before = Sim->GetStock(NHyd);
		BoreOrder(8);
		Borer = FindB(BorerId);
		const double Want = 12.0 * 1450.0 / 1000.0;
		Expect(Borer && Borer->bBatchOnH2 && Near(StartedHours(BorerId), Want, 1.0e-9) && Near(H2Before - Sim->GetStock(NHyd), 1.5 * Want, 1.0e-9),
			FString::Printf(TEXT("DP2 on hydrogen -8 starts at %.2f h (1450 per mille) and the fuel bill scales with it: %.4f kg (1.5 kg/h x %.2f h), not the 18 kg of a shallow floor"), StartedHours(BorerId), H2Before - Sim->GetStock(NHyd), Want));
		Expect(UntilDepth(8, 80) && Sim->GetShaftDepth(0) == 8, TEXT("DP2 the Borer bored -8"));
		// A shallow floor keeps today's fuel bill as written.
		Reset();
		const int32 Borer2 = Kit();
		Sim->AddStock(NHyd, 100.0);
		Sim->ExtendShaft(5, Head0);
		Sim->ExcavateFloor(-5, 2, R, 0);
		const double H2Pre = Sim->GetStock(NHyd);
		ExcOrder(-5, 1);
		Expect(Near(StartedHours(Borer2), 3.0, 1.0e-9) && Near(H2Pre - Sim->GetStock(NHyd), 4.5, 1.0e-9),
			FString::Printf(TEXT("DP2 a carve on -5 on hydrogen is today's 3 h and 4.5 kg (%.6f h, %.6f kg)"), StartedHours(Borer2), H2Pre - Sim->GetStock(NHyd)));
	}

	// --- DP3. Carve, a tunnel front, a growth floor-homes project. ---
	{
		Reset();
		const int32 BorerId = Kit();
		Sim->ExtendShaft(6, Head0);
		ExcOrder(-6, 2);
		Expect(Near(StartedHours(BorerId), 3.45, 1.0e-9), FString::Printf(TEXT("DP3 a carve on -6 starts at 3.45 h (3 h x 1150 per mille): %.9f"), StartedHours(BorerId)));
		UntilIdle(BorerId, 20);
		int32 TunnelId = INDEX_NONE;
		Sim->ExcavateFloor(-6, 6, R, 0);
		const bool bOrdered = Sim->Debug_DigTunnel(-6, FIntPoint(2, 0), FIntPoint(6, 0), TunnelId, Why);
		for (int32 K = 0; K < 3; ++K) { Step(); const FRHBuildingInstance* B = FindB(BorerId); if (B && !B->ActiveRecipe.IsNone()) { break; } }
		const FRHBuildingInstance* Borer = FindB(BorerId);
		Expect(bOrdered && Borer && !Borer->ActiveRecipe.IsNone() && Near(Borer->BatchRemaining_h + EraDt / 50.0, 3.45, 1.0e-9) || (bOrdered && Borer && Near(Borer->BatchRemaining_h + 2.0 * EraDt / 50.0, 3.45, 1.0e-9)),
			FString::Printf(TEXT("DP3 a tunnel front on -6 is scaled by the tunnel's level: 3.45 h (read %.9f; %s)"), Borer ? Borer->BatchRemaining_h : -1.0, *Why));
		// The control: a tunnel on -5 is exactly 3 h.
		Reset();
		const int32 Borer2 = Kit();
		Sim->ExtendShaft(6, Head0);
		Sim->ExcavateFloor(-5, 6, R, 0);
		const bool bOrdered5 = Sim->Debug_DigTunnel(-5, FIntPoint(2, 0), FIntPoint(6, 0), TunnelId, Why);
		Step();
		const FRHBuildingInstance* Borer5 = FindB(Borer2);
		Expect(bOrdered5 && Borer5 && !Borer5->ActiveRecipe.IsNone() && Near(Borer5->BatchRemaining_h + EraDt / 50.0, 3.0, 1.0e-9),
			FString::Printf(TEXT("DP3 a tunnel front on -5 is today's 3 h (read %.9f; %s)"), Borer5 ? Borer5->BatchRemaining_h + EraDt / 50.0 : -1.0, *Why));

		// Growth: a floor-homes project on -6 takes 1.15 x GrowthCellSols, on -5 exactly GrowthCellSols (the K10 growth world, floors -5 and -6).
		const FName NShield(TEXT("Shielding")), NHomes(TEXT("Homes"));
		const double CellSols = Sim->GetGrowthCellSols();
		Reset();
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetBigArrivalDone(true);
		Sim->AddStock(NStruct, 5000.0);
		Sim->AddStock(NShield, 3000.0);
		Sim->AddStock(NOx, 6000.0);
		Sim->AddStock(NWater, 150.0);
		Sim->AddStock(NFood, 6000.0);
		Sim->ExtendShaft(6, Head0);
		Sim->ExcavateFloor(-5, 4, R, 0);
		Sim->ExcavateFloor(-6, 4, R, 0);
		Place(TEXT("AirFilter"), Head0.X, Head0.Y + 500.0, -5);
		Place(TEXT("AirFilter"), Head0.X, Head0.Y + 500.0, -6);
		Step();
		Sim->Debug_ForceRateFloor(-5);
		Sim->Debug_ForceRateFloor(-6);
		for (int32 Cell = 0; Cell < 2; ++Cell) { Sim->DesignateRoom(-5, Cell, NLiving, R); Sim->DesignateRoom(-6, Cell, NLiving, R); } // four Living cells against eight beds (a bed is a carved cell on a rated floor)
		const auto PaintFloor = [&](int32 Level)
		{
			FRHCommand C; C.Verb = FName("PaintDistrict"); C.Target = NHomes; C.Level = Level;
			C.Location = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(0, 0));
			C.LocationB = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(0, 0));
			const int32 R0 = Rejects.Num();
			Send(C);
			Step();
			return Rejects.Num() == R0;
		};
		const bool bPainted = PaintFloor(-5) && PaintFloor(-6) && Sim->GetDistricts().Num() == 2;
		Expect(bPainted, TEXT("DP3 growth world: district #1 Homes over floor -5, #2 over floor -6"));
		const int32 Housed = Sim->Debug_AddColonists(8);
		Sim->Debug_StepPopulation(AgentDt);
		FString GrowthSaveErr;
		Expect(Housed == 8 && Sim->SaveColony(TEXT("depthgrowth"), GrowthSaveErr), FString::Printf(TEXT("DP3 growth world: eight housed against four Living cells, saved (%s)"), *GrowthSaveErr));
		const auto GrowthTotal = [&](int32 OnlyDistrict) -> double
		{
			FString E;
			if (!Sim->LoadColony(TEXT("depthgrowth"), E) || !E.IsEmpty()) { return -1.0; }
			Sim->Debug_SetGrowthEnabled(true);
			Sim->Debug_SetBigArrivalDone(true);
			Sim->Debug_SetHopeSmoothed(80.0);
			for (const FRHDistrict& D : TArray<FRHDistrict>(Sim->GetDistricts())) { Sim->SetDistrictGrowth(D.Id, D.Id == OnlyDistrict); }
			Sim->Debug_RunGrowthCheck();
			const FRHGrowthProject* P = Sim->FindGrowthProject(OnlyDistrict);
			return P && P->Kind == ERHGrowthKind::FloorCells ? P->TotalSols : -1.0;
		};
		const double Sols5 = GrowthTotal(1), Sols6 = GrowthTotal(2);
		Expect(Sols5 == CellSols && Near(Sols6, CellSols * 1150.0 / 1000.0, 1.0e-12),
			FString::Printf(TEXT("DP3 a floor-homes project on -5 takes %.4f sols (GrowthCellSols, unchanged) and on -6 %.4f (1.15 x %.2f)"), Sols5, Sols6, CellSols));
	}

	// --- DP4. The cooling load. ---
	{
		Reset();
		Sim->ExtendShaft(10, Head0);
		Sim->ExcavateFloor(-3, 100, R, 0);
		Step();
		const double Base = SumBuildingW();
		Expect(Near(Sim->GetPower().LoadW, Base, 1.0e-9) && Sim->GetPower().CoolingFloors == 0 && Sim->GetPower().CoolingW == 0.0,
			FString::Printf(TEXT("DP4 100 cells on -3 add 0 W: LoadW %.3f W is the %.3f W of buildings, no cooling floor"), Sim->GetPower().LoadW, Base));
		Sim->ExcavateFloor(-5, 100, R, 0);
		Step();
		Expect(Near(Sim->GetPower().LoadW, Base, 1.0e-9) && Sim->GetPower().CoolingFloors == 0, TEXT("DP4 and 100 cells on -5 (the cut-off itself) add 0 W"));
		Sim->ExcavateFloor(-6, 100, R, 0);
		Step();
		Expect(Near(Sim->GetPower().LoadW, Base + 100.0, 1.0e-9) && Sim->GetPower().CoolingFloors == 1 && Near(Sim->GetPower().CoolingW, 100.0, 1.0e-12) && Sim->GetFloorCoolingMilliW(-6) == 100000,
			FString::Printf(TEXT("DP4 100 cells on -6 add exactly 100 W (LoadW %.3f = %.3f + 100; %lld mW)"), Sim->GetPower().LoadW, Base, Sim->GetFloorCoolingMilliW(-6)));
		Sim->ExcavateFloor(-10, 100, R, 0);
		Step();
		Expect(Near(Sim->GetPower().LoadW, Base + 100.0 + 500.0, 1.0e-9) && Sim->GetPower().CoolingFloors == 2 && Near(Sim->GetPower().CoolingW, 600.0, 1.0e-12) && Sim->GetFloorCoolingMilliW(-10) == 500000,
			FString::Printf(TEXT("DP4 100 cells on -10 add exactly 500 W (5 floors past the line): LoadW %.3f, cooling %.3f W over %d floors"), Sim->GetPower().LoadW, Sim->GetPower().CoolingW, Sim->GetPower().CoolingFloors));
		Expect(Sim->GetFloorCoolingMilliW(-5) == 0 && Sim->GetFloorCoolingMilliW(0) == 0 && Sim->GetFloorCoolingMilliW(-15) == 0 && Sim->GetDeepCoolingFloorCount() == 2,
			TEXT("DP4 the cut-off floor, the surface and a floor with no cells carry no cooling entry"));
		// A tunnel's cells count like any carved cell (O3).
		int32 TunnelId = INDEX_NONE;
		Sim->ExcavateFloor(-8, 6, R, 0);
		const int64 Before = Sim->GetFloorCoolingMilliW(-8);
		const bool bTunnel = Sim->Debug_DigTunnel(-8, FIntPoint(2, 0), FIntPoint(6, 0), TunnelId, Why);
		const int32 Laid = bTunnel ? Sim->Debug_CarveTunnelCells(TunnelId, 64) : 0;
		Expect(bTunnel && Laid > 0 && Sim->GetFloorCoolingMilliW(-8) == (int64)(6 + Laid) * 3 * 1000 && Before == 6 * 3 * 1000,
			FString::Printf(TEXT("DP4 a tunnel's %d cells on -8 add cooling like any carved cell: %lld mW = (6 + %d) x 3 floors past the line x 1 W"), Laid, Sim->GetFloorCoolingMilliW(-8), Laid));
	}

	// --- DP5. The shed prefix on a scraping step: rule (i) and rule (ii). ---
	{
		struct FShed { int32 WaterId = 0, FilterId = 0; };
		const auto ShedWorld = [&](double ExtraW, double BankWh) -> FShed
		{
			Reset();
			FShed S;
			Sim->ExtendShaft(10, Head0);
			Sim->ExcavateFloor(-6, 60, R, 0);   // 60 W
			Sim->ExcavateFloor(-10, 30, R, 0);  // 150 W
			S.WaterId = Place(TEXT("WaterPlant"), 6000.0, -3000.0);
			S.FilterId = Place(TEXT("AirFilter"), 6000.0, 3000.0);
			Sim->Debug_SetExtraGenW(ExtraW);
			Sim->Debug_SetBatteryWh(BankWh);
			return S;
		};
		// Full load: Lander 20 + WaterPlant 15 + AirFilter 10 + cooling -6 60 + cooling -10 150 = 255 W against 50 W + the extra.
		{
			const FShed S = ShedWorld(25.0, 0.0); // generation 75 W; the walk: Lander, WaterPlant, -10 (Rem 70 <= 75 stops)
			EraRaw();
			const FRHBuildingInstance* Water = FindB(S.WaterId);
			const FRHBuildingInstance* Filter = FindB(S.FilterId);
			const FRHPowerState& P = Sim->GetPower();
			Expect(Water && !Water->bPowered && Filter && Filter->bPowered && P.ShedCount == 3 && Near(P.LoadW, 70.0, 1.0e-9)
				&& Near(Sim->GetFloorWarmth(-10), EraDt / 1200.0, 1.0e-12) && Sim->GetFloorWarmth(-6) == 0.0,
				FString::Printf(TEXT("DP5 a step that starts with an empty bank: the short prefix is Lander, WaterPlant (before any cooling), then -10 (before -6); the AirFilter is left out. Shed %d, LoadW %.1f, Warmth -10 %.4f, -6 %.4f"), P.ShedCount, P.LoadW, Sim->GetFloorWarmth(-10), Sim->GetFloorWarmth(-6)));
		}
		{
			const FShed S = ShedWorld(170.0, 0.0); // generation 220 W: shedding the Lander and the WaterPlant already fits - no cooling is cut
			EraRaw();
			const FRHBuildingInstance* Water = FindB(S.WaterId);
			Expect(Water && !Water->bPowered && Sim->GetPower().ShedCount == 2 && Sim->GetFloorWarmth(-10) == 0.0 && Sim->GetFloorWarmth(-6) == 0.0,
				FString::Printf(TEXT("DP5 with 220 W the WaterPlant sheds before any cooling does: shed %d, no floor warms (-10 %.4f, -6 %.4f)"), Sim->GetPower().ShedCount, Sim->GetFloorWarmth(-10), Sim->GetFloorWarmth(-6)));
		}
		{
			const FShed S = ShedWorld(25.0, 10.0); // a bank of 10 Wh: less than one era step's deficit (216 Wh), so rule (ii) scrapes and rule (i) does not
			EraRaw();
			const FRHBuildingInstance* Water = FindB(S.WaterId);
			const FRHPowerState& P = Sim->GetPower();
			bool bAllOn = true;
			for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { bAllOn &= Bd.bPowered; }
			Expect(bAllOn && Water && P.ShedCount == 0 && Near(P.LoadW, 255.0, 1.0e-9) && Near(Sim->GetFloorWarmth(-10), EraDt / 1200.0, 1.0e-12) && Sim->GetFloorWarmth(-6) == 0.0 && Near(P.BatteryWh, 0.0, 1.0e-9),
				FString::Printf(TEXT("DP5 rule (ii): the bank holds 10 Wh, under one step's deficit - nothing is shed (shed %d, LoadW %.1f) and the walk still marks -10 short (Warmth %.4f, -6 %.4f); the bank clamps to %.3f"), P.ShedCount, P.LoadW, Sim->GetFloorWarmth(-10), Sim->GetFloorWarmth(-6), P.BatteryWh));
		}
		{
			const FShed S = ShedWorld(25.0, 1.0e6); // a healthy bank: no step scrapes, no floor warms
			(void)S;
			for (int32 K = 0; K < 5; ++K) { EraRaw(); }
			Expect(Sim->GetPower().ShedCount == 0 && Sim->GetFloorWarmth(-10) == 0.0 && Sim->GetFloorWarmth(-6) == 0.0 && !Sim->IsAnyFloorWarm(),
				TEXT("DP5 a healthy bank scrapes nothing: five steps, no floor warms"));
		}
		Sim->Debug_SetExtraGenW(0.0);
	}

	// --- DP6. Warmth through the real grid: a Lander-only night, recovery by generation. ---
	{
		struct FLeg { double Warm1Sol = 0, WarmAfter = 0, Min = 1, WarmRecovered = 1, WarmPenalty = 1; int32 OnAlerts = 0, OffAlerts = 0; bool bStrict = true; };
		const auto RunLeg = [&](bool bEra) -> FLeg
		{
			FLeg L;
			Reset();
			Sim->ExtendShaft(10, Head0);
			Sim->ExcavateFloor(-10, 40, R, 0); // 40 cells x 5 floors past the line x 1 W = 200 W against the Lander's 50 W
			Sim->Debug_SetBatteryWh(0.0);
			const int32 SolSteps = bEra ? (int32)(SolLen / EraDt) : (int32)(SolLen / (double)AgentDt);
			double Prev = 0.0;
			for (int32 K = 0; K < SolSteps; ++K)
			{
				if (bEra) { EraRaw(); } else { AgentRaw(); }
				const double W = Sim->GetFloorWarmth(-10);
				L.bStrict &= W > Prev || W >= 1.0; // every step scrapes (r = 50 / 170 < 1): Warmth rises at every step until the clamp
				Prev = W;
			}
			L.Warm1Sol = Sim->GetFloorWarmth(-10);
			if (bEra) { EraRaw(); } else { AgentRaw(); }
			L.WarmAfter = Sim->GetFloorWarmth(-10); // one step after the sol: the clamp has engaged in both bands
			L.OnAlerts = CountAlerts(TEXT("running warm"), -10);
			Sim->Debug_SetExtraGenW(250.0); // recovery by generation: 300 W against a full load of 220 W - no step scrapes
			const int32 HalfSteps = bEra ? (int32)(0.5 * SolLen / EraDt) : (int32)(0.5 * SolLen / (double)AgentDt);
			for (int32 K = 0; K < HalfSteps; ++K)
			{
				if (bEra) { EraRaw(); } else { AgentRaw(); }
				L.Min = FMath::Min(L.Min, Sim->GetFloorWarmth(-10));
			}
			if (bEra) { EraRaw(); } else { AgentRaw(); }
			L.WarmRecovered = Sim->GetFloorWarmth(-10);
			L.OffAlerts = CountAlerts(TEXT("comfortable again"), -10);
			const URHSimWorldSubsystem::FRHHopeBreakdown Hp = Sim->GetColonyHope();
			L.WarmPenalty = Hp.WarmPenalty;
			Sim->Debug_SetExtraGenW(0.0);
			return L;
		};
		const FLeg Ag = RunLeg(false);
		const FLeg Er = RunLeg(true);
		Expect(Ag.bStrict && Er.bStrict, TEXT("DP6 on a Lander-only night against a 200 W floor every step scrapes: Warmth rises at every step of the sol in both bands (the shed flag would alternate)"));
		Expect(Ag.WarmAfter == 1.0 && Near(Er.WarmAfter, 1.0, 1.0e-12) && Ag.Warm1Sol >= 0.99 && Er.Warm1Sol >= 0.99,
			FString::Printf(TEXT("DP6 after one sol Warmth is 1.0 (agent %.9f, era %.9f) and exactly 1.0 one step after it (agent %.17g, era %.17g)"), Ag.Warm1Sol, Er.Warm1Sol, Ag.WarmAfter, Er.WarmAfter));
		Expect(Ag.OnAlerts == 1 && Er.OnAlerts == 1, FString::Printf(TEXT("DP6 the 0.25 alert fires once in each band (agent %d, era %d)"), Ag.OnAlerts, Er.OnAlerts));
		Expect(Ag.WarmRecovered == 0.0 && Er.WarmRecovered == 0.0 && Ag.OffAlerts == 1 && Er.OffAlerts == 1,
			FString::Printf(TEXT("DP6 recovered by generation (Debug_SetExtraGenW(250)): Warmth is exactly 0 one step after half a sol (agent %.3g, era %.3g) and the comfortable-again alert fired once (%d, %d)"), Ag.WarmRecovered, Er.WarmRecovered, Ag.OffAlerts, Er.OffAlerts));
		Expect(Ag.WarmPenalty == 0.0 && Er.WarmPenalty == 0.0 && !FMath::IsNegativeOrNegativeZero(Ag.WarmPenalty) && !FMath::IsNegativeOrNegativeZero(Er.WarmPenalty),
			TEXT("DP6 WarmPenalty is +0.0 again once the floor is comfortable"));
	}

	// --- DP7. No harm: twins on one start, with a real-grid shortage. Warmth costs Hope and nothing else directly. ---
	{
		const int32 Cells = 130, Living = 20;
		struct FHarm
		{
			bool bRated = false; double O2 = 0; int32 Beds = 0; int32 Pop = 0; int32 Recalled = 0; TArray<double> Acc; TArray<int32> Onset;
			double Warmth = 0, HopeSmoothed = 0, WarmPenalty = 0, TotalAfter = 0, WarmAfterRecovery = 1, PenaltyAfterRecovery = 1;
			int32 FreeBeds = 0; int32 Districts = 0; bool bLab = false; double Food = 0, Water = 0, Oxygen = 0; int32 Band = 0;
			int32 DeepResidents = 0; double ExpectedPenalty = 0;
			TArray<FString> AlertTexts;
		};
		const auto RunTwin = [&](bool bWarmCosts) -> FHarm
		{
			FHarm H;
			const int32 Housed = Populated(Cells, Living);
			(void)Housed;
			Sim->Debug_SetHopeWarmMax(bWarmCosts ? HopeWarmRow : 0.0);
			Sim->Debug_SetExtraGenW(250.0);   // generation 300 W against a full load of 680 W: cooling is short, the floor's AirFilter stays served
			Sim->Debug_SetBatteryWh(0.0);
			H.FreeBeds = Sim->GetFreeHousing();
			H.Districts = Sim->GetDistricts().Num();
			for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { H.bLab |= Bd.DefName == FName("LabModule"); }
			const int32 Steps = (int32)(2.0 * SolLen / EraDt);
			for (int32 K = 0; K < Steps; ++K) { EraRaw(); }
			H.Warmth = Sim->GetFloorWarmth(-10);
			H.bRated = Sim->IsFloorRated(-10);
			H.O2 = Sim->GetFloorO2Kg(-10);
			H.Beds = Sim->GetFloorBeds(-10);
			H.Pop = Sim->GetPopulation();
			H.Recalled = Sim->GetRecalledMarsBorn().Num();
			for (const URHSimWorldSubsystem::FRHHomeRation& Home : Sim->GetHomeRations()) { H.Acc.Add(Sim->GetHomeDepartureAcc(Home.Key)); H.Onset.Add(Sim->GetHomeOnsetResidents(Home.Key)); H.Acc.Add(Home.Fraction); }
			H.HopeSmoothed = Sim->GetHopeSmoothed();
			H.Band = (int32)Sim->GetHopeBand();
			const URHSimWorldSubsystem::FRHHopeBreakdown Hp = Sim->GetColonyHope();
			H.WarmPenalty = Hp.WarmPenalty;
			H.DeepResidents = Hp.DeepResidents;
			H.Food = Sim->GetStock(NFood); H.Water = Sim->GetStock(NWater); H.Oxygen = Sim->GetStock(NOx);
			// Recovery by generation: a source far above the full load; half a sol and one step later the floor is comfortable.
			Sim->Debug_SetExtraGenW(5000.0);
			const int32 Half = (int32)(0.5 * SolLen / EraDt);
			for (int32 K = 0; K < Half + 1; ++K) { EraRaw(); }
			H.WarmAfterRecovery = Sim->GetFloorWarmth(-10);
			const URHSimWorldSubsystem::FRHHopeBreakdown After = Sim->GetColonyHope();
			H.PenaltyAfterRecovery = After.WarmPenalty;
			H.TotalAfter = After.Total;
			H.AlertTexts = Alerts;
			Sim->Debug_SetExtraGenW(0.0);
			Sim->Debug_SetHopeWarmMax(HopeWarmRow);
			return H;
		};
		const FHarm A = RunTwin(true);
		const FHarm B = RunTwin(false);
		Expect(A.Pop == Cells && A.FreeBeds == 0 && A.Districts == 0 && !A.bLab && B.FreeBeds == 0,
			FString::Printf(TEXT("DP7 the fixture: %d residents on %d cells of rated -10 (a bed a cell), no free bed (%d), no zoned district (%d), no lab: no births, settlers or Big Arrival can differ, and city growth has nothing to wait on"), A.Pop, Cells, A.FreeBeds, A.Districts));
		Expect(A.Food > 1.0e6 && A.Water > 1.0e6 && A.Oxygen > 1.0e6 && B.Food > 1.0e6 && B.Water > 1.0e6 && B.Oxygen > 1.0e6,
			FString::Printf(TEXT("DP7 food (%.0f), water (%.0f) and oxygen (%.0f kg) stay deep, so a work-tempo gap cannot reach the floor's air, support or the departure accumulators"), A.Food, A.Water, A.Oxygen));
		Expect(A.HopeSmoothed > Sim->GetDisputeHopeBelow() && B.HopeSmoothed > Sim->GetDisputeHopeBelow() && A.Band != (int32)URHSimWorldSubsystem::ERHHopeBand::Failing && B.Band != (int32)URHSimWorldSubsystem::ERHHopeBand::Failing,
			FString::Printf(TEXT("DP7 HopeSmoothed stays above DisputeHopeBelow (%.1f) and out of the Failing band in both twins (A %.3f, B %.3f)"), Sim->GetDisputeHopeBelow(), A.HopeSmoothed, B.HopeSmoothed));
		Expect(A.Warmth == 1.0 && B.Warmth == 1.0, FString::Printf(TEXT("DP7 both twins stand at Warmth 1.0 after two sols of a real-grid shortage (A %.6f, B %.6f); HopeWarmMax only scales Hope"), A.Warmth, B.Warmth));
		Expect(A.bRated && B.bRated && A.O2 == B.O2 && A.Beds == B.Beds && A.Pop == B.Pop && A.Recalled == B.Recalled && A.Recalled == 0,
			FString::Printf(TEXT("DP7 the warm floor stays rated and its air, beds, headcount and recall list equal the twin's (rated %d/%d, O2 %.6f/%.6f kg, %d/%d beds, %d/%d people, %d/%d recalled)"), A.bRated, B.bRated, A.O2, B.O2, A.Beds, B.Beds, A.Pop, B.Pop, A.Recalled, B.Recalled));
		Expect(A.Acc == B.Acc && A.Onset == B.Onset && A.Acc.Num() > 0, FString::Printf(TEXT("DP7 every home's ration and departure accumulator equal the twin's (%d homes)"), A.Onset.Num()));
		const double Want = HopeWarmRow * 1.0; // every resident is homed on -10, so the share is 1
		Expect(A.DeepResidents == A.Pop && Near(A.WarmPenalty, Want, 1.0e-9) && B.WarmPenalty == 0.0 && A.HopeSmoothed < B.HopeSmoothed,
			FString::Printf(TEXT("DP7 A's penalty is HopeWarmMax x share = %.3f x 1 = %.4f; B's is 0; A's Hope is lower (%.4f < %.4f)"), HopeWarmRow, A.WarmPenalty, A.HopeSmoothed, B.HopeSmoothed));
		Expect(A.WarmAfterRecovery == 0.0 && A.PenaltyAfterRecovery == 0.0 && !FMath::IsNegativeOrNegativeZero(A.PenaltyAfterRecovery) && A.TotalAfter == B.TotalAfter,
			FString::Printf(TEXT("DP7 after recovery Warmth is 0, the penalty +0.0 and A's Total equals the twin's bit for bit (%.17g, %.17g)"), A.TotalAfter, B.TotalAfter));
		// A partial floor: the penalty is weighted by who lives where (Debug_SetFloorWarmth: a hand fixture for the Hope read only).
		{
			Populated(Cells, Living);
			const int32 Pop = Sim->GetPopulation();
			int32 Moved = 0;
			for (const FRHColonist& C : TArray<FRHColonist>(Sim->GetColonists())) { if (Moved < Pop / 4) { Sim->Debug_SetColonistHome(C.Id, 0, -1); ++Moved; } }
			Sim->Debug_SetFloorWarmth(-10, 0.4);
			const URHSimWorldSubsystem::FRHHopeBreakdown Hp = Sim->GetColonyHope();
			const double Share = 0.4 * (double)(Pop - Moved) / (double)Pop;
			Expect(Moved > 0 && Near(Hp.WarmPenalty, HopeWarmRow * Share, 1.0e-12), FString::Printf(TEXT("DP7 WarmPenalty is HopeWarmMax x sum(residents x Warmth) / people = %.1f x 0.4 x %d / %d = %.6f (read %.6f)"), HopeWarmRow, Pop - Moved, Pop, HopeWarmRow * Share, Hp.WarmPenalty));
			Sim->Debug_SetFloorWarmth(-10, 0.0);
		}
		// The alert copy passes the Gate-D banned-word check (prevention-framed: no harm imagery).
		{
			const TCHAR* Banned[] = { TEXT("die"), TEXT("death"), TEXT("dead"), TEXT("kill"), TEXT("suffer"), TEXT("cook"), TEXT("burn"), TEXT("blood"), TEXT("corpse"), TEXT("agony"), TEXT("suffocat"), TEXT("collapse"), TEXT("doom") };
			bool bClean = true;
			int32 Seen = 0;
			for (const FString& Text : A.AlertTexts)
			{
				if (!Text.Contains(TEXT("running warm")) && !Text.Contains(TEXT("comfortable again"))) { continue; }
				++Seen;
				for (const TCHAR* W : Banned) { bClean &= !Text.ToLower().Contains(W); }
			}
			const FString On = FString::Printf(TEXT("Floor %d is running warm: its cooling needs power. More generation or storage brings it back to comfort."), -8);
			const FString Off = FString::Printf(TEXT("Floor %d is comfortable again."), -8);
			for (const TCHAR* W : Banned) { bClean &= !On.ToLower().Contains(W) && !Off.ToLower().Contains(W); }
			Expect(bClean && Seen > 0, FString::Printf(TEXT("DP7 the warm and comfortable alerts (%d seen) pass the banned-word check: prevention-framed, no harm imagery"), Seen));
		}
	}

	// --- DP8. Shelter: a capped, bonus-only Hope term for people homed past the line. ---
	{
		Reset();
		Sim->AddStock(NOx, 2.0e6); Sim->AddStock(NWater, 2.0e6); Sim->AddStock(NFood, 2.0e6);
		Sim->ExtendShaft(6, Head0);
		Sim->ExcavateFloor(-1, 20, R, 0);
		Place(TEXT("AirFilter"), Head0.X, Head0.Y + 500.0, -1);
		Step();
		Sim->Debug_ForceRateFloor(-1);
		for (int32 I = 0; I < 20; ++I) { Sim->DesignateRoom(-1, I, NLiving, R); }
		Sim->Debug_SetColonistsPerCell(2.0); // two beds a cell: 20 cells house 40
		Step();
		const int32 Housed = Sim->Debug_AddColonists(Sim->GetHousingCapacity());
		Sim->ExcavateFloor(-5, 4, R, 0);
		Sim->ExcavateFloor(-6, 4, R, 0);
		Step();
		const URHSimWorldSubsystem::FRHHopeBreakdown Flat = Sim->GetColonyHope();
		Expect(Housed == 40 && Flat.Shelter == 0.0 && !FMath::IsNegativeOrNegativeZero(Flat.Shelter) && Flat.DeepResidents == 0,
			FString::Printf(TEXT("DP8 40 residents all on -1: Shelter is +0.0 (%d housed, %d deep)"), Housed, Flat.DeepResidents));
		TArray<int32> Ids;
		for (const FRHColonist& C : Sim->GetColonists()) { Ids.Add(C.Id); }
		while (Ids.Num() < 40) { Ids.Add(0); } // a short roster fails the Housed check above; id 0 is no colonist, so nothing below can index past the end
		for (int32 K = 0; K < 10; ++K) { Sim->Debug_SetColonistHome(Ids[K], 0, -5); }
		const URHSimWorldSubsystem::FRHHopeBreakdown OnLine = Sim->GetColonyHope();
		Expect(OnLine.Shelter == 0.0 && !FMath::IsNegativeOrNegativeZero(OnLine.Shelter) && OnLine.DeepResidents == 0 && OnLine.Total == Flat.Total,
			FString::Printf(TEXT("DP8 10 of 40 homed on -5 (the cut-off itself): Shelter is +0.0 and Total is bit-identical (%.17g vs %.17g)"), OnLine.Total, Flat.Total));
		for (int32 K = 0; K < 10; ++K) { Sim->Debug_SetColonistHome(Ids[K], 0, -6); }
		const URHSimWorldSubsystem::FRHHopeBreakdown Deep = Sim->GetColonyHope();
		Expect(Deep.DeepResidents == 10 && Near(Deep.Shelter, Sim->GetHopeShelterMax() * 0.25, 1.0e-12) && Near(Deep.Total - Flat.Total, Deep.Shelter, 1.0e-9),
			FString::Printf(TEXT("DP8 10 of 40 homed on -6: Shelter = HopeShelterMax x 10 / 40 = %.3f x 0.25 = %.4f, and Total rises by exactly that (%.6f)"), Sim->GetHopeShelterMax(), Deep.Shelter, Deep.Total - Flat.Total));
		for (int32 K = 0; K < 40; ++K) { Sim->Debug_SetColonistHome(Ids[K], 0, -6); }
		const URHSimWorldSubsystem::FRHHopeBreakdown All = Sim->GetColonyHope();
		Expect(All.DeepResidents == 40 && Near(All.Shelter, Sim->GetHopeShelterMax(), 1.0e-12), FString::Printf(TEXT("DP8 all 40 deep: Shelter is the cap, %.3f"), All.Shelter));
		for (int32 K = 0; K < 40; ++K) { Sim->Debug_SetColonistHome(Ids[K], 0, -1); }
		Sim->Debug_SetColonistsPerCell(1.0); // the row's own value
	}

	// --- DP9. Era parity: a paired 10-sol run, an agent leg and an era leg of one colony (the fix-A method). ---
	// A populated deep floor on -10 (100 cells: 500 W of cooling), the Lander's 2,000 Wh bank and six solar arrays: every night the
	// bank runs dry and the floor warms; every day the arrays refill it and the floor cools. The tolerance comes from the rows: one era
	// step at the faster rate, 60 / (HeatFallSols x 1200) = 0.1. It holds only because the fixture resets Warmth to the 0 clamp each
	// sol and the dusk onsets agree within two era steps - so both are asserted, not assumed.
	{
		const int32 Sols = 10;
		const auto AdvanceTo = [&](double Frac)
		{
			const double Now = Clock->GetSimSecondsTotal();
			const double Sols0 = FMath::FloorToDouble(Now / SolLen);
			const double Target = Frac * SolLen;
			const double T = (Sols0 + (Target < Now - Sols0 * SolLen ? 1.0 : 0.0)) * SolLen + Target;
			if (T > Now) { Clock->Debug_AdvanceSimSeconds(T - Now); }
		};
		const int32 Housed = Populated(100, 20);
		for (int32 K = 0; K < 6; ++K) { Place(TEXT("SolarArray"), 3500.0, 1000.0 + 1000.0 * K); }
		Step();
		Sim->Debug_SetBatteryWh(1.0e9); // the cap: the Lander's 2,000 Wh
		AdvanceTo(0.5);
		FString ParErr;
		const bool bParSaved = Sim->SaveColony(TEXT("depthpar"), ParErr);
		Expect(Housed == 100 && bParSaved, FString::Printf(TEXT("DP9 the paired colony (%d residents on -10, 500 W of cooling, six arrays) is saved for both legs (%s)"), Housed, *ParErr));
		struct FPar { TArray<double> Read, ReadHope, Peak, Onset; TArray<int32> Reset; double HopeEnd = 0; };
		const auto RunPar = [&](bool bEra) -> FPar
		{
			FPar L;
			L.Read.Init(-1.0, Sols); L.ReadHope.Init(-1.0, Sols); L.Peak.Init(0.0, Sols); L.Onset.Init(-1.0, Sols); L.Reset.Init(0, Sols);
			FString E;
			Sim->LoadColony(TEXT("depthpar"), E);
			Sim->Debug_SetExtraGenW(0.0);
			Sim->Debug_SetHopeWarmMax(HopeWarmRow);
			const double T0 = Clock->GetSimSecondsTotal();
			const int32 K0 = FMath::FloorToInt32(T0 / SolLen);
			const double End = (double)(K0 + Sols - 1) * SolLen + 0.95 * SolLen + 1.0;
			TArray<bool> bReadDone;
			bReadDone.Init(false, Sols);
			while (Clock->GetSimSecondsTotal() < End)
			{
				if (bEra) { EraRaw(); } else { AgentRaw(); }
				const double T = Clock->GetSimSecondsTotal();
				const double W = Sim->GetFloorWarmth(-10);
				const int32 K = FMath::FloorToInt32(T / SolLen) - K0;
				if (K >= 0 && K < Sols)
				{
					const double Off = T - (double)(K0 + K) * SolLen;
					if (Off <= 0.95 * SolLen + 1.0e-6 && W == 0.0) { L.Reset[K] = 1; }
					if (Off >= 0.95 * SolLen - 1.0e-6 && !bReadDone[K]) { bReadDone[K] = true; L.Read[K] = W; L.ReadHope[K] = Sim->GetColonyHope().Total; }
					if (Off >= 0.5 * SolLen && L.Onset[K] < 0.0 && W > 0.0) { L.Onset[K] = Off; }
				}
				const double TN = T - 0.75 * SolLen; // the night of sol K: dusk (0.75) to the next dawn (1.25)
				const int32 KN = FMath::FloorToInt32(TN / SolLen) - K0;
				const double OffN = TN - (double)(K0 + KN) * SolLen;
				if (KN >= 0 && KN < Sols && OffN < 0.5 * SolLen) { L.Peak[KN] = FMath::Max(L.Peak[KN], W); }
			}
			L.HopeEnd = Sim->GetHopeSmoothed();
			return L;
		};
		const FPar Ag = RunPar(false);
		const FPar Er = RunPar(true);
		const double Tol = EraDt / (Sim->GetHeatFallSols() * SolLen);
		Expect(Near(Tol, 0.1, 1.0e-12), FString::Printf(TEXT("DP9 the tolerance comes from the rows: 60 / (HeatFallSols x 1200) = %.4f"), Tol));
		bool bReset = true, bPeak = true, bOnset = true, bRead = true, bHope = true, bFilled = true;
		double WorstRead = 0.0, WorstOnset = 0.0, WorstHope = 0.0;
		for (int32 K = 1; K < Sols - 1; ++K)
		{
			bReset &= Ag.Reset[K] == 1 && Er.Reset[K] == 1;
			bPeak &= Ag.Peak[K] >= 0.25 && Er.Peak[K] >= 0.25;
			bFilled &= Ag.Onset[K] > 0.0 && Er.Onset[K] > 0.0 && Ag.Read[K] >= 0.0 && Er.Read[K] >= 0.0;
			if (bFilled)
			{
				WorstRead = FMath::Max(WorstRead, FMath::Abs(Ag.Read[K] - Er.Read[K]));
				WorstOnset = FMath::Max(WorstOnset, FMath::Abs(Ag.Onset[K] - Er.Onset[K]));
				WorstHope = FMath::Max(WorstHope, FMath::Abs(Ag.ReadHope[K] - Er.ReadHope[K]) / FMath::Max(1.0, FMath::Max(Ag.ReadHope[K], Er.ReadHope[K])));
			}
			UE_LOG(LogRedHopeSim, Display, TEXT("DEPTH DP9 sol %d: dusk onset (seconds into the sol) agent %.1f, era %.1f | Warmth at 0.95 agent %.4f, era %.4f | night peak agent %.4f, era %.4f | Hope at 0.95 agent %.4f, era %.4f"),
				K, Ag.Onset[K], Er.Onset[K], Ag.Read[K], Er.Read[K], Ag.Peak[K], Er.Peak[K], Ag.ReadHope[K], Er.ReadHope[K]);
		}
		bOnset = WorstOnset <= 2.0 * EraDt + 1.0e-6;
		bRead = WorstRead <= Tol + 1.0e-9;
		bHope = WorstHope <= 0.05;
		Expect(bFilled, TEXT("DP9 every sol's dusk onset and 0.95 read were observed in both bands"));
		Expect(bReset, TEXT("DP9 (a) in both bands Warmth reaches 0 at some step of every sol before the 0.95 read (the daily reset)"));
		Expect(bPeak, TEXT("DP9 the night is real, not vacuous: Warmth reaches at least 0.25 every night in both bands"));
		Expect(bOnset, FString::Printf(TEXT("DP9 (b) each night's first scraping step falls within 120 s in the two bands (worst %.1f s; the window is two era steps)"), WorstOnset));
		Expect(bRead, FString::Printf(TEXT("DP9 Warmth at sol fraction 0.95 agrees within %.3f (worst %.4f over nine sols; the read is the first step end at or after 1140 s, a window of up to 60 s)"), Tol, WorstRead));
		Expect(bHope, FString::Printf(TEXT("DP9 Hope agrees within 5%% at every read (worst %.4f)"), WorstHope));
	}

	// --- DP10. The limits. ---
	{
		Reset();
		const int32 BorerId = Kit();
		(void)BorerId;
		FRHCommand Bore; Bore.Verb = FName("Bore"); Bore.Value = 25;
		Send(Bore);
		Expect(Sim->GetBoreTargetDepth() == 20 && Sim->GetMaxDepth() == 20, FString::Printf(TEXT("DP10 an order to bore 25 floors clamps to the target -%d"), Sim->GetBoreTargetDepth()));
		Reset();
		Sim->ExtendShaft(20, Head0);
		Expect(Sim->GetShaftDepth() == 20 && Sim->IsLevelConnected(-20) && !Sim->IsLevelConnected(-21), TEXT("DP10 ExtendShaft(20) reaches -20 and no further"));
		Rejects.Reset();
		FRHCommand Exc; Exc.Verb = FName("Excavate"); Exc.Level = -21; Exc.Value = 3;
		Send(Exc);
		Expect(Rejects.Num() > 0 && !Sim->ExcavateFloor(-21, 3, R, 0), FString::Printf(TEXT("DP10 an Excavate order on -21 is refused (%s)"), Rejects.Num() > 0 ? *Rejects.Last() : TEXT("no rejection")));
		FString PlaceWhy;
		Expect(!Sim->CanPlaceBuilding(FName("AirFilter"), Head0, PlaceWhy, -21) && PlaceWhy.Contains(TEXT("No such floor")), FString::Printf(TEXT("DP10 a building on -21 reads \"No such floor\" (%s)"), *PlaceWhy));
		Expect(Sim->ExcavateFloor(-20, 6, R, 0) && Sim->GetFloorCarvedCells(-20) == 6, TEXT("DP10 -20 itself carves"));
		// A floor table with -20 loads; one with -21 is refused with the range.
		Expect(Sim->SaveColony(TEXT("depth20"), SaveErr), FString::Printf(TEXT("DP10 saved a colony with six cells on -20 (%s)"), *SaveErr));
		TArray<uint8> Good;
		const bool bRead = FFileHelper::LoadFileToArray(Good, *SaveFilePath(TEXT("depth20"))) && Good.Num() > 80;
		int32 FloorCount = 0, Level0 = 0;
		if (bRead) { FMemory::Memcpy(&FloorCount, Good.GetData() + 20, 4); FMemory::Memcpy(&Level0, Good.GetData() + 28, 4); }
		Expect(bRead && FloorCount == 1 && Level0 == -20, FString::Printf(TEXT("DP10 the file's one floor is level %d"), Level0));
		FString E1, E2;
		Expect(Sim->LoadColony(TEXT("depth20"), E1) && E1.IsEmpty() && Sim->GetFloorCarvedCells(-20) == 6, FString::Printf(TEXT("DP10 a floor table with -20 loads (%s)"), *E1));
		TArray<uint8> Deeper = Good;
		const int32 Minus21 = -21;
		FMemory::Memcpy(Deeper.GetData() + 28, &Minus21, 4);
		FFileHelper::SaveArrayToFile(Deeper, *SaveFilePath(TEXT("depth21")));
		const int32 CellsA = Sim->GetFloorCarvedCells(-20), DepthA = Sim->GetShaftDepth();
		const bool bLoaded = Sim->LoadColony(TEXT("depth21"), E2);
		Expect(!bLoaded && E2.Contains(TEXT("floor table")) && E2.Contains(TEXT("-1..-20")) && Sim->GetFloorCarvedCells(-20) == CellsA && Sim->GetShaftDepth() == DepthA,
			FString::Printf(TEXT("DP10 a floor table with -21 is refused, naming the range, and the colony is untouched (%s)"), *E2));
	}

	// --- DP11. The save: the warmth block. ---
	{
		Reset();
		Sim->ExtendShaft(10, Head0);
		Sim->ExcavateFloor(-9, 10, R, 0);
		Sim->ExcavateFloor(-10, 10, R, 0);
		Sim->ExcavateFloor(-3, 4, R, 0);
		Expect(Sim->SaveColony(TEXT("depthw0"), SaveErr), FString::Printf(TEXT("DP11 saved with no warm floor (%s)"), *SaveErr));
		Sim->Debug_SetFloorWarmth(-9, 0.25);
		Sim->Debug_SetFloorWarmth(-10, 0.75);
		Expect(Sim->SaveColony(TEXT("depthw2"), SaveErr), FString::Printf(TEXT("DP11 saved with -9 at 0.25 and -10 at 0.75 (%s)"), *SaveErr));
		TArray<uint8> Z, W;
		const bool bRead = FFileHelper::LoadFileToArray(Z, *SaveFilePath(TEXT("depthw0"))) && FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("depthw2")));
		uint32 Ver = 0;
		if (bRead) { FMemory::Memcpy(&Ver, W.GetData() + 4, 4); }
		int32 At = INDEX_NONE;
		for (int32 I = 0; bRead && I < FMath::Min(Z.Num(), W.Num()) && At == INDEX_NONE; ++I) { if (Z[I] != W[I]) { At = I; } }
		const auto Rd = [&](const TArray<uint8>& Bytes, int32 Off) { int32 V = INDEX_NONE; if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(&V, Bytes.GetData() + Off, 4); } return V; };
		Expect(bRead && Ver == 53 && At != INDEX_NONE && Rd(Z, At) == 0 && Rd(W, At) == 2 && W.Num() == Z.Num() + 32,
			FString::Printf(TEXT("DP11 the file says version %u; the warmth block sits at byte %d: a default colony writes 0 (4 bytes), two warm floors write 2 then 16 bytes each (%d -> %d bytes)"), Ver, At, Z.Num(), W.Num()));
		if (!bRead || At == INDEX_NONE) { return Finish(); }
		Expect(Rd(W, At + 4) == 0 && Rd(W, At + 8) == -9 && Rd(W, At + 20) == 0 && Rd(W, At + 24) == -10,
			TEXT("DP11 per floor: RegionId 0, Level, Warmth; -9 before -10 (shallowest first)"));
		// Round trip.
		FString E;
		const bool bLoaded = Sim->LoadColony(TEXT("depthw2"), E);
		Expect(bLoaded && E.IsEmpty() && Sim->GetFloorWarmth(-9) == 0.25 && Sim->GetFloorWarmth(-10) == 0.75 && !Sim->IsAnyFloorWarm() == false, FString::Printf(TEXT("DP11 the warmth block loads back exactly (%s)"), *E));
		Expect(Sim->SaveColony(TEXT("depthw2b"), SaveErr), TEXT("DP11 saved again"));
		TArray<uint8> W2;
		FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("depthw2b")));
		const bool bBlockEqual = W2.Num() == W.Num() && FMemory::Memcmp(W2.GetData() + At, W.GetData() + At, 36) == 0;
		Expect(bBlockEqual, FString::Printf(TEXT("DP11 the warmth block round-trips byte-equal (%d bytes; whole files %s)"), 36, W2 == W ? TEXT("equal too") : TEXT("differ elsewhere (runtime stamps), the block is the claim")));
		// Hand-made files: every structural refusal, the colony untouched. Warm floors in the file: -9, -10; floors loaded: -3, -9, -10.
		const auto TryFile = [&](const TCHAR* Slot, const TArray<uint8>& Bytes, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Bytes, *SaveFilePath(Slot));
			const double W9 = Sim->GetFloorWarmth(-9);
			const int32 Pop = Sim->GetPopulation(), Cells = Sim->GetFloorCarvedCells(-10), Builds = Sim->GetBuildings().Num();
			const bool bOk = Sim->LoadColony(Slot, OutErr);
			const bool bSame = Sim->GetFloorWarmth(-9) == W9 && Sim->GetPopulation() == Pop && Sim->GetFloorCarvedCells(-10) == Cells && Sim->GetBuildings().Num() == Builds;
			return !bOk && bSame && OutErr.Contains(TEXT("Save warmth block invalid"));
		};
		const auto PutI = [&](TArray<uint8>& B, int32 Off, int32 V) { FMemory::Memcpy(B.GetData() + Off, &V, 4); };
		const auto PutD = [&](TArray<uint8>& B, int32 Off, double V) { FMemory::Memcpy(B.GetData() + Off, &V, 8); };
		struct FCase { const TCHAR* Slot; const TCHAR* Name; const TCHAR* Needle; };
		FString Err;
		{ TArray<uint8> T = W; PutI(T, At, -1); Expect(TryFile(TEXT("depthr1"), T, Err) && Err.Contains(TEXT("count -1")), FString::Printf(TEXT("DP11 a count below 0 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At, RH::MaxDepthCap + 1); Expect(TryFile(TEXT("depthr2"), T, Err) && Err.Contains(TEXT("count 31")), FString::Printf(TEXT("DP11 a count above the cap of %d is refused (%s)"), RH::MaxDepthCap, *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 4, 1); Expect(TryFile(TEXT("depthr3"), T, Err) && Err.Contains(TEXT("region")), FString::Printf(TEXT("DP11 a RegionId other than 0 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 8, -21); Expect(TryFile(TEXT("depthr4"), T, Err) && Err.Contains(TEXT("-1..-20")), FString::Printf(TEXT("DP11 a level outside -1..-20 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 8, 0); Expect(TryFile(TEXT("depthr4b"), T, Err) && Err.Contains(TEXT("-1..-20")), FString::Printf(TEXT("DP11 level 0 is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 8, -8); Expect(TryFile(TEXT("depthr5"), T, Err) && Err.Contains(TEXT("no such floor")), FString::Printf(TEXT("DP11 a warm level with no loaded floor is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 8, -10); PutI(T, At + 24, -9); Expect(TryFile(TEXT("depthr6"), T, Err) && Err.Contains(TEXT("shallowest first")), FString::Printf(TEXT("DP11 out-of-order levels are refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutI(T, At + 24, -9); Expect(TryFile(TEXT("depthr7"), T, Err) && Err.Contains(TEXT("shallowest first")), FString::Printf(TEXT("DP11 a repeated level is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; double NaN = 0.0; const uint64 NaNBits = 0x7FF8000000000000ull; FMemory::Memcpy(&NaN, &NaNBits, 8); PutD(T, At + 12, NaN); Expect(TryFile(TEXT("depthr8"), T, Err) && Err.Contains(TEXT("warmth")), FString::Printf(TEXT("DP11 a NaN warmth is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutD(T, At + 12, 0.0); Expect(TryFile(TEXT("depthr9"), T, Err) && Err.Contains(TEXT("warmth")), FString::Printf(TEXT("DP11 a warmth of 0 is refused (only a warm floor is written) (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutD(T, At + 12, -0.5); Expect(TryFile(TEXT("depthr10"), T, Err) && Err.Contains(TEXT("warmth")), FString::Printf(TEXT("DP11 a negative warmth is refused (%s)"), *Err)); }
		{ TArray<uint8> T = W; PutD(T, At + 12, 1.0000001); Expect(TryFile(TEXT("depthr11"), T, Err) && Err.Contains(TEXT("warmth")), FString::Printf(TEXT("DP11 a warmth above 1 is refused (%s)"), *Err)); }
		{
			TArray<uint8> T = W;
			T.SetNum(At + 4 + 16); // cut right after one record, but say two
			Expect(TryFile(TEXT("depthr12"), T, Err) && Err.Contains(TEXT("warm floor count 2")), FString::Printf(TEXT("DP11 a count larger than the bytes left is refused (%s)"), *Err));
		}
		{ TArray<uint8> T = W; const uint32 V46 = 46; FMemory::Memcpy(T.GetData() + 4, &V46, 4); FFileHelper::SaveArrayToFile(T, *SaveFilePath(TEXT("depthr13"))); FString E46; const bool bOk = Sim->LoadColony(TEXT("depthr13"), E46); Expect(!bOk && E46.Contains(TEXT("Save format mismatch")) && E46.Contains(TEXT("/53")), FString::Printf(TEXT("DP11 a v46 file is refused (%s)"), *E46)); }
		// The Warning repair: a level at or above the cut-off (the row moved between save and load) is dropped, the colony kept.
		{
			Reset();
			Sim->ExtendShaft(10, Head0);
			Sim->ExcavateFloor(-3, 6, R, 0);
			Sim->ExcavateFloor(-10, 6, R, 0);
			Sim->Debug_SetFloorWarmth(-3, 0.5);
			Sim->Debug_SetFloorWarmth(-10, 0.5);
			Expect(Sim->SaveColony(TEXT("depthrep"), SaveErr), TEXT("DP11 saved with a warm -3 (at the line's shallow side) and a warm -10"));
			FString Ee;
			Expect(Sim->LoadColony(TEXT("depthrep"), Ee) && Ee.IsEmpty() && Sim->GetFloorWarmth(-3) == 0.0 && Sim->GetFloorWarmth(-10) == 0.5 && Sim->GetFloorCarvedCells(-3) == 6 && Sim->GetFloorCarvedCells(-10) == 6,
				FString::Printf(TEXT("DP11 the Warning repair: the warmth on -3 (not past -5) is dropped, -10 keeps 0.5, the colony loads (%s)"), *Ee));
		}
		// The runtime-only generation is never saved.
		{
			Reset();
			Sim->Debug_SetExtraGenW(0.0);
			Sim->SaveColony(TEXT("depthg0"), SaveErr);
			Sim->Debug_SetExtraGenW(250.0);
			Sim->SaveColony(TEXT("depthg1"), SaveErr);
			TArray<uint8> G0, G1;
			FFileHelper::LoadFileToArray(G0, *SaveFilePath(TEXT("depthg0")));
			FFileHelper::LoadFileToArray(G1, *SaveFilePath(TEXT("depthg1")));
			Sim->Debug_SetExtraGenW(0.0);
			Expect(G0.Num() > 0 && G0 == G1, TEXT("DP11 Debug_SetExtraGenW is runtime-only: the saves with and without it are byte-equal"));
		}
	}

	// --- DP12. Depth is far: the commute rules, pinned (Q-C (a): deep floors are for deep jobs). ---
	{
		Reset();
		Sim->ExtendShaft(20, Head0);
		const FIntPoint HeadXY(FMath::RoundToInt(Head0.X), FMath::RoundToInt(Head0.Y));
		const int64 Per = Sim->GetCommuteShaftCmPerFloor(), Lift = Sim->GetCommuteLiftCmPerFloor(), Long = Sim->GetCommuteLongCm();
		const int64 NoCar20 = Sim->GetCommuteDistanceCm(-20, HeadXY, 0, HeadXY, 0);
		Expect(NoCar20 == 20 * Per && NoCar20 == 120000 && NoCar20 > Long, FString::Printf(TEXT("DP12 a -20 home with a surface seat at the head and no lift car is %lld cm (20 x %lld), over the long line of %lld"), NoCar20, Per, Long));
		Sim->Debug_AddSolidAt(LanderId, NStruct, Sim->GetLiftFitBillKg(), false);
		FString FitWhy;
		const bool bFit = Sim->Debug_FitLift(0, FitWhy);
		const int64 Wait = (int64)Sim->GetTransitWaitSec(0) * Sim->GetCommuteWaitCmPerSec();
		const int64 Car20 = Sim->GetCommuteDistanceCm(-20, HeadXY, 0, HeadXY, 0);
		Expect(bFit && Car20 == 20 * Lift + Wait && Car20 == 40000 + Wait, FString::Printf(TEXT("DP12 the same home with a lift car is %lld cm = 20 x %lld + the frozen wait (%lld cm): 40,000 + wait (%s)"), Car20, Lift, Wait, *FitWhy));
		const int64 Car15 = Sim->GetCommuteDistanceCm(-15, HeadXY, 0, HeadXY, 0), Car16 = Sim->GetCommuteDistanceCm(-16, HeadXY, 0, HeadXY, 0);
		Expect(Car15 == 15 * Lift + Wait && Car16 == 16 * Lift + Wait && Wait == 0 && Car15 == 30000 && !(Car15 > Long) && Car16 == 32000 && Car16 > Long,
			FString::Printf(TEXT("DP12 with a car and a zero leg: -15 is %lld cm (30,000 is not long: the test is > %lld) and -16 is %lld cm (long even on an empty lift)"), Car15, Long, Car16));
		const int64 SameFloor = Sim->GetCommuteDistanceCm(-20, HeadXY, -20, HeadXY, 0);
		Expect(SameFloor == 0, FString::Printf(TEXT("DP12 a -20 home with a seat on -20 is %lld cm: deep floors are for deep jobs"), SameFloor));
	}

	// The runtime-only generation ends at 0, and the rows are back where the data has them.
	Sim->Debug_SetExtraGenW(0.0);
	Sim->Debug_SetHopeWarmMax(HopeWarmRow);
	Expect(Sim->GetExtraGenW() == 0.0 && Sim->GetMaxDepth() == 20 && Sim->GetDepthCostFromFloor() == 5 && Sim->GetHopeWarmMax() == HopeWarmRow, TEXT("DP-end the harness left the generation at 0 and the rows as the data has them"));
	return Finish();
}
