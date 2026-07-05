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
	UPROPERTY(Config) FSoftObjectPath BuildingsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Buildings.DT_Buildings"));
	UPROPERTY(Config) FSoftObjectPath RobotsTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Robots.DT_Robots"));
	UPROPERTY(Config) FSoftObjectPath ConfigTablePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/DT_Config.DT_Config"));
	UPROPERTY(Config) FSoftObjectPath SolarCurvePath = FSoftObjectPath(TEXT("/Game/RedHope/Data/CT_SolarDiurnal.CT_SolarDiurnal"));

	UPROPERTY() TObjectPtr<UDataTable> BuildingsTable;
	UPROPERTY() TObjectPtr<UDataTable> RobotsTable;
	UPROPERTY() TObjectPtr<UDataTable> ConfigTable;
	UPROPERTY() TObjectPtr<UCurveTable> SolarCurveTable;
};
