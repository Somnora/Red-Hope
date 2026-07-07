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
	const FRHManifestItemRow* GetManifestItem(FName Name) const;

	// Hybrid logistics rule: solids (StorageType=Stockpile) are hauled;
	// fluids/gases/abstract flow instantly in the colony-wide pool.
	bool IsSolidResource(FName Name) const;

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
	UPROPERTY(Config) FSoftObjectPath ResourcesTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Resources.DT_Resources"));
	UPROPERTY(Config) FSoftObjectPath BuildingsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Buildings.DT_Buildings"));
	UPROPERTY(Config) FSoftObjectPath RobotsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Robots.DT_Robots"));
	UPROPERTY(Config) FSoftObjectPath ConfigTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Config.DT_Config"));
	UPROPERTY(Config) FSoftObjectPath RecipesTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Recipes.DT_Recipes"));
	UPROPERTY(Config) FSoftObjectPath DepositsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Deposits.DT_Deposits"));
	UPROPERTY(Config) FSoftObjectPath QuotasTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Quotas.DT_Quotas"));
	UPROPERTY(Config) FSoftObjectPath ManifestTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_ManifestItems.DT_ManifestItems"));
	UPROPERTY(Config) FSoftObjectPath EventsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Events.DT_Events"));
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
	UPROPERTY() TObjectPtr<UCurveTable> SolarCurveTable;
};
