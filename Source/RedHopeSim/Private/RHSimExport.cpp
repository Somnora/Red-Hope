#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "RHSaveFrame.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// E3, selling to Earth (docs/qa/2026-10-23/e3-selling-to-earth-design.md: DIRECTOR ANSWER > REVISION R1-R11 > PROFESSOR REVIEW > design).
// Q2a: Earth takes the quota. A round is still met when the colony holds its bill (HEAD's test); the bill now LEAVES at launch, drawn
// in building-Id order from uncommitted stores and then the pool, never below a life-support floor. Beside it the player may sell surplus
// of a Mars-made resource at its Earth value x Earth's political multiplier, less a launch cost per kg, paid in credits at launch (after
// the manifest's debit: sales fund the next ship). The bill and the sales share the ship's cap (Q-A (a)); a bill that would pass the cap
// plateaus at one ship. A ship that cannot load the whole bill may launch short QuotaShortLaunchSols after the met sol (Q-B (a)): it
// carries what can be loaded and no sales, and the shortfall is owed on the next round (arrears).
namespace
{
	const FName NAME_ExportOxygen(TEXT("Oxygen"));
	const FName NAME_ExportWater(TEXT("Water"));
	const FName NAME_ExportFood(TEXT("Food"));
	const FName NAME_ExportHydrogen(TEXT("Hydrogen"));
}

// --- The bills ---------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::GetRoundBills(const FRHQuotaRow& Row, int32 Round, bool bWithArrears, TArray<TPair<FName, double>>& Out) const
{
	// Rounds whose scaled total stays within the configured cap are ScaleQuotaBill's numbers, bit for bit. From the first round whose
	// scaled total passes it, every line is floor(raw line x cap / raw total), from the raw row (whole kg), the same every round after.
	Out.Reset();
	const TMap<FName, double> Raw = URHDefinitionsSubsystem::ParseResourceList(Row.Requirements);
	double ScaledTotal = 0.0;
	int64 RawTotal = 0;
	for (const auto& L : Raw)
	{
		ScaledTotal += ScaleQuotaBill(L.Value, Round);
		RawTotal += FMath::RoundToInt64(L.Value);
	}
	const int64 Cap = FMath::RoundToInt64(ShipMassCapCfgKg);
	const bool bPlateau = Cap > 0 && RawTotal > 0 && ScaledTotal > (double)Cap;
	for (const auto& L : Raw)
	{
		Out.Emplace(L.Key, bPlateau ? (double)(FMath::RoundToInt64(L.Value) * Cap / RawTotal) : ScaleQuotaBill(L.Value, Round));
	}
	if (bWithArrears && QuotaArrearsKg.Num() > 0)
	{
		// In slot order (E1's sorted slots): a billed resource's line grows; any other gets its own line after the row's.
		for (const FRHEconSlot& Slot : EconSlots)
		{
			const int32 Owed = GetQuotaArrearsKg(Slot.Name);
			if (Owed <= 0)
			{
				continue;
			}
			TPair<FName, double>* Line = Out.FindByPredicate([&Slot](const TPair<FName, double>& P) { return P.Key == Slot.Name; });
			if (Line)
			{
				Line->Value += (double)Owed;
			}
			else
			{
				Out.Emplace(Slot.Name, (double)Owed);
			}
		}
	}
}

bool URHSimWorldSubsystem::IsQuotaBillCapped() const
{
	const FRHQuotaRow* Row = Defs ? Defs->GetQuota(ActiveQuota) : nullptr;
	if (!Row)
	{
		return false;
	}
	double ScaledTotal = 0.0;
	for (const auto& L : URHDefinitionsSubsystem::ParseResourceList(Row->Requirements))
	{
		ScaledTotal += ScaleQuotaBill(L.Value, QuotaRepeatRound);
	}
	return ShipMassCapCfgKg > 0.0 && ScaledTotal > (double)FMath::RoundToInt64(ShipMassCapCfgKg);
}

void URHSimWorldSubsystem::GetLaunchBill(TArray<TPair<FName, double>>& Out) const
{
	Out.Reset();
	const FRHQuotaRow* Row = Defs ? Defs->GetQuota(ActiveQuota) : nullptr;
	if (!Row || !bEarthTakesQuota)
	{
		return;
	}
	GetRoundBills(*Row, QuotaRepeatRound, !ArrearsBelongToNextRound(), Out);
}

double URHSimWorldSubsystem::GetLaunchBillTotalKg() const
{
	TArray<TPair<FName, double>> Bill;
	GetLaunchBill(Bill);
	double Total = 0.0;
	for (const auto& B : Bill) { Total += B.Value; }
	return Total;
}

// --- What can be loaded ------------------------------------------------------------------------------------------------------------------

double URHSimWorldSubsystem::GetLaunchFloorKg(FName Resource) const
{
	// Earth never takes what the colony breathes, drinks or eats: GrowthReserveO2Sols (the C5b row, so growth and Earth respect one
	// breathing floor) x the people x each draw. Earth does not honour the city's Struct float.
	const double PeopleSols = GrowthReserveO2Sols * (double)Colonists.Num();
	if (Resource == NAME_ExportOxygen) { return PeopleSols * ColonistO2KgPerSol; }
	if (Resource == NAME_ExportWater) { return PeopleSols * ColonistWaterKgPerSol; }
	if (Resource == NAME_ExportFood) { return PeopleSols * ColonistFoodKgPerSol; }
	return 0.0;
}

double URHSimWorldSubsystem::GetLaunchLoadableKg(FName Resource) const
{
	return GetGrowthHoldingsKg(Resource) - GetLaunchFloorKg(Resource);
}

void URHSimWorldSubsystem::GetLaunchLines(TArray<FRHLaunchLine>& Out) const
{
	// The bill in its row's order (arrears after), then the export lines in slot order; a resource both billed and sold is one entry, at
	// its bill line's place.
	Out.Reset();
	TArray<TPair<FName, double>> Bill;
	GetLaunchBill(Bill);
	for (const auto& B : Bill)
	{
		FRHLaunchLine& L = Out.AddDefaulted_GetRef();
		L.Resource = B.Key;
		L.Quota = B.Value;
	}
	for (const FRHExportLine& X : ExportLines)
	{
		FRHLaunchLine* L = Out.FindByPredicate([&X](const FRHLaunchLine& P) { return P.Resource == X.Resource; });
		if (!L)
		{
			L = &Out.AddDefaulted_GetRef();
			L->Resource = X.Resource;
		}
		L->Sale += (double)X.Kg;
	}
}

int32 URHSimWorldSubsystem::GetShortLaunchSol() const
{
	return QuotaMetSol + FMath::RoundToInt32(QuotaShortLaunchSols);
}

// --- The price ---------------------------------------------------------------------------------------------------------------------------

bool URHSimWorldSubsystem::DoesEarthBuy(FName Resource) const
{
	const FRHResourceRow* Row = Defs ? Defs->GetResource(Resource) : nullptr;
	return Row && Row->EarthBuys && EconIndex.Contains(Resource);
}

int64 URHSimWorldSubsystem::GetLaunchCostMcrPerKg() const
{
	const FRHResourceRow* H2 = Defs ? Defs->GetResource(NAME_ExportHydrogen) : nullptr;
	const FRHResourceRow* O2 = Defs ? Defs->GetResource(NAME_ExportOxygen) : nullptr;
	return (FMath::RoundToInt64(LaunchH2GPerKg) * (H2 ? (int64)H2->EarthValue_mcr : 0) + FMath::RoundToInt64(LaunchO2GPerKg) * (O2 ? (int64)O2->EarthValue_mcr : 0)) / 1000;
}

int64 URHSimWorldSubsystem::GetEarthPriceMcrPerKg(FName Resource) const
{
	// Mpm = llround(multiplier x 1000), the one rounding; then integer mcr: V x Mpm / 1000 - L (floor division of non-negatives).
	const FRHResourceRow* Row = Defs ? Defs->GetResource(Resource) : nullptr;
	const int64 V = Row ? (int64)FMath::Max(0, Row->EarthValue_mcr) : 0;
	const int64 Mpm = FMath::RoundToInt64(GetRequisitionMultiplier() * 1000.0);
	return (V * Mpm) / 1000 - GetLaunchCostMcrPerKg();
}

int64 URHSimWorldSubsystem::GetExportValueMcr() const
{
	int64 Total = 0;
	for (const FRHExportLine& X : ExportLines) { Total += (int64)X.Kg * X.PriceMcrPerKg; }
	return Total;
}

// --- Export lines ------------------------------------------------------------------------------------------------------------------------

bool URHSimWorldSubsystem::AddExportLine(FName Resource, int32 Kg, FString& OutError)
{
	if (QuotaPhase != ERHQuotaPhase::AwaitingManifest)
	{
		OutError = TEXT("No manifest is open (quota not met, or ship already launched)");
		return false;
	}
	if (!DoesEarthBuy(Resource))
	{
		OutError = FString::Printf(TEXT("Earth does not buy %s"), *Resource.ToString());
		return false;
	}
	if (Kg < 1)
	{
		OutError = TEXT("A sale is at least 1 kg");
		return false;
	}
	const int64 Price = GetEarthPriceMcrPerKg(Resource);
	if (Price <= 0)
	{
		const int64 Paid = Price + GetLaunchCostMcrPerKg();
		OutError = FString::Printf(TEXT("Earth pays %s cr/kg for %s, less than the %s cr/kg the launch burns"), *FormatCredits(Paid), *Resource.ToString(), *FormatCredits(GetLaunchCostMcrPerKg()));
		return false;
	}
	const FRHExportLine* Have = ExportLines.FindByPredicate([Resource](const FRHExportLine& X) { return X.Resource == Resource; });
	const int32 Listed = Have ? Have->Kg : 0;
	double BillLine = 0.0, BillTotal = 0.0;
	TArray<TPair<FName, double>> Bill;
	GetLaunchBill(Bill);
	for (const auto& B : Bill)
	{
		BillTotal += B.Value;
		BillLine += B.Key == Resource ? B.Value : 0.0;
	}
	const double Spare = GetLaunchLoadableKg(Resource) - BillLine - (double)Listed;
	if ((double)Kg > Spare + KINDA_SMALL_NUMBER)
	{
		OutError = FString::Printf(TEXT("Only %.0f kg of %s is spare above the quota and the life-support floor"), FMath::Max(0.0, FMath::FloorToDouble(Spare + KINDA_SMALL_NUMBER)), *Resource.ToString());
		return false;
	}
	int64 LinesKg = 0;
	for (const FRHExportLine& X : ExportLines) { LinesKg += X.Kg; }
	if (BillTotal + (double)LinesKg + (double)Kg > ShipMassCapKg)
	{
		OutError = FString::Printf(TEXT("Over the ship's mass cap for cargo to Earth: %.0f kg of quota + %lld kg of sales + %d > %.0f kg"), BillTotal, LinesKg, Kg, ShipMassCapKg);
		return false;
	}
	FRHExportLine* Line = ExportLines.FindByPredicate([Resource](const FRHExportLine& X) { return X.Resource == Resource; });
	if (!Line)
	{
		FRHExportLine New;
		New.Resource = Resource;
		ExportLines.Add(New);
		ExportLines.Sort([](const FRHExportLine& A, const FRHExportLine& B) { return A.Resource.ToString().Compare(B.Resource.ToString(), ESearchCase::CaseSensitive) < 0; });
		Line = ExportLines.FindByPredicate([Resource](const FRHExportLine& X) { return X.Resource == Resource; });
	}
	Line->Kg += Kg;
	Line->PriceMcrPerKg = Price; // the whole line is re-quoted at today's price
	UE_LOG(LogRedHopeSim, Display, TEXT("Export: +%d kg %s at %s cr/kg -> %d kg, %s cr"),
		Kg, *Resource.ToString(), *FormatCredits(Price), Line->Kg, *FormatCredits((int64)Line->Kg * Line->PriceMcrPerKg));
	return true;
}

bool URHSimWorldSubsystem::RemoveExportLine(FName Resource, FString& OutError)
{
	if (QuotaPhase != ERHQuotaPhase::AwaitingManifest)
	{
		OutError = TEXT("No manifest is open (quota not met, or ship already launched)");
		return false;
	}
	const int32 Removed = ExportLines.RemoveAll([Resource](const FRHExportLine& X) { return X.Resource == Resource; });
	if (Removed == 0)
	{
		OutError = FString::Printf(TEXT("No sale of %s is listed"), *Resource.ToString());
		return false;
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Export: %s removed"), *Resource.ToString());
	return true;
}

// --- The launch --------------------------------------------------------------------------------------------------------------------------

bool URHSimWorldSubsystem::CanLaunchShip(FString& OutWhy, bool& bOutShortAllowed) const
{
	// 1 the phase (HEAD), 2 the credits (E2), the export cap (R6: only when sales are listed - the quota alone always flies), 3 the cargo:
	// every line, quota + sale, against what can be loaded, with HEAD's trade tolerance. A refusal for cargo reports whether a short launch
	// is open. Nothing here acts.
	bOutShortAllowed = false;
	if (QuotaPhase != ERHQuotaPhase::AwaitingManifest)
	{
		OutWhy = TEXT("No manifest is open");
		return false;
	}
	const int64 Cost = bCreditEnabled ? GetManifestCostMcr() : 0;
	if (Cost > CreditBalanceMcr)
	{
		OutWhy = FString::Printf(TEXT("Over budget at launch: the manifest now costs %s cr against %s cr; remove or re-price items"),
			*FormatCredits(Cost), *FormatCredits(CreditBalanceMcr));
		return false;
	}
	TArray<FRHLaunchLine> Lines;
	GetLaunchLines(Lines);
	if (ExportLines.Num() > 0)
	{
		double OutKg = 0.0;
		for (const FRHLaunchLine& L : Lines) { OutKg += L.Quota + L.Sale; }
		if (OutKg > ShipMassCapKg)
		{
			OutWhy = FString::Printf(TEXT("Over the ship's mass cap for cargo to Earth: remove %.0f kg of sales"), OutKg - ShipMassCapKg);
			return false;
		}
	}
	for (const FRHLaunchLine& L : Lines)
	{
		const double Need = L.Quota + L.Sale;
		if (Need <= 0.0)
		{
			continue;
		}
		const double Held = GetGrowthHoldingsKg(L.Resource);
		const double Floor = GetLaunchFloorKg(L.Resource);
		if (Held - Floor + KINDA_SMALL_NUMBER < Need)
		{
			OutWhy = FString::Printf(TEXT("Earth's cargo is short: %s needs %.0f kg (quota %.0f + sale %.0f); %.0f kg can be loaded (%.0f held outside sites and cut-off floors, %.0f kept for life support)"),
				*L.Resource.ToString(), Need, L.Quota, L.Sale, FMath::Max(0.0, Held - Floor), Held, Floor);
			const double All = (Defs && Defs->IsSolidResource(L.Resource) ? GetTotalSolid(L.Resource) : 0.0) + GetStock(L.Resource);
			if (All - Held > 0.5)
			{
				OutWhy += FString::Printf(TEXT("; %.0f kg more is in construction sites or on floors with no lift"), All - Held);
			}
			bOutShortAllowed = Clock && Clock->GetSol() >= GetShortLaunchSol();
			return false;
		}
	}
	return true;
}

void URHSimWorldSubsystem::FinishLaunch(const TCHAR* EarthClause, int64 Cost)
{
	CreditBalanceMcr -= Cost; // E2b: the only debit, and the only writer of the spent total
	CreditSpentMcr += Cost;
	const double TransitSols = Defs->GetConfigScalar(FName("ShipTransitSols"), 3.0);
	ShipArrivalSimSeconds = Clock->GetSimSecondsTotal() + TransitSols * URHSimClockSubsystem::SolLengthSimSeconds;
	ShipAlertStage = 0; // fresh countdown per launch
	QuotaPhase = ERHQuotaPhase::ShipInbound;
	UE_LOG(LogRedHopeSim, Display, TEXT("Ship launched: %d items, %.0f kg of a %.0f kg cap, %s cr spent, %s cr carried; %s; arrival in %.0f sols"),
		ManifestItems.Num(), GetManifestMassKg(), ShipMassCapKg, *FormatCredits(Cost), *FormatCredits(CreditBalanceMcr), EarthClause, TransitSols);
}

bool URHSimWorldSubsystem::LaunchShip(FString& OutError)
{
	bool bShort = false;
	if (!CanLaunchShip(OutError, bShort))
	{
		return false; // a refused launch changes nothing
	}
	// The draw (1.3): each line once, quota + sale, from uncommitted stores in building-Id order, outputs before inputs, then the pool;
	// recorded on E1's Out leg. Then E2's debit, then the sales through CreditPay (they fund the next ship), then the lines clear.
	TArray<FRHLaunchLine> Lines;
	GetLaunchLines(Lines);
	double QuotaKg = 0.0, SoldKg = 0.0;
	for (const FRHLaunchLine& L : Lines)
	{
		const double Need = L.Quota + L.Sale;
		if (Need > 0.0)
		{
			TakeGrowthResource(L.Resource, Need);
			NoteFlow(L.Resource, ERHLedgerLeg::Out, Need);
		}
		QuotaKg += L.Quota;
		SoldKg += L.Sale;
	}
	const int64 Cost = bCreditEnabled ? GetManifestCostMcr() : 0;
	const int64 Pay = GetExportValueMcr();
	ExportLines.Reset();
	if (bEarthTakesQuota)
	{
		QuotaArrearsKg.Reset(); // a full launch loads the arrears with the bill
	}
	const FString Clause = FString::Printf(TEXT("to Earth %.0f kg (quota %.0f, sold %.0f) for %s cr"), QuotaKg + SoldKg, QuotaKg, SoldKg, *FormatCredits(Pay));
	FinishLaunch(*Clause, Cost);
	if (Pay > 0)
	{
		CreditPay(Pay);
		SalesPaidMcr += Pay;
	}
	return true;
}

bool URHSimWorldSubsystem::LaunchShipShort(FString& OutError)
{
	// R1 (Q-B (a)): a ship that cannot load the whole bill may go QuotaShortLaunchSols after the met sol with what can be loaded, whole kg a
	// line; its export lines are dropped, not sold; the shortfall becomes the arrears the next round's bill carries (replacing any old ones:
	// the bill it was loaded against already included them).
	FString Why;
	bool bShort = false;
	if (CanLaunchShip(Why, bShort))
	{
		return LaunchShip(OutError);
	}
	const int64 Cost = bCreditEnabled ? GetManifestCostMcr() : 0;
	if (QuotaPhase != ERHQuotaPhase::AwaitingManifest || Cost > CreditBalanceMcr)
	{
		OutError = Why;
		return false;
	}
	if (!Clock || Clock->GetSol() < GetShortLaunchSol())
	{
		OutError = FString::Printf(TEXT("%s; a short launch opens at sol %d"), *Why, GetShortLaunchSol());
		return false;
	}
	TArray<TPair<FName, double>> Bill;
	GetLaunchBill(Bill);
	TMap<FName, int32> Arrears;
	double Loaded = 0.0, Billed = 0.0;
	for (const auto& B : Bill)
	{
		const double Take = FMath::Max(0.0, FMath::FloorToDouble(FMath::Min(GetLaunchLoadableKg(B.Key) + KINDA_SMALL_NUMBER, B.Value)));
		if (Take > 0.0)
		{
			TakeGrowthResource(B.Key, Take);
			NoteFlow(B.Key, ERHLedgerLeg::Out, Take);
		}
		const int32 Owed = FMath::RoundToInt32(B.Value - Take);
		if (Owed > 0)
		{
			Arrears.Add(B.Key, Owed);
		}
		Loaded += Take;
		Billed += B.Value;
	}
	ExportLines.Reset();
	if (bEarthTakesQuota)
	{
		QuotaArrearsKg = MoveTemp(Arrears);
	}
	const FString Clause = FString::Printf(TEXT("to Earth %.0f kg (quota %.0f of %.0f, short %.0f owed next round)"), Loaded, Loaded, Billed, Billed - Loaded);
	FinishLaunch(*Clause, Cost);
	return true;
}

// --- The save ----------------------------------------------------------------------------------------------------------------------------

void URHSimWorldSubsystem::WriteExportBlock(FArchive& Ar) const
{
	// After E2's credit block, the last thing in the file (v53): the lifetime sales, the slot count, then for every E1 slot in its sorted
	// order the name, the line's kg and locked price (0 and 0 with no line) and the arrears. Its size depends only on the slot table.
	int64 Sales = SalesPaidMcr;
	Ar << Sales;
	int32 Count = EconSlots.Num();
	Ar << Count;
	for (const FRHEconSlot& Slot : EconSlots)
	{
		FString Name = Slot.Name.ToString();
		const FRHExportLine* Line = ExportLines.FindByPredicate([&Slot](const FRHExportLine& X) { return X.Resource == Slot.Name; });
		int32 Kg = Line ? Line->Kg : 0;
		int64 Price = Line ? Line->PriceMcrPerKg : 0;
		int32 Owed = GetQuotaArrearsKg(Slot.Name);
		Ar << Name << Kg << Price << Owed;
	}
}

int32 URHSimWorldSubsystem::GetExportBlockBytes() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, /*bIsPersistent*/ true);
	WriteExportBlock(Ar);
	return Bytes.Num();
}

int64 URHSimWorldSubsystem::GetEarthPriceCeilingMcrPerKg(FName Resource) const
{
	// The highest quote today's table can make: the multiplier at its clamp, 1 + RequisitionEarthBonus (GetRequisitionMultiplier).
	const FRHResourceRow* Row = Defs ? Defs->GetResource(Resource) : nullptr;
	const int64 V = Row ? (int64)FMath::Max(0, Row->EarthValue_mcr) : 0;
	const int64 Mpm = FMath::RoundToInt64((1.0 + RequisitionEarthBonus) * 1000.0);
	return (V * Mpm) / 1000 - GetLaunchCostMcrPerKg();
}

void URHSimWorldSubsystem::ReadExportBlock(FArchive& Ar, const TArray<uint8>& File)
{
	// Read after teardown, so it repairs with a Warning and never refuses (E1 R1, E2 R5): names must ascend (an unsorted block drops every
	// line and every arrears count); an unknown name is skipped; a negative kg, a priced-0 line, a line for a row Earth no longer buys, or
	// any line outside an open manifest is dropped; lines past the stamped cap are dropped from the end of the list (R6); a negative arrears
	// count is 0; the sales total is clamped into [0, the awarded total]. v54: the block sits in its own frame (RHSaveFrame.h); a lost one,
	// or one with bytes left over, drops every line and arrears count; a locked price above the highest quote today's table can make is
	// clamped to it (economy-code-review.md finding 2). No path logs an Error.
	ExportLines.Reset();
	QuotaArrearsKg.Reset();
	SalesPaidMcr = 0;
	TArray<uint8> Payload;
	FString Why;
	if (!RHSaveFrame::Read(Ar, File, RHSaveFrame::ExportTag, Payload, Why))
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v54: the export block is lost (%s); no sale or arrears is read"), *Why);
		return;
	}
	FMemoryReader In(Payload);
	int64 Sales = 0;
	int32 Count = 0;
	In << Sales << Count;
	if (In.IsError() || Count < 0 || Count > 4096)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v53: the export block is cut short or its slot count %d is invalid; no sale or arrears is read"), Count);
		return;
	}
	struct FReadLine { FString Name; int32 Kg = 0; int64 Price = 0; int32 Owed = 0; };
	TArray<FReadLine> Read;
	bool bSorted = true;
	for (int32 n = 0; n < Count; ++n)
	{
		FReadLine R;
		RHSaveFrame::ReadName(In, R.Name);
		In << R.Kg << R.Price << R.Owed;
		if (In.IsError())
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v53: the export block is cut short; no sale or arrears is read"));
			return;
		}
		if (Read.Num() > 0 && R.Name.Compare(Read.Last().Name, ESearchCase::CaseSensitive) <= 0)
		{
			bSorted = false;
		}
		Read.Add(R);
	}
	if (In.Tell() != In.TotalSize())
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v54: the export block has %lld bytes left over; no sale or arrears is read"), In.TotalSize() - In.Tell());
		return;
	}
	if (Sales < 0 || Sales > CreditAwardedMcr)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v53: lifetime sales of %lld mcr are outside [0, awarded %lld]; clamped"), Sales, CreditAwardedMcr);
		Sales = FMath::Clamp<int64>(Sales, 0, CreditAwardedMcr);
	}
	SalesPaidMcr = Sales;
	if (!bSorted)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v53: the export block's names are out of order; every sale and arrears count is dropped"));
		return;
	}
	for (const FReadLine& R : Read)
	{
		const FName Name(*R.Name);
		if (!EconIndex.Contains(Name))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v53: the export block holds %s, which is no longer a resource slot; skipped"), *R.Name);
			continue;
		}
		if (R.Owed < 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v53: %s owes %d kg; set to 0"), *R.Name, R.Owed);
		}
		else if (R.Owed > 0)
		{
			QuotaArrearsKg.Add(Name, R.Owed);
		}
		if (R.Kg == 0 && R.Price == 0)
		{
			continue;
		}
		int64 Price = R.Price;
		const int64 Ceiling = GetEarthPriceCeilingMcrPerKg(Name);
		if (R.Kg > 0 && Price > Ceiling)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v54: the sale of %d kg %s is locked at %s cr/kg, above the %s cr/kg today's table can quote; clamped"),
				R.Kg, *R.Name, *FormatCredits(Price), *FormatCredits(Ceiling));
			Price = Ceiling;
		}
		const TCHAR* Drop = R.Kg < 0 ? TEXT("a negative kg")
			: (R.Kg > 0 && Price <= 0 ? TEXT("no positive price")
			: (QuotaPhase != ERHQuotaPhase::AwaitingManifest ? TEXT("no manifest is open")
			: (!DoesEarthBuy(Name) ? TEXT("Earth no longer buys it") : nullptr)));
		if (Drop || R.Kg == 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v53: the sale of %d kg %s is dropped (%s)"), R.Kg, *R.Name, Drop ? Drop : TEXT("a price with no kg"));
			continue;
		}
		FRHExportLine Line;
		Line.Resource = Name;
		Line.Kg = R.Kg;
		Line.PriceMcrPerKg = Price;
		ExportLines.Add(Line);
	}
	// R6: the lines that no longer fit the stamped cap beside the bill go, from the end of the sorted list.
	double OutKg = GetLaunchBillTotalKg();
	for (const FRHExportLine& X : ExportLines) { OutKg += (double)X.Kg; }
	while (ExportLines.Num() > 0 && OutKg > ShipMassCapKg)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v53: the sale of %d kg %s no longer fits the ship's %.0f kg cap; dropped"), ExportLines.Last().Kg, *ExportLines.Last().Resource.ToString(), ShipMassCapKg);
		OutKg -= (double)ExportLines.Last().Kg;
		ExportLines.Pop();
	}
}
