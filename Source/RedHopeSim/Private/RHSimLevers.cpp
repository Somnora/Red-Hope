#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "RHSaveFrame.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// E6, the levers (docs/qa/2026-10-24/e6-levers-design.md: DIRECTOR ANSWER Q-A (a), Q-B (a), Q-C (a) > docs/qa/2026-10-26/preflight/
// ORCHESTRATOR-RULINGS.md > CROSS-CHECK.md section 6 > E6-brief.md > REFRESH AT HEAD 31ffa6f > REVISION R1-R10 > the re-review's required change
// and observations > PROFESSOR REVIEW > design). One list of trade rules, ordered by Id, replaces HEAD's four political sets with HEAD's timing,
// arithmetic and texts, and adds pacts and tariffs. Nothing a price, a store, a fill or a charge depends on reads a rule's Id, its live timer or
// its live flag: the market reads only windows keyed by each act's OWN sol (floor(ExecuteAt / sol)), which every roll sees the same in both
// bands. Pacts and tariffs start two sols after the act (c + 2: a booking that might run before its act in one band never meets them), a
// sabotage's production cut one sol after (c + 1: the act always runs before the roll closing c). Integer money only. Rivals are ordered by
// name string wherever E6 orders them, never by FName index. Nothing here reads or writes Hope, the population or growth.

namespace
{
	constexpr int32 LevMaxList = 4096;
	constexpr int32 LevMaxLines = 64;            // E4's MktMaxLines: an order has at most 64 lines
	constexpr int32 LevMaxRecords = 64;          // the transient records kept
	constexpr int64 LevMaxEffectG = 1000000000;  // a pending effect moves at most 10^6 kg
	constexpr int32 LevMaxRuleId = 1 << 30;      // a loaded NextRuleId above this is malformed (far past any colony's acts; NextRuleId++ never overflows)
	const TCHAR* const LevEveryResource = TEXT("*"); // the saved name of "every resource" (never "": RHSaveFrame::ReadName refuses an empty name)

	bool LevNameLess(const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; }
	int32 LevClamp32(int32 V, int32 Lo, int32 Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }

	const TCHAR* LevKindName(ERHTradeRuleKind K)
	{
		switch (K)
		{
		case ERHTradeRuleKind::Closed:    return TEXT("closed route");
		case ERHTradeRuleKind::Disrupted: return TEXT("disruption");
		case ERHTradeRuleKind::Severed:   return TEXT("defection");
		case ERHTradeRuleKind::Embargo:   return TEXT("embargo");
		case ERHTradeRuleKind::Pact:      return TEXT("pact");
		case ERHTradeRuleKind::Tariff:    return TEXT("tariff");
		default:                          return TEXT("rule");
		}
	}

	// The kind with its article, for the load Warnings ("an embargo", "a pact").
	FString LevKindWithArticle(ERHTradeRuleKind K) { return FString(K == ERHTradeRuleKind::Embargo ? TEXT("an ") : TEXT("a ")) + LevKindName(K); }

	bool LevBlockKind(ERHTradeRuleKind K) { return (uint8)K >= (uint8)ERHTradeRuleKind::Closed && (uint8)K <= (uint8)ERHTradeRuleKind::Embargo; }

	// A rule as read from the frame, before its repairs.
	struct FLevReadRule
	{
		int32 Id = 0;
		uint8 Kind = 0, Origin = 0, Dir = 0, Live = 0;
		FString Party, Res;
		int32 MarkupBp = 0, FromSol = -1, UntilSol = -1;
		double LiveSols = 0.0;
	};
	struct FLevReadEffect
	{
		int32 ClearSol = 0, CommandId = 0, Sub = 0;
		uint8 Kind = 0;
		FString Party, Res;
		int64 Grams = 0;
	};
	struct FLevReadTerms
	{
		int32 ConvoyId = 0, EffSol = 0;
		TArray<int32> Markups;
	};
}

// --- The config and the start -------------------------------------------------------------------------------------------------------------

double URHSimWorldSubsystem::LeversConfigClamp(FName Row, double Raw)
{
	// Each row's sane range (the spec gives three: SabotageMarketSols 1..30, SabotageCutPct 0..100, TariffMaxBp 0..9999; the brief leaves the
	// rest to the builder, pinned in LV0): the integer rows are rounded first. A row this does not know passes through.
	const double V = FMath::IsFinite(Raw) ? Raw : 0.0;
	const FString N = Row.ToString();
	if (N == TEXT("PactMinRelation")) { return FMath::Clamp(V, 0.0, 100.0); }
	if (N == TEXT("PactInfluenceCost")) { return FMath::Clamp(V, 0.0, 1000.0); }
	if (N == TEXT("PactTension") || N == TEXT("PactBreakTension")) { return FMath::Clamp(V, 0.0, 100.0); }
	if (N == TEXT("PactHumanNatureShift") || N == TEXT("PactBreakHumanNatureShift")) { return FMath::Clamp(V, 0.0, 100.0); }
	if (N == TEXT("PactBreakRelationHit")) { return FMath::Clamp(V, 0.0, 100.0); }
	// The pact's cap at most 10,000 kg: E5's rate step multiplies the 8-sol bloc tally by 10^4, and its int64 bound (O13: 8 sols x 3 convoys x
	// the cap x 4 V x 900,000 mcr/kg x 10^4) holds up to about 10,700 kg a convoy; 10,000 keeps it inside (8.6 x 10^18 < 9.2 x 10^18).
	if (N == TEXT("PactCargoKg")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 1, 10000); }
	if (N == TEXT("PactBetrayalTariffBp")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 0, 9999); }
	if (N == TEXT("PactBetrayalTariffSols")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 1, 1000); }
	if (N == TEXT("TariffMaxBp")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 0, 9999); }
	if (N == TEXT("SabotageMarketSols")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 1, 30); }
	if (N == TEXT("SabotageCutPct")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 0, 100); }
	return V;
}

void URHSimWorldSubsystem::InitLevers()
{
	InitLeversFrom(nullptr);
}

void URHSimWorldSubsystem::Debug_InitLeversWith(const TMap<FName, double>& Rows)
{
	// LV0: InitLevers itself, with the rows given read in place of the config's (each still through its clamp, then the cross-row rule). An
	// empty map is a plain InitLevers (the authored config). Like InitLevers, it starts an empty list: a fixture's world only.
	InitLeversFrom(&Rows);
}

void URHSimWorldSubsystem::InitLeversFrom(const TMap<FName, double>* Rows)
{
	if (Defs)
	{
		const auto Cfg = [this, Rows](const TCHAR* Name, double Fallback)
		{
			const FName Row(Name);
			const double* Given = Rows ? Rows->Find(Row) : nullptr;
			return LeversConfigClamp(Row, Given ? *Given : Defs->GetConfigScalar(Row, Fallback));
		};
		PactMinRelation = Cfg(TEXT("PactMinRelation"), PactMinRelation);
		PactInfluenceCost = Cfg(TEXT("PactInfluenceCost"), PactInfluenceCost);
		PactTension = Cfg(TEXT("PactTension"), PactTension);
		PactHumanNatureShift = Cfg(TEXT("PactHumanNatureShift"), PactHumanNatureShift);
		PactCargoKg = (int32)Cfg(TEXT("PactCargoKg"), (double)PactCargoKg);
		PactBreakRelationHit = Cfg(TEXT("PactBreakRelationHit"), PactBreakRelationHit);
		PactBreakHumanNatureShift = Cfg(TEXT("PactBreakHumanNatureShift"), PactBreakHumanNatureShift);
		PactBreakTension = Cfg(TEXT("PactBreakTension"), PactBreakTension);
		PactBetrayalTariffBp = (int32)Cfg(TEXT("PactBetrayalTariffBp"), (double)PactBetrayalTariffBp);
		PactBetrayalTariffSols = (int32)Cfg(TEXT("PactBetrayalTariffSols"), (double)PactBetrayalTariffSols);
		TariffMaxBp = (int32)Cfg(TEXT("TariffMaxBp"), (double)TariffMaxBp);
		SabotageMarketSols = (int32)Cfg(TEXT("SabotageMarketSols"), (double)SabotageMarketSols);
		SabotageCutPct = (int32)Cfg(TEXT("SabotageCutPct"), (double)SabotageCutPct);
	}
	ClampBetrayalTariffToMax();
	TradeRules.Reset();
	NextRuleId = 1;
	PendingMarketEffects.Reset();
	ConvoyTerms.Reset();
	TariffPaidMcr = 0;
	TariffPaidMmk = 0;
	MarketEffectsApplied.Reset();
	ProductionCuts.Reset();
	bLeversBlockLost = false;
}

void URHSimWorldSubsystem::GetLeversConfig(TArray<TPair<FName, double>>& Out) const
{
	Out.Reset();
	Out.Emplace(FName("PactMinRelation"), PactMinRelation);
	Out.Emplace(FName("PactInfluenceCost"), PactInfluenceCost);
	Out.Emplace(FName("PactTension"), PactTension);
	Out.Emplace(FName("PactHumanNatureShift"), PactHumanNatureShift);
	Out.Emplace(FName("PactCargoKg"), (double)PactCargoKg);
	Out.Emplace(FName("PactBreakRelationHit"), PactBreakRelationHit);
	Out.Emplace(FName("PactBreakHumanNatureShift"), PactBreakHumanNatureShift);
	Out.Emplace(FName("PactBreakTension"), PactBreakTension);
	Out.Emplace(FName("PactBetrayalTariffBp"), (double)PactBetrayalTariffBp);
	Out.Emplace(FName("PactBetrayalTariffSols"), (double)PactBetrayalTariffSols);
	Out.Emplace(FName("TariffMaxBp"), (double)TariffMaxBp);
	Out.Emplace(FName("SabotageMarketSols"), (double)SabotageMarketSols);
	Out.Emplace(FName("SabotageCutPct"), (double)SabotageCutPct);
}

void URHSimWorldSubsystem::ClampBetrayalTariffToMax()
{
	// The one rule across two rows: a betrayal tariff is never above the cap every markup is clamped to (a config with TariffMaxBp below
	// PactBetrayalTariffBp would otherwise make a rule its own load repairs, and every text name a rate the walk never charges).
	PactBetrayalTariffBp = FMath::Min(PactBetrayalTariffBp, TariffMaxBp);
}

void URHSimWorldSubsystem::Debug_SetTariffConfig(double TariffMax, double BetrayalBp)
{
	// LV0: the two tariff rows set as a config sets them - each through its own clamp, then the cross-row rule InitLevers applies.
	TariffMaxBp = (int32)LeversConfigClamp(FName("TariffMaxBp"), TariffMax);
	PactBetrayalTariffBp = (int32)LeversConfigClamp(FName("PactBetrayalTariffBp"), BetrayalBp);
	ClampBetrayalTariffToMax();
}

FString URHSimWorldSubsystem::FormatPercentBp(int32 Bp)
{
	// A markup in basis points as an exact percentage, integer only: 2500 -> "25%", 2550 -> "25.5%", 2505 -> "25.05%".
	const int32 V = FMath::Max(0, Bp);
	const int32 Whole = V / 100, Frac = V % 100;
	if (Frac == 0)
	{
		return FString::Printf(TEXT("%d%%"), Whole);
	}
	if (Frac % 10 == 0)
	{
		return FString::Printf(TEXT("%d.%d%%"), Whole, Frac / 10);
	}
	return FString::Printf(TEXT("%d.%02d%%"), Whole, Frac);
}

// --- The list ----------------------------------------------------------------------------------------------------------------------------------

FRHTradeRule& URHSimWorldSubsystem::AppendTradeRule(ERHTradeRuleKind Kind, ERHTradeRuleOrigin Origin, FName Party)
{
	// The next Id, at the end: the list stays ascending by Id and never needs sorting. Ids are assigned only here, at an act's execution or a
	// live event, never at a retirement, and never reused.
	FRHTradeRule& R = TradeRules.AddDefaulted_GetRef();
	R.Id = NextRuleId++;
	R.Kind = Kind;
	R.Origin = Origin;
	R.Party = Party;
	return R;
}

const FRHTradeRule* URHSimWorldSubsystem::FindTradeRule(ERHTradeRuleKind Kind, FName Party) const
{
	return TradeRules.FindByPredicate([Kind, Party](const FRHTradeRule& R) { return R.Kind == Kind && R.Party == Party; });
}

FRHTradeRule* URHSimWorldSubsystem::FindLivePact(FName Party)
{
	return TradeRules.FindByPredicate([Party](const FRHTradeRule& R) { return R.Kind == ERHTradeRuleKind::Pact && R.bLive && R.Party == Party; });
}

void URHSimWorldSubsystem::EndPact(FRHTradeRule& Pact, int32 ActSol)
{
	// Its live part ends now; its window ends two sols after the act that ended it (the same c + 2 bound as its start), never before its start
	// (re-review O2: a Break queued after the Sign but due in the sol before can run on the same step; the window is then empty, and retires).
	Pact.bLive = false;
	Pact.UntilSol = FMath::Max(ActSol + 2, Pact.FromSol);
}

int32 URHSimWorldSubsystem::AppendBetrayalTariff(FName Rival, int32 ActSol)
{
	// Q-C (a): only a betrayed partner charges a tariff: every resource, both ways, for PactBetrayalTariffSols from two sols after the act. Its
	// markup is clamped to TariffMaxBp here too (the config's cross-row rule already holds it there), so a rule is never one its load repairs.
	FRHTradeRule& T = AppendTradeRule(ERHTradeRuleKind::Tariff, ERHTradeRuleOrigin::Betrayal, Rival);
	T.ResSlot = -1;
	T.Dir = 3;
	T.MarkupBp = LevClamp32(PactBetrayalTariffBp, 0, TariffMaxBp);
	T.FromSol = ActSol + 2;
	T.UntilSol = ActSol + 2 + PactBetrayalTariffSols;
	T.bLive = false; // a tariff has no live part
	return T.MarkupBp;
}

void URHSimWorldSubsystem::BreakPactByPartner(FName Rival, int32 ActSol)
{
	// D20: a caught covert op or sabotage against a live partner ends the pact from the partner's side, with the betrayal tariff and no further
	// relation or HumanNature change (HEAD's caught effects have applied). A log line only: the caught op's own alert stays on the banner.
	FRHTradeRule* Pact = FindLivePact(Rival);
	if (!Pact)
	{
		return;
	}
	EndPact(*Pact, ActSol);
	AppendBetrayalTariff(Rival, ActSol);
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	UE_LOG(LogRedHopeSim, Display, TEXT("=== PACT BROKEN by %s: they caught you ==="), Row ? *Row->DisplayName : *Rival.ToString());
}

// --- The walk ------------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::EvaluateTradeRules(FName Rival, int32 EffSol, const TArray<FRHConvoyLine>& Lines, FRHTradeVerdict& Out) const
{
	// One walk of every rule in Id order. The block is chosen by kind, then Id, so its text is HEAD's whatever order the rules were made in
	// (-solidarity's Zarya is embargoed first and closed second, and HEAD prints the closed text). Sums and "is there any" do not depend on order.
	// The markups and the pact read only windows at EffSol; the block reads the live part (what may be booked now: HEAD's live gates).
	Out = FRHTradeVerdict();
	Out.LineMarkupBp.Init(0, Lines.Num());
	TArray<int64, TInlineAllocator<16>> Sum;
	for (const FRHTradeRule& R : TradeRules)
	{
		if (R.Party != Rival)
		{
			continue;
		}
		if (LevBlockKind(R.Kind))
		{
			// Closed, Severed and Embargo are live while they exist; a disruption while its timer runs (HEAD's GetSabotageRemaining > 0).
			const bool bBlocks = R.Kind != ERHTradeRuleKind::Disrupted || (R.bLive && R.LiveSols > 0.0);
			if (bBlocks && (Out.BlockKind == 0 || (uint8)R.Kind < Out.BlockKind))
			{
				Out.BlockKind = (uint8)R.Kind;
				Out.BlockId = R.Id;
			}
		}
		else if (R.Kind == ERHTradeRuleKind::Pact)
		{
			Out.bPact |= R.Covers(EffSol);
		}
		else if (R.Kind == ERHTradeRuleKind::Tariff && bLeversEnabled && Lines.Num() > 0 && R.Covers(EffSol))
		{
			if (Sum.Num() == 0)
			{
				Sum.Init(0, Lines.Num());
			}
			for (int32 l = 0; l < Lines.Num(); ++l)
			{
				const FRHConvoyLine& L = Lines[l];
				const uint8 Need = L.Kg > 0 ? 1 : (L.Kg < 0 ? 2 : 0);
				const int32* SlotPtr = R.ResSlot >= 0 ? EconIndex.Find(L.Resource) : nullptr;
				if ((R.Dir & Need) != 0 && (R.ResSlot < 0 || (SlotPtr && *SlotPtr == R.ResSlot)))
				{
					Sum[l] += R.MarkupBp;
				}
			}
		}
	}
	for (int32 l = 0; l < Sum.Num(); ++l)
	{
		Out.LineMarkupBp[l] = (int32)FMath::Clamp<int64>(Sum[l], 0, TariffMaxBp);
	}
}

FString URHSimWorldSubsystem::TradeBlockText(uint8 BlockKind, const FString& DisplayName)
{
	// HEAD's four refusals, word for word (PlanConvoy's checks 4-7 at HEAD).
	switch ((ERHTradeRuleKind)BlockKind)
	{
	case ERHTradeRuleKind::Closed:    return FString::Printf(TEXT("The route to %s is closed - you cut ties when you complied with Earth"), *DisplayName);
	case ERHTradeRuleKind::Disrupted: return FString::Printf(TEXT("%s's production is disrupted (sabotage) - no trade until they recover"), *DisplayName);
	case ERHTradeRuleKind::Severed:   return FString::Printf(TEXT("%s has sided with Earth - relations are frozen, no trade"), *DisplayName);
	case ERHTradeRuleKind::Embargo:   return FString::Printf(TEXT("%s has embargoed you under Earth pressure - reassure them first"), *DisplayName);
	default:                          return FString();
	}
}

int32 URHSimWorldSubsystem::GetLineMarkupBp(FName Rival, FName Resource, uint8 Dir, int32 Sol) const
{
	TArray<FRHConvoyLine> One;
	FRHConvoyLine& L = One.AddDefaulted_GetRef();
	L.Resource = Resource;
	L.Kg = Dir == 2 ? -1 : 1;
	FRHTradeVerdict V;
	EvaluateTradeRules(Rival, Sol, One, V);
	return V.LineMarkupBp.Num() > 0 ? V.LineMarkupBp[0] : 0;
}

int32 URHSimWorldSubsystem::GetRivalCutPct(FName Rival, int32 Sol) const
{
	// The switch is read here too, as RollMarket reads it at each roll (3.3: a window cuts only with the levers on); D29 already gives no window
	// to a sabotage made with them off, so this differs only for a window made before the harness turned them off.
	if (!bLeversEnabled)
	{
		return 0;
	}
	for (const FRHTradeRule& R : TradeRules)
	{
		if (R.Kind == ERHTradeRuleKind::Disrupted && R.Party == Rival && R.UntilSol >= 0 && R.Covers(Sol))
		{
			return SabotageCutPct;
		}
	}
	return 0;
}

// --- The political getters, on the list ------------------------------------------------------------------------------------------------------

bool URHSimWorldSubsystem::IsRouteClosed(FName Rival) const
{
	return FindTradeRule(ERHTradeRuleKind::Closed, Rival) != nullptr;
}

int32 URHSimWorldSubsystem::GetClosedRouteCount() const
{
	int32 N = 0;
	for (const FRHTradeRule& R : TradeRules) { N += R.Kind == ERHTradeRuleKind::Closed ? 1 : 0; }
	return N;
}

bool URHSimWorldSubsystem::IsEmbargoed(FName Rival) const
{
	return FindTradeRule(ERHTradeRuleKind::Embargo, Rival) != nullptr;
}

bool URHSimWorldSubsystem::HasDefected(FName Rival) const
{
	return FindTradeRule(ERHTradeRuleKind::Severed, Rival) != nullptr;
}

bool URHSimWorldSubsystem::HasLivePact(FName Rival) const
{
	return TradeRules.ContainsByPredicate([Rival](const FRHTradeRule& R) { return R.Kind == ERHTradeRuleKind::Pact && R.bLive && R.Party == Rival; });
}

int32 URHSimWorldSubsystem::GetLivePactFromSol(FName Rival) const
{
	const FRHTradeRule* P = TradeRules.FindByPredicate([Rival](const FRHTradeRule& R) { return R.Kind == ERHTradeRuleKind::Pact && R.bLive && R.Party == Rival; });
	return P ? P->FromSol : -1;
}

int32 URHSimWorldSubsystem::GetLivePactSignedSol(FName Rival) const
{
	// The sol the live pact was signed in: its window opens two sols after the act (c + 2), so this is always a sol already reached (the card's
	// "Pact since sol %d"; the window's first sol can still be ahead for the two sols after a signing).
	const int32 From = GetLivePactFromSol(Rival);
	return From >= 0 ? FMath::Max(0, From - 2) : -1;
}

bool URHSimWorldSubsystem::GetBetrayalTariff(FName Rival, int32& OutBp, int32& OutUntilSol, int32* OutFromSol) const
{
	// The betrayal tariff still running (its window not yet behind the open sol); with two, the later end, the earlier start and the larger
	// markup (one runs at a time in play: a partner never re-signs while one runs).
	OutBp = 0;
	OutUntilSol = -1;
	int32 From = -1;
	for (const FRHTradeRule& R : TradeRules)
	{
		if (R.Kind == ERHTradeRuleKind::Tariff && R.Origin == ERHTradeRuleOrigin::Betrayal && R.Party == Rival && R.UntilSol > MarketCheckedSol)
		{
			OutBp = FMath::Max(OutBp, R.MarkupBp);
			OutUntilSol = FMath::Max(OutUntilSol, R.UntilSol);
			From = From < 0 ? R.FromSol : FMath::Min(From, R.FromSol);
		}
	}
	if (OutFromSol)
	{
		*OutFromSol = From;
	}
	return OutUntilSol >= 0;
}

int32 URHSimWorldSubsystem::GetDisruptionUntilSol(FName Rival) const
{
	int32 From = -1, Until = -1;
	GetDisruptionWindow(Rival, From, Until);
	return Until;
}

bool URHSimWorldSubsystem::GetDisruptionWindow(FName Rival, int32& OutFromSol, int32& OutUntilSol) const
{
	// The sabotage windows still ahead of the open sol, as one span (the earliest start, the latest end). They never leave a gap: a sabotage
	// made while an earlier window is still ahead starts at most at that window's end (c + 1 <= the open sol + 1 <= its Until).
	OutFromSol = -1;
	OutUntilSol = -1;
	for (const FRHTradeRule& R : TradeRules)
	{
		if (R.Kind == ERHTradeRuleKind::Disrupted && R.Party == Rival && R.UntilSol > MarketCheckedSol)
		{
			OutFromSol = OutFromSol < 0 ? R.FromSol : FMath::Min(OutFromSol, R.FromSol);
			OutUntilSol = FMath::Max(OutUntilSol, R.UntilSol);
		}
	}
	return OutUntilSol >= 0;
}

int32 URHSimWorldSubsystem::GetOrderCargoCapKg(FName Rival) const
{
	// The cap PlanConvoy applies to an order sent now (the deck's draft clamp): the pact's PactCargoKg when a pact window covers the sol that
	// order would clear in (GetBookingSolNow: the walk's bPact, which also holds for the two sols an ended pact's window outlives it), else E4's
	// ConvoyCargoKg (so, too, for the two sols before a new pact's window opens).
	FRHTradeVerdict V;
	EvaluateTradeRules(Rival, GetBookingSolNow(), TArray<FRHConvoyLine>(), V);
	return V.bPact ? PactCargoKg : ConvoyCargoKg;
}

bool URHSimWorldSubsystem::GetConvoyTerms(int32 ConvoyId, FRHConvoyTerms& Out) const
{
	const FRHConvoyTerms* T = FindConvoyTerms(ConvoyId);
	if (!T)
	{
		Out = FRHConvoyTerms();
		return false;
	}
	Out = *T;
	return true;
}

const FRHConvoyTerms* URHSimWorldSubsystem::FindConvoyTerms(int32 ConvoyId) const
{
	return ConvoyTerms.FindByPredicate([ConvoyId](const FRHConvoyTerms& T) { return T.ConvoyId == ConvoyId; });
}

void URHSimWorldSubsystem::DropConvoyTerms(int32 ConvoyId)
{
	ConvoyTerms.RemoveAll([ConvoyId](const FRHConvoyTerms& T) { return T.ConvoyId == ConvoyId; });
}

// --- Pacts -------------------------------------------------------------------------------------------------------------------------------------

bool URHSimWorldSubsystem::CanSignPact(FName Rival, FString& OutWhy) const
{
	// 2.1, in this order. Every check reads colony-side state at execution, as HEAD's verbs do.
	OutWhy.Reset();
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	if (!Row || !IsRivalAvailable(Rival))
	{
		OutWhy = FString::Printf(TEXT("No trade contact '%s'"), *Rival.ToString());
		return false;
	}
	if (!bLeversEnabled)
	{
		OutWhy = TEXT("No treaties: disabled");
		return false;
	}
	if (HasLivePact(Rival))
	{
		OutWhy = FString::Printf(TEXT("You already have a pact with %s"), *Row->DisplayName);
		return false;
	}
	// A pact needs a road it can use: no live block (the walk's text). The block reads only the live part, so the sol given does not matter.
	FRHTradeVerdict V;
	EvaluateTradeRules(Rival, GetBookingSolNow(), TArray<FRHConvoyLine>(), V);
	if (V.BlockKind != 0)
	{
		OutWhy = TradeBlockText(V.BlockKind, Row->DisplayName);
		return false;
	}
	const double Rel = GetRivalRelation(Rival);
	if (Rel < PactMinRelation)
	{
		OutWhy = FString::Printf(TEXT("%s does not trust you enough for a pact yet (relation %.0f, needs %.0f)"), *Row->DisplayName, Rel, PactMinRelation);
		return false;
	}
	if (Influence < PactInfluenceCost)
	{
		OutWhy = FString::Printf(TEXT("Not enough Influence for a pact (%.0f needed, %.0f on hand)"), PactInfluenceCost, Influence);
		return false;
	}
	int32 Bp = 0, Until = -1;
	if (GetBetrayalTariff(Rival, Bp, Until))
	{
		// The window is half-open: the last sol it charges is Until - 1 (the modelled "until sol 91" for [62, 92)).
		OutWhy = FString::Printf(TEXT("%s will not sign while it still charges you for the last broken pact (until sol %d)"), *Row->DisplayName, Until - 1);
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::CanBreakPact(FName Rival, FString& OutWhy) const
{
	OutWhy.Reset();
	if (!bLeversEnabled)
	{
		OutWhy = TEXT("No treaties: disabled");
		return false;
	}
	if (!HasLivePact(Rival))
	{
		const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
		OutWhy = FString::Printf(TEXT("You have no pact with %s"), Row ? *Row->DisplayName : *Rival.ToString());
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::ResolveTreaty(const FRHCommand& Cmd, FString& OutReason)
{
	// The Treaty verb's worker (Target the rival, Text Sign or Break, any case). Its live half is at execution; the market sees its windows,
	// which start two sols after the act's own sol c, and the political record its journal entry at the roll closing c.
	const bool bSign = Cmd.Text.Equals(TEXT("Sign"), ESearchCase::IgnoreCase);
	const bool bBreak = Cmd.Text.Equals(TEXT("Break"), ESearchCase::IgnoreCase);
	if (!bSign && !bBreak)
	{
		OutReason = FString::Printf(TEXT("No such treaty order '%s' (Sign or Break)"), *Cmd.Text);
		return false;
	}
	const int32 ActSol = FMath::FloorToInt32(Cmd.ExecuteAtSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
	if (bSign)
	{
		if (!CanSignPact(Cmd.Target, OutReason))
		{
			return false;
		}
		const FRHRivalRow* Row = Defs->GetRival(Cmd.Target);
		Influence -= PactInfluenceCost;
		EarthTension = FMath::Clamp(EarthTension + PactTension, 0.0, 100.0);
		HumanNatureAxis = FMath::Clamp(HumanNatureAxis + PactHumanNatureShift, -100.0, 100.0); // diplomacy is the Evolved path
		FRHTradeRule& Pact = AppendTradeRule(ERHTradeRuleKind::Pact, ERHTradeRuleOrigin::Signed, Cmd.Target);
		Pact.FromSol = ActSol + 2;
		Pact.UntilSol = -1;
		Pact.bLive = true;
		const int32 TermsFrom = Pact.FromSol;
		// E5 risk 2: the rate reads the political record, never the live tension, so the act is journaled like Pacify.
		AppendJournal(Cmd, 0, ERHPoliticalAct::PactSign, false, 0, FMath::RoundToInt64(PactTension * 1000.0));
		// Earth's protection is live from now (StepPreemptive skips a live partner); the bigger convoys - the colony's own, to this partner -
		// wait for the window, two sols after the act. One banner line at 1440x860 (PACT BROKEN's heading's twin).
		OnAlert.Broadcast(FString::Printf(
			TEXT("PACT SIGNED — %s: Earth cannot turn them against you, and from sol %d your convoys to them carry up to %d kg each way. Earth is displeased."),
			*Row->DisplayName, TermsFrom, PactCargoKg));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== PACT SIGNED: %s (influence %.0f, tension %.0f, terms from sol %d) ==="), *Row->DisplayName, Influence, EarthTension, TermsFrom);
		return true;
	}
	if (!CanBreakPact(Cmd.Target, OutReason))
	{
		return false;
	}
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Cmd.Target) : nullptr;
	const FString Name = Row ? Row->DisplayName : Cmd.Target.ToString();
	EndPact(*FindLivePact(Cmd.Target), ActSol);
	double& Rel = RelationRef(Cmd.Target);
	Rel = FMath::Clamp(Rel - PactBreakRelationHit, 0.0, 100.0);
	HumanNatureAxis = FMath::Clamp(HumanNatureAxis - PactBreakHumanNatureShift, -100.0, 100.0); // betrayal is the Destructive path
	EarthTension = FMath::Clamp(EarthTension - PactBreakTension, 0.0, 100.0);                   // Earth approves
	AppendJournal(Cmd, 0, ERHPoliticalAct::PactBreak, false, 0, -FMath::RoundToInt64(PactBreakTension * 1000.0));
	const FString Pct = FormatPercentBp(AppendBetrayalTariff(Cmd.Target, ActSol)); // the rule's own markup, exact (never Bp / 100 truncated)
	const int32 From = ActSol + 2, Last = ActSol + 2 + PactBetrayalTariffSols - 1; // the window is half-open: Last is the last sol charged
	OnAlert.Broadcast(FString::Printf(
		TEXT("PACT BROKEN — the colony walks away from its pact with %s. They will charge %s on every trade with you from sol %d to sol %d."),
		*Name, *Pct, From, Last));
	UE_LOG(LogRedHopeSim, Display, TEXT("=== PACT BROKEN: %s (relation %.0f, humanNature %.0f); they charge %s on every trade from sol %d to sol %d ==="),
		*Name, Rel, HumanNatureAxis, *Pct, From, Last);
	return true;
}

// --- Market effects of covert acts, and the roll's E6 steps --------------------------------------------------------------------------------

void URHSimWorldSubsystem::RecordMarketEffects(const FRHCommand& Cmd, FName Rival, ERHMarketEffectKind Kind, const TMap<FName, double>& Goods)
{
	// 4.2 / 4.3: one pending effect per resource, keyed by the act's own sol, with the sub-index in slot order. Only with the levers on and only
	// against a rival with an economy (a barter rival has no store); the colony side of the act is HEAD's, already done.
	if (!bLeversEnabled || !HasRivalEconomy(Rival))
	{
		return;
	}
	const FRHRivalEconomy* E = FindRivalEconomy(Rival);
	TArray<TPair<int32, int64>> Slots;
	for (const auto& Item : Goods)
	{
		const int32* SlotPtr = EconIndex.Find(Item.Key);
		const int64 Grams = FMath::RoundToInt64(Item.Value * 1000.0);
		if (SlotPtr && Grams > 0 && E && E->Trades.IsValidIndex(*SlotPtr) && E->Trades[*SlotPtr])
		{
			Slots.Emplace(*SlotPtr, Grams);
		}
	}
	Slots.Sort([](const TPair<int32, int64>& A, const TPair<int32, int64>& B) { return A.Key < B.Key; });
	const int32 ClearSol = FMath::FloorToInt32(Cmd.ExecuteAtSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		FRHMarketEffect Fx;
		Fx.ClearSol = ClearSol;
		Fx.CommandId = Cmd.CommandId;
		Fx.Sub = i;
		Fx.Kind = Kind;
		Fx.Party = Rival;
		Fx.Slot = Slots[i].Key;
		Fx.Grams = Slots[i].Value;
		const int32 At = PendingMarketEffects.IndexOfByPredicate([&Fx](const FRHMarketEffect& X)
		{
			return X.ClearSol != Fx.ClearSol ? X.ClearSol > Fx.ClearSol : (X.CommandId != Fx.CommandId ? X.CommandId > Fx.CommandId : X.Sub > Fx.Sub);
		});
		if (At == INDEX_NONE)
		{
			PendingMarketEffects.Add(Fx);
		}
		else
		{
			PendingMarketEffects.Insert(Fx, At);
		}
	}
}

void URHSimWorldSubsystem::ApplyMarketEffects(int32 ClosingSol, int32 DueUpToSol)
{
	// D27: in RollMarket's pass, after the convoy and desk clearing (the colony's paid orders are served first) and after the ring advance,
	// before production and outside the live gate (they are part of the clearing, which runs with the market off too). Every effect due by the
	// pass, in (ClearSol, CommandId, Sub) order, so the due ones are a prefix. A theft takes min(grams, store): what the store does not hold was
	// never there. Neither kind enters any tally.
	int32 Applied = 0;
	while (Applied < PendingMarketEffects.Num() && PendingMarketEffects[Applied].ClearSol <= DueUpToSol)
	{
		const FRHMarketEffect& Fx = PendingMarketEffects[Applied];
		int64 Moved = 0;
		FRHRivalEconomy* E = FindRivalEconomy(Fx.Party);
		if (E && E->StoreG.IsValidIndex(Fx.Slot) && E->Trades.IsValidIndex(Fx.Slot) && E->Trades[Fx.Slot])
		{
			int64& Store = E->StoreG[Fx.Slot];
			Moved = Fx.Kind == ERHMarketEffectKind::Theft ? FMath::Min(Fx.Grams, Store) : Fx.Grams;
			Store += Fx.Kind == ERHMarketEffectKind::Theft ? -Moved : Moved;
		}
		FRHMarketEffectApplied Rec;
		Rec.ClosingSol = ClosingSol;
		Rec.Effect = Fx;
		Rec.MovedG = Moved;
		MarketEffectsApplied.Add(Rec);
		++Applied;
	}
	if (Applied > 0)
	{
		PendingMarketEffects.RemoveAt(0, Applied, EAllowShrinking::No); // the queue keeps its capacity (no reallocation at every roll that applies one)
		if (MarketEffectsApplied.Num() > 2 * LevMaxRecords)
		{
			MarketEffectsApplied.RemoveAt(0, MarketEffectsApplied.Num() - LevMaxRecords, EAllowShrinking::No); // the newest 64, trimmed now and then, its capacity kept
		}
	}
}

void URHSimWorldSubsystem::RetireTradeRules()
{
	// D10 with R3 and re-review O2, at the very end of RollMarket. Retirement changes only what is saved: no Id moves, and every order a window
	// covers has cleared by then (a clearing catches ClearSol <= the closing sol), so no booked order meets a changed markup. Closed and
	// Severed never retire; an Embargo leaves only by Pacify or by hardening into Severed.
	const int32 Open = MarketCheckedSol;
	TradeRules.RemoveAll([Open](const FRHTradeRule& R) { return TradeRuleRetiresAt(R, Open); });
}

// --- The save -----------------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::ProjectPoliticalMirror(TArray<FName>& OutClosed, TMap<FName, double>& OutSabotage, TMap<FName, double>& OutEmbargo, TArray<FName>& OutDefected) const
{
	OutClosed.Reset();
	OutSabotage.Reset();
	OutEmbargo.Reset();
	OutDefected.Reset();
	for (const FRHTradeRule& R : TradeRules)
	{
		switch (R.Kind)
		{
		case ERHTradeRuleKind::Closed:  OutClosed.AddUnique(R.Party); break;
		case ERHTradeRuleKind::Severed: OutDefected.AddUnique(R.Party); break;
		case ERHTradeRuleKind::Embargo: OutEmbargo.Add(R.Party, R.LiveSols); break;
		case ERHTradeRuleKind::Disrupted:
			if (R.bLive)
			{
				if (double* S = OutSabotage.Find(R.Party)) { *S = FMath::Max(*S, R.LiveSols); }
				else { OutSabotage.Add(R.Party, R.LiveSols); }
			}
			break;
		default: break;
		}
	}
	OutClosed.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	OutDefected.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
}

void URHSimWorldSubsystem::WriteLeversBlock(FArchive& Ar) const
{
	// v57. Inside E6's frame ('RHL6'), written straight after E5's: FormatRev 1; NextRuleId; the rules by Id (Id, kind, origin, the rival's name,
	// the resource's name - "*" for every resource -, direction, markup, the window's two sols, the live timer and the live flag); the pending
	// market effects in (ClearSol, CommandId, Sub) order (CommandId an int32, as E5's journal and FRHCommand have it); the two lifetime tariff
	// totals; the terms of every uncleared priced convoy by convoy Id (EffSol, then one booking markup per line). Names are strings, never
	// FNames. Sized by history: a test measures it with GetLeversBlockBytes(), never as a constant.
	uint8 FormatRev = 1;
	int32 Next = NextRuleId, RuleCount = TradeRules.Num();
	Ar << FormatRev << Next << RuleCount;
	for (const FRHTradeRule& R : TradeRules)
	{
		int32 Id = R.Id, Markup = R.MarkupBp, From = R.FromSol, Until = R.UntilSol;
		uint8 Kind = (uint8)R.Kind, Origin = (uint8)R.Origin, Dir = R.Dir, Live = R.bLive ? 1 : 0;
		FString Party = R.Party.ToString();
		FString Res = EconSlots.IsValidIndex(R.ResSlot) ? EconSlots[R.ResSlot].Name.ToString() : FString(LevEveryResource);
		double Sols = R.LiveSols;
		Ar << Id << Kind << Origin << Party << Res << Dir << Markup << From << Until << Sols << Live;
	}
	int32 EffectCount = PendingMarketEffects.Num();
	Ar << EffectCount;
	for (const FRHMarketEffect& Fx : PendingMarketEffects)
	{
		int32 ClearSol = Fx.ClearSol, CommandId = Fx.CommandId, Sub = Fx.Sub;
		uint8 Kind = (uint8)Fx.Kind;
		FString Party = Fx.Party.ToString();
		FString Res = EconSlots.IsValidIndex(Fx.Slot) ? EconSlots[Fx.Slot].Name.ToString() : FString(LevEveryResource);
		int64 Grams = Fx.Grams;
		Ar << ClearSol << CommandId << Sub << Kind << Party << Res << Grams;
	}
	int64 PaidMcr = TariffPaidMcr, PaidMmk = TariffPaidMmk;
	Ar << PaidMcr << PaidMmk;
	TArray<const FRHConvoyTerms*> Waiting;
	for (const FRHConvoyTerms& T : ConvoyTerms)
	{
		const FRHConvoy* C = Convoys.FindByPredicate([&T](const FRHConvoy& X) { return X.Id == T.ConvoyId; });
		if (C && C->IsPriced() && !C->bCleared)
		{
			Waiting.Add(&T);
		}
	}
	Waiting.Sort([](const FRHConvoyTerms& A, const FRHConvoyTerms& B) { return A.ConvoyId < B.ConvoyId; });
	int32 TermsCount = Waiting.Num();
	Ar << TermsCount;
	for (const FRHConvoyTerms* T : Waiting)
	{
		int32 ConvoyId = T->ConvoyId, EffSol = T->EffSol, LineCount = T->LineMarkupBp.Num();
		Ar << ConvoyId << EffSol << LineCount;
		for (int32 M : T->LineMarkupBp) { Ar << M; }
	}
}

int32 URHSimWorldSubsystem::GetLeversBlockBytes() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, /*bIsPersistent*/ true);
	WriteLeversBlock(Ar);
	return Bytes.Num();
}

void URHSimWorldSubsystem::ReadLeversBlock(FArchive& Ar, const TArray<uint8>& File, const TArray<FName>& MirrorClosed, const TMap<FName, double>& MirrorSabotage,
	const TMap<FName, double>& MirrorEmbargo, const TArray<FName>& MirrorDefected)
{
	// Read after teardown and after E4's and E5's blocks, so it repairs with a Warning and never refuses: the colony always loads, and no Error
	// is ever logged. The mid-file v19 / v21 / v22 slots were read into the mirror given here (nothing between them and this reads a rule). The
	// rules come from the frame; a lost or malformed frame rebuilds them from the mirror (7.4), so a closed route stays closed and a defection a
	// defection. Section 4's repairs follow, then the mirror check (the rules win: the frame is CRC-checked, the mid-file slots are not), then
	// the terms of every uncleared priced convoy. The open sol for every clamp is E4's MarketCheckedSol, read before this.
	TradeRules.Reset();
	NextRuleId = 1;
	PendingMarketEffects.Reset();
	ConvoyTerms.Reset();
	TariffPaidMcr = 0;
	TariffPaidMmk = 0;
	MarketEffectsApplied.Reset();
	ProductionCuts.Reset();
	bLeversBlockLost = false;
	const int32 Open = MarketCheckedSol;
	const auto RivalKey = [this](const FString& Name) -> FName
	{
		// The row's own FName for a name string (a case-only variant resolves to the row), or None when no rivals row has it.
		FName Found = NAME_None;
		if (Defs && !Name.IsEmpty())
		{
			const FName Probe(*Name);
			Defs->ForEachRivalRow([&Found, &Probe](FName Row, const FRHRivalRow&) { if (Found.IsNone() && Row == Probe) { Found = Row; } });
		}
		return Found;
	};
	// The mirror, validated (7.2 with R6): rival rows only; a timer finite and above 0, clamped to its config value, never dropped for being above it.
	TArray<FName> MClosed, MDefected;
	TMap<FName, double> MSabotage, MEmbargo;
	for (const FName& N : MirrorClosed)
	{
		const FName Key = RivalKey(N.ToString());
		if (Key.IsNone()) { UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the mid-file record closes the route to %s, which is no rival; skipped"), *N.ToString()); continue; }
		MClosed.AddUnique(Key);
	}
	for (const FName& N : MirrorDefected)
	{
		const FName Key = RivalKey(N.ToString());
		if (Key.IsNone()) { UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the mid-file record has %s defected, which is no rival; skipped"), *N.ToString()); continue; }
		MDefected.AddUnique(Key);
	}
	const auto Timers = [&](const TMap<FName, double>& In, TMap<FName, double>& Into, const TCHAR* What, double Max)
	{
		for (const auto& P : In)
		{
			const FName Key = RivalKey(P.Key.ToString());
			if (Key.IsNone()) { UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the mid-file record holds %s for %s, which is no rival; skipped"), What, *P.Key.ToString()); continue; }
			if (!FMath::IsFinite(P.Value) || P.Value <= 0.0) { UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the mid-file record's %s for %s is %f sols; skipped"), What, *P.Key.ToString(), P.Value); continue; }
			if (P.Value > Max) { UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the mid-file record's %s for %s is %f sols, above %f; clamped"), What, *P.Key.ToString(), P.Value, Max); }
			Into.Add(Key, FMath::Min(P.Value, Max));
		}
	};
	Timers(MirrorSabotage, MSabotage, TEXT("disruption"), SabotageDurationSols);
	Timers(MirrorEmbargo, MEmbargo, TEXT("embargo grace"), EmbargoGraceSols);

	TArray<uint8> Payload;
	FString Why;
	bool bFound = RHSaveFrame::Read(Ar, File, RHSaveFrame::LeversTag, Payload, Why);
	TArray<FLevReadRule> ReadRules;
	TArray<FLevReadEffect> ReadEffects;
	TArray<FLevReadTerms> ReadTerms;
	int32 ReadNext = 1;
	int64 ReadPaidMcr = 0, ReadPaidMmk = 0;
	bool bParsed = false;
	if (bFound)
	{
		FMemoryReader In(Payload);
		const auto Cut = [&]() { return In.IsError(); };
		const auto Left = [&]() { return In.TotalSize() - In.Tell(); };
		uint8 FormatRev = 0;
		int32 RuleCount = 0;
		In << FormatRev << ReadNext << RuleCount;
		bool bOk = !Cut() && FormatRev == 1;
		Why = bOk ? FString() : FString::Printf(TEXT("format revision %d"), (int32)FormatRev);
		// A rule takes at least 38 bytes (two one-character names), an effect 31, a terms entry 12: a count the bytes cannot hold is refused.
		if (bOk && (RuleCount < 0 || RuleCount > LevMaxList || (int64)RuleCount * 38 > Left())) { bOk = false; Why = FString::Printf(TEXT("rule count %d"), RuleCount); }
		int32 LastId = 0;
		for (int32 r = 0; bOk && r < RuleCount; ++r)
		{
			FLevReadRule R;
			In << R.Id << R.Kind << R.Origin;
			bOk = !Cut() && RHSaveFrame::ReadName(In, R.Party) && RHSaveFrame::ReadName(In, R.Res);
			if (bOk) { In << R.Dir << R.MarkupBp << R.FromSol << R.UntilSol << R.LiveSols << R.Live; bOk = !Cut(); }
			if (bOk && (R.Id <= 0 || R.Id <= LastId || R.Id >= ReadNext))
			{
				bOk = false;
				Why = FString::Printf(TEXT("rule Id %d after %d, the next Id %d"), R.Id, LastId, ReadNext);
			}
			LastId = R.Id;
			ReadRules.Add(MoveTemp(R));
		}
		// NextRuleId past every rule's Id (the loop's check) and at least 1 when the frame holds none; never near int32's end, where the next
		// append would overflow. A file breaking this is malformed (repair 1): the rules come from the mirror.
		if (bOk && (ReadNext < FMath::Max(1, LastId + 1) || ReadNext > LevMaxRuleId))
		{
			bOk = false;
			Why = FString::Printf(TEXT("the next Id %d"), ReadNext);
		}
		int32 EffectCount = 0;
		if (bOk) { In << EffectCount; bOk = !Cut() && EffectCount >= 0 && EffectCount <= LevMaxList && (int64)EffectCount * 31 <= Left(); if (!bOk) { Why = FString::Printf(TEXT("effect count %d"), EffectCount); } }
		for (int32 e = 0; bOk && e < EffectCount; ++e)
		{
			FLevReadEffect Fx;
			In << Fx.ClearSol << Fx.CommandId << Fx.Sub << Fx.Kind;
			bOk = !Cut() && RHSaveFrame::ReadName(In, Fx.Party) && RHSaveFrame::ReadName(In, Fx.Res);
			if (bOk) { In << Fx.Grams; bOk = !Cut(); }
			ReadEffects.Add(MoveTemp(Fx));
		}
		if (bOk) { In << ReadPaidMcr << ReadPaidMmk; bOk = !Cut(); }
		int32 TermsCount = 0;
		if (bOk) { In << TermsCount; bOk = !Cut() && TermsCount >= 0 && TermsCount <= LevMaxList && (int64)TermsCount * 12 <= Left(); if (!bOk) { Why = FString::Printf(TEXT("terms count %d"), TermsCount); } }
		for (int32 t = 0; bOk && t < TermsCount; ++t)
		{
			FLevReadTerms T;
			int32 LineCount = 0;
			In << T.ConvoyId << T.EffSol << LineCount;
			bOk = !Cut() && LineCount >= 0 && LineCount <= LevMaxLines && (int64)LineCount * 4 <= Left();
			if (!bOk) { Why = FString::Printf(TEXT("a terms entry with %d lines"), LineCount); }
			for (int32 l = 0; bOk && l < LineCount; ++l) { int32 M = 0; In << M; T.Markups.Add(M); bOk = !Cut(); }
			ReadTerms.Add(MoveTemp(T));
		}
		if (bOk && In.Tell() != In.TotalSize())
		{
			bOk = false;
			Why = FString::Printf(TEXT("%lld bytes are left over"), In.TotalSize() - In.Tell());
		}
		if (!bOk && Why.IsEmpty())
		{
			Why = TEXT("the block is cut short");
		}
		bParsed = bOk;
	}
	if (!bParsed)
	{
		// 7.4: the rules rebuilt from the mirror - the Closed rules by name string, then Severed, then Embargo, then Disrupted with no market window.
		bLeversBlockLost = true;
		TArray<FName> Sorted = MClosed;
		Sorted.Sort(LevNameLess);
		for (const FName& N : Sorted) { AppendTradeRule(ERHTradeRuleKind::Closed, ERHTradeRuleOrigin::Comply, N); }
		for (const FName& N : MDefected) { AppendTradeRule(ERHTradeRuleKind::Severed, ERHTradeRuleOrigin::Defection, N); }
		for (const auto& P : MEmbargo)
		{
			if (!HasDefected(P.Key)) { AppendTradeRule(ERHTradeRuleKind::Embargo, ERHTradeRuleOrigin::EarthPressure, P.Key).LiveSols = P.Value; }
		}
		for (const auto& P : MSabotage) { AppendTradeRule(ERHTradeRuleKind::Disrupted, ERHTradeRuleOrigin::Sabotage, P.Key).LiveSols = P.Value; }
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the levers block is %s (%s); the trade rules are rebuilt from the mid-file record (%d closed, %d defected, %d embargoed, %d disrupted) and the colony's pacts, any betrayal tariff, the pending thefts and offerings and the orders' terms are lost"),
			bFound ? TEXT("malformed") : TEXT("lost"), *Why, MClosed.Num(), MDefected.Num(), MEmbargo.Num(), MSabotage.Num());
	}
	else
	{
		NextRuleId = ReadNext;
		const int32 Limit = Open + 2 + FMath::Max(PactBetrayalTariffSols, SabotageMarketSols + 1); // re-review O1: no end later than this
		for (const FLevReadRule& In : ReadRules)
		{
			FString Drop;
			const FName Key = RivalKey(In.Party);
			int32 Slot = -1;
			if (In.Kind < (uint8)ERHTradeRuleKind::Closed || In.Kind > (uint8)ERHTradeRuleKind::Tariff) { Drop = FString::Printf(TEXT("kind %d"), (int32)In.Kind); }
			else if (In.Origin < (uint8)ERHTradeRuleOrigin::Comply || In.Origin > (uint8)ERHTradeRuleOrigin::Harness) { Drop = FString::Printf(TEXT("origin %d"), (int32)In.Origin); }
			else if (In.Dir < 1 || In.Dir > 3) { Drop = FString::Printf(TEXT("direction %d"), (int32)In.Dir); }
			else if (Key.IsNone()) { Drop = FString::Printf(TEXT("%s is no rival"), *In.Party); }
			else if (In.Res != LevEveryResource)
			{
				const int32* SlotPtr = EconIndex.Find(FName(*In.Res));
				if (!SlotPtr) { Drop = FString::Printf(TEXT("%s is no resource slot"), *In.Res); }
				else { Slot = *SlotPtr; }
			}
			if (!Drop.IsEmpty())
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d is dropped (%s)"), In.Id, *Drop);
				continue;
			}
			FRHTradeRule R;
			R.Id = In.Id;
			R.Kind = (ERHTradeRuleKind)In.Kind;
			R.Origin = (ERHTradeRuleOrigin)In.Origin;
			R.Party = Key;
			R.ResSlot = Slot;
			R.Dir = In.Dir;
			R.MarkupBp = In.MarkupBp;
			R.FromSol = In.FromSol;
			R.UntilSol = In.UntilSol;
			R.LiveSols = In.LiveSols;
			R.bLive = In.Live != 0;
			const TCHAR* KindName = LevKindName(R.Kind);
			const FString KindA = LevKindWithArticle(R.Kind);
			if (!Key.ToString().Equals(In.Party, ESearchCase::CaseSensitive)) // FString's == ignores case
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d names '%s'; read as the row %s"), R.Id, *In.Party, *Key.ToString());
			}
			// The markup: never negative (no rule pays the colony), never above TariffMaxBp; only a tariff carries one.
			const int32 WantBp = R.Kind == ERHTradeRuleKind::Tariff ? LevClamp32(R.MarkupBp, 0, TariffMaxBp) : 0;
			if (WantBp != R.MarkupBp)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d's markup %d bp is outside [0, %d] for %s; set to %d"), R.Id, R.MarkupBp, R.Kind == ERHTradeRuleKind::Tariff ? TariffMaxBp : 0, *KindA, WantBp);
				R.MarkupBp = WantBp;
			}
			// The window (R3): an illegal shape is repaired to its kind's, never dropped, except an ended pact or a tariff with no legal window.
			const bool bNone = R.FromSol == -1 && R.UntilSol == -1;
			const bool bOpenEnded = R.FromSol >= 0 && R.UntilSol == -1;
			const bool bClosedWin = R.FromSol >= 0 && R.UntilSol >= 0 && R.FromSol < R.UntilSol;
			const bool bEmpty = R.FromSol >= 0 && R.UntilSol == R.FromSol;
			const int32 From0 = R.FromSol, Until0 = R.UntilSol;
			bool bDropWindow = false;
			if (LevBlockKind(R.Kind) && R.Kind != ERHTradeRuleKind::Disrupted && !bNone) { R.FromSol = -1; R.UntilSol = -1; }
			else if (R.Kind == ERHTradeRuleKind::Disrupted && !bNone && !bClosedWin) { R.FromSol = -1; R.UntilSol = -1; }
			else if (R.Kind == ERHTradeRuleKind::Pact && R.bLive && !bOpenEnded) { R.FromSol = R.FromSol >= 0 ? R.FromSol : Open + 2; R.UntilSol = -1; }
			else if (R.Kind == ERHTradeRuleKind::Pact && !R.bLive && !bClosedWin && !bEmpty) { bDropWindow = true; }
			else if (R.Kind == ERHTradeRuleKind::Tariff && !bClosedWin) { bDropWindow = true; }
			if (bDropWindow)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d is dropped (%s with no legal window [%d, %d))"), R.Id, *KindA, From0, Until0);
				continue;
			}
			if (R.FromSol != From0 || R.UntilSol != Until0)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d's window [%d, %d) is not %s's; set to [%d, %d)"), R.Id, From0, Until0, *KindA, R.FromSol, R.UntilSol);
			}
			if (R.FromSol > Limit || R.UntilSol > Limit)
			{
				const int32 F1 = FMath::Min(R.FromSol, Limit), U1 = R.UntilSol >= 0 ? FMath::Min(R.UntilSol, Limit) : R.UntilSol;
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d's window [%d, %d) reaches past sol %d; clamped"), R.Id, R.FromSol, R.UntilSol, Limit);
				R.FromSol = F1;
				R.UntilSol = U1;
				if (R.Kind == ERHTradeRuleKind::Tariff && R.UntilSol <= R.FromSol)
				{
					UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d is dropped (its clamped window is empty)"), R.Id);
					continue;
				}
				if (R.Kind == ERHTradeRuleKind::Disrupted && R.UntilSol <= R.FromSol) { R.FromSol = -1; R.UntilSol = -1; }
			}
			// The live flag (R3): Closed, Severed and Embargo are live while they exist, whatever the file says (a doctored file can recompute its CRC).
			if ((R.Kind == ERHTradeRuleKind::Closed || R.Kind == ERHTradeRuleKind::Severed || R.Kind == ERHTradeRuleKind::Embargo) && !R.bLive)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d (%s) is held live"), R.Id, *KindA);
				R.bLive = true;
			}
			// The live timers: an embargo's is reset to the grace when not finite or not above 0 (a doctored file never makes an instant
			// defection); a disruption's ends its live part when not finite or negative; either is clamped to its config value.
			if (R.Kind == ERHTradeRuleKind::Embargo)
			{
				if (!FMath::IsFinite(R.LiveSols) || R.LiveSols <= 0.0)
				{
					UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d's embargo grace is %f sols; set to %f"), R.Id, R.LiveSols, EmbargoGraceSols);
					R.LiveSols = EmbargoGraceSols;
				}
				else if (R.LiveSols > EmbargoGraceSols)
				{
					UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d's embargo grace %f sols is above %f; clamped"), R.Id, R.LiveSols, EmbargoGraceSols);
					R.LiveSols = EmbargoGraceSols;
				}
			}
			else if (R.Kind == ERHTradeRuleKind::Disrupted)
			{
				if (!FMath::IsFinite(R.LiveSols) || R.LiveSols < 0.0)
				{
					UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d's downtime is %f sols; its live part ends"), R.Id, R.LiveSols);
					R.LiveSols = 0.0;
					R.bLive = false;
				}
				else if (R.LiveSols > SabotageDurationSols)
				{
					UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d's downtime %f sols is above %f; clamped"), R.Id, R.LiveSols, SabotageDurationSols);
					R.LiveSols = SabotageDurationSols;
				}
			}
			else if (R.LiveSols != 0.0)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d (%s) carries a live timer of %f sols; set to 0"), R.Id, *KindA, R.LiveSols);
				R.LiveSols = 0.0;
			}
			// The exclusive kinds (1.2): one Closed, one Severed and one Embargo per rival (the lowest Id kept), never an Embargo beside a
			// Severed, and at most one live pact (re-review O2: a second is dropped).
			FString Clash;
			if ((R.Kind == ERHTradeRuleKind::Closed || R.Kind == ERHTradeRuleKind::Severed || R.Kind == ERHTradeRuleKind::Embargo) && FindTradeRule(R.Kind, R.Party)) { Clash = FString::Printf(TEXT("a second %s for %s"), KindName, *R.Party.ToString()); }
			else if (R.Kind == ERHTradeRuleKind::Pact && R.bLive && HasLivePact(R.Party)) { Clash = FString::Printf(TEXT("a second live pact with %s"), *R.Party.ToString()); }
			if (!Clash.IsEmpty())
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d is dropped (%s)"), R.Id, *Clash);
				continue;
			}
			TradeRules.Add(R);
		}
		// An Embargo beside a Severed is dropped, whichever came first. The defected rivals are gathered first: the predicate never reads the
		// array RemoveAll is compacting.
		TSet<FName> Defected;
		for (const FRHTradeRule& R : TradeRules)
		{
			if (R.Kind == ERHTradeRuleKind::Severed) { Defected.Add(R.Party); }
		}
		TradeRules.RemoveAll([&Defected](const FRHTradeRule& R)
		{
			const bool bBeside = R.Kind == ERHTradeRuleKind::Embargo && Defected.Contains(R.Party);
			if (bBeside) { UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: trade rule #%d is dropped (an embargo beside a defection, %s)"), R.Id, *R.Party.ToString()); }
			return bBeside;
		});
		// The pending effects: a gram value <= 0 or above 10^9, a bad kind, a rival without an economy or a resource it does not trade is dropped;
		// unsorted effects are sorted.
		for (const FLevReadEffect& In : ReadEffects)
		{
			FString Drop;
			const FName Key = RivalKey(In.Party);
			const int32* SlotPtr = EconIndex.Find(FName(*In.Res));
			const FRHRivalEconomy* E = Key.IsNone() ? nullptr : FindRivalEconomy(Key);
			if (In.Kind < (uint8)ERHMarketEffectKind::Theft || In.Kind > (uint8)ERHMarketEffectKind::Offering) { Drop = FString::Printf(TEXT("kind %d"), (int32)In.Kind); }
			else if (In.Grams <= 0 || In.Grams > LevMaxEffectG) { Drop = FString::Printf(TEXT("%lld g"), In.Grams); }
			else if (In.ClearSol < 0 || In.Sub < 0) { Drop = FString::Printf(TEXT("sol %d, sub %d"), In.ClearSol, In.Sub); }
			else if (!E || !E->bHasEconomy) { Drop = FString::Printf(TEXT("%s has no economy"), *In.Party); }
			else if (!SlotPtr || !E->Trades.IsValidIndex(*SlotPtr) || !E->Trades[*SlotPtr]) { Drop = FString::Printf(TEXT("%s does not trade %s"), *In.Party, *In.Res); }
			if (!Drop.IsEmpty())
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: a pending market effect is dropped (%s)"), *Drop);
				continue;
			}
			FRHMarketEffect Fx;
			Fx.ClearSol = In.ClearSol;
			Fx.CommandId = In.CommandId;
			Fx.Sub = In.Sub;
			Fx.Kind = (ERHMarketEffectKind)In.Kind;
			Fx.Party = Key;
			Fx.Slot = *SlotPtr;
			Fx.Grams = In.Grams;
			PendingMarketEffects.Add(Fx);
		}
		const auto EffectLess = [](const FRHMarketEffect& A, const FRHMarketEffect& B)
		{
			return A.ClearSol != B.ClearSol ? A.ClearSol < B.ClearSol : (A.CommandId != B.CommandId ? A.CommandId < B.CommandId : A.Sub < B.Sub);
		};
		bool bSorted = true;
		for (int32 i = 1; i < PendingMarketEffects.Num(); ++i) { bSorted &= EffectLess(PendingMarketEffects[i - 1], PendingMarketEffects[i]); }
		if (!bSorted)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the pending market effects are out of order; sorted"));
			PendingMarketEffects.StableSort(EffectLess);
		}
		// NextCommandId is not saved (E5's trap 7, which raises it past the desk orders and the journal): raise it past every pending effect's
		// order too, so an act sent in a fresh process never takes a pending one's Id and (ClearSol, CommandId, Sub) keeps the order the acts
		// were issued in. Silent; it saves no field.
		for (const FRHMarketEffect& Fx : PendingMarketEffects)
		{
			if (Fx.CommandId >= NextCommandId && Fx.CommandId < MAX_int32)
			{
				NextCommandId = Fx.CommandId + 1;
			}
		}
		// The totals: a negative one is 0.
		if (ReadPaidMcr < 0 || ReadPaidMmk < 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the lifetime tariff totals (%lld mcr, %lld mmk) hold a value below 0; set to 0"), ReadPaidMcr, ReadPaidMmk);
		}
		TariffPaidMcr = FMath::Max<int64>(0, ReadPaidMcr);
		TariffPaidMmk = FMath::Max<int64>(0, ReadPaidMmk);
		// The terms (repair 11 with the cross-check's item 2): a terms entry whose convoy E4's reader did not load is dropped SILENTLY when E4 has
		// already warned about it (its block lost or malformed, or that convoy dropped by a repair); a Warning when the convoy loaded and its
		// line count disagrees, when the convoy has cleared, when E4's block parsed and never held it, or for a markup out of range. An EffSol
		// that is not max(ClearSol, the open sol) is re-derived silently (below).
		int32 LastConvoy = 0;
		for (const FLevReadTerms& In : ReadTerms)
		{
			const FRHConvoy* C = Convoys.FindByPredicate([&In](const FRHConvoy& X) { return X.Id == In.ConvoyId; });
			if (!bMarketParsed || (!C && ReadMarketConvoyIds.Contains(In.ConvoyId)))
			{
				continue;
			}
			FString Drop;
			if (In.ConvoyId <= LastConvoy) { Drop = TEXT("its convoy Id is not above the one before"); }
			else if (!C) { Drop = TEXT("the market block holds no such convoy"); }
			else if (!C->IsPriced() || C->bCleared) { Drop = TEXT("its convoy is not waiting to clear"); }
			else if (In.Markups.Num() != C->Lines.Num()) { Drop = FString::Printf(TEXT("%d markups for %d lines"), In.Markups.Num(), C->Lines.Num()); }
			if (!Drop.IsEmpty())
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the terms of convoy #%d are dropped (%s)"), In.ConvoyId, *Drop);
				continue;
			}
			LastConvoy = In.ConvoyId;
			FRHConvoyTerms T;
			T.ConvoyId = In.ConvoyId;
			T.EffSol = In.EffSol;
			if (T.EffSol != FMath::Max(C->ClearSol, Open))
			{
				// E4's block is authoritative for the convoy, and an uncleared convoy's EffSol is exactly max(ClearSol, the open sol) (re-review
				// O3: booked at max(ClearSol, the open sol then), and no roll has closed its ClearSol since, or it would have cleared): any other
				// stored EffSol - below its clearing sol, or past it - is re-derived from the convoy, so the clearing never reads markups at a sol
				// its reservation was not booked for. SILENTLY: E4 accepts a moved clearing sol without a word (CU9 moves one and asserts zero
				// Warnings, RHSimCurrencySuite.cpp's bQuiet), so a Warning here would turn E5's CU9 red (and add a line to currency.log: item 16).
				T.EffSol = FMath::Max(C->ClearSol, Open);
			}
			bool bClamped = false;
			for (const int32 M : In.Markups)
			{
				const int32 Fixed = LevClamp32(M, 0, TariffMaxBp);
				bClamped |= Fixed != M;
				T.LineMarkupBp.Add(Fixed);
			}
			if (bClamped)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the terms of convoy #%d hold a markup outside [0, %d]; clamped"), In.ConvoyId, TariffMaxBp);
			}
			ConvoyTerms.Add(MoveTemp(T));
		}
		// The mirror check (7.3): the rules' projection against the mid-file record just read. The rules win.
		TArray<FName> PClosed, PDefected;
		TMap<FName, double> PSabotage, PEmbargo;
		ProjectPoliticalMirror(PClosed, PSabotage, PEmbargo, PDefected);
		TArray<FName> SClosed = MClosed, SDefected = MDefected;
		SClosed.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		SDefected.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		const auto SameMap = [](const TMap<FName, double>& A, const TMap<FName, double>& B)
		{
			if (A.Num() != B.Num()) { return false; }
			for (const auto& P : A) { const double* V = B.Find(P.Key); if (!V || *V != P.Value) { return false; } }
			return true;
		};
		FString Diff;
		if (PClosed != SClosed) { Diff += FString::Printf(TEXT("%d closed routes against %d; "), PClosed.Num(), SClosed.Num()); }
		if (PDefected != SDefected) { Diff += FString::Printf(TEXT("%d defections against %d; "), PDefected.Num(), SDefected.Num()); }
		if (!SameMap(PEmbargo, MEmbargo)) { Diff += FString::Printf(TEXT("%d embargoes against %d; "), PEmbargo.Num(), MEmbargo.Num()); }
		if (!SameMap(PSabotage, MSabotage)) { Diff += FString::Printf(TEXT("%d disruptions against %d; "), PSabotage.Num(), MSabotage.Num()); }
		if (!Diff.IsEmpty())
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v57: the mid-file record disagrees with the levers block (%s); the levers block wins"), *Diff.LeftChop(2));
		}
	}
	// Every uncleared priced convoy without terms (a lost block, or a missing entry) clears at max(ClearSol, the open sol), which is exactly the
	// sol it clears in (re-review O3), with its booking markups read there. Silent: a lost block has said what it lost.
	for (const FRHConvoy& C : Convoys)
	{
		if (!C.IsPriced() || C.bCleared || FindConvoyTerms(C.Id))
		{
			continue;
		}
		FRHConvoyTerms T;
		T.ConvoyId = C.Id;
		T.EffSol = FMath::Max(C.ClearSol, Open);
		FRHTradeVerdict V;
		EvaluateTradeRules(C.Rival, T.EffSol, C.Lines, V);
		T.LineMarkupBp = V.LineMarkupBp;
		ConvoyTerms.Add(MoveTemp(T));
	}
	ConvoyTerms.Sort([](const FRHConvoyTerms& A, const FRHConvoyTerms& B) { return A.ConvoyId < B.ConvoyId; });
}

// --- Harness hooks -----------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::Debug_CloseRoute(FName Rival)
{
	if (!IsRouteClosed(Rival))
	{
		AppendTradeRule(ERHTradeRuleKind::Closed, ERHTradeRuleOrigin::Harness, Rival);
	}
}

void URHSimWorldSubsystem::Debug_SetEmbargo(FName Rival, double GraceSols)
{
	// HEAD's TMap::Add overwrote: the one Embargo is updated in place. Never beside a Severed (D9): a defected rival is left as it is.
	if (HasDefected(Rival))
	{
		return;
	}
	if (FRHTradeRule* E = TradeRules.FindByPredicate([Rival](const FRHTradeRule& R) { return R.Kind == ERHTradeRuleKind::Embargo && R.Party == Rival; }))
	{
		E->LiveSols = GraceSols;
		return;
	}
	AppendTradeRule(ERHTradeRuleKind::Embargo, ERHTradeRuleOrigin::Harness, Rival).LiveSols = GraceSols;
}

void URHSimWorldSubsystem::Debug_SetDefected(FName Rival)
{
	if (HasDefected(Rival))
	{
		return;
	}
	TradeRules.RemoveAll([Rival](const FRHTradeRule& R) { return R.Kind == ERHTradeRuleKind::Embargo && R.Party == Rival; });
	AppendTradeRule(ERHTradeRuleKind::Severed, ERHTradeRuleOrigin::Harness, Rival);
}

void URHSimWorldSubsystem::Debug_SetSabotage(FName Rival, double Sols)
{
	// A Disrupted rule with no market window (a legal R3 shape): the live refusal only, as MX11 pins it.
	AppendTradeRule(ERHTradeRuleKind::Disrupted, ERHTradeRuleOrigin::Harness, Rival).LiveSols = Sols;
}

void URHSimWorldSubsystem::Debug_ClearPolitics()
{
	TradeRules.RemoveAll([](const FRHTradeRule& R) { return LevBlockKind(R.Kind); });
}

void URHSimWorldSubsystem::Debug_AddTariff(FName Rival, FName Resource, uint8 Dir, int32 Bp, int32 FromSol, int32 UntilSol)
{
	FRHTradeRule& T = AppendTradeRule(ERHTradeRuleKind::Tariff, ERHTradeRuleOrigin::Harness, Rival);
	const int32* SlotPtr = Resource.IsNone() ? nullptr : EconIndex.Find(Resource);
	T.ResSlot = SlotPtr ? *SlotPtr : -1;
	T.Dir = (uint8)FMath::Clamp<int32>(Dir, 1, 3);
	T.MarkupBp = LevClamp32(Bp, 0, TariffMaxBp);
	T.FromSol = FromSol;
	T.UntilSol = UntilSol;
	T.bLive = false;
}

void URHSimWorldSubsystem::Debug_EnqueueAt(FRHCommand Cmd, double ExecuteAt)
{
	// EnqueueCommand's Id and IssuedAt, with an explicit ExecuteAt, APPENDED: the queue can then run out of ExecuteAt order, which is how the
	// paired runs build the inversion an era step meets when the uplink lag falls with the speed tier.
	Cmd.CommandId = NextCommandId++;
	Cmd.IssuedAtSimSeconds = Clock ? Clock->GetSimSecondsTotal() : 0.0;
	Cmd.ExecuteAtSimSeconds = ExecuteAt;
	UE_LOG(LogRedHopeSim, Display, TEXT("Uplink: '%s %s' transmitted (harness), executes at %.0f sim-s"), *Cmd.Verb.ToString(), *Cmd.Target.ToString(), ExecuteAt);
	UplinkQueue.Add(MoveTemp(Cmd));
}

void URHSimWorldSubsystem::Debug_SetRelation(FName Rival, double Value)
{
	RelationRef(Rival) = FMath::Clamp(Value, 0.0, 100.0);
}

bool URHSimWorldSubsystem::Debug_RemoveRule(int32 Id)
{
	return TradeRules.RemoveAll([Id](const FRHTradeRule& R) { return R.Id == Id; }) > 0;
}

int32 URHSimWorldSubsystem::Debug_AddTradeRule(const FRHTradeRule& Rule)
{
	FRHTradeRule& R = AppendTradeRule(Rule.Kind, Rule.Origin, Rule.Party);
	const int32 Id = R.Id;
	R = Rule;
	R.Id = Id;
	return Id;
}

void URHSimWorldSubsystem::Debug_AddMarketEffect(const FRHMarketEffect& Effect)
{
	const int32 At = PendingMarketEffects.IndexOfByPredicate([&Effect](const FRHMarketEffect& X)
	{
		return X.ClearSol != Effect.ClearSol ? X.ClearSol > Effect.ClearSol : (X.CommandId != Effect.CommandId ? X.CommandId > Effect.CommandId : X.Sub > Effect.Sub);
	});
	if (At == INDEX_NONE)
	{
		PendingMarketEffects.Add(Effect);
	}
	else
	{
		PendingMarketEffects.Insert(Effect, At);
	}
}
