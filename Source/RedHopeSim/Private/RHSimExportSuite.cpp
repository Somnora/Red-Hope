#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHSaveFrame.h"

// E3 selling to Earth self-test (docs/qa/2026-10-23/e3-selling-to-earth-design.md: DIRECTOR ANSWER Q-A (a), Q-B (a), Q-C (a) > REVISION
// R1-R11 > PROFESSOR REVIEW > design). Fails loudly. `-export`. Met is HEAD's test; the launch loads the round's bill (and the export lines)
// from uncommitted stores in building-Id order, then the pool, never below the life-support floor; sales are priced in integer mcr and
// paid after the manifest's debit; the bill and the sales share the ship's cap and the bill plateaus at one ship; a ship that cannot load
// may launch short after QuotaShortLaunchSols, the shortfall owed on the next round.
//  XE0 the data; XE1 met is unchanged; XE2 the draw order; XE3 the floor; XE4 met but not loadable; XE5 Owed; XE6 prices; XE7 a sale;
//  XE14 the share applies to the open round (E3b);
//  XE8 the cap and the plateau; XE9 era parity (R3); XE10 the save; XE11 ruling 11; XE13 the short launch (R1); XE14 E3b. XE12 is the mutation list,
//  run by hand and recorded in design-decisions.
int32 RHRunExportSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== EXPORT TEST (E3, 2026-10-23) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("EXPORT ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("EXPORT FAIL: %s"), *What); ++Fails; }
	};
	const auto Info = [](const FString& What) { UE_LOG(LogRedHopeSim, Display, TEXT("EXPORT info: %s"), *What); };
	const auto Near = [](double A, double B, double Tol) { return FMath::Abs(A - B) <= Tol; };
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	using ELeg = URHSimWorldSubsystem::ERHLedgerLeg;
	const FName NStruct(TEXT("Struct")), NWater(TEXT("Water")), NOx(TEXT("Oxygen")), NHyd(TEXT("Hydrogen")), NIce(TEXT("Ice")), NSeeds(TEXT("Seeds")), NFood(TEXT("Food"));
	const auto Cr = [](int64 Mcr) { return URHSimWorldSubsystem::FormatCredits(Mcr); };
	const auto Finish = [&]() -> int32
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== EXPORT TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto EraOnce = [&]() { Clock->Debug_AdvanceSimSeconds(EraDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->EraStep((float)EraDt); };
	const auto AgentOnce = [&]() { Clock->Debug_AdvanceSimSeconds((double)AgentDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->Debug_StepSim(AgentDt); };
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	const auto Inv = [&]() { return Sim->GetCreditBalanceMcr() == Sim->GetCreditAwardedMcr() - Sim->GetCreditSpentMcr() && Sim->GetCreditBalanceMcr() >= 0
		&& Sim->GetSalesPaidMcr() >= 0 && Sim->GetSalesPaidMcr() <= Sim->GetCreditAwardedMcr(); };
	const auto Held = [&](FName R) { return Sim->GetLaunchLoadableKg(R) + Sim->GetLaunchFloorKg(R); }; // uncommitted holdings
	const auto Out = [&](FName R) { return (double)Sim->GetLedgerLifetimeG(R, ELeg::Out) / 1000.0 + Sim->GetLedgerOpenKg(R, ELeg::Out); };
	const auto SetPool = [&](FName R, double Kg) { Sim->AddStock(R, Kg - Sim->GetStock(R)); };
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto Shelf = [&](int32 Id, FName R, bool bOut) { const FRHBuildingInstance* B = Sim->GetBuildings().FindByPredicate([Id](const FRHBuildingInstance& X) { return X.Id == Id; }); return B ? (bOut ? B->OutputKg : B->InputKg).FindRef(R) : -1.0; };
	FString SaveErr;
	Expect(Sim->SaveColony(TEXT("exportbase"), SaveErr), FString::Printf(TEXT("XE the fresh colony is saved as the base every fixture loads (%s)"), *SaveErr));
	int32 LanderId = 0;
	const auto LoadSlot = [&](const TCHAR* Slot)
	{
		FString E;
		const bool bOk = Sim->LoadColony(Slot, E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("EXPORT FAIL: loading %s failed: %s"), Slot, *E); ++Fails; }
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetEarthTakesQuota(true);
		LanderId = LastIdOf(TEXT("Lander"));
		return bOk;
	};
	const auto Reset = [&]() { return LoadSlot(TEXT("exportbase")); };
	// The open round's bill topped up to bill + the launch floor + Extra in the pool (solids and fluids alike), then steps to the met step.
	const auto Meet = [&](double Extra) -> bool
	{
		for (const auto& L : Sim->GetQuotaProgress())
		{
			const double Gap = L.Value.Value + Sim->GetLaunchFloorKg(L.Key) + Extra - L.Value.Key;
			if (Gap > 0.0) { Sim->AddStock(L.Key, Gap); }
		}
		for (int32 K = 0; K < 400 && Sim->GetQuotaPhase() == ERHQuotaPhase::Open; ++K) { EraOnce(); }
		return Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest;
	};
	const auto LandShip = [&]()
	{
		for (int32 K = 0; K < 10 * (int32)(SolLen / EraDt) && Sim->GetQuotaPhase() == ERHQuotaPhase::ShipInbound; ++K) { EraOnce(); }
		return Sim->GetQuotaPhase() != ERHQuotaPhase::ShipInbound;
	};
	const auto BillOf = [&](FName R) { TArray<TPair<FName, double>> B; Sim->GetLaunchBill(B); double V = 0.0; for (const auto& P : B) { V += P.Key == R ? P.Value : 0.0; } return V; };
	// A colony with people: floor -1 of 12 cells, rated, N colonists (the floor is then 10 x N x each draw).
	const auto Crew = [&](int32 N)
	{
		FString R;
		Sim->ExtendShaft(1, FVector(1000.0, 1000.0, 0.0));
		Sim->ExcavateFloor(-1, 12, R);
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.0, 1500.0, 0.0), -1);
		Sim->Debug_SetExtraGenW(1.0e6);
		EraOnce();
		Sim->Debug_ForceRateFloor(-1);
		return Sim->Debug_AddColonists(N);
	};

	// --- XE0. The data. ---
	{
		const TSet<FName> Buys = { FName("Regolith"), FName("Ore"), NStruct, FName("Shielding"), NIce, NWater, NOx, NHyd, FName("SpoilChemicals") };
		int32 Rows = 0, Right = 0;
		FString Bad;
		if (DefsSub)
		{
			DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow& Row)
			{
				++Rows;
				const bool bWant = Buys.Contains(N);
				if (Row.EarthBuys == bWant && Sim->DoesEarthBuy(N) == bWant) { ++Right; } else { Bad += N.ToString() + TEXT(" "); }
			});
		}
		FString Csv;
		const bool bCsv = FFileHelper::LoadFileToString(Csv, *(FPaths::ProjectDir() / TEXT("docs/data/RH_Resources.csv")));
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		int32 CsvSame = 0;
		if (Lines.Num() > 1)
		{
			TArray<FString> Head;
			Lines[0].ParseIntoArray(Head, TEXT(","), false);
			const int32 CN = Head.IndexOfByKey(TEXT("Name")), CB = Head.IndexOfByKey(TEXT("EarthBuys"));
			for (int32 i = 1; CN != INDEX_NONE && CB != INDEX_NONE && i < Lines.Num(); ++i)
			{
				TArray<FString> Cells;
				Lines[i].ParseIntoArray(Cells, TEXT(","), false); // every column up to EarthBuys is unquoted
				const FRHResourceRow* Row = (Cells.Num() > CB && DefsSub) ? DefsSub->GetResource(FName(*Cells[CN])) : nullptr;
				CsvSame += (Row && Row->EarthBuys == Cells[CB].Equals(TEXT("TRUE"), ESearchCase::IgnoreCase)) ? 1 : 0;
			}
		}
		const FRHQuotaRow* Q3 = DefsSub ? DefsSub->GetQuota(FName("Q3")) : nullptr;
		int64 RawTotal = 0;
		TArray<int64> Plateau;
		if (Q3)
		{
			for (const auto& L : URHDefinitionsSubsystem::ParseResourceList(Q3->Requirements)) { RawTotal += FMath::RoundToInt64(L.Value); }
			for (const auto& L : URHDefinitionsSubsystem::ParseResourceList(Q3->Requirements)) { Plateau.Add(FMath::RoundToInt64(L.Value) * 30000 / FMath::Max<int64>(1, RawTotal)); }
		}
		Expect(Rows == 21 && Right == 21 && bCsv && CsvSame == 21 && Sim->GetLaunchCostMcrPerKg() == 3000
			&& DefsSub && DefsSub->GetConfigTableScalar(FName("LaunchH2GPerKg"), -1.0) == 50.0 && DefsSub->GetConfigTableScalar(FName("LaunchO2GPerKg"), -1.0) == 300.0
			&& DefsSub->GetConfigTableScalar(FName("QuotaShortLaunchSols"), -1.0) == 3.0 && Sim->GetShipMassCapCfgKg() == 30000.0
			&& RawTotal == 3950 && Plateau.Num() == 3 && Plateau[0] == 18987 && Plateau[1] == 9113 && Plateau[2] == 1898,
			FString::Printf(TEXT("XE0 EarthBuys is TRUE for exactly the nine Mars-made rows and FALSE for the other 12 (CSV = DT on %d rows) %s; launch cost L = %s cr/kg (50 g H2 + 300 g O2 a kg); short launch after 3 sols; cap 30,000 kg; Q3's raw total %lld kg and its plateau %lld / %lld / %lld"),
				CsvSame, *Bad, *Cr(Sim->GetLaunchCostMcrPerKg()), RawTotal, Plateau.Num() > 0 ? Plateau[0] : -1, Plateau.Num() > 1 ? Plateau[1] : -1, Plateau.Num() > 2 ? Plateau[2] : -1));
	}

	// --- XE1. Met is unchanged: the take is at launch, so a colony that meets and does not launch is bit-identical with the take off. ---
	{
		const auto Run = [&](bool bTakes) -> FString
		{
			Reset();
			Sim->Debug_SetEarthTakesQuota(bTakes);
			for (int32 K = 0; K < 5 * (int32)(SolLen / EraDt); ++K) { EraOnce(); }
			const bool bMet = Meet(0.0);
			const int32 MetSol = Clock->GetSol();
			for (int32 K = 0; K < (int32)(SolLen / EraDt) / 2; ++K) { EraOnce(); }
			FString D = FString::Printf(TEXT("met %d sol %d award %.17g balance %lld phase %d"), bMet ? 1 : 0, MetSol, Sim->GetAwardMassKg(), Sim->GetCreditBalanceMcr(), (int32)Sim->GetQuotaPhase());
			if (DefsSub) { DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow&) { D += FString::Printf(TEXT(" %s=%.17g/%.17g"), *N.ToString(), Sim->GetStock(N), Sim->GetTotalSolid(N)); }); }
			for (const FName R : { NStruct, NWater, NOx })
			{
				const URHSimWorldSubsystem::FRHGrowthAvailability A = Sim->GetGrowthAvailability(R);
				D += FString::Printf(TEXT(" |%s %.17g %.17g %.17g %.17g"), *R.ToString(), A.Held, A.QuotaBill, A.FullBill, A.Reserve);
			}
			Sim->Debug_SetEarthTakesQuota(true);
			return D;
		};
		const FString On = Run(true), Off = Run(false);
		Expect(On == Off && On.StartsWith(TEXT("met 1")),
			FString::Printf(TEXT("XE1 5 sols, Q1's met step and half a sol after it, no launch: the met sol, the award, the balance, the phase, every stock and the growth figures (Held, QuotaBill, FullBill, Reserve) are bit-identical with the take on and off (%s)"), *On.Left(90)));
	}

	// --- XE2. The draw order: uncommitted stores in building-Id order, outputs before inputs, then the pool; never a site or an unlinked floor. ---
	{
		Reset();
		FRHCommand Build; Build.Verb = FName("Build"); Build.Target = FName("Stockpile"); Build.Location = FVector(0.0, -2000.0, 0.0);
		Sim->EnqueueCommand(Build);
		for (int32 K = 0; K < 5 && Sim->GetUplinkQueue().Num() > 0; ++K) { EraOnce(); }
		int32 Site = 0;
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.bUnderConstruction) { Site = Bd.Id; } }
		Sim->Debug_AddSolidAt(LanderId, NStruct, -Shelf(LanderId, NStruct, false), false); // the Lander's starter Struct goes, so the fixture holds every kg
		Sim->Debug_AddSolidAt(LanderId, NStruct, -Shelf(LanderId, NStruct, true), true);
		if (Site != 0) { Sim->Debug_AddSolidAt(Site, NStruct, -Shelf(Site, NStruct, false), false); }
		SetPool(NStruct, 0.0);
		Sim->Debug_PlaceInstant(FName("Stockpile"), FVector(4000.0, 4000.0, 0.0), 0);
		const int32 A = LastIdOf(TEXT("Stockpile"));
		Sim->Debug_PlaceInstant(FName("Stockpile"), FVector(8000.0, 4000.0, 0.0), 0);
		const int32 B = LastIdOf(TEXT("Stockpile"));
		Sim->Debug_PlaceInstant(FName("Stockpile"), FVector(4000.0, 8000.0, 0.0), -3); // a floor no shaft reaches
		const int32 Deep = LastIdOf(TEXT("Stockpile"));
		Sim->Debug_AddSolidAt(A, NStruct, 100.0, true);
		Sim->Debug_AddSolidAt(A, NStruct, 50.0, false);
		Sim->Debug_AddSolidAt(B, NStruct, 200.0, true);
		Sim->Debug_AddSolidAt(Site, NStruct, 400.0, false);
		Sim->Debug_AddSolidAt(Deep, NStruct, 300.0, true);
		SetPool(NStruct, 100.0);
		const bool bMet = Meet(0.0); // tops Water and Oxygen only: Struct's 1,150 kg already meets the 300
		const double Out0 = Out(NStruct), Held0 = Held(NStruct);
		FString Err;
		const bool bLaunched = Sim->LaunchShip(Err);
		Expect(A < B && Site != 0 && !Sim->IsLevelConnected(-3) && bMet && bLaunched
			&& Shelf(A, NStruct, true) == 0.0 && Shelf(A, NStruct, false) == 0.0 && Shelf(B, NStruct, true) == 50.0 && Shelf(Site, NStruct, false) == 400.0 && Shelf(Deep, NStruct, true) == 300.0
			&& Sim->GetStock(NStruct) == 100.0 && Near(Out(NStruct) - Out0, 300.0, 1e-9) && Sim->GetLedgerOpenKg(NStruct, ELeg::Out) + 0.0 >= 0.0 && Near(Held0 - Held(NStruct), 300.0, 1e-9),
			FString::Printf(TEXT("XE2 Q1's 300 kg Struct leaves as #%d's output 100, then its input 50, then #%d's output 150 (50 left); the site #%d keeps its 400, the store on unlinked floor -3 its 300, the pool its 100; E1's Out leg +300 kg; holdings -300 (%s)"), A, B, Site, *Err));
	}

	// --- XE3. The floor: Earth never takes what the colony breathes, drinks or eats. ---
	{
		Reset();
		const int32 People = Crew(6);
		const bool bMet = Meet(5.0);
		const double OxFloor = Sim->GetLaunchFloorKg(NOx), WaterFloor = Sim->GetLaunchFloorKg(NWater);
		const double OxBill = BillOf(NOx), WaterBill = BillOf(NWater);
		SetPool(NWater, WaterBill + WaterFloor + 50.0);
		SetPool(NOx, OxBill + OxFloor - 1.0);
		const double Ox0 = Sim->GetStock(NOx), St0 = Held(NStruct);
		FString Err;
		const bool bRefusedOx = !Sim->LaunchShip(Err) && Err.Contains(TEXT("Oxygen")) && Err.Contains(TEXT("45 kept for life support"))
			&& Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && Sim->GetStock(NOx) == Ox0 && Held(NStruct) == St0;
		const FString OxErr = Err;
		SetPool(NOx, OxBill + OxFloor + 1.0);
		SetPool(NWater, WaterBill + WaterFloor - 1.0);
		const bool bRefusedWater = !Sim->LaunchShip(Err) && Err.Contains(TEXT("Water")) && Err.Contains(TEXT("120 kept for life support"));
		SetPool(NWater, WaterBill + WaterFloor + 1.0);
		const bool bLaunched = Sim->LaunchShip(Err);
		Expect(People == 6 && bMet && OxFloor == 45.0 && WaterFloor == 120.0 && bRefusedOx && bRefusedWater && bLaunched && Near(Sim->GetStock(NOx), OxFloor + 1.0, 1e-9) && Near(Sim->GetStock(NWater), WaterFloor + 1.0, 1e-9),
			FString::Printf(TEXT("XE3 with 6 colonists Earth leaves 45 kg of Oxygen and 120 of Water: a kilo short is refused, nothing changes ('%s'); the same for Water; a kilo over launches and leaves exactly the floor + 1 kg"), *OxErr));
		Reset();
		Expect(Sim->GetLaunchFloorKg(NOx) == 0.0 && Sim->GetLaunchFloorKg(NWater) == 0.0 && Sim->GetLaunchFloorKg(NFood) == 0.0 && Sim->GetLaunchFloorKg(NStruct) == 0.0,
			TEXT("XE3 with no colonists the floor is 0, and Struct never has one (Earth does not honour the city's float)"));
	}

	// --- XE4. Met but not loadable: Struct in a construction site counts for the met test, not for the launch. ---
	{
		Reset();
		FRHCommand Build; Build.Verb = FName("Build"); Build.Target = FName("Stockpile"); Build.Location = FVector(0.0, -2000.0, 0.0);
		Sim->EnqueueCommand(Build);
		for (int32 K = 0; K < 5 && Sim->GetUplinkQueue().Num() > 0; ++K) { EraOnce(); }
		int32 Site = 0;
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.bUnderConstruction) { Site = Bd.Id; } }
		Sim->Debug_AddSolidAt(LanderId, NStruct, -Shelf(LanderId, NStruct, false), false);
		Sim->Debug_AddSolidAt(LanderId, NStruct, -Shelf(LanderId, NStruct, true), true);
		if (Site != 0) { Sim->Debug_AddSolidAt(Site, NStruct, 400.0 - Shelf(Site, NStruct, false), false); }
		SetPool(NStruct, 0.0);
		const bool bMet = Meet(0.0);
		const double Award = Sim->GetAwardMassKg();
		FString Err;
		bool bShort = true;
		const bool bRefused = !Sim->CanLaunchShip(Err, bShort) && !bShort && Err.Contains(TEXT("Struct needs 300 kg")) && Err.Contains(TEXT("400 kg more is in construction sites or on floors with no lift"));
		const FString Why = Err;
		const bool bNothing = !Sim->LaunchShip(Err) && Shelf(Site, NStruct, false) == 400.0 && Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest;
		for (int32 K = 0; K < 10 * (int32)(SolLen / EraDt); ++K) { EraOnce(); }
		const bool bStillOnTime = Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest && Sim->GetAwardMassKg() == Award;
		Sim->Debug_AddSolidAt(LanderId, NStruct, 300.0, true); // the Struct reaches a finished store
		const bool bLaunched = Sim->LaunchShip(Err);
		Expect(bMet && bRefused && bNothing && bStillOnTime && bLaunched && Shelf(Site, NStruct, false) == 400.0,
			FString::Printf(TEXT("XE4 Q1 met on 400 kg of Struct in a site's hopper: the launch is refused ('%s') and draws nothing; 10 sols later the award is still the on-time %.0f kg; with 300 kg in a finished store the ship goes, the site's hopper untouched"), *Why, Award));
	}

	// --- XE5. Owed: Earth's take stands beside the share's term while the manifest is open, and nowhere else. ---
	{
		bool bAll = true;
		FString Seen;
		for (const double Share : { 0.0, 0.5, 1.0 })
		{
			Reset();
			Sim->Debug_SetCityShare(Share);
			const auto Check = [&](const TCHAR* Phase, double OwedStruct)
			{
				const URHSimWorldSubsystem::FRHGrowthAvailability A = Sim->GetGrowthAvailability(NStruct);
				TMap<FName, double> Next;
				Sim->GetNextQuotaBills(Next);
				const bool bOpen = Sim->GetQuotaPhase() == ERHQuotaPhase::Open;
				const double HeadFull = bOpen ? Sim->GetQuotaProgress().FindRef(NStruct).Value : Next.FindRef(NStruct);
				const double HeadBill = (1.0 - Share) * HeadFull; // E3b: the share scales the open round's bill as it always scaled the next's
				const bool bOk = Near(A.Owed, OwedStruct, 1e-9) && Near(A.FullBill, HeadFull, 1e-9) && Near(A.QuotaBill, HeadBill, 1e-9) && Near(A.Free, A.Held - A.QuotaBill - A.Owed - A.Reserve, 1e-9);
				bAll &= bOk;
				Seen += FString::Printf(TEXT(" %.0f%%/%s owed %.0f bill %.0f;"), Share * 100.0, Phase, A.Owed, A.QuotaBill);
			};
			Check(TEXT("Open"), 0.0);
			Meet(0.0);
			SetPool(NStruct, 5000.0);
			FString Err;
			Sim->AddExportLine(NStruct, 700, Err);
			Check(TEXT("Awaiting"), 300.0 + 700.0);
			Sim->LaunchShip(Err);
			Check(TEXT("Inbound"), 0.0);
		}
		Reset();
		Sim->Debug_SetQuotaRepeatBillMul(0.0);
		Sim->Debug_SetEarthTakesQuota(false);
		for (int32 Ship = 0; Ship < 6 && Sim->GetQuotaPhase() != ERHQuotaPhase::Completed; ++Ship) { Meet(0.0); FString E; Sim->LaunchShip(E); LandShip(); }
		const bool bRest = Sim->GetQuotaPhase() == ERHQuotaPhase::Completed && Sim->GetGrowthAvailability(NStruct).Owed == 0.0;
		Sim->Debug_SetEarthTakesQuota(true);
		Sim->Debug_SetQuotaRepeatBillMul(DefsSub ? DefsSub->GetConfigScalar(FName("QuotaRepeatBillMul"), 1.10) : 1.10);
		Expect(bAll && bRest,
			FString::Printf(TEXT("XE5 Owed is the bill line + the export line only while the manifest is open (Struct 300 + 700), 0 when Open, inbound and at rest (Completed); FullBill is the bill and QuotaBill is (1 - share) x it in every phase (E3b) at shares 0, 0.5 and 1, and Free = Held - QuotaBill - Owed - Reserve:%s"), *Seen));
	}

	// --- XE6. Prices: integer mcr, V x Mpm / 1000 - L, through the real multiplier. ---
	{
		Reset();
		struct FCol { double Axis, Tension, Mult; };
		const FCol Cols[] = { { 0.0, 0.0, 1.0 }, { -100.0, 0.0, 1.4 }, { 100.0, 0.0, 0.4 }, { 0.0, 100.0, 0.7 }, { 100.0, 100.0, 0.1 } };
		// The design's table (section 2.3) in mcr a kg; 0 or less means Earth will not buy.
		struct FRowPrice { const TCHAR* Name; int64 P[5]; };
		const FRowPrice Table[] = {
			{ TEXT("Regolith"), { -2950, -2930, -2980, -2965, -2995 } }, { TEXT("Ore"), { -2500, -2300, -2800, -2650, -2950 } },
			{ TEXT("SpoilChemicals"), { -1000, -200, -2200, -1600, -2800 } }, { TEXT("Ice"), { 0, 1200, -1800, -900, -2700 } },
			{ TEXT("Water"), { 1000, 2600, -1400, -200, -2600 } }, { TEXT("Struct"), { 2000, 4000, -1000, 500, -2500 } },
			{ TEXT("Oxygen"), { 5000, 8200, 200, 2600, -2200 } }, { TEXT("Shielding"), { 5000, 8200, 200, 2600, -2200 } },
			{ TEXT("Hydrogen"), { 9000, 13800, 1800, 5400, -1800 } } };
		int32 Cells = 0, Right = 0;
		bool bMults = true;
		FString Bad;
		for (int32 c = 0; c < 5; ++c)
		{
			Sim->Debug_SetRequisitionInputs(Cols[c].Axis, Cols[c].Tension);
			bMults &= Near(Sim->GetRequisitionMultiplier(), Cols[c].Mult, 1e-12);
			for (const FRowPrice& R : Table)
			{
				++Cells;
				const int64 P = Sim->GetEarthPriceMcrPerKg(FName(R.Name));
				if (P == R.P[c]) { ++Right; } else { Bad += FString::Printf(TEXT("%s x%.1f %lld (want %lld); "), R.Name, Cols[c].Mult, P, R.P[c]); }
			}
		}
		Expect(bMults && Cells == 45 && Right == 45,
			FString::Printf(TEXT("XE6 every price at multipliers 1.0, 1.4, 0.4, 0.7 and 0.1 (axis and tension through the real GetRequisitionMultiplier) is the design's table as exact integers (%d of 45) %s"), Right, *Bad));
		Sim->Debug_SetRequisitionInputs(0.0, 0.0);
		Meet(0.0);
		for (const FName R : { NIce, FName("Regolith"), NStruct }) { SetPool(R, 5000.0); }
		FString EIce, EReg, ESeeds, EFood, EStruct;
		const bool bIce = !Sim->AddExportLine(NIce, 10, EIce) && EIce.StartsWith(TEXT("Earth pays 3 cr/kg for Ice, less than the 3 cr/kg the launch burns"));
		const bool bReg = !Sim->AddExportLine(FName("Regolith"), 10, EReg);
		const bool bSeeds = !Sim->AddExportLine(NSeeds, 10, ESeeds) && ESeeds == TEXT("Earth does not buy Seeds");
		const bool bFood = !Sim->AddExportLine(NFood, 10, EFood) && EFood == TEXT("Earth does not buy Food");
		Sim->Debug_SetRequisitionInputs(100.0, 100.0);
		const bool bPoor = !Sim->AddExportLine(NStruct, 10, EStruct) && EStruct.StartsWith(TEXT("Earth pays 0.500 cr/kg for Struct"));
		Sim->Debug_SetRequisitionInputs(0.0, 0.0);
		Expect(bIce && bReg && bSeeds && bFood && bPoor,
			FString::Printf(TEXT("XE6 refused: Ice at x1.0 ('%s'), Regolith always, Seeds and Food as not bought; at x0.1 every row, Struct included ('%s')"), *EIce, *EStruct));
	}

	// --- XE7. A sale: quoted, re-quoted, removed, re-added; paid after the debit, into the next ship's credit. ---
	{
		Reset();
		Sim->Debug_SetRequisitionInputs(0.0, 0.0);
		Meet(0.0);
		SetPool(NStruct, 0.0);
		Sim->Debug_AddSolidAt(LanderId, NStruct, 3000.0 - Shelf(LanderId, NStruct, false), false);
		FString Err;
		const bool bAdd = Sim->AddExportLine(NStruct, 1000, Err) && Sim->GetExportLines().Num() == 1 && Sim->GetExportLines()[0].PriceMcrPerKg == 2000;
		Sim->Debug_SetRequisitionInputs(0.0, 50.0); // x0.85: 4,250 - 3,000 = 1,250 mcr a kg
		const bool bMore = Sim->AddExportLine(NStruct, 500, Err) && Sim->GetExportLines()[0].Kg == 1500 && Sim->GetExportLines()[0].PriceMcrPerKg == 1250;
		const bool bRemoved = Sim->RemoveExportLine(NStruct, Err) && Sim->GetExportLines().Num() == 0;
		Sim->Debug_SetRequisitionInputs(0.0, 0.0);
		const bool bReAdd = Sim->AddExportLine(NStruct, 1000, Err) && Sim->GetExportLines()[0].PriceMcrPerKg == 2000 && Sim->GetExportValueMcr() == 2000000;
		const bool bTooMuch = !Sim->AddExportLine(NStruct, 1701, Err) && Err == TEXT("Only 1700 kg of Struct is spare above the quota and the life-support floor");
		const int64 B0 = Sim->GetCreditBalanceMcr(), S0 = Sim->GetSalesPaidMcr();
		const double Held0 = Held(NStruct), Out0 = Out(NStruct);
		const bool bLaunched = Sim->LaunchShip(Err);
		Expect(bAdd && bMore && bRemoved && bReAdd && bTooMuch && bLaunched && Near(Held0 - Held(NStruct), 1300.0, 1e-9) && Near(Out(NStruct) - Out0, 1300.0, 1e-9)
			&& Sim->GetCreditBalanceMcr() == B0 + 2000000 && Sim->GetSalesPaidMcr() == S0 + 2000000 && Sim->GetExportLines().Num() == 0 && Inv(),
			FString::Printf(TEXT("XE7 1,000 kg of Struct quoted at 2 cr/kg; +500 re-quotes the whole line at 1.25 (tension 50); removed and re-added at 2; 1,701 more refused (1,700 spare); the launch draws the bill 300 + the sale 1,000, pays %s cr after the debit, SalesPaid +%s, the lines clear (%s)"), *Cr(2000000), *Cr(2000000), *Err));
		// The credit check runs before the sale: a manifest dearer than the balance but cheaper than the balance plus the sale is refused.
		Reset();
		Meet(0.0);
		SetPool(NStruct, 2000.0);
		Sim->AddExportLine(NStruct, 1000, Err);
		int32 Pushed = 0;
		while (Sim->GetManifestCostMcr() <= Sim->GetCreditBalanceMcr()) { Sim->Debug_PushManifestItem(FName("SeedVault")); ++Pushed; }
		const int64 Cost = Sim->GetManifestCostMcr(), Bal = Sim->GetCreditBalanceMcr();
		const bool bRefused = Cost < Bal + Sim->GetExportValueMcr() && !Sim->LaunchShip(Err) && Err.StartsWith(TEXT("Over budget at launch")) && Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest;
		Expect(bRefused && Sim->GetCreditBalanceMcr() == Bal && Inv(),
			FString::Printf(TEXT("XE7 a manifest of %s cr against a balance of %s cr is refused at launch although the 2,000 cr sale would cover it: sales fund the next ship, never this one ('%s')"), *Cr(Cost), *Cr(Bal), *Err));
	}

	// --- XE8. The cap and the plateau (Q-A (a)). ---
	{
		Reset();
		Meet(0.0);
		SetPool(NStruct, 40000.0);
		FString Err, Err2;
		const double Bill = Sim->GetLaunchBillTotalKg();
		const bool bOver = !Sim->AddExportLine(NStruct, (int32)(30000.0 - Bill) + 1, Err) && Err.StartsWith(TEXT("Over the ship's mass cap for cargo to Earth"));
		const bool bFits = Sim->AddExportLine(NStruct, (int32)(30000.0 - Bill), Err2);
		Expect(Bill == 550.0 && bOver && bFits, FString::Printf(TEXT("XE8 beside Q1's 550 kg bill a sale may take 29,450 kg of the 30,000 kg cap; 29,451 is refused ('%s')"), *Err));
		// The ladder walked to Q3 round 11, nothing taken (the take is off so the walk need not refill), every bill checked. The repeat is
		// pinned at 1.25, where the plateau starts at round 10 (the design's numbers); the shipped value is 1.10.
		Reset();
		Sim->Debug_SetEarthTakesQuota(false);
		Sim->Debug_SetQuotaRepeatBillMul(1.25);
		bool bWalk = true, bIdentity = true, bCapFlags = true;
		FString Walk;
		const FRHQuotaRow* Q3 = DefsSub ? DefsSub->GetQuota(FName("Q3")) : nullptr;
		for (int32 Ship = 0; Ship < 14 && Q3; ++Ship)
		{
			Meet(0.0);
			FString E;
			const bool bInQ3 = Sim->GetActiveQuotaName() == FName("Q3");
			const int32 Round = Sim->GetQuotaRepeatRound();
			if (bInQ3)
			{
				const TMap<FName, TPair<double, double>> Now = Sim->GetQuotaProgress();
				double Total = 0.0, Scaled = 0.0;
				for (const auto& L : URHDefinitionsSubsystem::ParseResourceList(Q3->Requirements)) { Scaled += Sim->ScaleQuotaBill(L.Value, Round); }
				for (const auto& L : URHDefinitionsSubsystem::ParseResourceList(Q3->Requirements))
				{
					const double Want = Round <= 9 ? Sim->ScaleQuotaBill(L.Value, Round) : (double)(FMath::RoundToInt64(L.Value) * 30000 / 3950);
					bWalk &= Now.FindRef(L.Key).Value == Want; // bit for bit
					Total += Now.FindRef(L.Key).Value;
				}
				bCapFlags &= Sim->IsQuotaBillCapped() == (Round >= 10) && (Round != 10 || Scaled > 30000.0);
				Walk += FString::Printf(TEXT(" r%d %.0f;"), Round, Total);
			}
			TMap<FName, double> Next;
			Sim->LaunchShip(E);
			Sim->GetNextQuotaBills(Next);
			LandShip();
			const TMap<FName, TPair<double, double>> After = Sim->GetQuotaProgress();
			for (const auto& N : Next) { bIdentity &= After.FindRef(N.Key).Value == N.Value; }
			bIdentity &= After.Num() == Next.Num();
			if (bInQ3 && Round >= 11) { break; }
		}
		Sim->Debug_SetEarthTakesQuota(true);
		Sim->Debug_SetQuotaRepeatBillMul(DefsSub ? DefsSub->GetConfigScalar(FName("QuotaRepeatBillMul"), 1.10) : 1.10);
		Expect(bWalk && bIdentity && bCapFlags && Walk.Contains(TEXT("r9 29430")) && Walk.Contains(TEXT("r10 29998")) && Walk.Contains(TEXT("r11 29998")),
			FString::Printf(TEXT("XE8 Q3's rounds 0-9 are ScaleQuotaBill's bills bit for bit, rounds 10 and 11 the plateau (18,987 / 9,113 / 1,898); each round's next bills before the landing equal the bills after it (C5c); IsQuotaBillCapped from round 10:%s"), *Walk));
	}

	// --- XE9. Era parity (R3). ---
	{
		// A save with Q3 met and two export lines.
		Reset();
		Sim->Debug_SetEarthTakesQuota(false);
		for (int32 Ship = 0; Ship < 3 && Sim->GetActiveQuotaName() != FName("Q3"); ++Ship) { Meet(0.0); FString E; Sim->LaunchShip(E); LandShip(); }
		Sim->Debug_SetEarthTakesQuota(true);
		const bool bQ3 = Sim->GetActiveQuotaName() == FName("Q3") && Meet(0.0);
		SetPool(NOx, Sim->GetStock(NOx) + 500.0);
		SetPool(NHyd, Sim->GetStock(NHyd) + 200.0);
		FString Err;
		const bool bLines = Sim->AddExportLine(NOx, 400, Err) && Sim->AddExportLine(NHyd, 150, Err) && Sim->GetExportLines().Num() == 2;
		Sim->SaveColony(TEXT("exportq3"), SaveErr);
		// (i) One state, launched at once in each band's world: identical.
		FString Digest[2];
		for (int32 Band = 0; Band < 2; ++Band)
		{
			LoadSlot(TEXT("exportq3"));
			Sim->LaunchShip(Err);
			Digest[Band] = FString::Printf(TEXT("bal %lld sales %lld out %.17g/%.17g/%.17g"), Sim->GetCreditBalanceMcr(), Sim->GetSalesPaidMcr(), Out(NStruct), Out(NOx), Out(NHyd));
			for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { Digest[Band] += FString::Printf(TEXT(" #%d %.17g/%.17g"), Bd.Id, Bd.OutputKg.FindRef(NStruct), Bd.InputKg.FindRef(NStruct)); }
			for (const FName R : { NStruct, NOx, NHyd }) { Digest[Band] += FString::Printf(TEXT(" %s %.17g"), *R.ToString(), Sim->GetStock(R)); }
		}
		Expect(bQ3 && bLines && Digest[0] == Digest[1], FString::Printf(TEXT("XE9 (i) Q3 met with two export lines, launched from one loaded state twice: every store, Out leg, payment and the balance are identical (%s)"), *Digest[0].Left(80)));
		// (ii) From each band's own state after a sol: its deltas are its own bill and lines.
		bool bOwn = true;
		FString Cross;
		for (int32 Band = 0; Band < 2; ++Band)
		{
			LoadSlot(TEXT("exportq3"));
			for (int32 K = 0; K < (Band == 0 ? (int32)(SolLen / (double)AgentDt) : (int32)(SolLen / EraDt)); ++K) { Band == 0 ? AgentOnce() : EraOnce(); }
			SetPool(NOx, Sim->GetStock(NOx) + 2000.0); // keep the breathing colony loadable after its sol
			SetPool(NHyd, Sim->GetStock(NHyd) + 500.0);
			TArray<TPair<FName, double>> Bill;
			Sim->GetLaunchBill(Bill);
			const double S0 = Held(NStruct), O0 = Held(NOx), H0 = Held(NHyd), So0 = Out(NStruct), Oo0 = Out(NOx), Ho0 = Out(NHyd);
			const int64 Pay = Sim->GetExportValueMcr(), B0 = Sim->GetCreditBalanceMcr();
			const bool bL = Sim->LaunchShip(Err);
			const double DS = S0 - Held(NStruct), DO = O0 - Held(NOx), DH = H0 - Held(NHyd);
			double BS = 0, BO = 0, BH = 0;
			for (const auto& P : Bill) { BS += P.Key == NStruct ? P.Value : 0.0; BO += P.Key == NOx ? P.Value : 0.0; BH += P.Key == NHyd ? P.Value : 0.0; }
			bOwn &= bL && Near(DS, BS, 1e-6) && Near(DO, BO + 400.0, 1e-6) && Near(DH, BH + 150.0, 1e-6) && Near(Out(NStruct) - So0, BS, 1e-6) && Near(Out(NOx) - Oo0, BO + 400.0, 1e-6) && Near(Out(NHyd) - Ho0, BH + 150.0, 1e-6)
				&& Sim->GetCreditBalanceMcr() == B0 + Pay;
			Cross += FString::Printf(TEXT(" %s: Struct %.3f O2 %.3f H2 %.3f held after;"), Band == 0 ? TEXT("agent") : TEXT("era"), Held(NStruct), Held(NOx), Held(NHyd));
		}
		Expect(bOwn, TEXT("XE9 (ii) after a sol in each band, each launch draws exactly its own bill and lines from its own stores and pays its own lines"));
		Info(FString::Printf(TEXT("XE9 (ii) the cross-band difference (no bar):%s"), *Cross));
		// (iii) A floor-short launch waits in era and launches there; the ship lands in era.
		Reset();
		Crew(4);
		const bool bMet = Meet(5.0);
		SetPool(NWater, BillOf(NWater) + Sim->GetLaunchFloorKg(NWater) - 10.0);
		const bool bWaits = !Sim->LaunchShip(Err);
		for (int32 K = 0; K < 2 * (int32)(SolLen / EraDt); ++K) { EraOnce(); }
		{ TArray<TPair<FName, double>> Bill; Sim->GetLaunchBill(Bill); for (const auto& L : Bill) { if (Held(L.Key) < L.Value + Sim->GetLaunchFloorKg(L.Key) + 10.0) { Sim->AddStock(L.Key, L.Value + Sim->GetLaunchFloorKg(L.Key) + 10.0 - Held(L.Key)); } } }
		const bool bGoes = Sim->LaunchShip(Err);
		const bool bLands = LandShip();
		Expect(bMet && bWaits && bGoes && bLands && Sim->GetQuotaPhase() == ERHQuotaPhase::Open,
			FString::Printf(TEXT("XE9 (iii) a launch short of Water by the floor waits 2 sols in the era band, launches there once the bill is loadable, and the ship lands in the era band (%d %d %d %d phase %d: %s)"), bMet, bWaits, bGoes, bLands, (int32)Sim->GetQuotaPhase(), *Err));
	}

	// --- XE10. The save (v54: the frame, and a locked price bounded on load, economy-code-review.md finding 2). ---
	{
		Reset();
		Meet(0.0);
		SetPool(NStruct, 5000.0);
		SetPool(NOx, 2000.0);
		FString Err;
		Sim->AddExportLine(NStruct, 900, Err);
		Sim->AddExportLine(NOx, 300, Err);
		Sim->Debug_GrantSales(12345);
		const int32 Bytes = Sim->GetExportBlockBytes();
		Sim->SaveColony(TEXT("exportsave"), SaveErr);
		TArray<uint8> W;
		const bool bRead = FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("exportsave")));
		uint32 Ver = 0;
		if (bRead) { FMemory::Memcpy(&Ver, W.GetData() + 4, 4); }
		int32 Frame = INDEX_NONE, Payload = 0;
		const bool bFramed = bRead && RHSaveFrame::Find(W, RHSaveFrame::ExportTag, Frame, Payload);
		const int32 At = Frame + RHSaveFrame::HeaderBytes;
		const auto Rd32 = [&](int32 Off) { int32 V = 0; if (Off >= 0 && Off + 4 <= W.Num()) { FMemory::Memcpy(&V, W.GetData() + Off, 4); } return V; };
		const auto Rd64 = [&](int32 Off) { int64 V = 0; if (Off >= 0 && Off + 8 <= W.Num()) { FMemory::Memcpy(&V, W.GetData() + Off, 8); } return V; };
		TArray<FString> Names;
		TArray<int32> KgAt, PriceAt;
		int32 Cur = At + 12;
		for (int32 n = 0; bFramed && n < Rd32(At + 8); ++n)
		{
			const int32 Len = Rd32(Cur);
			Names.Add(FString(ANSI_TO_TCHAR((const ANSICHAR*)(W.GetData() + Cur + 4))));
			Cur += 4 + Len;
			KgAt.Add(Cur);
			PriceAt.Add(Cur + 4);
			Cur += 4 + 8 + 4;
		}
		bool bAscending = Names.Num() == 18;
		for (int32 i = 1; i < Names.Num(); ++i) { bAscending &= Names[i - 1].Compare(Names[i], ESearchCase::CaseSensitive) < 0; }
		const int32 IStruct = Names.IndexOfByKey(TEXT("Struct")), IOx = Names.IndexOfByKey(TEXT("Oxygen"));
		Expect(bFramed && Ver == 54 && Payload == Bytes && Rd64(At) == 12345 && bAscending && Cur == At + Payload && At + Payload + RHSaveFrame::FooterBytes == W.Num()
			&& IStruct != INDEX_NONE && Rd32(KgAt[IStruct]) == 900 && Rd64(PriceAt[IStruct]) == 2000 && Rd32(KgAt[IOx]) == 300,
			FString::Printf(TEXT("XE10 v%u: the export block is the file's last frame ('RHE3', %d payload bytes) - sales 12,345 mcr, 18 slots by name (%s .. %s), Struct 900 kg at 2 cr, Oxygen 300 kg"), Ver, Payload, Names.Num() ? *Names[0] : TEXT("?"), Names.Num() ? *Names.Last() : TEXT("?")));
		FString Le;
		const bool bLoaded = Sim->LoadColony(TEXT("exportsave"), Le) && Le.IsEmpty() && Sim->SaveColony(TEXT("exportsave2"), SaveErr);
		TArray<uint8> W2;
		Expect(bLoaded && FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("exportsave2"))) && W2 == W && Sim->GetExportLines().Num() == 2 && Sim->GetSalesPaidMcr() == 12345,
			FString::Printf(TEXT("XE10 mid-manifest with two lines: save, load, save is byte-identical (%d bytes)"), W.Num()));
		const int64 Balance0 = Sim->GetCreditBalanceMcr();
		const auto TryBytes = [&](const TCHAR* Slot, const TArray<uint8>& Bytes2)
		{
			FFileHelper::SaveArrayToFile(Bytes2, *SaveFilePath(Slot));
			FString E;
			return Sim->LoadColony(Slot, E) && E.IsEmpty();
		};
		// A doctored block: the edit, then the frame's CRC rewritten, so the block's own repairs are what judge it.
		const auto Sealed = [](TArray<uint8> T) { RHSaveFrame::Reseal(T, RHSaveFrame::ExportTag); return T; };
		const auto Put32 = [](TArray<uint8>& T, int32 Off, int32 V) { FMemory::Memcpy(T.GetData() + Off, &V, 4); };
		const auto Put64 = [](TArray<uint8>& T, int32 Off, int64 V) { FMemory::Memcpy(T.GetData() + Off, &V, 8); };
		bool bRepairs = true;
		{ TArray<uint8> T = W; Put32(T, KgAt[IStruct], -5); bRepairs &= TryBytes(TEXT("exportr1"), Sealed(T)) && Sim->GetExportLines().Num() == 1 && Sim->GetExportLines()[0].Resource == NOx; }
		{ TArray<uint8> T = W; Put64(T, PriceAt[IStruct], 0); bRepairs &= TryBytes(TEXT("exportr2"), Sealed(T)) && Sim->GetExportLines().Num() == 1; }
		{ TArray<uint8> T = W; Put64(T, At, Sim->GetCreditAwardedMcr() + 999999999); bRepairs &= TryBytes(TEXT("exportr3"), Sealed(T)) && Sim->GetSalesPaidMcr() == Sim->GetCreditAwardedMcr() && Inv(); }
		{
			// Unsorted: Struct's name overwritten with "Zzzzzz" (the same length), which sorts after the Water slot that follows it.
			TArray<uint8> T = W;
			FMemory::Memcpy(T.GetData() + KgAt[IStruct] - 7, "Zzzzzz", 6);
			bRepairs &= TryBytes(TEXT("exportr4"), Sealed(T)) && Sim->GetExportLines().Num() == 0;
		}
		Expect(bRepairs, TEXT("XE10 doctored blocks each load with a Warning: a negative kg drops that line, a priced-0 line drops, sales above awarded clamp to awarded (the invariant holds), an unsorted block drops every line"));
		// v54: a locked price above the highest quote today's table can make is clamped to it; a slot count one short leaves bytes over and
		// drops every line; a block that fails its CRC is lost (no line, no arrears), and the credit before it loads as saved.
		{
			const int64 Ceiling = Sim->GetEarthPriceCeilingMcrPerKg(NStruct);
			TArray<uint8> T = W; Put64(T, PriceAt[IStruct], 1000000000);
			const bool bClamped = TryBytes(TEXT("exportr7"), Sealed(T)) && Sim->GetExportLines().Num() == 2 && Sim->GetExportLines()[1].Resource == NStruct
				&& Sim->GetExportLines()[1].PriceMcrPerKg == Ceiling && Ceiling == 4000;
			TArray<uint8> U = W; Put32(U, At + 8, 17);
			const bool bShort = TryBytes(TEXT("exportr8"), Sealed(U)) && Sim->GetExportLines().Num() == 0 && Sim->GetSalesPaidMcr() == 0;
			TArray<uint8> C = W; C[KgAt[IStruct]] ^= 0x01;
			const bool bLost = TryBytes(TEXT("exportr9"), C) && Sim->GetExportLines().Num() == 0 && Sim->GetSalesPaidMcr() == 0 && Sim->GetCreditBalanceMcr() == Balance0 && Inv();
			Expect(bClamped && bShort && bLost,
				FString::Printf(TEXT("XE10 a Struct line locked at 1,000,000 cr/kg loads clamped to the %s cr/kg ceiling (%s); a count of 17 leaves bytes over and drops every line (%s); a corrupted block is lost and the credit loads as saved (%s)"),
					*Cr(Ceiling), bClamped ? TEXT("ok") : TEXT("FAIL"), bShort ? TEXT("ok") : TEXT("FAIL"), bLost ? TEXT("ok") : TEXT("FAIL")));
		}
		// Lines saved while the ship is inbound are dropped; arrears are kept in every phase.
		Reset();
		Meet(0.0);
		Sim->LaunchShip(Err);
		Sim->SaveColony(TEXT("exportinbound"), SaveErr);
		TArray<uint8> WI;
		FFileHelper::LoadFileToArray(WI, *SaveFilePath(TEXT("exportinbound")));
		int32 FrameI = INDEX_NONE, PayloadI = 0;
		RHSaveFrame::Find(WI, RHSaveFrame::ExportTag, FrameI, PayloadI);
		int32 CurI = FrameI + RHSaveFrame::HeaderBytes + 12, KgI = -1;
		for (int32 n = 0; FrameI != INDEX_NONE && n < 18; ++n)
		{
			int32 Len = 0; FMemory::Memcpy(&Len, WI.GetData() + CurI, 4);
			const FString Nm(ANSI_TO_TCHAR((const ANSICHAR*)(WI.GetData() + CurI + 4)));
			CurI += 4 + Len;
			if (Nm == TEXT("Struct")) { KgI = CurI; }
			CurI += 16;
		}
		TArray<uint8> T = WI;
		if (KgI > 0) { Put32(T, KgI, 500); Put64(T, KgI + 4, 2000); }
		const bool bInbound = KgI > 0 && TryBytes(TEXT("exportr5"), Sealed(T)) && Sim->GetQuotaPhase() == ERHQuotaPhase::ShipInbound && Sim->GetExportLines().Num() == 0;
		{ TArray<uint8> V = W; const uint32 V53 = 53; FMemory::Memcpy(V.GetData() + 4, &V53, 4); FFileHelper::SaveArrayToFile(V, *SaveFilePath(TEXT("exportr6"))); }
		FString E6;
		const bool bOld = !Sim->LoadColony(TEXT("exportr6"), E6) && E6.Contains(TEXT("/54"));
		Expect(bInbound && bOld, FString::Printf(TEXT("XE10 a line saved while the ship is inbound is dropped on load; a v53 file is refused (%s)"), *E6));
	}

	// --- XE11. Ruling 11 and Q4a. ---
	{
		Reset();
		Meet(0.0);
		FString EH, ER, EP;
		const bool bNever = !Sim->AddExportLine(FName("Hope"), 1, EH) && !Sim->AddExportLine(FName("Research"), 1, ER) && !Sim->AddExportLine(FName("Power"), 1, EP)
			&& EH == TEXT("Earth does not buy Hope");
		const auto Twin = [&](bool bSold) -> FString
		{
			Reset();
			Crew(8);
			Sim->AddStock(NFood, 400.0);
			Sim->AddStock(NWater, 600.0);
			Sim->AddStock(NOx, 600.0);
			Sim->Debug_SetGrowthEnabled(true);
			if (bSold) { Sim->Debug_GrantSales(50000000); }
			for (int32 K = 0; K < 10 * (int32)(SolLen / EraDt); ++K) { EraOnce(); }
			FString D = FString::Printf(TEXT("hope %.17g people %d growth %d"), Sim->GetHopeSmoothed(), Sim->GetPopulation(), Sim->GetGrowthProjects().Num());
			if (DefsSub) { DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow&) { D += FString::Printf(TEXT(" %s=%.17g"), *N.ToString(), Sim->GetStock(N)); }); }
			return D;
		};
		const FString None = Twin(false);
		const int64 Sales0 = Sim->GetSalesPaidMcr();
		const FString Sold = Twin(true);
		Expect(bNever && None == Sold && Sim->GetSalesPaidMcr() == Sales0 + 50000000 && Inv(),
			FString::Printf(TEXT("XE11 Hope, Research and Power can never be listed ('%s'); a colony credited 50,000 cr of sales and its twin with none are bit-identical after 10 sols (Hope, people, growth, every stock)"), *EH));
	}

	// --- XE13. The short launch (R1, Q-B (a)). ---
	{
		Reset();
		Crew(4);
		const bool bMet = Meet(40.0); // a margin: the crew breathes from the pool while the quota is met
		const int32 MetSol = Clock->GetSol();
		SetPool(NWater, BillOf(NWater) + Sim->GetLaunchFloorKg(NWater) - 60.0); // 60 kg short of Q1's 200 kg of Water
		SetPool(NStruct, 1000.0);
		FString Err;
		Sim->AddExportLine(NStruct, 200, Err); // a sale the short ship will drop
		const int32 Opens = Sim->GetShortLaunchSol();
		const double Base = (double)(Opens) * SolLen;
		Clock->Debug_AdvanceSimSeconds(Base - 0.01 * SolLen - Clock->GetSimSecondsTotal());
		bool bShort = true;
		const bool bClosed = !Sim->CanLaunchShip(Err, bShort) && !bShort && !Sim->LaunchShipShort(Err) && Err.Contains(FString::Printf(TEXT("a short launch opens at sol %d"), Opens));
		Clock->Debug_AdvanceSimSeconds(Base - Clock->GetSimSecondsTotal());
		const bool bOpen = !Sim->CanLaunchShip(Err, bShort) && bShort;
		const double Water0 = Held(NWater), Struct0 = Held(NStruct), Out0 = Out(NWater);
		const int64 Sales0 = Sim->GetSalesPaidMcr(), Bal0 = Sim->GetCreditBalanceMcr();
		const bool bLaunched = Sim->LaunchShipShort(Err);
		Expect(bMet && Opens == MetSol + 3 && bClosed && bOpen && bLaunched && Near(Water0 - Held(NWater), 140.0, 1e-6) && Near(Out(NWater) - Out0, 140.0, 1e-6) && Near(Struct0 - Held(NStruct), 300.0, 1e-6)
			&& Sim->GetQuotaArrearsKg(NWater) == 60 && Sim->GetQuotaArrearsKg(NStruct) == 0 && Sim->GetQuotaArrearsKg(NOx) == 0 && Sim->GetSalesPaidMcr() == Sales0 && Sim->GetCreditBalanceMcr() == Bal0 && Sim->GetExportLines().Num() == 0,
			FString::Printf(TEXT("XE13 met at sol %d, the short window opens at sol %d (refused 0.01 sol before, allowed at it); the short ship loads Water 140 of 200 and the whole 300 Struct, drops the 200 kg sale unpaid, and leaves 60 kg of Water owed"), MetSol, Opens));
		// The arrears ride the next round: Q2 has no Water line, so it gains one; C5c's identity holds with them.
		TMap<FName, double> Next;
		Sim->GetNextQuotaBills(Next);
		LandShip();
		const TMap<FName, TPair<double, double>> Q2 = Sim->GetQuotaProgress();
		bool bSame = Q2.Num() == Next.Num();
		for (const auto& N : Next) { bSame &= Q2.FindRef(N.Key).Value == N.Value; }
		Expect(Sim->GetActiveQuotaName() == FName("Q2") && Q2.Contains(NWater) && Q2.FindRef(NWater).Value == 60.0 && Q2.FindRef(NStruct).Value == 600.0 && bSame,
			FString::Printf(TEXT("XE13 Q2 opens with a Water line of 60 kg beside its Struct 600 / Oxygen 150 / Hydrogen 40, as the next bills read before the landing (%d lines)"), Q2.Num()));
		// Save, load, save with arrears held is byte-identical; a full launch clears them.
		Sim->SaveColony(TEXT("exportarrears"), SaveErr);
		TArray<uint8> A1, A2;
		FFileHelper::LoadFileToArray(A1, *SaveFilePath(TEXT("exportarrears")));
		LoadSlot(TEXT("exportarrears"));
		Sim->SaveColony(TEXT("exportarrears2"), SaveErr);
		FFileHelper::LoadFileToArray(A2, *SaveFilePath(TEXT("exportarrears2")));
		const bool bRound = A1 == A2 && Sim->GetQuotaArrearsKg(NWater) == 60;
		const bool bMet2 = Meet(200.0);
		FString Prog;
		for (const auto& L : Sim->GetQuotaProgress()) { Prog += FString::Printf(TEXT(" %s %.1f/%.1f"), *L.Key.ToString(), L.Value.Key, L.Value.Value); }
		const double Water1 = Held(NWater);
		Err.Reset();
		const bool bFull = Sim->LaunchShip(Err);
		Expect(bRound && bMet2 && bFull && Near(Water1 - Held(NWater), 60.0, 1e-6) && Sim->GetQuotaArrearsKg(NWater) == 0,
			FString::Printf(TEXT("XE13 save, load, save with the arrears held is byte-identical; Q2's full launch loads the 60 kg of Water and the arrears clear (%d %d %d phase %d,%s; %s)"), bRound, bMet2, bFull, (int32)Sim->GetQuotaPhase(), *Prog, *Err));
	}

	// --- XE14. E3b: the City / Earth share applies to the OPEN round (director, 2026-10-24). ---
	{
		// Q1 is open: Struct 300, Water 200, Oxygen 50. Crew(4) gives Oxygen a breathing floor (10 sols x 4 x 0.75 = 30 kg).
		Reset();
		Crew(4);
		SetPool(NStruct, 1500.0);
		SetPool(NOx, 400.0);
		const TMap<FName, TPair<double, double>> Prog = Sim->GetQuotaProgress();
		const double BillS = Prog.FindRef(NStruct).Value, BillO = Prog.FindRef(NOx).Value;
		bool bOpenNow = Sim->GetQuotaPhase() == ERHQuotaPhase::Open && BillS == 300.0 && BillO == 50.0;
		// share 0 reproduces the old hold exactly: the whole open bill stands, nothing is owed, free = held - bill - reserve.
		Sim->Debug_SetCityShare(0.0);
		const auto S0 = Sim->GetGrowthAvailability(NStruct), O0 = Sim->GetGrowthAvailability(NOx);
		const bool b0 = bOpenNow && !S0.bNextRound && S0.QuotaBill == BillS && S0.FullBill == BillS && S0.Owed == 0.0 && S0.Free == S0.Held - BillS - 1000.0
			&& O0.QuotaBill == BillO && O0.Owed == 0.0 && O0.Free == O0.Held - BillO - O0.Reserve && O0.Reserve > 0.0;
		Expect(b0, FString::Printf(TEXT("XE14 share 0, quota open: the whole bill stands as before E3b (Struct %.0f, Oxygen %.0f), free = held - bill - reserve (Struct %.0f, Oxygen %.0f)"), S0.QuotaBill, O0.QuotaBill, S0.Free, O0.Free));
		// share 0.5 frees half of it, for every resource, and nothing else moves.
		Sim->Debug_SetCityShare(0.5);
		const auto SH = Sim->GetGrowthAvailability(NStruct), OH = Sim->GetGrowthAvailability(NOx), WH = Sim->GetGrowthAvailability(NWater);
		const auto W0 = [&]() { Sim->Debug_SetCityShare(0.0); const auto R = Sim->GetGrowthAvailability(NWater); Sim->Debug_SetCityShare(0.5); return R; }();
		const bool bHalf = SH.QuotaBill == 0.5 * BillS && OH.QuotaBill == 0.5 * BillO && SH.FullBill == BillS && Near(SH.Free - S0.Free, 0.5 * BillS, 1e-9) && Near(OH.Free - O0.Free, 0.5 * BillO, 1e-9)
			&& Near(WH.Free - W0.Free, 0.5 * Prog.FindRef(NWater).Value, 1e-9) && SH.Reserve == S0.Reserve && OH.Reserve == O0.Reserve && SH.Owed == 0.0;
		Expect(bHalf, FString::Printf(TEXT("XE14 share 0.5, quota open: half the bill is freed (Struct %.0f, Oxygen %.0f, Water %.0f), reserves unchanged, free up by exactly that"), SH.Free - S0.Free, OH.Free - O0.Free, WH.Free - W0.Free));
		// share 1 frees all of it while open ...
		Sim->Debug_SetCityShare(1.0);
		const auto S1 = Sim->GetGrowthAvailability(NStruct), O1 = Sim->GetGrowthAvailability(NOx);
		const bool bAll = S1.QuotaBill == 0.0 && O1.QuotaBill == 0.0 && Near(S1.Free, S1.Held - 1000.0, 1e-9) && Near(O1.Free, O1.Held - O1.Reserve, 1e-9);
		// ... but once the round is met, E3's owed cargo and the life-support floor still stand, whatever the share.
		const bool bMet = Meet(0.0);
		SetPool(NStruct, 5000.0);
		SetPool(NOx, 1000.0);
		const auto S2 = Sim->GetGrowthAvailability(NStruct), O2 = Sim->GetGrowthAvailability(NOx);
		const double Floor = Sim->GetLaunchFloorKg(NOx);
		const bool bOwed = bMet && S2.Owed == 300.0 && O2.Owed == 50.0 && Near(S2.Free, S2.Held - 300.0 - 1000.0, 1e-9) && Near(O2.Free, O2.Held - 50.0 - Floor, 1e-9) && O2.Reserve == Floor && Floor > 0.0
			&& Near(S2.Free, Sim->GetLaunchLoadableKg(NStruct) - 300.0 - 1000.0, 1e-9);
		Expect(bAll && bOwed, FString::Printf(TEXT("XE14 share 1: while open all of the bill is freed (Struct free %.0f of %.0f held, bill held back %.0f); once met the %.0f / %.0f kg owed to Earth stand, the %.0f kg Struct reserve and the %.0f kg oxygen floor stand (Struct free %.0f, Oxygen free %.0f)"),
			S1.Free, S1.Held, S1.QuotaBill, S2.Owed, O2.Owed, S2.Reserve, Floor, S2.Free, O2.Free));
		// A short launch rolls the gap into the next round's bill, at any share (the share is growth's; Earth's bill is not scaled by it).
		bool bRolled = true;
		FString Rolled;
		for (const double Share : { 0.5, 1.0 })
		{
			Reset();
			Crew(4);
			Sim->Debug_SetCityShare(Share);
			Meet(40.0);
			SetPool(NWater, BillOf(NWater) + Sim->GetLaunchFloorKg(NWater) - 60.0);
			Clock->Debug_AdvanceSimSeconds((double)Sim->GetShortLaunchSol() * SolLen - Clock->GetSimSecondsTotal());
			FString Err;
			const bool bLaunched = Sim->LaunchShipShort(Err);
			LandShip();
			const TMap<FName, TPair<double, double>> Q2 = Sim->GetQuotaProgress();
			const bool bOk = bLaunched && Sim->GetActiveQuotaName() == FName("Q2") && Q2.FindRef(NWater).Value == 60.0 && Q2.FindRef(NStruct).Value == 600.0 && Sim->GetQuotaArrearsKg(NWater) == 60;
			const auto Q2S = Sim->GetGrowthAvailability(NStruct), Q2W = Sim->GetGrowthAvailability(NWater);
			bRolled &= bOk && Q2S.QuotaBill == (1.0 - Share) * 600.0 && Q2W.QuotaBill == (1.0 - Share) * 60.0 && Q2S.FullBill == 600.0;
			Rolled += FString::Printf(TEXT(" %.0f%%: Water arrears %d, Q2 Water bill %.0f, held back %.0f;"), Share * 100.0, Sim->GetQuotaArrearsKg(NWater), Q2.FindRef(NWater).Value, Q2W.QuotaBill);
		}
		Expect(bRolled, FString::Printf(TEXT("XE14 a short launch (Water 60 kg short) rolls the gap into Q2's bill at share 0.5 and 1, and growth then holds back (1 - share) x it with the rest:%s"), *Rolled));
	}

	return Finish();
}
