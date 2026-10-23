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
	BuildEconSlots();
	int32 Count = 0;
	Ar << Count;
	bool bBad = Count < 0 || Count > 4096 || Ar.IsError();
	FString Why = bBad ? FString::Printf(TEXT("slot count %d"), Count) : FString();
	FString Prev;
	struct FReadSlot { int32 Slot; double Open[LedgerLegs]; int64 Life[LedgerLegs]; int64 Ring[LedgerLegs][LedgerRing]; };
	TArray<FReadSlot> Read;
	for (int32 n = 0; !bBad && n < Count; ++n)
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
			Why = TEXT("the block is cut short");
			break;
		}
		if (n > 0 && Name.Compare(Prev, ESearchCase::CaseSensitive) <= 0)
		{
			bBad = true;
			Why = FString::Printf(TEXT("slot %s is out of order or repeated (after %s)"), *Name, *Prev);
			break;
		}
		Prev = Name;
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
	if (!bBad)
	{
		Ar << Filled << Head << Checked << Lost;
		if (Ar.IsError() || Filled < 0 || Filled > LedgerRing || Head < 0 || Head >= LedgerRing || Lost < 0)
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
