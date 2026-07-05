#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHAgentSubsystem.h"

namespace
{
	const FName NAME_Struct(TEXT("Struct"));
	const FName NAME_Lander(TEXT("Lander"));
	const FName NAME_Stockpile(TEXT("Stockpile"));
	const FName NAME_Q1(TEXT("Q1"));
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
	URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();
	if (!Agents)
	{
		return;
	}
	TArray<FMassEntityHandle> All;
	int32 Slot = 0;
	Defs->ForEachRobot([&](FName RowName, const FRHRobotRow& Row)
	{
		for (int32 i = 0; i < Row.StartingCount; ++i)
		{
			// Deploy ring south of the lander.
			const float Angle = Slot * 0.8f;
			const FVector Pos(-800.f + 250.f * Slot, -600.f + 150.f * FMath::Sin(Angle), 50.f);
			const FMassEntityHandle H = Agents->SpawnRobot(RowName, Row, Pos);
			if (H.IsValid())
			{
				All.Add(H);
			}
			++Slot;
		}
	});
	UE_LOG(LogRedHopeSim, Display, TEXT("Starting fleet deployed: %d robots"), All.Num());
	OnRobotsSpawned.Broadcast(All);
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
}

void URHSimWorldSubsystem::StepSim(float SubDt)
{
	if (!bFleetDeployed)
	{
		bFleetDeployed = true;
		SpawnStartingFleet();
	}

	// Fixed order: orders land, work is posted, factories run, the quota
	// arc advances, power settles.
	StepUplink();
	StepTaskBoard();
	StepProduction(SubDt);
	StepQuota();
	StepPower(SubDt);
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

	// Haul: construction sites want their Struct delivered before the
	// fabricator can work (site-delivery rule, replaces M0-b's colony-wide
	// instant deduction).
	for (const FRHBuildingInstance& Site : Buildings)
	{
		if (!Site.bUnderConstruction)
		{
			continue;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(Site.DefName);
		if (!Def || Def->CostStruct_kg <= 0.0)
		{
			continue;
		}
		const double* Delivered = Site.InputKg.Find(NAME_Struct);
		const double NeedKg = Def->CostStruct_kg - (Delivered ? *Delivered : 0.0);
		if (NeedKg <= 0.0)
		{
			continue;
		}
		// Source: any completed building holding Struct.
		int32 SourceId = 0;
		for (const FRHBuildingInstance& S : Buildings)
		{
			if (S.bUnderConstruction || S.Id == Site.Id)
			{
				continue;
			}
			const double* InS = S.InputKg.Find(NAME_Struct);
			const double* OutS = S.OutputKg.Find(NAME_Struct);
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
			if (!HasOpenTask(ERHTaskType::Haul, From, To))
			{
				FRHTask T;
				T.Id = NextTaskId++;
				T.Type = ERHTaskType::Haul;
				T.From = From;
				T.To = To;
				T.Resource = NAME_Struct;
				T.AmountKg = FMath::Min(NeedKg, 200.0);
				Tasks.Add(T);
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
	URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();

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
			if (Agents)
			{
				TArray<FMassEntityHandle> NewUnits;
				const FMassEntityHandle H = Agents->SpawnRobot(RobotRow, *Row, FVector(3000.f, -3000.f, 50.f));
				if (H.IsValid())
				{
					NewUnits.Add(H);
					OnRobotsSpawned.Broadcast(NewUnits);
				}
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
		GenW += Def->PowerGenPeak_W * Solar;
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

void URHSimWorldSubsystem::ExecuteCommand(const FRHCommand& Cmd)
{
	if (Cmd.Verb == FName("Build"))
	{
		const FRHBuildingRow* Def = Defs->GetBuilding(Cmd.Target);
		if (!Def)
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Unknown building '%s'"), *Cmd.Target.ToString()));
			return;
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
					NearestNodeCm = FMath::Min(NearestNodeCm, (double)FVector::DistXY(B.LocationCm, Cmd.Location));
				}
			}
			if (NearestNodeCm > Def->LinkRange_m * 100.0)
			{
				OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("No grid node within %.0f m link range"), Def->LinkRange_m));
				return;
			}
		}
		else if (!IsInCoverage(Cmd.Location))
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("Outside grid coverage - extend pylons first"));
			return;
		}
		if (Def->ImportOnly)
		{
			// Imported hardware: consumes flat-pack stock, never Struct.
			int32& Stock = ImportStock.FindOrAdd(Cmd.Target);
			if (Stock <= 0)
			{
				OnCommandRejected.Broadcast(Cmd, TEXT("No imported units in stock (manifest required)"));
				return;
			}
			--Stock;
		}
		else if (Def->CostStruct_kg > 0.0)
		{
			// Materials are delivered to the site by haulers (task board);
			// this only rejects orders the colony cannot possibly fill.
			if (GetTotalSolid(NAME_Struct) < Def->CostStruct_kg)
			{
				OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Insufficient Struct (%.0f needed)"), Def->CostStruct_kg));
				return;
			}
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
	if (Def)
	{
		Power.BatteryWh += Def->StorageWh * 0.5; // packs arrive half-charged
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
	// Fabrication waits for materials on site (haulers are bringing them).
	const FRHBuildingRow* Def = Defs->GetBuilding(B->DefName);
	if (Def && Def->CostStruct_kg > 0.0)
	{
		const double* Delivered = B->InputKg.Find(NAME_Struct);
		if (!Delivered || *Delivered + 0.5 < Def->CostStruct_kg)
		{
			return false; // standing by for materials; no work progress
		}
	}
	B->BuildRemaining_s -= Seconds * FabricatorSpeedMul;
	if (B->BuildRemaining_s <= 0.0)
	{
		B->bUnderConstruction = false;
		B->BuildRemaining_s = 0.0;
		// The delivered Struct becomes the structure.
		if (Def && Def->CostStruct_kg > 0.0)
		{
			double& Held = B->InputKg.FindOrAdd(NAME_Struct);
			Held = FMath::Max(0.0, Held - Def->CostStruct_kg);
			OnStockChanged.Broadcast(NAME_Struct, GetTotalSolid(NAME_Struct));
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

bool URHSimWorldSubsystem::HasOpenTask(ERHTaskType Type, const FRHSiteRef& From, const FRHSiteRef& To) const
{
	for (const FRHTask& T : Tasks)
	{
		if (T.Type == Type && T.From.BuildingId == From.BuildingId && T.From.DepositId == From.DepositId
			&& T.To.BuildingId == To.BuildingId && T.To.DepositId == To.DepositId)
		{
			return true;
		}
	}
	return false;
}

