#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHRobotMovement.h"
#include "RHSaveFrame.h"
#include "Data/RHRows.h"
#include "Serialization/MemoryReader.h"

// D7 atriums (docs/qa/2026-10-23/d7-atriums-design.md: DIRECTOR ANSWER Q-A (a) and Q-B (a), REFRESH AT 31ffa6f, REVISION R1-R12; the build
// brief docs/qa/2026-10-26/preflight/D7-brief.md). An atrium converts a finished cavern of two or three floors: on every floor of its box but
// the deepest the box cells are OPEN. No cell is ever removed, moved or reordered (ordinals are keys, D1 R0): the open flag is derived here
// from the saved list of cavern Ids and the floor table, and never saved. Every branch elsewhere tests Atriums.Num() > 0 (or a floor's open
// count) first, so with no atrium nothing here is reached and every existing expression is evaluated exactly as before.

namespace
{
	const FIntPoint AtriumSteps4[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	const FName NAME_AtriumOxygen(TEXT("Oxygen"));

	bool SameFlagMaps(const TMap<int32, TArray<uint8>>& A, const TMap<int32, TArray<uint8>>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const TPair<int32, TArray<uint8>>& P : A) // a boolean AND over every entry: the visiting order cannot matter
		{
			const TArray<uint8>* Other = B.Find(P.Key);
			if (!Other || !(*Other == P.Value))
			{
				return false;
			}
		}
		return true;
	}
}

bool URHSimWorldSubsystem::FRHAtriumCells::operator==(const FRHAtriumCells& O) const
{
	if (bValid != O.bValid || Atriums != O.Atriums || Cells != O.Cells || OpenCount.Num() != O.OpenCount.Num() || !SameFlagMaps(Flags, O.Flags))
	{
		return false;
	}
	for (const TPair<int32, int32>& P : OpenCount)
	{
		const int32* Other = O.OpenCount.Find(P.Key);
		if (!Other || *Other != P.Value)
		{
			return false;
		}
	}
	return true;
}

// --- The open and facing flags ---------------------------------------------------------

bool URHSimWorldSubsystem::IsAtrium(int32 CavernId) const
{
	for (const FRHAtrium& A : Atriums)
	{
		if (A.CavernId == CavernId)
		{
			return true;
		}
	}
	return false;
}

void URHSimWorldSubsystem::DeriveAtriumCells(FRHAtriumCells& Out) const
{
	// Levels in floor-table order are not needed: each atrium writes only its own box floors, and two boxes never share a point (R7). The open
	// pass runs over every atrium before the facing pass, so a cell open under one atrium never faces another.
	Out = FRHAtriumCells();
	Out.bValid = true;
	Out.Atriums = AtriumsVersion;
	Out.Cells = FloorCellsVersion;
	if (Atriums.Num() == 0)
	{
		return;
	}
	for (const FRHAtrium& A : Atriums)
	{
		const FRHCavern* C = FindCavern(A.CavernId);
		if (!C)
		{
			continue;
		}
		for (int32 L = C->Top; L >= C->Bottom; --L)
		{
			const FRHFloor* Floor = FindFloor(L);
			if (!Floor)
			{
				continue;
			}
			TArray<uint8>& Flags = Out.Flags.FindOrAdd(L);
			if (Flags.Num() != Floor->Cells.Num())
			{
				Flags.SetNumZeroed(Floor->Cells.Num());
			}
			if (L == C->Bottom)
			{
				continue; // the hall floor: ordinary cells under the open volume
			}
			for (int32 Y = C->Min.Y; Y <= C->Max.Y; ++Y) // (Y, X) row order
			{
				for (int32 X = C->Min.X; X <= C->Max.X; ++X)
				{
					const int32 O = Floor->FindOrdinal(FIntPoint(X, Y));
					if (O != INDEX_NONE && (Flags[O] & 1) == 0)
					{
						Flags[O] |= 1;
						++Out.OpenCount.FindOrAdd(L);
					}
				}
			}
		}
	}
	// Facing (section 4): a carved, non-open cell on one of the atrium's floors, inside its box or 4-adjacent to a box point. No diagonal, no
	// reach across floors, no line of sight.
	for (const FRHAtrium& A : Atriums)
	{
		const FRHCavern* C = FindCavern(A.CavernId);
		if (!C)
		{
			continue;
		}
		for (int32 L = C->Top; L >= C->Bottom; --L)
		{
			const FRHFloor* Floor = FindFloor(L);
			TArray<uint8>* Flags = Out.Flags.Find(L);
			if (!Floor || !Flags)
			{
				continue;
			}
			for (int32 Y = C->Min.Y - 1; Y <= C->Max.Y + 1; ++Y)
			{
				for (int32 X = C->Min.X - 1; X <= C->Max.X + 1; ++X)
				{
					const bool bOutX = X < C->Min.X || X > C->Max.X, bOutY = Y < C->Min.Y || Y > C->Max.Y;
					if (bOutX && bOutY)
					{
						continue; // a corner of the ring touches the box only diagonally
					}
					const int32 O = Floor->FindOrdinal(FIntPoint(X, Y));
					if (O != INDEX_NONE && ((*Flags)[O] & 1) == 0)
					{
						(*Flags)[O] |= 2;
					}
				}
			}
		}
	}
}

const URHSimWorldSubsystem::FRHAtriumCells& URHSimWorldSubsystem::EnsureAtriumCells() const
{
	FRHAtriumCells& C = AtriumCells;
	if (!(C.bValid && C.Atriums == AtriumsVersion && C.Cells == FloorCellsVersion))
	{
		++AtriumCellBuilds;
		DeriveAtriumCells(AtriumCells);
	}
	else if (bVerifyCaches)
	{
		// D8's requirement 4: presentation reads these flags, so a stale reuse is an Error under -RHVerifyCaches.
		FRHAtriumCells Fresh;
		DeriveAtriumCells(Fresh);
		if (!(Fresh == C))
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the atrium cells are stale (%d floors reused; atriums v%u, cells v%u)"), C.Flags.Num(), AtriumsVersion, FloorCellsVersion);
		}
	}
	return AtriumCells;
}

const TArray<uint8>* URHSimWorldSubsystem::GetAtriumFlags(int32 Level) const
{
	// The hot path (every neighbour generator, every IsCellOpen): rebuilt when its key moves, compared only through EnsureAtriumCells' other
	// readers (each floor's open count, the facing count), which run once a floor a step, not once a cell.
	if (Atriums.Num() == 0)
	{
		return nullptr;
	}
	const FRHAtriumCells& C = (AtriumCells.bValid && AtriumCells.Atriums == AtriumsVersion && AtriumCells.Cells == FloorCellsVersion) ? AtriumCells : EnsureAtriumCells();
	return C.Flags.Find(Level);
}

bool URHSimWorldSubsystem::IsCellOpen(int32 Level, int32 Ordinal) const
{
	const TArray<uint8>* Flags = GetAtriumFlags(Level);
	return Flags && Flags->IsValidIndex(Ordinal) && ((*Flags)[Ordinal] & 1) != 0;
}

bool URHSimWorldSubsystem::IsCellFacing(int32 Level, int32 Ordinal) const
{
	const TArray<uint8>* Flags = GetAtriumFlags(Level);
	return Flags && Flags->IsValidIndex(Ordinal) && ((*Flags)[Ordinal] & 2) != 0;
}

bool URHSimWorldSubsystem::IsPointOpen(int32 Level, const FIntPoint& P) const
{
	if (Atriums.Num() == 0)
	{
		return false;
	}
	const FRHFloor* Floor = FindFloor(Level);
	return Floor && IsCellOpen(Level, Floor->FindOrdinal(P));
}

int32 URHSimWorldSubsystem::GetFloorOpenCells(int32 Level) const
{
	if (Atriums.Num() == 0)
	{
		return 0;
	}
	return EnsureAtriumCells().OpenCount.FindRef(Level);
}

void URHSimWorldSubsystem::GetAtriumLevels(TArray<int32>& Out) const
{
	Out.Reset();
	for (const FRHAtrium& A : Atriums)
	{
		if (const FRHCavern* C = FindCavern(A.CavernId))
		{
			for (int32 L = C->Top; L >= C->Bottom; --L)
			{
				Out.AddUnique(L);
			}
		}
	}
	Out.Sort();
}

void URHSimWorldSubsystem::InvalidateAtriumCaches()
{
	AtriumCells.bValid = false;
	AtriumFacing.bValid = false;
}

// --- Air -------------------------------------------------------------------------------

double URHSimWorldSubsystem::GetFloorO2RequiredKg(int32 Level) const
{
	double Kg = GetFloorCarvedCells(Level) * O2FillKgPerCell;
	const int32 Open = GetFloorOpenCells(Level);
	if (Open > 0)
	{
		Kg += (double)Open * O2FillKgPerCell * (double)(AtriumFillPermille - 1000) / 1000.0; // only a floor with open cells: never + 0
	}
	return Kg;
}

double URHSimWorldSubsystem::GetFloorO2LeakKgPerSol(int32 Level) const
{
	double Kg = GetFloorCarvedCells(Level) * O2LeakKgPerCellPerSol;
	const int32 Open = GetFloorOpenCells(Level);
	if (Open > 0)
	{
		Kg += (double)Open * O2LeakKgPerCellPerSol * (double)(AtriumLeakPermille - 1000) / 1000.0;
	}
	return Kg;
}

// --- Walking ---------------------------------------------------------------------------

bool URHSimWorldSubsystem::FindNearestWalkCell(int32 Level, int32 FromOrdinal, int32& OutOrdinal) const
{
	// Resolve's rule (brief P2): the nearest non-open carved cell by 4-steps through open cells, ties to the lowest ordinal. On a rim cell this
	// is R3's lowest-ordinal non-open 4-neighbour; in a void's interior it is the nearest rim cell. False when the open cells reach none.
	const FRHFloor* Floor = FindFloor(Level);
	if (!Floor || !Floor->Cells.IsValidIndex(FromOrdinal))
	{
		return false;
	}
	if (!IsCellOpen(Level, FromOrdinal))
	{
		OutOrdinal = FromOrdinal;
		return true;
	}
	TSet<int32> Seen;
	Seen.Add(FromOrdinal);
	TArray<int32> Frontier = { FromOrdinal }, Next;
	while (Frontier.Num() > 0)
	{
		int32 Best = INDEX_NONE;
		Next.Reset();
		for (const int32 Cur : Frontier)
		{
			for (const FIntPoint& D : AtriumSteps4)
			{
				const int32 O = Floor->FindOrdinal(Floor->Cells[Cur] + D);
				if (O == INDEX_NONE)
				{
					continue;
				}
				bool bSeen = false;
				Seen.Add(O, &bSeen);
				if (bSeen)
				{
					continue;
				}
				if (!IsCellOpen(Level, O))
				{
					Best = Best == INDEX_NONE ? O : FMath::Min(Best, O);
				}
				else
				{
					Next.Add(O);
				}
			}
		}
		if (Best != INDEX_NONE)
		{
			OutOrdinal = Best;
			return true;
		}
		Swap(Frontier, Next);
	}
	return false;
}

// --- Beds ------------------------------------------------------------------------------

int32 URHSimWorldSubsystem::CountFloorBedsWith(int32 Level, int32 ExtraOpen) const
{
	return CountFloorBedsImpl(Level, nullptr, ExtraOpen);
}

// --- The facing count ------------------------------------------------------------------

int32 URHSimWorldSubsystem::CountFacingHeadsWith(const FRHAtriumCells& Cells, const FRHDeepHomes& Homes) const
{
	// One pass over Colonists in array order, integers only. A head counts once, whether it faces by its home, its seat or both, and however many
	// atriums or floors it faces.
	const auto Faces = [&Cells](int32 Level, int32 Ordinal)
	{
		const TArray<uint8>* Flags = Cells.Flags.Find(Level);
		return Flags && Flags->IsValidIndex(Ordinal) && ((*Flags)[Ordinal] & 2) != 0;
	};
	int32 Heads = 0;
	for (const FRHColonist& C : Colonists)
	{
		bool bFaces = false;
		if (C.HomeHab == 0 && C.HomeLevel < 0)
		{
			const int32* Cell = Homes.HomeCell.Find(C.Id);
			if (!Cell)
			{
				Cell = Homes.AtriumHomeCell.Find(C.Id);
			}
			bFaces = Cell && Faces(C.HomeLevel, *Cell);
		}
		if (!bFaces)
		{
			if (const FIntVector* Key = ColonistStations.Find(C.Id))
			{
				if (Key->X < 0 && Key->Z == 0)
				{
					bFaces = Faces(Key->X, Key->Y);
				}
				else if (Key->X < 0 && Key->Y == INDEX_NONE)
				{
					const FRHBuildingInstance* B = FindBuildingById(Key->Z);
					bFaces = B && Faces(Key->X, FindCarvedCellAt(Key->X, B->LocationCm));
				}
			}
		}
		Heads += bFaces ? 1 : 0;
	}
	return Heads;
}

int32 URHSimWorldSubsystem::DeriveAtriumFacingHeads() const
{
	return CountFacingHeadsWith(EnsureAtriumCells(), EnsureDeepHomes());
}

int32 URHSimWorldSubsystem::GetAtriumFacingHeads() const
{
	if (Atriums.Num() == 0)
	{
		return 0;
	}
	// Step-2 fix 2: the bed assignment of NOW, before BedAssignVersion is read into the key.
	EnsureBedAssignments();
	FRHAtriumFacing& F = AtriumFacing;
	const bool bFresh = F.bValid && F.Atriums == AtriumsVersion && F.Cells == FloorCellsVersion && F.Colonists == ColonistSetVersion && F.Furniture == FurnitureVersion
		&& F.Zoning == ZoningVersion && F.Districts == DistrictsVersion && F.BedAssign == BedAssignVersion && F.Jobs == JobsVersion;
	if (!bFresh)
	{
		++AtriumFacingBuilds;
		F.bValid = true;
		F.Atriums = AtriumsVersion;
		F.Cells = FloorCellsVersion;
		F.Colonists = ColonistSetVersion;
		F.Furniture = FurnitureVersion;
		F.Zoning = ZoningVersion;
		F.Districts = DistrictsVersion;
		F.BedAssign = BedAssignVersion;
		F.Jobs = JobsVersion;
		F.Heads = DeriveAtriumFacingHeads();
	}
	else
	{
		++AtriumFacingReuses;
		if (bVerifyCaches)
		{
			const int32 Fresh = DeriveAtriumFacingHeads();
			if (Fresh != F.Heads)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the atrium facing count is stale (%d reused, %d derived; atriums v%u, cells v%u, colonists v%u, jobs v%u)"),
					F.Heads, Fresh, AtriumsVersion, FloorCellsVersion, ColonistSetVersion, JobsVersion);
			}
		}
	}
	return F.Heads;
}

uint32 URHSimWorldSubsystem::GetAtriumFacingKey() const
{
	// The overlay's redraw key: every input of the facing count (the facing cache's own key), 0 with no atrium.
	if (Atriums.Num() == 0)
	{
		return 0;
	}
	EnsureBedAssignments(); // step-2 fix 2: the bed assignment of NOW, before BedAssignVersion is read
	uint32 K = HashCombine(AtriumsVersion, FloorCellsVersion);
	K = HashCombine(K, HashCombine(ColonistSetVersion, FurnitureVersion));
	K = HashCombine(K, HashCombine(ZoningVersion, DistrictsVersion));
	K = HashCombine(K, HashCombine(BedAssignVersion, JobsVersion));
	return K == 0 ? 1u : K;
}

void URHSimWorldSubsystem::GetAtriumFacingCells(int32 Level, TArray<int32>& OutOrdinals) const
{
	// The overlay's soft glow (design section 8, "facing homes and seats glow softly"): the cells of Level that face an atrium AND hold a
	// facing head's home or seat, by the facing count's own rule (CountFacingHeadsWith). Ascending, each once. Empty with no atrium.
	OutOrdinals.Reset();
	if (Atriums.Num() == 0)
	{
		return;
	}
	EnsureBedAssignments();
	const TArray<uint8>* Flags = GetAtriumFlags(Level);
	if (!Flags)
	{
		return;
	}
	const auto Faces = [Flags](int32 Ordinal) { return Flags->IsValidIndex(Ordinal) && ((*Flags)[Ordinal] & 2) != 0; };
	const FRHDeepHomes& Homes = EnsureDeepHomes();
	TSet<int32> Cells;
	for (const FRHColonist& C : Colonists)
	{
		if (C.HomeHab == 0 && C.HomeLevel == Level)
		{
			const int32* Cell = Homes.HomeCell.Find(C.Id);
			if (!Cell)
			{
				Cell = Homes.AtriumHomeCell.Find(C.Id);
			}
			if (Cell && Faces(*Cell))
			{
				Cells.Add(*Cell);
			}
		}
		if (const FIntVector* Key = ColonistStations.Find(C.Id))
		{
			if (Key->X == Level && Key->Z == 0 && Faces(Key->Y))
			{
				Cells.Add(Key->Y);
			}
			else if (Key->X == Level && Key->Y == INDEX_NONE)
			{
				const FRHBuildingInstance* B = FindBuildingById(Key->Z);
				const int32 Cell = B ? FindCarvedCellAt(Level, B->LocationCm) : (int32)INDEX_NONE;
				if (Faces(Cell))
				{
					Cells.Add(Cell);
				}
			}
		}
	}
	OutOrdinals = Cells.Array();
	OutOrdinals.Sort();
}

int32 URHSimWorldSubsystem::GetColonistAtriumHomeCell(int32 ColonistId) const
{
	const FRHColonist* C = FindColonist(ColonistId);
	if (Atriums.Num() == 0 || !C || C->HomeHab != 0 || C->HomeLevel >= 0)
	{
		return INDEX_NONE;
	}
	const int32* Cell = EnsureDeepHomes().AtriumHomeCell.Find(ColonistId);
	return Cell ? *Cell : INDEX_NONE;
}

// --- The preflight ---------------------------------------------------------------------

bool URHSimWorldSubsystem::FootprintTouchesOpen(const FRHBuildingRow* Def, const FVector& LocationCm, int32 Level, int32 SizeX, int32 SizeY, const TFunctionRef<bool(const FIntPoint&)>& IsOpenAt) const
{
	// R6: every footprint cell - the footprint's corners from GetFootprintHalfCm through CmToLattice (region 0's frame), and every lattice point
	// between them. The corners are taken 1 cm inside the footprint, so a footprint that ends on a cell's edge does not claim the next cell.
	if (Level >= 0)
	{
		return false;
	}
	const FVector2D Half = GetFootprintHalfCm(Def, SizeX, SizeY);
	const FVector Origin = Shafts[0].HeadCm;
	const FVector2D Inset(FMath::Max(0.0, Half.X - 1.0), FMath::Max(0.0, Half.Y - 1.0));
	const FIntPoint Lo = CmToLattice(Origin, FVector(LocationCm.X - Inset.X, LocationCm.Y - Inset.Y, 0.0));
	const FIntPoint Hi = CmToLattice(Origin, FVector(LocationCm.X + Inset.X, LocationCm.Y + Inset.Y, 0.0));
	for (int32 X = Lo.X; X <= Hi.X; ++X)
	{
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			if (IsOpenAt(FIntPoint(X, Y)))
			{
				return true;
			}
		}
	}
	return false;
}

bool URHSimWorldSubsystem::FindAtriumOccupant(const FRHCavern& C, int32& OutLevel, int32& OutOrdinal, FString& OutWhat) const
{
	// Refusal 5 and the load's occupancy repair share this test. Floors top down (Top .. Bottom + 1), the box cells of each in ordinal order;
	// per cell, in order: furniture, a room, a station module, any floor building's footprint, a station key, a tunnel chain cell, an open
	// growth target. The first cell in use answers.
	for (int32 L = C.Top; L > C.Bottom; --L)
	{
		const FRHFloor* Floor = FindFloor(L);
		if (!Floor)
		{
			continue;
		}
		TArray<int32> Box;
		for (int32 X = C.Min.X; X <= C.Max.X; ++X)
		{
			for (int32 Y = C.Min.Y; Y <= C.Max.Y; ++Y)
			{
				const int32 O = Floor->FindOrdinal(FIntPoint(X, Y));
				if (O != INDEX_NONE)
				{
					Box.Add(O);
				}
			}
		}
		Box.Sort();
		// The footprint points of every building on this floor (R6: modules and every other floor building alike), once a floor.
		// And the station modules' cells (FindModuleOnCell's rule, gathered once a floor instead of once a cell).
		TMap<FIntPoint, int32> Footprint;
		TMap<int32, const FRHBuildingInstance*> ModuleAt;
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.Level != L)
			{
				continue;
			}
			const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
			FootprintTouchesOpen(Def, B.LocationCm, L, B.SizeX, B.SizeY, [&Footprint, &B](const FIntPoint& P) { Footprint.FindOrAdd(P, B.Id); return false; });
			if (GetProvidedRoom(B, /*bRequireOnline*/ false))
			{
				ModuleAt.FindOrAdd(FindCarvedCellAt(L, B.LocationCm), &B);
			}
		}
		// The box cells that are a station key (a seat or the seat priority list): one pass over the stations, against the box's ordinals.
		TSet<int32> BoxSet(Box);
		TSet<int32> Stations;
		for (const TPair<int32, FIntVector>& S : ColonistStations)
		{
			if (S.Value.X == L && S.Value.Z == 0 && BoxSet.Contains(S.Value.Y)) { Stations.Add(S.Value.Y); }
		}
		for (const FIntVector& K : SeatPriority)
		{
			if (K.X == L && K.Z == 0 && BoxSet.Contains(K.Y)) { Stations.Add(K.Y); }
		}
		for (const int32 O : Box)
		{
			const FIntPoint P = Floor->Cells[O];
			const FIntVector Key(L, O, 0);
			const FName Room = GetRoomAt(L, O);
			OutLevel = L;
			OutOrdinal = O;
			const FRHCellFurnish* CF = FurnishedCells.Find(Key);
			if (CF && CF->Ids.Num() > 0)
			{
				OutWhat = TEXT("is furnished"); // before the room: furniture always stands in a room, so the room's test would hide it
				return true;
			}
			if (!Room.IsNone())
			{
				const FRHRoomRow* Row = Defs ? Defs->GetRoom(Room) : nullptr;
				OutWhat = FString::Printf(TEXT("is zoned (%s)"), Row ? *Row->DisplayName : *Room.ToString());
				return true;
			}
			if (const FRHBuildingInstance* M = ModuleAt.FindRef(O))
			{
				const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(M->DefName) : nullptr;
				OutWhat = FString::Printf(TEXT("holds a station module (%s)"), Def ? *Def->DisplayName : *M->DefName.ToString());
				return true;
			}
			if (const int32* Id = Footprint.Find(P))
			{
				const FRHBuildingInstance* B = FindBuildingById(*Id);
				const FRHBuildingRow* Def = (B && Defs) ? Defs->GetBuilding(B->DefName) : nullptr;
				const FString Name = Def ? Def->DisplayName : FString(TEXT("building"));
				const bool bVowel = Name.Len() > 0 && FString(TEXT("AEIOUaeiou")).Contains(Name.Left(1), ESearchCase::CaseSensitive);
				OutWhat = FString::Printf(TEXT("is under %s %s"), bVowel ? TEXT("an") : TEXT("a"), *Name);
				return true;
			}
			if (Stations.Contains(O))
			{
				OutWhat = TEXT("is a work station");
				return true;
			}
			if (IsTunnelChainCell(L, P))
			{
				OutWhat = TEXT("is part of a tunnel");
				return true;
			}
			for (const FRHGrowthProject& G : GrowthProjects)
			{
				if (G.Level == L && L < 0 && ((G.Kind == ERHGrowthKind::CivicRoom && !G.bCarve && G.Index == O) || G.Points.Contains(P)))
				{
					OutWhat = TEXT("is held by a growth project");
					return true;
				}
			}
		}
	}
	return false;
}

int32 URHSimWorldSubsystem::FindAtriumMeeting(const FRHCavern& C) const
{
	// Refusal 10's test (R7): the first atrium, in the list's order, whose box shares a floor and a lattice point with C's box; else INDEX_NONE.
	for (const FRHAtrium& A : Atriums)
	{
		const FRHCavern* Other = FindCavern(A.CavernId);
		if (Other && Other->Bottom <= C.Top && C.Bottom <= Other->Top && Other->Min.X <= C.Max.X && C.Min.X <= Other->Max.X
			&& Other->Min.Y <= C.Max.Y && C.Min.Y <= Other->Max.Y)
		{
			return A.CavernId;
		}
	}
	return INDEX_NONE;
}

int32 URHSimWorldSubsystem::CountAtriumNewOpen(const FRHCavern& C, int32 Level, bool bMeetsAtrium) const
{
	// The box cells floor Level (an upper floor of C) would newly open: every box point, unless the box meets an atrium's box (D6a lets a box
	// take in dug points, an atrium's open cells among them), when the points open already are left out.
	const int32 BoxCells = C.GetSizeX() * C.GetSizeY();
	if (!bMeetsAtrium)
	{
		return BoxCells;
	}
	int32 N = 0;
	for (int32 X = C.Min.X; X <= C.Max.X; ++X)
	{
		for (int32 Y = C.Min.Y; Y <= C.Max.Y; ++Y)
		{
			N += IsPointOpen(Level, FIntPoint(X, Y)) ? 0 : 1;
		}
	}
	return N;
}

int32 URHSimWorldSubsystem::GetAtriumNewOpenCells(int32 CavernId, int32 Level) const
{
	// The card's figure: what refusals 6, 8 and 9 count for this floor (0 on the hall floor, off the box, or for a cavern that is an atrium).
	const FRHCavern* C = FindCavern(CavernId);
	if (!C || C->bCancelled || Level > C->Top || Level <= C->Bottom || IsAtrium(CavernId))
	{
		return 0;
	}
	return CountAtriumNewOpen(*C, Level, FindAtriumMeeting(*C) != INDEX_NONE);
}

bool URHSimWorldSubsystem::CanOpenAtrium(int32 CavernId, FString& OutReason, FRHAtriumPreview* OutPreview) const
{
	// Refusals 1-10 (REVISION, "The refusals, in full"), in order. All wording is a Gate-D placeholder (gate-d-wording.md, prefix L).
	const FRHCavern* C = FindCavern(CavernId);
	// 1. No such cavern, or cancelled.
	if (!C || C->bCancelled)
	{
		OutReason = C ? FString::Printf(TEXT("Cavern #%d was cancelled - only a finished cavern can open as an atrium"), CavernId)
			: FString::Printf(TEXT("Unknown cavern #%d"), CavernId);
		return false;
	}
	// 2. Already an atrium.
	if (IsAtrium(CavernId))
	{
		OutReason = FString::Printf(TEXT("Cavern #%d is already an atrium"), CavernId);
		return false;
	}
	// 3. Not finished.
	const int32 Left = GetCavernUncarved(*C);
	if (Left > 0)
	{
		OutReason = FString::Printf(TEXT("Finish digging it first: %d cell%s left"), Left, Left == 1 ? TEXT("") : TEXT("s"));
		return false;
	}
	// 4. One floor tall.
	if (C->GetFloors() < 2)
	{
		OutReason = TEXT("An atrium needs a cavern at least two floors tall");
		return false;
	}
	// 5. An upper-floor box cell in use.
	{
		int32 L = 0, O = 0;
		FString What;
		if (FindAtriumOccupant(*C, L, O, What))
		{
			OutReason = FString::Printf(TEXT("Floor %d cell %d %s - clear it before the atrium can open"), L, O, *What);
			return false;
		}
	}
	// The cells that would open, per upper floor (every box point: a finished box is carved through).
	const int32 BoxCells = C->GetSizeX() * C->GetSizeY();
	// A box that shares a point with an atrium (refusal 10, tested last in the REVISION's order) can hold cells that are open already: refusals
	// 6, 8 and 9 count only the box cells each floor would newly open (CountAtriumNewOpen, the card's count too), so such a box is refused by
	// refusal 10's reason, not by a count that takes its open cells twice. Counted only when the box meets an atrium's box in 3D; otherwise
	// every floor opens BoxCells.
	const int32 Meets = FindAtriumMeeting(*C);
	int32 NewOpen[RH::CavernMaxFloorsCap];
	for (int32 k = 0; k < RH::CavernMaxFloorsCap; ++k)
	{
		NewOpen[k] = BoxCells;
	}
	if (Meets != INDEX_NONE)
	{
		for (int32 L = C->Top; L > C->Bottom && C->Top - L < RH::CavernMaxFloorsCap; --L)
		{
			NewOpen[C->Top - L] = CountAtriumNewOpen(*C, L, true);
		}
	}
	const auto NewOpenOn = [&NewOpen, C, BoxCells](int32 L) { return C->Top - L < RH::CavernMaxFloorsCap ? NewOpen[C->Top - L] : BoxCells; };
	// 6. Beds: no upper floor may be left with fewer beds than residents. An atrium never rehouses anyone.
	int32 ResidentsOn[RH::CavernMaxFloorsCap] = {}; // index Top - L, one pass over the roster
	for (const FRHColonist& Col : Colonists)
	{
		if (Col.HomeHab == 0 && Col.HomeLevel <= C->Top && Col.HomeLevel > C->Bottom && C->Top - Col.HomeLevel < RH::CavernMaxFloorsCap)
		{
			++ResidentsOn[C->Top - Col.HomeLevel];
		}
	}
	for (int32 L = C->Top; L > C->Bottom; --L)
	{
		const int32 Residents = C->Top - L < RH::CavernMaxFloorsCap ? ResidentsOn[C->Top - L] : 0;
		if (Residents == 0)
		{
			continue;
		}
		// R12: the what-if count, reused while its inputs stand.
		const int32 Opening = NewOpenOn(L);
		FRHAtriumBedMemo& Memo = AtriumBedMemo.FindOrAdd(L);
		if (!(Memo.Furniture == FurnitureVersion && Memo.Zoning == ZoningVersion && Memo.Atriums == AtriumsVersion && Memo.Cells == FloorCellsVersion
			&& Memo.BedAssign == BedAssignVersion && Memo.ExtraOpen == Opening && Memo.Furniture != 0))
		{
			Memo.Furniture = FurnitureVersion;
			Memo.Zoning = ZoningVersion;
			Memo.Atriums = AtriumsVersion;
			Memo.Cells = FloorCellsVersion;
			Memo.BedAssign = BedAssignVersion;
			Memo.ExtraOpen = Opening;
			Memo.Beds = CountFloorBedsWith(L, Opening);
		}
		else
		{
			++AtriumBedMemoReuses;
			if (bVerifyCaches && CountFloorBedsWith(L, Opening) != Memo.Beds)
			{
				UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the atrium preflight's bed count of floor %d is stale (%d reused, %d derived)"), L, Memo.Beds, CountFloorBedsWith(L, Opening));
			}
		}
		const int32 Beds = Memo.Beds;
		if (Beds < Residents)
		{
			OutReason = FString::Printf(TEXT("Floor %d would have %d beds for %d residents: add beds or move people first"), L, Beds, Residents);
			return false;
		}
	}
	// 7. Walking: on each upper floor, a dig that holds box points must keep its non-open cells in one 4-connected piece, and a dig that keeps any
	// non-open cell and had a landing must keep a non-open landing. A dig that is entirely void is allowed. R12: the answer is reused while the
	// floor table, the atrium list and the shafts stand (the dig labels' own key); -RHVerifyCaches re-runs the test on a reuse and compares.
	{
		FRHAtriumWalkMemo& WalkMemo = AtriumWalkMemo.FindOrAdd(CavernId);
		const bool bWalkFresh = WalkMemo.Cells == FloorCellsVersion && WalkMemo.Atriums == AtriumsVersion && WalkMemo.Shafts == ShaftsVersion && WalkMemo.Cells != 0;
		if (!bWalkFresh)
		{
			WalkMemo.Cells = FloorCellsVersion;
			WalkMemo.Atriums = AtriumsVersion;
			WalkMemo.Shafts = ShaftsVersion;
			WalkMemo.Reason.Reset();
			WalkMemo.bOk = AtriumWalkTest(*C, WalkMemo.Reason);
		}
		else
		{
			++AtriumWalkMemoReuses;
			if (bVerifyCaches)
			{
				FString FreshWhy;
				const bool bFreshOk = AtriumWalkTest(*C, FreshWhy);
				if (bFreshOk != WalkMemo.bOk || FreshWhy != WalkMemo.Reason)
				{
					UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the atrium preflight's walk test of cavern %d is stale (reused %s '%s', derived %s '%s')"), CavernId,
						WalkMemo.bOk ? TEXT("ok") : TEXT("refused"), *WalkMemo.Reason, bFreshOk ? TEXT("ok") : TEXT("refused"), *FreshWhy);
				}
			}
		}
		if (!WalkMemo.bOk)
		{
			OutReason = WalkMemo.Reason;
			return false;
		}
	}
	// The premium, floor by floor (top down): the up-front bill is the sum of the shares, in this order, as execute adds them.
	double PremiumKg = 0.0, ExtraLeak = 0.0;
	for (int32 L = C->Top; L > C->Bottom; --L)
	{
		PremiumKg += (double)NewOpenOn(L) * O2FillKgPerCell * (double)(AtriumFillPermille - 1000) / 1000.0;
		ExtraLeak += (double)NewOpenOn(L) * O2LeakKgPerCellPerSol * (double)(AtriumLeakPermille - 1000) / 1000.0;
	}
	// 8. The air bill: growth's availability rule for Oxygen (never the breathing reserve, never the open quota's share).
	{
		TMap<FName, double> Bill;
		Bill.Add(NAME_AtriumOxygen, PremiumKg);
		FString Why;
		if (!GrowthBillFits(Bill, Why))
		{
			OutReason = FString::Printf(TEXT("Store %.0f kg of oxygen for the atrium's air first (%s)"), PremiumKg, *Why);
			return false;
		}
	}
	// 9. The air ceiling, per upper floor: its leak a sol with the premium may not pass what one floor's circulation can push in a sol. The floor's
	// own Cells count the open cells too; the hall floor is not checked (it gets no premium).
	const double CeilingKg = O2FillRateKgPerHour * (double)URHSimClockSubsystem::SolLengthSimSeconds / 50.0;
	for (int32 L = C->Top; L > C->Bottom; --L)
	{
		const int32 Open = GetFloorOpenCells(L) + NewOpenOn(L);
		const double LeakKg = ((double)GetFloorCarvedCells(L) + (double)Open * (double)(AtriumLeakPermille - 1000) / 1000.0) * O2LeakKgPerCellPerSol;
		if (LeakKg > CeilingKg + 1e-9)
		{
			// P10: in cells to open fewer, N = ceil(excess x 1000 / (AtriumLeakPermille - 1000)); the excess in cell-equivalents.
			const double ExcessCells = O2LeakKgPerCellPerSol > 0.0 ? (LeakKg - CeilingKg) / O2LeakKgPerCellPerSol : 0.0;
			const int32 Fewer = AtriumLeakPermille > 1000 ? FMath::CeilToInt32(ExcessCells * 1000.0 / (double)(AtriumLeakPermille - 1000) - 1e-9) : 0;
			// An atrium always opens its whole box, so the fix is a smaller box or a smaller floor (P10's N says by how much).
			OutReason = FString::Printf(TEXT("Floor %d would leak more air than its circulation can replace: choose a cavern with %d fewer cell%s on this floor, or a floor with fewer cells"),
				L, Fewer, Fewer == 1 ? TEXT("") : TEXT("s"));
			return false;
		}
	}
	// 10. Overlap: no shared floor and lattice point with another atrium's box (R7); the first such atrium in the list's order.
	if (Meets != INDEX_NONE)
	{
		OutReason = FString::Printf(TEXT("The box shares a point with the atrium over cavern %d: choose a cavern clear of it"), Meets);
		return false;
	}
	if (OutPreview)
	{
		FRHAtriumPreview& P = *OutPreview;
		P = FRHAtriumPreview();
		P.CavernId = CavernId;
		P.Top = C->Top;
		P.Bottom = C->Bottom;
		P.Min = C->Min;
		P.Max = C->Max;
		P.PremiumKg = PremiumKg;
		P.ExtraLeakKgPerSol = ExtraLeak;
		// The flags and the home cells as they would be with this atrium open (derived, nothing live is touched).
		FRHAtriumCells Would;
		DeriveAtriumCellsWith(Would, CavernId);
		FRHDeepHomes Homes;
		DeriveDeepHomesWith(Homes, Would, CavernId);
		// This box's own facing ring: a carved cell of one of its floors, inside the box or 4-adjacent to it, not open with every atrium as it
		// would be. A head that faces only another atrium is not counted, and a ring another atrium already has is not drawn as this one's.
		FRHAtriumCells Mine;
		Mine.bValid = true;
		for (int32 L = C->Top; L >= C->Bottom; --L)
		{
			const FRHFloor* Floor = FindFloor(L);
			const TArray<uint8>* WouldFlags = Would.Flags.Find(L);
			if (!Floor || !WouldFlags)
			{
				continue;
			}
			TArray<uint8>& Flags = Mine.Flags.Add(L);
			Flags.SetNumZeroed(Floor->Cells.Num());
			for (int32 Y = C->Min.Y - 1; Y <= C->Max.Y + 1; ++Y)
			{
				for (int32 X = C->Min.X - 1; X <= C->Max.X + 1; ++X)
				{
					const bool bOutX = X < C->Min.X || X > C->Max.X, bOutY = Y < C->Min.Y || Y > C->Max.Y;
					const int32 O = (bOutX && bOutY) ? (int32)INDEX_NONE : Floor->FindOrdinal(FIntPoint(X, Y));
					if (O != INDEX_NONE && WouldFlags->IsValidIndex(O) && ((*WouldFlags)[O] & 1) == 0)
					{
						Flags[O] |= 2;
					}
				}
			}
		}
		P.FacingHeads = CountFacingHeadsWith(Mine, Homes);
		for (int32 L = C->Top; L >= C->Bottom; --L)
		{
			const FRHFloor* Floor = FindFloor(L);
			const TArray<uint8>* Flags = Mine.Flags.Find(L);
			FRHAtriumFloorPreview& FP = P.Floors.AddDefaulted_GetRef();
			FP.Level = L;
			if (L > C->Bottom)
			{
				FP.OpenCells = BoxCells;
				FP.PremiumKg = (double)BoxCells * O2FillKgPerCell * (double)(AtriumFillPermille - 1000) / 1000.0;
				FP.ExtraLeakKgPerSol = (double)BoxCells * O2LeakKgPerCellPerSol * (double)(AtriumLeakPermille - 1000) / 1000.0;
				for (int32 X = C->Min.X; X <= C->Max.X; ++X)
				{
					for (int32 Y = C->Min.Y; Y <= C->Max.Y; ++Y)
					{
						P.OpenCells.Emplace(L, X, Y);
					}
				}
			}
			FP.HeadroomCells = GetFloorAirHeadroomCellsWith(L, FP.OpenCells);
			if (Floor && Flags)
			{
				for (int32 O = 0; O < Flags->Num(); ++O)
				{
					const FIntPoint Pt = Floor->Cells[O];
					if (((*Flags)[O] & 2) != 0 && !C->ContainsPoint(Pt))
					{
						P.FacingRing.Emplace(L, Pt.X, Pt.Y);
					}
				}
			}
		}
	}
	return true;
}

bool URHSimWorldSubsystem::AtriumWalkTest(const FRHCavern& C, FString& OutWhy) const
{
	// Refusal 7, floor by floor (top down): on each upper floor, a dig that holds box points must keep its non-open cells in one 4-connected
	// piece, and a dig that keeps any non-open cell and had a landing must keep a non-open landing. A dig that is entirely void is allowed.
	for (int32 L = C.Top; L > C.Bottom; --L)
	{
		const FRHFloor* Floor = FindFloor(L);
		if (!Floor)
		{
			continue;
		}
		const int32 N = Floor->Cells.Num();
		const FRHDigInfo& Info = EnsureDigInfo(L);
		TArray<uint8> Gone; // open after the open: the floor's open cells now, and the box
		Gone.Init(0, N);
		TSet<int32> Digs;
		for (int32 c = 0; c < N; ++c)
		{
			Gone[c] = IsCellOpen(L, c) ? 1 : 0;
		}
		for (int32 X = C.Min.X; X <= C.Max.X; ++X)
		{
			for (int32 Y = C.Min.Y; Y <= C.Max.Y; ++Y)
			{
				const int32 O = Floor->FindOrdinal(FIntPoint(X, Y));
				if (O != INDEX_NONE)
				{
					Gone[O] = 1;
					if (Info.DigOf.IsValidIndex(O) && Info.DigOf[O] != INDEX_NONE)
					{
						Digs.Add(Info.DigOf[O]);
					}
				}
			}
		}
		// Landing cells: a carved orthogonal neighbour of a column whose shaft reaches L.
		TSet<int32> Landings;
		for (const FRHShaft& Sh : Shafts)
		{
			if (!ShaftReaches(Sh.Id, L))
			{
				continue;
			}
			for (const FIntPoint& D : AtriumSteps4)
			{
				const int32 O = Floor->FindOrdinal(Sh.Column + D);
				if (O != INDEX_NONE)
				{
					Landings.Add(O);
				}
			}
		}
		TMap<int32, int32> Pieces;       // dig -> non-open components left
		TSet<int32> HadLanding, KeepsLanding;
		TArray<uint8> Seen;
		Seen.Init(0, N);
		TArray<int32> Stack;
		for (int32 c = 0; c < N; ++c)
		{
			const int32 Dig = Info.DigOf.IsValidIndex(c) ? Info.DigOf[c] : INDEX_NONE;
			if (Dig == INDEX_NONE || !Digs.Contains(Dig))
			{
				continue;
			}
			if (Landings.Contains(c))
			{
				HadLanding.Add(Dig);
				if (!Gone[c])
				{
					KeepsLanding.Add(Dig);
				}
			}
			if (Gone[c] || Seen[c])
			{
				continue;
			}
			++Pieces.FindOrAdd(Dig);
			Seen[c] = 1;
			Stack.Add(c);
			while (Stack.Num() > 0)
			{
				const int32 Cur = Stack.Pop(EAllowShrinking::No);
				for (const FIntPoint& D : AtriumSteps4)
				{
					const int32 O = Floor->FindOrdinal(Floor->Cells[Cur] + D);
					if (O != INDEX_NONE && !Gone[O] && !Seen[O] && Info.DigOf[O] == Dig)
					{
						Seen[O] = 1;
						Stack.Add(O);
					}
				}
			}
		}
		for (const int32 Dig : Digs)
		{
			const int32 Count = Pieces.FindRef(Dig);
			if (Count > 1)
			{
				OutWhy = FString::Printf(TEXT("On floor %d the atrium would cut the dig in two, and people and robots could not walk round it: dig a way round the box first"), L);
				return false;
			}
			if (Count == 1 && HadLanding.Contains(Dig) && !KeepsLanding.Contains(Dig))
			{
				OutWhy = FString::Printf(TEXT("On floor %d the atrium would open every lift landing of the dig: keep one landing on solid floor"), L);
				return false;
			}
		}
	}
	return true;
}

void URHSimWorldSubsystem::DeriveAtriumCellsWith(FRHAtriumCells& Out, int32 ExtraCavernId) const
{
	// The preview's flags: the live list plus one more atrium (kept ascending, as the list is). A const read: the list is swapped in and out of
	// a copy, never edited.
	TArray<FRHAtrium>& Live = const_cast<TArray<FRHAtrium>&>(Atriums);
	TArray<FRHAtrium> Saved = Live;
	FRHAtrium Extra;
	Extra.CavernId = ExtraCavernId;
	Live.Add(Extra);
	Live.Sort([](const FRHAtrium& A, const FRHAtrium& B) { return A.CavernId < B.CavernId; });
	DeriveAtriumCells(Out);
	Live = MoveTemp(Saved);
}

void URHSimWorldSubsystem::DeriveDeepHomesWith(FRHDeepHomes& Out, const FRHAtriumCells& Cells, int32 ExtraCavernId) const
{
	// The preview's home cells, with the same swap: the derivation reads the open flags through the live cache, so the would-be flags are put
	// in its place for the call and the live cache is put back after it.
	TArray<FRHAtrium>& Live = const_cast<TArray<FRHAtrium>&>(Atriums);
	TArray<FRHAtrium> Saved = Live;
	FRHAtrium Extra;
	Extra.CavernId = ExtraCavernId;
	Live.Add(Extra);
	Live.Sort([](const FRHAtrium& A, const FRHAtrium& B) { return A.CavernId < B.CavernId; });
	FRHAtriumCells Kept = MoveTemp(AtriumCells);
	AtriumCells = Cells;
	DeriveDeepHomes(Out);
	AtriumCells = MoveTemp(Kept);
	Live = MoveTemp(Saved);
}

// --- The verbs -------------------------------------------------------------------------

void URHSimWorldSubsystem::ExecuteOpenAtrium(const FRHCommand& Cmd)
{
	// The preflight runs again here: the box can be zoned, furnished or built on while the order is in flight.
	const double V = Cmd.Value;
	const int32 Id = (FMath::IsNaN(V) || V < 0.0 || V > (double)MAX_int32) ? 0 : FMath::RoundToInt(V);
	FString Reason;
	if (!CanOpenAtrium(Id, Reason))
	{
		OnCommandRejected.Broadcast(Cmd, Reason);
		return;
	}
	const FRHCavern* C = FindCavern(Id);
	const int32 BoxCells = C->GetSizeX() * C->GetSizeY();
	// R4: the premium is billed up front through growth's path, and each upper floor's pool gets its share at once, as growth fills a new
	// floor's volume at completion: every atrium floor is exactly as full after the open as before it, against its new requirement. No
	// NoteFlow: oxygen put into a floor's air is ledgered as Used only when it leaks.
	double PremiumKg = 0.0;
	for (int32 L = C->Top; L > C->Bottom; --L)
	{
		PremiumKg += (double)BoxCells * O2FillKgPerCell * (double)(AtriumFillPermille - 1000) / 1000.0;
	}
	TakeGrowthResource(NAME_AtriumOxygen, PremiumKg);
	for (int32 L = C->Top; L > C->Bottom; --L)
	{
		FloorO2Kg.FindOrAdd(L) += (double)BoxCells * O2FillKgPerCell * (double)(AtriumFillPermille - 1000) / 1000.0;
	}
	FRHAtrium A;
	A.CavernId = Id;
	int32 At = 0;
	while (At < Atriums.Num() && Atriums[At].CavernId < Id)
	{
		++At;
	}
	Atriums.Insert(A, At);
	++AtriumsVersion;
	InvalidateAtriumCaches();
	UE_LOG(LogRedHopeSim, Display, TEXT("Atrium opened over cavern %d (floors %d..%d, %d x %d): %d cells open, %.0f kg of oxygen billed for the premium fill"),
		Id, C->Top, C->Bottom, C->GetSizeX(), C->GetSizeY(), BoxCells * (C->GetFloors() - 1), PremiumKg);
}

void URHSimWorldSubsystem::ExecuteCloseAtrium(const FRHCommand& Cmd)
{
	// Always allowed while the cavern is an atrium: the flag clears, nothing is refunded and no cell changes; the fill above the new requirement
	// stays in each floor's pool and leaks down at the normal rate.
	const double V = Cmd.Value;
	const int32 Id = (FMath::IsNaN(V) || V < 0.0 || V > (double)MAX_int32) ? 0 : FMath::RoundToInt(V);
	const int32 At = Atriums.IndexOfByPredicate([Id](const FRHAtrium& A) { return A.CavernId == Id; });
	if (At == INDEX_NONE)
	{
		OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Cavern #%d is not an atrium"), Id));
		return;
	}
	Atriums.RemoveAt(At);
	++AtriumsVersion;
	InvalidateAtriumCaches();
	UE_LOG(LogRedHopeSim, Display, TEXT("Atrium over cavern %d closed: its floors are whole again (nothing refunded)"), Id);
}

bool URHSimWorldSubsystem::Debug_OpenAtrium(int32 CavernId, FString& OutReason)
{
	if (!CanOpenAtrium(CavernId, OutReason))
	{
		return false;
	}
	FRHCommand Cmd;
	Cmd.Verb = FName("OpenAtrium");
	Cmd.Value = CavernId;
	ExecuteOpenAtrium(Cmd);
	return IsAtrium(CavernId);
}

bool URHSimWorldSubsystem::Debug_CloseAtrium(int32 CavernId, FString& OutReason)
{
	if (!IsAtrium(CavernId))
	{
		OutReason = FString::Printf(TEXT("Cavern #%d is not an atrium"), CavernId);
		return false;
	}
	FRHCommand Cmd;
	Cmd.Verb = FName("CloseAtrium");
	Cmd.Value = CavernId;
	ExecuteCloseAtrium(Cmd);
	return !IsAtrium(CavernId);
}

void URHSimWorldSubsystem::Debug_AtriumKeyFields(TArray<uint32>& Out) const
{
	// The key fields D7 adds, as the caches hold them now: dig labels (the largest over the cached floors), the deep net, the deep homes, the
	// membership key, the commute key, the walk network, the robots' charge reserve memos (the largest).
	Out.Reset();
	uint32 Dig = 0;
	for (const TPair<int32, FRHDigInfo>& P : DigCache)
	{
		Dig = FMath::Max(Dig, P.Value.Atriums);
	}
	Out.Add(Dig);
	Out.Add(DeepNet.Atriums);
	Out.Add(DeepHomes.Atriums);
	Out.Add(MakeDistrictMembershipKey().Atriums);
	Out.Add(MakeCommuteKey().DeepAtriums);
	Out.Add(DeepWalk.Atriums);
	uint32 Reserve = 0; // the robots' charge reserve memos (P4): the largest over every robot's memo
	for (const TPair<FMassEntityHandle, FRHReserveMemo>& M : ReserveMemos)
	{
		Reserve = FMath::Max(Reserve, M.Value.Atriums);
	}
	Out.Add(Reserve);
}

// --- Save v59: the atrium block, in its own frame ('RHD7') after D6e's 'RHD6' ---------

void URHSimWorldSubsystem::WriteAtriumBlock(FArchive& Ar) const
{
	// The count, then each CavernId ascending (an int32 each). The default colony writes 4 bytes, so the frame is 24 (12 + 4 + 8).
	int32 Count = Atriums.Num();
	Ar << Count;
	for (const FRHAtrium& A : Atriums)
	{
		int32 Id = A.CavernId;
		Ar << Id;
	}
}

void URHSimWorldSubsystem::InstallAtriumPayload(const TArray<uint8>& Payload)
{
	// Option (i) (ORCHESTRATOR-RULINGS 3): read after teardown, once the caverns and the floor table are installed, so every structural failure
	// drops (the whole list or one atrium) with one Warning, never an Error and never a refusal. A dropped atrium loses nothing: its cells are
	// ordinary cells again.
	Atriums.Reset();
	FMemoryReader In(Payload);
	int32 Count = 0;
	In << Count;
	if (In.IsError() || Count < 0 || Count > RH::MaxCavernRecords || (int64)Count * 4 + 4 != (int64)Payload.Num())
	{
		UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v59: the atrium block counts %d atriums in %d bytes (0..%d of 4 bytes after the count); no atrium is open"),
			Count, Payload.Num(), RH::MaxCavernRecords);
		return;
	}
	int32 Prev = 0;
	for (int32 n = 0; n < Count; ++n)
	{
		int32 Id = 0;
		In << Id;
		if (Id <= Prev)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v59: the atrium list names cavern %d after %d (cavern Ids ascend); that atrium is dropped"), Id, Prev);
			continue;
		}
		Prev = Id;
		const FRHCavern* C = FindCavern(Id);
		const TCHAR* Bad = !C ? TEXT("names no cavern") : C->bCancelled ? TEXT("names a cancelled cavern") : GetCavernUncarved(*C) > 0 ? TEXT("names an unfinished cavern")
			: C->GetFloors() < 2 ? TEXT("names a cavern one floor tall") : nullptr;
		if (Bad)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v59: the atrium over cavern %d %s; that atrium is dropped"), Id, Bad);
			continue;
		}
		bool bOverlap = false;
		for (const FRHAtrium& A : Atriums)
		{
			const FRHCavern* O = FindCavern(A.CavernId);
			if (O && O->Bottom <= C->Top && C->Bottom <= O->Top && O->Min.X <= C->Max.X && C->Min.X <= O->Max.X && O->Min.Y <= C->Max.Y && C->Min.Y <= O->Max.Y)
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v59: atriums over caverns %d and %d overlap; the atrium over cavern %d is dropped"), A.CavernId, Id, Id);
				bOverlap = true;
				break;
			}
		}
		if (bOverlap)
		{
			continue;
		}
		FRHAtrium A;
		A.CavernId = Id;
		Atriums.Add(A);
	}
}

void URHSimWorldSubsystem::RepairAtriumOccupancy()
{
	// Section 2's occupancy repair, run after RefreshJobs, once rooms, furniture, buildings, stations, tunnels and growth are loaded: a hand-made
	// file can put something on an open cell, and that atrium loads closed (deviation 5), with one Warning.
	bool bDropped = false;
	for (int32 i = Atriums.Num() - 1; i >= 0; --i)
	{
		const FRHCavern* C = FindCavern(Atriums[i].CavernId);
		int32 L = 0, O = 0;
		FString What;
		if (C && FindAtriumOccupant(*C, L, O, What))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("LoadColony v59: atrium over cavern %d closed at load: floor %d cell %d %s"), Atriums[i].CavernId, L, O, *What);
			Atriums.RemoveAt(i);
			bDropped = true;
		}
	}
	if (bDropped)
	{
		++AtriumsVersion;
		InvalidateAtriumCaches();
	}
}

void URHSimWorldSubsystem::RemapRobotsOffOpenCells(const TArray<FMassEntityHandle>& Handles, const TArray<int32>& Levels)
{
	// Cross-check item 4: the atrium list came only from the tail read, after RepairLoadedRobots judged the robots without it, so a robot that
	// stands on an open cell is moved now: to the nearest non-open cell by Resolve's walk through open cells, else to the surface deploy ring.
	URHAgentSubsystem* Agents = GetWorld() ? GetWorld()->GetSubsystem<URHAgentSubsystem>() : nullptr;
	if (!Agents || Atriums.Num() == 0)
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
		const int32 Level = LevelOfZ(Pos.Z);
		if (Level >= 0 || FindPortalColumnAt(Level, Pos) != INDEX_NONE)
		{
			continue;
		}
		const int32 Ord = FindCarvedCellAt(Level, Pos);
		if (Ord == INDEX_NONE || !IsCellOpen(Level, Ord))
		{
			continue;
		}
		int32 Walk = INDEX_NONE;
		if (FindNearestWalkCell(Level, Ord, Walk))
		{
			FVector To = LatticeToCm(Shafts[0].HeadCm, FindFloor(Level)->Cells[Walk]);
			To.Z = Level * FloorHeightCm + RH::RobotZOffsetCm;
			UE_LOG(LogRedHopeSim, Warning, TEXT("Load: robot %d on floor %d stands on a cell open to an atrium - moved to the nearest floor cell (%d)"), i, Level, Walk);
			Agents->SetRobotPosition(Handles[i], To);
		}
		else
		{
			const float Angle = i * 0.8f;
			const FVector Ring(-800.f + 250.f * i, -600.f + 150.f * FMath::Sin(Angle), RH::RobotZOffsetCm);
			UE_LOG(LogRedHopeSim, Warning, TEXT("Load: robot %d on floor %d stands on a cell open to an atrium with no floor cell beside the void - moved to the surface deploy ring"), i, Level);
			Agents->SetRobotPosition(Handles[i], Ring);
		}
	}
}

bool URHSimWorldSubsystem::Debug_AtriumCachesFresh() const
{
	// True unless a cache is being REUSED (its key still matches) while a fresh derivation differs.
	bool bFresh = true;
	if (AtriumCells.bValid && AtriumCells.Atriums == AtriumsVersion && AtriumCells.Cells == FloorCellsVersion)
	{
		FRHAtriumCells Fresh;
		DeriveAtriumCells(Fresh);
		bFresh &= Fresh == AtriumCells;
	}
	const FRHAtriumFacing& F = AtriumFacing;
	if (Atriums.Num() > 0 && F.bValid && F.Atriums == AtriumsVersion && F.Cells == FloorCellsVersion && F.Colonists == ColonistSetVersion && F.Furniture == FurnitureVersion
		&& F.Zoning == ZoningVersion && F.Districts == DistrictsVersion && F.BedAssign == BedAssignVersion && F.Jobs == JobsVersion)
	{
		bFresh &= DeriveAtriumFacingHeads() == F.Heads;
	}
	return bFresh;
}

double URHSimWorldSubsystem::GetFloorAirCeilingKgPerSol() const
{
	return O2FillRateKgPerHour * (double)URHSimClockSubsystem::SolLengthSimSeconds / 50.0;
}

int32 URHSimWorldSubsystem::GetFloorAirHeadroomCells(int32 Level) const
{
	return GetFloorAirHeadroomCellsWith(Level, 0);
}

int32 URHSimWorldSubsystem::GetFloorAirHeadroomCellsWith(int32 Level, int32 ExtraOpen) const
{
	// The card's "air headroom: N cells" (R4): the cell-equivalents the floor's circulation can still carry with ExtraOpen more of its cells
	// open (refusal 9's sum: Cells + O_L x (AtriumLeakPermille - 1000) / 1000, against the ceiling's 480 kg a sol = 240 at the rows); negative
	// past the ceiling. ExtraOpen 0 is the floor as it stands.
	if (O2LeakKgPerCellPerSol <= 0.0)
	{
		return 0;
	}
	const double Equiv = (double)GetFloorCarvedCells(Level) + (double)(GetFloorOpenCells(Level) + ExtraOpen) * (double)(AtriumLeakPermille - 1000) / 1000.0;
	return FMath::FloorToInt32(GetFloorAirCeilingKgPerSol() / O2LeakKgPerCellPerSol - Equiv + 1e-9);
}
