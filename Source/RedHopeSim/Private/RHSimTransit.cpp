#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHAgentFragments.h"
#include "RHRobotMovement.h"
#include "Algo/Reverse.h"
#include "RHSaveFrame.h"
#include "Serialization/MemoryReader.h"

// D4 (going deep, gate 4): transit - lifts, capacity and robots below (docs/qa/2026-10-21/d4-transit-design.md, the
// REVISION and the director's answers Q-A (a), Q-B (a)). The gate is the colony's first fitted lift car: with none, every
// function here returns before it touches anything, and every caller keeps its D3 lines. Everything a capacity decision
// reads is an integer, written in any order to a per-sol ledger, and frozen at a world-side threshold; everything a robot
// decides is robot-local (its plan, its hold, its ride), so no outcome depends on which robot the processor reaches first.

namespace
{
	const FName NAME_StructTransit(TEXT("Struct"));
	constexpr double TransitSolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;

	int32 TransitSolOf(double Seconds)
	{
		return FMath::FloorToInt32(Seconds / TransitSolLen);
	}

	FIntPoint TransitWholeCm(const FVector& Cm)
	{
		return FIntPoint(FMath::RoundToInt32(Cm.X), FMath::RoundToInt32(Cm.Y));
	}

	// The lattice's eight steps: the four orthogonals first, then the diagonals (a diagonal needs both orthogonals carved).
	const FIntPoint TransitSteps[8] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1), FIntPoint(1, 1), FIntPoint(-1, 1), FIntPoint(1, -1), FIntPoint(-1, -1) };
	constexpr int64 TransitStepCm[8] = { 1000, 1000, 1000, 1000, 1414, 1414, 1414, 1414 };

	struct FTransitHeapItem
	{
		int64 Key = 0;
		int32 Node = 0;
	};
	struct FTransitHeapLess
	{
		bool operator()(const FTransitHeapItem& A, const FTransitHeapItem& B) const { return A.Key != B.Key ? A.Key < B.Key : A.Node < B.Node; }
	};

	// A floor's lattice point -> ordinal as a flat grid over the cells' bounding box: the same answers as FRHFloor::FindOrdinal (cells have no
	// duplicates) without a hash per lookup. A box far larger than its cells (a long thin tunnel and a far cavern) keeps the hash.
	struct FTransitFlatIndex
	{
		const FRHFloor* Floor = nullptr;
		TArray<int32>* Grid = nullptr; // the caller's reused buffer
		bool bFlat = false;
		int32 MinX = 0, MinY = 0, W = 0, H = 0;

		void Build(const FRHFloor* InFloor, TArray<int32>& InGrid)
		{
			Floor = InFloor;
			Grid = &InGrid;
			bFlat = false;
			const TArray<FIntPoint>& Cells = Floor->GetCells();
			if (Cells.Num() == 0)
			{
				return;
			}
			int32 MaxX = Cells[0].X, MaxY = Cells[0].Y;
			MinX = MaxX;
			MinY = MaxY;
			for (const FIntPoint& P : Cells)
			{
				MinX = FMath::Min(MinX, P.X); MaxX = FMath::Max(MaxX, P.X);
				MinY = FMath::Min(MinY, P.Y); MaxY = FMath::Max(MaxY, P.Y);
			}
			const int64 Area = ((int64)MaxX - MinX + 1) * ((int64)MaxY - MinY + 1);
			if (Area > (int64)Cells.Num() * 64 + 4096)
			{
				return;
			}
			W = MaxX - MinX + 1;
			H = MaxY - MinY + 1;
			static_assert(INDEX_NONE == -1, "the grid is filled byte-wise");
			Grid->Reset();
			Grid->SetNumUninitialized((int32)Area, EAllowShrinking::No);
			FMemory::Memset(Grid->GetData(), 0xFF, (SIZE_T)Area * sizeof(int32));
			for (int32 c = 0; c < Cells.Num(); ++c)
			{
				(*Grid)[(Cells[c].Y - MinY) * W + (Cells[c].X - MinX)] = c;
			}
			bFlat = true;
		}

		int32 Find(int32 X, int32 Y) const
		{
			if (!bFlat)
			{
				return Floor->FindOrdinal(FIntPoint(X, Y));
			}
			const int64 Dx = (int64)X - MinX, Dy = (int64)Y - MinY;
			return (Dx < 0 || Dy < 0 || Dx >= W || Dy >= H) ? (int32)INDEX_NONE : (*Grid)[(int32)(Dy * W + Dx)];
		}
	};

	// PlanDeepRoute's Dijkstra state: raw arrays (the planner runs thousands of times a frame) and a binary heap ordered by FTransitHeapLess,
	// (distance, node index). The order is a strict total order and every (distance, node) pair is pushed at most once (a push needs a strict
	// improvement), so the sequence of nodes popped does not depend on how the heap is laid out.
	struct FTransitSearch
	{
		int64* Dist = nullptr;
		int32* Prev = nullptr;
		bool* Done = nullptr;
		TArray<FTransitHeapItem, TInlineAllocator<256>> Heap;
		int32 U = 0;       // the node being settled
		int64 DistU = 0;   // its distance

		void Push(int64 Key, int32 Node)
		{
			const FTransitHeapLess Less;
			const FTransitHeapItem Item{ Key, Node };
			int32 I = Heap.AddUninitialized(1);
			FTransitHeapItem* H = Heap.GetData();
			while (I > 0)
			{
				const int32 Parent = (I - 1) >> 1;
				if (!Less(Item, H[Parent]))
				{
					break;
				}
				H[I] = H[Parent];
				I = Parent;
			}
			H[I] = Item;
		}

		FTransitHeapItem Pop()
		{
			const FTransitHeapLess Less;
			FTransitHeapItem* H = Heap.GetData();
			const FTransitHeapItem Top = H[0];
			const int32 N = Heap.Num() - 1;
			if (N > 0)
			{
				const FTransitHeapItem Last = H[N];
				int32 I = 0;
				for (;;)
				{
					int32 Child = 2 * I + 1;
					if (Child >= N)
					{
						break;
					}
					if (Child + 1 < N && Less(H[Child + 1], H[Child]))
					{
						++Child;
					}
					if (!Less(H[Child], Last))
					{
						break;
					}
					H[I] = H[Child];
					I = Child;
				}
				H[I] = Last;
			}
			Heap.SetNumUninitialized(N, EAllowShrinking::No);
			return Top;
		}

		FORCEINLINE void Relax(int32 V, int64 C)
		{
			if (C != MAX_int64 && !Done[V] && DistU + C < Dist[V])
			{
				Dist[V] = DistU + C;
				Prev[V] = U;
				Push(Dist[V], V);
			}
		}
	};

	// BuildLatticeField's Dijkstra over a finished neighbour table (see there). The heap is the caller's, reused field after field: the pops
	// are fixed by the (distance, ordinal) order alone, never by the heap's history, so a reused heap pops the same sequence.
	void RunLatticeField(const TArray<int32>& Neighbours, int32 N, int32 Root, TArray<int64>& OutDist, TArray<int32>& OutToward, TArray<FTransitHeapItem>& Heap)
	{
		OutDist.Init(MAX_int64, N);
		OutToward.Init(INDEX_NONE, N);
		Heap.Reset();
		OutDist[Root] = 0;
		Heap.HeapPush({ 0, Root }, FTransitHeapLess());
		while (Heap.Num() > 0)
		{
			FTransitHeapItem Top;
			Heap.HeapPop(Top, FTransitHeapLess(), EAllowShrinking::No);
			if (Top.Key != OutDist[Top.Node])
			{
				continue;
			}
			for (int32 k = 0; k < 8; ++k)
			{
				const int32 O = Neighbours[Top.Node * 8 + k];
				if (O == INDEX_NONE)
				{
					continue;
				}
				const int64 D = Top.Key + TransitStepCm[k];
				if (D < OutDist[O])
				{
					OutDist[O] = D;
					OutToward[O] = Top.Node;
					Heap.HeapPush({ D, O }, FTransitHeapLess());
				}
			}
		}
	}
}

// --- The ledger and the waits ---

double URHSimWorldSubsystem::GetTransitRobotLagSeconds()
{
	// REVISION item 2(d): the crossing sub-step can end up to one sub-step past the boundary and its frame can run 31 more.
	return (double)(URHSimClockSubsystem::MaxSubStepsPerFrame + 1) * (double)URHSimClockSubsystem::SubStepSeconds;
}

int32 URHSimWorldSubsystem::GetRobotWaitSec(int32 ShaftId, double Stamp) const
{
	if (!TransitLedgers.IsValidIndex(ShaftId))
	{
		return 0;
	}
	// A sub-step stamped at the boundary + the lag or later belongs to a frame after the crossing one, so the world has
	// rolled in either batch order; one inside the window reads the previous sol's wait in either order (PrevWaitSec once
	// the world has rolled, WaitSec before it - both hold the previous sol's value).
	const FRHTransitLedger& L = TransitLedgers[ShaftId];
	const int32 RobotSol = TransitSolOf(Stamp - GetTransitRobotLagSeconds());
	return RobotSol == TransitCheckedSol ? L.WaitSec : L.PrevWaitSec;
}

int64 URHSimWorldSubsystem::GetShaftCapacityPlaces(int32 ShaftId) const
{
	if (!Shafts.IsValidIndex(ShaftId))
	{
		return 0;
	}
	const int32 Cars = Shafts[ShaftId].LiftCars;
	return Cars > 0 ? (int64)Cars * (int64)LiftPlacesPerSol : (int64)HoistPlacesPerSol;
}

int32 URHSimWorldSubsystem::ComputeTransitWaitSec(int64 Load, int64 Cap) const
{
	if (Cap <= 0 || Load <= Cap)
	{
		return 0;
	}
	const int64 Wait = ((Load - Cap) * (int64)WaitSecAtDouble + Cap - 1) / Cap; // ceil, in int64
	return (int32)FMath::Min<int64>(Wait, (int64)MaxWaitSec);
}

// --- D6e: lift and tram power (the short-ms rings, the served share, the served capacity) ---

namespace
{
	constexpr int32 TransitShortMsCap = 1200000; // one sol of ms: a slot never holds more (SP3)

	// A ring's ms for SolPrev: 0 before sol 0 (C++'s -1 % 2 is -1, never a slot) and when the slot holds another sol.
	int32 TransitShortMsOf(const int32 (&Sol)[2], const int32 (&Ms)[2], int32 SolPrev)
	{
		if (SolPrev < 0)
		{
			return 0;
		}
		const int32 Slot = SolPrev % 2;
		return Sol[Slot] == SolPrev ? Ms[Slot] : 0;
	}

	// A lift's served capacity: the hoist's at Served 0, the cars' at 1000, in int64. At 1000 this is Nominal exactly.
	int64 TransitServedLiftPlaces(int64 Hoist, int64 Nominal, int32 Served)
	{
		return Hoist + (Nominal - Hoist) * (int64)Served / 1000;
	}
}

void URHSimWorldSubsystem::NoteTransitShortMs(uint8 Kind, int32 Index, float SubDt, double StepEndS)
{
	// The ChargeTransit pattern on a 2-slot ring: a slot holding an older sol is reset first; one holding a later sol is a write two sols late.
	int32* Sol = nullptr;
	int32* Ms = nullptr;
	if (Kind == 2 && TransitLedgers.IsValidIndex(Index))
	{
		Sol = TransitLedgers[Index].ShortSol;
		Ms = TransitLedgers[Index].ShortMs;
	}
	else if (Kind == 3)
	{
		for (FRHTramLine& Line : TramLines)
		{
			if (Line.Id == Index)
			{
				Sol = Line.ShortSol;
				Ms = Line.ShortMs;
				break;
			}
		}
	}
	const int32 StepSol = TransitSolOf(StepEndS);
	if (!Sol || StepSol < 0)
	{
		return;
	}
	const int32 Slot = StepSol % 2;
	if (Sol[Slot] < StepSol)
	{
		Sol[Slot] = StepSol;
		Ms[Slot] = 0;
	}
	if (Sol[Slot] != StepSol)
	{
		// No writer can stamp one (the stamp is the step's end, sim time); say so.
		UE_LOG(LogRedHopeSim, Warning, TEXT("Transit power: a short step of %s %d stamped sol %d found its slot holding sol %d - dropped"),
			Kind == 2 ? TEXT("shaft") : TEXT("tram line"), Index, StepSol, Sol[Slot]);
		return;
	}
	Ms[Slot] = (int32)FMath::Min<int64>((int64)Ms[Slot] + FMath::RoundToInt64((double)SubDt * 1000.0), (int64)TransitShortMsCap);
}

int32 URHSimWorldSubsystem::ServedPermilleAt(uint8 Kind, int32 Index, int32 SolPrev) const
{
	int32 ShortMs = 0;
	if (Kind == 2 && TransitLedgers.IsValidIndex(Index))
	{
		ShortMs = TransitShortMsOf(TransitLedgers[Index].ShortSol, TransitLedgers[Index].ShortMs, SolPrev);
	}
	else if (Kind == 3)
	{
		if (const FRHTramLine* Line = FindTramLine(Index))
		{
			ShortMs = TransitShortMsOf(Line->ShortSol, Line->ShortMs, SolPrev);
		}
	}
	return 1000 - FMath::Min(1000, ShortMs / 1200);
}

int32 URHSimWorldSubsystem::GetShaftServedPermille(int32 ShaftId) const
{
	return GetLiftCars(ShaftId) > 0 ? ServedPermilleAt(2, ShaftId, TransitCheckedSol - 1) : 1000;
}

int64 URHSimWorldSubsystem::GetShaftServedCapacityPlaces(int32 ShaftId) const
{
	const int32 Cars = GetLiftCars(ShaftId);
	if (Cars <= 0)
	{
		return GetShaftCapacityPlaces(ShaftId); // the hoist (0 for a shaft that does not exist)
	}
	return TransitServedLiftPlaces((int64)HoistPlacesPerSol, GetShaftCapacityPlaces(ShaftId), GetShaftServedPermille(ShaftId));
}

int32 URHSimWorldSubsystem::GetTramServedPermille(int32 LineId) const
{
	return FindTramLine(LineId) ? ServedPermilleAt(3, LineId, TransitCheckedSol - 1) : 1000;
}

int64 URHSimWorldSubsystem::GetTramServedCapacityPlaces(int32 LineId) const
{
	// SP1: at least 1 place, so a dark sol never reads as an empty line (ComputeTransitWaitSec returns 0 for a capacity of 0).
	return FMath::Max<int64>(1, (int64)TramPlacesPerSol * GetTramServedPermille(LineId) / 1000);
}

int32 URHSimWorldSubsystem::Debug_GetTransitShortMs(int32 Kind, int32 Id, int32 Sol) const
{
	if (Kind == 2 && TransitLedgers.IsValidIndex(Id))
	{
		return TransitShortMsOf(TransitLedgers[Id].ShortSol, TransitLedgers[Id].ShortMs, Sol);
	}
	const FRHTramLine* Line = Kind == 3 ? FindTramLine(Id) : nullptr;
	return Line ? TransitShortMsOf(Line->ShortSol, Line->ShortMs, Sol) : 0;
}

int64 URHSimWorldSubsystem::GetCargoPlaces(double Kg) const
{
	const int64 Whole = FMath::Max<int64>(0, FMath::RoundToInt64(Kg));
	const int64 Per = FMath::Max(1, LiftKgPerPlace);
	return (Whole + Per - 1) / Per;
}

int64 URHSimWorldSubsystem::GetShaftPeopleLoad(int32 ShaftId) const
{
	// REVISION item 8: 2 x the shaft's commuters x the ramp in permille, floored once per shaft. 0 (and the flows untouched)
	// at 60 people or fewer.
	if (!bHasTransit)
	{
		return 0;
	}
	const double Ramp = GetCommuteRamp();
	if (Ramp <= 0.0)
	{
		return 0;
	}
	const int64 RampPermille = FMath::RoundToInt64(Ramp * 1000.0);
	const FRHCommuteFlows& F = EnsureCommuteFlows();
	const int64 People = F.ShaftPeople.IsValidIndex(ShaftId) ? (int64)F.ShaftPeople[ShaftId] : 0;
	return 2 * People * RampPermille / 1000;
}

int64 URHSimWorldSubsystem::GetShaftCargoLoad(int32 ShaftId) const
{
	// The cargo part of the current sol's wait: the slot of sol TransitCheckedSol - 2, when its tag matches.
	if (!TransitLedgers.IsValidIndex(ShaftId) || TransitCheckedSol < 2)
	{
		return 0;
	}
	const int32 CargoSol = TransitCheckedSol - 2;
	const FRHTransitLedger& L = TransitLedgers[ShaftId];
	return L.Sol[CargoSol % 3] == CargoSol ? (int64)L.Places[CargoSol % 3] : 0;
}

void URHSimWorldSubsystem::ChargeTransit(int32 ShaftId, int64 Places, int64 Kg, double Stamp)
{
	// A 0-place event writes nothing, and does not even reset a slot (REVISION item 4).
	if (!bHasTransit || !TransitLedgers.IsValidIndex(ShaftId) || Places <= 0)
	{
		return;
	}
	const int32 EventSol = TransitSolOf(Stamp);
	if (EventSol < 0)
	{
		return;
	}
	const int32 Slot = EventSol % 3;
	FRHTransitLedger& L = TransitLedgers[ShaftId];
	if (L.Sol[Slot] < EventSol)
	{
		L.Sol[Slot] = EventSol;
		L.Kg[Slot] = 0;
		L.Places[Slot] = 0;
	}
	if (L.Sol[Slot] != EventSol)
	{
		// The slot holds a later sol: an event three sols late. No writer can stamp one (stamps are sim time); say so.
		UE_LOG(LogRedHopeSim, Warning, TEXT("Transit: a ledger event for shaft %d stamped sol %d found its slot holding sol %d - dropped"), ShaftId, EventSol, L.Sol[Slot]);
		return;
	}
	L.Kg[Slot] += FMath::Max<int64>(0, Kg);
	L.Places[Slot] += (int32)FMath::Min<int64>(Places, (int64)MAX_int32 - L.Places[Slot]);
}

double URHSimWorldSubsystem::TakeRobotStamp()
{
	if (RobotStepEndSeconds >= 0.0)
	{
		return RobotStepEndSeconds;
	}
	++TransitUnstampedRobotWrites; // the -transit suite and the lifts=1 playtest assert this stays 0
	return GetStepEndSeconds();
}

void URHSimWorldSubsystem::StepTransit()
{
	if (!bHasTransit)
	{
		return;
	}
	// The StepCityGrowth pattern: the first step whose end falls in a new sol rolls once, to the latest sol.
	const int32 SolNow = TransitSolOf(GetStepEndSeconds());
	if (SolNow > TransitCheckedSol)
	{
		RollTransit(SolNow);
	}
}

void URHSimWorldSubsystem::RollTransit(int32 SolNow)
{
	// Every load first (the people part reads the flows once, before any wait moves under them), then the waits, in
	// ascending shaft Id.
	TArray<int64> Loads;
	Loads.SetNum(TransitLedgers.Num());
	const int32 CargoSol = SolNow - 2;
	for (int32 S = 0; S < TransitLedgers.Num(); ++S)
	{
		const FRHTransitLedger& L = TransitLedgers[S];
		int64 Cargo = 0;
		if (CargoSol >= 0 && L.Sol[CargoSol % 3] == CargoSol)
		{
			Cargo = L.Places[CargoSol % 3];
		}
		Loads[S] = GetShaftPeopleLoad(S) + Cargo;
	}
	// D6d: the tram lines' loads, read from the same flows before any wait moves (people only: no ledger, no lag).
	TArray<int64> TramLoads;
	TramLoads.SetNum(TramLines.Num());
	for (int32 i = 0; i < TramLines.Num(); ++i)
	{
		TramLoads[i] = GetTramPeopleLoad(TramLines[i].Id);
	}
	// D6e: each capacity is the one the previous sol's power served (its ring's slot for SolNow - 1, complete: StepTransit runs before StepPower).
	// A shaft with no car is the hoist; at Served 1000 every capacity is D4's / D6d's exactly.
	const int32 SolPrev = SolNow - 1;
	bool bChanged = false;
	for (int32 S = 0; S < TransitLedgers.Num(); ++S)
	{
		FRHTransitLedger& L = TransitLedgers[S];
		const int64 Cap = GetLiftCars(S) > 0 ? TransitServedLiftPlaces((int64)HoistPlacesPerSol, GetShaftCapacityPlaces(S), ServedPermilleAt(2, S, SolPrev)) : GetShaftCapacityPlaces(S);
		const int32 Wait = ComputeTransitWaitSec(Loads[S], Cap);
		bChanged |= Wait != L.WaitSec;
		L.PrevWaitSec = L.WaitSec;
		L.WaitSec = Wait;
	}
	// D6d: then the lines', in ascending line Id: the same wait formula against TramPlacesPerSol (D6e: the share of it power served, at least 1).
	for (int32 i = 0; i < TramLines.Num(); ++i)
	{
		FRHTramLine& Line = TramLines[i];
		const int32 Wait = ComputeTransitWaitSec(TramLoads[i], FMath::Max<int64>(1, (int64)TramPlacesPerSol * ServedPermilleAt(3, Line.Id, SolPrev) / 1000));
		bChanged |= Wait != Line.WaitSec;
		Line.PrevWaitSec = Line.WaitSec;
		Line.WaitSec = Wait;
	}
	TransitCheckedSol = SolNow;
	++TransitThresholds;
	if (bChanged)
	{
		++TransitWaitVersion;
	}
}

// --- Bills: the take order of C4's road bill; a part taken from a floor charges its shaft (O5) ---

int32 URHSimWorldSubsystem::ServingShaftOfSite(const FRHSiteRef& Site, int32 OtherLevel) const
{
	// GetApproachPoint's pick, so a hand-off pays the shaft whose head it was made at.
	if (Shafts.Num() <= 1)
	{
		return 0;
	}
	const int32 S = ServingShaft(FMath::Min(GetSiteLevel(Site), OtherLevel), CmToLattice(Shafts[0].HeadCm, GetSiteLocation(Site)));
	return S == INDEX_NONE ? 0 : S;
}

int32 URHSimWorldSubsystem::ServingShaftOfBuilding(const FRHBuildingInstance& B) const
{
	if (B.Level >= 0)
	{
		return INDEX_NONE;
	}
	return ServingShaft(B.Level, CmToLattice(Shafts[0].HeadCm, B.LocationCm));
}

void URHSimWorldSubsystem::ChargeBillPart(const FRHBuildingInstance& Source, double Kg)
{
	if (!bHasTransit || Source.Level >= 0 || Kg <= 0.0)
	{
		return;
	}
	const int32 S = ServingShaftOfBuilding(Source);
	if (S == INDEX_NONE)
	{
		return;
	}
	// World-side: stamped with the step's own end (REVISION item 2(a)), never the frame clock.
	ChargeTransit(S, GetCargoPlaces(Kg), FMath::RoundToInt64(Kg), GetStepEndSeconds());
}

bool URHSimWorldSubsystem::TakeStructForBill(double Kg, const TCHAR* Why)
{
	if (Kg <= 0.0)
	{
		return true;
	}
	if (GetRoadStructAvailableKg() + 1.0e-6 < Kg)
	{
		return false;
	}
	// Building order, outputs before held inputs (the order a hauler draws). With no transit this is C4's TakeRoadStruct
	// line for line: the part sum is a read, and ChargeBillPart returns at once.
	double Left = Kg;
	for (FRHBuildingInstance& B : Buildings)
	{
		if (Left <= 0.0)
		{
			break;
		}
		if (B.bUnderConstruction || !AreLevelsLinked(B.Level, 0))
		{
			continue;
		}
		double Part = 0.0;
		for (TMap<FName, double>* Shelf : { &B.OutputKg, &B.InputKg })
		{
			if (double* Held = Shelf->Find(NAME_StructTransit))
			{
				const double Take = FMath::Min(Left, *Held);
				*Held -= Take;
				Left -= Take;
				Part += Take;
			}
		}
		if (bHasTransit && B.Level < 0 && Part > 0.0)
		{
			ChargeBillPart(B, Part);
			UE_LOG(LogRedHopeSim, Display, TEXT("Transit: the %s bill took %.0f kg Struct from %s #%d on floor %d (%lld place(s) through its shaft)"),
				Why, Part, *B.DefName.ToString(), B.Id, B.Level, GetCargoPlaces(Part));
		}
	}
	NoteFlow(NAME_StructTransit, ERHLedgerLeg::Used, Kg - FMath::Max(Left, 0.0)); // E1: roads, lift cars and tram lines, up front
	OnStockChanged.Broadcast(NAME_StructTransit, GetTotalSolid(NAME_StructTransit));
	return true;
}

// --- FitLift ---

bool URHSimWorldSubsystem::CanFitLift(int32 ShaftId, FString& OutReason, double* OutBillKg) const
{
	const double Bill = GetLiftFitBillKg();
	if (OutBillKg)
	{
		*OutBillKg = Bill;
	}
	if (!Shafts.IsValidIndex(ShaftId))
	{
		OutReason = FString::Printf(TEXT("Unknown shaft %d"), ShaftId);
		return false;
	}
	if (Shafts[ShaftId].Depth == 0)
	{
		OutReason = FString::Printf(TEXT("Shaft %d is not bored yet - a lift car needs a floor to serve"), ShaftId);
		return false;
	}
	if (Shafts[ShaftId].LiftCars >= MaxLiftCars)
	{
		OutReason = FString::Printf(TEXT("Shaft %d already has %d lift cars, the most a shaft takes"), ShaftId, Shafts[ShaftId].LiftCars);
		return false;
	}
	const double Have = GetRoadStructAvailableKg();
	if (Have + 1.0e-6 < Bill)
	{
		OutReason = FString::Printf(TEXT("Insufficient Struct (%.0f kg needed, %.0f on hand)"), Bill, Have);
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::ExecuteFitLift(const FRHCommand& Cmd, FString& OutReason)
{
	// The preflight runs again here, at execute time: the Struct may have gone, or another order fitted the last car.
	const int32 ShaftId = FMath::RoundToInt32(Cmd.Value);
	double Bill = 0.0;
	if (!CanFitLift(ShaftId, OutReason, &Bill))
	{
		return false;
	}
	const bool bFirst = !bHasTransit;
	// The take runs before the car is added, so the first fit's own bill moves while there is no transit system yet and
	// writes no ledger entry (the REVISION's Q-A note).
	if (!TakeStructForBill(Bill, TEXT("lift car")))
	{
		OutReason = FString::Printf(TEXT("Insufficient Struct (%.0f kg needed)"), Bill);
		return false;
	}
	++Shafts[ShaftId].LiftCars;
	bHasTransit = true;
	++LiftVersion;
	if (bFirst)
	{
		TransitLedgers.Reset();
		TransitLedgers.SetNum(Shafts.Num()); // a hoist has a capacity too: one ledger per shaft
		TransitCheckedSol = TransitSolOf(GetStepEndSeconds());
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Lift car fitted on shaft %d (%d of %d), %.0f kg Struct paid%s"),
		ShaftId, Shafts[ShaftId].LiftCars, MaxLiftCars, Bill, bFirst ? TEXT(" - the colony's first: transit is on") : TEXT(""));
	if (bFirst)
	{
		// Gate-D placeholder copy (row T1 of docs/qa/2026-10-15/gate-d-wording.md): R1, said plainly.
		const FString Alert = FString::Printf(TEXT("The first lift car is in on shaft %d. Robots now ride it down to work on the floors. Every shaft now has a daily capacity: an unfitted one is a hoist that carries %d places a sol and holds each delivery %d s plus %d s a floor."),
			ShaftId, HoistPlacesPerSol, HoistDwellSeconds, HoistSecondsPerFloor);
		OnAlert.Broadcast(Alert);
	}
	return true;
}

bool URHSimWorldSubsystem::Debug_FitLift(int32 ShaftId, FString& OutReason)
{
	FRHCommand Cmd;
	Cmd.Verb = FName("FitLift");
	Cmd.Value = ShaftId;
	return ExecuteFitLift(Cmd, OutReason);
}

int32 URHSimWorldSubsystem::Debug_PostHaulTask(const FRHSiteRef& From, const FRHSiteRef& To, FName Resource, double Kg, FMassEntityHandle Robot)
{
	FRHTask T;
	T.Id = NextTaskId++;
	T.Type = ERHTaskType::Haul;
	T.From = From;
	T.To = To;
	T.Resource = Resource;
	T.AmountKg = Kg;
	T.ClaimedBy = Robot;
	Tasks.Add(T);
	return T.Id;
}

// --- Robots below: where they may go ---

int32 URHSimWorldSubsystem::LevelOfZ(double ZCm) const
{
	return RH::LevelOfZ(ZCm, FloorHeightCm);
}

bool URHSimWorldSubsystem::IsRouteReachable(int32 RobotLevel, const FVector& RobotPos, const FRHSiteRef& Site) const
{
	if (!bHasTransit)
	{
		return false;
	}
	const int32 SiteLevel = GetSiteLevel(Site);
	if (SiteLevel >= 0)
	{
		return true; // the surface: every robot below has a route up (it came down a fitted landing, and those only grow)
	}
	const int32 Ordinal = FindCarvedCellAt(SiteLevel, GetSiteLocation(Site));
	if (Ordinal == INDEX_NONE)
	{
		return false; // rock
	}
	const FRHDigInfo& Info = EnsureDigInfo(SiteLevel);
	if (!Info.DigOf.IsValidIndex(Ordinal))
	{
		return false;
	}
	const int32 Dig = Info.DigOf[Ordinal];
	if (EnsureDeepNet().FittedDigs.Contains(FIntPoint(SiteLevel, Dig)))
	{
		return true;
	}
	// Or the robot's own dig (a fitted column counts as standing in its landing's dig).
	if (RobotLevel == SiteLevel)
	{
		const int32 Col = FindPortalColumnAt(RobotLevel, RobotPos);
		if (Col != INDEX_NONE)
		{
			return DeepNet.Portals[Col].Dig == Dig;
		}
		const int32 Own = FindCarvedCellAt(RobotLevel, RobotPos);
		return Own != INDEX_NONE && Info.DigOf.IsValidIndex(Own) && Info.DigOf[Own] == Dig;
	}
	return false;
}

FVector URHSimWorldSubsystem::GetRobotTarget(const FRHSiteRef& Site, int32 RobotLevel, const FVector& RobotPos, int32& OutLevel) const
{
	OutLevel = 0;
	if (bHasTransit && IsRouteReachable(RobotLevel, RobotPos, Site))
	{
		OutLevel = GetSiteLevel(Site);
		return GetSiteLocation(Site);
	}
	// The fallback is always the surface head (REVISION item 1): never the site in rock, never a bare column.
	return GetApproachPoint(Site, 0);
}

int32 URHSimWorldSubsystem::FindPortalColumnAt(int32 Level, const FVector& PosCm) const
{
	if (!bHasTransit || Level >= 0)
	{
		return INDEX_NONE;
	}
	// A robot on a fitted column (its lattice point is the column) at a floor where the shaft is a portal. The lattice cell,
	// not ArriveDistCm: a robot stopped between the landing and the column centre stands on the column's cell.
	const FRHDeepNet& Net = EnsureDeepNet();
	const FIntPoint P = CmToLattice(Shafts[0].HeadCm, PosCm);
	// The portals on this level, ascending: the first with the column is the one the all-portals scan found.
	if (Net.LevelPortals.IsValidIndex(-Level))
	{
		for (const int32 i : Net.LevelPortals[-Level])
		{
			if (Net.Portals[i].Column == P)
			{
				return i;
			}
		}
	}
	return INDEX_NONE;
}

// --- The deep net (D3 section 2): fitted portals, one lattice field per landing, head-to-head surface costs ---

FVector URHSimWorldSubsystem::FloorCellXY(int32 Level, int32 Ordinal) const
{
	const FRHFloor* Floor = FindFloor(Level);
	const FIntPoint P = (Floor && Floor->Cells.IsValidIndex(Ordinal)) ? Floor->Cells[Ordinal] : FIntPoint::ZeroValue;
	FVector Out = LatticeToCm(Shafts[0].HeadCm, P);
	Out.Z = Level * FloorHeightCm;
	return Out;
}

void URHSimWorldSubsystem::BuildLatticeNeighbours(int32 Level, TArray<int32>& Out, TArray<int32>& GridScratch) const
{
	// The steps BuildLatticeField takes from each cell, looked up once a floor: a diagonal only when both orthogonal cells
	// are carved. The deep net shares one table among the landings of a floor. The lookups run through a flat grid over the
	// floor's box (the same answers as FindOrdinal), the orthogonals found once and the diagonals read from them.
	const FRHFloor* Floor = FindFloor(Level);
	const int32 N = Floor ? Floor->Cells.Num() : 0;
	Out.Init(INDEX_NONE, N * 8);
	if (N == 0)
	{
		return;
	}
	FTransitFlatIndex Index;
	Index.Build(Floor, GridScratch);
	for (int32 c = 0; c < N; ++c)
	{
		const FIntPoint Cur = Floor->Cells[c];
		int32* Row = &Out[c * 8];
		for (int32 k = 0; k < 4; ++k)
		{
			Row[k] = Index.Find(Cur.X + TransitSteps[k].X, Cur.Y + TransitSteps[k].Y);
		}
		// Steps 4..7 are (+1,+1), (-1,+1), (+1,-1), (-1,-1): the orthogonal cells they need are (X step, 0) and (0, Y step), i.e. steps
		// 0/1 by the X sign and 2/3 by the Y sign.
		for (int32 k = 4; k < 8; ++k)
		{
			const int32 XOrth = TransitSteps[k].X > 0 ? Row[0] : Row[1];
			const int32 YOrth = TransitSteps[k].Y > 0 ? Row[2] : Row[3];
			if (XOrth != INDEX_NONE && YOrth != INDEX_NONE)
			{
				Row[k] = Index.Find(Cur.X + TransitSteps[k].X, Cur.Y + TransitSteps[k].Y);
			}
		}
	}
}

void URHSimWorldSubsystem::BuildLatticeField(int32 Level, int32 Root, TArray<int64>& OutDist, TArray<int32>& OutToward, const TArray<int32>* Neighbours) const
{
	// Dijkstra over the floor's carved cells from Root: orthogonal steps 1000 cm, diagonal steps 1414 cm when both
	// orthogonal cells are carved. Popped by (distance, ordinal); a predecessor changes only on a strict improvement.
	const FRHFloor* Floor = FindFloor(Level);
	const int32 N = Floor ? Floor->Cells.Num() : 0;
	if (Neighbours && Floor && Floor->Cells.IsValidIndex(Root))
	{
		TArray<FTransitHeapItem> OwnHeap;
		OwnHeap.Reserve(N);
		RunLatticeField(*Neighbours, N, Root, OutDist, OutToward, OwnHeap);
		return;
	}
	OutDist.Init(MAX_int64, N);
	OutToward.Init(INDEX_NONE, N);
	if (!Floor || !Floor->Cells.IsValidIndex(Root))
	{
		return;
	}
	TArray<FTransitHeapItem> Heap;
	Heap.Reserve(N);
	OutDist[Root] = 0;
	Heap.HeapPush({ 0, Root }, FTransitHeapLess());
	while (Heap.Num() > 0)
	{
		FTransitHeapItem Top;
		Heap.HeapPop(Top, FTransitHeapLess(), EAllowShrinking::No);
		if (Top.Key != OutDist[Top.Node])
		{
			continue;
		}
		const FIntPoint Cur = Floor->Cells[Top.Node];
		for (int32 k = 0; k < 8; ++k)
		{
			int32 O = Floor->FindOrdinal(Cur + TransitSteps[k]);
			if (O != INDEX_NONE && k >= 4 && (Floor->FindOrdinal(Cur + FIntPoint(TransitSteps[k].X, 0)) == INDEX_NONE || Floor->FindOrdinal(Cur + FIntPoint(0, TransitSteps[k].Y)) == INDEX_NONE))
			{
				O = INDEX_NONE;
			}
			if (O == INDEX_NONE)
			{
				continue;
			}
			const int64 D = Top.Key + TransitStepCm[k];
			if (D < OutDist[O])
			{
				OutDist[O] = D;
				OutToward[O] = Top.Node;
				Heap.HeapPush({ D, O }, FTransitHeapLess());
			}
		}
	}
}

bool URHSimWorldSubsystem::LatticeAStar(int32 Level, int32 From, int32 To, TArray<int32>& OutCells, int64& OutCm) const
{
	// The direct floor leg between two cells of one dig: A* with the octile distance (admissible and consistent on this
	// grid), popped by (f, ordinal). OutCells runs from the cell after From to To.
	OutCells.Reset();
	OutCm = 0;
	const FRHFloor* Floor = FindFloor(Level);
	if (!Floor || !Floor->Cells.IsValidIndex(From) || !Floor->Cells.IsValidIndex(To))
	{
		return false;
	}
	if (From == To)
	{
		return true;
	}
	const FIntPoint Goal = Floor->Cells[To];
	const auto H = [&Goal](const FIntPoint& P)
	{
		const int64 Dx = FMath::Abs((int64)P.X - Goal.X), Dy = FMath::Abs((int64)P.Y - Goal.Y);
		const int64 Lo = FMath::Min(Dx, Dy), Hi = FMath::Max(Dx, Dy);
		return Lo * 1414 + (Hi - Lo) * 1000;
	};
	const int32 N = Floor->Cells.Num();
	TArray<int64> G;
	G.Init(MAX_int64, N);
	TArray<int32> Prev;
	Prev.Init(INDEX_NONE, N);
	TArray<FTransitHeapItem> Heap;
	G[From] = 0;
	Heap.HeapPush({ H(Floor->Cells[From]), From }, FTransitHeapLess());
	bool bFound = false;
	while (Heap.Num() > 0)
	{
		FTransitHeapItem Top;
		Heap.HeapPop(Top, FTransitHeapLess(), EAllowShrinking::No);
		const FIntPoint Cur = Floor->Cells[Top.Node];
		if (Top.Key != G[Top.Node] + H(Cur))
		{
			continue;
		}
		if (Top.Node == To)
		{
			bFound = true;
			break;
		}
		for (int32 k = 0; k < 8; ++k)
		{
			const int32 O = Floor->FindOrdinal(Cur + TransitSteps[k]);
			if (O == INDEX_NONE)
			{
				continue;
			}
			if (k >= 4 && (Floor->FindOrdinal(Cur + FIntPoint(TransitSteps[k].X, 0)) == INDEX_NONE || Floor->FindOrdinal(Cur + FIntPoint(0, TransitSteps[k].Y)) == INDEX_NONE))
			{
				continue;
			}
			const int64 D = G[Top.Node] + TransitStepCm[k];
			if (D < G[O])
			{
				G[O] = D;
				Prev[O] = Top.Node;
				Heap.HeapPush({ D + H(Floor->Cells[O]), O }, FTransitHeapLess());
			}
		}
	}
	if (!bFound)
	{
		return false;
	}
	OutCm = G[To];
	for (int32 O = To; O != From && O != INDEX_NONE; O = Prev[O])
	{
		OutCells.Add(O);
	}
	Algo::Reverse(OutCells);
	return true;
}

const FRHRoadRoute& URHSimWorldSubsystem::SurfaceRoute(FIntPoint FromCm, FIntPoint ToCm) const
{
	// RoadGraph.PlanRoute, memoized in the deep net while the net's key (which holds the built-road version) is current. With no
	// built road, or a stale net, it plans into a scratch route every time - the same answer, unmemoized.
	static const FRHRoadRoute Straight;
	if (!HasBuiltRoads())
	{
		return Straight;
	}
	const TPair<FIntPoint, FIntPoint> Key(FromCm, ToCm);
	const bool bMemo = DeepNet.bValid && DeepNet.Built == RoadGraph.BuiltVersion;
	if (bMemo)
	{
		if (const FRHRoadRoute* Hit = DeepNet.SurfaceRoutes.Find(Key))
		{
			if (bVerifyCaches)
			{
				// The memo outlives a net rebuild while the built-road version is unchanged: plan the leg again and compare.
				FRHRoadRoute Fresh;
				if (!RoadGraph.PlanRoute(FromCm, ToCm, RoadParams, Fresh) || !Fresh.bRoad)
				{
					Fresh = FRHRoadRoute();
				}
				if (Fresh.bRoad != Hit->bRoad || Fresh.NodeIds != Hit->NodeIds || Fresh.EdgeIds != Hit->EdgeIds || Fresh.OffInCm != Hit->OffInCm
					|| Fresh.OffOutCm != Hit->OffOutCm || Fresh.RoadCm != Hit->RoadCm || Fresh.CostUnits != Hit->CostUnits)
				{
					UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the deep net's surface leg (%d, %d) -> (%d, %d) is stale (cost %lld kept, %lld planned; built v%u)"),
						FromCm.X, FromCm.Y, ToCm.X, ToCm.Y, Hit->CostUnits, Fresh.CostUnits, RoadGraph.BuiltVersion);
				}
			}
			return *Hit;
		}
		if (DeepNet.SurfaceRoutes.Num() >= 4096)
		{
			DeepNet.SurfaceRoutes.Reset();
		}
	}
	static FRHRoadRoute Scratch;
	FRHRoadRoute& Out = bMemo ? DeepNet.SurfaceRoutes.Add(Key) : Scratch;
	Out = FRHRoadRoute();
	if (!RoadGraph.PlanRoute(FromCm, ToCm, RoadParams, Out) || !Out.bRoad)
	{
		Out = FRHRoadRoute(); // bRoad false: drive the straight line
	}
	return Out;
}

int64 URHSimWorldSubsystem::SurfaceCostUnits(const FVector& A, const FVector& B, int64* OutCm) const
{
	// C4's cost units: the road route's CostUnits when a built road beats the straight line, else SpeedMulPermille x the
	// rounded straight cm. OutCm is the distance walked, all of it at base speed (the reserve's safe side).
	const FIntPoint Ac = TransitWholeCm(A), Bc = TransitWholeCm(B);
	const FRHRoadRoute& Route = SurfaceRoute(Ac, Bc);
	if (Route.bRoad)
	{
		if (OutCm)
		{
			*OutCm = (int64)Route.OffInCm + Route.RoadCm + Route.OffOutCm;
		}
		return Route.CostUnits;
	}
	const int64 Dx = (int64)Bc.X - Ac.X, Dy = (int64)Bc.Y - Ac.Y;
	const int64 Straight = RHRoads::RoundSqrtCm(Dx * Dx + Dy * Dy);
	if (OutCm)
	{
		*OutCm = Straight;
	}
	return (int64)RoadParams.SpeedMulPermille * Straight;
}

void URHSimWorldSubsystem::AppendSurfaceLegs(FRHRobotRoadPath& Path, const FVector& From, const FVector& To) const
{
	// The waypoints PlanRobotPath lays for the same two points: entry node .. exit node along built edges, then To.
	const FRHRoadRoute& Route = SurfaceRoute(TransitWholeCm(From), TransitWholeCm(To));
	if (Route.bRoad)
	{
		for (int32 i = 0; i < Route.NodeIds.Num(); ++i)
		{
			const FIntPoint N = RoadGraph.NodeCm(Route.NodeIds[i], RoadParams);
			Path.Waypoints.Add(FVector((double)N.X, (double)N.Y, 0.0));
			Path.LegEdge.Add(i == 0 ? 0 : Route.EdgeIds[i - 1]);
			Path.Levels.Add(0);
		}
	}
	Path.Waypoints.Add(FVector(To.X, To.Y, 0.0));
	Path.LegEdge.Add(0);
	Path.Levels.Add(0);
}

const URHSimWorldSubsystem::FRHDeepNet& URHSimWorldSubsystem::EnsureDeepNet() const
{
	FRHDeepNet& N = DeepNet;
	if (N.bValid && N.Cells == FloorCellsVersion && N.ShaftsV == ShaftsVersion && N.Lift == LiftVersion && N.Built == RoadGraph.BuiltVersion && N.TunnelsV == TunnelsVersion)
	{
		return N;
	}
	++DeepNetBuilds;
	// The surface legs are a pure function of the built road graph (and the road parameters, fixed at start): the memo outlives a
	// rebuild whose built-road version is the one it was planned under. Any other rebuild starts it empty, as it always did.
	TMap<TPair<FIntPoint, FIntPoint>, FRHRoadRoute> KeptRoutes;
	if (N.bValid && N.Built == RoadGraph.BuiltVersion)
	{
		KeptRoutes = MoveTemp(N.SurfaceRoutes);
	}
	N = FRHDeepNet();
	N.SurfaceRoutes = MoveTemp(KeptRoutes);
	N.bValid = true;
	N.Cells = FloorCellsVersion;
	N.ShaftsV = ShaftsVersion;
	N.Lift = LiftVersion;
	N.Built = RoadGraph.BuiltVersion;
	N.TunnelsV = TunnelsVersion;
	TMap<int32, TArray<int32>> NeighboursOf; // level -> its step table, shared by every landing on it
	TArray<int32> GridScratch;                // the flat floor index, reused floor after floor
	TArray<FTransitHeapItem> FieldHeap;       // the field search's heap, reused landing after landing
	int32 MaxLevelDepth = 0;
	for (const FRHShaft& Sh : Shafts)
	{
		if (Sh.LiftCars <= 0)
		{
			continue;
		}
		// The head: a surface portal at the column centre.
		FRHDeepPortal Head;
		Head.Shaft = Sh.Id;
		Head.Level = 0;
		Head.Column = Sh.Column;
		Head.XY = FVector(Sh.HeadCm.X, Sh.HeadCm.Y, 0.0);
		N.PortalAt.Add(FIntPoint(Sh.Id, 0), N.Portals.Num());
		N.Heads.Add(N.Portals.Num());
		N.Portals.Add(MoveTemp(Head));
		// A floor portal on each level the shaft reaches where one of the column's four orthogonal neighbours is carved; the
		// landing is the lowest-ordinal such neighbour. A dig touched only diagonally has no landing.
		for (int32 L = -1; L >= -Sh.Depth; --L)
		{
			const FRHFloor* Floor = FindFloor(L);
			if (!Floor)
			{
				continue;
			}
			int32 Landing = INDEX_NONE;
			for (int32 k = 0; k < 4; ++k)
			{
				const int32 O = Floor->FindOrdinal(Sh.Column + TransitSteps[k]);
				if (O != INDEX_NONE && (Landing == INDEX_NONE || O < Landing))
				{
					Landing = O;
				}
			}
			if (Landing == INDEX_NONE)
			{
				continue;
			}
			FRHDeepPortal Port;
			Port.Shaft = Sh.Id;
			Port.Level = L;
			Port.Column = Sh.Column;
			Port.XY = FVector(Sh.HeadCm.X, Sh.HeadCm.Y, L * FloorHeightCm);
			Port.Landing = Landing;
			const FRHDigInfo& Info = EnsureDigInfo(L);
			Port.Dig = Info.DigOf.IsValidIndex(Landing) ? Info.DigOf[Landing] : INDEX_NONE;
			TArray<int32>* Nbr = NeighboursOf.Find(L);
			if (!Nbr)
			{
				Nbr = &NeighboursOf.Add(L);
				BuildLatticeNeighbours(L, *Nbr, GridScratch);
			}
			RunLatticeField(*Nbr, Floor->Cells.Num(), Landing, Port.Dist, Port.Toward, FieldHeap);
			N.FittedDigs.Add(FIntPoint(L, Port.Dig));
			N.PortalAt.Add(FIntPoint(Sh.Id, L), N.Portals.Num());
			MaxLevelDepth = FMath::Max(MaxLevelDepth, -L);
			N.Portals.Add(MoveTemp(Port));
		}
	}
	const int32 NH = N.Heads.Num();
	N.HeadToHead.Init(0, NH * NH);
	for (int32 i = 0; i < NH; ++i)
	{
		for (int32 j = 0; j < NH; ++j)
		{
			N.HeadToHead[i * NH + j] = i == j ? 0 : SurfaceCostUnits(N.Portals[N.Heads[i]].XY, N.Portals[N.Heads[j]].XY);
		}
	}
	// The planner's portal-to-portal edges that can be finite (every other pair prices MAX_int64 and is never relaxed), ascending by target,
	// each with its wait-free price stored beside it (the planner used to work the same numbers out on every pop): a ride's floors, a walk's
	// lattice cm along the landed field, a head-to-head leg's finished surface cost.
	N.Edges.SetNum(N.Portals.Num());
	for (int32 U = 0; U < N.Portals.Num(); ++U)
	{
		const FRHDeepPortal& A = N.Portals[U];
		for (int32 V = 0; V < N.Portals.Num(); ++V)
		{
			const FRHDeepPortal& B = N.Portals[V];
			const bool bRide = A.Shaft == B.Shaft && A.Level != B.Level;
			const bool bWalk = A.Level == B.Level && A.Level < 0 && A.Dig == B.Dig && B.Dist.IsValidIndex(A.Landing) && B.Dist[A.Landing] != MAX_int64;
			const bool bHeads = A.Level == 0 && B.Level == 0;
			if (V != U && (bRide || bWalk || bHeads))
			{
				FRHDeepEdge E;
				E.To = V;
				if (bRide)
				{
					E.Kind = 0;
					E.Shaft = A.Shaft;
					E.Base = (int64)FMath::Abs(A.Level - B.Level);
				}
				else if (bWalk)
				{
					E.Kind = 1;
					E.Base = B.Dist[A.Landing];
				}
				else
				{
					const int32 Hi = N.Heads.IndexOfByKey(U), Hj = N.Heads.IndexOfByKey(V);
					E.Kind = 2;
					E.Base = (Hi == INDEX_NONE || Hj == INDEX_NONE) ? MAX_int64 : N.HeadToHead[Hi * NH + Hj];
				}
				N.Edges[U].Add(E);
			}
		}
	}
	// The portals on each level, by -Level (0 = the heads), ascending: the planner's column and dig lookups scan one short list.
	N.LevelPortals.SetNum(MaxLevelDepth + 1);
	for (int32 i = 0; i < N.Portals.Num(); ++i)
	{
		N.LevelPortals[-N.Portals[i].Level].Add(i);
	}
	return N;
}

void URHSimWorldSubsystem::Debug_GetPortals(TArray<FIntVector>& Out) const
{
	Out.Reset();
	if (!bHasTransit)
	{
		return;
	}
	const FRHDeepNet& Net = EnsureDeepNet();
	for (const FRHDeepPortal& P : Net.Portals)
	{
		const FRHFloor* Floor = P.Level < 0 ? FindFloor(P.Level) : nullptr;
		const FIntPoint Land = (Floor && Floor->Cells.IsValidIndex(P.Landing)) ? Floor->Cells[P.Landing] : P.Column;
		Out.Emplace(P.Shaft * 100 + (-P.Level), Land.X, Land.Y);
	}
}

// --- The planner (D3 section 2, D4 1.3) ---

bool URHSimWorldSubsystem::PlanDeepRoute(int32 FromLevel, const FVector& FromCm, int32 ToLevel, const FVector& ToCm, double Stamp, FRHRobotRoadPath& OutPath, FRHDeepRouteCost* OutCost) const
{
	++DeepPlanRuns;
	OutPath.Waypoints.Reset();
	OutPath.LegEdge.Reset();
	OutPath.Levels.Reset();
	OutPath.Next = 0;
	OutPath.StartCm = FromCm;
	FRHDeepRouteCost Cost;
	if (!bHasTransit)
	{
		return false;
	}
	const FRHDeepNet& Net = EnsureDeepNet();
	const int64 Mm = RoadParams.SpeedMulPermille;
	const int32 NP = Net.Portals.Num();

	// The two ends: the surface, a carved cell, or a fitted column (a legal position: a robot off a ride, or halted on one).
	enum class EEnd : uint8 { Surface, Cell, Column };
	struct FEnd { EEnd Kind = EEnd::Surface; int32 Level = 0; int32 Ordinal = INDEX_NONE; int32 Portal = INDEX_NONE; int32 Dig = INDEX_NONE; };
	const auto Resolve = [&](int32 Level, const FVector& Cm, FEnd& Out)
	{
		Out = FEnd();
		Out.Level = Level;
		if (Level >= 0)
		{
			Out.Level = 0;
			return true;
		}
		const int32 Col = FindPortalColumnAt(Level, Cm);
		if (Col != INDEX_NONE)
		{
			Out.Kind = EEnd::Column;
			Out.Portal = Col;
			Out.Dig = Net.Portals[Col].Dig;
			return true;
		}
		const int32 Ord = FindCarvedCellAt(Level, Cm);
		if (Ord == INDEX_NONE)
		{
			return false;
		}
		const FRHDigInfo& Info = EnsureDigInfo(Level);
		Out.Kind = EEnd::Cell;
		Out.Ordinal = Ord;
		Out.Dig = Info.DigOf.IsValidIndex(Ord) ? Info.DigOf[Ord] : INDEX_NONE;
		return true;
	};
	FEnd Start, End;
	if (!Resolve(FromLevel, FromCm, Start))
	{
		++DeepOffLattice; // a robot below on no carved cell and no fitted column: the -transit suite and lifts=1 assert none
		return false;
	}
	if (!Resolve(ToLevel, ToCm, End))
	{
		return false; // a target in rock
	}

	// Ride cost at query time: the net is wait-free, the robot's waits are read by its own stamp (REVISION item 2(d)), once a shaft a call.
	// A ride's price is (wait + dwell + floors x seconds a floor) x the plan speed x the road multiplier, which is
	// RideShaftUnits[shaft] + RideFloorUnits x floors, with the wait of each shaft read once a call. The table has a slot for every shaft a
	// portal can name (a shaft with no ledger reads 0, as GetRobotWaitSec does).
	const int64 RideUnitsPerSec = Mm * (int64)LiftPlanSpeedCmPerS;
	const int64 RideFloorUnits = RideUnitsPerSec * (int64)LiftSecondsPerFloor;
	TArray<int64, TInlineAllocator<32>> RideShaftUnits;
	RideShaftUnits.SetNumUninitialized(Shafts.Num());
	for (int32 S = 0; S < RideShaftUnits.Num(); ++S)
	{
		RideShaftUnits[S] = RideUnitsPerSec * ((int64)GetRobotWaitSec(S, Stamp) + LiftDwellSeconds);
	}
	// The direct leg from the start to the target, worked out once.
	int64 DirectUnits = MAX_int64;
	TArray<int32> DirectCells;
	int64 DirectCm = 0;
	if (Start.Kind == EEnd::Surface && End.Kind == EEnd::Surface)
	{
		DirectUnits = SurfaceCostUnits(FromCm, ToCm);
	}
	else if (Start.Kind == EEnd::Cell && End.Kind == EEnd::Cell && Start.Level == End.Level && Start.Dig == End.Dig)
	{
		if (LatticeAStar(Start.Level, Start.Ordinal, End.Ordinal, DirectCells, DirectCm))
		{
			DirectUnits = Mm * DirectCm;
		}
	}
	// Node order: portals 0..NP-1, the start NP, the end NP + 1. Ties settle the lower index first (portals before the end).
	// Dijkstra popped by (distance, node index) from a binary heap with lazy deletion: the sequence of nodes it settles is the one the
	// old scan for the lowest unsettled (distance, index) found, and every relaxation is a strict improvement as before. A node's edges
	// are priced once, when it settles: from the start every node it can reach, from a portal its stored edges and the end.
	const int32 SNode = NP, ENode = NP + 1, NNodes = NP + 2;
	TArray<int64, TInlineAllocator<512>> Dist;
	Dist.SetNumUninitialized(NNodes);
	TArray<int32, TInlineAllocator<512>> Prev;
	Prev.SetNumUninitialized(NNodes);
	TArray<bool, TInlineAllocator<512>> Done;
	Done.SetNumUninitialized(NNodes);
	for (int32 i = 0; i < NNodes; ++i)
	{
		Dist[i] = MAX_int64;
		Prev[i] = INDEX_NONE;
		Done[i] = false;
	}
	FTransitSearch Search;
	Search.Dist = Dist.GetData();
	Search.Prev = Prev.GetData();
	Search.Done = Done.GetData();
	Dist[SNode] = 0;
	Search.Push(0, SNode);
	while (Search.Heap.Num() > 0)
	{
		const FTransitHeapItem Top = Search.Pop();
		const int32 U = Top.Node;
		if (Search.Done[U] || Top.Key != Search.Dist[U])
		{
			continue; // a superseded entry
		}
		if (U == ENode)
		{
			break;
		}
		Search.Done[U] = true;
		Search.U = U;
		Search.DistU = Search.Dist[U];
		if (U == SNode)
		{
			Search.Relax(ENode, DirectUnits);
			switch (Start.Kind)
			{
			case EEnd::Surface:
				for (const int32 V : Net.Heads) { Search.Relax(V, SurfaceCostUnits(FromCm, Net.Portals[V].XY)); }
				break;
			case EEnd::Cell:
				if (Net.LevelPortals.IsValidIndex(-Start.Level))
				{
					for (const int32 V : Net.LevelPortals[-Start.Level])
					{
						const FRHDeepPortal& P = Net.Portals[V];
						if (P.Dig == Start.Dig && P.Dist.IsValidIndex(Start.Ordinal) && P.Dist[Start.Ordinal] != MAX_int64)
						{
							Search.Relax(V, Mm * (P.Dist[Start.Ordinal] + 1000));
						}
					}
				}
				break;
			default:
				Search.Relax(Start.Portal, 0);
				break;
			}
		}
		else
		{
			const TArray<FRHDeepEdge>& Edges = Net.Edges[U];
			const FRHDeepEdge* EdgeIt = Edges.GetData();
			const FRHDeepEdge* const EdgeEnd = EdgeIt + Edges.Num();
			for (; EdgeIt != EdgeEnd; ++EdgeIt)
			{
				switch (EdgeIt->Kind)
				{
				case 0:
					Search.Relax(EdgeIt->To, RideShaftUnits[EdgeIt->Shaft] + RideFloorUnits * EdgeIt->Base);
					break;
				case 1:
					Search.Relax(EdgeIt->To, Mm * (1000 + EdgeIt->Base + 1000));
					break;
				default:
					Search.Relax(EdgeIt->To, EdgeIt->Base);
					break;
				}
			}
			const FRHDeepPortal& P = Net.Portals[U];
			int64 ToEnd = MAX_int64;
			switch (End.Kind)
			{
			case EEnd::Surface:
				if (P.Level == 0) { ToEnd = SurfaceCostUnits(P.XY, ToCm); }
				break;
			case EEnd::Cell:
				if (P.Level == End.Level && P.Dig == End.Dig && P.Dist.IsValidIndex(End.Ordinal) && P.Dist[End.Ordinal] != MAX_int64) { ToEnd = Mm * (1000 + P.Dist[End.Ordinal]); }
				break;
			default:
				ToEnd = U == End.Portal ? 0 : MAX_int64;
				break;
			}
			Search.Relax(ENode, ToEnd);
		}
	}
	if (Dist[ENode] == MAX_int64)
	{
		return false;
	}
	Cost.CostUnits = Dist[ENode];
	TArray<int32, TInlineAllocator<32>> Chain;
	for (int32 V = ENode; V != INDEX_NONE; V = Prev[V])
	{
		Chain.Add(V);
	}
	Algo::Reverse(Chain); // SNode .. ENode

	// The waypoints, leg by leg. A walk leg's waypoint is a cell centre, a column or a surface point; a ride leg's is the
	// column at the floor it arrives on.
	FRHRobotRoadPath& Path = OutPath;
	// A long walk below is a few dozen waypoints: one allocation for each array instead of a doubling at 4, 8, 16 and 32.
	Path.Waypoints.Reserve(64);
	Path.LegEdge.Reserve(64);
	Path.Levels.Reserve(64);
	const auto Walk = [&Path](const FVector& XY, int32 Level, int32 EdgeId = 0)
	{
		Path.Waypoints.Add(XY);
		Path.LegEdge.Add(EdgeId);
		Path.Levels.Add(Level);
	};
	const auto Surface = [&](const FVector& A, const FVector& B)
	{
		int64 Cm = 0;
		SurfaceCostUnits(A, B, &Cm);
		Cost.SurfaceCm += Cm;
		AppendSurfaceLegs(Path, A, B);
	};
	// From a cell to a portal's landing along its field (the cell itself excluded), then onto the column.
	const auto CellToColumn = [&](const FRHDeepPortal& P, int32 FromOrdinal)
	{
		Cost.LatticeCm += P.Dist[FromOrdinal] + 1000;
		for (int32 O = P.Toward[FromOrdinal]; O != INDEX_NONE; O = P.Toward[O])
		{
			Walk(FloorCellXY(P.Level, O), P.Level);
		}
		Walk(P.XY, P.Level);
	};
	// From a portal's column onto its landing and on to a cell (the cell excluded: the caller walks to the target in it).
	const auto ColumnToCell = [&](const FRHDeepPortal& P, int32 ToOrdinal)
	{
		Cost.LatticeCm += 1000 + P.Dist[ToOrdinal];
		TArray<int32, TInlineAllocator<64>> Back;
		for (int32 O = P.Toward[ToOrdinal]; O != INDEX_NONE; O = P.Toward[O])
		{
			Back.Add(O);
		}
		for (int32 i = Back.Num() - 1; i >= 0; --i)
		{
			Walk(FloorCellXY(P.Level, Back[i]), P.Level);
		}
	};
	for (int32 c = 0; c + 1 < Chain.Num(); ++c)
	{
		const int32 U = Chain[c], V = Chain[c + 1];
		if (U == SNode && V == ENode)
		{
			if (Start.Kind == EEnd::Surface)
			{
				Surface(FromCm, ToCm);
			}
			else
			{
				Cost.LatticeCm += DirectCm;
				for (int32 i = 0; i + 1 < DirectCells.Num(); ++i)
				{
					Walk(FloorCellXY(Start.Level, DirectCells[i]), Start.Level);
				}
			}
			continue;
		}
		if (U == SNode)
		{
			const FRHDeepPortal& P = Net.Portals[V];
			if (Start.Kind == EEnd::Surface) { Surface(FromCm, P.XY); }
			else if (Start.Kind == EEnd::Cell) { CellToColumn(P, Start.Ordinal); }
			else { Walk(P.XY, P.Level); }
			continue;
		}
		if (V == ENode)
		{
			const FRHDeepPortal& P = Net.Portals[U];
			if (End.Kind == EEnd::Surface) { Surface(P.XY, ToCm); }
			else if (End.Kind == EEnd::Cell) { ColumnToCell(P, End.Ordinal); }
			continue;
		}
		const FRHDeepPortal& A = Net.Portals[U];
		const FRHDeepPortal& B = Net.Portals[V];
		if (A.Shaft == B.Shaft && A.Level != B.Level)
		{
			const int32 Wait = GetRobotWaitSec(A.Shaft, Stamp);
			Path.Waypoints.Add(B.XY);
			Path.LegEdge.Add(-(A.Shaft + 1));
			Path.Levels.Add(B.Level);
			Cost.RideSeconds += LiftDwellSeconds + FMath::Abs(A.Level - B.Level) * LiftSecondsPerFloor;
			++Cost.Rides;
			if (B.Level > A.Level)
			{
				++Cost.AscentRides;
				Cost.AscentHoldSeconds += Wait;
			}
			else
			{
				Cost.DescentHoldSeconds += Wait;
			}
		}
		else if (A.Level < 0)
		{
			// Column A, its landing, along B's field to B's landing, then B's column.
			Cost.LatticeCm += 1000 + B.Dist[A.Landing] + 1000;
			Walk(FloorCellXY(A.Level, A.Landing), A.Level);
			for (int32 O = B.Toward[A.Landing]; O != INDEX_NONE; O = B.Toward[O])
			{
				Walk(FloorCellXY(A.Level, O), A.Level);
			}
			Walk(B.XY, B.Level);
		}
		else
		{
			Surface(A.XY, B.XY);
		}
	}
	// The last leg is always to the target itself (an exact drive to every waypoint before it, MoveToward's arrival on it).
	if (Path.Waypoints.Num() == 0 || !Path.Waypoints.Last().Equals(ToCm, 0.0) || Path.LegEdge.Last() != 0)
	{
		Walk(ToCm, End.Level);
	}
	else
	{
		Path.Waypoints.Last() = ToCm;
		Path.Levels.Last() = End.Level;
	}
	if (OutCost)
	{
		*OutCost = Cost;
	}
	return true;
}

// --- One robot sub-step along the deep network ---

bool URHSimWorldSubsystem::MoveAlongDeep(FMassEntityHandle Entity, FTransform& Transform, const FVector& TargetCm, int32 TargetLevel, float SpeedMps, float Dt, float CargoKg)
{
	FRHRobotRoadPath* Path = RobotPaths.Find(Entity);
	if (!Path || !Path->bDeep || Path->TargetCm != TargetCm || Path->TargetLevel != TargetLevel)
	{
		// A new target: plan from here. A hold or a ride in progress is discarded; the robot stays on the column at its
		// boarding floor with its Z unchanged (Z changes only when a ride completes), which is a legal start.
		FRHRobotRoadPath& Slot = RobotPaths.FindOrAdd(Entity);
		const FVector Pos = Transform.GetLocation();
		Slot = FRHRobotRoadPath();
		Slot.bPlanFailed = !PlanDeepRoute(LevelOfZ(Pos.Z), Pos, TargetLevel, TargetCm, PeekRobotStamp(), Slot);
		Slot.bDeep = true;
		Slot.TargetCm = TargetCm;
		Slot.TargetLevel = TargetLevel;
		Slot.StartCm = Pos;
		Slot.Serial = NextPathSerial++;
		Path = &Slot;
	}
	Path->bHeldThisStep = false;
	if (Path->bPlanFailed)
	{
		return false; // no route from here: stand until the target changes
	}
	const auto Board = [&]()
	{
		const int32 Leg = Path->Next;
		const int32 Shaft = -Path->LegEdge[Leg] - 1;
		const int32 FromLevel = LevelOfZ(Transform.GetLocation().Z);
		Path->HoldStepsLeft = 10 * GetRobotWaitSec(Shaft, PeekRobotStamp());
		Path->RideSteps = 10 * (LiftDwellSeconds + FMath::Abs(FromLevel - Path->Levels[Leg]) * LiftSecondsPerFloor);
		Path->RideStepsLeft = Path->RideSteps;
		Path->RideFromLevel = FromLevel;
		Path->bBoarded = true;
		++RidesBoarded;
		// Cargo only: the robot's body is 0 places (REVISION item 4), and a 0-place event writes nothing.
		const int64 Places = GetCargoPlaces(CargoKg);
		if (Places > 0)
		{
			ChargeTransit(Shaft, Places, FMath::RoundToInt64(CargoKg), TakeRobotStamp());
		}
	};
	if (Path->bBoarded)
	{
		if (Path->HoldStepsLeft > 0)
		{
			--Path->HoldStepsLeft;
			Path->bHeldThisStep = true;
			return false;
		}
		if (--Path->RideStepsLeft <= 0)
		{
			// The fixed sub-step point: Z flips at the end of the sub-step that ends the ride, and the robot moves no further.
			FVector Pos = Transform.GetLocation();
			Pos.Z = Path->Levels[Path->Next] * FloorHeightCm + RH::RobotZOffsetCm;
			Transform.SetLocation(Pos);
			Path->bBoarded = false;
			++Path->Next;
		}
		return false;
	}
	double RemT = (double)Dt;
	while (Path->Next < Path->Waypoints.Num() - 1)
	{
		const int32 Leg = Path->Next;
		if (Path->LegEdge[Leg] < 0)
		{
			Board(); // on the column: boarding ends the sub-step, the leftover time is dropped
			return false;
		}
		const FVector W = Path->Waypoints[Leg];
		const double Mul = Path->LegEdge[Leg] > 0 ? GetRoadEdgeSpeedMul(Path->LegEdge[Leg]) : 1.0;
		const double V = (double)SpeedMps * 100.0 * Mul;
		FVector Pos = Transform.GetLocation();
		const FVector To(W.X - Pos.X, W.Y - Pos.Y, 0.0);
		const double Dist = To.Size2D();
		if (V <= 0.0)
		{
			return false;
		}
		if (Dist > 0.0)
		{
			Transform.SetRotation(FRotationMatrix::MakeFromX(To).ToQuat());
		}
		const double Need = Dist / V;
		if (Need <= RemT)
		{
			Pos.X = W.X;
			Pos.Y = W.Y;
			Transform.SetLocation(Pos);
			RemT -= Need;
			++Path->Next;
			if (Path->Next < Path->Waypoints.Num() - 1 && Path->LegEdge[Path->Next] < 0)
			{
				Board();
				return false;
			}
		}
		else
		{
			Pos += To.GetSafeNormal2D() * (V * RemT);
			Transform.SetLocation(Pos);
			return false;
		}
	}
	const bool bArrived = RH::MoveToward(Transform, TargetCm, SpeedMps, (float)FMath::Max(0.0, RemT));
	if (bArrived)
	{
		RobotPaths.Remove(Entity);
	}
	return bArrived;
}

bool URHSimWorldSubsystem::ChargePadGone(FMassEntityHandle Entity, int32 PadId)
{
	if (HasBuilding(PadId))
	{
		return false;
	}
	DropDeepPath(Entity); // the task is reset without a step taken: a ride in progress was for the pad that is gone
	return true;
}

bool URHSimWorldSubsystem::GetRideState(FMassEntityHandle Entity, FRHRideState& Out) const
{
	const FRHRobotRoadPath* Path = RobotPaths.Find(Entity);
	if (!Path || !Path->bDeep || !Path->bBoarded || !Path->LegEdge.IsValidIndex(Path->Next) || Path->LegEdge[Path->Next] >= 0)
	{
		return false;
	}
	Out.Shaft = -Path->LegEdge[Path->Next] - 1;
	Out.FromLevel = Path->RideFromLevel;
	Out.ToLevel = Path->Levels[Path->Next];
	Out.HoldStepsLeft = Path->HoldStepsLeft;
	Out.RideStepsLeft = Path->RideStepsLeft;
	Out.RideSteps = Path->RideSteps;
	return true;
}

// --- The charge reserve below (1.5, REVISION item 7) ---

float URHSimWorldSubsystem::RobotDriveWh(const FRHRobotFragment& Robot, double DistCm)
{
	// The StateTree's DriveWh, the same expression: W x sol-hours (50 sim-s each) x 1.25, plus half an hour of the move draw.
	const double Secs = DistCm / FMath::Max(1.0, (double)Robot.SpeedMps * 100.0);
	return (float)(Robot.DrawMoveW * Secs / 50.0 * 1.25) + Robot.DrawMoveW * 0.5f;
}

float URHSimWorldSubsystem::DeepReserveAt(FMassEntityHandle Entity, const FRHRobotFragment& Robot, int32 Level, const FVector& PosCm, int32 PadId, const FVector& PadCm) const
{
	const double Stamp = PeekRobotStamp();
	const int32 RobotSol = TransitSolOf(Stamp - GetTransitRobotLagSeconds());
	// The memo's spot: a cell (ordinal + 1) or a column (-(portal + 1)); 0 = no memo (the surface, or off the lattice).
	int32 Spot = 0;
	if (Level < 0)
	{
		const int32 Col = FindPortalColumnAt(Level, PosCm);
		if (Col != INDEX_NONE)
		{
			Spot = -(Col + 1);
		}
		else
		{
			const int32 Ord = FindCarvedCellAt(Level, PosCm);
			Spot = Ord != INDEX_NONE ? Ord + 1 : 0;
		}
	}
	FRHReserveMemo* Memo = Spot != 0 ? &ReserveMemos.FindOrAdd(Entity) : nullptr;
	if (Memo && Memo->bValid && Memo->Level == Level && Memo->Spot == Spot && Memo->PadId == PadId && Memo->RobotSol == RobotSol
		&& Memo->Cells == FloorCellsVersion && Memo->ShaftsV == ShaftsVersion && Memo->Lift == LiftVersion && Memo->Built == RoadGraph.BuiltVersion && Memo->TunnelsV == TunnelsVersion)
	{
		return Memo->Wh;
	}
	// The route actually driven: PlanDeepRoute to the pad EnterState picks. Its waits are the robot's own; every ascent hold
	// is priced at MaxWaitSec (a threshold crossed on the walk to the landing can set any wait up to it), every descent at
	// the frozen wait.
	FRHRobotRoadPath Route;
	FRHDeepRouteCost Cost;
	float Wh = 0.f;
	if (PlanDeepRoute(Level, PosCm, 0, PadCm, Stamp, Route, &Cost))
	{
		Wh = RobotDriveWh(Robot, (double)(Cost.LatticeCm + Cost.SurfaceCm))
			+ (float)(Robot.DrawMoveW * (double)Cost.RideSeconds / 50.0 * 1.25)
			+ (float)(Robot.DrawIdleW * ((double)Cost.AscentRides * (double)MaxWaitSec + (double)Cost.DescentHoldSeconds) / 50.0 * 1.25);
	}
	if (Memo)
	{
		Memo->bValid = true;
		Memo->Level = Level;
		Memo->Spot = Spot;
		Memo->PadId = PadId;
		Memo->RobotSol = RobotSol;
		Memo->Cells = FloorCellsVersion;
		Memo->ShaftsV = ShaftsVersion;
		Memo->Lift = LiftVersion;
		Memo->Built = RoadGraph.BuiltVersion;
		Memo->TunnelsV = TunnelsVersion;
		Memo->Wh = Wh;
	}
	return Wh;
}

float URHSimWorldSubsystem::DeepReserveWh(FMassEntityHandle Entity, const FRHRobotFragment& Robot, const FVector& PosCm, int32 PadId, const FVector& PadCm) const
{
	return DeepReserveAt(Entity, Robot, LevelOfZ(PosCm.Z), PosCm, PadId, PadCm);
}

ERHDeepCharge URHSimWorldSubsystem::DeepChargeTest(FMassEntityHandle Entity, const FRHRobotFragment& Robot, const FVector& PosCm, float ChargeWh, float CapacityWh) const
{
	static const FName NRoverTransit(TEXT("Rover"));
	if (!bHasTransit || Robot.RobotClass == NRoverTransit)
	{
		return ERHDeepCharge::NotApplicable;
	}
	const int32 Level = LevelOfZ(PosCm.Z);
	FRHRideState Ride;
	const bool bRide = GetRideState(Entity, Ride);
	if (Level == 0 && !bRide)
	{
		return ERHDeepCharge::NotApplicable; // today's surface rule runs
	}
	if (bRide && Ride.IsAscent())
	{
		return ERHDeepCharge::Keep; // an ascent heads for the pads: it is never broken off
	}
	float NeedWh = 0.f;
	if (bRide)
	{
		// A boarded descent: the reserve as if the robot stood on the destination landing, plus the hold and the ride left.
		const FRHDeepNet& Net = EnsureDeepNet();
		const int32* At = Net.PortalAt.Find(FIntPoint(Ride.Shaft, Ride.ToLevel));
		if (!At)
		{
			return ERHDeepCharge::Keep;
		}
		const FRHDeepPortal& P = Net.Portals[*At];
		const FVector LandingCm = FloorCellXY(P.Level, P.Landing);
		int32 PadId = 0;
		FVector PadCm;
		if (!FindNearestChargePad(LandingCm, PadId, PadCm))
		{
			return ERHDeepCharge::Keep; // no pad: keep working (M0-c)
		}
		NeedWh = DeepReserveAt(Entity, Robot, P.Level, LandingCm, PadId, PadCm)
			+ (float)(Robot.DrawMoveW * ((double)Ride.RideStepsLeft / 10.0) / 50.0 * 1.25)
			+ (float)(Robot.DrawIdleW * ((double)Ride.HoldStepsLeft / 10.0) / 50.0 * 1.25);
	}
	else
	{
		int32 PadId = 0;
		FVector PadCm;
		if (!FindNearestChargePad(PosCm, PadId, PadCm))
		{
			return ERHDeepCharge::Keep;
		}
		NeedWh = DeepReserveAt(Entity, Robot, Level, PosCm, PadId, PadCm);
	}
	const float Frac = CapacityWh > 0.f ? ChargeWh / CapacityWh : 0.f;
	return (Frac >= GetChargeSeekFraction() && ChargeWh >= NeedWh) ? ERHDeepCharge::Keep : ERHDeepCharge::Seek;
}

// --- The head hand-off pays its shaft (2.4, REVISION item 6) ---

int32 URHSimWorldSubsystem::ProjectHandoffHoldSteps(int32 TaskId, int32 RobotLevel, double Stamp) const
{
	if (!bHasTransit)
	{
		return 0;
	}
	const FRHTask* T = Tasks.FindByPredicate([TaskId](const FRHTask& Each) { return Each.Id == TaskId; });
	if (!T || T->Type != ERHTaskType::Haul)
	{
		return 0;
	}
	const FRHBuildingInstance* B = FindBuildingById(T->To.BuildingId);
	if (!B || B->Level == RobotLevel)
	{
		return 0;
	}
	const int32 S = ServingShaftOfSite(T->To, RobotLevel);
	return 10 * (GetRobotWaitSec(S, Stamp) + GetShaftDwellSeconds(S) + FMath::Abs(B->Level - RobotLevel) * GetShaftSecondsPerFloor(S));
}

int32 URHSimWorldSubsystem::BeginHandoffHold(FMassEntityHandle Entity, int32 TaskId, int32 RobotLevel, const FVector& PosCm)
{
	const int32 H = ProjectHandoffHoldSteps(TaskId, RobotLevel, PeekRobotStamp());
	if (H <= 0)
	{
		HandoffHolds.Remove(Entity);
		return 0;
	}
	FRHHandoffHold& Hold = HandoffHolds.FindOrAdd(Entity);
	Hold.TaskId = TaskId;
	Hold.StepsLeft = H;
	Hold.AtCm = PosCm;
	++HandoffHoldsBegun;
	return H;
}

bool URHSimWorldSubsystem::InHandoffHold(FMassEntityHandle Entity, int32 TaskId, const FVector& PosCm)
{
	const FRHHandoffHold* Hold = HandoffHolds.Find(Entity);
	if (!Hold)
	{
		return false;
	}
	if (Hold->TaskId != TaskId || Hold->AtCm.X != PosCm.X || Hold->AtCm.Y != PosCm.Y)
	{
		HandoffHolds.Remove(Entity); // stale: another task, or a robot that has moved since (a seek reset its fragment)
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::StepHandoffHold(FMassEntityHandle Entity)
{
	FRHHandoffHold* Hold = HandoffHolds.Find(Entity);
	if (!Hold)
	{
		return true;
	}
	if (!Tasks.ContainsByPredicate([Id = Hold->TaskId](const FRHTask& T) { return T.Id == Id; }))
	{
		HandoffHolds.Remove(Entity); // the task vanished (a demolish): the hold ends at once and HaulUnload handles it
		return true;
	}
	if (--Hold->StepsLeft <= 0)
	{
		HandoffHolds.Remove(Entity);
		return true;
	}
	return false;
}

float URHSimWorldSubsystem::GetHandoffHoldReserveWh(FMassEntityHandle Entity, int32 TaskId, const FRHRobotFragment& Robot, int32 RobotLevel, const FVector& PosCm) const
{
	if (!bHasTransit)
	{
		return 0.f;
	}
	const FRHHandoffHold* Hold = HandoffHolds.Find(Entity);
	const bool bHolding = Hold && Hold->TaskId == TaskId && Hold->AtCm.X == PosCm.X && Hold->AtCm.Y == PosCm.Y;
	const int32 Steps = bHolding ? Hold->StepsLeft : ProjectHandoffHoldSteps(TaskId, RobotLevel, PeekRobotStamp());
	return Steps > 0 ? (float)(Robot.DrawIdleW * ((double)Steps / 10.0) / 50.0 * 1.25) : 0.f;
}

bool URHSimWorldSubsystem::HandoffHoldShort(FMassEntityHandle Entity, int32 TaskId, float ChargeWh, const FRHRobotFragment& Robot, const FVector& PosCm, const FVector& DropCm) const
{
	// The legacy brain's form of the StateTree's mid-delivery test with a hold: the drive to the drop, the hold, and the
	// drive from the drop to its pad. True = the hauler cannot afford to finish the drop (it seeks, by today's rule).
	const float HoldWh = GetHandoffHoldReserveWh(Entity, TaskId, Robot, LevelOfZ(PosCm.Z), PosCm);
	if (HoldWh <= 0.f)
	{
		return false;
	}
	int32 PadId = 0;
	FVector PadCm;
	if (!FindNearestChargePad(DropCm, PadId, PadCm))
	{
		return false;
	}
	const float NeedWh = RobotDriveWh(Robot, FVector::DistXY(PosCm, DropCm)) + RobotDriveWh(Robot, FVector::DistXY(DropCm, PadCm)) + HoldWh;
	return ChargeWh < NeedWh;
}

// --- Save v46: the transit block, straight after the tunnel block ---

void URHSimWorldSubsystem::WriteTransitBlock(FArchive& Ar) const
{
	// LiftShaftCount, then (ShaftId, Cars) per fitted shaft in Id order; TransitCheckedSol; LedgerCount, then per ledger its
	// ShaftId, three slots of (Sol, Kg as int64, Places), WaitSec and PrevWaitSec. The default colony writes 0, -1, 0: 12 bytes.
	int32 LiftCount = 0;
	for (const FRHShaft& Sh : Shafts)
	{
		LiftCount += Sh.LiftCars > 0 ? 1 : 0;
	}
	Ar << LiftCount;
	for (const FRHShaft& Sh : Shafts)
	{
		if (Sh.LiftCars > 0)
		{
			int32 Id = Sh.Id, Cars = Sh.LiftCars;
			Ar << Id << Cars;
		}
	}
	int32 Checked = TransitCheckedSol;
	Ar << Checked;
	int32 LedgerCount = TransitLedgers.Num();
	Ar << LedgerCount;
	for (int32 S = 0; S < TransitLedgers.Num(); ++S)
	{
		const FRHTransitLedger& L = TransitLedgers[S];
		int32 Id = S;
		Ar << Id;
		for (int32 k = 0; k < 3; ++k)
		{
			int32 Sol = L.Sol[k], Places = L.Places[k];
			int64 Kg = L.Kg[k];
			Ar << Sol << Kg << Places;
		}
		int32 Wait = L.WaitSec, Prev = L.PrevWaitSec;
		Ar << Wait << Prev;
	}
}

bool URHSimWorldSubsystem::ReadTransitBlock(FArchive& Ar, const TArray<FRHShaft>& InShafts, FRHLoadedTransit& Out, FString& OutError) const
{
	// Structural refusals only, taken before the live colony is touched: they set OutError and log nothing at Error level.
	const auto Refuse = [&OutError](const FString& Why) { OutError = FString::Printf(TEXT("Save transit block invalid: %s"), *Why); return false; };
	Out = FRHLoadedTransit();
	int32 LiftCount = 0;
	Ar << LiftCount;
	if (Ar.IsError() || LiftCount < 0 || LiftCount > InShafts.Num() || (int64)LiftCount * 8 > (int64)Ar.TotalSize() - Ar.Tell())
	{
		return Refuse(FString::Printf(TEXT("lift shaft count %d"), LiftCount));
	}
	int32 PrevId = INDEX_NONE;
	for (int32 n = 0; n < LiftCount; ++n)
	{
		int32 Id = 0, Cars = 0;
		Ar << Id << Cars;
		if (Ar.IsError())
		{
			return Refuse(TEXT("the lift list is cut short"));
		}
		if (!InShafts.IsValidIndex(Id))
		{
			return Refuse(FString::Printf(TEXT("a lift names shaft %d, which the file does not list"), Id));
		}
		if (Id <= PrevId)
		{
			return Refuse(FString::Printf(TEXT("lift shaft ids must ascend, found %d after %d"), Id, PrevId));
		}
		PrevId = Id;
		if (InShafts[Id].Depth == 0)
		{
			return Refuse(FString::Printf(TEXT("shaft %d has a lift but is not bored"), Id));
		}
		if (Cars < 1 || Cars > RH::MaxLiftCarsCap)
		{
			return Refuse(FString::Printf(TEXT("shaft %d has %d cars (the range is 1..%d)"), Id, Cars, RH::MaxLiftCarsCap));
		}
		Out.Cars.Emplace(Id, Cars);
	}
	Ar << Out.CheckedSol;
	if (Ar.IsError())
	{
		return Refuse(TEXT("the block is cut short"));
	}
	const bool bCars = LiftCount > 0;
	if (bCars ? Out.CheckedSol < 0 : Out.CheckedSol != INDEX_NONE)
	{
		return Refuse(bCars ? FString::Printf(TEXT("the threshold sol is %d with a car fitted"), Out.CheckedSol)
			: FString::Printf(TEXT("the threshold sol is %d with no car fitted"), Out.CheckedSol));
	}
	int32 LedgerCount = 0;
	Ar << LedgerCount;
	const int32 WantLedgers = bCars ? InShafts.Num() : 0;
	if (Ar.IsError() || LedgerCount != WantLedgers || (int64)LedgerCount * 60 > (int64)Ar.TotalSize() - Ar.Tell())
	{
		return Refuse(FString::Printf(TEXT("ledger count %d (expected %d)"), LedgerCount, WantLedgers));
	}
	for (int32 n = 0; n < LedgerCount; ++n)
	{
		FRHTransitLedger L;
		int32 Id = 0;
		Ar << Id;
		for (int32 k = 0; k < 3; ++k)
		{
			Ar << L.Sol[k] << L.Kg[k] << L.Places[k];
		}
		Ar << L.WaitSec << L.PrevWaitSec;
		if (Ar.IsError())
		{
			return Refuse(TEXT("the ledgers are cut short"));
		}
		if (Id != n)
		{
			return Refuse(FString::Printf(TEXT("ledger %d names shaft %d (one ledger a shaft, in Id order)"), n, Id));
		}
		for (int32 k = 0; k < 3; ++k)
		{
			if (L.Sol[k] < -1)
			{
				return Refuse(FString::Printf(TEXT("shaft %d slot %d is tagged %d"), Id, k, L.Sol[k]));
			}
			if (L.Sol[k] >= 0 && L.Sol[k] % 3 != k)
			{
				return Refuse(FString::Printf(TEXT("shaft %d slot %d holds sol %d, which belongs in slot %d"), Id, k, L.Sol[k], L.Sol[k] % 3));
			}
			// TransitCheckedSol + 1 is legal: a robots-first batch can stamp the new sol before the world rolls, and a save
			// made between the two batches of that frame records it (REVISION item 9(b)).
			if (L.Sol[k] > Out.CheckedSol + 1)
			{
				return Refuse(FString::Printf(TEXT("shaft %d slot %d is tagged sol %d, after the threshold sol %d + 1"), Id, k, L.Sol[k], Out.CheckedSol));
			}
			if (L.Kg[k] < 0 || L.Places[k] < 0)
			{
				return Refuse(FString::Printf(TEXT("shaft %d slot %d holds %lld kg and %d places"), Id, k, L.Kg[k], L.Places[k]));
			}
		}
		if (L.WaitSec < 0 || L.WaitSec > RH::MaxWaitSecCap || L.PrevWaitSec < 0 || L.PrevWaitSec > RH::MaxWaitSecCap)
		{
			return Refuse(FString::Printf(TEXT("shaft %d waits %d s (previous %d s; the range is 0..%d)"), Id, L.WaitSec, L.PrevWaitSec, RH::MaxWaitSecCap));
		}
		Out.Ledgers.Add(L);
	}
	return true;
}

void URHSimWorldSubsystem::InstallTransit(const FRHLoadedTransit& In)
{
	const bool bHad = bHasTransit;
	for (FRHShaft& Sh : Shafts)
	{
		Sh.LiftCars = 0;
	}
	for (const FIntPoint& Car : In.Cars)
	{
		if (Shafts.IsValidIndex(Car.X))
		{
			Shafts[Car.X].LiftCars = Car.Y;
		}
	}
	TransitCheckedSol = In.CheckedSol;
	TransitLedgers = In.Ledgers;
	bHasTransit = In.Cars.Num() > 0;
	if (bHad || bHasTransit)
	{
		// Only a colony that has (or had) a car moves the keys: with no car on either side nothing here changes.
		++LiftVersion;
		++TransitWaitVersion;
	}
	ResetRobotTransitRuntime();
}

// --- Save v58 (D6e): the transit power block, in its own frame ('RHD6') straight after E6's 'RHL6' ---

void URHSimWorldSubsystem::WriteTransitPowerBlock(FArchive& Ar) const
{
	// Count, then one entry per fitted shaft in Id order and then one per tram line in Id order: Kind (2 a lift, 3 a line), Id and the ring's
	// two slots as (sol tag, ms), an int32 each - 24 bytes an entry. The default colony writes Count 0: 4 bytes. Ms is clamped at one sol here
	// as well (SP3), so a slot never reads back as a repair.
	struct FEntry { int32 Kind = 0, Id = 0; const int32* Sol = nullptr; const int32* Ms = nullptr; };
	static const int32 EmptySol[2] = { -1, -1 }, EmptyMs[2] = { 0, 0 };
	TArray<FEntry> Entries;
	for (const FRHShaft& Sh : Shafts)
	{
		if (Sh.LiftCars > 0)
		{
			const bool bLedger = TransitLedgers.IsValidIndex(Sh.Id);
			Entries.Add({ 2, Sh.Id, bLedger ? TransitLedgers[Sh.Id].ShortSol : EmptySol, bLedger ? TransitLedgers[Sh.Id].ShortMs : EmptyMs });
		}
	}
	for (const FRHTramLine& Line : TramLines)
	{
		Entries.Add({ 3, Line.Id, Line.ShortSol, Line.ShortMs });
	}
	int32 Count = Entries.Num();
	Ar << Count;
	for (const FEntry& E : Entries)
	{
		int32 Kind = E.Kind, Id = E.Id;
		Ar << Kind << Id;
		for (int32 k = 0; k < 2; ++k)
		{
			int32 Sol = E.Sol[k], Ms = FMath::Clamp(E.Ms[k], 0, TransitShortMsCap);
			Ar << Sol << Ms;
		}
	}
}

void URHSimWorldSubsystem::ReadTransitPowerBlock(FArchive& Ar, const TArray<uint8>& File)
{
	// Read after teardown and after E6's block, once the shafts, their cars and the lines are installed, so it repairs with a Warning and never
	// refuses: every ring starts empty; a lost or malformed frame leaves them so; an entry this colony cannot hold is dropped; a slot that
	// cannot belong to its ring is emptied. A repair costs at most one sol's served share (an empty slot reads Served 1000).
	for (FRHTransitLedger& L : TransitLedgers)
	{
		L.ShortSol[0] = L.ShortSol[1] = -1;
		L.ShortMs[0] = L.ShortMs[1] = 0;
	}
	for (FRHTramLine& Line : TramLines)
	{
		Line.ShortSol[0] = Line.ShortSol[1] = -1;
		Line.ShortMs[0] = Line.ShortMs[1] = 0;
	}
	TArray<uint8> Payload;
	FString Why;
	if (!RHSaveFrame::Read(Ar, File, RHSaveFrame::TransitPowerTag, Payload, Why))
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v58: the transit power block is lost (%s); every lift's and tram line's power record starts empty"), *Why);
		return;
	}
	FMemoryReader In(Payload);
	int32 Count = 0;
	In << Count;
	// The compile-time caps, not the MaxShafts row (the brief's "MaxShafts + MaxTramLinesCap"): a colony whose MaxShafts row was lowered after it
	// sank its shafts keeps its rings. The exact-size test below still binds, and an entry naming a shaft the colony lacks is dropped.
	const int32 MaxCount = RH::MaxShaftsCap + RH::MaxTramLinesCap;
	if (In.IsError() || Count < 0 || Count > MaxCount || (int64)Count * 24 + 4 != (int64)Payload.Num())
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v58: the transit power block counts %d entries in %d bytes (0..%d entries of 24 bytes after the count); every power record starts empty"),
			Count, Payload.Num(), MaxCount);
		return;
	}
	TSet<int32> SeenShafts, SeenLines;
	for (int32 n = 0; n < Count; ++n)
	{
		int32 Kind = 0, Id = 0, Sol[2] = { -1, -1 }, Ms[2] = { 0, 0 };
		In << Kind << Id << Sol[0] << Ms[0] << Sol[1] << Ms[1];
		int32* RingSol = nullptr;
		int32* RingMs = nullptr;
		bool bRepeat = false;
		if (Kind == 2)
		{
			if (!Shafts.IsValidIndex(Id) || !TransitLedgers.IsValidIndex(Id))
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v58: a transit power entry names shaft %d, which the colony does not have; dropped"), Id);
				continue;
			}
			if (Shafts[Id].LiftCars <= 0)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v58: a transit power entry names shaft %d, which has no lift car; dropped"), Id);
				continue;
			}
			SeenShafts.Add(Id, &bRepeat);
			RingSol = TransitLedgers[Id].ShortSol;
			RingMs = TransitLedgers[Id].ShortMs;
		}
		else if (Kind == 3)
		{
			FRHTramLine* Line = TramLines.FindByPredicate([Id](const FRHTramLine& L) { return L.Id == Id; });
			if (!Line)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v58: a transit power entry names tram line %d, which the colony does not have; dropped"), Id);
				continue;
			}
			SeenLines.Add(Id, &bRepeat);
			RingSol = Line->ShortSol;
			RingMs = Line->ShortMs;
		}
		else
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v58: a transit power entry of kind %d (2 is a lift, 3 a tram line) is dropped"), Kind);
			continue;
		}
		const TCHAR* What = Kind == 2 ? TEXT("shaft") : TEXT("tram line");
		if (bRepeat)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v58: the transit power block names %s %d twice; the repeat is dropped"), What, Id);
			continue;
		}
		for (int32 k = 0; k < 2; ++k)
		{
			// Tag -1 is an empty slot (exempt from the parity test). A tag can never pass the threshold sol + 1 (StepTransit rolls before StepPower).
			if (Sol[k] < -1 || (Sol[k] >= 0 && Sol[k] % 2 != k) || Sol[k] > TransitCheckedSol + 1 || Ms[k] < 0 || Ms[k] > TransitShortMsCap)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v58: %s %d's power slot %d holds sol %d and %d ms (a slot holds the sols of its own parity up to the threshold sol %d + 1, and 0..%d ms); emptied"),
					What, Id, k, Sol[k], Ms[k], TransitCheckedSol, TransitShortMsCap);
				Sol[k] = -1;
				Ms[k] = 0;
			}
			RingSol[k] = Sol[k];
			RingMs[k] = Ms[k];
		}
	}
}

void URHSimWorldSubsystem::ResetRobotTransitRuntime()
{
	HandoffHolds.Reset();
	ReserveMemos.Reset();
	DeepNet = FRHDeepNet();
	RobotStepEndSeconds = -1.0;
}

void URHSimWorldSubsystem::RepairLoadedRobots(const TArray<FMassEntityHandle>& Handles, const TArray<int32>& Levels)
{
	// D3 REVISION item 3 / D4 1.6, run after the floors, shafts, tunnels and transit are installed and the shaft derivations
	// rebuilt: it judges against the loaded colony, never the one before. In order, with a Warning each.
	URHAgentSubsystem* Agents = GetWorld() ? GetWorld()->GetSubsystem<URHAgentSubsystem>() : nullptr;
	if (!Agents)
	{
		return;
	}
	for (int32 i = 0; i < Handles.Num() && i < Levels.Num(); ++i)
	{
		FVector Pos;
		if (!Agents->GetRobotPosition(Handles[i], Pos))
		{
			continue;
		}
		const int32 Level = Levels[i];
		if (Level != LevelOfZ(Pos.Z))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Load: robot %d's saved level %d disagrees with its Z (%.0f cm) - its Z is set to the level"), i, Level, Pos.Z);
			Pos.Z = Level * FloorHeightCm + RH::RobotZOffsetCm;
			Agents->SetRobotPosition(Handles[i], Pos);
		}
		if (Level >= 0)
		{
			continue;
		}
		if (FindPortalColumnAt(Level, Pos) != INDEX_NONE)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Load: robot %d on floor %d stands on a fitted column (saved mid-ride) - kept"), i, Level);
			continue;
		}
		const int32 Ord = bHasTransit ? FindCarvedCellAt(Level, Pos) : INDEX_NONE;
		if (Ord != INDEX_NONE)
		{
			const FRHDigInfo& Info = EnsureDigInfo(Level);
			if (Info.DigOf.IsValidIndex(Ord) && EnsureDeepNet().FittedDigs.Contains(FIntPoint(Level, Info.DigOf[Ord])))
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("Load: robot %d on floor %d stands in a dig a fitted lift reaches - kept"), i, Level);
				continue;
			}
		}
		// Rock, an unfitted dig, or no lift at all: the surface deploy ring (SpawnStartingFleet's).
		const float Angle = i * 0.8f;
		const FVector Ring(-800.f + 250.f * i, -600.f + 150.f * FMath::Sin(Angle), RH::RobotZOffsetCm);
		UE_LOG(LogRedHopeSim, Warning, TEXT("Load: robot %d on floor %d has no fitted lift reaching it - moved to the surface deploy ring"), i, Level);
		Agents->SetRobotPosition(Handles[i], Ring);
	}
}
