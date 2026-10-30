#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "HAL/PlatformOutputDevices.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "RHSaveFrame.h"

// E7 region-ready self-test (docs/qa/2026-10-25/e7-region-ready-design.md: DIRECTOR ANSWER Q-A (b), Q-B (b) > the orchestrator's rulings 4,
// 5 and 12 > the E7 build brief > REVISION 3 > REVISION 2 > REVISION (R3 only) > RE-CHECK > reviews > design). Fails loudly. `-regionready`.
//  RR3 depots and routes in name order (FIRST: it must be the first code in the process to create the names RrZeta, then RrAlpha, so their
//  FName index order is the reverse of their name order); RR0 the endowment data (the CSV against the asset, the default, one Warning per
//  bad row, ruling 12's case-sensitive resource check, an injected row read through the index); RR1 inertness, exact to the bit (the routes,
//  the legs, the depot, production at the identity, the band); RR2 landing follows the depot (a standing trade, a written order and a covert
//  run's loot land in the bound stockpile; the binding's refusals; a demolished depot); every injected rival retired; RR4 the endowment by
//  hand (from MX2's Set A Ice state) and the restore check; RR5 era parity with a bound depot and an endowment; RR6 the save: nothing
//  saved, nothing kept. No check prints a file size or a save version. The mutation list is run by hand and recorded in design-decisions.

namespace
{
	// Captures LogRedHopeSim lines for the "logs this Warning" checks.
	struct FRrSuiteCapture : public FOutputDevice
	{
		TArray<FString> Lines, Warnings;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category != FName(TEXT("LogRedHopeSim")))
			{
				return;
			}
			Lines.Add(V);
			if (Verbosity == ELogVerbosity::Warning) { Warnings.Add(V); }
		}
		// Log lines reach an output device on the logging thread's schedule, so every read flushes first.
		static void Pump() { GLog->FlushThreadedLogs(); }
		int32 CountWarnings(const TCHAR* Needle) { Pump(); int32 N = 0; for (const FString& W : Warnings) { N += W.Contains(Needle) ? 1 : 0; } return N; }
		int32 CountLines(const TCHAR* Needle) { Pump(); int32 N = 0; for (const FString& L : Lines) { N += L.Contains(Needle) ? 1 : 0; } return N; }
		int32 NumWarnings() { Pump(); return Warnings.Num(); }
		void Clear() { Pump(); Lines.Reset(); Warnings.Reset(); }
	};

	// A "Res:kg" list, read by plain string work, for the suite's own reference.
	TMap<FName, int64> RrSuiteList(const FString& Text)
	{
		TMap<FName, int64> Out;
		TArray<FString> Parts;
		Text.ParseIntoArray(Parts, TEXT(";"), true);
		for (const FString& P : Parts)
		{
			int32 Colon = INDEX_NONE;
			if (P.FindChar(TEXT(':'), Colon)) { Out.Add(FName(*P.Left(Colon).TrimStartAndEnd()), FCString::Atoi64(*P.Mid(Colon + 1).TrimStartAndEnd())); }
		}
		return Out;
	}

	bool RrSameBits(double A, double B) { return FMemory::Memcmp(&A, &B, sizeof(double)) == 0; }
}

int32 RHRunRegionReadySuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== REGION-READY TEST (E7, 2026-10-30) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("REGIONREADY ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("REGIONREADY FAIL: %s"), *What); ++Fails; }
	};
	// Expect with the conditions listed, tried in order with the && chain's own short circuit; a failure says which one was the first to fail.
	const auto ExpectSeq = [&](std::initializer_list<TFunction<bool()>> L, const FString& What)
	{
		int32 Index = 0, FirstBad = 0;
		for (const TFunction<bool()>& C : L)
		{
			++Index;
			if (!C()) { FirstBad = Index; break; }
		}
		Expect(FirstBad == 0, FirstBad == 0 ? What : What + FString::Printf(TEXT(" [first failing condition: #%d]"), FirstBad));
	};
	const auto Info = [](const FString& What) { UE_LOG(LogRedHopeSim, Display, TEXT("REGIONREADY info: %s"), *What); };
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	using ELeg = URHSimWorldSubsystem::ERHLedgerLeg;
	const FName NZarya(TEXT("Zarya")), NMeridian(TEXT("Meridian"));
	const FName NStruct(TEXT("Struct")), NWater(TEXT("Water")), NOx(TEXT("Oxygen")), NHyd(TEXT("Hydrogen")), NIce(TEXT("Ice")), NFood(TEXT("Food")), NParts(TEXT("SpareParts"));
	const FName NStockpile(TEXT("Stockpile")), NLander(TEXT("Lander"));
	const auto Finish = [&]() -> int32
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== REGION-READY TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto EraOnce = [&]() { Clock->Debug_AdvanceSimSeconds(EraDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->EraStep((float)EraDt); };
	const auto AgentOnce = [&]() { Clock->Debug_AdvanceSimSeconds((double)AgentDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->Debug_StepSim(AgentDt); };
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	const auto In = [&](FName R) { return (double)Sim->GetLedgerLifetimeG(R, ELeg::In) / 1000.0 + Sim->GetLedgerOpenKg(R, ELeg::In); };
	const auto Shelf = [&](int32 Id, FName R) { const FRHBuildingInstance* B = Sim->GetBuildings().FindByPredicate([Id](const FRHBuildingInstance& X) { return X.Id == Id; }); return B ? B->OutputKg.FindRef(R) : -1.0; };
	const auto LocOf = [&](int32 Id) { const FRHBuildingInstance* B = Sim->GetBuildings().FindByPredicate([Id](const FRHBuildingInstance& X) { return X.Id == Id; }); return B ? B->LocationCm : FVector(-1.0, -1.0, -1.0); };
	const auto LanderIdOf = [&]() { for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == NLander) { return Bd.Id; } } return 0; };
	const auto Cr = [](int64 Mcr) { return URHSimWorldSubsystem::FormatCredits(Mcr); };
	FRrSuiteCapture Capture;
	GLog->AddOutputDevice(&Capture);
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	FString SaveErr;
	// Every fixture's rival rows, the market suite's shape. Retiring one re-injects it with no economy and no activity, so its entry goes at the
	// next sync and it never touches a later fixture (S3: RR4's integers assume Zarya is region 0's only Ice economy).
	const auto MakeRival = [&](FName Name, double Km, const FString& Produce, const FString& Consume, const FString& Start, const FString& Cap, const FString& ExportLot, const FString& ImportLot, int32 Region)
	{
		FRHRivalRow Row;
		Row.DisplayName = Name.ToString() + TEXT(" Post");
		Row.Nation = TEXT("Test");
		Row.DistanceKm = (float)Km;
		Row.ExportLot = ExportLot;
		Row.ImportLot = ImportLot;
		Row.RegionId = Region;
		Row.Produce = Produce; Row.Consume = Consume; Row.StoreStart = Start; Row.StoreCap = Cap;
		Row.RelationStart = 30.f;
		Row.SliceActive = true;
		DefsSub->Debug_InjectRival(Name, Row);
	};
	TArray<FName> Injected;
	const auto Inject = [&](FName Name, double Km, const FString& Produce, const FString& Consume, const FString& Start, const FString& Cap, const FString& ExportLot, const FString& ImportLot, int32 Region)
	{
		MakeRival(Name, Km, Produce, Consume, Start, Cap, ExportLot, ImportLot, Region);
		Injected.AddUnique(Name);
	};
	const auto RetireAll = [&]()
	{
		for (const FName& N : Injected)
		{
			FRHRivalRow Row;
			Row.DisplayName = N.ToString() + TEXT(" Post");
			Row.SliceActive = false;
			DefsSub->Debug_InjectRival(N, Row);
		}
		Sim->Debug_SyncRivalEconomies();
	};
	Expect(Sim->SaveColony(TEXT("rrbase"), SaveErr), FString::Printf(TEXT("RR the fresh colony is saved as the base every fixture loads (%s)"), *SaveErr));
	const auto LoadSlot = [&](const TCHAR* Slot)
	{
		FString E;
		const bool bOk = Sim->LoadColony(Slot, E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("REGIONREADY FAIL: loading %s failed: %s"), Slot, *E); ++Fails; }
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetEarthTakesQuota(true);
		Sim->Debug_SetMarketEnabled(true);
		Sim->Debug_SetMarketRivalOnly(false);
		Sim->Debug_ClearPolitics(); // E6 rebuilt it on the rule list: no politics, no sabotage
		Rejects.Reset();
		return bOk;
	};
	const auto Reset = [&]() { return LoadSlot(TEXT("rrbase")); };
	// One order through the real uplink: transmitted, then one 60 s step (the 45 s lag is over), booked or refused.
	const auto Send = [&](const TCHAR* Verb, FName Rival, const FString& Text) -> bool
	{
		const int32 Before = Sim->GetConvoys().Num(), Rej = Rejects.Num();
		FRHCommand C; C.Verb = FName(Verb); C.Target = Rival; C.Text = Text;
		Sim->EnqueueCommand(C);
		EraOnce();
		return Sim->GetConvoys().Num() == Before + 1 && Rejects.Num() == Rej;
	};
	const auto Order = [&](FName Rival, const FString& Text) { return Send(TEXT("MarketOrder"), Rival, Text); };
	const auto Provision = [&](int64 CreditMcr)
	{
		Sim->AddStock(NHyd, 600.0);
		Sim->AddStock(NParts, 30.0);
		Sim->AddStock(NIce, 3000.0);
		Sim->AddStock(NOx, 500.0); // no Water: Q1 (Struct 300, Water 200, Oxygen 50) must stay unmet, or its award lands mid-fixture
		Sim->Debug_ZeroCredits();
		Sim->Debug_GrantCredits(CreditMcr);
	};
	const auto RollNext = [&]() { Sim->Debug_RollMarket(Sim->GetMarketCheckedSol() + 1); };
	const auto HomeAll = [&]() { int32 Steps = 0; while (Sim->GetConvoys().Num() > 0 && Steps++ < 12 * (int32)(SolLen / EraDt)) { EraOnce(); } return Sim->GetConvoys().Num() == 0; };
	// The digest of everything outside the market: stocks, shelves, credits, relations, Influence, HumanNature, tension, Hope, the quota phase,
	// the ledger (the market suite's MX1 digest).
	const auto Digest = [&](bool bBandFree = false) -> FString
	{
		// bBandFree leaves out what differs between the era and the agent band before E7: Influence and Earth tension (doubles that accumulate
		// step by step) and Regolith (the era band's abstract dig, which the agent band does not run here; MX8's own band difference). RR5's
		// control twin, with no E7 setting, shows the same three differing between its bands.
		FString D = bBandFree ? FString::Printf(TEXT("sol %d prod %d bal %lld/%lld/%lld res %lld rel %.17g/%.17g hn %.17g id %.17g hope %.17g pop %d phase %d"),
			Clock->GetSol(), Sim->GetProductionCheckedSol(), Sim->GetCreditBalanceMcr(), Sim->GetCreditAwardedMcr(), Sim->GetCreditSpentMcr(), Sim->GetCreditReservedMcr(),
			Sim->GetRivalRelation(NZarya), Sim->GetRivalRelation(NMeridian), Sim->GetHumanNatureAxis(), Sim->GetIdentityAxis(), Sim->GetHopeSmoothed(), Sim->GetPopulation(), (int32)Sim->GetQuotaPhase())
			: FString::Printf(TEXT("sol %d prod %d bal %lld/%lld/%lld res %lld rel %.17g/%.17g inf %.17g hn %.17g ten %.17g id %.17g hope %.17g pop %d phase %d"),
			Clock->GetSol(), Sim->GetProductionCheckedSol(), Sim->GetCreditBalanceMcr(), Sim->GetCreditAwardedMcr(), Sim->GetCreditSpentMcr(), Sim->GetCreditReservedMcr(),
			Sim->GetRivalRelation(NZarya), Sim->GetRivalRelation(NMeridian), Sim->GetInfluence(), Sim->GetHumanNatureAxis(), Sim->GetEarthTension(), Sim->GetIdentityAxis(),
			Sim->GetHopeSmoothed(), Sim->GetPopulation(), (int32)Sim->GetQuotaPhase());
		DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow&) { if (bBandFree && N == FName(TEXT("Regolith"))) { return; } D += FString::Printf(TEXT(" %s=%.17g/%.17g|%lld/%lld"), *N.ToString(), Sim->GetStock(N), Sim->GetTotalSolid(N), Sim->GetLedgerLifetimeG(N, ELeg::Made), Sim->GetLedgerLifetimeG(N, ELeg::Used)); });
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			D += FString::Printf(TEXT(" #%d%s"), B.Id, *B.DefName.ToString());
			TArray<FName> Keys;
			for (const TMap<FName, double>* M : { &B.InputKg, &B.OutputKg })
			{
				Keys.Reset();
				M->GetKeys(Keys);
				Keys.Sort([](const FName& A, const FName& C) { return A.ToString().Compare(C.ToString(), ESearchCase::CaseSensitive) < 0; });
				D += TEXT("[");
				for (const FName& K : Keys) { if (!(bBandFree && K == FName(TEXT("Regolith")))) { D += FString::Printf(TEXT("%s:%.17g,"), *K.ToString(), M->FindRef(K)); } }
				D += TEXT("]");
			}
		}
		return D;
	};
	const auto DepotsText = [](const TArray<FRHDepot>& L) { FString T; for (const FRHDepot& D : L) { T += FString::Printf(TEXT("%s%s/%d/%d"), T.IsEmpty() ? TEXT("") : TEXT(", "), D.Owner.IsNone() ? TEXT("colony") : *D.Owner.ToString(), D.RegionId, D.BuildingId); } return T; };
	const auto RoutesText = [](const TArray<FRHRoute>& L) { FString T; for (const FRHRoute& R : L) { T += FString::Printf(TEXT("%s%s %d/%d->%d %.17g k%d"), T.IsEmpty() ? TEXT("") : TEXT(", "), *R.Rival.ToString(), R.RegionA, R.BuildingA, R.RegionB, R.LengthKm, (int32)R.Kind); } return T; };
	const auto SameDepot = [](const FRHDepot& A, const FRHDepot& B) { return A.RegionId == B.RegionId && A.Owner == B.Owner && A.BuildingId == B.BuildingId; };
	const auto SameRoute = [](const FRHRoute& A, const FRHRoute& B) { return A.Rival == B.Rival && A.RegionA == B.RegionA && A.BuildingA == B.BuildingA && A.RegionB == B.RegionB && RrSameBits(A.LengthKm, B.LengthKm) && A.Kind == B.Kind; };
	const int32 LanderId = LanderIdOf();

	// --- RR3. Depots and routes, FIRST: two rivals injected in the reverse of their name order, the first creation of those names in the process
	// (so FName index order is RrZeta before RrAlpha): the depots read the colony first, then the rivals by name string; the routes the same;
	// the market's own economy order agrees; a lower-case name sorts last (ordinal); a region-1 rival's route starts at the colony's depot. ---
	{
		const FName NZeta(TEXT("RrZeta"));   // created at runtime, here, first; never a static
		const FName NAlpha(TEXT("RrAlpha"));
		Inject(NZeta, 70.0, TEXT("Water:5"), TEXT("Water:2"), TEXT("Water:100"), TEXT("Water:900"), TEXT(""), TEXT(""), 0);
		Inject(NAlpha, 80.0, TEXT("Water:6"), TEXT("Water:3"), TEXT("Water:110"), TEXT("Water:800"), TEXT(""), TEXT(""), 0);
		TArray<FRHDepot> D1;
		TArray<FRHRoute> R1;
		Sim->GetDepots(D1);
		Sim->GetRoutes(R1);
		const TArray<FName> Want = { NMeridian, NAlpha, NZeta, NZarya };
		bool bDepots = D1.Num() == 5 && D1[0].Owner.IsNone() && D1[0].RegionId == 0 && D1[0].BuildingId == LanderId && LanderId == 1;
		bool bRoutes = R1.Num() == 4;
		bool bEcon = Sim->GetRivalEconomyCount() == 4;
		for (int32 i = 0; i < 4; ++i)
		{
			bDepots &= D1.IsValidIndex(i + 1) && D1[i + 1].Owner == Want[i] && D1[i + 1].RegionId == 0 && D1[i + 1].BuildingId == 0;
			bRoutes &= R1.IsValidIndex(i) && R1[i].Rival == Want[i] && R1[i].RegionA == 0 && R1[i].BuildingA == LanderId && R1[i].RegionB == 0 && R1[i].Kind == 0;
			bEcon &= Sim->GetRivalEconomyName(i) == Want[i];
		}
		ExpectSeq({
			[&]() { return NZeta.CompareIndexes(NAlpha) < 0; },
			[&]() { return bDepots; },
			[&]() { return bRoutes; },
			[&]() { return bEcon; },
			},
			FString::Printf(TEXT("RR3 RrZeta then RrAlpha injected (their FName index order the reverse of their name order): the depots read [%s] (the colony's first, the Lander #%d), the routes [%s], and the market's economy order agrees"), *DepotsText(D1), LanderId, *RoutesText(R1)));
		// A lower-case name sorts after every upper-case one (ordinal: 'Z' 0x5A before 'a' 0x61); a case-insensitive compare would put it first.
		const FName NLow(TEXT("aRival"));
		Inject(NLow, 90.0, TEXT("Water:4"), TEXT("Water:1"), TEXT("Water:50"), TEXT("Water:500"), TEXT(""), TEXT(""), 0);
		TArray<FRHDepot> D2;
		TArray<FRHRoute> R2;
		Sim->GetDepots(D2);
		Sim->GetRoutes(R2);
		bool bKept = D2.Num() == 6 && R2.Num() == 5;
		for (int32 i = 0; bKept && i < 5; ++i) { bKept &= SameDepot(D2[i], D1[i]); }
		for (int32 i = 0; bKept && i < 4; ++i) { bKept &= SameRoute(R2[i], R1[i]); }
		ExpectSeq({
			[&]() { return D2.Num() == 6 && D2.Last().Owner == NLow; },
			[&]() { return R2.Num() == 5 && R2.Last().Rival == NLow && R2.Last().BuildingA == LanderId; },
			[&]() { return Sim->GetRivalEconomyName(4) == NLow; },
			[&]() { return bKept; },
			},
			FString::Printf(TEXT("RR3 'aRival' sorts last in the depots and the routes and in the economy order ([%s]); every earlier record is unchanged at its index"), *DepotsText(D2)));
		// A name that sorts between: the records after it move one index on and nothing else about them changes (no record carries an Id).
		const FName NMid(TEXT("RrMid"));
		Inject(NMid, 95.0, TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("Ice:10"), TEXT("Struct:10"), 0); // barter: no economy, still a depot and a route
		TArray<FRHDepot> D3;
		TArray<FRHRoute> R3;
		Sim->GetDepots(D3);
		Sim->GetRoutes(R3);
		bool bShift = D3.Num() == 7 && R3.Num() == 6 && D3[3].Owner == NMid && R3[2].Rival == NMid && R3[2].RegionA == 0 && R3[2].BuildingA == LanderId && RrSameBits(R3[2].LengthKm, 95.0);
		for (int32 i = 0; bShift && i < 6; ++i) { bShift &= SameDepot(D3[i < 3 ? i : i + 1], D2[i]); }
		for (int32 i = 0; bShift && i < 5; ++i) { bShift &= SameRoute(R3[i < 2 ? i : i + 1], R2[i]); }
		Expect(bShift, FString::Printf(TEXT("RR3 a barter rival 'RrMid' injected between: it takes depot 3 and route 2 (no economy, still a depot and a route), the records after it move one index on, and nothing else about any record changes ([%s])"), *DepotsText(D3)));
		// A rival in region 1: its route starts at the colony's depot in the colony's region, whatever region the rival stands in.
		const FName NFar(TEXT("RrFar"));
		Inject(NFar, 90.0, TEXT("Water:7"), TEXT("Water:2"), TEXT("Water:100"), TEXT("Water:700"), TEXT(""), TEXT(""), 1);
		FRHRoute Far;
		const bool bFar = Sim->GetRouteFor(NFar, Far);
		TArray<FRHDepot> D4;
		Sim->GetDepots(D4);
		const FRHDepot* FarDepot = D4.FindByPredicate([NFar](const FRHDepot& X) { return X.Owner == NFar; });
		ExpectSeq({
			[&]() { return bFar; },
			[&]() { return Far.Rival == NFar && Far.RegionA == 0 && Far.BuildingA == LanderId && Far.RegionB == 1 && Far.Kind == 0; },
			[&]() { return RrSameBits(Far.LengthKm, 90.0); },
			[&]() { return RrSameBits(Sim->GetConvoyLegSols(NFar), 90.0 / 60.0); },
			[&]() { return FarDepot && FarDepot->RegionId == 1 && FarDepot->BuildingId == 0; },
			[&]() { return Sim->GetMarketRegionIdAt(0) == 0 && Sim->GetMarketRegionCount() == 2 && Sim->GetMarketRegionIdAt(1) == 1 && Sim->GetMarketRegionIdAt(2) == -1 && Sim->GetMarketRegionIdAt(-1) == -1; },
			[&]() { return Sim->GetRivalRegionId(NFar) == 1 && Sim->GetRivalRegionId(NZarya) == 0 && Sim->GetRivalRegionId(FName(TEXT("RrNobody"))) == 0; },
			},
			FString::Printf(TEXT("RR3 'RrFar' in region 1: its route runs from region %d's depot (#%d) to region %d, %.17g km, a leg of %.17g sols (its row's / 60); its depot stands in region 1; the markets are regions 0 and 1 in order (-1 past the end)"),
				Far.RegionA, Far.BuildingA, Far.RegionB, Far.LengthKm, Sim->GetConvoyLegSols(NFar)));
	}

	// --- RR0. The endowment data: the CSV equals DT_Endowments row for row; the default; one Warning per bad row (Debug_ClearEndowments
	// between cases, S6), ruling 12's case-sensitive resource check on a pair with no shipped row, a repeat ignoring every row of its pair, and
	// a valid injected row read through the index; the clear restores 1000; the "Definitions loaded:" line is unchanged. ---
	{
		// The CSV against the asset. Every column up to Notes is unquoted and holds no comma; Notes is the rest of the line.
		FString Csv;
		const bool bCsv = FFileHelper::LoadFileToString(Csv, *(FPaths::ProjectDir() / TEXT("docs/data/RH_Endowments.csv")));
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		const bool bHead = Lines.Num() > 0 && Lines[0] == TEXT("Name,RegionId,Resource,ProducePermille,Notes");
		int32 CsvRows = 0, CsvSame = 0, AssetRows = 0;
		for (int32 i = 1; i < Lines.Num(); ++i)
		{
			TArray<FString> Cells;
			Lines[i].ParseIntoArray(Cells, TEXT(","), false);
			if (Cells.Num() < 5) { ++CsvRows; continue; }
			FString Notes = Lines[i].Mid(Cells[0].Len() + Cells[1].Len() + Cells[2].Len() + Cells[3].Len() + 4);
			if (Notes.Len() >= 2 && Notes.StartsWith(TEXT("\"")) && Notes.EndsWith(TEXT("\""))) { Notes = Notes.Mid(1, Notes.Len() - 2).Replace(TEXT("\"\""), TEXT("\"")); }
			++CsvRows;
			bool bSame = false;
			DefsSub->ForEachEndowmentRow([&](FName Name, const FRHEndowmentRow& Row)
			{
				bSame |= Name.ToString().Equals(Cells[0], ESearchCase::CaseSensitive) && Row.RegionId == FCString::Atoi(*Cells[1]) && Row.Resource.ToString().Equals(Cells[2], ESearchCase::CaseSensitive)
					&& Row.ProducePermille == FCString::Atoi(*Cells[3]) && Row.Notes == Notes;
			});
			CsvSame += bSame ? 1 : 0;
		}
		FName OnlyName;
		FRHEndowmentRow OnlyRow;
		DefsSub->ForEachEndowmentRow([&](FName Name, const FRHEndowmentRow& Row) { ++AssetRows; OnlyName = Name; OnlyRow = Row; });
		ExpectSeq({
			[&]() { return bCsv && bHead; },
			[&]() { return CsvRows == 1 && AssetRows == 1 && CsvSame == 1; },
			[&]() { return OnlyName == FName(TEXT("R0_Ice")) && OnlyRow.RegionId == 0 && OnlyRow.Resource.ToString().Equals(TEXT("Ice"), ESearchCase::CaseSensitive) && OnlyRow.ProducePermille == 1000; },
			},
			FString::Printf(TEXT("RR0 RH_Endowments.csv equals DT_Endowments row for row, Notes included: %d CSV row(s), %d asset row(s), %d the same (R0_Ice, region 0, Ice, 1000)"), CsvRows, AssetRows, CsvSame));
		// The default: 1000 for regions 0..3 and every E1 slot (the shipped identity row and no row agree).
		const auto AllThousand = [&]()
		{
			for (int32 Region = 0; Region <= 3; ++Region)
			{
				for (int32 s = 0; s < Sim->GetLedgerSlotCount(); ++s) { if (DefsSub->GetProducePermille(Region, Sim->GetLedgerSlotName(s)) != 1000) { return false; } }
			}
			return Sim->GetLedgerSlotCount() > 10;
		};
		Expect(AllThousand(), FString::Printf(TEXT("RR0 GetProducePermille answers 1000 for regions 0..3 x every one of the %d E1 slots"), Sim->GetLedgerSlotCount()));
		// The bad rows: each, from a clear table, logs exactly one Warning and reads 1000.
		struct FBad { const TCHAR* Name; int32 Region; const TCHAR* Res; int32 Permille; const TCHAR* Needle; int32 ReadRegion; FName ReadRes; const TCHAR* What; };
		const FBad Bads[] = {
			{ TEXT("RrE_Zero"), 0, TEXT("Ice"), 0, TEXT("ProducePermille 0 is outside 1..100000"), 0, NIce, TEXT("permille 0") },
			{ TEXT("RrE_Big"), 0, TEXT("Ice"), 100001, TEXT("ProducePermille 100001 is outside 1..100000"), 0, NIce, TEXT("permille 100,001") },
			{ TEXT("RrE_Neg"), -1, TEXT("Ice"), 500, TEXT("RegionId -1 is below 0"), -1, NIce, TEXT("region -1") },
			{ TEXT("RrE_Unknown"), 0, TEXT("Unobtainium"), 500, TEXT("is no resource row"), 0, FName(TEXT("Unobtainium")), TEXT("an unknown resource") },
			// Ruling 12: 'ice' in region 1, a pair with no shipped row and a permille other than 1000. FName equality ignores case, so a build without
			// the case-sensitive compare accepts it as Ice and reads 500 with no Warning.
			{ TEXT("RrE_Case"), 1, TEXT("ice"), 500, TEXT("Resource 'ice' is no resource row"), 1, NIce, TEXT("the wrong-case resource 'ice' (region 1)") },
		};
		for (const FBad& Bad : Bads)
		{
			DefsSub->Debug_ClearEndowments();
			Capture.Clear();
			FRHEndowmentRow Row;
			Row.RegionId = Bad.Region; Row.Resource = FName(Bad.Res); Row.ProducePermille = Bad.Permille; Row.Notes = TEXT("RR0");
			DefsSub->Debug_InjectEndowmentRow(FName(Bad.Name), Row);
			const int32 Warns = Capture.NumWarnings(), Named = Capture.CountWarnings(Bad.Needle);
			const int32 Read = DefsSub->GetProducePermille(Bad.ReadRegion, Bad.ReadRes);
			ExpectSeq({
				[&]() { return Warns == 1; },
				[&]() { return Named == 1; },
				[&]() { return Read == 1000; },
				},
				FString::Printf(TEXT("RR0 an injected row with %s logs exactly one Warning (%d, naming it: %d) and reads %d (want 1000)"), Bad.What, Warns, Named, Read));
		}
		// A repeat of the shipped pair (0, Ice): one Warning naming the pair; it reads 1000.
		{
			DefsSub->Debug_ClearEndowments();
			Capture.Clear();
			FRHEndowmentRow Row;
			Row.RegionId = 0; Row.Resource = NIce; Row.ProducePermille = 500;
			DefsSub->Debug_InjectEndowmentRow(FName(TEXT("RrE_Repeat")), Row);
			const int32 Warns = Capture.NumWarnings(), Named = Capture.CountWarnings(TEXT("repeat the pair (region 0, Ice)"));
			const int32 Read = DefsSub->GetProducePermille(0, NIce);
			Expect(Warns == 1 && Named == 1 && Read == 1000, FString::Printf(TEXT("RR0 a repeat of the shipped pair (0, Ice) at 500 logs exactly one Warning naming the pair (%d, %d) and reads %d (want 1000)"), Warns, Named, Read));
		}
		// A repeat of an unshipped pair, both rows injected: the second injection logs one Warning, and NEITHER row applies.
		{
			DefsSub->Debug_ClearEndowments();
			FRHEndowmentRow A, B;
			A.RegionId = 2; A.Resource = NWater; A.ProducePermille = 700;
			B.RegionId = 2; B.Resource = NWater; B.ProducePermille = 800;
			Capture.Clear();
			DefsSub->Debug_InjectEndowmentRow(FName(TEXT("RrE_PairA")), A);
			const int32 WarnsA = Capture.NumWarnings(), ReadA = DefsSub->GetProducePermille(2, NWater);
			Capture.Clear();
			DefsSub->Debug_InjectEndowmentRow(FName(TEXT("RrE_PairB")), B);
			const int32 WarnsB = Capture.NumWarnings(), NamedB = Capture.CountWarnings(TEXT("repeat the pair (region 2, Water)"));
			const int32 ReadB = DefsSub->GetProducePermille(2, NWater);
			ExpectSeq({
				[&]() { return WarnsA == 0 && ReadA == 700; },
				[&]() { return WarnsB == 1 && NamedB == 1; },
				[&]() { return ReadB == 1000; },
				},
				FString::Printf(TEXT("RR0 a repeated unshipped pair (2, Water): the first row alone reads %d with %d Warnings; the second (800) logs one Warning naming the pair (%d) and both rows are ignored: it reads %d (want 1000)"), ReadA, WarnsA, WarnsB, ReadB));
		}
		// Ruling 12's other case: a valid injected row in an unshipped pair is read through the table index at its own permille, with no Warning.
		{
			DefsSub->Debug_ClearEndowments();
			Capture.Clear();
			FRHEndowmentRow Row;
			Row.RegionId = 3; Row.Resource = NStruct; Row.ProducePermille = 750;
			DefsSub->Debug_InjectEndowmentRow(FName(TEXT("RrE_Valid")), Row);
			const int32 Warns = Capture.NumWarnings();
			const int32 Read = DefsSub->GetProducePermille(3, NStruct), Other = DefsSub->GetProducePermille(3, NIce), Region0 = DefsSub->GetProducePermille(0, NStruct);
			Expect(Warns == 0 && Read == 750 && Other == 1000 && Region0 == 1000, FString::Printf(TEXT("RR0 a valid injected row (3, Struct, 750) logs no Warning (%d) and reads %d through the index; (3, Ice) reads %d and (0, Struct) %d"), Warns, Read, Other, Region0));
			DefsSub->Debug_ClearEndowments();
			Expect(DefsSub->GetProducePermille(3, NStruct) == 1000 && AllThousand(), TEXT("RR0 Debug_ClearEndowments restores 1000 everywhere (regions 0..3 x every E1 slot)"));
		}
		// The "Definitions loaded:" line (Initialize, before this suite) lists exactly the nine counts it always did: no endowment count.
		GLog->Flush();
		FString LogText;
		const bool bLog = FFileHelper::LoadFileToString(LogText, *FPlatformOutputDevices::GetAbsoluteLogFilename());
		TArray<FString> LogLines;
		LogText.ParseIntoArrayLines(LogLines);
		int32 Found = 0;
		bool bShape = true;
		for (const FString& L : LogLines)
		{
			const int32 At = L.Find(TEXT("LogRedHopeSim: Definitions loaded: "), ESearchCase::CaseSensitive);
			if (At == INDEX_NONE) { continue; }
			++Found;
			TArray<FString> Pairs;
			L.Mid(At + 35).ParseIntoArray(Pairs, TEXT(" "), true);
			const TArray<FString> Keys = { TEXT("buildings"), TEXT("robots"), TEXT("recipes"), TEXT("deposits"), TEXT("quotas"), TEXT("config"), TEXT("events"), TEXT("rooms"), TEXT("solarCurve") };
			bShape &= Pairs.Num() == Keys.Num();
			for (int32 k = 0; bShape && k < Keys.Num(); ++k) { FString Key, Val; bShape &= Pairs[k].Split(TEXT("="), &Key, &Val) && Key == Keys[k]; }
		}
		Expect(bLog && Found == 1 && bShape, FString::Printf(TEXT("RR0 the \"Definitions loaded:\" line is unchanged: found %d time(s) in this process's log, with exactly its nine counts (buildings ... solarCurve) and no endowment count"), Found));
	}

	// --- RR1. Inertness, exact: for Zarya, Meridian, a barter rival in Greed's shape (injected: there is no shipped Greed row, S10) and rows at
	// 30 km and 33.33333 km, the route and the leg are HEAD's doubles bit for bit; an unknown rival has none; the colony's depot is the Lander;
	// every economy rival makes its row's kg x 1000; every slot's anchor, floor and ceiling are the Earth value x {1, 0.25, 4}. ---
	{
		const FRHRivalRow* Zr = DefsSub->GetRival(NZarya);
		const FName NGreed(TEXT("RrGreed")), NThirty(TEXT("RrThirty")), NThird(TEXT("RrThird"));
		Inject(NGreed, 40.0, Zr ? Zr->Produce : FString(), Zr ? Zr->Consume : FString(), Zr ? Zr->StoreStart : FString(), Zr ? Zr->StoreCap : FString(), TEXT("Ice:10"), TEXT("SpareParts:4"), 0);
		Inject(NThirty, 30.0, TEXT("Water:3"), TEXT("Water:1"), TEXT("Water:30"), TEXT("Water:300"), TEXT(""), TEXT(""), 0);
		Inject(NThird, 33.33333, TEXT("Water:3"), TEXT("Water:1"), TEXT("Water:30"), TEXT("Water:300"), TEXT(""), TEXT(""), 0);
		struct FLeg { FName Rival; double Want; };
		const FLeg Legs[] = { { NZarya, 2.0 }, { NMeridian, 3.3333333333333335 }, { NGreed, 40.0 / 60.0 }, { NThirty, 0.5 }, { NThird, 0.5555554707845052 } };
		for (const FLeg& G : Legs)
		{
			const FRHRivalRow* Row = DefsSub->GetRival(G.Rival);
			FRHRoute R;
			const bool bRoute = Sim->GetRouteFor(G.Rival, R);
			const double Mine = Row ? Row->DistanceKm / FMath::Max(60.0, (double)KINDA_SMALL_NUMBER) : -1.0;
			const double Leg = Sim->GetConvoyLegSols(G.Rival);
			ExpectSeq({
				[&]() { return Row && bRoute; },
				[&]() { return R.Rival == G.Rival && R.RegionA == 0 && R.BuildingA == LanderId && R.RegionB == Row->RegionId && R.Kind == 0; },
				[&]() { return RrSameBits(R.LengthKm, (double)Row->DistanceKm); },
				[&]() { return RrSameBits(Leg, Mine); },
				[&]() { return RrSameBits(Leg, G.Want); },
				[&]() { return Sim->GetRivalTradeKind(NGreed) == URHSimWorldSubsystem::ERHRivalTrade::LotsOutsideCap; },
				},
				FString::Printf(TEXT("RR1 %s: the route runs region 0 #%d -> region %d, %.17g km == (double) the row's %.9g bit for bit, and the leg is %.17g sols, bit for bit the row's distance / 60 computed here (%.17g)"),
					*G.Rival.ToString(), R.BuildingA, R.RegionB, R.LengthKm, Row ? Row->DistanceKm : -1.f, Leg, Mine));
		}
		FRHRoute None;
		const bool bNone = Sim->GetRouteFor(FName(TEXT("RrNobody")), None);
		ExpectSeq({
			[&]() { return !bNone; },
			[&]() { return RrSameBits(Sim->GetConvoyLegSols(FName(TEXT("RrNobody"))), 0.0); },
			[&]() { return Sim->FindColonyDepotBuildingId(0) == LanderId && LanderId == 1; },
			[&]() { return Sim->GetColonyDepotBindingId(0) == 0; },
			[&]() { return Sim->GetColonyDepotLocationCm(0) == LocOf(LanderId); },
			[&]() { return Sim->GetColonyRegionId() == 0; },
			},
			FString::Printf(TEXT("RR1 an unknown rival has no route and a leg of 0.0; the colony's depot is the Lander (#%d, no binding, at %s)"), Sim->FindColonyDepotBuildingId(0), *Sim->GetColonyDepotLocationCm(0).ToString()));
		// Production at the identity: every economy rival's every traded resource is its row's kg x 1000 (0 where it makes none).
		int32 Pairs = 0, Bad = 0;
		FString BadText;
		for (int32 r = 0; r < Sim->GetRivalEconomyCount(); ++r)
		{
			const FName Name = Sim->GetRivalEconomyName(r);
			const FRHRivalRow* Row = DefsSub->GetRival(Name);
			const TMap<FName, int64> Prod = RrSuiteList(Row ? Row->Produce : FString());
			TArray<FName> Traded;
			Sim->GetRivalTradedResources(Name, Traded);
			for (const FName& Res : Traded)
			{
				++Pairs;
				const int64 Want = Prod.FindRef(Res) * 1000, Got = Sim->GetRivalProduceG(Name, Res);
				if (Got != Want) { ++Bad; BadText += FString::Printf(TEXT(" %s.%s %lld (want %lld)"), *Name.ToString(), *Res.ToString(), Got, Want); }
			}
		}
		Expect(Pairs >= 10 && Bad == 0 && Sim->GetRivalProduceG(FName(TEXT("RrNobody")), NIce) == -1 && Sim->GetRivalProduceG(NGreed, NIce) == -1,
			FString::Printf(TEXT("RR1 every economy rival (%d) makes its row's kg x 1000 of each traded resource (%d pairs, %d off%s); a rival with no economy entry reads -1"), Sim->GetRivalEconomyCount(), Pairs, Bad, *BadText));
		// The band: every slot's anchor, floor and ceiling in region 0 are the Earth value x {1, 0.25, 4}.
		int32 Slots = 0, BandBad = 0;
		for (int32 s = 0; s < Sim->GetLedgerSlotCount(); ++s)
		{
			const FName Res = Sim->GetLedgerSlotName(s);
			const FRHResourceRow* RowR = DefsSub->GetResource(Res);
			const int64 V = RowR ? (int64)FMath::Max(0, RowR->EarthValue_mcr) : 0;
			++Slots;
			BandBad += (Sim->GetMarketAnchorMcr(Res) == V && Sim->GetMarketFloorMcr(Res) == V / 4 && Sim->GetMarketCeilMcr(Res) == V * 4) ? 0 : 1;
		}
		ExpectSeq({
			[&]() { return Slots > 10 && BandBad == 0; },
			[&]() { return Sim->GetMarketAnchorMcr(NIce) == 3000 && Sim->GetMarketFloorMcr(NIce) == 750 && Sim->GetMarketCeilMcr(NIce) == 12000; },
			[&]() { return Sim->GetMarketAnchorMcr(NIce, 7) == 0 && Sim->GetMarketFloorMcr(NIce, 7) == 0 && Sim->GetMarketCeilMcr(NIce, 7) == 0 && Sim->GetMarketProdSumG(NIce, 7) == 0; },
			},
			FString::Printf(TEXT("RR1 every one of the %d slots' anchor, floor and ceiling is its Earth value x {1, 0.25, 4} (%d off; Ice %lld / %lld / %lld); a region with no market reads 0"), Slots, BandBad,
				Sim->GetMarketAnchorMcr(NIce), Sim->GetMarketFloorMcr(NIce), Sim->GetMarketCeilMcr(NIce)));
	}

	// --- RR2. Landing follows the depot: with a stockpile bound by the hook, a standing trade's goods, a written order's filled solids and a
	// clean covert run's loot land on that stockpile's shelf and none on the Lander's; fluids join the pool; E1's In legs equal a twin's that
	// lands at the Lander. Then the binding's refusals, the clear, and a demolished depot handing the role back. ---
	{
		struct FLand
		{
			bool bBound = false, bPlaced = false, bStanding = false, bOrder = false, bHome1 = false, bHome2 = false, bClean = false;
			int32 StockId = 0, Attempts = 0;
			double Stock[3] = { 0.0, 0.0, 0.0 }, Lander[3] = { 0.0, 0.0, 0.0 }, LedgerIce[3] = { 0.0, 0.0, 0.0 }, Water = 0.0, WaterShelves = 0.0, InIce = 0.0, InWater = 0.0;
		};
		const auto RunLanding = [&](bool bBind) -> FLand
		{
			FLand O;
			Reset();
			Provision(50000000);
			Sim->Debug_PlaceInstant(NStockpile, FVector(0.0, -1500.0, 0.0));
			O.bPlaced = Sim->GetBuildings().Num() > 0 && Sim->GetBuildings().Last().DefName == NStockpile && !Sim->GetBuildings().Last().bUnderConstruction;
			O.StockId = O.bPlaced ? Sim->GetBuildings().Last().Id : 0; // Debug_PlaceInstant returns void; buildings are Id-ascending (S11)
			O.bBound = !bBind || Sim->Debug_SetColonyDepot(0, O.StockId);
			// 1. The standing trade (Zarya: Struct:100 for Ice:150).
			double S0 = Shelf(O.StockId, NIce), L0 = Shelf(LanderId, NIce), I0 = In(NIce);
			O.bStanding = Send(TEXT("Convoy"), NZarya, FString());
			O.bHome1 = HomeAll();
			O.Stock[0] = Shelf(O.StockId, NIce) - S0; O.Lander[0] = Shelf(LanderId, NIce) - L0; O.LedgerIce[0] = In(NIce) - I0;
			// 2. A written order: solids and a fluid.
			S0 = Shelf(O.StockId, NIce); L0 = Shelf(LanderId, NIce); I0 = In(NIce);
			const double W0 = Sim->GetStock(NWater);
			O.bOrder = Order(NZarya, TEXT("Ice:200;Water:50"));
			O.bHome2 = HomeAll();
			O.Stock[1] = Shelf(O.StockId, NIce) - S0; O.Lander[1] = Shelf(LanderId, NIce) - L0; O.LedgerIce[1] = In(NIce) - I0;
			O.Water = Sim->GetStock(NWater) - W0;
			O.WaterShelves = FMath::Max(0.0, Shelf(O.StockId, NWater)) + FMath::Max(0.0, Shelf(LanderId, NWater));
			// 3. A covert run on Zarya through the uplink, repeated until one comes back clean (the roll is deterministic from the attempt count).
			S0 = Shelf(O.StockId, NIce); L0 = Shelf(LanderId, NIce); I0 = In(NIce);
			for (int32 K = 0; K < 8 && !O.bClean; ++K)
			{
				Capture.Clear();
				FRHCommand C; C.Verb = FName(TEXT("Covert")); C.Target = NZarya;
				Sim->EnqueueCommand(C);
				EraOnce();
				++O.Attempts;
				O.bClean = Capture.CountLines(TEXT("=== COVERT CLEAN: Zarya Station")) == 1;
			}
			O.Stock[2] = Shelf(O.StockId, NIce) - S0; O.Lander[2] = Shelf(LanderId, NIce) - L0; O.LedgerIce[2] = In(NIce) - I0;
			O.InIce = In(NIce);
			O.InWater = In(NWater);
			return O;
		};
		const FLand Lander = RunLanding(false);
		const FLand Bound = RunLanding(true);
		const TCHAR* Phases[3] = { TEXT("the standing trade's Ice"), TEXT("the written order's filled Ice"), TEXT("the covert run's loot") };
		for (int32 p = 0; p < 3; ++p)
		{
			ExpectSeq({
				[&]() { return Bound.bPlaced && Bound.bBound && Bound.bStanding && Bound.bOrder && Bound.bHome1 && Bound.bHome2 && Bound.bClean; },
				[&]() { return Lander.bPlaced && Lander.bStanding && Lander.bOrder && Lander.bHome1 && Lander.bHome2 && Lander.bClean && Lander.Attempts == Bound.Attempts; },
				[&]() { return Bound.Stock[p] > 0.0 && Bound.Stock[p] == Bound.LedgerIce[p]; },
				[&]() { return Bound.Lander[p] == 0.0; },
				[&]() { return Lander.Lander[p] == Bound.Stock[p] && Lander.Stock[p] == 0.0; },
				[&]() { return Lander.LedgerIce[p] == Bound.LedgerIce[p]; },
				},
				FString::Printf(TEXT("RR2 %s lands on the bound stockpile #%d's shelf (+%.3f kg, the ledger's In +%.3f) and none on the Lander's (%+.3f); the twin with no binding lands the same %.3f kg at the Lander (stockpile %+.3f)"),
					Phases[p], Bound.StockId, Bound.Stock[p], Bound.LedgerIce[p], Bound.Lander[p], Lander.Lander[p], Lander.Stock[p]));
		}
		ExpectSeq({
			[&]() { return Bound.Water == 50.0 && Lander.Water == 50.0; },
			[&]() { return Bound.WaterShelves == 0.0 && Lander.WaterShelves == 0.0; },
			[&]() { return Bound.InIce == Lander.InIce && Bound.InWater == Lander.InWater; },
			[&]() { return Bound.Stock[0] == 150.0 && Bound.Stock[1] == 200.0; },
			},
			FString::Printf(TEXT("RR2 the order's 50 kg of Water joins the pool in both twins (%.3f / %.3f, none on a shelf), and E1's In legs equal the Lander twin's (Ice %.3f / %.3f kg, Water %.3f / %.3f kg)"),
				Bound.Water, Lander.Water, Bound.InIce, Lander.InIce, Bound.InWater, Lander.InWater));
		// The binding (the bound twin's colony is the live one).
		const int32 StockId = Bound.StockId;
		const bool bSpot = Sim->GetColonyDepotLocationCm(0) == LocOf(StockId) && Sim->FindColonyDepotBuildingId(0) == StockId && Sim->GetColonyDepotBindingId(0) == StockId;
		const bool bMissing = !Sim->Debug_SetColonyDepot(0, 999) && Sim->FindColonyDepotBuildingId(0) == StockId;
		const bool bRegion = !Sim->Debug_SetColonyDepot(1, StockId) && Sim->GetColonyDepotBindingId(1) == 0 && Sim->FindColonyDepotBuildingId(0) == StockId;
		FRHCommand Build; Build.Verb = FName(TEXT("Build")); Build.Target = NStockpile; Build.Location = FVector(0.0, -3000.0, 0.0); Build.Level = 0;
		Sim->EnqueueCommand(Build);
		EraOnce();
		const FRHBuildingInstance& Site = Sim->GetBuildings().Last();
		const int32 SiteId = Site.Id;
		const bool bSite = SiteId != StockId && Site.DefName == NStockpile && Site.bUnderConstruction;
		const bool bUnbuilt = bSite && !Sim->Debug_SetColonyDepot(0, SiteId) && Sim->FindColonyDepotBuildingId(0) == StockId;
		const bool bClear = Sim->Debug_SetColonyDepot(0, 0) && Sim->GetColonyDepotBindingId(0) == 0 && Sim->FindColonyDepotBuildingId(0) == LanderId && Sim->GetColonyDepotLocationCm(0) == LocOf(LanderId);
		ExpectSeq({
			[&]() { return bSpot; },
			[&]() { return bMissing; },
			[&]() { return bRegion; },
			[&]() { return bSite; },
			[&]() { return bUnbuilt; },
			[&]() { return bClear; },
			},
			FString::Printf(TEXT("RR2 the bound depot's spot is the stockpile's (%s); a binding to building 999, to region 1, or to a stockpile still under construction (#%d) is refused and the rule stands; Debug_SetColonyDepot(0, 0) returns the role to the Lander (#%d)"),
				*LocOf(StockId).ToString(), SiteId, Sim->FindColonyDepotBuildingId(0)));
		// Demolishing the bound depot hands the role back at the next read (the binding is left naming a building that is gone).
		const bool bRebound = Sim->Debug_SetColonyDepot(0, StockId) && Sim->FindColonyDepotBuildingId(0) == StockId;
		FRHCommand Demolish; Demolish.Verb = FName(TEXT("Demolish")); Demolish.Target = FName(TEXT("Building")); Demolish.Value = (double)StockId;
		Sim->EnqueueCommand(Demolish);
		EraOnce();
		const bool bGone = !Sim->GetBuildings().ContainsByPredicate([StockId](const FRHBuildingInstance& X) { return X.Id == StockId; });
		ExpectSeq({
			[&]() { return bRebound; },
			[&]() { return bGone; },
			[&]() { return Sim->GetColonyDepotBindingId(0) == StockId; },
			[&]() { return Sim->FindColonyDepotBuildingId(0) == LanderId; },
			[&]() { return Sim->GetColonyDepotLocationCm(0) == LocOf(LanderId); },
			},
			FString::Printf(TEXT("RR2 the bound stockpile #%d demolished: the binding still names it, and the depot is the Lander (#%d) again at the next read"), StockId, Sim->FindColonyDepotBuildingId(0)));
	}

	// Every rival injected so far is retired before RR4 (S3): their region-0 economies would change Ice's sums, fill and store.
	RetireAll();
	Reset();
	Info(FString::Printf(TEXT("every injected rival retired (%d): the economy rivals are now %d (%s, %s), one market"), Injected.Num(), Sim->GetRivalEconomyCount(),
		*Sim->GetRivalEconomyName(0).ToString(), *Sim->GetRivalEconomyName(1).ToString()));

	// --- RR4. The endowment by hand, from MX2's Set A Ice state (Zarya's Ice store 12,000 kg, the tally 0, the price 3,000), one roll each from a
	// fresh Reset: production scales, consumption does not (the store is 12,000,000 + ProduceG - 150,000 g), the band and E3's Earth price stand;
	// then the restore check: a set and a clear with no roll between leave the save byte-identical. ---
	{
		DefsSub->Debug_ClearEndowments();
		const int64 Earth0 = Sim->GetEarthPriceMcrPerKg(NIce);
		struct FCase { FName Res; int32 Permille; int64 WantProduce, WantSum, WantStoreKg, WantPrice; };
		const FCase Cases[] = {
			{ NIce, 500, 200000, 200000, 12050, 2921 },
			{ NIce, 2000, 800000, 800000, 12650, 2850 },
			{ NIce, 43, 17200, 17200, 11867, 3150 },
			{ NIce, 1000, 400000, 400000, 12250, 2850 }, // the control: MX2's own figure
			{ NIce, 9, 3600, 3600, -1, -1 },
			{ NStruct, 43, 1720, -1, -1, -1 },           // Zarya makes 40 kg of Struct a sol
		};
		for (const FCase& K : Cases)
		{
			Reset();
			DefsSub->Debug_ClearEndowments();
			Sim->Debug_SetRivalStore(NZarya, NIce, 12000);
			Sim->Debug_SetMarketTally(NIce, 0, 0);
			Sim->Debug_SetMarketPrice(NIce, 3000);
			const bool bSet = DefsSub->Debug_SetEndowment(0, K.Res, K.Permille);
			const int64 Consume0 = 150000; // Zarya's Ice Consume, 150 kg, untouched
			RollNext();
			const int64 Produce = Sim->GetRivalProduceG(NZarya, K.Res), Sum = Sim->GetMarketProdSumG(K.Res), StoreG = Sim->GetRivalStoreG(NZarya, NIce);
			const int64 StoreKg = Sim->GetRivalStoreKg(NZarya, NIce), Price = Sim->GetMarketPriceMcr(NIce);
			const int64 IceProduce = Sim->GetRivalProduceG(NZarya, NIce);
			ExpectSeq({
				[&]() { return bSet; },
				[&]() { return Produce == K.WantProduce; },
				[&]() { return K.WantSum < 0 || Sum == K.WantSum; },
				[&]() { return K.WantStoreKg < 0 || StoreKg == K.WantStoreKg; },
				[&]() { return K.WantPrice < 0 || Price == K.WantPrice; },
				[&]() { return Sim->GetMarketAnchorMcr(NIce) == 3000 && Sim->GetMarketFloorMcr(NIce) == 750 && Sim->GetMarketCeilMcr(NIce) == 12000; },
				[&]() { return Sim->GetMarketAnchorMcr(NStruct) == 5000 && Sim->GetMarketFloorMcr(NStruct) == 1250 && Sim->GetMarketCeilMcr(NStruct) == 20000; },
				[&]() { return Sim->GetEarthPriceMcrPerKg(NIce) == Earth0; },
				},
				FString::Printf(TEXT("RR4 (0, %s, %d): Zarya's %s ProduceG %lld (want %lld), the market's ProdSumG %lld; after one roll Ice's store is %lld kg (want %lld) and its price %lld mcr (want %lld); the band stands (Ice 3,000 / 750 / 12,000, Struct 5,000 / 1,250 / 20,000) and E3's Earth price for Ice is %lld, as before"),
					*K.Res.ToString(), K.Permille, *K.Res.ToString(), Produce, K.WantProduce, Sum, StoreKg, K.WantStoreKg, Price, K.WantPrice, Sim->GetEarthPriceMcrPerKg(NIce)));
			// Consume, its own pin: the store moved by the production read back and the row's 150 kg of consumption exactly, whatever the permille.
			Expect(StoreG == 12000000 + IceProduce - Consume0, FString::Printf(TEXT("RR4 (0, %s, %d) Consume untouched: Zarya's Ice store after the roll is %lld g = 12,000,000 + ProduceG %lld - 150,000 (%lld kg)"),
				*K.Res.ToString(), K.Permille, StoreG, IceProduce, StoreKg));
		}
		// A row for region 1 changes no region-0 figure: the control's numbers exactly.
		{
			Reset();
			DefsSub->Debug_ClearEndowments();
			Sim->Debug_SetRivalStore(NZarya, NIce, 12000);
			Sim->Debug_SetMarketTally(NIce, 0, 0);
			Sim->Debug_SetMarketPrice(NIce, 3000);
			const bool bSet = DefsSub->Debug_SetEndowment(1, NIce, 500) && DefsSub->Debug_SetEndowment(1, NStruct, 2000);
			RollNext();
			Expect(bSet && Sim->GetRivalProduceG(NZarya, NIce) == 400000 && Sim->GetRivalProduceG(NZarya, NStruct) == 40000 && Sim->GetMarketProdSumG(NIce) == 400000
				&& Sim->GetRivalStoreKg(NZarya, NIce) == 12250 && Sim->GetMarketPriceMcr(NIce) == 2850,
				FString::Printf(TEXT("RR4 endowments for region 1 (Ice 500, Struct 2,000) change no region-0 figure: Zarya's Ice %lld g and Struct %lld g a sol, the store %d kg, the price %lld (the control's)"),
					Sim->GetRivalProduceG(NZarya, NIce), Sim->GetRivalProduceG(NZarya, NStruct), Sim->GetRivalStoreKg(NZarya, NIce), Sim->GetMarketPriceMcr(NIce)));
		}
		// The restore check (REVISION 3 section 4): save, set (0, Ice, 500) and read it, clear, save; no roll or step between.
		{
			Reset();
			DefsSub->Debug_ClearEndowments();
			Sim->Debug_SetRivalStore(NZarya, NIce, 12000);
			Sim->Debug_SetMarketTally(NIce, 0, 0);
			Sim->Debug_SetMarketPrice(NIce, 3000);
			const auto Kept = [&]()
			{
				FString T;
				for (int32 s = 0; s < Sim->GetLedgerSlotCount(); ++s)
				{
					const FName Res = Sim->GetLedgerSlotName(s);
					T += FString::Printf(TEXT("%s %lld %lld %lld; "), *Res.ToString(), Sim->GetMarketPriceMcr(Res), Sim->GetRivalStoreG(NZarya, Res), Sim->GetRivalStoreG(NMeridian, Res));
				}
				return T;
			};
			const FString Kept0 = Kept();
			FString ErrA, ErrB;
			const bool bA = Sim->SaveColony(TEXT("rrmx4a"), ErrA);
			const bool bSet = DefsSub->Debug_SetEndowment(0, NIce, 500);
			const int64 Mid = Sim->GetRivalProduceG(NZarya, NIce); // the read resyncs at 500
			const FString KeptMid = Kept();
			DefsSub->Debug_ClearEndowments();
			const bool bB = Sim->SaveColony(TEXT("rrmx4b"), ErrB);
			TArray<uint8> A, B;
			FFileHelper::LoadFileToArray(A, *SaveFilePath(TEXT("rrmx4a")));
			FFileHelper::LoadFileToArray(B, *SaveFilePath(TEXT("rrmx4b")));
			const bool bSame = A.Num() > 0 && A.Num() == B.Num() && FMemory::Memcmp(A.GetData(), B.GetData(), A.Num()) == 0;
			// The diagnostic: where the bytes differ, if they do (the market frame, or outside it).
			int32 HeadA = INDEX_NONE, HeadB = INDEX_NONE, PayA = -1, PayB = -2;
			const bool bFoundA = RHSaveFrame::Find(A, RHSaveFrame::MarketTag, HeadA, PayA), bFoundB = RHSaveFrame::Find(B, RHSaveFrame::MarketTag, HeadB, PayB);
			const int32 Span = RHSaveFrame::HeaderBytes + PayA + RHSaveFrame::FooterBytes;
			const bool bFrame = bFoundA && bFoundB && PayA == PayB && PayA == Sim->GetMarketBlockBytes() && HeadA == HeadB && HeadA + Span <= A.Num() && HeadB + Span <= B.Num()
				&& FMemory::Memcmp(A.GetData() + HeadA, B.GetData() + HeadB, Span) == 0;
			bool bBack = true;
			for (int32 r = 0; r < Sim->GetRivalEconomyCount(); ++r)
			{
				const FName Name = Sim->GetRivalEconomyName(r);
				const TMap<FName, int64> Prod = RrSuiteList(DefsSub->GetRival(Name) ? DefsSub->GetRival(Name)->Produce : FString());
				TArray<FName> Traded;
				Sim->GetRivalTradedResources(Name, Traded);
				for (const FName& Res : Traded) { bBack &= Sim->GetRivalProduceG(Name, Res) == Prod.FindRef(Res) * 1000; }
			}
			ExpectSeq({
				[&]() { return bA && bB && bSet; },
				[&]() { return Mid == 200000; },
				[&]() { return KeptMid == Kept0; },
				[&]() { return bFrame; },
				[&]() { return bSame; },
				[&]() { return bBack; },
				},
				FString::Printf(TEXT("RR4 the restore check: saved, then (0, Ice, 500) set (Zarya's Ice reads %lld g a sol after the resync, and no price or store moves), cleared and saved again with no roll or step between: the two files are byte-identical, the market frame found in both equal in its payload (== GetMarketBlockBytes) and every byte, and every rival's production is back at its row's kg x 1000"), Mid));
			if (!bSame || !bFrame)
			{
				Info(FString::Printf(TEXT("RR4 restore diagnostic: frames found %d/%d, payloads equal %d, header at the same offset %d"), bFoundA ? 1 : 0, bFoundB ? 1 : 0, PayA == PayB ? 1 : 0, HeadA == HeadB ? 1 : 0));
			}
		}
		DefsSub->Debug_ClearEndowments();
	}

	// --- RR5. Era parity: a paired 10-sol window from one start save, the era band against the agent band, with a stockpile bound as the depot
	// and (0, Ice, 500) set before the window in both twins and the same orders sent at the same sim times: the landing building, the landed
	// kg, every price and rival store at every threshold, the balance and the digest of everything outside the market are identical. ---
	{
		Reset();
		Provision(50000000);
		const FName NKes(TEXT("RrKes"));
		Inject(NKes, 30.0, TEXT("Ice:100"), TEXT(""), TEXT("Ice:1000"), TEXT("Ice:5000"), TEXT(""), TEXT(""), 0);
		Sim->Debug_SyncRivalEconomies();
		Sim->Debug_PlaceInstant(NStockpile, FVector(0.0, -1500.0, 0.0));
		const int32 StockId = Sim->GetBuildings().Last().Id;
		Clock->Debug_SetSimSeconds(20.0 * SolLen - EraDt);
		EraOnce(); // the step ends at exactly sol 20.0 (Storm_1 is over)
		FString Err;
		const bool bSaved = Sim->SaveColony(TEXT("rrmx5"), Err);
		const bool bEndow = DefsSub->Debug_SetEndowment(0, NIce, 500);
		const double WindowStart = 20.0 * SolLen;
		const auto AtSol = [&](double Sol) { return WindowStart + Sol * SolLen; };
		struct FSend { double T; const TCHAR* Verb; FName Rival; FString Text; };
		const TArray<FSend> Sends = {
			{ AtSol(0.5), TEXT("Convoy"), NZarya, FString() },
			{ AtSol(1.5), TEXT("MarketOrder"), NKes, TEXT("Ice:100") },
			{ AtSol(3.5), TEXT("MarketOrder"), NKes, TEXT("Ice:150") },
			{ AtSol(5.5), TEXT("MarketOrder"), NZarya, TEXT("Ice:200;Water:50") },
			{ AtSol(7.3), TEXT("Covert"), NZarya, FString() },
		};
		struct FRun { FString Rolls, Returns; FString DigestText, Drift; int64 Balance = 0; int32 Depot = 0, Booked = 0; double StockIce = 0.0, LanderIce = 0.0; bool bBound = false; };
		const auto RunWindow = [&](bool bAgent, bool bE7) -> FRun
		{
			FRun O;
			LoadSlot(TEXT("rrmx5"));
			O.bBound = !bE7 || Sim->Debug_SetColonyDepot(0, StockId); // runtime only: set after the load, before the window
			const double Stock0 = Shelf(StockId, NIce), Lander0 = Shelf(LanderId, NIce);
			int32 NextSend = 0, LastChecked = Sim->GetMarketCheckedSol();
			TArray<int32> PrevIds;
			const double EndT = AtSol(10.0);
			while (Clock->GetSimSecondsTotal() < EndT - 1.0e-6)
			{
				const double Now = Clock->GetSimSecondsTotal();
				while (NextSend < Sends.Num() && Now >= Sends[NextSend].T)
				{
					FRHCommand C; C.Verb = FName(Sends[NextSend].Verb); C.Target = Sends[NextSend].Rival; C.Text = Sends[NextSend].Text;
					Sim->EnqueueCommand(C);
					++NextSend;
				}
				if (bAgent) { AgentOnce(); } else { EraOnce(); }
				const TArray<FRHConvoy>& Cv = Sim->GetConvoys();
				bool bSameIds = Cv.Num() == PrevIds.Num();
				for (int32 i = 0; bSameIds && i < Cv.Num(); ++i) { bSameIds &= Cv[i].Id == PrevIds[i]; }
				if (!bSameIds)
				{
					for (const FRHConvoy& C : Cv) { O.Booked += PrevIds.Contains(C.Id) ? 0 : 1; }
					for (const int32 Id : PrevIds)
					{
						if (!Cv.ContainsByPredicate([Id](const FRHConvoy& C) { return C.Id == Id; }))
						{
							O.Returns += FString::Printf(TEXT("#%d home in sol %d at #%d; "), Id, FMath::FloorToInt32(Clock->GetSimSecondsTotal() / SolLen), Sim->FindColonyDepotBuildingId(0));
						}
					}
					PrevIds.Reset();
					for (const FRHConvoy& C : Cv) { PrevIds.Add(C.Id); }
				}
				if (Sim->GetMarketCheckedSol() != LastChecked)
				{
					LastChecked = Sim->GetMarketCheckedSol();
					O.Rolls += FString::Printf(TEXT("[sol %d"), LastChecked);
					for (const FName R : { NIce, NWater, NOx, NStruct, NHyd, NFood, NParts }) { O.Rolls += FString::Printf(TEXT(" %s=%lld"), *R.ToString(), Sim->GetMarketPriceMcr(R)); }
					for (const FName R : { NIce, NWater, NStruct }) { O.Rolls += FString::Printf(TEXT(" Z.%s=%lld"), *R.ToString(), Sim->GetRivalStoreG(NZarya, R)); }
					O.Rolls += FString::Printf(TEXT(" K.Ice=%lld P=%lld/%lld bal %lld]\n"), Sim->GetRivalStoreG(NKes, NIce), Sim->GetRivalProduceG(NZarya, NIce), Sim->GetRivalProduceG(NKes, NIce), Sim->GetCreditBalanceMcr());
				}
			}
			O.Depot = Sim->FindColonyDepotBuildingId(0);
			O.StockIce = Shelf(StockId, NIce) - Stock0;
			O.LanderIce = Shelf(LanderId, NIce) - Lander0;
			O.Balance = Sim->GetCreditBalanceMcr();
			O.DigestText = Digest(true);
			O.Drift = FString::Printf(TEXT("inf %.17g ten %.17g Regolith %.17g/%.17g|%lld"), Sim->GetInfluence(), Sim->GetEarthTension(), Sim->GetStock(FName(TEXT("Regolith"))), Sim->GetTotalSolid(FName(TEXT("Regolith"))),
				Sim->GetLedgerLifetimeG(FName(TEXT("Regolith")), ELeg::Made));
			return O;
		};
		const FRun Era = RunWindow(false, true);
		const FRun Agent = RunWindow(true, true);
		DefsSub->Debug_ClearEndowments();
		// The control: the same window with no binding and no endowment. Its bands differ in the same three things and agree on the rest, so
		// that difference is HEAD's, not E7's.
		const FRun EraControl = RunWindow(false, false);
		const FRun AgentControl = RunWindow(true, false);
		const auto FirstDiff = [](const FString& A, const FString& B) -> FString
		{
			for (int32 i = 0; i < FMath::Max(A.Len(), B.Len()); ++i)
			{
				if (i >= A.Len() || i >= B.Len() || A[i] != B[i]) { return FString::Printf(TEXT("at char %d: '%s' | '%s'"), i, *A.Mid(FMath::Max(0, i - 40), 120), *B.Mid(FMath::Max(0, i - 40), 120)); }
			}
			return FString(TEXT("(equal)"));
		};
		ExpectSeq({
			[&]() { return bSaved && bEndow && Era.bBound && Agent.bBound; },
			[&]() { return Era.Booked == 4 && Agent.Booked == 4; },
			[&]() { return Era.Depot == StockId && Agent.Depot == StockId; },
			[&]() { return Era.StockIce > 0.0 && Era.StockIce == Agent.StockIce; },
			[&]() { return Era.LanderIce == 0.0 && Agent.LanderIce == 0.0; },
			[&]() { return Era.Returns == Agent.Returns; },
			[&]() { return Era.Rolls == Agent.Rolls && Era.Rolls.Contains(TEXT("P=200000/50000")); },
			[&]() { return Era.Balance == Agent.Balance; },
			[&]() { return Era.DigestText == Agent.DigestText; },
			[&]() { return EraControl.Depot == LanderId && AgentControl.Depot == LanderId && EraControl.LanderIce == Era.StockIce && AgentControl.LanderIce == Era.StockIce; },
			[&]() { return EraControl.DigestText == AgentControl.DigestText && EraControl.Drift != AgentControl.Drift; },
			},
			FString::Printf(TEXT("RR5 the paired 10-sol window with stockpile #%d bound and (0, Ice, 500) set: %d convoys booked in each band, every landing at #%d (%.3f kg of Ice on its shelf, none on the Lander's), the same returns, every price, rival store and production at all %d thresholds, the balance (%s cr) and the digest (%d chars; Influence, Earth tension and Regolith aside) identical in the era and the agent band; a control pair with no binding and no endowment lands the same kg at the Lander, and its bands also agree on that digest and differ in those three (era %s | agent %s)"),
				StockId, Era.Booked, Era.Depot, Era.StockIce, Era.Rolls.Len() > 0 ? Era.Rolls.Len() - Era.Rolls.Replace(TEXT("\n"), TEXT("")).Len() : 0, *Cr(Era.Balance), Era.DigestText.Len(), *EraControl.Drift, *AgentControl.Drift));
		if (Era.Returns != Agent.Returns || Era.Rolls != Agent.Rolls || Era.DigestText != Agent.DigestText || EraControl.DigestText != AgentControl.DigestText)
		{
			Info(FString::Printf(TEXT("RR5 accumulators: era %s, agent %s, era control %s, agent control %s"), *Era.Drift, *Agent.Drift, *EraControl.Drift, *AgentControl.Drift));
			Info(FString::Printf(TEXT("RR5 diff: returns %s ; rolls %s ; digest %s ; control digest %s"), *FirstDiff(Era.Returns, Agent.Returns), *FirstDiff(Era.Rolls, Agent.Rolls), *FirstDiff(Era.DigestText, Agent.DigestText),
				*FirstDiff(EraControl.DigestText, AgentControl.DigestText)));
		}
	}

	// --- RR6. The save: nothing saved, nothing kept. MX9's colony shape (two convoys out, an injected rival, a moved market) with the hook's
	// binding on a stockpile: (i) a save with the binding and one with it cleared are byte-identical; (ii) with it set again, save, load, save is
	// byte-identical; (iii) after the load the binding is gone (the teardown clears it), the depot is the Lander, and the routes, every rival's
	// production and every slot's band equal their values before the save. No version, size, frame or frame order is asserted or printed. ---
	{
		Reset();
		DefsSub->Debug_ClearEndowments();
		Provision(50000000);
		const FName NKes6(TEXT("RrKes6"));
		Inject(NKes6, 30.0, TEXT("Ice:100"), TEXT(""), TEXT("Ice:1000"), TEXT("Ice:5000"), TEXT(""), TEXT(""), 0);
		Sim->Debug_SyncRivalEconomies();
		Sim->Debug_PlaceInstant(NStockpile, FVector(0.0, -1500.0, 0.0));
		const int32 StockId = Sim->GetBuildings().Last().Id;
		const bool bOrder1 = Order(NZarya, TEXT("Ice:300;Struct:-100")); // convoy 1, booked in sol 0
		for (int32 K = 0; K < 30 && Clock->GetSol() < 1; ++K) { EraOnce(); } // the real roll at sol 1 clears it
		const bool bOrder2 = Order(NKes6, TEXT("Ice:100"));                // convoy 2, booked in sol 1: waiting to clear
		const bool bBind = Sim->Debug_SetColonyDepot(0, StockId);
		const auto State = [&]()
		{
			TArray<FRHRoute> Routes;
			Sim->GetRoutes(Routes);
			FString T = RoutesText(Routes);
			for (int32 r = 0; r < Sim->GetRivalEconomyCount(); ++r)
			{
				const FName Name = Sim->GetRivalEconomyName(r);
				for (int32 s = 0; s < Sim->GetLedgerSlotCount(); ++s) { T += FString::Printf(TEXT(" %s.%d=%lld"), *Name.ToString(), s, Sim->GetRivalProduceG(Name, Sim->GetLedgerSlotName(s))); }
			}
			for (int32 s = 0; s < Sim->GetLedgerSlotCount(); ++s)
			{
				const FName Res = Sim->GetLedgerSlotName(s);
				T += FString::Printf(TEXT(" %s %lld/%lld/%lld"), *Res.ToString(), Sim->GetMarketAnchorMcr(Res), Sim->GetMarketFloorMcr(Res), Sim->GetMarketCeilMcr(Res));
			}
			return T;
		};
		const auto Bytes = [&](const TCHAR* Slot) { TArray<uint8> W; FFileHelper::LoadFileToArray(W, *SaveFilePath(Slot)); return W; };
		const auto SameFile = [](const TArray<uint8>& A, const TArray<uint8>& B) { return A.Num() > 0 && A.Num() == B.Num() && FMemory::Memcmp(A.GetData(), B.GetData(), A.Num()) == 0; };
		FString E1, E2, E3, E4, LoadErr;
		// (i)
		const bool bSavedA = Sim->SaveColony(TEXT("rrmx6a"), E1);
		const bool bCleared = Sim->Debug_SetColonyDepot(0, 0);
		const FString Before = State(); // the values with no binding: what a load must give back
		const bool bSavedB = Sim->SaveColony(TEXT("rrmx6b"), E2);
		const bool bI = SameFile(Bytes(TEXT("rrmx6a")), Bytes(TEXT("rrmx6b")));
		ExpectSeq({
			[&]() { return bOrder1 && bOrder2 && Sim->GetConvoys().Num() == 2 && bBind; },
			[&]() { return bSavedA && bCleared && bSavedB; },
			[&]() { return bI; },
			},
			TEXT("RR6 (i) MX9's colony (two convoys out, an injected rival, a moved market) saved with a stockpile bound as the depot and saved again with the binding cleared, no step between: the two files are byte-identical (the binding is not in the file)"));
		// (ii) and (iii)
		const bool bRebind = Sim->Debug_SetColonyDepot(0, StockId) && Sim->FindColonyDepotBuildingId(0) == StockId;
		const bool bSavedC = Sim->SaveColony(TEXT("rrmx6c"), E3);
		const bool bLoaded = Sim->LoadColony(TEXT("rrmx6c"), LoadErr) && LoadErr.IsEmpty();
		const int32 BindingAfter = Sim->GetColonyDepotBindingId(0), DepotAfter = Sim->FindColonyDepotBuildingId(0);
		const FString After = State();
		const bool bSavedD = Sim->SaveColony(TEXT("rrmx6d"), E4);
		const bool bII = SameFile(Bytes(TEXT("rrmx6c")), Bytes(TEXT("rrmx6d")));
		ExpectSeq({
			[&]() { return bRebind && bSavedC && bLoaded && bSavedD; },
			[&]() { return bII; },
			},
			TEXT("RR6 (ii) with the binding set again: save, load, save is byte-identical"));
		ExpectSeq({
			[&]() { return BindingAfter == 0; },
			[&]() { return DepotAfter == LanderId; },
			[&]() { return Sim->GetColonyRegionId() == 0; },
			[&]() { return Sim->GetBuildings().ContainsByPredicate([StockId](const FRHBuildingInstance& X) { return X.Id == StockId && !X.bUnderConstruction; }); },
			[&]() { return After == Before; },
			},
			FString::Printf(TEXT("RR6 (iii) after the load the binding is gone (%d) though the stockpile #%d stands, the depot is the Lander (#%d), the colony's region is %d, and the routes, every rival's production and every slot's anchor, floor and ceiling equal their values before the save"),
				BindingAfter, StockId, DepotAfter, Sim->GetColonyRegionId()));
		RetireAll();
	}

	GLog->RemoveOutputDevice(&Capture);
	Sim->OnCommandRejected.Remove(RejectH);
	return Finish();
}
