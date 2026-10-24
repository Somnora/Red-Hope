#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"
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
	// The one hook. It records an amount that really moved (never a nominal one), ignores Kg <= 0 and a name with no slot, and only
	// ever writes ledger members: no sim value, branch or evaluation order depends on it (the inert twin, EC1).
	if (!bLedgerEnabled || !(Kg > 0.0))
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
	// The last block of the file (v51). Its size depends only on the slot table, never on history: the count; then per slot, in sorted
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

void URHSimWorldSubsystem::ReadLedgerBlock(FArchive& Ar)
{
	// R1: this block is never refused (nothing reads it, and it is the last thing in the file). A malformed one - an unsorted or repeated
	// name, a ring index out of range, a negative ring entry - resets the ledger to empty with a Warning; a name with no slot (a row was
	// removed) is skipped with a Warning; a slot the file does not hold starts at zero. No path logs an Error.
	// E2b: the credit block follows this one, so a malformed block is still read to its end whenever its slot count is sane (every slot is a
	// fixed size); only a bad count or a block cut short leaves the reader lost, and then the credit block is not read (bLedgerBlockLost).
	BuildEconSlots();
	int32 Count = 0;
	Ar << Count;
	const bool bCountOk = Count >= 0 && Count <= 4096 && !Ar.IsError();
	bool bBad = !bCountOk;
	bLedgerBlockLost = !bCountOk;
	FString Why = bBad ? FString::Printf(TEXT("slot count %d"), Count) : FString();
	FString Prev;
	struct FReadSlot { int32 Slot; double Open[LedgerLegs]; int64 Life[LedgerLegs]; int64 Ring[LedgerLegs][LedgerRing]; };
	TArray<FReadSlot> Read;
	for (int32 n = 0; bCountOk && n < Count; ++n)
	{
		FString Name;
		Ar << Name;
		FReadSlot R;
		R.Slot = INDEX_NONE;
		for (int32 Leg = 0; Leg < LedgerLegs; ++Leg)
		{
			Ar << R.Open[Leg] << R.Life[Leg];
			for (int32 i = 0; i < LedgerRing; ++i)
			{
				Ar << R.Ring[Leg][i];
				if (R.Ring[Leg][i] < 0)
				{
					bBad = true;
					Why = FString::Printf(TEXT("slot %s has a negative ring entry"), *Name);
				}
			}
		}
		if (Ar.IsError())
		{
			bBad = true;
			bLedgerBlockLost = true;
			Why = TEXT("the block is cut short");
			break;
		}
		if (n > 0 && Name.Compare(Prev, ESearchCase::CaseSensitive) <= 0 && !bBad)
		{
			bBad = true;
			Why = FString::Printf(TEXT("slot %s is out of order or repeated (after %s)"), *Name, *Prev);
		}
		Prev = Name;
		if (bBad)
		{
			continue; // read on to the block's end; nothing of a bad block is kept
		}
		if (const int32* Slot = EconIndex.Find(FName(*Name)))
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
	if (!bLedgerBlockLost)
	{
		Ar << Filled << Head << Checked << Lost;
		if (Ar.IsError())
		{
			bLedgerBlockLost = true;
		}
		if (!bBad && (Ar.IsError() || Filled < 0 || Filled > LedgerRing || Head < 0 || Head >= LedgerRing || Lost < 0))
		{
			bBad = true;
			Why = FString::Printf(TEXT("ring fill %d, head %d, unslotted loss %lld"), Filled, Head, Lost);
		}
	}
	if (bBad)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v51: the ledger block is malformed (%s); the ledger starts empty"), *Why);
		BuildEconSlots();
		EconCheckedSol = Checked > 0 ? Checked : ProductionCheckedSol;
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

// E2b, Earth credit: the credit block (docs/qa/2026-10-23/e2-earth-credit-design.md section 6, REVISION R5). After E1's block, the last
// thing in the file: the balance, the awarded and spent totals (int64 mcr), the ship's mass cap (double), then the number of stack-capped
// manifest rows and per row, in name order (case-sensitive ordinal), its name and its landings. Its size depends only on the manifest table.
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
}

int32 URHSimWorldSubsystem::GetCreditBlockBytes() const
{
	TArray<uint8> Bytes;
	FMemoryWriter Ar(Bytes, /*bIsPersistent*/ true);
	WriteCreditBlock(Ar);
	return Bytes.Num();
}

void URHSimWorldSubsystem::ReadCreditBlock(FArchive& Ar)
{
	// Read after teardown, so it is never refused: it repairs with a Warning (E1 R1, E2 R5). Balance and totals: a negative one is set to 0;
	// a balance that disagrees with the totals sets Spent = Awarded - Balance; a Spent that would then be negative sets Awarded = Balance and
	// Spent = 0. The cap: non-finite or negative is invalid; in Open or Completed it is 0; in AwaitingManifest or ShipInbound an invalid or
	// zero cap becomes the config value, and a valid one (a ship in flight) is kept. The counts: names must ascend; an unknown name is
	// skipped, a missing one starts at 0, and an unsorted block resets every count. No path logs an Error.
	int64 Balance = 0, Awarded = 0, Spent = 0;
	double Cap = 0.0;
	ManifestLanded.Reset();
	if (bLedgerBlockLost)
	{
		// The ledger block before this one had a bad count or was cut short: where this block starts is unknown, and reading on would turn
		// ledger bytes into credit. It is not read.
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the ledger block before the credit block could not be measured; the credit starts at 0"));
		Ar.SetError();
	}
	else
	{
		Ar << Balance << Awarded << Spent << Cap;
	}
	if (Ar.IsError())
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the credit block is cut short; the credit starts at 0"));
		Balance = Awarded = Spent = 0;
		Cap = 0.0;
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
	bCreditBlockLost = bLedgerBlockLost || Ar.IsError();
	if (bCreditBlockLost)
	{
		return;
	}
	// E3: the export block follows, so the rows are read to their end before any is judged; only a bad count or a block cut short leaves
	// the reader lost (bCreditBlockLost), and then the export block is not read.
	int32 Count = 0;
	Ar << Count;
	if (Ar.IsError() || Count < 0 || Count > 4096)
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the credit block's row count %d is invalid; every landing count starts at 0"), Count);
		bCreditBlockLost = true;
		return;
	}
	TArray<TPair<FString, int32>> Rows;
	for (int32 n = 0; n < Count; ++n)
	{
		FString Name;
		int32 Landed = 0;
		Ar << Name << Landed;
		if (Ar.IsError())
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the credit block's counts are cut short; every landing count starts at 0"));
			bCreditBlockLost = true;
			return;
		}
		Rows.Emplace(Name, Landed);
	}
	TMap<FName, int32> Read;
	for (int32 n = 0; n < Rows.Num(); ++n)
	{
		const FString& Name = Rows[n].Key;
		if (n > 0 && Name.Compare(Rows[n - 1].Key, ESearchCase::CaseSensitive) <= 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: manifest row %s is out of order or repeated (after %s); every landing count starts at 0"), *Name, *Rows[n - 1].Key);
			return;
		}
		const FRHManifestItemRow* Row = Defs ? Defs->GetManifestItem(FName(*Name)) : nullptr;
		if (!Row || Row->MaxStack <= 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v52: the credit block counts %s, which is not a stack-capped manifest row; skipped"), *Name);
			continue;
		}
		if (Rows[n].Value > 0)
		{
			Read.Add(FName(*Name), Rows[n].Value);
		}
	}
	ManifestLanded = MoveTemp(Read);
}
