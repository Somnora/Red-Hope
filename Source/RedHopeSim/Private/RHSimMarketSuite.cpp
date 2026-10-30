#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "RHSaveFrame.h"

// E4 the Martian market self-test (docs/qa/2026-10-24/e4-martian-market-design.md: DIRECTOR ANSWER Q-A (a), Q-B (a), Q-C (a) > PROFESSOR
// RE-REVIEW conditions 2-6 > REVISION R1-R11 > PROFESSOR REVIEW > design). Fails loudly. `-market`.
//  MX0 the data, the strict parser, the malformed rows; MX1 inertness (the switch, and every rival inactive); MX2 one reprice by hand and the
//  rival order; MX3 a glut; MX4 a shortage; MX5 bounded and integer by enumeration; MX6 orders, money and the three readers of spendable
//  credit; MX7 the standing order and goodwill; MX9 the save and every load repair; MX10 ruling 11; MX11 politics; MX8 era parity (the
//  paired run, the boundary fixture, the fallback twin and the margin case), last because it injects a rival of its own. MX12 is the
//  mutation list, run by hand and recorded in design-decisions.
// Every expected number below that is not plain arithmetic was computed by a separate integer model of the update, written from the spec's
// own rules (R5) and not from the C++; the model reproduces the re-review's worked examples (Ice's floor at sol 37, Struct settling at 9.996 cr).
// Debug_SetMarketTally fills every ring entry and a roll then overwrites the closing sol's entry with its own fills (0 here), so a window of
// W = 5 holds four hook entries and the closing sol's own: Sold = (W - 1) x the hook's kg.

namespace
{
	// Captures LogRedHopeSim lines for the "logs nothing" and "logs this Warning" checks.
	struct FMktSuiteCapture : public FOutputDevice
	{
		TArray<FString> Lines, Warnings, Errors;
		virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category != FName(TEXT("LogRedHopeSim")))
			{
				return;
			}
			Lines.Add(V);
			if (Verbosity == ELogVerbosity::Warning) { Warnings.Add(V); }
			if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Fatal) { Errors.Add(V); }
		}
		// Log lines reach an output device on the logging thread's schedule, so every read flushes first.
		static void Pump() { GLog->FlushThreadedLogs(); }
		int32 CountWarnings(const TCHAR* Needle) { Pump(); int32 N = 0; for (const FString& W : Warnings) { N += W.Contains(Needle) ? 1 : 0; } return N; }
		int32 CountLines(const TCHAR* Needle) { Pump(); int32 N = 0; for (const FString& L : Lines) { N += L.Contains(Needle) ? 1 : 0; } return N; }
		int32 NumWarnings() { Pump(); return Warnings.Num(); }
		int32 NumErrors() { Pump(); return Errors.Num(); }
		int32 NumLines() { Pump(); return Lines.Num(); }
		void Clear() { Pump(); Lines.Reset(); Warnings.Reset(); Errors.Reset(); }
	};

	// The market block, read straight off a save's bytes with every field's offset, so a test can doctor one field and reseal the frame.
	struct FMktSuiteBlock
	{
		bool bOk = false;
		int32 FrameAt = INDEX_NONE, PayloadAt = 0, PayloadBytes = 0, EndAt = 0;
		TArray<FString> Slots;
		TArray<int32> SlotNameAt;
		struct FMarketRec { int32 Region = 0; TArray<int64> Price, Sold, Bought; TArray<int32> PriceAt, SoldAt, BoughtAt; int32 Fill = 0, Head = 0, FillAt = 0, HeadAt = 0; };
		TArray<FMarketRec> Markets;
		int32 Checked = 0, CheckedAt = 0, RivalCountAt = 0;
		struct FRivalRec { FString Name; int32 NameAt = 0; TArray<int64> Store; TArray<int32> StoreAt; };
		TArray<FRivalRec> Rivals;
		int64 Sales = 0, Reserved = 0;
		int32 SalesAt = 0, ReservedAt = 0, Next = 0, NextAt = 0, ConvoyCountAt = 0;
		struct FLineRec { FString Name; int32 Kg = 0, Filled = 0, NameAt = 0, KgAt = 0, FilledAt = 0; };
		struct FConvoyRec
		{
			int32 Id = 0; FString Rival; uint8 Returning = 0, Standing = 0, Cleared = 0; double Leg = 0.0; int32 ClearSol = 0; int64 Held = 0, Owed = 0, Value = 0;
			int32 IdAt = 0, RivalAt = 0, ReturningAt = 0, LegAt = 0, StandingAt = 0, ClearSolAt = 0, ClearedAt = 0, HeldAt = 0, OwedAt = 0, ValueAt = 0, LineCountAt = 0;
			TArray<FLineRec> Lines;
		};
		TArray<FConvoyRec> Convoys;
	};

	bool MktSuiteParse(const TArray<uint8>& W, FMktSuiteBlock& B)
	{
		B = FMktSuiteBlock();
		int32 Frame = INDEX_NONE, Bytes = 0;
		if (!RHSaveFrame::Find(W, RHSaveFrame::MarketTag, Frame, Bytes))
		{
			return false;
		}
		B.FrameAt = Frame;
		B.PayloadAt = Frame + RHSaveFrame::HeaderBytes;
		B.PayloadBytes = Bytes;
		const int32 End = B.PayloadAt + Bytes;
		int32 Cur = B.PayloadAt;
		bool bShort = false;
		const auto R32 = [&]() { int32 V = 0; if (Cur + 4 <= End) { FMemory::Memcpy(&V, W.GetData() + Cur, 4); } else { bShort = true; } Cur += 4; return V; };
		const auto R64 = [&]() { int64 V = 0; if (Cur + 8 <= End) { FMemory::Memcpy(&V, W.GetData() + Cur, 8); } else { bShort = true; } Cur += 8; return V; };
		const auto RD = [&]() { double V = 0.0; if (Cur + 8 <= End) { FMemory::Memcpy(&V, W.GetData() + Cur, 8); } else { bShort = true; } Cur += 8; return V; };
		const auto R8 = [&]() { uint8 V = 0; if (Cur + 1 <= End) { V = W[Cur]; } else { bShort = true; } Cur += 1; return V; };
		const auto RStr = [&](int32& OutAt) -> FString
		{
			const int32 Len = R32();
			OutAt = Cur;
			FString S;
			if (Len > 0 && Cur + Len <= End) { S = FString(ANSI_TO_TCHAR((const ANSICHAR*)(W.GetData() + Cur))); } else { bShort = true; }
			Cur += FMath::Max(0, Len);
			return S;
		};
		const int32 SlotCount = R32();
		for (int32 k = 0; k < SlotCount && !bShort; ++k) { int32 At = 0; B.Slots.Add(RStr(At)); B.SlotNameAt.Add(At); }
		const int32 MarketCount = R32();
		for (int32 m = 0; m < MarketCount && !bShort; ++m)
		{
			FMktSuiteBlock::FMarketRec Mk;
			Mk.Region = R32();
			for (int32 s = 0; s < SlotCount && !bShort; ++s)
			{
				Mk.PriceAt.Add(Cur); Mk.Price.Add(R64());
				for (int32 e = 0; e < 8; ++e) { Mk.SoldAt.Add(Cur); Mk.Sold.Add(R64()); }
				for (int32 e = 0; e < 8; ++e) { Mk.BoughtAt.Add(Cur); Mk.Bought.Add(R64()); }
			}
			Mk.FillAt = Cur; Mk.Fill = R32();
			Mk.HeadAt = Cur; Mk.Head = R32();
			B.Markets.Add(Mk);
		}
		B.CheckedAt = Cur; B.Checked = R32();
		B.RivalCountAt = Cur;
		const int32 RivalCount = R32();
		for (int32 r = 0; r < RivalCount && !bShort; ++r)
		{
			FMktSuiteBlock::FRivalRec Rv;
			Rv.Name = RStr(Rv.NameAt);
			for (int32 s = 0; s < SlotCount && !bShort; ++s) { Rv.StoreAt.Add(Cur); Rv.Store.Add(R64()); }
			B.Rivals.Add(Rv);
		}
		B.SalesAt = Cur; B.Sales = R64();
		B.ReservedAt = Cur; B.Reserved = R64();
		B.NextAt = Cur; B.Next = R32();
		B.ConvoyCountAt = Cur;
		const int32 ConvoyCount = R32();
		for (int32 c = 0; c < ConvoyCount && !bShort; ++c)
		{
			FMktSuiteBlock::FConvoyRec C;
			C.IdAt = Cur; C.Id = R32();
			C.Rival = RStr(C.RivalAt);
			C.ReturningAt = Cur; C.Returning = R8();
			C.LegAt = Cur; C.Leg = RD();
			C.StandingAt = Cur; C.Standing = R8();
			C.ClearSolAt = Cur; C.ClearSol = R32();
			C.ClearedAt = Cur; C.Cleared = R8();
			C.HeldAt = Cur; C.Held = R64();
			C.OwedAt = Cur; C.Owed = R64();
			C.ValueAt = Cur; C.Value = R64();
			C.LineCountAt = Cur;
			const int32 LineCount = R32();
			for (int32 l = 0; l < LineCount && !bShort; ++l)
			{
				FMktSuiteBlock::FLineRec L;
				L.Name = RStr(L.NameAt);
				L.KgAt = Cur; L.Kg = R32();
				L.FilledAt = Cur; L.Filled = R32();
				C.Lines.Add(L);
			}
			B.Convoys.Add(C);
		}
		B.EndAt = Cur;
		B.bOk = !bShort && Cur == End;
		return B.bOk;
	}

	// A "Res:kg" list, read by plain string work (no ParseResourceList), for the suite's own independent reference.
	TMap<FName, int64> MktSuiteList(const FString& Text)
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

	// The seeded LCG of MX5.
	struct FMktSuiteLcg
	{
		uint64 State = 0x2545F4914F6CDD1DULL;
		uint32 Next() { State = State * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32)(State >> 33); }
		int64 Range(int64 Lo, int64 Hi) { return Lo + (int64)(Next() % (uint32)(Hi - Lo + 1)); }
	};
}

int32 RHRunMarketSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== MARKET TEST (E4, 2026-10-24) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("MARKET ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("MARKET FAIL: %s"), *What); ++Fails; }
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
	const auto Info = [](const FString& What) { UE_LOG(LogRedHopeSim, Display, TEXT("MARKET info: %s"), *What); };
	const auto Near = [](double A, double B, double Tol = 1.0e-6) { return FMath::Abs(A - B) <= Tol; };
	const auto Flags = [](std::initializer_list<bool> L) { FString F; for (const bool b : L) { F += b ? TEXT("1") : TEXT("0"); } return F; };
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	using ELeg = URHSimWorldSubsystem::ERHLedgerLeg;
	using ETrade = URHSimWorldSubsystem::ERHRivalTrade;
	const FName NZarya(TEXT("Zarya")), NMeridian(TEXT("Meridian"));
	const FName NStruct(TEXT("Struct")), NWater(TEXT("Water")), NOx(TEXT("Oxygen")), NHyd(TEXT("Hydrogen")), NIce(TEXT("Ice")), NFood(TEXT("Food")), NParts(TEXT("SpareParts")), NCompost(TEXT("Compost"));
	const auto Cr = [](int64 Mcr) { return URHSimWorldSubsystem::FormatCredits(Mcr); };
	const auto Finish = [&]() -> int32
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== MARKET TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto EraOnce = [&]() { Clock->Debug_AdvanceSimSeconds(EraDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->EraStep((float)EraDt); };
	const auto AgentOnce = [&]() { Clock->Debug_AdvanceSimSeconds((double)AgentDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->Debug_StepSim(AgentDt); };
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	const auto Inv = [&]()
	{
		return Sim->GetCreditBalanceMcr() == Sim->GetCreditAwardedMcr() - Sim->GetCreditSpentMcr() && Sim->GetCreditBalanceMcr() >= 0
			&& Sim->GetSalesPaidMcr() >= 0 && Sim->GetMarketSalesMcr() >= 0 && Sim->GetSalesPaidMcr() + Sim->GetMarketSalesMcr() <= Sim->GetCreditAwardedMcr()
			&& Sim->GetCreditReservedMcr() >= 0 && Sim->GetCreditReservedMcr() <= Sim->GetCreditBalanceMcr();
	};
	const auto ReservedSum = [&]() { int64 S = 0; for (const FRHConvoy& C : Sim->GetConvoys()) { S += (C.IsPriced() && !C.bCleared) ? C.ReservedMcr : 0; } return S; };
	const auto Out = [&](FName R) { return (double)Sim->GetLedgerLifetimeG(R, ELeg::Out) / 1000.0 + Sim->GetLedgerOpenKg(R, ELeg::Out); };
	const auto In = [&](FName R) { return (double)Sim->GetLedgerLifetimeG(R, ELeg::In) / 1000.0 + Sim->GetLedgerOpenKg(R, ELeg::In); };
	const auto Used = [&](FName R) { return (double)Sim->GetLedgerLifetimeG(R, ELeg::Used) / 1000.0 + Sim->GetLedgerOpenKg(R, ELeg::Used); };
	const auto SetPool = [&](FName R, double Kg) { Sim->AddStock(R, Kg - Sim->GetStock(R)); };
	const auto Held = [&](FName R) { return Sim->GetLaunchLoadableKg(R) + Sim->GetLaunchFloorKg(R); };
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto Shelf = [&](int32 Id, FName R, bool bOut) { const FRHBuildingInstance* B = Sim->GetBuildings().FindByPredicate([Id](const FRHBuildingInstance& X) { return X.Id == Id; }); return B ? (bOut ? B->OutputKg : B->InputKg).FindRef(R) : -1.0; };
	FMktSuiteCapture Capture;
	GLog->AddOutputDevice(&Capture);
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	FString SaveErr;
	// Every fixture's rows: a rival with an economy (Ice by default), its standing lots empty. Retiring one re-injects it with no economy and
	// no activity, so its entry goes at the next sync and it never touches a later fixture.
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
	Expect(Sim->SaveColony(TEXT("marketbase"), SaveErr), FString::Printf(TEXT("MX the fresh colony is saved as the base every fixture loads (%s)"), *SaveErr));
	const auto LoadSlot = [&](const TCHAR* Slot)
	{
		FString E;
		const bool bOk = Sim->LoadColony(Slot, E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("MARKET FAIL: loading %s failed: %s"), Slot, *E); ++Fails; }
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetEarthTakesQuota(true);
		Sim->Debug_SetMarketEnabled(true);
		Sim->Debug_SetMarketRivalOnly(false);
		Sim->Debug_ClearPolitics();
		Rejects.Reset();
		return bOk;
	};
	const auto Reset = [&]() { return LoadSlot(TEXT("marketbase")); };
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
	// The colony the order fixtures run in: fuel, parts, goods to sell and credit to reserve against.
	const auto Provision = [&](int64 CreditMcr)
	{
		Sim->AddStock(NHyd, 600.0);
		Sim->AddStock(NParts, 30.0);
		Sim->AddStock(NIce, 3000.0);
		Sim->AddStock(NOx, 500.0); // no Water: Q1 (Struct 300, Water 200, Oxygen 50) must stay unmet, or its 10,000 cr award lands mid-fixture
		Sim->Debug_ZeroCredits();
		Sim->Debug_GrantCredits(CreditMcr);
	};
	const auto RollNext = [&]() { Sim->Debug_RollMarket(Sim->GetMarketCheckedSol() + 1); };

	// --- MX0. The data: the economy columns exactly, the CSV equal to the live DT, the config rows, the start prices, the strict parser. ---
	{
		const FRHRivalRow* Z = DefsSub ? DefsSub->GetRival(NZarya) : nullptr;
		const FRHRivalRow* M = DefsSub ? DefsSub->GetRival(NMeridian) : nullptr;
		const bool bZarya = Z && Z->RegionId == 0 && Z->Produce == TEXT("Ice:400;Water:120;Oxygen:60;Struct:40;Hydrogen:10") && Z->Consume == TEXT("Ice:150;Water:140;Oxygen:60;Struct:120;Hydrogen:15")
			&& Z->StoreStart == TEXT("Ice:12000;Water:2000;Oxygen:1500;Struct:2500;Hydrogen:300") && Z->StoreCap == TEXT("Ice:20000;Water:6000;Oxygen:3000;Struct:8000;Hydrogen:1000")
			&& Z->ExportLot == TEXT("Ice:150") && Z->ImportLot == TEXT("Struct:100") && Z->SliceActive;
		const bool bMeridian = M && M->RegionId == 0 && M->Produce == TEXT("SpareParts:2;Food:20") && M->Consume == TEXT("SpareParts:1;Food:60")
			&& M->StoreStart == TEXT("SpareParts:40;Food:600") && M->StoreCap == TEXT("SpareParts:100;Food:2000") && M->ExportLot == TEXT("SpareParts:4") && M->ImportLot == TEXT("Food:60") && !M->SliceActive;
		// The CSV equals the live DT on every economy column of every row.
		FString Csv;
		const bool bCsv = FFileHelper::LoadFileToString(Csv, *(FPaths::ProjectDir() / TEXT("docs/data/RH_Rivals.csv")));
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		int32 CsvRows = 0, CsvSame = 0;
		if (Lines.Num() > 1)
		{
			TArray<FString> Head;
			Lines[0].ParseIntoArray(Head, TEXT(","), false);
			const int32 CN = Head.IndexOfByKey(TEXT("Name")), CR = Head.IndexOfByKey(TEXT("RegionId")), CP = Head.IndexOfByKey(TEXT("Produce")), CC = Head.IndexOfByKey(TEXT("Consume")),
				CS = Head.IndexOfByKey(TEXT("StoreStart")), CK = Head.IndexOfByKey(TEXT("StoreCap"));
			for (int32 i = 1; CN != INDEX_NONE && i < Lines.Num(); ++i)
			{
				TArray<FString> Cells;
				Lines[i].ParseIntoArray(Cells, TEXT(","), false); // every column up to StoreCap is unquoted and holds no comma
				const FRHRivalRow* Row = (Cells.Num() > CK && DefsSub) ? DefsSub->GetRival(FName(*Cells[CN])) : nullptr;
				++CsvRows;
				CsvSame += (Row && Row->RegionId == FCString::Atoi(*Cells[CR]) && Row->Produce == Cells[CP] && Row->Consume == Cells[CC] && Row->StoreStart == Cells[CS] && Row->StoreCap == Cells[CK]) ? 1 : 0;
			}
		}
		// Every economy list is whole kg, every key is in StoreCap and has a slot with an Earth value above 0, the bounds hold, and the shipped
		// round trips are at least a sol. The row's own verdict is Economy for both shipped rows.
		bool bBounds = true;
		FString BoundsBad;
		double MinRoundTrip = 1.0e9;
		for (const FRHRivalRow* Row : { Z, M })
		{
			if (!Row) { bBounds = false; continue; }
			const TMap<FName, int64> Prod = MktSuiteList(Row->Produce), Cons = MktSuiteList(Row->Consume), Start = MktSuiteList(Row->StoreStart), Cap = MktSuiteList(Row->StoreCap);
			for (const auto& K : Cap)
			{
				const FRHResourceRow* Res = DefsSub->GetResource(K.Key);
				bBounds &= Res && Res->EarthValue_mcr > 0 && K.Value <= 10000000 && Sim->HasLedgerSlot(K.Key);
			}
			for (const auto& K : Prod) { bBounds &= Cap.Contains(K.Key) && K.Value <= 1000000; }
			for (const auto& K : Cons) { bBounds &= Cap.Contains(K.Key) && K.Value <= 1000000; }
			for (const auto& K : Start) { bBounds &= Cap.Contains(K.Key) && K.Value <= Cap.FindRef(K.Key); }
			MinRoundTrip = FMath::Min(MinRoundTrip, 2.0 * (double)Row->DistanceKm / 60.0);
		}
		const bool bKinds = Sim->GetRivalTradeKind(NZarya) == ETrade::Economy && Sim->GetRivalTradeKind(NMeridian) == ETrade::Economy;
		ExpectSeq({
			[&]() { return bZarya; },
			[&]() { return bMeridian; },
			[&]() { return bCsv; },
			[&]() { return CsvRows == 2; },
			[&]() { return CsvSame == 2; },
			[&]() { return bBounds; },
			[&]() { return bKinds; },
			[&]() { return MinRoundTrip >= 1.0; },
			},
			FString::Printf(TEXT("MX0 Zarya's and Meridian's economy columns are exactly the design's whole-kg lists, equal to the CSV on %d of %d rows; every key is in StoreCap with an Earth value above 0, every rate is at most 1,000,000 kg a sol, every start is within its cap, the cap at most 10,000,000 kg; both rows are economies; the shipped round trips are at least a sol (shortest %.2f)"), CsvSame, CsvRows, MinRoundTrip));
		// Every new config row, from the live DT_Config only.
		struct FCfg { const TCHAR* Name; double Value; };
		const FCfg Cfgs[] = { { TEXT("MarketWindowSols"), 5.0 }, { TEXT("MarketMaxStepBp"), 500.0 }, { TEXT("MarketGainPct"), 20.0 }, { TEXT("MarketFlowWeight"), 50.0 }, { TEXT("MarketStoreWeight"), 30.0 },
			{ TEXT("MarketAnchorWeight"), 50.0 }, { TEXT("MarketTargetFillBp"), 5000.0 }, { TEXT("MarketFloorBp"), 2500.0 }, { TEXT("MarketCeilBp"), 40000.0 }, { TEXT("MarketMinVolumeG"), 1000000.0 },
			{ TEXT("MarketRivalOnly"), 0.0 }, { TEXT("MaxConvoysOut"), 3.0 }, { TEXT("ConvoyCargoKg"), 1000.0 }, { TEXT("GoodwillMinValueMcr"), 300000.0 } };
		int32 CfgRight = 0;
		FString CfgBad;
		for (const FCfg& C : Cfgs)
		{
			if (DefsSub && DefsSub->GetConfigTableScalar(FName(C.Name), -999.0) == C.Value) { ++CfgRight; } else { CfgBad += FString(C.Name) + TEXT(" "); }
		}
		ExpectSeq({
			[&]() { return CfgRight == 14; },
			[&]() { return Sim->GetMaxConvoysOut() == 3; },
			[&]() { return Sim->GetConvoyCargoKg() == 1000; },
			[&]() { return Sim->GetMarketMaxStepBp() == 500; },
			[&]() { return DefsSub->GetConfigTableScalar(FName("MarketMinVolumeG"), 0.0) >= 1.0; },
			[&]() { return DefsSub->GetConfigTableScalar(FName("MarketFloorBp"), 0.0) < 10000.0; },
			[&]() { return DefsSub->GetConfigTableScalar(FName("MarketCeilBp"), 0.0) > 10000.0; },
			},
			FString::Printf(TEXT("MX0 the 14 new RH_Config rows read their authored values from the live DT (%d of 14) %s; MarketMinVolumeG is at least 1 and the floor is under 10000 under the ceiling"), CfgRight, *CfgBad));
		// Each traded slot starts at its Earth value, and the open sol is C5d's.
		bool bStart = true;
		FString StartSeen;
		for (const FName R : { NIce, NWater, NOx, NStruct, NHyd, NFood, NParts })
		{
			const FRHResourceRow* Res = DefsSub->GetResource(R);
			const int64 P = Sim->GetMarketPriceMcr(R);
			bStart &= Res && P == Res->EarthValue_mcr && Sim->IsMarketTraded(R);
			StartSeen += FString::Printf(TEXT("%s %s; "), *R.ToString(), *Cr(P));
		}
		ExpectSeq({
			[&]() { return bStart; },
			[&]() { return !Sim->IsMarketTraded(NCompost); },
			[&]() { return Sim->GetMarketPriceMcr(NCompost) == 0; },
			[&]() { return Sim->GetMarketCheckedSol() == Sim->GetProductionCheckedSol(); },
			[&]() { return Sim->GetMarketRegionCount() == 1; },
			[&]() { return Sim->IsMarketLive(); },
			},
			FString::Printf(TEXT("MX0 a fresh colony has one market, whose seven traded slots start at their Earth values (%s cr/kg), Compost (Earth value 0) is not traded, and the open sol is C5d's %d"), *StartSeen, Sim->GetProductionCheckedSol()));
		// The strict parser against the lenient one: three cases the lenient parser takes without a word.
		TArray<TPair<FName, int64>> Got;
		FString Why;
		const bool bGood = URHDefinitionsSubsystem::ParseResourceListStrict(TEXT(" Ice:5 ; Water : 7 "), Got, Why) && Got.Num() == 2 && Got[0].Key == NIce && Got[0].Value == 5 && Got[1].Value == 7
			&& URHDefinitionsSubsystem::ParseResourceListStrict(TEXT(""), Got, Why) && Got.Num() == 0 && URHDefinitionsSubsystem::ParseResourceListStrict(TEXT("   "), Got, Why);
		struct FBadList { const TCHAR* Text; const TCHAR* Needle; };
		const FBadList Bad[] = {
			{ TEXT("Ice:5;Water"), TEXT("exactly one ':'") },          // an entry with no ':' (the lenient parser skips it)
			{ TEXT("Ice:5;Ice:7"), TEXT("appears twice") },             // a repeated key (the lenient parser keeps the last value)
			{ TEXT("Ice:x"), TEXT("whole number") },                    // a value that is not a number (the lenient parser reads 0)
			{ TEXT("Ice:5;;Water:3"), TEXT("empty entry") }, { TEXT("Ice:5;"), TEXT("empty entry") }, { TEXT("Ice:-5"), TEXT("whole number") }, { TEXT("Ice:1.5"), TEXT("whole number") },
			{ TEXT("Ice:1e3"), TEXT("whole number") }, { TEXT("Ice:+5"), TEXT("whole number") }, { TEXT(":5"), TEXT("no resource") }, { TEXT("Ice:5:6"), TEXT("exactly one ':'") },
			{ TEXT("Ice:"), TEXT("whole number") }, { TEXT("Ice:5;ice:6"), TEXT("appears twice") }, { TEXT("Ice:1234567890123"), TEXT("whole number") } };
		bool bAllRefused = true;
		FString NotRefused;
		for (const FBadList& B : Bad)
		{
			Got.Reset();
			const bool bRefused = !URHDefinitionsSubsystem::ParseResourceListStrict(B.Text, Got, Why) && Got.Num() == 0 && Why.Contains(B.Needle);
			bAllRefused &= bRefused;
			if (!bRefused) { NotRefused += FString::Printf(TEXT("'%s' (%s); "), B.Text, *Why); }
		}
		const TMap<FName, double> Lenient1 = URHDefinitionsSubsystem::ParseResourceList(TEXT("Ice:5;Water"));
		const TMap<FName, double> Lenient2 = URHDefinitionsSubsystem::ParseResourceList(TEXT("Ice:5;Ice:7"));
		const TMap<FName, double> Lenient3 = URHDefinitionsSubsystem::ParseResourceList(TEXT("Ice:x"));
		ExpectSeq({
			[&]() { return bGood; },
			[&]() { return bAllRefused; },
			[&]() { return Lenient1.Num() == 1; },
			[&]() { return Lenient2.FindRef(NIce) == 7.0; },
			[&]() { return Lenient3.FindRef(NIce) == 0.0; },
			},
			FString::Printf(TEXT("MX0 the strict parser refuses by name what ParseResourceList takes silently - an entry with no ':' (lenient: skipped), a repeated key (lenient: the last wins, 7) and 'Ice:x' (lenient: 0) - and a doubled or trailing ';', a sign, a fraction, an exponent, an empty key or value, a second ':', a key repeated in another case, a 13-digit value (%d refusals); it accepts blanks around keys and values and an empty list %s"), (int32)UE_ARRAY_COUNT(Bad), *NotRefused));
		// A malformed injected row trades by barter and logs nothing at runtime; so does Greed's shape (lots outside StoreCap); neither logs.
		Capture.Clear();
		Inject(FName("MxCompost"), 40.0, TEXT("Compost:5"), TEXT(""), TEXT("Compost:10"), TEXT("Compost:100"), TEXT(""), TEXT(""), 0);
		Inject(FName("MxDup"), 40.0, TEXT("Ice:5;Ice:7"), TEXT(""), TEXT("Ice:10"), TEXT("Ice:100"), TEXT(""), TEXT(""), 0);
		Inject(FName("MxNoSlot"), 40.0, TEXT(""), TEXT(""), TEXT(""), TEXT("Unobtainium:100"), TEXT(""), TEXT(""), 0);
		Inject(FName("MxOver"), 40.0, TEXT("Ice:5"), TEXT(""), TEXT("Ice:500"), TEXT("Ice:100"), TEXT(""), TEXT(""), 0);
		Inject(FName("MxBig"), 40.0, TEXT("Ice:1000001"), TEXT(""), TEXT("Ice:1"), TEXT("Ice:100"), TEXT(""), TEXT(""), 0);
		Inject(FName("MxGreed"), 40.0, Z ? Z->Produce : FString(), Z ? Z->Consume : FString(), Z ? Z->StoreStart : FString(), Z ? Z->StoreCap : FString(), TEXT("Ice:10"), TEXT("SpareParts:4"), 0);
		Inject(FName("MxBare"), 40.0, TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("Ice:10"), TEXT("Struct:10"), 0);
		FString W1, W2, W3, W4, W5, W6;
		const ETrade K1 = Sim->GetRivalTradeKind(FName("MxCompost"), &W1), K2 = Sim->GetRivalTradeKind(FName("MxDup"), &W2), K3 = Sim->GetRivalTradeKind(FName("MxNoSlot"), &W3),
			K4 = Sim->GetRivalTradeKind(FName("MxOver"), &W4), K5 = Sim->GetRivalTradeKind(FName("MxBig"), &W5), K6 = Sim->GetRivalTradeKind(FName("MxGreed"), &W6),
			K7 = Sim->GetRivalTradeKind(FName("MxBare"));
		FString Why1, Why2;
		const bool bBarter = !Sim->CanPlaceOrder(FName("MxCompost"), TEXT("Ice:5"), Why1) && Why1.Contains(TEXT("trades by barter only"))
			&& !Sim->CanPlaceOrder(FName("MxGreed"), TEXT("Ice:5"), Why2) && Why2.Contains(TEXT("trades by barter only"));
		ExpectSeq({
			[&]() { return K1 == ETrade::Malformed; },
			[&]() { return W1.Contains(TEXT("Earth value")); },
			[&]() { return K2 == ETrade::Malformed; },
			[&]() { return W2.Contains(TEXT("appears twice")); },
			[&]() { return K3 == ETrade::Malformed; },
			[&]() { return W3.Contains(TEXT("no resource slot")); },
			[&]() { return K4 == ETrade::Malformed; },
			[&]() { return W4.Contains(TEXT("above its StoreCap")); },
			[&]() { return K5 == ETrade::Malformed; },
			[&]() { return K6 == ETrade::LotsOutsideCap; },
			[&]() { return K7 == ETrade::NoEconomy; },
			[&]() { return bBarter; },
			[&]() { return Sim->GetRivalEconomyCount() == 2; },
			[&]() { return Capture.NumWarnings() == 0; },
			[&]() { return Capture.NumErrors() == 0; },
			},
			FString::Printf(TEXT("MX0 an injected row naming Compost (Earth value 0: '%s'), repeating a key, naming no slot, starting above its cap or making over 1,000,000 kg a sol is malformed; Greed's shape (Zarya's columns, SpareParts in the lot) is legal and barter; an empty StoreCap is no economy; each takes no priced order; none adds an economy entry (%d economies) and none logs a Warning or an Error at runtime (%d, %d)"),
				*W1, Sim->GetRivalEconomyCount(), Capture.NumWarnings(), Capture.NumErrors()));
		RetireAll();
	}

	// The digest of everything outside the market: stocks, shelves, credits, relations, Influence, HumanNature, tension, Hope, the quota phase, the ledger.
	const auto Digest = [&]() -> FString
	{
		FString D = FString::Printf(TEXT("sol %d prod %d bal %lld/%lld/%lld res %lld rel %.17g/%.17g inf %.17g hn %.17g ten %.17g id %.17g hope %.17g pop %d phase %d"),
			Clock->GetSol(), Sim->GetProductionCheckedSol(), Sim->GetCreditBalanceMcr(), Sim->GetCreditAwardedMcr(), Sim->GetCreditSpentMcr(), Sim->GetCreditReservedMcr(),
			Sim->GetRivalRelation(NZarya), Sim->GetRivalRelation(NMeridian), Sim->GetInfluence(), Sim->GetHumanNatureAxis(), Sim->GetEarthTension(), Sim->GetIdentityAxis(),
			Sim->GetHopeSmoothed(), Sim->GetPopulation(), (int32)Sim->GetQuotaPhase());
		DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow&) { D += FString::Printf(TEXT(" %s=%.17g/%.17g|%lld/%lld"), *N.ToString(), Sim->GetStock(N), Sim->GetTotalSolid(N), Sim->GetLedgerLifetimeG(N, ELeg::Made), Sim->GetLedgerLifetimeG(N, ELeg::Used)); });
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
				for (const FName& K : Keys) { D += FString::Printf(TEXT("%s:%.17g,"), *K.ToString(), M->FindRef(K)); }
				D += TEXT("]");
			}
		}
		return D;
	};

	// --- MX1. Inertness: the market on against the market off, in each band, moves nothing outside itself; the saves are byte-identical up
	// to the market block; and with every rival inactive the market still runs silently and no order can be booked. ---
	{
		const FRHRivalRow ZeroZ = *DefsSub->GetRival(NZarya), ZeroM = *DefsSub->GetRival(NMeridian);
		struct FTwin { FString Digest; TArray<uint8> Bytes; int64 IcePrice = 0; };
		const auto Run = [&](bool bAgent, bool bOn, bool bInactive, const TCHAR* Slot) -> FTwin
		{
			Reset();
			if (bInactive)
			{
				FRHRivalRow A = ZeroZ, B = ZeroM;
				A.SliceActive = false; B.SliceActive = false;
				DefsSub->Debug_InjectRival(NZarya, A);
				DefsSub->Debug_InjectRival(NMeridian, B);
			}
			Sim->Debug_SetMarketEnabled(bOn);
			const int32 Steps = bAgent ? 10 * FMath::RoundToInt(SolLen / (double)AgentDt) : 10 * FMath::RoundToInt(SolLen / EraDt);
			for (int32 K = 0; K < Steps; ++K) { bAgent ? AgentOnce() : EraOnce(); }
			FTwin T;
			T.Digest = Digest();
			T.IcePrice = Sim->GetMarketPriceMcr(NIce);
			FString Err;
			Sim->SaveColony(Slot, Err);
			FFileHelper::LoadFileToArray(T.Bytes, *SaveFilePath(Slot));
			if (bInactive)
			{
				DefsSub->Debug_InjectRival(NZarya, ZeroZ);
				DefsSub->Debug_InjectRival(NMeridian, ZeroM);
			}
			return T;
		};
		const auto BytesToMarket = [](const FTwin& T) { int32 F = INDEX_NONE, P = 0; return RHSaveFrame::Find(T.Bytes, RHSaveFrame::MarketTag, F, P) ? F : INDEX_NONE; };
		for (const bool bAgent : { false, true })
		{
			const FTwin On = Run(bAgent, true, false, bAgent ? TEXT("mktmx1_a1") : TEXT("mktmx1_e1"));
			const FTwin Off = Run(bAgent, false, false, bAgent ? TEXT("mktmx1_a0") : TEXT("mktmx1_e0"));
			const int32 OnAt = BytesToMarket(On), OffAt = BytesToMarket(Off);
			const bool bSame = OnAt > 0 && OnAt == OffAt && FMemory::Memcmp(On.Bytes.GetData(), Off.Bytes.GetData(), OnAt) == 0;
			// E5 (v56): the market frame is no longer the file's last ('RHE5' follows it), so the compare is bounded to the frame's own bytes.
			int32 OnFrame = INDEX_NONE, OnPayload = 0, OffFrame = INDEX_NONE, OffPayload = 0;
			RHSaveFrame::Find(On.Bytes, RHSaveFrame::MarketTag, OnFrame, OnPayload);
			RHSaveFrame::Find(Off.Bytes, RHSaveFrame::MarketTag, OffFrame, OffPayload);
			const int32 FrameLen = RHSaveFrame::HeaderBytes + OnPayload + RHSaveFrame::FooterBytes;
			const bool bFrameDiffers = bSame && On.Bytes.Num() == Off.Bytes.Num() && OnFrame == OnAt && OffFrame == OffAt && OnPayload == OffPayload && OnAt + FrameLen <= On.Bytes.Num()
				&& FMemory::Memcmp(On.Bytes.GetData() + OnAt, Off.Bytes.GetData() + OnAt, FrameLen) != 0;
			ExpectSeq({
			[&]() { return On.Digest == Off.Digest; },
			[&]() { return On.Digest.Len() > 300; },
			[&]() { return bSame; },
			[&]() { return bFrameDiffers; },
			[&]() { return On.IcePrice != 3000; },
			[&]() { return Off.IcePrice == 3000; },
			},
				FString::Printf(TEXT("MX1 %s band, 10 sols, no order: the digest (%d chars: stocks, shelves, credits, relations, Influence, HumanNature, tension, Hope, the quota phase, the ledger) is bit-identical with the market on and off, and the saves (%d bytes) are byte-identical up to the market block (%d bytes); only that block differs (on: Ice %s cr/kg, off: %s)"),
					bAgent ? TEXT("agent") : TEXT("era"), On.Digest.Len(), On.Bytes.Num(), OnAt, *Cr(On.IcePrice), *Cr(Off.IcePrice)));
		}
		// (b) With every rival inactive the market still runs (it reads the rivals' data, not the colony's contact), no order can be booked,
		// and the digest equals the market-off twin's.
		{
			const FTwin On = Run(false, true, true, TEXT("mktmx1_i1"));
			const FTwin Off = Run(false, false, true, TEXT("mktmx1_i0"));
			Run(false, true, true, TEXT("mktmx1_i2")); // leaves the colony loaded to ask the refusal below
			FRHRivalRow A = ZeroZ; A.SliceActive = false;
			DefsSub->Debug_InjectRival(NZarya, A);
			FString Why;
			const bool bRefused = !Sim->CanPlaceOrder(NZarya, TEXT("Ice:5"), Why) && Why.Contains(TEXT("No trade contact")) && !Sim->CanPlaceStandingOrder(NZarya, Why) && Why.Contains(TEXT("No trade contact"));
			const bool bLive = Sim->IsMarketLive();
			DefsSub->Debug_InjectRival(NZarya, ZeroZ);
			ExpectSeq({
			[&]() { return On.Digest == Off.Digest; },
			[&]() { return On.IcePrice != 3000; },
			[&]() { return Off.IcePrice == 3000; },
			[&]() { return bRefused; },
			[&]() { return bLive; },
			},
				FString::Printf(TEXT("MX1 with every rival inactive (SliceActive false), 10 sols: the market runs silently on the rivals' data (Ice %s cr/kg against %s with the market off), the digest equals the market-off twin's, and no order can be booked ('%s')"), *Cr(On.IcePrice), *Cr(Off.IcePrice), *Why));
		}
	}

	// --- MX2. One reprice, by hand: every traded slot's new price is an integer written here from a separate model (production first, then the
	// update), including a step that rounds to zero (the 1 mcr minimum), the floor clamp, an anchor beyond +-10000 and the signed division's
	// negative cases; and rivals ordered by name string, never by FName index or table order. ---
	{
		using M = URHSimWorldSubsystem;
		ExpectSeq({
			[&]() { return M::MarketDivTrunc(-7, 2) == -3; },
			[&]() { return M::MarketDivTrunc(7, 2) == 3; },
			[&]() { return M::MarketDivTrunc(-1, 2) == 0; },
			[&]() { return M::MarketDivTrunc(-10000, 3) == -3333; },
			[&]() { return M::MarketDivTrunc(0, 5) == 0; },
			[&]() { return M::MarketDivTrunc(-9, 3) == -3; },
			[&]() { return M::MarketDivTrunc(-9999, 10000) == 0; },
			[&]() { return M::MarketDivTrunc(-10001, 10000) == -1; },
			},
			TEXT("MX2 the signed division truncates toward zero: -7 / 2 = -3, -1 / 2 = 0, -10000 / 3 = -3333, -9999 / 10000 = 0, -10001 / 10000 = -1"));
		struct FMx2 { const TCHAR* Res; int64 StoreKg, SoldKg, BoughtKg, Price, Want; };
		const FMx2 SetA[] = {
			{ TEXT("Ice"), 12000, 0, 0, 3000, 2850 }, { TEXT("Water"), 2000, 0, 0, 4000, 4112 }, { TEXT("Oxygen"), 1500, 0, 0, 7985, 7986 }, { TEXT("Struct"), 0, 0, 0, 20000, 19000 },
			{ TEXT("Hydrogen"), 1000, 2000, 0, 3000, 3000 }, { TEXT("Food"), 600, 0, 500, 20000, 21000 }, { TEXT("SpareParts"), 40, 0, 0, 800000, 760000 } };
		const FMx2 SetB[] = {
			{ TEXT("Ice"), 20000, 1000, 0, 750, 750 }, { TEXT("Water"), 6000, 0, 0, 4000, 3800 }, { TEXT("Oxygen"), 3000, 0, 300, 8000, 8052 }, { TEXT("Struct"), 0, 0, 0, 11000, 10780 },
			{ TEXT("Hydrogen"), 0, 0, 100, 12000, 12600 }, { TEXT("Food"), 2000, 300, 0, 5000, 5000 }, { TEXT("SpareParts"), 0, 0, 0, 200000, 210000 } };
		const FMx2 SetC[] = {
			{ TEXT("Ice"), 5000, 0, 2000, 2000, 2100 }, { TEXT("Water"), 3000, 500, 0, 9000, 8550 }, { TEXT("Oxygen"), 200, 0, 0, 12000, 12024 }, { TEXT("Struct"), 8000, 0, 0, 2500, 2578 },
			{ TEXT("Hydrogen"), 500, 0, 0, 6000, 6300 }, { TEXT("Food"), 100, 0, 10, 26000, 27300 }, { TEXT("SpareParts"), 100, 5, 0, 150000, 144375 } };
		const FMx2* Sets[3] = { SetA, SetB, SetC };
		const TCHAR* SetNames[3] = { TEXT("A: a falling, a rising and a flat price, the 1 mcr step (Oxygen 7985 to 7986), the floor, a shortage from the ceiling, Meridian's Food and SpareParts"), TEXT("B: an anchor beyond +10000 (Struct at 2.2 V falls 200 bp: with the anchor clamped it would stand still), the floor held (Ice 750), a full climb"), TEXT("C: mixed, a step of 312 bp and one of -375 bp") };
		for (int32 s = 0; s < 3; ++s)
		{
			Reset();
			bool bAll = true;
			FString Seen;
			for (int32 i = 0; i < 7; ++i)
			{
				const FMx2& F = Sets[s][i];
				const FName Res(F.Res), Owner = (Res == NFood || Res == NParts) ? NMeridian : NZarya;
				Sim->Debug_SetRivalStore(Owner, Res, F.StoreKg);
				Sim->Debug_SetMarketTally(Res, F.SoldKg, F.BoughtKg);
				Sim->Debug_SetMarketPrice(Res, F.Price);
			}
			RollNext();
			for (int32 i = 0; i < 7; ++i)
			{
				const FMx2& F = Sets[s][i];
				const int64 Got = Sim->GetMarketPriceMcr(FName(F.Res));
				bAll &= Got == F.Want;
				Seen += FString::Printf(TEXT("%s %lld->%lld%s; "), F.Res, F.Price, Got, Got == F.Want ? TEXT("") : *FString::Printf(TEXT(" (WANT %lld)"), F.Want));
			}
			Expect(bAll, FString::Printf(TEXT("MX2 set %s: one roll gives %s"), SetNames[s], *Seen));
		}
		// Rival order: two rivals injected in the reverse of their name order (the first name created is the later one), against two in the
		// forward order (other names, the same columns). The saved block lists the economy rivals by name string, so each gives the same
		// prices and the same stores in the same saved order.
		const auto RunPair = [&](FName NameA, FName NameB, bool bBFirst, FMktSuiteBlock& OutBlock, int64& OutIce)
		{
			Reset();
			// A is the lower name and carries the first data set; B the second. The injection order is the test's variable.
			const auto InjectA = [&]() { Inject(NameA, 40.0, TEXT("Ice:50;Water:5"), TEXT("Ice:10"), TEXT("Ice:700;Water:50"), TEXT("Ice:4000;Water:900"), TEXT(""), TEXT(""), 0); };
			const auto InjectB = [&]() { Inject(NameB, 55.0, TEXT("Ice:20"), TEXT("Ice:5;Water:2"), TEXT("Ice:300;Water:90"), TEXT("Ice:3000;Water:700"), TEXT(""), TEXT(""), 0); };
			if (bBFirst) { InjectB(); InjectA(); } else { InjectA(); InjectB(); }
			Sim->Debug_SyncRivalEconomies();
			RollNext();
			OutIce = Sim->GetMarketPriceMcr(NIce);
			FString Err;
			Sim->SaveColony(TEXT("mktmx2_pair"), Err);
			TArray<uint8> W;
			FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("mktmx2_pair")));
			MktSuiteParse(W, OutBlock);
			RetireAll();
		};
		const FName RevB(TEXT("MxRevB")), RevA(TEXT("MxRevA")); // created B first: the FName index order is B, A; the name order is A, B
		const FName FwdC(TEXT("MxFwdC")), FwdD(TEXT("MxFwdD"));
		FMktSuiteBlock BlockRev, BlockFwd;
		int64 IceRev = 0, IceFwd = 0;
		RunPair(RevA, RevB, true, BlockRev, IceRev);   // table order: ..., MxRevB, MxRevA
		RunPair(FwdC, FwdD, false, BlockFwd, IceFwd);  // table order: ..., MxFwdC, MxFwdD
		const auto Ascending = [](const FMktSuiteBlock& B) { bool bAsc = B.Rivals.Num() == 4; for (int32 i = 1; i < B.Rivals.Num(); ++i) { bAsc &= B.Rivals[i - 1].Name.Compare(B.Rivals[i].Name, ESearchCase::CaseSensitive) < 0; } return bAsc; };
		bool bSameStores = BlockRev.bOk && BlockFwd.bOk && BlockRev.Rivals.Num() == 4 && BlockFwd.Rivals.Num() == 4;
		for (int32 i = 0; bSameStores && i < 4; ++i) { bSameStores &= BlockRev.Rivals[i].Store == BlockFwd.Rivals[i].Store; }
		const bool bSameMarket = BlockRev.bOk && BlockFwd.bOk && BlockRev.Markets.Num() == 1 && BlockFwd.Markets.Num() == 1 && BlockRev.Markets[0].Price == BlockFwd.Markets[0].Price
			&& BlockRev.Markets[0].Sold == BlockFwd.Markets[0].Sold && BlockRev.Markets[0].Bought == BlockFwd.Markets[0].Bought;
		const bool bPlaced = BlockRev.Rivals.Num() == 4 && BlockRev.Rivals[1].Name == TEXT("MxRevA") && BlockRev.Rivals[2].Name == TEXT("MxRevB") && BlockFwd.Rivals.Num() == 4 && BlockFwd.Rivals[1].Name == TEXT("MxFwdC") && BlockFwd.Rivals[2].Name == TEXT("MxFwdD");
		ExpectSeq({
			[&]() { return Ascending(BlockRev); },
			[&]() { return Ascending(BlockFwd); },
			[&]() { return bPlaced; },
			[&]() { return bSameStores; },
			[&]() { return bSameMarket; },
			[&]() { return IceRev == IceFwd; },
			},
			FString::Printf(TEXT("MX2 rivals injected in the reverse of their name order (table order MxRevB, MxRevA) and in the forward order (MxFwdC, MxFwdD) are both saved ascending by name (%s %s %s %s), with the same stores in the same saved order, the same market part and the same Ice price (%s cr/kg)"),
				BlockRev.Rivals.Num() > 0 ? *BlockRev.Rivals[0].Name : TEXT("?"), BlockRev.Rivals.Num() > 1 ? *BlockRev.Rivals[1].Name : TEXT("?"), BlockRev.Rivals.Num() > 2 ? *BlockRev.Rivals[2].Name : TEXT("?"), BlockRev.Rivals.Num() > 3 ? *BlockRev.Rivals[3].Name : TEXT("?"), *Cr(IceRev)));
	}

	// --- MX3. A glut: Zarya's Ice at its cap and the colony selling 1,000 kg of it every sol (the hook, before each roll) falls exactly 5% a sol,
	// truncated, to 0.75 cr and stays; then a real order sells 1,000 kg past Zarya's room: taken in full, the store stands above its cap,
	// Zarya's own production adds nothing until consumption brings it back under, and not a gram is deleted. ---
	{
		Reset();
		Sim->Debug_SetRivalStore(NZarya, NIce, 20000);
		int64 Prev = Sim->GetMarketPriceMcr(NIce);
		int32 FloorRoll = 0;
		bool bExact = Prev == 3000, bStore = true;
		for (int32 K = 1; K <= 40; ++K)
		{
			Sim->Debug_SetMarketTally(NIce, 1000, 0);
			RollNext();
			const int64 P = Sim->GetMarketPriceMcr(NIce);
			bExact &= P == FMath::Max<int64>(750, Prev - Prev * 500 / 10000);
			if (P == 750 && FloorRoll == 0) { FloorRoll = K; }
			bStore &= Sim->GetRivalStoreKg(NZarya, NIce) == 20000;
			Prev = P;
		}
		ExpectSeq({
			[&]() { return bExact; },
			[&]() { return FloorRoll == 28; },
			[&]() { return Prev == 750; },
			[&]() { return bStore; },
			},
			FString::Printf(TEXT("MX3 a glut: the Ice price falls by exactly 5%% of itself, truncated, every sol (the clamp binds all 28 sols) to the floor at roll %d (0.75 cr, %s now) and stays there; Zarya's store stays at its cap of 20,000 kg"), FloorRoll, *Cr(Prev)));
		Provision(1000000000);
		const double Out0 = Out(NIce);
		const bool bBooked = Order(NZarya, TEXT("Ice:-1000"));
		const bool bQuiet = Sim->GetRivalStoreKg(NZarya, NIce) == 20000 && Sim->GetCreditReservedMcr() == 0 && Sim->GetConvoys().Num() == 1 && !Sim->GetConvoys()[0].bCleared;
		RollNext();
		const int32 AtCap = Sim->GetRivalStoreKg(NZarya, NIce);
		int64 Sold = 0, Bought = 0;
		Sim->GetMarketTally(NIce, 1, Sold, Bought);
		bool bDrain = true;
		TArray<int32> Seen;
		for (int32 K = 1; K <= 6; ++K)
		{
			RollNext();
			Seen.Add(Sim->GetRivalStoreKg(NZarya, NIce));
			bDrain &= Seen.Last() == (K < 6 ? 20850 - 150 * K : 20000);
		}
		ExpectSeq({
			[&]() { return bBooked; },
			[&]() { return bQuiet; },
			[&]() { return AtCap == 20850; },
			[&]() { return Sold == 1000000; },
			[&]() { return Bought == 0; },
			[&]() { return bDrain; },
			[&]() { return Near(Out(NIce) - Out0, 1000.0); },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX3 a sale of 1,000 kg past Zarya's room is taken in full: the store reads 20,850 kg after its clearing roll (21,000 less one sol's 150 kg of use; its own 250 kg of net production added nothing), then 20,700 .. 20,100 as use brings it back, then 20,000 (%d); not a gram deleted; the tally holds the 1,000,000 g sold; E1's Out leg +1,000 kg"), Seen.Num() ? Seen.Last() : -1));
	}

	// --- MX4. A shortage: Zarya's Struct store at 0 and the colony buying 1,000 kg a sol (the hook) climbs 5% a sol for 15 sols, then more
	// slowly, and settles at 12,579 mcr (2.516 V) - never at the 4 V ceiling; then a real buy above the store fills to the store, and only the
	// fill is charged. ---
	{
		Reset();
		Sim->Debug_SetRivalStore(NZarya, NStruct, 0);
		int64 Prev = Sim->GetMarketPriceMcr(NStruct), Peak = Prev;
		int32 FullSteps = 0;
		bool bMonotone = true, bBand = true;
		for (int32 K = 1; K <= 80; ++K)
		{
			Sim->Debug_SetMarketTally(NStruct, 0, 1000);
			RollNext();
			const int64 P = Sim->GetMarketPriceMcr(NStruct);
			FullSteps += (P - Prev == Prev * 500 / 10000) ? 1 : 0;
			bMonotone &= P >= Prev && P - Prev <= Prev * 500 / 10000;
			bBand &= P <= 20000;
			Peak = FMath::Max(Peak, P);
			Prev = P;
		}
		ExpectSeq({
			[&]() { return Prev == 12579; },
			[&]() { return FullSteps == 15; },
			[&]() { return bMonotone; },
			[&]() { return bBand; },
			[&]() { return Peak < 20000; },
			},
			FString::Printf(TEXT("MX4 a shortage: the Struct price rises by at most 5%% a sol (15 full steps, then 440, 348 .. bp), never falls, settles at %s cr/kg (2.516 V), and the 20 cr ceiling is never reached"), *Cr(Prev)));
		Sim->Debug_SetRivalStore(NZarya, NStruct, 300);
		Provision(10000000);
		const int64 Quote = Sim->GetMarketPriceMcr(NStruct);
		const int64 Bal0 = Sim->GetCreditBalanceMcr();
		const bool bBooked = Order(NZarya, TEXT("Struct:500"));
		const int64 Reserve = Sim->GetCreditReservedMcr();
		const int64 WantReserve = 500 * (Quote * 10500 / 10000 + 1);
		RollNext();
		const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
		ExpectSeq({
			[&]() { return bBooked; },
			[&]() { return Reserve == WantReserve; },
			[&]() { return Rec; },
			[&]() { return Rec->Lines.Num() == 1; },
			[&]() { return Rec->Lines[0].FilledKg == 300; },
			[&]() { return Rec->NetMcr == 300 * Quote; },
			[&]() { return Rec->PaidMcr == 300 * Quote; },
			[&]() { return Rec->ReleasedMcr == Reserve; },
			[&]() { return Sim->GetCreditBalanceMcr() == Bal0 - 300 * Quote; },
			[&]() { return Sim->GetCreditReservedMcr() == 0; },
			[&]() { return Sim->GetRivalStoreKg(NZarya, NStruct) == 0; },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX4 a buy of 500 kg against a store of 300 reserves %s cr (500 x the one-step quote), fills 300 kg at the clearing, charges exactly 300 x %s = %s cr, releases the reservation and empties the store"), *Cr(Reserve), *Cr(Quote), *Cr(300 * Quote)));
	}

	// --- MX5. Bounded and integer, by enumeration: 10,010 reprices from a seeded LCG over stores, tallies and prices (half of them with the
	// fallback on) equal a reference written here with the same integer rules, every step is within 5% or 1 mcr, every price inside the band. ---
	{
		Reset();
		struct FSlotDef { FName Res, Owner; int64 ProdG, ConsG, CapG, V; };
		TArray<FSlotDef> Defs7;
		for (const FName Owner : { NZarya, NMeridian })
		{
			const FRHRivalRow* Row = DefsSub->GetRival(Owner);
			const TMap<FName, int64> Prod = MktSuiteList(Row->Produce), Cons = MktSuiteList(Row->Consume), Cap = MktSuiteList(Row->StoreCap);
			TArray<FName> Names;
			Cap.GetKeys(Names);
			Names.Sort([](const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; });
			for (const FName& N : Names)
			{
				FSlotDef D;
				D.Res = N; D.Owner = Owner; D.ProdG = Prod.FindRef(N) * 1000; D.ConsG = Cons.FindRef(N) * 1000; D.CapG = Cap.FindRef(N) * 1000;
				D.V = DefsSub->GetResource(N)->EarthValue_mcr;
				Defs7.Add(D);
			}
		}
		const int64 W = 5, MaxStep = 500, Gain = 20, WF = 50, WS = 30, WA = 50, Target = 5000, FloorBp = 2500, CeilBp = 40000, MinVol = 1000000;
		FMktSuiteLcg Rng;
		int64 Reprices = 0, Wrong = 0, StepBad = 0, BandBad = 0, Moved = 0;
		FString FirstWrong;
		for (int32 Iter = 0; Iter < 1430; ++Iter)
		{
			const bool bOnly = (Iter & 1) != 0;
			Sim->Debug_SetMarketRivalOnly(bOnly);
			TArray<int64> Store, Sold, Bought, Price;
			for (const FSlotDef& D : Defs7)
			{
				Store.Add(Rng.Range(0, D.CapG / 1000 * 12 / 10));
				Sold.Add(Rng.Range(0, 3) == 0 ? Rng.Range(0, 4000) : 0);
				Bought.Add(Rng.Range(0, 3) == 0 ? Rng.Range(0, 4000) : 0);
				Price.Add(Rng.Range(D.V * FloorBp / 10000, D.V * CeilBp / 10000));
				Sim->Debug_SetRivalStore(D.Owner, D.Res, Store.Last());
				Sim->Debug_SetMarketTally(D.Res, Sold.Last(), Bought.Last());
				Sim->Debug_SetMarketPrice(D.Res, Price.Last());
			}
			RollNext();
			for (int32 i = 0; i < Defs7.Num(); ++i)
			{
				const FSlotDef& D = Defs7[i];
				int64 After = Store[i] * 1000 + D.ProdG - D.ConsG;
				if (After > D.CapG) { After = FMath::Max(D.CapG, Store[i] * 1000 - D.ConsG); }
				const int64 S = FMath::Max<int64>(0, After);
				const int64 SoldW = bOnly ? 0 : (W - 1) * Sold[i] * 1000, BoughtW = bOnly ? 0 : (W - 1) * Bought[i] * 1000;
				const int64 Supply = D.ProdG * W + SoldW, Demand = D.ConsG * W + BoughtW;
				const int64 Flow = ((Supply - Demand) * 10000) / FMath::Max<int64>(Supply + Demand, MinVol);
				const int64 Fill = FMath::Min(S, D.CapG) * 10000 / FMath::Max<int64>(D.CapG, 1);
				const int64 StoreBp = bOnly ? 0 : FMath::Clamp((Fill - Target) * 2, (int64)-10000, (int64)10000);
				const int64 Anchor = ((Price[i] - D.V) * 10000) / D.V;
				const int64 Pressure = (Flow * WF + StoreBp * WS + Anchor * WA) / 100;
				const int64 Step = FMath::Clamp((-Pressure * Gain) / 100, -MaxStep, MaxStep);
				const int64 Delta = Step == 0 ? 0 : (Step > 0 ? 1 : -1) * FMath::Max<int64>(1, FMath::Abs(Price[i] * Step) / 10000);
				const int64 Want = FMath::Clamp(Price[i] + Delta, D.V * FloorBp / 10000, D.V * CeilBp / 10000);
				const int64 Got = Sim->GetMarketPriceMcr(D.Res);
				++Reprices;
				if (Got != Want) { ++Wrong; if (FirstWrong.IsEmpty()) { FirstWrong = FString::Printf(TEXT("%s iter %d price %lld store %lld sold %lld bought %lld only %d: got %lld want %lld"), *D.Res.ToString(), Iter, Price[i], Store[i], Sold[i], Bought[i], bOnly ? 1 : 0, Got, Want); } }
				if (FMath::Abs(Got - Price[i]) > FMath::Max<int64>(1, Price[i] * MaxStep / 10000)) { ++StepBad; }
				if (Got < D.V * FloorBp / 10000 || Got > D.V * CeilBp / 10000) { ++BandBad; }
				Moved += Got != Price[i] ? 1 : 0;
			}
		}
		Sim->Debug_SetMarketRivalOnly(false);
		ExpectSeq({
			[&]() { return Reprices >= 10000; },
			[&]() { return Wrong == 0; },
			[&]() { return StepBad == 0; },
			[&]() { return BandBad == 0; },
			[&]() { return Moved > 5000; },
			},
			FString::Printf(TEXT("MX5 %lld seeded reprices (half with the fallback): %lld differ from the reference, %lld steps beyond 5%% or 1 mcr, %lld prices outside [0.25 V, 4 V]; %lld moved %s"), Reprices, Wrong, StepBad, BandBad, Moved, *FirstWrong));
	}

	// --- MX6. Orders: every refusal of 3.2 with its text; a buy and sell order's draws and reservation at booking, nothing in the market until
	// the roll, then the fills, the stores, the tally, CreditSpend and the released reservation, and CreditPay and the landed goods at the
	// return, E1's legs exact; the reservation covers a full price step; the clock-jump catch-up; three convoys out and the limits; the spare
	// while a manifest is open; and E2's manifest, E3's launch and E3's short launch reading the spendable credit. ---
	{
		Reset();
		Provision(5000000);
		FString Why;
		struct FRefuse { const TCHAR* Text; const TCHAR* Needle; };
		const FRefuse Refusals[] = {
			{ TEXT(""), TEXT("The order has no lines") }, { TEXT("Ice"), TEXT("is not Resource:kg") }, { TEXT("Ice:0"), TEXT("is a zero") }, { TEXT("Ice:1.5"), TEXT("whole number of kg") },
			{ TEXT("Ice:+5"), TEXT("whole number of kg") }, { TEXT("Ice:5;Ice:7"), TEXT("Ice appears twice") }, { TEXT("Ice:5;Ice:-7"), TEXT("Ice is both bought and sold") },
			{ TEXT("Unobtainium:5"), TEXT("'Unobtainium' is not a resource") }, { TEXT("Ice:5;;Water:1"), TEXT("is not Resource:kg") }, { TEXT("Food:5"), TEXT("Zarya Post does not trade Food") },
			{ TEXT("Ice:1001"), TEXT("carries at most 1000 kg each way: this order buys 1001 kg and sells 0 kg") }, { TEXT("Ice:600;Water:600"), TEXT("buys 1200 kg") }, { TEXT("Struct:-1001"), TEXT("sells 1001 kg") },
			{ TEXT("Struct:-900"), TEXT("Convoy short of Struct (needs fuel 8 H2 + 1 parts + Struct:900)") } };
		// Zarya's display name is "Zarya Station"; the test rows' is "<Name> Post". Both texts name the row's own DisplayName.
		bool bAll = true;
		FString Bad;
		for (const FRefuse& R : Refusals)
		{
			FString Needle = R.Needle;
			if (Needle.StartsWith(TEXT("Zarya Post"))) { Needle = Needle.Replace(TEXT("Zarya Post"), TEXT("Zarya Station")); }
			const bool bOk = !Sim->CanPlaceOrder(NZarya, R.Text, Why) && Why.Contains(Needle);
			bAll &= bOk;
			if (!bOk) { Bad += FString::Printf(TEXT("'%s' -> '%s' (want '%s'); "), R.Text, *Why, *Needle); }
		}
		SetPool(NHyd, 5.0);
		const bool bFuel = !Sim->CanPlaceOrder(NZarya, TEXT("Ice:5"), Why) && Why.Contains(TEXT("Convoy short of Hydrogen (needs fuel 8 H2 + 1 parts + nothing)"));
		SetPool(NHyd, 600.0);
		Sim->Debug_ZeroCredits();
		Sim->Debug_GrantCredits(100000);
		const bool bMoney = !Sim->CanPlaceOrder(NZarya, TEXT("Ice:300"), Why) && Why.Contains(TEXT("The order may cost up to 945.300 cr net; 100 cr is spendable"));
		const FString MoneyWhy = Why;
		Provision(5000000);
		int64 Reserve = -1;
		const bool bOk = Sim->CanPlaceOrder(NZarya, TEXT("Ice:300;Struct:-100"), Why, &Reserve);
		ExpectSeq({
			[&]() { return bAll; },
			[&]() { return bFuel; },
			[&]() { return bMoney; },
			[&]() { return bOk; },
			[&]() { return Reserve == 470400; },
			},
			FString::Printf(TEXT("MX6 the written order's refusals, each by name (%d line and cargo refusals, a short sell, a short fuel, the money: '%s') and an acceptable order's reservation is 300 x Hi(3 cr) - 100 x Lo(5 cr) = %s cr %s"), (int32)UE_ARRAY_COUNT(Refusals), *MoneyWhy, *Cr(Reserve), *Bad));
		// The market and the contact.
		Sim->Debug_SetMarketEnabled(false);
		const bool bOff = !Sim->CanPlaceOrder(NZarya, TEXT("Ice:5"), Why) && Why == TEXT("No market: no neighbor trades yet");
		const bool bBarterOk = Sim->CanPlaceStandingOrder(NZarya, Why); // the bare Convoy is today's barter with the market off
		Sim->Debug_SetMarketEnabled(true);
		const bool bContact = !Sim->CanPlaceOrder(NMeridian, TEXT("Food:5"), Why) && Why == TEXT("No trade contact 'Meridian'") && !Sim->CanPlaceOrder(FName("Nowhere"), TEXT("Ice:5"), Why) && Why == TEXT("No trade contact 'Nowhere'");
		ExpectSeq({
			[&]() { return bOff; },
			[&]() { return bBarterOk; },
			[&]() { return bContact; },
			}, TEXT("MX6 with the market off a written order is refused ('No market: no neighbor trades yet') and the bare Convoy is still today's barter; a dormant or unknown rival gives 'No trade contact'"));
	}
	{
		// The cycle, exact.
		Reset();
		Provision(5000000);
		const double Struct0 = Held(NStruct), Hyd0 = Sim->GetStock(NHyd), Parts0 = Sim->GetTotalSolid(NParts) + Sim->GetStock(NParts), IceHeld0 = Held(NIce);
		const double OutStruct0 = Out(NStruct), UsedHyd0 = Used(NHyd), UsedParts0 = Used(NParts), InIce0 = In(NIce);
		const int64 Bal0 = Sim->GetCreditBalanceMcr();
		const int32 IceStore0 = Sim->GetRivalStoreKg(NZarya, NIce), StructStore0 = Sim->GetRivalStoreKg(NZarya, NStruct);
		Capture.Clear();
		const bool bBooked = Order(NZarya, TEXT("Ice:300;Struct:-100"));
		const FRHConvoy* C = Sim->GetConvoys().Num() > 0 ? &Sim->GetConvoys()[0] : nullptr;
		ExpectSeq({
			[&]() { return bBooked; },
			[&]() { return C; },
			[&]() { return C->Id == 1; },
			[&]() { return C->IsPriced(); },
			[&]() { return !C->bCleared; },
			[&]() { return !C->bStanding; },
			[&]() { return C->ClearSol == 0; },
			[&]() { return C->ReservedMcr == 470400; },
			[&]() { return C->Lines.Num() == 2; },
			[&]() { return C->Lines[0].Resource == NIce; },
			[&]() { return C->Lines[0].Kg == 300; },
			[&]() { return C->Lines[1].Resource == NStruct; },
			[&]() { return C->Lines[1].Kg == -100; },
			[&]() { return Near(Struct0 - Held(NStruct), 100.0); },
			[&]() { return Near(Hyd0 - Sim->GetStock(NHyd), 8.0); },
			[&]() { return Near(Parts0 - (Sim->GetTotalSolid(NParts) + Sim->GetStock(NParts)), 1.0); },
			[&]() { return Sim->GetCreditReservedMcr() == 470400; },
			[&]() { return Sim->GetSpendableMcr() == Bal0 - 470400; },
			[&]() { return Sim->GetCreditBalanceMcr() == Bal0; },
			[&]() { return Near(Out(NStruct) - OutStruct0, 100.0); },
			[&]() { return Near(Used(NHyd) - UsedHyd0, 8.0); },
			[&]() { return Near(Used(NParts) - UsedParts0, 1.0); },
			[&]() { return Near(In(NIce) - InIce0, 0.0); },
			[&]() { return Sim->GetRivalStoreKg(NZarya, NIce) == IceStore0; },
			[&]() { return Sim->GetRivalStoreKg(NZarya, NStruct) == StructStore0; },
			[&]() { return Sim->GetMarketPriceMcr(NIce) == 3000; },
			[&]() { return Sim->GetMarketPriceMcr(NStruct) == 5000; },
			[&]() { return Sim->GetMarketCheckedSol() == 0; },
			[&]() { return Capture.CountLines(TEXT("Order #1 to Zarya: buy Ice:300, sell Struct:100; clears at the end of sol 0; convoy 1 of 3 out")) == 1; },
			[&]() { return Inv(); },
			[&]() { return Sim->GetCreditReservedMcr() == ReservedSum(); },
			},
			FString::Printf(TEXT("MX6 booking 'Ice:300;Struct:-100': the convoy leaves at once with its draws (Struct -100, Hydrogen -8, parts -1), 470,400 cr-mcr reserved (spendable %s of %s cr), E1 Out Struct +100 and Used Hydrogen +8, parts +1; the market is untouched until the roll (stores %d and %d kg, prices 3 and 5 cr); the log line says when it clears"), *Cr(Sim->GetSpendableMcr()), *Cr(Bal0), IceStore0, StructStore0));
		RollNext();
		const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
		int64 Sold = 0, Bought = 0, Sold2 = 0, Bought2 = 0;
		Sim->GetMarketTally(NStruct, 1, Sold, Bought);
		Sim->GetMarketTally(NIce, 1, Sold2, Bought2);
		C = Sim->GetConvoys().Num() > 0 ? &Sim->GetConvoys()[0] : nullptr;
		ExpectSeq({
			[&]() { return Rec; },
			[&]() { return Rec->ConvoyId == 1; },
			[&]() { return Rec->ClosingSol == 0; },
			[&]() { return Rec->NetMcr == 400000; },
			[&]() { return Rec->PaidMcr == 400000; },
			[&]() { return Rec->WrittenOffMcr == 0; },
			[&]() { return Rec->ReleasedMcr == 470400; },
			[&]() { return Rec->ValueMcr == 1400000; },
			[&]() { return C; },
			[&]() { return C->bCleared; },
			[&]() { return C->OwedMcr == 0; },
			[&]() { return C->ValueMcr == 1400000; },
			[&]() { return C->ReservedMcr == 0; },
			[&]() { return C->Lines[0].FilledKg == 300; },
			[&]() { return C->Lines[1].FilledKg == 100; },
			[&]() { return Sim->GetCreditBalanceMcr() == Bal0 - 400000; },
			[&]() { return Sim->GetCreditReservedMcr() == 0; },
			[&]() { return Sim->GetRivalStoreKg(NZarya, NIce) == IceStore0 - 300 + 250; },
			[&]() { return Sim->GetRivalStoreKg(NZarya, NStruct) == StructStore0 + 100 - 80; },
			[&]() { return Sold == 100000; },
			[&]() { return Bought == 0; },
			[&]() { return Sold2 == 0; },
			[&]() { return Bought2 == 300000; },
			[&]() { return Sim->GetMarketCheckedSol() == 1; },
			[&]() { return Rec->PaidMcr <= Rec->ReleasedMcr; },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX6 the roll clears it: Net = 300 x 3 - 100 x 5 = %s cr paid through CreditSpend (at most the %s cr reserved, which is released), the fills are 300 and 100 kg, the stores %d (-300 + 250 made) and %d kg (+100 - 80 used), the tally holds 100,000 g sold and 300,000 g bought, the balance %s cr"),
				Rec ? *Cr(Rec->NetMcr) : TEXT("?"), Rec ? *Cr(Rec->ReleasedMcr) : TEXT("?"), Sim->GetRivalStoreKg(NZarya, NIce), Sim->GetRivalStoreKg(NZarya, NStruct), *Cr(Sim->GetCreditBalanceMcr())));
		Capture.Clear();
		int32 Steps = 0;
		while (Sim->GetConvoys().Num() > 0 && Steps++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
		ExpectSeq({
			[&]() { return Sim->GetConvoys().Num() == 0; },
			[&]() { return Sim->GetRivalRelation(NZarya) == 42.0; },
			[&]() { return Near(In(NIce) - InIce0, 300.0); },
			[&]() { return Sim->GetCreditBalanceMcr() == Bal0 - 400000; },
			[&]() { return Sim->GetMarketSalesMcr() == 0; },
			[&]() { return Capture.CountLines(TEXT("=== CONVOY RETURN: Zarya Station delivered Ice:300, relation 42 ===")) == 1; },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX6 the return (%d era steps, 4 sols): 300 kg of Ice lands (E1 In +300), nothing is owed (the colony paid), the relation warms 40 -> 42 (the order was worth 1,400 cr, over the 300 cr floor), and the line reads 'delivered Ice:300'"), Steps));
	}
	{
		// A full price step between booking and the clearing: the charge never outruns the reservation.
		Reset();
		Provision(5000000);
		Order(NZarya, TEXT("Ice:300;Struct:-100"));
		const int64 R = Sim->GetCreditReservedMcr();
		Sim->Debug_SetMarketPrice(NIce, 3000 + 3000 * 500 / 10000);     // the buy gets a full step dearer
		Sim->Debug_SetMarketPrice(NStruct, 5000 - 5000 * 500 / 10000);  // the sale gets a full step cheaper
		RollNext();
		const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
		ExpectSeq({
			[&]() { return R == 470400; },
			[&]() { return Rec; },
			[&]() { return Rec->NetMcr == 300 * 3150 - 100 * 4750; },
			[&]() { return Rec->PaidMcr == Rec->NetMcr; },
			[&]() { return Rec->PaidMcr <= Rec->ReleasedMcr; },
			[&]() { return Rec->ReleasedMcr == R; },
			[&]() { return Rec->WrittenOffMcr == 0; },
			[&]() { return Sim->GetCreditReservedMcr() == 0; },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX6 with Ice a full step dearer (3.15) and Struct a full step cheaper (4.75) between booking and clearing the charge is %s cr, under the %s cr reserved, and CreditSpend has no rest"), Rec ? *Cr(Rec->PaidMcr) : TEXT("?"), *Cr(R)));
	}
	{
		// The catch-up: a 5-sol clock jump before a booking rolls the market to the order's sol (logging nothing); the order books at the
		// caught-up quote.
		Reset();
		Provision(5000000);
		Clock->Debug_SetSimSeconds(5.0 * SolLen);
		Capture.Clear();
		const bool bBooked = Order(NZarya, TEXT("Ice:300"));
		const int64 P = Sim->GetMarketPriceMcr(NIce);
		const FRHConvoy* C = Sim->GetConvoys().Num() > 0 ? &Sim->GetConvoys()[0] : nullptr;
		ExpectSeq({
			[&]() { return bBooked; },
			[&]() { return Sim->GetMarketCheckedSol() == 5; },
			[&]() { return P == 2334; },
			[&]() { return C; },
			[&]() { return C->ClearSol == 5; },
			[&]() { return C->ReservedMcr == 300 * (P * 10500 / 10000 + 1); },
			[&]() { return Capture.CountLines(TEXT("Order #")) == 1; },
			[&]() { return Capture.NumWarnings() == 0; },
			[&]() { return Capture.NumErrors() == 0; },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX6 a 5-sol jump then an order: the worker rolls the market to the order's sol first (open sol %d, Ice %lld mcr after five reprices), the order books at that quote (%s cr reserved), and nothing but the order's own line is logged"), Sim->GetMarketCheckedSol(), P, C ? *Cr(C->ReservedMcr) : TEXT("?")));
	}
	{
		// Three convoys out to three economy rivals; a fourth is refused, and so is a second to the same rival.
		Reset();
		Provision(50000000);
		Inject(FName("MxA"), 40.0, TEXT("Ice:50"), TEXT("Ice:10"), TEXT("Ice:700"), TEXT("Ice:4000"), TEXT(""), TEXT(""), 0);
		Inject(FName("MxB"), 55.0, TEXT("Ice:20;Water:3"), TEXT("Ice:5"), TEXT("Ice:300;Water:100"), TEXT("Ice:3000;Water:900"), TEXT(""), TEXT(""), 0);
		Inject(FName("MxC"), 70.0, TEXT("Ice:20"), TEXT("Ice:5"), TEXT("Ice:300"), TEXT("Ice:3000"), TEXT(""), TEXT(""), 0);
		Sim->Debug_SyncRivalEconomies();
		const bool b1 = Order(NZarya, TEXT("Ice:100")), b2 = Order(FName("MxA"), TEXT("Ice:100")), b3 = Order(FName("MxB"), TEXT("Water:10"));
		Rejects.Reset();
		const bool b4 = Order(FName("MxC"), TEXT("Ice:100"));
		const FString Fourth = Rejects.Num() > 0 ? Rejects.Last() : FString();
		Rejects.Reset();
		const bool b5 = Order(NZarya, TEXT("Water:10"));
		const FString Second = Rejects.Num() > 0 ? Rejects.Last() : FString();
		FString W1, W2;
		const bool bPlan = !Sim->CanPlaceOrder(FName("MxC"), TEXT("Ice:100"), W1) && W1 == TEXT("3 convoys are already out") && !Sim->CanPlaceOrder(NZarya, TEXT("Water:10"), W2) && W2 == TEXT("The convoy is already out (to Zarya)");
		bool bIds = Sim->GetConvoys().Num() == 3;
		for (int32 i = 0; bIds && i < 3; ++i) { bIds &= Sim->GetConvoys()[i].Id == i + 1; }
		ExpectSeq({
			[&]() { return b1; },
			[&]() { return b2; },
			[&]() { return b3; },
			[&]() { return !b4; },
			[&]() { return !b5; },
			[&]() { return Fourth == TEXT("3 convoys are already out"); },
			[&]() { return Second == TEXT("The convoy is already out (to Zarya)"); },
			[&]() { return bPlan; },
			[&]() { return bIds; },
			[&]() { return Sim->GetConvoys().Num() == 3; },
			[&]() { return Sim->GetCreditReservedMcr() == ReservedSum(); },
			[&]() { return Inv(); },
			[&]() { return Sim->GetConvoyRival() == NZarya; },
			},
			FString::Printf(TEXT("MX6 three convoys are out (Zarya, MxA, MxB, by Id; the lowest-Id readers see Zarya); a fourth is refused ('%s') and so is a second to Zarya ('%s'); the reservations sum to the counter (%s cr)"), *Fourth, *Second, *Cr(Sim->GetCreditReservedMcr())));
		// They all clear on one roll and come home on their own legs.
		RollNext();
		bool bCleared = true;
		for (const FRHConvoy& C : Sim->GetConvoys()) { bCleared &= C.bCleared; }
		int32 Steps = 0;
		while (Sim->GetConvoys().Num() > 0 && Steps++ < 12 * (int32)(SolLen / EraDt)) { EraOnce(); }
		ExpectSeq({
			[&]() { return bCleared; },
			[&]() { return Sim->GetConvoys().Num() == 0; },
			[&]() { return Sim->GetCreditReservedMcr() == 0; },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX6 one roll clears all three; each comes home on its own leg (%d era steps) and the reservation counter is back to 0"), Steps));
		RetireAll();
	}
	{
		// While a manifest is open Earth is promised the bill and the listed sales: a written order's sells draw only from E3's spare.
		Reset();
		Provision(5000000);
		Sim->Debug_SetEarthTakesQuota(false);
		FString Err, Why;
		const bool bOpened = Sim->Debug_OpenManifestWithAward(0.0);
		const bool bListed = Sim->AddExportLine(NStruct, 350, Err); // Struct held: the Lander's 400 kg; spare = 400 - 350 = 50
		const bool bWrittenRefused = !Sim->CanPlaceOrder(NZarya, TEXT("Struct:-100"), Why) && Why.Contains(TEXT("Only 50 kg of Struct is spare above Earth's quota and sales"));
		const bool bWrittenOk = Sim->CanPlaceOrder(NZarya, TEXT("Struct:-50"), Why);
		const bool bStandingOk = Sim->CanPlaceStandingOrder(NZarya, Why); // the standing order keeps HEAD's rule: it sells 100 Struct against the 400 loadable
		Sim->Debug_ResetManifest();
		const bool bOpenOk = Sim->CanPlaceOrder(NZarya, TEXT("Struct:-100"), Why); // no manifest open: the plain loadable rule
		ExpectSeq({
			[&]() { return bOpened; },
			[&]() { return bListed; },
			[&]() { return bWrittenRefused; },
			[&]() { return bWrittenOk; },
			[&]() { return bStandingOk; },
			[&]() { return bOpenOk; },
			},
			TEXT("MX6 with a manifest open and 350 kg of Struct listed for Earth (400 held), a written sale of 100 kg is refused (50 spare), 50 kg is accepted, the standing order (100 kg) keeps HEAD's rule, and with no manifest open the 100 kg sale is accepted"));
	}
	{
		// E2's manifest, E3's launch and E3's short launch read the spendable credit, never the balance.
		Reset();
		Provision(1500000);
		Order(NZarya, TEXT("Ice:300")); // reserves 300 x Hi(3 cr) = 945,300 mcr: spendable 554,700 of the 1,500,000 balance
		const int64 Spendable = Sim->GetSpendableMcr(), Bal = Sim->GetCreditBalanceMcr();
		Sim->Debug_SetEarthTakesQuota(false);
		FString W1, W2, W3;
		const bool bOpened = Sim->Debug_OpenManifestWithAward(0.0);
		const bool bAddRefused = !Sim->CanAddManifestItem(FName("SoilPallet"), W1) && W1.Contains(TEXT("Over budget")) && W1.Contains(TEXT("554.700"));
		Sim->Debug_PushManifestItem(FName("SoilPallet")); // aboard with no check: 1,000 cr, over the spendable 554.7 and under the balance 1,500
		const bool bLaunchRefused = !Sim->CanLaunchShip(W2) && W2.Contains(TEXT("Over budget at launch")) && W2.Contains(TEXT("554.700"));
		Clock->Debug_SetSimSeconds(10.0 * SolLen); // past the short-launch window
		FString ShortErr;
		const bool bShortRefused = !Sim->LaunchShipShort(ShortErr) && ShortErr.Contains(TEXT("Over budget at launch")) && Sim->GetCreditBalanceMcr() == Bal && Sim->GetCreditReservedMcr() == 945300
			&& Sim->GetQuotaPhase() == ERHQuotaPhase::AwaitingManifest;
		const bool bLaunchPlain = !Sim->LaunchShip(W3) && W3.Contains(TEXT("Over budget at launch")) && Sim->GetCreditBalanceMcr() == Bal;
		ExpectSeq({
			[&]() { return bOpened; },
			[&]() { return Spendable == Bal - 945300; },
			[&]() { return bAddRefused; },
			[&]() { return bLaunchRefused; },
			[&]() { return bShortRefused; },
			[&]() { return bLaunchPlain; },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX6 with 945.3 cr reserved of a 1,500 cr balance (spendable %s): ADD of a 1,000 cr item is refused, the launch is refused ('%s'), a short launch is refused when only reserved credit would pay for it, the balance and the reservation untouched"), *Cr(Spendable), *W2));
	}

	// --- MX7. The standing order: a bare Convoy to Zarya sells 100 Struct, buys 150 Ice, warms the relation 40 -> 42, logs HEAD's two lines
	// byte for byte, and at reference prices pays the colony 50 cr at its return; a rival with no economy trades by barter with no credit;
	// the goodwill floor holds for a written order and never for the standing one. ---
	{
		Reset();
		Sim->AddStock(NHyd, 40.0);
		const double Struct0 = Held(NStruct), Hyd0 = Sim->GetStock(NHyd), Parts0 = Sim->GetTotalSolid(NParts) + Sim->GetStock(NParts);
		const double OutStruct0 = Out(NStruct), InIce0 = In(NIce), IceShelf0 = Held(NIce);
		Capture.Clear();
		const bool bSent = Send(TEXT("Convoy"), NZarya, FString());
		const FRHConvoy* C = Sim->GetConvoys().Num() > 0 ? &Sim->GetConvoys()[0] : nullptr;
		ExpectSeq({
			[&]() { return bSent; },
			[&]() { return C; },
			[&]() { return C->bStanding; },
			[&]() { return C->IsPriced(); },
			[&]() { return C->ReservedMcr == 0; },
			[&]() { return Sim->GetCreditReservedMcr() == 0; },
			[&]() { return Sim->GetCreditBalanceMcr() == 0; },
			[&]() { return C->Lines.Num() == 2; },
			[&]() { return C->Lines[0].Kg == 150; },
			[&]() { return C->Lines[1].Kg == -100; },
			[&]() { return Near(Struct0 - Held(NStruct), 100.0); },
			[&]() { return Near(Hyd0 - Sim->GetStock(NHyd), 8.0); },
			[&]() { return Near(Parts0 - (Sim->GetTotalSolid(NParts) + Sim->GetStock(NParts)), 1.0); },
			[&]() { return Near(Out(NStruct) - OutStruct0, 100.0); },
			[&]() { return Capture.CountLines(TEXT("Convoy dispatched to Zarya Station (120 km; Struct:100 -> Ice:150)")) == 1; },
			[&]() { return Capture.CountLines(TEXT("Order #")) == 0; },
			},
			TEXT("MX7 a bare Convoy to Zarya books its standing order: Struct -100, Hydrogen -8, parts -1, its lines Ice +150 and Struct -100, nothing reserved at reference prices (472,650 against 474,900 mcr) so it books at a zero balance, and the log says 'Convoy dispatched to Zarya Station (120 km; Struct:100 -> Ice:150)' and nothing about an order"));
		RollNext();
		const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
		C = Sim->GetConvoys().Num() > 0 ? &Sim->GetConvoys()[0] : nullptr;
		const bool bCleared = Rec && Rec->NetMcr == -50000 && Rec->PaidMcr == 0 && C && C->bCleared && C->OwedMcr == 50000 && Sim->GetCreditBalanceMcr() == 0 && Capture.CountLines(TEXT("clears:")) == 0;
		int32 Steps = 0;
		Capture.Clear();
		while (Sim->GetConvoys().Num() > 0 && Steps++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
		ExpectSeq({
			[&]() { return bCleared; },
			[&]() { return Sim->GetCreditBalanceMcr() == 50000; },
			[&]() { return Sim->GetMarketSalesMcr() == 50000; },
			[&]() { return Sim->GetRivalRelation(NZarya) == 42.0; },
			[&]() { return Near(In(NIce) - InIce0, 150.0); },
			[&]() { return Near(Held(NIce) - IceShelf0, 150.0); },
			[&]() { return Capture.CountLines(TEXT("=== CONVOY RETURN: Zarya Station delivered Ice:150, relation 42 ===")) == 1; },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX7 the clearing owes the colony 50 cr (150 x 3 - 100 x 5) and logs nothing for a standing order; at the return 150 kg of Ice lands, the relation is 42, the line reads 'delivered Ice:150, relation 42' as at HEAD, and the colony is paid %s cr through CreditPay (market sales %s)"), *Cr(Sim->GetCreditBalanceMcr()), *Cr(Sim->GetMarketSalesMcr())));
		// A short fill prints the kg that landed.
		Reset();
		Sim->AddStock(NHyd, 40.0);
		Send(TEXT("Convoy"), NZarya, FString());
		Sim->Debug_SetRivalStore(NZarya, NIce, 100);
		RollNext();
		Capture.Clear();
		Steps = 0;
		while (Sim->GetConvoys().Num() > 0 && Steps++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
		ExpectSeq({
			[&]() { return Capture.CountLines(TEXT("=== CONVOY RETURN: Zarya Station delivered Ice:100, relation 42 ===")) == 1; },
			[&]() { return Sim->GetCreditBalanceMcr() == 200000; },
			[&]() { return Sim->GetMarketSalesMcr() == 200000; },
			[&]() { return Inv(); },
			},
			TEXT("MX7 a standing order against a store of 100 kg fills 100 of the 150 kg: the return prints 'delivered Ice:100' (the kg that landed), still warms the relation, and brings home the 200 cr the smaller buy leaves owing (100 x 5 - 100 x 3)"));
		// A rival with no economy - Greed's shape, and a row with its columns emptied - trades by barter with no credit.
		const FRHRivalRow* Zr = DefsSub->GetRival(NZarya);
		Reset();
		Sim->AddStock(NHyd, 40.0);
		Sim->AddStock(NParts, 10.0);
		Inject(FName("MxGreed"), 60.0, Zr->Produce, Zr->Consume, Zr->StoreStart, Zr->StoreCap, TEXT("Ice:10"), TEXT("SpareParts:4"), 0);
		Inject(FName("MxBare"), 60.0, TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("Ice:10"), TEXT("Struct:10"), 0);
		Sim->Debug_SyncRivalEconomies();
		const double PartsBefore = Sim->GetTotalSolid(NParts) + Sim->GetStock(NParts);
		Capture.Clear();
		const bool bGreed = Send(TEXT("Convoy"), FName("MxGreed"), FString());
		const FRHConvoy* G = Sim->GetConvoys().Num() > 0 ? &Sim->GetConvoys()[0] : nullptr;
		const bool bBarter = bGreed && G && !G->IsPriced() && G->bCleared && G->ReservedMcr == 0 && G->Lines.Num() == 0 && Near(PartsBefore - (Sim->GetTotalSolid(NParts) + Sim->GetStock(NParts)), 5.0)
			&& Capture.CountLines(TEXT("Convoy dispatched to MxGreed Post (60 km; SpareParts:4 -> Ice:10)")) == 1;
		const int32 ClearingsBefore = Sim->GetMarketClearings().Num();
		RollNext();
		const bool bUntouched = Sim->GetMarketClearings().Num() == ClearingsBefore && Sim->GetConvoys().Num() == 1;
		const double IceBefore = In(NIce);
		Steps = 0;
		while (Sim->GetConvoys().Num() > 0 && Steps++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
		const bool bBare = Send(TEXT("Convoy"), FName("MxBare"), FString()) && Sim->GetConvoys().Num() == 1 && !Sim->GetConvoys()[0].IsPriced() && Sim->GetCreditReservedMcr() == 0;
		ExpectSeq({
			[&]() { return bBarter; },
			[&]() { return bUntouched; },
			[&]() { return Near(In(NIce) - IceBefore, 10.0); },
			[&]() { return Sim->GetCreditBalanceMcr() == 0; },
			[&]() { return Sim->GetMarketSalesMcr() == 0; },
			[&]() { return bBare; },
			[&]() { return Inv(); },
			},
			TEXT("MX7 a rival with no economy trades by barter exactly as before: Greed's shape books a barter convoy (no lines, nothing reserved, no clearing, parts -5), brings its 10 kg of Ice home with no credit; a row with its columns emptied does the same"));
		// The goodwill floor: a written order worth under 300 cr comes home with no relation change; one over it warms; the standing order
		// warms at any value (at the floor prices its value is 237.5 cr).
		const auto Cycle = [&](const TCHAR* Verb, const FString& Text, bool bFloorPrices) -> double
		{
			Reset();
			Provision(5000000);
			if (bFloorPrices) { Sim->Debug_SetMarketPrice(NIce, 750); Sim->Debug_SetMarketPrice(NStruct, 1250); }
			if (!Send(Verb, NZarya, Text)) { return -1.0; }
			RollNext();
			int32 K = 0;
			while (Sim->GetConvoys().Num() > 0 && K++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
			return Sim->GetRivalRelation(NZarya);
		};
		const double RelSmall = Cycle(TEXT("MarketOrder"), TEXT("Ice:1"), false);                      // 3 cr: under the floor
		const double RelBig = Cycle(TEXT("MarketOrder"), TEXT("Ice:200;Struct:-100"), false);            // 1,100 cr: over it
		const double RelStanding = Cycle(TEXT("Convoy"), FString(), true);                                // 237.5 cr, but the standing order
		ExpectSeq({
			[&]() { return RelSmall == 40.0; },
			[&]() { return RelBig == 42.0; },
			[&]() { return RelStanding == 42.0; },
			},
			FString::Printf(TEXT("MX7 the goodwill floor: a written order worth 3 cr leaves the relation at %.0f, one worth 1,100 cr warms it to %.0f, and the standing order at the floor prices (worth 237.5 cr, under 300) still warms it to %.0f"), RelSmall, RelBig, RelStanding));
		RetireAll();
	}

	// --- MX9. The save: two convoys out and a moved market round-trip byte for byte; the block's layout; the v17 slot mirrors the lowest-Id
	// convoy; the previous version is refused; a lost market block starts the market fresh (the open sol from C5d's) and brings the v17
	// convoy home empty; a lost earlier block never touches it; and every repair, on a doctored file, with the colony loaded. ---
	{
		Reset();
		Provision(50000000);
		Inject(FName("MxKes"), 30.0, TEXT("Ice:100"), TEXT(""), TEXT("Ice:1000"), TEXT("Ice:5000"), TEXT(""), TEXT(""), 0);
		Sim->Debug_SyncRivalEconomies();
		Order(NZarya, TEXT("Ice:300;Struct:-100")); // convoy 1, booked in sol 0
		for (int32 K = 0; K < 30 && Clock->GetSol() < 1; ++K) { EraOnce(); } // the real roll at sol 1 clears it
		Order(FName("MxKes"), TEXT("Ice:100"));     // convoy 2, booked in sol 1: waiting to clear
		FString Err;
		Expect(Sim->SaveColony(TEXT("mktmx9"), Err), FString::Printf(TEXT("MX9 the fixture is saved (%s)"), *Err));
		TArray<uint8> W;
		FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("mktmx9")));
		uint32 Ver = 0;
		if (W.Num() > 8) { FMemory::Memcpy(&Ver, W.GetData() + 4, 4); }
		FMktSuiteBlock B;
		const bool bParsed = MktSuiteParse(W, B);
		bool bSlotsAsc = B.Slots.Num() == 18, bRivalsAsc = B.Rivals.Num() == 3;
		for (int32 i = 1; i < B.Slots.Num(); ++i) { bSlotsAsc &= B.Slots[i - 1].Compare(B.Slots[i], ESearchCase::CaseSensitive) < 0; }
		for (int32 i = 1; i < B.Rivals.Num(); ++i) { bRivalsAsc &= B.Rivals[i - 1].Name.Compare(B.Rivals[i].Name, ESearchCase::CaseSensitive) < 0; }
		const int32 IceSlot = B.Slots.IndexOfByKey(TEXT("Ice")), StructSlot = B.Slots.IndexOfByKey(TEXT("Struct")), FoodSlot = B.Slots.IndexOfByKey(TEXT("Food"));
		const int32 ZaryaAt = B.Rivals.IndexOfByPredicate([](const FMktSuiteBlock::FRivalRec& R) { return R.Name == TEXT("Zarya"); });
		const int32 MeridianAt = B.Rivals.IndexOfByPredicate([](const FMktSuiteBlock::FRivalRec& R) { return R.Name == TEXT("Meridian"); });
		const int64 Reserved0 = Sim->GetCreditReservedMcr(), Bal0 = Sim->GetCreditBalanceMcr();
		const bool bShape = bParsed && Ver == 59 && B.PayloadBytes == Sim->GetMarketBlockBytes() && bSlotsAsc && bRivalsAsc && B.Markets.Num() == 1 && B.Convoys.Num() == 2 && B.Next == 3
			&& B.Convoys[0].Id == 1 && B.Convoys[0].Rival == TEXT("Zarya") && B.Convoys[0].Cleared == 1 && B.Convoys[0].Lines.Num() == 2 && B.Convoys[0].Lines[0].Filled == 300
			&& B.Convoys[1].Id == 2 && B.Convoys[1].Rival == TEXT("MxKes") && B.Convoys[1].Cleared == 0 && B.Convoys[1].ClearSol == 1 && B.Convoys[1].Held > 0 && B.Reserved == Reserved0 && B.Convoys[1].Held == Reserved0
			&& B.Checked == 1 && B.Markets[0].Fill == 1 && IceSlot != INDEX_NONE && B.Markets[0].Price[IceSlot] == Sim->GetMarketPriceMcr(NIce) && ZaryaAt != INDEX_NONE && B.Rivals[ZaryaAt].Store[IceSlot] == Sim->GetRivalStoreG(NZarya, NIce);
		Expect(bShape,
			FString::Printf(TEXT("MX9 v%u: the market block is a frame ('RHE4', %d payload bytes = GetMarketBlockBytes()) of 18 slot names ascending, 1 region, 3 rivals ascending (%s %s %s), and 2 convoys by Id: #1 to Zarya cleared with its fills 300 and 100, #2 to MxKes waiting (clears at the end of sol 1, %s cr held = the counter); next convoy Id 3, open sol 1"),
				Ver, B.PayloadBytes, B.Rivals.Num() > 0 ? *B.Rivals[0].Name : TEXT("?"), B.Rivals.Num() > 1 ? *B.Rivals[1].Name : TEXT("?"), B.Rivals.Num() > 2 ? *B.Rivals[2].Name : TEXT("?"), *Cr(Reserved0)));
		// The v17 slot: the rival's name as a string, the returning byte, the leg's double - that of the LOWEST-Id convoy; None / 0 / 0 with none out.
		const auto HasSlot = [](const TArray<uint8>& Bytes, int32 Limit, const FString& Name, uint8 Ret, double Leg) -> bool
		{
			TArray<uint8> Pat;
			const int32 Len = Name.Len() + 1;
			Pat.Append((const uint8*)&Len, 4);
			for (int32 i = 0; i < Name.Len(); ++i) { Pat.Add((uint8)Name[i]); }
			Pat.Add(0);
			Pat.Add(Ret);
			Pat.Append((const uint8*)&Leg, 8);
			for (int32 At = 0; At + Pat.Num() <= Limit; ++At)
			{
				if (FMemory::Memcmp(Bytes.GetData() + At, Pat.GetData(), Pat.Num()) == 0) { return true; }
			}
			return false;
		};
		TArray<uint8> WBase;
		FFileHelper::LoadFileToArray(WBase, *SaveFilePath(TEXT("marketbase")));
		FMktSuiteBlock BBase;
		MktSuiteParse(WBase, BBase);
		// Searched before the market block only: the block holds the same string, byte and double for every convoy.
		const bool bSlotOne = bParsed && HasSlot(W, B.FrameAt, TEXT("Zarya"), B.Convoys[0].Returning, B.Convoys[0].Leg) && !HasSlot(W, B.FrameAt, TEXT("MxKes"), B.Convoys[1].Returning, B.Convoys[1].Leg);
		Expect(bSlotOne && HasSlot(WBase, BBase.FrameAt, TEXT("None"), 0, 0.0),
			TEXT("MX9 the v17 convoy slot mirrors the lowest-Id convoy (Zarya's, not MxKes's), and reads None / 0 / 0 in a colony with none out"));
		FString Le;
		const bool bLoaded = Sim->LoadColony(TEXT("mktmx9"), Le) && Le.IsEmpty() && Sim->SaveColony(TEXT("mktmx9b2"), Err);
		TArray<uint8> W2;
		ExpectSeq({
			[&]() { return bLoaded; },
			[&]() { return FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("mktmx9b2"))); },
			[&]() { return W2 == W; },
			[&]() { return Sim->GetConvoys().Num() == 2; },
			[&]() { return Sim->GetCreditReservedMcr() == Reserved0; },
			[&]() { return Sim->GetConvoyRival() == NZarya; },
			[&]() { return Inv(); },
			},
			FString::Printf(TEXT("MX9 two convoys out with an injected rival and a moved market: save, load, save is byte-identical (%d bytes)"), W.Num()));
		const auto TryBytes = [&](const TCHAR* Slot, const TArray<uint8>& Bytes, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Bytes, *SaveFilePath(Slot));
			Capture.Clear();
			Rejects.Reset();
			FString E;
			const bool bOk = Sim->LoadColony(Slot, E);
			OutErr = E;
			return bOk && E.IsEmpty();
		};
		const auto Sealed = [](TArray<uint8> T) { RHSaveFrame::Reseal(T, RHSaveFrame::MarketTag); return T; };
		const auto Put32 = [](TArray<uint8>& T, int32 Off, int32 V) { FMemory::Memcpy(T.GetData() + Off, &V, 4); };
		const auto Put64 = [](TArray<uint8>& T, int32 Off, int64 V) { FMemory::Memcpy(T.GetData() + Off, &V, 8); };
		{
			TArray<uint8> T = W; const uint32 V54 = 54; FMemory::Memcpy(T.GetData() + 4, &V54, 4);
			FString E;
			const bool bOk = TryBytes(TEXT("mktmx9r1"), T, E);
			ExpectSeq({
			[&]() { return !bOk; },
			[&]() { return E.Contains(TEXT("Save format mismatch")); },
			[&]() { return E.Contains(TEXT("/59")); },
			}, FString::Printf(TEXT("MX9 a v54 file is refused (%s)"), *E));
		}
		// A lost market block: the frame's tag destroyed.
		{
			TArray<uint8> T = W; Put32(T, B.FrameAt, 0);
			FString E;
			const bool bOk = TryBytes(TEXT("mktmx9r2"), T, E);
			const FRHConvoy* C = Sim->GetConvoys().Num() == 1 ? &Sim->GetConvoys()[0] : nullptr;
			ExpectSeq({
			[&]() { return bOk; },
			[&]() { return Capture.CountWarnings(TEXT("the market block is lost")) == 1; },
			[&]() { return Capture.NumErrors() == 0; },
			[&]() { return Sim->GetMarketPriceMcr(NIce) == 3000; },
			[&]() { return Sim->GetRivalStoreKg(NZarya, NIce) == 12000; },
			[&]() { return Sim->GetMarketCheckedSol() == Sim->GetProductionCheckedSol(); },
			[&]() { return Sim->GetProductionCheckedSol() == 1; },
			[&]() { return C; },
			[&]() { return C->Rival == NZarya; },
			[&]() { return C->IsPriced(); },
			[&]() { return C->bCleared; },
			[&]() { return C->Lines.Num() == 0; },
			[&]() { return C->ReservedMcr == 0; },
			[&]() { return Sim->GetCreditReservedMcr() == 0; },
			[&]() { return Sim->GetCreditBalanceMcr() == Bal0; },
			[&]() { return Inv(); },
			},
				TEXT("MX9 a lost market block loads with a Warning: prices at the Earth value, stores at StoreStart, the open sol seeded from C5d's (1), no tally; the colony and its credit load as saved; the v17 slot's convoy is rebuilt EMPTY (cleared, no lines, nothing reserved)"));
			const double IceBefore = In(NIce);
			const double RelBefore = Sim->GetRivalRelation(NZarya);
			int32 Steps = 0;
			while (Sim->GetConvoys().Num() > 0 && Steps++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
			ExpectSeq({
			[&]() { return Sim->GetConvoys().Num() == 0; },
			[&]() { return Near(In(NIce) - IceBefore, 0.0); },
			[&]() { return Sim->GetRivalRelation(NZarya) == RelBefore; },
			[&]() { return Sim->GetCreditBalanceMcr() == Bal0; },
			},
				TEXT("MX9 the rebuilt convoy comes home with nothing: no Ice lands, no credit is paid, the relation does not move"));
		}
		// A lost earlier block (the export frame's tag destroyed) never moves the market block, which is found by the walk from the end.
		{
			int32 EF = INDEX_NONE, EP = 0;
			RHSaveFrame::Find(W, RHSaveFrame::ExportTag, EF, EP);
			TArray<uint8> T = W; Put32(T, EF, 0);
			FString E;
			const bool bOk = TryBytes(TEXT("mktmx9r3"), T, E);
			ExpectSeq({
			[&]() { return bOk; },
			[&]() { return Capture.CountWarnings(TEXT("the export block is lost")) == 1; },
			[&]() { return Capture.CountWarnings(TEXT("the market block is lost")) == 0; },
			[&]() { return Sim->GetConvoys().Num() == 2; },
			[&]() { return Sim->GetCreditReservedMcr() == Reserved0; },
			[&]() { return Sim->GetMarketPriceMcr(NIce) == B.Markets[0].Price[IceSlot]; },
			[&]() { return Inv(); },
			},
				TEXT("MX9 a lost export block (its tag destroyed) is lost alone: the market block is found by the walk back from the end, with both convoys, the reservation and the prices as saved"));
		}
		// Doctored blocks.
		const auto Doctor = [&](const TCHAR* Slot, const TArray<uint8>& Base, const TCHAR* Warning, TFunctionRef<bool()> Check) -> bool
		{
			FString E;
			const bool bOk = TryBytes(Slot, Sealed(Base), E);
			const bool bWarned = Capture.CountWarnings(Warning) >= 1 && Capture.NumErrors() == 0;
			const bool bCheck = Check();
			if (!bOk || !bWarned || !bCheck) { Info(FString::Printf(TEXT("MX9 repair %s: loaded %d, warned %d (%s), check %d, %d warnings"), Slot, bOk ? 1 : 0, bWarned ? 1 : 0, Warning, bCheck ? 1 : 0, Capture.NumWarnings())); }
			return bOk && bWarned && bCheck;
		};
		bool bRepairs = bParsed && IceSlot != INDEX_NONE && StructSlot != INDEX_NONE && ZaryaAt != INDEX_NONE && MeridianAt != INDEX_NONE && FoodSlot != INDEX_NONE;
		FString Failed;
		const auto Note = [&](const TCHAR* What, bool bOk) { bRepairs &= bOk; if (!bOk) { Failed += FString(What) + TEXT("; "); } };
		if (bRepairs)
		{
			{ TArray<uint8> T = W; Put64(T, B.Markets[0].PriceAt[IceSlot], 1); Put64(T, B.Markets[0].PriceAt[StructSlot], 1000000000);
			  Note(TEXT("price band"), Doctor(TEXT("mktmx9d1"), T, TEXT("outside"), [&]() { return Sim->GetMarketPriceMcr(NIce) == 750 && Sim->GetMarketPriceMcr(NStruct) == 20000 && Inv(); })); }
			{ TArray<uint8> T = W; Put64(T, B.Rivals[ZaryaAt].StoreAt[IceSlot], -5000);
			  Note(TEXT("negative store"), Doctor(TEXT("mktmx9d2"), T, TEXT("set to 0"), [&]() { return Sim->GetRivalStoreG(NZarya, NIce) == 0 && Sim->GetRivalStoreKg(NZarya, NStruct) == B.Rivals[ZaryaAt].Store[StructSlot] / 1000; })); }
			{ TArray<uint8> T = W; Put32(T, B.Markets[0].HeadAt, 9);
			  int64 So = 0, Bo = 0;
			  Note(TEXT("ring index"), Doctor(TEXT("mktmx9d3"), T, TEXT("ring index"), [&]() { return !Sim->GetMarketTally(NIce, 1, So, Bo) && Sim->GetMarketPriceMcr(NIce) == B.Markets[0].Price[IceSlot]; })); }
			{ TArray<uint8> T = W; Put32(T, B.Markets[0].FillAt, -3);
			  int64 So = 0, Bo = 0;
			  Note(TEXT("ring fill"), Doctor(TEXT("mktmx9d3b"), T, TEXT("ring index"), [&]() { return !Sim->GetMarketTally(NIce, 1, So, Bo); })); }
			{ TArray<uint8> T = W; Put64(T, B.Markets[0].SoldAt[IceSlot * 8 + 2], -1);
			  Note(TEXT("negative ring entry"), Doctor(TEXT("mktmx9d3c"), T, TEXT("ring index"), [&]() { return Inv(); })); }
			{ TArray<uint8> T = W; T[B.SlotNameAt[0]] = 'Z'; // Compost -> Zompost: the slot names no longer ascend
			  Note(TEXT("slot order"), Doctor(TEXT("mktmx9d4"), T, TEXT("slot names are out of order"), [&]() { return Sim->GetMarketPriceMcr(NIce) == 3000 && Sim->GetRivalStoreKg(NZarya, NIce) == 12000 && Sim->GetConvoys().Num() == 2 && Sim->GetCreditReservedMcr() == Reserved0 && Inv(); })); }
			{ TArray<uint8> T = W; T[B.Convoys[1].RivalAt + 4] = 'z'; // MxKes -> MxKez: no such rival
			  Note(TEXT("convoy rival"), Doctor(TEXT("mktmx9d5"), T, TEXT("its rival no longer exists"), [&]() { return Sim->GetConvoys().Num() == 1 && Sim->GetCreditReservedMcr() == 0 && Inv(); })); }
			{ TArray<uint8> T = W; T[B.Convoys[1].Lines[0].NameAt] = 'O'; T[B.Convoys[1].Lines[0].NameAt + 1] = 'r'; T[B.Convoys[1].Lines[0].NameAt + 2] = 'e'; // Ice -> Ore: not a resource MxKes trades
			  Note(TEXT("untraded line"), Doctor(TEXT("mktmx9d6"), T, TEXT("is not a resource its rival trades"), [&]() { return Sim->GetConvoys().Num() == 1 && Sim->GetCreditReservedMcr() == 0 && Inv(); })); }
			{ TArray<uint8> T = W; Put32(T, B.Convoys[1].Lines[0].KgAt, 0);
			  Note(TEXT("zero line"), Doctor(TEXT("mktmx9d7"), T, TEXT("a line with no kg"), [&]() { return Sim->GetConvoys().Num() == 1 && Inv(); })); }
			{ TArray<uint8> T = W; Put32(T, B.Convoys[0].Lines[0].FilledAt, 999);
			  Note(TEXT("fill past line"), Doctor(TEXT("mktmx9d8"), T, TEXT("a fill outside its line"), [&]() { return Sim->GetConvoys().Num() == 1 && Sim->GetConvoys()[0].Rival == FName("MxKes") && Inv(); })); }
			{ TArray<uint8> T = W; Put64(T, B.SalesAt, 1000000000000LL);
			  Note(TEXT("sales clamp"), Doctor(TEXT("mktmx9d9"), T, TEXT("lifetime market sales"), [&]() { return Sim->GetMarketSalesMcr() == FMath::Max<int64>(0, Sim->GetCreditAwardedMcr() - Sim->GetSalesPaidMcr()) && Inv(); })); }
			{ TArray<uint8> T = W; Put64(T, B.ReservedAt, B.Reserved + 7);
			  Note(TEXT("reserved counter"), Doctor(TEXT("mktmx9d10"), T, TEXT("disagrees with the convoys"), [&]() { return Sim->GetCreditReservedMcr() == Reserved0 && Inv(); })); }
			{ TArray<uint8> T = W; T[B.Rivals[MeridianAt].NameAt + 7] = 'x'; // Meridian -> Meridix: the data rival is missing from the block
			  Note(TEXT("rival missing"), Doctor(TEXT("mktmx9d11"), T, TEXT("has no economy now"), [&]() { return Sim->GetRivalStoreKg(NMeridian, NFood) == 600 && Sim->GetRivalStoreKg(NMeridian, NParts) == 40 && Sim->GetRivalStoreKg(NZarya, NIce) == B.Rivals[ZaryaAt].Store[IceSlot] / 1000; })); }
			{ TArray<uint8> T = W; Put32(T, B.NextAt, 1);
			  Note(TEXT("next id"), Doctor(TEXT("mktmx9d12"), T, TEXT("next convoy Id"), [&]() { return Sim->GetConvoys().Num() == 2 && Inv(); })); }
			{ TArray<uint8> T = W; Put32(T, B.CheckedAt, 99);
			  Note(TEXT("open sol ahead"), Doctor(TEXT("mktmx9d13"), T, TEXT("open sol 99"), [&]() { return Sim->GetMarketCheckedSol() == Sim->GetProductionCheckedSol(); })); }
		}
		Expect(bRepairs, FString::Printf(TEXT("MX9 sealed, doctored blocks each load with their own Warning and no Error: a price outside [0.25 V, 4 V] is clamped, a negative store is 0, a ring index or fill or entry out of range resets the tally, unsorted slot names reset the market and the stores (the convoys load), a convoy naming a missing rival or an untraded resource or with a zero line or a fill past its line is dropped with its reservation, the lifetime sales and the reserved counter and the next Id and an open sol ahead are repaired, a data rival missing from the block starts at StoreStart %s"), *Failed));
		// A loaded convoy with a past clearing sol clears at the next roll (ClearSol 1 doctored to 0, the open sol being 1).
		{
			TArray<uint8> T = W; Put32(T, B.Convoys[1].ClearSolAt, 0);
			FString E;
			const bool bOk = TryBytes(TEXT("mktmx9e1"), Sealed(T), E);
			const bool bStill = bOk && Sim->GetConvoys().Num() == 2 && !Sim->GetConvoys()[1].bCleared;
			Capture.Clear();
			RollNext();
			const bool bCleared = Sim->GetConvoys().Num() == 2 && Sim->GetConvoys()[1].bCleared && Capture.CountLines(TEXT("Order #2 to MxKes clears: bought Ice:100, sold nothing")) == 1 && Sim->GetCreditReservedMcr() == 0 && Inv();
			ExpectSeq({
			[&]() { return bOk; },
			[&]() { return bStill; },
			[&]() { return bCleared; },
			},
				TEXT("MX9 a loaded convoy whose clearing sol (0) is already past waits for the next roll and clears at it, whatever its sol: the reservation is released and the order's line is logged"));
		}
		// Over-reserved: two convoys both waiting, their reservations beyond the balance, are cut from the highest Id down.
		{
			Reset();
			Provision(2000000);
			Sim->Debug_SyncRivalEconomies();
			Order(NZarya, TEXT("Ice:300"));
			Order(FName("MxKes"), TEXT("Ice:300"));
			Sim->SaveColony(TEXT("mktmx9c"), Err);
			TArray<uint8> Wc;
			FFileHelper::LoadFileToArray(Wc, *SaveFilePath(TEXT("mktmx9c")));
			FMktSuiteBlock Bc;
			const bool bOkc = MktSuiteParse(Wc, Bc) && Bc.Convoys.Num() == 2 && Bc.Convoys[0].Held == 945300 && Bc.Convoys[1].Held == 945300;
			const int64 Balance = Sim->GetCreditBalanceMcr();
			bool bCut1 = false, bCut2 = false;
			if (bOkc)
			{
				{ TArray<uint8> T = Wc; Put64(T, Bc.Convoys[0].HeldAt, Balance); Put64(T, Bc.Convoys[1].HeldAt, 5000);
				  FString E; bCut1 = TryBytes(TEXT("mktmx9f1"), Sealed(T), E) && Capture.CountWarnings(TEXT("convoy #2's is cut by 5")) == 1 && Sim->GetConvoys()[0].ReservedMcr == Balance && Sim->GetConvoys()[1].ReservedMcr == 0 && Sim->GetCreditReservedMcr() == Balance && Sim->GetSpendableMcr() == 0 && Inv(); }
				{ TArray<uint8> T = Wc; Put64(T, Bc.Convoys[0].HeldAt, Balance + 100000);
				  FString E; bCut2 = TryBytes(TEXT("mktmx9f2"), Sealed(T), E) && Sim->GetConvoys()[1].ReservedMcr == 0 && Sim->GetConvoys()[0].ReservedMcr == Balance && Sim->GetCreditReservedMcr() == Balance && Inv(); }
			}
			ExpectSeq({
			[&]() { return bOkc; },
			[&]() { return bCut1; },
			[&]() { return bCut2; },
			},
				TEXT("MX9 reservations beyond the balance are cut from the highest Id down: convoy #2's 5,000 mcr goes first and convoy #1 keeps the whole balance; with #1 itself 100,000 over, #2's 945,300 goes and then #1's excess; the counter ends equal to the balance, spendable 0"));
		}
		// A clearing that cannot be paid in full (a reservation cut by a repair) pays what it may, writes the rest off with a Warning, and the balance stays above 0.
		{
			TArray<uint8> T = W; Put64(T, B.Convoys[1].HeldAt, 0); Put64(T, B.ReservedAt, 0);
			FString E;
			const bool bOk = TryBytes(TEXT("mktmx9g1"), Sealed(T), E) && Sim->GetCreditReservedMcr() == 0;
			Sim->Debug_ZeroCredits();
			Sim->Debug_GrantCredits(1000);
			Capture.Clear();
			RollNext();
			const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
			ExpectSeq({
			[&]() { return bOk; },
			[&]() { return Rec; },
			[&]() { return Rec->ConvoyId == 2; },
			[&]() { return Rec->NetMcr > 1000; },
			[&]() { return Rec->PaidMcr == 1000; },
			[&]() { return Rec->WrittenOffMcr == Rec->NetMcr - 1000; },
			[&]() { return Sim->GetCreditBalanceMcr() == 0; },
			[&]() { return Capture.CountWarnings(TEXT("could not be paid; MxKes writes it off")) == 1; },
			[&]() { return Inv(); },
			},
				FString::Printf(TEXT("MX9 a clearing with no reservation and 1 cr spendable pays that 1 cr of its %s cr, writes the rest off with a Warning ('could not be paid; MxKes writes it off') and the balance is 0, never below"), Rec ? *Cr(Rec->NetMcr) : TEXT("?")));
		}
		// CreditSpend never goes below 0: a spend above the balance pays the balance and returns the rest.
		{
			Reset();
			Provision(1000000);
			const int64 Spent0 = Sim->GetCreditSpentMcr();
			const int64 Rest = Sim->Debug_CreditSpend(1000000 + 500);
			ExpectSeq({
			[&]() { return Rest == 500; },
			[&]() { return Sim->GetCreditBalanceMcr() == 0; },
			[&]() { return Sim->GetCreditSpentMcr() - Spent0 == 1000000; },
			[&]() { return Inv(); },
			[&]() { return Sim->Debug_CreditSpend(0) == 0; },
			[&]() { return Sim->Debug_CreditSpend(-5) == 0; },
			[&]() { return Sim->GetCreditBalanceMcr() == 0; },
			},
				TEXT("MX9 CreditSpend of 1,000,500 mcr against a balance of 1,000,000 pays the balance, returns the 500 it could not pay, and the balance is 0, never negative; zero and negative spends do nothing"));
		}
		RetireAll();
	}

	// --- MX10. Ruling 11 and Q4a: prices forced to both bounds against the start values change nothing outside the market: Hope, the
	// population, every stock and shelf are bit-identical over 10 sols. ---
	{
		struct FForce { FString Digest; int64 Ice = 0, Struct = 0; };
		const auto Run = [&](int32 Mode) -> FForce
		{
			Reset();
			for (const FName R : { NIce, NWater, NOx, NStruct, NHyd, NFood, NParts })
			{
				if (Mode == 1) { Sim->Debug_SetMarketPrice(R, 0); }
				if (Mode == 2) { Sim->Debug_SetMarketPrice(R, 1000000000); }
			}
			for (int32 K = 0; K < 10 * (int32)(SolLen / EraDt); ++K) { EraOnce(); }
			FForce F;
			F.Digest = Digest();
			F.Ice = Sim->GetMarketPriceMcr(NIce);
			F.Struct = Sim->GetMarketPriceMcr(NStruct);
			return F;
		};
		const FForce Start = Run(0), Floor = Run(1), Ceil = Run(2);
		ExpectSeq({
			[&]() { return Start.Digest == Floor.Digest; },
			[&]() { return Start.Digest == Ceil.Digest; },
			[&]() { return Start.Digest.Len() > 300; },
			[&]() { return Floor.Ice < 1500; },
			[&]() { return Ceil.Ice > 7000; },
			[&]() { return Floor.Struct < 3000; },
			[&]() { return Ceil.Struct > 10000; },
			},
			FString::Printf(TEXT("MX10 10 sols with every price forced to its floor (Ice %s, Struct %s cr/kg after), its ceiling (Ice %s, Struct %s) or left at the start values (Ice %s, Struct %s): Hope, the population, the credit, the ledger and every stock and shelf are bit-identical (%d chars)"),
				*Cr(Floor.Ice), *Cr(Floor.Struct), *Cr(Ceil.Ice), *Cr(Ceil.Struct), *Cr(Start.Ice), *Cr(Start.Struct), Start.Digest.Len()));
	}

	// --- MX11. Politics: orders are refused through a closed route, sabotage, defection and an embargo with today's texts; and a convoy out
	// when its rival defects still clears, lands its goods and pays its owed credit, with no goodwill. ---
	{
		Reset();
		Provision(5000000);
		struct FPolitic { const TCHAR* Name; const TCHAR* Text; };
		const FPolitic Politics[] = {
			{ TEXT("closed"), TEXT("The route to Zarya Station is closed - you cut ties when you complied with Earth") },
			{ TEXT("sabotage"), TEXT("Zarya Station's production is disrupted (sabotage) - no trade until they recover") },
			{ TEXT("defected"), TEXT("Zarya Station has sided with Earth - relations are frozen, no trade") },
			{ TEXT("embargo"), TEXT("Zarya Station has embargoed you under Earth pressure - reassure them first") } };
		bool bAll = true;
		FString Bad;
		for (int32 i = 0; i < 4; ++i)
		{
			Sim->Debug_ClearPolitics();
			if (i == 0) { Sim->Debug_CloseRoute(NZarya); }
			if (i == 1) { Sim->Debug_SetSabotage(NZarya, 3.0); }
			if (i == 2) { Sim->Debug_SetDefected(NZarya); }
			if (i == 3) { Sim->Debug_SetEmbargo(NZarya, 5.0); }
			FString W1, W2;
			Rejects.Reset();
			const bool bBooked = Order(NZarya, TEXT("Ice:100"));
			const bool bStandingBooked = Send(TEXT("Convoy"), NZarya, FString());
			const bool bOk = !Sim->CanPlaceOrder(NZarya, TEXT("Ice:100"), W1) && W1 == Politics[i].Text && !Sim->CanPlaceStandingOrder(NZarya, W2) && W2 == Politics[i].Text
				&& !bBooked && !bStandingBooked && Rejects.Num() == 2 && Rejects[0] == Politics[i].Text && Rejects[1] == Politics[i].Text && Sim->GetConvoys().Num() == 0;
			bAll &= bOk;
			if (!bOk) { Bad += FString::Printf(TEXT("%s: '%s'; "), Politics[i].Name, *W1); }
		}
		Sim->Debug_ClearPolitics();
		Expect(bAll, FString::Printf(TEXT("MX11 through a closed route, sabotage, defection and an embargo both a written order and the standing order are refused with HEAD's texts, by the plan and by the verb, and no convoy leaves %s"), *Bad));
		// A convoy out when its rival defects: it still clears (a deal is not undone), lands its goods, pays what is owed, and earns no goodwill.
		Reset();
		Provision(5000000);
		const double InIce0 = In(NIce);
		const bool bBooked = Order(NZarya, TEXT("Struct:-100;Ice:100")); // worth 3 x 100 less 5 x 100: the rival owes 200 cr
		Sim->Debug_SetDefected(NZarya);
		const int64 Bal0 = Sim->GetCreditBalanceMcr();
		RollNext();
		const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
		const bool bClears = bBooked && Rec && Rec->NetMcr == -200000 && Sim->GetConvoys().Num() == 1 && Sim->GetConvoys()[0].bCleared && Sim->GetConvoys()[0].OwedMcr == 200000;
		Capture.Clear();
		int32 Steps = 0;
		while (Sim->GetConvoys().Num() > 0 && Steps++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
		ExpectSeq({
			[&]() { return bClears; },
			[&]() { return Near(In(NIce) - InIce0, 100.0); },
			[&]() { return Sim->GetCreditBalanceMcr() == Bal0 + 200000; },
			[&]() { return Sim->GetRivalRelation(NZarya) == 40.0; },
			[&]() { return Capture.CountLines(TEXT("(soured - goods only)")) == 1; },
			[&]() { return Inv(); },
			},
			TEXT("MX11 a convoy out when Zarya defects still clears at its roll, lands its 100 kg of Ice, pays the 200 cr it was owed, and gets no goodwill (the relation stays 40; the line says 'soured - goods only')"));
		Sim->Debug_ClearPolitics();
	}

	// --- MX8. Era parity, last (it injects a rival of its own). The start save: the default colony provisioned, Kestrel (a 30 km neighbor with
	// a 1-sol round trip, making 100 kg of Ice a sol) beside Zarya, the clock at sol 20.0 by the real roll - a 20-sol window [20, 40) with no
	// storm (Storm_1 ends at sol 15; Flare_1 at sol 17 freezes nothing). Both bands run the same scripted orders through EnqueueCommand at the
	// same sim times, over two rivals, with the discovered set fixed (no survey runs): at every threshold every traded price, every rival store,
	// every fill, every clearing sol and every charge are identical (int64 equality); so are the sols convoys come home in, and the end
	// balance. The era band's steps sit 25 s off the agent band's, so the boundary order (ExecuteAt 10 s past the sol-36 boundary) is booked
	// by the boundary step BEFORE the roll in the era band and ~100 sub-steps AFTER it in the agent band. The fallback (MarketRivalOnly) twin
	// is held to the same bar; and a margin case in which one band accepts an order and the other refuses it moves the main twins' prices
	// apart while the fallback twins' stay identical. ---
	{
		Reset();
		Inject(FName("Kestrel"), 30.0, TEXT("Ice:100;Regolith:50"), TEXT(""), TEXT("Ice:1000;Regolith:3000"), TEXT("Ice:5000;Regolith:10000"), TEXT(""), TEXT(""), 0);
		Sim->Debug_SyncRivalEconomies();
		Sim->AddStock(NHyd, 1000.0);
		Sim->AddStock(NParts, 40.0);
		Sim->AddStock(NIce, 4000.0);
		Sim->AddStock(NStruct, 2000.0);
		// The colony MAKES a traded resource (Regolith, which Kestrel trades) at rates that differ between the bands: a dig designated at Regolith_A
		// is worked by the era band's abstract excavator fleet, while the agent band, with no Mass robots stepped here, digs nothing. The market must
		// not read it (mutation 12). No Oxygen or Water is held, so Q1 (Struct 300, Water 200, Oxygen 50) stays unmet and pays no award, which E2 times by band.
		{
			FRHCommand Dig; Dig.Verb = FName("Dig"); Dig.Target = FName("Regolith_A");
			Sim->EnqueueCommand(Dig);
			for (int32 K = 0; K < 200; ++K) { EraOnce(); bool bDesignated = false; for (const FRHDepositState& Dp : Sim->GetDeposits()) { bDesignated |= Dp.RowName == FName("Regolith_A") && Dp.bDesignated; } if (bDesignated) { break; } }
		}
		Sim->Debug_ZeroCredits();
		Sim->Debug_GrantCredits(200000000);
		Clock->Debug_SetSimSeconds(20.0 * SolLen - EraDt);
		EraOnce(); // the step ends at exactly sol 20.0: the real roll closes the market's first eight sols here, no order yet
		FString Err;
		const bool bSaved = Sim->SaveColony(TEXT("marketmx8"), Err);
		ExpectSeq({
			[&]() { return bSaved; },
			[&]() { return Clock->GetSimSecondsTotal() == 20.0 * SolLen; },
			[&]() { return Sim->GetMarketCheckedSol() == 20; },
			[&]() { return Sim->GetConvoys().Num() == 0; },
			[&]() { return Sim->GetRivalEconomyCount() == 3; },
			},
			FString::Printf(TEXT("MX8 the start save 'marketmx8': three economy rivals (Kestrel, Meridian, Zarya), the clock at sol 20.0 exactly, the market open at sol %d, nothing out (%s)"), Sim->GetMarketCheckedSol(), *Err));
		struct FSend { double T; FName Rival; FString Text; };
		struct FEvt { double T; int32 Kind; int64 Arg; };
		struct FRun
		{
			FString Prices, PriceOnly, Clearings, Returns, Books, Refusals, RegolithRing;
			int64 Balance = 0, Reserved = 0, BoundaryFill = -1, MarginPrice = 0, RegolithMadeG = 0;
			int32 Booked = 0, Refused = 0, Rolls = 0;
			bool bHalfOk = true, bDiscoveredSame = true;
		};
		const double WindowStart = 20.0 * SolLen;
		const auto AtSol = [&](double Sol) { return WindowStart + Sol * SolLen; };
		const double BoundaryB = AtSol(16.0); // the sol-36 boundary
		const FName NKestrel(TEXT("Kestrel"));
		const FName NRegolith(TEXT("Regolith"));
		const TArray<FName> TracedRes = { NIce, NWater, NOx, NStruct, NHyd, NFood, NParts, NRegolith };
		const auto RunWindow = [&](bool bAgent, bool bRivalOnly, const TArray<FSend>& Sends, const TArray<FEvt>& Evts, double EndSol) -> FRun
		{
			LoadSlot(TEXT("marketmx8"));
			Sim->Debug_SetMarketRivalOnly(bRivalOnly);
			FRun O;
			int32 NextSend = 0, NextEvt = 0, Cleared = Sim->GetMarketClearings().Num(), Refused = 0;
			int32 LastChecked = Sim->GetMarketCheckedSol();
			TArray<int32> PrevIds;
			bool bFirstEra = true;
			const double EndT = AtSol(EndSol);
			while (Clock->GetSimSecondsTotal() < EndT - 1.0e-6)
			{
				const double Now = Clock->GetSimSecondsTotal();
				while (NextEvt < Evts.Num() && Now >= Evts[NextEvt].T)
				{
					if (Evts[NextEvt].Kind == 0) { Sim->Debug_SetRivalStore(NKestrel, NIce, Evts[NextEvt].Arg); }
					if (Evts[NextEvt].Kind == 1) { Sim->Debug_ZeroCredits(); Sim->Debug_GrantCredits(Evts[NextEvt].Arg); }
					++NextEvt;
				}
				while (NextSend < Sends.Num() && Now >= Sends[NextSend].T)
				{
					FRHCommand C; C.Verb = FName("MarketOrder"); C.Target = Sends[NextSend].Rival; C.Text = Sends[NextSend].Text;
					Sim->EnqueueCommand(C);
					++NextSend;
				}
				if (bAgent) { AgentOnce(); }
				else if (bFirstEra) { bFirstEra = false; Clock->Debug_AdvanceSimSeconds(25.0); Sim->Debug_SetBatteryWh(1.0e9); Sim->EraStep(25.f); } // the era grid sits 25 s off the agent's
				else { EraOnce(); }
				// What the colony did this step: bookings, returns, refusals.
				const TArray<FRHConvoy>& Cv = Sim->GetConvoys();
				bool bSame = Cv.Num() == PrevIds.Num();
				for (int32 i = 0; bSame && i < Cv.Num(); ++i) { bSame &= Cv[i].Id == PrevIds[i]; }
				if (!bSame)
				{
					for (const FRHConvoy& C : Cv)
					{
						if (!PrevIds.Contains(C.Id))
						{
							++O.Booked;
							O.Books += FString::Printf(TEXT("#%d %s clear %d; "), C.Id, *C.Rival.ToString(), C.ClearSol);
							O.bHalfOk &= C.ReservedMcr * 2 <= Sim->GetSpendableMcr() + C.ReservedMcr; // at most half the spendable credit (before it was set aside)
						}
					}
					for (const int32 Id : PrevIds)
					{
						if (!Cv.ContainsByPredicate([Id](const FRHConvoy& C) { return C.Id == Id; })) { O.Returns += FString::Printf(TEXT("#%d home in sol %d; "), Id, FMath::FloorToInt32(Clock->GetSimSecondsTotal() / SolLen)); }
					}
					PrevIds.Reset();
					for (const FRHConvoy& C : Cv) { PrevIds.Add(C.Id); }
				}
				while (Refused < Rejects.Num()) { ++O.Refused; O.Refusals += Rejects[Refused] + TEXT(" | "); ++Refused; }
				if (Sim->GetMarketCheckedSol() != LastChecked)
				{
					LastChecked = Sim->GetMarketCheckedSol();
					++O.Rolls;
					O.Prices += FString::Printf(TEXT("[sol %d"), LastChecked);
					O.PriceOnly += FString::Printf(TEXT("[sol %d"), LastChecked);
					for (const FName R : TracedRes) { O.Prices += FString::Printf(TEXT(" %s=%lld"), *R.ToString(), Sim->GetMarketPriceMcr(R)); O.PriceOnly += FString::Printf(TEXT(" %s=%lld"), *R.ToString(), Sim->GetMarketPriceMcr(R)); }
					O.PriceOnly += TEXT("]\n");
					for (const FName R : { NIce, NWater, NOx, NStruct, NHyd }) { O.Prices += FString::Printf(TEXT(" Z.%s=%lld"), *R.ToString(), Sim->GetRivalStoreG(NZarya, R)); }
					for (const FName R : { NParts, NFood }) { O.Prices += FString::Printf(TEXT(" M.%s=%lld"), *R.ToString(), Sim->GetRivalStoreG(NMeridian, R)); }
					O.Prices += FString::Printf(TEXT(" K.Ice=%lld K.Regolith=%lld]\n"), Sim->GetRivalStoreG(NKestrel, NIce), Sim->GetRivalStoreG(NKestrel, NRegolith));
					for (; Cleared < Sim->GetMarketClearings().Num(); ++Cleared)
					{
						const FRHMarketClearing& K = Sim->GetMarketClearings()[Cleared];
						O.Clearings += FString::Printf(TEXT("[#%d %s clear %d closing %d net %lld paid %lld off %lld value %lld lines"), K.ConvoyId, *K.Rival.ToString(), K.ClearSol, K.ClosingSol, K.NetMcr, K.PaidMcr, K.WrittenOffMcr, K.ValueMcr);
						for (const FRHConvoyLine& L : K.Lines) { O.Clearings += FString::Printf(TEXT(" %s %d/%d"), *L.Resource.ToString(), L.Kg, L.FilledKg); }
						O.Clearings += TEXT("]\n");
						if (K.Rival == NKestrel && K.ClearSol == 36 && K.Lines.Num() == 1) { O.BoundaryFill = K.Lines[0].FilledKg; }
					}
				}
			}
			O.RegolithMadeG = Sim->GetLedgerLifetimeG(NRegolith, ELeg::Made);
			for (int32 Back = 1; Back <= 8; ++Back) { O.RegolithRing += FString::Printf(TEXT("%lld "), Sim->GetLedgerRingG(NRegolith, ELeg::Made, Back)); }
			O.Balance = Sim->GetCreditBalanceMcr();
			O.Reserved = Sim->GetCreditReservedMcr();
			O.MarginPrice = Sim->GetMarketPriceMcr(NStruct);
			for (const FName R : { NZarya, NMeridian, NKestrel }) { O.bDiscoveredSame &= !Sim->IsRivalDiscovered(R); }
			return O;
		};
		// The main schedule: Kestrel on window sols 2, 4, 7, 9 and 12 and a boundary order; Zarya on 1, 6 and 11; all at sol fraction .5.
		TArray<FSend> Main;
		Main.Add({ AtSol(1.5), NZarya, TEXT("Struct:-100;Hydrogen:50") }); // not Water: 200 kg of it would meet Q1 and pay a quota award, which E2 times by band
		Main.Add({ AtSol(2.5), NKestrel, TEXT("Ice:100") });
		Main.Add({ AtSol(4.5), NKestrel, TEXT("Ice:-150") });
		Main.Add({ AtSol(6.5), NZarya, TEXT("Ice:-200;Hydrogen:20") });
		Main.Add({ AtSol(7.5), NKestrel, TEXT("Ice:200") });
		Main.Add({ AtSol(9.5), NKestrel, TEXT("Ice:-100") });
		Main.Add({ AtSol(11.5), NZarya, TEXT("Struct:100;Hydrogen:-50") });
		Main.Add({ AtSol(12.5), NKestrel, TEXT("Ice:150") });
		Main.Add({ BoundaryB - 35.0, NKestrel, TEXT("Ice:120") }); // ExecuteAt = the boundary + 10 s
		TArray<FEvt> MainEvt;
		MainEvt.Add({ AtSol(15.5), 0, 50 }); // Kestrel's Ice store is set to 50 kg, below the order; the roll at the boundary adds its 100
		const FRun EraMain = RunWindow(false, false, Main, MainEvt, 20.0);
		const FRun AgentMain = RunWindow(true, false, Main, MainEvt, 20.0);
		const bool bMainSame = EraMain.Prices == AgentMain.Prices && EraMain.Clearings == AgentMain.Clearings && EraMain.Returns == AgentMain.Returns && EraMain.Books == AgentMain.Books
			&& EraMain.Balance == AgentMain.Balance && EraMain.Reserved == AgentMain.Reserved && EraMain.Rolls == AgentMain.Rolls;
		ExpectSeq({
			[&]() { return EraMain.Rolls >= 19; },
			[&]() { return EraMain.Booked == 9; },
			[&]() { return AgentMain.Booked == 9; },
			[&]() { return EraMain.Refused == 0; },
			[&]() { return AgentMain.Refused == 0; },
			[&]() { return bMainSame; },
			[&]() { return EraMain.BoundaryFill == 120; },
			[&]() { return AgentMain.BoundaryFill == 120; },
			[&]() { return EraMain.bHalfOk; },
			[&]() { return AgentMain.bHalfOk; },
			[&]() { return EraMain.bDiscoveredSame; },
			[&]() { return AgentMain.bDiscoveredSame; },
			[&]() { return EraMain.Reserved == 0; },
			[&]() { return Sim->GetQuotaPhase() == ERHQuotaPhase::Open; },
			[&]() { return EraMain.RegolithMadeG > 1000000 && EraMain.RegolithMadeG != AgentMain.RegolithMadeG; },
			[&]() { return Sim->GetConvoys().Num() == 0; },
			},
			FString::Printf(TEXT("MX8 the paired run, 20 sols, nine orders to two rivals (%d booked in each band, %d refused): the %d thresholds' prices and rival stores, every fill, clearing sol and charge, the sols the convoys come home in and the end balance (%s cr) are identical in the era and the agent band; the boundary order (booked before the roll in one band, after it in the other) fills 120 kg in both against Kestrel's 150 while the colony's own dig, which makes Regolith in the era band and none in the agent band, moves no price; every reservation is at most half the spendable credit; no survey ran (the discovered set is equal)"),
				EraMain.Booked, EraMain.Refused, EraMain.Rolls, *Cr(EraMain.Balance)));
		const auto FirstDiff = [](const FString& A, const FString& B) -> FString
		{
			TArray<FString> LA, LB;
			A.ParseIntoArrayLines(LA, false);
			B.ParseIntoArrayLines(LB, false);
			for (int32 i = 0; i < FMath::Max(LA.Num(), LB.Num()); ++i)
			{
				const FString X = i < LA.Num() ? LA[i] : FString(TEXT("<none>")), Y = i < LB.Num() ? LB[i] : FString(TEXT("<none>"));
				if (X != Y) { return FString::Printf(TEXT("line %d: %s  |  %s"), i, *X, *Y); }
			}
			return FString(TEXT("(equal)"));
		};
		Info(FString::Printf(TEXT("MX8 the colony's own Regolith dug over the window: era %lld g, agent %lld g; ring (newest first) era: %s | agent: %s"), EraMain.RegolithMadeG, AgentMain.RegolithMadeG, *EraMain.RegolithRing, *AgentMain.RegolithRing));
		if (!bMainSame)
		{
			Info(FString::Printf(TEXT("MX8 main diff: prices %s ; clearings %s ; returns %s | %s ; books %s | %s ; balance %lld | %lld ; reserved %lld | %lld ; rolls %d | %d"), *FirstDiff(EraMain.Prices, AgentMain.Prices), *FirstDiff(EraMain.Clearings, AgentMain.Clearings),
				*EraMain.Returns, *AgentMain.Returns, *EraMain.Books, *AgentMain.Books, EraMain.Balance, AgentMain.Balance, EraMain.Reserved, AgentMain.Reserved, EraMain.Rolls, AgentMain.Rolls));
		}
		const FRun EraOnly = RunWindow(false, true, Main, MainEvt, 20.0);
		const FRun AgentOnly = RunWindow(true, true, Main, MainEvt, 20.0);
		if (EraOnly.Prices != AgentOnly.Prices || EraOnly.Clearings != AgentOnly.Clearings) { Info(FString::Printf(TEXT("MX8 fallback diff: prices %s ; clearings %s"), *FirstDiff(EraOnly.Prices, AgentOnly.Prices), *FirstDiff(EraOnly.Clearings, AgentOnly.Clearings))); }
		const bool bOnlySame = EraOnly.Prices == AgentOnly.Prices && EraOnly.Clearings == AgentOnly.Clearings && EraOnly.Returns == AgentOnly.Returns && EraOnly.Books == AgentOnly.Books
			&& EraOnly.Balance == AgentOnly.Balance && EraOnly.Rolls == AgentOnly.Rolls;
		ExpectSeq({
			[&]() { return EraOnly.Booked == 9; },
			[&]() { return AgentOnly.Booked == 9; },
			[&]() { return bOnlySame; },
			[&]() { return EraOnly.BoundaryFill == 120; },
			[&]() { return AgentOnly.BoundaryFill == 120; },
			[&]() { return EraOnly.Prices != EraMain.Prices; },
			},
			FString::Printf(TEXT("MX8 the fallback twin (MarketRivalOnly) is held to the same bar: the same nine orders, thresholds, fills and returns identical in both bands (%d rolls); its prices follow the rivals' data alone, so they differ from the main run's"), EraOnly.Rolls));
		// The margin case. One boundary order buys Zarya's Struct, whose price rises at the sol-21 roll; the spendable credit is set between the
		// reservation quoted before that roll (the era band books there) and the one quoted after it (the agent band books there), so one band
		// books the order and the other refuses it. (A probe rolls once from the start save to read both quotes.)
		const double MarginB = AtSol(1.0);
		const TArray<FRHConvoyLine> MarginLines = { FRHConvoyLine{ NStruct, 300, 0 } };
		bool bRising = true;
		const auto MarginS = [&](bool bRivalOnly, bool& OutRising) -> int64
		{
			LoadSlot(TEXT("marketmx8"));
			Sim->Debug_SetMarketRivalOnly(bRivalOnly);
			const int64 R0 = Sim->GetOrderReservationMcr(NZarya, MarginLines);
			RollNext();
			const int64 R1 = Sim->GetOrderReservationMcr(NZarya, MarginLines);
			OutRising = R1 > R0;
			return FMath::Abs(R1 - R0) >= 2 ? (R0 + R1) / 2 : -1;
		};
		for (const bool bOnly : { false, true })
		{
			const int64 S = MarginS(bOnly, bRising);
			TArray<FSend> One;
			One.Add({ MarginB - 35.0, NZarya, TEXT("Struct:300") }); // ExecuteAt = the sol-21 boundary + 10 s
			TArray<FEvt> Credit;
			Credit.Add({ MarginB - 100.0, 1, S });
			const FRun E = RunWindow(false, bOnly, One, Credit, 4.0);
			const FRun A = RunWindow(true, bOnly, One, Credit, 4.0);
			if (E.PriceOnly != A.PriceOnly) { Info(FString::Printf(TEXT("MX8 margin (%s) price diff: %s ; era books %d agent books %d"), bOnly ? TEXT("fallback") : TEXT("main"), *FirstDiff(E.PriceOnly, A.PriceOnly), E.Booked, A.Booked)); }
			if (!bOnly)
			{
				ExpectSeq({
			[&]() { return S > 0; },
			[&]() { return bRising; },
			[&]() { return E.Booked == 1; },
			[&]() { return A.Booked == 0; },
			[&]() { return A.Refused == 1; },
			[&]() { return A.Refusals.Contains(TEXT("is spendable")); },
			[&]() { return E.PriceOnly != A.PriceOnly; },
			[&]() { return E.MarginPrice != A.MarginPrice; },
			},
					FString::Printf(TEXT("MX8 the margin case (main): with %s cr spendable the era band books the 300 kg Struct order (quoted before the roll) and the agent band refuses it ('%s'), and the main twins' prices then differ (Struct %lld against %lld mcr at the end): the case bites"),
						*Cr(S), *A.Refusals.Left(120), E.MarginPrice, A.MarginPrice));
			}
			else
			{
				// In the fallback the price at that roll FALLS (the start save was made under the main rules, and the rivals' data alone pull
				// Struct down toward 1.4 V), so the reservation quoted before the roll is the larger and it is the agent band that books.
				ExpectSeq({
			[&]() { return S > 0; },
			[&]() { return !bRising; },
			[&]() { return E.Booked + A.Booked == 1; },
			[&]() { return E.Booked == 0; },
			[&]() { return E.PriceOnly == A.PriceOnly; },
			[&]() { return E.MarginPrice == A.MarginPrice; },
			},
					FString::Printf(TEXT("MX8 the margin case (fallback twins): with %s cr spendable one band books and the other refuses (era %d, agent %d), yet the fallback twins' prices stay identical at every threshold (Struct %lld mcr at the end)"), *Cr(S), E.Booked, A.Booked, E.MarginPrice));
			}
		}
		RetireAll();
	}

	GLog->RemoveOutputDevice(&Capture);
	Sim->OnCommandRejected.Remove(RejectH);
	return Finish();
}
