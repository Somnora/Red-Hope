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
	EventsTable = Cast<UDataTable>(EventsTablePath.TryLoad()); // absent = clear skies
	RoomsTable = Cast<UDataTable>(RoomsTablePath.TryLoad()); // absent = nothing designatable
	DiscoveriesTable = Cast<UDataTable>(DiscoveriesTablePath.TryLoad()); // absent = the layer stays dormant
	SolarCurveTable = Cast<UCurveTable>(SolarCurvePath.TryLoad());

	UE_LOG(LogRedHopeSim, Log, TEXT("Definitions loaded: buildings=%d robots=%d recipes=%d deposits=%d quotas=%d config=%d events=%d rooms=%d solarCurve=%s"),
		BuildingsTable ? BuildingsTable->GetRowMap().Num() : 0,
		RobotsTable ? RobotsTable->GetRowMap().Num() : 0,
		RecipesTable ? RecipesTable->GetRowMap().Num() : 0,
		DepositsTable ? DepositsTable->GetRowMap().Num() : 0,
		QuotasTable ? QuotasTable->GetRowMap().Num() : 0,
		ConfigTable ? ConfigTable->GetRowMap().Num() : 0,
		EventsTable ? EventsTable->GetRowMap().Num() : 0,
		RoomsTable ? RoomsTable->GetRowMap().Num() : 0,
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

const FRHRoomRow* URHDefinitionsSubsystem::GetRoom(FName Name) const
{
	return RoomsTable ? RoomsTable->FindRow<FRHRoomRow>(Name, TEXT("GetRoom"), false) : nullptr;
}

void URHDefinitionsSubsystem::ForEachRoom(TFunctionRef<void(FName, const FRHRoomRow&)> Fn) const
{
	if (!RoomsTable)
	{
		return;
	}
	for (const auto& Pair : RoomsTable->GetRowMap())
	{
		const FRHRoomRow* Row = reinterpret_cast<const FRHRoomRow*>(Pair.Value);
		if (Row->SliceActive)
		{
			Fn(Pair.Key, *Row);
		}
	}
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

void URHDefinitionsSubsystem::ForEachBuilding(TFunctionRef<void(FName, const FRHBuildingRow&)> Fn) const
{
	if (!BuildingsTable)
	{
		return;
	}
	for (const auto& Pair : BuildingsTable->GetRowMap())
	{
		const FRHBuildingRow* Row = reinterpret_cast<const FRHBuildingRow*>(Pair.Value);
		if (Row->SliceActive)
		{
			Fn(Pair.Key, *Row);
		}
	}
}

void URHDefinitionsSubsystem::ForEachEvent(TFunctionRef<void(FName, const FRHEventRow&)> Fn) const
{
	if (!EventsTable)
	{
		return;
	}
	for (const auto& Pair : EventsTable->GetRowMap())
	{
		const FRHEventRow* Row = reinterpret_cast<const FRHEventRow*>(Pair.Value);
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

const FRHRecipeRow* URHDefinitionsSubsystem::FindRunnableRecipe(FName BuildingDef, TFunctionRef<bool(const TMap<FName, double>&)> InputsOk, TFunctionRef<bool(const FRHRecipeRow&)> RowFilter) const
{
	if (!RecipesTable)
	{
		return nullptr;
	}
	for (const auto& Pair : RecipesTable->GetRowMap())
	{
		const FRHRecipeRow* Row = reinterpret_cast<const FRHRecipeRow*>(Pair.Value);
		if (Row->SliceActive && Row->Building == BuildingDef && RowFilter(*Row) && InputsOk(ParseResourceList(Row->Inputs)))
		{
			return Row;
		}
	}
	return nullptr;
}

const FRHRecipeRow* URHDefinitionsSubsystem::FindRecipeByOutput(FName BuildingDef, FName Resource) const
{
	if (!RecipesTable)
	{
		return nullptr;
	}
	for (const auto& Pair : RecipesTable->GetRowMap())
	{
		const FRHRecipeRow* Row = reinterpret_cast<const FRHRecipeRow*>(Pair.Value);
		if (Row->SliceActive && Row->Building == BuildingDef && ParseResourceList(Row->Outputs).Contains(Resource))
		{
			return Row;
		}
	}
	return nullptr;
}

const FRHRecipeRow* URHDefinitionsSubsystem::GetRecipe(FName Name) const
{
	return RecipesTable ? RecipesTable->FindRow<FRHRecipeRow>(Name, TEXT("GetRecipe"), false) : nullptr;
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

void URHDefinitionsSubsystem::Debug_InjectRoom(FName Name, const FRHRoomRow& Row)
{
	if (RoomsTable) { RoomsTable->AddRow(Name, Row); }
}

void URHDefinitionsSubsystem::Debug_InjectResource(FName Name, const FRHResourceRow& Row)
{
	if (ResourcesTable) { ResourcesTable->AddRow(Name, Row); }
}

void URHDefinitionsSubsystem::Debug_InjectManifest(FName Name, const FRHManifestItemRow& Row)
{
	if (ManifestTable) { ManifestTable->AddRow(Name, Row); }
}

void URHDefinitionsSubsystem::Debug_InjectDiscovery(FName Name, const FRHDiscoveryRow& Row)
{
	if (!DiscoveriesTable)
	{
		// The asset may not exist before its first editor import; a transient
		// table lets headless tests exercise the layer (dev-only).
		DiscoveriesTable = NewObject<UDataTable>(this, TEXT("DT_Discoveries_Transient"));
		DiscoveriesTable->RowStruct = FRHDiscoveryRow::StaticStruct();
	}
	DiscoveriesTable->AddRow(Name, Row);
}

const FRHDiscoveryRow* URHDefinitionsSubsystem::GetDiscovery(FName Name) const
{
	return DiscoveriesTable ? DiscoveriesTable->FindRow<FRHDiscoveryRow>(Name, TEXT("GetDiscovery"), false) : nullptr;
}

void URHDefinitionsSubsystem::GetDiscoveriesSorted(TArray<TPair<FName, const FRHDiscoveryRow*>>& Out) const
{
	Out.Reset();
	if (!DiscoveriesTable)
	{
		return;
	}
	for (const auto& Pair : DiscoveriesTable->GetRowMap())
	{
		const FRHDiscoveryRow* Row = reinterpret_cast<const FRHDiscoveryRow*>(Pair.Value);
		if (Row->SliceActive)
		{
			Out.Emplace(Pair.Key, Row);
		}
	}
	// Sort by the AUTHORED order, never map iteration order - the sequence is
	// deterministic regardless of import/insertion details.
	Out.Sort([](const TPair<FName, const FRHDiscoveryRow*>& A, const TPair<FName, const FRHDiscoveryRow*>& B)
	{
		return A.Value->Order != B.Value->Order ? A.Value->Order < B.Value->Order
			: A.Key.LexicalLess(B.Key);
	});
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
