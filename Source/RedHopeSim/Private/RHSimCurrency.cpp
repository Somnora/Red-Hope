#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "RHSaveFrame.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// E5, the Martian currency (docs/qa/2026-10-24/e5-martian-currency-design.md: DIRECTOR ANSWER Q-A (a), Q-B (a) - the Comply loophole accepted,
// no rule added - and Q-C (a) > docs/qa/2026-10-26/preflight/ORCHESTRATOR-RULINGS.md > CROSS-CHECK.md section 6 > E5-brief.md > PROFESSOR
// RE-REVIEW conditions 1-7 > REVISION R1-R7 > review > design). The mark is a political act with a market behind it. Earth's side is live (the
// flag and the penalty move at the act's execution); the market's side - the issue, the open sol, the bloc, the rate, every settlement and the
// withdrawal - is written only inside RollMarket, from the rivals' data, E4's market state and a sol-close RECORD of the politics kept by a
// journal keyed by each act's own ExecuteAt, never by the live identity or tension, which differ between the bands. So, given the same accepted
// acts and orders, every one of them is identical in the agent and the era band. Integer money only: no float or double enters the rate, a
// conversion or the desk. Rivals by name string, never by FName index. Nothing here reads or writes Hope, the population, growth or Influence.

namespace
{
	constexpr int32 CurMaxList = 4096;
	constexpr int64 CurViewMax = 100000;              // the record's bounds: the identity in [-100000, 100000], the tension in [0, 100000]

	bool CurNameLess(const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; }
	int64 CurClamp64(int64 V, int64 Lo, int64 Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	int64 CurAbs64(int64 V) { return V < 0 ? -V : V; }
	int64 CurMax64(int64 A, int64 B) { return A > B ? A : B; }
	int64 CurMin64(int64 A, int64 B) { return A < B ? A : B; }

	// llround(V x 1000), half away from zero. Used only where an act or a sync enters the record (the act's live value is exact in both bands:
	// the identity moves only in whole steps, and the tension enters as its nominal config delta) - never in the rate, a conversion or the desk.
	int64 CurMilli(double V)
	{
		const double M = V * 1000.0;
		return M >= 0.0 ? (int64)FMath::FloorToDouble(M + 0.5) : -(int64)FMath::FloorToDouble(-M + 0.5);
	}

	bool CurEntryLess(const FRHPoliticalEntry& A, const FRHPoliticalEntry& B)
	{
		if (A.ExecuteAt != B.ExecuteAt) { return A.ExecuteAt < B.ExecuteAt; }
		if (A.CommandId != B.CommandId) { return A.CommandId < B.CommandId; }
		return A.Sub < B.Sub;
	}

	// E4's quote margins (RHSimMarket.cpp's MktHiQuote / MktLoQuote), named apart so the unity build never meets two definitions.
	int64 CurHiQuote(int64 Q, int64 MaxStepBp) { return Q * (10000 + MaxStepBp) / 10000 + 1; }
	int64 CurLoQuote(int64 Q, int64 MaxStepBp) { return CurMax64(0, Q * (10000 - MaxStepBp) / 10000 - 1); }

	const TCHAR* CurDeskSource(uint8 Dir) { return Dir == (uint8)URHSimWorldSubsystem::ERHDeskDir::ToMarks ? TEXT("credits") : TEXT("marks"); }
}

// --- The config and the start ------------------------------------------------------------------------------------------------------------

double URHSimWorldSubsystem::CurrencyConfigClamp(FName Row, double Raw)
{
	// Each row's sane range, as InitMarket clamps E4's (3.6): the integer rows are rounded first. A row this does not know passes through.
	const double V = FMath::IsFinite(Raw) ? Raw : 0.0;
	const FString N = Row.ToString();
	if (N == TEXT("CurrencyIssueIdentity")) { return FMath::Clamp(V, -100.0, 100.0); }
	if (N == TEXT("CurrencyIssueTension")) { return FMath::Clamp(V, 0.0, 100.0); }
	if (N == TEXT("CurrencyRequisitionPenalty")) { return FMath::Clamp(V, 0.0, 1.0); }
	if (N == TEXT("CurrencyRateStartMcr")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 1, 1000000); }
	if (N == TEXT("CurrencyWindowSols")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 1, BlocRing); }
	if (N == TEXT("CurrencyMinVolumeMmk")) { return (double)FMath::Max<int64>(1, FMath::RoundToInt64(V)); }
	if (N == TEXT("CurrencyTradeWeight") || N == TEXT("CurrencyIdentityWeight") || N == TEXT("CurrencyTensionWeight") || N == TEXT("CurrencyAnchorWeight") || N == TEXT("CurrencyGainPct"))
	{
		return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 0, 1000);
	}
	if (N == TEXT("CurrencyMaxStepBp")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 1, 10000); }
	if (N == TEXT("CurrencyFloorBp")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 1, 9999); }
	if (N == TEXT("CurrencyCeilBp")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 10001, 1000000); }
	if (N == TEXT("DeskSpreadBp")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 0, 9999); }
	if (N == TEXT("DeskCapPerSol")) { return (double)FMath::Clamp<int64>(FMath::RoundToInt64(V), 0, 1000000); }
	return V;
}

void URHSimWorldSubsystem::InitCurrency()
{
	if (Defs)
	{
		const auto Cfg = [this](const TCHAR* Name, double Fallback) { return CurrencyConfigClamp(FName(Name), Defs->GetConfigScalar(FName(Name), Fallback)); };
		CurrencyIssueIdentity = Cfg(TEXT("CurrencyIssueIdentity"), CurrencyIssueIdentity);
		CurrencyIssueTension = Cfg(TEXT("CurrencyIssueTension"), CurrencyIssueTension);
		CurrencyRequisitionPenalty = Cfg(TEXT("CurrencyRequisitionPenalty"), CurrencyRequisitionPenalty);
		CurrencyRateStartMcr = (int64)Cfg(TEXT("CurrencyRateStartMcr"), (double)CurrencyRateStartMcr);
		CurrencyWindowSols = (int32)Cfg(TEXT("CurrencyWindowSols"), CurrencyWindowSols);
		CurrencyMinVolumeMmk = (int64)Cfg(TEXT("CurrencyMinVolumeMmk"), (double)CurrencyMinVolumeMmk);
		CurrencyTradeWeight = (int32)Cfg(TEXT("CurrencyTradeWeight"), CurrencyTradeWeight);
		CurrencyIdentityWeight = (int32)Cfg(TEXT("CurrencyIdentityWeight"), CurrencyIdentityWeight);
		CurrencyTensionWeight = (int32)Cfg(TEXT("CurrencyTensionWeight"), CurrencyTensionWeight);
		CurrencyAnchorWeight = (int32)Cfg(TEXT("CurrencyAnchorWeight"), CurrencyAnchorWeight);
		CurrencyGainPct = (int32)Cfg(TEXT("CurrencyGainPct"), CurrencyGainPct);
		CurrencyMaxStepBp = (int32)Cfg(TEXT("CurrencyMaxStepBp"), CurrencyMaxStepBp);
		CurrencyFloorBp = (int32)Cfg(TEXT("CurrencyFloorBp"), CurrencyFloorBp);
		CurrencyCeilBp = (int32)Cfg(TEXT("CurrencyCeilBp"), CurrencyCeilBp);
		DeskSpreadBp = (int64)Cfg(TEXT("DeskSpreadBp"), (double)DeskSpreadBp);
		DeskCapPerSol = (int64)Cfg(TEXT("DeskCapPerSol"), (double)DeskCapPerSol);
	}
	ResetCurrencyState();
}

void URHSimWorldSubsystem::GetCurrencyConfig(TArray<TPair<FName, double>>& Out) const
{
	Out.Reset();
	Out.Emplace(FName("CurrencyIssueIdentity"), CurrencyIssueIdentity);
	Out.Emplace(FName("CurrencyIssueTension"), CurrencyIssueTension);
	Out.Emplace(FName("CurrencyRequisitionPenalty"), CurrencyRequisitionPenalty);
	Out.Emplace(FName("CurrencyRateStartMcr"), (double)CurrencyRateStartMcr);
	Out.Emplace(FName("CurrencyWindowSols"), (double)CurrencyWindowSols);
	Out.Emplace(FName("CurrencyMinVolumeMmk"), (double)CurrencyMinVolumeMmk);
	Out.Emplace(FName("CurrencyTradeWeight"), (double)CurrencyTradeWeight);
	Out.Emplace(FName("CurrencyIdentityWeight"), (double)CurrencyIdentityWeight);
	Out.Emplace(FName("CurrencyTensionWeight"), (double)CurrencyTensionWeight);
	Out.Emplace(FName("CurrencyAnchorWeight"), (double)CurrencyAnchorWeight);
	Out.Emplace(FName("CurrencyGainPct"), (double)CurrencyGainPct);
	Out.Emplace(FName("CurrencyMaxStepBp"), (double)CurrencyMaxStepBp);
	Out.Emplace(FName("CurrencyFloorBp"), (double)CurrencyFloorBp);
	Out.Emplace(FName("CurrencyCeilBp"), (double)CurrencyCeilBp);
	Out.Emplace(FName("DeskSpreadBp"), (double)DeskSpreadBp);
	Out.Emplace(FName("DeskCapPerSol"), (double)DeskCapPerSol);
}

void URHSimWorldSubsystem::ResetCurrencyState()
{
	// Unissued, every purse and list empty, the rate at its start; the record seeded from the live identity and tension (they are exact at the
	// world's start and at a load, the only two callers).
	bCurrencyIssued = false;
	CurrencyIssueSol = -1;
	CurrencyOpenSol = -1;
	bWithdrawPending = false;
	WithdrawClearSol = -1;
	RateMcr = CurrencyRateStartMcr;
	IdentityViewMilli = CurClamp64(CurMilli(IdentityAxis), -CurViewMax, CurViewMax);
	TensionViewMilli = CurClamp64(CurMilli(EarthTension), 0, CurViewMax);
	Journal.Reset();
	for (int32 e = 0; e < BlocRing; ++e)
	{
		BlocSoldMmk[e] = 0;
		BlocBoughtMmk[e] = 0;
	}
	BlocFill = 0;
	BlocHead = 0;
	MarkBalanceMmk = 0;
	MarkEarnedMmk = 0;
	MarkSpentMmk = 0;
	MarkReservedMmk = 0;
	DeskCreditMcr = 0;
	CurrencyRedeemedMcr = 0;
	BlocMembers.Reset();
	DeskOrders.Reset();
	DeskClearings.Reset();
	LastRateStepMcr = 0;
	RateStepNote = 0; // no close seen since the world started or the colony loaded: the deck does not call the step "unchanged"
}

// --- Money: the mark purse, the rate band, the conversions ----------------------------------------------------------------------------------

void URHSimWorldSubsystem::MarkPay(int64 Mmk)
{
	if (Mmk <= 0)
	{
		return;
	}
	MarkBalanceMmk += Mmk;
	MarkEarnedMmk += Mmk;
}

int64 URHSimWorldSubsystem::MarkSpend(int64 Mmk)
{
	// A mirror of CreditSpend: one debit writer, which pays min(Mmk, Balance) so the balance never goes below 0, and returns the rest.
	if (Mmk <= 0)
	{
		return 0;
	}
	const int64 Pay = Mmk < MarkBalanceMmk ? Mmk : MarkBalanceMmk;
	MarkBalanceMmk -= Pay;
	MarkSpentMmk += Pay;
	return Mmk - Pay;
}

int64 URHSimWorldSubsystem::RateFloorMcr() const
{
	return CurMax64(1, CurrencyRateStartMcr * CurrencyFloorBp / 10000);
}

int64 URHSimWorldSubsystem::RateCeilMcr() const
{
	return CurMax64(RateFloorMcr(), CurrencyRateStartMcr * CurrencyCeilBp / 10000);
}

int64 URHSimWorldSubsystem::ConvertChargeMcr(int64 NetMmk, int64 Rate) const
{
	// A charge in credits for a net in marks, rounded up (against the colony): ceil(Net x r / 1000), Net > 0.
	return NetMmk <= 0 ? 0 : (NetMmk * Rate + 999) / 1000;
}

int64 URHSimWorldSubsystem::ConvertOwedMcr(int64 NetMmk, int64 Rate) const
{
	// What a rival owes in credits for a net in marks, rounded down (against the colony): floor(Net x r / 1000), Net > 0.
	return NetMmk <= 0 ? 0 : NetMmk * Rate / 1000;
}

int64 URHSimWorldSubsystem::GetConvertedReservationMcr(FName Rival, const TArray<FRHConvoyLine>& Lines, int64 Rate) const
{
	// 2.4: R = max(0, ceil(sum(buy kg x Hi(q)) x HiR(r) / 1000) - floor(sum(sell kg x Lo(q)) x LoR(r) / 1000)). The clearing price lies in
	// [Lo(q), Hi(q)] and the clearing rate in [LoR(r), HiR(r)] (a booking never sees a clearing more than one roll ahead, and each steps once a
	// roll), and ceil(a - b) <= ceil(a) - floor(b), so R covers the rounded-up charge.
	const FRHRivalEconomy* E = FindRivalEconomy(Rival);
	const FRHMarket* Mk = E ? FindMarket(E->RegionId) : nullptr;
	if (!Mk)
	{
		return 0;
	}
	int64 BuyMmk = 0, SellMmk = 0;
	for (const FRHConvoyLine& L : Lines)
	{
		const int32* SlotPtr = EconIndex.Find(L.Resource);
		if (!SlotPtr || !Mk->PriceMcr.IsValidIndex(*SlotPtr))
		{
			continue;
		}
		const int64 Quote = Mk->PriceMcr[*SlotPtr];
		if (L.Kg > 0) { BuyMmk += (int64)L.Kg * CurHiQuote(Quote, MarketMaxStepBp); }
		else { SellMmk += -(int64)L.Kg * CurLoQuote(Quote, MarketMaxStepBp); }
	}
	const int64 HiR = Rate * (10000 + CurrencyMaxStepBp) / 10000 + 1;
	const int64 LoR = CurMax64(1, Rate * (10000 - CurrencyMaxStepBp) / 10000 - 1);
	const int64 BuyMcr = (BuyMmk * HiR + 999) / 1000;
	const int64 SellMcr = SellMmk * LoR / 1000;
	return CurMax64(0, BuyMcr - SellMcr);
}

// --- Reads -----------------------------------------------------------------------------------------------------------------------------------

int32 URHSimWorldSubsystem::GetBookingSolNow() const
{
	const double Now = Clock ? Clock->GetSimSecondsTotal() : 0.0;
	return FMath::Max(FMath::FloorToInt32((Now + OrderLagSeconds) / (double)URHSimClockSubsystem::SolLengthSimSeconds), MarketCheckedSol);
}

uint8 URHSimWorldSubsystem::GetOrderCurrencyFor(FName Rival, int32 EffSol) const
{
	// 2.3: marks iff the market is issued and open for the clearing sol and the rival is a member from it; else, once the market is issued and
	// open, credits converted at the rate; else E4's order. Every conversion is gated on the market's issue, never on the open sol alone.
	if (!IsConvertingAt(EffSol))
	{
		return 0;
	}
	const int32 From = GetBlocMemberFromSol(Rival);
	return (From >= 0 && From <= EffSol) ? 1 : 2;
}

int32 URHSimWorldSubsystem::GetBlocMemberFromSol(FName Rival) const
{
	const FRHBlocMember* M = BlocMembers.FindByPredicate([Rival](const FRHBlocMember& X) { return X.Rival == Rival; });
	return M ? M->MemberFromSol : -1;
}

void URHSimWorldSubsystem::GetBlocMembers(TArray<TPair<FName, int32>>& Out) const
{
	Out.Reset();
	for (const FRHBlocMember& M : BlocMembers)
	{
		Out.Emplace(M.Rival, M.MemberFromSol);
	}
}

bool URHSimWorldSubsystem::GetBlocTally(int32 Back, int64& OutSoldMmk, int64& OutBoughtMmk) const
{
	OutSoldMmk = 0;
	OutBoughtMmk = 0;
	if (Back < 1 || Back > BlocFill)
	{
		return false;
	}
	const int32 Entry = (BlocHead - Back + 2 * BlocRing) % BlocRing;
	OutSoldMmk = BlocSoldMmk[Entry];
	OutBoughtMmk = BlocBoughtMmk[Entry];
	return true;
}

bool URHSimWorldSubsystem::GetRateInputsBp(int32& OutTrade, int32& OutIdent, int32& OutTension) const
{
	// What the last step read: the record and the bloc tally are unchanged between a roll and the next, and the rate step is a roll's last
	// write, so these are the inputs of the step the last roll took (none at the issue roll). Never the live axes (3.2).
	OutTrade = 0;
	OutIdent = 0;
	OutTension = 0;
	if (CurrencyIssueSol < 0)
	{
		return false;
	}
	int64 Sold = 0, Bought = 0;
	const int32 Back = FMath::Min(CurrencyWindowSols, BlocFill);
	for (int32 b = 1; b <= Back; ++b)
	{
		const int32 Entry = (BlocHead - b + 2 * BlocRing) % BlocRing;
		Sold += BlocSoldMmk[Entry];
		Bought += BlocBoughtMmk[Entry];
	}
	OutTrade = (int32)MarketDivTrunc((Sold - Bought) * 10000, CurMax64(Sold + Bought, CurrencyMinVolumeMmk));
	OutIdent = (int32)MarketDivTrunc(IdentityViewMilli, 10);
	OutTension = (int32)MarketDivTrunc(TensionViewMilli, 10);
	return true;
}

bool URHSimWorldSubsystem::GetDeskBooked(int32 Sol, int64& OutToMarks, int64& OutToCredits) const
{
	int64 ToMarks = 0, ToCredits = 0;
	for (const FRHDeskOrder& D : DeskOrders)
	{
		if (D.ClearSol == Sol)
		{
			(D.Dir == (uint8)ERHDeskDir::ToMarks ? ToMarks : ToCredits) += D.Amount;
		}
	}
	OutToMarks = ToMarks / 1000;
	OutToCredits = ToCredits / 1000;
	return ToMarks + ToCredits > 0;
}

bool URHSimWorldSubsystem::GetMarkOrder(int32 ConvoyId, int64& OutReservedMmk, int64& OutOwedMmk) const
{
	const FRHConvoy* C = Convoys.FindByPredicate([ConvoyId](const FRHConvoy& X) { return X.Id == ConvoyId; });
	OutReservedMmk = (C && C->bMarks) ? C->ReservedMmk : 0;
	OutOwedMmk = (C && C->bMarks) ? C->OwedMmk : 0;
	return C && C->bMarks;
}

// --- Harness hooks ---------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::Debug_SyncPoliticalView()
{
	IdentityViewMilli = CurClamp64(CurMilli(IdentityAxis), -CurViewMax, CurViewMax);
	TensionViewMilli = CurClamp64(CurMilli(EarthTension), 0, CurViewMax);
}

void URHSimWorldSubsystem::Debug_SetRate(int64 Mcr)
{
	RateMcr = CurClamp64(Mcr, RateFloorMcr(), RateCeilMcr());
}

void URHSimWorldSubsystem::Debug_SetBlocTally(int64 SoldMmkPerSol, int64 BoughtMmkPerSol)
{
	for (int32 e = 0; e < BlocRing; ++e)
	{
		BlocSoldMmk[e] = CurMax64(0, SoldMmkPerSol);
		BlocBoughtMmk[e] = CurMax64(0, BoughtMmkPerSol);
	}
	BlocFill = BlocRing;
}

// --- The acts --------------------------------------------------------------------------------------------------------------------------------

bool URHSimWorldSubsystem::CanIssueCurrency(bool bDefyIssue, FString& OutWhy) const
{
	// 1.1 with R7 O1, in this order. DefyIssue reads the identity its own Defy will leave (clamped), the stand-alone issue the live one. The
	// partner test is read the way the issue roll's admissions read the record (whole points, truncated), so at least one rival joins there.
	OutWhy.Reset();
	if (!bCurrencyEnabled)
	{
		OutWhy = TEXT("No currency: disabled");
		return false;
	}
	if (bCurrencyIssued)
	{
		OutWhy = TEXT("The mark is already issued");
		return false;
	}
	if (bWithdrawPending)
	{
		OutWhy = TEXT("The mark is being withdrawn");
		return false;
	}
	const double After = bDefyIssue ? FMath::Clamp(IdentityAxis + DilemmaDefyAxisShift, -100.0, 100.0) : IdentityAxis;
	const int64 AfterWhole = MarketDivTrunc(CurMilli(After), 1000);
	SyncRivalEconomies(false);
	bool bPartner = false;
	for (const FRHRivalEconomy& E : RivalEconomies)
	{
		if (!E.bHasEconomy || ClosedRoutes.Contains(E.Rival))
		{
			continue;
		}
		const FRHRivalRow* Row = Defs ? Defs->GetRival(E.Rival) : nullptr;
		bPartner |= Row && Row->BlocStance >= 0 && (int64)Row->BlocStance <= AfterWhole;
	}
	if (!bPartner)
	{
		OutWhy = TEXT("No neighbour would join the mark");
		return false;
	}
	if (!bDefyIssue && IdentityAxis < CurrencyIssueIdentity)
	{
		OutWhy = FString::Printf(TEXT("The colony does not lean Martian yet (identity %+.0f, needs %+.0f)"), IdentityAxis, CurrencyIssueIdentity);
		return false;
	}
	return true;
}

void URHSimWorldSubsystem::AppendJournal(const FRHCommand& Cmd, int32 Sub, ERHPoliticalAct Kind, bool bHasIdentity, int64 IdentityMilli, int64 TensionDeltaMilli)
{
	// Keyed by the act's OWN ExecuteAt: the sol it falls in is the roll that applies it, in both bands, whichever step ran the act (3.2).
	if (!bCurrencyEnabled)
	{
		return;
	}
	FRHPoliticalEntry E;
	E.ClearSol = FMath::FloorToInt32(Cmd.ExecuteAtSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
	E.ExecuteAt = Cmd.ExecuteAtSimSeconds;
	E.CommandId = Cmd.CommandId;
	E.Sub = Sub;
	E.Kind = (uint8)Kind;
	E.bHasIdentity = bHasIdentity;
	E.IdentityMilli = IdentityMilli;
	E.TensionDeltaMilli = TensionDeltaMilli;
	const int32 At = Journal.IndexOfByPredicate([&E](const FRHPoliticalEntry& X) { return CurEntryLess(E, X); });
	if (At == INDEX_NONE)
	{
		Journal.Add(E);
	}
	else
	{
		Journal.Insert(E, At);
	}
}

void URHSimWorldSubsystem::JournalDilemma(const FRHCommand& Cmd, bool bComply)
{
	// The Defy (and DefyIssue's Defy half) or the Comply: the identity the act left, live (exact in both bands), and the nominal tension delta.
	AppendJournal(Cmd, 0, bComply ? ERHPoliticalAct::Comply : ERHPoliticalAct::Defy, true, CurMilli(IdentityAxis),
		-CurMilli(bComply ? DilemmaComplyTensionDrop : DilemmaDefyTensionDrop));
	if (bComply && bCurrencyIssued)
	{
		// Q-B (a): a Comply withdraws the mark. Earth's penalty ends now; the market closes until the withdrawal roll, the first at or after
		// this order's sol with no order uncleared, redeems every mark at the rate in force (no spread, no cap) and re-expresses the prices.
		// One banner per act (the banner keeps only the last alert): the Comply's own alert, Gate D's final D43, stays on it. The withdrawal's
		// start is a log line here; its news reaches the banner with THE MARK IS RETIRED at the withdrawal roll.
		bCurrencyIssued = false;
		bWithdrawPending = true;
		WithdrawClearSol = FMath::FloorToInt32(Cmd.ExecuteAtSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
		AppendJournal(Cmd, 1, ERHPoliticalAct::Withdraw, false, 0, 0);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CURRENCY: the mark is withdrawn at Earth's demand; the market is closed until its marks are redeemed (at the end of sol %d or later) ==="), WithdrawClearSol);
	}
}

FString URHSimWorldSubsystem::CurrencyIssueAlert(bool bAfterDefy)
{
	// The issue's alert (placeholder wording, gate-d-wording Y10). After DefyIssue's Defy it is a clause the Defy's own alert (Gate D's final
	// D44, unchanged) carries, so that act shows one banner, not two in one tick.
	return bAfterDefy ? FString(TEXT("And the colony issues the mark: Earth reads it as a further step away, so tension rises and its supply ships shrink while the mark exists."))
		: FString(TEXT("A MARTIAN CURRENCY — the colony issues the mark. Earth reads it as a step away: tension rises, and its supply ships shrink while the mark exists."));
}

void URHSimWorldSubsystem::IssueCurrency(const FRHCommand& Cmd, int32 Sub, bool bAlert)
{
	// 1.2, live at the act: tension and the flag (the penalty reads it). It does not touch the identity, HumanNature, Influence, relations,
	// Hope or the endings. The market's half follows at the roll that closes this order's sol (the Issue entry). bAlert: the stand-alone issue
	// raises its own alert; DefyIssue's issue half does not (its news rides on the Defy's alert).
	EarthTension = FMath::Clamp(EarthTension + CurrencyIssueTension, 0.0, 100.0);
	bCurrencyIssued = true;
	if (bAlert)
	{
		OnAlert.Broadcast(CurrencyIssueAlert(false));
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("=== CURRENCY ISSUED: the mark (identity %+.0f, tension %.0f, requisition x%.2f) ==="),
		IdentityAxis, EarthTension, GetRequisitionMultiplier());
	AppendJournal(Cmd, Sub, ERHPoliticalAct::Issue, false, 0, CurMilli(CurrencyIssueTension));
}

// --- The desk ------------------------------------------------------------------------------------------------------------------------------

bool URHSimWorldSubsystem::CanExchange(ERHDeskDir Dir, int64 Amount, int32 EffSol, FString& OutWhy) const
{
	// 2.5, in its order: the desk is open for the order's clearing sol; the mark is not being withdrawn; a member the colony can trade with
	// (admitted at an earlier roll, whatever its MemberFromSol: re-review 1); the daily cap; the source purse's spendable amount. While a
	// withdrawal is pending the desk gives the text every priced order gets (the brief's binding 10, from REVISION R5, which outranks the
	// design's 2.5 whose check-2 text the brief's 3.3 copied); the flag is read too, so a mark that is not issued never trades at the desk.
	OutWhy.Reset();
	const TCHAR* Source = CurDeskSource((uint8)Dir);
	if (Amount < 1 || Amount > 1000000000)
	{
		OutWhy = FString::Printf(TEXT("An exchange is a whole number of %s, at least 1"), Source);
		return false;
	}
	if (!IsConvertingAt(EffSol))
	{
		if (CurrencyIssueSol >= 0)
		{
			OutWhy = FString::Printf(TEXT("The desk opens at the end of sol %d"), CurrencyOpenSol);
			return false;
		}
		const FRHPoliticalEntry* Pending = Journal.FindByPredicate([](const FRHPoliticalEntry& X) { return X.Kind == (uint8)ERHPoliticalAct::Issue; });
		OutWhy = Pending ? FString::Printf(TEXT("The desk opens at the end of sol %d"), FMath::Max(Pending->ClearSol, MarketCheckedSol) + 2)
			: FString(TEXT("No Martian currency is issued"));
		return false;
	}
	if (bWithdrawPending || !bCurrencyIssued)
	{
		OutWhy = TEXT("The market is closed while the mark is withdrawn");
		return false;
	}
	bool bPartner = false;
	for (const FRHBlocMember& M : BlocMembers)
	{
		bPartner |= IsRivalAvailable(M.Rival) && !ClosedRoutes.Contains(M.Rival) && !DefectedRivals.Contains(M.Rival) && !EmbargoGrace.Contains(M.Rival);
	}
	if (!bPartner)
	{
		OutWhy = TEXT("No bloc member can trade with you");
		return false;
	}
	int64 BookedMilli = 0;
	for (const FRHDeskOrder& D : DeskOrders)
	{
		BookedMilli += (D.Dir == (uint8)Dir && D.ClearSol == EffSol) ? D.Amount : 0;
	}
	if (BookedMilli + Amount * 1000 > DeskCapPerSol * 1000)
	{
		OutWhy = FString::Printf(TEXT("The desk takes at most %lld %s a sol (%lld booked)"), DeskCapPerSol, Source, BookedMilli / 1000);
		return false;
	}
	const int64 Spendable = Dir == ERHDeskDir::ToMarks ? GetSpendableMcr() : GetSpendableMmk();
	if (Amount * 1000 > Spendable)
	{
		OutWhy = FString::Printf(TEXT("Not enough %s spendable (%s)"), Source, *FormatCredits(Spendable));
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::BookExchange(const FRHCommand& Cmd, FString& OutWhy)
{
	// The Exchange verb's worker, any step. Like BookConvoy: the clearing sol is the order's OWN ExecuteAt sol (E4 R3 (b)'s catch-up first, then
	// the open sol for a late booking); the amount is set aside in its source purse; the order's Id is its CommandId.
	ERHDeskDir Dir = ERHDeskDir::ToMarks;
	if (Cmd.Target == FName("ToCredits"))
	{
		Dir = ERHDeskDir::ToCredits;
	}
	else if (Cmd.Target != FName("ToMarks"))
	{
		OutWhy = FString::Printf(TEXT("No such exchange '%s' (ToMarks or ToCredits)"), *Cmd.Target.ToString());
		return false;
	}
	const double Value = Cmd.Value;
	if (!(Value >= 1.0) || Value > 1.0e9 || Value != FMath::FloorToDouble(Value))
	{
		OutWhy = FString::Printf(TEXT("An exchange is a whole number of %s, at least 1"), CurDeskSource((uint8)Dir));
		return false;
	}
	const int64 Amount = (int64)Value;
	const int32 ClearSol = FMath::FloorToInt32(Cmd.ExecuteAtSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
	if (ClearSol > MarketCheckedSol + 1)
	{
		RollMarket(ClearSol);
	}
	const int32 EffSol = FMath::Max(ClearSol, MarketCheckedSol);
	if (!CanExchange(Dir, Amount, EffSol, OutWhy))
	{
		return false;
	}
	FRHDeskOrder D;
	D.Id = Cmd.CommandId;
	D.Dir = (uint8)Dir;
	D.Amount = Amount * 1000;
	D.ClearSol = EffSol;
	if (Dir == ERHDeskDir::ToMarks)
	{
		CreditReservedMcr += D.Amount;
	}
	else
	{
		MarkReservedMmk += D.Amount;
	}
	const int32 At = DeskOrders.IndexOfByPredicate([&D](const FRHDeskOrder& X) { return X.Id > D.Id; });
	if (At == INDEX_NONE)
	{
		DeskOrders.Add(D);
	}
	else
	{
		DeskOrders.Insert(D, At);
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Desk #%d: %lld %s to %s, set aside; clears at the end of sol %d"), D.Id, Amount, CurDeskSource(D.Dir),
		Dir == ERHDeskDir::ToMarks ? TEXT("marks") : TEXT("credits"), EffSol);
	return true;
}

void URHSimWorldSubsystem::ClearDeskOrders(int32 ClosingSol, int32 DueUpToSol)
{
	// In RollMarket's clearing, after the convoys, by Id, at the rate in force during the closing sol, less the spread S. Each payout is ONE
	// floor of the exact rational (trap 11): ToMarks floor(A x 1000 x (10000 - S) / (r x 10000)) mmk, ToCredits floor(A x r x (10000 - S) /
	// (1000 x 10000)) mcr. Desk flows never enter the bloc tally: only goods traded with the bloc move the trade term (2.5).
	if (DeskOrders.Num() == 0)
	{
		return;
	}
	const int64 Rate = RateMcr;
	const int64 Keep = 10000 - DeskSpreadBp;
	for (int32 i = 0; i < DeskOrders.Num();)
	{
		const FRHDeskOrder D = DeskOrders[i];
		if (D.ClearSol > DueUpToSol)
		{
			++i;
			continue;
		}
		int64 Paid = 0, Payout = 0;
		if (D.Dir == (uint8)ERHDeskDir::ToMarks)
		{
			CreditReservedMcr = CurMax64(0, CreditReservedMcr - D.Amount);
			Paid = D.Amount - CreditSpend(D.Amount);
			Payout = Paid * 1000 * Keep / (Rate * 10000);
			MarkPay(Payout);
		}
		else
		{
			MarkReservedMmk = CurMax64(0, MarkReservedMmk - D.Amount);
			Paid = D.Amount - MarkSpend(D.Amount);
			// Still ONE floor of Paid x r x Keep / 10^7, split so no product leaves int64 inside the config clamps (a desk order is at most
			// DeskCapPerSol x 1000 <= 1e9 mmk, the rate at most RateCeilMcr <= 1e8, Keep at most 1e4): with Paid x r = q x 10^7 + m, the floor
			// is q x Keep + floor(m x Keep / 10^7), exact.
			const int64 PaidRate = Paid * Rate;
			Payout = (PaidRate / (1000 * 10000)) * Keep + (PaidRate % (1000 * 10000)) * Keep / (1000 * 10000);
			CreditPay(Payout);
			DeskCreditMcr += Payout;
		}
		if (Paid < D.Amount)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Desk #%d: only %s of its %s %s could be paid"), D.Id, *FormatCredits(Paid), *FormatCredits(D.Amount), CurDeskSource(D.Dir));
		}
		FRHDeskClearing Rec;
		Rec.Id = D.Id;
		Rec.Dir = D.Dir;
		Rec.Amount = D.Amount;
		Rec.Paid = Paid;
		Rec.Payout = Payout;
		Rec.RateMcr = Rate;
		Rec.ClearSol = D.ClearSol;
		Rec.ClosingSol = ClosingSol;
		DeskClearings.Add(Rec);
		if (DeskClearings.Num() > 64)
		{
			DeskClearings.RemoveAt(0, DeskClearings.Num() - 64);
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("Desk #%d: %s %s -> %s %s at %s cr a mark (end of sol %d)"), D.Id, *FormatCredits(Paid),
			D.Dir == (uint8)ERHDeskDir::ToMarks ? TEXT("cr") : TEXT("marks"), *FormatCredits(Payout), D.Dir == (uint8)ERHDeskDir::ToMarks ? TEXT("marks") : TEXT("cr"),
			*FormatCredits(Rate), ClosingSol);
		DeskOrders.RemoveAt(i);
	}
}

// --- The roll: the record, the transitions, the bloc, the rate -----------------------------------------------------------------------------

void URHSimWorldSubsystem::StepCurrencyPass(int32 ClosingSol, int32 LastClosedSol)
{
	// One pass of RollMarket, after its clearing and reprice and outside its live gate, in this order (brief 3.3): 1 the record; 2 the
	// transitions - the market's issue, then the withdrawal if it is due; 3 the admissions; 4 the rate step. On a long jump's last pass the
	// sol being closed is SolNow - 1 (LastClosedSol; brief M6): the record walks every skipped sol, and the rest read that sol.
	const int32 SolClosed = LastClosedSol;
	const bool bIssueEntry = StepPoliticalRecord(ClosingSol, LastClosedSol);
	if (bIssueEntry && CurrencyIssueSol < 0)
	{
		// 1.3: the market issues the mark at the roll closing the act's sol; it counts in marks two sols later, so every booking's currency is
		// decided by rolls both bands have run (section 6). The first rate step is at the next roll.
		CurrencyIssueSol = SolClosed;
		CurrencyOpenSol = SolClosed + 2;
		RateMcr = CurrencyRateStartMcr;
		LastRateStepMcr = 0;
		RateStepNote = 1; // set by the issue: its first step comes at the next roll
		UE_LOG(LogRedHopeSim, Display, TEXT("Currency: the market issues the mark at the end of sol %d; orders clearing at the end of sol %d or later count in marks (%s cr a mark)"),
			CurrencyIssueSol, CurrencyOpenSol, *FormatCredits(RateMcr));
	}
	bool bWithdrew = false;
	if (IsWithdrawalDue(SolClosed))
	{
		RunWithdrawal(SolClosed);
		bWithdrew = true;
	}
	if (!bWithdrew && CurrencyIssueSol >= 0)
	{
		AdmitBlocMembers(SolClosed); // from the issue roll on, inclusive (R7 O2); never at or after a withdrawal in this roll
	}
	if (!bWithdrew && CurrencyIssueSol >= 0 && CurrencyIssueSol < SolClosed)
	{
		StepRate(); // once a pass, from the roll after the issue roll; never at the withdrawal roll
	}
}

bool URHSimWorldSubsystem::StepPoliticalRecord(int32 ClosingSol, int32 LastClosedSol)
{
	// 3.2, silent (it logs nothing; nothing reads it before an issue). One sol at a time, from the pass's closing sol to the last sol it closes
	// (more than one only on a long jump's last pass): that sol's drift, then every entry due by it, in (ExecuteAt, CommandId, Sub) order - an
	// identity entry sets the record to the value the act left, a tension delta adds - clamped to [0, 100000] after each. So a jump leaves the
	// record closing those sols one by one would leave, clamps included. The drift is data-gated: it runs while any rival row is SliceActive
	// (the live drift also counts a discovered rival, which only the agent band discovers: a stated deviation, risk 3).
	bool bAnyActive = false;
	if (Defs)
	{
		Defs->ForEachRival([&bAnyActive](FName, const FRHRivalRow&) { bAnyActive = true; });
	}
	const int64 Drift = bAnyActive ? CurMilli(EarthTensionDriftPerSol) : 0;
	const int32 Last = FMath::Max(ClosingSol, LastClosedSol);
	if (Journal.Num() == 0)
	{
		// Nothing to apply between the drifts: n of them at once leave what n one at a time do (a constant step meets only one of the clamps).
		TensionViewMilli = CurClamp64(TensionViewMilli + Drift * (int64)(Last - ClosingSol + 1), 0, CurViewMax);
		return false;
	}
	bool bIssue = false;
	for (int32 Sol = ClosingSol; Sol <= Last; ++Sol)
	{
		TensionViewMilli = CurClamp64(TensionViewMilli + Drift, 0, CurViewMax);
		TArray<FRHPoliticalEntry> Left;
		for (const FRHPoliticalEntry& E : Journal)
		{
			if (E.ClearSol > Sol)
			{
				Left.Add(E);
				continue;
			}
			if (E.bHasIdentity)
			{
				IdentityViewMilli = CurClamp64(E.IdentityMilli, -CurViewMax, CurViewMax);
			}
			TensionViewMilli = CurClamp64(TensionViewMilli + E.TensionDeltaMilli, 0, CurViewMax);
			bIssue |= E.Kind == (uint8)ERHPoliticalAct::Issue;
		}
		if (Left.Num() != Journal.Num())
		{
			Journal = MoveTemp(Left);
		}
	}
	return bIssue;
}

bool URHSimWorldSubsystem::IsWithdrawalDue(int32 SolClosed) const
{
	// R5 + re-review 5: the first roll at or after the Comply's sol with no uncleared PRICED convoy (a barter convoy never clears) and no
	// pending desk order. Booking refuses both from the Comply on, so this is that roll or the next.
	if (!bWithdrawPending || SolClosed < WithdrawClearSol || DeskOrders.Num() > 0)
	{
		return false;
	}
	return !Convoys.ContainsByPredicate([](const FRHConvoy& C) { return C.IsPriced() && !C.bCleared; });
}

void URHSimWorldSubsystem::RunWithdrawal(int32 SolClosed)
{
	// R5 with re-review 4 and 5, after the clearing and the reprice: 1 r is the rate in force during the closing sol (the start rate when the
	// issue and the withdrawal fall on one roll: the issue just set it); 2 ONE floor over the exact total of the balance and every on-road owed
	// mark, paid through CreditPay into CurrencyRedeemedMcr, so which convoys are home (band-dependent) cannot split the bands by a milli-credit;
	// the purse is emptied and the on-road convoys owe nothing more; 3 every slot of every region re-expressed in credits and held in E4's band;
	// 4 the bloc dissolves, the rate and the sols reset, and booking reopens. Admissions and the rate step skip this roll.
	const bool bIssued = CurrencyIssueSol >= 0;
	const int64 Rate = bIssued ? RateMcr : CurrencyRateStartMcr;
	int64 OnRoad = 0;
	for (const FRHConvoy& C : Convoys)
	{
		OnRoad += C.bMarks ? C.OwedMmk : 0;
	}
	const int64 Total = MarkBalanceMmk + OnRoad;
	const int64 Redeemed = (Total / 1000) * Rate + (Total % 1000) * Rate / 1000; // floor(Total x r / 1000), exact, with room in int64
	CreditPay(Redeemed);
	CurrencyRedeemedMcr += Redeemed;
	MarkSpend(MarkBalanceMmk);
	for (FRHConvoy& C : Convoys)
	{
		if (C.bMarks)
		{
			C.OwedMmk = 0;
			C.bMarks = false;
		}
	}
	if (MarkReservedMmk != 0)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("Currency: %s marks were still reserved at the withdrawal; released"), *FormatCredits(MarkReservedMmk));
		MarkReservedMmk = 0;
	}
	if (bIssued)
	{
		const int32 N = EconSlots.Num();
		for (FRHMarket& Mk : Markets)
		{
			for (int32 s = 0; s < N && s < Mk.PriceMcr.Num(); ++s)
			{
				Mk.PriceMcr[s] = CurClamp64(Mk.PriceMcr[s] * Rate / 1000, Mk.FloorMcr[s], Mk.CeilMcr[s]);
			}
		}
	}
	BlocMembers.Reset();
	for (int32 e = 0; e < BlocRing; ++e)
	{
		BlocSoldMmk[e] = 0;
		BlocBoughtMmk[e] = 0;
	}
	BlocFill = 0;
	BlocHead = 0;
	RateMcr = CurrencyRateStartMcr;
	LastRateStepMcr = 0;
	RateStepNote = 0;
	CurrencyIssueSol = -1;
	CurrencyOpenSol = -1;
	bWithdrawPending = false;
	WithdrawClearSol = -1;
	bCurrencyIssued = false; // already false since the Comply; held so here too, whatever a file said (a load repairs both flags set at once)
	OnAlert.Broadcast(FString::Printf(TEXT("THE MARK IS RETIRED — %s marks were exchanged for %s credits. The market trades in credits again."), *FormatCredits(Total), *FormatCredits(Redeemed)));
	UE_LOG(LogRedHopeSim, Display, TEXT("=== CURRENCY WITHDRAWN: %s marks redeemed for %s cr at %s ==="), *FormatCredits(Total), *FormatCredits(Redeemed), *FormatCredits(Rate));
	UE_LOG(LogRedHopeSim, Display, TEXT("Currency: the market counts in credits again from the end of sol %d"), SolClosed);
}

void URHSimWorldSubsystem::AdmitBlocMembers(int32 SolClosed)
{
	// Section 4: walk the economies by name string (never FName index). A rival with an economy that is not a member joins when the RECORD of the
	// identity meets its stance (0 <= stance <= identity); it trades in marks from two sols on. Only exact inputs: data, the record, the economy -
	// never a relation, an availability, an embargo or a defection (band-dependent); a hostile member is refused at booking with HEAD's texts.
	const int64 Ident = MarketDivTrunc(IdentityViewMilli, 1000);
	bool bAdded = false;
	for (const FRHRivalEconomy& E : RivalEconomies)
	{
		if (!E.bHasEconomy || GetBlocMemberFromSol(E.Rival) >= 0)
		{
			continue;
		}
		const FRHRivalRow* Row = Defs ? Defs->GetRival(E.Rival) : nullptr;
		if (!Row || Row->BlocStance < 0 || (int64)Row->BlocStance > Ident)
		{
			continue;
		}
		FRHBlocMember M;
		M.Rival = E.Rival;
		M.MemberFromSol = SolClosed + 2;
		BlocMembers.Add(M);
		bAdded = true;
		UE_LOG(LogRedHopeSim, Display, TEXT("Bloc: %s joins the mark (stance %+d, identity %+d)"), *E.Rival.ToString(), Row->BlocStance, (int32)Ident);
	}
	if (bAdded)
	{
		BlocMembers.Sort([](const FRHBlocMember& A, const FRHBlocMember& B) { return CurNameLess(A.Rival, B.Rival); });
	}
}

void URHSimWorldSubsystem::StepRate()
{
	// 3.3, int64 throughout, the one signed division MarketDivTrunc. A surplus with the bloc raises the mark, a Martian identity raises it,
	// Earth tension lowers it, the anchor pulls it back to the start. At most CurrencyMaxStepBp a step (or the 1 mcr minimum), held in the band.
	const int64 R0 = CurrencyRateStartMcr;
	int64 Sold = 0, Bought = 0;
	const int32 Back = FMath::Min(CurrencyWindowSols, BlocFill);
	for (int32 b = 1; b <= Back; ++b)
	{
		const int32 Entry = (BlocHead - b + 2 * BlocRing) % BlocRing;
		Sold += BlocSoldMmk[Entry];
		Bought += BlocBoughtMmk[Entry];
	}
	const int64 TradeBp = MarketDivTrunc((Sold - Bought) * 10000, CurMax64(Sold + Bought, CurrencyMinVolumeMmk));
	const int64 IdentBp = MarketDivTrunc(IdentityViewMilli, 10);
	const int64 TensionBp = MarketDivTrunc(TensionViewMilli, 10);
	const int64 AnchorBp = MarketDivTrunc((RateMcr - R0) * 10000, R0);
	const int64 Pressure = MarketDivTrunc(TradeBp * CurrencyTradeWeight + IdentBp * CurrencyIdentityWeight - TensionBp * CurrencyTensionWeight - AnchorBp * CurrencyAnchorWeight, 100);
	const int64 StepBp = CurClamp64(MarketDivTrunc(Pressure * CurrencyGainPct, 100), -CurrencyMaxStepBp, CurrencyMaxStepBp);
	int64 Delta = 0;
	if (StepBp != 0)
	{
		const int64 Mag = CurMax64(1, CurAbs64(RateMcr * StepBp) / 10000); // floor division of non-negatives; at least 1 mcr
		Delta = StepBp > 0 ? Mag : -Mag;
	}
	const int64 Before = RateMcr;
	RateMcr = CurClamp64(RateMcr + Delta, RateFloorMcr(), RateCeilMcr());
	LastRateStepMcr = RateMcr - Before;
	RateStepNote = 2;
}

// --- The save ---------------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::WriteCurrencyBlock(FArchive& Ar) const
{
	// v56. Inside E5's frame ('RHE5'), written straight after E4's. In order: the flags and sols (issued, issue sol, open sol, withdrawal flag
	// and its sol) and the rate; the record (identity, tension) and the pending journal; the bloc tally (8 sold, 8 bought, fill, head); the
	// purses (mark balance, earned, spent, reserved; the desk's and the withdrawals' lifetime credit); the members by name string; the mark
	// convoys by convoy Id (reserved and owed milli-marks); the pending desk orders by Id. Sized by history: a test measures it with
	// GetCurrencyBlockBytes(), never as a constant.
	uint8 Issued = bCurrencyIssued ? 1 : 0, Withdraw = bWithdrawPending ? 1 : 0;
	int32 IssueSol = CurrencyIssueSol, OpenSol = CurrencyOpenSol, WithdrawSol = WithdrawClearSol;
	int64 Rate = RateMcr;
	Ar << Issued << IssueSol << OpenSol << Withdraw << WithdrawSol << Rate;
	int64 IdentView = IdentityViewMilli, TensionView = TensionViewMilli;
	int32 JournalCount = Journal.Num();
	Ar << IdentView << TensionView << JournalCount;
	for (const FRHPoliticalEntry& E : Journal)
	{
		int32 ClearSol = E.ClearSol, CommandId = E.CommandId, Sub = E.Sub;
		double ExecuteAt = E.ExecuteAt;
		uint8 Kind = E.Kind, HasIdentity = E.bHasIdentity ? 1 : 0;
		int64 IdentityMilli = E.IdentityMilli, TensionDelta = E.TensionDeltaMilli;
		Ar << ClearSol << ExecuteAt << CommandId << Sub << Kind << HasIdentity << IdentityMilli << TensionDelta;
	}
	for (int32 e = 0; e < BlocRing; ++e) { int64 V = BlocSoldMmk[e]; Ar << V; }
	for (int32 e = 0; e < BlocRing; ++e) { int64 V = BlocBoughtMmk[e]; Ar << V; }
	int32 Fill = BlocFill, Head = BlocHead;
	Ar << Fill << Head;
	int64 Balance = MarkBalanceMmk, Earned = MarkEarnedMmk, Spent = MarkSpentMmk, Reserved = MarkReservedMmk, Desk = DeskCreditMcr, Redeemed = CurrencyRedeemedMcr;
	Ar << Balance << Earned << Spent << Reserved << Desk << Redeemed;
	int32 MemberCount = BlocMembers.Num();
	Ar << MemberCount;
	for (const FRHBlocMember& M : BlocMembers)
	{
		FString Name = M.Rival.ToString();
		int32 From = M.MemberFromSol;
		Ar << Name << From;
	}
	int32 MarkCount = 0;
	for (const FRHConvoy& C : Convoys) { MarkCount += C.bMarks ? 1 : 0; }
	Ar << MarkCount;
	for (const FRHConvoy& C : Convoys)
	{
		if (!C.bMarks)
		{
			continue;
		}
		int32 Id = C.Id;
		int64 Held = C.ReservedMmk, Owed = C.OwedMmk;
		Ar << Id << Held << Owed;
	}
	int32 DeskCount = DeskOrders.Num();
	Ar << DeskCount;
	for (const FRHDeskOrder& D : DeskOrders)
	{
		int32 Id = D.Id, ClearSol = D.ClearSol;
		uint8 Dir = D.Dir, Cleared = D.bCleared ? 1 : 0;
		int64 Amount = D.Amount;
		Ar << Id << Dir << Amount << ClearSol << Cleared;
	}
}

int32 URHSimWorldSubsystem::GetCurrencyBlockBytes() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, /*bIsPersistent*/ true);
	WriteCurrencyBlock(Ar);
	return Bytes.Num();
}

void URHSimWorldSubsystem::ReadCurrencyBlock(FArchive& Ar, const TArray<uint8>& File)
{
	// Read after teardown and after E4's block, so it repairs with a Warning and never refuses: the colony always loads, and no Error is ever
	// logged. It starts from the start values (unissued, the record seeded from the identity and tension just loaded); a lost or malformed frame
	// stays there with a Warning; a lost market block (any reason E4 started the market fresh) drops the frame too, with a Warning only when it
	// held something to lose (ruling 2). Otherwise the frame is applied with section 5's repairs; ReconcileReservations then sets both purses'
	// reservations. A mark order whose convoy E4's reader dropped is dropped silently (E4 has warned; cross-check item 2).
	ResetCurrencyState();
	bCurrencyBlockLost = false;
	bCurrencyParsed = false;
	ReadCurrencyReservedMmk = 0;
	for (FRHConvoy& C : Convoys)
	{
		C.bMarks = false;
		C.ReservedMmk = 0;
		C.OwedMmk = 0;
	}
	TArray<uint8> Payload;
	FString Why;
	if (!RHSaveFrame::Read(Ar, File, RHSaveFrame::CurrencyTag, Payload, Why))
	{
		bCurrencyBlockLost = true;
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the currency block is lost (%s); the mark starts unissued and the political record is read from today's identity and tension"), *Why);
		return;
	}
	struct FReadMark { int32 Id = 0; int64 Held = 0, Owed = 0; };
	struct FReadDesk { int32 Id = 0; uint8 Dir = 0, Cleared = 0; int64 Amount = 0; int32 ClearSol = 0; };
	struct FReadMember { FString Name; int32 From = 0; };
	FMemoryReader In(Payload);
	const auto Cut = [&]() { return In.IsError(); };
	uint8 Issued = 0, Withdraw = 0;
	int32 IssueSol = -1, OpenSol = -1, WithdrawSol = -1, JournalCount = 0;
	int64 Rate = 0, IdentView = 0, TensionView = 0;
	TArray<FRHPoliticalEntry> ReadJournal;
	int64 Sold[BlocRing] = {}, Bought[BlocRing] = {};
	int32 Fill = 0, Head = 0;
	int64 Balance = 0, Earned = 0, Spent = 0, Reserved = 0, Desk = 0, Redeemed = 0;
	TArray<FReadMember> ReadMembers;
	TArray<FReadMark> ReadMarks;
	TArray<FReadDesk> ReadDesks;
	In << Issued << IssueSol << OpenSol << Withdraw << WithdrawSol << Rate << IdentView << TensionView << JournalCount;
	bool bOk = !Cut() && JournalCount >= 0 && JournalCount <= CurMaxList;
	Why = bOk ? FString() : FString::Printf(TEXT("journal count %d"), JournalCount);
	for (int32 j = 0; bOk && j < JournalCount; ++j)
	{
		FRHPoliticalEntry E;
		uint8 HasIdentity = 0;
		In << E.ClearSol << E.ExecuteAt << E.CommandId << E.Sub << E.Kind << HasIdentity << E.IdentityMilli << E.TensionDeltaMilli;
		E.bHasIdentity = HasIdentity != 0;
		bOk = !Cut();
		// Read as written; the order and the values are judged below (an entry this reader cannot use resets the whole journal).
		if (HasIdentity > 1) { E.Kind = 255; }
		ReadJournal.Add(E);
	}
	for (int32 e = 0; bOk && e < BlocRing; ++e) { In << Sold[e]; }
	for (int32 e = 0; bOk && e < BlocRing; ++e) { In << Bought[e]; }
	if (bOk) { In << Fill << Head << Balance << Earned << Spent << Reserved << Desk << Redeemed; bOk = !Cut(); }
	int32 MemberCount = 0;
	if (bOk) { In << MemberCount; bOk = !Cut() && MemberCount >= 0 && MemberCount <= CurMaxList; if (!bOk) { Why = FString::Printf(TEXT("member count %d"), MemberCount); } }
	for (int32 m = 0; bOk && m < MemberCount; ++m)
	{
		FReadMember M;
		bOk = RHSaveFrame::ReadName(In, M.Name);
		if (bOk) { In << M.From; bOk = !Cut(); }
		ReadMembers.Add(MoveTemp(M));
	}
	int32 MarkCount = 0;
	if (bOk) { In << MarkCount; bOk = !Cut() && MarkCount >= 0 && MarkCount <= CurMaxList; if (!bOk) { Why = FString::Printf(TEXT("mark order count %d"), MarkCount); } }
	for (int32 k = 0; bOk && k < MarkCount; ++k)
	{
		FReadMark Mo;
		In << Mo.Id << Mo.Held << Mo.Owed;
		bOk = !Cut();
		ReadMarks.Add(Mo);
	}
	int32 DeskCount = 0;
	if (bOk) { In << DeskCount; bOk = !Cut() && DeskCount >= 0 && DeskCount <= CurMaxList; if (!bOk) { Why = FString::Printf(TEXT("desk order count %d"), DeskCount); } }
	for (int32 k = 0; bOk && k < DeskCount; ++k)
	{
		FReadDesk Rd;
		In << Rd.Id << Rd.Dir << Rd.Amount << Rd.ClearSol << Rd.Cleared;
		bOk = !Cut();
		ReadDesks.Add(Rd);
	}
	if (bOk && In.Tell() != In.TotalSize())
	{
		bOk = false;
		Why = FString::Printf(TEXT("%lld bytes are left over"), In.TotalSize() - In.Tell());
	}
	if (!bOk)
	{
		bCurrencyBlockLost = true;
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the currency block is malformed (%s); the mark starts unissued and the political record is read from today's identity and tension"),
			Why.IsEmpty() ? TEXT("the block is cut short") : *Why);
		return;
	}
	// E4's block started the market at its start values: the mark is lost with it (its prices and its convoys' currencies are gone). Silent
	// unless the frame held something to lose (ruling 2): an issue, a market issue, a withdrawal, a member, a purse or total, an order.
	if (bMarketBlockLost)
	{
		bCurrencyBlockLost = true;
		const bool bHeld = Issued != 0 || IssueSol >= 0 || Withdraw != 0 || ReadMembers.Num() > 0 || ReadMarks.Num() > 0 || ReadDesks.Num() > 0
			|| Balance != 0 || Earned != 0 || Spent != 0 || Reserved != 0 || Desk != 0 || Redeemed != 0;
		if (bHeld)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the market block was lost, so the mark is lost with it: it starts unissued, its purse and its bloc empty, and the political record is read from today's identity and tension"));
		}
		return;
	}
	bCurrencyParsed = true;
	// The flags and the sols: the open sol is the issue sol + 2, and both are -1 without an issue; a withdrawal names its sol.
	bCurrencyIssued = Issued != 0;
	bWithdrawPending = Withdraw != 0;
	if (IssueSol < -1 || (IssueSol >= 0 && OpenSol != IssueSol + 2) || (IssueSol < 0 && OpenSol != -1))
	{
		const int32 Fixed = IssueSol >= 0 ? IssueSol + 2 : -1;
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the market's issue sol %d and open sol %d disagree; set to %d and %d"), IssueSol, OpenSol, FMath::Max(-1, IssueSol), Fixed);
		IssueSol = FMath::Max(-1, IssueSol);
		OpenSol = Fixed;
	}
	CurrencyIssueSol = IssueSol;
	CurrencyOpenSol = OpenSol;
	if (bWithdrawPending != (WithdrawSol >= 0) || WithdrawSol < -1)
	{
		const int32 Fixed = bWithdrawPending ? FMath::Max(0, MarketCheckedSol) : -1;
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the withdrawal's sol %d does not fit its flag (%d); set to %d"), WithdrawSol, Withdraw, Fixed);
		WithdrawSol = Fixed;
	}
	WithdrawClearSol = WithdrawSol;
	// The rate, held in its band.
	if (Rate < RateFloorMcr() || Rate > RateCeilMcr())
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the rate %lld mcr a mark is outside [%lld, %lld]; clamped"), Rate, RateFloorMcr(), RateCeilMcr());
	}
	RateMcr = CurClamp64(Rate, RateFloorMcr(), RateCeilMcr());
	// The record, held in its range.
	if (IdentView < -CurViewMax || IdentView > CurViewMax || TensionView < 0 || TensionView > CurViewMax)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the political record (identity %lld, tension %lld) is outside its range; clamped"), IdentView, TensionView);
	}
	IdentityViewMilli = CurClamp64(IdentView, -CurViewMax, CurViewMax);
	TensionViewMilli = CurClamp64(TensionView, 0, CurViewMax);
	// The journal: strictly ascending by (ExecuteAt, CommandId, Sub), every entry one the record can apply - or it is emptied and the record is
	// read from today's identity and tension (ReconcileReservations then reconciles the flags with the lost entries, R7 O8).
	FString JournalBad;
	for (int32 j = 0; j < ReadJournal.Num() && JournalBad.IsEmpty(); ++j)
	{
		const FRHPoliticalEntry& E = ReadJournal[j];
		if (!FMath::IsFinite(E.ExecuteAt) || E.ExecuteAt < 0.0 || E.ClearSol < 0 || E.Sub < 0 || E.Sub > 1 || E.Kind > (uint8)ERHPoliticalAct::Withdraw
			|| CurAbs64(E.IdentityMilli) > CurViewMax || CurAbs64(E.TensionDeltaMilli) > CurViewMax)
		{
			JournalBad = FString::Printf(TEXT("entry %d is not an act the record can apply"), j);
		}
		else if (j > 0 && !CurEntryLess(ReadJournal[j - 1], E))
		{
			JournalBad = FString::Printf(TEXT("entry %d is out of order or repeated"), j);
		}
	}
	if (!JournalBad.IsEmpty())
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the political journal is unusable (%s); it is emptied and the record is read from today's identity and tension"), *JournalBad);
		IdentityViewMilli = CurClamp64(CurMilli(IdentityAxis), -CurViewMax, CurViewMax);
		TensionViewMilli = CurClamp64(CurMilli(EarthTension), 0, CurViewMax);
	}
	else
	{
		Journal = ReadJournal;
	}
	// The bloc tally: a ring index or an entry out of range resets it.
	bool bRingBad = Fill < 0 || Fill > BlocRing || Head < 0 || Head >= BlocRing;
	for (int32 e = 0; e < BlocRing; ++e) { bRingBad |= Sold[e] < 0 || Bought[e] < 0; }
	if (bRingBad)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the bloc tally has a ring index or an entry out of range; it is reset"));
	}
	else
	{
		for (int32 e = 0; e < BlocRing; ++e)
		{
			BlocSoldMmk[e] = Sold[e];
			BlocBoughtMmk[e] = Bought[e];
		}
		BlocFill = Fill;
		BlocHead = Head;
	}
	// The purses: a negative one is 0, then the balance is earned less spent (spent follows; earned only if spent would go negative).
	const auto NonNegative = [](const TCHAR* What, int64& V)
	{
		if (V < 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the %s is %lld; set to 0"), What, V);
			V = 0;
		}
	};
	NonNegative(TEXT("mark balance"), Balance);
	NonNegative(TEXT("marks earned"), Earned);
	NonNegative(TEXT("marks spent"), Spent);
	if (Balance != Earned - Spent)
	{
		const int64 FixedSpent = Earned - Balance;
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the mark balance %lld disagrees with earned %lld less spent %lld; spent set to %lld"), Balance, Earned, Spent, CurMax64(0, FixedSpent));
		Spent = FixedSpent;
		if (Spent < 0)
		{
			Earned = Balance;
			Spent = 0;
		}
	}
	MarkBalanceMmk = Balance;
	MarkEarnedMmk = Earned;
	MarkSpentMmk = Spent;
	ReadCurrencyReservedMmk = Reserved;
	DeskCreditMcr = Desk;          // both lifetime totals are clamped in ReconcileReservations, beside E4's
	CurrencyRedeemedMcr = Redeemed;
	// The members: ascending by name string, no repeat, from a sol >= 0, and only while the market is issued - or the list resets.
	bool bMembersOk = true;
	for (int32 m = 0; m < ReadMembers.Num(); ++m)
	{
		bMembersOk &= ReadMembers[m].From >= 0 && (m == 0 || ReadMembers[m].Name.Compare(ReadMembers[m - 1].Name, ESearchCase::CaseSensitive) > 0);
	}
	if (ReadMembers.Num() > 0 && (!bMembersOk || CurrencyIssueSol < 0))
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the bloc members are out of order, repeated or held without an issued market; the bloc is emptied"));
		ReadMembers.Reset();
	}
	for (const FReadMember& M : ReadMembers)
	{
		FRHBlocMember Member;
		Member.Rival = FName(*M.Name);
		Member.MemberFromSol = M.From;
		BlocMembers.Add(Member);
	}
	// The mark orders, onto the convoys E4's block loaded. One whose convoy E4 dropped goes silently; one naming a convoy E4 never held, or one
	// that disagrees with the convoy it names, goes with a Warning.
	int32 LastMark = 0;
	for (const FReadMark& Mo : ReadMarks)
	{
		FRHConvoy* C = Convoys.FindByPredicate([&Mo](const FRHConvoy& X) { return X.Id == Mo.Id; });
		if (!C && ReadMarketConvoyIds.Contains(Mo.Id))
		{
			continue;
		}
		FString Drop;
		if (Mo.Id <= LastMark) { Drop = TEXT("its Id is not above the one before"); }
		else if (!C) { Drop = TEXT("the market block holds no such convoy"); }
		else if (CurrencyIssueSol < 0) { Drop = TEXT("the market is not issued"); }
		else if (!C->IsPriced() || C->ReservedMcr != 0 || C->OwedMcr != 0) { Drop = TEXT("its convoy carries a credit order"); }
		else if (Mo.Held < 0 || Mo.Owed < 0) { Drop = TEXT("a negative amount of marks"); }
		else if (!C->bCleared && Mo.Owed != 0) { Drop = TEXT("an uncleared order that already owes marks"); }
		if (!Drop.IsEmpty())
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the mark order of convoy #%d is dropped (%s); the convoy settles in credits"), Mo.Id, *Drop);
			continue;
		}
		LastMark = Mo.Id;
		C->bMarks = true;
		C->OwedMmk = Mo.Owed;
		C->ReservedMmk = C->bCleared ? 0 : Mo.Held;
		if (C->bCleared && Mo.Held != 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the cleared convoy #%d still held %s marks; released"), Mo.Id, *FormatCredits(Mo.Held));
		}
	}
	// The desk orders: ascending by Id, a direction, a positive amount no larger than the desk takes in a sol (booking never sets aside more,
	// and the clearing's arithmetic is sized for it), not cleared, and only while the market is issued.
	int32 LastDesk = 0;
	for (const FReadDesk& Rd : ReadDesks)
	{
		FString Drop;
		if (Rd.Id <= LastDesk) { Drop = TEXT("its Id is not above the one before"); }
		else if (Rd.Dir > (uint8)ERHDeskDir::ToCredits) { Drop = TEXT("no such direction"); }
		else if (Rd.Amount <= 0) { Drop = TEXT("no amount"); }
		else if (Rd.Amount > DeskCapPerSol * 1000) { Drop = FString::Printf(TEXT("more than the desk takes in a sol, %lld"), DeskCapPerSol); }
		else if (Rd.ClearSol < 0 || Rd.Cleared != 0) { Drop = TEXT("a clearing sol below 0, or already cleared"); }
		else if (CurrencyIssueSol < 0) { Drop = TEXT("the market is not issued"); }
		if (!Drop.IsEmpty())
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the desk order #%d is dropped (%s); nothing it set aside is held"), Rd.Id, *Drop);
			continue;
		}
		LastDesk = Rd.Id;
		FRHDeskOrder D;
		D.Id = Rd.Id;
		D.Dir = Rd.Dir;
		D.Amount = Rd.Amount;
		D.ClearSol = Rd.ClearSol;
		DeskOrders.Add(D);
	}
	// NextCommandId is not saved (a load raises it past the queued orders only): raise it past every desk order and journal entry, so a new
	// order in a fresh process never takes a pending one's Id (trap 7). It saves no field and changes no line.
	for (const FRHDeskOrder& D : DeskOrders) { NextCommandId = FMath::Max(NextCommandId, D.Id + 1); }
	for (const FRHPoliticalEntry& E : Journal) { NextCommandId = FMath::Max(NextCommandId, E.CommandId + 1); }
}

void URHSimWorldSubsystem::ReconcileReservations()
{
	// R4: the one post-load step for both purses, after the currency block. E4's two repairs moved here from ReadMarketBlock with their texts,
	// conditions and order, so with no E5 state this is E4's behaviour byte for byte. In order: E5's lifetime totals (each in [0, Awarded -
	// SalesPaid], then the redeemed cut before the desk's if together they pass it), E4's market sales in what is left, the credit reservations
	// (the credit convoys' and the pending ToMarks amounts, cut from the desk orders by Id descending, then the convoys by Id descending, until
	// they fit the balance), the mark reservations the same way, then the flags (R7 O8). Silent when the state is consistent.
	const int64 Room = CurMax64(0, CreditAwardedMcr - SalesPaidMcr);
	const auto ClampTotal = [Room](const TCHAR* What, int64& V)
	{
		if (V < 0 || V > Room)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the lifetime %s of %lld mcr is outside [0, %lld]; clamped"), What, V, Room);
			V = CurClamp64(V, 0, Room);
		}
	};
	ClampTotal(TEXT("desk credit"), DeskCreditMcr);
	ClampTotal(TEXT("redeemed credit"), CurrencyRedeemedMcr);
	if (DeskCreditMcr + CurrencyRedeemedMcr > Room)
	{
		const int64 Over = DeskCreditMcr + CurrencyRedeemedMcr - Room;
		const int64 CutRedeemed = CurMin64(Over, CurrencyRedeemedMcr);
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the desk and redeemed credit pass the awarded total by %lld mcr; the redeemed credit is cut by %lld"), Over, CutRedeemed);
		CurrencyRedeemedMcr -= CutRedeemed;
		if (Over > CutRedeemed)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the desk credit is cut by %lld mcr"), Over - CutRedeemed);
			DeskCreditMcr -= Over - CutRedeemed;
		}
	}
	// E4's lifetime market sales (its text; nothing when its block did not parse, as at HEAD).
	if (bMarketParsed)
	{
		const int64 SalesMax = CurMax64(0, Room - DeskCreditMcr - CurrencyRedeemedMcr);
		if (ReadMarketSalesMcr < 0 || ReadMarketSalesMcr > SalesMax)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: lifetime market sales of %lld mcr are outside [0, %lld]; clamped"), ReadMarketSalesMcr, SalesMax);
		}
		MarketSalesMcr = CurClamp64(ReadMarketSalesMcr, 0, SalesMax);
	}
	// The credit reservations.
	int64 Sum = 0;
	for (const FRHConvoy& C : Convoys) { Sum += (C.IsPriced() && !C.bCleared && !C.bMarks) ? C.ReservedMcr : 0; }
	for (const FRHDeskOrder& D : DeskOrders) { Sum += D.Dir == (uint8)ERHDeskDir::ToMarks ? D.Amount : 0; }
	for (int32 k = DeskOrders.Num() - 1; k >= 0 && Sum > CreditBalanceMcr; --k)
	{
		FRHDeskOrder& D = DeskOrders[k];
		if (D.Dir == (uint8)ERHDeskDir::ToMarks)
		{
			const int64 CutBy = CurMin64(D.Amount, Sum - CreditBalanceMcr);
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the reservations (%s cr) pass the balance (%s cr); desk order #%d's is cut by %s cr"),
				*FormatCredits(Sum), *FormatCredits(CreditBalanceMcr), D.Id, *FormatCredits(CutBy));
			D.Amount -= CutBy;
			Sum -= CutBy;
		}
	}
	for (int32 c = Convoys.Num() - 1; c >= 0 && Sum > CreditBalanceMcr; --c)
	{
		FRHConvoy& C = Convoys[c];
		if (C.IsPriced() && !C.bCleared && !C.bMarks && C.ReservedMcr > 0)
		{
			const int64 CutBy = CurMin64(C.ReservedMcr, Sum - CreditBalanceMcr);
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the reservations (%s cr) pass the balance (%s cr); convoy #%d's is cut by %s cr"),
				*FormatCredits(Sum), *FormatCredits(CreditBalanceMcr), C.Id, *FormatCredits(CutBy));
			C.ReservedMcr -= CutBy;
			Sum -= CutBy;
		}
	}
	if (bMarketParsed && ReadMarketReservedMcr != Sum)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the reserved credit %lld mcr disagrees with the convoys' %lld mcr; set to the convoys'"), ReadMarketReservedMcr, Sum);
	}
	CreditReservedMcr = Sum;
	// The mark reservations: the mark convoys' and the pending ToCredits amounts, cut the same way against the mark balance.
	int64 SumMmk = 0;
	for (const FRHConvoy& C : Convoys) { SumMmk += (C.IsPriced() && !C.bCleared && C.bMarks) ? C.ReservedMmk : 0; }
	for (const FRHDeskOrder& D : DeskOrders) { SumMmk += D.Dir == (uint8)ERHDeskDir::ToCredits ? D.Amount : 0; }
	for (int32 k = DeskOrders.Num() - 1; k >= 0 && SumMmk > MarkBalanceMmk; --k)
	{
		FRHDeskOrder& D = DeskOrders[k];
		if (D.Dir == (uint8)ERHDeskDir::ToCredits)
		{
			const int64 CutBy = CurMin64(D.Amount, SumMmk - MarkBalanceMmk);
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the mark reservations (%s) pass the mark balance (%s); desk order #%d's is cut by %s marks"),
				*FormatCredits(SumMmk), *FormatCredits(MarkBalanceMmk), D.Id, *FormatCredits(CutBy));
			D.Amount -= CutBy;
			SumMmk -= CutBy;
		}
	}
	for (int32 c = Convoys.Num() - 1; c >= 0 && SumMmk > MarkBalanceMmk; --c)
	{
		FRHConvoy& C = Convoys[c];
		if (C.IsPriced() && !C.bCleared && C.bMarks && C.ReservedMmk > 0)
		{
			const int64 CutBy = CurMin64(C.ReservedMmk, SumMmk - MarkBalanceMmk);
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the mark reservations (%s) pass the mark balance (%s); convoy #%d's is cut by %s marks"),
				*FormatCredits(SumMmk), *FormatCredits(MarkBalanceMmk), C.Id, *FormatCredits(CutBy));
			C.ReservedMmk -= CutBy;
			SumMmk -= CutBy;
		}
	}
	if (bCurrencyParsed && ReadCurrencyReservedMmk != SumMmk)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the reserved marks %lld mmk disagree with the orders' %lld mmk; set to the orders'"), ReadCurrencyReservedMmk, SumMmk);
	}
	MarkReservedMmk = SumMmk;
	DeskOrders.RemoveAll([](const FRHDeskOrder& D) { return D.Amount <= 0; }); // a desk order cut to nothing is gone
	// The flags (R7 O8, re-review 5): Earth's flag with no market issue and no Issue entry pending is cleared; a market issue with the flag
	// down and no withdrawal pending raises it; a pending withdrawal with neither a market issue nor an Issue entry is cleared; and a file with
	// both flags up (play never writes one: the Comply that sets the withdrawal clears the issue) keeps the withdrawal, so the penalty, the
	// "already issued" refusal and the desk all end, as after the Comply.
	const bool bIssueEntry = Journal.ContainsByPredicate([](const FRHPoliticalEntry& E) { return E.Kind == (uint8)ERHPoliticalAct::Issue; });
	if (bCurrencyIssued && CurrencyIssueSol < 0 && !bIssueEntry)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the mark was issued with no issue in the market or the journal; it is not issued"));
		bCurrencyIssued = false;
	}
	if (CurrencyIssueSol >= 0 && !bCurrencyIssued && !bWithdrawPending)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the market counts in marks with the mark not issued and no withdrawal pending; it is issued"));
		bCurrencyIssued = true;
	}
	if (bWithdrawPending && CurrencyIssueSol < 0 && !bIssueEntry)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: a withdrawal is pending with no issue in the market or the journal; it is dropped"));
		bWithdrawPending = false;
		WithdrawClearSol = -1;
	}
	if (bWithdrawPending && bCurrencyIssued)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v56: the mark is both issued and being withdrawn; the withdrawal stands and the mark is not issued"));
		bCurrencyIssued = false;
	}
}
