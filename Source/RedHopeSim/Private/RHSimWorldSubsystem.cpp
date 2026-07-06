#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHAgentSubsystem.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/NameAsStringProxyArchive.h"

namespace
{
	const FName NAME_Struct(TEXT("Struct"));
	const FName NAME_Lander(TEXT("Lander"));
	const FName NAME_Stockpile(TEXT("Stockpile"));
	const FName NAME_Q1(TEXT("Q1"));

	// Save format: versioned binary, sim-owned (approved architecture). Bump
	// the version on any payload change; old versions refuse loudly.
	constexpr uint32 RHSaveMagic = 0x52485331;   // 'RHS1'
	constexpr uint32 RHSaveVersion = 1;

	FString SaveSlotToPath(const FString& Slot)
	{
		return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), *Slot);
	}

	void SerializeSite(FArchive& Ar, FRHSiteRef& Site)
	{
		Ar << Site.BuildingId;
		Ar << Site.DepositId;
	}

	void SerializeResourceMap(FArchive& Ar, TMap<FName, double>& Map)
	{
		int32 Num = Map.Num();
		Ar << Num;
		if (Ar.IsLoading())
		{
			Map.Reset();
			for (int32 i = 0; i < Num; ++i)
			{
				FName Key; double Value = 0.0;
				Ar << Key; Ar << Value;
				Map.Add(Key, Value);
			}
		}
		else
		{
			for (auto& Pair : Map)
			{
				FName Key = Pair.Key;
				Ar << Key; Ar << Pair.Value;
			}
		}
	}
}

void URHSimWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Defs = Collection.InitializeDependency<URHDefinitionsSubsystem>();
	Clock = Collection.InitializeDependency<URHSimClockSubsystem>();
}

void URHSimWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (!Defs)
	{
		return;
	}

	OrderLagSeconds = Defs->GetConfigScalar(FName("OrderLagTier0_s"), OrderLagSeconds);
	PileCapKg = Defs->GetConfigScalar(FName("PilesCapacity_kg"), PileCapKg);
	HaulLoadMinKg = FMath::Max(25.0, PileCapKg / 5.0);
	ChargeSeekFraction = (float)Defs->GetConfigScalar(FName("ChargeSeekFraction"), ChargeSeekFraction);
	ChargeResumeFraction = (float)Defs->GetConfigScalar(FName("ChargeResumeFraction"), ChargeResumeFraction);
	AutosaveEverySols = Defs->GetConfigScalar(FName("AutosaveEverySols"), 0.0);
	if (Clock)
	{
		Clock->OnSolElapsed.AddUObject(this, &URHSimWorldSubsystem::HandleSolElapsed);
		// Mission clock at landing (StartSolHour, sol-hour = 50 sim-s). Default
		// 6 = dawn: the colony wakes with the sun instead of five real minutes
		// of darkness (director's first-play feedback).
		const double StartHour = Defs->GetConfigScalar(FName("StartSolHour"), 0.0);
		if (StartHour > 0.0)
		{
			Clock->Debug_SetSimSeconds(StartHour * 50.0);
		}
	}

	// Deposits from data.
	Defs->ForEachDeposit([this](FName RowName, const FRHDepositRow& Row)
	{
		FRHDepositState D;
		D.Id = Deposits.Num() + 1;
		D.RowName = RowName;
		D.Type = Row.Type;
		D.RemainingKg = Row.Mass_kg;
		D.LocationCm = FVector(Row.LocX_m * 100.f, Row.LocY_m * 100.f, 0.f);
		Deposits.Add(D);
	});

	// Mission start: the Lander, its starter Struct, and the imported
	// flat-pack stock (import-only hardware arrives, never smelted).
	AddBuilding(NAME_Lander, FVector::ZeroVector, /*bInstant*/ true);
	if (FRHBuildingInstance* Lander = FindBuilding(1))
	{
		Lander->InputKg.Add(NAME_Struct, Defs->GetConfigScalar(FName("StarterStruct_kg"), 400.0));
	}
	ImportStock.Add(FName("SolarArray"), (int32)Defs->GetConfigScalar(FName("StarterSolarPacks"), 6));
	ImportStock.Add(FName("BatteryBank"), (int32)Defs->GetConfigScalar(FName("StarterBatteryPacks"), 2));

	// Fleet deploys on the first sim step, after every subsystem's
	// BeginPlay has run - subscribers never miss the spawn broadcast.

	UE_LOG(LogRedHopeSim, Display, TEXT("Colony sim online. Lag %.0f sim-s. Deposits %d. Starter Struct %.0f kg."),
		OrderLagSeconds, Deposits.Num(), GetTotalSolid(NAME_Struct));
}

void URHSimWorldSubsystem::SpawnStartingFleet()
{
	TArray<FMassEntityHandle> All;
	int32 Slot = 0;
	Defs->ForEachRobot([&](FName RowName, const FRHRobotRow& Row)
	{
		for (int32 i = 0; i < Row.StartingCount; ++i)
		{
			// Deploy ring south of the lander.
			const float Angle = Slot * 0.8f;
			const FVector Pos(-800.f + 250.f * Slot, -600.f + 150.f * FMath::Sin(Angle), 50.f);
			SpawnRobotTracked(RowName, Row, Pos, Row.Battery_Wh, 0.f, All);
			++Slot;
		}
	});
	UE_LOG(LogRedHopeSim, Display, TEXT("Starting fleet deployed: %d robots"), All.Num());
	OnRobotsSpawned.Broadcast(All);
}

void URHSimWorldSubsystem::DeployFleetOnce()
{
	if (!bFleetDeployed)
	{
		bFleetDeployed = true;
		SpawnStartingFleet();
	}
}

FMassEntityHandle URHSimWorldSubsystem::SpawnRobotTracked(FName RowName, const FRHRobotRow& Row, const FVector& PosCm, float ChargeWh, float Wear, TArray<FMassEntityHandle>& OutSpawned)
{
	URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();
	if (!Agents)
	{
		return FMassEntityHandle();
	}
	const FMassEntityHandle H = Agents->SpawnRobotWithState(RowName, Row, PosCm, ChargeWh, Wear);
	if (H.IsValid())
	{
		FleetCounts.FindOrAdd(RowName)++;
		OutSpawned.Add(H);
	}
	return H;
}

void URHSimWorldSubsystem::HandleSolElapsed(int32 NewSol)
{
	if (AutosaveEverySols > 0.0 && NewSol != LastAutosaveSol
		&& (NewSol % FMath::Max(1, (int32)AutosaveEverySols)) == 0)
	{
		LastAutosaveSol = NewSol;
		FString Error;
		if (!SaveColony(TEXT("auto"), Error))
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("Autosave failed: %s"), *Error);
		}
	}
}

void URHSimWorldSubsystem::Tick(float DeltaTime)
{
	if (!Clock)
	{
		return;
	}
	const int32 Steps = Clock->GetStepsThisFrame();
	for (int32 i = 0; i < Steps; ++i)
	{
		StepSim(URHSimClockSubsystem::SubStepSeconds);
	}

	// Era band: agent sub-steps stop publishing; the ledger integrator runs.
	// Any agent-fidelity event yanks the throttle back to 1x (auto-drop).
	if (Clock->IsEraMode())
	{
		FString Why;
		if (!CanEnterEraMode(Why))
		{
			Clock->SetSpeed(1.f);
			UE_LOG(LogRedHopeSim, Display, TEXT("ERA DROP: %s - returning to 1x"), *Why);
			return;
		}
	}
	const int32 EraSteps = Clock->GetEraStepsThisFrame();
	for (int32 i = 0; i < EraSteps; ++i)
	{
		EraStep(URHSimClockSubsystem::EraStepSimSeconds);
	}
}

void URHSimWorldSubsystem::StepSim(float SubDt)
{
	DeployFleetOnce();

	// Fixed order: orders land, work is posted, factories run, the quota
	// arc advances, power settles.
	StepUplink();
	StepTaskBoard();
	StepProduction(SubDt);
	StepQuota();
	StepPower(SubDt);
}

bool URHSimWorldSubsystem::CanEnterEraMode(FString& OutReason) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction)
		{
			OutReason = FString::Printf(TEXT("construction in progress (%s #%d)"), *B.DefName.ToString(), B.Id);
			return false;
		}
	}
	if (QuotaPhase == ERHQuotaPhase::ShipInbound)
	{
		OutReason = TEXT("supply ship inbound");
		return false;
	}
	if (QuotaPhase == ERHQuotaPhase::Open && Defs && Clock)
	{
		if (const FRHQuotaRow* Quota = Defs->GetQuota(NAME_Q1))
		{
			const double DeadlineSimSeconds = (double)(Quota->DeadlineSol + 1) * URHSimClockSubsystem::SolLengthSimSeconds;
			if (DeadlineSimSeconds - Clock->GetSimSecondsTotal() < URHSimClockSubsystem::SolLengthSimSeconds)
			{
				OutReason = TEXT("quota deadline within one sol");
				return false;
			}
		}
	}
	return true;
}

void URHSimWorldSubsystem::EraStep(float DtSimSeconds)
{
	DeployFleetOnce();

	// Same spine as StepSim at a coarser dt: the sub-step functions are
	// already dimensionally honest, so era mode is the same integrator run
	// at 1 sim-minute. Only dig/haul need the abstract stand-in (agents park).
	StepUplink();
	EraLogistics(DtSimSeconds);
	StepProduction(DtSimSeconds);
	StepQuota();
	StepPower(DtSimSeconds);
}

void URHSimWorldSubsystem::EraLogistics(float DtSimSeconds)
{
	// Aggregate dig rate of the parked excavator fleet (kg per sol-hour).
	double DigRateKgPerH = 0.0;
	for (const auto& Fleet : FleetCounts)
	{
		if (const FRHRobotRow* Row = Defs->GetRobot(Fleet.Key))
		{
			if (Row->RobotClass == FName("Excavator"))
			{
				DigRateKgPerH += (double)Fleet.Value * Row->WorkRate;
			}
		}
	}
	double DigBudgetKg = DigRateKgPerH * (DtSimSeconds / 50.0); // sol-hour = 50 sim-s

	// Ground -> demanding hopper at fleet rate (piles are an agent-band
	// fidelity detail; era mode digs straight into the chain).
	for (FRHDepositState& D : Deposits)
	{
		if (DigBudgetKg <= 0.0)
		{
			break;
		}
		if (!D.bDesignated || D.RemainingKg <= 0.0)
		{
			continue;
		}
		for (FRHBuildingInstance& B : Buildings)
		{
			if (B.bUnderConstruction)
			{
				continue;
			}
			const bool bWants = Defs->FindRunnableRecipe(B.DefName, [&](const TMap<FName, double>& Inputs)
			{
				const double* Need = Inputs.Find(D.Type);
				if (!Need)
				{
					return false;
				}
				const double* Have = B.InputKg.Find(D.Type);
				return (!Have || *Have < *Need * 2.0);
			}) != nullptr;
			if (!bWants)
			{
				continue;
			}
			const double TakeKg = FMath::Min3(DigBudgetKg, D.RemainingKg, 200.0);
			D.RemainingKg -= TakeKg;
			B.InputKg.FindOrAdd(D.Type) += TakeKg;
			DigBudgetKg -= TakeKg;
			break;
		}
	}

	// Finished outputs teleport to demanders, else to a store. Haulers are
	// not modeled above the agent band (M1 acceptance: <=5% divergence over
	// 10 sols vs the agent sim; the paired-run harness owns that bar).
	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction || B.DefName == NAME_Stockpile || B.DefName == NAME_Lander)
		{
			continue;
		}
		for (auto& Out : B.OutputKg)
		{
			if (Out.Value <= 0.0)
			{
				continue;
			}
			FRHBuildingInstance* Dest = nullptr;
			for (FRHBuildingInstance& C : Buildings)
			{
				if (C.bUnderConstruction || C.Id == B.Id)
				{
					continue;
				}
				if (Defs->FindRunnableRecipe(C.DefName, [&](const TMap<FName, double>& Inputs)
					{
						const double* Need = Inputs.Find(Out.Key);
						if (!Need)
						{
							return false;
						}
						const double* Have = C.InputKg.Find(Out.Key);
						return (!Have || *Have < *Need * 2.0);
					}))
				{
					Dest = &C;
					break;
				}
			}
			if (!Dest)
			{
				for (FRHBuildingInstance& S : Buildings)
				{
					if (!S.bUnderConstruction && S.Id != B.Id && (S.DefName == NAME_Stockpile || S.DefName == NAME_Lander))
					{
						Dest = &S;
						break;
					}
				}
			}
			if (Dest)
			{
				Dest->InputKg.FindOrAdd(Out.Key) += Out.Value;
				Out.Value = 0.0;
			}
		}
	}
}

void URHSimWorldSubsystem::StepUplink()
{
	const double Now = Clock->GetSimSecondsTotal();
	for (int32 i = 0; i < UplinkQueue.Num();)
	{
		if (UplinkQueue[i].ExecuteAtSimSeconds <= Now)
		{
			FRHCommand Cmd = UplinkQueue[i];
			UplinkQueue.RemoveAt(i);
			ExecuteCommand(Cmd);
		}
		else
		{
			++i;
		}
	}
}

void URHSimWorldSubsystem::StepTaskBoard()
{
	// Haul: deposit piles worth collecting -> Forge hopper (if a Forge wants
	// that resource) else Stockpile-capable store (Stockpile or Lander).
	for (const FRHDepositState& D : Deposits)
	{
		if (D.PileKg < HaulLoadMinKg)
		{
			continue;
		}
		FRHSiteRef From; From.DepositId = D.Id;

		int32 DestId = 0;
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.bUnderConstruction)
			{
				continue;
			}
			// A production building wants the resource if some slice recipe
			// there consumes it and the hopper holds less than two batches.
			if (Defs->FindRunnableRecipe(B.DefName, [&](const TMap<FName, double>& Inputs)
				{
					const double* Need = Inputs.Find(D.Type);
					if (!Need)
					{
						return false;
					}
					const double* Have = B.InputKg.Find(D.Type);
					return (!Have || *Have < *Need * 2.0);
				}))
			{
				DestId = B.Id;
				break;
			}
		}
		if (DestId == 0)
		{
			for (const FRHBuildingInstance& B : Buildings)
			{
				if (!B.bUnderConstruction && (B.DefName == NAME_Stockpile || B.DefName == NAME_Lander))
				{
					DestId = B.Id;
					break;
				}
			}
		}
		if (DestId != 0)
		{
			FRHSiteRef To; To.BuildingId = DestId;
			if (!HasOpenTask(ERHTaskType::Haul, From, To))
			{
				FRHTask T;
				T.Id = NextTaskId++;
				T.Type = ERHTaskType::Haul;
				T.From = From;
				T.To = To;
				T.Resource = D.Type;
				T.AmountKg = FMath::Min(D.PileKg, 200.0);
				Tasks.Add(T);
			}
		}
	}

	// Haul: production outputs -> demanding consumer (recipe wants it and the
	// hopper holds under two batches), else -> Stockpile/Lander store. This
	// routes drill ice to the Water Plant and Forge Struct to stores with
	// one rule.
	for (const FRHBuildingInstance& B : Buildings)
	{
		for (const auto& Out : B.OutputKg)
		{
			if (Out.Value < HaulLoadMinKg || B.DefName == NAME_Stockpile || B.DefName == NAME_Lander)
			{
				continue;
			}
			int32 DestId = 0;
			for (const FRHBuildingInstance& C : Buildings)
			{
				if (C.bUnderConstruction || C.Id == B.Id)
				{
					continue;
				}
				if (Defs->FindRunnableRecipe(C.DefName, [&](const TMap<FName, double>& Inputs)
					{
						const double* Need = Inputs.Find(Out.Key);
						if (!Need)
						{
							return false;
						}
						const double* Have = C.InputKg.Find(Out.Key);
						return (!Have || *Have < *Need * 2.0);
					}))
				{
					DestId = C.Id;
					break;
				}
			}
			if (DestId == 0)
			{
				for (const FRHBuildingInstance& S : Buildings)
				{
					if (!S.bUnderConstruction && (S.DefName == NAME_Stockpile || S.DefName == NAME_Lander) && S.Id != B.Id)
					{
						DestId = S.Id;
						break;
					}
				}
			}
			if (DestId != 0)
			{
				FRHSiteRef From; From.BuildingId = B.Id;
				FRHSiteRef To; To.BuildingId = DestId;
				if (!HasOpenTask(ERHTaskType::Haul, From, To))
				{
					FRHTask T;
					T.Id = NextTaskId++;
					T.Type = ERHTaskType::Haul;
					T.From = From;
					T.To = To;
					T.Resource = Out.Key;
					T.AmountKg = FMath::Min(Out.Value, 200.0);
					Tasks.Add(T);
				}
			}
		}
	}

	// Haul: construction sites want their bill of materials delivered before
	// the fabricator can work (site-delivery rule; multi-resource since M1-a).
	for (const FRHBuildingInstance& Site : Buildings)
	{
		if (!Site.bUnderConstruction)
		{
			continue;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(Site.DefName);
		if (!Def)
		{
			continue;
		}
		for (const auto& Cost : URHDefinitionsSubsystem::GetBuildCost(*Def))
		{
			const double* Delivered = Site.InputKg.Find(Cost.Key);
			const double NeedKg = Cost.Value - (Delivered ? *Delivered : 0.0);
			if (NeedKg <= 0.0)
			{
				continue;
			}
			// Source: any completed building holding that resource.
			int32 SourceId = 0;
			for (const FRHBuildingInstance& S : Buildings)
			{
				if (S.bUnderConstruction || S.Id == Site.Id)
				{
					continue;
				}
				const double* InS = S.InputKg.Find(Cost.Key);
				const double* OutS = S.OutputKg.Find(Cost.Key);
				if ((InS && *InS > 0.0) || (OutS && *OutS > 0.0))
				{
					SourceId = S.Id;
					break;
				}
			}
			if (SourceId != 0)
			{
				FRHSiteRef From; From.BuildingId = SourceId;
				FRHSiteRef To; To.BuildingId = Site.Id;
				if (!HasOpenTask(ERHTaskType::Haul, From, To, Cost.Key))
				{
					FRHTask T;
					T.Id = NextTaskId++;
					T.Type = ERHTaskType::Haul;
					T.From = From;
					T.To = To;
					T.Resource = Cost.Key;
					T.AmountKg = FMath::Min(NeedKg, 200.0);
					Tasks.Add(T);
				}
			}
		}
	}
}

void URHSimWorldSubsystem::StepProduction(float SubDt)
{
	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction)
		{
			continue;
		}
		if (B.BatchRemaining_h > 0.0)
		{
			if (B.bPowered) // shed buildings stall (priority shedding, M0-c)
			{
				B.BatchRemaining_h -= SubDt / 50.0;
				if (B.BatchRemaining_h <= 0.0)
				{
					B.BatchRemaining_h = 0.0;
					if (const TMap<FName, double>* Outputs = PendingOutputs.Find(B.Id))
					{
						for (const auto& Res : *Outputs)
						{
							// Hybrid rule: solids drop at the building for
							// hauling; fluids/gases join the network pool.
							if (Defs->IsSolidResource(Res.Key))
							{
								B.OutputKg.FindOrAdd(Res.Key) += Res.Value;
								OnStockChanged.Broadcast(Res.Key, GetTotalSolid(Res.Key));
							}
							else
							{
								AddStock(Res.Key, Res.Value);
							}
						}
						PendingOutputs.Remove(B.Id);
					}
					UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d batch complete (%s)"),
						*B.DefName.ToString(), B.Id, *B.ActiveRecipe.ToString());
					B.ActiveRecipe = NAME_None;
				}
			}
			continue;
		}

		if (!B.bPowered)
		{
			continue; // shed buildings do not start new batches either
		}

		// Extraction buildings (RequiresDeposit): the recipe has no hopper
		// inputs; the batch draws its output mass from the attached deposit.
		const FRHBuildingRow* BDef = Defs->GetBuilding(B.DefName);
		const bool bExtractor = BDef && BDef->RequiresDeposit;

		// Idle: try to start a batch whose inputs are covered - solids from
		// the hopper, fluids from the network pool, extraction from ground.
		if (const FRHRecipeRow* Recipe = Defs->FindRunnableRecipe(B.DefName,
			[&](const TMap<FName, double>& Inputs)
			{
				if (Inputs.Num() == 0)
				{
					if (!bExtractor || B.AttachedDepositId == 0)
					{
						return false;
					}
					const FRHDepositState* D = nullptr;
					for (const FRHDepositState& Dep : Deposits)
					{
						if (Dep.Id == B.AttachedDepositId) { D = &Dep; break; }
					}
					return D && D->RemainingKg > 0.0;
				}
				for (const auto& In : Inputs)
				{
					if (Defs->IsSolidResource(In.Key))
					{
						const double* Have = B.InputKg.Find(In.Key);
						if (!Have || *Have < In.Value)
						{
							return false;
						}
					}
					else if (GetStock(In.Key) < In.Value)
					{
						return false;
					}
				}
				return true;
			}))
		{
			const TMap<FName, double> Inputs = URHDefinitionsSubsystem::ParseResourceList(Recipe->Inputs);
			const TMap<FName, double> Outputs = URHDefinitionsSubsystem::ParseResourceList(Recipe->Outputs);

			if (Inputs.Num() == 0 && bExtractor)
			{
				// Draw the output mass from the ground now (committed batch).
				double OutMassKg = 0.0;
				for (const auto& O : Outputs)
				{
					OutMassKg += O.Value;
				}
				if (FRHDepositState* D = FindDeposit(B.AttachedDepositId))
				{
					if (D->RemainingKg < OutMassKg)
					{
						continue; // not enough left for a full batch
					}
					D->RemainingKg -= OutMassKg;
				}
			}
			for (const auto& In : Inputs)
			{
				if (Defs->IsSolidResource(In.Key))
				{
					B.InputKg.FindOrAdd(In.Key) -= In.Value;
				}
				else
				{
					AddStock(In.Key, -In.Value);
				}
			}
			B.ActiveRecipe = FName(*Recipe->Outputs); // display only; outputs cached:
			PendingOutputs.Add(B.Id, Outputs);
			B.BatchRemaining_h = Recipe->BatchTime_h;
			UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d batch started (%s -> %s)"),
				*B.DefName.ToString(), B.Id, *Recipe->Inputs, *Recipe->Outputs);
		}
	}
}

void URHSimWorldSubsystem::StepQuota()
{
	const FRHQuotaRow* Quota = Defs->GetQuota(NAME_Q1);
	if (!Quota)
	{
		return;
	}

	if (QuotaPhase == ERHQuotaPhase::Open)
	{
		for (const auto& Req : GetQuotaProgress())
		{
			if (Req.Value.Key < Req.Value.Value)
			{
				return;
			}
		}
		QuotaPhase = ERHQuotaPhase::AwaitingManifest;
		QuotaMetSol = Clock->GetSol();
		const bool bOnTime = QuotaMetSol <= Quota->DeadlineSol;
		AwardMassKg = bOnTime ? Quota->OnTimeAward_kg : Quota->LateAward_kg;
		UE_LOG(LogRedHopeSim, Display,
			TEXT("=== CEO TRANSMISSION (Sol %d): Quota Q1 met%s. Supply ship authorized: %.0f kg manifest budget. Compose and launch. ==="),
			QuotaMetSol, bOnTime ? TEXT(" ON TIME") : TEXT(" (late)"), AwardMassKg);
		OnQuotaMet.Broadcast(QuotaMetSol, AwardMassKg);
	}
	else if (QuotaPhase == ERHQuotaPhase::ShipInbound)
	{
		if (Clock->GetSimSecondsTotal() >= ShipArrivalSimSeconds)
		{
			QuotaPhase = ERHQuotaPhase::Completed;
			UE_LOG(LogRedHopeSim, Display, TEXT("=== SHIP LANDED (Sol %d). Cargo transfer: %d items, %.0f kg. ==="),
				Clock->GetSol(), ManifestItems.Num(), GetManifestMassKg());
			for (const FName& Item : ManifestItems)
			{
				ApplyManifestItemEffect(Item);
			}
			// Slice end card.
			UE_LOG(LogRedHopeSim, Display,
				TEXT("=== THE PROGRAM CONTINUES: sols elapsed %d | quota met sol %d (deadline %d) | fleet + colony operational ==="),
				Clock->GetSol(), QuotaMetSol, Quota->DeadlineSol);
			OnShipArrived.Broadcast(ManifestItems);
		}
	}
}

void URHSimWorldSubsystem::ApplyManifestItemEffect(FName ItemName)
{
	// Mechanical effects keyed by row name at slice scale (the CSV Effect
	// column stays display text; data-driven effect verbs are an M1 task).
	if (ItemName == FName("SolarPack"))       { ImportStock.FindOrAdd(FName("SolarArray"))++; }
	else if (ItemName == FName("BatteryPack")) { ImportStock.FindOrAdd(FName("BatteryBank"))++; }
	else if (ItemName == FName("PartsCrate"))  { AddStock(FName("SpareParts"), 10); }
	else if (ItemName == FName("Toolkit"))     { FabricatorSpeedMul += 0.15; }
	else if (ItemName == FName("ComputeCore"))
	{
		OrderLagSeconds = Defs->GetConfigScalar(FName("OrderLagTier2_s"), 8.0);
		UE_LOG(LogRedHopeSim, Display, TEXT("Compute core online: order lag now %.0f sim-s"), OrderLagSeconds);
	}
	else if (ItemName == FName("AdvExcavator") || ItemName == FName("ExtraHauler"))
	{
		const FName RobotRow = (ItemName == FName("AdvExcavator")) ? FName("RC_E2") : FName("RC_H");
		if (const FRHRobotRow* Row = Defs->GetRobot(RobotRow))
		{
			TArray<FMassEntityHandle> NewUnits;
			SpawnRobotTracked(RobotRow, *Row, FVector(3000.f, -3000.f, 50.f), Row->Battery_Wh, 0.f, NewUnits);
			if (NewUnits.Num() > 0)
			{
				OnRobotsSpawned.Broadcast(NewUnits);
			}
		}
	}
	else if (ItemName == FName("SoilPallet")) { AddStock(FName("Soil"), 1000); }
	else if (ItemName == FName("SeedVault"))  { AddStock(FName("Seeds"), 200); }
	else if (ItemName == FName("LuxuryGoods")) { AddStock(FName("Luxury"), 300); }
	UE_LOG(LogRedHopeSim, Display, TEXT("  cargo unloaded: %s"), *ItemName.ToString());
}

void URHSimWorldSubsystem::StepPower(float SubDt)
{
	const float Solar = Defs->EvalSolarCurve(Clock->GetSolFraction());

	// Pass 1: generation, storage, and each building's wanted draw.
	struct FLoadEntry { FRHBuildingInstance* B = nullptr; double WantW = 0.0; int32 Priority = 0; };
	TArray<FLoadEntry> Loads;
	double GenW = 0.0, CapWh = 0.0;
	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction)
		{
			B.bPowered = false;
			continue;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName);
		if (!Def)
		{
			continue;
		}
		GenW += Def->PowerGenBase_W + Def->PowerGenPeak_W * Solar;
		CapWh += Def->StorageWh;
		FLoadEntry E;
		E.B = &B;
		E.WantW = (B.BatchRemaining_h > 0.0) ? Def->PowerDraw_W : Def->PowerIdle_W;
		E.Priority = Def->LoadPriority;
		Loads.Add(E);
	}

	// Pass 2: shedding. While the bank holds charge every load is served;
	// once it empties, loads shed lowest LoadPriority first until demand
	// fits generation (spec: Forge sheds first, charge pads last).
	double LoadW = 0.0;
	for (const FLoadEntry& E : Loads)
	{
		E.B->bPowered = true;
		LoadW += E.WantW;
	}
	Power.ShedCount = 0;
	if (Power.BatteryWh <= 0.0 && LoadW > GenW)
	{
		Loads.Sort([](const FLoadEntry& A, const FLoadEntry& B) { return A.Priority < B.Priority; });
		for (FLoadEntry& E : Loads)
		{
			if (LoadW <= GenW)
			{
				break;
			}
			E.B->bPowered = false;
			LoadW -= E.WantW;
			++Power.ShedCount;
		}
	}

	Power.GenW = GenW;
	Power.LoadW = LoadW;
	Power.BatteryCapWh = CapWh;
	const double DeltaWh = (GenW - LoadW) * (SubDt / 50.0);
	Power.BatteryWh = FMath::Clamp(Power.BatteryWh + DeltaWh, 0.0, CapWh);
	Power.bDeficit = Power.ShedCount > 0;
}

void URHSimWorldSubsystem::EnqueueCommand(FRHCommand Command)
{
	Command.IssuedAtSimSeconds = Clock ? Clock->GetSimSecondsTotal() : 0.0;
	Command.ExecuteAtSimSeconds = Command.IssuedAtSimSeconds + OrderLagSeconds;
	UE_LOG(LogRedHopeSim, Display, TEXT("Uplink: '%s %s' transmitted, executes in %.0f sim-s"),
		*Command.Verb.ToString(), *Command.Target.ToString(), OrderLagSeconds);
	UplinkQueue.Add(MoveTemp(Command));
}

bool URHSimWorldSubsystem::CanPlaceBuilding(FName DefName, const FVector& LocationCm, FString& OutReason) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(DefName) : nullptr;
	if (!Def)
	{
		OutReason = FString::Printf(TEXT("Unknown building '%s'"), *DefName.ToString());
		return false;
	}
	// Pylon-like defs (they project coverage and link by range) are the
	// territory extenders: their rule is link range to the nearest node,
	// not the coverage union. Everything else must sit inside coverage.
	if (Def->CoverageRadius_m > 0.f && Def->LinkRange_m > 0.f)
	{
		double NearestNodeCm = TNumericLimits<double>::Max();
		for (const FRHBuildingInstance& B : Buildings)
		{
			const FRHBuildingRow* NodeDef = B.bUnderConstruction ? nullptr : Defs->GetBuilding(B.DefName);
			if (NodeDef && NodeDef->CoverageRadius_m > 0.f)
			{
				NearestNodeCm = FMath::Min(NearestNodeCm, (double)FVector::DistXY(B.LocationCm, LocationCm));
			}
		}
		if (NearestNodeCm > Def->LinkRange_m * 100.0)
		{
			OutReason = FString::Printf(TEXT("No grid node within %.0f m link range"), Def->LinkRange_m);
			return false;
		}
	}
	else if (!IsInCoverage(LocationCm))
	{
		OutReason = TEXT("Outside grid coverage - extend pylons first");
		return false;
	}
	if (Def->ImportOnly)
	{
		// Imported hardware: needs flat-pack stock, never Struct.
		const int32* Stock = ImportStock.Find(DefName);
		if (!Stock || *Stock <= 0)
		{
			OutReason = TEXT("No imported units in stock (manifest required)");
			return false;
		}
	}
	else
	{
		// Materials are delivered to the site by haulers (task board);
		// this only rejects orders the colony cannot possibly fill.
		for (const auto& Cost : URHDefinitionsSubsystem::GetBuildCost(*Def))
		{
			const double Available = GetTotalSolid(Cost.Key) + GetStock(Cost.Key);
			if (Available < Cost.Value)
			{
				OutReason = FString::Printf(TEXT("Insufficient %s (%.0f needed, %.0f on hand)"),
					*Cost.Key.ToString(), Cost.Value, Available);
				return false;
			}
		}
	}
	return true;
}

const FRHDepositState* URHSimWorldSubsystem::FindDepositNear(const FVector& LocationCm, double MaxDistCm) const
{
	const FRHDepositState* Best = nullptr;
	double BestDist = MaxDistCm;
	for (const FRHDepositState& D : Deposits)
	{
		const double Dist = FVector::DistXY(D.LocationCm, LocationCm);
		if (Dist <= BestDist)
		{
			BestDist = Dist;
			Best = &D;
		}
	}
	return Best;
}

void URHSimWorldSubsystem::ExecuteCommand(const FRHCommand& Cmd)
{
	if (Cmd.Verb == FName("Build"))
	{
		FString Reason;
		if (!CanPlaceBuilding(Cmd.Target, Cmd.Location, Reason))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(Cmd.Target);
		if (Def->ImportOnly)
		{
			--ImportStock.FindOrAdd(Cmd.Target); // consumption happens at execution, not preview
		}
		AddBuilding(Cmd.Target, Cmd.Location, /*bInstant*/ Def->BuildTime_s <= 0.0);
	}
	else if (Cmd.Verb == FName("Dig"))
	{
		bool bFound = false;
		for (FRHDepositState& D : Deposits)
		{
			if (D.RowName == Cmd.Target)
			{
				D.bDesignated = true;
				bFound = true;
				UE_LOG(LogRedHopeSim, Display, TEXT("Dig designation: %s (%s, %.0f t remaining)"),
					*D.RowName.ToString(), *D.Type.ToString(), D.RemainingKg / 1000.0);
			}
		}
		if (!bFound)
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Unknown deposit '%s'"), *Cmd.Target.ToString()));
			return;
		}
	}

	OnCommandExecuted.Broadcast(Cmd);
}

void URHSimWorldSubsystem::AddBuilding(FName DefName, const FVector& LocationCm, bool bInstant)
{
	FRHBuildingInstance Instance;
	Instance.Id = NextBuildingId++;
	Instance.DefName = DefName;
	Instance.LocationCm = LocationCm;

	const FRHBuildingRow* Def = Defs->GetBuilding(DefName);
	if (!bInstant && Def && Def->BuildTime_s > 0.0)
	{
		Instance.bUnderConstruction = true;
		Instance.BuildRemaining_s = Def->BuildTime_s;
	}
	if (Def && !Instance.bUnderConstruction)
	{
		// Packs arrive half-charged. Constructed storage gets its credit at
		// completion instead (M0-c bug: crediting at order time clamped the
		// energy away because capacity only counts completed banks).
		Power.BatteryWh += Def->StorageWh * 0.5;
	}
	// Extraction buildings bind to the deposit under them (within 12 m).
	if (Def && Def->RequiresDeposit)
	{
		for (const FRHDepositState& D : Deposits)
		{
			if (FVector::DistXY(D.LocationCm, LocationCm) <= 1200.0)
			{
				Instance.AttachedDepositId = D.Id;
				break;
			}
		}
		if (Instance.AttachedDepositId == 0)
		{
			UE_LOG(LogRedHopeSim, Warning, TEXT("%s placed with no deposit within 12 m - it will never produce"), *DefName.ToString());
		}
	}

	const int32 NewId = Instance.Id;
	Buildings.Add(MoveTemp(Instance));
	const FRHBuildingInstance& Added = Buildings.Last();

	if (Added.bUnderConstruction)
	{
		FRHTask T;
		T.Id = NextTaskId++;
		T.Type = ERHTaskType::Build;
		T.To.BuildingId = NewId;
		Tasks.Add(T);
		UE_LOG(LogRedHopeSim, Display, TEXT("Construction site: %s #%d at (%.0f, %.0f) m (%.0f s of fabrication)"),
			*DefName.ToString(), NewId, LocationCm.X / 100.0, LocationCm.Y / 100.0, Added.BuildRemaining_s);
	}
	else
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("Built %s #%d at (%.0f, %.0f) m"),
			*DefName.ToString(), NewId, LocationCm.X / 100.0, LocationCm.Y / 100.0);
	}
	OnBuildingAdded.Broadcast(Added);
}

bool URHSimWorldSubsystem::IsInCoverage(const FVector& LocationCm) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction)
		{
			continue;
		}
		if (const FRHBuildingRow* Def = Defs->GetBuilding(B.DefName))
		{
			const double RadiusCm = Def->CoverageRadius_m * 100.0;
			if (RadiusCm > 0.0 && FVector::DistXY(B.LocationCm, LocationCm) <= RadiusCm)
			{
				return true;
			}
		}
	}
	return false;
}

double URHSimWorldSubsystem::GetStock(FName Resource) const
{
	const double* Found = Stocks.Find(Resource);
	return Found ? *Found : 0.0;
}

void URHSimWorldSubsystem::AddStock(FName Resource, double Delta)
{
	double& Amount = Stocks.FindOrAdd(Resource);
	Amount += Delta;
	OnStockChanged.Broadcast(Resource, Amount);
}

double URHSimWorldSubsystem::GetTotalSolid(FName Resource) const
{
	double Total = 0.0;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (const double* In = B.InputKg.Find(Resource))
		{
			Total += *In;
		}
		if (const double* Out = B.OutputKg.Find(Resource))
		{
			Total += *Out;
		}
	}
	return Total;
}

TMap<FName, TPair<double, double>> URHSimWorldSubsystem::GetQuotaProgress() const
{
	TMap<FName, TPair<double, double>> Progress;
	if (const FRHQuotaRow* Quota = Defs ? Defs->GetQuota(NAME_Q1) : nullptr)
	{
		for (const auto& Req : URHDefinitionsSubsystem::ParseResourceList(Quota->Requirements))
		{
			double Have = GetTotalSolid(Req.Key);
			if (const double* Fluid = Stocks.Find(Req.Key))
			{
				Have += *Fluid;
			}
			Progress.Add(Req.Key, TPair<double, double>(Have, Req.Value));
		}
	}
	return Progress;
}

// --- Robot work API ---

FVector URHSimWorldSubsystem::GetSiteLocation(const FRHSiteRef& Site) const
{
	if (Site.BuildingId > 0)
	{
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.Id == Site.BuildingId)
			{
				return B.LocationCm;
			}
		}
	}
	if (Site.DepositId > 0)
	{
		for (const FRHDepositState& D : Deposits)
		{
			if (D.Id == Site.DepositId)
			{
				return D.LocationCm;
			}
		}
	}
	return FVector::ZeroVector;
}

double URHSimWorldSubsystem::DigDeposit(int32 DepositId, double Kg)
{
	FRHDepositState* D = FindDeposit(DepositId);
	if (!D)
	{
		return 0.0;
	}
	const double Dug = FMath::Min3(Kg, D->RemainingKg, FMath::Max(0.0, PileCapKg - D->PileKg));
	D->RemainingKg -= Dug;
	D->PileKg += Dug;
	return Dug;
}

bool URHSimWorldSubsystem::IsDepositWorkable(int32 DepositId) const
{
	for (const FRHDepositState& D : Deposits)
	{
		if (D.Id == DepositId)
		{
			return D.bDesignated && D.RemainingKg > 0.0 && D.PileKg < PileCapKg;
		}
	}
	return false;
}

void URHSimWorldSubsystem::ReleaseDigClaim(int32 DepositId)
{
	if (FRHDepositState* D = FindDeposit(DepositId))
	{
		D->DigClaims = FMath::Max(0, D->DigClaims - 1);
	}
}

int32 URHSimWorldSubsystem::TryClaimDig(const FVector& RobotPosCm)
{
	int32 BestId = 0;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHDepositState& D : Deposits)
	{
		if (D.bDesignated && D.RemainingKg > 0.0 && D.DigClaims < 2)
		{
			const double Dist = FVector::DistXY(D.LocationCm, RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				BestId = D.Id;
			}
		}
	}
	if (BestId != 0)
	{
		FindDeposit(BestId)->DigClaims++;
	}
	return BestId;
}

bool URHSimWorldSubsystem::TryClaimHaul(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask)
{
	FRHTask* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Haul && !T.ClaimedBy.IsValid())
		{
			const double Dist = FVector::DistXY(GetSiteLocation(T.From), RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = &T;
			}
		}
	}
	if (Best)
	{
		Best->ClaimedBy = Robot;
		OutTask = *Best;
		return true;
	}
	return false;
}

bool URHSimWorldSubsystem::HaulLoad(int32 TaskId, float CargoCapKg, float& OutLoadedKg, FVector& OutDropoffCm)
{
	FRHTask* T = FindTask(TaskId);
	if (!T)
	{
		return false;
	}
	const double WantKg = FMath::Min((double)CargoCapKg, T->AmountKg);
	double Taken = 0.0;

	if (T->From.DepositId > 0)
	{
		if (FRHDepositState* D = FindDeposit(T->From.DepositId))
		{
			Taken = FMath::Min(WantKg, D->PileKg);
			D->PileKg -= Taken;
		}
	}
	else if (FRHBuildingInstance* B = FindBuilding(T->From.BuildingId))
	{
		// Outputs first, then held inputs (stores like the Lander keep their
		// starter Struct in InputKg).
		if (double* Out = B->OutputKg.Find(T->Resource))
		{
			Taken = FMath::Min(WantKg, *Out);
			*Out -= Taken;
		}
		if (Taken < WantKg)
		{
			if (double* In = B->InputKg.Find(T->Resource))
			{
				const double More = FMath::Min(WantKg - Taken, *In);
				*In -= More;
				Taken += More;
			}
		}
	}

	if (Taken <= 0.0)
	{
		CompleteTask(TaskId); // stale task: source emptied since posting
		return false;
	}
	OutLoadedKg = (float)Taken;
	OutDropoffCm = GetSiteLocation(T->To);
	return true;
}

void URHSimWorldSubsystem::HaulUnload(int32 TaskId, float CargoKg)
{
	if (FRHTask* T = FindTask(TaskId))
	{
		if (FRHBuildingInstance* B = FindBuilding(T->To.BuildingId))
		{
			B->InputKg.FindOrAdd(T->Resource) += CargoKg;
			OnStockChanged.Broadcast(T->Resource, GetTotalSolid(T->Resource));
		}
		CompleteTask(TaskId);
	}
}

bool URHSimWorldSubsystem::IsDepositSpent(int32 DepositId) const
{
	for (const FRHDepositState& D : Deposits)
	{
		if (D.Id == DepositId)
		{
			return !D.bDesignated || D.RemainingKg <= 0.0;
		}
	}
	return true;
}

bool URHSimWorldSubsystem::TryClaimBuild(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask)
{
	FRHTask* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Build && !T.ClaimedBy.IsValid())
		{
			const double Dist = FVector::DistXY(GetSiteLocation(T.To), RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Best = &T;
			}
		}
	}
	if (Best)
	{
		Best->ClaimedBy = Robot;
		OutTask = *Best;
		return true;
	}
	return false;
}

bool URHSimWorldSubsystem::ApplyBuildWork(int32 BuildingId, double Seconds)
{
	FRHBuildingInstance* B = FindBuilding(BuildingId);
	if (!B || !B->bUnderConstruction)
	{
		return true;
	}
	// Fabrication waits for the full bill of materials on site (haulers are
	// bringing it; multi-resource since M1-a).
	const FRHBuildingRow* Def = Defs->GetBuilding(B->DefName);
	const TMap<FName, double> Cost = Def ? URHDefinitionsSubsystem::GetBuildCost(*Def) : TMap<FName, double>();
	for (const auto& Line : Cost)
	{
		const double* Delivered = B->InputKg.Find(Line.Key);
		if (!Delivered || *Delivered + 0.5 < Line.Value)
		{
			return false; // standing by for materials; no work progress
		}
	}
	B->BuildRemaining_s -= Seconds * FabricatorSpeedMul;
	if (B->BuildRemaining_s <= 0.0)
	{
		B->bUnderConstruction = false;
		B->BuildRemaining_s = 0.0;
		// The delivered materials become the structure.
		for (const auto& Line : Cost)
		{
			double& Held = B->InputKg.FindOrAdd(Line.Key);
			Held = FMath::Max(0.0, Held - Line.Value);
			OnStockChanged.Broadcast(Line.Key, GetTotalSolid(Line.Key));
		}
		if (Def)
		{
			Power.BatteryWh += Def->StorageWh * 0.5; // constructed storage arrives half-charged
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d construction complete"), *B->DefName.ToString(), B->Id);
		OnBuildingCompleted.Broadcast(*B);
		return true;
	}
	return false;
}

bool URHSimWorldSubsystem::FindNearestChargePad(const FVector& RobotPosCm, int32& OutBuildingId, FVector& OutLocationCm) const
{
	const FName NAME_ChargePad(TEXT("ChargePad"));
	double BestDist = TNumericLimits<double>::Max();
	OutBuildingId = 0;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (!B.bUnderConstruction && B.DefName == NAME_ChargePad)
		{
			const double Dist = FVector::DistXY(B.LocationCm, RobotPosCm);
			if (Dist < BestDist)
			{
				BestDist = Dist;
				OutBuildingId = B.Id;
				OutLocationCm = B.LocationCm;
			}
		}
	}
	return OutBuildingId != 0;
}

double URHSimWorldSubsystem::RequestChargeWh(int32 PadBuildingId, double SubDt)
{
	FRHBuildingInstance* Pad = FindBuilding(PadBuildingId);
	if (!Pad || Pad->bUnderConstruction || !Pad->bPowered)
	{
		return 0.0;
	}
	const FRHBuildingRow* Def = Defs->GetBuilding(Pad->DefName);
	const double RateW = Def ? Def->PowerDraw_W : 500.0;
	const double WantWh = RateW * (SubDt / 50.0);
	// Grid energy: from the bank if it holds charge, else from live surplus.
	if (Power.BatteryWh >= WantWh)
	{
		Power.BatteryWh -= WantWh;
		return WantWh;
	}
	if (Power.GenW > Power.LoadW)
	{
		return WantWh; // absorbed by generation surplus (slice approximation)
	}
	return 0.0; // night + empty bank: robot waits docked
}

double URHSimWorldSubsystem::GetManifestMassKg() const
{
	double Total = 0.0;
	for (const FName& Item : ManifestItems)
	{
		if (const FRHManifestItemRow* Row = Defs->GetManifestItem(Item))
		{
			Total += Row->Mass_kg;
		}
	}
	return Total;
}

int32 URHSimWorldSubsystem::GetImportStock(FName DefName) const
{
	const int32* Found = ImportStock.Find(DefName);
	return Found ? *Found : 0;
}

bool URHSimWorldSubsystem::AddManifestItem(FName ItemName, FString& OutError)
{
	if (QuotaPhase != ERHQuotaPhase::AwaitingManifest)
	{
		OutError = TEXT("No manifest is open (quota not met, or ship already launched)");
		return false;
	}
	const FRHManifestItemRow* Row = Defs->GetManifestItem(ItemName);
	if (!Row || !Row->SliceActive)
	{
		OutError = FString::Printf(TEXT("Unknown manifest item '%s'"), *ItemName.ToString());
		return false;
	}
	if (GetManifestMassKg() + Row->Mass_kg > AwardMassKg)
	{
		OutError = FString::Printf(TEXT("Over budget: %.0f + %.0f > %.0f kg"),
			GetManifestMassKg(), Row->Mass_kg, AwardMassKg);
		return false;
	}
	ManifestItems.Add(ItemName);
	UE_LOG(LogRedHopeSim, Display, TEXT("Manifest: +%s (%.0f kg) -> %.0f / %.0f kg"),
		*ItemName.ToString(), Row->Mass_kg, GetManifestMassKg(), AwardMassKg);
	return true;
}

bool URHSimWorldSubsystem::LaunchShip(FString& OutError)
{
	if (QuotaPhase != ERHQuotaPhase::AwaitingManifest)
	{
		OutError = TEXT("No manifest is open");
		return false;
	}
	const double TransitSols = Defs->GetConfigScalar(FName("ShipTransitSols"), 3.0);
	ShipArrivalSimSeconds = Clock->GetSimSecondsTotal() + TransitSols * URHSimClockSubsystem::SolLengthSimSeconds;
	QuotaPhase = ERHQuotaPhase::ShipInbound;
	UE_LOG(LogRedHopeSim, Display, TEXT("Ship launched: %d items, %.0f/%.0f kg, arrival in %.0f sols"),
		ManifestItems.Num(), GetManifestMassKg(), AwardMassKg, TransitSols);
	return true;
}

void URHSimWorldSubsystem::CompleteTask(int32 TaskId)
{
	Tasks.RemoveAll([TaskId](const FRHTask& T) { return T.Id == TaskId; });
}

void URHSimWorldSubsystem::AbandonTask(int32 TaskId)
{
	if (FRHTask* T = FindTask(TaskId))
	{
		T->ClaimedBy = FMassEntityHandle();
	}
}

// --- helpers ---

FRHBuildingInstance* URHSimWorldSubsystem::FindBuilding(int32 Id)
{
	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.Id == Id)
		{
			return &B;
		}
	}
	return nullptr;
}

FRHDepositState* URHSimWorldSubsystem::FindDeposit(int32 Id)
{
	for (FRHDepositState& D : Deposits)
	{
		if (D.Id == Id)
		{
			return &D;
		}
	}
	return nullptr;
}

FRHTask* URHSimWorldSubsystem::FindTask(int32 Id)
{
	for (FRHTask& T : Tasks)
	{
		if (T.Id == Id)
		{
			return &T;
		}
	}
	return nullptr;
}

bool URHSimWorldSubsystem::HasOpenTask(ERHTaskType Type, const FRHSiteRef& From, const FRHSiteRef& To, FName Resource) const
{
	for (const FRHTask& T : Tasks)
	{
		if (T.Type == Type && T.From.BuildingId == From.BuildingId && T.From.DepositId == From.DepositId
			&& T.To.BuildingId == To.BuildingId && T.To.DepositId == To.DepositId
			&& (Resource.IsNone() || T.Resource == Resource))
		{
			return true;
		}
	}
	return false;
}

// --- Save / load ---

bool URHSimWorldSubsystem::SaveColony(const FString& Slot, FString& OutError)
{
	if (!Clock || !Defs)
	{
		OutError = TEXT("Sim not initialized");
		return false;
	}

	// Snapshot copies: claims cleared, in-flight hauler cargo returned to its
	// source so mass is conserved without serializing robot intent.
	TArray<FRHBuildingInstance> SavedBuildings = Buildings;
	TArray<FRHDepositState> SavedDeposits = Deposits;
	for (FRHDepositState& D : SavedDeposits)
	{
		D.DigClaims = 0;
	}
	TArray<FRHTask> SavedTasks;
	for (const FRHTask& T : Tasks)
	{
		FRHTask Copy = T;
		Copy.ClaimedBy = FMassEntityHandle();
		SavedTasks.Add(Copy);
	}

	TArray<FRHRobotSaveState> Robots;
	if (URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>())
	{
		Agents->CollectRobotStates(Robots);
	}
	for (FRHRobotSaveState& R : Robots)
	{
		if (R.CargoKg <= 0.f || R.TaskId == 0)
		{
			continue;
		}
		for (const FRHTask& T : SavedTasks)
		{
			if (T.Id != R.TaskId)
			{
				continue;
			}
			if (T.From.DepositId > 0)
			{
				for (FRHDepositState& D : SavedDeposits)
				{
					if (D.Id == T.From.DepositId)
					{
						D.PileKg += R.CargoKg;
						break;
					}
				}
			}
			else if (T.From.BuildingId > 0)
			{
				for (FRHBuildingInstance& B : SavedBuildings)
				{
					if (B.Id == T.From.BuildingId)
					{
						B.OutputKg.FindOrAdd(R.CargoResource) += R.CargoKg;
						break;
					}
				}
			}
			break;
		}
		R.CargoKg = 0.f;
		R.TaskId = 0;
	}

	TArray<uint8> Bytes;
	FMemoryWriter Raw(Bytes);
	FNameAsStringProxyArchive Ar(Raw);

	uint32 Magic = RHSaveMagic, Version = RHSaveVersion;
	double SimSeconds = Clock->GetSimSecondsTotal();
	float Speed = Clock->GetSpeed();
	Ar << Magic << Version << SimSeconds << Speed;

	Ar << OrderLagSeconds << FabricatorSpeedMul << NextBuildingId << NextTaskId;
	uint8 Phase = (uint8)QuotaPhase;
	Ar << Phase << AwardMassKg << QuotaMetSol << ShipArrivalSimSeconds << ManifestItems;
	Ar << Power.BatteryWh;
	SerializeResourceMap(Ar, Stocks);
	Ar << ImportStock;

	int32 Num = UplinkQueue.Num();
	Ar << Num;
	for (FRHCommand& C : UplinkQueue)
	{
		Ar << C.Verb << C.Target << C.Location << C.Value << C.IssuedAtSimSeconds << C.ExecuteAtSimSeconds;
	}

	Num = SavedBuildings.Num();
	Ar << Num;
	for (FRHBuildingInstance& B : SavedBuildings)
	{
		Ar << B.Id << B.DefName << B.LocationCm << B.bUnderConstruction << B.bPowered
		   << B.BuildRemaining_s << B.BatchRemaining_h << B.ActiveRecipe << B.AttachedDepositId;
		SerializeResourceMap(Ar, B.InputKg);
		SerializeResourceMap(Ar, B.OutputKg);
	}

	Num = PendingOutputs.Num();
	Ar << Num;
	for (auto& P : PendingOutputs)
	{
		int32 BuildingId = P.Key;
		Ar << BuildingId;
		SerializeResourceMap(Ar, P.Value);
	}

	Num = SavedDeposits.Num();
	Ar << Num;
	for (FRHDepositState& D : SavedDeposits)
	{
		Ar << D.Id << D.RowName << D.Type << D.RemainingKg << D.PileKg << D.LocationCm << D.bDesignated;
	}

	Num = SavedTasks.Num();
	Ar << Num;
	for (FRHTask& T : SavedTasks)
	{
		uint8 Type = (uint8)T.Type;
		Ar << T.Id << Type;
		SerializeSite(Ar, T.From);
		SerializeSite(Ar, T.To);
		Ar << T.Resource << T.AmountKg;
	}

	Num = Robots.Num();
	Ar << Num;
	for (FRHRobotSaveState& R : Robots)
	{
		Ar << R.DefName << R.PosCm << R.ChargeWh << R.Wear;
	}

	const FString Path = SaveSlotToPath(Slot);
	if (!FFileHelper::SaveArrayToFile(Bytes, *Path))
	{
		OutError = FString::Printf(TEXT("Could not write %s"), *Path);
		return false;
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Colony saved: slot '%s' (%d bytes, sol %d, %d buildings, %d robots)"),
		*Slot, Bytes.Num(), Clock->GetSol(), SavedBuildings.Num(), Robots.Num());
	return true;
}

bool URHSimWorldSubsystem::LoadColony(const FString& Slot, FString& OutError)
{
	if (!Clock || !Defs)
	{
		OutError = TEXT("Sim not initialized");
		return false;
	}
	const FString Path = SaveSlotToPath(Slot);
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		OutError = FString::Printf(TEXT("No save at %s"), *Path);
		return false;
	}

	FMemoryReader Raw(Bytes);
	FNameAsStringProxyArchive Ar(Raw);

	uint32 Magic = 0, Version = 0;
	double SimSeconds = 0.0;
	float Speed = 1.f;
	Ar << Magic << Version << SimSeconds << Speed;
	if (Magic != RHSaveMagic || Version != RHSaveVersion)
	{
		OutError = FString::Printf(TEXT("Save format mismatch (magic %08x version %u; expected %08x/%u)"),
			Magic, Version, RHSaveMagic, RHSaveVersion);
		return false;
	}

	// Tear down the live colony before applying the snapshot.
	if (URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>())
	{
		Agents->DespawnAllRobots();
	}
	Buildings.Reset();
	Deposits.Reset();
	Tasks.Reset();
	UplinkQueue.Reset();
	Stocks.Reset();
	ImportStock.Reset();
	PendingOutputs.Reset();
	ManifestItems.Reset();
	FleetCounts.Reset();
	bFleetDeployed = true; // robots respawn explicitly below

	Ar << OrderLagSeconds << FabricatorSpeedMul << NextBuildingId << NextTaskId;
	uint8 Phase = 0;
	Ar << Phase << AwardMassKg << QuotaMetSol << ShipArrivalSimSeconds << ManifestItems;
	QuotaPhase = (ERHQuotaPhase)Phase;
	Ar << Power.BatteryWh;
	SerializeResourceMap(Ar, Stocks);
	Ar << ImportStock;

	int32 Num = 0;
	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHCommand C;
		Ar << C.Verb << C.Target << C.Location << C.Value << C.IssuedAtSimSeconds << C.ExecuteAtSimSeconds;
		UplinkQueue.Add(MoveTemp(C));
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHBuildingInstance B;
		Ar << B.Id << B.DefName << B.LocationCm << B.bUnderConstruction << B.bPowered
		   << B.BuildRemaining_s << B.BatchRemaining_h << B.ActiveRecipe << B.AttachedDepositId;
		SerializeResourceMap(Ar, B.InputKg);
		SerializeResourceMap(Ar, B.OutputKg);
		Buildings.Add(MoveTemp(B));
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		int32 BuildingId = 0;
		TMap<FName, double> Outputs;
		Ar << BuildingId;
		SerializeResourceMap(Ar, Outputs);
		PendingOutputs.Add(BuildingId, MoveTemp(Outputs));
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHDepositState D;
		Ar << D.Id << D.RowName << D.Type << D.RemainingKg << D.PileKg << D.LocationCm << D.bDesignated;
		Deposits.Add(MoveTemp(D));
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHTask T;
		uint8 Type = 0;
		Ar << T.Id << Type;
		SerializeSite(Ar, T.From);
		SerializeSite(Ar, T.To);
		Ar << T.Resource << T.AmountKg;
		T.Type = (ERHTaskType)Type;
		Tasks.Add(MoveTemp(T));
	}

	Ar << Num;
	TArray<FMassEntityHandle> Respawned;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHRobotSaveState R;
		Ar << R.DefName << R.PosCm << R.ChargeWh << R.Wear;
		if (const FRHRobotRow* Row = Defs->GetRobot(R.DefName))
		{
			SpawnRobotTracked(R.DefName, *Row, R.PosCm, R.ChargeWh, R.Wear, Respawned);
		}
	}

	// Loads land at 1x regardless of the saved speed: give the player a calm
	// frame before they choose a tier again.
	Clock->Debug_SetSimSeconds(SimSeconds);
	Clock->SetSpeed(1.f);
	LastAutosaveSol = Clock->GetSol();

	OnColonyReloaded.Broadcast();
	if (Respawned.Num() > 0)
	{
		OnRobotsSpawned.Broadcast(Respawned);
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Colony loaded: slot '%s' (sol %d, %d buildings, %d robots, %d tasks)"),
		*Slot, Clock->GetSol(), Buildings.Num(), Respawned.Num(), Tasks.Num());
	return true;
}

