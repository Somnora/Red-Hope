#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimClockSubsystem.h"

// C5b (living city gate 5b): growth. The data is RHGrowth.h; this file is the colony's side of
// it - the sol-boundary start check, the bill, the planners (surface module, underground cells,
// civic room), the countdown and the direct completion. Everything is an integer sum, an
// id-ordered walk or an explicit sort with a unique key: no container's iteration order decides
// an outcome. A colony with no districts and no open project pays one integer compare a step.

namespace
{
	const FName NAME_GrowthStruct(TEXT("Struct"));
	const FName NAME_GrowthOxygen(TEXT("Oxygen"));
	const FName NAME_GrowthHabModule(TEXT("HabModule"));
	const FName NAME_GrowthBunkBed(TEXT("BunkBed"));
	const FName NAME_GrowthLiving(TEXT("Living"));
	constexpr double GrowthBillEps = 1.0e-6;

	const TCHAR* GrowthPlural(int32 N) { return N == 1 ? TEXT("") : TEXT("s"); }

	TArray<FName> GrowthSortedKeys(const TMap<FName, double>& Map)
	{
		TArray<FName> Keys;
		Map.GetKeys(Keys);
		Keys.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
		return Keys;
	}

	FString GrowthBillText(const TMap<FName, double>& Bill)
	{
		FString Out;
		for (const FName& Key : GrowthSortedKeys(Bill))
		{
			Out += FString::Printf(TEXT("%s%s %.0f"), Out.IsEmpty() ? TEXT("") : TEXT(", "), *Key.ToString(), Bill.FindChecked(Key));
		}
		return Out.IsEmpty() ? FString(TEXT("nothing")) : Out;
	}

	void GrowthAddLines(TMap<FName, double>& Into, const TMap<FName, double>& Lines, double Times)
	{
		for (const TPair<FName, double>& L : Lines) // a sum per key: the visiting order cannot matter
		{
			Into.FindOrAdd(L.Key) += L.Value * Times;
		}
	}

	bool GrowthListHas(const FString& List, const FString& Tag)
	{
		TArray<FString> Parts;
		List.ParseIntoArray(Parts, TEXT(";"));
		for (const FString& P : Parts)
		{
			if (P.TrimStartAndEnd() == Tag) { return true; }
		}
		return false;
	}

	// Does any tag in A also stand in B? (Both are "Odor;Contagion"-style lists.)
	bool GrowthTagsMeet(const FString& A, const FString& B)
	{
		if (A.IsEmpty() || B.IsEmpty()) { return false; }
		TArray<FString> AParts;
		A.ParseIntoArray(AParts, TEXT(";"));
		for (const FString& P : AParts)
		{
			if (GrowthListHas(B, P.TrimStartAndEnd())) { return true; }
		}
		return false;
	}
}

bool URHSimWorldSubsystem::RoomEmitsTag(const FRHRoomRow& Row, const TCHAR* Tag)
{
	return GrowthListHas(Row.EmitsTags, Tag);
}

// --- Reads --------------------------------------------------------------------------

const FRHGrowthProject* URHSimWorldSubsystem::FindGrowthProject(int32 DistrictId) const
{
	for (const FRHGrowthProject& P : GrowthProjects) // at most one a district, and few districts grow at once
	{
		if (P.DistrictId == DistrictId)
		{
			return &P;
		}
	}
	return nullptr;
}

bool URHSimWorldSubsystem::SetDistrictGrowth(int32 DistrictId, bool bOn)
{
	const int32* Index = EnsureDistrictGeo().IndexById.Find(DistrictId);
	if (!Index)
	{
		return false;
	}
	FRHDistrict& D = Districts[*Index];
	if (D.bGrowth != bOn)
	{
		D.bGrowth = bOn;
		++GrowthVersion;
		UE_LOG(LogRedHopeSim, Display, TEXT("District #%d '%s' (%s): growth switched %s"), D.Id, *D.Name, *D.Kind.ToString(), bOn ? TEXT("on") : TEXT("off"));
		OnGrowthChanged.Broadcast();
	}
	return true;
}

void URHSimWorldSubsystem::SetCityShare(double Share)
{
	const double Clamped = FMath::Clamp(Share, 0.0, 1.0);
	if (Clamped == CityShare)
	{
		return;
	}
	CityShare = Clamped;
	++GrowthVersion;
	UE_LOG(LogRedHopeSim, Display, TEXT("City / Earth share set to %.0f%% city, %.0f%% Earth"), Clamped * 100.0, (1.0 - Clamped) * 100.0);
	OnGrowthChanged.Broadcast();
}

bool URHSimWorldSubsystem::IsDistrictGrowthOn(int32 DistrictId) const
{
	const FRHDistrict* D = FindDistrict(DistrictId);
	return D && D->bGrowth;
}

int32 URHSimWorldSubsystem::GetGrowthProjectBeds(const FRHGrowthProject& Project) const
{
	TArray<FName> Bunks;
	GetGrowthBunkPieces(Bunks);
	int32 PerSite = 0;
	for (const FName& B : Bunks)
	{
		const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(B) : nullptr;
		PerSite += Row ? FMath::Max(0, Row->Capacity) : 0;
	}
	return Project.HomeSites() * PerSite;
}

FString URHSimWorldSubsystem::DescribeGrowthProject(const FRHGrowthProject& P) const
{
	switch (P.Kind)
	{
	case ERHGrowthKind::SurfaceModule:
		return FString::Printf(TEXT("%d new home bay%s (a %d x %d habitat module)"), P.HomeSites(), GrowthPlural(P.HomeSites()), P.Size.X, P.Size.Y);
	case ERHGrowthKind::FloorCells:
		return FString::Printf(TEXT("%d new home cell%s on floor %d"), P.Cells, GrowthPlural(P.Cells), P.Level);
	case ERHGrowthKind::CivicRoom:
	default:
	{
		const FRHRoomRow* Row = Defs ? Defs->GetRoom(P.Room) : nullptr;
		return FString::Printf(TEXT("a new %s%s"), Row ? *Row->DisplayName.ToLower() : *P.Room.ToString(), P.bCarve ? TEXT(" (carving a cell for it)") : TEXT(""));
	}
	}
}

// --- Money --------------------------------------------------------------------------

double URHSimWorldSubsystem::GetGrowthHoldingsKg(FName Resource) const
{
	// Uncommitted holdings, as C4's road bill counts them: the pool (fluids and gases, and the
	// solids that ride it, Glass and SpareParts) plus the stores of COMPLETED buildings on linked
	// floors. A construction site's hopper is spoken for and never counts.
	double Total = GetStock(Resource);
	if (Defs && Defs->IsSolidResource(Resource))
	{
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.bUnderConstruction || !AreLevelsLinked(B.Level, 0))
			{
				continue;
			}
			if (const double* In = B.InputKg.Find(Resource)) { Total += *In; }
			if (const double* Out = B.OutputKg.Find(Resource)) { Total += *Out; }
		}
	}
	return Total;
}

double URHSimWorldSubsystem::GetProtectedQuotaBillKg(FName Resource, bool& bOutNext) const
{
	// The quota's bills are HOLDINGS (it is met when the colony holds them), so while a quota is
	// open, growth must leave them standing. Once it is met nothing is being collected, but the
	// ladder is about to ask for the next round's bills (awaiting the manifest, ship inbound, or
	// resting): growth that spends stock down between rounds slows the ladder, so those are the ones
	// it leaves standing then (C5c).
	bOutNext = QuotaPhase != ERHQuotaPhase::Open;
	if (bOutNext)
	{
		TMap<FName, double> Next;
		GetNextQuotaBills(Next);
		const double* Bill = Next.Find(Resource);
		return Bill ? *Bill : 0.0;
	}
	for (const TPair<FName, TPair<double, double>>& Line : GetQuotaProgress())
	{
		if (Line.Key == Resource)
		{
			return Line.Value.Value;
		}
	}
	return 0.0;
}

URHSimWorldSubsystem::FRHGrowthAvailability URHSimWorldSubsystem::GetGrowthAvailability(FName Resource) const
{
	FRHGrowthAvailability A;
	A.Held = GetGrowthHoldingsKg(Resource);
	A.FullBill = GetProtectedQuotaBillKg(Resource, A.bNextRound);
	// C5d: the open quota's bill stands whole (a quota is met by holding it); between rounds, the share of the
	// next round's bill that goes to Earth stands, and the rest (CityShare of it) may grow the city.
	A.QuotaBill = A.bNextRound ? (1.0 - CityShare) * A.FullBill : A.FullBill;
	if (Resource == NAME_GrowthStruct)
	{
		A.Reserve = GrowthReserveStructKg;
	}
	else if (Resource == NAME_GrowthOxygen)
	{
		A.Reserve = GrowthReserveO2Sols * (double)Colonists.Num() * ColonistO2KgPerSol; // a breathing floor: sols of everyone's draw
	}
	A.Free = A.Held - A.QuotaBill - A.Reserve;
	return A;
}

bool URHSimWorldSubsystem::GrowthBillFits(const TMap<FName, double>& Bill, FString& OutWhy) const
{
	for (const FName& Res : GrowthSortedKeys(Bill))
	{
		const double Need = Bill.FindChecked(Res);
		if (Need <= GrowthBillEps)
		{
			continue;
		}
		const FRHGrowthAvailability A = GetGrowthAvailability(Res);
		if (Need > A.Free + GrowthBillEps)
		{
			OutWhy = FString::Printf(TEXT("short of %s: needs %.0f kg, %.0f free (%.0f held - %.0f for the %s - %.0f reserve)"),
				*Res.ToString(), Need, FMath::Max(0.0, A.Free), A.Held, A.QuotaBill,
				A.bNextRound ? *FString::Printf(TEXT("next quota round (%.0f%% of its bill, the Earth share)"), (1.0 - CityShare) * 100.0) : TEXT("open quota"), A.Reserve);
			return false;
		}
	}
	return true;
}

void URHSimWorldSubsystem::TakeGrowthResource(FName Resource, double Kg)
{
	if (Kg <= 0.0)
	{
		return;
	}
	if (Defs && Defs->IsSolidResource(Resource))
	{
		// Building order (Id ascending), outputs before held inputs - the order a hauler draws and
		// TakeRoadStruct takes in; then the pool. Never a construction site.
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
			for (TMap<FName, double>* Shelf : { &B.OutputKg, &B.InputKg })
			{
				if (double* Held = Shelf->Find(Resource))
				{
					const double Take = FMath::Min(Left, *Held);
					*Held -= Take;
					Left -= Take;
				}
			}
		}
		OnStockChanged.Broadcast(Resource, GetTotalSolid(Resource));
		if (Left > 0.0)
		{
			AddStock(Resource, -Left);
		}
	}
	else
	{
		AddStock(Resource, -Kg);
	}
}

// --- Bunks, furniture, rooms ---------------------------------------------------------

FName URHSimWorldSubsystem::GetGrowthLivingRoom() const
{
	// The Living row homes are zoned as: the lowest tier, then the lexically first name.
	FName Best;
	int32 BestTier = MAX_int32;
	if (Defs)
	{
		Defs->ForEachRoom([&](FName RowName, const FRHRoomRow& Row)
		{
			if (Row.Function == NAME_GrowthLiving && (Row.Tier < BestTier || (Row.Tier == BestTier && RowName.LexicalLess(Best))))
			{
				Best = RowName;
				BestTier = Row.Tier;
			}
		});
	}
	return Best;
}

void URHSimWorldSubsystem::GetGrowthBunkPieces(TArray<FName>& Out) const
{
	Out.Reset();
	const FRHFurnitureRow* Row = (Defs && Defs->HasFurnitureCatalogue()) ? Defs->GetFurniture(NAME_GrowthBunkBed) : nullptr;
	if (Row && Row->SliceActive && Row->Provides == FName(TEXT("Bed")))
	{
		for (int32 i = 0; i < GrowthBunksPerHome; ++i)
		{
			Out.Add(NAME_GrowthBunkBed);
		}
	}
}

void URHSimWorldSubsystem::AddGrowthFurnitureCost(const TArray<FName>& Pieces, TMap<FName, double>& Bill) const
{
	for (const FName& Piece : Pieces)
	{
		if (const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(Piece) : nullptr)
		{
			GrowthAddLines(Bill, URHDefinitionsSubsystem::GetFurnitureCost(*Row), 1.0);
		}
	}
}

void URHSimWorldSubsystem::ParseCivicPieces(const FRHRoomRow& Row, TArray<FName>& Out) const
{
	// "DiningTable;Chair;Chair": a piece a row, in order. A piece that does not exist, is dormant
	// or may not stand in this function is dropped (it is a data error the -citygrowth suite checks
	// for; the city just builds what it can).
	Out.Reset();
	TArray<FString> Parts;
	Row.CivicFurniture.ParseIntoArray(Parts, TEXT(";"));
	for (const FString& Part : Parts)
	{
		const FName Piece(*Part.TrimStartAndEnd());
		const FRHFurnitureRow* F = (Defs && !Piece.IsNone()) ? Defs->GetFurniture(Piece) : nullptr;
		if (F && F->SliceActive && URHDefinitionsSubsystem::FurnitureAllowsFunction(*F, Row.Function))
		{
			Out.Add(Piece);
		}
	}
}

TMap<FName, double> URHSimWorldSubsystem::FurnishGrowthRoom(const FIntVector& Key, const TArray<FName>& Pieces)
{
	TMap<FName, double> Unplaced;
	for (const FName& Piece : Pieces)
	{
		const FRHFurnitureRow* Row = Defs ? Defs->GetFurniture(Piece) : nullptr;
		if (!Row)
		{
			continue;
		}
		bool bPlaced = false;
		FString Why;
		for (int32 Slot = 0; Slot < FurnishGrid * FurnishGrid && !bPlaced; ++Slot)
		{
			if (CanPlaceFurniture(Piece, Key, Slot, 0, Why, /*bCheckQueued*/ false, /*bCheckCost*/ false))
			{
				AddFurniture(Piece, Key, Slot, 0);
				bPlaced = true;
			}
		}
		if (!bPlaced)
		{
			GrowthAddLines(Unplaced, URHDefinitionsSubsystem::GetFurnitureCost(*Row), 1.0);
		}
	}
	return Unplaced;
}

bool URHSimWorldSubsystem::IsCellFreeForRoom(int32 Level, int32 Cell) const
{
	const FIntVector Key(Level, Cell, 0);
	return Cell >= 0 && Cell < GetFloorCarvedCells(Level) && GetRoomAt(Level, Cell).IsNone() && GetCellFurniture(Key) == 0
		&& FindModuleOnCell(Level, Cell) == nullptr && BuildingMaskInCell(Key) == 0u;
}

bool URHSimWorldSubsystem::IsBayFreeForRoom(int32 HabId, int32 Bay) const
{
	// A corridor's bays are fixed Hallways (GetBayRoom says so) and a zoned bay has its room: only
	// a bare, unfurnished bay with no station module in it is free.
	const FIntVector Key(0, Bay, HabId);
	const FRHHabMemberCache* M = HabMembers.Find(HabId);
	return M && Bay >= 0 && Bay < M->NX * M->NY && GetBayRoom(HabId, Bay).IsNone() && FindModuleInBay(HabId, Bay) == nullptr
		&& GetCellFurniture(Key) == 0 && BuildingMaskInCell(Key) == 0u;
}

void URHSimWorldSubsystem::GatherFloorRoomGrid(int32 Level, TMap<FIntPoint, const FRHRoomRow*>& Out) const
{
	Out.Reset();
	const int32 Carved = GetFloorCarvedCells(Level);
	for (int32 i = 0; i < Carved; ++i)
	{
		const FRHRoomRow* Row = Defs ? Defs->GetRoom(GetRoomAt(Level, i)) : nullptr;
		if (Row && Row->SliceActive)
		{
			Out.Add(GetFloorCell(Level, i), Row);
		}
	}
}

void URHSimWorldSubsystem::GatherNetRoomGrid(const FRHHabNetCache& Net, TMap<FIntPoint, const FRHRoomRow*>& Out) const
{
	Out.Reset();
	for (const int32 HabId : Net.Members)
	{
		const FRHHabMemberCache& M = HabMembers.FindChecked(HabId);
		for (int32 Bay = 0; Bay < M.NX * M.NY; ++Bay)
		{
			const FRHRoomRow* Row = Defs ? Defs->GetRoom(GetBayRoom(HabId, Bay)) : nullptr;
			if (Row && Row->SliceActive)
			{
				Out.Add(CellPos(FIntVector(0, Bay, HabId)), Row);
			}
		}
	}
}

bool URHSimWorldSubsystem::ZoningWouldOffend(const FRHRoomRow& Row, const FIntPoint& Pos, const TMap<FIntPoint, const FRHRoomRow*>& Rooms) const
{
	// The adjacency calculus (habitat vision section 4) read conservatively: an emitter reaches
	// a refuser within Manhattan distance 2, and the city does not count on a Hallway partition
	// to cure it. The room offends nobody if nothing it emits is refused by a room within reach,
	// and is offended by nobody if nothing within reach emits what it refuses.
	static const FIntPoint Reach[12] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1),
		FIntPoint(2, 0), FIntPoint(-2, 0), FIntPoint(0, 2), FIntPoint(0, -2),
		FIntPoint(1, 1), FIntPoint(1, -1), FIntPoint(-1, 1), FIntPoint(-1, -1) };
	for (const FIntPoint& Off : Reach)
	{
		const FRHRoomRow* const* Other = Rooms.Find(Pos + Off);
		if (!Other || !*Other)
		{
			continue;
		}
		if (GrowthTagsMeet(Row.EmitsTags, (*Other)->RefusesTags) || GrowthTagsMeet((*Other)->EmitsTags, Row.RefusesTags))
		{
			return true;
		}
	}
	return false;
}

// --- The sol boundary -----------------------------------------------------------------

void URHSimWorldSubsystem::StepCityGrowth(float SubDt)
{
	// The boundary is a threshold on the monotone clock: the first step whose END falls in a new
	// sol. The agent band and the era band both read it from GetStepEndSeconds(), so both cross on
	// the same step window and run the very same code from here on.
	const int32 SolNow = FMath::FloorToInt32(GetStepEndSeconds() / (double)URHSimClockSubsystem::SolLengthSimSeconds);
	const bool bBoundary = SolNow > GrowthCheckedSol;
	if (bBoundary)
	{
		GrowthCheckedSol = SolNow;
	}
	RollProductionLedger(SolNow); // C5d: the sol's production goes into the window (one compare a step otherwise)
	if (Districts.Num() == 0 && GrowthProjects.Num() == 0)
	{
		return; // no districts, no growth: the colony is exactly as it was
	}

	// 1. The countdown: linear in dt, so the agent band and the era band finish on the same sol.
	const double DtSols = (double)SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds;
	for (int32 i = 0; i < GrowthProjects.Num();)
	{
		FRHGrowthProject& P = GrowthProjects[i];
		P.RemainingSols -= DtSols;
		if (P.RemainingSols > 1.0e-9)
		{
			++i;
			continue;
		}
		const FRHGrowthProject Done = P;
		GrowthProjects.RemoveAt(i);
		++GrowthVersion;
		const FRHDistrict* D = FindDistrict(Done.DistrictId);
		const FString Name = D ? D->Name : FString::Printf(TEXT("#%d"), Done.DistrictId);
		FString Why;
		if (CompleteGrowthProject(Done, Why))
		{
			GrowthSolFinished.Add(FString::Printf(TEXT("%s: %s"), *Name, *DescribeGrowthProject(Done)));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== CITY GROWTH (Sol %d): '%s' finished %s ==="), SolNow, *Name, *DescribeGrowthProject(Done));
		}
		else
		{
			CreditLander(Done.Bill, /*bOutputShelf*/ true); // nothing was applied: the whole bill comes back
			GrowthSolRefused.Add(FString::Printf(TEXT("%s: %s"), *Name, *DescribeGrowthProject(Done)));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== CITY GROWTH (Sol %d): '%s' could not build %s (%s) - the bill is returned: %s ==="), SolNow, *Name, *DescribeGrowthProject(Done), *Why, *GrowthBillText(Done.Bill));
		}
		OnGrowthChanged.Broadcast();
	}

	// 2. At a sol boundary, the one start; then the one alert.
	if (bBoundary)
	{
		RunGrowthStartCheck();
		EmitGrowthAlert(SolNow);
	}
}

void URHSimWorldSubsystem::EmitGrowthAlert(int32 Sol)
{
	if (GrowthSolStarted.Num() == 0 && GrowthSolFinished.Num() == 0 && GrowthSolRefused.Num() == 0)
	{
		return;
	}
	// One banner a sol for the whole city, never one per project or per person. Gate-D placeholder copy.
	TArray<FString> Parts;
	if (GrowthSolStarted.Num() > 0) { Parts.Add(FString::Printf(TEXT("under way: %s"), *FString::Join(GrowthSolStarted, TEXT("; ")))); }
	if (GrowthSolFinished.Num() > 0) { Parts.Add(FString::Printf(TEXT("finished: %s"), *FString::Join(GrowthSolFinished, TEXT("; ")))); }
	if (GrowthSolRefused.Num() > 0) { Parts.Add(FString::Printf(TEXT("not built, the site changed, materials back in stock: %s"), *FString::Join(GrowthSolRefused, TEXT("; ")))); }
	OnAlert.Broadcast(FString::Printf(TEXT("CITY GROWTH (sol %d) - %s"), Sol, *FString::Join(Parts, TEXT(" | "))));
	GrowthSolStarted.Reset();
	GrowthSolFinished.Reset();
	GrowthSolRefused.Reset();
}

int32 URHSimWorldSubsystem::RunGrowthStartCheck()
{
	GrowthNotes.Reset();
	GrowthPause.Reset();
	if (!bGrowthEnabled || Districts.Num() == 0 || !Defs)
	{
		return 0;
	}
	// The pauses: a city whose people are short of air, food or water does not start building.
	// At the lowest Hope band (LOW) it keeps building Civic rooms, the services that lift Hope,
	// and only the new homes wait (F5b, Gate-D review, 2026-10-21).
	int32 ShortHomes = 0;
	for (const FRHHomeRation& Home : HomeRations)
	{
		ShortHomes += Home.Fraction < 1.0 ? 1 : 0;
	}
	if (ShortHomes > 0)
	{
		GrowthPause = FString::Printf(TEXT("paused: %d home%s on short rations"), ShortHomes, GrowthPlural(ShortHomes));
		return 0;
	}
	const bool bHomesWait = HopeBand == ERHHopeBand::Failing;
	if (bHomesWait)
	{
		GrowthPause = TEXT("only builds services for now: new homes wait for Hope to recover");
	}

	const FRHCityDemand Demand = GetCityDemand();
	int32 PendingBeds = 0;
	for (const FRHGrowthProject& P : GrowthProjects)
	{
		PendingBeds += GetGrowthProjectBeds(P);
	}
	const int32 BedsShort = Demand.HousingBedsShort - PendingBeds; // the shortfall net of the beds already under way

	// Round-robin by Id from the cursor: the first district after the one that last started a project.
	const int32 N = Districts.Num();
	int32 First = 0;
	while (First < N && Districts[First].Id <= GrowthCursor)
	{
		++First;
	}
	if (First >= N)
	{
		First = 0;
	}
	for (int32 k = 0; k < N; ++k)
	{
		const FRHDistrict& D = Districts[(First + k) % N];
		if (D.Kind != FName(TEXT("Homes")) && D.Kind != FName(TEXT("Civic")))
		{
			continue; // the player places Industry, Farming and Research (Q2a)
		}
		if (!D.bGrowth)
		{
			GrowthNotes.Add(D.Id, TEXT("growth is switched off"));
			continue;
		}
		if (FindGrowthProject(D.Id))
		{
			GrowthNotes.Add(D.Id, TEXT("a project is under way"));
			continue;
		}
		FRHGrowthProject P;
		P.DistrictId = D.Id;
		FString Why;
		bool bPlanned = false;
		if (D.Kind == FName(TEXT("Homes")))
		{
			if (BedsShort <= 0)
			{
				GrowthNotes.Add(D.Id, Demand.Population <= 0 ? TEXT("nobody lives here yet") : TEXT("housing is not short"));
				continue;
			}
			if (bHomesWait)
			{
				GrowthNotes.Add(D.Id, TEXT("new homes wait for Hope to recover"));
				continue;
			}
			bPlanned = D.IsFloor() ? PlanFloorHomes(D, P, Why) : PlanSurfaceHomes(D, P, Why);
		}
		else
		{
			bPlanned = PlanCivic(D, Demand, P, Why);
		}
		if (!bPlanned)
		{
			GrowthNotes.Add(D.Id, Why);
			continue;
		}
		if (!GrowthBillFits(P.Bill, Why))
		{
			GrowthNotes.Add(D.Id, Why);
			continue;
		}
		GrowthCursor = D.Id;
		StartGrowthProject(MoveTemp(P));
		return 1;
	}
	return 0;
}

void URHSimWorldSubsystem::StartGrowthProject(FRHGrowthProject&& Project)
{
	// The whole bill, atomically: the caller has checked every line fits.
	for (const FName& Res : GrowthSortedKeys(Project.Bill))
	{
		TakeGrowthResource(Res, Project.Bill.FindChecked(Res));
	}
	Project.RemainingSols = Project.TotalSols;
	const FRHDistrict* D = FindDistrict(Project.DistrictId);
	const FString Name = D ? D->Name : FString::Printf(TEXT("#%d"), Project.DistrictId);
	GrowthSolStarted.Add(FString::Printf(TEXT("%s: %s"), *Name, *DescribeGrowthProject(Project)));
	UE_LOG(LogRedHopeSim, Display, TEXT("=== CITY GROWTH (Sol %d): '%s' (#%d, %s) begins %s - %.2f sol(s); bill: %s ==="),
		GrowthCheckedSol, *Name, Project.DistrictId, D ? *D->Kind.ToString() : TEXT("?"), *DescribeGrowthProject(Project), Project.TotalSols, *GrowthBillText(Project.Bill));
	int32 At = 0;
	while (At < GrowthProjects.Num() && GrowthProjects[At].DistrictId < Project.DistrictId)
	{
		++At;
	}
	GrowthProjects.Insert(MoveTemp(Project), At);
	++GrowthVersion;
	OnGrowthChanged.Broadcast();
}

void URHSimWorldSubsystem::CancelGrowthProject(int32 DistrictId, const TCHAR* Why)
{
	for (int32 i = 0; i < GrowthProjects.Num(); ++i)
	{
		if (GrowthProjects[i].DistrictId != DistrictId)
		{
			continue;
		}
		const FRHGrowthProject P = GrowthProjects[i];
		GrowthProjects.RemoveAt(i);
		CreditLander(P.Bill, /*bOutputShelf*/ true); // nothing was applied: a full refund
		++GrowthVersion;
		const FRHDistrict* D = FindDistrict(DistrictId);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== CITY GROWTH: '%s' - %s cancelled (%s); the whole bill is returned: %s ==="),
			D ? *D->Name : TEXT("?"), *DescribeGrowthProject(P), Why, *GrowthBillText(P.Bill));
		OnGrowthChanged.Broadcast();
		return;
	}
}

// --- Planning -------------------------------------------------------------------------

bool URHSimWorldSubsystem::PlanFloorHomes(const FRHDistrict& D, FRHGrowthProject& Out, FString& OutWhy) const
{
	const int32 Level = D.Level;
	if (!IsLevelConnected(Level) || !RatedFloors.Contains(Level))
	{
		OutWhy = FString::Printf(TEXT("floor %d is not rated yet"), Level);
		return false;
	}
	if (GetGrowthLivingRoom().IsNone())
	{
		OutWhy = TEXT("no Living room row is live");
		return false;
	}
	TArray<FName> Bunks;
	GetGrowthBunkPieces(Bunks);
	Out.Kind = ERHGrowthKind::FloorCells;
	Out.Level = Level;
	Out.Cells = GrowthCellsPerProject;
	Out.FirstCell = GetFloorCarvedCells(Level);
	AddGrowthFurnitureCost(Bunks, Out.Bill);
	for (TPair<FName, double>& L : Out.Bill)
	{
		L.Value *= Out.Cells;
	}
	Out.Bill.FindOrAdd(NAME_GrowthOxygen) += Out.Cells * O2FillKgPerCell; // the new cells' air, in full: the floor stays rated
	Out.TotalSols = GrowthCellSols;
	return true;
}

void URHSimWorldSubsystem::GatherShellSiteCells(TMap<FIntPoint, int32>& Out) const
{
	// Shells still under construction are not in the topology (HabBayOwner holds built ones), but a
	// new module may not touch or overlap one either.
	Out.Reset();
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Level != 0 || !B.bUnderConstruction)
		{
			continue;
		}
		const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
		const FIntPoint G = GetBayGridFor(Def, B.SizeX, B.SizeY);
		if (G.X <= 0)
		{
			continue;
		}
		const FIntPoint Min = GetBuildingAnchorLattice(B);
		for (int32 BY = 0; BY < G.Y; ++BY)
		{
			for (int32 BX = 0; BX < G.X; ++BX)
			{
				Out.Add(Min + FIntPoint(BX, BY), B.Id);
			}
		}
	}
}

bool URHSimWorldSubsystem::CheckSurfaceModuleSite(int32 DistrictId, const FRHHabNetCache& Net, const FIntPoint& Anchor, const FIntPoint& Size, const TMap<FIntPoint, int32>& SiteCells, FString& OutWhy) const
{
	// The module is Size bays from Anchor (its min corner), on free lattice cells inside the district,
	// edge-adjacent to Net and touching NO other shell; then the placement check without the material
	// part (every bay centre in coverage, off roads, machines, furniture and fittings).
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();
	const FRHHabMemberCache* RootM = HabMembers.Find(Net.Root);
	if (!RootM)
	{
		OutWhy = TEXT("the network is gone");
		return false;
	}
	const int32 NetIndex = RootM->NetIndex;
	static const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	bool bBeside = false;
	for (int32 DY = 0; DY < Size.Y; ++DY)
	{
		for (int32 DX = 0; DX < Size.X; ++DX)
		{
			const FIntPoint C = Anchor + FIntPoint(DX, DY);
			if (HabBayOwner.Contains(C) || SiteCells.Contains(C))
			{
				OutWhy = TEXT("the ground is taken by a shell");
				return false;
			}
			if (Geo.CellOwner.FindRef(C) != DistrictId)
			{
				OutWhy = TEXT("the ground is outside the district");
				return false;
			}
			for (const FIntPoint& Dir : Dirs)
			{
				const FIntPoint Q = C + Dir;
				if (Q.X >= Anchor.X && Q.X < Anchor.X + Size.X && Q.Y >= Anchor.Y && Q.Y < Anchor.Y + Size.Y)
				{
					continue; // the module's own cell
				}
				if (SiteCells.Contains(Q))
				{
					OutWhy = TEXT("it would touch a shell under construction");
					return false;
				}
				if (const int32* Owner = HabBayOwner.Find(Q))
				{
					const FRHHabMemberCache* OM = HabMembers.Find(*Owner);
					if (!OM || OM->NetIndex != NetIndex)
					{
						OutWhy = TEXT("it would touch another habitat");
						return false;
					}
					bBeside = true;
				}
			}
		}
	}
	if (!bBeside)
	{
		OutWhy = TEXT("it does not touch the network");
		return false;
	}
	const FVector Loc((Anchor.X + (Size.X - 1) * 0.5) * 1000.0, (Anchor.Y + (Size.Y - 1) * 0.5) * 1000.0, 0.0);
	return CanPlaceBuildingSized(NAME_GrowthHabModule, Loc, OutWhy, 0, Size.X, Size.Y, /*bCheckMaterials*/ false);
}

bool URHSimWorldSubsystem::FindSurfaceModuleSite(const FRHDistrict& D, const FRHHabNetCache& Net, FIntPoint& OutAnchor, FIntPoint& OutSize, FString& OutWhy) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(NAME_GrowthHabModule) : nullptr;
	if (!Def || !Def->SliceActive || !Def->Pressurized || !Def->DragSized || GetBayGridFor(Def, 1, 1) != FIntPoint(1, 1))
	{
		OutWhy = TEXT("the habitat module is not available");
		return false;
	}
	const int32 MaxSide = FMath::Min(2, GetMaxBedsPerSide(Def));
	const FRHDistrictGeo& Geo = EnsureDistrictGeo();
	const FRHHabMemberCache& RootM = HabMembers.FindChecked(Net.Root);
	TMap<FIntPoint, int32> SiteCells;
	GatherShellSiteCells(SiteCells);

	// Every module of at most 2 x 2 bays that has a cell on free ground of the district beside the
	// network: anchored (min corner) so that one of its cells is that neighbour. A set, then sorted:
	// the order comes from the sort key, never from the set.
	static const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	static const FIntPoint Sizes[4] = { FIntPoint(2, 2), FIntPoint(2, 1), FIntPoint(1, 2), FIntPoint(1, 1) };
	TSet<FIntVector> Seen; // (anchor X, anchor Y, SizeX * 10 + SizeY)
	TArray<FIntVector> Candidates;
	for (const int32 HabId : Net.Members)
	{
		const FRHHabMemberCache& M = HabMembers.FindChecked(HabId);
		for (int32 BY = 0; BY < M.NY; ++BY)
		{
			for (int32 BX = 0; BX < M.NX; ++BX)
			{
				const FIntPoint P = M.GridMin + FIntPoint(BX, BY);
				for (const FIntPoint& Dir : Dirs)
				{
					const FIntPoint Q = P + Dir;
					if (HabBayOwner.Contains(Q) || SiteCells.Contains(Q) || Geo.CellOwner.FindRef(Q) != D.Id)
					{
						continue;
					}
					for (const FIntPoint& S : Sizes)
					{
						if (S.X > MaxSide || S.Y > MaxSide)
						{
							continue;
						}
						for (int32 DY = 0; DY < S.Y; ++DY)
						{
							for (int32 DX = 0; DX < S.X; ++DX)
							{
								const FIntVector Key(Q.X - DX, Q.Y - DY, S.X * 10 + S.Y);
								if (!Seen.Contains(Key))
								{
									Seen.Add(Key);
									Candidates.Add(Key);
								}
							}
						}
					}
				}
			}
		}
	}
	// Nearest to the network root first by integer squared lattice distance, then (X, Y); at one
	// anchor the larger module first (2 x 2, 2 x 1, 1 x 2, 1 x 1). The key is unique.
	const FIntPoint Root = RootM.GridMin;
	const auto Dist2 = [&Root](const FIntVector& K) { return FMath::Square((int64)K.X - Root.X) + FMath::Square((int64)K.Y - Root.Y); };
	Candidates.Sort([&Dist2](const FIntVector& A, const FIntVector& B)
	{
		const int64 DA = Dist2(A), DB = Dist2(B);
		if (DA != DB) { return DA < DB; }
		if (A.X != B.X) { return A.X < B.X; }
		if (A.Y != B.Y) { return A.Y < B.Y; }
		const int32 AreaA = (A.Z / 10) * (A.Z % 10), AreaB = (B.Z / 10) * (B.Z % 10);
		if (AreaA != AreaB) { return AreaA > AreaB; }
		return A.Z > B.Z; // 2 x 1 before 1 x 2
	});
	FString LastWhy = TEXT("no free ground beside it inside the district");
	for (const FIntVector& K : Candidates)
	{
		const FIntPoint Anchor(K.X, K.Y), Size(K.Z / 10, K.Z % 10);
		FString Why;
		if (CheckSurfaceModuleSite(D.Id, Net, Anchor, Size, SiteCells, Why))
		{
			OutAnchor = Anchor;
			OutSize = Size;
			return true;
		}
		LastWhy = Why;
	}
	OutWhy = LastWhy;
	return false;
}

bool URHSimWorldSubsystem::PlanSurfaceHomes(const FRHDistrict& D, FRHGrowthProject& Out, FString& OutWhy) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(NAME_GrowthHabModule) : nullptr;
	if (GetGrowthLivingRoom().IsNone() || !Def)
	{
		OutWhy = TEXT("no Living room row or habitat module is live");
		return false;
	}
	const FRHDistrictMembership& Members = EnsureDistrictMembership();
	FString Why;
	bool bAnyNetwork = false;
	for (const FRHHabNetCache& Net : HabNets) // ascending root
	{
		bool bHasBay = false;
		for (const int32 Id : Net.Members)
		{
			bHasBay |= Members.OfBuilding.FindRef(Id) == D.Id;
		}
		if (!bHasBay)
		{
			continue;
		}
		bAnyNetwork = true;
		if (!IsNetRated(Net))
		{
			Why = FString::Printf(TEXT("habitat #%d is not rated yet"), Net.Root);
			continue;
		}
		if (IsNetSealed(Net))
		{
			// A new module opens doorways with no door in them: the network would stop being sealed.
			Why = FString::Printf(TEXT("habitat #%d is door-sealed - a new module would open new doorways, so it is not extended"), Net.Root);
			continue;
		}
		FIntPoint Anchor, Size;
		FString SiteWhy;
		if (!FindSurfaceModuleSite(D, Net, Anchor, Size, SiteWhy))
		{
			Why = FString::Printf(TEXT("habitat #%d has no room to grow: %s"), Net.Root, *SiteWhy);
			continue;
		}
		TArray<FName> Bunks;
		GetGrowthBunkPieces(Bunks);
		const int32 Bays = Size.X * Size.Y;
		Out.Kind = ERHGrowthKind::SurfaceModule;
		Out.Level = 0;
		Out.Cell = Anchor;
		Out.Size = Size;
		Out.HabId = Net.Root;
		GrowthAddLines(Out.Bill, URHDefinitionsSubsystem::GetBuildCostFor(*Def, 0), Bays);
		TMap<FName, double> Beds;
		AddGrowthFurnitureCost(Bunks, Beds);
		GrowthAddLines(Out.Bill, Beds, Bays);
		Out.Bill.FindOrAdd(NAME_GrowthOxygen) += Bays * O2FillKgPerCell;
		Out.TotalSols = GrowthModuleSols;
		return true;
	}
	OutWhy = bAnyNetwork ? Why : FString(TEXT("no habitat of the district to extend - the player founds habitats, the city grows them"));
	return false;
}

bool URHSimWorldSubsystem::PlanCivic(const FRHDistrict& D, const FRHCityDemand& Demand, FRHGrowthProject& Out, FString& OutWhy) const
{
	if (Demand.Population <= 0)
	{
		OutWhy = TEXT("nobody lives here yet");
		return false;
	}
	struct FCivic { FName Function; FName RowName; const FRHRoomRow* Row = nullptr; int32 Pending = 0; double Gap = 0.0; };
	TArray<FCivic> Civics;
	Defs->ForEachRoom([&](FName RowName, const FRHRoomRow& Row)
	{
		if (!Row.CivicService || Row.Function == NAME_GrowthLiving || RoomEmitsTag(Row, TEXT("Odor")))
		{
			return; // never an odor emitter: such a row is refused, whatever the data says
		}
		FCivic* Found = Civics.FindByPredicate([&Row](const FCivic& C) { return C.Function == Row.Function; });
		if (!Found)
		{
			FCivic C;
			C.Function = Row.Function;
			C.RowName = RowName;
			C.Row = &Row;
			Civics.Add(C);
		}
		else if (Row.Tier < Found->Row->Tier || (Row.Tier == Found->Row->Tier && RowName.LexicalLess(Found->RowName)))
		{
			Found->RowName = RowName; // the function's own row: its lowest tier, then the lexically first
			Found->Row = &Row;
		}
	});
	if (Civics.Num() == 0)
	{
		OutWhy = TEXT("no room row is marked CivicService");
		return false;
	}
	for (const FRHGrowthProject& P : GrowthProjects)
	{
		const FRHRoomRow* Row = P.Kind == ERHGrowthKind::CivicRoom ? Defs->GetRoom(P.Room) : nullptr;
		FCivic* Found = Row ? Civics.FindByPredicate([Row](const FCivic& C) { return C.Function == Row->Function; }) : nullptr;
		if (Found)
		{
			++Found->Pending; // a room already under way covers its share once it stands
		}
	}
	const FCivic* Worst = nullptr;
	for (FCivic& C : Civics)
	{
		const FRHServiceGap* G = Demand.ServiceGaps.FindByPredicate([&C](const FRHServiceGap& S) { return S.Function == C.Function; });
		if (!G || Demand.Population <= 0)
		{
			continue; // a function the Hope index does not weigh has no coverage to be short of
		}
		const double Coverage = FMath::Min(1.0, (double)(G->Rooms + C.Pending) * RoomServesResidents / (double)Demand.Population);
		C.Gap = 1.0 - Coverage;
		if (C.Gap > 0.0 && (!Worst || C.Gap > Worst->Gap || (C.Gap == Worst->Gap && C.Function.LexicalLess(Worst->Function))))
		{
			Worst = &C;
		}
	}
	if (!Worst)
	{
		OutWhy = TEXT("every civic service is covered");
		return false;
	}
	const FRHRoomRow& Row = *Worst->Row;
	TArray<FName> Pieces;
	ParseCivicPieces(Row, Pieces);

	Out.Kind = ERHGrowthKind::CivicRoom;
	Out.Room = Worst->RowName;
	Out.Level = D.Level;
	if (D.IsFloor())
	{
		const int32 Level = D.Level;
		if (!IsLevelConnected(Level) || !RatedFloors.Contains(Level))
		{
			OutWhy = FString::Printf(TEXT("floor %d is not rated yet"), Level);
			return false;
		}
		TMap<FIntPoint, const FRHRoomRow*> Grid;
		GatherFloorRoomGrid(Level, Grid);
		const int32 Carved = GetFloorCarvedCells(Level);
		TArray<FIntPoint> NextCell; // the cell a carve would add now: the plan first, else the lowest uncarved spiral cell
		PeekCarveCells(Level, 1, NextCell);
		int32 Free = 0, Chosen = INDEX_NONE;
		for (int32 i = 0; i < Carved && Chosen == INDEX_NONE; ++i)
		{
			if (!IsCellFreeForRoom(Level, i))
			{
				continue;
			}
			++Free;
			if (!ZoningWouldOffend(Row, GetFloorCell(Level, i), Grid))
			{
				Chosen = i;
			}
		}
		if (Chosen != INDEX_NONE)
		{
			Out.Index = Chosen;
		}
		else if (Free > 0)
		{
			OutWhy = TEXT("every free cell sits beside a room it would offend or that would offend it");
			return false;
		}
		else
		{
			// No free cell on the floor: carve one at its end (if the spot is clear of odor).
			if (ZoningWouldOffend(Row, NextCell[0], Grid))
			{
				OutWhy = TEXT("the cell it would carve sits beside a room it would offend or that would offend it");
				return false;
			}
			Out.bCarve = true;
			Out.Index = INDEX_NONE;
			Out.Bill.FindOrAdd(NAME_GrowthOxygen) += O2FillKgPerCell;
		}
	}
	else
	{
		const FRHDistrictMembership& Members = EnsureDistrictMembership();
		bool bFound = false;
		for (const FRHHabNetCache& Net : HabNets) // ascending root, members ascending, bays ascending
		{
			if (bFound || !IsNetRated(Net))
			{
				continue;
			}
			bool bHasBay = false;
			for (const int32 Id : Net.Members)
			{
				bHasBay |= Members.OfBuilding.FindRef(Id) == D.Id;
			}
			if (!bHasBay)
			{
				continue;
			}
			TMap<FIntPoint, const FRHRoomRow*> Grid;
			GatherNetRoomGrid(Net, Grid);
			for (const int32 HabId : Net.Members)
			{
				const FRHHabMemberCache& M = HabMembers.FindChecked(HabId);
				for (int32 Bay = 0; Bay < M.NX * M.NY && !bFound; ++Bay)
				{
					if (Members.OfBuilding.FindRef(HabId) == D.Id && IsBayFreeForRoom(HabId, Bay) && !ZoningWouldOffend(Row, CellPos(FIntVector(0, Bay, HabId)), Grid))
					{
						Out.HabId = HabId;
						Out.Index = Bay;
						bFound = true;
					}
				}
			}
		}
		if (!bFound)
		{
			OutWhy = TEXT("no free bay in a rated habitat of the district that the new room would not offend");
			return false;
		}
	}
	AddGrowthFurnitureCost(Pieces, Out.Bill);
	Out.TotalSols = GrowthRoomSols;
	return true;
}

// --- Completion -----------------------------------------------------------------------

bool URHSimWorldSubsystem::CompleteGrowthProject(const FRHGrowthProject& P, FString& OutWhy)
{
	const FRHDistrict* D = FindDistrict(P.DistrictId);
	if (!D)
	{
		OutWhy = TEXT("the district is gone");
		return false;
	}
	switch (P.Kind)
	{
	case ERHGrowthKind::SurfaceModule: return CompleteSurfaceModule(P, *D, OutWhy);
	case ERHGrowthKind::FloorCells: return CompleteFloorCells(P, OutWhy);
	case ERHGrowthKind::CivicRoom: return CompleteCivicRoom(P, OutWhy);
	}
	OutWhy = TEXT("unknown project kind");
	return false;
}

bool URHSimWorldSubsystem::CompleteSurfaceModule(const FRHGrowthProject& P, const FRHDistrict& D, FString& OutWhy)
{
	const FRHHabNetCache* Net = FindHabNet(P.HabId);
	const FName LivingRow = GetGrowthLivingRoom();
	if (!Net)
	{
		OutWhy = TEXT("the habitat it extends is gone");
		return false;
	}
	if (!IsNetRated(*Net))
	{
		OutWhy = TEXT("the habitat it extends is no longer rated");
		return false;
	}
	if (IsNetSealed(*Net))
	{
		OutWhy = TEXT("the habitat it extends has been sealed with doors");
		return false;
	}
	if (LivingRow.IsNone())
	{
		OutWhy = TEXT("no Living room row is live");
		return false;
	}
	TMap<FIntPoint, int32> SiteCells;
	GatherShellSiteCells(SiteCells);
	if (!CheckSurfaceModuleSite(D.Id, *Net, P.Cell, P.Size, SiteCells, OutWhy))
	{
		return false;
	}
	// The site holds: the building goes up in one step, the bays are zoned Living and given beds, and the
	// air for them comes out of the bill, so the network stays rated through it.
	const FVector Loc((P.Cell.X + (P.Size.X - 1) * 0.5) * 1000.0, (P.Cell.Y + (P.Size.Y - 1) * 0.5) * 1000.0, 0.0);
	AddBuilding(NAME_GrowthHabModule, Loc, /*bInstant*/ true, 0, P.Size.X, P.Size.Y);
	const int32 NewId = NextBuildingId - 1;
	if (!HabMembers.Contains(NewId))
	{
		OutWhy = TEXT("the new module did not join the topology");
		return false; // not reachable after the site check; the bill is refunded and the module (if any) stays
	}
	HabO2Kg.FindOrAdd(NewId) = P.Bill.FindRef(NAME_GrowthOxygen);
	TArray<FName> Bunks;
	GetGrowthBunkPieces(Bunks);
	TMap<FName, double> Unplaced;
	const int32 Bays = P.Size.X * P.Size.Y;
	for (int32 Bay = 0; Bay < Bays; ++Bay)
	{
		FString Why;
		if (!DesignateBay(NewId, Bay, LivingRow, Why))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("City growth: bay %d of the new module #%d would not take %s: %s"), Bay, NewId, *LivingRow.ToString(), *Why);
			continue;
		}
		GrowthAddLines(Unplaced, FurnishGrowthRoom(FIntVector(0, Bay, NewId), Bunks), 1.0);
	}
	CreditLander(Unplaced, true);
	RefreshJobs();
	return true;
}

bool URHSimWorldSubsystem::CompleteFloorCells(const FRHGrowthProject& P, FString& OutWhy)
{
	const FName LivingRow = GetGrowthLivingRoom();
	if (!IsLevelConnected(P.Level) || !RatedFloors.Contains(P.Level))
	{
		OutWhy = FString::Printf(TEXT("floor %d is no longer rated"), P.Level);
		return false;
	}
	if (LivingRow.IsNone())
	{
		OutWhy = TEXT("no Living room row is live");
		return false;
	}
	const int32 First = GetFloorCarvedCells(P.Level);
	FString Why;
	if (!ExcavateFloor(P.Level, P.Cells, Why))
	{
		OutWhy = Why;
		return false;
	}
	// The new volume is full of air from the first step.
	FloorO2Kg.FindOrAdd(P.Level) += P.Bill.FindRef(NAME_GrowthOxygen);
	TArray<FName> Bunks;
	GetGrowthBunkPieces(Bunks);
	TMap<FName, double> Unplaced;
	for (int32 i = 0; i < P.Cells; ++i)
	{
		if (!DesignateRoom(P.Level, First + i, LivingRow, Why))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("City growth: cell %d of floor %d would not take %s: %s"), First + i, P.Level, *LivingRow.ToString(), *Why);
			continue;
		}
		GrowthAddLines(Unplaced, FurnishGrowthRoom(FIntVector(P.Level, First + i, 0), Bunks), 1.0);
	}
	CreditLander(Unplaced, true);
	RefreshJobs();
	return true;
}

bool URHSimWorldSubsystem::CompleteCivicRoom(const FRHGrowthProject& P, FString& OutWhy)
{
	const FRHRoomRow* Row = Defs ? Defs->GetRoom(P.Room) : nullptr;
	if (!Row || !Row->SliceActive || !Row->CivicService || RoomEmitsTag(*Row, TEXT("Odor")))
	{
		OutWhy = TEXT("the room is no longer a civic service");
		return false;
	}
	TArray<FName> Pieces;
	ParseCivicPieces(*Row, Pieces);
	FString Why;
	if (P.Level < 0)
	{
		if (!IsLevelConnected(P.Level) || !RatedFloors.Contains(P.Level))
		{
			OutWhy = FString::Printf(TEXT("floor %d is no longer rated"), P.Level);
			return false;
		}
		TMap<FIntPoint, const FRHRoomRow*> Grid;
		GatherFloorRoomGrid(P.Level, Grid);
		int32 Cell = P.Index;
		const int32 Carved = GetFloorCarvedCells(P.Level);
		TArray<FIntPoint> NextCell;
		PeekCarveCells(P.Level, 1, NextCell);
		if (P.bCarve)
		{
			Cell = Carved; // the cell goes on the end of the floor, wherever it ends now
			if (ZoningWouldOffend(*Row, NextCell[0], Grid))
			{
				OutWhy = TEXT("the cell it would carve now sits beside a room it would offend");
				return false;
			}
		}
		else if (!IsCellFreeForRoom(P.Level, Cell) || ZoningWouldOffend(*Row, GetFloorCell(P.Level, Cell), Grid))
		{
			OutWhy = FString::Printf(TEXT("cell %d is no longer free or clear of odor"), Cell);
			return false;
		}
		if (P.bCarve)
		{
			if (!ExcavateFloor(P.Level, 1, Why))
			{
				OutWhy = Why;
				return false;
			}
			FloorO2Kg.FindOrAdd(P.Level) += P.Bill.FindRef(NAME_GrowthOxygen);
		}
		if (!DesignateRoom(P.Level, Cell, P.Room, Why))
		{
			OutWhy = Why;
			return false; // after a carve this leaves a bare cell: not reachable (the cell is fresh and free)
		}
		CreditLander(FurnishGrowthRoom(FIntVector(P.Level, Cell, 0), Pieces), true);
	}
	else
	{
		const FRHHabNetCache* Net = FindHabNet(P.HabId);
		if (!Net || !IsNetRated(*Net))
		{
			OutWhy = TEXT("the habitat is gone or no longer rated");
			return false;
		}
		TMap<FIntPoint, const FRHRoomRow*> Grid;
		GatherNetRoomGrid(*Net, Grid);
		if (!IsBayFreeForRoom(P.HabId, P.Index) || ZoningWouldOffend(*Row, CellPos(FIntVector(0, P.Index, P.HabId)), Grid))
		{
			OutWhy = FString::Printf(TEXT("bay %d is no longer free or clear of odor"), P.Index);
			return false;
		}
		if (!DesignateBay(P.HabId, P.Index, P.Room, Why))
		{
			OutWhy = Why;
			return false;
		}
		CreditLander(FurnishGrowthRoom(FIntVector(0, P.Index, P.HabId), Pieces), true);
	}
	RefreshJobs();
	return true;
}
