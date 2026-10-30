#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Data/RHRows.h"
#include "RHDefinitionsSubsystem.generated.h"

class UDataTable;
class UCurveTable;

// Read-only registry over the DT_*/CT_* assets. The single place sim code
// asks "what is a Forge" - definitions never live in code. Asset paths are
// config-overridable; defaults match the scaffold content layout.
UCLASS(Config = Game)
class REDHOPESIM_API URHDefinitionsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	const FRHBuildingRow* GetBuilding(FName Name) const;
	const FRHRobotRow* GetRobot(FName Name) const;
	const FRHQuotaRow* GetQuota(FName Name) const;
	// All slice-active quotas (the goal ladder, save v25). Ordering is the
	// caller's job (the sim advances by ascending DeadlineSol, then name).
	void ForEachQuota(TFunctionRef<void(FName, const FRHQuotaRow&)> Fn) const;
	// Flip a room row live (UnlockRoom discoveries; also the RH.ActivateRoom
	// test knob). In-memory only - the DT asset on disk is untouched; unlocks
	// re-apply from the discovery log on load. Returns false if no such row.
	bool ActivateRoomRow(FName Name);
	// Same knob for quota rows (test/bridge until the DT_Quotas sync lands).
	bool Debug_ActivateQuota(FName Name);
	const FRHManifestItemRow* GetManifestItem(FName Name) const;
	// All slice-active manifest items (the deck's supply-ship picker).
	void ForEachManifestItem(TFunctionRef<void(FName, const FRHManifestItemRow&)> Fn) const;
	// Room function row (M2 Gate B). Returns dormant rows too - callers gate
	// on SliceActive (designation refuses dormant functions).
	const FRHRoomRow* GetRoom(FName Name) const;
	// All slice-active room functions (the deck's designation palette).
	void ForEachRoom(TFunctionRef<void(FName, const FRHRoomRow&)> Fn) const;
	// Discoveries (M2 Gate D+): row by name, and the slice-active rows sorted
	// by their authored Order (the deterministic research sequence). Absent
	// table = no discoveries - the layer simply stays dormant.
	const FRHDiscoveryRow* GetDiscovery(FName Name) const;
	void GetDiscoveriesSorted(TArray<TPair<FName, const FRHDiscoveryRow*>>& Out) const;
	// Crops (greenhouse-agriculture Gate A): row by name (dormant rows too),
	// all slice-active crops sorted by name (deterministic rotation order),
	// and ALL rows for the pure-data verifier. Absent table / all-dormant =
	// the garden keeps its legacy flat-yield behavior exactly.
	const FRHCropRow* GetCrop(FName Name) const;
	void GetActiveCropsSorted(TArray<TPair<FName, const FRHCropRow*>>& Out) const;
	void ForEachCropRow(TFunctionRef<void(FName, const FRHCropRow&)> Fn) const;
	// Flips a dormant crop live in-memory (per-gate director flip / test knob;
	// the DT asset is untouched, same contract as ActivateRoomRow).
	bool ActivateCropRow(FName Name);
	// Furniture (BM-4): row by name (dormant rows too), the slice-active rows
	// sorted by name (the catalogue's order), all rows (pure-data verifier).
	// Absent table = no furnishing layer.
	bool HasFurnitureCatalogue() const { return FurnitureTable != nullptr; }
	// Wall fittings (BM-6, DT_Doors) and compartment classes (DT_Compartments).
	bool HasWallCatalogue() const { return DoorsTable != nullptr; }
	const FRHDoorRow* GetDoor(FName Name) const;
	void GetActiveDoorsSorted(TArray<TPair<FName, const FRHDoorRow*>>& Out) const;
	void ForEachDoorRow(TFunctionRef<void(FName, const FRHDoorRow&)> Fn) const;
	const FRHCompartmentRow* GetCompartment(FName Name) const;
	static TMap<FName, double> GetDoorCost(const FRHDoorRow& Row)
	{
		if (!Row.CostResources.IsEmpty())
		{
			return ParseResourceList(Row.CostResources);
		}
		TMap<FName, double> Cost;
		if (Row.CostStruct_kg > 0.f)
		{
			Cost.Add(FName(TEXT("Struct")), Row.CostStruct_kg);
		}
		return Cost;
	}
	const FRHFurnitureRow* GetFurniture(FName Name) const;
	void GetActiveFurnitureSorted(TArray<TPair<FName, const FRHFurnitureRow*>>& Out) const;
	void ForEachFurnitureRow(TFunctionRef<void(FName, const FRHFurnitureRow&)> Fn) const;
	bool ActivateFurnitureRow(FName Name);
	static TMap<FName, double> GetFurnitureCost(const FRHFurnitureRow& Row) { return ParseResourceList(Row.CostResources); }
	// True if the row may stand in a room of this Function.
	static bool FurnitureAllowsFunction(const FRHFurnitureRow& Row, FName Function)
	{
		TArray<FString> Parts;
		Row.RoomFunctions.ParseIntoArray(Parts, TEXT(";"), true);
		for (const FString& P : Parts)
		{
			if (FName(*P.TrimStartAndEnd()) == Function)
			{
				return true;
			}
		}
		return false;
	}
	// Rivals (M3 Gate A): row by name + all slice-active neighbors. Absent
	// table = a lonely Mars - the layer stays dormant.
	const FRHRivalRow* GetRival(FName Name) const;
	void ForEachRival(TFunctionRef<void(FName, const FRHRivalRow&)> Fn) const;
	// ALL rival rows including dormant (SliceActive=false) - discovery-on-scout
	// (M4 Gate B) reveals dormant settlements, so it must see them.
	void ForEachRivalRow(TFunctionRef<void(FName, const FRHRivalRow&)> Fn) const;

	// Hybrid logistics rule: solids (StorageType=Stockpile) are hauled;
	// fluids/gases/abstract flow instantly in the colony-wide pool.
	bool IsSolidResource(FName Name) const;
	// E1: a resource row by name (null when unknown), and every resource row, dormant ones included, in the table's own order (a
	// caller that needs an order sorts by name string).
	const FRHResourceRow* GetResource(FName Name) const;
	void ForEachResourceRow(TFunctionRef<void(FName, const FRHResourceRow&)> Fn) const;

	// All robot rows (for starting-fleet spawn).
	void ForEachRobot(TFunctionRef<void(FName, const FRHRobotRow&)> Fn) const;
	// All slice-active deposits.
	void ForEachDeposit(TFunctionRef<void(FName, const FRHDepositRow&)> Fn) const;
	// All slice-active building defs (the command deck's build palette).
	void ForEachBuilding(TFunctionRef<void(FName, const FRHBuildingRow&)> Fn) const;
	// All slice-active world events (M1-c). Absent table = zero events -
	// clear skies remain the default world.
	void ForEachEvent(TFunctionRef<void(FName, const FRHEventRow&)> Fn) const;
	// First slice-active recipe of a building whose inputs the predicate
	// accepts. Recipe Inputs/Outputs strings parse as "Res:Kg;Res:Kg".
	const FRHRecipeRow* FindRunnableRecipe(FName BuildingDef, TFunctionRef<bool(const TMap<FName, double>&)> InputsOk) const;
	// As above with an extra row predicate (M1-d Gate B: demand preference -
	// "first runnable recipe whose OUTPUT construction is short of" outranks
	// plain row order, so a Forge with a Shielding-hungry site makes Shielding
	// instead of smelting more Struct).
	const FRHRecipeRow* FindRunnableRecipe(FName BuildingDef, TFunctionRef<bool(const TMap<FName, double>&)> InputsOk, TFunctionRef<bool(const FRHRecipeRow&)> RowFilter) const;
	// First slice-active recipe of a building whose outputs include Resource
	// (the "can the colony produce this at all" preflight question).
	const FRHRecipeRow* FindRecipeByOutput(FName BuildingDef, FName Resource) const;
	// Recipe row by name (M1-d: designation-driven work - BoreFloor/CarveCell -
	// starts its recipe explicitly; it never passes the runnable search).
	const FRHRecipeRow* GetRecipe(FName Name) const;

	static TMap<FName, double> ParseResourceList(const FString& List);
	// E4: the STRICT parse of a "Res:kg;Res:kg" list of whole kg, for the rival economy columns. ParseResourceList skips an entry with no ':',
	// lets the last value of a repeated key win and reads "Ice:x" as 0, so it cannot say that a list is well formed; this one can. An empty
	// (or blank) list is valid and empty. Refused, with the reason in OutWhy and OutLines empty: an empty entry (";;" or a trailing ";"), an
	// entry without exactly one ':', an empty key, a value that is not 1-12 plain digits (a sign, a fraction, an exponent, a letter), a
	// repeated key (FName equality, which ignores case). Whitespace around a key or a value is tolerated. The lines come back in file order.
	static bool ParseResourceListStrict(const FString& List, TArray<TPair<FName, int64>>& OutLines, FString& OutWhy);

	// Construction bill of materials: CostResources when set, else the legacy
	// single-resource CostStruct_kg. Empty map = free to build (Lander, packs).
	static TMap<FName, double> GetBuildCost(const FRHBuildingRow& Def)
	{
		if (!Def.CostResources.IsEmpty())
		{
			return ParseResourceList(Def.CostResources);
		}
		TMap<FName, double> Cost;
		if (Def.CostStruct_kg > 0.f)
		{
			Cost.Add(FName(TEXT("Struct")), Def.CostStruct_kg);
		}
		return Cost;
	}

	// Bill of materials at a floor (M1-d Gate B): the base bill plus the
	// surface radiation tax - Level >= 0 adds Shielding:SurfaceShielding_kg;
	// underground the overburden shields for free. ALL construction math goes
	// through this; plain GetBuildCost is the Level-blind base bill.
	static TMap<FName, double> GetBuildCostFor(const FRHBuildingRow& Def, int32 Level)
	{
		TMap<FName, double> Cost = GetBuildCost(Def);
		if (Level >= 0 && Def.SurfaceShielding_kg > 0.f)
		{
			Cost.FindOrAdd(FName(TEXT("Shielding"))) += Def.SurfaceShielding_kg;
		}
		return Cost;
	}

	// Config scalars from DT_Config (Value column parsed as double).
	double GetConfigScalar(FName Name, double Default) const;
	// The DT_Config row only: ignores -RHConfig overrides and the C++ fallback,
	// so a pure-data check cannot be satisfied by anything but the synced row.
	double GetConfigTableScalar(FName Name, double Missing) const;

	// Dev-only in-memory row injection (headless tests exercise rows that a new
	// gate ADDS before the live DT is MCP-synced - the whole-row extension of
	// the const_cast SliceActive test-knob pattern). Never called in shipping.
	void Debug_InjectRoom(FName Name, const FRHRoomRow& Row);
	void Debug_InjectResource(FName Name, const FRHResourceRow& Row);
	void Debug_InjectManifest(FName Name, const FRHManifestItemRow& Row);
	// Discoveries: the DT asset may not exist at all before its first editor
	// import, so this one lazily creates a TRANSIENT table for the injection
	// (dev-only; the real asset supersedes it once imported).
	void Debug_InjectDiscovery(FName Name, const FRHDiscoveryRow& Row);
	void Debug_InjectRival(FName Name, const FRHRivalRow& Row);
	// E4: bumped whenever the rivals table changes (Debug_InjectRival, which RH.InjectRivals also calls), so the sim re-parses the economy
	// columns only then - at world start, at load, and on a change here - never once a roll.
	uint32 GetRivalsGeneration() const { return RivalsGeneration; }
	void Debug_InjectFurniture(FName Name, const FRHFurnitureRow& Row);

	// E7 (region-ready, Q-B (b)): a region's endowment, a permille on the rivals' production of one resource there (DT_Endowments; an absent
	// table, or a (region, resource) with no valid row, reads 1000 = the rows as written). The sim multiplies a rival's Produce kg by it in
	// integers (RHSimMarket.cpp, EvaluateRivalRow); nothing else reads it. The index is built at Initialize and on every hook, one Warning per
	// bad row (FRHEndowmentRow's rules); a harness override (Debug_SetEndowment) is consulted before it.
	int32 GetProducePermille(int32 RegionId, FName Resource) const;
	// The DT asset's rows only, in the table's own order: never an injected row or an override (RR0 compares the asset with the CSV).
	void ForEachEndowmentRow(TFunctionRef<void(FName, const FRHEndowmentRow&)> Fn) const;
	// Harness hooks (runtime only; the loaded asset is never touched). Each rebuilds the index with warnings on and bumps RivalsGeneration, so
	// the sim re-derives every rival's production on its next read. Debug_SetEndowment writes an override for one pair, consulted before the
	// table index and exempt from the duplicate rule; it is range-checked (false, with a Warning, for a permille outside 1..100,000, a region
	// below 0 or a resource that is no resource row). Debug_InjectEndowmentRow adds a raw row under the table's full validation (a row of the
	// same name replaces it). Debug_ClearEndowments drops every override and every injected row.
	bool Debug_SetEndowment(int32 RegionId, FName Resource, int32 Permille);
	void Debug_InjectEndowmentRow(FName Name, const FRHEndowmentRow& Row);
	void Debug_ClearEndowments();

	// Solar diurnal factor for a 0..1 sol fraction (CT_SolarDiurnal/SolarOutput).
	float EvalSolarCurve(float SolFraction) const;

	// Director directive: import-only is Phase 1 policy, not world law.
	// The ONLY place allowed to interpret ImportOnly/UnlockTech.
	bool CanFabricateLocally(const FRHBuildingRow& Def, const TSet<FName>& UnlockedTechs) const
	{
		if (!Def.ImportOnly)
		{
			return true;
		}
		return !Def.UnlockTech.IsNone() && UnlockedTechs.Contains(Def.UnlockTech);
	}

private:
	// -RHConfig=Name=Value,... command-line overrides (balance sweeps only).
	TMap<FName, double> ConfigOverrides;
	UPROPERTY(Config) FSoftObjectPath ResourcesTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Resources.DT_Resources"));
	UPROPERTY(Config) FSoftObjectPath BuildingsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Buildings.DT_Buildings"));
	UPROPERTY(Config) FSoftObjectPath RobotsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Robots.DT_Robots"));
	UPROPERTY(Config) FSoftObjectPath ConfigTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Config.DT_Config"));
	UPROPERTY(Config) FSoftObjectPath RecipesTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Recipes.DT_Recipes"));
	UPROPERTY(Config) FSoftObjectPath DepositsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Deposits.DT_Deposits"));
	UPROPERTY(Config) FSoftObjectPath QuotasTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Quotas.DT_Quotas"));
	UPROPERTY(Config) FSoftObjectPath ManifestTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_ManifestItems.DT_ManifestItems"));
	UPROPERTY(Config) FSoftObjectPath EventsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Events.DT_Events"));
	UPROPERTY(Config) FSoftObjectPath RoomsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Rooms.DT_Rooms"));
	UPROPERTY(Config) FSoftObjectPath DiscoveriesTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Discoveries.DT_Discoveries"));
	UPROPERTY(Config) FSoftObjectPath RivalsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Rivals.DT_Rivals"));
	UPROPERTY(Config) FSoftObjectPath EndowmentsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Endowments.DT_Endowments"));
	UPROPERTY(Config) FSoftObjectPath CropsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Crops.DT_Crops"));
	UPROPERTY(Config) FSoftObjectPath FurnitureTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Furniture.DT_Furniture"));
	UPROPERTY(Config) FSoftObjectPath DoorsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Doors.DT_Doors"));
	UPROPERTY(Config) FSoftObjectPath CompartmentsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Compartments.DT_Compartments"));
	UPROPERTY(Config) FSoftObjectPath SolarCurvePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/CT_SolarDiurnal.CT_SolarDiurnal"));

	UPROPERTY() TObjectPtr<UDataTable> ResourcesTable;
	UPROPERTY() TObjectPtr<UDataTable> BuildingsTable;
	UPROPERTY() TObjectPtr<UDataTable> RobotsTable;
	UPROPERTY() TObjectPtr<UDataTable> ConfigTable;
	UPROPERTY() TObjectPtr<UDataTable> RecipesTable;
	UPROPERTY() TObjectPtr<UDataTable> DepositsTable;
	UPROPERTY() TObjectPtr<UDataTable> QuotasTable;
	UPROPERTY() TObjectPtr<UDataTable> ManifestTable;
	UPROPERTY() TObjectPtr<UDataTable> EventsTable;
	UPROPERTY() TObjectPtr<UDataTable> RoomsTable;
	UPROPERTY() TObjectPtr<UDataTable> DiscoveriesTable;
	UPROPERTY() TObjectPtr<UDataTable> RivalsTable;
	uint32 RivalsGeneration = 0; // E4: bumped by Debug_InjectRival (E7: and by every endowment hook)
	// E7: the endowment table, the harness's injected rows (kept here, never AddRow'd into the asset) and overrides, and the index of the
	// valid rows by (RegionId, Resource).
	UPROPERTY() TObjectPtr<UDataTable> EndowmentsTable;
	TArray<TPair<FName, FRHEndowmentRow>> InjectedEndowments;
	TMap<TPair<int32, FName>, int32> EndowmentOverrides;
	TMap<TPair<int32, FName>, int32> EndowmentIndex;
	void RebuildEndowmentIndex(bool bWarn);
	bool IsResourceRowKey(FName Resource) const; // a resource row's key, compared case-sensitively (FName equality ignores case)
	UPROPERTY() TObjectPtr<UDataTable> CropsTable;
	UPROPERTY() TObjectPtr<UDataTable> FurnitureTable;
	UPROPERTY() TObjectPtr<UDataTable> DoorsTable;
	UPROPERTY() TObjectPtr<UDataTable> CompartmentsTable;
	UPROPERTY() TObjectPtr<UCurveTable> SolarCurveTable;
};
