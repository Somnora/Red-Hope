#include "RHDefinitionsSubsystem.h"
#include "RedHopeSim.h"
#include "Engine/DataTable.h"
#include "Engine/CurveTable.h"

void URHDefinitionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	BuildingsTable = Cast<UDataTable>(BuildingsTablePath.TryLoad());
	RobotsTable = Cast<UDataTable>(RobotsTablePath.TryLoad());
	ConfigTable = Cast<UDataTable>(ConfigTablePath.TryLoad());
	SolarCurveTable = Cast<UCurveTable>(SolarCurvePath.TryLoad());

	UE_LOG(LogRedHopeSim, Log, TEXT("Definitions loaded: buildings=%d robots=%d config=%d solarCurve=%s"),
		BuildingsTable ? BuildingsTable->GetRowMap().Num() : 0,
		RobotsTable ? RobotsTable->GetRowMap().Num() : 0,
		ConfigTable ? ConfigTable->GetRowMap().Num() : 0,
		SolarCurveTable ? TEXT("ok") : TEXT("MISSING"));
}

const FRHBuildingRow* URHDefinitionsSubsystem::GetBuilding(FName Name) const
{
	return BuildingsTable ? BuildingsTable->FindRow<FRHBuildingRow>(Name, TEXT("GetBuilding"), false) : nullptr;
}

const FRHRobotRow* URHDefinitionsSubsystem::GetRobot(FName Name) const
{
	return RobotsTable ? RobotsTable->FindRow<FRHRobotRow>(Name, TEXT("GetRobot"), false) : nullptr;
}

double URHDefinitionsSubsystem::GetConfigScalar(FName Name, double Default) const
{
	if (const FRHConfigRow* Row = ConfigTable ? ConfigTable->FindRow<FRHConfigRow>(Name, TEXT("GetConfigScalar"), false) : nullptr)
	{
		return FCString::Atod(*Row->Value);
	}
	return Default;
}

float URHDefinitionsSubsystem::EvalSolarCurve(float SolFraction) const
{
	if (SolarCurveTable)
	{
		if (const FRealCurve* Curve = SolarCurveTable->FindCurve(FName("SolarOutput"), TEXT("EvalSolarCurve"), false))
		{
			return Curve->Eval(SolFraction);
		}
	}
	// Fallback: crude day window so the sim keeps working without content.
	return (SolFraction > 0.25f && SolFraction < 0.75f) ? 0.6f : 0.f;
}
