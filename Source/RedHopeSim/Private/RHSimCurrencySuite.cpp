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
#include "Misc/Crc.h"

// E5 the Martian currency self-test (docs/qa/2026-10-24/e5-martian-currency-design.md: DIRECTOR ANSWER Q-A (a), Q-B (a), Q-C (a) > the queue's
// ORCHESTRATOR-RULINGS and CROSS-CHECK > E5-brief.md > PROFESSOR RE-REVIEW conditions 1-7 > REVISION R1-R7 > review > design). Fails loudly.
// `-currency`. No path logs an Error on purpose, and a file's size is printed only in the masked "(N bytes" form.
//  CU0 the data and the clamps; CU1 inertness (the switch off turns off ALL of E5, both bands) and CU1b the unissued multiplier bit for bit;
//  CU2 one rate step by hand and CU2b 10,000 seeded steps against a reference written here; CU3 issuing (every refusal, DefyIssue against a
//  plain Defy, the issue roll); CU4 joining (name order, never FName order; a stance of -1; a stance of 40 after a Defy; an embargoed member;
//  Greed); CU5 settlement (a mark order, a converted credit order, the roundings); CU6 the desk; CU7 the political record; CU9 the withdrawal
//  (the era band, and the reopen edge in both); CU10 the save and every load repair; CU11 ruling 11; CU12 politics untouched; CU8 era parity
//  with a withdrawal, last (it injects a rival of its own). CU13 is the mutation list, run by a driver and recorded in design-decisions
//  (the brief's 14, and four from the reviews: 15 the redemption floored per part, which CU9's one-floor fixture bites; 16 the load's desk-order
//  raise of NextCommandId removed, 17 and 18 O8's Issue-entry exception removed from the flag's and the withdrawal's rule, which CU10 bites).
// Every expected number that is not plain arithmetic was computed by a separate integer model of 3.3 written from the spec (not from the C++);
// CU2b's reference below is a second, independent transcription.

namespace
{
	// Captures LogRedHopeSim lines for the "logs nothing" and "logs this Warning" checks.
	struct FCurSuiteCapture : public FOutputDevice
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
		TArray<FString> Matching(const TCHAR* Needle) { Pump(); TArray<FString> Out; for (const FString& L : Lines) { if (L.Contains(Needle)) { Out.Add(L); } } return Out; }
		void Clear() { Pump(); Lines.Reset(); Warnings.Reset(); Errors.Reset(); }
	};

	// The currency block read straight off a save's bytes with every field's offset, so a test can doctor one field and reseal the frame.
	struct FCurSuiteBlock
	{
		bool bOk = false;
		int32 FrameAt = INDEX_NONE, PayloadAt = 0, PayloadBytes = 0;
		uint8 Issued = 0, Withdraw = 0;
		int32 IssueSol = 0, OpenSol = 0, WithdrawSol = 0;
		int64 Rate = 0, IdentView = 0, TensionView = 0;
		int32 IssuedAt = 0, IssueSolAt = 0, OpenSolAt = 0, WithdrawAt = 0, WithdrawSolAt = 0, RateAt = 0, IdentViewAt = 0, TensionViewAt = 0;
		struct FEntry { int32 ClearSol = 0, CommandId = 0, Sub = 0; double ExecuteAt = 0.0; uint8 Kind = 0, HasIdentity = 0; int64 IdentityMilli = 0, TensionDelta = 0; int32 At = 0, ExecuteAtAt = 0, SubAt = 0; };
		TArray<FEntry> Journal;
		int32 FillAt = 0, HeadAt = 0;
		int64 Balance = 0, Earned = 0, Spent = 0, Reserved = 0, Desk = 0, Redeemed = 0;
		int32 BalanceAt = 0, EarnedAt = 0, SpentAt = 0, ReservedAt = 0, DeskAt = 0, RedeemedAt = 0;
		struct FMember { FString Name; int32 From = 0, NameAt = 0; };
		TArray<FMember> Members;
		struct FMark { int32 Id = 0; int64 Held = 0, Owed = 0; int32 IdAt = 0, HeldAt = 0, OwedAt = 0; };
		TArray<FMark> Marks;
		struct FDesk { int32 Id = 0, ClearSol = 0; uint8 Dir = 0, Cleared = 0; int64 Amount = 0; int32 IdAt = 0; };
		TArray<FDesk> Desks;
	};

	bool CurSuiteParse(const TArray<uint8>& W, FCurSuiteBlock& B)
	{
		B = FCurSuiteBlock();
		int32 Frame = INDEX_NONE, Bytes = 0;
		if (!RHSaveFrame::Find(W, RHSaveFrame::CurrencyTag, Frame, Bytes))
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
		B.IssuedAt = Cur; B.Issued = R8();
		B.IssueSolAt = Cur; B.IssueSol = R32();
		B.OpenSolAt = Cur; B.OpenSol = R32();
		B.WithdrawAt = Cur; B.Withdraw = R8();
		B.WithdrawSolAt = Cur; B.WithdrawSol = R32();
		B.RateAt = Cur; B.Rate = R64();
		B.IdentViewAt = Cur; B.IdentView = R64();
		B.TensionViewAt = Cur; B.TensionView = R64();
		const int32 JournalCount = R32();
		for (int32 j = 0; j < JournalCount && !bShort; ++j)
		{
			FCurSuiteBlock::FEntry E;
			E.At = Cur;
			E.ClearSol = R32();
			E.ExecuteAtAt = Cur; E.ExecuteAt = RD();
			E.CommandId = R32();
			E.SubAt = Cur; E.Sub = R32();
			E.Kind = R8();
			E.HasIdentity = R8();
			E.IdentityMilli = R64();
			E.TensionDelta = R64();
			B.Journal.Add(E);
		}
		for (int32 e = 0; e < 16; ++e) { R64(); }
		B.FillAt = Cur; R32();
		B.HeadAt = Cur; R32();
		B.BalanceAt = Cur; B.Balance = R64();
		B.EarnedAt = Cur; B.Earned = R64();
		B.SpentAt = Cur; B.Spent = R64();
		B.ReservedAt = Cur; B.Reserved = R64();
		B.DeskAt = Cur; B.Desk = R64();
		B.RedeemedAt = Cur; B.Redeemed = R64();
		const int32 MemberCount = R32();
		for (int32 m = 0; m < MemberCount && !bShort; ++m)
		{
			FCurSuiteBlock::FMember M;
			const int32 Len = R32();
			M.NameAt = Cur;
			if (Len > 0 && Cur + Len <= End) { M.Name = FString(ANSI_TO_TCHAR((const ANSICHAR*)(W.GetData() + Cur))); } else { bShort = true; }
			Cur += FMath::Max(0, Len);
			M.From = R32();
			B.Members.Add(M);
		}
		const int32 MarkCount = R32();
		for (int32 k = 0; k < MarkCount && !bShort; ++k)
		{
			FCurSuiteBlock::FMark Mo;
			Mo.IdAt = Cur; Mo.Id = R32();
			Mo.HeldAt = Cur; Mo.Held = R64();
			Mo.OwedAt = Cur; Mo.Owed = R64();
			B.Marks.Add(Mo);
		}
		const int32 DeskCount = R32();
		for (int32 k = 0; k < DeskCount && !bShort; ++k)
		{
			FCurSuiteBlock::FDesk D;
			D.IdAt = Cur; D.Id = R32();
			D.Dir = R8();
			D.Amount = R64();
			D.ClearSol = R32();
			D.Cleared = R8();
			B.Desks.Add(D);
		}
		B.bOk = !bShort && Cur == End;
		return B.bOk;
	}

	// E4's market block walked by its counts (the market and rival parts skipped), for CU9's doctored clearing sol and CU10's doctored reserved
	// counter: OutReservedAt is the reserved credit's offset; OutAt the clearing sol of convoy ConvoyIndex.
	bool CurSuiteConvoyClearSolAt(const TArray<uint8>& W, int32 ConvoyIndex, int32& OutAt, int32* OutReservedAt = nullptr)
	{
		OutAt = INDEX_NONE;
		int32 Frame = INDEX_NONE, Bytes = 0;
		if (!RHSaveFrame::Find(W, RHSaveFrame::MarketTag, Frame, Bytes))
		{
			return false;
		}
		const int32 End = Frame + RHSaveFrame::HeaderBytes + Bytes;
		int32 Cur = Frame + RHSaveFrame::HeaderBytes;
		const auto R32 = [&]() { int32 V = 0; if (Cur + 4 <= End) { FMemory::Memcpy(&V, W.GetData() + Cur, 4); } Cur += 4; return V; };
		const auto Skip = [&](int32 N) { Cur += N; };
		const auto SkipStr = [&]() { const int32 Len = R32(); Cur += FMath::Max(0, Len); };
		const int32 SlotCount = R32();
		for (int32 k = 0; k < SlotCount; ++k) { SkipStr(); }
		const int32 MarketCount = R32();
		for (int32 m = 0; m < MarketCount; ++m) { Skip(4 + SlotCount * 17 * 8 + 8); }
		Skip(4); // the open sol
		const int32 RivalCount = R32();
		for (int32 r = 0; r < RivalCount; ++r) { SkipStr(); Skip(SlotCount * 8); }
		if (OutReservedAt) { *OutReservedAt = Cur + 8; }
		Skip(8 + 8 + 4); // sales, reserved, next Id
		const int32 ConvoyCount = R32();
		for (int32 c = 0; c < ConvoyCount && Cur < End; ++c)
		{
			Skip(4); SkipStr(); Skip(1 + 8 + 1);
			if (c == ConvoyIndex) { OutAt = Cur; return true; }
			Skip(4 + 1 + 8 + 8 + 8);
			const int32 Lines = R32();
			for (int32 l = 0; l < Lines; ++l) { SkipStr(); Skip(8); }
		}
		return false;
	}

	// A frame rebuilt around a new payload (header, CRC and footer rewritten for its new length), for CU10's doctored lengths. The outer reader
	// still finds each block where it stands (the frames before it do not move) and the walk from the end still meets every footer.
	bool CurSuiteReframe(TArray<uint8>& File, uint32 Tag, const TArray<uint8>& NewPayload)
	{
		int32 At = INDEX_NONE, Bytes = 0;
		if (!RHSaveFrame::Find(File, Tag, At, Bytes))
		{
			return false;
		}
		const int32 After = At + RHSaveFrame::HeaderBytes + Bytes + RHSaveFrame::FooterBytes;
		const uint32 TagV = Tag, Size = (uint32)NewPayload.Num(), Crc = FCrc::MemCrc32(NewPayload.GetData(), NewPayload.Num());
		TArray<uint8> Out;
		Out.Append(File.GetData(), At);
		Out.Append((const uint8*)&TagV, 4);
		Out.Append((const uint8*)&Size, 4);
		Out.Append((const uint8*)&Crc, 4);
		Out.Append(NewPayload);
		Out.Append((const uint8*)&Size, 4);
		Out.Append((const uint8*)&TagV, 4);
		Out.Append(File.GetData() + After, File.Num() - After);
		File = MoveTemp(Out);
		return true;
	}

	// The seeded LCG of CU2b (E4's MX5 generator).
	struct FCurSuiteLcg
	{
		uint64 State = 0x2545F4914F6CDD1DULL;
		uint32 Next() { State = State * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32)(State >> 33); }
		int64 Range(int64 Lo, int64 Hi) { return Lo + (int64)(Next() % (uint32)(Hi - Lo + 1)); }
	};

	// CU2b's reference: 3.3 transcribed again from the spec, with its own truncating division (never MarketDivTrunc, never the C++ under test).
	int64 CurSuiteRefTrunc(int64 A, int64 B) { const int64 Q = (A < 0 ? -A : A) / B; return A < 0 ? -Q : Q; }
	int64 CurSuiteRefStep(int64 Rate, int64 Sold, int64 Bought, int64 IdentMilli, int64 TensionMilli)
	{
		const int64 R0 = 1000, WTrade = 40, WIdent = 30, WTension = 30, WAnchor = 50, Gain = 20, MaxStep = 200, MinVol = 5000000;
		const int64 Trade = CurSuiteRefTrunc((Sold - Bought) * 10000, (Sold + Bought) > MinVol ? (Sold + Bought) : MinVol);
		const int64 Ident = CurSuiteRefTrunc(IdentMilli, 10);
		const int64 Tension = CurSuiteRefTrunc(TensionMilli, 10);
		const int64 Anchor = CurSuiteRefTrunc((Rate - R0) * 10000, R0);
		const int64 Pressure = CurSuiteRefTrunc(Trade * WTrade + Ident * WIdent - Tension * WTension - Anchor * WAnchor, 100);
		int64 Step = CurSuiteRefTrunc(Pressure * Gain, 100);
		Step = Step > MaxStep ? MaxStep : (Step < -MaxStep ? -MaxStep : Step);
		int64 Next = Rate;
		if (Step != 0)
		{
			int64 Mag = (Rate * (Step < 0 ? -Step : Step)) / 10000;
			Mag = Mag < 1 ? 1 : Mag;
			Next = Rate + (Step > 0 ? Mag : -Mag);
		}
		return Next < 250 ? 250 : (Next > 4000 ? 4000 : Next);
	}
}

int32 RHRunCurrencySuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== CURRENCY TEST (E5, 2026-10-27) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("CURRENCY ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("CURRENCY FAIL: %s"), *What); ++Fails; }
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
	const auto Info = [](const FString& What) { UE_LOG(LogRedHopeSim, Display, TEXT("CURRENCY info: %s"), *What); };
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	using EDir = URHSimWorldSubsystem::ERHDeskDir;
	using EAct = URHSimWorldSubsystem::ERHPoliticalAct;
	const FName NZarya(TEXT("Zarya")), NMeridian(TEXT("Meridian"));
	const FName NStruct(TEXT("Struct")), NWater(TEXT("Water")), NOx(TEXT("Oxygen")), NHyd(TEXT("Hydrogen")), NIce(TEXT("Ice")), NFood(TEXT("Food")), NParts(TEXT("SpareParts"));
	const auto Cr = [](int64 Mcr) { return URHSimWorldSubsystem::FormatCredits(Mcr); };
	const auto Finish = [&]() -> int32
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CURRENCY TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto EraOnce = [&]() { Clock->Debug_AdvanceSimSeconds(EraDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->EraStep((float)EraDt); };
	const auto AgentOnce = [&]() { Clock->Debug_AdvanceSimSeconds((double)AgentDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->Debug_StepSim(AgentDt); };
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	FCurSuiteCapture Capture;
	GLog->AddOutputDevice(&Capture);
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	// The alerts (the deck's banner keeps only the last one, so an act raises one): CU3 and CU9 read what each act raised.
	TArray<FString> Alerts;
	const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& Text) { Alerts.Add(Text); });
	const auto AlertsStarting = [&Alerts](const TCHAR* Head) { int32 N = 0; for (const FString& A : Alerts) { N += A.StartsWith(Head) ? 1 : 0; } return N; };
	FString SaveErr;
	// Every fixture's injected rival: an economy in region 0, its standing lots empty. Retiring one re-injects it with no economy and no
	// activity, so its entry goes at the next sync and it never touches a later fixture.
	TArray<FName> Injected;
	const auto Inject = [&](FName Name, double Km, const FString& Produce, const FString& Consume, const FString& Start, const FString& Cap, int32 Stance, float Relation = 30.f)
	{
		FRHRivalRow Row;
		Row.DisplayName = Name.ToString() + TEXT(" Post");
		Row.Nation = TEXT("Test");
		Row.DistanceKm = (float)Km;
		Row.RegionId = 0;
		Row.Produce = Produce; Row.Consume = Consume; Row.StoreStart = Start; Row.StoreCap = Cap;
		Row.BlocStance = Stance;
		Row.RelationStart = Relation;
		Row.SliceActive = true;
		DefsSub->Debug_InjectRival(Name, Row);
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
	const FRHRivalRow ZaryaRow = *DefsSub->GetRival(NZarya), MeridianRow = *DefsSub->GetRival(NMeridian);
	Expect(Sim->SaveColony(TEXT("currencybase"), SaveErr), FString::Printf(TEXT("CU the fresh colony is saved as the base every fixture loads (%s)"), *SaveErr));
	const auto LoadSlot = [&](const TCHAR* Slot)
	{
		FString E;
		const bool bOk = Sim->LoadColony(Slot, E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("CURRENCY FAIL: loading %s failed: %s"), Slot, *E); ++Fails; }
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetEarthTakesQuota(true);
		Sim->Debug_SetMarketEnabled(true);
		Sim->Debug_SetMarketRivalOnly(false);
		Sim->Debug_SetCurrencyEnabled(true);
		Sim->Debug_ClearPolitics();
		Rejects.Reset();
		return bOk;
	};
	const auto Reset = [&]() { return LoadSlot(TEXT("currencybase")); };
	// One order through the real uplink: transmitted, then one 60 s step (the 45 s lag is over), booked or refused.
	const auto Send = [&](const TCHAR* Verb, FName Target, const FString& Text = FString(), double Value = 0.0) -> bool
	{
		const int32 Rej = Rejects.Num();
		FRHCommand C; C.Verb = FName(Verb); C.Target = Target; C.Text = Text; C.Value = Value;
		Sim->EnqueueCommand(C);
		EraOnce();
		return Rejects.Num() == Rej;
	};
	const auto LastReject = [&]() { return Rejects.Num() > 0 ? Rejects.Last() : FString(); };
	const auto RollNext = [&]() { Sim->Debug_RollMarket(Sim->GetMarketCheckedSol() + 1); };
	// Before a fixture's save: the clock brought up to the market's open sol by one real step ending there (C5d's ledger rolls to it), so the
	// load does not repair an open sol ahead of C5d's (E4's own rule) - RollNext runs the market ahead of the clock.
	const auto SyncClock = [&]()
	{
		const double To = (double)Sim->GetMarketCheckedSol() * SolLen;
		if (Clock->GetSimSecondsTotal() < To)
		{
			Clock->Debug_SetSimSeconds(To - EraDt);
			EraOnce();
		}
	};
	// A pending demand: the tension set to T and one step for StepEarth to raise it.
	const auto Demand = [&](double Tension) { Sim->Debug_SetRequisitionInputs(Sim->GetIdentityAxis(), Tension); EraOnce(); return Sim->IsEarthDemandPending(); };
	// The colony the order fixtures run in: fuel, parts, goods to sell, credit (no Water or Oxygen: Q1 stays unmet and pays no award).
	const auto Provision = [&](int64 CreditMcr)
	{
		Sim->AddStock(NHyd, 600.0);
		Sim->AddStock(NParts, 40.0);
		Sim->AddStock(NIce, 3000.0);
		Sim->AddStock(NStruct, 2000.0);
		Sim->Debug_ZeroCredits();
		Sim->Debug_GrantCredits(CreditMcr);
	};
	// The mark issued through the real uplink at identity +20 (the record synced to it), then the issue roll and the next: the market counts in
	// marks for an order booked now (its clearing sol is the open sol), and Zarya (stance 20) is a member from it.
	const auto IssueOpen = [&](double Identity = 20.0) -> bool
	{
		Sim->Debug_SetRequisitionInputs(Identity, Sim->GetEarthTension());
		Sim->Debug_SyncPoliticalView();
		if (!Send(TEXT("Currency"), FName("Issue")))
		{
			return false;
		}
		RollNext();
		RollNext();
		return Sim->GetCurrencyIssueSol() >= 0 && Sim->IsMarketInMarks();
	};
	// Both purses' invariants and the lifetime credit (R4).
	const auto Inv = [&]() -> bool
	{
		int64 ConvoyMcr = 0, ConvoyMmk = 0, DeskMcr = 0, DeskMmk = 0;
		for (const FRHConvoy& C : Sim->GetConvoys())
		{
			if (C.IsPriced() && !C.bCleared) { if (C.bMarks) { ConvoyMmk += C.ReservedMmk; } else { ConvoyMcr += C.ReservedMcr; } }
		}
		TArray<FRHDeskOrder> Desk;
		Sim->GetDeskOrders(Desk);
		for (const FRHDeskOrder& D : Desk) { if (D.Dir == (uint8)EDir::ToMarks) { DeskMcr += D.Amount; } else { DeskMmk += D.Amount; } }
		return Sim->GetCreditBalanceMcr() == Sim->GetCreditAwardedMcr() - Sim->GetCreditSpentMcr() && Sim->GetCreditBalanceMcr() >= 0
			&& Sim->GetCreditReservedMcr() == ConvoyMcr + DeskMcr && Sim->GetCreditReservedMcr() >= 0 && Sim->GetCreditReservedMcr() <= Sim->GetCreditBalanceMcr()
			&& Sim->GetMarkBalance() == Sim->GetMarkEarnedMmk() - Sim->GetMarkSpentMmk() && Sim->GetMarkBalance() >= 0
			&& Sim->GetMarkReservedMmk() == ConvoyMmk + DeskMmk && Sim->GetMarkReservedMmk() >= 0 && Sim->GetMarkReservedMmk() <= Sim->GetMarkBalance()
			&& Sim->GetSalesPaidMcr() >= 0 && Sim->GetMarketSalesMcr() >= 0 && Sim->GetDeskCreditMcr() >= 0 && Sim->GetCurrencyRedeemedMcr() >= 0
			&& Sim->GetSalesPaidMcr() + Sim->GetMarketSalesMcr() + Sim->GetDeskCreditMcr() + Sim->GetCurrencyRedeemedMcr() <= Sim->GetCreditAwardedMcr();
	};
	// HEAD's requisition multiplier, computed here in GetRequisitionMultiplier's own operation order (CU1b, CU3, CU9).
	const double EarthBonus = DefsSub->GetConfigTableScalar(FName("RequisitionEarthBonus"), 0.4);
	const double MartianPenalty = DefsSub->GetConfigTableScalar(FName("RequisitionMartianPenalty"), 0.6);
	const double TensionPenalty = DefsSub->GetConfigTableScalar(FName("RequisitionTensionPenalty"), 0.3);
	const double IssuePenalty = DefsSub->GetConfigTableScalar(FName("CurrencyRequisitionPenalty"), 0.10);
	const auto RefMult = [&](double Axis, double Tension, bool bIssued) -> double
	{
		const double AxisFrac = Axis / 100.0;
		const double EarthAlign = FMath::Max(0.0, -AxisFrac);
		const double MartianAlign = FMath::Max(0.0, AxisFrac);
		double Mult = 1.0
			+ EarthBonus * EarthAlign
			- MartianPenalty * MartianAlign
			- TensionPenalty * (Tension / 100.0);
		if (bIssued)
		{
			Mult -= IssuePenalty;
		}
		return FMath::Clamp(Mult, 0.0, 1.0 + EarthBonus);
	};
	// The market's open sol at least two sols ahead of the clock, so no real roll can land inside a fixture's next steps.
	const auto Ahead = [&]() { while (Sim->GetMarketCheckedSol() <= Clock->GetSol() + 1) { RollNext(); } };
	// E4's quote margins, written again here (3.2 step 5), and the converted reservation of 2.4 from its own formula.
	const auto Hi = [](int64 Q) { return Q * 10500 / 10000 + 1; };
	const auto Lo = [](int64 Q) { return FMath::Max<int64>(0, Q * 9500 / 10000 - 1); };
	const auto CeilDiv = [](int64 A, int64 B) { return (A + B - 1) / B; };

	// --- CU0. The data: the 16 config rows read their CSV values from the live DT and each clamp holds; BlocStance from the live DT, equal to
	// the CSV; a row built without the column reads -1. ---
	{
		struct FCfg { const TCHAR* Name; double Value; };
		const FCfg Cfgs[] = {
			{ TEXT("CurrencyIssueIdentity"), 20.0 }, { TEXT("CurrencyIssueTension"), 10.0 }, { TEXT("CurrencyRequisitionPenalty"), 0.10 }, { TEXT("CurrencyRateStartMcr"), 1000.0 },
			{ TEXT("CurrencyWindowSols"), 5.0 }, { TEXT("CurrencyMinVolumeMmk"), 5000000.0 }, { TEXT("CurrencyTradeWeight"), 40.0 }, { TEXT("CurrencyIdentityWeight"), 30.0 },
			{ TEXT("CurrencyTensionWeight"), 30.0 }, { TEXT("CurrencyAnchorWeight"), 50.0 }, { TEXT("CurrencyGainPct"), 20.0 }, { TEXT("CurrencyMaxStepBp"), 200.0 },
			{ TEXT("CurrencyFloorBp"), 2500.0 }, { TEXT("CurrencyCeilBp"), 40000.0 }, { TEXT("DeskSpreadBp"), 300.0 }, { TEXT("DeskCapPerSol"), 500.0 } };
		TArray<TPair<FName, double>> Live;
		Sim->GetCurrencyConfig(Live);
		int32 DtRight = 0, SimRight = 0;
		FString Bad;
		for (const FCfg& C : Cfgs)
		{
			const bool bDt = DefsSub->GetConfigTableScalar(FName(C.Name), -999.0) == C.Value;
			const TPair<FName, double>* L = Live.FindByPredicate([&C](const TPair<FName, double>& P) { return P.Key == FName(C.Name); });
			const bool bSim = L && L->Value == C.Value;
			DtRight += bDt ? 1 : 0;
			SimRight += bSim ? 1 : 0;
			if (!bDt || !bSim) { Bad += FString(C.Name) + TEXT(" "); }
		}
		// The clamps, through the one function InitCurrency reads every row with: an out-of-range value lands on its bound, an in-range one passes.
		using M = URHSimWorldSubsystem;
		struct FClamp { const TCHAR* Name; double Raw; double Want; };
		const FClamp Clamps[] = {
			{ TEXT("CurrencyWindowSols"), 0.0, 1.0 }, { TEXT("CurrencyWindowSols"), 99.0, 8.0 }, { TEXT("CurrencyWindowSols"), 5.4, 5.0 }, { TEXT("CurrencyMinVolumeMmk"), 0.0, 1.0 },
			{ TEXT("CurrencyMaxStepBp"), 0.0, 1.0 }, { TEXT("CurrencyMaxStepBp"), 20000.0, 10000.0 }, { TEXT("CurrencyFloorBp"), 10000.0, 9999.0 }, { TEXT("CurrencyFloorBp"), 0.0, 1.0 },
			{ TEXT("CurrencyCeilBp"), 10000.0, 10001.0 }, { TEXT("CurrencyRateStartMcr"), 0.0, 1.0 }, { TEXT("DeskSpreadBp"), -5.0, 0.0 }, { TEXT("DeskSpreadBp"), 10000.0, 9999.0 },
			{ TEXT("DeskCapPerSol"), -1.0, 0.0 }, { TEXT("CurrencyIssueIdentity"), 150.0, 100.0 }, { TEXT("CurrencyIssueTension"), -3.0, 0.0 }, { TEXT("CurrencyRequisitionPenalty"), 2.0, 1.0 },
			{ TEXT("CurrencyTradeWeight"), -3.0, 0.0 }, { TEXT("CurrencyGainPct"), 20.0, 20.0 } };
		int32 ClampRight = 0;
		FString ClampBad;
		for (const FClamp& C : Clamps)
		{
			const double Got = M::CurrencyConfigClamp(FName(C.Name), C.Raw);
			ClampRight += Got == C.Want ? 1 : 0;
			if (Got != C.Want) { ClampBad += FString::Printf(TEXT("%s(%g)=%g; "), C.Name, C.Raw, Got); }
		}
		ExpectSeq({
			[&]() { return DtRight == 16; },
			[&]() { return SimRight == 16; },
			[&]() { return ClampRight == (int32)UE_ARRAY_COUNT(Clamps); },
			[&]() { return DefsSub->GetConfigTableScalar(FName("CurrencyFloorBp"), 0.0) < 10000.0 && DefsSub->GetConfigTableScalar(FName("CurrencyCeilBp"), 0.0) > 10000.0; },
			},
			FString::Printf(TEXT("CU0 the 16 new RH_Config rows read their authored values from the live DT (%d of 16) and the sim holds them after their clamps (%d of 16) %s; %d clamp cases land on their bounds (a window of 0 or 99 sols is 1 or 8, a floor of 10000 bp is 9999, a ceiling of 10000 is 10001, a negative spread or cap is 0 ...) %s"),
				DtRight, SimRight, *Bad, (int32)UE_ARRAY_COUNT(Clamps), *ClampBad));
		// BlocStance: the live DT, the CSV (its columns up to BlocStance are unquoted and hold no comma), and a row built without the column.
		FString Csv;
		const bool bCsv = FFileHelper::LoadFileToString(Csv, *(FPaths::ProjectDir() / TEXT("docs/data/RH_Rivals.csv")));
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		int32 CsvRows = 0, CsvSame = 0;
		if (Lines.Num() > 1)
		{
			TArray<FString> Head;
			Lines[0].ParseIntoArray(Head, TEXT(","), false);
			const int32 CN = Head.IndexOfByKey(TEXT("Name")), CB = Head.IndexOfByKey(TEXT("BlocStance")), CK = Head.IndexOfByKey(TEXT("StoreCap"));
			for (int32 i = 1; CN != INDEX_NONE && CB != INDEX_NONE && i < Lines.Num(); ++i)
			{
				TArray<FString> Cells;
				Lines[i].ParseIntoArray(Cells, TEXT(","), false);
				const FRHRivalRow* Row = Cells.Num() > CB ? DefsSub->GetRival(FName(*Cells[CN])) : nullptr;
				++CsvRows;
				CsvSame += (Row && Row->BlocStance == FCString::Atoi(*Cells[CB]) && CB == CK + 1) ? 1 : 0;
			}
		}
		FRHRivalRow Bare;
		Bare.DisplayName = TEXT("CuBare Post");
		DefsSub->Debug_InjectRival(FName("CuBare"), Bare);
		const FRHRivalRow* BareRow = DefsSub->GetRival(FName("CuBare"));
		const int32 BareStance = BareRow ? BareRow->BlocStance : 0;
		FRHRivalRow Off = Bare;
		Off.SliceActive = false;
		DefsSub->Debug_InjectRival(FName("CuBare"), Off);
		Sim->Debug_SyncRivalEconomies();
		ExpectSeq({
			[&]() { return ZaryaRow.BlocStance == 20; },
			[&]() { return MeridianRow.BlocStance == 40; },
			[&]() { return bCsv; },
			[&]() { return CsvRows == 2; },
			[&]() { return CsvSame == 2; },
			[&]() { return BareStance == -1; },
			},
			FString::Printf(TEXT("CU0 BlocStance from the live DT: Zarya %d, Meridian %d; the CSV's BlocStance column (straight after StoreCap) equals the live DT on %d of %d rows; an injected row built without the column reads %d (it never joins)"),
				ZaryaRow.BlocStance, MeridianRow.BlocStance, CsvSame, CsvRows, BareStance));
	}

	// The digest of everything outside the currency: stocks, shelves, both purses, relations, Influence, HumanNature, tension, identity, the
	// multiplier, Hope, the population, the quota phase, the projected ending, the ledger.
	const auto Digest = [&]() -> FString
	{
		FString D = FString::Printf(TEXT("sol %d bal %lld/%lld/%lld res %lld marks %lld/%lld/%lld/%lld rel %.17g/%.17g inf %.17g hn %.17g ten %.17g id %.17g mult %.17g hope %.17g pop %d phase %d end %d demand %d"),
			Clock->GetSol(), Sim->GetCreditBalanceMcr(), Sim->GetCreditAwardedMcr(), Sim->GetCreditSpentMcr(), Sim->GetCreditReservedMcr(),
			Sim->GetMarkBalance(), Sim->GetMarkEarnedMmk(), Sim->GetMarkSpentMmk(), Sim->GetMarkReservedMmk(),
			Sim->GetRivalRelation(NZarya), Sim->GetRivalRelation(NMeridian), Sim->GetInfluence(), Sim->GetHumanNatureAxis(), Sim->GetEarthTension(), Sim->GetIdentityAxis(),
			Sim->GetRequisitionMultiplier(), Sim->GetHopeSmoothed(), Sim->GetPopulation(), (int32)Sim->GetQuotaPhase(), (int32)Sim->GetProjectedEnding(), Sim->IsEarthDemandPending() ? 1 : 0);
		DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow&) { D += FString::Printf(TEXT(" %s=%.17g/%.17g"), *N.ToString(), Sim->GetStock(N), Sim->GetTotalSolid(N)); });
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			D += FString::Printf(TEXT(" #%d%s"), B.Id, *B.DefName.ToString());
			TArray<FName> Keys;
			for (const TMap<FName, double>* Map : { &B.InputKg, &B.OutputKg })
			{
				Keys.Reset();
				Map->GetKeys(Keys);
				Keys.Sort([](const FName& A, const FName& C) { return A.ToString().Compare(C.ToString(), ESearchCase::CaseSensitive) < 0; });
				D += TEXT("[");
				for (const FName& K : Keys) { D += FString::Printf(TEXT("%s:%.17g,"), *K.ToString(), Map->FindRef(K)); }
				D += TEXT("]");
			}
		}
		return D;
	};

	// --- CU1. Inertness: the switch off turns off ALL of E5 (the record, the journal, the bloc, the rate, both issue acts); twins on and off, 10
	// sols in each band with a Defy, a Comply and a Pacify through the uplink, are bit-identical outside the currency, their saves byte-identical
	// up to 'RHE5', their logs identical; the "on" twin's record moved, silently. ---
	{
		struct FCu1 { FString Digest; TArray<uint8> Bytes; int64 IdView = 0, TenView = 0, TenView0 = 0; TArray<FString> Log; int32 Rejected = 0; };
		const auto Run = [&](bool bAgent, bool bOn) -> FCu1
		{
			Reset();
			Sim->Debug_SetCurrencyEnabled(bOn);
			Sim->Debug_AddInfluence(40.0);
			FCu1 T;
			T.TenView0 = Sim->GetTensionView();
			Capture.Clear();
			struct FEv { double T; int32 Kind; };
			const FEv Evs[] = { { 1.0, 0 }, { 1.5, 1 }, { 4.0, 0 }, { 4.5, 2 }, { 6.0, 3 }, { 6.5, 4 } };
			int32 Next = 0;
			while (Clock->GetSimSecondsTotal() < 10.0 * SolLen - 1.0e-6)
			{
				const double Now = Clock->GetSimSecondsTotal();
				while (Next < (int32)UE_ARRAY_COUNT(Evs) && Now >= Evs[Next].T * SolLen)
				{
					FRHCommand C;
					switch (Evs[Next].Kind)
					{
					case 0: Sim->Debug_AddTension(65.0 - Sim->GetEarthTension()); break;
					case 1: C.Verb = FName("Solidarity"); C.Target = FName("Defy"); Sim->EnqueueCommand(C); break;
					case 2: C.Verb = FName("Solidarity"); C.Target = FName("Comply"); Sim->EnqueueCommand(C); break;
					case 3: Sim->Debug_SetEmbargo(NZarya, 5.0); break;
					case 4: C.Verb = FName("Pacify"); C.Target = NZarya; Sim->EnqueueCommand(C); break;
					default: break;
					}
					++Next;
				}
				if (bAgent) { AgentOnce(); } else { EraOnce(); }
			}
			T.Digest = Digest();
			T.IdView = Sim->GetIdentityView();
			T.TenView = Sim->GetTensionView();
			T.Rejected = Rejects.Num();
			FString Err;
			Sim->SaveColony(TEXT("cucu1"), Err);
			FFileHelper::LoadFileToArray(T.Bytes, *SaveFilePath(TEXT("cucu1")));
			FCurSuiteCapture::Pump();
			// The logs are compared with every "(N bytes" masked, as diff_battery.py masks them: an autosave mid-run measures the 'RHE5' frame,
			// whose journal (sized by history) only the on twin keeps between an act and its roll.
			for (const FString& L : Capture.Lines)
			{
				FString M = L;
				const int32 Bytes = M.Find(TEXT(" bytes"));
				const int32 Open = Bytes > 0 ? M.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromEnd, Bytes) : INDEX_NONE;
				if (Open != INDEX_NONE && Bytes > Open + 1)
				{
					M = M.Left(Open + 1) + TEXT("N") + M.Mid(Bytes);
				}
				T.Log.Add(M);
			}
			Sim->Debug_SetCurrencyEnabled(true);
			return T;
		};
		const auto UpToCurrency = [](const TArray<uint8>& B) { int32 F = INDEX_NONE, P = 0; return RHSaveFrame::Find(B, RHSaveFrame::CurrencyTag, F, P) ? F : INDEX_NONE; };
		for (const bool bAgent : { false, true })
		{
			const FCu1 On = Run(bAgent, true);
			const FCu1 Off = Run(bAgent, false);
			const int32 OnAt = UpToCurrency(On.Bytes), OffAt = UpToCurrency(Off.Bytes);
			const bool bSame = OnAt > 0 && OnAt == OffAt && FMemory::Memcmp(On.Bytes.GetData(), Off.Bytes.GetData(), OnAt) == 0;
			ExpectSeq({
				[&]() { return On.Digest == Off.Digest; },
				[&]() { return On.Digest.Len() > 300; },
				[&]() { return On.Rejected == 0 && Off.Rejected == 0; },
				[&]() { return bSame; },
				[&]() { return On.Log == Off.Log; },
				[&]() { return On.TenView != On.TenView0; },
				[&]() { return Off.TenView == Off.TenView0; },
				},
				FString::Printf(TEXT("CU1 %s band, 10 sols with a Defy, a Comply and a Pacify through the uplink: the digest (%d chars: stocks, shelves, both purses, relations, Influence, HumanNature, tension, identity, the multiplier, Hope, the population, the quota phase, the projected ending) is bit-identical with E5 on and off, the saves are byte-identical up to 'RHE5' (%d bytes), the logs are identical (%d lines); the on twin's record moved (tension %s -> %s) and logged nothing; the off twin's stood still"),
					bAgent ? TEXT("agent") : TEXT("era"), On.Digest.Len(), OnAt, On.Log.Num(), *Cr(On.TenView0), *Cr(On.TenView)));
		}
	}

	// --- CU1b. The unissued multiplier equals HEAD's expression, computed here in its operation order, bit for bit, on a 5 x 4 grid. ---
	{
		Reset();
		const double Axes[] = { -100.0, -20.0, 0.0, 20.0, 100.0 };
		const double Tensions[] = { 0.0, 48.3, 60.0, 100.0 };
		int32 Same = 0, Cases = 0;
		FString Bad;
		for (const double A : Axes)
		{
			for (const double T : Tensions)
			{
				Sim->Debug_SetRequisitionInputs(A, T);
				const double Got = Sim->GetRequisitionMultiplier(), Want = RefMult(A, T, false);
				++Cases;
				Same += Got == Want ? 1 : 0;
				if (Got != Want) { Bad += FString::Printf(TEXT("(%g, %g): %.17g against %.17g; "), A, T, Got, Want); }
			}
		}
		ExpectSeq({
			[&]() { return !Sim->IsCurrencyIssued(); },
			[&]() { return Same == 20; },
			}, FString::Printf(TEXT("CU1b the unissued requisition multiplier equals HEAD's expression bit for bit on %d of %d identity x tension cases (identity -100, -20, 0, +20, +100; tension 0, 48.3, 60, 100) %s"), Same, Cases, *Bad));
	}

	// --- CU2. One rate step by hand: each new rate is an integer written here from a separate model of 3.3, including the 1 mcr minimum, the
	// floor clamp, a step down from the ceiling (no step can rise past 2.4: the anchor outweighs the best trade and identity), the signed
	// division truncating (a floor division would give 999 in the small-deficit case), an anchor beyond +10000 and an unclamped step. ---
	{
		Reset();
		struct FStep { const TCHAR* What; int64 Rate, SoldPerSol, BoughtPerSol; double Axis, Tension; int64 Want; };
		const FStep Steps[] = {
			{ TEXT("neutral"), 1000, 0, 0, 0.0, 0.0, 1000 },
			{ TEXT("the 1 mcr minimum (trade 13 bp, step 1 bp)"), 1000, 1300, 0, 0.0, 0.0, 1001 },
			{ TEXT("the floor clamp (252 -> 247 -> 250)"), 252, 0, 2000000, -100.0, 100.0, 250 },
			{ TEXT("down from the ceiling"), 4000, 0, 0, 0.0, 0.0, 3920 },
			{ TEXT("truncation, not floor (trade -2.6 bp)"), 1000, 0, 260, 0.0, 0.0, 1000 },
			{ TEXT("an anchor beyond +10000 (25000 bp)"), 3500, 0, 0, 0.0, 0.0, 3430 },
			{ TEXT("at the floor the anchor pulls up"), 250, 0, 0, 0.0, 0.0, 255 },
			{ TEXT("issued at +20 with tension 57"), 1000, 0, 0, 20.0, 57.0, 980 },
			{ TEXT("the full surplus and identity"), 1000, 1000000, 0, 100.0, 0.0, 1020 },
			{ TEXT("a negative identity's remainder (-1.999)"), 1000, 0, 0, -1.999, 0.0, 999 },
			{ TEXT("an unclamped step of -180 bp"), 1200, 100000, 0, 30.0, 40.0, 1179 },
			{ TEXT("mixed"), 1180, 400000, 900000, 40.0, 63.0, 1157 } };
		int32 Right = 0;
		FString Seen;
		for (const FStep& S : Steps)
		{
			Sim->Debug_SetRate(S.Rate);
			Sim->Debug_SetBlocTally(S.SoldPerSol, S.BoughtPerSol);
			Sim->Debug_SetRequisitionInputs(S.Axis, S.Tension);
			Sim->Debug_SyncPoliticalView();
			Sim->Debug_StepRate();
			const int64 Got = Sim->GetRate();
			Right += Got == S.Want ? 1 : 0;
			Seen += FString::Printf(TEXT("%s %lld->%lld%s; "), S.What, S.Rate, Got, Got == S.Want ? TEXT("") : *FString::Printf(TEXT(" (WANT %lld)"), S.Want));
		}
		Sim->Debug_SetRate(5000);
		const int64 Top = Sim->GetRate();
		Sim->Debug_SetRate(1);
		const int64 Bottom = Sim->GetRate();
		ExpectSeq({
			[&]() { return Right == (int32)UE_ARRAY_COUNT(Steps); },
			[&]() { return Top == 4000; },
			[&]() { return Bottom == 250; },
			}, FString::Printf(TEXT("CU2 one rate step by hand, %d fixtures: %s and the band holds a rate at [250, 4000] (%lld, %lld)"), (int32)UE_ARRAY_COUNT(Steps), *Seen, Top, Bottom));
	}

	// --- CU2b. 10,000 seeded steps over tallies, views and rates: each equals the reference above, moves at most 2% (or 1 mcr) and stays in the band. ---
	{
		Reset();
		FCurSuiteLcg Rng;
		int64 Steps = 0, Wrong = 0, StepBad = 0, BandBad = 0, Moved = 0;
		FString FirstWrong;
		for (int32 Iter = 0; Iter < 10000; ++Iter)
		{
			const int64 Rate = Rng.Range(250, 4000);
			const int64 S = Rng.Range(0, 3) == 0 ? 0 : Rng.Range(0, 3000000);
			const int64 B = Rng.Range(0, 3) == 0 ? 0 : Rng.Range(0, 3000000);
			const int64 IdMilli = Rng.Range(-100000, 100000);
			const int64 TeMilli = Rng.Range(0, 100000);
			Sim->Debug_SetRate(Rate);
			Sim->Debug_SetBlocTally(S, B);
			Sim->Debug_SetRequisitionInputs((double)IdMilli / 1000.0, (double)TeMilli / 1000.0);
			Sim->Debug_SyncPoliticalView();
			const int64 IdV = Sim->GetIdentityView(), TeV = Sim->GetTensionView();
			Sim->Debug_StepRate();
			const int64 Got = Sim->GetRate();
			const int64 Want = CurSuiteRefStep(Rate, 5 * S, 5 * B, IdV, TeV);
			++Steps;
			if (Got != Want) { ++Wrong; if (FirstWrong.IsEmpty()) { FirstWrong = FString::Printf(TEXT("iter %d rate %lld sold %lld bought %lld id %lld ten %lld: got %lld want %lld"), Iter, Rate, S, B, IdV, TeV, Got, Want); } }
			if (FMath::Abs(Got - Rate) > FMath::Max<int64>(1, Rate * 200 / 10000)) { ++StepBad; }
			if (Got < 250 || Got > 4000) { ++BandBad; }
			Moved += Got != Rate ? 1 : 0;
		}
		ExpectSeq({
			[&]() { return Steps == 10000; },
			[&]() { return Wrong == 0; },
			[&]() { return StepBad == 0; },
			[&]() { return BandBad == 0; },
			[&]() { return Moved > 5000; },
			}, FString::Printf(TEXT("CU2b %lld seeded rate steps: %lld differ from the independent integer reference, %lld move beyond 2%% or 1 mcr, %lld leave [250, 4000]; %lld moved %s"), Steps, Wrong, StepBad, BandBad, Moved, *FirstWrong));
	}

	// --- CU3. Issuing: every refusal with its text (check 3 answers before check 4 unless a neighbor's stance is met: problem M4); a DefyIssue
	// refused leaves its Defy undone; DefyIssue against a plain Defy from one state; the issue roll; a stand-alone Issue at +20. ---
	{
		Reset();
		FString WOff, W3, W4, WIssue;
		Sim->Debug_SetCurrencyEnabled(false);
		const bool bDisabled = !Sim->CanIssueCurrency(false, WOff) && WOff == TEXT("No currency: disabled");
		Sim->Debug_SetCurrencyEnabled(true);
		Sim->Debug_SetRequisitionInputs(0.0, 0.0);
		const bool bNoPartner = !Sim->CanIssueCurrency(false, W3) && W3 == TEXT("No neighbour would join the mark");
		Inject(FName("CuZero"), 40.0, TEXT("Ice:50"), TEXT("Ice:10"), TEXT("Ice:700"), TEXT("Ice:4000"), 0);
		Sim->Debug_SyncRivalEconomies();
		Sim->Debug_SetRequisitionInputs(10.0, 0.0);
		const bool bNotYet = !Sim->CanIssueCurrency(false, W4) && W4 == TEXT("The colony does not lean Martian yet (identity +10, needs +20)");
		const bool bSentRefused = !Send(TEXT("Currency"), FName("Issue")) && LastReject() == W4 && !Sim->IsCurrencyIssued();
		const bool bOtherTarget = !Send(TEXT("Currency"), FName("Mint")) && LastReject() == TEXT("No such currency order 'Mint'");
		RetireAll();
		Sim->Debug_SetRequisitionInputs(0.0, 0.0);
		const bool bNoDemand = !Send(TEXT("Solidarity"), FName("DefyIssue")) && LastReject() == TEXT("There is no demand to answer right now.");
		ExpectSeq({
			[&]() { return bDisabled; },
			[&]() { return bNoPartner; },
			[&]() { return bNotYet; },
			[&]() { return bSentRefused; },
			[&]() { return bOtherTarget; },
			[&]() { return bNoDemand; },
			}, FString::Printf(TEXT("CU3 the refusals: '%s'; at identity 0 '%s' (check 3 answers first); with a stance-0 neighbor at +10 '%s', the same from the uplink; an unknown target '%s'; DefyIssue with no demand gives HEAD's text"), *WOff, *W3, *W4, TEXT("No such currency order 'Mint'")));
		// A DefyIssue whose own check fails leaves the whole Defy undone: Zarya's route closed and Meridian's stance 40 beyond the +20 it would reach.
		Reset();
		Demand(65.0);
		Sim->Debug_CloseRoute(NZarya);
		const double Id0 = Sim->GetIdentityAxis(), Rel0 = Sim->GetRivalRelation(NZarya), Hope0 = Sim->GetSolidarityHope();
		const int32 Journal0 = Sim->GetPoliticalJournal().Num();
		const bool bRefused = !Send(TEXT("Solidarity"), FName("DefyIssue")) && LastReject() == TEXT("No neighbour would join the mark");
		ExpectSeq({
			[&]() { return bRefused; },
			[&]() { return Sim->GetIdentityAxis() == Id0; },
			[&]() { return Sim->IsEarthDemandPending(); },
			[&]() { return Sim->GetRivalRelation(NZarya) == Rel0; },
			[&]() { return Sim->GetSolidarityHope() == Hope0; },
			[&]() { return Sim->GetPoliticalJournal().Num() == Journal0; },
			[&]() { return !Sim->IsCurrencyIssued(); },
			}, FString::Printf(TEXT("CU3 a DefyIssue refused by its own check ('%s') leaves its Defy undone: identity %+.0f, the demand still pending, the relation and the morale swing unmoved, no journal entry, no mark"), *LastReject(), Sim->GetIdentityAxis()));
		Sim->Debug_ClearPolitics();
	}
	{
		// DefyIssue against a plain Defy from one state (tension 61.1, identity 0, a demand pending).
		Reset();
		Sim->Debug_SetRequisitionInputs(0.0, 61.1);
		EraOnce();
		Sim->Debug_SyncPoliticalView();
		Expect(Sim->IsEarthDemandPending() && Sim->SaveColony(TEXT("cu3demand"), SaveErr), TEXT("CU3 the twins' start: a demand pending at tension 61.1, identity 0, saved"));
		struct FSnap { double Id = 0, Ten = 0, Mult = 0, Rel = 0, Hope = 0, Hn = 0, Inf = 0; bool bIssued = false; TArray<FRHPoliticalEntry> Journal; int32 IssueLines = 0, DefyLines = 0; TArray<FString> Alerts; };
		const auto Answer = [&](FName Target) -> FSnap
		{
			LoadSlot(TEXT("cu3demand"));
			Capture.Clear();
			Alerts.Reset();
			Send(TEXT("Solidarity"), Target);
			FSnap S;
			S.Alerts = Alerts;
			S.Id = Sim->GetIdentityAxis(); S.Ten = Sim->GetEarthTension(); S.Mult = Sim->GetRequisitionMultiplier(); S.Rel = Sim->GetRivalRelation(NZarya);
			S.Hope = Sim->GetSolidarityHope(); S.Hn = Sim->GetHumanNatureAxis(); S.Inf = Sim->GetInfluence(); S.bIssued = Sim->IsCurrencyIssued();
			S.Journal = Sim->GetPoliticalJournal();
			S.IssueLines = Capture.CountLines(TEXT("=== CURRENCY ISSUED: the mark (identity +20, tension "));
			S.DefyLines = Capture.CountLines(TEXT("=== SOLIDARITY: DEFIED - axis 20"));
			return S;
		};
		const FSnap P = Answer(FName("Defy"));
		const FSnap I = Answer(FName("DefyIssue"));
		const double WantP = RefMult(P.Id, P.Ten, false), WantI = RefMult(I.Id, I.Ten, true);
		// One alert each: the plain Defy's is Gate D's D44; DefyIssue's is that same text, whole, then the issue's clause (no second alert).
		const FString D44 = P.Alerts.Num() == 1 ? P.Alerts[0] : FString();
		const bool bAlerts = P.Alerts.Num() == 1 && D44.StartsWith(TEXT("YOU DEFIED EARTH")) && !D44.Contains(TEXT("mark"))
			&& I.Alerts.Num() == 1 && I.Alerts[0].StartsWith(D44 + TEXT(" ")) && I.Alerts[0].EndsWith(TEXT("And the colony issues the mark: Earth reads it as a further step away, so tension rises and its supply ships shrink while the mark exists."));
		const bool bJournal = I.Journal.Num() == 2 && I.Journal[0].Kind == (uint8)EAct::Defy && I.Journal[0].Sub == 0 && I.Journal[0].bHasIdentity && I.Journal[0].IdentityMilli == 20000
			&& I.Journal[0].TensionDeltaMilli == -12000 && I.Journal[1].Kind == (uint8)EAct::Issue && I.Journal[1].Sub == 1 && !I.Journal[1].bHasIdentity && I.Journal[1].TensionDeltaMilli == 10000
			&& I.Journal[0].CommandId == I.Journal[1].CommandId && I.Journal[0].ClearSol == FMath::FloorToInt32(I.Journal[0].ExecuteAt / SolLen) && P.Journal.Num() == 1;
		ExpectSeq({
			[&]() { return I.bIssued && !P.bIssued; },
			[&]() { return I.Id == 20.0 && P.Id == 20.0; },
			[&]() { return I.Rel == P.Rel && I.Hope == P.Hope && I.Hn == P.Hn && I.Inf == P.Inf; },
			[&]() { return FMath::Abs((I.Ten - P.Ten) - 10.0) < 1.0e-9; },
			[&]() { return P.Mult == WantP; },
			[&]() { return I.Mult == WantI; },
			[&]() { return FMath::Abs((P.Mult - I.Mult) - 0.13) < 1.0e-12; },
			[&]() { return I.IssueLines == 1 && I.DefyLines == 1 && P.IssueLines == 0 && P.DefyLines == 1; },
			[&]() { return bJournal; },
			[&]() { return bAlerts; },
			}, FString::Printf(TEXT("CU3 DefyIssue against a plain Defy: the Defy's HEAD effects in both (identity +20, relation %.0f, the same morale swing), tension %.3f against %.3f (+10), the multiplier %.4f against %.4f (exactly -0.13 = -0.10 - 0.3 x 10 / 100, each equal to HEAD's expression in its own order), the issue line once, two journal entries (Defy sub 0 at +20000 and -12000, Issue sub 1 at +10000, one CommandId), and one alert each: D44 alone, and D44 whole with the issue's clause after it (%d and %d alerts)"),
				I.Rel, I.Ten, P.Ten, I.Mult, P.Mult, P.Alerts.Num(), I.Alerts.Num()));
		// The issue roll: the record applies both entries, the market issues at the act's sol s, opens at s + 2, the rate at 1000, Zarya joins;
		// the rate takes no step until the roll closing s + 1.
		const int32 S = I.Journal.Num() > 0 ? I.Journal[0].ClearSol : -1;
		const int64 Ten0 = Sim->GetTensionView();
		RollNext();
		TArray<TPair<FName, int32>> Members;
		Sim->GetBlocMembers(Members);
		const bool bIssueRoll = Sim->GetMarketCheckedSol() == S + 1 && Sim->GetCurrencyIssueSol() == S && Sim->GetCurrencyOpenSol() == S + 2 && Sim->GetRate() == 1000
			&& Sim->GetLastRateStepMcr() == 0 && Members.Num() == 1 && Members[0].Key == NZarya && Members[0].Value == S + 2 && Sim->GetPoliticalJournal().Num() == 0
			&& Sim->GetIdentityView() == 20000 && Sim->GetTensionView() == Ten0 + 600 - 12000 + 10000 && !Sim->IsMarketInMarks();
		const int64 IdBefore = Sim->GetIdentityView(), TeBefore = Sim->GetTensionView();
		RollNext();
		const int64 WantRate = CurSuiteRefStep(1000, 0, 0, IdBefore, TeBefore + 600);
		const bool bFirstStep = Sim->GetRate() == WantRate && WantRate != 1000 && Sim->IsMarketInMarks();
		FString WAgain;
		const bool bAgain = !Sim->CanIssueCurrency(false, WAgain) && WAgain == TEXT("The mark is already issued");
		ExpectSeq({
			[&]() { return S >= 0; },
			[&]() { return bIssueRoll; },
			[&]() { return bFirstStep; },
			[&]() { return bAgain; },
			}, FString::Printf(TEXT("CU3 the issue roll closes the act's sol %d: the market issues the mark there, opens it for sol %d, at 1000 mcr, with no step; Zarya joins from sol %d; the journal is applied and empty (identity +20000, tension %s); the roll closing %d takes the first step (%lld mcr, the reference's); a second issue is refused ('%s')"),
				S, S + 2, S + 2, *Cr(Ten0 + 600 - 12000 + 10000), S + 1, Sim->GetRate(), *WAgain));
		// A stand-alone issue at +20.
		Reset();
		Sim->Debug_SetRequisitionInputs(20.0, 10.0);
		const double T0 = Sim->GetEarthTension();
		Alerts.Reset();
		const bool bIssued = Send(TEXT("Currency"), FName("Issue"));
		const bool bOneAlert = Alerts.Num() == 1 && AlertsStarting(TEXT("A MARTIAN CURRENCY")) == 1;
		ExpectSeq({
			[&]() { return bIssued; },
			[&]() { return bOneAlert; },
			[&]() { return Sim->IsCurrencyIssued(); },
			[&]() { return Sim->GetEarthTension() > T0 + 9.99 && Sim->GetEarthTension() < T0 + 10.1; },
			[&]() { return Sim->GetIdentityAxis() == 20.0; },
			[&]() { return Sim->GetPoliticalJournal().Num() == 1 && Sim->GetPoliticalJournal()[0].Kind == (uint8)EAct::Issue; },
			[&]() { return Sim->GetRequisitionMultiplier() == RefMult(20.0, Sim->GetEarthTension(), true); },
			}, FString::Printf(TEXT("CU3 a stand-alone Issue at identity +20 is accepted with its own one alert: tension %.2f -> %.2f, identity unmoved, one Issue entry, the multiplier %.4f (HEAD's expression less 0.10)"), T0, Sim->GetEarthTension(), Sim->GetRequisitionMultiplier()));
	}

	// --- CU4. Joining: in name order, never FName order (two rivals created in the reverse of their name order against two in the forward
	// order); a stance of -1 never joins; Greed (Zarya's row copied, stance 20, no economy) never joins; a stance of 40 joins at the roll after a
	// Defy brings the RECORD to +40, from that sol + 2; an embargoed member is refused at booking with HEAD's text. ---
	{
		const FName RevB(TEXT("CuRevB")), RevA(TEXT("CuRevA")); // created B first: the FName index order is B, A; the name order is A, B
		const FName FwdC(TEXT("CuFwdC")), FwdD(TEXT("CuFwdD"));
		struct FJoin { TArray<FString> Joins; TArray<TPair<FName, int32>> Members; FCurSuiteBlock Block; };
		const auto JoinRun = [&](FName First, FName Second) -> FJoin
		{
			FJoin J;
			Reset();
			Inject(First, 40.0, TEXT("Ice:50"), TEXT("Ice:10"), TEXT("Ice:700"), TEXT("Ice:4000"), 0);
			Inject(Second, 55.0, TEXT("Ice:20"), TEXT("Ice:5"), TEXT("Ice:300"), TEXT("Ice:3000"), 0);
			Sim->Debug_SyncRivalEconomies();
			Sim->Debug_SetRequisitionInputs(20.0, 0.0);
			Sim->Debug_SyncPoliticalView();
			Send(TEXT("Currency"), FName("Issue"));
			Capture.Clear();
			RollNext();
			for (const FString& L : Capture.Matching(TEXT("Bloc: "))) { const int32 At = L.Find(TEXT("Bloc: ")); J.Joins.Add(L.Mid(At)); }
			Sim->GetBlocMembers(J.Members);
			FString Err;
			Sim->SaveColony(TEXT("cu4pair"), Err);
			TArray<uint8> W;
			FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("cu4pair")));
			CurSuiteParse(W, J.Block);
			RetireAll();
			return J;
		};
		const FJoin Rev = JoinRun(RevB, RevA);
		const FJoin Fwd = JoinRun(FwdC, FwdD);
		const TArray<FString> WantRev = { TEXT("Bloc: CuRevA joins the mark (stance +0, identity +20)"), TEXT("Bloc: CuRevB joins the mark (stance +0, identity +20)"), TEXT("Bloc: Zarya joins the mark (stance +20, identity +20)") };
		const TArray<FString> WantFwd = { TEXT("Bloc: CuFwdC joins the mark (stance +0, identity +20)"), TEXT("Bloc: CuFwdD joins the mark (stance +0, identity +20)"), TEXT("Bloc: Zarya joins the mark (stance +20, identity +20)") };
		const auto Asc = [](const FCurSuiteBlock& B) { bool bAsc = B.bOk && B.Members.Num() == 3; for (int32 i = 1; i < B.Members.Num(); ++i) { bAsc &= B.Members[i - 1].Name.Compare(B.Members[i].Name, ESearchCase::CaseSensitive) < 0; } return bAsc; };
		bool bSameFrom = Rev.Block.Members.Num() == 3 && Fwd.Block.Members.Num() == 3;
		for (int32 i = 0; bSameFrom && i < 3; ++i) { bSameFrom &= Rev.Block.Members[i].From == Fwd.Block.Members[i].From; }
		ExpectSeq({
			[&]() { return Rev.Joins == WantRev; },
			[&]() { return Fwd.Joins == WantFwd; },
			[&]() { return Asc(Rev.Block) && Asc(Fwd.Block); },
			[&]() { return bSameFrom; },
			[&]() { return Rev.Block.PayloadBytes == Fwd.Block.PayloadBytes; },
			[&]() { return Rev.Members.Num() == 3 && Rev.Members[0].Key == RevA && Rev.Members[1].Key == RevB; },
			}, FString::Printf(TEXT("CU4 two stance-0 rivals created in the reverse of their name order (CuRevB, then CuRevA) join in name order at the issue roll ('%s', '%s', '%s'), as the forward pair does; both bloc lists save ascending with the same sols"),
				Rev.Joins.Num() > 0 ? *Rev.Joins[0] : TEXT("?"), Rev.Joins.Num() > 1 ? *Rev.Joins[1] : TEXT("?"), Rev.Joins.Num() > 2 ? *Rev.Joins[2] : TEXT("?")));
	}
	{
		// A stance of -1 and Greed never join; Meridian (40) joins at the roll closing the Defy that brings the record to +40, from that sol + 2.
		// Real era steps from sol 0, so every act's sol and every roll's are the clock's.
		Reset();
		Inject(FName("CuNever"), 40.0, TEXT("Ice:50"), TEXT("Ice:10"), TEXT("Ice:700"), TEXT("Ice:4000"), -1);
		FRHRivalRow Greed = ZaryaRow;
		Greed.DisplayName = TEXT("CuGreed Post");
		Greed.ExportLot = TEXT("Ice:10");
		Greed.ImportLot = TEXT("SpareParts:4");
		DefsSub->Debug_InjectRival(FName("CuGreed"), Greed);
		Injected.AddUnique(FName("CuGreed"));
		Sim->Debug_SyncRivalEconomies();
		const FRHRivalRow* GreedRow = DefsSub->GetRival(FName("CuGreed"));
		// What this checks is the inject: the copy (built as the commandlet builds its Greed) keeps the stance it was given and has no economy.
		const bool bGreedShape = GreedRow && GreedRow->BlocStance == 20 && !Sim->HasRivalEconomy(FName("CuGreed"));
		Demand(65.0);
		Send(TEXT("Solidarity"), FName("DefyIssue"));
		while (Sim->GetCurrencyIssueSol() < 0 && Clock->GetSol() < 3) { EraOnce(); }
		TArray<TPair<FName, int32>> M1;
		Sim->GetBlocMembers(M1);
		const bool bFirst = M1.Num() == 1 && M1[0].Key == NZarya;
		// A second demand and a Defy: the record reaches +40 at the roll closing the Defy's sol c; Meridian joins there, from c + 2.
		Demand(65.0);
		Capture.Clear();
		Send(TEXT("Solidarity"), FName("Defy"));
		const int32 C = Sim->GetPoliticalJournal().Num() > 0 ? Sim->GetPoliticalJournal().Last().ClearSol : -1;
		const int32 MeridianBefore = Sim->GetBlocMemberFromSol(NMeridian);
		while (Sim->GetMarketCheckedSol() <= C && Clock->GetSol() < 10) { EraOnce(); }
		const int32 MeridianFrom = Sim->GetBlocMemberFromSol(NMeridian);
		const bool bLine = Capture.CountLines(TEXT("Bloc: Meridian joins the mark (stance +40, identity +40)")) == 1;
		// At +100 neither the -1 stance nor Greed joins.
		Sim->Debug_SetRequisitionInputs(100.0, Sim->GetEarthTension());
		Sim->Debug_SyncPoliticalView();
		RollNext();
		const bool bNeverJoined = Sim->GetBlocMemberFromSol(FName("CuNever")) < 0 && Sim->GetBlocMemberFromSol(FName("CuGreed")) < 0;
		// An embargoed member: refused at booking with HEAD's text, and still a member.
		Sim->Debug_SetEmbargo(NZarya, 5.0);
		FString WEmb;
		const bool bEmbargo = !Sim->CanPlaceOrder(NZarya, TEXT("Ice:5"), WEmb) && WEmb == TEXT("Zarya Station has embargoed you under Earth pressure - reassure them first") && Sim->GetBlocMemberFromSol(NZarya) >= 0;
		Sim->Debug_ClearPolitics();
		ExpectSeq({
			[&]() { return bGreedShape; },
			[&]() { return bFirst; },
			[&]() { return C >= 0 && MeridianBefore < 0; },
			[&]() { return MeridianFrom == C + 2; },
			[&]() { return bLine; },
			[&]() { return bNeverJoined; },
			[&]() { return bEmbargo; },
			}, FString::Printf(TEXT("CU4 a Greed-shaped rival injected as a copy of Zarya's row (as the commandlet builds its Greed) keeps the stance 20 it was given and has no economy; at the issue roll only Zarya joins; the Defy of sol %d brings the record to +40 and Meridian (stance 40, undiscovered: availability gates booking only) joins at that sol's roll, from sol %d; at +100 a stance of -1 and Greed still stay out; an embargoed member is refused at booking ('%s') and stays a member"), C, MeridianFrom, *WEmb));
		RetireAll();
	}

	// --- CU5. Settlement: a member's order in marks (its reservation in marks, MarkSpend at the clearing, the bloc tally; a net sale's owed marks
	// paid at the return); a non-member's order in credits converted at the rate (its reservation with the rate margin, the charge rounded up
	// with the price and the rate each a full step from the booking's, the owed rounded down and paid at the return). Both purses' invariants
	// hold after every step. ---
	{
		Reset();
		Provision(20000000);
		Inject(FName("CuIn"), 40.0, TEXT("Ice:50;Struct:20"), TEXT("Ice:10;Struct:5"), TEXT("Ice:700;Struct:400"), TEXT("Ice:4000;Struct:3000"), 0);
		Inject(FName("CuOut"), 40.0, TEXT("Ice:50;Struct:20"), TEXT("Ice:10;Struct:5"), TEXT("Ice:700;Struct:400"), TEXT("Ice:4000;Struct:3000"), -1);
		Sim->Debug_SyncRivalEconomies();
		const bool bOpen = IssueOpen();
		Sim->Debug_GrantMarks(5000000);
		bool bInv = Inv();
		// (a) A net buy to Zarya and a net sale to CuIn, both in marks.
		const int64 QIce = Sim->GetMarketPriceMcr(NIce), QStruct = Sim->GetMarketPriceMcr(NStruct);
		const int64 WantR = 300 * Hi(QIce) - 50 * Lo(QStruct);
		const int64 Credit0 = Sim->GetCreditBalanceMcr(), Marks0 = Sim->GetMarkBalance();
		const bool bA = Send(TEXT("MarketOrder"), NZarya, TEXT("Ice:300;Struct:-50"));
		const bool bB = Send(TEXT("MarketOrder"), FName("CuIn"), TEXT("Ice:-200"));
		int64 HeldA = -1, OwedA = -1, HeldB = -1, OwedB = -1;
		const int32 IdA = Sim->GetConvoys().Num() > 0 ? Sim->GetConvoys()[0].Id : -1, IdB = Sim->GetConvoys().Num() > 1 ? Sim->GetConvoys()[1].Id : -1;
		const bool bMarksA = Sim->GetMarkOrder(IdA, HeldA, OwedA), bMarksB = Sim->GetMarkOrder(IdB, HeldB, OwedB);
		bInv &= Inv();
		ExpectSeq({
			[&]() { return bOpen; },
			[&]() { return bA && bB; },
			[&]() { return bMarksA && bMarksB; },
			[&]() { return HeldA == WantR && HeldB == 0; },
			[&]() { return Sim->GetMarkReservedMmk() == WantR; },
			[&]() { return Sim->GetCreditReservedMcr() == 0; },
			[&]() { return Sim->GetSpendableMmk() == Marks0 - WantR; },
			[&]() { return Sim->GetCreditBalanceMcr() == Credit0; },
			[&]() { return bInv; },
			}, FString::Printf(TEXT("CU5 a member's orders book in marks: 'Ice:300;Struct:-50' to Zarya reserves %s marks (300 x Hi(%s) - 50 x Lo(%s): E4's formula read in marks), the net sale to CuIn reserves nothing, no credit is set aside"),
				*Cr(WantR), *Cr(QIce), *Cr(QStruct)));
		RollNext();
		const int64 NetA = 300 * QIce - 50 * QStruct, NetB = -200 * QIce;
		const FRHConvoy* CA = Sim->GetConvoys().FindByPredicate([IdA](const FRHConvoy& X) { return X.Id == IdA; });
		const FRHConvoy* CB = Sim->GetConvoys().FindByPredicate([IdB](const FRHConvoy& X) { return X.Id == IdB; });
		int64 TSold = 0, TBought = 0;
		Sim->GetBlocTally(1, TSold, TBought);
		const FRHMarketClearing* RecA = Sim->GetMarketClearings().FindByPredicate([IdA](const FRHMarketClearing& R) { return R.ConvoyId == IdA; });
		bInv &= Inv();
		ExpectSeq({
			[&]() { return CA && CB && CA->bCleared && CB->bCleared; },
			[&]() { return Sim->GetMarkBalance() == Marks0 - NetA; },
			[&]() { return CB->OwedMmk == -NetB && CB->OwedMcr == 0 && CA->OwedMmk == 0; },
			[&]() { return Sim->GetMarkReservedMmk() == 0; },
			[&]() { return RecA && RecA->Currency == 1 && RecA->SettledMcr == NetA && RecA->PaidMcr == NetA && RecA->ReleasedMcr == WantR; },
			[&]() { return TSold == 50 * QStruct + 200 * QIce && TBought == 300 * QIce; },
			[&]() { return Sim->GetCreditBalanceMcr() == Credit0; },
			[&]() { return bInv; },
			}, FString::Printf(TEXT("CU5 the clearing spends %s marks through MarkSpend for the buy (the net, no conversion), the sale owes %s marks to carry home, the reservation is released, and the bloc tally's closing sol holds %s marks sold and %s bought (both orders: they are mark orders)"),
				*Cr(NetA), *Cr(-NetB), *Cr(TSold), *Cr(TBought)));
		const int64 MarksBeforeReturn = Sim->GetMarkBalance(), SalesBefore = Sim->GetMarketSalesMcr();
		int32 Steps = 0;
		while (Sim->GetConvoys().ContainsByPredicate([IdB](const FRHConvoy& X) { return X.Id == IdB; }) && Steps++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
		bInv &= Inv();
		ExpectSeq({
			[&]() { return !Sim->GetConvoys().ContainsByPredicate([IdB](const FRHConvoy& X) { return X.Id == IdB; }); },
			[&]() { return Sim->GetMarkBalance() == MarksBeforeReturn + (-NetB); },
			[&]() { return Sim->GetMarketSalesMcr() == SalesBefore; },
			[&]() { return bInv; },
			}, FString::Printf(TEXT("CU5 the sale to CuIn comes home (%d era steps) and its %s owed marks are paid through MarkPay; no credit and no market sales move"), Steps, *Cr(-NetB)));
		// (b) A non-member's order, in credits converted at the rate: the reservation, then the price and the rate each a full step from the
		// booking's before the clearing.
		while (Sim->GetConvoys().Num() > 0 && Steps++ < 30 * (int32)(SolLen / EraDt)) { EraOnce(); }
		Ahead();
		Sim->Debug_SetMarketPrice(NIce, 3001);
		Sim->Debug_SetMarketPrice(NStruct, 4999);
		Sim->Debug_SetRate(1001);
		const int64 HiR = 1001 * 10200 / 10000 + 1, LoR = 1001 * 9800 / 10000 - 1;
		const int64 WantConv = CeilDiv(200 * Hi(3001) * HiR, 1000) - (37 * Lo(4999) * LoR) / 1000;
		const int64 Credit1 = Sim->GetCreditBalanceMcr();
		const bool bC = Send(TEXT("MarketOrder"), FName("CuOut"), TEXT("Ice:200;Struct:-37"));
		const FRHConvoy* CC = Sim->GetConvoys().Num() > 0 ? &Sim->GetConvoys().Last() : nullptr;
		const int32 IdC = CC ? CC->Id : -1;
		const bool bConvBooked = bC && CC && !CC->bMarks && CC->ReservedMcr == WantConv && Sim->GetCreditReservedMcr() == WantConv && Sim->GetMarkReservedMmk() == 0
			&& Sim->GetConvertedReservationMcr(FName("CuOut"), CC->Lines, 1001) == WantConv;
		Sim->Debug_SetMarketPrice(NIce, 3001 * 10500 / 10000);
		Sim->Debug_SetMarketPrice(NStruct, 4999 * 9500 / 10000);
		Sim->Debug_SetRate(1001 * 10200 / 10000);
		RollNext();
		const int64 Net = 200 * (3001 * 10500 / 10000) - 37 * (4999 * 9500 / 10000);
		const int64 Rate = 1001 * 10200 / 10000;
		const int64 WantCharge = CeilDiv(Net * Rate, 1000);
		const FRHMarketClearing* RecC = Sim->GetMarketClearings().FindByPredicate([IdC](const FRHMarketClearing& R) { return R.ConvoyId == IdC; });
		int64 CSold = -1, CBought = -1;
		const bool bCTally = Sim->GetBlocTally(1, CSold, CBought) && CSold == 0 && CBought == 0;
		bInv &= Inv();
		ExpectSeq({
			[&]() { return bConvBooked; },
			[&]() { return bCTally; },
			[&]() { return (Net * Rate) % 1000 != 0; },
			[&]() { return RecC && RecC->Currency == 2 && RecC->RateMcr == Rate && RecC->NetMcr == Net && RecC->SettledMcr == WantCharge && RecC->PaidMcr == WantCharge && RecC->WrittenOffMcr == 0; },
			[&]() { return Sim->GetCreditBalanceMcr() == Credit1 - WantCharge; },
			[&]() { return WantCharge <= WantConv; },
			[&]() { return Sim->GetCreditReservedMcr() == 0; },
			[&]() { return Sim->GetMarkBalance() == MarksBeforeReturn + (-NetB); },
			[&]() { return bInv; },
			}, FString::Printf(TEXT("CU5 a non-member's order converts: 'Ice:200;Struct:-37' at 3.001 / 4.999 marks and 1.001 cr a mark reserves %s cr (ceil(sum buy x Hi(q) x HiR(r) / 1000) - floor(sum sell x Lo(q) x LoR(r) / 1000)); with Ice a full step dearer, Struct a full step cheaper and the rate a full step up (%lld mcr) the net of %s marks is charged %s cr, rounded UP from %lld.%03lld, inside the reservation; the bloc tally of its clearing sol stays empty (a credit order, not a mark order)"),
				*Cr(WantConv), Rate, *Cr(Net), *Cr(WantCharge), (Net * Rate) / 1000, (Net * Rate) % 1000));
		// The converted owed: a net sale to CuOut, owed floor(-Net x r / 1000) and paid at the return through CreditPay (market sales).
		Steps = 0;
		while (Sim->GetConvoys().Num() > 0 && Steps++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
		Ahead();
		Sim->Debug_SetMarketPrice(NStruct, 4747);
		Sim->Debug_SetRate(1019);
		const bool bD = Send(TEXT("MarketOrder"), FName("CuOut"), TEXT("Struct:-100"));
		Sim->Debug_SetRate(1019);
		RollNext();
		const int64 WantOwed = 100 * 4747 * 1019 / 1000;
		const FRHConvoy* CD = Sim->GetConvoys().Num() > 0 ? &Sim->GetConvoys().Last() : nullptr;
		int64 DSold = -1, DBought = -1;
		const bool bOwed = bD && CD && CD->OwedMcr == WantOwed && CD->OwedMmk == 0 && (100 * 4747 * 1019) % 1000 != 0
			&& Sim->GetBlocTally(1, DSold, DBought) && DSold == 0 && DBought == 0;
		const int64 Credit2 = Sim->GetCreditBalanceMcr(), Sales2 = Sim->GetMarketSalesMcr();
		Steps = 0;
		while (Sim->GetConvoys().Num() > 0 && Steps++ < 10 * (int32)(SolLen / EraDt)) { EraOnce(); }
		bInv &= Inv();
		ExpectSeq({
			[&]() { return bOwed; },
			[&]() { return Sim->GetCreditBalanceMcr() == Credit2 + WantOwed; },
			[&]() { return Sim->GetMarketSalesMcr() == Sales2 + WantOwed; },
			[&]() { return bInv; },
			}, FString::Printf(TEXT("CU5 a converted sale of 100 kg of Struct at 4.747 marks and 1.019 cr a mark owes %s cr (rounded DOWN from 483,719.3), paid at its return through CreditPay into the market sales (the bloc tally untouched); both purses' invariants held after every step"), *Cr(WantOwed)));
		RetireAll();
	}

	// --- CU6. The desk: both directions at a fixed rate (the payouts, one floor each, the spread, the reservation and its release); the cap
	// each way, check 3 (no member to trade with) and the spendable refusals; a pure desk order leaves the next rate step unchanged; a one-sol
	// round trip at the steepest climb returns at most 0.960 of its stake. (The refusal while withdrawing is CU9's.) The rate is 1.036: there
	// the 300 cr's ONE floor (trap 11) is 280.888 marks and the spec's left-to-right form, floored twice, 280.887 (a marks-to-credits payout
	// cannot tell the two apart: whole marks times the rate are whole milli-credits before the spread). ---
	{
		Reset();
		Provision(10000000);
		const bool bOpen = IssueOpen();
		Sim->Debug_SetRate(1036);
		const int64 Credit0 = Sim->GetCreditBalanceMcr();
		const bool bTo = Send(TEXT("Exchange"), FName("ToMarks"), FString(), 300.0);
		TArray<FRHDeskOrder> Desk;
		Sim->GetDeskOrders(Desk);
		const int32 Sol = Desk.Num() > 0 ? Desk[0].ClearSol : -1;
		int64 BookedM = 0, BookedC = 0;
		Sim->GetDeskBooked(Sol, BookedM, BookedC);
		FString WCap, WSpend, WCapBack, WNoMember;
		const bool bCap = !Sim->CanExchange(EDir::ToMarks, 201, Sol, WCap) && WCap == TEXT("The desk takes at most 500 credits a sol (300 booked)") && Sim->CanExchange(EDir::ToMarks, 200, Sol, WSpend);
		const bool bCapSent = !Send(TEXT("Exchange"), FName("ToMarks"), FString(), 201.0) && LastReject() == TEXT("The desk takes at most 500 credits a sol (300 booked)");
		const bool bWhole = !Send(TEXT("Exchange"), FName("ToMarks"), FString(), 2.5) && LastReject() == TEXT("An exchange is a whole number of credits, at least 1");
		// The cap the other way (check 4 answers before the purse: no marks are held yet), and check 3: the one member embargoed, none to trade with.
		const bool bCapBack = !Sim->CanExchange(EDir::ToCredits, 501, Sol, WCapBack) && WCapBack == TEXT("The desk takes at most 500 marks a sol (0 booked)");
		Sim->Debug_SetEmbargo(NZarya, 5.0);
		const bool bNoMember = !Sim->CanExchange(EDir::ToMarks, 10, Sol, WNoMember) && WNoMember == TEXT("No bloc member can trade with you");
		Sim->Debug_ClearPolitics();
		ExpectSeq({
			[&]() { return bOpen && bTo; },
			[&]() { return Desk.Num() == 1 && Desk[0].Amount == 300000 && Desk[0].Dir == (uint8)EDir::ToMarks && Sol == Sim->GetMarketCheckedSol(); },
			[&]() { return Sim->GetCreditReservedMcr() == 300000 && Sim->GetSpendableMcr() == Credit0 - 300000; },
			[&]() { return BookedM == 300 && BookedC == 0; },
			[&]() { return bCap && bCapSent && bWhole; },
			[&]() { return bCapBack; },
			[&]() { return bNoMember; },
			[&]() { return Inv(); },
			}, FString::Printf(TEXT("CU6 an exchange of 300 credits to marks sets exactly 300 cr aside to clear at the end of sol %d; a second of 201 is refused ('%s'), 200 would fit, half a credit is refused; 501 marks the other way '%s'; with the one member embargoed '%s'"),
				Sol, *WCap, *WCapBack, *WNoMember));
		Sim->Debug_SetRate(1036);
		RollNext();
		const int64 WantMarks = 300000LL * 1000 * 9700 / (1036LL * 10000);
		const int64 TwoFloors = (300000LL * 1000 / 1036) * 9700 / 10000; // the spec's left-to-right form, which floors after the division too
		const FRHDeskClearing* RecTo = Sim->GetDeskClearings().Num() > 0 ? &Sim->GetDeskClearings().Last() : nullptr;
		ExpectSeq({
			[&]() { return WantMarks == 280888 && TwoFloors == 280887; },
			[&]() { return Sim->GetMarkBalance() == 280888; },
			[&]() { return Sim->GetCreditBalanceMcr() == Credit0 - 300000; },
			[&]() { return Sim->GetCreditReservedMcr() == 0; },
			[&]() { return RecTo && RecTo->Payout == 280888 && RecTo->Paid == 300000 && RecTo->RateMcr == 1036; },
			[&]() { return Inv(); },
			}, TEXT("CU6 at 1.036 cr a mark the 300 cr clear for 280.888 marks (floor(300,000 x 1000 x 9700 / (1036 x 10000)): ONE floor - floored twice it would be 280.887 - the 3% spread kept), the 300 cr leave through CreditSpend and the reservation is released"));
		const bool bBack = Send(TEXT("Exchange"), FName("ToCredits"), FString(), 200.0);
		const bool bReservedM = Sim->GetMarkReservedMmk() == 200000 && Sim->GetSpendableMmk() == 80888;
		FString WNot;
		const bool bNotEnough = !Sim->CanExchange(EDir::ToCredits, 81, Sim->GetBookingSolNow(), WNot) && WNot == TEXT("Not enough marks spendable (80.888)");
		const int64 Credit1 = Sim->GetCreditBalanceMcr(), Desk1 = Sim->GetDeskCreditMcr();
		Sim->Debug_SetRate(1036);
		RollNext();
		const int64 WantCr = 200000LL * 1036 * 9700 / (1000LL * 10000);
		ExpectSeq({
			[&]() { return bBack && bReservedM; },
			[&]() { return bNotEnough; },
			[&]() { return WantCr == 200984; },
			[&]() { return Sim->GetCreditBalanceMcr() == Credit1 + 200984; },
			[&]() { return Sim->GetDeskCreditMcr() == Desk1 + 200984; },
			[&]() { return Sim->GetMarkBalance() == 80888 && Sim->GetMarkReservedMmk() == 0; },
			[&]() { return Inv(); },
			}, FString::Printf(TEXT("CU6 200 marks back set exactly 200 marks aside ('%s' for 81 more) and clear for 200.984 cr (floor(200,000 x 1036 x 9700 / (1000 x 10000))), paid through CreditPay into the desk's lifetime credit"), *WNot));
		// A pure desk order leaves the next rate step unchanged: twins from one save, one with an exchange clearing at the roll and one without.
		Sim->Debug_SetRate(1100);
		SyncClock();
		Expect(Sim->SaveColony(TEXT("cu6pure"), SaveErr), TEXT("CU6 the pure-desk twins' start is saved"));
		LoadSlot(TEXT("cu6pure"));
		EraOnce();
		RollNext();
		const int64 RateA = Sim->GetRate();
		LoadSlot(TEXT("cu6pure"));
		const int32 Cleared0 = Sim->GetDeskClearings().Num();
		const bool bSent = Send(TEXT("Exchange"), FName("ToMarks"), FString(), 300.0);
		RollNext();
		const int64 RateB = Sim->GetRate();
		int64 S1 = 0, B1 = 0;
		Sim->GetBlocTally(1, S1, B1);
		ExpectSeq({
			[&]() { return bSent && Sim->GetDeskClearings().Num() == Cleared0 + 1; },
			[&]() { return RateA == RateB; },
			[&]() { return RateA != 1100; },
			[&]() { return S1 == 0 && B1 == 0; },
			}, FString::Printf(TEXT("CU6 a pure desk order leaves the rate step unchanged: %lld mcr with an exchange of 300 credits clearing at the roll and %lld without (the bloc tally holds no desk flow)"), RateB, RateA));
		// A one-sol round trip at the steepest climb (+2%: the record at +100, the bloc tally a full surplus).
		LoadSlot(TEXT("cu6pure"));
		Sim->Debug_SetRequisitionInputs(100.0, 0.0);
		Sim->Debug_SyncPoliticalView();
		Sim->Debug_SetBlocTally(1000000000, 0);
		Sim->Debug_SetRate(1000);
		const int64 Credit2 = Sim->GetCreditBalanceMcr(), Marks2 = Sim->GetMarkBalance();
		const bool bOut = Send(TEXT("Exchange"), FName("ToMarks"), FString(), 500.0);
		RollNext();
		const int64 Got = Sim->GetMarkBalance() - Marks2;
		const int64 R1 = Sim->GetRate();
		const bool bIn = Send(TEXT("Exchange"), FName("ToCredits"), FString(), (double)(Got / 1000));
		RollNext();
		const int64 Back = Sim->GetCreditBalanceMcr() - (Credit2 - 500000);
		const int64 WantBack = (Got / 1000) * 1000 * R1 * 9700 / (1000LL * 10000);
		ExpectSeq({
			[&]() { return bOut && bIn; },
			[&]() { return Got == 485000; },
			[&]() { return R1 == 1020; },
			[&]() { return Back == WantBack && Back == 479859; },
			[&]() { return Back * 1000 <= 960 * 500000; },
			[&]() { return Inv(); },
			}, FString::Printf(TEXT("CU6 a one-sol round trip at the steepest climb: 500 cr buy %s marks at 1.000, the rate steps +2%% to %s, the %lld whole marks come back as %s cr: %.4f of the stake (at most 0.960: 0.97 x 0.97 x 1.02)"),
				*Cr(Got), *Cr(R1), Got / 1000, *Cr(Back), (double)Back / 500000.0));
	}

	// --- CU7. The political record: 30 sols with no act drift it 600 a sol and keep it within 0.61 of the live tension; an act due 10 s after a
	// boundary changes it at the roll closing its OWN sol (the era band runs it before the roll closing the sol before); a Comply from tension 5
	// clamps it at 0; a 12-sol jump adds 12 drifts; with no SliceActive row there is no drift; and the identity record is the live value the act
	// left, even when a harness write came between the sync and the act. ---
	{
		Reset();
		const int64 T0 = Sim->GetTensionView();
		const int32 M0 = Sim->GetMarketCheckedSol();
		int32 Rolls = 0, LastChecked = M0;
		bool bExact = true;
		double Worst = 0.0;
		while (Clock->GetSimSecondsTotal() < 30.0 * SolLen - 1.0e-6)
		{
			EraOnce();
			if (Sim->GetMarketCheckedSol() != LastChecked)
			{
				LastChecked = Sim->GetMarketCheckedSol();
				++Rolls;
				bExact &= Sim->GetTensionView() == T0 + 600 * (LastChecked - M0);
				Worst = FMath::Max(Worst, FMath::Abs(Sim->GetEarthTension() - (double)Sim->GetTensionView() / 1000.0));
			}
		}
		ExpectSeq({
			[&]() { return Rolls == 30; },
			[&]() { return bExact; },
			[&]() { return Worst <= 0.61; },
			[&]() { return Sim->GetPoliticalJournal().Num() == 0; },
			}, FString::Printf(TEXT("CU7 30 sols with no act: at each of %d rolls the tension record is the start + 600 a sol (%s after 30), and the live tension is within %.4f of it (at most 0.61)"), Rolls, *Cr(Sim->GetTensionView()), Worst));
	}
	{
		// The boundary act: era steps on a grid 25 s off the minute, a Defy enqueued at 3565 s (ExecuteAt 3610, 10 s after the sol-3 boundary)
		// runs on the step ending 3625 BEFORE that step's roll closing sol 2; the record must not move there, and moves at the roll closing 3.
		Reset();
		Sim->Debug_SetRequisitionInputs(0.0, 65.0);
		Clock->Debug_AdvanceSimSeconds(25.0);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->EraStep(25.f);
		while (Clock->GetSimSecondsTotal() < 3565.0 - 1.0e-6) { EraOnce(); }
		const bool bOnGrid = FMath::Abs(Clock->GetSimSecondsTotal() - 3565.0) < 1.0e-6 && Sim->IsEarthDemandPending() && Sim->GetMarketCheckedSol() == 2;
		FRHCommand C; C.Verb = FName("Solidarity"); C.Target = FName("Defy");
		Sim->EnqueueCommand(C);
		EraOnce();
		const bool bRanBeforeRoll = Sim->GetIdentityAxis() == 20.0 && Sim->GetMarketCheckedSol() == 3 && Sim->GetIdentityView() == 0 && Sim->GetPoliticalJournal().Num() == 1
			&& Sim->GetPoliticalJournal()[0].ClearSol == 3;
		while (Sim->GetMarketCheckedSol() < 4 && Clock->GetSol() < 6) { EraOnce(); }
		ExpectSeq({
			[&]() { return bOnGrid; },
			[&]() { return bRanBeforeRoll; },
			[&]() { return Sim->GetIdentityView() == 20000 && Sim->GetPoliticalJournal().Num() == 0; },
			}, TEXT("CU7 a Defy due 10 s after the sol-3 boundary runs on the era step that crosses it, before that step's roll closing sol 2: the record is still +0 after that roll (the entry is keyed to sol 3) and is +20000 after the roll closing sol 3"));
	}
	{
		// A Comply from tension 5 clamps the record at 0 (the drift first, then the act: 5000 + 600 - 45000).
		Reset();
		Demand(65.0);
		Sim->Debug_SetRequisitionInputs(Sim->GetIdentityAxis(), 5.0);
		Sim->Debug_SyncPoliticalView();
		const bool bComply = Send(TEXT("Solidarity"), FName("Comply"));
		const bool bLiveZero = Sim->GetEarthTension() < 0.1;
		RollNext();
		const int64 AfterComply = Sim->GetTensionView();
		// A 12-sol jump adds 12 drifts (the record walks the sols the market's 8 passes skip).
		const int64 T1 = Sim->GetTensionView();
		Sim->Debug_RollMarket(Sim->GetMarketCheckedSol() + 12);
		const int64 Jump = Sim->GetTensionView() - T1;
		// No SliceActive row, no drift.
		FRHRivalRow Quiet = ZaryaRow;
		Quiet.SliceActive = false;
		DefsSub->Debug_InjectRival(NZarya, Quiet);
		const int64 T2 = Sim->GetTensionView();
		RollNext();
		RollNext();
		RollNext();
		const int64 Still = Sim->GetTensionView() - T2;
		DefsSub->Debug_InjectRival(NZarya, ZaryaRow);
		Sim->Debug_SyncRivalEconomies();
		ExpectSeq({
			[&]() { return bComply && bLiveZero; },
			[&]() { return AfterComply == 0; },
			[&]() { return Jump == 12 * 600; },
			[&]() { return Still == 0; },
			}, FString::Printf(TEXT("CU7 a Comply from tension 5 leaves the record at %s (clamped at 0); a 12-sol jump adds %s (12 drifts); with no SliceActive row three rolls add %s"), *Cr(AfterComply), *Cr(Jump), *Cr(Still)));
	}
	{
		// A long jump with an act due inside it (a harness jump only: in play a roll closes every sol): the last pass walks each sol it skips -
		// that sol's drift, then its entries - so the record is the one closing those sols one by one leaves, clamps included. The file is doctored
		// (resealed, the frame rebuilt for its length) to hold the record at 99500 and one Comply entry keyed to the second sol of the last pass
		// of a 12-sol jump (passes close M .. M+6 one at a time, the last walks M+7 .. M+11): 100000 (clamped), -45000 in sol M+8, then three
		// drifts: 56800. All the skipped sols' drifts added before the entry (one clamp at the top) would leave 55000.
		Reset();
		Sim->SaveColony(TEXT("cu7jump"), SaveErr);
		TArray<uint8> W;
		FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("cu7jump")));
		FCurSuiteBlock B;
		const int32 M = Sim->GetMarketCheckedSol();
		bool bBuilt = CurSuiteParse(W, B) && B.Journal.Num() == 0;
		if (bBuilt)
		{
			TArray<uint8> P(W.GetData() + B.PayloadAt, B.PayloadBytes);
			const int64 Top = 99500;
			FMemory::Memcpy(P.GetData() + (B.TensionViewAt - B.PayloadAt), &Top, 8);
			const int32 CountAt = B.TensionViewAt + 8 - B.PayloadAt;
			const int32 One = 1, ClearSol = M + 8, CommandId = 7, Sub = 0;
			const double ExecuteAt = ((double)M + 8.5) * SolLen;
			const uint8 Kind = (uint8)EAct::Comply, HasIdentity = 1;
			const int64 IdentityMilli = 0, Delta = -45000;
			TArray<uint8> NewP(P.GetData(), CountAt);
			NewP.Append((const uint8*)&One, 4);
			NewP.Append((const uint8*)&ClearSol, 4);
			NewP.Append((const uint8*)&ExecuteAt, 8);
			NewP.Append((const uint8*)&CommandId, 4);
			NewP.Append((const uint8*)&Sub, 4);
			NewP.Append(&Kind, 1);
			NewP.Append(&HasIdentity, 1);
			NewP.Append((const uint8*)&IdentityMilli, 8);
			NewP.Append((const uint8*)&Delta, 8);
			NewP.Append(P.GetData() + CountAt + 4, P.Num() - CountAt - 4);
			bBuilt = CurSuiteReframe(W, RHSaveFrame::CurrencyTag, NewP) && FFileHelper::SaveArrayToFile(W, *SaveFilePath(TEXT("cu7jump2")));
		}
		Capture.Clear();
		const bool bLoaded = bBuilt && LoadSlot(TEXT("cu7jump2"));
		const bool bHeld = bLoaded && Capture.NumWarnings() == 0 && Sim->GetPoliticalJournal().Num() == 1 && Sim->GetPoliticalJournal()[0].ClearSol == M + 8 && Sim->GetTensionView() == 99500;
		Sim->Debug_RollMarket(M + 12);
		ExpectSeq({
			[&]() { return bHeld; },
			[&]() { return Sim->GetMarketCheckedSol() == M + 12 && Sim->GetPoliticalJournal().Num() == 0; },
			[&]() { return Sim->GetTensionView() == 56800; },
			}, FString::Printf(TEXT("CU7 a 12-sol jump with a Comply due in its last pass's second sol: the record reads %lld (100000 clamped, -45000 in sol %d, then three drifts: 56800; every drift first would give 55000)"), Sim->GetTensionView(), M + 8));
	}
	{
		// The identity record is the value the act left: (1) the brief's order - a harness shift, a sync, a Defy; (2) a harness write between the
		// sync and the Defy (mutation 13: built from nominal deltas, the record would read +20 against the live +30).
		Reset();
		Demand(65.0);
		Sim->Debug_ShiftIdentity(10.0);
		Sim->Debug_SyncPoliticalView();
		Send(TEXT("Solidarity"), FName("Defy"));
		RollNext();
		const int64 View1 = Sim->GetIdentityView();
		const double Live1 = Sim->GetIdentityAxis();
		Reset();
		Demand(65.0);
		Sim->Debug_SyncPoliticalView();
		Sim->Debug_ShiftIdentity(10.0);
		Send(TEXT("Solidarity"), FName("Defy"));
		RollNext();
		const int64 View2 = Sim->GetIdentityView();
		const double Live2 = Sim->GetIdentityAxis();
		ExpectSeq({
			[&]() { return Live1 == 30.0 && View1 == 30000; },
			[&]() { return Live2 == 30.0 && View2 == 30000; },
			}, FString::Printf(TEXT("CU7 after a +10 harness shift and a Defy the identity record equals the live value: %s against %+.0f with the sync after the shift, %s against %+.0f with the shift between the sync and the Defy"), *Cr(View1), Live1, *Cr(View2), Live2));
	}
	{
		// Each act's own entry against hand-written values (binding design 8). A Comply from a record of 65000 at identity +10: the roll adds the
		// drift and the Comply's -45000 (65000 + 600 - 45000 = 20600) and sets the identity record to the identity it left, -10. A Pacify from
		// 50000 after a harness shift the record does not see (+10): -20000 (50000 + 600 - 20000 = 30600) and no identity (the record stays 0).
		Reset();
		Demand(65.0);
		Sim->Debug_SetRequisitionInputs(10.0, 65.0);
		Sim->Debug_SyncPoliticalView();
		const bool bComply = Send(TEXT("Solidarity"), FName("Comply"));
		const TArray<FRHPoliticalEntry> CJ = Sim->GetPoliticalJournal();
		const double LiveId = Sim->GetIdentityAxis();
		RollNext();
		const int64 CTen = Sim->GetTensionView(), CId = Sim->GetIdentityView();
		ExpectSeq({
			[&]() { return bComply && LiveId == -10.0; },
			[&]() { return CJ.Num() == 1 && CJ[0].Kind == (uint8)EAct::Comply && CJ[0].Sub == 0 && CJ[0].bHasIdentity && CJ[0].IdentityMilli == -10000 && CJ[0].TensionDeltaMilli == -45000; },
			[&]() { return CTen == 20600; },
			[&]() { return CId == -10000 && CId == (int64)FMath::RoundToDouble(LiveId * 1000.0); },
			[&]() { return Sim->GetPoliticalJournal().Num() == 0; },
			}, FString::Printf(TEXT("CU7 a Comply from a record of (identity 10000, tension 65000): its entry sets the identity to %lld and adds -45000; after the roll the tension record is %lld (65000 + 600 - 45000 = 20600) and the identity record %lld (the identity the Comply left, -10, x 1000)"),
				CJ.Num() > 0 ? CJ[0].IdentityMilli : 0LL, CTen, CId));
		Reset();
		Sim->Debug_AddInfluence(40.0);
		Sim->Debug_SetEmbargo(NZarya, 5.0);
		Sim->Debug_SetRequisitionInputs(0.0, 50.0);
		Sim->Debug_SyncPoliticalView();
		Sim->Debug_ShiftIdentity(10.0);
		const bool bPacify = Send(TEXT("Pacify"), NZarya);
		const TArray<FRHPoliticalEntry> PJ = Sim->GetPoliticalJournal();
		RollNext();
		const int64 PTen = Sim->GetTensionView(), PId = Sim->GetIdentityView();
		ExpectSeq({
			[&]() { return bPacify && !Sim->IsEmbargoed(NZarya) && Sim->GetIdentityAxis() == 10.0; },
			[&]() { return PJ.Num() == 1 && PJ[0].Kind == (uint8)EAct::Pacify && !PJ[0].bHasIdentity && PJ[0].TensionDeltaMilli == -20000; },
			[&]() { return PTen == 30600; },
			[&]() { return PId == 0; },
			}, FString::Printf(TEXT("CU7 a Pacify from a record of 50000: its entry adds -20000 and sets no identity; after the roll the tension record is %lld (50000 + 600 - 20000 = 30600) and the identity record stays %lld against the live +10"),
				PTen, PId));
	}
	{
		// The clamp after EACH entry (binding design 8): a sol whose entries cross 0 part-way. From a record of 5000: the drift (5600), a Pacify
		// (-20000: 0, clamped), then a stand-alone Issue (+10000) gives 10000; one clamp at the end would give max(0, 5600 - 20000 + 10000) = 0.
		Reset();
		Sim->Debug_AddInfluence(40.0);
		Sim->Debug_SetEmbargo(NZarya, 5.0);
		Sim->Debug_SetRequisitionInputs(20.0, 5.0);
		Sim->Debug_SyncPoliticalView();
		const bool bPacify = Send(TEXT("Pacify"), NZarya);
		const bool bIssue = Send(TEXT("Currency"), FName("Issue"));
		const TArray<FRHPoliticalEntry> J = Sim->GetPoliticalJournal();
		RollNext();
		ExpectSeq({
			[&]() { return bPacify && bIssue; },
			[&]() { return J.Num() == 2 && J[0].Kind == (uint8)EAct::Pacify && J[1].Kind == (uint8)EAct::Issue && J[0].ClearSol == J[1].ClearSol; },
			[&]() { return Sim->GetTensionView() == 10000; },
			}, FString::Printf(TEXT("CU7 the tension record is clamped after each entry: from 5000, the drift, a Pacify and an Issue in one sol leave %lld (5600 - 20000 clamped to 0, then + 10000), not 0 (one clamp at the end)"), Sim->GetTensionView()));
	}

	// --- CU9. The withdrawal, in the ERA band (its reopen edge in both bands): a Comply ends the penalty at once and closes the market; a
	// discovered member whose route is still open is refused; the desk and a new issue are refused; at the roll (after the clearing) every mark is
	// redeemed with one floor at the rate in force, no spread, into the redeemed total; the convoy on the road pays nothing more; every slot is
	// re-expressed and held in its band; the bloc dissolves; a later issue starts with an empty purse. Then: the withdrawal waits for an order
	// still uncleared; an issue and a withdrawal at one roll use 1000; and the reopen edge. ---
	{
		Reset();
		Provision(20000000);
		Sim->Debug_AddInfluence(40.0);
		Sim->Debug_DiscoverRival(NMeridian);
		const bool bOpen = IssueOpen(40.0);
		TArray<TPair<FName, int32>> Members;
		Sim->GetBlocMembers(Members);
		Sim->Debug_GrantMarks(1000000);
		const bool bSold = Send(TEXT("MarketOrder"), NZarya, TEXT("Struct:-100"));
		const int32 IdZ = Sim->GetConvoys().Num() > 0 ? Sim->GetConvoys()[0].Id : -1;
		RollNext();
		int64 HeldZ = 0, OwedZ = 0;
		const bool bOwes = Sim->GetMarkOrder(IdZ, HeldZ, OwedZ) && OwedZ > 0;
		Demand(65.0);
		const double MultIssued = Sim->GetRequisitionMultiplier();
		const bool bPenaltyOn = Sim->IsCurrencyIssued() && MultIssued == RefMult(Sim->GetIdentityAxis(), Sim->GetEarthTension(), true);
		Alerts.Reset();
		const bool bComply = Send(TEXT("Solidarity"), FName("Comply"));
		const bool bD43Alone = Alerts.Num() == 1 && AlertsStarting(TEXT("YOU COMPLIED")) == 1 && !Alerts[0].Contains(TEXT("mark"));
		const bool bPenaltyGone = !Sim->IsCurrencyIssued() && Sim->IsCurrencyWithdrawPending() && Sim->GetRequisitionMultiplier() == RefMult(Sim->GetIdentityAxis(), Sim->GetEarthTension(), false);
		FString WOrder, WDesk, WIssue;
		const bool bClosed = !Sim->CanPlaceOrder(NMeridian, TEXT("SpareParts:1"), WOrder) && WOrder == TEXT("The market is closed while the mark is withdrawn") && !Sim->IsRouteClosed(NMeridian) && Sim->IsRivalAvailable(NMeridian);
		const bool bClosedSent = !Send(TEXT("MarketOrder"), NMeridian, TEXT("SpareParts:1")) && LastReject() == TEXT("The market is closed while the mark is withdrawn");
		const bool bDeskShut = !Sim->CanExchange(EDir::ToMarks, 10, Sim->GetBookingSolNow(), WDesk) && WDesk == TEXT("The market is closed while the mark is withdrawn");
		const bool bNoIssue = !Sim->CanIssueCurrency(false, WIssue) && WIssue == TEXT("The mark is being withdrawn");
		ExpectSeq({
			[&]() { return bOpen && Members.Num() == 2; },
			[&]() { return bSold && bOwes; },
			[&]() { return bPenaltyOn && bComply; },
			[&]() { return bD43Alone; },
			[&]() { return bPenaltyGone; },
			[&]() { return bClosed && bClosedSent; },
			[&]() { return bDeskShut && bNoIssue; },
			}, FString::Printf(TEXT("CU9 era band: a Comply while the mark exists raises one alert, Gate D's D43 (the withdrawal's start is a log line), ends Earth's penalty at once (the multiplier is HEAD's unissued value again, %.4f) and closes the market: an order to Meridian (a discovered member, its route open) is refused ('%s'), the desk says '%s', a new issue '%s'"),
				Sim->GetRequisitionMultiplier(), *WOrder, *WDesk, *WIssue));
		// The withdrawal roll, with the market off (no reprice at this roll, so the re-expression is read against the prices before it; the
		// live market's order is the next fixture's), the rate at 3.499 and Ice near its ceiling: Ice clamps to 4 V, the others scale. The
		// redemption is ONE floor over the balance and the owed marks together: the purse is topped up by the fewest milli-marks that make the two
		// parts' remainders at 3.499 pass a whole milli-credit together, so a floor of each part would come out one milli-credit short.
		Sim->Debug_SetMarketEnabled(false);
		const int64 R = 3499;
		Sim->Debug_SetRate(R);
		Sim->Debug_SetMarketPrice(NIce, 11000);
		TArray<TPair<FName, int64>> Before;
		for (int32 i = 0; i < Sim->GetLedgerSlotCount(); ++i)
		{
			const FName Res = Sim->GetLedgerSlotName(i);
			Before.Emplace(Res, Sim->GetMarketPriceMcr(Res));
		}
		const int64 RemOwed = OwedZ * R % 1000;
		int64 TopUp = 0;
		while (TopUp < 1000 && (Sim->GetMarkBalance() + TopUp) * R % 1000 + RemOwed < 1000) { ++TopUp; }
		Sim->Debug_GrantMarks(TopUp);
		const int64 Bal = Sim->GetMarkBalance();
		const int64 Total = Bal + OwedZ;
		const int64 WantRedeem = Total * R / 1000;
		const int64 PerPart = Bal * R / 1000 + OwedZ * R / 1000;
		const bool bBites = Bal * R % 1000 + RemOwed >= 1000 && WantRedeem == PerPart + 1;
		const int64 Credit0 = Sim->GetCreditBalanceMcr();
		Capture.Clear();
		Alerts.Reset();
		RollNext();
		const bool bRetired = Alerts.Num() == 1 && AlertsStarting(TEXT("THE MARK IS RETIRED")) == 1;
		Sim->Debug_SetMarketEnabled(true);
		bool bPrices = Before.Num() > 0;
		FString PriceBad;
		for (const TPair<FName, int64>& P : Before)
		{
			const FRHResourceRow* Res = DefsSub->GetResource(P.Key);
			const int64 V = Res ? FMath::Max<int64>(0, Res->EarthValue_mcr) : 0;
			const int64 Want = Sim->IsMarketTraded(P.Key) || P.Value > 0 ? FMath::Clamp<int64>(P.Value * R / 1000, V * 2500 / 10000, V * 40000 / 10000) : P.Value;
			const int64 Got = Sim->GetMarketPriceMcr(P.Key);
			bPrices &= Got == Want;
			if (Got != Want) { PriceBad += FString::Printf(TEXT("%s %lld (want %lld); "), *P.Key.ToString(), Got, Want); }
		}
		int64 H2 = 0, O2 = 0;
		const bool bStillMarks = Sim->GetMarkOrder(IdZ, H2, O2);
		Sim->GetBlocMembers(Members);
		FString WAfter;
		const bool bDeskNone = !Sim->CanExchange(EDir::ToMarks, 10, Sim->GetBookingSolNow(), WAfter) && WAfter == TEXT("No Martian currency is issued");
		ExpectSeq({
			[&]() { return Sim->GetCurrencyRedeemedMcr() == WantRedeem; },
			[&]() { return bBites; },
			[&]() { return Sim->GetCreditBalanceMcr() == Credit0 + WantRedeem; },
			[&]() { return Sim->GetMarkBalance() == 0 && Sim->GetMarkReservedMmk() == 0; },
			[&]() { return !bStillMarks && O2 == 0; },
			[&]() { return bPrices; },
			[&]() { return Sim->GetMarketPriceMcr(NIce) == 12000; },
			[&]() { return Members.Num() == 0 && Sim->GetRate() == 1000 && Sim->GetCurrencyIssueSol() == -1 && Sim->GetCurrencyOpenSol() == -1 && !Sim->IsCurrencyWithdrawPending(); },
			[&]() { return Capture.CountLines(*FString::Printf(TEXT("=== CURRENCY WITHDRAWN: %s marks redeemed for %s cr at 3.499 ==="), *Cr(Total), *Cr(WantRedeem))) == 1; },
			[&]() { return bRetired; },
			[&]() { return bDeskNone; },
			[&]() { return Inv(); },
			}, FString::Printf(TEXT("CU9 the withdrawal roll redeems the balance (%s, topped up by %lld mmk) and the %s marks still owed on the road together, %s marks at 3.499 with ONE floor and no spread: %s cr into the redeemed total (the parts' remainders, %lld and %lld milli-credits, pass a whole one together: a floor of each part would pay %s); the purse is empty, the convoy owes nothing more; every slot is re-expressed (Ice 11 -> 38.489, held at its 12 cr ceiling) %s; the bloc dissolves, the rate is 1000, the alert THE MARK IS RETIRED carries the news, and the desk says '%s'"),
				*Cr(Bal), TopUp, *Cr(OwedZ), *Cr(Total), *Cr(WantRedeem), Bal * R % 1000, RemOwed, *Cr(PerPart), *PriceBad, *WAfter));
		const int64 Credit1 = Sim->GetCreditBalanceMcr();
		int32 Steps = 0;
		while (Sim->GetConvoys().ContainsByPredicate([IdZ](const FRHConvoy& X) { return X.Id == IdZ; }) && Steps++ < 12 * (int32)(SolLen / EraDt)) { EraOnce(); }
		Sim->Debug_SetRequisitionInputs(40.0, 30.0);
		FString WReissue;
		const bool bReissue = Sim->CanIssueCurrency(false, WReissue) && Send(TEXT("Currency"), FName("Issue")) && Sim->IsCurrencyIssued() && Sim->GetMarkBalance() == 0;
		ExpectSeq({
			[&]() { return !Sim->GetConvoys().ContainsByPredicate([IdZ](const FRHConvoy& X) { return X.Id == IdZ; }); },
			[&]() { return Sim->GetCreditBalanceMcr() == Credit1 && Sim->GetMarkBalance() == 0; },
			[&]() { return bReissue; },
			}, FString::Printf(TEXT("CU9 the convoy that was on the road comes home (%d era steps) and pays nothing more in either currency; a later issue is allowed (Meridian's route is open at +40) and its purse starts at 0"), Steps));
	}
	{
		// The withdrawal waits for every priced order: a mark order to Zarya still uncleared, its clearing sol moved one sol past the roll and the
		// withdrawal made pending in the file (both frames resealed). The first roll waits; the next clears the order, then withdraws.
		Reset();
		Provision(20000000);
		IssueOpen();
		Sim->Debug_GrantMarks(1000000);
		Send(TEXT("MarketOrder"), NZarya, TEXT("Hydrogen:10"));
		SyncClock();
		const int32 Mcs = Sim->GetMarketCheckedSol();
		Sim->SaveColony(TEXT("cu9wait"), SaveErr);
		TArray<uint8> W;
		FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("cu9wait")));
		FCurSuiteBlock B;
		int32 ClearAt = INDEX_NONE;
		const bool bParsed = CurSuiteParse(W, B) && CurSuiteConvoyClearSolAt(W, 0, ClearAt) && B.Marks.Num() == 1;
		if (bParsed)
		{
			const int32 Later = Mcs + 1, Past = Mcs - 1;
			FMemory::Memcpy(W.GetData() + ClearAt, &Later, 4);
			W[B.IssuedAt] = 0;
			W[B.WithdrawAt] = 1;
			FMemory::Memcpy(W.GetData() + B.WithdrawSolAt, &Past, 4);
			RHSaveFrame::Reseal(W, RHSaveFrame::MarketTag);
			RHSaveFrame::Reseal(W, RHSaveFrame::CurrencyTag);
		}
		FFileHelper::SaveArrayToFile(W, *SaveFilePath(TEXT("cu9wait2")));
		Capture.Clear();
		const bool bLoaded = bParsed && LoadSlot(TEXT("cu9wait2"));
		const bool bQuiet = Capture.NumWarnings() == 0 && Sim->IsCurrencyWithdrawPending() && !Sim->IsCurrencyIssued();
		RollNext();
		const bool bWaited = Sim->IsCurrencyWithdrawPending() && Sim->GetCurrencyIssueSol() >= 0 && Sim->GetConvoys().Num() == 1 && !Sim->GetConvoys()[0].bCleared;
		RollNext();
		const bool bThen = !Sim->IsCurrencyWithdrawPending() && Sim->GetCurrencyIssueSol() == -1 && Sim->GetConvoys().Num() == 1 && Sim->GetConvoys()[0].bCleared && Sim->GetMarkBalance() == 0;
		ExpectSeq({
			[&]() { return bLoaded && bQuiet; },
			[&]() { return bWaited; },
			[&]() { return bThen; },
			[&]() { return Inv(); },
			}, FString::Printf(TEXT("CU9 a withdrawal pending since sol %d waits at the roll closing %d while a mark order due at the end of sol %d is uncleared, and runs at the next roll after that order clears"), Mcs - 1, Mcs, Mcs + 1));
	}
	{
		// An issue and a withdrawal at one roll: DefyIssue from tension 70 leaves 68 (a demand at once), a Comply in the same sol; the roll issues
		// the market and withdraws it at 1000, so the redemption is the balance itself and the prices do not move.
		Reset();
		Sim->Debug_SetRequisitionInputs(0.0, 70.0);
		EraOnce();
		const bool bDI = Send(TEXT("Solidarity"), FName("DefyIssue"));
		EraOnce();
		const bool bAgainPending = Sim->IsEarthDemandPending();
		const bool bC = Send(TEXT("Solidarity"), FName("Comply"));
		const TArray<FRHPoliticalEntry> Four = Sim->GetPoliticalJournal();
		// Defy (sub 0: the identity it left, +20, and -12000), Issue (sub 1: +10000), Comply (sub 0: the identity back at 0, -45000), Withdraw
		// (sub 1: nothing), all keyed to one sol, each pair under its order's CommandId.
		const bool bFour = Four.Num() == 4 && Four[0].ClearSol == Four[3].ClearSol
			&& Four[0].Kind == (uint8)EAct::Defy && Four[0].Sub == 0 && Four[0].bHasIdentity && Four[0].IdentityMilli == 20000 && Four[0].TensionDeltaMilli == -12000
			&& Four[1].Kind == (uint8)EAct::Issue && Four[1].Sub == 1 && !Four[1].bHasIdentity && Four[1].TensionDeltaMilli == 10000 && Four[1].CommandId == Four[0].CommandId
			&& Four[2].Kind == (uint8)EAct::Comply && Four[2].Sub == 0 && Four[2].bHasIdentity && Four[2].IdentityMilli == 0 && Four[2].TensionDeltaMilli == -45000
			&& Four[3].Kind == (uint8)EAct::Withdraw && Four[3].Sub == 1 && !Four[3].bHasIdentity && Four[3].TensionDeltaMilli == 0 && Four[3].CommandId == Four[2].CommandId
			&& Four[2].CommandId > Four[0].CommandId;
		Sim->Debug_GrantMarks(1234567);
		const int64 Ice0 = Sim->GetMarketPriceMcr(NIce), Credit0 = Sim->GetCreditBalanceMcr();
		Sim->Debug_SetMarketEnabled(false);
		Capture.Clear();
		RollNext();
		Sim->Debug_SetMarketEnabled(true);
		TArray<TPair<FName, int32>> Members;
		Sim->GetBlocMembers(Members);
		ExpectSeq({
			[&]() { return bDI && bAgainPending && bC; },
			[&]() { return bFour; },
			[&]() { return Sim->GetCurrencyRedeemedMcr() == 1234567 && Sim->GetCreditBalanceMcr() == Credit0 + 1234567; },
			[&]() { return Sim->GetMarketPriceMcr(NIce) == Ice0; },
			[&]() { return Members.Num() == 0 && Sim->GetCurrencyIssueSol() == -1 && !Sim->IsCurrencyWithdrawPending() && Sim->GetMarkBalance() == 0; },
			[&]() { return Capture.CountLines(TEXT("Currency: the market issues the mark")) == 1 && Capture.CountLines(TEXT("=== CURRENCY WITHDRAWN: 1234.567 marks redeemed for 1234.567 cr at 1 ===")) == 1; },
			[&]() { return Capture.CountLines(TEXT("Bloc: ")) == 0; },
			}, TEXT("CU9 an issue and a withdrawal at one roll (DefyIssue and a Comply in one sol, their four entries each its own: Defy +20 and -12000, Issue +10000, Comply 0 and -45000, Withdraw nothing): the roll issues the market and withdraws it at the start rate 1000: 1234.567 marks redeem for 1234.567 cr, no price moves, no rival joins"));
	}
	{
		// The withdrawal roll with the market LIVE: the re-expression comes after the clearing and E4's reprice (brief binding 10). Twins from one
		// save: A rolls with no Comply (its prices after the roll are that roll's reprice: E4's reprice reads the economies and the tally, never
		// the politics); B Complies first, so the same roll reprices the same way and then withdraws at 3.499: every slot in B must be A's reprice
		// re-expressed and held in its band.
		Reset();
		Provision(20000000);
		const bool bOpen = IssueOpen();
		Demand(65.0);
		SyncClock();
		Sim->SaveColony(TEXT("cu9live"), SaveErr);
		const auto Prices = [&]() { TArray<int64> P; for (int32 i = 0; i < Sim->GetLedgerSlotCount(); ++i) { P.Add(Sim->GetMarketPriceMcr(Sim->GetLedgerSlotName(i))); } return P; };
		LoadSlot(TEXT("cu9live"));
		const TArray<int64> Before = Prices();
		EraOnce();
		Sim->Debug_SetRate(3499);
		RollNext();
		const TArray<int64> A = Prices();
		const bool bANoWithdraw = Sim->GetCurrencyIssueSol() >= 0 && Sim->IsCurrencyIssued();
		LoadSlot(TEXT("cu9live"));
		const bool bComply = Send(TEXT("Solidarity"), FName("Comply"));
		Sim->Debug_SetRate(3499);
		RollNext();
		const TArray<int64> B = Prices();
		const bool bBWithdrew = Sim->GetCurrencyIssueSol() == -1 && !Sim->IsCurrencyWithdrawPending();
		int32 Same = 0, Moved = 0, Repriced = 0;
		FString Bad;
		for (int32 i = 0; i < A.Num() && i < B.Num(); ++i)
		{
			const FName Res = Sim->GetLedgerSlotName(i);
			const FRHResourceRow* Row = DefsSub->GetResource(Res);
			const int64 V = Row ? FMath::Max<int64>(0, Row->EarthValue_mcr) : 0;
			const int64 Want = Sim->IsMarketTraded(Res) || A[i] > 0 ? FMath::Clamp<int64>(A[i] * 3499 / 1000, V * 2500 / 10000, V * 40000 / 10000) : A[i];
			Same += B[i] == Want ? 1 : 0;
			Moved += B[i] != A[i] ? 1 : 0;
			Repriced += A[i] != Before[i] ? 1 : 0;
			if (B[i] != Want) { Bad += FString::Printf(TEXT("%s %lld (want %lld from %lld); "), *Res.ToString(), B[i], Want, A[i]); }
		}
		ExpectSeq({
			[&]() { return bOpen && bComply; },
			[&]() { return bANoWithdraw && bBWithdrew; },
			[&]() { return A.Num() > 0 && Same == A.Num(); },
			[&]() { return Repriced > 0 && Moved > 0; },
			}, FString::Printf(TEXT("CU9 with the market live the withdrawal re-expresses the roll's reprice: %d of %d slots equal the no-Comply twin's price after the same roll (%d slots repriced there) times 3.499, held in the band %s"),
				Same, A.Num(), Repriced, *Bad));
	}
	{
		// The reopen edge: a Comply in sol 3 withdraws at the roll closing 3; an order (to Meridian, discovered: its route survives the Comply) due
		// 10 s after that boundary is refused in the era band (it runs on the step that crosses the boundary, before the roll) and accepted in the
		// agent band (after it). Section 6 allows the split.
		Reset();
		Provision(20000000);
		Sim->Debug_DiscoverRival(NMeridian);
		Sim->Debug_SetRequisitionInputs(20.0, 30.0);
		Sim->Debug_SyncPoliticalView();
		Clock->Debug_AdvanceSimSeconds(25.0);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->EraStep(25.f);
		Send(TEXT("Currency"), FName("Issue"));
		while (Clock->GetSimSecondsTotal() < 3.5 * SolLen) { EraOnce(); }
		Sim->Debug_SetRequisitionInputs(Sim->GetIdentityAxis(), 65.0);
		EraOnce();
		Send(TEXT("Solidarity"), FName("Comply"));
		while (Clock->GetSimSecondsTotal() < 4345.0 - 1.0e-6) { EraOnce(); }
		const bool bReady = FMath::Abs(Clock->GetSimSecondsTotal() - 4345.0) < 1.0e-6 && Sim->IsCurrencyWithdrawPending() && Sim->GetMarketCheckedSol() == 3;
		Sim->SaveColony(TEXT("cu9edge"), SaveErr);
		const auto Edge = [&](bool bAgent) -> bool
		{
			LoadSlot(TEXT("cu9edge"));
			bool bSent = false;
			const int32 Rej = Rejects.Num();
			while (Clock->GetSimSecondsTotal() < 4900.0)
			{
				if (!bSent && Clock->GetSimSecondsTotal() >= 4765.0 - 1.0e-6)
				{
					FRHCommand C; C.Verb = FName("MarketOrder"); C.Target = NMeridian; C.Text = TEXT("SpareParts:1");
					Sim->EnqueueCommand(C);
					bSent = true;
				}
				if (bAgent) { AgentOnce(); } else { EraOnce(); }
			}
			return Rejects.Num() == Rej && Sim->GetConvoys().Num() == 1;
		};
		const bool bEraBooked = Edge(false);
		const FString EraWhy = LastReject();
		const bool bAgentBooked = Edge(true);
		ExpectSeq({
			[&]() { return bReady; },
			[&]() { return !bEraBooked && EraWhy == TEXT("The market is closed while the mark is withdrawn"); },
			[&]() { return bAgentBooked; },
			}, FString::Printf(TEXT("CU9 the reopen edge: an order due 10 s after the withdrawal roll's boundary is refused in the era band ('%s': it runs before that roll) and booked in the agent band (after it)"), *EraWhy));
	}

	// --- CU10. The save: an issued colony with two members, a mark convoy out, a desk order pending and two acts pending in the journal: save,
	// load, save is byte-identical; 'RHE5' FOLLOWS 'RHE4' (found by tag; never "the last frame") with payload == GetCurrencyBlockBytes(); the
	// lists read back ascending; every repair of section 5 on a doctored, resealed file; the previous version refused; a lost 'RHE4' and a lost
	// 'RHE5'; the flags' reconciliation (O8), and its exception on the two consistent files saved between an issue act and its roll; and a
	// fresh-load NextCommandId past the journal's acts and, where it is the newest command, a pending desk order. ---
	{
		Reset();
		Provision(20000000);
		Sim->Debug_AddInfluence(40.0);
		Inject(FName("CuIn"), 40.0, TEXT("Ice:50;Struct:20"), TEXT("Ice:10;Struct:5"), TEXT("Ice:700;Struct:400"), TEXT("Ice:4000;Struct:3000"), 0);
		Sim->Debug_SyncRivalEconomies();
		IssueOpen();
		Sim->Debug_GrantMarks(2000000);
		Send(TEXT("MarketOrder"), NZarya, TEXT("Ice:100"));
		Send(TEXT("Exchange"), FName("ToMarks"), FString(), 100.0);
		Demand(65.0);
		Send(TEXT("Solidarity"), FName("Defy"));
		Sim->Debug_SetEmbargo(FName("CuIn"), 5.0);
		Send(TEXT("Pacify"), FName("CuIn"));
		SyncClock();
		const int32 Bytes = Sim->GetCurrencyBlockBytes();
		const int64 CreditReserved0 = Sim->GetCreditReservedMcr();
		const int64 Room0 = Sim->GetCreditAwardedMcr() - Sim->GetSalesPaidMcr();
		const double Ident0 = Sim->GetIdentityAxis(), Tension0 = Sim->GetEarthTension();
		Expect(Sim->SaveColony(TEXT("cu10"), SaveErr), FString::Printf(TEXT("CU10 the fixture is saved (%s)"), *SaveErr));
		TArray<uint8> W;
		FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("cu10")));
		uint32 Ver = 0;
		if (W.Num() > 8) { FMemory::Memcpy(&Ver, W.GetData() + 4, 4); }
		FCurSuiteBlock B;
		const bool bParsed = CurSuiteParse(W, B);
		int32 MarketAt = INDEX_NONE, MarketPayload = 0;
		const bool bMarket = RHSaveFrame::Find(W, RHSaveFrame::MarketTag, MarketAt, MarketPayload);
		bool bJournalAsc = B.Journal.Num() >= 2;
		for (int32 j = 1; j < B.Journal.Num(); ++j) { bJournalAsc &= B.Journal[j - 1].ExecuteAt < B.Journal[j].ExecuteAt || (B.Journal[j - 1].ExecuteAt == B.Journal[j].ExecuteAt && B.Journal[j - 1].CommandId < B.Journal[j].CommandId); }
		const bool bShape = bParsed && Ver == 58 && bMarket && MarketAt + RHSaveFrame::HeaderBytes + MarketPayload + RHSaveFrame::FooterBytes == B.FrameAt && B.PayloadBytes == Bytes
			&& B.Issued == 1 && B.IssueSol >= 0 && B.OpenSol == B.IssueSol + 2 && B.Members.Num() == 2 && B.Members[0].Name == TEXT("CuIn") && B.Members[1].Name == TEXT("Zarya")
			&& B.Marks.Num() == 1 && B.Marks[0].Held > 0 && B.Desks.Num() == 1 && B.Desks[0].Amount == 100000 && bJournalAsc && B.Journal.Num() == 2
			&& B.Reserved == Sim->GetMarkReservedMmk() && B.Balance == Sim->GetMarkBalance();
		Expect(bShape, FString::Printf(TEXT("CU10 v%u: 'RHE5' follows 'RHE4' straight after its footer (found by tag), its payload GetCurrencyBlockBytes() (%d bytes): issued at sol %d, open at %d, 2 members ascending (%s, %s), 1 mark order, 1 desk order of 100 cr, %d journal entries in (ExecuteAt, CommandId, sub) order"),
			Ver, Bytes, B.IssueSol, B.OpenSol, B.Members.Num() > 0 ? *B.Members[0].Name : TEXT("?"), B.Members.Num() > 1 ? *B.Members[1].Name : TEXT("?"), B.Journal.Num()));
		// The two acts pending in the file, each with its own effect: the Defy (the identity it left, +40, and -12000) and the Pacify (no
		// identity, -20000).
		ExpectSeq({
			[&]() { return B.Journal.Num() == 2; },
			[&]() { return B.Journal[0].Kind == (uint8)EAct::Defy && B.Journal[0].Sub == 0 && B.Journal[0].HasIdentity == 1 && B.Journal[0].IdentityMilli == 40000 && B.Journal[0].TensionDelta == -12000; },
			[&]() { return B.Journal[1].Kind == (uint8)EAct::Pacify && B.Journal[1].Sub == 0 && B.Journal[1].HasIdentity == 0 && B.Journal[1].IdentityMilli == 0 && B.Journal[1].TensionDelta == -20000; },
			[&]() { return B.Journal[0].ClearSol == FMath::FloorToInt32(B.Journal[0].ExecuteAt / SolLen) && B.Journal[1].ClearSol == FMath::FloorToInt32(B.Journal[1].ExecuteAt / SolLen); },
			}, TEXT("CU10 the file's pending acts carry their own effects: the Defy sets the identity it left (+40000) and adds -12000, the Pacify sets none and adds -20000, each keyed to its ExecuteAt's sol"));
		FString Le;
		const bool bLoaded = Sim->LoadColony(TEXT("cu10"), Le) && Le.IsEmpty() && Sim->SaveColony(TEXT("cu10b"), SaveErr);
		TArray<uint8> W2;
		ExpectSeq({
			[&]() { return bLoaded; },
			[&]() { return FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("cu10b"))); },
			[&]() { return W2 == W; },
			[&]() { return Inv(); },
			}, FString::Printf(TEXT("CU10 save, load, save is byte-identical (%d bytes)"), W.Num()));
		const auto TryBytes = [&](const TCHAR* Slot, const TArray<uint8>& Raw, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Raw, *SaveFilePath(Slot));
			Capture.Clear();
			Rejects.Reset();
			FString E;
			const bool bOk = Sim->LoadColony(Slot, E);
			OutErr = E;
			return bOk && E.IsEmpty();
		};
		const auto Sealed = [](TArray<uint8> T) { RHSaveFrame::Reseal(T, RHSaveFrame::CurrencyTag); return T; };
		const auto Put32 = [](TArray<uint8>& T, int32 Off, int32 V) { FMemory::Memcpy(T.GetData() + Off, &V, 4); };
		const auto Put64 = [](TArray<uint8>& T, int32 Off, int64 V) { FMemory::Memcpy(T.GetData() + Off, &V, 8); };
		const auto PutD = [](TArray<uint8>& T, int32 Off, double V) { FMemory::Memcpy(T.GetData() + Off, &V, 8); };
		{
			TArray<uint8> T = W; const uint32 V55 = 55; FMemory::Memcpy(T.GetData() + 4, &V55, 4);
			FString E;
			const bool bOk = TryBytes(TEXT("cu10r1"), T, E);
			ExpectSeq({
				[&]() { return !bOk; },
				[&]() { return E.Contains(TEXT("Save format mismatch")); },
				[&]() { return E.Contains(TEXT("/58")); },
				}, FString::Printf(TEXT("CU10 a v55 file is refused (%s)"), *E));
		}
		const auto Doctor = [&](const TCHAR* Slot, const TArray<uint8>& Base, const TCHAR* Warning, TFunctionRef<bool()> Check) -> bool
		{
			FString E;
			const bool bOk = TryBytes(Slot, Sealed(Base), E);
			const bool bWarned = Capture.CountWarnings(Warning) >= 1 && Capture.NumErrors() == 0;
			const bool bCheck = Check();
			if (!bOk || !bWarned || !bCheck) { Info(FString::Printf(TEXT("CU10 repair %s: loaded %d, warned %d (%s), check %d, %d warnings"), Slot, bOk ? 1 : 0, bWarned ? 1 : 0, Warning, bCheck ? 1 : 0, Capture.NumWarnings())); }
			return bOk && bWarned && bCheck;
		};
		bool bRepairs = bParsed && B.Journal.Num() == 2 && B.Members.Num() == 2 && B.Marks.Num() == 1 && B.Desks.Num() == 1;
		FString Failed;
		const auto Note = [&](const TCHAR* What, bool bOk) { bRepairs &= bOk; if (!bOk) { Failed += FString(What) + TEXT("; "); } };
		if (bRepairs)
		{
			{ TArray<uint8> T = W; Put64(T, B.RateAt, 99999);
			  Note(TEXT("rate band"), Doctor(TEXT("cu10d1"), T, TEXT("outside [250, 4000]; clamped"), [&]() { return Sim->GetRate() == 4000 && Inv(); })); }
			{ TArray<uint8> T = W; Put64(T, B.IdentViewAt, 200000);
			  Note(TEXT("record range"), Doctor(TEXT("cu10d2"), T, TEXT("the political record (identity 200000"), [&]() { return Sim->GetIdentityView() == 100000; })); }
			{ TArray<uint8> T = W; PutD(T, B.Journal[1].ExecuteAtAt, B.Journal[0].ExecuteAt - 1.0);
			  Note(TEXT("journal order"), Doctor(TEXT("cu10d3"), T, TEXT("the political journal is unusable (entry 1 is out of order or repeated)"), [&]() {
				  return Sim->GetPoliticalJournal().Num() == 0 && Sim->GetIdentityView() == (int64)FMath::RoundToDouble(Sim->GetIdentityAxis() * 1000.0) && Sim->GetTensionView() == (int64)FMath::RoundToDouble(Sim->GetEarthTension() * 1000.0); })); }
			{ TArray<uint8> T = W; T[B.Members[0].NameAt] = 'Z'; T[B.Members[0].NameAt + 1] = 'u'; // CuIn -> ZuIn: after Zarya
			  Note(TEXT("member order"), Doctor(TEXT("cu10d4"), T, TEXT("the bloc members are out of order"), [&]() { TArray<TPair<FName, int32>> Mm; Sim->GetBlocMembers(Mm); return Mm.Num() == 0; })); }
			{ TArray<uint8> T = W; Put32(T, B.Marks[0].IdAt, 99);
			  Note(TEXT("mark order names no convoy"), Doctor(TEXT("cu10d5"), T, TEXT("the mark order of convoy #99 is dropped (the market block holds no such convoy)"), [&]() {
				  return Sim->GetConvoys().Num() == 1 && !Sim->GetConvoys()[0].bMarks && Sim->GetMarkReservedMmk() == 0 && Inv(); })); }
			{ TArray<uint8> T = W; Put64(T, B.BalanceAt, -5);
			  Note(TEXT("negative purse"), Doctor(TEXT("cu10d6"), T, TEXT("the mark balance is -5; set to 0"), [&]() {
				  return Sim->GetMarkBalance() == 0 && Sim->GetMarkSpentMmk() == Sim->GetMarkEarnedMmk() && Sim->GetMarkReservedMmk() == 0 && Inv(); })); }
			{ TArray<uint8> T = W; Put64(T, B.ReservedAt, B.Reserved + 7);
			  Note(TEXT("mark reservation"), Doctor(TEXT("cu10d7"), T, TEXT("disagree with the orders'"), [&]() { return Sim->GetMarkReservedMmk() == B.Reserved && Inv(); })); }
			{ TArray<uint8> T = W; T[B.IssuedAt] = 0;
			  Note(TEXT("O8 flag raised"), Doctor(TEXT("cu10d8"), T, TEXT("the market counts in marks with the mark not issued"), [&]() { return Sim->IsCurrencyIssued(); })); }
			{ TArray<uint8> T = W; Put32(T, B.IssueSolAt, -1); Put32(T, B.OpenSolAt, -1);
			  Note(TEXT("O8 flag cleared"), Doctor(TEXT("cu10d9"), T, TEXT("the mark was issued with no issue in the market or the journal"), [&]() {
				  TArray<TPair<FName, int32>> Mm; Sim->GetBlocMembers(Mm); TArray<FRHDeskOrder> Dk; Sim->GetDeskOrders(Dk);
				  return !Sim->IsCurrencyIssued() && Mm.Num() == 0 && Dk.Num() == 0 && Sim->GetCreditReservedMcr() == 0 && Inv(); })); }
			{ TArray<uint8> T = W; Put32(T, B.IssueSolAt, -1); Put32(T, B.OpenSolAt, -1); T[B.IssuedAt] = 0; T[B.WithdrawAt] = 1; Put32(T, B.WithdrawSolAt, B.IssueSol + 1);
			  Note(TEXT("O8 withdrawal dropped"), Doctor(TEXT("cu10d10"), T, TEXT("a withdrawal is pending with no issue in the market or the journal"), [&]() { return !Sim->IsCurrencyWithdrawPending() && !Sim->IsCurrencyIssued(); })); }
			// Both flags up (play never writes it): the withdrawal stands, so the penalty, the "already issued" refusal and the desk all end.
			{ TArray<uint8> T = W; T[B.WithdrawAt] = 1; Put32(T, B.WithdrawSolAt, B.IssueSol + 1);
			  Note(TEXT("O8 both flags"), Doctor(TEXT("cu10d11"), T, TEXT("the mark is both issued and being withdrawn; the withdrawal stands"), [&]() {
				  FString WI, WX;
				  return !Sim->IsCurrencyIssued() && Sim->IsCurrencyWithdrawPending() && Sim->GetCurrencyIssueSol() == B.IssueSol
					  && !Sim->CanIssueCurrency(false, WI) && WI == TEXT("The mark is being withdrawn")
					  && !Sim->CanExchange(EDir::ToMarks, 10, Sim->GetBookingSolNow(), WX) && WX == TEXT("The market is closed while the mark is withdrawn")
					  && Sim->GetRequisitionMultiplier() == RefMult(Sim->GetIdentityAxis(), Sim->GetEarthTension(), false) && Inv(); })); }
			// A malformed frame (a journal count out of range) and a truncated one (3 bytes short, the frame rebuilt for its length): each starts the
			// mark unissued with the record read from the identity and tension just loaded.
			const auto Fresh = [&]() { return !Sim->IsCurrencyIssued() && Sim->GetCurrencyIssueSol() == -1 && Sim->IsCurrencyBlockLost() && !Sim->IsMarketBlockLost() && Sim->GetMarkBalance() == 0
				&& Sim->GetIdentityView() == (int64)FMath::RoundToDouble(Sim->GetIdentityAxis() * 1000.0) && Sim->GetTensionView() == (int64)FMath::RoundToDouble(Sim->GetEarthTension() * 1000.0) && Inv(); };
			{ TArray<uint8> T = W; Put32(T, B.TensionViewAt + 8, 9999);
			  Note(TEXT("malformed frame"), Doctor(TEXT("cu10d12"), T, TEXT("the currency block is malformed (journal count 9999)"), Fresh)); }
			{ TArray<uint8> T = W; TArray<uint8> Cut(W.GetData() + B.PayloadAt, B.PayloadBytes - 3);
			  Note(TEXT("truncated frame"), CurSuiteReframe(T, RHSaveFrame::CurrencyTag, Cut) && Doctor(TEXT("cu10d13"), T, TEXT("the currency block is malformed (the block is cut short)"), Fresh)); }
			// Trap 8, the lifetime totals' precedence: each clamped to [0, awarded - sales paid] first (the desk's, 5 mcr over, with its Warning),
			// then, together over by 50, the redeemed total cut first (to 0); E4's sales clamp then sees no room left.
			{ TArray<uint8> T = W; Put64(T, B.DeskAt, Room0 + 5); Put64(T, B.RedeemedAt, 50);
			  Note(TEXT("lifetime totals"), Doctor(TEXT("cu10d14"), T, TEXT("the desk and redeemed credit pass the awarded total by 50 mcr; the redeemed credit is cut by 50"), [&]() {
				  return Capture.CountWarnings(*FString::Printf(TEXT("the lifetime desk credit of %lld mcr is outside [0, %lld]; clamped"), Room0 + 5, Room0)) == 1
					  && Capture.CountWarnings(TEXT("the desk credit is cut")) == 0 && Sim->GetDeskCreditMcr() == Room0 && Sim->GetCurrencyRedeemedMcr() == 0 && Sim->GetMarketSalesMcr() == 0 && Inv(); })); }
			// Each repair's other direction: the rate below the band, the tension's record below its range, a journal key repeated (entry 1 given
			// entry 0's ExecuteAt, CommandId and sub) and a member name repeated (the second member's record replaced by the first's, the frame
			// rebuilt for its length) - so a strict comparison written as a non-strict one fails here.
			{ TArray<uint8> T = W; Put64(T, B.RateAt, 100);
			  Note(TEXT("rate band, below"), Doctor(TEXT("cu10d15"), T, TEXT("the rate 100 mcr a mark is outside [250, 4000]; clamped"), [&]() { return Sim->GetRate() == 250 && Inv(); })); }
			{ TArray<uint8> T = W; Put64(T, B.TensionViewAt, -5);
			  Note(TEXT("record range, tension"), Doctor(TEXT("cu10d16"), T, *FString::Printf(TEXT("the political record (identity %lld, tension -5) is outside its range; clamped"), B.IdentView), [&]() {
				  return Sim->GetTensionView() == 0 && Sim->GetIdentityView() == B.IdentView && Sim->GetPoliticalJournal().Num() == 2; })); }
			{ TArray<uint8> T = W; PutD(T, B.Journal[1].ExecuteAtAt, B.Journal[0].ExecuteAt); Put32(T, B.Journal[1].ExecuteAtAt + 8, B.Journal[0].CommandId); Put32(T, B.Journal[1].SubAt, B.Journal[0].Sub);
			  Note(TEXT("journal key repeated"), Doctor(TEXT("cu10d17"), T, TEXT("the political journal is unusable (entry 1 is out of order or repeated)"), [&]() {
				  return Sim->GetPoliticalJournal().Num() == 0 && Sim->GetIdentityView() == (int64)FMath::RoundToDouble(Sim->GetIdentityAxis() * 1000.0) && Sim->GetTensionView() == (int64)FMath::RoundToDouble(Sim->GetEarthTension() * 1000.0); })); }
			{
				const int32 M0 = B.Members[0].NameAt - 4 - B.PayloadAt, M1 = B.Members[1].NameAt - 4 - B.PayloadAt;
				const int32 M1End = M1 + 4 + (B.Members[1].Name.Len() + 1) + 4;
				const TArray<uint8> P(W.GetData() + B.PayloadAt, B.PayloadBytes);
				TArray<uint8> Twice(P.GetData(), M1);
				Twice.Append(P.GetData() + M0, M1 - M0);
				Twice.Append(P.GetData() + M1End, P.Num() - M1End);
				TArray<uint8> T = W;
				Note(TEXT("member repeated"), M1End <= P.Num() && CurSuiteReframe(T, RHSaveFrame::CurrencyTag, Twice) && Doctor(TEXT("cu10d18"), T, TEXT("the bloc members are out of order, repeated or held without an issued market"), [&]() {
					TArray<TPair<FName, int32>> Mm; Sim->GetBlocMembers(Mm); return Mm.Num() == 0 && Sim->IsCurrencyIssued(); }));
			}
			// A desk order above what the desk takes in a sol (booking never sets one aside): dropped with its Warning, nothing it held kept.
			{ TArray<uint8> T = W; Put64(T, B.Desks[0].IdAt + 5, Sim->GetDeskCapPerSol() * 1000 + 1);
			  Note(TEXT("desk order over the cap"), Doctor(TEXT("cu10d19"), T, *FString::Printf(TEXT("the desk order #%d is dropped (more than the desk takes in a sol, %lld); nothing it set aside is held"), B.Desks[0].Id, Sim->GetDeskCapPerSol()), [&]() {
				  TArray<FRHDeskOrder> Dk; Sim->GetDeskOrders(Dk); return Dk.Num() == 0 && Sim->GetCreditReservedMcr() == 0 && Inv(); })); }
		}
		Expect(bRepairs, FString::Printf(TEXT("CU10 sealed, doctored currency blocks each load with their own Warning and no Error: a rate outside the band is clamped (above and below), the record outside its range is clamped (the identity above, the tension below), a journal out of order or with a repeated key is emptied and the record read from the live values, members out of order or repeated empty the bloc, a mark order naming no convoy is dropped (the convoy settles in credits), a negative purse is 0 with spent following, the reserved marks are set to the orders' sum, the flags are reconciled four ways (both up: the withdrawal stands), a malformed and a truncated frame start the mark unissued with the record read from the live values, the lifetime totals are clamped each, then the redeemed cut first, and a desk order above the daily cap is dropped %s"), *Failed));
		// E4's reserved counter now includes the desk: doctored, it is set to the convoys' and the ToMarks orders' sum together, with E4's text.
		{
			TArray<uint8> T = W;
			int32 ClearAt = INDEX_NONE, ReservedAt = INDEX_NONE;
			CurSuiteConvoyClearSolAt(W, 0, ClearAt, &ReservedAt);
			int64 InFile = -1;
			if (ReservedAt != INDEX_NONE) { FMemory::Memcpy(&InFile, W.GetData() + ReservedAt, 8); }
			FString E;
			bool bOk = false;
			if (ReservedAt != INDEX_NONE && InFile == CreditReserved0 && CreditReserved0 == 100000)
			{
				const int64 Want = CreditReserved0;
				const int64 Doctored = Want + 7;
				FMemory::Memcpy(T.GetData() + ReservedAt, &Doctored, 8);
				RHSaveFrame::Reseal(T, RHSaveFrame::MarketTag);
				bOk = TryBytes(TEXT("cu10m1"), T, E) && Capture.CountWarnings(*FString::Printf(TEXT("the reserved credit %lld mcr disagrees with the convoys' %lld mcr"), Doctored, Want)) == 1 && Sim->GetCreditReservedMcr() == Want && Inv();
			}
			Expect(bOk, TEXT("CU10 E4's reserved counter, doctored by 7 mcr, is set to the credit convoys' and the pending ToMarks orders' sum together, with E4's own text (ReconcileReservations runs after the currency block)"));
		}
		// A mark order whose convoy E4's reader dropped goes silently (E4 has warned): the convoy's rival renamed in E4's block.
		{
			TArray<uint8> T = W;
			int32 MAt = INDEX_NONE, MBytes = 0;
			RHSaveFrame::Find(W, RHSaveFrame::MarketTag, MAt, MBytes);
			int32 NameAt = INDEX_NONE;
			const uint8 Pat[] = { 6, 0, 0, 0, 'Z', 'a', 'r', 'y', 'a', 0 };
			for (int32 At = MAt + MBytes; At > MAt; --At)
			{
				if (At + 10 <= W.Num() && FMemory::Memcmp(W.GetData() + At, Pat, 10) == 0) { NameAt = At + 4; break; }
			}
			FString E;
			bool bOk = false;
			if (NameAt != INDEX_NONE)
			{
				T[NameAt + 4] = 'z';
				RHSaveFrame::Reseal(T, RHSaveFrame::MarketTag);
				// The exact Warning set: E4's drop, then the reserved-marks counter set to the orders' sum (the file counted the dropped convoy's marks;
				// E4's own reserved-credit repair does the same when it drops a credit convoy). No mark-order line: the order itself goes silently.
				bOk = TryBytes(TEXT("cu10m2"), T, E) && Capture.NumWarnings() == 2 && Capture.CountWarnings(TEXT("its rival no longer exists")) == 1
					&& Capture.CountWarnings(*FString::Printf(TEXT("LoadColony v56: the reserved marks %lld mmk disagree with the orders' 0 mmk; set to the orders'"), B.Reserved)) == 1
					&& Capture.CountWarnings(TEXT("LoadColony v56: the mark order")) == 0 && Capture.NumErrors() == 0 && Sim->GetMarkReservedMmk() == 0 && Inv();
				if (!bOk) { Info(FString::Printf(TEXT("CU10 cu10m2: %d warnings: %s"), Capture.NumWarnings(), *FString::Join(Capture.Warnings, TEXT(" | ")))); }
			}
			Expect(bOk, FString::Printf(TEXT("CU10 a mark order whose convoy E4's reader dropped (its rival renamed) is dropped silently (cross-check item 2): exactly two Warnings, E4's drop and the reserved marks (%s in the file) set to the orders' 0, none for the mark order itself; no marks stay reserved"), *Cr(B.Reserved)));
		}
		// A lost 'RHE4' (its tag destroyed) starts the currency unissued with its Warning; a lost 'RHE5' too.
		{
			TArray<uint8> T = W; Put32(T, MarketAt, 0);
			FString E;
			const bool bOk = TryBytes(TEXT("cu10l1"), T, E);
			ExpectSeq({
				[&]() { return bOk; },
				[&]() { return Capture.CountWarnings(TEXT("the market block is lost")) == 1; },
				[&]() { return Capture.CountWarnings(TEXT("the market block was lost, so the mark is lost with it")) == 1; },
				[&]() { return Capture.NumErrors() == 0; },
				[&]() { return !Sim->IsCurrencyIssued() && Sim->GetCurrencyIssueSol() == -1 && Sim->GetMarkBalance() == 0 && Sim->IsMarketBlockLost() && Sim->IsCurrencyBlockLost(); },
				[&]() { return Sim->GetMarketBlockLoss() == URHSimWorldSubsystem::ERHMarketBlockLoss::Lost; },
				[&]() { return Inv(); },
				}, TEXT("CU10 a lost 'RHE4' starts the currency unissued (purse, bloc and orders gone) with its Warning, the loss reason Lost"));
			TArray<uint8> T2 = W; Put32(T2, B.FrameAt, 0);
			const bool bOk2 = TryBytes(TEXT("cu10l2"), T2, E);
			ExpectSeq({
				[&]() { return bOk2; },
				[&]() { return Capture.CountWarnings(TEXT("LoadColony v56: the currency block is lost")) == 1; },
				[&]() { return Capture.NumErrors() == 0; },
				[&]() { return !Sim->IsCurrencyIssued() && Sim->GetCurrencyIssueSol() == -1 && Sim->IsCurrencyBlockLost() && !Sim->IsMarketBlockLost(); },
				[&]() { return Sim->GetConvoys().Num() == 1 && !Sim->GetConvoys()[0].bMarks; },
				[&]() { return Sim->GetPoliticalJournal().Num() == 0 && Sim->GetIdentityAxis() == Ident0 && Sim->GetEarthTension() == Tension0; },
				[&]() { return Sim->GetIdentityView() == (int64)FMath::RoundToDouble(Ident0 * 1000.0) && Sim->GetTensionView() == (int64)FMath::RoundToDouble(Tension0 * 1000.0); },
				[&]() { return Inv(); },
				}, FString::Printf(TEXT("CU10 a lost 'RHE5' (its tag destroyed) starts the mark unissued with a Warning, the record read from the identity and tension just loaded (%s, %s; the journal's two acts are gone with the frame); E4's block loads whole and its convoy settles in credits"),
					*Cr(Sim->GetIdentityView()), *Cr(Sim->GetTensionView())));
		}
		// The other three reasons E4's block starts the market at its start values (cross-check item 3; E8 words its drop from each): a malformed
		// payload (its slot count -1), a slot table out of order (the first name made to sort last) and a repeated region (the region's record
		// written twice, the frame rebuilt for its length). Each drops the currency frame with ruling 2's Warning, and records its reason.
		{
			const auto LostWith = [&](const TCHAR* Slot, TArray<uint8> T, const TCHAR* E4Warning, URHSimWorldSubsystem::ERHMarketBlockLoss Why) -> bool
			{
				FString E;
				const bool bOk = TryBytes(Slot, T, E);
				const bool bAll = bOk && Capture.CountWarnings(E4Warning) == 1 && Capture.CountWarnings(TEXT("the market block was lost, so the mark is lost with it")) == 1 && Capture.NumErrors() == 0
					&& Sim->GetMarketBlockLoss() == Why && Sim->IsMarketBlockLost() && Sim->IsCurrencyBlockLost() && !Sim->IsCurrencyIssued() && Sim->GetCurrencyIssueSol() == -1 && Sim->GetMarkBalance() == 0 && Inv();
				if (!bAll) { Info(FString::Printf(TEXT("CU10 %s: loaded %d, E4's Warning %d, the currency's %d, reason %d, %d warnings"), Slot, bOk ? 1 : 0, Capture.CountWarnings(E4Warning), Capture.CountWarnings(TEXT("the market block was lost, so the mark is lost with it")), (int32)Sim->GetMarketBlockLoss(), Capture.NumWarnings())); }
				return bAll;
			};
			// The market frame found for the shape check above (nothing is doctored without it: the first condition below then fails).
			const TArray<uint8> MP = bMarket ? TArray<uint8>(W.GetData() + MarketAt + RHSaveFrame::HeaderBytes, MarketPayload) : TArray<uint8>();
			int32 SlotCount = 0;
			if (MP.Num() >= 4) { FMemory::Memcpy(&SlotCount, MP.GetData(), 4); }
			int32 Cur = 4;
			for (int32 k = 0; k < SlotCount && Cur + 4 <= MP.Num(); ++k) { int32 Len = 0; FMemory::Memcpy(&Len, MP.GetData() + Cur, 4); Cur += 4 + FMath::Max(0, Len); }
			int32 MarketCount = 0;
			if (Cur + 4 <= MP.Num()) { FMemory::Memcpy(&MarketCount, MP.GetData() + Cur, 4); }
			const int32 RecAt = Cur + 4, RecBytes = 4 + SlotCount * 17 * 8 + 8;
			TArray<uint8> Malformed = W, Slots = W;
			if (MP.Num() > 8)
			{
				Put32(Malformed, MarketAt + RHSaveFrame::HeaderBytes, -1);
				RHSaveFrame::Reseal(Malformed, RHSaveFrame::MarketTag);
				Slots[MarketAt + RHSaveFrame::HeaderBytes + 8] = 'z'; // the first slot name's first letter: it now sorts after the second
				RHSaveFrame::Reseal(Slots, RHSaveFrame::MarketTag);
			}
			TArray<uint8> Regions = W;
			bool bRegionsBuilt = MarketCount == 1 && RecAt + RecBytes <= MP.Num();
			if (bRegionsBuilt)
			{
				TArray<uint8> Twice(MP.GetData(), Cur);
				const int32 Two = 2;
				Twice.Append((const uint8*)&Two, 4);
				Twice.Append(MP.GetData() + RecAt, RecBytes);
				Twice.Append(MP.GetData() + RecAt, RecBytes);
				Twice.Append(MP.GetData() + RecAt + RecBytes, MP.Num() - RecAt - RecBytes);
				bRegionsBuilt = CurSuiteReframe(Regions, RHSaveFrame::MarketTag, Twice);
			}
			using ELoss = URHSimWorldSubsystem::ERHMarketBlockLoss;
			ExpectSeq({
				[&]() { return bMarket && SlotCount > 1 && MarketCount == 1; },
				[&]() { return LostWith(TEXT("cu10l3"), Malformed, TEXT("the market block is malformed (slot count -1)"), ELoss::Malformed); },
				[&]() { return LostWith(TEXT("cu10l4"), Slots, TEXT("the market block's slot names are out of order or repeated"), ELoss::SlotsReset); },
				[&]() { return bRegionsBuilt && LostWith(TEXT("cu10l5"), Regions, TEXT("a region repeats in the market block"), ELoss::RegionsRepeated); },
				}, FString::Printf(TEXT("CU10 E4's block started fresh three more ways - malformed (slot count -1), a slot table out of order, a region written twice - each drops the currency frame with ruling 2's Warning and records its reason (Malformed, SlotsReset, RegionsRepeated): the mark unissued, the purse empty (%d slots, %d region)"),
					SlotCount, MarketCount));
		}
		// A fresh process: the counter is not saved; the load raises it past the pending desk order and every journal entry.
		{
			Sim->Debug_SetNextCommandId(1);
			FString E;
			const bool bOk = Sim->LoadColony(TEXT("cu10"), E) && E.IsEmpty();
			int32 MaxId = B.Desks[0].Id;
			for (const FCurSuiteBlock::FEntry& J : B.Journal) { MaxId = FMath::Max(MaxId, J.CommandId); }
			Expect(bOk && Sim->GetNextCommandId() == MaxId + 1, FString::Printf(TEXT("CU10 a fresh process's command counter (1) is raised by the load past the journal's acts, here the newest commands (the desk order #%d is older; the desk orders' own raise is shown on 'cu10cut' below): next %d"), B.Desks[0].Id, Sim->GetNextCommandId()));
		}
		// R4's cut order, in the mark purse: Zarya's mark order and an exchange of 100 marks both set aside, then the file's balance doctored
		// below their sum (spent following, so the purse itself reads true). The cut takes the desk order first (by Id descending), then the
		// convoy: 50 marks short cuts only the desk order; 110 short empties it and cuts the convoy by 10.
		{
			LoadSlot(TEXT("cu10"));
			const bool bBack = Send(TEXT("Exchange"), FName("ToCredits"), FString(), 100.0);
			Sim->SaveColony(TEXT("cu10cut"), SaveErr);
			TArray<uint8> WC;
			FFileHelper::LoadFileToArray(WC, *SaveFilePath(TEXT("cu10cut")));
			FCurSuiteBlock BC;
			const bool bShapeC = bBack && CurSuiteParse(WC, BC) && BC.Marks.Num() == 1 && BC.Desks.Num() == 2 && BC.Reserved == BC.Marks[0].Held + 100000;
			const int64 Held = bShapeC ? BC.Marks[0].Held : 0;
			const int32 MarkId = bShapeC ? BC.Marks[0].Id : -1, DeskId = bShapeC ? BC.Desks[1].Id : -1;
			// Trap 7's desk half: here the newest command is the pending exchange of 100 marks (sent after the journal's two acts), so the fresh
			// load's counter can only clear it by the desk orders' raise; the journal's alone would leave it ON that order's Id.
			{
				int32 JournalMax = 0;
				for (const FCurSuiteBlock::FEntry& J : BC.Journal) { JournalMax = FMath::Max(JournalMax, J.CommandId); }
				const int32 DeskMax = bShapeC ? BC.Desks.Last().Id : -1;
				Sim->Debug_SetNextCommandId(1);
				FString E;
				const bool bOk = Sim->LoadColony(TEXT("cu10cut"), E) && E.IsEmpty();
				ExpectSeq({
					[&]() { return bShapeC && BC.Journal.Num() == 2; },
					[&]() { return BC.Desks[0].Id < BC.Desks[1].Id; },
					[&]() { return DeskMax > JournalMax; },
					[&]() { return bOk && Sim->GetNextCommandId() == DeskMax + 1; },
					}, FString::Printf(TEXT("CU10 the desk orders read back ascending (#%d, #%d), and with the newest command a pending desk order (#%d, past the journal's #%d) a fresh process's counter (1) is raised by the load to %d, one past it"),
						bShapeC ? BC.Desks[0].Id : -1, DeskMax, DeskMax, JournalMax, Sim->GetNextCommandId()));
			}
			const auto CutRun = [&](const TCHAR* Slot, int64 Balance, int64 WantDesk, int64 WantHeld) -> bool
			{
				TArray<uint8> T = WC;
				Put64(T, BC.BalanceAt, Balance);
				Put64(T, BC.SpentAt, BC.Earned - Balance);
				FString E;
				const bool bOk = TryBytes(Slot, Sealed(T), E);
				TArray<FRHDeskOrder> Dk;
				Sim->GetDeskOrders(Dk);
				const FRHDeskOrder* Back = Dk.FindByPredicate([DeskId](const FRHDeskOrder& D) { return D.Id == DeskId; });
				int64 H = -1, O = -1;
				Sim->GetMarkOrder(MarkId, H, O);
				const TArray<FString> Cuts = Capture.Matching(TEXT("pass the mark balance"));
				const bool bOrder = WantHeld == Held ? (Cuts.Num() == 1 && Cuts[0].Contains(*FString::Printf(TEXT("desk order #%d's"), DeskId)))
					: (Cuts.Num() == 2 && Cuts[0].Contains(*FString::Printf(TEXT("desk order #%d's"), DeskId)) && Cuts[1].Contains(*FString::Printf(TEXT("convoy #%d's"), MarkId)));
				const bool bAll = bOk && bOrder && Capture.NumErrors() == 0 && (WantDesk > 0 ? (Back && Back->Amount == WantDesk) : !Back) && H == WantHeld
					&& Sim->GetMarkReservedMmk() == Balance && Sim->GetMarkBalance() == Balance && Inv();
				if (!bAll) { Info(FString::Printf(TEXT("CU10 %s: loaded %d, %d cut line(s), desk %lld, held %lld (want %lld, %lld), reserved %lld"), Slot, bOk ? 1 : 0, Cuts.Num(), Back ? Back->Amount : -1, H, WantDesk, WantHeld, Sim->GetMarkReservedMmk())); }
				return bAll;
			};
			ExpectSeq({
				[&]() { return bShapeC && Held > 10000; },
				[&]() { return CutRun(TEXT("cu10c1"), Held + 50000, 50000, Held); },
				[&]() { return CutRun(TEXT("cu10c2"), Held - 10000, 0, Held - 10000); },
				}, FString::Printf(TEXT("CU10 R4's cut order in the mark purse: with Zarya's order holding %s marks and an exchange of 100 marks pending, a balance 50 marks short cuts the desk order alone (to 50); 110 short drops the desk order and then cuts convoy #%d by 10, each with its Warning in that order"),
					*Cr(Held), MarkId));
			// Two mark orders (Zarya's, then one to CuIn): the mark-order list reads back ascending by convoy Id, as does the desk list.
			LoadSlot(TEXT("cu10cut"));
			const bool bSecond = Send(TEXT("MarketOrder"), FName("CuIn"), TEXT("Ice:50"));
			Sim->SaveColony(TEXT("cu10two"), SaveErr);
			TArray<uint8> WT;
			FFileHelper::LoadFileToArray(WT, *SaveFilePath(TEXT("cu10two")));
			FCurSuiteBlock BT;
			const bool bParsedT = CurSuiteParse(WT, BT);
			ExpectSeq({
				[&]() { return bSecond && bParsedT; },
				[&]() { return BT.Marks.Num() == 2 && BT.Marks[0].Id < BT.Marks[1].Id && BT.Marks[0].Id == MarkId; },
				[&]() { return BT.Desks.Num() == 2 && BT.Desks[0].Id < BT.Desks[1].Id; },
				}, FString::Printf(TEXT("CU10 with two mark orders out the mark-order list reads back ascending by convoy Id (#%d, #%d), and the desk list by order Id (#%d, #%d)"),
					BT.Marks.Num() > 0 ? BT.Marks[0].Id : -1, BT.Marks.Num() > 1 ? BT.Marks[1].Id : -1, BT.Desks.Num() > 0 ? BT.Desks[0].Id : -1, BT.Desks.Num() > 1 ? BT.Desks[1].Id : -1));
		}
		// R7 O8's exception, on the two consistent files every save in the window between an issue act and its issue roll is (trap 9: silent
		// when consistent). (a) A stand-alone Issue saved before its roll: Earth's flag up, no market issue, the Issue entry pending - it loads
		// with no Warning, still issued, and the next roll issues the market at 1000. (b) DefyIssue and a Comply in one sol, saved before the roll:
		// the withdrawal pending, no market issue, the Issue entry pending - it loads with no Warning, the withdrawal still pending, and the roll
		// issues the market and withdraws it at 1000.
		{
			Reset();
			Sim->Debug_SetRequisitionInputs(20.0, 10.0);
			Sim->Debug_SyncPoliticalView();
			const bool bSent = Send(TEXT("Currency"), FName("Issue"));
			const int32 SA = Sim->GetPoliticalJournal().Num() == 1 ? Sim->GetPoliticalJournal()[0].ClearSol : -1;
			const bool bWindow = bSent && Sim->IsCurrencyIssued() && Sim->GetCurrencyIssueSol() == -1 && SA == Sim->GetMarketCheckedSol();
			Sim->SaveColony(TEXT("cu10o8a"), SaveErr);
			Capture.Clear();
			FString E;
			const bool bOk = Sim->LoadColony(TEXT("cu10o8a"), E) && E.IsEmpty();
			const int32 Warned = Capture.NumWarnings();
			const bool bLoadedA = Sim->IsCurrencyIssued() && Sim->GetCurrencyIssueSol() == -1 && Sim->GetPoliticalJournal().Num() == 1 && Sim->GetPoliticalJournal()[0].Kind == (uint8)EAct::Issue
				&& Sim->GetRequisitionMultiplier() == RefMult(Sim->GetIdentityAxis(), Sim->GetEarthTension(), true);
			if (Warned > 0) { Info(FString::Printf(TEXT("CU10 cu10o8a: %d warnings: %s"), Warned, *FString::Join(Capture.Warnings, TEXT(" | ")))); }
			RollNext();
			ExpectSeq({
				[&]() { return bWindow; },
				[&]() { return bOk && Warned == 0 && Capture.NumErrors() == 0; },
				[&]() { return bLoadedA; },
				[&]() { return Sim->GetCurrencyIssueSol() == SA && Sim->GetCurrencyOpenSol() == SA + 2 && Sim->GetRate() == 1000 && Sim->IsCurrencyIssued() && Sim->GetPoliticalJournal().Num() == 0; },
				[&]() { return Capture.CountLines(*FString::Printf(TEXT("Currency: the market issues the mark at the end of sol %d"), SA)) == 1; },
				}, FString::Printf(TEXT("CU10 a stand-alone Issue saved before its roll (Earth's flag up, no market issue, the Issue entry of sol %d pending) loads with no Warning (%d), still issued with Earth's penalty and the entry kept, and the next roll issues the market at 1000 for sol %d"),
					SA, Warned, SA + 2));
		}
		{
			Reset();
			Sim->Debug_SetRequisitionInputs(0.0, 70.0);
			EraOnce();
			const bool bDI = Send(TEXT("Solidarity"), FName("DefyIssue"));
			EraOnce();
			const bool bC = Send(TEXT("Solidarity"), FName("Comply"));
			const TArray<FRHPoliticalEntry> Four = Sim->GetPoliticalJournal();
			const bool bWindow = bDI && bC && Four.Num() == 4 && Four[0].ClearSol == Four[3].ClearSol && Four[1].Kind == (uint8)EAct::Issue
				&& !Sim->IsCurrencyIssued() && Sim->IsCurrencyWithdrawPending() && Sim->GetCurrencyIssueSol() == -1;
			Sim->SaveColony(TEXT("cu10o8b"), SaveErr);
			Capture.Clear();
			FString E;
			const bool bOk = Sim->LoadColony(TEXT("cu10o8b"), E) && E.IsEmpty();
			const int32 Warned = Capture.NumWarnings();
			const TArray<FRHPoliticalEntry> Back = Sim->GetPoliticalJournal();
			const bool bLoadedB = !Sim->IsCurrencyIssued() && Sim->IsCurrencyWithdrawPending() && Sim->GetCurrencyIssueSol() == -1 && Back.Num() == 4 && Back[1].Kind == (uint8)EAct::Issue
				&& Sim->GetRequisitionMultiplier() == RefMult(Sim->GetIdentityAxis(), Sim->GetEarthTension(), false);
			if (Warned > 0) { Info(FString::Printf(TEXT("CU10 cu10o8b: %d warnings: %s"), Warned, *FString::Join(Capture.Warnings, TEXT(" | ")))); }
			Sim->Debug_GrantMarks(1234567);
			const int64 Credit0 = Sim->GetCreditBalanceMcr();
			Sim->Debug_SetMarketEnabled(false);
			Capture.Clear();
			RollNext();
			Sim->Debug_SetMarketEnabled(true);
			ExpectSeq({
				[&]() { return bWindow; },
				[&]() { return bOk && Warned == 0 && Capture.NumErrors() == 0; },
				[&]() { return bLoadedB; },
				[&]() { return Sim->GetCurrencyRedeemedMcr() == 1234567 && Sim->GetCreditBalanceMcr() == Credit0 + 1234567 && Sim->GetMarkBalance() == 0; },
				[&]() { return Sim->GetCurrencyIssueSol() == -1 && !Sim->IsCurrencyWithdrawPending() && !Sim->IsCurrencyIssued() && Sim->GetPoliticalJournal().Num() == 0; },
				[&]() { return Capture.CountLines(TEXT("Currency: the market issues the mark")) == 1 && Capture.CountLines(TEXT("=== CURRENCY WITHDRAWN: 1234.567 marks redeemed for 1234.567 cr at 1 ===")) == 1; },
				}, FString::Printf(TEXT("CU10 DefyIssue and a Comply in one sol, saved before their roll (the withdrawal pending, no market issue, the Issue entry pending among four) load with no Warning (%d), the withdrawal still pending and the entries kept, and the roll issues the market and withdraws it at 1000: 1234.567 marks redeem for 1234.567 cr"),
					Warned));
		}
		RetireAll();
	}

	// --- CU11. Ruling 11 and Q4a: the rate forced to its floor or its ceiling every step, with or without a large mark balance, changes nothing
	// outside the currency over 10 sols: Hope, the population, Influence, every stock and shelf and the credit are bit-identical. ---
	{
		Reset();
		IssueOpen();
		SyncClock();
		Sim->SaveColony(TEXT("cu11"), SaveErr);
		const auto Run = [&](int64 Rate, int64 Marks) -> FString
		{
			LoadSlot(TEXT("cu11"));
			Sim->Debug_SetGrowthEnabled(true);
			Sim->Debug_GrantMarks(Marks);
			for (int32 K = 0; K < 10 * (int32)(SolLen / EraDt); ++K) { Sim->Debug_SetRate(Rate); EraOnce(); }
			FString D = FString::Printf(TEXT("hope %.17g pop %d inf %.17g hn %.17g cr %lld/%lld/%lld districts %d"), Sim->GetHopeSmoothed(), Sim->GetPopulation(), Sim->GetInfluence(), Sim->GetHumanNatureAxis(),
				Sim->GetCreditBalanceMcr(), Sim->GetCreditAwardedMcr(), Sim->GetCreditSpentMcr(), Sim->GetDistricts().Num());
			DefsSub->ForEachResourceRow([&](FName N, const FRHResourceRow&) { D += FString::Printf(TEXT(" %s=%.17g/%.17g"), *N.ToString(), Sim->GetStock(N), Sim->GetTotalSolid(N)); });
			for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { D += FString::Printf(TEXT(" #%d%s"), Bd.Id, *Bd.DefName.ToString()); }
			return D;
		};
		const FString Par = Run(1000, 0), Floor = Run(250, 0), Ceil = Run(4000, 0), Rich = Run(250, 1000000000000LL);
		ExpectSeq({
			[&]() { return Par == Floor; },
			[&]() { return Par == Ceil; },
			[&]() { return Par == Rich; },
			[&]() { return Par.Len() > 200; },
			}, FString::Printf(TEXT("CU11 10 sols with the rate forced to 250, to 4000 or left at parity, and with a billion marks held: Hope, the population, Influence, HumanNature, the credit, the districts and every stock and shelf are bit-identical (%d chars)"), Par.Len()));
	}

	// --- CU12. Politics untouched: twins from one save, a plain Defy and a DefyIssue, 25 sols on (before the issuing twin's tension reaches 75):
	// identity, HumanNature, relations and Influence equal (both did the Defy), the projected ending equal; tension and the multiplier differ by the
	// issue's +10 and -0.10 (-0.13 with the tension's own share). ---
	{
		Reset();
		Sim->Debug_SetRequisitionInputs(0.0, 60.3);
		EraOnce();
		Sim->SaveColony(TEXT("cu12"), SaveErr);
		struct FTw { double Id = 0, Hn = 0, Inf = 0, RelZ = 0, RelM = 0, Ten = 0, Mult = 0, Hope = 0; int32 End = 0, Pop = 0; bool bEmbargo = false, bIssued = false; };
		const auto Twin = [&](FName Target) -> FTw
		{
			LoadSlot(TEXT("cu12"));
			Send(TEXT("Solidarity"), Target);
			for (int32 K = 0; K < 25 * (int32)(SolLen / EraDt); ++K) { EraOnce(); }
			FTw T;
			T.Id = Sim->GetIdentityAxis(); T.Hn = Sim->GetHumanNatureAxis(); T.Inf = Sim->GetInfluence(); T.RelZ = Sim->GetRivalRelation(NZarya); T.RelM = Sim->GetRivalRelation(NMeridian);
			T.Ten = Sim->GetEarthTension(); T.Mult = Sim->GetRequisitionMultiplier(); T.Hope = Sim->GetHopeSmoothed(); T.End = (int32)Sim->GetProjectedEnding(); T.Pop = Sim->GetPopulation();
			T.bEmbargo = !Sim->GetEmbargoingRival().IsNone(); T.bIssued = Sim->IsCurrencyIssued();
			return T;
		};
		const FTw P = Twin(FName("Defy")), I = Twin(FName("DefyIssue"));
		ExpectSeq({
			[&]() { return I.bIssued && !P.bIssued; },
			[&]() { return I.Id == P.Id && I.Hn == P.Hn && I.Inf == P.Inf && I.RelZ == P.RelZ && I.RelM == P.RelM; },
			[&]() { return I.End == P.End && I.Pop == P.Pop && I.Hope == P.Hope; },
			[&]() { return FMath::Abs((I.Ten - P.Ten) - 10.0) < 1.0e-9; },
			[&]() { return FMath::Abs((P.Mult - I.Mult) - 0.13) < 1.0e-9; },
			[&]() { return !I.bEmbargo && !P.bEmbargo && I.Ten < 75.0; },
			}, FString::Printf(TEXT("CU12 25 sols after a plain Defy and a DefyIssue from one save: identity %+.0f, HumanNature, relations (%.0f), Influence, Hope, the population and the projected ending equal; tension %.4f against %.4f (+10), the multiplier %.4f against %.4f (-0.13); no embargo in either"),
				I.Id, I.RelZ, I.Ten, P.Ten, I.Mult, P.Mult));
	}

	// --- CU8. Era parity, last (it injects a rival of its own). The paired run of R1 with re-review conditions 1, 2, 3 and 7: Zarya and an
	// injected Kestrel (30 km: a 1-sol round trip; stance 40; relation 10, so Earth's embargo picks Zarya), the start save at sol 16.0 by the real
	// catch-up roll (no storm in [16, 36]), the record synced at load, then the same orders at the same sim times in both bands: the era twin's
	// steps end 30 s past each minute, so an order due just after a boundary runs on the step that crosses it, BEFORE its roll. Every order is
	// enqueued on that grid (ExecuteAt = enqueue + 45 s, the same in both twins). At every threshold from 16 to 36 the rate, both views, the
	// members, every slot's price, every economy rival's store of every slot, every fill, every charge in either currency, every desk payout and
	// the bloc tally are identical; so are the end balances and the redeemed credit; and the withdrawal roll (closing 34) sees Zarya's last
	// convoy home in the era twin and on the road in the agent twin. The schedule keeps R7's three rules, each asserted at the moment of booking
	// (OnCommandExecuted, after the amount is set aside): 1 home a sol before the next order to the same rival; 2 every reservation - convoy or
	// desk - at most half its purse's spendable before it was set aside; 3 a mark reservation sized against the marks held without any convoy
	// home or due home within a sol (what that reads is band-exact, so it is compared across the twins too). Zarya's sol-25 order is a net
	// sale, so the sol-28 exchange of 200 marks reads 504.682 marks held (39.6%). The table's own expectations are asserted: each order's
	// currency, the embargo and the third demand at 30.0, the record blind to the +25, the Pacify's -20000 at the roll closing 30 with the
	// demand still pending. ---
	{
		Reset();
		Inject(FName("Kestrel"), 30.0, TEXT("Ice:100;Regolith:50"), TEXT(""), TEXT("Ice:1000;Regolith:3000"), TEXT("Ice:5000;Regolith:10000"), 40, 10.f);
		Sim->Debug_SyncRivalEconomies();
		Sim->AddStock(NHyd, 1000.0);
		Sim->AddStock(NParts, 40.0);
		Sim->AddStock(NIce, 4000.0);
		Sim->AddStock(NStruct, 2000.0);
		Clock->Debug_SetSimSeconds(16.0 * SolLen - EraDt);
		EraOnce(); // the step ends at exactly sol 16.0: the catch-up roll closes sols 0-15 here, before the save (re-review observation 1)
		FString Err;
		const bool bSaved = Sim->SaveColony(TEXT("cu8start"), Err);
		ExpectSeq({
			[&]() { return bSaved; },
			[&]() { return Clock->GetSimSecondsTotal() == 16.0 * SolLen; },
			[&]() { return Sim->GetMarketCheckedSol() == 16; },
			[&]() { return Sim->GetConvoys().Num() == 0; },
			[&]() { return Sim->GetRivalEconomyCount() == 3; },
			}, FString::Printf(TEXT("CU8 the start save 'cu8start': Kestrel, Meridian and Zarya with economies, the clock at sol 16.0 exactly, the market open at sol %d by the real roll, nothing out (%s)"), Sim->GetMarketCheckedSol(), *Err));
		const FName NKestrel(TEXT("Kestrel"));
		struct FEvt { double T; const TCHAR* Verb; FName Target; const TCHAR* Text; double Value; int32 Harness; };
		const auto At = [&](double Sol) { return Sol * SolLen; };
		// Enqueue times on the era grid (30 s past a minute): ".5" orders at x.5 sols - 30 s; boundary orders 30 s before the boundary (ExecuteAt 15 s after it).
		const TArray<FEvt> Evts = {
			{ At(19.5) - 30.0, TEXT("Solidarity"), FName("DefyIssue"), TEXT(""), 0.0, 0 },
			{ At(20.0) - 30.0, TEXT("Exchange"), FName("ToMarks"), TEXT(""), 50.0, 0 },
			{ At(22.5) - 30.0, TEXT("Exchange"), FName("ToMarks"), TEXT(""), 500.0, 0 },
			{ At(23.5) - 30.0, TEXT("MarketOrder"), NKestrel, TEXT("Ice:100"), 0.0, 0 },
			{ At(25.5) - 30.0, TEXT("MarketOrder"), NZarya, TEXT("Struct:-60"), 0.0, 0 },
			{ At(26.0) - 30.0, TEXT("Solidarity"), FName("Defy"), TEXT(""), 0.0, 0 },
			{ At(27.0) - 30.0, TEXT("MarketOrder"), NKestrel, TEXT("Ice:-150"), 0.0, 0 },
			{ At(28.5) - 30.0, TEXT("Exchange"), FName("ToCredits"), TEXT(""), 200.0, 0 },
			{ At(29.5) - 30.0, TEXT("MarketOrder"), NKestrel, TEXT("Ice:-100"), 0.0, 0 },
			{ At(30.0) + 30.0, TEXT(""), NAME_None, TEXT(""), 0.0, 1 },
			{ At(30.5) - 30.0, TEXT("Pacify"), NZarya, TEXT(""), 0.0, 0 },
			{ At(31.0) - 30.0, TEXT("MarketOrder"), NZarya, TEXT("Struct:-100"), 0.0, 0 },
			{ At(34.5) - 30.0, TEXT("Solidarity"), FName("Comply"), TEXT(""), 0.0, 0 } };
		struct FRun
		{
			FString Rolls, Clearings, Desks, Returns, Books, Refusals, Currencies, Rules, MarksHeld;
			int64 Credit = 0, Marks = 0, Redeemed = 0, Awarded0 = 0, Sales0 = 0, Desk0 = 0;
			int32 Booked = 0, Refused = 0, RollCount = 0, Storms = 0, ZaryaLastHome = -1, WithdrawRoll = -1, KestrelFrom = -1, ZaryaFrom = -1;
			int32 MarkBookings = 0, DeskBookings = 0;
			uint8 Kestrel27 = 255, Kestrel29 = 255;
			bool bRule2Ok = true, bRule3Ok = true, bHomeOk = true, bAwardFree = true;
			int64 Ten29 = -1, Ten30 = -1, TenAfterWrite = -1;
			double LiveBeforeWrite = -1.0, LiveAfterWrite = -1.0;
			bool bEmbargo30 = false, bDemand30 = false, bPacified = false, bPacifyKeepsDemand = false;
		};
		const auto RunTwin = [&](bool bAgent) -> FRun
		{
			LoadSlot(TEXT("cu8start"));
			Sim->Debug_SetRequisitionInputs(0.0, 59.0);
			Sim->Debug_SyncPoliticalView();
			Sim->Debug_GrantCredits(20000000);
			Sim->Debug_AddInfluence(40.0);
			FRun O;
			O.Awarded0 = Sim->GetCreditAwardedMcr();
			O.Sales0 = Sim->GetMarketSalesMcr();
			O.Desk0 = Sim->GetDeskCreditMcr();
			const int64 SalesPaid0 = Sim->GetSalesPaidMcr();
			int32 NextEvt = 0, ClearSeen = Sim->GetMarketClearings().Num(), DeskSeen = Sim->GetDeskClearings().Num(), RejSeen = Rejects.Num();
			int32 LastChecked = Sim->GetMarketCheckedSol();
			TArray<int32> PrevIds;
			TMap<FName, double> LastHome;
			TMap<int32, int64> OwedSeen;              // each convoy's owed marks at the last look (they are paid in at its return)
			TArray<TPair<double, int64>> HomeMarks;   // (when a convoy came home, the owed marks it paid in)
			bool bFirstEra = true, bLooked30 = false;
			int32 ZaryaLastId = -1;
			// R7's rules 2 and 3 at the moment of booking: the order is set aside (BookConvoy / BookExchange) and OnCommandExecuted follows at once.
			const FDelegateHandle ExecH = Sim->OnCommandExecuted.AddLambda([&](const FRHCommand& Cmd)
			{
				const double Now = Clock->GetSimSecondsTotal();
				int64 Recent = 0; // owed marks a convoy home within the last sol paid in: not counted as held (rule 3)
				for (const TPair<double, int64>& H : HomeMarks) { Recent += (Now - H.Key <= SolLen) ? H.Value : 0; }
				if ((Cmd.Verb == FName("MarketOrder") || Cmd.Verb == FName("Convoy")) && Sim->GetConvoys().Num() > 0)
				{
					const FRHConvoy& C = Sim->GetConvoys().Last();
					const int64 Res = C.bMarks ? C.ReservedMmk : C.ReservedMcr;
					const int64 Before = (C.bMarks ? Sim->GetSpendableMmk() : Sim->GetSpendableMcr()) + Res;
					O.bRule2Ok &= Res * 2 <= Before;
					O.Rules += FString::Printf(TEXT("#%d %s %lld of %lld; "), C.Id, C.bMarks ? TEXT("marks") : TEXT("cr"), Res, Before);
					if (C.bMarks)
					{
						++O.MarkBookings;
						O.bRule3Ok &= Res * 2 <= Before - Recent;
						O.MarksHeld += FString::Printf(TEXT("#%d %lld of %lld; "), C.Id, Res, Before - Recent);
					}
				}
				else if (Cmd.Verb == FName("Exchange"))
				{
					TArray<FRHDeskOrder> Desk;
					Sim->GetDeskOrders(Desk);
					const FRHDeskOrder* D = Desk.FindByPredicate([&Cmd](const FRHDeskOrder& X) { return X.Id == Cmd.CommandId; });
					if (D)
					{
						const bool bToMarks = D->Dir == (uint8)EDir::ToMarks;
						const int64 Before = (bToMarks ? Sim->GetSpendableMcr() : Sim->GetSpendableMmk()) + D->Amount;
						++O.DeskBookings;
						O.bRule2Ok &= D->Amount * 2 <= Before;
						O.Rules += FString::Printf(TEXT("desk %s %lld of %lld; "), bToMarks ? TEXT("cr") : TEXT("marks"), D->Amount, Before);
						if (!bToMarks)
						{
							++O.MarkBookings;
							O.bRule3Ok &= D->Amount * 2 <= Before - Recent;
							O.MarksHeld += FString::Printf(TEXT("desk %lld of %lld; "), D->Amount, Before - Recent);
						}
					}
					else
					{
						O.bRule2Ok = false;
					}
				}
				else if (Cmd.Verb == FName("Pacify"))
				{
					O.bPacified = true;
					O.bPacifyKeepsDemand = Sim->IsEarthDemandPending() && !Sim->IsEmbargoed(NZarya);
				}
			});
			while (Clock->GetSimSecondsTotal() < At(36.0) - 1.0e-6)
			{
				const double Now = Clock->GetSimSecondsTotal();
				while (NextEvt < Evts.Num() && Now >= Evts[NextEvt].T - 1.0e-6)
				{
					const FEvt& E = Evts[NextEvt];
					if (E.Harness == 1)
					{
						O.LiveBeforeWrite = Sim->GetEarthTension();
						Sim->Debug_AddTension(25.0);
						O.LiveAfterWrite = Sim->GetEarthTension();
						O.TenAfterWrite = Sim->GetTensionView();
					}
					else
					{
						FRHCommand C; C.Verb = FName(E.Verb); C.Target = E.Target; C.Text = E.Text; C.Value = E.Value;
						Sim->EnqueueCommand(C);
					}
					++NextEvt;
				}
				if (bAgent) { AgentOnce(); }
				else if (bFirstEra) { bFirstEra = false; Clock->Debug_AdvanceSimSeconds(30.0); Sim->Debug_SetBatteryWh(1.0e9); Sim->EraStep(30.f); }
				else { EraOnce(); }
				// Bookings, returns and refusals this step.
				const TArray<FRHConvoy>& Cv = Sim->GetConvoys();
				for (const FRHConvoy& C : Cv)
				{
					if (PrevIds.Contains(C.Id))
					{
						continue;
					}
					++O.Booked;
					const double BookedAt = Clock->GetSimSecondsTotal();
					O.Books += FString::Printf(TEXT("#%d %s clear %d %s; "), C.Id, *C.Rival.ToString(), C.ClearSol, C.bMarks ? TEXT("marks") : TEXT("credits"));
					// R7's rule 1: home a sol before the next order to the same rival (an order booked on the era twin's boundary step is exempt).
					const double* Home = LastHome.Find(C.Rival);
					const bool bBoundaryStep = FMath::Fmod(BookedAt, SolLen) < 61.0;
					O.bHomeOk &= !Home || bBoundaryStep || BookedAt - *Home >= SolLen;
					if (C.Rival == NKestrel && C.ClearSol == 27) { O.Kestrel27 = C.bMarks ? 1 : 0; }
					if (C.Rival == NKestrel && C.ClearSol == 29) { O.Kestrel29 = C.bMarks ? 1 : 0; }
					if (C.Rival == NZarya) { ZaryaLastId = C.Id; }
				}
				for (const int32 Id : PrevIds)
				{
					if (!Cv.ContainsByPredicate([Id](const FRHConvoy& C) { return C.Id == Id; }))
					{
						const int32 HomeSol = FMath::FloorToInt32(Clock->GetSimSecondsTotal() / SolLen);
						O.Returns += FString::Printf(TEXT("#%d home in sol %d; "), Id, HomeSol);
						if (Id == ZaryaLastId) { O.ZaryaLastHome = HomeSol; }
						// The rival of a convoy gone home, from the clearings record; and the owed marks it paid in at its return.
						const FRHMarketClearing* R = Sim->GetMarketClearings().FindByPredicate([Id](const FRHMarketClearing& K) { return K.ConvoyId == Id; });
						if (R) { LastHome.Add(R->Rival, Clock->GetSimSecondsTotal()); }
						if (OwedSeen.FindRef(Id) > 0) { HomeMarks.Emplace(Clock->GetSimSecondsTotal(), OwedSeen.FindRef(Id)); }
					}
				}
				PrevIds.Reset();
				OwedSeen.Reset();
				for (const FRHConvoy& C : Cv) { PrevIds.Add(C.Id); OwedSeen.Add(C.Id, C.bMarks ? C.OwedMmk : 0); }
				for (; RejSeen < Rejects.Num(); ++RejSeen) { ++O.Refused; O.Refusals += Rejects[RejSeen] + TEXT(" | "); }
				// The table at 30.0: a quarter sol after the +25 write (and before the Pacify), Earth embargoes Zarya and a third demand is pending.
				if (!bLooked30 && Clock->GetSimSecondsTotal() >= At(30.25) - 1.0e-6)
				{
					bLooked30 = true;
					O.bEmbargo30 = Sim->IsEmbargoed(NZarya) && Sim->GetEmbargoingRival() == NZarya;
					O.bDemand30 = Sim->IsEarthDemandPending();
				}
				if (Sim->GetMarketCheckedSol() != LastChecked)
				{
					LastChecked = Sim->GetMarketCheckedSol();
					++O.RollCount;
					const FRHEventRow* Ev = Sim->GetActiveEvent();
					O.Storms += (Ev && Ev->Type == FName("DustStorm")) ? 1 : 0;
					if (LastChecked == 30) { O.Ten29 = Sim->GetTensionView(); }
					if (LastChecked == 31) { O.Ten30 = Sim->GetTensionView(); }
					TArray<TPair<FName, int32>> Members;
					Sim->GetBlocMembers(Members);
					O.Rolls += FString::Printf(TEXT("[sol %d issue %d open %d rate %lld id %lld ten %lld members"), LastChecked, Sim->GetCurrencyIssueSol(), Sim->GetCurrencyOpenSol(), Sim->GetRate(), Sim->GetIdentityView(), Sim->GetTensionView());
					for (const TPair<FName, int32>& M : Members) { O.Rolls += FString::Printf(TEXT(" %s@%d"), *M.Key.ToString(), M.Value); }
					// Every slot's price (the withdrawal re-expresses the untraded ones too) and every economy rival's store of every slot.
					O.Rolls += TEXT(" prices");
					for (int32 Slot = 0; Slot < Sim->GetLedgerSlotCount(); ++Slot) { O.Rolls += FString::Printf(TEXT(" %s=%lld"), *Sim->GetLedgerSlotName(Slot).ToString(), Sim->GetMarketPriceMcr(Sim->GetLedgerSlotName(Slot))); }
					for (int32 r = 0; r < Sim->GetRivalEconomyCount(); ++r)
					{
						const FName Rv = Sim->GetRivalEconomyName(r);
						O.Rolls += FString::Printf(TEXT(" %s:"), *Rv.ToString());
						for (int32 Slot = 0; Slot < Sim->GetLedgerSlotCount(); ++Slot) { O.Rolls += FString::Printf(TEXT(" %lld"), Sim->GetRivalStoreG(Rv, Sim->GetLedgerSlotName(Slot))); }
					}
					O.Rolls += TEXT(" tally");
					for (int32 Back = 1; Back <= 5; ++Back) { int64 S = 0, B = 0; Sim->GetBlocTally(Back, S, B); O.Rolls += FString::Printf(TEXT(" %lld/%lld"), S, B); }
					O.Rolls += TEXT("]\n");
					for (; ClearSeen < Sim->GetMarketClearings().Num(); ++ClearSeen)
					{
						const FRHMarketClearing& K = Sim->GetMarketClearings()[ClearSeen];
						O.Clearings += FString::Printf(TEXT("[#%d %s clear %d closing %d cur %d rate %lld net %lld settled %lld paid %lld off %lld value %lld lines"), K.ConvoyId, *K.Rival.ToString(), K.ClearSol, K.ClosingSol, K.Currency, K.RateMcr, K.NetMcr, K.SettledMcr, K.PaidMcr, K.WrittenOffMcr, K.ValueMcr);
						for (const FRHConvoyLine& L : K.Lines) { O.Clearings += FString::Printf(TEXT(" %s %d/%d"), *L.Resource.ToString(), L.Kg, L.FilledKg); }
						O.Clearings += TEXT("]\n");
						O.Currencies += FString::Printf(TEXT("%s %d cur %d; "), *K.Rival.ToString(), K.ClearSol, K.Currency);
					}
					for (; DeskSeen < Sim->GetDeskClearings().Num(); ++DeskSeen)
					{
						const FRHDeskClearing& D = Sim->GetDeskClearings()[DeskSeen];
						O.Desks += FString::Printf(TEXT("[desk %d amount %lld paid %lld payout %lld rate %lld clear %d closing %d]\n"), D.Dir, D.Amount, D.Paid, D.Payout, D.RateMcr, D.ClearSol, D.ClosingSol);
					}
					if (O.WithdrawRoll < 0 && Sim->GetCurrencyRedeemedMcr() > 0) { O.WithdrawRoll = LastChecked - 1; }
					O.bAwardFree &= Sim->GetQuotaPhase() == ERHQuotaPhase::Open && Sim->GetSalesPaidMcr() == SalesPaid0;
					if (O.KestrelFrom < 0) { O.KestrelFrom = Sim->GetBlocMemberFromSol(NKestrel); }
					if (O.ZaryaFrom < 0) { O.ZaryaFrom = Sim->GetBlocMemberFromSol(NZarya); }
				}
			}
			Sim->OnCommandExecuted.Remove(ExecH);
			O.Credit = Sim->GetCreditBalanceMcr();
			O.Marks = Sim->GetMarkBalance();
			O.Redeemed = Sim->GetCurrencyRedeemedMcr();
			O.bAwardFree &= Sim->GetCreditAwardedMcr() - O.Awarded0 == (Sim->GetMarketSalesMcr() - O.Sales0) + (Sim->GetDeskCreditMcr() - O.Desk0) + O.Redeemed;
			return O;
		};
		const FRun Era = RunTwin(false);
		const FRun Agent = RunTwin(true);
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
		// Every convoy's home sol is equal but Zarya's last (re-review 7: home at 34.975 in the era twin and 35.008 in the agent twin, by design).
		const auto WithoutLast = [](const FString& R, int32 Sol) { return R.Replace(*FString::Printf(TEXT(" home in sol %d; "), Sol), TEXT(" home; ")); };
		const bool bSame = Era.Rolls == Agent.Rolls && Era.Clearings == Agent.Clearings && Era.Desks == Agent.Desks && Era.Books == Agent.Books && Era.Credit == Agent.Credit
			&& Era.Marks == Agent.Marks && Era.Redeemed == Agent.Redeemed && Era.RollCount == Agent.RollCount && WithoutLast(Era.Returns, 34) == WithoutLast(Agent.Returns, 35);
		// The table: Kestrel's sol-23 and sol-27 orders converted (2), Zarya's sol-25 and sol-31 orders and Kestrel's sol-29 order in marks (1).
		const FString WantCurrencies = TEXT("Kestrel 23 cur 2; Zarya 25 cur 1; Kestrel 27 cur 2; Kestrel 29 cur 1; Zarya 31 cur 1; ");
		const auto Thirty = [](const FRun& R) { return R.Ten30 - R.Ten29 == 600 - 20000 && R.TenAfterWrite == R.Ten29 && R.LiveAfterWrite - R.LiveBeforeWrite > 24.9 && R.LiveAfterWrite >= 75.0; };
		Info(FString::Printf(TEXT("CU8 rules 2 and 3 at booking (reserved of spendable before it was set aside): era %s | agent %s ; the marks held without a convoy home within a sol, at each mark booking: era %s | agent %s ; the record at the rolls closing 29 and 30: era %lld -> %lld, agent %lld -> %lld (the live tension %.3f -> %.3f at the +25)"),
			*Era.Rules, *Agent.Rules, *Era.MarksHeld, *Agent.MarksHeld, Era.Ten29, Era.Ten30, Agent.Ten29, Agent.Ten30, Era.LiveBeforeWrite, Era.LiveAfterWrite));
		if (!bSame)
		{
			Info(FString::Printf(TEXT("CU8 diff: rolls %s ; clearings %s ; desks %s ; books %s | %s ; returns %s | %s ; credit %lld | %lld ; marks %lld | %lld ; redeemed %lld | %lld ; refusals %s | %s"),
				*FirstDiff(Era.Rolls, Agent.Rolls), *FirstDiff(Era.Clearings, Agent.Clearings), *FirstDiff(Era.Desks, Agent.Desks), *Era.Books, *Agent.Books, *Era.Returns, *Agent.Returns,
				Era.Credit, Agent.Credit, Era.Marks, Agent.Marks, Era.Redeemed, Agent.Redeemed, *Era.Refusals, *Agent.Refusals));
		}
		ExpectSeq({
			[&]() { return Era.RollCount == 20 && Agent.RollCount == 20; },
			[&]() { return Era.Storms == 0 && Agent.Storms == 0; },
			[&]() { return Era.Booked == 5 && Agent.Booked == 5; },
			[&]() { return Era.Refused == 1 && Agent.Refused == 1 && Era.Refusals == TEXT("The desk opens at the end of sol 21 | ") && Agent.Refusals == Era.Refusals; },
			[&]() { return bSame; },
			[&]() { return Era.ZaryaFrom == 21 && Era.KestrelFrom == 28 && Agent.ZaryaFrom == 21 && Agent.KestrelFrom == 28; },
			[&]() { return Era.Kestrel27 == 0 && Agent.Kestrel27 == 0 && Era.Kestrel29 == 1 && Agent.Kestrel29 == 1; },
			[&]() { return Era.WithdrawRoll == 34 && Agent.WithdrawRoll == 34 && Era.ZaryaLastHome == 34 && Agent.ZaryaLastHome == 35; },
			[&]() { return Era.Redeemed > 0 && Era.Marks == 0; },
			[&]() { return Era.bHomeOk && Agent.bHomeOk; },
			[&]() { return Era.bRule2Ok && Agent.bRule2Ok && Era.DeskBookings == 2 && Agent.DeskBookings == 2; },
			[&]() { return Era.bRule3Ok && Agent.bRule3Ok && Era.MarkBookings == 4 && Agent.MarkBookings == 4 && Era.MarksHeld == Agent.MarksHeld; },
			[&]() { return Era.Currencies == WantCurrencies && Agent.Currencies == WantCurrencies; },
			[&]() { return Era.bEmbargo30 && Agent.bEmbargo30 && Era.bDemand30 && Agent.bDemand30; },
			[&]() { return Thirty(Era) && Thirty(Agent); },
			[&]() { return Era.bPacified && Agent.bPacified && Era.bPacifyKeepsDemand && Agent.bPacifyKeepsDemand; },
			[&]() { return Era.bAwardFree && Agent.bAwardFree; },
			}, FString::Printf(TEXT("CU8 the paired run, sols 16-36, no storm: the DefyIssue issues at the roll closing 19 (open 21, Zarya from 21), the exchange due just after the sol-20 boundary is refused in both bands ('%s'), Kestrel joins at the roll closing 26 from 28 (its sol-27 order books in credits, its sol-29 order in marks, in both), each order settles as the table says (%s), and at all %d thresholds the rate, both views, the members, every slot's price, every economy rival's store, every fill, every charge in either currency, every desk payout and the bloc tally are identical; at 30.0 Earth embargoes Zarya and a third demand is pending, the record does not see the +25 (%lld at the roll closing 29, %lld just after the write) and takes 600 - 20000 at the roll closing 30 (%lld), and the Pacify leaves the demand pending; the withdrawal at the roll closing 34 redeems %s cr in both, Zarya's last convoy home in sol %d (era) and %d (agent); the end balances (%s cr, %s marks) are equal; R7's three rules hold at every booking (every convoy home a sol before the next order to its rival; every reservation, convoy or desk, at most half its purse's spendable; every mark reservation at most half the marks held without a convoy home within a sol, which reads the same in both bands); no award, launch or sale lands"),
				*Era.Refusals.LeftChop(3), *Era.Currencies.LeftChop(2), Era.RollCount, Era.Ten29, Era.TenAfterWrite, Era.Ten30, *Cr(Era.Redeemed), Era.ZaryaLastHome, Agent.ZaryaLastHome, *Cr(Era.Credit), *Cr(Era.Marks)));
		RetireAll();
	}

	GLog->RemoveOutputDevice(&Capture);
	Sim->OnCommandRejected.Remove(RejectH);
	Sim->OnAlert.Remove(AlertH);
	return Finish();
}
