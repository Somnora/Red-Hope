#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "Data/RHRows.h"

// E7, region-ready (docs/qa/2026-10-25/e7-region-ready-design.md: DIRECTOR ANSWER Q-A (b), Q-B (b) > REVISION 3 > REVISION 2 > REVISION
// (R3 only) > RE-CHECK > INDEPENDENT REVIEW > LEAD REVIEW > design). Depots and routes are derived on every call from the buildings and the
// rival rows: nothing here is cached, carries an Id or is saved, and E7 takes no save version. The colony's depot is a ROLE the Lander keeps
// (Q-A (b)): one rule, FindColonyDepotBuildingId, stands behind the three trade-facing Lander roles (a convoy's goods landing, a covert
// run's loot, the convoy's drawn start) and nothing else. Rows are ordered by name string (a case-sensitive ordinal compare, as the market
// orders them), never by FName index or table order.

namespace
{
	const FName NAME_RegionLander(TEXT("Lander"));

	bool RegionNameLess(const FName& A, const FName& B) { return A.ToString().Compare(B.ToString(), ESearchCase::CaseSensitive) < 0; }

	// The one length source: the row's distance, verbatim ((double) of a float is exact). Never whole metres: the leg must stay the double
	// HEAD computed (Row->DistanceKm / Speed), bit for bit. T6 replaces it with the map distance between the two depots.
	double RegionRouteLengthKm(const FRHRivalRow& Row) { return (double)Row.DistanceKm; }
}

// --- The colony's depot ----------------------------------------------------------------------------------------------------------------------

int32 URHSimWorldSubsystem::FindColonyDepotBuildingId(int32 RegionId) const
{
	// 1. The runtime binding, while its building exists and is complete (a demolished one hands the role back at once).
	if (const int32* Bound = DepotBindings.Find(RegionId))
	{
		const FRHBuildingInstance* B = FindBuildingById(*Bound);
		if (B && !B->bUnderConstruction)
		{
			return B->Id;
		}
	}
	// 2. The first complete Lander in the region, in list order (buildings stay in ascending Id order, so first is lowest Id).
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.DefName == NAME_RegionLander && !B.bUnderConstruction && GetBuildingRegionId(B) == RegionId)
		{
			return B.Id;
		}
	}
	// 3. No depot.
	return 0;
}

int32 URHSimWorldSubsystem::GetColonyDepotBindingId(int32 RegionId) const
{
	const int32* Bound = DepotBindings.Find(RegionId);
	return Bound ? *Bound : 0;
}

FVector URHSimWorldSubsystem::GetColonyDepotLocationCm(int32 RegionId) const
{
	const int32 Id = FindColonyDepotBuildingId(RegionId);
	const FRHBuildingInstance* B = Id != 0 ? FindBuildingById(Id) : nullptr;
	return B ? B->LocationCm : FVector::ZeroVector;
}

bool URHSimWorldSubsystem::Debug_SetColonyDepot(int32 RegionId, int32 BuildingId)
{
	if (RegionId != GetColonyRegionId())
	{
		return false; // one colony region until T2
	}
	if (BuildingId == 0)
	{
		DepotBindings.Remove(RegionId);
		return true;
	}
	const FRHBuildingInstance* B = FindBuildingById(BuildingId);
	if (!B || B->bUnderConstruction)
	{
		return false; // the rule stands
	}
	DepotBindings.Add(RegionId, BuildingId);
	return true;
}

// --- Routes and the rivals' depots -----------------------------------------------------------------------------------------------------------

int32 URHSimWorldSubsystem::GetRivalRegionId(FName Rival) const
{
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	return Row ? Row->RegionId : 0;
}

bool URHSimWorldSubsystem::GetRouteFor(FName Rival, FRHRoute& Out) const
{
	Out = FRHRoute();
	const FRHRivalRow* Row = Defs ? Defs->GetRival(Rival) : nullptr;
	if (!Row)
	{
		return false;
	}
	// The colony's end is the colony's own region and the same depot landing uses, whatever region the rival stands in.
	Out.Rival = Rival;
	Out.RegionA = GetColonyRegionId();
	Out.BuildingA = FindColonyDepotBuildingId(Out.RegionA);
	Out.RegionB = Row->RegionId;
	Out.LengthKm = RegionRouteLengthKm(*Row);
	Out.Kind = 0;
	return true;
}

double URHSimWorldSubsystem::GetConvoyLegSols(FName Rival) const
{
	FRHRoute R;
	const double Speed = FMath::Max(ConvoySpeedKmPerSol, KINDA_SMALL_NUMBER);
	return GetRouteFor(Rival, R) ? R.LengthKm / Speed : 0.0;
}

void URHSimWorldSubsystem::GetRoutes(TArray<FRHRoute>& Out) const
{
	Out.Reset();
	if (!Defs)
	{
		return;
	}
	// Every row, barter and dormant ones included, by name string: the walk BuildMarketState makes.
	TArray<TPair<FName, const FRHRivalRow*>> Rows;
	Defs->ForEachRivalRow([&Rows](FName Name, const FRHRivalRow& Row) { Rows.Emplace(Name, &Row); });
	Rows.Sort([](const TPair<FName, const FRHRivalRow*>& A, const TPair<FName, const FRHRivalRow*>& B) { return RegionNameLess(A.Key, B.Key); });
	const int32 RegionA = GetColonyRegionId();
	const int32 BuildingA = FindColonyDepotBuildingId(RegionA);
	Out.Reserve(Rows.Num());
	for (const TPair<FName, const FRHRivalRow*>& R : Rows)
	{
		FRHRoute& Route = Out.AddDefaulted_GetRef();
		Route.Rival = R.Key;
		Route.RegionA = RegionA;
		Route.BuildingA = BuildingA;
		Route.RegionB = R.Value->RegionId;
		Route.LengthKm = RegionRouteLengthKm(*R.Value);
		Route.Kind = 0;
	}
}

void URHSimWorldSubsystem::GetDepots(TArray<FRHDepot>& Out) const
{
	Out.Reset();
	// The colony's depots first, in RegionId order (one region until T2).
	FRHDepot& Colony = Out.AddDefaulted_GetRef();
	Colony.RegionId = GetColonyRegionId();
	Colony.Owner = NAME_None;
	Colony.BuildingId = FindColonyDepotBuildingId(Colony.RegionId);
	if (!Defs)
	{
		return;
	}
	TArray<TPair<FName, const FRHRivalRow*>> Rows;
	Defs->ForEachRivalRow([&Rows](FName Name, const FRHRivalRow& Row) { Rows.Emplace(Name, &Row); });
	Rows.Sort([](const TPair<FName, const FRHRivalRow*>& A, const TPair<FName, const FRHRivalRow*>& B) { return RegionNameLess(A.Key, B.Key); });
	Out.Reserve(1 + Rows.Num());
	for (const TPair<FName, const FRHRivalRow*>& R : Rows)
	{
		FRHDepot& Depot = Out.AddDefaulted_GetRef();
		Depot.RegionId = R.Value->RegionId;
		Depot.Owner = R.Key;
		Depot.BuildingId = 0; // off-map until T5
	}
}
