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
#include "Misc/Crc.h"
#include "RHSaveFrame.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/NameAsStringProxyArchive.h"
#include "Algo/AnyOf.h"

// E6 the levers self-test (docs/qa/2026-10-24/e6-levers-design.md: DIRECTOR ANSWER Q-A (a), Q-B (a), Q-C (a) > the queue's ORCHESTRATOR-RULINGS
// and CROSS-CHECK > E6-brief.md > REFRESH > REVISION R1-R10 > re-review > review > design). Fails loudly. `-levers`. No path logs an Error on
// purpose, and a file's size is printed only in the masked "(N bytes" form.
//  LV0 the data, the clamps (the table, and InitLevers itself on out-of-range rows) and the one cross-row rule; LV2 the migrated sequences of
//  -covert, -espionage, -preemptive and -solidarity replayed with hard checks, and the drift timeline -icelow and -arrivals share; LV1 inertness
//  (the switch on against off, both bands, on one composite script of the four suites' acts) and the switch's four gates read one by one; LV3
//  precedence, and the list's retirement roll by roll; LV4 tariffs by hand (credits, marks and converted credits); LV5 pacts; LV6 sabotage moves prices (hand-computed integers) and a
//  repeated sabotage's timers; LV7 thefts and offerings (a barter rival's clean op and launder record nothing); LV9 the save and every load
//  repair; LV10 ruling 11; LV11 no pump; LV8 era parity, last (it injects a rival of its own). LV12 is the mutation list, run by hand and
//  recorded in design-decisions.
// The integers LV6 asserts were computed by a separate model of E4's update with E6's cut (written from the specs, not from the C++; it
// reproduces MX2's set-A Ice and Water steps on the first roll); the review's covert U values (Zarya at attempt 0, 0.907; Kestrel at 1, 0.727)
// are asserted from the roll's own formula, and LV8 ties them to the real rolls' outcomes (two attempts, both clean).

namespace
{
	// Captures LogRedHopeSim lines for the "logs nothing" and "logs this Warning" checks.
	struct FLevSuiteCapture : public FOutputDevice
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

	// The levers block read straight off a save's bytes with every field's offset, so a test can doctor one field and reseal the frame.
	struct FLevSuiteBlock
	{
		bool bOk = false;
		int32 FrameAt = INDEX_NONE, PayloadAt = 0, PayloadBytes = 0;
		uint8 FormatRev = 0;
		int32 FormatRevAt = 0, Next = 0, NextAt = 0, RuleCountAt = 0;
		struct FRule
		{
			int32 Id = 0, IdAt = 0, KindAt = 0, OriginAt = 0, PartyAt = 0, ResAt = 0, DirAt = 0, MarkupAt = 0, FromAt = 0, UntilAt = 0, LiveSolsAt = 0, LiveAt = 0;
			uint8 Kind = 0, Origin = 0, Dir = 0, Live = 0;
			FString Party, Res;
			int32 Markup = 0, From = 0, Until = 0;
			double LiveSols = 0.0;
		};
		TArray<FRule> Rules;
		int32 EffectCountAt = 0;
		struct FEffect
		{
			int32 ClearSol = 0, ClearSolAt = 0, CommandId = 0, CommandIdAt = 0, Sub = 0, SubAt = 0, KindAt = 0, PartyAt = 0, ResAt = 0, GramsAt = 0;
			uint8 Kind = 0;
			FString Party, Res;
			int64 Grams = 0;
		};
		TArray<FEffect> Effects;
		int64 PaidMcr = 0, PaidMmk = 0;
		int32 PaidMcrAt = 0, PaidMmkAt = 0, TermsCountAt = 0;
		struct FTerms
		{
			int32 ConvoyId = 0, ConvoyIdAt = 0, EffSol = 0, EffSolAt = 0, LineCount = 0, LineCountAt = 0;
			TArray<int32> Markups, MarkupAt;
		};
		TArray<FTerms> Terms;
	};

	bool LevSuiteParse(const TArray<uint8>& W, FLevSuiteBlock& B)
	{
		B = FLevSuiteBlock();
		int32 Frame = INDEX_NONE, Bytes = 0;
		if (!RHSaveFrame::Find(W, RHSaveFrame::LeversTag, Frame, Bytes))
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
		B.FormatRevAt = Cur; B.FormatRev = R8();
		B.NextAt = Cur; B.Next = R32();
		B.RuleCountAt = Cur;
		const int32 RuleCount = R32();
		for (int32 r = 0; r < RuleCount && r < 4096 && !bShort; ++r)
		{
			FLevSuiteBlock::FRule R;
			R.IdAt = Cur; R.Id = R32();
			R.KindAt = Cur; R.Kind = R8();
			R.OriginAt = Cur; R.Origin = R8();
			R.Party = RStr(R.PartyAt);
			R.Res = RStr(R.ResAt);
			R.DirAt = Cur; R.Dir = R8();
			R.MarkupAt = Cur; R.Markup = R32();
			R.FromAt = Cur; R.From = R32();
			R.UntilAt = Cur; R.Until = R32();
			R.LiveSolsAt = Cur; R.LiveSols = RD();
			R.LiveAt = Cur; R.Live = R8();
			B.Rules.Add(R);
		}
		B.EffectCountAt = Cur;
		const int32 EffectCount = R32();
		for (int32 e = 0; e < EffectCount && e < 4096 && !bShort; ++e)
		{
			FLevSuiteBlock::FEffect Fx;
			Fx.ClearSolAt = Cur; Fx.ClearSol = R32();
			Fx.CommandIdAt = Cur; Fx.CommandId = R32();
			Fx.SubAt = Cur; Fx.Sub = R32();
			Fx.KindAt = Cur; Fx.Kind = R8();
			Fx.Party = RStr(Fx.PartyAt);
			Fx.Res = RStr(Fx.ResAt);
			Fx.GramsAt = Cur; Fx.Grams = R64();
			B.Effects.Add(Fx);
		}
		B.PaidMcrAt = Cur; B.PaidMcr = R64();
		B.PaidMmkAt = Cur; B.PaidMmk = R64();
		B.TermsCountAt = Cur;
		const int32 TermsCount = R32();
		for (int32 t = 0; t < TermsCount && t < 4096 && !bShort; ++t)
		{
			FLevSuiteBlock::FTerms T;
			T.ConvoyIdAt = Cur; T.ConvoyId = R32();
			T.EffSolAt = Cur; T.EffSol = R32();
			T.LineCountAt = Cur; T.LineCount = R32();
			for (int32 l = 0; l < T.LineCount && l < 64 && !bShort; ++l) { T.MarkupAt.Add(Cur); T.Markups.Add(R32()); }
			B.Terms.Add(T);
		}
		B.bOk = !bShort && Cur == End;
		return B.bOk;
	}

	// A frame rebuilt around a new payload (header, CRC and footer rewritten for its new length); everything after the frame is kept.
	bool LevSuiteReframe(TArray<uint8>& File, uint32 Tag, const TArray<uint8>& NewPayload)
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

	// The seeded LCG of MX5 and CU2b.
	struct FLevSuiteLcg
	{
		uint64 State = 0x2545F4914F6CDD1DULL;
		uint32 Next() { State = State * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32)(State >> 33); }
		int64 Range(int64 Lo, int64 Hi) { return Lo + (int64)(Next() % (uint64)(Hi - Lo + 1)); }
	};

	// The covert roll's U at an attempt, by the roll's own formula (RollCovertDetected): a content hash of the name with the attempt counter.
	double LevSuiteCovertU(const TCHAR* Rival, int32 Attempt)
	{
		const uint32 Seed = HashCombine(FCrc::StrCrc32(Rival), (uint32)(Attempt * 2654435761u));
		return (Seed % 100000u) / 100000.0;
	}

	bool LevSuiteHas(const TArray<uint8>& Hay, int32 Limit, const TArray<uint8>& Needle)
	{
		for (int32 At = 0; Needle.Num() > 0 && At + Needle.Num() <= Limit && At + Needle.Num() <= Hay.Num(); ++At)
		{
			if (FMemory::Memcmp(Hay.GetData() + At, Needle.GetData(), Needle.Num()) == 0) { return true; }
		}
		return false;
	}
}

int32 RHRunLeversSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== LEVERS TEST (E6, 2026-10-28) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("LEVERS ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("LEVERS FAIL: %s"), *What); ++Fails; }
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
	const auto Info = [](const FString& What) { UE_LOG(LogRedHopeSim, Display, TEXT("LEVERS info: %s"), *What); };
	const auto Near = [](double A, double B, double Tol = 1.0e-6) { return FMath::Abs(A - B) <= Tol; };
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	const double DtSols = (double)URHSimClockSubsystem::EraStepSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds; // StepEarth's, per era step
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	using ELeg = URHSimWorldSubsystem::ERHLedgerLeg;
	using EKind = ERHTradeRuleKind;
	using EOrigin = ERHTradeRuleOrigin;
	using EAct = URHSimWorldSubsystem::ERHPoliticalAct;
	const FName NZarya(TEXT("Zarya")), NMeridian(TEXT("Meridian")), NKestrel(TEXT("Kestrel"));
	const FName NStruct(TEXT("Struct")), NWater(TEXT("Water")), NOx(TEXT("Oxygen")), NHyd(TEXT("Hydrogen")), NIce(TEXT("Ice")), NFood(TEXT("Food")), NParts(TEXT("SpareParts"));
	const TArray<FName> ZaryaSlots = { NIce, NWater, NOx, NStruct, NHyd };
	const TArray<FName> TradedSlots = { NIce, NWater, NOx, NStruct, NHyd, NFood, NParts };
	// HEAD's four refusal texts, written here (not read from the build), and the cargo refusal.
	const FString HeadClosed = TEXT("The route to Zarya Station is closed - you cut ties when you complied with Earth");
	const FString HeadDisrupted = TEXT("Zarya Station's production is disrupted (sabotage) - no trade until they recover");
	const FString HeadSevered = TEXT("Zarya Station has sided with Earth - relations are frozen, no trade");
	const FString HeadEmbargo = TEXT("Zarya Station has embargoed you under Earth pressure - reassure them first");
	const auto Cr = [](int64 Mcr) { return URHSimWorldSubsystem::FormatCredits(Mcr); };
	const auto Finish = [&]() -> int32
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("=== LEVERS TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto EraOnce = [&]() { Clock->Debug_AdvanceSimSeconds(EraDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->EraStep((float)EraDt); };
	const auto AgentOnce = [&]() { Clock->Debug_AdvanceSimSeconds((double)AgentDt); Sim->Debug_SetBatteryWh(1.0e9); Sim->Debug_StepSim(AgentDt); };
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	FLevSuiteCapture Capture;
	GLog->AddOutputDevice(&Capture);
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	// Every alert with the clock it was raised at (LV2 reads the step a timer ran out on).
	TArray<TPair<double, FString>> Alerts;
	const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts, Clock](const FString& Text) { Alerts.Emplace(Clock->GetSimSecondsTotal(), Text); });
	const auto AlertAt = [&Alerts](const TCHAR* Head, int32 Nth) -> double { int32 N = 0; for (const TPair<double, FString>& A : Alerts) { if (A.Value.StartsWith(Head) && N++ == Nth) { return A.Key; } } return -1.0; };
	FString SaveErr;
	TArray<FName> Injected;
	const auto Inject = [&](FName Name, double Km, const FString& Produce, const FString& Consume, const FString& Start, const FString& Cap, const FString& ExportLot, const FString& ImportLot, int32 Stance, float Relation)
	{
		FRHRivalRow Row;
		Row.DisplayName = Name.ToString() + TEXT(" Post");
		Row.Nation = TEXT("Test");
		Row.DistanceKm = (float)Km;
		Row.ExportLot = ExportLot;
		Row.ImportLot = ImportLot;
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
	Expect(Sim->SaveColony(TEXT("leversbase"), SaveErr), FString::Printf(TEXT("LV the fresh colony is saved as the base every fixture loads (%s)"), *SaveErr));
	// bFixture: growth off and every other harness switch at its default (the fixtures' world); otherwise every switch at its default, growth
	// on: HEAD's suites' world, for LV2's replays and LV10.
	const auto LoadSlot = [&](const TCHAR* Slot, bool bFixture)
	{
		FString E;
		const bool bOk = Sim->LoadColony(Slot, E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("LEVERS FAIL: loading %s failed: %s"), Slot, *E); ++Fails; }
		Sim->Debug_SetGrowthEnabled(!bFixture);
		Sim->Debug_SetEarthTakesQuota(true);
		Sim->Debug_SetMarketEnabled(true);
		Sim->Debug_SetMarketRivalOnly(false);
		Sim->Debug_SetCurrencyEnabled(true);
		Sim->Debug_SetLeversEnabled(true);
		Rejects.Reset();
		Alerts.Reset();
		return bOk;
	};
	const auto Fresh = [&]() { return LoadSlot(TEXT("leversbase"), false); };
	const auto Reset = [&]() { return LoadSlot(TEXT("leversbase"), true); };
	// HEAD's suites' own stepping: one-minute era steps, no battery top-up, (int32)(Sols x 20) of them.
	const auto RunSolsH = [&](double Sols)
	{
		const int32 StepsPerSolH = (int32)(SolLen / EraDt);
		for (int32 S = 0; S < (int32)(Sols * StepsPerSolH); ++S)
		{
			Clock->Debug_AdvanceSimSeconds(EraDt);
			Sim->EraStep((float)EraDt);
		}
	};
	const auto VerbH = [&](const TCHAR* V, FName Target)
	{
		FRHCommand Cmd; Cmd.Verb = FName(V); Cmd.Target = Target;
		Sim->EnqueueCommand(Cmd);
		RunSolsH(0.1);
	};
	// One order through the real uplink: transmitted, then one 60 s step (the 45 s lag is over), executed or refused.
	const auto Send = [&](const TCHAR* Verb, FName Target, const FString& Text = FString()) -> bool
	{
		const int32 Rej = Rejects.Num();
		FRHCommand C; C.Verb = FName(Verb); C.Target = Target; C.Text = Text;
		Sim->EnqueueCommand(C);
		EraOnce();
		return Rejects.Num() == Rej;
	};
	const auto Order = [&](FName Rival, const FString& Text) -> bool
	{
		const int32 Before = Sim->GetConvoys().Num();
		return Send(TEXT("MarketOrder"), Rival, Text) && Sim->GetConvoys().Num() == Before + 1;
	};
	const auto LastReject = [&]() { return Rejects.Num() > 0 ? Rejects.Last() : FString(); };
	const auto RollNext = [&]() { Sim->Debug_RollMarket(Sim->GetMarketCheckedSol() + 1); };
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
	const auto RulesOf = [&](EKind Kind, FName Party) { int32 N = 0; for (const FRHTradeRule& R : Sim->GetTradeRules()) { N += (R.Kind == Kind && (Party.IsNone() || R.Party == Party)) ? 1 : 0; } return N; };
	const auto RuleOf = [&](EKind Kind, FName Party) -> const FRHTradeRule* { return Sim->GetTradeRules().FindByPredicate([Kind, Party](const FRHTradeRule& R) { return R.Kind == Kind && R.Party == Party; }); };
	const auto SolOf = [](double SimSeconds) { return FMath::FloorToInt32(SimSeconds / SolLen); };
	const auto ActCmd = [&](const TCHAR* Verb, FName Target, double ExecuteAt)
	{
		FRHCommand C; C.Verb = FName(Verb); C.Target = Target; C.ExecuteAtSimSeconds = ExecuteAt; C.IssuedAtSimSeconds = ExecuteAt; C.CommandId = 900000 + Sim->GetNextRuleId();
		return C;
	};

	// --- LV0. The data: the 13 config rows read their CSV values from the live DT, the sim holds them, each clamp holds; SabotageMarketSols
	// equals SabotageDurationSols rounded, and TariffMaxBp is under 10000. ---
	{
		struct FCfg { const TCHAR* Name; double Value; };
		const FCfg Cfgs[] = { { TEXT("PactMinRelation"), 50.0 }, { TEXT("PactInfluenceCost"), 15.0 }, { TEXT("PactTension"), 5.0 }, { TEXT("PactHumanNatureShift"), 3.0 },
			{ TEXT("PactCargoKg"), 2000.0 }, { TEXT("PactBreakRelationHit"), 25.0 }, { TEXT("PactBreakHumanNatureShift"), 10.0 }, { TEXT("PactBreakTension"), 5.0 },
			{ TEXT("PactBetrayalTariffBp"), 2500.0 }, { TEXT("PactBetrayalTariffSols"), 30.0 }, { TEXT("TariffMaxBp"), 9000.0 }, { TEXT("SabotageMarketSols"), 6.0 },
			{ TEXT("SabotageCutPct"), 100.0 } };
		TArray<TPair<FName, double>> Held;
		Sim->GetLeversConfig(Held);
		int32 DtRight = 0, HeldRight = 0;
		FString Bad;
		for (const FCfg& C : Cfgs)
		{
			const bool bDt = DefsSub && DefsSub->GetConfigTableScalar(FName(C.Name), -999.0) == C.Value;
			const TPair<FName, double>* H = Held.FindByPredicate([&C](const TPair<FName, double>& P) { return P.Key == FName(C.Name); });
			const bool bHeld = H && H->Value == C.Value;
			DtRight += bDt ? 1 : 0;
			HeldRight += bHeld ? 1 : 0;
			if (!bDt || !bHeld) { Bad += FString(C.Name) + TEXT(" "); }
		}
		// The CSV equals the live DT on these rows.
		FString Csv;
		const bool bCsv = FFileHelper::LoadFileToString(Csv, *(FPaths::ProjectDir() / TEXT("docs/data/RH_Config.csv")));
		TArray<FString> Lines;
		Csv.ParseIntoArrayLines(Lines);
		int32 CsvRows = 0, CsvSame = 0;
		for (const FString& Line : Lines)
		{
			FString Name, Rest, Value, Tail;
			if (!Line.Split(TEXT(","), &Name, &Rest) || !Rest.Split(TEXT(","), &Value, &Tail))
			{
				continue;
			}
			if (!Algo::AnyOf(Cfgs, [&Name](const FCfg& C) { return Name == C.Name; }))
			{
				continue;
			}
			++CsvRows;
			CsvSame += DefsSub->GetConfigTableScalar(FName(*Name), -999.0) == FCString::Atod(*Value) ? 1 : 0;
		}
		const double Downtime = DefsSub->GetConfigTableScalar(FName("SabotageDurationSols"), -1.0);
		ExpectSeq({
			[&]() { return DtRight == 13; },
			[&]() { return HeldRight == 13; },
			[&]() { return bCsv && CsvRows == 13 && CsvSame == 13; },
			[&]() { return FMath::RoundToInt(Downtime) == 6 && DefsSub->GetConfigTableScalar(FName("SabotageMarketSols"), -1.0) == (double)FMath::RoundToInt(Downtime); },
			[&]() { return DefsSub->GetConfigTableScalar(FName("TariffMaxBp"), 99999.0) < 10000.0; },
			[&]() { return Sim->GetPactCargoKg() == 2000; },
			}, FString::Printf(TEXT("LV0 the 13 new RH_Config rows (50, 15, 5, 3, 2000, 25, 10, 5, 2500, 30, 9000, 6, 100) read their authored values from the live DT (%d of 13) and the sim holds them (%d of 13) %s; the CSV equals the DT on all %d; SabotageMarketSols is SabotageDurationSols (%.0f) rounded; TariffMaxBp is under 10000"),
				DtRight, HeldRight, *Bad, CsvRows, Downtime));
		struct FClamp { const TCHAR* Name; double In, Want; };
		const FClamp Clamps[] = {
			{ TEXT("SabotageMarketSols"), 0.0, 1.0 }, { TEXT("SabotageMarketSols"), 31.0, 30.0 }, { TEXT("SabotageMarketSols"), 6.4, 6.0 },
			{ TEXT("SabotageCutPct"), -5.0, 0.0 }, { TEXT("SabotageCutPct"), 101.0, 100.0 }, { TEXT("SabotageCutPct"), 49.6, 50.0 },
			{ TEXT("TariffMaxBp"), -1.0, 0.0 }, { TEXT("TariffMaxBp"), 10000.0, 9999.0 }, { TEXT("TariffMaxBp"), 25000.0, 9999.0 },
			{ TEXT("PactCargoKg"), 0.0, 1.0 }, { TEXT("PactCargoKg"), 2.0e6, 10000.0 }, { TEXT("PactCargoKg"), 10001.0, 10000.0 }, { TEXT("PactCargoKg"), 10000.0, 10000.0 },
			{ TEXT("PactBetrayalTariffBp"), -3.0, 0.0 }, { TEXT("PactBetrayalTariffBp"), 12000.0, 9999.0 },
			{ TEXT("PactBetrayalTariffSols"), 0.0, 1.0 }, { TEXT("PactBetrayalTariffSols"), 5000.0, 1000.0 },
			{ TEXT("PactMinRelation"), -1.0, 0.0 }, { TEXT("PactMinRelation"), 150.0, 100.0 },
			{ TEXT("PactInfluenceCost"), -2.0, 0.0 }, { TEXT("PactInfluenceCost"), 5000.0, 1000.0 },
			{ TEXT("PactTension"), 101.0, 100.0 }, { TEXT("PactBreakTension"), -1.0, 0.0 }, { TEXT("PactHumanNatureShift"), 200.0, 100.0 },
			{ TEXT("PactBreakHumanNatureShift"), -7.0, 0.0 }, { TEXT("PactBreakRelationHit"), 300.0, 100.0 } };
		int32 ClampRight = 0;
		FString ClampBad;
		for (const FClamp& C : Clamps)
		{
			const double Got = URHSimWorldSubsystem::LeversConfigClamp(FName(C.Name), C.In);
			ClampRight += Got == C.Want ? 1 : 0;
			if (Got != C.Want) { ClampBad += FString::Printf(TEXT("%s(%g)=%g; "), C.Name, C.In, Got); }
		}
		Expect(ClampRight == UE_ARRAY_COUNT(Clamps), FString::Printf(TEXT("LV0 each row's clamp holds on %d of %d probes (SabotageMarketSols 1..30, SabotageCutPct 0..100, TariffMaxBp 0..9999, PactCargoKg 1..10000 - inside E5's int64 rate bound -, the betrayal tariff 0..9999 bp and 1..1000 sols, the relation, tension and HumanNature rows 0..100, the Influence cost 0..1000) %s"),
			ClampRight, (int32)UE_ARRAY_COUNT(Clamps), *ClampBad));
		// The one cross-row rule: a betrayal tariff is never above TariffMaxBp. With a config of TariffMaxBp 2000 and PactBetrayalTariffBp 2500
		// the sim holds 2000, a Break makes its tariff at 2000 and the texts say 20%, and the colony saves, loads (no Warning: no load repair
		// rewrites the rule) and saves again byte-identical. The percentage is exact (never Bp / 100 truncated).
		Reset();
		Sim->Debug_SetTariffConfig(2000.0, 2500.0);
		const int32 HeldBp = Sim->GetPactBetrayalTariffBp();
		Sim->Debug_SetRelation(NZarya, 60.0);
		Sim->Debug_AddInfluence(40.0);
		Capture.Clear();
		const bool bSign = Send(TEXT("Treaty"), NZarya, TEXT("Sign"));
		const bool bBreak = Send(TEXT("Treaty"), NZarya, TEXT("Break"));
		const FRHTradeRule* T = RuleOf(EKind::Tariff, NZarya);
		const int32 RuleBp = T ? T->MarkupBp : -1;
		const bool bText = Capture.CountLines(TEXT("they charge 20% on every trade from sol")) == 1;
		FString E1, E2;
		const int32 Warn0 = Capture.NumWarnings();
		const bool bRound = Sim->SaveColony(TEXT("lv0cap"), E1) && Sim->LoadColony(TEXT("lv0cap"), E2) && E2.IsEmpty() && Sim->SaveColony(TEXT("lv0cap2"), E1);
		const int32 Warn1 = Capture.NumWarnings();
		TArray<uint8> CapA, CapB;
		const bool bSame = FFileHelper::LoadFileToArray(CapA, *SaveFilePath(TEXT("lv0cap"))) && FFileHelper::LoadFileToArray(CapB, *SaveFilePath(TEXT("lv0cap2"))) && CapA == CapB;
		Sim->Debug_SetTariffConfig(9000.0, 2500.0); // the authored config again (LV0's first check read it)
		const bool bBack = Sim->GetPactBetrayalTariffBp() == 2500;
		using M = URHSimWorldSubsystem;
		ExpectSeq({
			[&]() { return HeldBp == 2000; },
			[&]() { return bSign && bBreak && RuleBp == 2000; },
			[&]() { return bText; },
			[&]() { return bRound && Warn1 == Warn0 && bSame; },
			[&]() { return bBack; },
			[&]() { return M::FormatPercentBp(2500) == TEXT("25%") && M::FormatPercentBp(2550) == TEXT("25.5%") && M::FormatPercentBp(2505) == TEXT("25.05%") && M::FormatPercentBp(9000) == TEXT("90%") && M::FormatPercentBp(1) == TEXT("0.01%") && M::FormatPercentBp(0) == TEXT("0%"); },
			}, FString::Printf(TEXT("LV0 a config with TariffMaxBp 2000 below PactBetrayalTariffBp 2500: the sim holds %d, a Break's betrayal tariff is made at %d bp and its texts say 20%%; save, load (%d new Warnings) and save are byte-identical; the authored config is restored; a markup prints exactly (2500 25%%, 2550 25.5%%, 2505 25.05%%)"),
				HeldBp, RuleBp, Warn1 - Warn0));
		// InitLevers itself (the world start's read, not the clamp table alone): every row through its own clamp, then the cross-row rule. All 13
		// rows out of range or unrounded, with TariffMaxBp (1500.4) below PactBetrayalTariffBp (12000): the sim holds each clamped value and the
		// betrayal tariff at the cap; a plain InitLevers (no rows given) then reads the authored config back.
		{
			Reset();
			struct FWild { const TCHAR* Name; double In, Want; };
			const FWild Wild[] = { { TEXT("PactMinRelation"), 150.0, 100.0 }, { TEXT("PactInfluenceCost"), -2.0, 0.0 }, { TEXT("PactTension"), 101.0, 100.0 },
				{ TEXT("PactHumanNatureShift"), 200.0, 100.0 }, { TEXT("PactCargoKg"), 2.0e6, 10000.0 }, { TEXT("PactBreakRelationHit"), 300.0, 100.0 },
				{ TEXT("PactBreakHumanNatureShift"), -7.0, 0.0 }, { TEXT("PactBreakTension"), -1.0, 0.0 }, { TEXT("PactBetrayalTariffBp"), 12000.0, 1500.0 },
				{ TEXT("PactBetrayalTariffSols"), 5000.0, 1000.0 }, { TEXT("TariffMaxBp"), 1500.4, 1500.0 }, { TEXT("SabotageMarketSols"), 31.0, 30.0 },
				{ TEXT("SabotageCutPct"), 49.6, 50.0 } };
			TMap<FName, double> Rows;
			for (const FWild& Wd : Wild) { Rows.Add(FName(Wd.Name), Wd.In); }
			Sim->Debug_InitLeversWith(Rows);
			TArray<TPair<FName, double>> Wl;
			Sim->GetLeversConfig(Wl);
			int32 WildRight = 0;
			FString WildBad;
			for (const FWild& Wd : Wild)
			{
				const TPair<FName, double>* H = Wl.FindByPredicate([&Wd](const TPair<FName, double>& P) { return P.Key == FName(Wd.Name); });
				WildRight += (H && H->Value == Wd.Want) ? 1 : 0;
				if (!H || H->Value != Wd.Want) { WildBad += FString::Printf(TEXT("%s=%g (want %g); "), Wd.Name, H ? H->Value : -1.0, Wd.Want); }
			}
			const int32 WildBp = Sim->GetPactBetrayalTariffBp(), WildCap = Sim->GetPactCargoKg(), WildCut = Sim->GetSabotageCutPct();
			Sim->Debug_InitLeversWith(TMap<FName, double>());
			TArray<TPair<FName, double>> Back;
			Sim->GetLeversConfig(Back);
			int32 BackRight = 0;
			for (const FCfg& Cf : Cfgs)
			{
				const TPair<FName, double>* H = Back.FindByPredicate([&Cf](const TPair<FName, double>& P) { return P.Key == FName(Cf.Name); });
				BackRight += (H && H->Value == Cf.Value) ? 1 : 0;
			}
			ExpectSeq({
				[&]() { return WildRight == 13; },
				[&]() { return WildBp == 1500 && WildCap == 10000 && WildCut == 50; },
				[&]() { return BackRight == 13 && Sim->GetPactBetrayalTariffBp() == 2500 && Sim->GetTradeRules().Num() == 0; },
				}, FString::Printf(TEXT("LV0 InitLevers itself reads every row through its clamp, then the cross-row rule: with all 13 rows out of range (TariffMaxBp 1500.4 under PactBetrayalTariffBp 12000) the sim holds each clamped value (%d of 13) %s, the betrayal tariff at the cap (%d bp), the cargo cap %d kg and the cut %d%%; a plain InitLevers reads the authored config back (%d of 13)"),
					WildRight, *WildBad, WildBp, WildCap, WildCut, BackRight));
		}
	}

	// --- LV2. The migrated sequences, replayed with hard checks in fresh colonies (HEAD's own scripts, each with its save round trip). ---
	// (a) -covert: four ops at deep night give CXCX and the axis -24; the clean thefts now come out of Zarya's store (at the roll closing each
	// op's sol), silently; the fair trade lifts the axis to -22; the v20 round trip.
	{
		Fresh();
		Clock->Debug_SetSimSeconds(0.9 * SolLen);
		FString Pattern;
		for (int32 Op = 0; Op < 4; ++Op)
		{
			const double IceB = Sim->GetTotalSolid(NIce), RelB = Sim->GetRivalRelation(NZarya);
			VerbH(TEXT("Covert"), NZarya);
			const bool bC = Sim->GetTotalSolid(NIce) > IceB + 0.01, bX = Sim->GetRivalRelation(NZarya) < RelB - 0.01;
			Pattern.AppendChar(bC ? TEXT('C') : (bX ? TEXT('X') : TEXT('?')));
		}
		const double Axis = Sim->GetHumanNatureAxis();
		Sim->AddStock(NHyd, 40.0);
		{ FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = NZarya; Sim->EnqueueCommand(Cv); }
		RunSolsH(4.5);
		const double AxisTrade = Sim->GetHumanNatureAxis();
		int32 Thefts = 0;
		for (const FRHMarketEffectApplied& A : Sim->GetMarketEffectsApplied()) { Thefts += (A.Effect.Kind == ERHMarketEffectKind::Theft && A.Effect.Party == NZarya && A.Effect.Grams == 75000) ? 1 : 0; }
		const double AxB = Sim->GetHumanNatureAxis(), HidB = Sim->GetHiddenTension(NZarya);
		const int32 AttemptsB = Sim->GetCovertAttempts();
		FString Err;
		const int32 Warn0 = Capture.NumWarnings();
		const bool bRound = Sim->SaveColony(TEXT("lv2covert"), Err) && Sim->LoadColony(TEXT("lv2covert"), Err) && Err.IsEmpty() && Capture.NumWarnings() == Warn0;
		ExpectSeq({
			[&]() { return Pattern == TEXT("CXCX"); },
			[&]() { return Near(Axis, -24.0); },
			[&]() { return Near(AxisTrade, -22.0); },
			[&]() { return Thefts == 2 && Sim->GetPendingMarketEffects().Num() == 0; },
			[&]() { return bRound && Sim->GetHumanNatureAxis() == AxB && Sim->GetHiddenTension(NZarya) == HidB && Sim->GetCovertAttempts() == AttemptsB && AttemptsB == 4; },
			}, FString::Printf(TEXT("LV2 -covert replayed: COVERT sequence %s (CXCX), axis %.0f (-24), the fair trade lifts it to %.0f (-22); the two clean ops' thefts (75 kg of Ice each) left Zarya's store at the rolls closing their sols (%d applied); the v20 round trip keeps the axis, the hidden tension and 4 attempts, with no Warning"),
				*Pattern, Axis, AxisTrade, Thefts));
	}
	// (b) -espionage: launder 33 -> 13; the sabotage (caught=0) refuses trade with HEAD's text, remaining within 0.05 of 5.8; the rival recovers
	// on the very step HEAD's double arithmetic gives; the second sabotage is caught=1; the v21 round trip keeps 5.9.
	{
		Fresh();
		Clock->Debug_SetSimSeconds(0.9 * SolLen);
		VerbH(TEXT("Covert"), NZarya);
		VerbH(TEXT("Covert"), NZarya);
		const double Hid0 = Sim->GetHiddenTension(NZarya), HN0 = Sim->GetHumanNatureAxis();
		Sim->AddStock(NStruct, 200.0);
		VerbH(TEXT("Launder"), NZarya);
		const double Hid1 = Sim->GetHiddenTension(NZarya), HN1 = Sim->GetHumanNatureAxis();
		double SabotageAt = -1.0;
		const FDelegateHandle ExecH = Sim->OnCommandExecuted.AddLambda([&SabotageAt, Clock](const FRHCommand& Cmd) { if (Cmd.Verb == FName("Sabotage") && SabotageAt < 0.0) { SabotageAt = Clock->GetSimSecondsTotal(); } });
		Capture.Clear();
		VerbH(TEXT("Sabotage"), NZarya);
		Sim->AddStock(NHyd, 40.0);
		const int32 Rej0 = Rejects.Num();
		{ FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = NZarya; Sim->EnqueueCommand(Cv); }
		RunSolsH(0.1);
		const double Remaining = Sim->GetSabotageRemaining(NZarya);
		const FString Refusal = Rejects.Num() > Rej0 ? Rejects.Last() : FString();
		const bool bIdle = Sim->GetConvoyRival().IsNone();
		RunSolsH(6.5);
		Sim->AddStock(NHyd, 40.0);
		{ FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = NZarya; Sim->EnqueueCommand(Cv); }
		RunSolsH(0.1);
		const double Remaining2 = Sim->GetSabotageRemaining(NZarya);
		const bool bOut = Sim->GetConvoyRival() == NZarya;
		RunSolsH(4.5);
		for (int32 i = 1; i <= 8 && !Sim->IsRivalDiscovered(NMeridian); ++i) { Sim->Debug_MaybeDiscoverSettlement(i); }
		VerbH(TEXT("Sabotage"), NZarya);
		Sim->OnCommandExecuted.Remove(ExecH);
		const double SabB = Sim->GetSabotageRemaining(NZarya);
		FString Err;
		const int32 Warn0 = Capture.NumWarnings();
		const bool bRound = Sim->SaveColony(TEXT("lv2esp"), Err) && Sim->LoadColony(TEXT("lv2esp"), Err) && Err.IsEmpty() && Capture.NumWarnings() == Warn0;
		const double SabA = Sim->GetSabotageRemaining(NZarya);
		// HEAD's arithmetic, as a reference: the timer starts at SabotageDurationSols on the act's step, which already takes one DtSols, and the
		// rival recovers on the step whose decrement first reaches <= 0.
		const double Duration = DefsSub->GetConfigScalar(FName("SabotageDurationSols"), 6.0);
		double T = Duration;
		int32 N = 0;
		do { T -= DtSols; ++N; } while (T > 0.0);
		const double WantRecovered = SabotageAt + (double)(N - 1) * EraDt;
		const double RecoveredAt = AlertAt(TEXT("RECOVERED"), 0);
		const TArray<FString> SabLines = Capture.Matching(TEXT("=== SABOTAGE: Zarya Station offline 6 sols"));
		ExpectSeq({
			[&]() { return Near(Hid0, 33.0) && Near(Hid1, 13.0) && Near(HN1 - HN0, 3.0); },
			[&]() { return Refusal == HeadDisrupted && bIdle; },
			[&]() { return Near(Remaining, 5.8, 0.05); },
			[&]() { return SabotageAt > 0.0 && RecoveredAt == WantRecovered; },
			[&]() { return Remaining2 == 0.0 && bOut; },
			[&]() { return SabLines.Num() == 2 && SabLines[0].Contains(TEXT("caught=0")) && SabLines[1].Contains(TEXT("caught=1")); },
			[&]() { return bRound && Near(SabB, 5.9, 0.05) && SabA == SabB && Sim->IsRivalDiscovered(NMeridian); },
			}, FString::Printf(TEXT("LV2 -espionage replayed: launder %.0f -> %.0f; the sabotage refuses the convoy ('%s'), remaining %.2f (5.8); the rival recovers at %.0f s, the step HEAD's arithmetic gives (the act's step %.0f + %d steps); trade resumes; the sabotages log caught=0 then caught=1; the v21 round trip keeps %.2f -> %.2f, with no Warning"),
				Hid0, Hid1, *Refusal, Remaining, RecoveredAt, SabotageAt, N - 1, SabB, SabA));
	}
	// (c) -preemptive: the embargo at tension 78 with grace 8, refusing trade with HEAD's text (grace 7.8); Pacify 33 -> 13 and 78 -> 58; the
	// re-embargo at 100 with a convoy out; the soured return; the defection at relation 5 on HEAD's step; the v22 round trip.
	{
		Fresh();
		Capture.Clear();
		{ FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = NZarya; Sim->AddStock(NHyd, 40.0); Sim->EnqueueCommand(Cv); }
		RunSolsH(4.5);
		const double Influence1 = Sim->GetInfluence();
		Sim->Debug_AddTension(75.0);
		RunSolsH(0.2);
		const FName Embargoing = Sim->GetEmbargoingRival();
		Sim->AddStock(NHyd, 40.0);
		const int32 Rej0 = Rejects.Num();
		{ FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = NZarya; Sim->EnqueueCommand(Cv); }
		RunSolsH(0.1);
		const double Grace = Sim->GetEmbargoGrace(NZarya), GraceAt = Clock->GetSimSecondsTotal();
		const FString Refusal = Rejects.Num() > Rej0 ? Rejects.Last() : FString();
		Sim->Debug_AddInfluence(30.0);
		const double InfBefore = Sim->GetInfluence(), TenBefore = Sim->GetEarthTension();
		VerbH(TEXT("Pacify"), NZarya);
		const double InfAfter = Sim->GetInfluence(), TenAfter = Sim->GetEarthTension();
		const bool bLifted = !Sim->IsEmbargoed(NZarya);
		Sim->AddStock(NHyd, 40.0);
		{ FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = NZarya; Sim->EnqueueCommand(Cv); }
		RunSolsH(0.1);
		Sim->Debug_AddTension(80.0);
		RunSolsH(0.1);
		const bool bReEmbargo = Sim->IsEmbargoed(NZarya) && Sim->GetConvoyRival() == NZarya;
		const double RelSour = Sim->GetRivalRelation(NZarya);
		RunSolsH(4.4);
		const double RelAfter = Sim->GetRivalRelation(NZarya);
		RunSolsH(4.5);
		const bool bDefected = Sim->HasDefected(NZarya) && !Sim->IsEmbargoed(NZarya) && Near(Sim->GetRivalRelation(NZarya), 5.0);
		// HEAD's step for the defection: the embargo is made after the step's countdown, so the grace first falls on the next step.
		const double GraceSols = DefsSub->GetConfigScalar(FName("EmbargoGraceSols"), 8.0);
		double G = GraceSols;
		int32 N = 0;
		do { G -= DtSols; ++N; } while (G > 0.0);
		const double ReEmbargoAt = AlertAt(TEXT("EARTH LEANS ON"), 1), DefectedAt = AlertAt(TEXT("DEFECTION"), 0);
		// The grace read after the first embargo, replayed: made after its step's countdown at 8, it falls DtSols on each later step (HEAD's
		// arithmetic, double for double), so one decrement more or less is a different double (the 7.8 within 0.05 alone could not tell).
		const double EmbargoAt = AlertAt(TEXT("EARTH LEANS ON"), 0);
		const int32 GraceSteps = EmbargoAt > 0.0 ? FMath::RoundToInt32((GraceAt - EmbargoAt) / EraDt) : -1;
		double GraceWant = GraceSols;
		for (int32 i = 0; i < GraceSteps; ++i) { GraceWant -= DtSols; }
		const double InfB = Sim->GetInfluence();
		FString Err;
		const int32 Warn0 = Capture.NumWarnings();
		const bool bRound = Sim->SaveColony(TEXT("lv2pre"), Err) && Sim->LoadColony(TEXT("lv2pre"), Err) && Err.IsEmpty() && Capture.NumWarnings() == Warn0;
		ExpectSeq({
			[&]() { return Influence1 >= 3.0; },
			[&]() { return Capture.CountLines(TEXT("=== EMBARGO: Zarya Station (Earth tension 78, grace 8 sols) ===")) == 1 && Embargoing == NZarya; },
			[&]() { return Refusal == HeadEmbargo && GraceSteps > 0 && Grace == GraceWant && Near(Grace, 7.8, 0.05); },
			[&]() { return bLifted && Near(InfBefore, 33.0, 0.05) && Near(InfAfter, 13.0, 0.05) && Near(TenBefore, 78.0, 0.5) && Near(TenAfter, 58.0, 0.5); },
			[&]() { return bReEmbargo && Capture.CountLines(TEXT("=== EMBARGO: Zarya Station (Earth tension 100, grace 8 sols) ===")) == 1; },
			[&]() { return Capture.CountLines(TEXT("relation 42 (soured - goods only) ===")) == 1 && RelAfter == RelSour; },
			[&]() { return bDefected && Capture.CountLines(TEXT("=== DEFECTION: Zarya Station (relation 5) ===")) == 1; },
			[&]() { return ReEmbargoAt > 0.0 && DefectedAt == ReEmbargoAt + (double)N * EraDt; },
			[&]() { return bRound && Sim->HasDefected(NZarya) && Sim->GetInfluence() == InfB; },
			}, FString::Printf(TEXT("LV2 -preemptive replayed: the embargo at tension 78, grace 8, refusing the convoy ('%s'), grace %.17g, exactly HEAD's 8 less %d steps' DtSols (7.8 within 0.05); Pacify %.1f -> %.1f and %.0f -> %.0f; the re-embargo at 100 with the convoy out; the soured return (relation %.0f); the defection at relation 5 at %.0f s, %d steps after the re-embargo (%.0f s), HEAD's step; the v22 round trip, with no Warning"),
				*Refusal, Grace, GraceSteps, InfBefore, InfAfter, TenBefore, TenAfter, RelAfter, DefectedAt, N, ReEmbargoAt));
	}
	// (d) -solidarity: a Comply closes 1 route, relation 5, tension 55; the convoy is refused with the closed text while Zarya is ALSO
	// embargoed (precedence by kind: the embargo was made first); the v19 round trip.
	{
		Fresh();
		Capture.Clear();
		FString R;
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 4, R);
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(NOx, 2000.0);
		Sim->AddStock(NWater, 2000.0);
		Sim->AddStock(NFood, 2000.0);
		RunSolsH(2.5);
		Sim->Debug_AddColonists(2);
		RunSolsH(0.2);
		const auto Answer = [&](const TCHAR* Choice) { FRHCommand Cmd; Cmd.Verb = FName("Solidarity"); Cmd.Target = FName(Choice); Sim->EnqueueCommand(Cmd); RunSolsH(0.1); };
		Answer(TEXT("Defy"));
		Sim->Debug_AddTension(65.0);
		RunSolsH(0.2);
		Answer(TEXT("Defy"));
		RunSolsH(10.0);
		Sim->Debug_AddTension(65.0);
		RunSolsH(0.2);
		Answer(TEXT("Comply"));
		const int32 Closed = Sim->GetClosedRouteCount();
		const double Rel = Sim->GetRivalRelation(NZarya), Ten = Sim->GetEarthTension();
		Sim->AddStock(NHyd, 40.0);
		const int32 Rej0 = Rejects.Num();
		{ FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = NZarya; Sim->EnqueueCommand(Cv); }
		RunSolsH(0.1);
		const FString Refusal = Rejects.Num() > Rej0 ? Rejects.Last() : FString();
		const bool bBoth = Sim->IsEmbargoed(NZarya) && Sim->IsRouteClosed(NZarya);
		const FRHTradeRule* Emb = RuleOf(EKind::Embargo, NZarya);
		const FRHTradeRule* Cls = RuleOf(EKind::Closed, NZarya);
		const bool bOrder = Emb && Cls && Emb->Id < Cls->Id && Cls->Origin == EOrigin::Comply;
		FString Err;
		const int32 Warn0 = Capture.NumWarnings();
		const bool bRound = Sim->SaveColony(TEXT("lv2sol"), Err) && Sim->LoadColony(TEXT("lv2sol"), Err) && Err.IsEmpty() && Capture.NumWarnings() == Warn0;
		ExpectSeq({
			[&]() { return Capture.CountLines(TEXT("=== SOLIDARITY: COMPLIED - 1 route(s) closed")) == 1 && Closed == 1; },
			[&]() { return Near(Rel, 5.0) && Near(Ten, 55.0, 0.5); },
			[&]() { return bBoth && bOrder && Refusal == HeadClosed; },
			[&]() { return bRound && Sim->GetClosedRouteCount() == 1 && Sim->IsRouteClosed(NZarya); },
			}, FString::Printf(TEXT("LV2 -solidarity replayed: the Comply closes %d route (the log says '1 route(s) closed'), relation %.0f, tension %.0f; Zarya is embargoed (rule #%d, made first) and closed (#%d) and the convoy is refused with the closed text ('%s'); the v19 round trip keeps the closed route, with no Warning"),
				Closed, Rel, Ten, Emb ? Emb->Id : -1, Cls ? Cls->Id : -1, *Refusal));
	}
	// (e) -icelow and -arrivals: in both, nothing but Earth's drift moves the tension, so the embargo lands at 75 in sol 125 and the defection at
	// relation 5 in sol 133. Replayed as that drift timeline, each on the step HEAD's double arithmetic gives.
	{
		Fresh();
		Capture.Clear();
		const double Start = Clock->GetSimSecondsTotal(), Tension0 = Sim->GetEarthTension();
		const double Drift = DefsSub->GetConfigScalar(FName("EarthTensionDriftPerSol"), 0.6);
		const double Threshold = DefsSub->GetConfigScalar(FName("EarthPreemptiveThreshold"), 75.0);
		double Tn = Tension0;
		int32 K = 0;
		while (Tn < Threshold) { Tn = FMath::Clamp(Tn + Drift * DtSols, 0.0, 100.0); ++K; }
		double G = DefsSub->GetConfigScalar(FName("EmbargoGraceSols"), 8.0);
		int32 N = 0;
		do { G -= DtSols; ++N; } while (G > 0.0);
		const double WantEmbargo = Start + (double)K * EraDt, WantDefection = WantEmbargo + (double)N * EraDt;
		while (Clock->GetSimSecondsTotal() < WantDefection + 2.0 * EraDt) { RunSolsH(0.05); }
		const double EmbargoAt = AlertAt(TEXT("EARTH LEANS ON"), 0), DefectedAt = AlertAt(TEXT("DEFECTION"), 0);
		ExpectSeq({
			[&]() { return Capture.CountLines(TEXT("=== EMBARGO: Zarya Station (Earth tension 75, grace 8 sols) ===")) == 1; },
			[&]() { return EmbargoAt == WantEmbargo && SolOf(EmbargoAt) == 125; },
			[&]() { return Capture.CountLines(TEXT("=== DEFECTION: Zarya Station (relation 5) ===")) == 1; },
			[&]() { return DefectedAt == WantDefection && SolOf(DefectedAt) == 133; },
			[&]() { const FRHTradeRule* S = RuleOf(EKind::Severed, NZarya); return S && S->Origin == EOrigin::Defection && RulesOf(EKind::Embargo, NAME_None) == 0; },
			}, FString::Printf(TEXT("LV2 -icelow and -arrivals' drift timeline replayed: the embargo at tension 75 with grace 8 at %.0f s (sol %d; %d steps of drift), the defection at relation 5 at %.0f s (sol %d; %d steps later), each on HEAD's step; the embargo became a Severed rule in place"),
				EmbargoAt, SolOf(EmbargoAt), K, DefectedAt, SolOf(DefectedAt), N));
	}

	// --- LV1. Inertness: twins with the levers on and off run, in each band, the scripts of -covert, -espionage, -preemptive and -solidarity
	// after 10 sols of standing trade. The digest (relations, hidden tensions, Influence, HumanNature, identity, tension, every refusal, every
	// timer, CovertAttempts, the colony's stocks, Hope, the quota phase, E5's record and journal) is bit-identical; rival stores, prices and
	// credit balances are left out, because the "on" twin's thefts and cuts move them on purpose. The saves are byte-identical up to E1's frame
	// (the v19 / v21 / v22 mirror included). A third pair of twins with no sabotage and no theft holds credits and prices identical too. ---
	{
		struct FEvt { double Sol; const TCHAR* Verb; FName Target; int32 Harness; };
		const auto Script = [&](bool bCovert) -> TArray<FEvt>
		{
			TArray<FEvt> E;
			E.Add({ 0.30, TEXT("Convoy"), NZarya, 0 });
			E.Add({ 4.80, TEXT("Convoy"), NZarya, 0 });
			E.Add({ 9.30, TEXT("Convoy"), NZarya, 0 });
			// Storm_1 freezes every convoy in sols 12-15, so the third trade is home only at about T0 + 16.3: the scripts start after it.
			if (bCovert)
			{
				E.Add({ 17.40, TEXT("Covert"), NZarya, 0 });
				E.Add({ 17.45, TEXT("Covert"), NZarya, 0 });
			}
			E.Add({ 17.50, TEXT("Launder"), NZarya, 0 });
			if (bCovert)
			{
				E.Add({ 17.55, TEXT("Sabotage"), NZarya, 0 });
			}
			E.Add({ 17.60, TEXT("Convoy"), NZarya, 0 });          // refused: disrupted (with the sabotage)
			E.Add({ 24.10, TEXT("Convoy"), NZarya, 0 });
			E.Add({ 28.50, TEXT(""), NAME_None, 1 });            // tension +75: Earth embargoes Zarya
			E.Add({ 28.70, TEXT("Convoy"), NZarya, 0 });          // refused: embargoed
			E.Add({ 28.80, TEXT(""), NAME_None, 2 });            // Influence +30
			E.Add({ 28.90, TEXT("Pacify"), NZarya, 0 });
			E.Add({ 29.50, TEXT(""), NAME_None, 3 });            // tension +65: a demand (and an embargo again)
			E.Add({ 29.70, TEXT("Solidarity"), FName("Comply"), 0 });
			E.Add({ 29.90, TEXT("Convoy"), NZarya, 0 });          // refused: closed
			return E;
		};
		struct FTwin { FString Digest, Rejected, Prices, Credits; TArray<uint8> Bytes; int32 Thefts = 0, Cuts = 0, Windowed = 0; };
		const auto RunTwin = [&](bool bAgent, bool bLevers, bool bCovert, const TCHAR* Slot) -> FTwin
		{
			Reset();
			Sim->Debug_SetLeversEnabled(bLevers);
			Provision(100000000);
			Sim->AddStock(NHyd, 600.0);
			Sim->AddStock(NStruct, 1000.0);
			const double T0 = FMath::FloorToDouble(Clock->GetSimSecondsTotal() / SolLen) * SolLen + SolLen; // the next sol's start
			const TArray<FEvt> Evts = Script(bCovert);
			int32 Next = 0, Thefts = 0, Cuts = 0;
			bool bFirst = true;
			const double EndT = T0 + 30.5 * SolLen;
			while (Clock->GetSimSecondsTotal() < EndT - 1.0e-6)
			{
				const double Now = Clock->GetSimSecondsTotal();
				while (Next < Evts.Num() && Now >= T0 + Evts[Next].Sol * SolLen - 1.0e-6)
				{
					const FEvt& E = Evts[Next];
					if (E.Harness == 1) { Sim->Debug_AddTension(75.0); }
					else if (E.Harness == 2) { Sim->Debug_AddInfluence(30.0); }
					else if (E.Harness == 3) { Sim->Debug_AddTension(65.0); }
					else
					{
						FRHCommand C; C.Verb = FName(E.Verb); C.Target = E.Target;
						Sim->EnqueueCommand(C);
					}
					++Next;
				}
				if (bAgent) { AgentOnce(); }
				else if (bFirst) { bFirst = false; Clock->Debug_AdvanceSimSeconds(30.0); Sim->Debug_SetBatteryWh(1.0e9); Sim->EraStep(30.f); }
				else { EraOnce(); }
			}
			for (const FRHMarketEffectApplied& A : Sim->GetMarketEffectsApplied()) { Thefts += A.Effect.Kind == ERHMarketEffectKind::Theft ? 1 : 0; }
			Cuts = Sim->GetProductionCuts().Num();
			FTwin O;
			O.Thefts = Thefts + Sim->GetPendingMarketEffects().Num();
			O.Cuts = Cuts;
			for (const FRHTradeRule& R : Sim->GetTradeRules()) { O.Windowed += R.HasWindow() ? 1 : 0; }
			O.Digest = FString::Printf(TEXT("sol %d rel %.17g/%.17g hid %.17g inf %.17g hn %.17g id %.17g ten %.17g att %d sab %.17g grace %.17g closed %d def %d emb %d hope %.17g pop %d phase %d view %lld/%lld journal %d demand %d"),
				Clock->GetSol(), Sim->GetRivalRelation(NZarya), Sim->GetRivalRelation(NMeridian), Sim->GetHiddenTension(NZarya), Sim->GetInfluence(), Sim->GetHumanNatureAxis(),
				Sim->GetIdentityAxis(), Sim->GetEarthTension(), Sim->GetCovertAttempts(), Sim->GetSabotageRemaining(NZarya), Sim->GetEmbargoGrace(NZarya), Sim->GetClosedRouteCount(),
				Sim->HasDefected(NZarya) ? 1 : 0, Sim->IsEmbargoed(NZarya) ? 1 : 0, Sim->GetHopeSmoothed(), Sim->GetPopulation(), (int32)Sim->GetQuotaPhase(),
				Sim->GetIdentityView(), Sim->GetTensionView(), Sim->GetPoliticalJournal().Num(), Sim->IsEarthDemandPending() ? 1 : 0);
			DefsSub->ForEachResourceRow([&](FName R, const FRHResourceRow&) { O.Digest += FString::Printf(TEXT(" %s=%.17g/%.17g"), *R.ToString(), Sim->GetStock(R), Sim->GetTotalSolid(R)); });
			for (const FString& Rj : Rejects) { O.Rejected += Rj + TEXT(" | "); }
			O.Digest += TEXT(" refusals: ") + O.Rejected;
			for (const FName R : TradedSlots) { O.Prices += FString::Printf(TEXT("%s=%lld "), *R.ToString(), Sim->GetMarketPriceMcr(R)); }
			O.Credits = FString::Printf(TEXT("%lld/%lld/%lld"), Sim->GetCreditBalanceMcr(), Sim->GetCreditAwardedMcr(), Sim->GetCreditSpentMcr());
			FString Err;
			Sim->SaveColony(Slot, Err);
			FFileHelper::LoadFileToArray(O.Bytes, *SaveFilePath(Slot));
			return O;
		};
		const auto ToLedger = [](const FTwin& T) { int32 F = INDEX_NONE, P = 0; return RHSaveFrame::Find(T.Bytes, RHSaveFrame::LedgerTag, F, P) ? F : INDEX_NONE; };
		for (const bool bAgent : { false, true })
		{
			const FTwin On = RunTwin(bAgent, true, true, bAgent ? TEXT("lv1_a1") : TEXT("lv1_e1"));
			const FTwin Off = RunTwin(bAgent, false, true, bAgent ? TEXT("lv1_a0") : TEXT("lv1_e0"));
			const int32 OnAt = ToLedger(On), OffAt = ToLedger(Off);
			const bool bBytes = OnAt > 0 && OnAt == OffAt && FMemory::Memcmp(On.Bytes.GetData(), Off.Bytes.GetData(), OnAt) == 0;
			Info(FString::Printf(TEXT("LV1 %s refusals: %s"), bAgent ? TEXT("agent") : TEXT("era"), *On.Rejected));
			if (On.Digest != Off.Digest) { Info(FString::Printf(TEXT("LV1 %s digest on: %s"), bAgent ? TEXT("agent") : TEXT("era"), *On.Digest.Left(600))); Info(FString::Printf(TEXT("LV1 %s digest off: %s"), bAgent ? TEXT("agent") : TEXT("era"), *Off.Digest.Left(600))); }
			ExpectSeq({
				[&]() { return On.Digest == Off.Digest; },
				[&]() { return On.Digest.Len() > 400 && On.Rejected.Contains(HeadDisrupted) && On.Rejected.Contains(HeadEmbargo) && On.Rejected.Contains(HeadClosed); },
				[&]() { return bBytes; },
				[&]() { return On.Thefts >= 1 && Off.Thefts == 0 && On.Cuts == 6 && Off.Cuts == 0; },
				[&]() { return On.Prices != Off.Prices; },
				}, FString::Printf(TEXT("LV1 %s band, 10 sols of standing trade then the four suites' scripts, 30.5 sols: the digest (%d chars: relations, hidden tension, Influence, HumanNature, identity, tension, the refusals, the timers, CovertAttempts, every stock, Hope, the quota phase, E5's record and journal) is bit-identical with the levers on and off, every refusal HEAD's text; the saves (N bytes) are byte-identical up to E1's frame (%d bytes, the v19 / v21 / v22 mirror included); only the on twin's theft (%d) and six cut rolls (%d) moved Zarya's stores and prices"),
					bAgent ? TEXT("agent") : TEXT("era"), On.Digest.Len(), OnAt, On.Thefts, On.Cuts));
		}
		{
			const FTwin On = RunTwin(false, true, false, TEXT("lv1_q1"));
			const FTwin Off = RunTwin(false, false, false, TEXT("lv1_q0"));
			const int32 OnAt = ToLedger(On);
			const FString NoGrievance = TEXT("No hidden grievance with Zarya Station to defuse");
			ExpectSeq({
				[&]() { return On.Digest == Off.Digest; },
				[&]() { return On.Prices == Off.Prices && On.Credits == Off.Credits; },
				[&]() { return OnAt > 0 && On.Bytes.Num() == Off.Bytes.Num() && FMemory::Memcmp(On.Bytes.GetData(), Off.Bytes.GetData(), OnAt) == 0; },
				[&]() { return On.Thefts == 0 && On.Cuts == 0 && On.Windowed == 0; },
				[&]() { return On.Rejected.Contains(NoGrievance) && Off.Rejected.Contains(NoGrievance); },
				}, FString::Printf(TEXT("LV1 a third pair of twins with no covert op (so no theft, no sabotage and no offering: the launder is refused in both, '%s'), with the convoys, the embargo, Pacify and the Comply: the digest, the prices and the credits (%s) are identical with the levers on and off"), *NoGrievance, *On.Credits));
		}
		// (d) D29, gate by gate, each read directly (the twins above cannot see a gate that a later one hides: a window made with the levers off
		// would still cut nothing, since the roll reads the switch too). With the levers off: every markup reads 0 - two tariffs made while they
		// were on read 0, and an order books and clears untaxed at LV4's untaxed net; a clean theft and an offering record nothing; a sabotage's
		// Disrupted rule gets no window (its live refusal and timer are HEAD's: the migration is not switchable) and no roll cuts; the Treaty
		// verb is refused ('No treaties: disabled'), a Sign and a Break, even with a pact made while they were on. The switch is then on again.
		{
			Reset();
			Provision(20000000);
			Sim->Debug_SetMarketPrice(NIce, 3001);
			Sim->Debug_SetMarketPrice(NStruct, 5001);
			const int32 Eff = Sim->GetBookingSolNow();
			Sim->Debug_AddTariff(NZarya, NIce, 1, 2500, Eff, Eff + 5);
			Sim->Debug_AddTariff(NZarya, NStruct, 2, 9000, Eff, Eff + 5);
			const int32 OnBuy = Sim->GetLineMarkupBp(NZarya, NIce, 1, Eff), OnSell = Sim->GetLineMarkupBp(NZarya, NStruct, 2, Eff);
			Sim->Debug_SetLeversEnabled(false);
			const int32 OffBuy = Sim->GetLineMarkupBp(NZarya, NIce, 1, Eff), OffSell = Sim->GetLineMarkupBp(NZarya, NStruct, 2, Eff);
			// Markups: the order books with no markup in its terms and clears untaxed.
			const bool bBooked = Order(NZarya, TEXT("Ice:333;Struct:-7"));
			FRHConvoyTerms Terms;
			const bool bTerms = bBooked && Sim->GetConvoyTerms(Sim->GetConvoys().Last().Id, Terms) && Terms.LineMarkupBp == TArray<int32>({ 0, 0 });
			const bool bPricesHeld = Sim->GetMarketPriceMcr(NIce) == 3001 && Sim->GetMarketPriceMcr(NStruct) == 5001;
			RollNext();
			const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
			const bool bUntaxed = Rec && Rec->Rival == NZarya && Rec->LineMarkupBp == TArray<int32>({ 0, 0 }) && Rec->BookedMarkupBp == Rec->LineMarkupBp && Rec->TariffMcr == 0
				&& Rec->NetMcr == 964326 && Sim->GetTariffPaidMcr() == 0 && Sim->GetTariffPaidMmk() == 0;
			// Effects: a clean op (Zarya's U is 0.907 at attempt 0, clean whatever P) and a launder of the tension it leaves record nothing.
			FString Why;
			const int32 Att0 = Sim->GetCovertAttempts();
			Capture.Clear();
			const bool bOp = Sim->ResolveCovert(ActCmd(TEXT("Covert"), NZarya, Clock->GetSimSecondsTotal()), NZarya, Why) && Capture.CountLines(TEXT("=== COVERT CLEAN: Zarya Station")) == 1;
			const bool bLaunder = Sim->ResolveLaunder(ActCmd(TEXT("Launder"), NZarya, Clock->GetSimSecondsTotal()), NZarya, Why) && Capture.CountLines(TEXT("=== LAUNDER: Zarya Station")) == 1;
			const int32 Pending = Sim->GetPendingMarketEffects().Num();
			// The sabotage: no window, HEAD's live refusal and timer, no cut at the next two rolls.
			const bool bSab = Sim->ResolveSabotage(ActCmd(TEXT("Sabotage"), NZarya, Clock->GetSimSecondsTotal()), NZarya, Why);
			const FRHTradeRule* D = RuleOf(EKind::Disrupted, NZarya);
			const bool bNoWindow = bSab && D && !D->HasWindow() && D->bLive && D->LiveSols == 6.0 && Sim->GetDisruptionUntilSol(NZarya) == -1 && Sim->GetSabotageRemaining(NZarya) == 6.0;
			// The walk's live block, read directly (the order's convoy is still out, so PlanConvoy would refuse with "already out" first).
			URHSimWorldSubsystem::FRHTradeVerdict Live;
			Sim->EvaluateTradeRules(NZarya, Sim->GetBookingSolNow(), TArray<FRHConvoyLine>(), Live);
			const FString LiveWhy = URHSimWorldSubsystem::TradeBlockText(Live.BlockKind, TEXT("Zarya Station"));
			RollNext();
			RollNext();
			const int32 CutRecords = Sim->GetProductionCuts().Num();
			// The Treaty verb: a pact signed with the levers on, then neither a Sign nor a Break with them off.
			Sim->Debug_ClearPolitics();
			Sim->Debug_SetRelation(NZarya, 60.0);
			Sim->Debug_AddInfluence(40.0);
			Sim->Debug_SetLeversEnabled(true);
			const bool bSigned = Send(TEXT("Treaty"), NZarya, TEXT("Sign")) && Sim->HasLivePact(NZarya);
			Sim->Debug_SetLeversEnabled(false);
			FString WSign, WBreak;
			Sim->CanSignPact(NZarya, WSign);
			Sim->CanBreakPact(NZarya, WBreak);
			const bool bSignOff = !Send(TEXT("Treaty"), NZarya, TEXT("Sign")) && LastReject() == TEXT("No treaties: disabled");
			const bool bBreakOff = !Send(TEXT("Treaty"), NZarya, TEXT("Break")) && LastReject() == TEXT("No treaties: disabled") && Sim->HasLivePact(NZarya);
			Sim->Debug_SetLeversEnabled(true);
			ExpectSeq({
				[&]() { return OnBuy == 2500 && OnSell == 9000 && OffBuy == 0 && OffSell == 0; },
				[&]() { return bTerms && bPricesHeld && bUntaxed; },
				[&]() { return Att0 == 0 && bOp && bLaunder && Pending == 0; },
				[&]() { return bNoWindow && LiveWhy == HeadDisrupted && CutRecords == 0; },
				[&]() { return bSigned && WSign == TEXT("No treaties: disabled") && WBreak == TEXT("No treaties: disabled") && bSignOff && bBreakOff; },
				}, FString::Printf(TEXT("LV1 (d) the switch, gate by gate, with the levers off: a 2,500 bp buy tariff and a 9,000 bp sell tariff read %d and %d (%d and %d with them on), and Ice:333 against Struct:7 books with no markup and clears untaxed at net %s cr (LV4's untaxed net), tariff 0; a clean op and a launder record %d effects; a sabotage's rule has no window (its live refusal '%s' and 6-sol timer HEAD's) and no roll cuts (%d cut records); a Sign and a Break are refused ('%s', '%s'), the pact made with the levers on standing"),
					OffBuy, OffSell, OnBuy, OnSell, *Cr(Rec ? Rec->NetMcr : 0), Pending, *LiveWhy, CutRecords, *WSign, *WBreak));
		}
	}

	// --- LV3. Precedence (R2's split). (a) One rival gets an Embargo (tension forced to 80), then a sabotage, then a Comply: the refusal is
	// the closed text; with the Closed rule removed, the disrupted text; once the downtime ends, the embargo text. (b) A second rival's embargo
	// runs out into Severed in place, and its text is the defection text. (c) Two rivals injected in the reverse of their name order and closed by
	// one Comply give Closed rules in name order and the same block bytes as the forward order; a 12-rule list under seeded permutations gives an
	// unchanged verdict. ---
	{
		Reset();
		Provision(10000000);
		Sim->Debug_SetRequisitionInputs(Sim->GetIdentityAxis(), 80.0);
		EraOnce();
		const bool bEmb = Sim->IsEmbargoed(NZarya) && Sim->IsEarthDemandPending();
		const bool bSab = Send(TEXT("Sabotage"), NZarya);
		const bool bComply = Send(TEXT("Solidarity"), FName("Comply"));
		FString W1, W2, W3;
		Sim->CanPlaceStandingOrder(NZarya, W1);
		const FRHTradeRule* E = RuleOf(EKind::Embargo, NZarya);
		const FRHTradeRule* D = RuleOf(EKind::Disrupted, NZarya);
		const FRHTradeRule* C = RuleOf(EKind::Closed, NZarya);
		const bool bIds = E && D && C && E->Id < D->Id && D->Id < C->Id;
		const int32 ClosedId = C ? C->Id : -1;
		const bool bRemoved = Sim->Debug_RemoveRule(ClosedId);
		Sim->CanPlaceStandingOrder(NZarya, W2);
		int32 Steps = 0;
		while (Sim->GetSabotageRemaining(NZarya) > 0.0 && Steps++ < 200) { EraOnce(); }
		Sim->CanPlaceStandingOrder(NZarya, W3);
		const bool bStillEmb = Sim->IsEmbargoed(NZarya) && !Sim->HasDefected(NZarya);
		ExpectSeq({
			[&]() { return bEmb && bSab && bComply; },
			[&]() { return bIds; },
			[&]() { return W1 == HeadClosed; },
			[&]() { return bRemoved && W2 == HeadDisrupted; },
			[&]() { return bStillEmb && W3 == HeadEmbargo; },
			}, FString::Printf(TEXT("LV3 (a) Zarya embargoed (rule #%d), then sabotaged (#%d), then closed by a Comply (#%d): the refusal is the closed text ('%s'); with the Closed rule removed, the disrupted text; once the downtime ends (%d steps), still embargoed, the embargo text ('%s')"),
				E ? E->Id : -1, D ? D->Id : -1, ClosedId, *W1, Steps, *W3));
	}
	{
		Reset();
		Inject(FName("LvB"), 60.0, TEXT("Ice:50"), TEXT("Ice:10"), TEXT("Ice:700"), TEXT("Ice:4000"), TEXT(""), TEXT(""), -1, 30.f);
		Sim->Debug_SyncRivalEconomies();
		Sim->Debug_SetEmbargo(FName("LvB"), 0.1);
		const FRHTradeRule* Before = RuleOf(EKind::Embargo, FName("LvB"));
		const int32 IdBefore = Before ? Before->Id : -1;
		EraOnce();
		EraOnce();
		EraOnce();
		const FRHTradeRule* After = RuleOf(EKind::Severed, FName("LvB"));
		FString W;
		Sim->CanPlaceStandingOrder(FName("LvB"), W);
		ExpectSeq({
			[&]() { return IdBefore > 0 && After && After->Id == IdBefore && After->Origin == EOrigin::Defection && After->bLive; },
			[&]() { return !Sim->IsEmbargoed(FName("LvB")) && Sim->HasDefected(FName("LvB")); },
			[&]() { return W == TEXT("LvB Post has sided with Earth - relations are frozen, no trade"); },
			[&]() { return Near(Sim->GetRivalRelation(FName("LvB")), 5.0); },
			}, FString::Printf(TEXT("LV3 (b) a second rival's embargo runs out into a Severed rule in place (Id %d kept, Origin Defection), relation floored at 5, and its refusal is the defection text ('%s')"), IdBefore, *W));
		RetireAll();
	}
	{
		// (c) Closed rules in name order, whatever order the rows were made in. The rivals of each pair have names of one length, so a pair's
		// block bytes compare once each name is replaced by its rank.
		const auto RunPair = [&](FName NameA, FName NameB, bool bBFirst, TArray<uint8>& OutBlock, FString& OutOrder)
		{
			Reset();
			const auto InjectA = [&]() { Inject(NameA, 40.0, TEXT("Ice:50"), TEXT("Ice:10"), TEXT("Ice:700"), TEXT("Ice:4000"), TEXT(""), TEXT(""), -1, 30.f); };
			const auto InjectB = [&]() { Inject(NameB, 55.0, TEXT("Ice:20"), TEXT("Ice:5"), TEXT("Ice:300"), TEXT("Ice:3000"), TEXT(""), TEXT(""), -1, 30.f); };
			if (bBFirst) { InjectB(); InjectA(); } else { InjectA(); InjectB(); }
			Sim->Debug_SyncRivalEconomies();
			Sim->Debug_SetRequisitionInputs(Sim->GetIdentityAxis(), 65.0);
			EraOnce();
			Send(TEXT("Solidarity"), FName("Comply"));
			for (const FRHTradeRule& R : Sim->GetTradeRules()) { if (R.Kind == EKind::Closed) { OutOrder += R.Party.ToString() + TEXT(" "); } }
			FString Err;
			Sim->SaveColony(TEXT("lv3pair"), Err);
			TArray<uint8> W;
			FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("lv3pair")));
			int32 At = INDEX_NONE, Bytes = 0;
			OutBlock.Reset();
			if (RHSaveFrame::Find(W, RHSaveFrame::LeversTag, At, Bytes)) { OutBlock.Append(W.GetData() + At + RHSaveFrame::HeaderBytes, Bytes); }
			const auto Replace = [&OutBlock](const FString& From, const FString& To)
			{
				const FTCHARToUTF8 F(*From), T(*To);
				for (int32 i = 0; i + F.Length() <= OutBlock.Num(); ++i)
				{
					if (FMemory::Memcmp(OutBlock.GetData() + i, F.Get(), F.Length()) == 0) { FMemory::Memcpy(OutBlock.GetData() + i, T.Get(), T.Length()); }
				}
			};
			Replace(NameA.ToString(), TEXT("LvRnk1"));
			Replace(NameB.ToString(), TEXT("LvRnk2"));
			RetireAll();
		};
		TArray<uint8> BlockRev, BlockFwd;
		FString OrderRev, OrderFwd;
		RunPair(FName("LvRevA"), FName("LvRevB"), true, BlockRev, OrderRev);   // table order: ..., LvRevB, LvRevA
		RunPair(FName("LvFwdC"), FName("LvFwdD"), false, BlockFwd, OrderFwd);  // table order: ..., LvFwdC, LvFwdD
		ExpectSeq({
			[&]() { return OrderRev == TEXT("LvRevA LvRevB Zarya ") && OrderFwd == TEXT("LvFwdC LvFwdD Zarya "); },
			[&]() { return BlockRev.Num() > 33 && BlockRev == BlockFwd; },
			}, FString::Printf(TEXT("LV3 (c) one Comply closing two rivals injected in the reverse of their name order (%s) and two in the forward order (%s) appends the Closed rules in name-string order, and the two blocks are byte-identical once each name is replaced by its rank"),
				*OrderRev, *OrderFwd));
		// The walk's verdict does not depend on the list's order: 12 rules naming Zarya, re-added in 20 seeded permutations (new Ids each time).
		Reset();
		const int32 Open = Sim->GetMarketCheckedSol();
		TArray<FRHTradeRule> Twelve;
		const auto Add = [&Twelve, NZarya](EKind Kind, int32 Slot, uint8 Dir, int32 Bp, int32 From, int32 Until, double Live, bool bLive)
		{
			FRHTradeRule R; R.Kind = Kind; R.Origin = EOrigin::Harness; R.Party = NZarya; R.ResSlot = Slot; R.Dir = Dir; R.MarkupBp = Bp; R.FromSol = From; R.UntilSol = Until; R.LiveSols = Live; R.bLive = bLive;
			Twelve.Add(R);
		};
		const int32 SIce = Sim->GetLedgerSlotIndex(NIce), SStruct = Sim->GetLedgerSlotIndex(NStruct), SWater = Sim->GetLedgerSlotIndex(NWater);
		Add(EKind::Disrupted, -1, 3, 0, Open + 5, Open + 11, 3.0, true);
		Add(EKind::Embargo, -1, 3, 0, -1, -1, 4.0, true);
		Add(EKind::Pact, -1, 3, 0, Open + 3, -1, 0.0, true);
		Add(EKind::Pact, -1, 3, 0, Open + 1, Open + 4, 0.0, false);
		Add(EKind::Tariff, SIce, 1, 1200, Open + 2, Open + 9, 0.0, false);
		Add(EKind::Tariff, -1, 3, 700, Open + 4, Open + 20, 0.0, false);
		Add(EKind::Tariff, SStruct, 2, 3000, Open, Open + 6, 0.0, false);
		Add(EKind::Tariff, SIce, 3, 8000, Open + 5, Open + 7, 0.0, false);
		Add(EKind::Closed, -1, 3, 0, -1, -1, 0.0, true);
		Add(EKind::Severed, -1, 3, 0, -1, -1, 0.0, true);
		Add(EKind::Tariff, SWater, 2, 50, Open + 1, Open + 100, 0.0, false);
		Add(EKind::Disrupted, -1, 3, 0, Open, Open + 3, 0.0, false);
		TArray<FRHConvoyLine> Lines;
		for (const TPair<FName, int32>& L : { TPair<FName, int32>(NIce, 100), TPair<FName, int32>(NStruct, -100), TPair<FName, int32>(NWater, -50), TPair<FName, int32>(NOx, 10) })
		{
			FRHConvoyLine& Ln = Lines.AddDefaulted_GetRef(); Ln.Resource = L.Key; Ln.Kg = L.Value;
		}
		const auto Verdict = [&]() { URHSimWorldSubsystem::FRHTradeVerdict V; Sim->EvaluateTradeRules(NZarya, Open + 5, Lines, V); return FString::Printf(TEXT("block %d pact %d markups %d %d %d %d"), V.BlockKind, V.bPact ? 1 : 0, V.LineMarkupBp[0], V.LineMarkupBp[1], V.LineMarkupBp[2], V.LineMarkupBp[3]); };
		for (const FRHTradeRule& R : Twelve) { Sim->Debug_AddTradeRule(R); }
		const FString Want = Verdict();
		FLevSuiteLcg Lcg;
		int32 Same = 0;
		FString Seen;
		for (int32 P = 0; P < 20; ++P)
		{
			Reset();
			TArray<int32> Perm;
			for (int32 i = 0; i < Twelve.Num(); ++i) { Perm.Add(i); }
			for (int32 i = Perm.Num() - 1; i > 0; --i) { Perm.Swap(i, (int32)Lcg.Range(0, i)); }
			for (const int32 i : Perm) { Sim->Debug_AddTradeRule(Twelve[i]); }
			const FString Got = Verdict();
			Same += Got == Want ? 1 : 0;
			if (Got != Want) { Seen += Got + TEXT("; "); }
		}
		ExpectSeq({
			[&]() { return Want == TEXT("block 1 pact 1 markups 9000 3700 750 700"); },
			[&]() { return Same == 20; },
			}, FString::Printf(TEXT("LV3 (c) a 12-rule list naming Zarya gives at EffSol %d '%s' (closed first by kind; a pact window; Ice 1200 + 700 + 8000 clamped to 9000, Struct 700 + 3000, Water 700 + 50, Oxygen 700), and the same in all %d of 20 seeded permutations %s"),
				Open + 5, *Want, Same, *Seen));
	}
	{
		// (d) Retirement (D10 with R3 and re-review O2), at the end of each roll, read after each of five rolls (RollNext: no StepEarth, so no
		// live timer moves). Closed, Severed and Embargo never retire; a disruption with no window retires once its live part is over, and one
		// with a window once that is over too and its window is behind the open sol; a live pact never, an ended one once its window is behind
		// the open sol, or at the next roll when the window is empty (O2); a tariff once its window is behind the open sol. Ids never move.
		Reset();
		const int32 O = Sim->GetMarketCheckedSol();
		const auto AddRule = [&](EKind Kind, FName Party, int32 From, int32 Until, double Live, bool bLive)
		{
			FRHTradeRule R; R.Kind = Kind; R.Origin = EOrigin::Harness; R.Party = Party; R.FromSol = From; R.UntilSol = Until; R.LiveSols = Live; R.bLive = bLive;
			if (Kind == EKind::Tariff) { R.ResSlot = -1; R.Dir = 3; R.MarkupBp = 500; }
			return Sim->Debug_AddTradeRule(R);
		};
		const int32 FirstId = AddRule(EKind::Closed, NZarya, -1, -1, 0.0, true);   // never
		AddRule(EKind::Severed, NMeridian, -1, -1, 0.0, true);                        // never
		AddRule(EKind::Embargo, NZarya, -1, -1, 8.0, true);                           // never (Pacify or hardening only)
		AddRule(EKind::Disrupted, NZarya, -1, -1, 6.0, true);                         // no window, live: never while live
		AddRule(EKind::Disrupted, NZarya, -1, -1, 0.0, false);                        // no window, over: at the first roll
		AddRule(EKind::Disrupted, NZarya, O + 1, O + 3, 0.0, false);                  // over, its window behind the open sol at the third
		AddRule(EKind::Disrupted, NZarya, O + 1, O + 3, 6.0, true);                   // its window passes, but it is still live: never
		AddRule(EKind::Pact, NZarya, O, -1, 0.0, true);                               // live: never
		AddRule(EKind::Pact, NZarya, O + 5, O + 5, 0.0, false);                       // ended, an empty window: at the first roll (O2)
		AddRule(EKind::Pact, NZarya, O, O + 2, 0.0, false);                           // ended: at the second
		AddRule(EKind::Tariff, NZarya, O, O + 4, 0.0, false);                         // at the fourth
		AddRule(EKind::Tariff, NZarya, O + 10, O + 12, 0.0, false);                   // not within five rolls
		const int32 NextBefore = Sim->GetNextRuleId();
		FString Left;
		for (int32 k = 1; k <= 5; ++k)
		{
			RollNext();
			Left += FString::Printf(TEXT("%s%d:"), k > 1 ? TEXT(" | ") : TEXT(""), k);
			for (const FRHTradeRule& R : Sim->GetTradeRules()) { Left += FString::Printf(TEXT(" %d"), R.Id - FirstId + 1); }
		}
		ExpectSeq({
			[&]() { return FirstId > 0 && NextBefore == FirstId + 12 && Sim->GetMarketCheckedSol() == O + 5; },
			[&]() { return Left == TEXT("1: 1 2 3 4 6 7 8 10 11 12 | 2: 1 2 3 4 6 7 8 11 12 | 3: 1 2 3 4 7 8 11 12 | 4: 1 2 3 4 7 8 12 | 5: 1 2 3 4 7 8 12"); },
			[&]() { return Sim->GetNextRuleId() == NextBefore; },
			}, FString::Printf(TEXT("LV3 (d) retirement at the end of each roll, twelve rules over five rolls from the open sol %d (the rules by their order, 1-12, left after roll k: %s): Closed, Severed and Embargo stay; a disruption with no window goes once it is over (5), one with a window once it is over and its window is behind the open sol (6, at the third), a live one never (4, 7); a live pact stays (8), an ended one goes when its window is behind the open sol (10, at the second) or at once when its window is empty (9, re-review O2); a tariff when its window is behind the open sol (11, at the fourth; 12 stays); NextRuleId does not move"),
				O, *Left));
	}

	// --- LV4. Tariff arithmetic, by hand: the one line function (every rounding against the colony); then real orders through the market at
	// 0, 1, 2,500 and 9,000 bp on a buy and a sell (values whose markup is not whole), two tariffs on one line (summed and clamped), per resource
	// and per direction, the window's edges; the reservation covering the charge when the price moves a full step, in credits and in marks; the
	// lifetime totals; and the prices, the tally, the bloc tally and the rate bit-identical to an untaxed twin. ---
	{
		using M = URHSimWorldSubsystem;
		ExpectSeq({
			[&]() { return M::MarketTariffMcr(999333, 0) == 0 && M::MarketTariffMcr(999333, 1) == 100 && M::MarketTariffMcr(999333, 2500) == 249834 && M::MarketTariffMcr(999333, 9000) == 899400; },
			[&]() { return M::MarketLineSettleMcr(999333, 2500, true) == 1249167 && M::MarketLineSettleMcr(999333, 2500, false) == 749499; },
			[&]() { return M::MarketLineSettleMcr(35007, 9000, false) == 3500 && M::MarketLineSettleMcr(35007, 1, false) == 35003 && M::MarketLineSettleMcr(35007, 0, false) == 35007; },
			[&]() { return M::MarketTariffMcr(0, 9000) == 0 && M::MarketTariffMcr(10000, 2500) == 2500 && M::MarketTariffMcr(1, 1) == 1; },
			}, TEXT("LV4 a line's tariff is ceil(value x m / 10000): 999,333 mcr at 0 / 1 / 2,500 / 9,000 bp charges 0 / 100 / 249,834 / 899,400; a buy costs 1,249,167 at 2,500 bp and a sell of it receives 749,499; 35,007 sold at 9,000 / 1 / 0 bp receives 3,500 / 35,003 / 35,007; an exact product rounds to itself"));
		struct FTaxed { bool bOk = false; int64 Net = 0, Tariff = 0, Settled = 0, Released = 0, PaidMcr = 0, PaidMmk = 0, Rate = 0; TArray<int32> Applied, Booked; FString Prices, Tally; int32 EffSol = 0; uint8 Currency = 0; };
		const auto RunTaxed = [&](bool bMarks, int32 BuyBp, int32 SellBp, int32 BuyBp2, bool bMove, const FString& Text) -> FTaxed
		{
			FTaxed O;
			Reset();
			Provision(20000000);
			bool bIssued = true;
			if (bMarks)
			{
				Sim->Debug_SetRequisitionInputs(20.0, Sim->GetEarthTension());
				Sim->Debug_SyncPoliticalView();
				bIssued = Send(TEXT("Currency"), FName("Issue"));
				RollNext();
				RollNext();
				bIssued = bIssued && Sim->IsMarketInMarks();
				Sim->Debug_GrantMarks(5000000);
			}
			Sim->Debug_SetMarketPrice(NIce, 3001);
			Sim->Debug_SetMarketPrice(NStruct, 5001);
			const int32 Eff = Sim->GetBookingSolNow();
			if (BuyBp > 0) { Sim->Debug_AddTariff(NZarya, NIce, 1, BuyBp, Eff, Eff + 5); }
			if (BuyBp2 > 0) { Sim->Debug_AddTariff(NZarya, NIce, 3, BuyBp2, Eff, Eff + 1); }
			if (SellBp > 0) { Sim->Debug_AddTariff(NZarya, NStruct, 2, SellBp, Eff, Eff + 5); }
			const bool bBooked = Order(NZarya, Text);
			if (bMove)
			{
				// A full price step each way between the booking and the clearing: the most the reservation was sized for.
				Sim->Debug_SetMarketPrice(NIce, 3001 * 10500 / 10000 + 1);
				Sim->Debug_SetMarketPrice(NStruct, FMath::Max<int64>(0, 5001 * 9500 / 10000 - 1));
			}
			RollNext();
			const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
			O.bOk = bIssued && bBooked && Rec && Rec->Rival == NZarya;
			if (Rec)
			{
				O.Net = Rec->NetMcr; O.Tariff = Rec->TariffMcr; O.Settled = Rec->SettledMcr; O.Released = Rec->ReleasedMcr; O.Applied = Rec->LineMarkupBp; O.Booked = Rec->BookedMarkupBp; O.EffSol = Rec->EffSol; O.Currency = Rec->Currency;
			}
			O.PaidMcr = Sim->GetTariffPaidMcr();
			O.PaidMmk = Sim->GetTariffPaidMmk();
			O.Rate = Sim->GetRate();
			for (int32 s = 0; s < Sim->GetLedgerSlotCount(); ++s) { O.Prices += FString::Printf(TEXT("%lld "), Sim->GetMarketPriceMcr(Sim->GetLedgerSlotName(s))); }
			for (const FName R : { NIce, NStruct }) { int64 So = 0, Bo = 0; Sim->GetMarketTally(R, 1, So, Bo); O.Tally += FString::Printf(TEXT("%s %lld/%lld "), *R.ToString(), So, Bo); }
			{ int64 So = 0, Bo = 0; Sim->GetBlocTally(1, So, Bo); O.Tally += FString::Printf(TEXT("bloc %lld/%lld"), So, Bo); }
			return O;
		};
		const FString Both = TEXT("Ice:333;Struct:-7");
		const FTaxed Z0 = RunTaxed(false, 0, 0, 0, false, Both);
		const FTaxed Z1 = RunTaxed(false, 1, 1, 0, false, Both);
		const FTaxed Z25 = RunTaxed(false, 2500, 9000, 0, false, Both);
		const FTaxed Z90 = RunTaxed(false, 9000, 2500, 0, false, Both);
		ExpectSeq({
			[&]() { return Z0.bOk && Z1.bOk && Z25.bOk && Z90.bOk; },
			[&]() { return Z0.Net == 964326 && Z0.Tariff == 0 && Z0.Applied == TArray<int32>({ 0, 0 }); },
			[&]() { return Z1.Net == 964430 && Z1.Tariff == 104 && Z1.Applied == TArray<int32>({ 1, 1 }); },
			[&]() { return Z25.Net == 1245667 && Z25.Tariff == 281341 && Z25.Applied == TArray<int32>({ 2500, 9000 }); },
			[&]() { return Z90.Net == 1872478 && Z90.Tariff == 908152 && Z90.Applied == TArray<int32>({ 9000, 2500 }); },
			[&]() { return Z25.Booked == Z25.Applied && Z90.Booked == Z90.Applied && Z1.Booked == Z1.Applied; },
			[&]() { return Z25.PaidMcr == 281341 && Z25.PaidMmk == 0 && Z0.PaidMcr == 0; },
			[&]() { return Z25.Prices == Z0.Prices && Z90.Prices == Z0.Prices && Z25.Tally == Z0.Tally && Z90.Tally == Z0.Tally; },
			}, FString::Printf(TEXT("LV4 Ice:333 bought at 3.001 cr and Struct:7 sold at 5.001 cr clear at net %s / %s / %s / %s cr with the buy at 0 / 1 / 2,500 / 9,000 bp and the sell at 0 / 1 / 9,000 / 2,500 bp (the tariffs 0, 0.104, 281.341 and 908.152 cr, rounded against the colony line by line); each order's booking markups equal its clearing markups; TariffPaidMcr counts it; the prices and the tally equal the untaxed twin's"),
				*Cr(Z0.Net), *Cr(Z1.Net), *Cr(Z25.Net), *Cr(Z90.Net)));
		// Two tariffs on one line sum and clamp (5,000 + 5,000 to 9,000); a tariff names its resource and its direction (an Ice buy tariff does
		// not touch the Struct sell, a sell tariff not the buy); the window's edges: FromSol is in, UntilSol is out.
		const FTaxed Two = RunTaxed(false, 5000, 0, 5000, false, Both);
		Reset();
		const int32 Base = Sim->GetMarketCheckedSol() + 10;
		Sim->Debug_AddTariff(NZarya, NIce, 1, 700, Base, Base + 3);
		Sim->Debug_AddTariff(NZarya, NStruct, 2, 400, Base, Base + 3);
		ExpectSeq({
			[&]() { return Two.bOk && Two.Applied == TArray<int32>({ 9000, 0 }) && Two.Tariff == 899400; },
			[&]() { return Sim->GetLineMarkupBp(NZarya, NIce, 1, Base) == 700 && Sim->GetLineMarkupBp(NZarya, NIce, 2, Base) == 0 && Sim->GetLineMarkupBp(NZarya, NWater, 1, Base) == 0; },
			[&]() { return Sim->GetLineMarkupBp(NZarya, NStruct, 2, Base) == 400 && Sim->GetLineMarkupBp(NZarya, NStruct, 1, Base) == 0; },
			[&]() { return Sim->GetLineMarkupBp(NZarya, NIce, 1, Base - 1) == 0 && Sim->GetLineMarkupBp(NZarya, NIce, 1, Base + 2) == 700 && Sim->GetLineMarkupBp(NZarya, NIce, 1, Base + 3) == 0; },
			[&]() { return Sim->GetLineMarkupBp(NMeridian, NIce, 1, Base) == 0; },
			}, FString::Printf(TEXT("LV4 two tariffs of 5,000 bp on one Ice buy sum and clamp to 9,000 (tariff %s cr); a tariff covers only its resource and its direction (Ice buys, Struct sells; not Water, not the other way, not another neighbour); its window [%d, %d) covers %d and %d, not %d or %d"),
				*Cr(Two.Tariff), Base, Base + 3, Base, Base + 2, Base - 1, Base + 3));
		// The reservation covers the charge when both prices move a full step against the colony, in credits and in marks; the tariff goes to
		// TariffPaidMcr in credits and to TariffPaidMmk in marks; the bloc tally and the rate equal the untaxed twin's.
		const FTaxed MoveCr = RunTaxed(false, 2500, 9000, 0, true, Both);
		const FTaxed MoveMk = RunTaxed(true, 2500, 9000, 0, true, Both);
		const FTaxed MoveMk0 = RunTaxed(true, 0, 0, 0, true, Both);
		ExpectSeq({
			[&]() { return MoveCr.bOk && MoveCr.Currency == 0 && MoveCr.Net == 1308696 && MoveCr.Settled == MoveCr.Net && MoveCr.Settled <= MoveCr.Released; },
			[&]() { return MoveCr.Released == 1308702; },
			[&]() { return MoveMk.bOk && MoveMk.Currency == 1 && MoveMk.Net == 1308696 && MoveMk.Settled <= MoveMk.Released && MoveMk.PaidMmk == MoveMk.Tariff && MoveMk.PaidMcr == 0; },
			[&]() { return MoveMk0.bOk && MoveMk0.Tariff == 0 && MoveMk0.Tally == MoveMk.Tally && MoveMk0.Prices == MoveMk.Prices && MoveMk0.Rate == MoveMk.Rate; },
			}, FString::Printf(TEXT("LV4 with Ice a full step up (3.152) and Struct a full step down (4.749) the charge %s cr stays within the reservation %s cr (HiM = Hi + ceil(Hi x m), LoM = Lo - ceil(Lo x m)); the same order in marks charges %s marks within %s and its tariff %s goes to TariffPaidMmk; its bloc tally, prices and rate equal the untaxed mark twin's"),
				*Cr(MoveCr.Settled), *Cr(MoveCr.Released), *Cr(MoveMk.Settled), *Cr(MoveMk.Released), *Cr(MoveMk.Tariff)));
		// A non-member's order after the issue, in credits converted at the rate (E5's Currency 2): under the same markups, with Ice a full step
		// up, Struct a full step down and the rate a full step up between its booking and its clearing, the charge stays inside the converted
		// reservation (the markups enter E5's HiR / LoR sums), and its tariff counts in TariffPaidMmk (the market counts in marks), never in
		// TariffPaidMcr. By hand: HiM(3.001) = 3.152 + 0.788 and LoM(5.001) = 4.749 - 4.275 marks; at 1.001 cr a mark HiR 1.022 and LoR 0.979, so
		// the reservation is ceil(333 x 3.940 x 1.022) - floor(7 x 0.474 x 0.979) = 1340.885 - 3.248 = 1337.637 cr; the net 1308.696 marks
		// at 1.021 cr a mark is charged ceil(1336.178616) = 1336.179 cr.
		{
			const FName NOut(TEXT("LvOut"));
			Inject(NOut, 40.0, TEXT("Ice:50;Struct:10"), TEXT("Ice:10"), TEXT("Ice:900;Struct:200"), TEXT("Ice:4000;Struct:900"), TEXT(""), TEXT(""), -1, 60.f);
			Reset();
			Sim->Debug_SyncRivalEconomies();
			Provision(20000000);
			Sim->Debug_SetRequisitionInputs(20.0, Sim->GetEarthTension());
			Sim->Debug_SyncPoliticalView();
			bool bIssued = Send(TEXT("Currency"), FName("Issue"));
			RollNext();
			RollNext();
			bIssued = bIssued && Sim->IsMarketInMarks();
			Sim->Debug_SetMarketPrice(NIce, 3001);
			Sim->Debug_SetMarketPrice(NStruct, 5001);
			Sim->Debug_SetRate(1001);
			const int32 Eff = Sim->GetBookingSolNow();
			Sim->Debug_AddTariff(NOut, NIce, 1, 2500, Eff, Eff + 5);
			Sim->Debug_AddTariff(NOut, NStruct, 2, 9000, Eff, Eff + 5);
			const int64 Paid0Mcr = Sim->GetTariffPaidMcr(), Paid0Mmk = Sim->GetTariffPaidMmk();
			const bool bBooked = Order(NOut, Both);
			const FRHConvoy* Cv = Sim->GetConvoys().Num() > 0 ? &Sim->GetConvoys().Last() : nullptr;
			const int64 Reserved = Cv ? Cv->ReservedMcr : -1;
			const bool bCredit = Cv && !Cv->bMarks && Cv->Rival == NOut;
			Sim->Debug_SetMarketPrice(NIce, 3001 * 10500 / 10000 + 1);
			Sim->Debug_SetMarketPrice(NStruct, FMath::Max<int64>(0, 5001 * 9500 / 10000 - 1));
			Sim->Debug_SetRate(1001 * 10200 / 10000);
			RollNext();
			const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
			const int64 PaidMmk = Sim->GetTariffPaidMmk() - Paid0Mmk, PaidMcr = Sim->GetTariffPaidMcr() - Paid0Mcr;
			ExpectSeq({
				[&]() { return bIssued && bBooked && bCredit && Rec && Rec->Rival == NOut && Sim->GetBlocMemberFromSol(NOut) < 0; },
				[&]() { return Rec->Currency == 2 && Rec->RateMcr == 1021 && Rec->LineMarkupBp == TArray<int32>({ 2500, 9000 }) && Rec->BookedMarkupBp == Rec->LineMarkupBp; },
				[&]() { return Reserved == 1337637 && Rec->ReleasedMcr == Reserved; },
				[&]() { return Rec->NetMcr == 1308696 && Rec->TariffMcr == 292323 && Rec->SettledMcr == 1336179 && Rec->SettledMcr <= Rec->ReleasedMcr; },
				[&]() { return PaidMmk == Rec->TariffMcr && PaidMcr == 0; },
				}, FString::Printf(TEXT("LV4 a non-member's order after the issue converts (Currency %d): the same markups (Ice 2,500 bp bought, Struct 9,000 bp sold) reserve %s cr at 1.001 cr a mark; with both prices and the rate a full step against the colony its net %s marks is charged %s cr, inside the reservation; its tariff %s goes to TariffPaidMmk (+%s), TariffPaidMcr +%s"),
					Rec ? (int32)Rec->Currency : -1, *Cr(Reserved), *Cr(Rec ? Rec->NetMcr : 0), *Cr(Rec ? Rec->SettledMcr : 0), *Cr(Rec ? Rec->TariffMcr : 0), *Cr(PaidMmk), *Cr(PaidMcr)));
			RetireAll();
		}
	}

	// --- LV5. Pacts: every CanSignPact refusal with its text; signing (Influence -15, tension +5, HumanNature +3, one journal entry of +5,000,
	// the log line); the cargo cap (1,500 kg refused at c + 1, accepted at c + 2); breaking (relation -25, HumanNature -10, tension -5 journaled
	// -5,000, the betrayal tariff [e + 2, e + 32)); a caught covert op ends the pact with the tariff and a clean one does not; a Comply ends it with
	// no tariff; re-signing is refused while the tariff runs; Q-A (a): Earth embargoes Meridian, never the partner, and with no other neighbour nobody. ---
	{
		Reset();
		Provision(20000000);
		FString W0, W1, W1b, W2, W3, W3b, W4, W5;
		Sim->CanSignPact(NMeridian, W0);
		Sim->CanSignPact(FName("Nowhere"), W1);
		Sim->Debug_SetLeversEnabled(false);
		Sim->CanSignPact(FName("Nowhere"), W1b);         // no contact and the switch off: check 1 before check 2
		Sim->CanSignPact(NZarya, W2);
		Sim->Debug_SetLeversEnabled(true);
		Sim->CanSignPact(NZarya, W3);                     // relation 40
		Sim->Debug_CloseRoute(NZarya);
		Sim->CanSignPact(NZarya, W3b);                    // relation 40 and a closed route: the walk's block (check 4) before the relation (check 5)
		Sim->Debug_ClearPolitics();
		Sim->Debug_SetRelation(NZarya, 60.0);
		Sim->CanSignPact(NZarya, W4);                     // Influence 0
		Sim->Debug_CloseRoute(NZarya);
		Sim->CanSignPact(NZarya, W5);                     // the walk's block before the Influence (check 4 before check 6)
		Sim->Debug_ClearPolitics();
		Sim->Debug_AddInfluence(40.0);
		FString WOk;
		const bool bCan = Sim->CanSignPact(NZarya, WOk);
		ExpectSeq({
			[&]() { return W0 == TEXT("No trade contact 'Meridian'") && W1 == TEXT("No trade contact 'Nowhere'") && W1b == W1; },
			[&]() { return W2 == TEXT("No treaties: disabled"); },
			[&]() { return W3 == TEXT("Zarya Station does not trust you enough for a pact yet (relation 40, needs 50)"); },
			[&]() { return W3b == HeadClosed; },
			[&]() { return W4 == FString::Printf(TEXT("Not enough Influence for a pact (15 needed, %.0f on hand)"), Sim->GetInfluence() - 40.0); },
			[&]() { return W5 == HeadClosed; },
			[&]() { return bCan && WOk.IsEmpty(); },
			}, FString::Printf(TEXT("LV5 CanSignPact refuses in its order: '%s' (the contact before the switch, '%s'); '%s'; '%s'; at relation 40 with a closed route '%s' (the block before the relation); '%s'; '%s' (the block before the Influence); with relation 60 and Influence 40 it signs"), *W0, *W1b, *W2, *W3, *W3b, *W4, *W5));
		// Signing.
		const double Inf0 = Sim->GetInfluence(), Ten0 = Sim->GetEarthTension(), HN0 = Sim->GetHumanNatureAxis();
		const int32 Journal0 = Sim->GetPoliticalJournal().Num();
		Capture.Clear();
		const double SignAt = Clock->GetSimSecondsTotal() + Sim->GetOrderLagSeconds();
		const bool bSigned = Send(TEXT("Treaty"), NZarya, TEXT("Sign"));
		const int32 C = SolOf(SignAt);
		const FRHTradeRule* P = RuleOf(EKind::Pact, NZarya);
		const TArray<FRHPoliticalEntry>& J = Sim->GetPoliticalJournal();
		const bool bJournal = J.Num() == Journal0 + 1 && J.Last().Kind == (uint8)EAct::PactSign && J.Last().TensionDeltaMilli == 5000 && !J.Last().bHasIdentity && J.Last().ClearSol == C;
		FString WAgain;
		Sim->CanSignPact(NZarya, WAgain);
		ExpectSeq({
			[&]() { return bSigned && P && P->bLive && P->Origin == EOrigin::Signed && P->FromSol == C + 2 && P->UntilSol == -1; },
			[&]() { return Near(Sim->GetInfluence(), Inf0 - 15.0, 0.01) && Near(Sim->GetEarthTension(), FMath::Min(100.0, Ten0 + 5.0), 0.05) && Near(Sim->GetHumanNatureAxis(), HN0 + 3.0); },
			[&]() { return bJournal; },
			[&]() { return Capture.CountLines(*FString::Printf(TEXT("=== PACT SIGNED: Zarya Station (influence %.0f, tension %.0f, terms from sol %d) ==="), Inf0 - 15.0, FMath::Min(100.0, Ten0 + 5.0), C + 2)) == 1; },
			[&]() { return WAgain == TEXT("You already have a pact with Zarya Station"); },
			[&]() { return Sim->HasLivePact(NZarya) && Sim->GetLivePactFromSol(NZarya) == C + 2; },
			}, FString::Printf(TEXT("LV5 a Treaty Sign executing in sol %d makes a live pact [%d, open): Influence %.0f -> %.0f, tension +5, HumanNature +3, one journal entry of +5,000 keyed to sol %d, the log line; a second Sign is refused ('%s')"),
				C, C + 2, Inf0, Sim->GetInfluence(), C, *WAgain));
		// The cargo cap: a 1,500 kg order clearing in c + 1 is refused at 1,000 kg; one clearing in c + 2 is accepted at the pact's 2,000.
		Sim->AddStock(NHyd, 200.0);
		const auto At = [&](double Sec, const TCHAR* Verb, FName Target, const FString& Text) -> bool
		{
			const int32 Rej = Rejects.Num(), Conv = Sim->GetConvoys().Num();
			FRHCommand Cmd; Cmd.Verb = FName(Verb); Cmd.Target = Target; Cmd.Text = Text;
			Sim->Debug_EnqueueAt(Cmd, Sec);
			int32 Steps = 0;
			while (Sim->GetUplinkQueue().Num() > 0 && Steps++ < 200) { EraOnce(); }
			return Rejects.Num() == Rej && (FName(Verb) != FName("MarketOrder") || Sim->GetConvoys().Num() == Conv + 1);
		};
		const bool bCapRefused = !At((double)(C + 1) * SolLen + 100.0, TEXT("MarketOrder"), NZarya, TEXT("Ice:1500"));
		const FString CapWhy = LastReject();
		const bool bCapOk = At((double)(C + 2) * SolLen + 100.0, TEXT("MarketOrder"), NZarya, TEXT("Ice:1500"));
		FRHConvoyTerms Terms;
		const bool bTerms = Sim->GetConvoys().Num() > 0 && Sim->GetConvoyTerms(Sim->GetConvoys().Last().Id, Terms) && Terms.EffSol == C + 2 && Terms.LineMarkupBp == TArray<int32>({ 0 });
		ExpectSeq({
			[&]() { return bCapRefused && CapWhy == TEXT("A convoy carries at most 1000 kg each way: this order buys 1500 kg and sells 0 kg"); },
			[&]() { return bCapOk && bTerms; },
			}, FString::Printf(TEXT("LV5 the pact's cargo cap reads the order's clearing sol: 1,500 kg of Ice clearing in sol %d is refused ('%s'), clearing in sol %d it is booked (terms: EffSol %d, markup 0)"), C + 1, *CapWhy, C + 2, Terms.EffSol));
		// Breaking.
		Sim->Debug_SetRelation(NZarya, 60.0);
		const double Ten1 = Sim->GetEarthTension(), HN1 = Sim->GetHumanNatureAxis();
		const int32 Journal1 = Sim->GetPoliticalJournal().Num();
		Capture.Clear();
		const double BreakAt = Clock->GetSimSecondsTotal() + Sim->GetOrderLagSeconds();
		const bool bBroke = Send(TEXT("Treaty"), NZarya, TEXT("break"));
		const int32 E = SolOf(BreakAt);
		const FRHTradeRule* PB = RuleOf(EKind::Pact, NZarya);
		const FRHTradeRule* T = RuleOf(EKind::Tariff, NZarya);
		const TArray<FRHPoliticalEntry>& J2 = Sim->GetPoliticalJournal();
		const bool bJournal2 = J2.Num() == Journal1 + 1 && J2.Last().Kind == (uint8)EAct::PactBreak && J2.Last().TensionDeltaMilli == -5000;
		const double RelBroken = Sim->GetRivalRelation(NZarya), HNBroken = Sim->GetHumanNatureAxis(), TenBroken = Sim->GetEarthTension();
		Sim->Debug_SetRelation(NZarya, 60.0);
		Sim->Debug_AddInfluence(-1000.0);
		FString WShort, WResign, WNoPact;
		Sim->CanSignPact(NZarya, WShort);                 // relation 60, no Influence and the tariff running: the Influence (check 6) before it (check 7)
		Sim->Debug_AddInfluence(40.0);
		Sim->CanSignPact(NZarya, WResign);
		Sim->CanBreakPact(NZarya, WNoPact);
		ExpectSeq({
			[&]() { return bBroke && PB && !PB->bLive && PB->UntilSol == FMath::Max(E + 2, PB->FromSol); },
			[&]() { return Near(RelBroken, 35.0) && Near(HNBroken, HN1 - 10.0) && Near(TenBroken, FMath::Max(0.0, Ten1 - 5.0), 0.05) && bJournal2; },
			[&]() { return T && T->Origin == EOrigin::Betrayal && T->ResSlot == -1 && T->Dir == 3 && T->MarkupBp == 2500 && T->FromSol == E + 2 && T->UntilSol == E + 32; },
			[&]() { return Capture.CountLines(*FString::Printf(TEXT("); they charge 25%% on every trade from sol %d to sol %d ==="), E + 2, E + 31)) == 1 && Capture.CountLines(TEXT("=== PACT BROKEN: Zarya Station (relation 35, humanNature")) == 1; },
			[&]() { return WShort == TEXT("Not enough Influence for a pact (15 needed, 0 on hand)") && WResign == FString::Printf(TEXT("Zarya Station will not sign while it still charges you for the last broken pact (until sol %d)"), E + 31); },
			[&]() { return WNoPact == TEXT("You have no pact with Zarya Station"); },
			}, FString::Printf(TEXT("LV5 a Treaty Break (any case) in sol %d ends the pact at sol %d, relation 60 -> 35, HumanNature -10, tension -5 journaled -5,000, and Zarya charges 2,500 bp on every resource both ways in [%d, %d); re-signing is refused ('%s'; with no Influence, the Influence's text first: '%s'); a second Break is refused ('%s')"),
				E, PB ? PB->UntilSol : -1, E + 2, E + 32, *WResign, *WShort, *WNoPact));
	}
	{
		// A clean covert op leaves the pact standing; a caught one ends it from the partner's side with the betrayal tariff (Zarya's U is 0.907 at
		// attempt 0 and under 0.16 at attempt 1: by day at relation 60 the eyes are P = 0.35).
		Reset();
		Provision(20000000);
		Sim->Debug_SetRelation(NZarya, 60.0);
		Sim->Debug_AddInfluence(40.0);
		Clock->Debug_SetSimSeconds(FMath::FloorToDouble(Clock->GetSimSecondsTotal() / SolLen) * SolLen + 0.45 * SolLen - EraDt);
		EraOnce();
		Send(TEXT("Treaty"), NZarya, TEXT("Sign"));
		const double U0 = LevSuiteCovertU(TEXT("Zarya"), 0), U1 = LevSuiteCovertU(TEXT("Zarya"), 1);
		const int32 Att0 = Sim->GetCovertAttempts();
		Send(TEXT("Covert"), NZarya);
		const bool bStands = Sim->HasLivePact(NZarya) && RulesOf(EKind::Tariff, NZarya) == 0;
		Capture.Clear();
		const double CaughtAt = Clock->GetSimSecondsTotal() + Sim->GetOrderLagSeconds();
		const double RelB = Sim->GetRivalRelation(NZarya), HNB = Sim->GetHumanNatureAxis();
		Send(TEXT("Covert"), NZarya);
		const FRHTradeRule* T = RuleOf(EKind::Tariff, NZarya);
		const int32 K = SolOf(CaughtAt);
		ExpectSeq({
			[&]() { return Att0 == 0 && U0 >= 0.5 && U1 < 0.35 && !Sim->IsNight(); },
			[&]() { return bStands; },
			[&]() { return !Sim->HasLivePact(NZarya) && Capture.CountLines(TEXT("=== PACT BROKEN by Zarya Station: they caught you ===")) == 1; },
			[&]() { return T && T->Origin == EOrigin::Betrayal && T->FromSol == K + 2 && T->UntilSol == K + 32 && T->MarkupBp == 2500; },
			[&]() { return Near(Sim->GetRivalRelation(NZarya), FMath::Max(0.0, RelB - 30.0)) && Near(Sim->GetHumanNatureAxis(), HNB - 6.0); },
			}, FString::Printf(TEXT("LV5 a partner's covert ops: attempt 0 (U %.3f) is clean and the pact stands; attempt 1 (U %.3f, P 0.35 by day) is caught, the pact ends from Zarya's side with the betrayal tariff [%d, %d), and only HEAD's caught effects apply (relation -30, HumanNature -6)"),
				U0, U1, K + 2, K + 32));
	}
	{
		// A Comply that closes the partner's route ends the pact, with no tariff.
		Reset();
		Sim->Debug_SetRelation(NZarya, 60.0);
		Sim->Debug_AddInfluence(40.0);
		Send(TEXT("Treaty"), NZarya, TEXT("Sign"));
		Sim->Debug_SetRequisitionInputs(Sim->GetIdentityAxis(), 65.0);
		EraOnce();
		Capture.Clear();
		const double ComplyAt = Clock->GetSimSecondsTotal() + Sim->GetOrderLagSeconds();
		Send(TEXT("Solidarity"), FName("Comply"));
		const FRHTradeRule* P = RuleOf(EKind::Pact, NZarya);
		const int32 K = SolOf(ComplyAt);
		ExpectSeq({
			[&]() { return P && !P->bLive && P->UntilSol == FMath::Max(K + 2, P->FromSol) && Sim->IsRouteClosed(NZarya); },
			[&]() { return RulesOf(EKind::Tariff, NAME_None) == 0 && Capture.CountLines(TEXT("=== PACT ENDED: Zarya Station (the route is closed) ===")) == 1; },
			}, FString::Printf(TEXT("LV5 a Comply in sol %d closes Zarya's route and ends its pact at sol %d, with no tariff"), K, P ? P->UntilSol : -1));
	}
	{
		// Q-A (a): Earth cannot embargo a pact partner. With Meridian discovered it embargoes Meridian; with no other neighbour, nobody.
		const auto LeanOn = [&](bool bMeridian) -> FName
		{
			Reset();
			Sim->Debug_SetRelation(NZarya, 60.0);
			Sim->Debug_AddInfluence(40.0);
			Send(TEXT("Treaty"), NZarya, TEXT("Sign"));
			if (bMeridian) { Sim->Debug_DiscoverRival(NMeridian); }
			Sim->Debug_SetRequisitionInputs(Sim->GetIdentityAxis(), 80.0);
			EraOnce();
			EraOnce();
			return Sim->GetEmbargoingRival();
		};
		const FName WithMeridian = LeanOn(true);
		const bool bZaryaFree = !Sim->IsEmbargoed(NZarya) && Sim->HasLivePact(NZarya);
		const FName Alone = LeanOn(false);
		ExpectSeq({
			[&]() { return WithMeridian == NMeridian && bZaryaFree; },
			[&]() { return Alone.IsNone() && Sim->GetEarthTension() >= 75.0 && RulesOf(EKind::Embargo, NAME_None) == 0; },
			}, FString::Printf(TEXT("LV5 Q-A (a): at tension 80 Earth embargoes %s (discovered), never the partner Zarya; with no other neighbour it embargoes %s"),
				WithMeridian.IsNone() ? TEXT("nobody") : *WithMeridian.ToString(), Alone.IsNone() ? TEXT("nobody") : *Alone.ToString()));
	}

	// --- LV6. Sabotage moves prices: in sol 0 of the fresh colony (MX2's fixture: Zarya's stores at StoreStart, every price at its Earth value,
	// no tally), a sabotage gives the window [1, 7); at the rolls closing 0..7 Zarya's five stores and the seven traded prices equal the
	// integers below, from a separate model (the roll closing 7 makes Zarya's goods again: Ice +250 kg). The live refusal and timer are HEAD's;
	// a second sabotage in sol 3, after real era steps, makes a second rule: the remaining time is the newest's full 6 while the first's has
	// counted down, and the rival recovers once, when the second runs out (HEAD's reset timer); a barter rival's window moves nothing. ---
	{
		static const int64 CutStores[8][5] = { { 12250000, 1980000, 1500000, 2420000, 295000 }, { 12100000, 1840000, 1440000, 2300000, 280000 }, { 11950000, 1700000, 1380000, 2180000, 265000 },
			{ 11800000, 1560000, 1320000, 2060000, 250000 }, { 11650000, 1420000, 1260000, 1940000, 235000 }, { 11500000, 1280000, 1200000, 1820000, 220000 },
			{ 11350000, 1140000, 1140000, 1700000, 205000 }, { 11600000, 1120000, 1140000, 1620000, 200000 } };
		static const int64 CutPrices[8][7] = { { 2850, 4112, 8000, 5250, 12325, 20928, 202060 }, { 2992, 4317, 8259, 5512, 12709, 21850, 203676 }, { 3141, 4532, 8519, 5787, 13087, 22765, 204898 },
			{ 3298, 4758, 8780, 6076, 13458, 23668, 205758 }, { 3462, 4995, 9041, 6379, 13822, 24557, 206292 }, { 3635, 5244, 9302, 6697, 14179, 25431, 206518 },
			{ 3801, 5506, 9563, 7031, 14529, 26285, 206498 }, { 3611, 5547, 9515, 7277, 14781, 27118, 206230 } };
		static const int64 BasePrices[8][7] = { { 2850, 4112, 8000, 5250, 12325, 20928, 202060 }, { 2708, 4217, 8000, 5512, 12633, 21850, 203676 }, { 2573, 4315, 8000, 5787, 12923, 22765, 204898 },
			{ 2447, 4407, 8000, 6076, 13196, 23668, 205758 }, { 2334, 4492, 8000, 6360, 13453, 24557, 206292 }, { 2231, 4571, 8000, 6630, 13695, 25431, 206518 },
			{ 2137, 4645, 8000, 6883, 13920, 26285, 206498 }, { 2051, 4713, 8000, 7119, 14132, 27118, 206230 } };
		const auto Snap = [&](int32 k, bool bCut, FString& Bad) -> bool
		{
			bool bAll = true;
			for (int32 i = 0; i < 5; ++i)
			{
				const int64 Got = Sim->GetRivalStoreG(NZarya, ZaryaSlots[i]);
				if (bCut && Got != CutStores[k][i]) { bAll = false; Bad += FString::Printf(TEXT("roll %d %s store %lld (want %lld); "), k, *ZaryaSlots[i].ToString(), Got, CutStores[k][i]); }
			}
			for (int32 i = 0; i < 7; ++i)
			{
				const int64 Got = Sim->GetMarketPriceMcr(TradedSlots[i]), Want = bCut ? CutPrices[k][i] : BasePrices[k][i];
				if (Got != Want) { bAll = false; Bad += FString::Printf(TEXT("roll %d %s %lld (want %lld); "), k, *TradedSlots[i].ToString(), Got, Want); }
			}
			return bAll;
		};
		// The undisturbed path first (the model's other half).
		Reset();
		const bool bFixture = Sim->GetMarketCheckedSol() == 0 && SolOf(Clock->GetSimSecondsTotal()) == 0 && Sim->GetRivalStoreG(NZarya, NIce) == 12000000 && Sim->GetMarketPriceMcr(NIce) == 3000;
		bool bBase = true;
		FString BadBase;
		for (int32 k = 0; k < 8; ++k) { RollNext(); bBase &= Snap(k, false, BadBase); }
		// The sabotage in sol 0, by the worker itself (no step runs, so the timer is read before any countdown).
		Reset();
		FString Why;
		const bool bSab = Sim->ResolveSabotage(ActCmd(TEXT("Sabotage"), NZarya, Clock->GetSimSecondsTotal()), NZarya, Why);
		const FRHTradeRule* D = RuleOf(EKind::Disrupted, NZarya);
		const bool bWindow = D && D->FromSol == 1 && D->UntilSol == 7 && D->Origin == EOrigin::Sabotage && D->bLive && D->LiveSols == 6.0;
		FString Live;
		Sim->CanPlaceStandingOrder(NZarya, Live);
		const double Remaining = Sim->GetSabotageRemaining(NZarya);
		bool bCutPath = true;
		FString BadCut;
		for (int32 k = 0; k < 8; ++k) { RollNext(); bCutPath &= Snap(k, true, BadCut); }
		FString CutSols;
		for (const FRHProductionCut& Cut : Sim->GetProductionCuts()) { CutSols += FString::Printf(TEXT("%d%s "), Cut.ClosingSol, Cut.Rival == NZarya && Cut.CutPct == 100 ? TEXT("") : TEXT("?")); }
		ExpectSeq({
			[&]() { return bFixture && bBase; },
			[&]() { return bSab && bWindow; },
			[&]() { return Live == HeadDisrupted && Remaining == 6.0; },
			[&]() { return bCutPath; },
			[&]() { return CutSols == TEXT("1 2 3 4 5 6 "); },
			}, FString::Printf(TEXT("LV6 a sabotage of Zarya in sol 0 gives the window [1, 7) and HEAD's live refusal ('%s', 6 sols); at the rolls closing 0-7 Zarya's stores and the seven traded prices are the hand-computed integers (Ice 2850 -> 3801 mcr at the roll closing 6 against 2137 undisturbed; its store falls 150 kg a sol, then the roll closing 7 makes 400 again), and the cut lands at the rolls closing %s%s%s"),
				*Live, *CutSols, *BadBase, *BadCut));
		// A second sabotage at c + 3 makes a second rule. Real era steps run from the first act (sol 0) into sol 3, so the first rule's timer
		// counts down (StepEarth) before the second act lands in sol 3: the remaining time is then the newest rule's full 6 - the largest live
		// timer, as HEAD's reset gave - while the first keeps its decayed timer; stepping on, the first runs out silently under the second and
		// the rival recovers once, when the second runs out (HEAD's one alert, from its one reset timer).
		Reset();
		const double Duration = DefsSub->GetConfigScalar(FName("SabotageDurationSols"), 6.0);
		const bool bFirstSab = Sim->ResolveSabotage(ActCmd(TEXT("Sabotage"), NZarya, Clock->GetSimSecondsTotal()), NZarya, Why);
		int32 StepsTo3 = 0;
		while (SolOf(Clock->GetSimSecondsTotal()) < 3 && StepsTo3 < 400) { EraOnce(); ++StepsTo3; }
		double FirstWant = Duration;
		for (int32 i = 0; i < StepsTo3; ++i) { FirstWant -= DtSols; }
		const double SecondAt = Clock->GetSimSecondsTotal();
		const bool bSecondSab = Sim->ResolveSabotage(ActCmd(TEXT("Sabotage"), NZarya, SecondAt), NZarya, Why);
		const double RemainingBoth = Sim->GetSabotageRemaining(NZarya);
		TArray<FString> Windows;
		TArray<double> Timers;
		for (const FRHTradeRule& R : Sim->GetTradeRules())
		{
			if (R.Kind == EKind::Disrupted && R.Party == NZarya) { Windows.Add(FString::Printf(TEXT("#%d [%d, %d)"), R.Id, R.FromSol, R.UntilSol)); Timers.Add(R.LiveSols); }
		}
		const int32 Alerts0 = Alerts.Num();
		int32 StepsOut = 0;
		while (Sim->GetSabotageRemaining(NZarya) > 0.0 && StepsOut < 400) { EraOnce(); ++StepsOut; }
		int32 Recovered = 0;
		double RecoveredAt = -1.0;
		for (int32 a = Alerts0; a < Alerts.Num(); ++a)
		{
			if (Alerts[a].Value.StartsWith(TEXT("RECOVERED")) && Alerts[a].Value.Contains(TEXT("Zarya Station's systems are back online"))) { ++Recovered; RecoveredAt = Alerts[a].Key; }
		}
		double T2 = Duration;
		int32 N2 = 0;
		do { T2 -= DtSols; ++N2; } while (T2 > 0.0);
		const double WantRecovered = SecondAt + (double)N2 * EraDt; // the act ran between steps: its timer first falls on the next step
		// A barter rival's window moves nothing in the market.
		Reset();
		Inject(FName("LvBarter"), 50.0, TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("Ice:10"), TEXT("Struct:10"), -1, 30.f);
		Sim->Debug_SyncRivalEconomies();
		Sim->ResolveSabotage(ActCmd(TEXT("Sabotage"), FName("LvBarter"), Clock->GetSimSecondsTotal()), FName("LvBarter"), Why);
		const bool bBarterWindow = RuleOf(EKind::Disrupted, FName("LvBarter")) && RuleOf(EKind::Disrupted, FName("LvBarter"))->FromSol == 1;
		bool bBarter = true;
		FString BadBarter;
		for (int32 k = 0; k < 8; ++k) { RollNext(); bBarter &= Snap(k, false, BadBarter); }
		const int32 BarterCuts = Sim->GetProductionCuts().Num();
		RetireAll();
		// A window made with the levers on cuts nothing once the harness turns them off (3.3: the roll reads the switch too): the market follows
		// the undisturbed path and no cut is recorded.
		Reset();
		Sim->ResolveSabotage(ActCmd(TEXT("Sabotage"), NZarya, Clock->GetSimSecondsTotal()), NZarya, Why);
		const FRHTradeRule* OffWin = RuleOf(EKind::Disrupted, NZarya);
		const bool bOffWindow = OffWin && OffWin->FromSol == 1 && OffWin->UntilSol == 7;
		Sim->Debug_SetLeversEnabled(false);
		bool bOffPath = true;
		FString BadOff;
		for (int32 k = 0; k < 8; ++k) { RollNext(); bOffPath &= Snap(k, false, BadOff); }
		const bool bOffNoCut = Sim->GetProductionCuts().Num() == 0 && Sim->GetRivalCutPct(NZarya, 3) == 0;
		Sim->Debug_SetLeversEnabled(true);
		const bool bOnAgain = Sim->GetRivalCutPct(NZarya, 6) == 100;
		ExpectSeq({
			[&]() { return Windows.Num() == 2 && Windows[0].EndsWith(TEXT("[1, 7)")) && Windows[1].EndsWith(TEXT("[4, 10)")); },
			[&]() { return bFirstSab && bSecondSab && SolOf(SecondAt) == 3 && StepsTo3 > 0; },
			[&]() { return Timers.Num() == 2 && FirstWant < Duration && Timers[0] == FirstWant && Timers[1] == Duration; },
			[&]() { return RemainingBoth == Duration && Duration == 6.0; },
			[&]() { return Recovered == 1 && RecoveredAt == WantRecovered; },
			[&]() { return bBarterWindow && bBarter && BarterCuts == 0; },
			[&]() { return bOffWindow && bOffPath && bOffNoCut && bOnAgain; },
			}, FString::Printf(TEXT("LV6 a second sabotage of Zarya in sol 3 (after %d real era steps from the first, in sol 0) appends a second rule (%s), never updates the first in place; GetSabotageRemaining is the newest's full %.17g while the first's timer has counted down to %.17g (the suite's replay of HEAD's countdown: %.17g); the first runs out silently under the second, and the rival recovers once (%d RECOVERED) at %.0f s, the second timer's end (the act at %.0f s + %d steps); a barter rival's sabotage window cuts nothing and the market follows the undisturbed path %s; a window [1, 7) made with the levers on cuts nothing once the harness turns them off (the undisturbed path, no cut) %s"),
				StepsTo3, *FString::Join(Windows, TEXT(", ")), RemainingBoth, Timers.Num() > 0 ? Timers[0] : -1.0, FirstWant, Recovered, RecoveredAt, SecondAt, N2, *BadBarter, *BadOff));
	}

	// --- LV7. Thefts and offerings: a clean theft lands at the roll closing its own sol, after that roll's convoy clearing, taking min(grams,
	// store); a caught op records nothing; a Launder's offering lands in the store; a barter rival records nothing; E1's legs are HEAD's. ---
	{
		Reset();
		Provision(20000000);
		Sim->Debug_SetRivalStore(NZarya, NIce, 100);
		const bool bBooked = Order(NZarya, TEXT("Ice:60"));
		const double InIce0 = (double)Sim->GetLedgerLifetimeG(NIce, ELeg::In) / 1000.0 + Sim->GetLedgerOpenKg(NIce, ELeg::In);
		const double OutStruct0 = (double)Sim->GetLedgerLifetimeG(NStruct, ELeg::Out) / 1000.0 + Sim->GetLedgerOpenKg(NStruct, ELeg::Out);
		FString Why;
		const int32 Sol0 = SolOf(Clock->GetSimSecondsTotal());
		const bool bClean = Sim->ResolveCovert(ActCmd(TEXT("Covert"), NZarya, Clock->GetSimSecondsTotal()), NZarya, Why);
		const int32 Pending1 = Sim->GetPendingMarketEffects().Num();
		const bool bOne = Pending1 == 1 && Sim->GetPendingMarketEffects()[0].Kind == ERHMarketEffectKind::Theft && Sim->GetPendingMarketEffects()[0].Grams == 75000 && Sim->GetPendingMarketEffects()[0].ClearSol == Sol0;
		const double RelBeforeCaught = Sim->GetRivalRelation(NZarya);
		Capture.Clear();
		const bool bCaught = Sim->ResolveCovert(ActCmd(TEXT("Covert"), NZarya, Clock->GetSimSecondsTotal()), NZarya, Why) && Sim->GetPendingMarketEffects().Num() == 1
			&& Capture.CountLines(TEXT("=== COVERT CAUGHT: Zarya Station")) == 1 && Near(Sim->GetRivalRelation(NZarya), FMath::Max(0.0, RelBeforeCaught - 30.0));
		const double InIce1 = (double)Sim->GetLedgerLifetimeG(NIce, ELeg::In) / 1000.0 + Sim->GetLedgerOpenKg(NIce, ELeg::In);
		Sim->Debug_SetMarketEnabled(false); // no production at the roll: the store shows the clearing and the theft alone
		RollNext();
		const FRHMarketClearing* Rec = Sim->GetMarketClearings().Num() > 0 ? &Sim->GetMarketClearings().Last() : nullptr;
		const FRHMarketEffectApplied* App = Sim->GetMarketEffectsApplied().Num() > 0 ? &Sim->GetMarketEffectsApplied().Last() : nullptr;
		const bool bOrderFirst = Rec && Rec->Lines.Num() == 1 && Rec->Lines[0].FilledKg == 60 && App && App->MovedG == 40000 && App->ClosingSol == Sol0 && Sim->GetRivalStoreG(NZarya, NIce) == 0;
		// The offering.
		Sim->AddStock(NStruct, 100.0);
		const int64 Struct0 = Sim->GetRivalStoreG(NZarya, NStruct);
		const bool bLaunder = Sim->ResolveLaunder(ActCmd(TEXT("Launder"), NZarya, (double)Sim->GetMarketCheckedSol() * SolLen + 10.0), NZarya, Why);
		const bool bOffer = Sim->GetPendingMarketEffects().Num() == 1 && Sim->GetPendingMarketEffects()[0].Kind == ERHMarketEffectKind::Offering && Sim->GetPendingMarketEffects()[0].Grams == 100000;
		RollNext();
		const bool bLanded = Sim->GetRivalStoreG(NZarya, NStruct) == Struct0 + 100000 && Sim->GetPendingMarketEffects().Num() == 0;
		const double OutStruct1 = (double)Sim->GetLedgerLifetimeG(NStruct, ELeg::Out) / 1000.0 + Sim->GetLedgerOpenKg(NStruct, ELeg::Out);
		// A barter rival records nothing - through both paths that reach the recorder: a clean op against it (at relation 60 by day its eyes are
		// P = 0.35, under its U at attempt 2, 0.422, so the clean branch runs: the first build's op at relation 30, P 0.425, was caught and never
		// reached the recorder), then a Launder of the 8 hidden tension that op leaves. Each logs HEAD's line; neither records an effect (the
		// recorder's guards: no economy, no traded slot), and the next roll applies none.
		Sim->Debug_SetMarketEnabled(true);
		const FName NBarter(TEXT("LvBarter2"));
		Inject(NBarter, 50.0, TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("Ice:10"), TEXT("Struct:10"), -1, 30.f);
		Sim->Debug_SyncRivalEconomies();
		Sim->Debug_SetRelation(NBarter, 60.0);
		const int32 BarterAttempt = Sim->GetCovertAttempts();
		const double UBarter = LevSuiteCovertU(TEXT("LvBarter2"), BarterAttempt);
		Capture.Clear();
		const bool bBarterOp = Sim->ResolveCovert(ActCmd(TEXT("Covert"), NBarter, Clock->GetSimSecondsTotal()), NBarter, Why);
		const bool bBarterClean = bBarterOp && !Sim->IsNight() && Capture.CountLines(TEXT("=== COVERT CLEAN: LvBarter2 Post took 5 Ice")) == 1 && Capture.CountLines(TEXT("=== COVERT CAUGHT")) == 0;
		const int32 PendingAfterOp = Sim->GetPendingMarketEffects().Num();
		const bool bBarterLaunder = Sim->ResolveLaunder(ActCmd(TEXT("Launder"), NBarter, Clock->GetSimSecondsTotal()), NBarter, Why) && Capture.CountLines(TEXT("=== LAUNDER: LvBarter2 Post")) == 1;
		const int32 PendingAfterLaunder = Sim->GetPendingMarketEffects().Num();
		RollNext();
		const bool bNoneApplied = !Sim->GetMarketEffectsApplied().ContainsByPredicate([NBarter](const FRHMarketEffectApplied& A) { return A.Effect.Party == NBarter; });
		RetireAll();
		ExpectSeq({
			[&]() { return bBooked && bClean && bOne; },
			[&]() { return bCaught; },
			[&]() { return bOrderFirst; },
			[&]() { return bLaunder && bOffer && bLanded; },
			[&]() { return BarterAttempt == 2 && UBarter > 0.35 && bBarterClean && PendingAfterOp == 0 && bBarterLaunder && PendingAfterLaunder == 0 && bNoneApplied; },
			[&]() { return Near(InIce1 - InIce0, 75.0, 1.0e-6) && Near(OutStruct1 - OutStruct0, 100.0, 1.0e-6); },
			}, FString::Printf(TEXT("LV7 a clean theft records 75 kg of Ice due at the roll closing its own sol; a caught op (its caught line, relation -30) records nothing; at that roll the 60 kg order to Zarya fills first and the theft takes the 40 kg left (min of the store); a Launder's 100 kg of Struct lands in Zarya's store at its roll; a barter rival records nothing: a clean op against it (attempt %d, U %.3f, P 0.35 by day: '=== COVERT CLEAN: LvBarter2 Post took 5 Ice') leaves %d pending effects, its launder %d, and the next roll applies none; E1 records the loot In and the offering Out, as before"),
				BarterAttempt, UBarter, PendingAfterOp, PendingAfterLaunder));
	}

	// --- LV9. The save: a colony with every kind, three pending effects, a terms entry and a moved market saves, loads and saves byte-identical;
	// the mirror slots have HEAD's layout; 'RHL6' follows 'RHE5' straight after its footer (never asserted last); every repair of section 4 on a
	// doctored file (resealed), with the colony loaded, each with its Warning and no Error; a flipped byte and a wrong length rebuild the rules
	// from the mirror and no closed route reopens; a lost 'RHE5' never takes E6's block with it (found by the walk from the end); a case-only
	// Party loads as the row; the previous version is refused. ---
	{
		Reset();
		Provision(20000000);
		for (int32 K = 0; K < 2 * (int32)(SolLen / EraDt); ++K) { EraOnce(); } // a moved market, by real rolls (the open sol stays with the clock)
		Inject(FName("LvA"), 40.0, TEXT("Ice:50"), TEXT("Ice:10"), TEXT("Ice:700"), TEXT("Ice:4000"), TEXT(""), TEXT(""), -1, 30.f);
		Inject(FName("LvB"), 45.0, TEXT("Ice:50"), TEXT("Ice:10"), TEXT("Ice:700"), TEXT("Ice:4000"), TEXT(""), TEXT(""), -1, 30.f);
		Inject(FName("LvC"), 50.0, TEXT("Ice:50"), TEXT("Ice:10"), TEXT("Ice:700"), TEXT("Ice:4000"), TEXT(""), TEXT(""), -1, 30.f);
		Inject(FName("LvD"), 30.0, TEXT("Ice:50;Struct:10"), TEXT("Ice:10"), TEXT("Ice:900;Struct:200"), TEXT("Ice:4000;Struct:900"), TEXT(""), TEXT(""), -1, 60.f);
		Inject(FName("LvE"), 60.0, TEXT(""), TEXT(""), TEXT(""), TEXT(""), TEXT("Ice:10"), TEXT("Struct:10"), -1, 30.f);
		Sim->Debug_SyncRivalEconomies();
		const int32 C = Sim->GetMarketCheckedSol();
		Sim->Debug_SetRelation(NZarya, 60.0);
		Sim->Debug_AddInfluence(40.0);
		const bool bSign = Send(TEXT("Treaty"), NZarya, TEXT("Sign"));
		Sim->Debug_AddTariff(NZarya, NIce, 1, 1500, C, C + 20);
		Sim->Debug_AddTariff(FName("LvD"), NIce, 1, 1500, C, C + 20);
		// The sabotage before the order: the order's step counts its live timer down once, so the mirror's v21 slot holds a decayed timer (a save
		// writing SabotageDurationSols there would differ from it).
		FString Why;
		Sim->ResolveSabotage(ActCmd(TEXT("Sabotage"), NZarya, Clock->GetSimSecondsTotal()), NZarya, Why);
		const bool bBooked = Order(FName("LvD"), TEXT("Ice:200"));
		const int32 LvDConvoy = Sim->GetConvoys().Num() > 0 ? Sim->GetConvoys().Last().Id : -1;
		double SabWant = DefsSub->GetConfigScalar(FName("SabotageDurationSols"), 6.0);
		SabWant -= DtSols;
		// A tariff on every resource (LvC; its window [C + 10, C + 20) covers no booked order), saved with the resource "*".
		Sim->Debug_AddTariff(FName("LvC"), NAME_None, 3, 700, C + 10, C + 20);
		// The rest with no step before the save: the embargo's grace is exactly 5.
		Sim->Debug_CloseRoute(FName("LvA"));
		Sim->Debug_SetDefected(FName("LvB"));
		Sim->Debug_SetEmbargo(FName("LvC"), 5.0);
		const double SabAtSave = Sim->GetSabotageRemaining(NZarya);
		// Three pending effects whose orders (700,777 to 700,779) are the save's newest commands (a fresh process's counter must pass them).
		FRHMarketEffect Fx;
		Fx.ClearSol = C + 3; Fx.CommandId = 700777; Fx.Sub = 0; Fx.Kind = ERHMarketEffectKind::Theft; Fx.Party = NZarya; Fx.Slot = Sim->GetLedgerSlotIndex(NIce); Fx.Grams = 50000;
		Sim->Debug_AddMarketEffect(Fx);
		Fx.ClearSol = C + 4; Fx.CommandId = 700778; Fx.Kind = ERHMarketEffectKind::Offering; Fx.Slot = Sim->GetLedgerSlotIndex(NStruct); Fx.Grams = 20000;
		Sim->Debug_AddMarketEffect(Fx);
		Fx.ClearSol = C + 5; Fx.CommandId = 700779; Fx.Kind = ERHMarketEffectKind::Theft; Fx.Party = FName("LvD"); Fx.Slot = Sim->GetLedgerSlotIndex(NIce); Fx.Grams = 30000;
		Sim->Debug_AddMarketEffect(Fx);
		FString Err;
		Expect(Sim->SaveColony(TEXT("lv9"), Err), FString::Printf(TEXT("LV9 the fixture is saved (%s)"), *Err));
		TArray<uint8> W;
		FFileHelper::LoadFileToArray(W, *SaveFilePath(TEXT("lv9")));
		uint32 Ver = 0;
		if (W.Num() > 8) { FMemory::Memcpy(&Ver, W.GetData() + 4, 4); }
		FLevSuiteBlock B;
		const bool bParsed = LevSuiteParse(W, B);
		int32 CurAt = INDEX_NONE, CurBytes = 0, LedgerAt = INDEX_NONE, LedgerBytes = 0;
		RHSaveFrame::Find(W, RHSaveFrame::CurrencyTag, CurAt, CurBytes);
		RHSaveFrame::Find(W, RHSaveFrame::LedgerTag, LedgerAt, LedgerBytes);
		TSet<uint8> Kinds;
		for (const FLevSuiteBlock::FRule& R : B.Rules) { Kinds.Add(R.Kind); }
		const bool bShape = bParsed && Ver == 59 && B.FormatRev == 1 && CurAt != INDEX_NONE && CurAt + RHSaveFrame::HeaderBytes + CurBytes + RHSaveFrame::FooterBytes == B.FrameAt
			&& B.PayloadBytes == Sim->GetLeversBlockBytes() && Kinds.Num() == 6 && B.Effects.Num() == 3 && B.Terms.Num() == 1 && B.Terms[0].ConvoyId == LvDConvoy && B.Terms[0].Markups == TArray<int32>({ 1500 })
			&& B.Rules.ContainsByPredicate([](const FLevSuiteBlock::FRule& R) { return R.Kind == 6 && R.Res == TEXT("Ice"); })
			&& B.Rules.ContainsByPredicate([](const FLevSuiteBlock::FRule& R) { return R.Kind == 6 && R.Res == TEXT("*") && R.Party == TEXT("LvC") && R.Dir == 3 && R.Markup == 700; })
			&& !B.Rules.ContainsByPredicate([](const FLevSuiteBlock::FRule& R) { return R.Kind != 6 && R.Res != TEXT("*"); }) && SabAtSave == SabWant && SabWant < 6.0;
		Expect(bSign && bBooked && bShape, FString::Printf(TEXT("LV9 v%u: 'RHL6' sits straight after 'RHE5''s footer (found by tag), its payload GetLeversBlockBytes() (%d bytes): %d rules of all six kinds by Id, NextRuleId %d, 3 pending effects, the terms of convoy #%d (EffSol %d, markup 1500), the resource of every non-tariff rule and of LvC's every-resource tariff saved as \"*\"; Zarya's disruption counted down once (%.17g)"),
			Ver, B.PayloadBytes, B.Rules.Num(), B.Next, LvDConvoy, B.Terms.Num() > 0 ? B.Terms[0].EffSol : -1, SabAtSave));
		// The mirror slots, byte for byte as HEAD wrote them, before E1's frame.
		TArray<FName> MClosed = { FName("LvA") }, MDefected = { FName("LvB") }, MDiscovered;
		TMap<FName, double> MSabotage, MEmbargo;
		MSabotage.Add(NZarya, Sim->GetSabotageRemaining(NZarya));
		MEmbargo.Add(FName("LvC"), 5.0);
		double Solid = Sim->GetSolidarityHope(), Infl = Sim->GetInfluence();
		TArray<uint8> V19, V21, V22;
		{ FMemoryWriter M(V19); FNameAsStringProxyArchive A(M); A << MClosed << Solid; }
		{ FMemoryWriter M(V21); FNameAsStringProxyArchive A(M); A << MSabotage << MDiscovered; }
		{ FMemoryWriter M(V22); FNameAsStringProxyArchive A(M); A << MEmbargo << MDefected << Infl; }
		Expect(LedgerAt > 0 && LevSuiteHas(W, LedgerAt, V19) && LevSuiteHas(W, LedgerAt, V21) && LevSuiteHas(W, LedgerAt, V22),
			FString::Printf(TEXT("LV9 the v19 / v21 / v22 slots hold the mirror in HEAD's layout before E1's frame: the Closed names (LvA) and SolidarityHope; each rival's largest live disruption (Zarya's live timer, %.17g, not the 6 it started at) and the discovered; each embargo's grace (LvC 5), the Severed names (LvB) and Influence"), SabAtSave));
		FString Le;
		const int32 Warn0 = Capture.NumWarnings(), Err0 = Capture.NumErrors();
		const bool bLoaded = Sim->LoadColony(TEXT("lv9"), Le) && Le.IsEmpty() && Sim->SaveColony(TEXT("lv9b"), Err);
		const int32 Warn1 = Capture.NumWarnings(), Err1 = Capture.NumErrors();
		TArray<uint8> W2;
		ExpectSeq({
			[&]() { return bLoaded; },
			[&]() { return FFileHelper::LoadFileToArray(W2, *SaveFilePath(TEXT("lv9b"))); },
			[&]() { return W2 == W; },
			[&]() { return Sim->HasLivePact(NZarya) && Sim->IsRouteClosed(FName("LvA")) && Sim->HasDefected(FName("LvB")) && Sim->IsEmbargoed(FName("LvC")); },
			[&]() { return Warn1 == Warn0 && Err1 == Err0; },
			[&]() { return Sim->GetSabotageRemaining(NZarya) == SabAtSave && Sim->GetEmbargoGrace(FName("LvC")) == 5.0; },
			[&]()
			{
				const FRHTradeRule* T = RuleOf(EKind::Tariff, FName("LvC"));
				return T && T->ResSlot == -1 && T->Dir == 3 && T->MarkupBp == 700 && T->FromSol == C + 10 && T->UntilSol == C + 20
					&& Sim->GetLineMarkupBp(FName("LvC"), NIce, 1, C + 10) == 700 && Sim->GetLineMarkupBp(FName("LvC"), NStruct, 2, C + 19) == 700 && Sim->GetLineMarkupBp(FName("LvC"), NIce, 1, C + 20) == 0;
			},
			}, FString::Printf(TEXT("LV9 save, load, save is byte-identical (%d bytes), and the clean load logs no Warning and no Error; the live timers (%.17g, 5) and LvC's every-resource tariff (\"*\": 700 bp on Ice buys and Struct sells in [%d, %d)) come back as they were"), W.Num(), SabAtSave, C + 10, C + 20));
		// A fresh process's command counter (1) is raised by the load past the pending effects' orders (E5's trap 7, extended to E6): the effects'
		// CommandIds are the save's newest commands, so the counter comes back at 700,780; the counter this process had is then restored.
		{
			const int32 NextBefore = Sim->GetNextCommandId();
			int32 JournalMax = 0;
			for (const FRHPoliticalEntry& Entry : Sim->GetPoliticalJournal()) { JournalMax = FMath::Max(JournalMax, Entry.CommandId); }
			Sim->Debug_SetNextCommandId(1);
			FString Lf;
			const bool bFreshLoad = Sim->LoadColony(TEXT("lv9"), Lf) && Lf.IsEmpty();
			const int32 FreshNext = Sim->GetNextCommandId();
			Sim->Debug_SetNextCommandId(FMath::Max(NextBefore, FreshNext));
			ExpectSeq({
				[&]() { return bFreshLoad && JournalMax > 0 && JournalMax < 700777 && Sim->GetPendingMarketEffects().Num() == 3; },
				[&]() { return FreshNext == 700780; },
				}, FString::Printf(TEXT("LV9 a fresh process's command counter (1) is raised by the load past the pending effects' orders (700,777 to 700,779; the journal's newest act is #%d): next %d"), JournalMax, FreshNext));
		}
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
		const auto Sealed = [](TArray<uint8> T) { RHSaveFrame::Reseal(T, RHSaveFrame::LeversTag); return T; };
		const auto Put8 = [](TArray<uint8>& T, int32 Off, uint8 V) { T[Off] = V; };
		const auto Put32 = [](TArray<uint8>& T, int32 Off, int32 V) { FMemory::Memcpy(T.GetData() + Off, &V, 4); };
		const auto Put64 = [](TArray<uint8>& T, int32 Off, int64 V) { FMemory::Memcpy(T.GetData() + Off, &V, 8); };
		const auto PutD = [](TArray<uint8>& T, int32 Off, double V) { FMemory::Memcpy(T.GetData() + Off, &V, 8); };
		const auto PutStr = [](TArray<uint8>& T, int32 Off, const char* S) { for (int32 i = 0; S[i]; ++i) { T[Off + i] = (uint8)S[i]; } };
		const auto RuleIx = [&B](uint8 Kind, const TCHAR* Party) { return B.Rules.IndexOfByPredicate([Kind, Party](const FLevSuiteBlock::FRule& R) { return R.Kind == Kind && R.Party == Party; }); };
		const int32 IPact = RuleIx(5, TEXT("Zarya")), ITariff = RuleIx(6, TEXT("Zarya")), IDisrupt = RuleIx(2, TEXT("Zarya")), IClosed = RuleIx(1, TEXT("LvA")), ISevered = RuleIx(3, TEXT("LvB")), IEmbargo = RuleIx(4, TEXT("LvC"));
		const auto Kept = [&]()
		{
			// A lost block rebuilds from the mirror: no closed route reopens, no defection is forgiven, the embargo keeps its grace, the disruption
			// its timer (no window); the pact, the tariffs, the effects and the stored terms are lost (the convoy's terms default). D33's order and
			// origins: Closed by name string (#1, Comply), then Severed (#2, Defection), Embargo (#3, EarthPressure) and Disrupted (#4, Sabotage,
			// live, no window), the next Id 5.
			FRHConvoyTerms T;
			const TArray<FRHTradeRule>& Rs = Sim->GetTradeRules();
			const bool bRebuilt = Rs.Num() == 4 && Sim->GetNextRuleId() == 5
				&& Rs[0].Id == 1 && Rs[0].Kind == EKind::Closed && Rs[0].Origin == EOrigin::Comply && Rs[0].Party == FName("LvA")
				&& Rs[1].Id == 2 && Rs[1].Kind == EKind::Severed && Rs[1].Origin == EOrigin::Defection && Rs[1].Party == FName("LvB")
				&& Rs[2].Id == 3 && Rs[2].Kind == EKind::Embargo && Rs[2].Origin == EOrigin::EarthPressure && Rs[2].Party == FName("LvC")
				&& Rs[3].Id == 4 && Rs[3].Kind == EKind::Disrupted && Rs[3].Origin == EOrigin::Sabotage && Rs[3].Party == NZarya && Rs[3].bLive && !Rs[3].HasWindow();
			return bRebuilt && Sim->IsRouteClosed(FName("LvA")) && Sim->HasDefected(FName("LvB")) && Sim->IsEmbargoed(FName("LvC")) && Sim->GetEmbargoGrace(FName("LvC")) == 5.0
				&& Sim->GetSabotageRemaining(NZarya) == SabAtSave && !Sim->HasLivePact(NZarya) && RulesOf(EKind::Tariff, NAME_None) == 0 && Sim->GetPendingMarketEffects().Num() == 0
				&& Sim->GetConvoyTerms(LvDConvoy, T) && Sim->IsLeversBlockLost() && Capture.NumErrors() == 0;
		};
		const auto Doctor = [&](const TCHAR* Slot, const TArray<uint8>& Base, bool bReseal, const TCHAR* Warning, TFunctionRef<bool()> Check) -> bool
		{
			FString E;
			const bool bOk = TryBytes(Slot, bReseal ? Sealed(Base) : Base, E);
			const bool bWarned = Capture.CountWarnings(Warning) >= 1 && Capture.NumErrors() == 0;
			const bool bCheck = Check();
			if (!bOk || !bWarned || !bCheck) { Info(FString::Printf(TEXT("LV9 repair %s: loaded %d, warned %d (%s), check %d, %d warnings"), Slot, bOk ? 1 : 0, bWarned ? 1 : 0, Warning, bCheck ? 1 : 0, Capture.NumWarnings())); }
			return bOk && bWarned && bCheck;
		};
		bool bRepairs = bParsed && IPact >= 0 && ITariff >= 0 && IDisrupt >= 0 && IClosed >= 0 && ISevered >= 0 && IEmbargo >= 0 && B.Effects.Num() == 3 && B.Terms.Num() == 1;
		FString Failed;
		const auto Note = [&](const TCHAR* What, bool bOk) { bRepairs &= bOk; if (!bOk) { Failed += FString(What) + TEXT("; "); } };
		if (bRepairs)
		{
			// 1. Structure: the block is lost and the rules come from the mirror.
			{ TArray<uint8> T = W; Put32(T, B.Rules[1].IdAt, B.Rules[0].Id); Note(TEXT("ids not ascending"), Doctor(TEXT("lv9s1"), T, true, TEXT("the levers block is malformed"), Kept)); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[0].IdAt, 0); Note(TEXT("id 0"), Doctor(TEXT("lv9s2"), T, true, TEXT("the levers block is malformed"), Kept)); }
			{ TArray<uint8> T = W; Put32(T, B.Rules.Last().IdAt, B.Next); Note(TEXT("id >= next"), Doctor(TEXT("lv9s3"), T, true, TEXT("the levers block is malformed"), Kept)); }
			{ TArray<uint8> T = W; Put32(T, B.RuleCountAt, 9999); Note(TEXT("count past the bytes"), Doctor(TEXT("lv9s4"), T, true, TEXT("(rule count 9999)"), Kept)); }
			{ TArray<uint8> T = W; Put8(T, B.FormatRevAt, 2); Note(TEXT("format revision"), Doctor(TEXT("lv9s5"), T, true, TEXT("(format revision 2)"), Kept)); }
			{ TArray<uint8> T = W; PutD(T, B.Rules[IEmbargo].LiveSolsAt, 4.0); Note(TEXT("flipped byte"), Doctor(TEXT("lv9s6"), T, false, TEXT("fail their CRC"), Kept)); }
			{ TArray<uint8> T = W; Put32(T, B.FrameAt, 0); Note(TEXT("no frame"), Doctor(TEXT("lv9s7"), T, false, TEXT("the levers block is lost"), Kept)); }
			{ TArray<uint8> T = W; Put32(T, B.FrameAt + 4, B.PayloadBytes + 4); Note(TEXT("wrong length"), Doctor(TEXT("lv9s8"), T, false, TEXT("the levers block is lost"), Kept)); }
			{
				// NextRuleId out of range with no rule in the frame (the base colony's empty block): 0, and past 2^30. Each is malformed (repair 1),
				// the rules come from the (empty) mirror and the next Id is 1.
				TArray<uint8> Base;
				FLevSuiteBlock BB;
				const bool bBase = FFileHelper::LoadFileToArray(Base, *SaveFilePath(TEXT("leversbase"))) && LevSuiteParse(Base, BB) && BB.Rules.Num() == 0 && BB.Next == 1;
				const auto Empty = [&]() { return Sim->GetTradeRules().Num() == 0 && Sim->GetNextRuleId() == 1 && Sim->IsLeversBlockLost(); };
				{ TArray<uint8> T = Base; if (bBase) { Put32(T, BB.NextAt, 0); } Note(TEXT("next id 0, no rules"), bBase && Doctor(TEXT("lv9s9"), T, true, TEXT("(the next Id 0)"), Empty)); }
				{ TArray<uint8> T = Base; if (bBase) { Put32(T, BB.NextAt, (1 << 30) + 1); } Note(TEXT("next id past 2^30"), bBase && Doctor(TEXT("lv9s10"), T, true, TEXT("(the next Id 1073741825)"), Empty)); }
			}
			// 2. A rule dropped.
			const auto TariffGone = [&]() { return RulesOf(EKind::Tariff, NZarya) == 0 && Sim->HasLivePact(NZarya) && !Sim->IsLeversBlockLost(); };
			{ TArray<uint8> T = W; Put8(T, B.Rules[ITariff].KindAt, 9); Note(TEXT("kind"), Doctor(TEXT("lv9r1"), T, true, TEXT("is dropped (kind 9)"), TariffGone)); }
			{ TArray<uint8> T = W; Put8(T, B.Rules[ITariff].OriginAt, 0); Note(TEXT("origin"), Doctor(TEXT("lv9r2"), T, true, TEXT("is dropped (origin 0)"), TariffGone)); }
			{ TArray<uint8> T = W; Put8(T, B.Rules[ITariff].DirAt, 4); Note(TEXT("direction"), Doctor(TEXT("lv9r3"), T, true, TEXT("is dropped (direction 4)"), TariffGone)); }
			{ TArray<uint8> T = W; PutStr(T, B.Rules[ITariff].PartyAt, "Zxrya"); Note(TEXT("no rival"), Doctor(TEXT("lv9r4"), T, true, TEXT("is dropped (Zxrya is no rival)"), TariffGone)); }
			{ TArray<uint8> T = W; PutStr(T, B.Rules[ITariff].ResAt, "Ixe"); Note(TEXT("no slot"), Doctor(TEXT("lv9r5"), T, true, TEXT("is dropped (Ixe is no resource slot)"), TariffGone)); }
			// 3. A case-only Party loads as the row.
			{ TArray<uint8> T = W; PutStr(T, B.Rules[ITariff].PartyAt, "zarya"); Note(TEXT("case-only party"), Doctor(TEXT("lv9r6"), T, true, TEXT("names 'zarya'; read as the row Zarya"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Tariff, NZarya); return R && R->Party.ToString().Equals(TEXT("Zarya"), ESearchCase::CaseSensitive) && Sim->GetLineMarkupBp(NZarya, NIce, 1, C) == 1500; })); }
			// 4. The markup.
			{ TArray<uint8> T = W; Put32(T, B.Rules[ITariff].MarkupAt, -5); Note(TEXT("negative markup"), Doctor(TEXT("lv9r7"), T, true, TEXT("markup -5 bp is outside [0, 9000]"), [&]() { const FRHTradeRule* R = RuleOf(EKind::Tariff, NZarya); return R && R->MarkupBp == 0; })); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[ITariff].MarkupAt, 9500); Note(TEXT("markup above max"), Doctor(TEXT("lv9r8"), T, true, TEXT("markup 9500 bp is outside [0, 9000]"), [&]() { const FRHTradeRule* R = RuleOf(EKind::Tariff, NZarya); return R && R->MarkupBp == 9000; })); }
			// Only a tariff carries a markup (a repair beyond section 4's list): a closed route's is set to 0, the route still closed.
			{ TArray<uint8> T = W; Put32(T, B.Rules[IClosed].MarkupAt, 500); Note(TEXT("markup on a closed route"), Doctor(TEXT("lv9r9"), T, true, TEXT("markup 500 bp is outside [0, 0] for a closed route; set to 0"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Closed, FName("LvA")); return R && R->MarkupBp == 0 && Sim->IsRouteClosed(FName("LvA")); })); }
			// 5. Windows: an illegal shape repaired; an ended pact or a tariff with no legal window dropped; the upper clamp.
			{ TArray<uint8> T = W; Put32(T, B.Rules[IClosed].FromAt, 3); Put32(T, B.Rules[IClosed].UntilAt, 9); Note(TEXT("closed with a window"), Doctor(TEXT("lv9w1"), T, true, TEXT("is not a closed route's; set to [-1, -1)"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Closed, FName("LvA")); return R && !R->HasWindow(); })); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[IPact].FromAt, -1); Note(TEXT("live pact from -1"), Doctor(TEXT("lv9w2"), T, true, TEXT("is not a pact's"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Pact, NZarya); return R && R->bLive && R->FromSol == Sim->GetMarketCheckedSol() + 2 && R->UntilSol == -1; })); }
			{ TArray<uint8> T = W; Put8(T, B.Rules[IPact].LiveAt, 0); Note(TEXT("ended pact, open window"), Doctor(TEXT("lv9w3"), T, true, TEXT("a pact with no legal window"), [&]() { return RulesOf(EKind::Pact, NZarya) == 0; })); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[ITariff].UntilAt, B.Rules[ITariff].From - 1); Note(TEXT("tariff with no window"), Doctor(TEXT("lv9w4"), T, true, TEXT("a tariff with no legal window"), [&]() { return RulesOf(EKind::Tariff, NZarya) == 0; })); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[ITariff].UntilAt, C + 5000); Note(TEXT("upper clamp"), Doctor(TEXT("lv9w5"), T, true, TEXT("reaches past sol"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Tariff, NZarya); return R && R->UntilSol == Sim->GetMarketCheckedSol() + 2 + 30; })); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[IPact].UntilAt, B.Rules[IPact].From + 3); Note(TEXT("live pact with an end"), Doctor(TEXT("lv9w6"), T, true, TEXT("is not a pact's"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Pact, NZarya); return R && R->bLive && R->FromSol == B.Rules[IPact].From && R->UntilSol == -1; })); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[IDisrupt].UntilAt, B.Rules[IDisrupt].From - 1); Note(TEXT("disruption with a reversed window"), Doctor(TEXT("lv9w7"), T, true, TEXT("is not a disruption's; set to [-1, -1)"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Disrupted, NZarya); return R && !R->HasWindow() && R->bLive && Sim->GetSabotageRemaining(NZarya) == SabAtSave; })); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[ISevered].FromAt, 3); Put32(T, B.Rules[ISevered].UntilAt, 9); Note(TEXT("severed with a window"), Doctor(TEXT("lv9w8"), T, true, TEXT("is not a defection's; set to [-1, -1)"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Severed, FName("LvB")); return R && !R->HasWindow(); })); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[IEmbargo].FromAt, 2); Put32(T, B.Rules[IEmbargo].UntilAt, -1); Note(TEXT("embargo with a window"), Doctor(TEXT("lv9w9"), T, true, TEXT("is not an embargo's; set to [-1, -1)"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Embargo, FName("LvC")); return R && !R->HasWindow() && Sim->GetEmbargoGrace(FName("LvC")) == 5.0; })); }
			// The upper clamp emptying a window (repairs beyond section 4's list): a tariff's [C + 100, C + 110) clamps to an empty window and is
			// dropped; a disruption's keeps no window and its live part.
			{ TArray<uint8> T = W; Put32(T, B.Rules[ITariff].FromAt, C + 100); Put32(T, B.Rules[ITariff].UntilAt, C + 110); Note(TEXT("tariff clamped empty"), Doctor(TEXT("lv9w10"), T, true, TEXT("is dropped (its clamped window is empty)"),
				[&]() { return RulesOf(EKind::Tariff, NZarya) == 0 && Sim->HasLivePact(NZarya); })); }
			{ TArray<uint8> T = W; Put32(T, B.Rules[IDisrupt].FromAt, C + 100); Put32(T, B.Rules[IDisrupt].UntilAt, C + 110); Note(TEXT("disruption clamped empty"), Doctor(TEXT("lv9w11"), T, true, TEXT("reaches past sol"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Disrupted, NZarya); return R && !R->HasWindow() && R->bLive && Sim->GetSabotageRemaining(NZarya) == SabAtSave; })); }
			// 6. The live flag forced on Closed, Severed and Embargo.
			{ TArray<uint8> T = W; Put8(T, B.Rules[ISevered].LiveAt, 0); Note(TEXT("severed not live"), Doctor(TEXT("lv9l1"), T, true, TEXT("(a defection) is held live"), [&]() { const FRHTradeRule* R = RuleOf(EKind::Severed, FName("LvB")); return R && R->bLive; })); }
			{ TArray<uint8> T = W; Put8(T, B.Rules[IEmbargo].LiveAt, 0); Note(TEXT("embargo not live"), Doctor(TEXT("lv9l2"), T, true, TEXT("(an embargo) is held live"), [&]() { const FRHTradeRule* R = RuleOf(EKind::Embargo, FName("LvC")); return R && R->bLive && Sim->IsEmbargoed(FName("LvC")); })); }
			{ TArray<uint8> T = W; Put8(T, B.Rules[IClosed].LiveAt, 0); Note(TEXT("closed not live"), Doctor(TEXT("lv9l3"), T, true, TEXT("(a closed route) is held live"), [&]() { const FRHTradeRule* R = RuleOf(EKind::Closed, FName("LvA")); return R && R->bLive && Sim->IsRouteClosed(FName("LvA")); })); }
			// 7. The live timers.
			{ TArray<uint8> T = W; PutD(T, B.Rules[IEmbargo].LiveSolsAt, -1.0); Note(TEXT("embargo grace <= 0"), Doctor(TEXT("lv9t1"), T, true, TEXT("embargo grace is -1"), [&]() { return Sim->GetEmbargoGrace(FName("LvC")) == 8.0; })); }
			{ TArray<uint8> T = W; PutD(T, B.Rules[IEmbargo].LiveSolsAt, 99.0); Note(TEXT("embargo grace above"), Doctor(TEXT("lv9t2"), T, true, TEXT("embargo grace 99"), [&]() { return Sim->GetEmbargoGrace(FName("LvC")) == 8.0; })); }
			{ TArray<uint8> T = W; PutD(T, B.Rules[IDisrupt].LiveSolsAt, -1.0); Note(TEXT("downtime negative"), Doctor(TEXT("lv9t3"), T, true, TEXT("its live part ends"), [&]() { return Sim->GetSabotageRemaining(NZarya) == 0.0 && RulesOf(EKind::Disrupted, NZarya) == 1; })); }
			{ TArray<uint8> T = W; PutD(T, B.Rules[IDisrupt].LiveSolsAt, 50.0); Note(TEXT("downtime above"), Doctor(TEXT("lv9t4"), T, true, TEXT("downtime 50"), [&]() { return Sim->GetSabotageRemaining(NZarya) == 6.0; })); }
			{
				// Timers that are not finite: an embargo's NaN grace is reset to the grace (never an instant defection); a disruption's infinite
				// downtime ends its live part.
				double NaN = 0.0, Inf = 0.0;
				const uint64 NaNBits = 0x7FF8000000000000ULL, InfBits = 0x7FF0000000000000ULL;
				FMemory::Memcpy(&NaN, &NaNBits, 8);
				FMemory::Memcpy(&Inf, &InfBits, 8);
				{ TArray<uint8> T = W; PutD(T, B.Rules[IEmbargo].LiveSolsAt, NaN); Note(TEXT("embargo grace NaN"), Doctor(TEXT("lv9t5"), T, true, TEXT("embargo grace is"), [&]() { return Sim->GetEmbargoGrace(FName("LvC")) == 8.0 && !Sim->HasDefected(FName("LvC")); })); }
				{ TArray<uint8> T = W; PutD(T, B.Rules[IDisrupt].LiveSolsAt, Inf); Note(TEXT("downtime infinite"), Doctor(TEXT("lv9t6"), T, true, TEXT("its live part ends"), [&]() { return Sim->GetSabotageRemaining(NZarya) == 0.0 && RulesOf(EKind::Disrupted, NZarya) == 1; })); }
			}
			// A live timer on a kind that has none (a repair beyond section 4's list): a pact's is set to 0, the pact still live.
			{ TArray<uint8> T = W; PutD(T, B.Rules[IPact].LiveSolsAt, 3.0); Note(TEXT("timer on a pact"), Doctor(TEXT("lv9t7"), T, true, TEXT("(a pact) carries a live timer of 3"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Pact, NZarya); return R && R->LiveSols == 0.0 && R->bLive && Sim->HasLivePact(NZarya); })); }
			// 8. The exclusive kinds.
			{ TArray<uint8> T = W; Put8(T, B.Rules[IEmbargo].KindAt, 1); PutStr(T, B.Rules[IEmbargo].PartyAt, "LvA"); Note(TEXT("second closed"), Doctor(TEXT("lv9x1"), T, true, TEXT("(a second closed route for LvA)"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Closed, FName("LvA")); return RulesOf(EKind::Closed, FName("LvA")) == 1 && R && R->Id == B.Rules[IClosed].Id; })); }
			{ TArray<uint8> T = W; Put8(T, B.Rules[IEmbargo].KindAt, 1); PutStr(T, B.Rules[IEmbargo].PartyAt, "lva"); Note(TEXT("second closed, case variant"), Doctor(TEXT("lv9x4"), T, true, TEXT("(a second closed route for LvA)"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Closed, FName("LvA")); return Capture.CountWarnings(TEXT("names 'lva'; read as the row LvA")) == 1 && RulesOf(EKind::Closed, FName("LvA")) == 1 && R && R->Id == B.Rules[IClosed].Id; })); }
			// A second defection and a second embargo for one rival (the lowest Id kept): LvA's closed route (#6) made a defection by LvB, before
			// LvB's own (#7); LvB's defection (#7) made an embargo by LvC, before LvC's own (#8) - its grace, 0 as a defection's, reset to 8.
			{ TArray<uint8> T = W; Put8(T, B.Rules[IClosed].KindAt, 3); PutStr(T, B.Rules[IClosed].PartyAt, "LvB"); Note(TEXT("second severed"), Doctor(TEXT("lv9x5"), T, true, TEXT("(a second defection for LvB)"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Severed, FName("LvB")); return RulesOf(EKind::Severed, FName("LvB")) == 1 && R && R->Id == B.Rules[IClosed].Id && !Sim->IsRouteClosed(FName("LvA")); })); }
			{ TArray<uint8> T = W; Put8(T, B.Rules[ISevered].KindAt, 4); PutStr(T, B.Rules[ISevered].PartyAt, "LvC"); Note(TEXT("second embargo"), Doctor(TEXT("lv9x6"), T, true, TEXT("(a second embargo for LvC)"),
				[&]() { const FRHTradeRule* R = RuleOf(EKind::Embargo, FName("LvC")); return RulesOf(EKind::Embargo, FName("LvC")) == 1 && R && R->Id == B.Rules[ISevered].Id && Sim->GetEmbargoGrace(FName("LvC")) == 8.0 && !Sim->HasDefected(FName("LvB")); })); }
			{ TArray<uint8> T = W; PutStr(T, B.Rules[IEmbargo].PartyAt, "LvB"); Note(TEXT("embargo beside severed"), Doctor(TEXT("lv9x2"), T, true, TEXT("an embargo beside a defection"), [&]() { return RulesOf(EKind::Embargo, NAME_None) == 0 && Sim->HasDefected(FName("LvB")); })); }
			{ TArray<uint8> T = W; Put8(T, B.Rules[ITariff].KindAt, 5); Put32(T, B.Rules[ITariff].UntilAt, -1); Put8(T, B.Rules[ITariff].LiveAt, 1); Put32(T, B.Rules[ITariff].MarkupAt, 0);
			  Note(TEXT("second live pact"), Doctor(TEXT("lv9x3"), T, true, TEXT("a second live pact with Zarya"), [&]() { return RulesOf(EKind::Pact, NZarya) == 1; })); }
			// 9. The pending effects.
			{ TArray<uint8> T = W; Put64(T, B.Effects[0].GramsAt, 0); Note(TEXT("effect 0 g"), Doctor(TEXT("lv9e1"), T, true, TEXT("pending market effect is dropped (0 g)"), [&]() { return Sim->GetPendingMarketEffects().Num() == 2; })); }
			{ TArray<uint8> T = W; Put64(T, B.Effects[0].GramsAt, 2000000000); Note(TEXT("effect too large"), Doctor(TEXT("lv9e2"), T, true, TEXT("pending market effect is dropped (2000000000 g)"), [&]() { return Sim->GetPendingMarketEffects().Num() == 2; })); }
			{ TArray<uint8> T = W; Put8(T, B.Effects[1].KindAt, 7); Note(TEXT("effect kind"), Doctor(TEXT("lv9e3"), T, true, TEXT("pending market effect is dropped (kind 7)"), [&]() { return Sim->GetPendingMarketEffects().Num() == 2; })); }
			{ TArray<uint8> T = W; PutStr(T, B.Effects[2].PartyAt, "LvE"); Note(TEXT("effect barter rival"), Doctor(TEXT("lv9e4"), T, true, TEXT("(LvE has no economy)"), [&]() { return Sim->GetPendingMarketEffects().Num() == 2; })); }
			{ TArray<uint8> T = W; Put32(T, B.Effects[0].ClearSolAt, C + 9); Note(TEXT("effects unsorted"), Doctor(TEXT("lv9e5"), T, true, TEXT("pending market effects are out of order; sorted"),
				[&]() { const TArray<FRHMarketEffect>& P = Sim->GetPendingMarketEffects(); return P.Num() == 3 && P[0].ClearSol == C + 4 && P[1].ClearSol == C + 5 && P[2].ClearSol == C + 9; })); }
			// 10. The totals.
			{ TArray<uint8> T = W; Put64(T, B.PaidMcrAt, -5); Note(TEXT("negative total"), Doctor(TEXT("lv9p1"), T, true, TEXT("lifetime tariff totals"), [&]() { return Sim->GetTariffPaidMcr() == 0; })); }
			{ TArray<uint8> T = W; Put64(T, B.PaidMmkAt, -5); Note(TEXT("negative mark total"), Doctor(TEXT("lv9p2"), T, true, TEXT("lifetime tariff totals"), [&]() { return Sim->GetTariffPaidMmk() == 0; })); }
			// 11. The terms.
			{ TArray<uint8> T = W; Put32(T, B.Terms[0].ConvoyIdAt, 99); Note(TEXT("terms unknown convoy"), Doctor(TEXT("lv9c1"), T, true, TEXT("terms of convoy #99 are dropped (the market block holds no such convoy)"),
				[&]() { FRHConvoyTerms Tm; return Sim->GetConvoyTerms(LvDConvoy, Tm) && Tm.EffSol == FMath::Max(Sim->GetConvoys().Last().ClearSol, Sim->GetMarketCheckedSol()) && Tm.LineMarkupBp == TArray<int32>({ 1500 }); })); }
			{
				// An EffSol below the convoy's clearing sol is re-derived from E4's convoy, silently (E4 accepts a moved clearing sol without a word).
				TArray<uint8> T = W; Put32(T, B.Terms[0].EffSolAt, B.Terms[0].EffSol - 3);
				FString E;
				const bool bOk = TryBytes(TEXT("lv9c2"), Sealed(T), E);
				FRHConvoyTerms Tm;
				Note(TEXT("terms effsol low"), bOk && Capture.NumWarnings() == 0 && Sim->GetConvoyTerms(LvDConvoy, Tm) && Tm.EffSol == B.Terms[0].EffSol);
			}
			{
				// An EffSol past max(ClearSol, the open sol) (only a resealed file holds one) is re-derived too, silently: the clearing never reads
				// markups at a sol the reservation was not booked for.
				TArray<uint8> T = W; Put32(T, B.Terms[0].EffSolAt, B.Terms[0].EffSol + 3);
				FString E;
				const bool bOk = TryBytes(TEXT("lv9c6"), Sealed(T), E);
				FRHConvoyTerms Tm;
				Note(TEXT("terms effsol high"), bOk && Capture.NumWarnings() == 0 && Sim->GetConvoyTerms(LvDConvoy, Tm) && Tm.EffSol == B.Terms[0].EffSol);
			}
			{ TArray<uint8> T = W; Put32(T, B.Terms[0].MarkupAt[0], -3); Note(TEXT("terms markup"), Doctor(TEXT("lv9c3"), T, true, TEXT("hold a markup outside [0, 9000]"),
				[&]() { FRHConvoyTerms Tm; return Sim->GetConvoyTerms(LvDConvoy, Tm) && Tm.LineMarkupBp == TArray<int32>({ 0 }); })); }
			{
				// A terms entry with two markups for a one-line convoy: the payload rebuilt four bytes longer.
				TArray<uint8> P;
				P.Append(W.GetData() + B.PayloadAt, B.Terms[0].MarkupAt[0] + 4 - B.PayloadAt);
				const int32 Extra = 0;
				P.Append((const uint8*)&Extra, 4);
				P.Append(W.GetData() + B.Terms[0].MarkupAt[0] + 4, B.PayloadAt + B.PayloadBytes - (B.Terms[0].MarkupAt[0] + 4));
				const int32 Two = 2;
				FMemory::Memcpy(P.GetData() + (B.Terms[0].LineCountAt - B.PayloadAt), &Two, 4);
				TArray<uint8> T = W;
				LevSuiteReframe(T, RHSaveFrame::LeversTag, P);
				Note(TEXT("terms line count"), Doctor(TEXT("lv9c4"), T, false, TEXT("(2 markups for 1 lines)"), [&]() { FRHConvoyTerms Tm; return Sim->GetConvoyTerms(LvDConvoy, Tm) && Tm.LineMarkupBp == TArray<int32>({ 1500 }); }));
			}
			{
				// No terms entry at all: the convoy clears at max(ClearSol, the open sol), its markup read there (silently).
				TArray<uint8> P;
				P.Append(W.GetData() + B.PayloadAt, B.TermsCountAt - B.PayloadAt);
				const int32 Zero = 0;
				P.Append((const uint8*)&Zero, 4);
				TArray<uint8> T = W;
				LevSuiteReframe(T, RHSaveFrame::LeversTag, P);
				FString E;
				const bool bOk = TryBytes(TEXT("lv9c5"), T, E);
				FRHConvoyTerms Tm;
				Note(TEXT("no terms entry"), bOk && Capture.NumWarnings() == 0 && Sim->GetConvoyTerms(LvDConvoy, Tm) && Tm.EffSol == FMath::Max(Sim->GetConvoys().Last().ClearSol, Sim->GetMarketCheckedSol()) && Tm.LineMarkupBp == TArray<int32>({ 1500 }));
			}
			// 13. The mirror check: the block names LvD closed where the mid-file record has LvA; the block wins.
			{ TArray<uint8> T = W; PutStr(T, B.Rules[IClosed].PartyAt, "LvD"); Note(TEXT("mirror disagrees"), Doctor(TEXT("lv9m1"), T, true, TEXT("the mid-file record disagrees with the levers block"),
				[&]() { return Sim->IsRouteClosed(FName("LvD")) && !Sim->IsRouteClosed(FName("LvA")); })); }
			{
				// 11, the last branch: a terms entry naming a convoy that has cleared but is not home yet (a consistent save never writes one). A
				// fixture of its own: an order to Zarya, its clearing rolled, the colony saved before the convoy is back; the entry is added to that
				// save's block (no other offset moves) and the loader drops it with a Warning, leaving the convoy cleared and with no terms.
				Reset();
				Provision(20000000);
				const bool bBookC = Order(NZarya, TEXT("Ice:10"));
				const int32 CId = bBookC ? Sim->GetConvoys().Last().Id : -1;
				RollNext();
				const FRHConvoy* Cc = Sim->GetConvoys().FindByPredicate([CId](const FRHConvoy& X) { return X.Id == CId; });
				const bool bClearedOut = Cc && Cc->IsPriced() && Cc->bCleared;
				const int32 CClear = Cc ? Cc->ClearSol : -1;
				FString Es;
				TArray<uint8> Wc;
				FLevSuiteBlock Bc;
				const bool bSavedC = Sim->SaveColony(TEXT("lv9cc"), Es) && FFileHelper::LoadFileToArray(Wc, *SaveFilePath(TEXT("lv9cc"))) && LevSuiteParse(Wc, Bc) && Bc.Terms.Num() == 0;
				bool bCase = false;
				if (bBookC && bClearedOut && bSavedC)
				{
					TArray<uint8> P;
					P.Append(Wc.GetData() + Bc.PayloadAt, Bc.TermsCountAt - Bc.PayloadAt);
					const int32 Entry[] = { 1, CId, CClear, 1, 0 }; // TermsCount 1; ConvoyId, EffSol, LineCount 1, one markup 0
					P.Append((const uint8*)Entry, sizeof(Entry));
					TArray<uint8> T = Wc;
					LevSuiteReframe(T, RHSaveFrame::LeversTag, P);
					bCase = Doctor(TEXT("lv9c7"), T, false, *FString::Printf(TEXT("the terms of convoy #%d are dropped (its convoy is not waiting to clear)"), CId), [&]()
					{
						FRHConvoyTerms Tm;
						const FRHConvoy* L = Sim->GetConvoys().FindByPredicate([CId](const FRHConvoy& X) { return X.Id == CId; });
						return L && L->bCleared && !Sim->GetConvoyTerms(CId, Tm);
					});
				}
				else
				{
					Info(FString::Printf(TEXT("LV9 repair lv9c7: its fixture booked %d, cleared and out %d, saved with no terms %d"), bBookC ? 1 : 0, bClearedOut ? 1 : 0, bSavedC ? 1 : 0));
				}
				Note(TEXT("terms of a cleared convoy"), bCase);
			}
		}
		Expect(bRepairs, FString::Printf(TEXT("LV9 sealed, doctored blocks each load with their own Warning and no Error: Ids out of order, an Id of 0 or at NextRuleId, a count past the bytes, a format revision, a flipped byte (CRC), a lost frame and a wrong length rebuild the rules from the mirror in D33's order (#1 the closed route, #2 the defection, #3 the embargo, #4 the disruption with no window, the next Id 5: no closed route reopens, no defection is forgiven, the embargo keeps its grace and the disruption its timer; the pact, the tariffs and the effects are lost), and so does a NextRuleId of 0 or past 2^30 in a frame with no rule (the next Id is then 1); a rule with a bad kind, origin, direction, rival or resource is dropped; a case-only rival loads as the row; a markup below 0 or above 9000 is clamped, and a closed route's markup set to 0; windows are repaired to their kind's shape (a closed route's, a live pact's with an end, a reversed disruption's, a defection's and an embargo's), an ended pact or a tariff with none is dropped, an end past the open sol + 32 is clamped, and a window the clamp empties drops its tariff or leaves its disruption no window; Closed, Severed and Embargo are held live; the timers are reset, ended or clamped (a NaN grace reset, an infinite downtime ended), and a pact's live timer set to 0; a second closed route (the lowest Id kept, also from a name differing only in case), a second defection, a second embargo, an embargo beside a defection and a second live pact are dropped; bad effects are dropped and unsorted ones sorted; a negative total, in credits or marks, is 0; terms naming no convoy or a cleared one, with a bad markup or the wrong line count are repaired or dropped with a Warning, terms whose EffSol is not max(ClearSol, the open sol) - below it or past it - are re-derived from the convoy silently, and a convoy with none clears at max(ClearSol, the open sol); a mid-file record that disagrees loses to the block %s"), *Failed));
		// A lost 'RHE5' (the frame immediately before 'RHL6', its tag destroyed) never takes E6's block with it: it is found by the walk from the end.
		{
			TArray<uint8> T = W;
			Put32(T, CurAt, 0);
			FString E;
			const bool bOk = TryBytes(TEXT("lv9f1"), T, E);
			ExpectSeq({
				[&]() { return bOk && CurAt > 0; },
				[&]() { return Capture.CountWarnings(TEXT("the currency block is lost")) == 1 && Capture.CountWarnings(TEXT("the levers block")) == 0; },
				[&]() { return Sim->HasLivePact(NZarya) && RulesOf(EKind::Tariff, NAME_None) == 3 && Sim->GetPendingMarketEffects().Num() == 3 && !Sim->IsLeversBlockLost(); },
				}, TEXT("LV9 with 'RHE5''s tag destroyed (the frame just before 'RHL6'), the currency block is lost alone: E6's block is found by the walk back from the end, with the pact, the three tariffs and the three effects"));
		}
		// The previous version is refused.
		{
			TArray<uint8> T = W;
			const uint32 V56 = 56;
			FMemory::Memcpy(T.GetData() + 4, &V56, 4);
			FString E;
			const bool bOk = TryBytes(TEXT("lv9v"), T, E);
			Expect(!bOk && E.Contains(TEXT("Save format mismatch")) && E.Contains(TEXT("/59")), FString::Printf(TEXT("LV9 the previous version is refused (%s)"), *E));
		}
		RetireAll();
	}

	// --- LV10. Ruling 11 and Q4a: tariffs at 9,000 bp on everything, a pact and a sabotage's production cut, against a twin with none, over 10 sols
	// of standing trade: Hope, the population, growth and every colony stock are bit-identical (the money and the prices move; nothing else). ---
	{
		struct FCity { FString Colony; int64 Credit = 0, Paid = 0; FString Prices; };
		const auto RunCity = [&](bool bLevers) -> FCity
		{
			Fresh();
			Provision(1000000000);
			const int32 C = Sim->GetMarketCheckedSol();
			if (bLevers)
			{
				Sim->Debug_AddTariff(NZarya, NAME_None, 3, 9000, C, C + 30);
				FRHTradeRule Pact; Pact.Kind = EKind::Pact; Pact.Origin = EOrigin::Harness; Pact.Party = NZarya; Pact.FromSol = C; Pact.UntilSol = -1; Pact.bLive = true;
				Sim->Debug_AddTradeRule(Pact);
				FRHTradeRule Cut; Cut.Kind = EKind::Disrupted; Cut.Origin = EOrigin::Harness; Cut.Party = NZarya; Cut.FromSol = C + 1; Cut.UntilSol = C + 7; Cut.bLive = false;
				Sim->Debug_AddTradeRule(Cut);
			}
			const double T0 = Clock->GetSimSecondsTotal();
			for (const double At : { 0.1, 4.6, 9.1 })
			{
				while (Clock->GetSimSecondsTotal() < T0 + At * SolLen) { EraOnce(); }
				FRHCommand Cv; Cv.Verb = FName("Convoy"); Cv.Target = NZarya;
				Sim->EnqueueCommand(Cv);
			}
			while (Clock->GetSimSecondsTotal() < T0 + 10.0 * SolLen) { EraOnce(); }
			FCity O;
			const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
			O.Colony = FString::Printf(TEXT("hope %.17g/%.17g pop %d births %d projects %d"), Sim->GetHopeSmoothed(), H.Base + H.Housing + H.Rooms + H.Jobs + H.Milestones + H.Comforts, Sim->GetPopulation(), Sim->GetBirthsOnMars(), Sim->GetGrowthProjects().Num());
			DefsSub->ForEachResourceRow([&](FName R, const FRHResourceRow&) { O.Colony += FString::Printf(TEXT(" %s=%.17g/%.17g"), *R.ToString(), Sim->GetStock(R), Sim->GetTotalSolid(R)); });
			for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { O.Colony += FString::Printf(TEXT(" #%d%s"), Bd.Id, *Bd.DefName.ToString()); }
			O.Colony += FString::Printf(TEXT(" convoys %d rel %.17g inf %.17g"), Sim->GetConvoys().Num(), Sim->GetRivalRelation(NZarya), Sim->GetInfluence());
			O.Credit = Sim->GetCreditBalanceMcr();
			O.Paid = Sim->GetTariffPaidMcr();
			for (const FName R : TradedSlots) { O.Prices += FString::Printf(TEXT("%lld "), Sim->GetMarketPriceMcr(R)); }
			return O;
		};
		const FCity With = RunCity(true);
		const FCity Without = RunCity(false);
		ExpectSeq({
			[&]() { return With.Colony == Without.Colony; },
			[&]() { return With.Paid > 0 && Without.Paid == 0 && With.Credit != Without.Credit && With.Prices != Without.Prices; },
			}, FString::Printf(TEXT("LV10 10 sols of standing trade under a 9,000 bp tariff, a pact and a six-sol production cut against a twin with none: Hope, the population, growth, every stock and shelf, the convoys, the relation and Influence are bit-identical; only the money (tariff %s cr) and the prices move"), *Cr(With.Paid)));
	}

	// --- LV11. No pump: 10,000 seeded lines over values and markups - each buy costs at least its value, each sell receives at most its value and
	// never below 0; and a saved negative markup loads as 0 (LV9's repair 4). ---
	{
		FLevSuiteLcg Lcg;
		int32 Good = 0;
		FString Bad;
		const int32 MaxBp = (int32)DefsSub->GetConfigTableScalar(FName("TariffMaxBp"), 9000.0);
		for (int32 i = 0; i < 10000; ++i)
		{
			const int64 Kg = Lcg.Range(1, 2000), Price = Lcg.Range(1, 3600000);
			const int32 Bp = (int32)Lcg.Range(0, MaxBp);
			const int64 Value = Kg * Price;
			const int64 Buy = URHSimWorldSubsystem::MarketLineSettleMcr(Value, Bp, true), Sell = URHSimWorldSubsystem::MarketLineSettleMcr(Value, Bp, false);
			const bool bOk = Buy >= Value && Sell <= Value && Sell >= 0 && Buy - Value == Value - Sell;
			Good += bOk ? 1 : 0;
			if (!bOk && Bad.Len() < 300) { Bad += FString::Printf(TEXT("%lld at %d bp: buy %lld sell %lld; "), Value, Bp, Buy, Sell); }
		}
		Expect(Good == 10000, FString::Printf(TEXT("LV11 over 10,000 seeded lines (up to 2,000 kg at up to 3,600 cr/kg, markups 0..%d bp) every buy costs at least its value and every sell receives at most its value, never below 0 (%d of 10,000) %s"), MaxBp, Good, *Bad));
	}

	// --- LV8. Era parity, last (it injects a rival of its own). The start: E5's CU8 (no convoy out, sol 16.0 by the real catch-up roll), Zarya and
	// an injected Kestrel (30 km: a 1-sol round trip; relation 60, so a pact can be signed at once; stance -1); at load Influence +40 and 20,000 cr.
	// The schedule places every act with Debug_EnqueueAt, so the queue runs out of ExecuteAt order on the era twin's boundary steps (whose steps
	// end 30 s past each minute). R1's bar: at every threshold from 16 to 36, int64-equal across the twins - every price, every economy rival's
	// store, every fill and charge, each line's applied markup, the cut per rival per roll, the effects as applied and TariffPaidMcr; rules
	// compared as the set of (kind, rival, origin, From, Until) over the windows covering a sol at or after the open sol (no Id, no window-less
	// rule); per order, in each twin, the booking markup equals the clearing markup and the charge is at most the reservation. No rule-making act
	// (Sign, Break, Sabotage) is due in the first 30 s of a sol (re-review O6): the era twin's threshold step has not run any of them early. ---
	{
		Reset();
		Inject(NKestrel, 30.0, TEXT("Ice:100;Regolith:50"), TEXT(""), TEXT("Ice:1000;Regolith:3000"), TEXT("Ice:5000;Regolith:10000"), TEXT("Ice:150"), TEXT(""), -1, 60.f); // the covert op loots half its ExportLot: 75 kg
		Sim->Debug_SyncRivalEconomies();
		Sim->AddStock(NHyd, 1000.0);
		Sim->AddStock(NParts, 40.0);
		Sim->AddStock(NIce, 4000.0);
		Sim->AddStock(NStruct, 2000.0);
		Clock->Debug_SetSimSeconds(16.0 * SolLen - EraDt);
		EraOnce(); // the step ends at exactly sol 16.0: the catch-up roll closes the market's sols here, before the save
		FString Err;
		const bool bSaved = Sim->SaveColony(TEXT("lv8start"), Err);
		const double UKestrel1 = LevSuiteCovertU(TEXT("Kestrel"), 1), UZarya0 = LevSuiteCovertU(TEXT("Zarya"), 0);
		ExpectSeq({
			[&]() { return bSaved && Clock->GetSimSecondsTotal() == 16.0 * SolLen && Sim->GetMarketCheckedSol() == 16 && Sim->GetConvoys().Num() == 0 && Sim->GetRivalEconomyCount() == 3; },
			[&]() { return Sim->GetCovertAttempts() == 0 && Sim->GetTradeRules().Num() == 0; },
			[&]() { return UKestrel1 >= 0.5 && UZarya0 >= 0.5 && Near(UKestrel1, 0.727, 0.0005) && Near(UZarya0, 0.907, 0.0005); },
			}, FString::Printf(TEXT("LV8 the start save 'lv8start': Kestrel, Meridian and Zarya with economies, the clock at sol 16.0, the market open at 16, nothing out, no rule, no covert attempt yet; the covert rolls the schedule takes are clean whatever P (Zarya at attempt 0: U %.3f; Kestrel at attempt 1: U %.3f)"),
				UZarya0, UKestrel1));
		struct FAct { double QueuedAt; double ExecuteAt; const TCHAR* Verb; FName Target; const TCHAR* Text; int32 Harness; int64 Arg; };
		const auto S = [&](double Sol) { return Sol * SolLen; };
		const TArray<FAct> Acts = {
			{ S(16.2), S(17.0) + 10.0, TEXT("MarketOrder"), NKestrel, TEXT("Ice:1500"), 0, 0 }, // queued BEFORE the Sign, due after it: refused in both (EffSol 17)
			{ S(16.3), S(17.0) - 20.0, TEXT("Treaty"), NKestrel, TEXT("Sign"), 0, 0 },           // c = 16: the pact's window [18, open)
			{ S(18.2), S(19.0) + 10.0, TEXT("MarketOrder"), NKestrel, TEXT("Ice:1500"), 0, 0 }, // queued BEFORE the Break: accepted in both, markup 0
			{ S(18.3), S(19.0) - 20.0, TEXT("Treaty"), NKestrel, TEXT("Break"), 0, 0 },          // c = 18: the pact [18, 20), the tariff [20, 50)
			{ S(19.5), 0.0, TEXT(""), NKestrel, TEXT(""), 1, 2000 },                             // Kestrel's Ice 2,000 kg before the roll closing 19
			{ S(20.5), S(21.0) - 10.0, TEXT("Sabotage"), NZarya, TEXT(""), 0, 0 },               // c = 20: Zarya's production stops [21, 27)
			{ S(23.2), 0.0, TEXT(""), NKestrel, TEXT(""), 1, 150 },                              // Kestrel's Ice 150 kg
			{ S(23.4), S(23.5), TEXT("MarketOrder"), NKestrel, TEXT("Ice:100"), 0, 0 },          // clears at the roll closing 23: fills 100
			{ S(23.8), S(24.0) + 10.0, TEXT("Covert"), NKestrel, TEXT(""), 0, 0 },               // c = 24: the theft leaves at the roll closing 24
			{ S(27.4), S(27.5), TEXT("MarketOrder"), NZarya, TEXT("Struct:-200"), 0, 0 },
			{ S(27.45), S(27.5) + 5.0, TEXT("MarketOrder"), NKestrel, TEXT("Ice:100"), 0, 0 },   // under the betrayal tariff
			{ S(32.4), S(32.5), TEXT("MarketOrder"), NZarya, TEXT("Struct:-200"), 0, 0 } };
		struct FRun
		{
			FString Rolls, Clearings, Rules, Returns, Books, Refusals, Cuts, Effects;
			int32 RollCount = 0, Storms = 0, Booked = 0, Refused = 0;
			bool bTermsOk = true, bChargeOk = true, bHalfOk = true, bHomeOk = true, bAwardFree = true, bBookedTermsOk = true;
			int32 TermsChecked = 0, Attempts = -1, CleanCovert = 0, CleanSabotage = 0, CaughtLines = 0;
			int64 Credit = 0, PaidMcr = 0;
			FString PactWindows, Kestrel19Markup, Kestrel27Markup;
			int32 Fill23 = -1;
		};
		const auto RunTwin = [&](bool bAgent) -> FRun
		{
			LoadSlot(TEXT("lv8start"), true);
			Sim->Debug_AddInfluence(40.0);
			Sim->Debug_GrantCredits(20000000);
			Capture.Clear();
			FRun O;
			const int64 SalesPaid0 = Sim->GetSalesPaidMcr(), Awarded0 = Sim->GetCreditAwardedMcr(), Sales0 = Sim->GetMarketSalesMcr();
			int32 NextAct = 0, ClearSeen = Sim->GetMarketClearings().Num(), RejSeen = Rejects.Num(), LastChecked = Sim->GetMarketCheckedSol();
			int32 CutSeen = Sim->GetProductionCuts().Num(), EffSeen = Sim->GetMarketEffectsApplied().Num();
			TArray<int32> PrevIds;
			TMap<FName, double> LastHome;
			bool bFirstEra = true;
			while (Clock->GetSimSecondsTotal() < S(36.0) - 1.0e-6)
			{
				const double Now = Clock->GetSimSecondsTotal();
				while (NextAct < Acts.Num() && Now >= Acts[NextAct].QueuedAt - 1.0e-6)
				{
					const FAct& A = Acts[NextAct];
					if (A.Harness == 1) { Sim->Debug_SetRivalStore(A.Target, NIce, A.Arg); }
					else
					{
						FRHCommand C; C.Verb = FName(A.Verb); C.Target = A.Target; C.Text = A.Text;
						Sim->Debug_EnqueueAt(C, A.ExecuteAt);
					}
					++NextAct;
				}
				const int32 OpenBefore = Sim->GetMarketCheckedSol();
				if (bAgent) { AgentOnce(); }
				else if (bFirstEra) { bFirstEra = false; Clock->Debug_AdvanceSimSeconds(30.0); Sim->Debug_SetBatteryWh(1.0e9); Sim->EraStep(30.f); }
				else { EraOnce(); }
				// Bookings (R7's rules 1 and 2) and returns.
				const TArray<FRHConvoy>& Cv = Sim->GetConvoys();
				for (const FRHConvoy& C : Cv)
				{
					if (PrevIds.Contains(C.Id))
					{
						continue;
					}
					++O.Booked;
					const double BookedAt = Clock->GetSimSecondsTotal();
					O.Books += FString::Printf(TEXT("%s clear %d; "), *C.Rival.ToString(), C.ClearSol);
					const double* Home = LastHome.Find(C.Rival);
					const bool bBoundaryStep = FMath::Fmod(BookedAt, SolLen) < 61.0;
					O.bHomeOk &= !Home || bBoundaryStep || BookedAt - *Home >= SolLen;
					O.bHalfOk &= C.ReservedMcr * 2 <= Sim->GetSpendableMcr() + C.ReservedMcr;
					// Each priced booking stored its terms: EffSol max(ClearSol, the open sol at its booking) - no late booking here, so its ClearSol -
					// and one booking markup per line (the clearing's comparison is then never against the clearing's own verdict).
					if (C.IsPriced() && !C.bCleared)
					{
						FRHConvoyTerms T;
						O.bBookedTermsOk &= Sim->GetConvoyTerms(C.Id, T) && T.EffSol == FMath::Max(C.ClearSol, OpenBefore) && T.EffSol == C.ClearSol && T.LineMarkupBp.Num() == C.Lines.Num();
						++O.TermsChecked;
					}
				}
				for (const int32 Id : PrevIds)
				{
					const FRHConvoy* Still = Cv.FindByPredicate([Id](const FRHConvoy& C) { return C.Id == Id; });
					if (!Still)
					{
						const FRHMarketClearing* R = Sim->GetMarketClearings().FindByPredicate([Id](const FRHMarketClearing& K) { return K.ConvoyId == Id; });
						O.Returns += FString::Printf(TEXT("%s home in sol %d; "), R ? *R->Rival.ToString() : TEXT("?"), SolOf(Clock->GetSimSecondsTotal()));
						if (R) { LastHome.Add(R->Rival, Clock->GetSimSecondsTotal()); }
					}
				}
				PrevIds.Reset();
				for (const FRHConvoy& C : Cv) { PrevIds.Add(C.Id); }
				for (; RejSeen < Rejects.Num(); ++RejSeen) { ++O.Refused; O.Refusals += Rejects[RejSeen] + TEXT(" | "); }
				if (Sim->GetMarketCheckedSol() != LastChecked)
				{
					LastChecked = Sim->GetMarketCheckedSol();
					++O.RollCount;
					const FRHEventRow* Ev = Sim->GetActiveEvent();
					O.Storms += (Ev && Ev->Type == FName("DustStorm")) ? 1 : 0;
					O.Rolls += FString::Printf(TEXT("[sol %d prices"), LastChecked);
					for (int32 Slot = 0; Slot < Sim->GetLedgerSlotCount(); ++Slot) { O.Rolls += FString::Printf(TEXT(" %lld"), Sim->GetMarketPriceMcr(Sim->GetLedgerSlotName(Slot))); }
					for (int32 r = 0; r < Sim->GetRivalEconomyCount(); ++r)
					{
						const FName Rv = Sim->GetRivalEconomyName(r);
						O.Rolls += FString::Printf(TEXT(" %s:"), *Rv.ToString());
						for (int32 Slot = 0; Slot < Sim->GetLedgerSlotCount(); ++Slot) { O.Rolls += FString::Printf(TEXT(" %lld"), Sim->GetRivalStoreG(Rv, Sim->GetLedgerSlotName(Slot))); }
					}
					O.Rolls += FString::Printf(TEXT(" tariff %lld]\n"), Sim->GetTariffPaidMcr());
					for (; ClearSeen < Sim->GetMarketClearings().Num(); ++ClearSeen)
					{
						const FRHMarketClearing& K = Sim->GetMarketClearings()[ClearSeen];
						O.Clearings += FString::Printf(TEXT("[%s clear %d closing %d eff %d net %lld settled %lld paid %lld value %lld tariff %lld lines"), *K.Rival.ToString(), K.ClearSol, K.ClosingSol, K.EffSol, K.NetMcr, K.SettledMcr, K.PaidMcr, K.ValueMcr, K.TariffMcr);
						for (int32 l = 0; l < K.Lines.Num(); ++l) { O.Clearings += FString::Printf(TEXT(" %s %d/%d @%d"), *K.Lines[l].Resource.ToString(), K.Lines[l].Kg, K.Lines[l].FilledKg, K.LineMarkupBp.IsValidIndex(l) ? K.LineMarkupBp[l] : -1); }
						O.Clearings += TEXT("]\n");
						O.bTermsOk &= K.BookedMarkupBp == K.LineMarkupBp;
						O.bChargeOk &= K.SettledMcr <= 0 || K.SettledMcr <= K.ReleasedMcr;
						if (K.Rival == NKestrel && K.ClearSol == 19) { O.Kestrel19Markup = FString::Printf(TEXT("%d/%d"), K.LineMarkupBp.Num() > 0 ? K.LineMarkupBp[0] : -1, K.Lines.Num() > 0 ? K.Lines[0].FilledKg : -1); }
						if (K.Rival == NKestrel && K.ClearSol == 23 && K.Lines.Num() == 1) { O.Fill23 = K.Lines[0].FilledKg; }
						if (K.Rival == NKestrel && K.ClearSol == 27) { O.Kestrel27Markup = FString::Printf(TEXT("%d/%d"), K.LineMarkupBp.Num() > 0 ? K.LineMarkupBp[0] : -1, K.BookedMarkupBp.Num() > 0 ? K.BookedMarkupBp[0] : -1); }
					}
					for (; CutSeen < Sim->GetProductionCuts().Num(); ++CutSeen) { const FRHProductionCut& Ct = Sim->GetProductionCuts()[CutSeen]; O.Cuts += FString::Printf(TEXT("%s@%d "), *Ct.Rival.ToString(), Ct.ClosingSol); }
					for (; EffSeen < Sim->GetMarketEffectsApplied().Num(); ++EffSeen)
					{
						const FRHMarketEffectApplied& A = Sim->GetMarketEffectsApplied()[EffSeen];
						O.Effects += FString::Printf(TEXT("%s %d sub %d kind %d slot %d %lld moved %lld at %d; "), *A.Effect.Party.ToString(), A.Effect.ClearSol, A.Effect.Sub, (int32)A.Effect.Kind, A.Effect.Slot, A.Effect.Grams, A.MovedG, A.ClosingSol);
					}
					// The rules whose market window covers a sol at or after the open sol, as a set (no Id).
					TArray<FString> Set;
					for (const FRHTradeRule& R : Sim->GetTradeRules())
					{
						if (R.HasWindow() && (R.UntilSol < 0 || R.UntilSol > LastChecked)) { Set.Add(FString::Printf(TEXT("%d %s %d [%d, %d)"), (int32)R.Kind, *R.Party.ToString(), (int32)R.Origin, R.FromSol, R.UntilSol)); }
					}
					Set.Sort();
					O.Rules += FString::Printf(TEXT("[sol %d %s]\n"), LastChecked, *FString::Join(Set, TEXT("; ")));
					if (LastChecked == 17 || LastChecked == 19 || LastChecked == 20)
					{
						for (const FRHTradeRule& R : Sim->GetTradeRules()) { if (R.Kind == EKind::Pact) { O.PactWindows += FString::Printf(TEXT("@%d [%d, %d) "), LastChecked, R.FromSol, R.UntilSol); } }
					}
					O.bAwardFree &= Sim->GetQuotaPhase() == ERHQuotaPhase::Open && Sim->GetSalesPaidMcr() == SalesPaid0;
				}
			}
			O.Credit = Sim->GetCreditBalanceMcr();
			O.PaidMcr = Sim->GetTariffPaidMcr();
			// The covert rolls' real outcomes (the U values above are the suite's copy of the roll's formula): two rolls, both clean.
			O.Attempts = Sim->GetCovertAttempts();
			O.CleanCovert = Capture.CountLines(TEXT("=== COVERT CLEAN: Kestrel Post took 75 Ice"));
			O.CleanSabotage = Capture.CountLines(TEXT("=== SABOTAGE: Zarya Station offline 6 sols (caught=0"));
			O.CaughtLines = Capture.CountLines(TEXT("=== COVERT CAUGHT")) + Capture.CountLines(TEXT("caught=1")) + Capture.CountLines(TEXT("PACT BROKEN by"));
			O.bAwardFree &= Sim->GetCreditAwardedMcr() - Awarded0 == Sim->GetMarketSalesMcr() - Sales0;
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
				if (X != Y) { return FString::Printf(TEXT("line %d: %s  |  %s"), i, *X.Left(300), *Y.Left(300)); }
			}
			return FString(TEXT("(equal)"));
		};
		const bool bSame = Era.Rolls == Agent.Rolls && Era.Clearings == Agent.Clearings && Era.Cuts == Agent.Cuts && Era.Effects == Agent.Effects && Era.Rules == Agent.Rules
			&& Era.PaidMcr == Agent.PaidMcr && Era.Credit == Agent.Credit && Era.Books == Agent.Books && Era.Returns == Agent.Returns && Era.RollCount == Agent.RollCount;
		Info(FString::Printf(TEXT("LV8 era: books %s returns %s refusals %s cuts %s effects %s pact %s"), *Era.Books, *Era.Returns, *Era.Refusals, *Era.Cuts, *Era.Effects, *Era.PactWindows));
		// The per-order sub-checks, printed whatever the outcome (the check below stops at its first failing condition, so a mutation that fails
		// an earlier one would otherwise never show these).
		Info(FString::Printf(TEXT("LV8 per order: booking == clearing markups era %d agent %d; charges within reservations era %d agent %d; terms at booking era %d of %d, agent %d of %d"),
			Era.bTermsOk ? 1 : 0, Agent.bTermsOk ? 1 : 0, Era.bChargeOk ? 1 : 0, Agent.bChargeOk ? 1 : 0, Era.bBookedTermsOk ? Era.TermsChecked : 0, Era.TermsChecked, Agent.bBookedTermsOk ? Agent.TermsChecked : 0, Agent.TermsChecked));
		if (!bSame)
		{
			Info(FString::Printf(TEXT("LV8 diff: rolls %s ; clearings %s ; rules %s ; cuts %s | %s ; effects %s | %s ; books %s | %s ; returns %s | %s ; credit %lld | %lld"),
				*FirstDiff(Era.Rolls, Agent.Rolls), *FirstDiff(Era.Clearings, Agent.Clearings), *FirstDiff(Era.Rules, Agent.Rules), *Era.Cuts, *Agent.Cuts, *Era.Effects, *Agent.Effects,
				*Era.Books, *Agent.Books, *Era.Returns, *Agent.Returns, Era.Credit, Agent.Credit));
		}
		const FString CapText = TEXT("A convoy carries at most 1000 kg each way: this order buys 1500 kg and sells 0 kg | ");
		ExpectSeq({
			[&]() { return Era.RollCount == 20 && Agent.RollCount == 20 && Era.Storms == 0 && Agent.Storms == 0; },
			[&]() { return Era.Refused == 1 && Agent.Refused == 1 && Era.Refusals == CapText && Agent.Refusals == CapText; },
			[&]() { return Era.Booked == 5 && Agent.Booked == 5; },
			[&]() { return bSame; },
			[&]() { return Era.Kestrel19Markup == TEXT("0/1500") && Agent.Kestrel19Markup == TEXT("0/1500"); },
			[&]() { return Era.PactWindows == TEXT("@17 [18, -1) @19 [18, 20) ") && Agent.PactWindows == Era.PactWindows; },
			[&]() { return Era.Cuts == TEXT("Zarya@21 Zarya@22 Zarya@23 Zarya@24 Zarya@25 Zarya@26 "); },
			[&]() { return Era.Fill23 == 100 && Agent.Fill23 == 100 && Era.Effects.Contains(TEXT("Kestrel 24 sub 0 kind 1")) && Era.Effects.Contains(TEXT("moved 75000 at 24")); },
			[&]() { return Era.Kestrel27Markup == TEXT("2500/2500") && Agent.Kestrel27Markup == TEXT("2500/2500") && Era.PaidMcr > 0; },
			[&]() { return Era.bTermsOk && Agent.bTermsOk && Era.bChargeOk && Agent.bChargeOk; },
			[&]() { return Era.bBookedTermsOk && Agent.bBookedTermsOk && Era.TermsChecked == 5 && Agent.TermsChecked == 5; },
			[&]() { return Era.Attempts == 2 && Agent.Attempts == 2 && Era.CleanCovert == 1 && Agent.CleanCovert == 1 && Era.CleanSabotage == 1 && Agent.CleanSabotage == 1 && Era.CaughtLines == 0 && Agent.CaughtLines == 0; },
			[&]() { return Era.bHomeOk && Agent.bHomeOk && Era.bHalfOk && Agent.bHalfOk && Era.bAwardFree && Agent.bAwardFree; },
			}, FString::Printf(TEXT("LV8 the paired run, sols 16-36, no storm: the 1,500 kg order queued before the Sign is refused in both twins (its EffSol 17 is before the pact's 18); the one queued before the Break is booked in both at markup 0 and fills 1,500; the pact ends [18, 20) and the betrayal tariff runs [20, 50); Zarya's production stops at the rolls closing 21-26 in both; the 23.5 order fills 100 before the theft's 75 kg leaves at the roll closing 24; the 27.5 order pays the 2,500 bp markup it was booked at; at all %d thresholds every price, every economy rival's store, every fill and charge, each line's markup, the cuts, the effects as applied, TariffPaidMcr (%s cr) and the windows' set are identical, and so are the bookings, the home sols and the end balance (%s cr); every order's booking markup equals its clearing markup and its charge is within its reservation; each of the %d priced bookings stored its terms at booking (EffSol its clearing sol, one markup a line); the two covert rolls the schedule takes are clean in fact (%d attempts: the sabotage caught=0 and Kestrel's theft, no caught line); R7's rules hold; no award, launch or sale lands"),
				Era.RollCount, *Cr(Era.PaidMcr), *Cr(Era.Credit), Era.TermsChecked, Era.Attempts));
		RetireAll();
	}

	GLog->RemoveOutputDevice(&Capture);
	Sim->OnCommandRejected.Remove(RejectH);
	Sim->OnAlert.Remove(AlertH);
	return Finish();
}
