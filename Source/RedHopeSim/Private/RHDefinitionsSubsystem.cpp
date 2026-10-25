#include "RHDefinitionsSubsystem.h"
#include "RedHopeSim.h"
#include "Engine/DataTable.h"
#include "Engine/CurveTable.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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
	RivalsTable = Cast<UDataTable>(RivalsTablePath.TryLoad()); // absent = a lonely Mars
	CropsTable = Cast<UDataTable>(CropsTablePath.TryLoad()); // absent/dormant = the legacy flat-yield garden
	FurnitureTable = Cast<UDataTable>(FurnitureTablePath.TryLoad()); // absent = no furnishing layer
	DoorsTable = Cast<UDataTable>(DoorsTablePath.TryLoad()); // absent = no wall fittings
	CompartmentsTable = Cast<UDataTable>(CompartmentsTablePath.TryLoad()); // absent = no sealed-habitat rate
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

	// Balance sweeps (2026-10-15): a playtest can run "the world where X is
	// different" without editing the CSVs or the DataTables. Both flags act
	// only when present on the command line, and say so loudly:
	//   -RHConfig=StarterSolarPacks=9,HopePerJob=1.5   config-row overrides
	//   -RHQuotas=Q3                                   flip dormant quota rows live
	FString ConfigArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("RHConfig="), ConfigArg, /*bShouldStopOnSeparator*/ false))
	{
		ConfigArg = ConfigArg.Left(ConfigArg.Find(TEXT(" ")) == INDEX_NONE ? ConfigArg.Len() : ConfigArg.Find(TEXT(" ")));
		TArray<FString> Pairs;
		ConfigArg.ParseIntoArray(Pairs, TEXT(","));
		for (const FString& Pair : Pairs)
		{
			FString Key, Value;
			if (Pair.Split(TEXT("="), &Key, &Value))
			{
				ConfigOverrides.Add(FName(*Key), FCString::Atod(*Value));
				UE_LOG(LogRedHopeSim, Warning, TEXT("[RHConfig] command-line override %s = %s (balance sweep - not the shipped value)"), *Key, *Value);
			}
		}
	}
	FString QuotasArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("RHQuotas="), QuotasArg))
	{
		TArray<FString> Names;
		QuotasArg.ParseIntoArray(Names, TEXT(","));
		for (const FString& QName : Names)
		{
			const bool bOk = Debug_ActivateQuota(FName(*QName));
			UE_LOG(LogRedHopeSim, Warning, TEXT("[RHQuotas] command-line activation of %s: %s (balance sweep)"), *QName, bOk ? TEXT("live") : TEXT("NO SUCH ROW"));
		}
	}
	// Struct study (2026-10-24): -RHQuotaBill=Q3:Struct=1500[,Q3:Oxygen=900] rewrites one line of a quota row's bill, in memory, for
	// this run only (a balance sweep: the CSV and the DataTable asset are untouched). A line the row does not bill is not added.
	FString BillArg;
	if (FParse::Value(FCommandLine::Get(), TEXT("RHQuotaBill="), BillArg, /*bShouldStopOnSeparator*/ false))
	{
		BillArg = BillArg.Left(BillArg.Find(TEXT(" ")) == INDEX_NONE ? BillArg.Len() : BillArg.Find(TEXT(" ")));
		TArray<FString> Items;
		BillArg.ParseIntoArray(Items, TEXT(","));
		for (const FString& Item : Items)
		{
			FString RowName, Rest, Resource, KgText;
			if (!Item.Split(TEXT(":"), &RowName, &Rest) || !Rest.Split(TEXT("="), &Resource, &KgText))
			{
				UE_LOG(LogRedHopeSim, Warning, TEXT("[RHQuotaBill] cannot read '%s' (want Row:Resource=Kg)"), *Item);
				continue;
			}
			FRHQuotaRow* Row = QuotasTable ? QuotasTable->FindRow<FRHQuotaRow>(FName(*RowName), TEXT("RHQuotaBill"), false) : nullptr;
			bool bDone = false;
			if (Row)
			{
				TArray<FString> Lines;
				Row->Requirements.ParseIntoArray(Lines, TEXT(";"));
				for (FString& Line : Lines)
				{
					FString LineRes, LineKg;
					if (Line.Split(TEXT(":"), &LineRes, &LineKg) && LineRes.TrimStartAndEnd() == Resource)
					{
						Line = FString::Printf(TEXT("%s:%.0f"), *Resource, FCString::Atod(*KgText));
						bDone = true;
					}
				}
				if (bDone)
				{
					Row->Requirements = FString::Join(Lines, TEXT(";"));
				}
			}
			UE_LOG(LogRedHopeSim, Warning, TEXT("[RHQuotaBill] command-line override %s %s = %s kg: %s (balance sweep - not the shipped bill)"),
				*RowName, *Resource, *KgText, bDone ? *FString::Printf(TEXT("row now reads %s"), *Row->Requirements) : TEXT("NO SUCH ROW OR LINE"));
		}
	}
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

void URHDefinitionsSubsystem::ForEachManifestItem(TFunctionRef<void(FName, const FRHManifestItemRow&)> Fn) const
{
	if (!ManifestTable)
	{
		return;
	}
	for (const auto& Pair : ManifestTable->GetRowMap())
	{
		const FRHManifestItemRow* Row = reinterpret_cast<const FRHManifestItemRow*>(Pair.Value);
		if (Row->SliceActive)
		{
			Fn(Pair.Key, *Row);
		}
	}
}

void URHDefinitionsSubsystem::ForEachQuota(TFunctionRef<void(FName, const FRHQuotaRow&)> Fn) const
{
	if (!QuotasTable)
	{
		return;
	}
	for (const auto& Pair : QuotasTable->GetRowMap())
	{
		const FRHQuotaRow* Row = reinterpret_cast<const FRHQuotaRow*>(Pair.Value);
		if (Row->SliceActive)
		{
			Fn(Pair.Key, *Row);
		}
	}
}

bool URHDefinitionsSubsystem::ActivateRoomRow(FName Name)
{
	if (FRHRoomRow* Row = const_cast<FRHRoomRow*>(GetRoom(Name)))
	{
		Row->SliceActive = true;
		return true;
	}
	return false;
}

const FRHCropRow* URHDefinitionsSubsystem::GetCrop(FName Name) const
{
	return CropsTable ? CropsTable->FindRow<FRHCropRow>(Name, TEXT("GetCrop"), false) : nullptr;
}

void URHDefinitionsSubsystem::GetActiveCropsSorted(TArray<TPair<FName, const FRHCropRow*>>& Out) const
{
	Out.Reset();
	if (!CropsTable)
	{
		return;
	}
	for (const auto& Pair : CropsTable->GetRowMap())
	{
		const FRHCropRow* Row = reinterpret_cast<const FRHCropRow*>(Pair.Value);
		if (Row && Row->SliceActive)
		{
			Out.Emplace(Pair.Key, Row);
		}
	}
	Out.Sort([](const TPair<FName, const FRHCropRow*>& A, const TPair<FName, const FRHCropRow*>& B)
	{
		return A.Key.LexicalLess(B.Key);
	});
}

void URHDefinitionsSubsystem::ForEachCropRow(TFunctionRef<void(FName, const FRHCropRow&)> Fn) const
{
	if (!CropsTable)
	{
		return;
	}
	for (const auto& Pair : CropsTable->GetRowMap())
	{
		Fn(Pair.Key, *reinterpret_cast<const FRHCropRow*>(Pair.Value));
	}
}

const FRHFurnitureRow* URHDefinitionsSubsystem::GetFurniture(FName Name) const
{
	return FurnitureTable ? FurnitureTable->FindRow<FRHFurnitureRow>(Name, TEXT("GetFurniture"), false) : nullptr;
}

void URHDefinitionsSubsystem::GetActiveFurnitureSorted(TArray<TPair<FName, const FRHFurnitureRow*>>& Out) const
{
	Out.Reset();
	if (!FurnitureTable)
	{
		return;
	}
	for (const auto& Pair : FurnitureTable->GetRowMap())
	{
		const FRHFurnitureRow* Row = reinterpret_cast<const FRHFurnitureRow*>(Pair.Value);
		if (Row && Row->SliceActive)
		{
			Out.Emplace(Pair.Key, Row);
		}
	}
	Out.Sort([](const TPair<FName, const FRHFurnitureRow*>& A, const TPair<FName, const FRHFurnitureRow*>& B)
	{
		return A.Key.LexicalLess(B.Key);
	});
}

void URHDefinitionsSubsystem::ForEachFurnitureRow(TFunctionRef<void(FName, const FRHFurnitureRow&)> Fn) const
{
	if (!FurnitureTable)
	{
		return;
	}
	for (const auto& Pair : FurnitureTable->GetRowMap())
	{
		Fn(Pair.Key, *reinterpret_cast<const FRHFurnitureRow*>(Pair.Value));
	}
}

const FRHDoorRow* URHDefinitionsSubsystem::GetDoor(FName Name) const
{
	return DoorsTable ? DoorsTable->FindRow<FRHDoorRow>(Name, TEXT("GetDoor"), false) : nullptr;
}

void URHDefinitionsSubsystem::GetActiveDoorsSorted(TArray<TPair<FName, const FRHDoorRow*>>& Out) const
{
	Out.Reset();
	if (!DoorsTable)
	{
		return;
	}
	for (const auto& Pair : DoorsTable->GetRowMap())
	{
		const FRHDoorRow* Row = reinterpret_cast<const FRHDoorRow*>(Pair.Value);
		if (Row && Row->SliceActive)
		{
			Out.Emplace(Pair.Key, Row);
		}
	}
	Out.Sort([](const TPair<FName, const FRHDoorRow*>& A, const TPair<FName, const FRHDoorRow*>& B)
	{
		return A.Key.LexicalLess(B.Key);
	});
}

void URHDefinitionsSubsystem::ForEachDoorRow(TFunctionRef<void(FName, const FRHDoorRow&)> Fn) const
{
	if (!DoorsTable)
	{
		return;
	}
	for (const auto& Pair : DoorsTable->GetRowMap())
	{
		Fn(Pair.Key, *reinterpret_cast<const FRHDoorRow*>(Pair.Value));
	}
}

const FRHCompartmentRow* URHDefinitionsSubsystem::GetCompartment(FName Name) const
{
	return CompartmentsTable ? CompartmentsTable->FindRow<FRHCompartmentRow>(Name, TEXT("GetCompartment"), false) : nullptr;
}

bool URHDefinitionsSubsystem::ActivateFurnitureRow(FName Name)
{
	if (FRHFurnitureRow* Row = FurnitureTable ? FurnitureTable->FindRow<FRHFurnitureRow>(Name, TEXT("ActivateFurnitureRow"), false) : nullptr)
	{
		Row->SliceActive = true;
		return true;
	}
	return false;
}

void URHDefinitionsSubsystem::Debug_InjectFurniture(FName Name, const FRHFurnitureRow& Row)
{
	if (!FurnitureTable)
	{
		FurnitureTable = NewObject<UDataTable>(this, TEXT("DT_Furniture_Transient"));
		FurnitureTable->RowStruct = FRHFurnitureRow::StaticStruct();
	}
	FurnitureTable->AddRow(Name, Row);
}

bool URHDefinitionsSubsystem::ActivateCropRow(FName Name)
{
	if (FRHCropRow* Row = CropsTable ? CropsTable->FindRow<FRHCropRow>(Name, TEXT("ActivateCropRow"), false) : nullptr)
	{
		Row->SliceActive = true;
		return true;
	}
	return false;
}

bool URHDefinitionsSubsystem::Debug_ActivateQuota(FName Name)
{
	if (FRHQuotaRow* Row = QuotasTable ? QuotasTable->FindRow<FRHQuotaRow>(Name, TEXT("Debug_ActivateQuota"), false) : nullptr)
	{
		Row->SliceActive = true;
		return true;
	}
	return false;
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

const FRHResourceRow* URHDefinitionsSubsystem::GetResource(FName Name) const
{
	return ResourcesTable ? ResourcesTable->FindRow<FRHResourceRow>(Name, TEXT("GetResource"), false) : nullptr;
}

void URHDefinitionsSubsystem::ForEachResourceRow(TFunctionRef<void(FName, const FRHResourceRow&)> Fn) const
{
	if (!ResourcesTable)
	{
		return;
	}
	for (const auto& Pair : ResourcesTable->GetRowMap())
	{
		Fn(Pair.Key, *reinterpret_cast<const FRHResourceRow*>(Pair.Value));
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

bool URHDefinitionsSubsystem::ParseResourceListStrict(const FString& List, TArray<TPair<FName, int64>>& OutLines, FString& OutWhy)
{
	OutLines.Reset();
	OutWhy.Reset();
	if (List.TrimStartAndEnd().IsEmpty())
	{
		return true;
	}
	TArray<FString> Entries;
	List.ParseIntoArray(Entries, TEXT(";"), /*bCullEmpty*/ false);
	for (const FString& Raw : Entries)
	{
		const FString Entry = Raw.TrimStartAndEnd();
		if (Entry.IsEmpty())
		{
			OutWhy = TEXT("an empty entry (a doubled or trailing ';')");
			OutLines.Reset();
			return false;
		}
		FString Res, Amount;
		int32 Colons = 0;
		for (const TCHAR Ch : Entry) { Colons += Ch == TEXT(':') ? 1 : 0; }
		if (Colons != 1 || !Entry.Split(TEXT(":"), &Res, &Amount))
		{
			OutWhy = FString::Printf(TEXT("entry '%s' is not Res:kg (it needs exactly one ':')"), *Entry);
			OutLines.Reset();
			return false;
		}
		Res.TrimStartAndEndInline();
		Amount.TrimStartAndEndInline();
		if (Res.IsEmpty())
		{
			OutWhy = FString::Printf(TEXT("entry '%s' names no resource"), *Entry);
			OutLines.Reset();
			return false;
		}
		bool bDigits = Amount.Len() >= 1 && Amount.Len() <= 12;
		for (const TCHAR Ch : Amount) { bDigits &= Ch >= TEXT('0') && Ch <= TEXT('9'); }
		if (!bDigits)
		{
			OutWhy = FString::Printf(TEXT("entry '%s' is not a whole number of kg"), *Entry);
			OutLines.Reset();
			return false;
		}
		const FName Key(*Res);
		if (OutLines.ContainsByPredicate([Key](const TPair<FName, int64>& P) { return P.Key == Key; }))
		{
			OutWhy = FString::Printf(TEXT("'%s' appears twice"), *Res);
			OutLines.Reset();
			return false;
		}
		OutLines.Emplace(Key, FCString::Atoi64(*Amount));
	}
	return true;
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

const FRHRivalRow* URHDefinitionsSubsystem::GetRival(FName Name) const
{
	return RivalsTable ? RivalsTable->FindRow<FRHRivalRow>(Name, TEXT("GetRival"), false) : nullptr;
}

void URHDefinitionsSubsystem::ForEachRival(TFunctionRef<void(FName, const FRHRivalRow&)> Fn) const
{
	if (!RivalsTable)
	{
		return;
	}
	for (const auto& Pair : RivalsTable->GetRowMap())
	{
		const FRHRivalRow* Row = reinterpret_cast<const FRHRivalRow*>(Pair.Value);
		if (Row->SliceActive)
		{
			Fn(Pair.Key, *Row);
		}
	}
}

void URHDefinitionsSubsystem::ForEachRivalRow(TFunctionRef<void(FName, const FRHRivalRow&)> Fn) const
{
	if (!RivalsTable)
	{
		return;
	}
	for (const auto& Pair : RivalsTable->GetRowMap())
	{
		Fn(Pair.Key, *reinterpret_cast<const FRHRivalRow*>(Pair.Value));
	}
}

void URHDefinitionsSubsystem::Debug_InjectRival(FName Name, const FRHRivalRow& Row)
{
	if (!RivalsTable)
	{
		RivalsTable = NewObject<UDataTable>(this, TEXT("DT_Rivals_Transient"));
		RivalsTable->RowStruct = FRHRivalRow::StaticStruct();
	}
	RivalsTable->AddRow(Name, Row);
	++RivalsGeneration; // E4: the sim re-parses the economy columns
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
	if (const double* Override = ConfigOverrides.Find(Name))
	{
		return *Override; // -RHConfig balance sweep (see Initialize)
	}
	if (const FRHConfigRow* Row = ConfigTable ? ConfigTable->FindRow<FRHConfigRow>(Name, TEXT("GetConfigScalar"), false) : nullptr)
	{
		return FCString::Atod(*Row->Value);
	}
	return Default;
}

double URHDefinitionsSubsystem::GetConfigTableScalar(FName Name, double Missing) const
{
	if (const FRHConfigRow* Row = ConfigTable ? ConfigTable->FindRow<FRHConfigRow>(Name, TEXT("GetConfigTableScalar"), false) : nullptr)
	{
		return FCString::Atod(*Row->Value);
	}
	return Missing;
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
