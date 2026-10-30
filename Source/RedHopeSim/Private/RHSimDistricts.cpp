#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"

// C5a (living city gate 5a): districts. The data and the pure helpers are RHDistricts;
// this file is the colony's side of them - the three uplink verbs and their preflight,
// the lazy membership caches, the per-district counts and the city's demand readout.
// Everything is an integer sum or an id-ordered walk: no container's iteration order
// decides an outcome, and a colony that never paints a district pays for none of it.

namespace
{
	const FName NAME_LivingFn(TEXT("Living"));
	const FName NAME_OxygenLeg(TEXT("Oxygen"));

	bool LatticeLess(const FIntPoint& A, const FIntPoint& B)
	{
		return A.X != B.X ? A.X < B.X : A.Y < B.Y;
	}

	template <typename KeyT, typename ValueT>
	bool SameMap(const TMap<KeyT, ValueT>& A, const TMap<KeyT, ValueT>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const TPair<KeyT, ValueT>& P : A) // a boolean AND over every entry: the visiting order cannot matter
		{
			const ValueT* Other = B.Find(P.Key);
			if (!Other || !(*Other == P.Value))
			{
				return false;
			}
		}
		return true;
	}
}

const FRHDistrict* URHSimWorldSubsystem::FindDistrict(int32 Id) const
{
	const int32* Index = EnsureDistrictGeo().IndexById.Find(Id);
	return Index ? &Districts[*Index] : nullptr;
}

FIntPoint URHSimWorldSubsystem::LatticeOf(const FVector& Cm)
{
	// Cell centres sit on multiples of 10 m; a cell spans [10k - 5, 10k + 5) m on each axis -
	// the same half-open floor the habitat bays use (FindMemberBayAt).
	return FIntPoint(FMath::FloorToInt32(Cm.X / 1000.0 + 0.5), FMath::FloorToInt32(Cm.Y / 1000.0 + 0.5));
}

// --- The ground: who owns which cell and floor -------------------------------------

void URHSimWorldSubsystem::DeriveDistrictGeo(FRHDistrictGeo& Out) const
{
	Out = FRHDistrictGeo();
	Out.bValid = true;
	Out.Version = DistrictsVersion;
	for (int32 i = 0; i < Districts.Num(); ++i)
	{
		const FRHDistrict& D = Districts[i];
		Out.IndexById.Add(D.Id, i);
		if (D.IsFloor())
		{
			Out.FloorOwner.Add(D.Level, D.Id);
			continue;
		}
		if (D.IsDeep())
		{
			// D6b: a shape's points are region 0's underground frame and never enter CellOwner or FloorOwner.
			for (const FIntVector& P : D.Deep)
			{
				Out.DeepOwner.Add(P, D.Id);
				if (!Out.DeepFloor.Contains(P.Z))
				{
					Out.DeepFloor.Add(P.Z, D.Id); // the lowest Id of the shapes on the floor (Districts is Id-ascending)
				}
			}
			continue;
		}
		for (const FIntPoint& Cell : D.Lattice)
		{
			Out.CellOwner.Add(Cell, D.Id);
		}
	}
	// D6d: a floor that holds a tram line is mapped (a line is no shape: it never enters DeepFloor, DeepOwner or the paint refusal). A laid line
	// and a load both bump DistrictsVersion, so this re-derives with the rest.
	for (const FRHTramLine& L : TramLines)
	{
		++Out.TramOn.FindOrAdd(L.Level);
	}
}

const URHSimWorldSubsystem::FRHDistrictGeo& URHSimWorldSubsystem::EnsureDistrictGeo() const
{
	if (!DistrictGeo.bValid || DistrictGeo.Version != DistrictsVersion)
	{
		DeriveDistrictGeo(DistrictGeo);
	}
	else if (bVerifyCaches)
	{
		// The cache is about to be reused: derive it again and compare.
		FRHDistrictGeo Fresh;
		DeriveDistrictGeo(Fresh);
		if (!SameMap(Fresh.CellOwner, DistrictGeo.CellOwner) || !SameMap(Fresh.FloorOwner, DistrictGeo.FloorOwner) || !SameMap(Fresh.IndexById, DistrictGeo.IndexById)
			|| !SameMap(Fresh.DeepOwner, DistrictGeo.DeepOwner) || !SameMap(Fresh.DeepFloor, DistrictGeo.DeepFloor) || !SameMap(Fresh.TramOn, DistrictGeo.TramOn))
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the district ground cache is stale (%d cells reused, %d re-derived; districts v%u)"),
				DistrictGeo.CellOwner.Num(), Fresh.CellOwner.Num(), DistrictsVersion);
		}
	}
	return DistrictGeo;
}

// --- Membership --------------------------------------------------------------------

FIntPoint URHSimWorldSubsystem::GetBuildingAnchorLattice(const FRHBuildingInstance& B) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
	const FIntPoint G = GetBayGridFor(Def, B.SizeX, B.SizeY);
	if (G.X > 0)
	{
		// A shell, in any state: the lattice cell of bay 0 - the same GridMin RebuildHabTopology
		// computes. One district per building: no bay of a habitat ever looks anywhere else.
		const FVector2D Half = GetFootprintHalfCm(Def, B.SizeX, B.SizeY);
		return FIntPoint(FMath::RoundToInt32((B.LocationCm.X - Half.X + 500.0) / 1000.0),
			FMath::RoundToInt32((B.LocationCm.Y - Half.Y + 500.0) / 1000.0));
	}
	return LatticeOf(B.LocationCm);
}

URHSimWorldSubsystem::FRHDistrictMembershipKey URHSimWorldSubsystem::MakeDistrictMembershipKey() const
{
	FRHDistrictMembershipKey Key;
	Key.Districts = DistrictsVersion;
	Key.Zoning = ZoningVersion;
	Key.Colonists = ColonistSetVersion;
	Key.Furniture = FurnitureVersion;
	Key.BuildingCount = Buildings.Num();
	Key.NextBuilding = NextBuildingId;
	if (EnsureDistrictGeo().DeepFloor.Num() > 0)
	{
		// D6b (RC 6): moved only while a floor shape exists; 0 for ever otherwise.
		Key.DeepCells = FloorCellsVersion;
		EnsureBedAssignments(); // step-2 fix 2: the generation of the assignment of NOW, so a key is never built from the old one and derived twice
		Key.DeepBeds = BedAssignVersion;
		// D7 (R5): an atrium opened or closed on a mapped floor moves HomeCell (its slots skip open cells). 0 while no atrium exists.
		Key.Atriums = Atriums.Num() > 0 ? AtriumsVersion : 0;
	}
	return Key;
}

// --- Floor shapes: the home cell, the district of a point ------------------------------

bool URHSimWorldSubsystem::IsFloorMapped(int32 Level) const
{
	return Level < 0 && EnsureDistrictGeo().IsMapped(Level); // D6d: a floor that holds a shape or a tram line
}

int32 URHSimWorldSubsystem::GetDistrictOfFloorPoint(int32 Level, const FIntPoint& P) const
{
	if (Level >= 0)
	{
		return 0;
	}
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();
	if (const int32* Whole = Geo.FloorOwner.Find(Level))
	{
		return *Whole;
	}
	return Geo.DeepOwner.FindRef(FIntVector(P.X, P.Y, Level));
}

int32 URHSimWorldSubsystem::GetDistrictOfCell(int32 Level, int32 CellIndex) const
{
	if (CellIndex < 0)
	{
		return 0;
	}
	if (!IsFloorMapped(Level))
	{
		return GetDistrictOfFloor(Level); // a whole floor's, as before D6b
	}
	return GetDistrictOfFloorPoint(Level, GetFloorCell(Level, CellIndex));
}

void URHSimWorldSubsystem::DeriveDeepHomes(FRHDeepHomes& Out) const
{
	Out = FRHDeepHomes();
	Out.bValid = true;
	Out.Colonists = ColonistSetVersion;
	Out.Furniture = FurnitureVersion;
	Out.Zoning = ZoningVersion;
	Out.FloorCells = FloorCellsVersion;
	Out.Districts = DistrictsVersion;
	Out.BedAssign = BedAssignVersion;
	Out.Atriums = Atriums.Num() > 0 ? AtriumsVersion : 0;
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();
	const bool bAtriums = Atriums.Num() > 0; // D7: the switch - every atrium branch below tests it first
	if (!Geo.AnyMapped() && !bAtriums)
	{
		return; // no shape, no line and no atrium: no mapped floor, home cells are never built
	}
	// Step-2 fix 2: the bed pieces of NOW (never the last RefreshJobs' or another colony's). The call may re-derive them and bump the generation, so the key's
	// BedAssign is stamped again after it. An unmapped colony returned above and never reaches it, so it derives exactly when it did.
	const TMap<int32, int32>& BedPieces = EnsureBedAssignments();
	Out.BedAssign = BedAssignVersion;
	TArray<int32> Levels;
	Geo.GetMappedLevels(Levels); // D6d: the floors that hold a shape or a tram line, ascending
	TArray<int32> AtriumLevels;
	if (bAtriums)
	{
		// D7 (section 4): every floor of every atrium's box too (upper and hall floors), as the ascending union with the mapped floors.
		GetAtriumLevels(AtriumLevels);
		for (const int32 L : AtriumLevels)
		{
			Levels.AddUnique(L);
		}
		Levels.Sort();
	}
	TMap<int32, TArray<int32>> LevelResidents; // mapped (or, D7, atrium) Level -> colonist Ids
	for (const FRHColonist& C : Colonists)
	{
		if (C.HomeHab == 0 && C.HomeLevel < 0 && (Geo.IsMapped(C.HomeLevel) || (bAtriums && AtriumLevels.Contains(C.HomeLevel))))
		{
			LevelResidents.FindOrAdd(C.HomeLevel).Add(C.Id);
		}
	}
	for (const int32 Level : Levels)
	{
		// D7 (R8): a mapped floor keeps its home cells in HomeCell (they change when its cells open); an atrium floor that is not mapped writes
		// AtriumHomeCell, never HomeCell, so HomeCell keeps exactly D6b's contents there.
		const bool bMapped = Geo.IsMapped(Level);
		const TArray<uint8>* Open = bAtriums ? GetAtriumFlags(Level) : nullptr;
		// The beds of every carved cell, by the CountFloorBeds rule: a Living cell with bed pieces holds exactly its beds; every other
		// cell holds legacy beds. The k-th such cell (ordinal order) offers floor((k+1) x CPC) - floor(k x CPC) of them, so the floor's
		// total is (int32)(m x CPC) at any ColonistsPerCell, the figure CountFloorBeds gives.
		const int32 Carved = GetFloorCarvedCells(Level);
		TArray<int32>& Beds = bMapped ? Out.CellBeds.Add(Level) : Out.AtriumCellBeds.Add(Level);
		Beds.Init(0, Carved);
		TArray<int32> Slots; // one entry per legacy bed: the ordinal of its cell, ascending
		int32 K = 0;
		for (int32 i = 0; i < Carved; ++i)
		{
			const FIntVector Key(Level, i, 0);
			if (Open && Open->IsValidIndex(i) && ((*Open)[i] & 1) != 0)
			{
				continue; // D7: an open cell holds no bed and no slot, and k does not advance, so the slots total the new CountFloorBeds
			}
			const FRHCellFurnish* CF = FurnishedCells.Find(Key);
			if (CF && CF->BedPieces > 0 && IsLivingKey(Key))
			{
				Beds[i] = CF->Beds;
				continue;
			}
			const int32 N = (int32)((double)(K + 1) * ColonistsPerCell) - (int32)((double)K * ColonistsPerCell);
			++K;
			Beds[i] = N;
			for (int32 j = 0; j < N; ++j)
			{
				Slots.Add(i);
			}
		}
		TArray<int32>* Ids = LevelResidents.Find(Level);
		if (!Ids)
		{
			continue;
		}
		Ids->Sort();
		TMap<int32, int32>& HomeOut = bMapped ? Out.HomeCell : Out.AtriumHomeCell;
		int32 NextSlot = 0;
		for (const int32 Id : *Ids)
		{
			// A resident with a bed piece lives in that piece's cell (BuildBedAssignments gave pieces to the lowest Ids first;
			// this reads its result and never re-derives it); the others, in Id order, take the legacy slots.
			const FRHFurnitureInstance* Piece = nullptr;
			if (const int32* PieceId = BedPieces.Find(Id))
			{
				Piece = FindFurniture(*PieceId);
			}
			if (Piece && Piece->Key.Z == 0 && Piece->Key.X == Level)
			{
				HomeOut.Add(Id, Piece->Key.Y);
			}
			else if (Slots.IsValidIndex(NextSlot))
			{
				HomeOut.Add(Id, Slots[NextSlot++]);
			}
		}
	}
}

const URHSimWorldSubsystem::FRHDeepHomes& URHSimWorldSubsystem::EnsureDeepHomes() const
{
	const FRHDeepHomes& H = DeepHomes;
	const bool bFresh = H.bValid && H.Colonists == ColonistSetVersion && H.Furniture == FurnitureVersion && H.Zoning == ZoningVersion
		&& H.FloorCells == FloorCellsVersion && H.Districts == DistrictsVersion && H.BedAssign == BedAssignVersion && H.Atriums == (Atriums.Num() > 0 ? AtriumsVersion : 0);
	if (!bFresh)
	{
		DeriveDeepHomes(DeepHomes);
	}
	else if (bVerifyCaches && !IsDeepHomesFresh())
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the floor-shape home cells are stale (%d reused; colonists v%u, furniture v%u, zoning v%u, cells v%u, districts v%u)"),
			DeepHomes.HomeCell.Num(), ColonistSetVersion, FurnitureVersion, ZoningVersion, FloorCellsVersion, DistrictsVersion);
	}
	return DeepHomes;
}

bool URHSimWorldSubsystem::IsDeepHomesFresh() const
{
	// True unless the cache is being REUSED (its key still matches) while a fresh derivation differs.
	const FRHDeepHomes& H = DeepHomes;
	if (!(H.bValid && H.Colonists == ColonistSetVersion && H.Furniture == FurnitureVersion && H.Zoning == ZoningVersion
		&& H.FloorCells == FloorCellsVersion && H.Districts == DistrictsVersion && H.BedAssign == BedAssignVersion && H.Atriums == (Atriums.Num() > 0 ? AtriumsVersion : 0)))
	{
		return true; // a cache whose key has moved reads as fresh: it rebuilds on its next read
	}
	FRHDeepHomes Fresh;
	DeriveDeepHomes(Fresh);
	if (!SameMap(Fresh.HomeCell, H.HomeCell) || Fresh.CellBeds.Num() != H.CellBeds.Num())
	{
		return false;
	}
	// D7 (R9): the atrium floors' home cells and beds are compared as well.
	if (!SameMap(Fresh.AtriumHomeCell, H.AtriumHomeCell) || Fresh.AtriumCellBeds.Num() != H.AtriumCellBeds.Num())
	{
		return false;
	}
	for (const TPair<int32, TArray<int32>>& P : Fresh.AtriumCellBeds)
	{
		const TArray<int32>* Other = H.AtriumCellBeds.Find(P.Key);
		if (!Other || !(*Other == P.Value))
		{
			return false;
		}
	}
	for (const TPair<int32, TArray<int32>>& P : Fresh.CellBeds) // a boolean AND over every entry
	{
		const TArray<int32>* Other = H.CellBeds.Find(P.Key);
		if (!Other || !(*Other == P.Value))
		{
			return false;
		}
	}
	return true;
}

int32 URHSimWorldSubsystem::GetColonistHomeCell(int32 ColonistId) const
{
	const FRHColonist* C = FindColonist(ColonistId);
	if (!C || C->HomeHab != 0 || C->HomeLevel >= 0 || !IsFloorMapped(C->HomeLevel))
	{
		return INDEX_NONE;
	}
	const int32* Cell = EnsureDeepHomes().HomeCell.Find(ColonistId);
	return Cell ? *Cell : INDEX_NONE;
}

void URHSimWorldSubsystem::DeriveDistrictMembership(FRHDistrictMembership& Out) const
{
	Out = FRHDistrictMembership();
	Out.bValid = true;
	Out.Key = MakeDistrictMembershipKey();
	const int32 N = Districts.Num();
	Out.Buildings.Init(0, N);
	Out.Bays.Init(0, N);
	Out.Residents.Init(0, N);
	if (N == 0)
	{
		return; // no districts: nothing belongs to any
	}
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();
	for (const FRHBuildingInstance& B : Buildings)
	{
		int32 DistrictId = 0;
		if (B.Level < 0)
		{
			DistrictId = Geo.FloorOwner.FindRef(B.Level);
			if (DistrictId == 0 && Geo.DeepFloor.Contains(B.Level) && Shafts.Num() > 0)
			{
				// D6b: on a shaped floor a building belongs to the shape holding its lattice point.
				const FIntPoint P = CmToLattice(Shafts[0].HeadCm, B.LocationCm);
				DistrictId = Geo.DeepOwner.FindRef(FIntVector(P.X, P.Y, B.Level));
			}
		}
		else if (B.Level == 0)
		{
			DistrictId = Geo.CellOwner.FindRef(GetBuildingAnchorLattice(B));
		}
		if (DistrictId == 0)
		{
			continue;
		}
		Out.OfBuilding.Add(B.Id, DistrictId);
		const int32 Index = Geo.IndexById.FindRef(DistrictId);
		++Out.Buildings[Index];
		if (!B.bUnderConstruction)
		{
			if (const FRHHabMemberCache* M = HabMembers.Find(B.Id))
			{
				Out.Bays[Index] += M->NX * M->NY; // a built shell's every bay is its building's
			}
		}
	}
	const FRHDeepHomes* Homes = Geo.DeepFloor.Num() > 0 ? &EnsureDeepHomes() : nullptr; // D6b: only while a shape exists
	for (const FRHColonist& C : Colonists)
	{
		int32 DistrictId = 0;
		if (C.HomeHab != 0)
		{
			DistrictId = Out.OfBuilding.FindRef(C.HomeHab);
		}
		else if (C.HomeLevel < 0)
		{
			DistrictId = Geo.FloorOwner.FindRef(C.HomeLevel);
			if (DistrictId == 0 && Homes && Geo.DeepFloor.Contains(C.HomeLevel))
			{
				// D6b: a resident of a shaped floor belongs to the shape holding their home cell (none: unzoned).
				const int32* Cell = Homes->HomeCell.Find(C.Id);
				if (Cell)
				{
					DistrictId = GetDistrictOfFloorPoint(C.HomeLevel, GetFloorCell(C.HomeLevel, *Cell));
				}
			}
		}
		if (DistrictId != 0)
		{
			++Out.Residents[Geo.IndexById.FindRef(DistrictId)];
		}
	}
}

const URHSimWorldSubsystem::FRHDistrictMembership& URHSimWorldSubsystem::EnsureDistrictMembership() const
{
	if (!DistrictMembers.bValid || !(DistrictMembers.Key == MakeDistrictMembershipKey()))
	{
		DeriveDistrictMembership(DistrictMembers);
	}
	else if (bVerifyCaches)
	{
		FRHDistrictMembership Fresh;
		DeriveDistrictMembership(Fresh);
		if (!SameMap(Fresh.OfBuilding, DistrictMembers.OfBuilding) || Fresh.Buildings != DistrictMembers.Buildings
			|| Fresh.Bays != DistrictMembers.Bays || Fresh.Residents != DistrictMembers.Residents)
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("RHVERIFY: the district membership cache is stale (%d buildings reused, %d re-derived; districts v%u, zoning v%u, colonists v%u, furniture v%u)"),
				DistrictMembers.OfBuilding.Num(), Fresh.OfBuilding.Num(), DistrictsVersion, ZoningVersion, ColonistSetVersion, FurnitureVersion);
		}
	}
	return DistrictMembers;
}

bool URHSimWorldSubsystem::Debug_DistrictCachesFresh() const
{
	bool bFresh = true;
	if (DistrictGeo.bValid && DistrictGeo.Version == DistrictsVersion)
	{
		FRHDistrictGeo Fresh;
		DeriveDistrictGeo(Fresh);
		bFresh &= SameMap(Fresh.CellOwner, DistrictGeo.CellOwner) && SameMap(Fresh.FloorOwner, DistrictGeo.FloorOwner) && SameMap(Fresh.IndexById, DistrictGeo.IndexById)
			&& SameMap(Fresh.DeepOwner, DistrictGeo.DeepOwner) && SameMap(Fresh.DeepFloor, DistrictGeo.DeepFloor) && SameMap(Fresh.TramOn, DistrictGeo.TramOn);
	}
	if (DistrictMembers.bValid && DistrictMembers.Key == MakeDistrictMembershipKey())
	{
		FRHDistrictMembership Fresh;
		DeriveDistrictMembership(Fresh);
		bFresh &= SameMap(Fresh.OfBuilding, DistrictMembers.OfBuilding) && Fresh.Buildings == DistrictMembers.Buildings
			&& Fresh.Bays == DistrictMembers.Bays && Fresh.Residents == DistrictMembers.Residents;
	}
	bFresh &= IsDeepHomesFresh();
	return bFresh;
}

void URHSimWorldSubsystem::Debug_DeepKeys(uint32& OutMemberCells, uint32& OutMemberBeds, uint32& OutCommuteCells) const
{
	const FRHDistrictMembershipKey M = MakeDistrictMembershipKey();
	OutMemberCells = M.DeepCells;
	OutMemberBeds = M.DeepBeds;
	OutCommuteCells = MakeCommuteKey().DeepCells;
}

int32 URHSimWorldSubsystem::GetDistrictAtCell(const FIntPoint& Cell) const
{
	return EnsureDistrictGeo().CellOwner.FindRef(Cell);
}

int32 URHSimWorldSubsystem::GetDistrictOfFloor(int32 Level) const
{
	return Level < 0 ? EnsureDistrictGeo().FloorOwner.FindRef(Level) : 0;
}

int32 URHSimWorldSubsystem::GetDistrictAtPoint(int32 Level, const FVector& Cm) const
{
	if (Level < 0)
	{
		const int32 Whole = GetDistrictOfFloor(Level);
		if (Whole == 0 && Shafts.Num() > 0 && IsFloorMapped(Level))
		{
			return GetDistrictOfFloorPoint(Level, CmToLattice(Shafts[0].HeadCm, Cm)); // D6b: a shaped floor, in the underground frame
		}
		return Whole;
	}
	return GetDistrictAtCell(LatticeOf(Cm));
}

int32 URHSimWorldSubsystem::GetDistrictOfBuilding(int32 BuildingId) const
{
	return EnsureDistrictMembership().OfBuilding.FindRef(BuildingId);
}

int32 URHSimWorldSubsystem::GetDistrictOfColonist(int32 ColonistId) const
{
	const FRHColonist* C = FindColonist(ColonistId);
	if (!C)
	{
		return 0;
	}
	if (C->HomeHab != 0)
	{
		return GetDistrictOfBuilding(C->HomeHab);
	}
	if (C->HomeLevel >= 0)
	{
		return 0;
	}
	const int32 Whole = GetDistrictOfFloor(C->HomeLevel);
	if (Whole != 0 || !IsFloorMapped(C->HomeLevel))
	{
		return Whole;
	}
	const int32 Cell = GetColonistHomeCell(ColonistId); // D6b: the shape holding their home cell
	return Cell == INDEX_NONE ? 0 : GetDistrictOfFloorPoint(C->HomeLevel, GetFloorCell(C->HomeLevel, Cell));
}

void URHSimWorldSubsystem::GetDistrictCounts(TArray<FRHDistrictCounts>& Out) const
{
	Out.Reset();
	if (Districts.Num() == 0)
	{
		return;
	}
	const FRHDistrictMembership& M = EnsureDistrictMembership();
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();
	Out.SetNum(Districts.Num());
	for (int32 i = 0; i < Districts.Num(); ++i)
	{
		const FRHDistrict& D = Districts[i];
		FRHDistrictCounts& C = Out[i];
		C.DistrictId = D.Id;
		C.Cells = D.IsFloor() ? GetFloorCarvedCells(D.Level) : D.Lattice.Num();
		C.Buildings = M.Buildings[i];
		C.Bays = M.Bays[i];
		C.Residents = M.Residents[i];
		if (D.IsFloor() && RatedFloors.Contains(D.Level))
		{
			C.Beds += CountFloorBeds(D.Level, nullptr); // the GetHousingCapacity rule: a rated floor's beds
		}
		if (D.IsDeep())
		{
			// D6b: a shape's Cells are its carved points; its Beds are the beds of those cells on a rated floor.
			const FRHDeepHomes& Homes = EnsureDeepHomes();
			C.Cells = 0;
			for (const FIntVector& P : D.Deep)
			{
				const int32 Ordinal = FindFloorCell(P.Z, FIntPoint(P.X, P.Y));
				if (Ordinal == INDEX_NONE)
				{
					continue;
				}
				++C.Cells;
				const TArray<int32>* CellBeds = RatedFloors.Contains(P.Z) ? Homes.CellBeds.Find(P.Z) : nullptr;
				if (CellBeds && CellBeds->IsValidIndex(Ordinal))
				{
					C.Beds += (*CellBeds)[Ordinal];
				}
			}
		}
	}
	// Surface beds: a rated habitat's Living bays, once the vault is certified (the same gate
	// GetHousingCapacity applies), to the district the building belongs to.
	if (bVaultRated)
	{
		for (const FRHBuildingInstance& B : Buildings)
		{
			const int32 Owner = M.OfBuilding.FindRef(B.Id);
			if (Owner != 0 && HabMembers.Contains(B.Id) && IsHabRated(B.Id))
			{
				Out[Geo.IndexById.FindRef(Owner)].Beds += GetHabBeds(B.Id);
			}
		}
	}
	// Seats: the working stations (rated homes only, as RefreshJobs lists them). A station is a
	// cell (Level, Cell, 0), a shell bay (0, Bay, HabId) or a module (Level, INDEX_NONE, BuildingId).
	for (const TPair<FIntVector, int32>& Station : StationCapacity) // an integer sum: order-free
	{
		int32 Owner = 0;
		if (Station.Key.Y == INDEX_NONE || Station.Key.X == 0)
		{
			Owner = M.OfBuilding.FindRef(Station.Key.Z);
		}
		else if (Station.Key.X < 0)
		{
			Owner = Geo.FloorOwner.FindRef(Station.Key.X);
			if (Owner == 0 && Geo.DeepFloor.Contains(Station.Key.X))
			{
				Owner = GetDistrictOfFloorPoint(Station.Key.X, GetFloorCell(Station.Key.X, Station.Key.Y)); // D6b: the shape holding the cell
			}
		}
		if (Owner != 0)
		{
			Out[Geo.IndexById.FindRef(Owner)].Seats += Station.Value;
		}
	}
}

// --- The three verbs ---------------------------------------------------------------

bool URHSimWorldSubsystem::CanPaintDeepDistrict(FName Kind, int32 Level, int32 Floors, const FVector& FromCm, const FVector& ToCm, FString& OutReason, FRHDistrictPaintPreview* OutPreview, TArray<FIntVector>* OutAdded) const
{
	// D6b: a rectangle on the floors Level .. Level - Floors + 1, in region 0's underground frame. In place of grid coverage every
	// floor must be reached; rock may be painted (that is how a player zones a cavern before it is dug); shaft columns are left out.
	const FName K = RHDistricts::ResolveKind(Kind);
	if (OutPreview)
	{
		OutPreview->bDeep = true;
		OutPreview->Floors = Floors;
	}
	if (Floors > RH::CavernMaxFloorsCap)
	{
		OutReason = FString::Printf(TEXT("A shape spans 1 to %d floors (this one would span %d)"), RH::CavernMaxFloorsCap, Floors);
		return false;
	}
	const int64 Bottom64 = (int64)Level - (int64)Floors + 1;
	if (Bottom64 < -(int64)MaxDepth)
	{
		OutReason = FString::Printf(TEXT("No such floor (%lld)"), Bottom64);
		return false;
	}
	const int32 Bottom = (int32)Bottom64;
	if (!IsLevelConnected(Bottom) || Shafts.Num() == 0)
	{
		OutReason = FString::Printf(TEXT("Floor %d not reached - bore the shaft deeper"), Bottom);
		return false;
	}
	const FVector Origin = Shafts[0].HeadCm;
	const FIntPoint A = CmToLattice(Origin, FromCm), B = CmToLattice(Origin, ToCm);
	// Step-2 fix 5: a corner past the lattice bound is refused before any box loop runs (a rectangle at the int32 edge would overflow them).
	if (!RH::IsLatticeInRange(A) || !RH::IsLatticeInRange(B))
	{
		OutReason = TEXT("That rectangle is out of range");
		return false;
	}
	const FIntPoint Lo(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), Hi(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
	const int64 W = (int64)Hi.X - Lo.X + 1, H = (int64)Hi.Y - Lo.Y + 1;
	if (OutPreview)
	{
		OutPreview->Min = Lo;
		OutPreview->Max = Hi;
	}
	if (W > RHDistricts::MaxPaintSide || H > RHDistricts::MaxPaintSide)
	{
		OutReason = FString::Printf(TEXT("A shape is painted %d x %d cells (10 m each) at a time at most - this is %lld x %lld"),
			RHDistricts::MaxPaintSide, RHDistricts::MaxPaintSide, W, H);
		return false;
	}
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();
	// A floor is either whole or shaped: a shape on a floor held whole is refused, naming the district to erase.
	for (int32 L = Level; L >= Bottom; --L)
	{
		if (const int32* Whole = Geo.FloorOwner.Find(L))
		{
			const FRHDistrict& Held = Districts[Geo.IndexById.FindRef(*Whole)];
			if (OutPreview)
			{
				OutPreview->JoinId = Held.Id;
			}
			OutReason = FString::Printf(TEXT("Floor %d is already a whole %s district (%s) - erase it first, or paint the shape on another floor"), L, *Held.Kind.ToString(), *Held.Name);
			return false;
		}
	}
	// Who holds this ground. One district at most: a rectangle over two is refused, and so is one over a district of another Kind;
	// the same Kind joins the one it overlaps.
	int32 First = 0, Second = 0, InRect = 0, NewPoints = 0, Carved = 0;
	TArray<FIntVector> Added;
	for (int32 L = Level; L >= Bottom && Second == 0; --L)
	{
		for (int32 X = Lo.X; X <= Hi.X && Second == 0; ++X)
		{
			for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
			{
				const FIntPoint P(X, Y);
				if (ShaftColumns.Contains(P))
				{
					continue; // a column runs through every floor: never a district's point
				}
				++InRect;
				Carved += FindFloorCell(L, P) != INDEX_NONE ? 1 : 0;
				const int32 Owner = Geo.DeepOwner.FindRef(FIntVector(X, Y, L));
				if (Owner == 0)
				{
					++NewPoints;
					Added.Add(FIntVector(X, Y, L));
				}
				else if (First == 0)
				{
					First = Owner;
				}
				else if (Owner != First)
				{
					Second = Owner;
					break;
				}
			}
		}
	}
	if (OutPreview)
	{
		OutPreview->CellsInRect = InRect;
		OutPreview->CellsNew = NewPoints;
		OutPreview->CellsCarved = Carved;
	}
	if (Second != 0)
	{
		const FRHDistrict& D1 = Districts[Geo.IndexById.FindRef(First)];
		const FRHDistrict& D2 = Districts[Geo.IndexById.FindRef(Second)];
		OutReason = FString::Printf(TEXT("That rectangle crosses two districts (%s and %s) - paint inside one, or on open ground"), *D1.Name, *D2.Name);
		return false;
	}
	if (InRect == 0)
	{
		OutReason = TEXT("That rectangle is nothing but shaft columns - nothing to paint");
		return false;
	}
	const FRHDistrict* Joined = First != 0 ? &Districts[Geo.IndexById.FindRef(First)] : nullptr;
	if (Joined)
	{
		if (OutPreview)
		{
			OutPreview->JoinId = Joined->Id;
		}
		if (Joined->Kind != K)
		{
			OutReason = FString::Printf(TEXT("That ground overlaps %s, a %s district - erase it first, or paint %s elsewhere"),
				*Joined->Name, *Joined->Kind.ToString(), *K.ToString());
			return false;
		}
	}
	if (NewPoints == 0)
	{
		OutReason = FString::Printf(TEXT("That ground is already part of %s"), Joined ? *Joined->Name : TEXT("a district"));
		return false;
	}
	if (Geo.DeepOwner.Num() + NewPoints > RH::MaxDeepPoints)
	{
		OutReason = FString::Printf(TEXT("The city's shapes already hold %d points, the most the sim keeps (%d)"), Geo.DeepOwner.Num(), RH::MaxDeepPoints);
		return false;
	}
	if (OutAdded)
	{
		*OutAdded = MoveTemp(Added);
	}
	return true;
}

bool URHSimWorldSubsystem::CanPaintDistrict(FName Kind, int32 Level, const FVector& FromCm, const FVector& ToCm, FString& OutReason, FRHDistrictPaintPreview* OutPreview, int32 Floors) const
{
	if (OutPreview)
	{
		*OutPreview = FRHDistrictPaintPreview();
	}
	const FName K = RHDistricts::ResolveKind(Kind);
	if (K.IsNone())
	{
		OutReason = FString::Printf(TEXT("'%s' is not a district kind (Homes, Industry, Farming, Research or Civic)"), *Kind.ToString());
		return false;
	}
	if (Level > 0 || Level < -MaxDepth)
	{
		OutReason = FString::Printf(TEXT("No such floor (%d)"), Level);
		return false;
	}
	// D6b: a shape on a floor (Value >= 1 on an underground order); Floors 0 is today's whole-floor order below, line for line.
	if (Level < 0 && Floors >= 1)
	{
		return CanPaintDeepDistrict(Kind, Level, Floors, FromCm, ToCm, OutReason, OutPreview, nullptr);
	}
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();

	// A whole floor.
	if (Level < 0)
	{
		if (OutPreview)
		{
			OutPreview->bFloor = true;
		}
		if (!IsLevelConnected(Level))
		{
			OutReason = FString::Printf(TEXT("Floor %d not reached - bore the shaft deeper"), Level);
			return false;
		}
		if (const int32* Owner = Geo.FloorOwner.Find(Level))
		{
			const FRHDistrict& Held = Districts[Geo.IndexById.FindRef(*Owner)];
			if (OutPreview)
			{
				OutPreview->JoinId = Held.Id;
			}
			OutReason = Held.Kind == K
				? FString::Printf(TEXT("Floor %d is already %s (%s)"), Level, *Held.Name, *Held.Kind.ToString())
				: FString::Printf(TEXT("Floor %d is already a %s district (%s) - erase it first"), Level, *Held.Kind.ToString(), *Held.Name);
			return false;
		}
		// D6b: a floor is either whole or shaped - a whole-floor paint on a floor that holds a shape is refused, naming it.
		if (const int32* Shaped = Geo.DeepFloor.Find(Level))
		{
			const FRHDistrict& Held = Districts[Geo.IndexById.FindRef(*Shaped)];
			if (OutPreview)
			{
				OutPreview->JoinId = Held.Id;
			}
			OutReason = FString::Printf(TEXT("Floor %d already holds a shape (%s, a %s district) - erase it first"), Level, *Held.Name, *Held.Kind.ToString());
			return false;
		}
		return true;
	}

	// Surface ground: a rectangle of lattice cells.
	const FIntPoint A = LatticeOf(FromCm), B = LatticeOf(ToCm);
	// Step-2 fix 5: the same bound on the surface lattice (the loops below and ExecutePaintDistrict's run X <= Hi.X, ++X).
	if (!RH::IsLatticeInRange(A) || !RH::IsLatticeInRange(B))
	{
		OutReason = TEXT("That rectangle is out of range");
		return false;
	}
	const FIntPoint Lo(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), Hi(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y));
	const int64 W = (int64)Hi.X - Lo.X + 1, H = (int64)Hi.Y - Lo.Y + 1;
	if (OutPreview)
	{
		OutPreview->Min = Lo;
		OutPreview->Max = Hi;
	}
	if (W > RHDistricts::MaxPaintSide || H > RHDistricts::MaxPaintSide)
	{
		OutReason = FString::Printf(TEXT("A district is painted %d x %d cells (10 m each) at a time at most - this is %lld x %lld"),
			RHDistricts::MaxPaintSide, RHDistricts::MaxPaintSide, W, H);
		return false;
	}
	if (OutPreview)
	{
		OutPreview->CellsInRect = (int32)(W * H);
	}

	// Who holds this ground. One district at most: a rectangle over two is refused, and so
	// is one over a district of another Kind; the same Kind joins the one it overlaps.
	int32 First = 0, Second = 0, NewCells = 0;
	for (int32 X = Lo.X; X <= Hi.X && Second == 0; ++X)
	{
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			const int32 Owner = Geo.CellOwner.FindRef(FIntPoint(X, Y));
			if (Owner == 0)
			{
				++NewCells;
			}
			else if (First == 0)
			{
				First = Owner;
			}
			else if (Owner != First)
			{
				Second = Owner;
				break;
			}
		}
	}
	if (Second != 0)
	{
		const FRHDistrict& D1 = Districts[Geo.IndexById.FindRef(First)];
		const FRHDistrict& D2 = Districts[Geo.IndexById.FindRef(Second)];
		OutReason = FString::Printf(TEXT("That rectangle crosses two districts (%s and %s) - paint inside one, or on open ground"), *D1.Name, *D2.Name);
		return false;
	}
	const FRHDistrict* Joined = First != 0 ? &Districts[Geo.IndexById.FindRef(First)] : nullptr;
	if (OutPreview)
	{
		OutPreview->CellsNew = NewCells;
	}
	if (Joined)
	{
		if (OutPreview)
		{
			OutPreview->JoinId = Joined->Id;
		}
		if (Joined->Kind != K)
		{
			OutReason = FString::Printf(TEXT("That ground overlaps %s, a %s district - erase it first, or paint %s elsewhere"),
				*Joined->Name, *Joined->Kind.ToString(), *K.ToString());
			return false;
		}
	}

	// Power as territory: every cell's centre inside grid coverage (a pylon's or the Lander's
	// reach), the rule a building follows. The reaches are gathered once, not per cell.
	struct FReach { FVector Centre; double RadiusCm; };
	TArray<FReach> Reaches;
	for (const FRHBuildingInstance& Bd : Buildings)
	{
		if (Bd.bUnderConstruction || Bd.Level != 0)
		{
			continue;
		}
		const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(Bd.DefName) : nullptr;
		if (Def && Def->CoverageRadius_m > 0.f)
		{
			Reaches.Add({ Bd.LocationCm, Def->CoverageRadius_m * 100.0 });
		}
	}
	int32 Outside = 0;
	for (int32 X = Lo.X; X <= Hi.X; ++X)
	{
		for (int32 Y = Lo.Y; Y <= Hi.Y; ++Y)
		{
			const FVector Centre = LatticeCentreCm(FIntPoint(X, Y));
			bool bCovered = false;
			for (const FReach& R : Reaches)
			{
				if (FVector::DistXY(R.Centre, Centre) <= R.RadiusCm)
				{
					bCovered = true;
					break;
				}
			}
			Outside += bCovered ? 0 : 1;
		}
	}
	if (OutPreview)
	{
		OutPreview->CellsOutside = Outside;
	}
	if (Outside > 0)
	{
		OutReason = FString::Printf(TEXT("Outside grid coverage (%d of %lld cells) - extend pylons first"), Outside, W * H);
		return false;
	}
	if (NewCells == 0)
	{
		OutReason = FString::Printf(TEXT("That ground is already part of %s"), Joined ? *Joined->Name : TEXT("a district"));
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::CanEraseDistrict(int32 Id, FString& OutReason) const
{
	if (!FindDistrict(Id))
	{
		OutReason = FString::Printf(TEXT("No such district (#%d)"), Id);
		return false;
	}
	return true;
}

bool URHSimWorldSubsystem::CanRenameDistrict(int32 Id, const FString& Typed, FString& OutReason, FString* OutName) const
{
	if (!FindDistrict(Id))
	{
		OutReason = FString::Printf(TEXT("No such district (#%d)"), Id);
		return false;
	}
	FString Name;
	if (!RHDistricts::NormalizeName(Typed, Name))
	{
		OutReason = TEXT("A district's name cannot be empty");
		return false;
	}
	if (OutName)
	{
		*OutName = Name;
	}
	return true;
}

bool URHSimWorldSubsystem::ExecutePaintDistrict(const FRHCommand& Cmd, FString& OutReason)
{
	FRHDistrictPaintPreview Pre;
	// D6b: Value >= 1 on an underground order is a shape that many floors tall; Value 0 (every existing order) is the whole floor.
	const double V = Cmd.Value;
	const int32 Floors = (Cmd.Level < 0 && !FMath::IsNaN(V) && V >= 1.0) ? (V > 1000.0 ? 1000 : FMath::RoundToInt(V)) : 0;
	if (Floors >= 1)
	{
		return ExecutePaintDeepDistrict(Cmd, Floors, OutReason);
	}
	if (!CanPaintDistrict(Cmd.Target, Cmd.Level, Cmd.Location, Cmd.LocationB, OutReason, &Pre))
	{
		return false;
	}
	const FName Kind = RHDistricts::ResolveKind(Cmd.Target);
	// Cells the order adds: every rectangle cell no district holds (a floor has none).
	TArray<FIntPoint> Added;
	if (Cmd.Level == 0)
	{
		const FRHDistrictGeo& Geo = EnsureDistrictGeo();
		for (int32 X = Pre.Min.X; X <= Pre.Max.X; ++X)
		{
			for (int32 Y = Pre.Min.Y; Y <= Pre.Max.Y; ++Y)
			{
				if (!Geo.CellOwner.Contains(FIntPoint(X, Y)))
				{
					Added.Add(FIntPoint(X, Y));
				}
			}
		}
	}
	if (Pre.JoinId != 0)
	{
		const int32 Index = EnsureDistrictGeo().IndexById.FindRef(Pre.JoinId);
		FRHDistrict& D = Districts[Index];
		D.Lattice.Append(Added);
		D.Lattice.Sort(LatticeLess);
		UE_LOG(LogRedHopeSim, Display, TEXT("District #%d '%s' (%s) grows: +%d cell(s), %d in all"), D.Id, *D.Name, *D.Kind.ToString(), Added.Num(), D.Lattice.Num());
	}
	else
	{
		FRHDistrict D;
		D.Id = NextDistrictId++;
		D.Kind = Kind;
		D.Name = RHDistricts::NameForId(D.Id);
		D.RegionId = 0;
		D.Level = Cmd.Level;
		D.Lattice = MoveTemp(Added);
		D.Lattice.Sort(LatticeLess);
		if (D.IsFloor())
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("District #%d '%s' founded: %s, the whole of floor %d"), D.Id, *D.Name, *D.Kind.ToString(), D.Level);
		}
		else
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("District #%d '%s' founded: %s, %d cell(s) on the surface"), D.Id, *D.Name, *D.Kind.ToString(), D.Lattice.Num());
		}
		Districts.Add(MoveTemp(D));
	}
	++DistrictsVersion;
	return true;
}

bool URHSimWorldSubsystem::ExecutePaintDeepDistrict(const FRHCommand& Cmd, int32 Floors, FString& OutReason)
{
	FRHDistrictPaintPreview Pre;
	TArray<FIntVector> Added;
	if (!CanPaintDistrict(Cmd.Target, Cmd.Level, Cmd.Location, Cmd.LocationB, OutReason, &Pre, Floors) || !CanPaintDeepDistrict(Cmd.Target, Cmd.Level, Floors, Cmd.Location, Cmd.LocationB, OutReason, nullptr, &Added))
	{
		return false;
	}
	const FName Kind = RHDistricts::ResolveKind(Cmd.Target);
	if (Pre.JoinId != 0)
	{
		const int32 Index = EnsureDistrictGeo().IndexById.FindRef(Pre.JoinId);
		FRHDistrict& D = Districts[Index];
		D.Deep.Append(Added);
		D.Deep.Sort(RHDistricts::DeepLess);
		D.Level = D.Deep[0].Z; // its shallowest floor
		UE_LOG(LogRedHopeSim, Display, TEXT("District #%d '%s' (%s) grows: +%d point(s) of a shape, %d in all"), D.Id, *D.Name, *D.Kind.ToString(), Added.Num(), D.Deep.Num());
	}
	else
	{
		FRHDistrict D;
		D.Id = NextDistrictId++;
		D.Kind = Kind;
		D.Name = RHDistricts::NameForId(D.Id);
		D.RegionId = 0;
		D.Deep = MoveTemp(Added);
		D.Deep.Sort(RHDistricts::DeepLess);
		D.Level = D.Deep[0].Z;
		UE_LOG(LogRedHopeSim, Display, TEXT("District #%d '%s' founded: %s, a shape of %d point(s) on floor %d%s"), D.Id, *D.Name, *D.Kind.ToString(), D.Deep.Num(), D.Level,
			Floors > 1 ? *FString::Printf(TEXT(" and %d below"), Floors - 1) : TEXT(""));
		Districts.Add(MoveTemp(D));
	}
	++DistrictsVersion;
	return true;
}

bool URHSimWorldSubsystem::ExecuteEraseDistrict(const FRHCommand& Cmd, FString& OutReason)
{
	const int32 Id = (int32)Cmd.Value;
	if (!CanEraseDistrict(Id, OutReason))
	{
		return false;
	}
	CancelGrowthProject(Id, TEXT("its district was erased")); // C5b: an open project is cancelled with a full refund (nothing was applied)
	const int32 Index = EnsureDistrictGeo().IndexById.FindRef(Id);
	UE_LOG(LogRedHopeSim, Display, TEXT("District #%d '%s' (%s) erased"), Districts[Index].Id, *Districts[Index].Name, *Districts[Index].Kind.ToString());
	Districts.RemoveAt(Index);
	++DistrictsVersion;
	return true;
}

bool URHSimWorldSubsystem::ExecuteRenameDistrict(const FRHCommand& Cmd, FString& OutReason)
{
	const int32 Id = (int32)Cmd.Value;
	FString Name;
	if (!CanRenameDistrict(Id, Cmd.Text, OutReason, &Name))
	{
		return false;
	}
	const int32 Index = EnsureDistrictGeo().IndexById.FindRef(Id);
	UE_LOG(LogRedHopeSim, Display, TEXT("District #%d '%s' renamed '%s'"), Districts[Index].Id, *Districts[Index].Name, *Name);
	Districts[Index].Name = Name;
	++DistrictsVersion;
	return true;
}

// --- The city's demand -------------------------------------------------------------

FRHCityDemand URHSimWorldSubsystem::GetCityDemand() const
{
	FRHCityDemand Out;
	const int32 Pop = Colonists.Num();
	Out.Population = Pop;
	if (Pop <= 0 || !Defs)
	{
		return Out; // nobody here, nothing to want
	}

	// --- Housing: the arrivals' own limits ---
	Out.FreeBeds = GetFreeHousing();
	Out.ComfortCapacity = GetHousingComfortCapacity();
	Out.bVaultCertified = bVaultRated;
	Out.bSettlersDraw = HopeSmoothed >= ImmigrationHopeMin;
	Out.SettlersWanted = Out.bSettlersDraw ? GetImmigrationRaw(HopeSmoothed, Pop) : FMath::Max(0, CrewPodColonists);
	Out.SettlerRoom = GetImmigrationRoom(Pop, Out.FreeBeds, Out.ComfortCapacity);
	// For W settlers to come, FreeBeds - ReserveBeds >= W and ComfortCapacity - Pop >= W. A new
	// certified bed raises both by one, so the beds missing are the larger shortfall.
	const int32 ShortFree = FMath::Max(0, Out.SettlersWanted + ImmigrationReserveBeds - Out.FreeBeds);
	const int32 ShortComfort = FMath::Max(0, Out.SettlersWanted - (Out.ComfortCapacity - Pop));
	Out.SettlerBedsShort = FMath::Max(ShortFree, ShortComfort);
	const int32 SettlerWanted = FMath::Max(1, Out.SettlersWanted + ImmigrationReserveBeds);
	Out.bBigArrivalPending = !bBigArrivalDone && BigArrivalColonists > 0 && BigArrivalStreakSols > 0.0;
	Out.BigArrivalBedsShort = Out.bBigArrivalPending ? FMath::Max(0, BigArrivalFreeBeds - Out.FreeBeds) : 0;
	if (Out.BigArrivalBedsShort > Out.SettlerBedsShort)
	{
		Out.HousingBedsShort = Out.BigArrivalBedsShort;
		Out.HousingBedsWanted = FMath::Max(1, BigArrivalFreeBeds);
		Out.HousingLimit = FName(TEXT("BigArrival"));
	}
	else
	{
		Out.HousingBedsShort = Out.SettlerBedsShort;
		Out.HousingBedsWanted = SettlerWanted;
		Out.HousingLimit = Out.SettlerBedsShort <= 0 ? NAME_None : (ShortFree >= ShortComfort ? FName(TEXT("FreeBeds")) : FName(TEXT("Comfort")));
	}
	Out.Housing = FMath::Clamp((double)Out.HousingBedsShort / (double)Out.HousingBedsWanted, 0.0, 1.0);

	// --- Jobs ---
	Out.FilledSeats = ColonistJobs.Num();
	for (const TPair<FIntVector, int32>& Station : StationCapacity) // an integer sum
	{
		Out.TotalSeats += Station.Value;
	}
	Out.Unemployed = FMath::Max(0, Pop - Out.FilledSeats);
	Out.SeatsShort = FMath::Max(0, Pop - Out.TotalSeats);
	Out.Jobs = (double)Out.Unemployed / (double)Pop;

	// --- Services: the coverage gap of every room function that earns Hope ---
	// The functions: every slice-active room row with a MoraleWeight, Living aside (housing is
	// counted above). A function's weight is the one GetColonyHope uses - the first live room
	// of it found, walking rated floors shallowest first, then rated networks by root - and,
	// with no such room, the weight of its lowest tier (ties by row name).
	struct FFn { FName Function; double Weight = 0.0; int32 Rooms = 0; int32 LowTier = MAX_int32; FName LowRow; double LowWeight = 0.0; };
	TArray<FFn> Fns;
	const auto FnOf = [&Fns](FName Function) -> FFn&
	{
		for (FFn& F : Fns)
		{
			if (F.Function == Function)
			{
				return F;
			}
		}
		FFn Fresh;
		Fresh.Function = Function;
		return Fns.Add_GetRef(Fresh);
	};
	Defs->ForEachRoom([&](FName RowName, const FRHRoomRow& Row)
	{
		if (Row.Function == NAME_LivingFn || Row.MoraleWeight <= 0.f)
		{
			return;
		}
		FFn& F = FnOf(Row.Function);
		if (Row.Tier < F.LowTier || (Row.Tier == F.LowTier && RowName.LexicalLess(F.LowRow)))
		{
			F.LowTier = Row.Tier;
			F.LowRow = RowName;
			F.LowWeight = Row.MoraleWeight;
		}
	});
	TArray<int32> RatedLevels;
	for (const TPair<int32, TArray<FName>>& Pair : FloorRoomCells)
	{
		if (RatedFloors.Contains(Pair.Key))
		{
			RatedLevels.Add(Pair.Key);
		}
	}
	RatedLevels.Sort([](int32 A, int32 B) { return A > B; });
	const auto Count = [&](const FRHRoomRow* Row)
	{
		if (!Row || !Row->SliceActive || Row->Function == NAME_LivingFn || Row->MoraleWeight <= 0.f)
		{
			return;
		}
		FFn& F = FnOf(Row->Function);
		if (F.Rooms == 0)
		{
			F.Weight = Row->MoraleWeight; // the first live room seen sets it, as in GetColonyHope
		}
		++F.Rooms;
	};
	for (const int32 Level : RatedLevels)
	{
		for (const FName& RoomName : FloorRoomCells.FindChecked(Level))
		{
			Count(Defs->GetRoom(RoomName));
		}
	}
	for (const FRHHabNetCache& Net : HabNets)
	{
		if (!IsNetRated(Net))
		{
			continue;
		}
		for (const int32 HabId : Net.Members)
		{
			const FRHHabMemberCache& M = HabMembers.FindChecked(HabId);
			for (int32 Bay = 0; Bay < M.NX * M.NY; ++Bay)
			{
				Count(Defs->GetRoom(GetBayRoom(HabId, Bay)));
			}
		}
	}
	double WeightSum = 0.0, CoveredSum = 0.0;
	for (const FFn& F : Fns)
	{
		FRHServiceGap Gap;
		Gap.Function = F.Function;
		Gap.Rooms = F.Rooms;
		Gap.Weight = F.Rooms > 0 ? F.Weight : F.LowWeight;
		Gap.Coverage = FMath::Min(1.0, (double)F.Rooms * RoomServesResidents / (double)Pop);
		Gap.Gap = 1.0 - Gap.Coverage;
		WeightSum += Gap.Weight;
		CoveredSum += Gap.Weight * Gap.Coverage;
		Out.ServiceGaps.Add(Gap);
	}
	Out.ServiceGaps.Sort([](const FRHServiceGap& A, const FRHServiceGap& B)
	{
		if (A.Gap != B.Gap) { return A.Gap > B.Gap; }
		if (A.Weight != B.Weight) { return A.Weight > B.Weight; }
		return A.Function.LexicalLess(B.Function);
	});
	Out.Services = WeightSum > 0.0 ? FMath::Clamp(1.0 - CoveredSum / WeightSum, 0.0, 1.0) : 0.0;

	// --- Air: the homes' rations and oxygen margins from the last population step ---
	double LowestRation = 1.0;
	for (const FRHHomeRation& Home : HomeRations) // ascending Key
	{
		++Out.Homes;
		if (Home.Fraction < 1.0)
		{
			++Out.HomesShort;
			Out.ResidentsShort += Home.Residents;
			Out.HomesShortOnAir += Home.Limiting == NAME_OxygenLeg ? 1 : 0;
			if (Home.Fraction < LowestRation)
			{
				LowestRation = Home.Fraction;
				Out.ScarcestLeg = Home.Limiting;
			}
		}
		if (Home.Residents > 0 && ColonistO2KgPerSol > 0.0)
		{
			// What the home can breathe: its floor's fill, or its network's (an unrated home has no air to share).
			double SupplyKg = 0.0;
			if (Home.HomeHab != 0)
			{
				const FRHHabNetCache* Net = FindHabNet(Home.HomeHab);
				SupplyKg = (Net && IsNetRated(*Net)) ? GetHabNetworkO2Kg(Home.HomeHab) : 0.0;
			}
			else
			{
				SupplyKg = RatedFloors.Contains(Home.HomeLevel) ? GetFloorO2Kg(Home.HomeLevel) : 0.0;
			}
			const double Margin = SupplyKg / ((double)Home.Residents * ColonistO2KgPerSol);
			if (Out.MinOxygenMarginSols < 0.0 || Margin < Out.MinOxygenMarginSols)
			{
				Out.MinOxygenMarginSols = Margin;
			}
		}
	}
	Out.Air = FMath::Clamp((double)Out.ResidentsShort / (double)Pop, 0.0, 1.0);

	// --- The Big Arrival, ready but for life support (C5c, C5d) ---
	// Its streak is complete and the beds are there: only air, food or water holds it - the stocks (C5c) or what
	// the colony makes against what N + the wave would draw (C5d). Gate-D placeholder copy (D153): plain, names
	// what to restore, never a threat.
	if (Out.bBigArrivalPending && BigArrivalStreakSols >= BigArrivalStreakSolsNeeded && Out.FreeBeds >= BigArrivalFreeBeds)
	{
		const FRHBigArrivalSupport Support = GetBigArrivalSupport();
		if (!Support.AllOk())
		{
			Out.bBigArrivalWaitsOnSupply = true;
			Out.bBigArrivalShortAir = !Support.O2Carried();
			Out.bBigArrivalShortFood = !Support.FoodCarried();
			Out.bBigArrivalShortWater = !Support.WaterCarried();
			if (Out.bBigArrivalShortAir)
			{
				const TCHAR* What = (!Support.bO2Ok && !Support.O2Leg.bOk) ? TEXT("air reserve and oxygen production") : (!Support.bO2Ok ? TEXT("air reserve") : TEXT("oxygen production"));
				Out.BigArrivalAirNote = FString::Printf(TEXT("The settlers are ready to come once the %s can carry %d more."), What, Support.Wave);
				if (!Support.O2Leg.bOk)
				{
					Out.BigArrivalAirNote += FString::Printf(TEXT(" The colony makes about %.0f kg of oxygen a sol and would need %.0f."), Support.O2Leg.ProductionPerSol, Support.O2Leg.DrawPerSol);
				}
			}
			if (Out.bBigArrivalShortFood || Out.bBigArrivalShortWater)
			{
				// "food and water stores" (the C5c words) for stock; "food and water production" for what is made; both, joined.
				const auto Phrase = [](bool bFood, bool bWater, const TCHAR* Noun, const TCHAR* NounBoth) -> FString
				{
					return bFood && bWater ? FString::Printf(TEXT("food and water %s"), NounBoth) : (bFood ? FString::Printf(TEXT("food %s"), Noun) : (bWater ? FString::Printf(TEXT("water %s"), Noun) : FString()));
				};
				const FString StockPhrase = Phrase(!Support.bFoodOk, !Support.bWaterOk, TEXT("store"), TEXT("stores"));
				const FString MadePhrase = Phrase(!Support.FoodLeg.bOk, !Support.WaterLeg.bOk, TEXT("production"), TEXT("production"));
				const FString Joined = StockPhrase + (!StockPhrase.IsEmpty() && !MadePhrase.IsEmpty() ? TEXT(" and ") : TEXT("")) + MadePhrase;
				Out.BigArrivalRationsNote = FString::Printf(TEXT("The settlers are ready to come once the %s can carry %d more."), *Joined, Support.Wave);
			}
		}
	}

	// --- Settler ships, held to what life support carries (C5d, C5e) ---
	// Settlers come only on net production: each leg's measured production must already carry N + the
	// settlers (the stock cover that lets the Big Arrival through does not admit them), and its stock must
	// back them too (GetSettlerLegRoom). Gate-D placeholder copy (D156): plain, says what to build.
	if (Out.bSettlersDraw)
	{
		const FName Legs[3] = { FName("Oxygen"), FName("Water"), FName("Food") };
		FRHSettlerLegRoom Rooms[3];
		Out.SettlerSupportRoom = MAX_int32;
		for (int32 i = 0; i < 3; ++i)
		{
			Rooms[i] = GetSettlerLegRoom(Legs[i]);
			Out.SettlerSupportRoom = FMath::Min(Out.SettlerSupportRoom, Rooms[i].Room());
		}
		const int32 Housed = FMath::Min(Out.SettlersWanted, Out.SettlerRoom);
		if (Housed > 0 && Out.SettlerSupportRoom < Housed)
		{
			Out.bSettlersHeldBySupply = true;
			Out.bSettlersShortAir = Rooms[0].ProductionRoom < Housed;
			Out.bSettlersShortWater = Rooms[1].ProductionRoom < Housed;
			Out.bSettlersShortFood = Rooms[2].ProductionRoom < Housed;
			Out.bSettlersShortStores = Rooms[0].StockRoom < Housed || Rooms[1].StockRoom < Housed || Rooms[2].StockRoom < Housed;
			const auto Names = [&](bool bStores) -> FString
			{
				TArray<FString> Short;
				const TCHAR* Words[3] = { TEXT("oxygen"), TEXT("water"), TEXT("food") };
				for (int32 i = 0; i < 3; ++i)
				{
					if ((bStores ? Rooms[i].StockRoom : Rooms[i].ProductionRoom) < Housed) { Short.Add(Words[i]); }
				}
				return Short.Num() == 3 ? FString(TEXT("oxygen, water and food")) : FString::Join(Short, TEXT(" and "));
			};
			const int32 Held = FMath::Max(0, Out.SettlerSupportRoom);
			const bool bMade = Out.bSettlersShortAir || Out.bSettlersShortWater || Out.bSettlersShortFood;
			Out.SettlerSupportNote = bMade
				? FString::Printf(TEXT("The next ship's settlers are held to %d: they come only on what the colony already makes, and its %s production would not carry them."), Held, *Names(false))
				: FString::Printf(TEXT("The next ship's settlers are held to %d: the colony's %s stores are too thin to back them."), Held, *Names(true));
			if (bMade && Out.bSettlersShortStores)
			{
				Out.SettlerSupportNote += FString::Printf(TEXT(" Its %s stores are also too thin."), *Names(true));
			}
		}
	}
	return Out;
}

// --- Save v49: the floor-shape block, straight after the cavern block ---------------------

void URHSimWorldSubsystem::WriteDeepBlock(FArchive& Ar) const
{
	// The district skeleton (Id, Level, class: 0 surface, 1 whole floor, 2 deep - so a load can cross-check the shapes against the
	// district list on locals, before it tears anything down), then every deep district in Id order with its sorted points (X, Y,
	// Level), then every growth project that holds points (DistrictId, Level, its points). An empty colony is 12 bytes.
	int32 SkeletonCount = Districts.Num();
	Ar << SkeletonCount;
	int32 DeepCount = 0;
	for (const FRHDistrict& D : Districts)
	{
		int32 Id = D.Id, Level = D.Level, Class = D.IsDeep() ? 2 : (D.IsFloor() ? 1 : 0);
		DeepCount += D.IsDeep() ? 1 : 0;
		Ar << Id << Level << Class;
	}
	Ar << DeepCount;
	for (const FRHDistrict& D : Districts)
	{
		if (!D.IsDeep())
		{
			continue;
		}
		int32 Id = D.Id, Count = D.Deep.Num();
		Ar << Id << Count;
		for (const FIntVector& P : D.Deep)
		{
			int32 X = P.X, Y = P.Y, Z = P.Z;
			Ar << X << Y << Z;
		}
	}
	int32 ProjectCount = 0;
	for (const FRHGrowthProject& P : GrowthProjects)
	{
		ProjectCount += P.Points.Num() > 0 ? 1 : 0;
	}
	Ar << ProjectCount;
	for (const FRHGrowthProject& P : GrowthProjects)
	{
		if (P.Points.Num() == 0)
		{
			continue;
		}
		int32 Id = P.DistrictId, Level = P.Level, Count = P.Points.Num();
		Ar << Id << Level << Count;
		for (const FIntPoint& Pt : P.Points)
		{
			int32 X = Pt.X, Y = Pt.Y;
			Ar << X << Y;
		}
	}
}

bool URHSimWorldSubsystem::ReadDeepBlock(FArchive& Ar, const TArray<FRHShaft>& InShafts, FRHLoadedDeep& Out, FString& OutError) const
{
	// Structural refusals only, taken before the live colony is touched: they set OutError and log nothing at Error level.
	const auto Refuse = [&OutError](const FString& Why) { OutError = FString::Printf(TEXT("Save floor-shape block invalid: %s"), *Why); return false; };
	Out = FRHLoadedDeep();
	int32 Deepest = 0;
	for (const FRHShaft& Sh : InShafts)
	{
		Deepest = FMath::Max(Deepest, Sh.Depth);
	}
	const auto BytesLeft = [&Ar]() { return (int64)Ar.TotalSize() - Ar.Tell(); };
	int32 SkeletonCount = 0;
	Ar << SkeletonCount;
	if (Ar.IsError() || SkeletonCount < 0 || (int64)SkeletonCount * 12 > BytesLeft())
	{
		return Refuse(FString::Printf(TEXT("district count %d"), SkeletonCount));
	}
	TSet<int32> WholeLevels;
	TMap<int32, int32> DeepLevelOfId; // deep district Id -> its Level
	int32 PrevId = 0;
	for (int32 i = 0; i < SkeletonCount; ++i)
	{
		FRHLoadedDeep::FSkeleton S;
		Ar << S.Id << S.Level << S.Class;
		if (Ar.IsError() || S.Id <= PrevId || S.Class < 0 || S.Class > 2 || (S.Class == 0 ? S.Level != 0 : (S.Level >= 0 || S.Level < -MaxDepth)))
		{
			return Refuse(FString::Printf(TEXT("district #%d (level %d, class %d) is out of order or out of range"), S.Id, S.Level, S.Class));
		}
		PrevId = S.Id;
		if (S.Class == 1)
		{
			WholeLevels.Add(S.Level);
		}
		else if (S.Class == 2)
		{
			DeepLevelOfId.Add(S.Id, S.Level);
		}
		Out.Skeleton.Add(S);
	}
	int32 ShapeCount = 0;
	Ar << ShapeCount;
	if (Ar.IsError() || ShapeCount != DeepLevelOfId.Num())
	{
		return Refuse(FString::Printf(TEXT("%d shape(s) listed for %d deep district(s)"), ShapeCount, DeepLevelOfId.Num()));
	}
	TMap<FIntVector, int32> Held; // every deep point -> its district: no point sits in two
	int32 PrevShape = 0;
	for (int32 n = 0; n < ShapeCount; ++n)
	{
		FRHLoadedDeep::FShape Shape;
		int32 Count = 0;
		Ar << Shape.Id << Count;
		if (Ar.IsError() || Shape.Id <= PrevShape || !DeepLevelOfId.Contains(Shape.Id))
		{
			return Refuse(FString::Printf(TEXT("shape #%d names no deep district, or is out of order"), Shape.Id));
		}
		PrevShape = Shape.Id;
		if (Count < 1 || Count > RH::MaxDeepPoints || (int64)Count * 12 > BytesLeft())
		{
			return Refuse(FString::Printf(TEXT("shape #%d lists %d points (1 to %d)"), Shape.Id, Count, RH::MaxDeepPoints));
		}
		Shape.Points.Reserve(Count);
		for (int32 k = 0; k < Count; ++k)
		{
			FIntVector P;
			Ar << P.X << P.Y << P.Z;
			if (Ar.IsError())
			{
				return Refuse(TEXT("the shape list is cut short"));
			}
			if (P.Z >= 0 || P.Z < -MaxDepth || -P.Z > Deepest)
			{
				return Refuse(FString::Printf(TEXT("shape #%d holds a point on floor %d, which is outside -1..-%d or reached by no shaft"), Shape.Id, P.Z, MaxDepth));
			}
			if (k > 0 && !RHDistricts::DeepLess(Shape.Points.Last(), P))
			{
				return Refuse(FString::Printf(TEXT("shape #%d lists (%d,%d,%d) out of order or twice"), Shape.Id, P.X, P.Y, P.Z));
			}
			if (WholeLevels.Contains(P.Z))
			{
				return Refuse(FString::Printf(TEXT("floor %d is held whole by a district and also holds shape #%d's point (%d,%d)"), P.Z, Shape.Id, P.X, P.Y));
			}
			if (Held.Contains(P))
			{
				return Refuse(FString::Printf(TEXT("the point (%d,%d) on floor %d sits in two districts"), P.X, P.Y, P.Z));
			}
			Held.Add(P, Shape.Id);
			Shape.Points.Add(P);
		}
		if (Held.Num() > RH::MaxDeepPoints)
		{
			return Refuse(FString::Printf(TEXT("the shapes together hold more than %d points"), RH::MaxDeepPoints));
		}
		if (Shape.Points[0].Z != DeepLevelOfId.FindChecked(Shape.Id))
		{
			return Refuse(FString::Printf(TEXT("shape #%d starts on floor %d but its district says floor %d"), Shape.Id, Shape.Points[0].Z, DeepLevelOfId.FindChecked(Shape.Id)));
		}
		Out.Shapes.Add(MoveTemp(Shape));
	}
	int32 ProjectCount = 0;
	Ar << ProjectCount;
	if (Ar.IsError() || ProjectCount < 0 || ProjectCount > SkeletonCount || (int64)ProjectCount * 12 > BytesLeft())
	{
		return Refuse(FString::Printf(TEXT("growth project count %d"), ProjectCount));
	}
	int32 PrevProject = 0;
	for (int32 n = 0; n < ProjectCount; ++n)
	{
		FRHLoadedDeep::FProjectPoints Pr;
		int32 Count = 0;
		Ar << Pr.DistrictId << Pr.Level << Count;
		if (Ar.IsError() || Pr.DistrictId <= PrevProject || !DeepLevelOfId.Contains(Pr.DistrictId) || Pr.Level >= 0 || Pr.Level < -MaxDepth)
		{
			return Refuse(FString::Printf(TEXT("the growth points of district #%d (floor %d) name no deep district, or are out of order or range"), Pr.DistrictId, Pr.Level));
		}
		PrevProject = Pr.DistrictId;
		if (Count < 1 || Count > RH::MaxGrowthPoints || (int64)Count * 8 > BytesLeft())
		{
			return Refuse(FString::Printf(TEXT("district #%d's growth project lists %d points (1 to %d)"), Pr.DistrictId, Count, RH::MaxGrowthPoints));
		}
		for (int32 k = 0; k < Count; ++k)
		{
			FIntPoint P;
			Ar << P.X << P.Y;
			if (Ar.IsError())
			{
				return Refuse(TEXT("the growth points are cut short"));
			}
			const int32* Owner = Held.Find(FIntVector(P.X, P.Y, Pr.Level));
			if (!Owner || *Owner != Pr.DistrictId)
			{
				return Refuse(FString::Printf(TEXT("district #%d's growth point (%d,%d) on floor %d is outside its shape"), Pr.DistrictId, P.X, P.Y, Pr.Level));
			}
			Pr.Points.Add(P);
		}
		Out.Projects.Add(MoveTemp(Pr));
	}
	return true;
}
