#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
#include "RHSaveFrame.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

// E1, the ledger (docs/qa/2026-10-22/e1-ledger-design.md, DIRECTOR ANSWER > REVISION > design). A second ledger beside C5d's: no
// sim rule, panel, cheat or log line reads it. C5d's ProdAccKg / ProdRing / NoteProduced / GetProductionPerSol are untouched.

void URHSimWorldSubsystem::BuildEconSlots()
{
	// Every row but Power (GridBuffer) and the abstract Ledger rows (Research, Hope: Hope is never spent and never valued), dormant
	// rows included, sorted by name string with a case-sensitive ordinal compare - never by FName index (the covert seed bug of 2026-07).
	TArray<FName> Names;
	if (Defs)
	{
		Defs->ForEachResourceRow([&Names](FName Name, const FRHResourceRow& Row)
		{
			if (Row.StorageType != TEXT("GridBuffer") && Row.StorageType != TEXT("Ledger"))
			{
				Names.Add(Name);
			}
		});
	}
	Names.Sort([](const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; });
	EconSlots.Reset();
	EconIndex.Reset();
	for (const FName& Name : Names)
	{
		EconIndex.Add(Name, EconSlots.Num());
		EconSlots.AddDefaulted_GetRef().Name = Name;
	}
	EconRingFilled = 0;
	EconRingHead = 0;
	EconCheckedSol = ProductionCheckedSol;
	UnslottedLostG = 0;
}

void URHSimWorldSubsystem::NoteFlow(FName Resource, ERHLedgerLeg Leg, double Kg)
{
	// The one hook. It records an amount that really moved (never a nominal one), ignores Kg <= 0, a non-finite amount and a name with no
	// slot, and only ever writes ledger members: no sim value, branch or evaluation order depends on it (the inert twin, EC1).
	if (!bLedgerEnabled || !(Kg > 0.0) || !FMath::IsFinite(Kg))
	{
		return;
	}
	if (const int32* Slot = EconIndex.Find(Resource))
	{
		EconSlots[*Slot].OpenKg[(int32)Leg] += Kg;
	}
}

void URHSimWorldSubsystem::RollEconomyLedger(int32 SolNow)
{
	// C5d's threshold on the monotone clock, so both bands roll on the same step. Each leg's open kg becomes whole grams in the ring and
	// the lifetime total; the sub-gram residual carries into the next sol (under half a gram), so lifetime totals never drift. A long jump
	// records empty sols after the first, at most a ring's worth.
	if (SolNow <= EconCheckedSol)
	{
		return;
	}
	const int32 Passed = FMath::Min(SolNow - EconCheckedSol, LedgerRing);
	EconCheckedSol = SolNow;
	for (int32 i = 0; i < Passed; ++i)
	{
		for (FRHEconSlot& S : EconSlots)
		{
			for (int32 Leg = 0; Leg < LedgerLegs; ++Leg)
			{
				int64 G = 0;
				if (i == 0)
				{
					G = FMath::RoundToInt64(S.OpenKg[Leg] * 1000.0);
					S.OpenKg[Leg] -= (double)G / 1000.0;
					S.LifetimeG[Leg] += G;
				}
				S.RingG[Leg][EconRingHead] = G;
			}
		}
		EconRingHead = (EconRingHead + 1) % LedgerRing;
		EconRingFilled = FMath::Min(EconRingFilled + 1, LedgerRing);
	}
}

int64 URHSimWorldSubsystem::GetLedgerLifetimeG(FName Resource, ERHLedgerLeg Leg) const
{
	const int32* Slot = EconIndex.Find(Resource);
	return Slot ? EconSlots[*Slot].LifetimeG[(int32)Leg] : 0;
}

double URHSimWorldSubsystem::GetLedgerOpenKg(FName Resource, ERHLedgerLeg Leg) const
{
	const int32* Slot = EconIndex.Find(Resource);
	return Slot ? EconSlots[*Slot].OpenKg[(int32)Leg] : 0.0;
}

int64 URHSimWorldSubsystem::GetLedgerRingG(FName Resource, ERHLedgerLeg Leg, int32 Back) const
{
	const int32* Slot = EconIndex.Find(Resource);
	if (!Slot || Back < 1 || Back > EconRingFilled)
	{
		return 0;
	}
	return EconSlots[*Slot].RingG[(int32)Leg][(EconRingHead - Back + 2 * LedgerRing) % LedgerRing];
}

int64 URHSimWorldSubsystem::GetLedgerWindowG(FName Resource, ERHLedgerLeg Leg) const
{
	int64 Sum = 0;
	for (int32 Back = 1; Back <= FMath::Min(EconRingFilled, ProductionWindowSols); ++Back)
	{
		Sum += GetLedgerRingG(Resource, Leg, Back);
	}
	return Sum;
}

int64 URHSimWorldSubsystem::GetLedgerRingSumG(FName Resource, ERHLedgerLeg Leg) const
{
	int64 Sum = 0;
	for (int32 Back = 1; Back <= EconRingFilled; ++Back)
	{
		Sum += GetLedgerRingG(Resource, Leg, Back);
	}
	return Sum;
}

void URHSimWorldSubsystem::WriteLedgerBlock(FArchive& Ar) const
{
	// The ledger block (v51; framed since v54). Its size depends only on the slot table, never on history: the count; then per slot, in sorted
	// order, the name and, per leg, the open kg, the lifetime grams and the 8 ring entries; then the ring's fill and head, the sol it last
	// rolled at, and the unslotted haul loss.
	int32 Count = EconSlots.Num();
	Ar << Count;
	for (const FRHEconSlot& S : EconSlots)
	{
		FString Name = S.Name.ToString();
		Ar << Name;
		for (int32 Leg = 0; Leg < LedgerLegs; ++Leg)
		{
			double Open = S.OpenKg[Leg];
			int64 Life = S.LifetimeG[Leg];
			Ar << Open << Life;
			for (int32 i = 0; i < LedgerRing; ++i)
			{
				int64 G = S.RingG[Leg][i];
				Ar << G;
			}
		}
	}
	int32 Filled = EconRingFilled, Head = EconRingHead, Checked = EconCheckedSol;
	int64 Lost = UnslottedLostG;
	Ar << Filled << Head << Checked << Lost;
}

int32 URHSimWorldSubsystem::GetLedgerBlockBytes() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, /*bIsPersistent*/ true);
	WriteLedgerBlock(Ar);
	return Bytes.Num();
}

void URHSimWorldSubsystem::ReadLedgerBlock(FArchive& Ar, const TArray<uint8>& File)
{
	// R1: this block is never refused (nothing reads it). A malformed one resets the ledger to empty with a Warning, rolled at C5d's sol: an
	// unsorted or repeated name (a repeat is FName equality, which ignores case), a ring index out of range, a negative ring entry or
	// lifetime, an open kg that is not finite or is below -0.001 kg (a real residual sits within half a gram of 0), a checked sol other than
	// C5d's (both roll in lockstep from one SolNow), or bytes left over. A name with no slot (a row was removed) is skipped with a Warning; a
	// slot the file does not hold starts at zero. v54: the block sits in its own frame (RHSaveFrame.h), so a lost or malformed one never
	// moves where the credit block after it is read. No path logs an Error.
	BuildEconSlots();
	TArray<uint8> Payload;
	FString Why;
	if (!RHSaveFrame::Read(Ar, File, RHSaveFrame::LedgerTag, Payload, Why))
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v54: the ledger block is lost (%s); the ledger starts empty"), *Why);
		return;
	}
	FMemoryReader In(Payload);
	int32 Count = 0;
	In << Count;
	bool bBad = In.IsError() || Count < 0 || Count > 4096;
	Why = bBad ? FString::Printf(TEXT("slot count %d"), Count) : FString();
	FString Prev;
	TSet<FName> Seen;
	struct FReadSlot { int32 Slot; double Open[LedgerLegs]; int64 Life[LedgerLegs]; int64 Ring[LedgerLegs][LedgerRing]; };
	TArray<FReadSlot> Read;
	for (int32 n = 0; !bBad && n < Count; ++n)
	{
		FString Name;
		if (!RHSaveFrame::ReadName(In, Name))
		{
			bBad = true;
			Why = FString::Printf(TEXT("slot %d's name cannot be read"), n);
			break;
		}
		FReadSlot R;
		R.Slot = INDEX_NONE;
		for (int32 Leg = 0; Leg < LedgerLegs; ++Leg)
		{
			In << R.Open[Leg] << R.Life[Leg];
			for (int32 i = 0; i < LedgerRing; ++i)
			{
				In << R.Ring[Leg][i];
			}
		}
		if (In.IsError())
		{
			bBad = true;
			Why = TEXT("the block is cut short");
			break;
		}
		for (int32 Leg = 0; Leg < LedgerLegs && !bBad; ++Leg)
		{
			if (!FMath::IsFinite(R.Open[Leg]) || R.Open[Leg] < -0.001)
			{
				bBad = true;
				Why = FString::Printf(TEXT("slot %s has an open tally of %g kg"), *Name, R.Open[Leg]);
			}
			else if (R.Life[Leg] < 0)
			{
				bBad = true;
				Why = FString::Printf(TEXT("slot %s has a negative lifetime"), *Name);
			}
			for (int32 i = 0; i < LedgerRing && !bBad; ++i)
			{
				if (R.Ring[Leg][i] < 0)
				{
					bBad = true;
					Why = FString::Printf(TEXT("slot %s has a negative ring entry"), *Name);
				}
			}
		}
		const FName Key(*Name);
		if (!bBad && n > 0 && Name.Compare(Prev, ESearchCase::CaseSensitive) <= 0)
		{
			bBad = true;
			Why = FString::Printf(TEXT("slot %s is out of order or repeated (after %s)"), *Name, *Prev);
		}
		else if (!bBad && Seen.Contains(Key))
		{
			bBad = true;
			Why = FString::Printf(TEXT("slot %s repeats an earlier name"), *Name);
		}
		Seen.Add(Key);
		Prev = Name;
		if (bBad)
		{
			break;
		}
		if (const int32* Slot = EconIndex.Find(Key))
		{
			R.Slot = *Slot;
			Read.Add(R);
		}
		else
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v51: the ledger holds %s, which is no longer a resource row; skipped"), *Name);
		}
	}
	int32 Filled = 0, Head = 0, Checked = 0;
	int64 Lost = 0;
	if (!bBad)
	{
		In << Filled << Head << Checked << Lost;
		if (In.IsError())
		{
			bBad = true;
			Why = TEXT("the block is cut short");
		}
		else if (Filled < 0 || Filled > LedgerRing || Head < 0 || Head >= LedgerRing || Lost < 0)
		{
			bBad = true;
			Why = FString::Printf(TEXT("ring fill %d, head %d, unslotted loss %lld"), Filled, Head, Lost);
		}
		else if (Checked != ProductionCheckedSol)
		{
			bBad = true;
			Why = FString::Printf(TEXT("it rolled at sol %d, C5d's ledger at sol %d"), Checked, ProductionCheckedSol);
		}
		else if (In.Tell() != In.TotalSize())
		{
			bBad = true;
			Why = FString::Printf(TEXT("%lld bytes are left over"), In.TotalSize() - In.Tell());
		}
	}
	if (bBad)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v51: the ledger block is malformed (%s); the ledger starts empty"), *Why);
		BuildEconSlots(); // rolled at ProductionCheckedSol, never at a value read from a bad block
		return;
	}
	for (const FReadSlot& R : Read)
	{
		FRHEconSlot& S = EconSlots[R.Slot];
		for (int32 Leg = 0; Leg < LedgerLegs; ++Leg)
		{
			S.OpenKg[Leg] = R.Open[Leg];
			S.LifetimeG[Leg] = R.Life[Leg];
			for (int32 i = 0; i < LedgerRing; ++i)
			{
				S.RingG[Leg][i] = R.Ring[Leg][i];
			}
		}
	}
	EconRingFilled = Filled;
	EconRingHead = Head;
	EconCheckedSol = Checked;
	UnslottedLostG = Lost;
}

// E2b, Earth credit: the credit block (docs/qa/2026-10-23/e2-earth-credit-design.md section 6, REVISION R5). After E1's block: the
// balance, the awarded and spent totals (int64 mcr), the ship's mass cap (double), then the number of stack-capped manifest rows and per
// row, in name order (case-sensitive ordinal), its name and its landings; v54 adds the manifest's locked prices (the count, then each item's
// price in manifest order). The block sits in its own frame (RHSaveFrame.h).
namespace
{
	void GatherCappedRows(const URHDefinitionsSubsystem* Defs, TArray<FName>& Out)
	{
		Out.Reset();
		if (Defs)
		{
			Defs->ForEachManifestItem([&Out](FName Name, const FRHManifestItemRow& Row) { if (Row.MaxStack > 0) { Out.Add(Name); } });
		}
		Out.Sort([](const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; });
	}
}

void URHSimWorldSubsystem::WriteCreditBlock(FArchive& Ar) const
{
	int64 Balance = CreditBalanceMcr, Awarded = CreditAwardedMcr, Spent = CreditSpentMcr;
	double Cap = ShipMassCapKg;
	Ar << Balance << Awarded << Spent << Cap;
	TArray<FName> Capped;
	GatherCappedRows(Defs, Capped);
	int32 Count = Capped.Num();
	Ar << Count;
	for (const FName& Name : Capped)
	{
		FString Text = Name.ToString();
		int32 Landed = GetManifestLanded(Name);
		Ar << Text << Landed;
	}
	int32 Items = ManifestPricesMcr.Num();
	Ar << Items;
	for (int64 Price : ManifestPricesMcr)
	{
		Ar << Price;
	}
}

int32 URHSimWorldSubsystem::GetCreditBlockBytes() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, /*bIsPersistent*/ true);
	WriteCreditBlock(Ar);
	return Bytes.Num();
}

void URHSimWorldSubsystem::ReadCreditBlock(FArchive& Ar, const TArray<uint8>& File)
{
	// Read after teardown, so it is never refused: it repairs with a Warning (E1 R1, E2 R5). Balance and totals: a lost block starts them at
	// 0; one above CreditMaxMcr sets all three to 0 (v54: no real game comes near it, so it is a broken file, never a balance to keep); a
	// negative one is set to 0; a balance that disagrees with the totals sets Spent = Awarded - Balance; a Spent that would then be negative
	// sets Awarded = Balance and Spent = 0. The cap: non-finite or negative is invalid; in Open or Completed it is 0; in AwaitingManifest or
	// ShipInbound an invalid or zero cap becomes the config value, and a valid one (a ship in flight) is kept. The counts: names must ascend;
	// an unknown name is skipped, a missing one starts at 0, and an unsorted block resets every count. The locked prices (v54): a count other
	// than the manifest's, a price outside [0, CreditMaxMcr], bytes left over or a lost block price every item aboard from today's table
	// (RemoveManifestItem stays open whatever they say). No path logs an Error.
	int64 Balance = 0, Awarded = 0, Spent = 0;
	double Cap = 0.0;
	ManifestLanded.Reset();
	TArray<uint8> Payload;
	FString Why;
	const bool bFound = RHSaveFrame::Read(Ar, File, RHSaveFrame::CreditTag, Payload, Why);
	FMemoryReader In(Payload);
	if (!bFound)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v54: the credit block is lost (%s); the credit starts at 0"), *Why);
		In.SetError();
	}
	else
	{
		In << Balance << Awarded << Spent << Cap;
		if (In.IsError())
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the credit block is cut short; the credit starts at 0"));
		}
	}
	if (In.IsError())
	{
		Balance = Awarded = Spent = 0;
		Cap = 0.0;
	}
	if (Balance > CreditMaxMcr || Awarded > CreditMaxMcr || Spent > CreditMaxMcr)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v54: a credit total above %s cr (balance %lld, awarded %lld, spent %lld mcr) is not a real one; all three are set to 0"),
			*FormatCredits(CreditMaxMcr), Balance, Awarded, Spent);
		Balance = Awarded = Spent = 0;
	}
	if (Balance < 0 || Awarded < 0 || Spent < 0)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: a negative credit total (balance %lld, awarded %lld, spent %lld mcr) is set to 0"), Balance, Awarded, Spent);
		Balance = FMath::Max<int64>(0, Balance);
		Awarded = FMath::Max<int64>(0, Awarded);
		Spent = FMath::Max<int64>(0, Spent);
	}
	if (Balance != Awarded - Spent)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the balance %lld mcr disagrees with awarded %lld - spent %lld; spent is set to awarded - balance"), Balance, Awarded, Spent);
		Spent = Awarded - Balance;
		if (Spent < 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: that leaves spent negative; awarded is set to the balance and spent to 0"));
			Awarded = Balance;
			Spent = 0;
		}
	}
	CreditBalanceMcr = Balance;
	CreditAwardedMcr = Awarded;
	CreditSpentMcr = Spent;
	const bool bShipOpen = QuotaPhase == ERHQuotaPhase::AwaitingManifest || QuotaPhase == ERHQuotaPhase::ShipInbound;
	const bool bCapValid = FMath::IsFinite(Cap) && Cap >= 0.0;
	if (!bShipOpen)
	{
		if (!bCapValid || Cap != 0.0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: a mass cap of %g kg with no ship open is set to 0"), Cap);
		}
		Cap = 0.0;
	}
	else if (!bCapValid || Cap == 0.0)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the open ship's mass cap %g kg is invalid; it is set to the config value %.0f kg"), Cap, ShipMassCapCfgKg);
		Cap = ShipMassCapCfgKg;
	}
	ShipMassCapKg = Cap;

	// The landing counts: read to their end before any is judged.
	TArray<TPair<FString, int32>> Rows;
	if (!In.IsError())
	{
		int32 Count = 0;
		In << Count;
		if (In.IsError() || Count < 0 || Count > 4096)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the credit block's row count %d is invalid; every landing count starts at 0"), Count);
			In.SetError();
		}
		for (int32 n = 0; !In.IsError() && n < Count; ++n)
		{
			FString Name;
			int32 Landed = 0;
			RHSaveFrame::ReadName(In, Name);
			In << Landed;
			if (In.IsError())
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the credit block's counts are cut short; every landing count starts at 0"));
				Rows.Reset();
				break;
			}
			Rows.Emplace(Name, Landed);
		}
	}
	TMap<FName, int32> Landings;
	for (int32 n = 0; n < Rows.Num(); ++n)
	{
		const FString& Name = Rows[n].Key;
		if (n > 0 && Name.Compare(Rows[n - 1].Key, ESearchCase::CaseSensitive) <= 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: manifest row %s is out of order or repeated (after %s); every landing count starts at 0"), *Name, *Rows[n - 1].Key);
			Landings.Reset();
			break;
		}
		const FRHManifestItemRow* Row = Defs ? Defs->GetManifestItem(FName(*Name)) : nullptr;
		if (!Row || Row->MaxStack <= 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the credit block counts %s, which is not a stack-capped manifest row; skipped"), *Name);
			continue;
		}
		if (Rows[n].Value > 0)
		{
			Landings.Add(FName(*Name), Rows[n].Value);
		}
	}
	ManifestLanded = MoveTemp(Landings);

	// v54 (economy-code-review.md finding 1): the price each item aboard was locked at when it was added.
	TArray<int64> Prices;
	bool bPricesOk = false;
	if (!In.IsError())
	{
		int32 Items = -1;
		In << Items;
		if (!In.IsError() && Items == ManifestItems.Num())
		{
			Prices.SetNum(Items);
			bPricesOk = true;
			for (int64& Price : Prices)
			{
				In << Price;
				bPricesOk &= Price >= 0 && Price <= CreditMaxMcr;
			}
			bPricesOk &= !In.IsError() && In.Tell() == In.TotalSize();
		}
		if (!bPricesOk)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v54: the manifest's locked prices (%d for %d items aboard) are invalid; each item is priced from today's table"), Items, ManifestItems.Num());
		}
	}
	else if (ManifestItems.Num() > 0)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v54: the manifest's locked prices were not read; its %d items are priced from today's table"), ManifestItems.Num());
	}
	if (!bPricesOk)
	{
		Prices.Reset();
		for (const FName& Item : ManifestItems)
		{
			Prices.Add(GetManifestTablePriceMcr(Item));
		}
	}
	ManifestPricesMcr = MoveTemp(Prices);
}
