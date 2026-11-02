#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "RHSaveFrame.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// E4, the Martian market (docs/qa/2026-10-24/e4-martian-market-design.md: DIRECTOR ANSWER Q-A (a), Q-B (a), Q-C (a) > PROFESSOR RE-REVIEW
// conditions 2-6 > REVISION R1-R11 > PROFESSOR REVIEW > design). Prices come from E4's OWN per-sol tally of what the colony sold and bought
// plus the rivals' data - never E1's ledger - so a normal play and a fast-forward price identically. Everything the market writes (prices,
// rival stores, the tally, fills, charges) is written inside RollMarket, which runs on the same threshold step in both bands; the orders
// enter it only by their ExecuteAt sol. Integer maths only. Rivals are ordered by name string, never by FName index.

namespace
{
	const FName NAME_MktHydrogen(TEXT("Hydrogen"));
	const FName NAME_MktSpareParts(TEXT("SpareParts"));
	constexpr int64 MktMaxRateKg = 1000000;     // Produce / Consume / a standing lot, kg
	constexpr int64 MktMaxStoreKg = 10000000;   // StoreCap, kg
	constexpr int32 MktMaxLines = 64;
	constexpr int32 MktMaxList = 4096;
	constexpr int32 MktMaxClearingsKept = 64;

	bool MktNameLess(const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; }
	int64 MktClamp64(int64 V, int64 Lo, int64 Hi) { return V < Lo ? Lo : (V > Hi ? Hi : V); }
	int64 MktAbs64(int64 V) { return V < 0 ? -V : V; }
	int64 MktMax64(int64 A, int64 B) { return A > B ? A : B; }
	int64 MktMin64(int64 A, int64 B) { return A < B ? A : B; }

	// One price step's worth of room either side of a quote (3.2 step 5): one full step up and the 1 mcr minimum, one full step down less 1.
	int64 MktHiQuote(int64 Q, int64 MaxStepBp) { return Q * (10000 + MaxStepBp) / 10000 + 1; }
	int64 MktLoQuote(int64 Q, int64 MaxStepBp) { return MktMax64(0, Q * (10000 - MaxStepBp) / 10000 - 1); }
}

int64 URHSimWorldSubsystem::MarketDivTrunc(int64 A, int64 B)
{
	// The update's one signed division. Written from the magnitudes so it never depends on how a compiler rounds a negative quotient.
	return A >= 0 ? A / B : -((-A) / B);
}

int64 URHSimWorldSubsystem::MarketTariffMcr(int64 Value, int32 MarkupBp)
{
	// E6 (D21): ceil(value x m / 10000) of non-negatives, rounded against the colony. value x m stays inside int64: a line is at most the cargo
	// cap - PactCargoKg, clamped to 10,000 kg (E4's ConvoyCargoKg has no upper clamp of its own) - at 4 V (V <= 900,000 mcr/kg), times at most
	// 9,999 bp: about 3.6 x 10^14 at 10,000 kg.
	return (Value <= 0 || MarkupBp <= 0) ? 0 : (Value * MarkupBp + 9999) / 10000;
}

int64 URHSimWorldSubsystem::MarketLineSettleMcr(int64 Value, int32 MarkupBp, bool bBuy)
{
	// What the colony pays for a buy, or receives for a sell. A sell's receipt is never above its value, and with m < 10000 never below 0.
	const int64 Tariff = MarketTariffMcr(Value, MarkupBp);
	return bBuy ? Value + Tariff : Value - Tariff;
}

// --- Money: the one debit writer, and the reservation --------------------------------------------------------------------------------------

int64 URHSimWorldSubsystem::CreditSpend(int64 Mcr)
{
	// E2's invariant keeps one writer on each side: CreditPay writes the income, this writes the spend. It pays min(Mcr, Balance), so the
	// balance never goes below 0 (E2's CR7), and returns what it could not pay.
	if (Mcr <= 0)
	{
		return 0;
	}
	const int64 Pay = Mcr < CreditBalanceMcr ? Mcr : CreditBalanceMcr;
	CreditBalanceMcr -= Pay;
	CreditSpentMcr += Pay;
	return Mcr - Pay;
}

bool URHSimWorldSubsystem::Debug_ZeroCredits()
{
	if (CreditReservedMcr != 0)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("Debug_ZeroCredits refused: %s cr is reserved for convoys still waiting to clear"), *FormatCredits(CreditReservedMcr));
		return false;
	}
	if (MarkReservedMmk != 0)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("Debug_ZeroCredits refused: %s marks are reserved for orders still waiting to clear"), *FormatCredits(MarkReservedMmk));
		return false;
	}
	CreditBalanceMcr = 0;
	CreditAwardedMcr = 0;
	CreditSpentMcr = 0;
	SalesPaidMcr = 0;
	MarketSalesMcr = 0; // E4 (re-review 5a): SalesPaid + MarketSales <= Awarded must keep holding
	DeskCreditMcr = 0;  // E5 (R4): and so must the desk's and the withdrawals' parts of it
	CurrencyRedeemedMcr = 0;
	return true;
}

// --- The rows and the cache --------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::InitMarket()
{
	if (!Defs)
	{
		return;
	}
	const auto Cfg = [this](const TCHAR* Name, double Fallback) { return Defs->GetConfigScalar(FName(Name), Fallback); };
	MarketWindowSols = FMath::Clamp(FMath::RoundToInt(Cfg(TEXT("MarketWindowSols"), MarketWindowSols)), 1, MarketRing);
	MarketMaxStepBp = FMath::Clamp(FMath::RoundToInt(Cfg(TEXT("MarketMaxStepBp"), MarketMaxStepBp)), 1, 10000);
	MarketGainPct = FMath::Max(0, FMath::RoundToInt(Cfg(TEXT("MarketGainPct"), MarketGainPct)));
	MarketFlowWeight = FMath::Max(0, FMath::RoundToInt(Cfg(TEXT("MarketFlowWeight"), MarketFlowWeight)));
	MarketStoreWeight = FMath::Max(0, FMath::RoundToInt(Cfg(TEXT("MarketStoreWeight"), MarketStoreWeight)));
	MarketAnchorWeight = FMath::Max(0, FMath::RoundToInt(Cfg(TEXT("MarketAnchorWeight"), MarketAnchorWeight)));
	MarketTargetFillBp = FMath::Clamp(FMath::RoundToInt(Cfg(TEXT("MarketTargetFillBp"), MarketTargetFillBp)), 0, 10000);
	MarketFloorBp = FMath::Clamp(FMath::RoundToInt(Cfg(TEXT("MarketFloorBp"), MarketFloorBp)), 1, 9999);
	MarketCeilBp = FMath::Clamp(FMath::RoundToInt(Cfg(TEXT("MarketCeilBp"), MarketCeilBp)), 10001, 1000000);
	MarketMinVolumeG = FMath::Max<int64>(1, FMath::RoundToInt64(Cfg(TEXT("MarketMinVolumeG"), (double)MarketMinVolumeG)));
	bMarketRivalOnly = Cfg(TEXT("MarketRivalOnly"), bMarketRivalOnly ? 1.0 : 0.0) >= 0.5;
	MaxConvoysOut = FMath::Max(1, FMath::RoundToInt(Cfg(TEXT("MaxConvoysOut"), MaxConvoysOut)));
	ConvoyCargoKg = FMath::Max(1, FMath::RoundToInt(Cfg(TEXT("ConvoyCargoKg"), ConvoyCargoKg)));
	GoodwillMinValueMcr = FMath::Max<int64>(0, FMath::RoundToInt64(Cfg(TEXT("GoodwillMinValueMcr"), (double)GoodwillMinValueMcr)));
	MarketCheckedSol = ProductionCheckedSol; // never 0 against a loaded clock (E1 seeds EconCheckedSol the same way)
	SyncRivalEconomies(/*bWarn*/ true, /*bForce*/ true);
}

bool URHSimWorldSubsystem::IsRivalSyncStale() const
{
	return Defs && Defs->GetRivalsGeneration() != SyncedRivalsGeneration;
}

URHSimWorldSubsystem::ERHRivalTrade URHSimWorldSubsystem::EvaluateRivalRow(const FRHRivalRow& Row, FRHRivalEconomy* OutEcon, FString* OutWhy) const
{
	// "Has an economy" is a property of the row (R1): StoreCap non-empty; every list strictly parsed; every key an E1 slot with an Earth value
	// above 0 (R2: no division by zero); Produce, Consume and StoreStart naming only StoreCap's keys; the bounds of R2; and both standing lots
	// naming only StoreCap's keys, in whole positive kg, with no resource on both sides. A row that fails only the last test is legal and
	// silent (it trades by barter, as Greed does); a row that fails any other test is malformed.
	if (Row.StoreCap.TrimStartAndEnd().IsEmpty())
	{
		return ERHRivalTrade::NoEconomy;
	}
	const auto Malformed = [OutWhy](const FString& Why)
	{
		if (OutWhy) { *OutWhy = Why; }
		return ERHRivalTrade::Malformed;
	};
	TArray<TPair<FName, int64>> Produce, Consume, Start, Cap;
	FString Why;
	if (!URHDefinitionsSubsystem::ParseResourceListStrict(Row.Produce, Produce, Why)) { return Malformed(FString::Printf(TEXT("Produce: %s"), *Why)); }
	if (!URHDefinitionsSubsystem::ParseResourceListStrict(Row.Consume, Consume, Why)) { return Malformed(FString::Printf(TEXT("Consume: %s"), *Why)); }
	if (!URHDefinitionsSubsystem::ParseResourceListStrict(Row.StoreStart, Start, Why)) { return Malformed(FString::Printf(TEXT("StoreStart: %s"), *Why)); }
	if (!URHDefinitionsSubsystem::ParseResourceListStrict(Row.StoreCap, Cap, Why)) { return Malformed(FString::Printf(TEXT("StoreCap: %s"), *Why)); }
	const int32 N = EconSlots.Num();
	TMap<FName, int64> CapMap;
	for (const TPair<FName, int64>& C : Cap)
	{
		const int32* SlotPtr = EconIndex.Find(C.Key);
		if (!SlotPtr)
		{
			return Malformed(FString::Printf(TEXT("StoreCap names %s, which is no resource slot"), *C.Key.ToString()));
		}
		const FRHResourceRow* Res = Defs ? Defs->GetResource(C.Key) : nullptr;
		if (!Res || Res->EarthValue_mcr <= 0)
		{
			return Malformed(FString::Printf(TEXT("StoreCap names %s, which has no Earth value above 0"), *C.Key.ToString()));
		}
		if (C.Value > MktMaxStoreKg)
		{
			return Malformed(FString::Printf(TEXT("StoreCap of %s is %lld kg (at most %lld)"), *C.Key.ToString(), C.Value, MktMaxStoreKg));
		}
		CapMap.Add(C.Key, C.Value);
	}
	const auto CheckList = [&](const TArray<TPair<FName, int64>>& List, const TCHAR* Column, int64 MaxKg, bool bAgainstCap) -> bool
	{
		for (const TPair<FName, int64>& L : List)
		{
			const int64* CapKg = CapMap.Find(L.Key);
			if (!CapKg)
			{
				Why = FString::Printf(TEXT("%s names %s, which StoreCap does not list"), Column, *L.Key.ToString());
				return false;
			}
			if (L.Value > MaxKg || (bAgainstCap && L.Value > *CapKg))
			{
				Why = FString::Printf(TEXT("%s of %s is %lld kg (%s)"), Column, *L.Key.ToString(), L.Value, bAgainstCap ? TEXT("above its StoreCap") : TEXT("over the bound"));
				return false;
			}
		}
		return true;
	};
	if (!CheckList(Produce, TEXT("Produce"), MktMaxRateKg, false) || !CheckList(Consume, TEXT("Consume"), MktMaxRateKg, false) || !CheckList(Start, TEXT("StoreStart"), MktMaxStoreKg, true))
	{
		return Malformed(Why);
	}
	// The lots test: the legal reason to trade by barter.
	TSet<FName> ImportKeys;
	bool bLotsInside = true;
	FString LotsWhy;
	const auto LotOk = [&](const FString& Text, TSet<FName>* Keys) -> bool
	{
		for (const TPair<FName, double>& L : URHDefinitionsSubsystem::ParseResourceList(Text))
		{
			if (!CapMap.Contains(L.Key))
			{
				LotsWhy = FString::Printf(TEXT("a standing lot names %s, which StoreCap does not list"), *L.Key.ToString());
				return false;
			}
			if (!(L.Value >= 1.0) || L.Value > (double)MktMaxRateKg || L.Value != FMath::FloorToDouble(L.Value))
			{
				LotsWhy = FString::Printf(TEXT("a standing lot of %s is not a whole number of kg"), *L.Key.ToString());
				return false;
			}
			if (Keys) { Keys->Add(L.Key); }
		}
		return true;
	};
	bLotsInside = LotOk(Row.ImportLot, &ImportKeys);
	TSet<FName> ExportKeys;
	bLotsInside = bLotsInside && LotOk(Row.ExportLot, &ExportKeys);
	if (bLotsInside && ImportKeys.Intersect(ExportKeys).Num() > 0)
	{
		bLotsInside = false;
		LotsWhy = TEXT("a resource is on both sides of the standing order");
	}
	if (!bLotsInside)
	{
		if (OutWhy) { *OutWhy = LotsWhy; }
		return ERHRivalTrade::LotsOutsideCap;
	}
	if (OutEcon)
	{
		OutEcon->RegionId = Row.RegionId;
		OutEcon->bHasEconomy = true;
		OutEcon->ProduceG.Init(0, N);
		OutEcon->ConsumeG.Init(0, N);
		OutEcon->CapG.Init(0, N);
		OutEcon->StoreG.Init(0, N);
		OutEcon->Trades.Init(0, N);
		for (const TPair<FName, int64>& C : Cap) { const int32 SlotIndex = EconIndex.FindChecked(C.Key); OutEcon->Trades[SlotIndex] = 1; OutEcon->CapG[SlotIndex] = C.Value * 1000; }
		// E7 (Q-B (b)): the region's endowment, a permille on production (1000 = the row as written), exact in integers with no division.
		for (const TPair<FName, int64>& P : Produce) { OutEcon->ProduceG[EconIndex.FindChecked(P.Key)] = P.Value * (int64)Defs->GetProducePermille(Row.RegionId, P.Key); }
		for (const TPair<FName, int64>& P : Consume) { OutEcon->ConsumeG[EconIndex.FindChecked(P.Key)] = P.Value * 1000; }
		for (const TPair<FName, int64>& P : Start) { OutEcon->StoreG[EconIndex.FindChecked(P.Key)] = P.Value * 1000; }
	}
	return ERHRivalTrade::Economy;
}

void URHSimWorldSubsystem::BuildMarketState(const TArray<FRHRivalEconomy>& OldEcon, const TArray<FRHMarket>& OldMarkets, TArray<FRHRivalEconomy>& OutEcon, TArray<FRHMarket>& OutMarkets, bool bWarn) const
{
	OutEcon.Reset();
	OutMarkets.Reset();
	if (!Defs)
	{
		return;
	}
	const int32 N = EconSlots.Num();
	// Every row, in name-string order (a case-sensitive ordinal compare): never the table's own order, never FName index order.
	TArray<TPair<FName, const FRHRivalRow*>> Rows;
	Defs->ForEachRivalRow([&Rows](FName Name, const FRHRivalRow& Row) { Rows.Emplace(Name, &Row); });
	Rows.Sort([](const TPair<FName, const FRHRivalRow*>& A, const TPair<FName, const FRHRivalRow*>& B) { return MktNameLess(A.Key, B.Key); });
	for (const TPair<FName, const FRHRivalRow*>& R : Rows)
	{
		FRHRivalEconomy Fresh;
		FString Why;
		const ERHRivalTrade Kind = EvaluateRivalRow(*R.Value, &Fresh, &Why);
		if (Kind == ERHRivalTrade::Malformed && bWarn)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Rival %s: its economy columns are malformed (%s); it trades by barter"), *R.Key.ToString(), *Why);
		}
		if (Kind != ERHRivalTrade::Economy)
		{
			continue;
		}
		Fresh.Rival = R.Key;
		// An existing entry keeps its store; a rival with no entry (an injected rival, or a data rival a loaded block did not hold) starts at StoreStart.
		const FRHRivalEconomy* Old = OldEcon.FindByPredicate([&R](const FRHRivalEconomy& E) { return E.Rival == R.Key; });
		if (Old && Old->StoreG.Num() == N)
		{
			for (int32 s = 0; s < N; ++s) { Fresh.StoreG[s] = Fresh.Trades[s] ? Old->StoreG[s] : 0; }
		}
		OutEcon.Add(MoveTemp(Fresh));
	}
	// An entry whose row lost its economy stops producing and leaves the sums; it is kept only while an uncleared priced convoy names it.
	for (const FRHRivalEconomy& Old : OldEcon)
	{
		if (OutEcon.ContainsByPredicate([&Old](const FRHRivalEconomy& E) { return E.Rival == Old.Rival; }))
		{
			continue;
		}
		const bool bNamed = Convoys.ContainsByPredicate([&Old](const FRHConvoy& C) { return C.Rival == Old.Rival && C.IsPriced() && !C.bCleared; });
		if (bNamed && Old.StoreG.Num() == N)
		{
			FRHRivalEconomy Keep = Old;
			Keep.bHasEconomy = false;
			Keep.ProduceG.Init(0, N);
			Keep.ConsumeG.Init(0, N);
			OutEcon.Add(MoveTemp(Keep));
		}
	}
	OutEcon.Sort([](const FRHRivalEconomy& A, const FRHRivalEconomy& B) { return MktNameLess(A.Rival, B.Rival); });
	// One market per region with an economy rival, by RegionId.
	TArray<int32> Regions;
	for (const FRHRivalEconomy& E : OutEcon)
	{
		if (E.bHasEconomy) { Regions.AddUnique(E.RegionId); }
	}
	Regions.Sort();
	for (const int32 Region : Regions)
	{
		FRHMarket Mk;
		Mk.RegionId = Region;
		const FRHMarket* OldMk = OldMarkets.FindByPredicate([Region](const FRHMarket& X) { return X.RegionId == Region; });
		const bool bKeep = OldMk && OldMk->PriceMcr.Num() == N && OldMk->RingSoldG.Num() == N * MarketRing && OldMk->RingBoughtG.Num() == N * MarketRing;
		// E8 (R3-1): the history is kept under its own test, after E4's (which it never joins): a market E4 builds at a load holds prices and a
		// tally but no ring yet, so it keeps its prices here and starts an empty ring that the history's reader then fills.
		const bool bKeepHist = bKeep && OldMk->HistPrice.Num() == N * MarketHistSols && OldMk->HistSoldValue.Num() == MarketHistSols
			&& OldMk->HistBoughtValue.Num() == MarketHistSols && OldMk->HistRateMcr.Num() == MarketHistSols && OldMk->HistUnit.Num() == MarketHistSols;
		Mk.VMcr.Init(0, N);
		Mk.FloorMcr.Init(0, N);
		Mk.CeilMcr.Init(0, N);
		Mk.bTraded.Init(0, N);
		Mk.ProdSumG.Init(0, N);
		Mk.ConsSumG.Init(0, N);
		Mk.CapSumG.Init(0, N);
		for (int32 s = 0; s < N; ++s)
		{
			const FRHResourceRow* Res = Defs->GetResource(EconSlots[s].Name);
			const int64 V = Res ? (int64)FMath::Max(0, Res->EarthValue_mcr) : 0;
			Mk.VMcr[s] = V;
			Mk.FloorMcr[s] = V * MarketFloorBp / 10000;
			Mk.CeilMcr[s] = V * MarketCeilBp / 10000;
		}
		if (bKeep)
		{
			Mk.PriceMcr = OldMk->PriceMcr;
			Mk.RingSoldG = OldMk->RingSoldG;
			Mk.RingBoughtG = OldMk->RingBoughtG;
			Mk.RingFill = OldMk->RingFill;
			Mk.RingHead = OldMk->RingHead;
			if (bKeepHist)
			{
				Mk.HistPrice = OldMk->HistPrice;
				Mk.HistSoldValue = OldMk->HistSoldValue;
				Mk.HistBoughtValue = OldMk->HistBoughtValue;
				Mk.HistRateMcr = OldMk->HistRateMcr;
				Mk.HistUnit = OldMk->HistUnit;
				Mk.HistFill = OldMk->HistFill;
				Mk.HistHead = OldMk->HistHead;
				Mk.HistLastSol = OldMk->HistLastSol;
			}
			else
			{
				HistInitEmpty(Mk, N);
			}
		}
		else
		{
			Mk.PriceMcr = Mk.VMcr; // a market starts at the Earth value
			Mk.RingSoldG.Init(0, N * MarketRing);
			Mk.RingBoughtG.Init(0, N * MarketRing);
			HistInitEmpty(Mk, N);
		}
		for (const FRHRivalEconomy& E : OutEcon)
		{
			if (!E.bHasEconomy || E.RegionId != Region)
			{
				continue;
			}
			for (int32 s = 0; s < N; ++s)
			{
				if (E.Trades[s])
				{
					Mk.bTraded[s] = 1;
					Mk.ProdSumG[s] += E.ProduceG[s];
					Mk.ConsSumG[s] += E.ConsumeG[s];
					Mk.CapSumG[s] += E.CapG[s];
				}
			}
		}
		OutMarkets.Add(MoveTemp(Mk));
	}
}

void URHSimWorldSubsystem::SyncRivalEconomies(bool bWarn, bool bForce) const
{
	if (!Defs || (!bForce && !IsRivalSyncStale()))
	{
		return;
	}
	TArray<FRHRivalEconomy> NewEcon;
	TArray<FRHMarket> NewMarkets;
	BuildMarketState(RivalEconomies, Markets, NewEcon, NewMarkets, bWarn);
	RivalEconomies = MoveTemp(NewEcon);
	Markets = MoveTemp(NewMarkets);
	SyncedRivalsGeneration = Defs->GetRivalsGeneration();
}

URHSimWorldSubsystem::FRHRivalEconomy* URHSimWorldSubsystem::FindRivalEconomy(FName Rival) const
{
	SyncRivalEconomies(false);
	return RivalEconomies.FindByPredicate([Rival](const FRHRivalEconomy& E) { return E.Rival == Rival; });
}

URHSimWorldSubsystem::FRHMarket* URHSimWorldSubsystem::FindMarket(int32 RegionId) const
{
	SyncRivalEconomies(false);
	return Markets.FindByPredicate([RegionId](const FRHMarket& X) { return X.RegionId == RegionId; });
}

int32 URHSimWorldSubsystem::FindConvoyIndexFor(FName Rival) const
{
	return Convoys.IndexOfByPredicate([Rival](const FRHConvoy& C) { return C.Rival == Rival; });
}

URHSimWorldSubsystem::ERHRivalTrade URHSimWorldSubsystem::GetRivalTradeKind(FName Rival, FString* OutWhy) const
{
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	if (!Row)
	{
		return ERHRivalTrade::NoEconomy;
	}
	if (const FRHRivalEconomy* E = FindRivalEconomy(Rival))
	{
		if (E->bHasEconomy)
		{
			return ERHRivalTrade::Economy;
		}
	}
	return EvaluateRivalRow(*Row, nullptr, OutWhy);
}

bool URHSimWorldSubsystem::IsMarketLive() const
{
	if (!bMarketEnabled)
	{
		return false;
	}
	SyncRivalEconomies(false);
	return RivalEconomies.ContainsByPredicate([](const FRHRivalEconomy& E) { return E.bHasEconomy; });
}

// --- Reads ----------------------------------------------------------------------------------------------------------------------------------

bool URHSimWorldSubsystem::IsMarketTraded(FName Resource, int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	return Mk && SlotPtr && Mk->bTraded.IsValidIndex(*SlotPtr) && Mk->bTraded[*SlotPtr] != 0;
}

int64 URHSimWorldSubsystem::GetMarketPriceMcr(FName Resource, int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	return (Mk && SlotPtr && Mk->PriceMcr.IsValidIndex(*SlotPtr)) ? Mk->PriceMcr[*SlotPtr] : 0;
}

int64 URHSimWorldSubsystem::GetRivalStoreG(FName Rival, FName Resource) const
{
	const FRHRivalEconomy* E = FindRivalEconomy(Rival);
	const int32* SlotPtr = EconIndex.Find(Resource);
	return (E && SlotPtr && E->StoreG.IsValidIndex(*SlotPtr)) ? E->StoreG[*SlotPtr] : -1;
}

int32 URHSimWorldSubsystem::GetRivalStoreKg(FName Rival, FName Resource) const
{
	const int64 G = GetRivalStoreG(Rival, Resource);
	return G < 0 ? -1 : (int32)FMath::Min<int64>(G / 1000, MAX_int32);
}

bool URHSimWorldSubsystem::GetMarketTally(FName Resource, int32 Back, int64& OutSoldG, int64& OutBoughtG, int32 RegionId) const
{
	OutSoldG = 0;
	OutBoughtG = 0;
	const FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	if (!Mk || !SlotPtr || Back < 1 || Back > Mk->RingFill)
	{
		return false;
	}
	const int32 Entry = (Mk->RingHead - Back + 2 * MarketRing) % MarketRing;
	OutSoldG = Mk->RingSoldG[*SlotPtr * MarketRing + Entry];
	OutBoughtG = Mk->RingBoughtG[*SlotPtr * MarketRing + Entry];
	return true;
}

void URHSimWorldSubsystem::GetRivalTradedResources(FName Rival, TArray<FName>& Out) const
{
	Out.Reset();
	const FRHRivalEconomy* E = FindRivalEconomy(Rival);
	for (int32 s = 0; E && s < E->Trades.Num() && s < EconSlots.Num(); ++s)
	{
		if (E->Trades[s]) { Out.Add(EconSlots[s].Name); }
	}
}

int32 URHSimWorldSubsystem::GetRivalEconomyCount() const
{
	SyncRivalEconomies(false);
	int32 Count = 0;
	for (const FRHRivalEconomy& E : RivalEconomies) { Count += E.bHasEconomy ? 1 : 0; }
	return Count;
}

FName URHSimWorldSubsystem::GetRivalEconomyName(int32 Index) const
{
	SyncRivalEconomies(false);
	int32 At = 0;
	for (const FRHRivalEconomy& E : RivalEconomies)
	{
		if (E.bHasEconomy && At++ == Index) { return E.Rival; }
	}
	return NAME_None;
}

// E7: the band and the sums, as the sync derived them (each read syncs first, so a read after an injection or an endowment hook is fresh).
int64 URHSimWorldSubsystem::GetMarketAnchorMcr(FName Resource, int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	return (Mk && SlotPtr && Mk->VMcr.IsValidIndex(*SlotPtr)) ? Mk->VMcr[*SlotPtr] : 0;
}

int64 URHSimWorldSubsystem::GetMarketFloorMcr(FName Resource, int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	return (Mk && SlotPtr && Mk->FloorMcr.IsValidIndex(*SlotPtr)) ? Mk->FloorMcr[*SlotPtr] : 0;
}

int64 URHSimWorldSubsystem::GetMarketCeilMcr(FName Resource, int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	return (Mk && SlotPtr && Mk->CeilMcr.IsValidIndex(*SlotPtr)) ? Mk->CeilMcr[*SlotPtr] : 0;
}

int32 URHSimWorldSubsystem::GetMarketRegionIdAt(int32 Index) const
{
	SyncRivalEconomies(false);
	return Markets.IsValidIndex(Index) ? Markets[Index].RegionId : -1;
}

int64 URHSimWorldSubsystem::GetRivalProduceG(FName Rival, FName Resource) const
{
	const FRHRivalEconomy* E = FindRivalEconomy(Rival);
	const int32* SlotPtr = EconIndex.Find(Resource);
	return (E && SlotPtr && E->ProduceG.IsValidIndex(*SlotPtr)) ? E->ProduceG[*SlotPtr] : -1;
}

int64 URHSimWorldSubsystem::GetMarketProdSumG(FName Resource, int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	return (Mk && SlotPtr && Mk->ProdSumG.IsValidIndex(*SlotPtr)) ? Mk->ProdSumG[*SlotPtr] : 0;
}

// --- Harness hooks -------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::Debug_SetRivalStore(FName Rival, FName Resource, int64 Kg)
{
	FRHRivalEconomy* E = FindRivalEconomy(Rival);
	const int32* SlotPtr = EconIndex.Find(Resource);
	if (E && SlotPtr && E->StoreG.IsValidIndex(*SlotPtr) && E->Trades.IsValidIndex(*SlotPtr) && E->Trades[*SlotPtr])
	{
		E->StoreG[*SlotPtr] = FMath::Max<int64>(0, Kg) * 1000;
	}
}

void URHSimWorldSubsystem::Debug_SetMarketPrice(FName Resource, int64 Mcr, int32 RegionId)
{
	FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	if (Mk && SlotPtr && Mk->PriceMcr.IsValidIndex(*SlotPtr))
	{
		Mk->PriceMcr[*SlotPtr] = MktClamp64(Mcr, Mk->FloorMcr[*SlotPtr], Mk->CeilMcr[*SlotPtr]);
		// E8 (R3-3): the open sol's history entry follows the price, so every file the harness saves keeps "the open sol's entry is the live
		// price" and its load warns nothing.
		if (Mk->HistFill > 0 && Mk->HistPrice.Num() == Mk->PriceMcr.Num() * MarketHistSols)
		{
			Mk->HistPrice[*SlotPtr * MarketHistSols + (Mk->HistHead - 1 + MarketHistSols) % MarketHistSols] = Mk->PriceMcr[*SlotPtr];
		}
	}
}

void URHSimWorldSubsystem::Debug_SetMarketTally(FName Resource, int64 SoldKgPerSol, int64 BoughtKgPerSol, int32 RegionId)
{
	FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	if (!Mk || !SlotPtr)
	{
		return;
	}
	for (int32 e = 0; e < MarketRing; ++e)
	{
		Mk->RingSoldG[*SlotPtr * MarketRing + e] = FMath::Max<int64>(0, SoldKgPerSol) * 1000;
		Mk->RingBoughtG[*SlotPtr * MarketRing + e] = FMath::Max<int64>(0, BoughtKgPerSol) * 1000;
	}
	Mk->RingFill = MarketRing;
}

// --- The roll: the clearing, the rivals' sol, the repricing ------------------------------------------------------------------------------

void URHSimWorldSubsystem::RollMarket(int32 SolNow)
{
	// A threshold on the monotone clock, called straight after RollEconomyLedger with the same SolNow, so both bands roll on the same step.
	// Once for each sol the threshold closes (a long jump closes several, at most MarketRing of them are repriced): 1 the clearing of every
	// booked order due at or before the closing sol, by convoy Id, at the prices in force during that sol, its fills written into that
	// sol's ring entry, then E5's desk orders, by Id, at the rate in force; 2 the rivals produce and consume; 3 every traded slot reprices;
	// 4 E5's steps, outside the live gate (the record runs from sol 0 in every colony): the political record, the issue and the withdrawal,
	// the bloc's admissions, the rate. The market off still clears and keeps the tally; it skips only 2 and 3 (R4), so a priced convoy out
	// when the harness turns the market off is never stranded. E6, in the cross-check's order: each clearing charges its markups (read at the
	// order's stored EffSol); after the ring advance every pending theft or offering due by the pass lands in its rival's store, outside the
	// live gate; production reads the sabotage windows covering the closing sol; and after the last pass the rules whose windows are behind
	// the open sol retire, last of all. E8 writes its history at three points of each pass, none of which any step reads: the closing sol's
	// entry at the top (a no-op unless the ring is empty or lags), the clearings' values into it, and after E5's steps the new open sol's
	// entry (the sols a long jump skipped go in as unknown first).
	if (SolNow <= MarketCheckedSol)
	{
		return;
	}
	SyncRivalEconomies(false);
	const int32 First = MarketCheckedSol;
	const int32 Passed = FMath::Min(SolNow - First, MarketRing);
	MarketCheckedSol = SolNow;
	const bool bLive = IsMarketLive();
	const int32 N = EconSlots.Num();
	// E6: one walk of the trade rules serves the whole call, since nothing in a roll changes a rule before the retirement at its end: the
	// sabotage windows (each pass cuts the rivals whose window covers its closing sol) and whether any rule retires at the new open sol.
	TArray<TPair<FName, FIntPoint>, TInlineAllocator<8>> CutWindows;
	bool bRetire = false;
	for (const FRHTradeRule& R : TradeRules)
	{
		if (bLeversEnabled && R.Kind == ERHTradeRuleKind::Disrupted && R.FromSol >= 0 && R.UntilSol >= 0) // 3.3: a window cuts with the levers on
		{
			CutWindows.Emplace(R.Party, FIntPoint(R.FromSol, R.UntilSol));
		}
		bRetire = bRetire || TradeRuleRetiresAt(R, SolNow);
	}
	for (int32 i = 0; i < Passed; ++i)
	{
		const int32 ClosingSol = First + i;
		// The last pass catches every due convoy, whatever its ClearSol (a jump of more than a ring's worth skips sols, never a clearing).
		const int32 DueUpTo = (i == Passed - 1) ? SolNow - 1 : ClosingSol;
		if (bMarketHistoryEnabled)
		{
			for (FRHMarket& Mk : Markets) { HistEnsure(Mk, ClosingSol); } // E8: the entry the clearing below writes its values into
		}
		for (FRHMarket& Mk : Markets)
		{
			for (int32 s = 0; s < N; ++s)
			{
				Mk.RingSoldG[s * MarketRing + Mk.RingHead] = 0;
				Mk.RingBoughtG[s * MarketRing + Mk.RingHead] = 0;
			}
		}
		if (bCurrencyEnabled)
		{
			BlocSoldMmk[BlocHead] = 0; // E5: the bloc tally keeps E4's ring discipline
			BlocBoughtMmk[BlocHead] = 0;
		}
		ClearConvoys(ClosingSol, DueUpTo);
		ClearDeskOrders(ClosingSol, DueUpTo); // E5: after the convoys, by Id
		for (FRHMarket& Mk : Markets)
		{
			Mk.RingHead = (Mk.RingHead + 1) % MarketRing;
			Mk.RingFill = FMath::Min(Mk.RingFill + 1, MarketRing);
		}
		if (bCurrencyEnabled)
		{
			BlocHead = (BlocHead + 1) % BlocRing;
			BlocFill = FMath::Min(BlocFill + 1, BlocRing);
		}
		ApplyMarketEffects(ClosingSol, DueUpTo); // E6: after the clearing, before production, whether or not the market is live
		if (bLive)
		{
			TArray<FName, TInlineAllocator<8>> CutRivals; // E6: the rivals whose sabotage window covers this pass's closing sol
			for (const TPair<FName, FIntPoint>& W : CutWindows)
			{
				if (ClosingSol >= W.Value.X && ClosingSol < W.Value.Y) { CutRivals.AddUnique(W.Key); }
			}
			for (FRHMarket& Mk : Markets)
			{
				MarketProduceAndReprice(Mk, ClosingSol, CutRivals);
			}
		}
		if (bCurrencyEnabled)
		{
			// E5, outside the live gate. On the last pass the record also walks the sols a long jump skips (it adds their drifts and applies
			// every entry due up to SolNow - 1, as DueUpTo does for convoys); the rate still steps at most once a pass.
			StepCurrencyPass(ClosingSol, DueUpTo);
		}
		if (bMarketHistoryEnabled)
		{
			// E8: the sol this pass opens, at its prices after the reprice and E5's withdrawal re-expression, in the unit and at the rate it trades
			// in; written whether or not the market is live (an off market's entry repeats its price). The last pass opens SolNow.
			const int32 Next = (i == Passed - 1) ? SolNow : ClosingSol + 1;
			for (FRHMarket& Mk : Markets) { HistOpen(Mk, Next); }
		}
	}
	// An entry kept only for an uncleared convoy goes once nothing waits on it.
	RivalEconomies.RemoveAll([this](const FRHRivalEconomy& E)
	{
		return !E.bHasEconomy && !Convoys.ContainsByPredicate([&E](const FRHConvoy& C) { return C.Rival == E.Rival && C.IsPriced() && !C.bCleared; });
	});
	if (bRetire)
	{
		RetireTradeRules(); // E6 (D10): after everything else, E5's steps included (the walk above found a rule to retire)
	}
}

void URHSimWorldSubsystem::MarketProduceAndReprice(FRHMarket& Mk, int32 ClosingSol, TArrayView<const FName> CutRivals)
{
	const int32 N = EconSlots.Num();
	TArray<int64, TInlineAllocator<32>> SumStore;
	SumStore.Init(0, N);
	// E6 (Q-B a, 4.1): a rival whose Disrupted window covers the closing sol (CutRivals, from RollMarket's walk of the rules) makes ProduceG x
	// (100 - SabotageCutPct) / 100 of every slot this sol - in step 2's store and in step 3's Prod alike - while it goes on consuming. The window
	// is keyed by the act's own sol, so the cut is the same in both bands, caught or not. It is computed here for this sol and never written
	// into ProduceG or ProdSumG (both derived at the sync, trap T2); the market reads only windows, never a live timer or an Id.
	TArray<int64, TInlineAllocator<32>> UnmadeG; // per slot, what the cut rivals of this region do not make this sol
	if (CutRivals.Num() > 0)
	{
		UnmadeG.Init(0, N);
	}
	// 2. The rivals' sol: production never fills a store past its cap, and goods the colony sold are never destroyed (they are used up by
	// consumption). Rivals by name string, slots in slot order. A rival that is not discovered still produces: availability gates booking only.
	for (FRHRivalEconomy& E : RivalEconomies)
	{
		if (!E.bHasEconomy || E.RegionId != Mk.RegionId)
		{
			continue;
		}
		const bool bCut = CutRivals.Num() > 0 && CutRivals.Contains(E.Rival);
		if (bCut)
		{
			FRHProductionCut Rec;
			Rec.ClosingSol = ClosingSol;
			Rec.Rival = E.Rival;
			Rec.CutPct = SabotageCutPct;
			ProductionCuts.Add(Rec);
			if (ProductionCuts.Num() > 2 * MktMaxClearingsKept)
			{
				// The newest 64, trimmed now and then (never a shift per cut), its capacity kept: shrinking at each trim reallocated the record two or three
				// times every eight rolls with every rival cut (-scale).
				ProductionCuts.RemoveAt(0, ProductionCuts.Num() - MktMaxClearingsKept, EAllowShrinking::No);
			}
		}
		for (int32 s = 0; s < N; ++s)
		{
			if (!E.Trades[s])
			{
				continue;
			}
			const int64 Made = !bCut ? E.ProduceG[s] : (SabotageCutPct >= 100 ? 0 : E.ProduceG[s] * (100 - SabotageCutPct) / 100);
			if (bCut)
			{
				UnmadeG[s] += E.ProduceG[s] - Made;
			}
			int64 After = E.StoreG[s] + Made - E.ConsumeG[s];
			if (After > E.CapG[s])
			{
				After = MktMax64(E.CapG[s], E.StoreG[s] - E.ConsumeG[s]);
			}
			E.StoreG[s] = MktMax64(0, After);
			SumStore[s] += E.StoreG[s];
		}
	}
	// 3. The repricing: every traded slot, in slot order; the new price holds for the sol that opens. All int64, one signed division.
	const int64 W = FMath::Clamp(MarketWindowSols, 1, MarketRing);
	const int32 Back = (int32)FMath::Min<int64>(W, Mk.RingFill);
	for (int32 s = 0; s < N; ++s)
	{
		const int64 V = Mk.VMcr[s];
		if (!Mk.bTraded[s] || V <= 0)
		{
			continue;
		}
		int64 Sold = 0, Bought = 0;
		if (!bMarketRivalOnly)
		{
			for (int32 b = 1; b <= Back; ++b)
			{
				const int32 Entry = (Mk.RingHead - b + 2 * MarketRing) % MarketRing;
				Sold += Mk.RingSoldG[s * MarketRing + Entry];
				Bought += Mk.RingBoughtG[s * MarketRing + Entry];
			}
		}
		const int64 ProdG = UnmadeG.Num() > 0 ? Mk.ProdSumG[s] - UnmadeG[s] : Mk.ProdSumG[s]; // E6: this sol's production, after any cut
		const int64 Supply = ProdG * W + Sold;
		const int64 Demand = Mk.ConsSumG[s] * W + Bought;
		const int64 FlowBp = MarketDivTrunc((Supply - Demand) * 10000, MktMax64(Supply + Demand, MarketMinVolumeG));
		const int64 FillBp = MktMin64(SumStore[s], Mk.CapSumG[s]) * 10000 / MktMax64(Mk.CapSumG[s], 1);
		const int64 StoreBp = bMarketRivalOnly ? 0 : MktClamp64((FillBp - MarketTargetFillBp) * 2, -10000, 10000);
		const int64 Price = Mk.PriceMcr[s];
		const int64 AnchorBp = MarketDivTrunc((Price - V) * 10000, V); // inside the band: -7500 .. +30000 (it saturates the shortage at 2.6 V)
		const int64 Pressure = MarketDivTrunc(FlowBp * MarketFlowWeight + StoreBp * MarketStoreWeight + AnchorBp * MarketAnchorWeight, 100);
		const int64 StepBp = MktClamp64(MarketDivTrunc(-Pressure * MarketGainPct, 100), -MarketMaxStepBp, MarketMaxStepBp);
		int64 Delta = 0;
		if (StepBp != 0)
		{
			const int64 Mag = MktMax64(1, MktAbs64(Price * StepBp) / 10000); // floor division of non-negatives; at least 1 mcr
			Delta = StepBp > 0 ? Mag : -Mag;
		}
		Mk.PriceMcr[s] = MktClamp64(Price + Delta, Mk.FloorMcr[s], Mk.CeilMcr[s]);
	}
}

void URHSimWorldSubsystem::ClearConvoys(int32 ClosingSol, int32 DueUpToSol)
{
	// By convoy Id (the list is kept in Id order). Every uncleared priced convoy due at or before DueUpToSol clears (R4: <=, never ==).
	for (FRHConvoy& C : Convoys)
	{
		if (C.IsPriced() && !C.bCleared && C.ClearSol <= DueUpToSol)
		{
			ClearConvoy(C, ClosingSol);
		}
	}
}

void URHSimWorldSubsystem::ClearConvoy(FRHConvoy& Convoy, int32 ClosingSol)
{
	// The market's only write besides production and repricing. Sells fill in full (a glut is answered by the price, never by a refusal, so a
	// sell never comes home); the rival's store rises by it, past its cap if it must. Buys fill up to the rival's store, whole kg. Money never
	// limits a fill: the reservation was sized for the whole order, so the fills depend only on the rival's state and the orders - both
	// band-exact. The price is the slot's price in force during the closing sol (before this roll's step 3).
	FRHRivalEconomy* E = FindRivalEconomy(Convoy.Rival);
	FRHMarket* Mk = E ? FindMarket(E->RegionId) : nullptr;
	// E6 (D21, R5): each line's markup is read again here, from the rules, at the order's STORED EffSol - never at the closing sol (a long jump
	// or a reseeded open sol cannot move it) - and no window covering a booked order's EffSol changes after its booking, so this is the markup
	// the reservation was sized with. The booking markups are kept on the record only so a test can see the two agree (trap T3).
	const FRHConvoyTerms* Terms = FindConvoyTerms(Convoy.Id);
	// No terms (no path makes such a convoy: BookConvoy and the load give every uncleared priced convoy its own): the loader's default, max(ClearSol,
	// the open sol), read at this pass, whose closing sol was the open one until this roll - the sol the order clears in (re-review O3).
	const int32 EffSol = Terms ? Terms->EffSol : FMath::Max(Convoy.ClearSol, ClosingSol);
	FRHTradeVerdict Verdict;
	EvaluateTradeRules(Convoy.Rival, EffSol, Convoy.Lines, Verdict);
	int64 BuysValue = 0, SellsValue = 0, BuysCost = 0, SellsGot = 0, Tariff = 0;
	for (int32 l = 0; l < Convoy.Lines.Num(); ++l)
	{
		FRHConvoyLine& L = Convoy.Lines[l];
		L.FilledKg = 0;
		const int32* SlotPtr = EconIndex.Find(L.Resource);
		if (!E || !SlotPtr || !E->StoreG.IsValidIndex(*SlotPtr))
		{
			continue;
		}
		const int32 SlotIndex = *SlotPtr;
		int64 Price = Mk ? Mk->PriceMcr[SlotIndex] : 0;
		if (!Mk)
		{
			const FRHResourceRow* Res = Defs ? Defs->GetResource(L.Resource) : nullptr;
			Price = Res ? (int64)FMath::Max(0, Res->EarthValue_mcr) : 0;
		}
		const int32 MarkupBp = Verdict.LineMarkupBp.IsValidIndex(l) ? Verdict.LineMarkupBp[l] : 0;
		if (L.Kg < 0)
		{
			const int64 Kg = -(int64)L.Kg;
			E->StoreG[SlotIndex] += Kg * 1000;
			L.FilledKg = (int32)Kg;
			const int64 Value = Kg * Price;
			SellsValue += Value;
			SellsGot += MarketLineSettleMcr(Value, MarkupBp, false); // its value less the markup, never more
			Tariff += MarketTariffMcr(Value, MarkupBp);
			if (Mk) { Mk->RingSoldG[SlotIndex * MarketRing + Mk->RingHead] += Kg * 1000; }
		}
		else if (L.Kg > 0)
		{
			const int64 Fill = MktMin64((int64)L.Kg, E->StoreG[SlotIndex] / 1000);
			E->StoreG[SlotIndex] -= Fill * 1000;
			L.FilledKg = (int32)Fill;
			const int64 Value = Fill * Price;
			BuysValue += Value;
			BuysCost += MarketLineSettleMcr(Value, MarkupBp, true); // its value plus the markup
			Tariff += MarketTariffMcr(Value, MarkupBp);
			if (Mk) { Mk->RingBoughtG[SlotIndex * MarketRing + Mk->RingHead] += Fill * 1000; }
		}
	}
	// E8: the clearing's values go into the history entry of the closing sol (HistEnsure made it at the top of this pass), before any markup, in
	// the market's unit then, as the tally's grams are.
	if (Mk && bMarketHistoryEnabled && Mk->HistFill > 0 && Mk->HistSoldValue.Num() == MarketHistSols)
	{
		const int32 Back = Mk->HistLastSol - ClosingSol;
		if (Back >= 0 && Back < Mk->HistFill)
		{
			const int32 Entry = (Mk->HistHead - 1 - Back + 2 * MarketHistSols) % MarketHistSols;
			Mk->HistSoldValue[Entry] += SellsValue;
			Mk->HistBoughtValue[Entry] += BuysValue;
		}
	}
	// With no markup every cost and receipt is its value, so Net is E4's to the milli-unit.
	const int64 Net = BuysCost - SellsGot;
	// E5: the order's currency was fixed at booking from its clearing sol, which is this roll's closing sol. A bloc member's order (bMarks)
	// settles in marks with no conversion; after the market's issue any other order is a credit order whose Net (in the market's unit, marks)
	// converts at the rate in force during the closing sol, a charge rounded up and a payment down. Before the open sol every order is E4's.
	const bool bMarks = Convoy.bMarks;
	const bool bConverted = !bMarks && IsConvertingAt(ClosingSol);
	const int64 Rate = (bMarks || bConverted) ? RateMcr : 0;
	const int64 Settled = bMarks ? Net : (bConverted ? (Net > 0 ? ConvertChargeMcr(Net, Rate) : -ConvertOwedMcr(-Net, Rate)) : Net);
	const int64 Reserved = bMarks ? Convoy.ReservedMmk : Convoy.ReservedMcr;
	int64 Paid = 0;
	if (Settled > 0)
	{
		// A clearing pays at most its own reservation plus the spendable credit (re-review 5b): what is set aside for other convoys is
		// never touched. The rest, which only a load repair can leave, is written off with a Warning and the rival takes the loss.
		// E5: a mark order the same way in marks (R4).
		const int64 Allowed = bMarks ? MktMax64(0, MarkBalanceMmk - (MarkReservedMmk - Reserved)) : MktMax64(0, CreditBalanceMcr - (CreditReservedMcr - Reserved));
		const int64 Pay = MktMin64(Settled, Allowed);
		Paid = Pay - (bMarks ? MarkSpend(Pay) : CreditSpend(Pay));
	}
	else if (Settled < 0)
	{
		if (bMarks)
		{
			Convoy.OwedMmk = -Settled; // E5: carried home, paid through MarkPay at the return
		}
		else
		{
			Convoy.OwedMcr = -Settled; // carried home, paid through CreditPay at the return
		}
	}
	const int64 WrittenOff = Settled > 0 ? Settled - Paid : 0;
	if (WrittenOff > 0)
	{
		if (bMarks)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Order #%d to %s: %s marks could not be paid; %s writes them off"), Convoy.Id, *Convoy.Rival.ToString(), *FormatCredits(WrittenOff), *Convoy.Rival.ToString());
		}
		else
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Order #%d to %s: %s cr could not be paid; %s writes it off"), Convoy.Id, *Convoy.Rival.ToString(), *FormatCredits(WrittenOff), *Convoy.Rival.ToString());
		}
	}
	if (bMarks)
	{
		MarkReservedMmk = MktMax64(0, MarkReservedMmk - Reserved);
		Convoy.ReservedMmk = 0;
		// E5: only a bloc member's mark order enters the bloc tally (re-review 3b), at the clearing price, in the closing sol's entry.
		BlocSoldMmk[BlocHead] += SellsValue;
		BlocBoughtMmk[BlocHead] += BuysValue;
	}
	else
	{
		CreditReservedMcr = MktMax64(0, CreditReservedMcr - Reserved);
		Convoy.ReservedMcr = 0;
	}
	Convoy.ValueMcr = BuysValue + SellsValue; // in the market's unit for every order (E5: the goodwill floor is read in it too); E6: before the markup
	Convoy.bCleared = true;
	// E6 (D22): the markup the colony paid or forwent, in the market's unit: credits before the market counts in marks, marks while it does.
	if (bMarks || bConverted)
	{
		TariffPaidMmk += Tariff;
	}
	else
	{
		TariffPaidMcr += Tariff;
	}
	FRHMarketClearing Rec;
	Rec.ConvoyId = Convoy.Id;
	Rec.Rival = Convoy.Rival;
	Rec.ClosingSol = ClosingSol;
	Rec.ClearSol = Convoy.ClearSol;
	Rec.NetMcr = Net;
	Rec.PaidMcr = Paid;
	Rec.WrittenOffMcr = WrittenOff;
	Rec.ValueMcr = Convoy.ValueMcr;
	Rec.ReleasedMcr = Reserved;
	Rec.Lines = Convoy.Lines;
	Rec.Currency = bMarks ? 1 : (bConverted ? 2 : 0);
	Rec.RateMcr = Rate;
	Rec.SettledMcr = Settled;
	Rec.EffSol = EffSol;
	Rec.LineMarkupBp = Verdict.LineMarkupBp;
	Rec.BookedMarkupBp = Terms ? Terms->LineMarkupBp : TArray<int32>(); // no terms, no booking markups: a test comparing the two then fails loudly
	Rec.TariffMcr = Tariff;
	MarketClearings.Add(MoveTemp(Rec));
	DropConvoyTerms(Convoy.Id); // E6: only an uncleared order keeps its terms (Terms is not read after this)
	if (MarketClearings.Num() > MktMaxClearingsKept)
	{
		MarketClearings.RemoveAt(0, MarketClearings.Num() - MktMaxClearingsKept);
	}
	if (!Convoy.bStanding)
	{
		TArray<FRHConvoyLine> Bought, Sold;
		for (const FRHConvoyLine& L : Convoy.Lines) { (L.Kg > 0 ? Bought : Sold).Add(L); }
		const FString BoughtText = OrderLinesText(Bought, true), SoldText = OrderLinesText(Sold, false);
		if (bMarks)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("Order #%d to %s clears: bought %s, sold %s at %s marks net"), Convoy.Id, *Convoy.Rival.ToString(),
				BoughtText.IsEmpty() ? TEXT("nothing") : *BoughtText, SoldText.IsEmpty() ? TEXT("nothing") : *SoldText, *FormatCredits(Net));
		}
		else if (bConverted)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("Order #%d to %s clears: bought %s, sold %s at %s marks net, %s cr at %s cr a mark"), Convoy.Id, *Convoy.Rival.ToString(),
				BoughtText.IsEmpty() ? TEXT("nothing") : *BoughtText, SoldText.IsEmpty() ? TEXT("nothing") : *SoldText, *FormatCredits(Net), *FormatCredits(Settled), *FormatCredits(Rate));
		}
		else
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("Order #%d to %s clears: bought %s, sold %s at %s cr net"), Convoy.Id, *Convoy.Rival.ToString(),
				BoughtText.IsEmpty() ? TEXT("nothing") : *BoughtText, SoldText.IsEmpty() ? TEXT("nothing") : *SoldText, *FormatCredits(Net));
		}
	}
}

// --- Orders --------------------------------------------------------------------------------------------------------------------------------

FString URHSimWorldSubsystem::OrderLinesText(const TArray<FRHConvoyLine>& Lines, bool bBuys)
{
	FString Out;
	for (const FRHConvoyLine& L : Lines)
	{
		if ((L.Kg > 0) != bBuys || L.Kg == 0)
		{
			continue;
		}
		Out += FString::Printf(TEXT("%s%s:%d"), Out.IsEmpty() ? TEXT("") : TEXT(";"), *L.Resource.ToString(), FMath::Abs(L.Kg));
	}
	return Out;
}

bool URHSimWorldSubsystem::ParseOrderLines(const FString& Text, TArray<FRHConvoyLine>& OutLines, FString& OutWhy) const
{
	// A dedicated strict parser (not ParseResourceList): the kg is a whole number, negative to sell, and a sign other than a leading '-', a
	// zero, a fraction, an empty line, a duplicate or a resource both bought and sold is refused by name.
	OutLines.Reset();
	OutWhy.Reset();
	if (Text.TrimStartAndEnd().IsEmpty())
	{
		OutWhy = TEXT("The order has no lines");
		return false;
	}
	TArray<FString> Entries;
	Text.ParseIntoArray(Entries, TEXT(";"), /*bCullEmpty*/ false);
	for (const FString& Raw : Entries)
	{
		const FString Entry = Raw.TrimStartAndEnd();
		FString Res, Amount;
		int32 Colons = 0;
		for (const TCHAR Ch : Entry) { Colons += Ch == TEXT(':') ? 1 : 0; }
		if (Entry.IsEmpty() || Colons != 1 || !Entry.Split(TEXT(":"), &Res, &Amount))
		{
			OutWhy = FString::Printf(TEXT("The order line '%s' is not Resource:kg"), *Entry);
			OutLines.Reset();
			return false;
		}
		Res.TrimStartAndEndInline();
		Amount.TrimStartAndEndInline();
		if (Res.IsEmpty())
		{
			OutWhy = FString::Printf(TEXT("The order line '%s' names no resource"), *Entry);
			OutLines.Reset();
			return false;
		}
		const bool bSell = Amount.StartsWith(TEXT("-"));
		const FString Digits = bSell ? Amount.Mid(1) : Amount;
		bool bDigits = Digits.Len() >= 1 && Digits.Len() <= 7;
		for (const TCHAR Ch : Digits) { bDigits &= Ch >= TEXT('0') && Ch <= TEXT('9'); }
		if (!bDigits)
		{
			OutWhy = FString::Printf(TEXT("The order line '%s' is not a whole number of kg (negative to sell, no other sign, no fraction)"), *Entry);
			OutLines.Reset();
			return false;
		}
		const int64 Kg = FCString::Atoi64(*Digits);
		if (Kg == 0)
		{
			OutWhy = FString::Printf(TEXT("The order line '%s' is a zero: an order line moves at least 1 kg"), *Entry);
			OutLines.Reset();
			return false;
		}
		const FName Key(*Res);
		if (!EconIndex.Contains(Key))
		{
			OutWhy = FString::Printf(TEXT("'%s' is not a resource"), *Res);
			OutLines.Reset();
			return false;
		}
		const FRHConvoyLine* Dup = OutLines.FindByPredicate([Key](const FRHConvoyLine& L) { return L.Resource == Key; });
		if (Dup)
		{
			OutWhy = ((Dup->Kg < 0) != bSell)
				? FString::Printf(TEXT("%s is both bought and sold in one order"), *Res)
				: FString::Printf(TEXT("%s appears twice in one order"), *Res);
			OutLines.Reset();
			return false;
		}
		FRHConvoyLine& L = OutLines.AddDefaulted_GetRef();
		L.Resource = Key;
		L.Kg = (int32)(bSell ? -Kg : Kg);
	}
	if (OutLines.Num() > MktMaxLines)
	{
		OutWhy = FString::Printf(TEXT("An order has at most %d lines"), MktMaxLines);
		OutLines.Reset();
		return false;
	}
	OutLines.Sort([](const FRHConvoyLine& A, const FRHConvoyLine& B) { return MktNameLess(A.Resource, B.Resource); });
	return true;
}

int64 URHSimWorldSubsystem::GetOrderReservationMcr(FName Rival, const TArray<FRHConvoyLine>& Lines) const
{
	return GetOrderReservationMcr(Rival, Lines, TArray<int32>());
}

int64 URHSimWorldSubsystem::GetOrderReservationMcr(FName Rival, const TArray<FRHConvoyLine>& Lines, const TArray<int32>& MarkupBp) const
{
	// R = max(0, sum(buy kg x Hi(quote)) - sum(sell kg x Lo(quote))): the clearing price is the quote, or one step from it (booking never sees
	// a quote more than one reprice from the clearing), sells always fill in full and buys never fill past the order, so R covers the net.
	// E6 (D21): with a line's markup m, HiM(q) = Hi(q) + DivCeil(Hi(q) x m, 10000) and LoM(q) = max(0, Lo(q) - DivCeil(Lo(q) x m, 10000)): a
	// buy's cost rises with the price and kg x DivCeil(x) >= DivCeil(kg x x), a sell's receipt rises with the price too, so R covers the
	// marked-up net. A missing markup is 0 (E4's reservation, to the milli-credit).
	const FRHRivalEconomy* E = FindRivalEconomy(Rival);
	const FRHMarket* Mk = E ? FindMarket(E->RegionId) : nullptr;
	if (!Mk)
	{
		return 0;
	}
	int64 BuyMcr = 0, SellMcr = 0;
	for (int32 l = 0; l < Lines.Num(); ++l)
	{
		const FRHConvoyLine& L = Lines[l];
		const int32* SlotPtr = EconIndex.Find(L.Resource);
		if (!SlotPtr || !Mk->PriceMcr.IsValidIndex(*SlotPtr))
		{
			continue;
		}
		const int64 Quote = Mk->PriceMcr[*SlotPtr];
		const int32 M = MarkupBp.IsValidIndex(l) ? MarkupBp[l] : 0;
		if (L.Kg > 0)
		{
			const int64 Hi = MktHiQuote(Quote, MarketMaxStepBp);
			BuyMcr += (int64)L.Kg * (Hi + MarketTariffMcr(Hi, M));
		}
		else
		{
			const int64 Lo = MktLoQuote(Quote, MarketMaxStepBp);
			SellMcr += -(int64)L.Kg * MktMax64(0, Lo - MarketTariffMcr(Lo, M));
		}
	}
	return MktMax64(0, BuyMcr - SellMcr);
}

bool URHSimWorldSubsystem::PlanConvoy(FName RivalName, bool bStanding, const FString& OrderText, int32 EffSol, FRHBookPlan& OutPlan, FString& OutWhy) const
{
	// One rule for the verb, the deck and the cheat. HEAD's refusals keep their texts and their order (no contact, already out, the closed
	// route, sabotage, defection, embargo, the lot); the new ones follow: the other convoys out, the lines, the cargo, the loadable lot, the
	// manifest's spare, the money. Booking reads the colony, the data and today's quote - never a rival's store - so nothing it reads can
	// change what the market does (section 5). Nothing here acts.
	OutPlan = FRHBookPlan();
	OutWhy.Reset();
	const FRHRivalRow* Rival = Defs ? Defs->GetRival(RivalName) : nullptr;
	const bool bLive = IsMarketLive();
	if (!bStanding && !bLive)
	{
		OutWhy = TEXT("No market: no neighbor trades yet");
		return false;
	}
	if (!Rival || !IsRivalAvailable(RivalName))
	{
		OutWhy = FString::Printf(TEXT("No trade contact '%s'"), *RivalName.ToString());
		return false;
	}
	const bool bPriced = bLive && HasRivalEconomy(RivalName);
	if (!bStanding && !bPriced)
	{
		OutWhy = FString::Printf(TEXT("%s trades by barter only: it takes no priced orders"), *Rival->DisplayName);
		return false;
	}
	if (FindConvoyIndexFor(RivalName) != INDEX_NONE)
	{
		OutWhy = FString::Printf(TEXT("The convoy is already out (to %s)"), *RivalName.ToString());
		return false;
	}
	if (Convoys.Num() >= MaxConvoysOut)
	{
		OutWhy = FString::Printf(TEXT("%d convoys are already out"), MaxConvoysOut);
		return false;
	}
	// E6: HEAD's four political refusals (closed, disrupted, defected, embargo) are the rule walk's block - the lowest live blocking kind naming
	// the rival, then the lowest Id - with HEAD's texts, so the precedence holds whatever order the rules were made in.
	FRHTradeVerdict Verdict;
	EvaluateTradeRules(RivalName, EffSol, TArray<FRHConvoyLine>(), Verdict);
	if (Verdict.BlockKind != 0)
	{
		OutWhy = TradeBlockText(Verdict.BlockKind, Rival->DisplayName);
		return false;
	}
	// E5 (R5): from a Comply that withdrew the mark until the withdrawal roll, no priced order is booked (a barter convoy never clears, so it
	// is still allowed). Straight after the political refusals (E6's walk keeps it there).
	if (bPriced && bWithdrawPending)
	{
		OutWhy = TEXT("The market is closed while the mark is withdrawn");
		return false;
	}
	OutPlan.bPriced = bPriced;
	OutPlan.bStanding = bStanding;
	// The aggregate lot, as the convoy has always built it (M3 Gate A's adversarial fix): fuel + wear + every sell line, summed per resource.
	OutPlan.Cost.FindOrAdd(NAME_MktHydrogen) += ConvoyH2PerRun;
	OutPlan.Cost.FindOrAdd(NAME_MktSpareParts) += ConvoyWearParts;
	FString LotText;
	if (!bPriced)
	{
		// Barter, exactly as before: the rival's ImportLot is your side, and the lot is checked against the colony's whole stock.
		OutPlan.Sells = RoundLotToGrams(URHDefinitionsSubsystem::ParseResourceList(Rival->ImportLot));
		for (const auto& Item : OutPlan.Sells) { OutPlan.Cost.FindOrAdd(Item.Key) += Item.Value; }
		if (!HasTradeLot(OutPlan.Cost))
		{
			FName Short = NAME_None;
			for (const auto& Item : OutPlan.Cost)
			{
				const double Have = (Defs && Defs->IsSolidResource(Item.Key)) ? GetTotalSolid(Item.Key) + GetStock(Item.Key) : GetStock(Item.Key);
				if (Have + KINDA_SMALL_NUMBER < Item.Value) { Short = Item.Key; break; }
			}
			OutWhy = FString::Printf(TEXT("Convoy short of %s (needs fuel %.0f H2 + %.0f parts + %s)"), *Short.ToString(), ConvoyH2PerRun, ConvoyWearParts, *Rival->ImportLot);
			return false;
		}
		return true;
	}
	// A priced convoy: its lines.
	const FRHRivalEconomy* E = FindRivalEconomy(RivalName);
	if (bStanding)
	{
		// Sell the ImportLot, buy the ExportLot: the same kilograms, now priced.
		for (const auto& Item : RoundLotToGrams(URHDefinitionsSubsystem::ParseResourceList(Rival->ImportLot)))
		{
			FRHConvoyLine& L = OutPlan.Lines.AddDefaulted_GetRef();
			L.Resource = Item.Key;
			L.Kg = -(int32)FMath::RoundToInt64(Item.Value);
		}
		for (const auto& Item : RoundLotToGrams(URHDefinitionsSubsystem::ParseResourceList(Rival->ExportLot)))
		{
			FRHConvoyLine& L = OutPlan.Lines.AddDefaulted_GetRef();
			L.Resource = Item.Key;
			L.Kg = (int32)FMath::RoundToInt64(Item.Value);
		}
		OutPlan.Lines.Sort([](const FRHConvoyLine& A, const FRHConvoyLine& B) { return MktNameLess(A.Resource, B.Resource); });
		LotText = Rival->ImportLot;
	}
	else
	{
		if (!ParseOrderLines(OrderText, OutPlan.Lines, OutWhy))
		{
			return false;
		}
		LotText = OrderLinesText(OutPlan.Lines, false);
		if (LotText.IsEmpty()) { LotText = TEXT("nothing"); }
	}
	int64 BuyKg = 0, SellKg = 0;
	for (const FRHConvoyLine& L : OutPlan.Lines)
	{
		const int32* SlotPtr = EconIndex.Find(L.Resource);
		if (!E || !SlotPtr || !E->Trades.IsValidIndex(*SlotPtr) || !E->Trades[*SlotPtr])
		{
			OutWhy = FString::Printf(TEXT("%s does not trade %s"), *Rival->DisplayName, *L.Resource.ToString());
			return false;
		}
		(L.Kg > 0 ? BuyKg : SellKg) += FMath::Abs((int64)L.Kg);
	}
	// E6: the terms at the order's clearing sol - each line's markup (read again at the clearing, at this same sol) and whether a pact's window
	// covers it: a partner's convoy carries PactCargoKg each way instead of ConvoyCargoKg (2.1). A barter rival never reaches this.
	EvaluateTradeRules(RivalName, EffSol, OutPlan.Lines, Verdict);
	OutPlan.EffSol = EffSol;
	OutPlan.LineMarkupBp = Verdict.LineMarkupBp;
	OutPlan.bPact = Verdict.bPact;
	const int32 CargoKg = Verdict.bPact ? PactCargoKg : ConvoyCargoKg;
	if (BuyKg > CargoKg || SellKg > CargoKg)
	{
		OutWhy = FString::Printf(TEXT("A convoy carries at most %d kg each way: this order buys %lld kg and sells %lld kg"), CargoKg, BuyKg, SellKg);
		return false;
	}
	for (const FRHConvoyLine& L : OutPlan.Lines)
	{
		if (L.Kg < 0)
		{
			OutPlan.Sells.Add(L.Resource, (double)(-(int64)L.Kg));
			OutPlan.Cost.FindOrAdd(L.Resource) += (double)(-(int64)L.Kg);
		}
	}
	// The colony can load it: E3's loadable (uncommitted holdings less the life-support floor), with HEAD's trade tolerance.
	for (const auto& Item : OutPlan.Cost)
	{
		if (GetLaunchLoadableKg(Item.Key) + KINDA_SMALL_NUMBER < Item.Value)
		{
			OutWhy = FString::Printf(TEXT("Convoy short of %s (needs fuel %.0f H2 + %.0f parts + %s)"), *Item.Key.ToString(), ConvoyH2PerRun, ConvoyWearParts, *LotText);
			return false;
		}
	}
	// Re-review condition 2: while a manifest is open Earth is promised the bill and the listed sales, so a written order's sells draw only
	// from E3's spare (loadable less the bill's line less what is already listed). The standing order keeps HEAD's rule.
	if (!bStanding && QuotaPhase == ERHQuotaPhase::AwaitingManifest && OutPlan.Sells.Num() > 0)
	{
		TArray<TPair<FName, double>> Bill;
		GetLaunchBill(Bill);
		for (const FRHConvoyLine& L : OutPlan.Lines)
		{
			if (L.Kg >= 0)
			{
				continue;
			}
			double BillLine = 0.0;
			for (const auto& B : Bill) { BillLine += B.Key == L.Resource ? B.Value : 0.0; }
			const FRHExportLine* Have = ExportLines.FindByPredicate([&L](const FRHExportLine& X) { return X.Resource == L.Resource; });
			const double Spare = GetLaunchLoadableKg(L.Resource) - BillLine - (double)(Have ? Have->Kg : 0);
			const double Need = OutPlan.Cost.FindRef(L.Resource);
			if (Need > Spare + KINDA_SMALL_NUMBER)
			{
				OutWhy = FString::Printf(TEXT("Only %.0f kg of %s is spare above Earth's quota and sales and the life-support floor; the order needs %.0f kg"),
					FMath::Max(0.0, FMath::FloorToDouble(Spare + KINDA_SMALL_NUMBER)), *L.Resource.ToString(), Need);
				return false;
			}
		}
	}
	// E5: the order's currency, fixed here from its clearing sol (2.3): a member's order in marks (E4's reservation, read in milli-marks,
	// against the spendable marks); any other order after the market's issue in credits converted at the rate (the reservation covers a full
	// rate step as well as a full price step); before the open sol E4's order, exactly.
	const uint8 Currency = GetOrderCurrencyFor(RivalName, EffSol);
	OutPlan.bMarks = Currency == 1;
	OutPlan.bConverted = Currency == 2;
	if (OutPlan.bMarks)
	{
		OutPlan.ReserveMmk = GetOrderReservationMcr(RivalName, OutPlan.Lines, OutPlan.LineMarkupBp);
		if (OutPlan.ReserveMmk > GetSpendableMmk())
		{
			OutWhy = FString::Printf(TEXT("The order may cost up to %s marks net; %s marks are spendable"), *FormatCredits(OutPlan.ReserveMmk), *FormatCredits(GetSpendableMmk()));
			return false;
		}
		return true;
	}
	OutPlan.ReserveMcr = OutPlan.bConverted ? GetConvertedReservationMcr(RivalName, OutPlan.Lines, RateMcr, OutPlan.LineMarkupBp) : GetOrderReservationMcr(RivalName, OutPlan.Lines, OutPlan.LineMarkupBp);
	if (OutPlan.ReserveMcr > GetSpendableMcr())
	{
		OutWhy = FString::Printf(TEXT("The order may cost up to %s cr net; %s cr is spendable"), *FormatCredits(OutPlan.ReserveMcr), *FormatCredits(GetSpendableMcr()));
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::CanPlaceOrder(FName Rival, const FString& Text, FString& OutWhy, int64* OutReserveMcr, uint8* OutCurrency) const
{
	FRHBookPlan Plan;
	const bool bOk = PlanConvoy(Rival, false, Text, GetBookingSolNow(), Plan, OutWhy);
	if (OutReserveMcr) { *OutReserveMcr = bOk ? (Plan.bMarks ? Plan.ReserveMmk : Plan.ReserveMcr) : 0; }
	if (OutCurrency) { *OutCurrency = Plan.bMarks ? 1 : (Plan.bConverted ? 2 : 0); }
	return bOk;
}

bool URHSimWorldSubsystem::CanPlaceStandingOrder(FName Rival, FString& OutWhy) const
{
	FRHBookPlan Plan;
	return PlanConvoy(Rival, true, FString(), GetBookingSolNow(), Plan, OutWhy);
}

bool URHSimWorldSubsystem::BookConvoy(const FRHCommand& Cmd, bool bStanding, FString& OutWhy)
{
	// The worker of the Convoy and MarketOrder verbs, any step. The clearing sol is the order's OWN scheduled time (ExecuteAt), not the step
	// that runs it, which is what keeps the market band-exact.
	SyncRivalEconomies(false);
	const int32 ClearSol = FMath::FloorToInt32(Cmd.ExecuteAtSimSeconds / (double)URHSimClockSubsystem::SolLengthSimSeconds);
	// R3 (b): a booking never sees a clearing more than one reprice from its quote. If the order clears later than the sol after the open one
	// (a clock jump, a frame longer than a sol), the worker first rolls the market to that sol, exactly as StepCityGrowth would later in this
	// step, logging nothing. At ClearSol == open + 1 it does not (the queue can run out of ExecuteAt order on the boundary step).
	const bool bMayPrice = !bStanding || (IsMarketLive() && HasRivalEconomy(Cmd.Target));
	if (bMayPrice && ClearSol > MarketCheckedSol + 1)
	{
		RollMarket(ClearSol);
	}
	// E5: the sol the order clears in decides its currency, read AFTER the catch-up (a late booking clears at the open sol's roll).
	const int32 EffSol = FMath::Max(ClearSol, MarketCheckedSol);
	FRHBookPlan Plan;
	if (!PlanConvoy(Cmd.Target, bStanding, Cmd.Text, EffSol, Plan, OutWhy))
	{
		return false;
	}
	const FRHRivalRow* Rival = Defs->GetRival(Cmd.Target);
	if (!Plan.bPriced)
	{
		// Barter: commit at departure, one spend of the validated aggregate (HEAD).
		SpendTradeLot(Plan.Cost);
	}
	else
	{
		for (const auto& Item : Plan.Cost)
		{
			TakeGrowthResource(Item.Key, Item.Value);
		}
	}
	// E1: the fuel and the wear are Used; the rival's lot (or the sells) is Out - the one nominal record, within 0.1 g a line.
	NoteFlow(NAME_MktHydrogen, ERHLedgerLeg::Used, RoundToGrams(ConvoyH2PerRun));
	NoteFlow(NAME_MktSpareParts, ERHLedgerLeg::Used, RoundToGrams(ConvoyWearParts));
	NoteFlowMap(Plan.Sells, ERHLedgerLeg::Out);
	FRHConvoy Convoy;
	Convoy.Id = NextConvoyId++;
	Convoy.Rival = Cmd.Target;
	Convoy.bStanding = bStanding && Plan.bPriced;
	if (Plan.bPriced)
	{
		Convoy.ClearSol = ClearSol;
		Convoy.bCleared = false;
		Convoy.Lines = Plan.Lines;
		if (Plan.bMarks)
		{
			Convoy.bMarks = true; // E5: a bloc member's order, its reservation in marks
			Convoy.ReservedMmk = Plan.ReserveMmk;
			MarkReservedMmk += Plan.ReserveMmk;
		}
		else
		{
			Convoy.ReservedMcr = Plan.ReserveMcr;
			CreditReservedMcr += Plan.ReserveMcr;
		}
		// E6 (R5): the order's terms - the sol it clears in (its markups are read there, now and at the clearing) and each line's booking
		// markup - kept by convoy Id in E6's own block, never in E4's (MX9 pins its layout). Ids only grow, so the list stays in Id order.
		FRHConvoyTerms Terms;
		Terms.ConvoyId = Convoy.Id;
		Terms.EffSol = Plan.EffSol;
		Terms.LineMarkupBp = Plan.LineMarkupBp;
		ConvoyTerms.Add(MoveTemp(Terms));
	}
	Convoys.Add(Convoy);
	RelationRef(Cmd.Target); // ensure the relation entry exists
	if (bStanding || !Plan.bPriced)
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("Convoy dispatched to %s (%.0f km; %s -> %s)"), *Rival->DisplayName, Rival->DistanceKm, *Rival->ImportLot, *Rival->ExportLot);
	}
	else
	{
		const FString BuyText = OrderLinesText(Plan.Lines, true), SellText = OrderLinesText(Plan.Lines, false);
		UE_LOG(LogRedHopeSim, Display, TEXT("Order #%d to %s: buy %s, sell %s; clears at the end of sol %d; convoy %d of %d out"), Convoy.Id, *Cmd.Target.ToString(),
			BuyText.IsEmpty() ? TEXT("nothing") : *BuyText, SellText.IsEmpty() ? TEXT("nothing") : *SellText, ClearSol, Convoys.Num(), MaxConvoysOut);
	}
	return true;
}

// --- The road ------------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::StepTrade(float SubDt)
{
	// Idle colonies (and every pre-M3 baseline) have no convoy out - a true no-op, so the 10-sol baseline is byte-identical.
	if (Convoys.Num() == 0)
	{
		return;
	}
	// A dust storm freezes every convoy where it sits (brief: routes disrupted by storms). The rover waits out the weather.
	const FRHEventRow* Event = GetActiveEvent();
	const bool bStorm = Event && Event->Type == FName("DustStorm");
	const double DtSols = SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	for (int32 i = 0; i < Convoys.Num();)
	{
		FRHConvoy& Active = Convoys[i];
		const FRHRivalRow* Rival = Defs ? Defs->GetRival(Active.Rival) : nullptr;
		if (!Rival)
		{
			// The row vanished (data edit) - abort cleanly, and give back what was set aside for its order (E5: in either purse).
			if (Active.IsPriced() && !Active.bCleared)
			{
				CreditReservedMcr = MktMax64(0, CreditReservedMcr - Active.ReservedMcr);
				MarkReservedMmk = MktMax64(0, MarkReservedMmk - Active.ReservedMmk);
				DropConvoyTerms(Active.Id); // E6: its order will never clear
			}
			Convoys.RemoveAt(i);
			continue;
		}
		if (bStorm)
		{
			++i;
			continue;
		}
		const double LegTime = ConvoyLegSolsFor(Active.Rival);
		Active.LegSols += DtSols;
		if (Active.LegSols < LegTime)
		{
			++i; // still en route on this leg
			continue;
		}
		if (!Active.bReturning)
		{
			// Reached the rival; turn around (carry the spillover into the return).
			Active.bReturning = true;
			Active.LegSols -= LegTime;
			++i;
			continue;
		}
		if (Active.IsPriced() && !Active.bCleared)
		{
			// A convoy cannot come home before its order has cleared: it holds at the end of its last leg. (StepCityGrowth runs first in both
			// spines, so a convoy whose clearing sol has closed has cleared before it can hold; the shipped data's round trips make this moot.)
			Active.LegSols = LegTime;
			++i;
			continue;
		}
		const FRHConvoy Done = Active;
		Convoys.RemoveAt(i);
		StepConvoyReturn(Done);
	}
}

void URHSimWorldSubsystem::StepConvoyReturn(const FRHConvoy& Done)
{
	// Home again: deliver what was bought (what filled, for a priced convoy; the rival's ExportLot for a barter one), pay what the colony
	// is owed, warm the relation, drop the record.
	const FRHRivalRow* Rival = Defs->GetRival(Done.Rival);
	if (!Rival)
	{
		return;
	}
	TMap<FName, double> TheirLot;
	FString Delivered;
	if (!Done.IsPriced())
	{
		TheirLot = RoundLotToGrams(URHDefinitionsSubsystem::ParseResourceList(Rival->ExportLot)); // E1: whole grams
		Delivered = Rival->ExportLot;
	}
	else
	{
		bool bInFull = true;
		TArray<FRHConvoyLine> Filled;
		for (const FRHConvoyLine& L : Done.Lines)
		{
			if (L.Kg > 0)
			{
				bInFull &= L.FilledKg == L.Kg;
				if (L.FilledKg > 0)
				{
					TheirLot.Add(L.Resource, (double)L.FilledKg);
					FRHConvoyLine F = L;
					F.Kg = L.FilledKg;
					Filled.Add(F);
				}
			}
		}
		// A standing order filled in full prints ExportLot verbatim, so HEAD's line is byte-identical; a short fill prints the kg that landed.
		Delivered = (Done.bStanding && bInFull) ? Rival->ExportLot : OrderLinesText(Filled, true);
		if (Delivered.IsEmpty()) { Delivered = TEXT("nothing"); }
	}
	NoteFlowMap(TheirLot, ERHLedgerLeg::In); // E1: the convoy home - every line lands, at the depot (the Lander) or in the pool
	// E7: the colony's depot, by the one depot rule (the Lander unless the harness binds another building; 0 = none, the pool takes it).
	const int32 DepotId = FindColonyDepotBuildingId(GetColonyRegionId());
	for (const auto& Item : TheirLot)
	{
		if (Defs && Defs->IsSolidResource(Item.Key))
		{
			// Drop at the trade depot (the Lander) for normal hauling.
			bool bDropped = false;
			if (FRHBuildingInstance* Depot = DepotId != 0 ? FindBuilding(DepotId) : nullptr)
			{
				Depot->OutputKg.FindOrAdd(Item.Key) += Item.Value;
				bDropped = true;
			}
			if (!bDropped) { AddStock(Item.Key, Item.Value); }
			OnStockChanged.Broadcast(Item.Key, GetTotalSolid(Item.Key));
		}
		else
		{
			AddStock(Item.Key, Item.Value); // fluids join the pool
		}
	}
	if (Done.OwedMcr > 0)
	{
		// What the rival owed the colony at the clearing comes home with the convoy, whatever became of the relationship on the road.
		CreditPay(Done.OwedMcr);
		MarketSalesMcr += Done.OwedMcr;
	}
	if (Done.OwedMmk > 0)
	{
		MarkPay(Done.OwedMmk); // E5: a bloc member pays in marks (a withdrawal on the road has already redeemed them, and set this to 0)
	}
	// The goods always land (paid for at dispatch - conservation), but the DIPLOMATIC upside accrues only if the relationship survived the
	// trip (M4 C+D's adversarial fix), and, for a written order, only if what filled was worth GoodwillMinValueMcr: ten one-kilogram orders
	// must not farm ten runs' goodwill. The standing order keeps today's rule unconditionally (its value at the floor prices is 237.5 cr).
	// E6: on the rule list - no Severed, Embargo or Closed rule names the rival; a disruption is still left out, as HEAD.
	const bool bRelationshipIntact = !HasDefected(Done.Rival) && !IsEmbargoed(Done.Rival) && !IsRouteClosed(Done.Rival);
	const bool bWorthIt = !Done.IsPriced() || Done.bStanding || Done.ValueMcr >= GoodwillMinValueMcr;
	if (bRelationshipIntact && bWorthIt)
	{
		double& Rel = RelationRef(Done.Rival);
		Rel = FMath::Clamp(Rel + RelationPerRun, 0.0, 100.0);
		// Fair dealing is who you are: an honest completed trade nudges the HumanNature axis toward Evolved/Diplomatic (M4 Gate A) and
		// earns influence (M4 Gate C - the peaceful path earns the tools of peace).
		HumanNatureAxis = FMath::Clamp(HumanNatureAxis + HumanNatureFairTradeShift, -100.0, 100.0);
		Influence += InfluencePerTrade;
		OnAlert.Broadcast(FString::Printf(
			TEXT("CONVOY HOME — %s delivered (%s). Relations with %s now %.0f."),
			*Rival->DisplayName, *Delivered, *Rival->Nation, Rel));
	}
	else if (bRelationshipIntact)
	{
		OnAlert.Broadcast(FString::Printf(
			TEXT("CONVOY HOME — %s delivered (%s). Too small a trade to change standing."),
			*Rival->DisplayName, *Delivered));
	}
	else
	{
		OnAlert.Broadcast(FString::Printf(
			TEXT("CONVOY HOME — cargo from %s arrived, but the relationship soured while the rover was out. Goods only; no goodwill."),
			*Rival->DisplayName));
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("=== CONVOY RETURN: %s delivered %s, relation %.0f%s ==="),
		*Rival->DisplayName, *Delivered, GetRivalRelation(Done.Rival),
		bRelationshipIntact ? TEXT("") : TEXT(" (soured - goods only)"));
}

double URHSimWorldSubsystem::GetConvoyProgress(const FRHConvoy& Convoy) const
{
	const double LegTime = ConvoyLegSolsFor(Convoy.Rival);
	if (LegTime <= 0.0)
	{
		return 0.0;
	}
	const double Legs = (Convoy.bReturning ? 1.0 : 0.0) + FMath::Clamp(Convoy.LegSols / LegTime, 0.0, 1.0);
	return FMath::Clamp(Legs / 2.0, 0.0, 1.0);
}

// --- The save ----------------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::WriteMarketPayload(FArchive& Ar, const TArray<FRHRivalEconomy>& Econ, const TArray<FRHMarket>& Mkts) const
{
	// v55. Inside E4's frame ('RHE4'), followed by E5's 'RHE5' (v56). The slot table once (the count, then the names ascending: the E1 slots by
	// name); then per region in Id order: RegionId and per slot the price and the 8 + 8 ring entries, then the ring's fill and head; the open
	// sol; the rivals with an economy (their count, then per rival in name order its name and the store of every slot, 0 where it trades
	// nothing); the lifetime market sales and the reserved credit; then NextConvoyId and the convoys by Id. The market and rival parts are
	// sized by the tables; the convoy list is sized by history, so a test measures the block with GetMarketBlockBytes(), never as a constant.
	int32 SlotCount = EconSlots.Num();
	Ar << SlotCount;
	for (const FRHEconSlot& S : EconSlots)
	{
		FString Name = S.Name.ToString();
		Ar << Name;
	}
	int32 MarketCount = Mkts.Num();
	Ar << MarketCount;
	for (const FRHMarket& Mk : Mkts)
	{
		int32 Region = Mk.RegionId;
		Ar << Region;
		for (int32 s = 0; s < SlotCount; ++s)
		{
			int64 Price = Mk.PriceMcr[s];
			Ar << Price;
			for (int32 e = 0; e < MarketRing; ++e) { int64 G = Mk.RingSoldG[s * MarketRing + e]; Ar << G; }
			for (int32 e = 0; e < MarketRing; ++e) { int64 G = Mk.RingBoughtG[s * MarketRing + e]; Ar << G; }
		}
		int32 Fill = Mk.RingFill, Head = Mk.RingHead;
		Ar << Fill << Head;
	}
	int32 Checked = MarketCheckedSol;
	Ar << Checked;
	int32 RivalCount = 0;
	for (const FRHRivalEconomy& E : Econ) { RivalCount += E.bHasEconomy ? 1 : 0; }
	Ar << RivalCount;
	for (const FRHRivalEconomy& E : Econ)
	{
		if (!E.bHasEconomy)
		{
			continue;
		}
		FString Name = E.Rival.ToString();
		Ar << Name;
		for (int32 s = 0; s < SlotCount; ++s) { int64 G = E.StoreG[s]; Ar << G; }
	}
	int64 Sales = MarketSalesMcr, Reserved = CreditReservedMcr;
	Ar << Sales << Reserved;
	int32 Next = NextConvoyId, ConvoyCount = Convoys.Num();
	Ar << Next << ConvoyCount;
	for (const FRHConvoy& C : Convoys)
	{
		int32 Id = C.Id;
		FString Rival = C.Rival.ToString();
		uint8 Returning = C.bReturning ? 1 : 0, Standing = C.bStanding ? 1 : 0, Cleared = C.bCleared ? 1 : 0;
		double Leg = C.LegSols;
		int32 ClearSol = C.ClearSol;
		int64 Held = C.ReservedMcr, Owed = C.OwedMcr, Value = C.ValueMcr;
		int32 LineCount = C.Lines.Num();
		Ar << Id << Rival << Returning << Leg << Standing << ClearSol << Cleared << Held << Owed << Value << LineCount;
		for (const FRHConvoyLine& L : C.Lines)
		{
			FString Name = L.Resource.ToString();
			int32 Kg = L.Kg, Filled = L.FilledKg;
			Ar << Name << Kg << Filled;
		}
	}
}

void URHSimWorldSubsystem::WriteMarketBlock(FArchive& Ar) const
{
	SyncRivalEconomies(false); // the block always carries the synced state
	WriteMarketPayload(Ar, RivalEconomies, Markets);
}

int32 URHSimWorldSubsystem::GetMarketBlockBytes() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, /*bIsPersistent*/ true);
	WriteMarketBlock(Ar);
	return Bytes.Num();
}

void URHSimWorldSubsystem::ReadMarketBlock(FArchive& Ar, const TArray<uint8>& File, FName V17Rival, bool bV17Returning, double V17LegSols)
{
	// Read after teardown, so it repairs with a Warning and never refuses (E1 R1, E2 R5, E3): the colony always loads. The block is
	// authoritative for the convoys; the v17 slot is only the fallback when the block cannot be read, and then it comes home EMPTY (O7): nothing
	// is delivered that was not bought. Repairs, each with a Warning (no Error is ever logged): a slot table out of order drops the market and
	// the rivals' stores to their start values; a price outside [0.25 V, 4 V] is clamped; a ring index out of range resets that tally; a store
	// below 0 is 0 (above its cap is legal); an unknown rival is skipped; a convoy naming a rival with no economy, an untraded resource or a
	// malformed line is dropped; an open sol ahead of C5d's is C5d's. E5 (R4) moved two repairs, with their texts, conditions and order, to
	// ReconcileReservations, which runs after the currency block: the sales total clamped into [0, Awarded - SalesPaid] and the reserved
	// credit set to the sum of the reservations (now the desk's too), cut from the highest Id down until it fits the balance. This reader
	// leaves what they need: what the block said, whether it parsed, every convoy Id it held, and why it started at its start values.
	RivalEconomies.Reset();
	Markets.Reset();
	Convoys.Reset();
	MarketClearings.Reset();
	NextConvoyId = 1;
	CreditReservedMcr = 0;
	MarketSalesMcr = 0;
	MarketCheckedSol = ProductionCheckedSol;
	SyncedRivalsGeneration = MAX_uint32;
	MarketBlockLoss = ERHMarketBlockLoss::None;
	bMarketBlockLost = false;
	bMarketParsed = false;
	ReadMarketSalesMcr = 0;
	ReadMarketReservedMcr = 0;
	ReadMarketConvoyIds.Reset();
	TArray<uint8> Payload;
	FString Why;
	bool bFound = RHSaveFrame::Read(Ar, File, RHSaveFrame::MarketTag, Payload, Why);
	if (!bFound)
	{
		MarketBlockLoss = ERHMarketBlockLoss::Lost;
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the market block is lost (%s); the market starts at its start values"), *Why);
	}
	const int32 N = EconSlots.Num();
	struct FReadMarket { int32 Region = 0; TArray<int64> Price, Sold, Bought; int32 Fill = 0, Head = 0; };
	struct FReadRival { FString Name; TArray<int64> Store; };
	struct FReadConvoy
	{
		int32 Id = 0; FString Rival; uint8 Returning = 0, Standing = 0, Cleared = 0; double Leg = 0.0; int32 ClearSol = -1;
		int64 Held = 0, Owed = 0, Value = 0; TArray<FRHConvoyLine> Lines; TArray<FString> LineNames;
	};
	TArray<FString> FileSlots;
	TArray<FReadMarket> ReadMarkets;
	TArray<FReadRival> ReadRivals;
	TArray<FReadConvoy> ReadConvoys;
	int32 ReadChecked = 0, ReadNext = 1;
	int64 ReadSales = 0, ReadReserved = 0;
	bool bParsed = false;
	if (bFound)
	{
		FMemoryReader In(Payload);
		const auto Cut = [&]() { return In.IsError(); };
		int32 SlotCount = 0;
		In << SlotCount;
		bool bOk = !Cut() && SlotCount >= 0 && SlotCount <= MktMaxList;
		Why = bOk ? FString() : FString::Printf(TEXT("slot count %d"), SlotCount);
		for (int32 k = 0; bOk && k < SlotCount; ++k)
		{
			FString Name;
			bOk = RHSaveFrame::ReadName(In, Name);
			FileSlots.Add(Name);
		}
		int32 MarketCount = 0;
		if (bOk) { In << MarketCount; bOk = !Cut() && MarketCount >= 0 && MarketCount <= 64; if (!bOk) { Why = FString::Printf(TEXT("region count %d"), MarketCount); } }
		for (int32 m = 0; bOk && m < MarketCount; ++m)
		{
			FReadMarket Rm;
			In << Rm.Region;
			Rm.Price.SetNum(SlotCount);
			Rm.Sold.SetNum(SlotCount * MarketRing);
			Rm.Bought.SetNum(SlotCount * MarketRing);
			for (int32 s = 0; s < SlotCount && !Cut(); ++s)
			{
				In << Rm.Price[s];
				for (int32 e = 0; e < MarketRing; ++e) { In << Rm.Sold[s * MarketRing + e]; }
				for (int32 e = 0; e < MarketRing; ++e) { In << Rm.Bought[s * MarketRing + e]; }
			}
			In << Rm.Fill << Rm.Head;
			bOk = !Cut();
			ReadMarkets.Add(MoveTemp(Rm));
		}
		if (bOk) { In << ReadChecked; bOk = !Cut(); }
		int32 RivalCount = 0;
		if (bOk) { In << RivalCount; bOk = !Cut() && RivalCount >= 0 && RivalCount <= MktMaxList; if (!bOk) { Why = FString::Printf(TEXT("rival count %d"), RivalCount); } }
		for (int32 r = 0; bOk && r < RivalCount; ++r)
		{
			FReadRival Rr;
			bOk = RHSaveFrame::ReadName(In, Rr.Name);
			Rr.Store.SetNum(SlotCount);
			for (int32 s = 0; bOk && s < SlotCount; ++s) { In << Rr.Store[s]; }
			bOk = bOk && !Cut();
			ReadRivals.Add(MoveTemp(Rr));
		}
		if (bOk) { In << ReadSales << ReadReserved << ReadNext; bOk = !Cut(); }
		int32 ConvoyCount = 0;
		if (bOk) { In << ConvoyCount; bOk = !Cut() && ConvoyCount >= 0 && ConvoyCount <= MktMaxList; if (!bOk) { Why = FString::Printf(TEXT("convoy count %d"), ConvoyCount); } }
		for (int32 c = 0; bOk && c < ConvoyCount; ++c)
		{
			FReadConvoy Rc;
			In << Rc.Id;
			bOk = RHSaveFrame::ReadName(In, Rc.Rival);
			if (bOk) { In << Rc.Returning << Rc.Leg << Rc.Standing << Rc.ClearSol << Rc.Cleared << Rc.Held << Rc.Owed << Rc.Value; }
			int32 LineCount = 0;
			if (bOk) { In << LineCount; bOk = !Cut() && LineCount >= 0 && LineCount <= MktMaxLines; if (!bOk) { Why = FString::Printf(TEXT("a convoy with %d lines"), LineCount); } }
			for (int32 l = 0; bOk && l < LineCount; ++l)
			{
				FString Name;
				FRHConvoyLine L;
				bOk = RHSaveFrame::ReadName(In, Name);
				if (bOk) { In << L.Kg << L.FilledKg; bOk = !Cut(); }
				Rc.LineNames.Add(Name);
				Rc.Lines.Add(L);
			}
			ReadConvoys.Add(MoveTemp(Rc));
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
		if (!bParsed)
		{
			MarketBlockLoss = ERHMarketBlockLoss::Malformed;
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the market block is malformed (%s); the market starts at its start values"), *Why);
		}
	}
	bMarketParsed = bParsed;
	if (!bParsed)
	{
		bMarketBlockLost = true; // E5: the currency block then starts unissued
		SyncRivalEconomies(false, true); // the start values: prices at the Earth value, stores at StoreStart, an empty tally
		if (!V17Rival.IsNone())
		{
			if (Defs && Defs->GetRival(V17Rival))
			{
				FRHConvoy Empty;
				Empty.Id = NextConvoyId++;
				Empty.Rival = V17Rival;
				Empty.bReturning = bV17Returning;
				Empty.LegSols = (FMath::IsFinite(V17LegSols) && V17LegSols >= 0.0) ? V17LegSols : 0.0;
				Empty.ClearSol = 0; // priced, cleared and empty: it comes home with nothing
				Empty.bCleared = true;
				Convoys.Add(Empty);
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the convoy to %s lost its order with the market block; it comes home empty"), *V17Rival.ToString());
			}
			else
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the convoy slot names %s, which no longer exists; dropped"), *V17Rival.ToString());
			}
		}
		return;
	}
	// The slot table: ascending, and no repeat, or the market and the rivals' stores (both indexed by it) go to their start values.
	bool bSlotsBad = false;
	for (int32 k = 1; k < FileSlots.Num(); ++k)
	{
		bSlotsBad |= FileSlots[k].Compare(FileSlots[k - 1], ESearchCase::CaseSensitive) <= 0;
	}
	TArray<int32> LiveOf; // file slot -> live slot, or INDEX_NONE
	for (const FString& Name : FileSlots)
	{
		const int32* SlotPtr = EconIndex.Find(FName(*Name));
		LiveOf.Add(SlotPtr ? *SlotPtr : INDEX_NONE);
		if (!SlotPtr && !bSlotsBad)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the market block holds %s, which is no longer a resource slot; skipped"), *Name);
		}
	}
	if (bSlotsBad)
	{
		MarketBlockLoss = ERHMarketBlockLoss::SlotsReset;
		bMarketBlockLost = true;
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the market block's slot names are out of order or repeated; the market and the rival stores start at their start values"));
		ReadMarkets.Reset();
		ReadRivals.Reset();
	}
	// Assign what was read as the "old" state; the sync then keeps it where a row still has an economy and starts everything else fresh.
	bool bRegionsOk = true;
	for (const FReadMarket& Rm : ReadMarkets)
	{
		FRHMarket Mk;
		Mk.RegionId = Rm.Region;
		Mk.PriceMcr.Init(0, N);
		Mk.RingSoldG.Init(0, N * MarketRing);
		Mk.RingBoughtG.Init(0, N * MarketRing);
		bool bRingBad = Rm.Fill < 0 || Rm.Fill > MarketRing || Rm.Head < 0 || Rm.Head >= MarketRing;
		for (int32 k = 0; k < Rm.Price.Num() && k < LiveOf.Num(); ++k)
		{
			if (LiveOf[k] == INDEX_NONE) { continue; }
			Mk.PriceMcr[LiveOf[k]] = Rm.Price[k];
			for (int32 e = 0; e < MarketRing; ++e)
			{
				const int64 Sold = Rm.Sold[k * MarketRing + e], Bought = Rm.Bought[k * MarketRing + e];
				bRingBad |= Sold < 0 || Bought < 0;
				Mk.RingSoldG[LiveOf[k] * MarketRing + e] = Sold;
				Mk.RingBoughtG[LiveOf[k] * MarketRing + e] = Bought;
			}
		}
		if (bRingBad)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: region %d's tally has a ring index or an entry out of range; the tally is reset"), Rm.Region);
			Mk.RingSoldG.Init(0, N * MarketRing);
			Mk.RingBoughtG.Init(0, N * MarketRing);
			Mk.RingFill = 0;
			Mk.RingHead = 0;
		}
		else
		{
			Mk.RingFill = Rm.Fill;
			Mk.RingHead = Rm.Head;
		}
		bRegionsOk &= !Markets.ContainsByPredicate([&Rm](const FRHMarket& X) { return X.RegionId == Rm.Region; });
		Markets.Add(MoveTemp(Mk));
	}
	if (!bRegionsOk)
	{
		MarketBlockLoss = ERHMarketBlockLoss::RegionsRepeated;
		bMarketBlockLost = true;
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: a region repeats in the market block; the market starts at its start values"));
		Markets.Reset();
	}
	bool bRivalsOk = true;
	for (int32 r = 0; r < ReadRivals.Num(); ++r)
	{
		bRivalsOk &= r == 0 || ReadRivals[r].Name.Compare(ReadRivals[r - 1].Name, ESearchCase::CaseSensitive) > 0;
	}
	if (!bRivalsOk)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the rival names are out of order or repeated; every rival store starts at StoreStart"));
		ReadRivals.Reset();
	}
	for (const FReadRival& Rr : ReadRivals)
	{
		FRHRivalEconomy E;
		E.Rival = FName(*Rr.Name);
		E.StoreG.Init(0, N);
		for (int32 k = 0; k < Rr.Store.Num() && k < LiveOf.Num(); ++k)
		{
			if (LiveOf[k] == INDEX_NONE) { continue; }
			if (Rr.Store[k] < 0)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: %s's store of %s is %lld g; set to 0"), *Rr.Name, *FileSlots[k], Rr.Store[k]);
			}
			E.StoreG[LiveOf[k]] = MktMax64(0, Rr.Store[k]);
		}
		RivalEconomies.Add(MoveTemp(E));
	}
	// Convoys first, so the sync below keeps an entry that an uncleared priced convoy still names.
	int32 LastId = 0;
	TSet<FName> Named;
	for (const FReadConvoy& Rc : ReadConvoys)
	{
		ReadMarketConvoyIds.Add(Rc.Id); // E5: a mark order whose convoy this drops is dropped silently (cross-check item 2)
		FRHConvoy C;
		C.Id = Rc.Id;
		C.Rival = FName(*Rc.Rival);
		C.bReturning = Rc.Returning != 0;
		C.LegSols = Rc.Leg;
		C.bStanding = Rc.Standing != 0;
		C.ClearSol = Rc.ClearSol;
		C.bCleared = Rc.Cleared != 0;
		C.ReservedMcr = Rc.Held;
		C.OwedMcr = Rc.Owed;
		C.ValueMcr = Rc.Value;
		C.Lines = Rc.Lines;
		for (int32 l = 0; l < C.Lines.Num(); ++l) { C.Lines[l].Resource = FName(*Rc.LineNames[l]); }
		FString Drop;
		const FRHRivalRow* Row = Defs ? Defs->GetRival(C.Rival) : nullptr;
		if (C.Id <= LastId) { Drop = TEXT("its Id is not above the one before"); }
		else if (!Row) { Drop = TEXT("its rival no longer exists"); }
		else if (Named.Contains(C.Rival)) { Drop = TEXT("a second convoy to the same rival"); }
		else if (!FMath::IsFinite(C.LegSols) || C.LegSols < 0.0) { Drop = TEXT("its leg is not a finite non-negative number of sols"); }
		else if (C.ReservedMcr < 0 || C.OwedMcr < 0 || C.ValueMcr < 0) { Drop = TEXT("a negative amount of credit"); }
		else if (C.ClearSol < -1) { Drop = TEXT("a clearing sol below -1"); }
		else if (!C.IsPriced() && (!C.bCleared || C.Lines.Num() > 0 || C.ReservedMcr != 0 || C.OwedMcr != 0 || C.bStanding)) { Drop = TEXT("a barter convoy that carries an order"); }
		else if (C.IsPriced() && !C.bCleared && (C.OwedMcr != 0 || C.ValueMcr != 0)) { Drop = TEXT("an uncleared order that already owes credit"); }
		if (Drop.IsEmpty() && C.IsPriced())
		{
			// An uncleared order needs a rival that still has an economy, and every line a resource it trades. A cleared one only needs lines the
			// table still knows (it delivers what filled), and the same trade test while its rival still has an economy.
			FRHRivalEconomy Probe;
			const bool bEconomy = EvaluateRivalRow(*Row, &Probe, nullptr) == ERHRivalTrade::Economy;
			if (!bEconomy && !C.bCleared)
			{
				Drop = TEXT("its rival no longer has an economy");
			}
			for (int32 l = 0; Drop.IsEmpty() && l < C.Lines.Num(); ++l)
			{
				const FRHConvoyLine& L = C.Lines[l];
				const int32* SlotPtr = EconIndex.Find(L.Resource);
				if (!SlotPtr) { Drop = FString::Printf(TEXT("%s is no longer a resource slot"), *L.Resource.ToString()); }
				else if (bEconomy && !Probe.Trades[*SlotPtr]) { Drop = FString::Printf(TEXT("%s is not a resource its rival trades"), *L.Resource.ToString()); }
				else if (L.Kg == 0 || FMath::Abs((int64)L.Kg) > MktMaxStoreKg) { Drop = TEXT("a line with no kg or an absurd one"); }
				else if (l > 0 && !MktNameLess(C.Lines[l - 1].Resource, L.Resource)) { Drop = TEXT("lines out of order or repeated"); }
				else if (L.FilledKg < 0 || L.FilledKg > FMath::Abs(L.Kg) || (!C.bCleared && L.FilledKg != 0)) { Drop = TEXT("a fill outside its line"); }
				else if (C.bCleared && L.Kg < 0 && L.FilledKg != -L.Kg) { Drop = TEXT("a sell that did not fill in full"); }
			}
		}
		if (!Drop.IsEmpty())
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the convoy #%d to %s is dropped (%s); its cargo and owed credit are lost"), C.Id, *Rc.Rival, *Drop);
			continue;
		}
		if (C.IsPriced() && C.bCleared && C.ReservedMcr != 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the cleared convoy #%d still held %s cr; released"), C.Id, *FormatCredits(C.ReservedMcr));
			C.ReservedMcr = 0;
		}
		LastId = C.Id;
		Named.Add(C.Rival);
		Convoys.Add(MoveTemp(C));
	}
	NextConvoyId = FMath::Max(ReadNext, LastId + 1);
	if (ReadNext <= LastId)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the next convoy Id %d is not past the highest (#%d); raised"), ReadNext, LastId);
	}
	SyncRivalEconomies(false, true);
	// A rival the block held a store for that has no economy now is skipped (its row lost its columns, or it was a test rival).
	for (const FReadRival& Rr : ReadRivals)
	{
		const FName Key(*Rr.Name);
		if (!RivalEconomies.ContainsByPredicate([Key](const FRHRivalEconomy& E) { return E.Rival == Key && E.bHasEconomy; }))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the market block holds a store for %s, which has no economy now; skipped"), *Rr.Name);
		}
	}
	// A price outside the band is clamped to it (no price is read for a slot with no Earth value: it is 0).
	for (FRHMarket& Mk : Markets)
	{
		for (int32 s = 0; s < N; ++s)
		{
			const int64 Fixed = MktClamp64(Mk.PriceMcr[s], Mk.FloorMcr[s], Mk.CeilMcr[s]);
			if (Fixed != Mk.PriceMcr[s])
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: region %d's price of %s is %lld mcr, outside [%lld, %lld]; clamped"),
					Mk.RegionId, *EconSlots[s].Name.ToString(), Mk.PriceMcr[s], Mk.FloorMcr[s], Mk.CeilMcr[s]);
				Mk.PriceMcr[s] = Fixed;
			}
		}
	}
	// The open sol: never ahead of C5d's roll outside a step (a catch-up rolls ahead only inside one); behind it, the next roll catches up.
	if (ReadChecked < 0 || ReadChecked > ProductionCheckedSol)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v55: the market's open sol %d is outside [0, C5d's sol %d]; set to %d"), ReadChecked, ProductionCheckedSol, ProductionCheckedSol);
		MarketCheckedSol = ProductionCheckedSol;
	}
	else
	{
		MarketCheckedSol = ReadChecked;
	}
	// The lifetime market sales and the reserved credit are repaired in ReconcileReservations (E5 R4), after the currency block.
	ReadMarketSalesMcr = ReadSales;
	ReadMarketReservedMcr = ReadReserved;
}

// --- E8: the 30-sol history (save v60, the 'RHE8' frame) -------------------------------------------------------------------------------------
// Per region a ring of the last MarketHistSols sols: the price in force during each sol in the market's unit then, the values the colony sold
// and bought at that sol's clearings (pre-markup), and E5's rate and unit. Written only: RollMarket's three points, Debug_SetMarketPrice and
// the load repairs write it, and no rule, price, digest or in-play log line reads it (MX14 is the proof). Its newest entry is the open sol.

namespace
{
	constexpr int32 MktHistSols = URHSimWorldSubsystem::MarketHistSols;
	constexpr int32 MktHistMaxRegions = 64;
	// The ring entry Back sols behind the newest (Back 0 = the newest).
	int32 MktHistEntry(int32 Head, int32 Back) { return (Head - 1 - Back + 2 * MktHistSols) % MktHistSols; }
}

void URHSimWorldSubsystem::HistInitEmpty(FRHMarket& Mk, int32 N)
{
	Mk.HistPrice.Init(-1, N * MktHistSols);
	Mk.HistSoldValue.Init(0, MktHistSols);
	Mk.HistBoughtValue.Init(0, MktHistSols);
	Mk.HistRateMcr.Init(0, MktHistSols);
	Mk.HistUnit.Init(0, MktHistSols);
	Mk.HistFill = 0;
	Mk.HistHead = 0;
	Mk.HistLastSol = -1;
}

void URHSimWorldSubsystem::HistAppend(FRHMarket& Mk, int32 Sol, bool bKnown) const
{
	// One entry at the head. A known sol takes the live prices and E5's unit and rate for that sol (the unit is marks from the open sol on,
	// while the mark is issued in the market); an unknown one is -1 with nothing traded and no rate. Values start at 0: the clearing adds them.
	const int32 N = Mk.PriceMcr.Num();
	const int32 E = Mk.HistHead;
	for (int32 s = 0; s < N; ++s)
	{
		Mk.HistPrice[s * MktHistSols + E] = bKnown ? Mk.PriceMcr[s] : -1;
	}
	Mk.HistSoldValue[E] = 0;
	Mk.HistBoughtValue[E] = 0;
	const bool bIssued = bKnown && CurrencyIssueSol >= 0;
	Mk.HistRateMcr[E] = bIssued ? RateMcr : 0;
	Mk.HistUnit[E] = (bIssued && Sol >= CurrencyOpenSol) ? 1 : 0;
	Mk.HistHead = (Mk.HistHead + 1) % MktHistSols;
	Mk.HistFill = FMath::Min(Mk.HistFill + 1, MktHistSols);
	Mk.HistLastSol = Sol;
}

void URHSimWorldSubsystem::HistEnsure(FRHMarket& Mk, int32 Sol) const
{
	// The top of the pass closing Sol (R3-2). An empty ring starts AT Sol, never at 0..Sol (no invented unknown entries); a ring that lags
	// (only the harness switch lets one) gets the sols it missed as unknown, then Sol live. Otherwise the entry for Sol is already there.
	if (Mk.HistPrice.Num() != Mk.PriceMcr.Num() * MktHistSols)
	{
		HistInitEmpty(Mk, Mk.PriceMcr.Num());
	}
	if (Mk.HistFill == 0)
	{
		HistAppend(Mk, Sol, true);
		return;
	}
	if (Mk.HistLastSol < Sol)
	{
		for (int32 t = FMath::Max(Mk.HistLastSol + 1, Sol - (MktHistSols - 1)); t < Sol; ++t)
		{
			HistAppend(Mk, t, false);
		}
		HistAppend(Mk, Sol, true);
	}
}

void URHSimWorldSubsystem::HistOpen(FRHMarket& Mk, int32 OpenSol) const
{
	// After a pass's clearing: the sols a long jump skips (the ring's last sol + 1 .. OpenSol - 1, only on its last pass; HistEnsure has left the
	// ring ending at the closing sol) go in as unknown, at most a ring's worth, then the open sol with the prices, unit and rate it opens with.
	if (Mk.HistPrice.Num() != Mk.PriceMcr.Num() * MktHistSols)
	{
		HistInitEmpty(Mk, Mk.PriceMcr.Num());
	}
	// An empty ring starts at the sol it is first written (R3-2: never invented unknown entries before it), so it starts at the open sol here.
	const int32 From = Mk.HistFill > 0 ? Mk.HistLastSol + 1 : OpenSol;
	for (int32 t = FMath::Max(From, OpenSol - (MktHistSols - 1)); t < OpenSol; ++t)
	{
		HistAppend(Mk, t, false);
	}
	HistAppend(Mk, OpenSol, true);
}

void URHSimWorldSubsystem::Debug_SetMarketHistoryEnabled(bool bOn)
{
	// Off empties every ring (brief B1), so a twin run with it off saves an empty ring that loads with no Warning; on starts each ring again at
	// the next roll's closing sol.
	bMarketHistoryEnabled = bOn;
	if (!bOn)
	{
		SyncRivalEconomies(false);
		for (FRHMarket& Mk : Markets) { HistInitEmpty(Mk, EconSlots.Num()); }
	}
}

int32 URHSimWorldSubsystem::GetMarketHistoryFill(int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	return Mk ? Mk->HistFill : 0;
}

int32 URHSimWorldSubsystem::Debug_GetMarketHistoryHead(int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	return Mk ? Mk->HistHead : 0;
}

int32 URHSimWorldSubsystem::GetMarketHistoryLastSol(int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	return Mk ? Mk->HistLastSol : -1;
}

bool URHSimWorldSubsystem::GetMarketHistory(FName Resource, TArray<int64>& OutPrice, int32 RegionId) const
{
	OutPrice.Reset();
	const FRHMarket* Mk = FindMarket(RegionId);
	const int32* SlotPtr = EconIndex.Find(Resource);
	if (!Mk || !SlotPtr || Mk->HistPrice.Num() != Mk->PriceMcr.Num() * MktHistSols || !Mk->PriceMcr.IsValidIndex(*SlotPtr))
	{
		return false;
	}
	for (int32 k = 0; k < Mk->HistFill; ++k)
	{
		OutPrice.Add(Mk->HistPrice[*SlotPtr * MktHistSols + MktHistEntry(Mk->HistHead, Mk->HistFill - 1 - k)]);
	}
	return true;
}

bool URHSimWorldSubsystem::GetMarketHistoryValue(int32 Back, int64& OutSoldValue, int64& OutBoughtValue, int32 RegionId) const
{
	OutSoldValue = 0;
	OutBoughtValue = 0;
	const FRHMarket* Mk = FindMarket(RegionId);
	if (!Mk || Back < 0 || Back >= Mk->HistFill || Mk->HistSoldValue.Num() != MktHistSols)
	{
		return false;
	}
	const int32 E = MktHistEntry(Mk->HistHead, Back);
	OutSoldValue = Mk->HistSoldValue[E];
	OutBoughtValue = Mk->HistBoughtValue[E];
	return true;
}

int64 URHSimWorldSubsystem::GetMarketHistoryRateMcr(int32 Back, int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	return (Mk && Back >= 0 && Back < Mk->HistFill && Mk->HistRateMcr.Num() == MktHistSols) ? Mk->HistRateMcr[MktHistEntry(Mk->HistHead, Back)] : 0;
}

uint8 URHSimWorldSubsystem::GetMarketHistoryUnit(int32 Back, int32 RegionId) const
{
	const FRHMarket* Mk = FindMarket(RegionId);
	return (Mk && Back >= 0 && Back < Mk->HistFill && Mk->HistUnit.Num() == MktHistSols) ? Mk->HistUnit[MktHistEntry(Mk->HistHead, Back)] : 0;
}

void URHSimWorldSubsystem::WriteMarketHistoryBlock(FArchive& Ar) const
{
	// v60. Inside E8's frame ('RHE8'), after every other frame. The slot table once (the count, then the names ascending, as E4's block writes
	// it); the market count; then per region in Id order: RegionId, fill, head and last sol, then every slot's 30 prices in raw entry order,
	// the 30 sold and 30 bought values, the 30 rates and the 30 unit bytes. Sized by the tables, never by history: 4 + the names + 4, then
	// 5,086 bytes a region with E4's 18 slots (16 + 18 x 240 + 3 x 240 + 30).
	SyncRivalEconomies(false); // the block always carries the synced state
	int32 SlotCount = EconSlots.Num();
	Ar << SlotCount;
	for (const FRHEconSlot& S : EconSlots)
	{
		FString Name = S.Name.ToString();
		Ar << Name;
	}
	int32 MarketCount = Markets.Num();
	Ar << MarketCount;
	for (const FRHMarket& Mk : Markets)
	{
		const bool bSized = Mk.HistPrice.Num() == SlotCount * MktHistSols && Mk.HistSoldValue.Num() == MktHistSols && Mk.HistBoughtValue.Num() == MktHistSols
			&& Mk.HistRateMcr.Num() == MktHistSols && Mk.HistUnit.Num() == MktHistSols;
		int32 Region = Mk.RegionId, Fill = bSized ? Mk.HistFill : 0, Head = bSized ? Mk.HistHead : 0, Last = bSized ? Mk.HistLastSol : -1;
		Ar << Region << Fill << Head << Last;
		for (int32 k = 0; k < SlotCount * MktHistSols; ++k) { int64 P = bSized ? Mk.HistPrice[k] : -1; Ar << P; }
		for (int32 e = 0; e < MktHistSols; ++e) { int64 V = bSized ? Mk.HistSoldValue[e] : 0; Ar << V; }
		for (int32 e = 0; e < MktHistSols; ++e) { int64 V = bSized ? Mk.HistBoughtValue[e] : 0; Ar << V; }
		for (int32 e = 0; e < MktHistSols; ++e) { int64 R = bSized ? Mk.HistRateMcr[e] : 0; Ar << R; }
		for (int32 e = 0; e < MktHistSols; ++e) { uint8 U = bSized ? Mk.HistUnit[e] : 0; Ar << U; }
	}
}

int32 URHSimWorldSubsystem::GetMarketHistoryBlockBytes() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, /*bIsPersistent*/ true);
	WriteMarketHistoryBlock(Ar);
	return Bytes.Num();
}

void URHSimWorldSubsystem::ReadMarketHistoryBlock(FArchive& Ar, const TArray<uint8>& File)
{
	// Read after the last tail reader, so E4's (and E5's) repaired state is final: it repairs with Warnings and never refuses, and never logs
	// an Error (R2-6 as REVISION 3 restates it). Every ring starts empty. In order: find the frame; parse it with its bounds (a failure starts
	// every ring empty); when E4's block started the market at its start values the rings describe prices that no longer exist and are dropped,
	// with a Warning only for a ring that held something (ruling 10); then against E4's loaded state: (a) the slot table must be the market's,
	// (b) a region with no market drops its ring, (c) a non-empty ring must end at the market's open sol, (d) a known price outside its band is
	// clamped, (e) the open sol's entry is the loaded price, (f) a sol in marks before the mark's open sol, or any when E5's block was lost,
	// reads in credits. An empty ring (fill 0, last sol -1) is valid at any open sol and logs nothing. Each ring is assigned into the synced
	// market by RegionId. No text here may contain "the market block is lost" (MX9 counts that substring in E4's line exactly).
	const int32 N = EconSlots.Num();
	SyncRivalEconomies(false);
	for (FRHMarket& Mk : Markets) { HistInitEmpty(Mk, N); }
	TArray<uint8> Payload;
	FString Why;
	if (!RHSaveFrame::Read(Ar, File, RHSaveFrame::HistoryTag, Payload, Why))
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v60: the market history is lost (%s); the history starts empty"), *Why);
		return;
	}
	struct FReadHist
	{
		int32 Region = 0, Fill = 0, Head = 0, Last = -1;
		TArray<int64> Price, Sold, Bought, Rate;
		TArray<uint8> Unit;
	};
	TArray<FString> FileSlots;
	TArray<FReadHist> Rings;
	FMemoryReader In(Payload);
	const auto Cut = [&]() { return In.IsError(); };
	int32 SlotCount = 0;
	In << SlotCount;
	bool bOk = !Cut() && SlotCount >= 0 && SlotCount <= MktMaxList;
	Why = bOk ? FString() : FString::Printf(TEXT("slot count %d"), SlotCount);
	for (int32 k = 0; bOk && k < SlotCount; ++k)
	{
		FString Name;
		bOk = RHSaveFrame::ReadName(In, Name);
		if (!bOk && Why.IsEmpty())
		{
			// ReadName fails only by setting the reader's error (a length of 0, over 256, or past the payload), so the reason is named here, not
			// left to the generic "the block is cut short".
			Why = FString::Printf(TEXT("slot name %d cannot be read"), k);
		}
		if (bOk && k > 0 && Name.Compare(FileSlots.Last(), ESearchCase::CaseSensitive) <= 0)
		{
			bOk = false;
			Why = FString::Printf(TEXT("slot name %s is out of order or repeated"), *Name);
		}
		FileSlots.Add(Name);
	}
	int32 MarketCount = 0;
	if (bOk) { In << MarketCount; bOk = !Cut() && MarketCount >= 0 && MarketCount <= MktHistMaxRegions; if (!bOk) { Why = FString::Printf(TEXT("region count %d"), MarketCount); } }
	for (int32 m = 0; bOk && m < MarketCount; ++m)
	{
		FReadHist R;
		In << R.Region << R.Fill << R.Head << R.Last;
		// A ring's oldest entry is a sol >= 0 (Last - (Fill - 1)): no writer starts one before sol 0.
		const bool bFields = R.Fill >= 0 && R.Fill <= MktHistSols && R.Head >= 0 && R.Head < MktHistSols && (R.Fill == 0 ? R.Last == -1 : R.Last >= R.Fill - 1);
		const bool bOrder = m == 0 || R.Region > Rings.Last().Region;
		bOk = !Cut() && bFields && bOrder;
		if (!bOk && !Cut())
		{
			Why = !bFields ? FString::Printf(TEXT("region %d: fill %d, head %d, last sol %d"), R.Region, R.Fill, R.Head, R.Last)
				: FString::Printf(TEXT("region %d is out of order or repeated"), R.Region);
		}
		const int64 PriceCount = (int64)SlotCount * MktHistSols;
		if (bOk && In.TotalSize() - In.Tell() < PriceCount * 8 + MktHistSols * (3 * 8 + 1))
		{
			bOk = false; // the arrays cannot fit what is left: nothing is allocated for them
		}
		for (int64 k = 0; bOk && k < PriceCount; ++k) { int64 P = 0; In << P; R.Price.Add(P); bOk = !Cut() && P >= -1; }
		for (int32 e = 0; bOk && e < MktHistSols; ++e) { int64 V = 0; In << V; R.Sold.Add(V); bOk = !Cut() && V >= 0; }
		for (int32 e = 0; bOk && e < MktHistSols; ++e) { int64 V = 0; In << V; R.Bought.Add(V); bOk = !Cut() && V >= 0; }
		for (int32 e = 0; bOk && e < MktHistSols; ++e) { int64 V = 0; In << V; R.Rate.Add(V); bOk = !Cut() && V >= 0; }
		for (int32 e = 0; bOk && e < MktHistSols; ++e) { uint8 U = 0; In << U; R.Unit.Add(U); bOk = !Cut() && U <= 1; }
		if (!bOk && Why.IsEmpty())
		{
			Why = FString::Printf(TEXT("region %d's arrays are cut short or hold a value out of range"), R.Region);
		}
		Rings.Add(MoveTemp(R));
	}
	if (bOk && In.Tell() != In.TotalSize())
	{
		bOk = false;
		Why = FString::Printf(TEXT("%lld bytes are left over"), In.TotalSize() - In.Tell());
	}
	if (!bOk)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v60: the market history is malformed (%s); the history starts empty"), Why.IsEmpty() ? TEXT("the block is cut short") : *Why);
		return;
	}
	// E4's block started the market at its start values (cross-check item 3): an intact ring would end at sols and prices that no longer exist.
	if (MarketBlockLoss != ERHMarketBlockLoss::None)
	{
		const bool bSlots = MarketBlockLoss == ERHMarketBlockLoss::SlotsReset || MarketBlockLoss == ERHMarketBlockLoss::RegionsRepeated;
		for (const FReadHist& R : Rings)
		{
			if (R.Fill > 0)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v60: the market history is dropped: %s (region %d)"),
					bSlots ? TEXT("its market block's slot table was reset") : TEXT("its market block was lost"), R.Region);
			}
		}
		return;
	}
	// (a) The slot table, exactly: a shifted slot would mislabel every price, so any difference drops every ring.
	FString SlotsWhy;
	if (FileSlots.Num() != N)
	{
		SlotsWhy = FString::Printf(TEXT("%d slots, the market %d"), FileSlots.Num(), N);
	}
	for (int32 k = 0; SlotsWhy.IsEmpty() && k < N; ++k)
	{
		if (!FileSlots[k].Equals(EconSlots[k].Name.ToString(), ESearchCase::CaseSensitive))
		{
			SlotsWhy = FString::Printf(TEXT("slot %d is %s, the market's %s"), k, *FileSlots[k], *EconSlots[k].Name.ToString());
		}
	}
	if (!SlotsWhy.IsEmpty())
	{
		if (Rings.ContainsByPredicate([](const FReadHist& R) { return R.Fill > 0; }))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v60: the market history is dropped: its slot table is not the market's (%s)"), *SlotsWhy);
		}
		return;
	}
	for (FReadHist& R : Rings)
	{
		FRHMarket* Mk = Markets.FindByPredicate([&R](const FRHMarket& X) { return X.RegionId == R.Region; });
		// (b) No market trades in its region now.
		if (!Mk)
		{
			if (R.Fill > 0)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v60: the market history of region %d is dropped: no market trades there now"), R.Region);
			}
			continue;
		}
		if (R.Fill == 0)
		{
			continue; // empty and valid at any open sol: the market's ring is already empty
		}
		// (c) The newest entry is the open sol after any roll.
		if (R.Last != MarketCheckedSol)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v60: the market history of region %d is dropped: the history ends at sol %d but the market's open sol is %d"),
				R.Region, R.Last, MarketCheckedSol);
			continue;
		}
		// (d) A known price outside its slot's band is clamped (the unknown -1 stays unknown); only the entries the ring holds are read.
		for (int32 s = 0; s < N; ++s)
		{
			int32 Clamped = 0;
			for (int32 Back = 0; Back < R.Fill; ++Back)
			{
				int64& P = R.Price[s * MktHistSols + MktHistEntry(R.Head, Back)];
				if (P >= 0 && (P < Mk->FloorMcr[s] || P > Mk->CeilMcr[s]))
				{
					P = MktClamp64(P, Mk->FloorMcr[s], Mk->CeilMcr[s]);
					++Clamped;
				}
			}
			if (Clamped > 0)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v60: the market history of region %d held %d price(s) of %s outside [%lld, %lld]; clamped"),
					R.Region, Clamped, *EconSlots[s].Name.ToString(), Mk->FloorMcr[s], Mk->CeilMcr[s]);
			}
		}
		// (e) The open sol's entry is the loaded price (E4 may have clamped one).
		const int32 OpenEntry = MktHistEntry(R.Head, 0);
		for (int32 s = 0; s < N; ++s)
		{
			int64& P = R.Price[s * MktHistSols + OpenEntry];
			if (P != Mk->PriceMcr[s])
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v60: the market history of region %d is repaired: its history entry for the open sol %d held %s at %lld mcr but the loaded price is %lld; rewritten"),
					R.Region, R.Last, *EconSlots[s].Name.ToString(), P, Mk->PriceMcr[s]);
				P = Mk->PriceMcr[s];
			}
		}
		// (f) A sol counted in marks before the mark's open sol, or any sol in marks when E5's block was lost (the mark starts unissued), reads in
		// credits: its unit and rate are reset.
		int32 UnitsReset = 0;
		for (int32 Back = 0; Back < R.Fill; ++Back)
		{
			const int32 E = MktHistEntry(R.Head, Back);
			const int32 Sol = R.Last - Back;
			if (R.Unit[E] == 1 && (bCurrencyBlockLost || (CurrencyOpenSol >= 0 && Sol < CurrencyOpenSol)))
			{
				R.Unit[E] = 0;
				R.Rate[E] = 0;
				++UnitsReset;
			}
		}
		if (UnitsReset > 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v60: the market history of region %d is repaired: %d sol(s) held in marks %s; they read in credits"),
				R.Region, UnitsReset, bCurrencyBlockLost ? TEXT("but the currency block could not be read") : *FString::Printf(TEXT("before the mark's open sol %d"), CurrencyOpenSol));
		}
		Mk->HistPrice = MoveTemp(R.Price);
		Mk->HistSoldValue = MoveTemp(R.Sold);
		Mk->HistBoughtValue = MoveTemp(R.Bought);
		Mk->HistRateMcr = MoveTemp(R.Rate);
		Mk->HistUnit = MoveTemp(R.Unit);
		Mk->HistFill = R.Fill;
		Mk->HistHead = R.Head;
		Mk->HistLastSol = R.Last;
	}
}
