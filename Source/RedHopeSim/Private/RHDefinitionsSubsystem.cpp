#include "RHDefinitionsSubsystem.h"
#include "RedHopeSim.h"
#include "Engine/DataTable.h"
#include "Engine/CurveTable.h"

void URHDefinitionsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	ResourcesTable = Cast<UDataTable>(ResourcesTablePath.TryLoad());
	BuildingsTable = Cast<UDataTable>(BuildingsTablePath.TryLoad());
	RobotsTable = Cast<UDataTable>(RobotsTablePath.TryLoad());
	ConfigTable = Cast<UDataTable>(ConfigTablePath.TryLoad());
	RecipesTable = Cast<UDataTable>(RecipesTablePath.TryLoad());
	DepositsTable = Cast<UDataTable>(DepositsTablePath.TryLoad());
	QuotasTable = Cast<UDataTable>(QuotasTablePath.TryLoad());
	ManifestTable = Cast<UDataTable>(ManifestTablePath.TryLoad());
	SolarCurveTable = Cast<UCurveTable>(SolarCurvePath.TryLoad());

	UE_LOG(LogRedHopeSim, Log, TEXT("Definitions loaded: buildings=%d robots=%d recipes=%d deposits=%d quotas=%d config=%d solarCurve=%s"),
		BuildingsTable ? BuildingsTable->GetRowMap().Num() : 0,
		RobotsTable ? RobotsTable->GetRowMap().Num() : 0,
		RecipesTable ? RecipesTable->GetRowMap().Num() : 0,
		DepositsTable ? DepositsTable->GetRowMap().Num() : 0,
		QuotasTable ? QuotasTable->GetRowMap().Num() : 0,
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

const FRHQuotaRow* URHDefinitionsSubsystem::GetQuota(FName Name) const
{
	return QuotasTable ? QuotasTable->FindRow<FRHQuotaRow>(Name, TEXT("GetQuota"), false) : nullptr;
}

const FRHManifestItemRow* URHDefinitionsSubsystem::GetManifestItem(FName Name) const
{
	return ManifestTable ? ManifestTable->FindRow<FRHManifestItemRow>(Name, TEXT("GetManifestItem"), false) : nullptr;
}

bool URHDefinitionsSubsystem::IsSolidResource(FName Name) const
{
	if (const FRHResourceRow* Row = ResourcesTable ? ResourcesTable->FindRow<FRHResourceRow>(Name, TEXT("IsSolidResource"), false) : nullptr)
	{
		return Row->StorageType == TEXT("Stockpile");
	}
	return true; // unknown resources default to physical
}

void URHDefinitionsSubsystem::ForEachRobot(TFunctionRef<void(FName, const FRHRobotRow&)> Fn) const
{
	if (!RobotsTable)
	{
		return;
	}
	for (const auto& Pair : RobotsTable->GetRowMap())
	{
		Fn(Pair.Key, *reinterpret_cast<const FRHRobotRow*>(Pair.Value));
	}
}

void URHDefinitionsSubsystem::ForEachDeposit(TFunctionRef<void(FName, const FRHDepositRow&)> Fn) const
{
	if (!DepositsTable)
	{
		return;
	}
	for (const auto& Pair : DepositsTable->GetRowMap())
	{
		const FRHDepositRow* Row = reinterpret_cast<const FRHDepositRow*>(Pair.Value);
		if (Row->SliceActive)
		{
			Fn(Pair.Key, *Row);
		}
	}
}

const FRHRecipeRow* URHDefinitionsSubsystem::FindRunnableRecipe(FName BuildingDef, TFunctionRef<bool(const TMap<FName, double>&)> InputsOk) const
{
	if (!RecipesTable)
	{
		return nullptr;
	}
	for (const auto& Pair : RecipesTable->GetRowMap())
	{
		const FRHRecipeRow* Row = reinterpret_cast<const FRHRecipeRow*>(Pair.Value);
		if (Row->SliceActive && Row->Building == BuildingDef && InputsOk(ParseResourceList(Row->Inputs)))
		{
			return Row;
		}
	}
	return nullptr;
}

TMap<FName, double> URHDefinitionsSubsystem::ParseResourceList(const FString& List)
{
	// "Struct:300;Water:200" -> map. Whitespace tolerated.
	TMap<FName, double> Out;
	TArray<FString> Entries;
	List.ParseIntoArray(Entries, TEXT(";"));
	for (const FString& Entry : Entries)
	{
		FString Res, Amount;
		if (Entry.Split(TEXT(":"), &Res, &Amount))
		{
			Out.Add(FName(*Res.TrimStartAndEnd()), FCString::Atod(*Amount));
		}
	}
	return Out;
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
