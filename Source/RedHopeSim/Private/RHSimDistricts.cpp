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
		for (const FIntPoint& Cell : D.Lattice)
		{
			Out.CellOwner.Add(Cell, D.Id);
		}
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
		if (!SameMap(Fresh.CellOwner, DistrictGeo.CellOwner) || !SameMap(Fresh.FloorOwner, DistrictGeo.FloorOwner) || !SameMap(Fresh.IndexById, DistrictGeo.IndexById))
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
	return Key;
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
		bFresh &= SameMap(Fresh.CellOwner, DistrictGeo.CellOwner) && SameMap(Fresh.FloorOwner, DistrictGeo.FloorOwner) && SameMap(Fresh.IndexById, DistrictGeo.IndexById);
	}
	if (DistrictMembers.bValid && DistrictMembers.Key == MakeDistrictMembershipKey())
	{
		FRHDistrictMembership Fresh;
		DeriveDistrictMembership(Fresh);
		bFresh &= SameMap(Fresh.OfBuilding, DistrictMembers.OfBuilding) && Fresh.Buildings == DistrictMembers.Buildings
			&& Fresh.Bays == DistrictMembers.Bays && Fresh.Residents == DistrictMembers.Residents;
	}
	return bFresh;
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
	return Level < 0 ? GetDistrictOfFloor(Level) : GetDistrictAtCell(LatticeOf(Cm));
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
	return C->HomeLevel < 0 ? GetDistrictOfFloor(C->HomeLevel) : 0;
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
		}
		if (Owner != 0)
		{
			Out[Geo.IndexById.FindRef(Owner)].Seats += Station.Value;
		}
	}
}

// --- The three verbs ---------------------------------------------------------------

bool URHSimWorldSubsystem::CanPaintDistrict(FName Kind, int32 Level, const FVector& FromCm, const FVector& ToCm, FString& OutReason, FRHDistrictPaintPreview* OutPreview) const
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
		return true;
	}

	// Surface ground: a rectangle of lattice cells.
	const FIntPoint A = LatticeOf(FromCm), B = LatticeOf(ToCm);
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
