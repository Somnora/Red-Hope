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
	// v2 (M1-b Gate A): Level on commands, buildings, deposits, robots.
	// v3 (M1-b Gate B): task TargetCm (survey), deposit bDiscovered.
	// v4 (M1-b close): survey history (the player's map survives loads).
	constexpr uint32 RHSaveMagic = 0x52485331;   // 'RHS1'
	constexpr uint32 RHSaveVersion = 7;   // v7: habitability (floor O2 fill + ratings, Gate B); v6 designations/H2; v5 shaft

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
	FloorHeightCm = Defs->GetConfigScalar(FName("FloorHeightMeters"), 4.0) * 100.0;
	MaxDepth = (int32)Defs->GetConfigScalar(FName("MaxDepth"), 5.0);
	WearDegradeThreshold = (float)Defs->GetConfigScalar(FName("WearDegradeThreshold"), WearDegradeThreshold);
	WearHaltThreshold = (float)Defs->GetConfigScalar(FName("WearHaltThreshold"), WearHaltThreshold);
	RepairWearPerPart = (float)Defs->GetConfigScalar(FName("RepairWearPerPart"), RepairWearPerPart);
	StormWearMul = (float)Defs->GetConfigScalar(FName("StormWearMul"), StormWearMul);
	RadiationSurface = (float)Defs->GetConfigScalar(FName("RadiationSurface"), RadiationSurface);
	RadiationPerLevelMul = (float)Defs->GetConfigScalar(FName("RadiationPerLevelMul"), RadiationPerLevelMul);
	ShaftSpoilKgPerFloor = Defs->GetConfigScalar(FName("ShaftSpoilKgPerFloor"), ShaftSpoilKgPerFloor);
	SpoilKgPerCell = Defs->GetConfigScalar(FName("SpoilKgPerCell"), SpoilKgPerCell);
	O2FillKgPerCell = Defs->GetConfigScalar(FName("O2FillKgPerCell"), O2FillKgPerCell);
	O2LeakKgPerCellPerSol = Defs->GetConfigScalar(FName("O2LeakKgPerCellPerSol"), O2LeakKgPerCellPerSol);
	O2FillRateKgPerHour = Defs->GetConfigScalar(FName("O2FillRateKgPerHour"), O2FillRateKgPerHour);
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

	// Deposits from data. SurfaceVisible=FALSE rows exist in the world but
	// stay undiscovered until a scout surveys them (M1-b: knowledge costs a
	// trip; the flag waited in the CSV since M0).
	Defs->ForEachDeposit([this](FName RowName, const FRHDepositRow& Row)
	{
		FRHDepositState D;
		D.Id = Deposits.Num() + 1;
		D.RowName = RowName;
		D.Type = Row.Type;
		D.RemainingKg = Row.Mass_kg;
		D.LocationCm = FVector(Row.LocX_m * 100.f, Row.LocY_m * 100.f, 0.f);
		D.bDiscovered = Row.SurfaceVisible;
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
	// Starter maintenance stock (M1-b: the row waited unused since M0). Parts
	// live in the network pool - the maintenance unit's van is the fiction.
	AddStock(FName("SpareParts"), Defs->GetConfigScalar(FName("StarterSpareParts"), 6.0));

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
			// Refusing era mode is not a punishment: hold the player's last
			// agent-band speed (not 1x) and say so LOUDLY - the notice-line
			// version read as "nothing happened" in the director's hands.
			const float Restore = FMath::Max(1.f, Clock->GetLastAgentSpeed());
			Clock->SetSpeed(Restore);
			UE_LOG(LogRedHopeSim, Display, TEXT("ERA REFUSED: %s - holding %.0fx"), *Why, Restore);
			OnAlert.Broadcast(FString::Printf(TEXT("60x unavailable: %s — holding %.0fx"), *Why, Restore));
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
	StepEventEdges();
	StepUplink();
	StepTaskBoard();
	StepProduction(SubDt);
	StepHabitability(SubDt);
	StepQuota();
	StepPower(SubDt);
}

void URHSimWorldSubsystem::StepEventEdges()
{
	const FRHEventRow* Event = GetActiveEvent();
	const bool bActive = Event != nullptr;
	if (bActive && !bEventWasActive)
	{
		// Onset (director ruling): snap ANY speed to 1x on the spot - the
		// player gets real time to batten down. They may re-speed at will.
		LastEventType = Event->Type;
		if (Clock && Clock->GetSpeed() > 1.f)
		{
			Clock->SetSpeed(1.f);
		}
		const bool bStorm = Event->Type == FName("DustStorm");
		const FString Alert = bStorm
			? FString::Printf(TEXT("DUST STORM ONSET — solar dropping to %.0f%%. Speed set to 1x: batten down. Robots working outside wear %.1fx faster."),
				Event->Severity * 100.f, StormWearMul)
			: FString::Printf(TEXT("SOLAR FLARE — exposed units wearing %.1fx faster until sol %.1f. Speed set to 1x."),
				Event->Severity, Event->StartSol + Event->DurationSols);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== %s ==="), *Alert);
		OnAlert.Broadcast(Alert);
	}
	else if (!bActive && bEventWasActive)
	{
		const FString Alert = LastEventType == FName("DustStorm")
			? FString(TEXT("The dust storm has passed. Solar output restored."))
			: FString(TEXT("The solar flare has subsided."));
		UE_LOG(LogRedHopeSim, Display, TEXT("=== %s ==="), *Alert);
		OnAlert.Broadcast(Alert);
	}
	bEventWasActive = bActive;
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
	// Field operations are agent-fidelity by definition: a scout mid-drive or
	// a maintenance call cannot integrate analytically. (Repair never posts a
	// board task - the claim set is its ledger.)
	if (RepairClaims.Num() > 0)
	{
		OutReason = TEXT("maintenance call in progress");
		return false;
	}
	for (const FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Survey)
		{
			OutReason = TEXT("survey in progress");
			return false;
		}
	}
	// World pressure (director ruling 2026-07-07b): "sleep through the siege,
	// never through the onset, never through a flare." A steady-state dust
	// storm may be era-skipped (the onset was experienced at 1x); flares
	// never - their whole life is the emergency.
	if (const FRHEventRow* Event = GetActiveEvent())
	{
		if (Event->Type == FName("SolarFlare"))
		{
			OutReason = TEXT("SolarFlare in progress");
			return false;
		}
	}
	if (Defs && Clock)
	{
		const double SolNow = Clock->GetSimSecondsTotal() / URHSimClockSubsystem::SolLengthSimSeconds;
		bool bImminent = false;
		FName ImminentType;
		Defs->ForEachEvent([&](FName, const FRHEventRow& Row)
		{
			// Within one era step's horizon of onset (in sols).
			const double HorizonSols = (double)URHSimClockSubsystem::EraStepSimSeconds / URHSimClockSubsystem::SolLengthSimSeconds;
			if (!bImminent && SolNow < (double)Row.StartSol && SolNow + HorizonSols >= (double)Row.StartSol)
			{
				bImminent = true;
				ImminentType = Row.Type;
			}
		});
		if (bImminent)
		{
			OutReason = FString::Printf(TEXT("%s imminent"), *ImminentType.ToString());
			return false;
		}
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
	StepEventEdges(); // storm END mid-era alerts; onsets can't occur here (imminent check drops first)
	StepUplink();
	EraLogistics(DtSimSeconds);
	StepProduction(DtSimSeconds);
	StepHabitability(DtSimSeconds);
	StepQuota();
	StepPower(DtSimSeconds);
}

void URHSimWorldSubsystem::EraLogistics(float DtSimSeconds)
{
	// Aggregate dig rate of the parked excavator fleet (kg per sol-hour),
	// derated by the charge duty cycle the agent band lives under: a robot
	// works Battery x (resume - seek) / DrawWork hours, then spends
	// Battery x (resume - seek) / PadRate hours docked. Every term is a data
	// row - a derived physical constant, not a tuning knob (paired-run
	// finding #3: ungated era dig ran ~8% hot from skipped charge trips).
	const FRHBuildingRow* PadDef = Defs->GetBuilding(FName("ChargePad"));
	const double PadRateW = PadDef ? PadDef->PowerDraw_W : 500.0;
	double DigRateKgPerH = 0.0;
	for (const auto& Fleet : FleetCounts)
	{
		if (const FRHRobotRow* Row = Defs->GetRobot(Fleet.Key))
		{
			if (Row->RobotClass == FName("Excavator"))
			{
				const double WorkH = Row->Battery_Wh * (ChargeResumeFraction - ChargeSeekFraction) / FMath::Max(1.f, Row->DrawWork_W);
				const double ChargeH = Row->Battery_Wh * (ChargeResumeFraction - ChargeSeekFraction) / PadRateW;
				const double Duty = WorkH / FMath::Max(0.1, WorkH + ChargeH);
				DigRateKgPerH += (double)Fleet.Value * Row->WorkRate * Duty;
			}
		}
	}
	double DigBudgetKg = DigRateKgPerH * (DtSimSeconds / 50.0); // sol-hour = 50 sim-s

	// Night gate (paired-run finding #2): the agent band's excavators halt
	// after dark whenever the bank is empty (pads shed, robots dock till
	// dawn) - ungated era digging ran ~70% hot. Same physical constraint,
	// integrated: no sun and no stored energy means no work.
	if (Defs->EvalSolarCurve(Clock->GetSolFraction()) <= 0.f && Power.BatteryWh <= 0.0)
	{
		DigBudgetKg = 0.0;
	}

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
			if (B.bUnderConstruction || B.Level != D.Level)
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
		// Leftover budget digs to a store, exactly like the agent band's
		// pile->hauler->Stockpile flow (paired-run finding: era extracted 27%
		// less and held 58% less raw regolith because the surplus dig was
		// simply never modeled - the Session 9 "digs only to feed demanding
		// consumers" limitation, now closed).
		if (DigBudgetKg > 0.0)
		{
			for (FRHBuildingInstance& S : Buildings)
			{
				if (!S.bUnderConstruction && S.Level == D.Level && (S.DefName == NAME_Stockpile || S.DefName == NAME_Lander))
				{
					const double TakeKg = FMath::Min(DigBudgetKg, D.RemainingKg);
					D.RemainingKg -= TakeKg;
					S.InputKg.FindOrAdd(D.Type) += TakeKg;
					DigBudgetKg -= TakeKg;
					break;
				}
			}
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
				if (C.bUnderConstruction || C.Id == B.Id || C.Level != B.Level)
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
					if (!S.bUnderConstruction && S.Id != B.Id && S.Level == B.Level && (S.DefName == NAME_Stockpile || S.DefName == NAME_Lander))
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
			if (B.bUnderConstruction || B.Level != D.Level)
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
				if (!B.bUnderConstruction && B.Level == D.Level && (B.DefName == NAME_Stockpile || B.DefName == NAME_Lander))
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
				if (C.bUnderConstruction || C.Id == B.Id || C.Level != B.Level)
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
					if (!S.bUnderConstruction && (S.DefName == NAME_Stockpile || S.DefName == NAME_Lander) && S.Id != B.Id && S.Level == B.Level)
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

	// Haul: store stock -> producer hopper, ONLY for inputs of a recipe whose
	// output construction is short of (M1-d Gate B: the Struct+Ore feed that
	// lets the Forge make the Shielding a taxed site is waiting on). Scoped to
	// construction shortage so steady-state logistics never grow this leg.
	{
		const TMap<FName, double> ShortageKg = ComputeConstructionShortage();
		const auto OutputsShort = [&ShortageKg](const FRHRecipeRow& Row)
		{
			for (const auto& Out : URHDefinitionsSubsystem::ParseResourceList(Row.Outputs))
			{
				if (ShortageKg.Contains(Out.Key))
				{
					return true;
				}
			}
			return false;
		};
		for (const FRHBuildingInstance& Store : Buildings)
		{
			if (ShortageKg.Num() == 0)
			{
				break;
			}
			if (Store.bUnderConstruction || (Store.DefName != NAME_Stockpile && Store.DefName != NAME_Lander))
			{
				continue;
			}
			for (const auto& Held : Store.InputKg)
			{
				if (Held.Value < HaulLoadMinKg)
				{
					continue;
				}
				for (const FRHBuildingInstance& C : Buildings)
				{
					if (C.bUnderConstruction || C.Id == Store.Id || C.Level != Store.Level)
					{
						continue;
					}
					const FRHRecipeRow* Wants = Defs->FindRunnableRecipe(C.DefName,
						[&](const TMap<FName, double>& Inputs)
						{
							const double* Need = Inputs.Find(Held.Key);
							if (!Need)
							{
								return false;
							}
							const double* Have = C.InputKg.Find(Held.Key);
							return (!Have || *Have < *Need * 2.0);
						}, OutputsShort);
					if (!Wants)
					{
						continue;
					}
					FRHSiteRef From; From.BuildingId = Store.Id;
					FRHSiteRef To; To.BuildingId = C.Id;
					if (!HasOpenTask(ERHTaskType::Haul, From, To, Held.Key))
					{
						FRHTask T;
						T.Id = NextTaskId++;
						T.Type = ERHTaskType::Haul;
						T.From = From;
						T.To = To;
						T.Resource = Held.Key;
						T.AmountKg = FMath::Min(Held.Value, 200.0);
						Tasks.Add(T);
					}
					break;
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
		for (const auto& Cost : URHDefinitionsSubsystem::GetBuildCostFor(*Def, Site.Level))
		{
			const double* Delivered = Site.InputKg.Find(Cost.Key);
			const double NeedKg = Cost.Value - (Delivered ? *Delivered : 0.0);
			if (NeedKg <= 0.0)
			{
				continue;
			}
			// Source: any completed building holding that resource, on the
			// site's floor or any trunk-linked floor (M1-d: the lift carries
			// materials across levels - how underground sites get built).
			int32 SourceId = 0;
			for (const FRHBuildingInstance& S : Buildings)
			{
				if (S.bUnderConstruction || S.Id == Site.Id || !AreLevelsLinked(S.Level, Site.Level))
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
	// Time-budget integrator (M1-c era-honesty fix): each building spends the
	// step's hours across batch progress, completion, AND the next batch's
	// start within one call - overshoot carries instead of rounding every
	// batch up to a whole step. At agent dt the budget is far smaller than
	// any batch, so behavior is unchanged; at era dt (1.2 h) this is what
	// closed the 8.8% divergence (a 2 h batch no longer costs 2 whole steps
	// plus an idle step to restart).
	// Construction shortage (M1-d Gate B): what the open sites need beyond
	// colony holdings. Recipes whose outputs are short get first claim on an
	// idle building - the site's demand is what tells the Forge to make
	// Shielding instead of more Struct. Empty map = pure row-order (M0 rule).
	const TMap<FName, double> ShortageKg = ComputeConstructionShortage();
	const auto OutputsShort = [&ShortageKg](const FRHRecipeRow& Row)
	{
		for (const auto& Out : URHDefinitionsSubsystem::ParseResourceList(Row.Outputs))
		{
			if (ShortageKg.Contains(Out.Key))
			{
				return true;
			}
		}
		return false;
	};

	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction || (!B.bPowered && !B.bBatchOnH2))
		{
			// Shed/unbuilt: batches stall, nothing starts (M0-c shedding rule).
			// Exception (M1-d): a Hydrogen-fuelled batch bought its whole run
			// up-front - it grinds on through the brownout. That is the fuel's
			// entire strategic point.
			continue;
		}

		const FRHBuildingRow* BDef = Defs->GetBuilding(B.DefName);
		const bool bExtractor = BDef && BDef->RequiresDeposit;

		double BudgetH = SubDt / 50.0; // sol-hour = 50 sim-s
		// Bounded loop: at most a handful of batch boundaries fit in one era
		// step; the guard is against a zero-time recipe row, not real data.
		for (int32 Guard = 0; Guard < 8 && BudgetH > 0.0; ++Guard)
		{
			if (B.BatchRemaining_h > 0.0)
			{
				const double SpendH = FMath::Min(BudgetH, B.BatchRemaining_h);
				B.BatchRemaining_h -= SpendH;
				BudgetH -= SpendH;
				if (B.BatchRemaining_h > 0.0)
				{
					break; // budget exhausted mid-batch
				}
				// Batch complete: grant outputs (hybrid rule - solids drop at
				// the building for hauling; fluids/gases join the pool).
				if (const TMap<FName, double>* Outputs = PendingOutputs.Find(B.Id))
				{
					for (const auto& Res : *Outputs)
					{
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
				// M1-d: designation work applies its world change at completion -
				// the spoil already dropped with the outputs above.
				if (const FIntPoint* Work = PendingBoreWork.Find(B.Id))
				{
					if (Work->X == 0) // bore: the trunk descends one floor
					{
						if (ShaftDepth == 0)
						{
							ShaftHeadCm = B.LocationCm; // the column is fixed by the Borer
						}
						++ShaftDepth;
						UE_LOG(LogRedHopeSim, Display, TEXT("Shaft reached floor -%d%s"), ShaftDepth,
							ShaftDepth >= BoreTargetDepth ? TEXT(" - bore designation complete") : TEXT(""));
					}
					else // carve: one cell opens on its floor
					{
						int32& Carved = FloorCarvedCells.FindOrAdd(Work->Y);
						++Carved;
						UE_LOG(LogRedHopeSim, Display, TEXT("Carved cell on floor %d (%d carved, %d queued)"),
							Work->Y, Carved, GetCarveQueued(Work->Y));
					}
					PendingBoreWork.Remove(B.Id);
				}
				B.ActiveRecipe = NAME_None;
				B.bBatchOnH2 = false;
				continue; // leftover budget flows into the next batch below
			}

			if (!B.bPowered)
			{
				break; // shed: an H2 batch may finish above, but nothing new starts
			}

			// M1-d Gate A2: designation-driven work first (the Borer). The
			// player's queue is the gate - these recipes never pass the
			// runnable search below. Bore before carve; carve shallowest-first
			// over sorted keys (TMap iteration order is not deterministic).
			if (BDef && BDef->CanBore)
			{
				FName WorkRecipeName = NAME_None;
				FIntPoint WorkItem(0, 0);
				bool bFloorWorkInFlight = false;
				for (const auto& W : PendingBoreWork)
				{
					if (W.Value.X == 0) { bFloorWorkInFlight = true; break; }
				}
				if (BoreTargetDepth > ShaftDepth && !bFloorWorkInFlight)
				{
					WorkRecipeName = FName("BoreFloor");
					WorkItem = FIntPoint(0, -(ShaftDepth + 1));
				}
				else
				{
					TArray<int32> Levels;
					CarveQueue.GenerateKeyArray(Levels);
					Levels.Sort([](int32 A, int32 C) { return A > C; }); // -1 before -2
					for (int32 L : Levels)
					{
						if (CarveQueue[L] > 0 && IsLevelConnected(L))
						{
							WorkRecipeName = FName("CarveCell");
							WorkItem = FIntPoint(1, L);
							break;
						}
					}
				}
				if (const FRHRecipeRow* Work = WorkRecipeName.IsNone() ? nullptr : Defs->GetRecipe(WorkRecipeName))
				{
					// Fuel decision at start, committed like extraction: the
					// whole batch's Hydrogen deducts up-front if stocked, else
					// the batch draws grid power.
					B.bBatchOnH2 = false;
					if (BDef->H2BurnKgPerHour > 0.f)
					{
						const double FuelKg = BDef->H2BurnKgPerHour * Work->BatchTime_h;
						if (GetStock(FName("Hydrogen")) >= FuelKg)
						{
							AddStock(FName("Hydrogen"), -FuelKg);
							B.bBatchOnH2 = true;
						}
					}
					if (WorkItem.X == 1)
					{
						--CarveQueue[WorkItem.Y]; // committed at start
					}
					PendingBoreWork.Add(B.Id, WorkItem);
					B.ActiveRecipe = WorkRecipeName;
					PendingOutputs.Add(B.Id, URHDefinitionsSubsystem::ParseResourceList(Work->Outputs));
					B.BatchRemaining_h = Work->BatchTime_h;
					UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d %s started%s (%.0f h, floor %d)"),
						*B.DefName.ToString(), B.Id, *WorkRecipeName.ToString(),
						B.bBatchOnH2 ? TEXT(" on H2") : TEXT(""), Work->BatchTime_h, WorkItem.Y);
					continue; // budget flows into the new batch
				}
			}

			// Idle: try to start a batch whose inputs are covered - solids
			// from the hopper, fluids from the network pool, extraction from
			// the attached deposit. Construction-short outputs outrank row
			// order (Gate B); no shortage = the M0 rule exactly.
			const auto InputsOk = [&](const TMap<FName, double>& Inputs)
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
			};
			const FRHRecipeRow* Recipe = ShortageKg.Num() > 0
				? Defs->FindRunnableRecipe(B.DefName, InputsOk, OutputsShort)
				: nullptr;
			if (!Recipe)
			{
				Recipe = Defs->FindRunnableRecipe(B.DefName, InputsOk);
			}
			if (!Recipe)
			{
				break; // nothing startable: the rest of the budget is idle time
			}

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
				FRHDepositState* D = FindDeposit(B.AttachedDepositId);
				if (!D || D->RemainingKg < OutMassKg)
				{
					break; // not enough left for a full batch
				}
				D->RemainingKg -= OutMassKg;
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
		// Arrival countdown (director ruling): loud alerts at T-2 and T-1
		// sols so the colony can prepare. Crew ships inherit this seam in M2.
		const double SolsOut = (ShipArrivalSimSeconds - Clock->GetSimSecondsTotal()) / URHSimClockSubsystem::SolLengthSimSeconds;
		if (ShipAlertStage < 1 && SolsOut <= 2.0 && SolsOut > 1.0)
		{
			ShipAlertStage = 1;
			OnAlert.Broadcast(TEXT("SUPPLY SHIP: touchdown in 2 sols — prepare the colony."));
		}
		else if (ShipAlertStage < 2 && SolsOut <= 1.0 && SolsOut > 0.0)
		{
			ShipAlertStage = 2;
			OnAlert.Broadcast(TEXT("SUPPLY SHIP: touchdown in 1 sol."));
		}
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

const FRHEventRow* URHSimWorldSubsystem::GetActiveEvent() const
{
	if (!Defs || !Clock)
	{
		return nullptr;
	}
	// Sol-fraction precision: an event starting at sol 12 begins at 12.0 exactly.
	const double SolNow = Clock->GetSimSecondsTotal() / URHSimClockSubsystem::SolLengthSimSeconds;
	const FRHEventRow* Active = nullptr;
	Defs->ForEachEvent([&](FName, const FRHEventRow& Row)
	{
		if (!Active && SolNow >= (double)Row.StartSol && SolNow < (double)Row.StartSol + Row.DurationSols)
		{
			Active = &Row;
		}
	});
	return Active;
}

double URHSimWorldSubsystem::GetDustFactorNow() const
{
	const FRHEventRow* Event = GetActiveEvent();
	if (Event && Event->Type == FName("DustStorm"))
	{
		return Event->Severity;
	}
	return Defs ? Defs->GetConfigScalar(FName("DustFactor"), 1.0) : 1.0;
}

float URHSimWorldSubsystem::GetRadiationAtLevel(int32 Level) const
{
	// Level 0 = surface (index 1.0). Subsurface floors are negative; each floor
	// of overburden multiplies exposure by RadiationPerLevelMul (~0.05 = a
	// couple metres of regolith is excellent shielding). Above-surface stays
	// at the surface index.
	const int32 Depth = FMath::Max(0, -Level);
	return RadiationSurface * FMath::Pow(RadiationPerLevelMul, (float)Depth);
}

float URHSimWorldSubsystem::GetRadiationNow(int32 Level) const
{
	float Rad = GetRadiationAtLevel(Level);
	// A solar flare floods the surface; overburden still shields, so only the
	// sky-exposed floors (Level >= 0) take the multiplier.
	if (Level >= 0)
	{
		if (const FRHEventRow* Event = GetActiveEvent())
		{
			if (Event->Type == FName("SolarFlare"))
			{
				Rad *= Event->Severity;
			}
		}
	}
	return Rad;
}

TMap<FName, double> URHSimWorldSubsystem::ComputeConstructionShortage() const
{
	TMap<FName, double> NeedKg, DeliveredKg;
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
		for (const auto& Cost : URHDefinitionsSubsystem::GetBuildCostFor(*Def, Site.Level))
		{
			const double* D = Site.InputKg.Find(Cost.Key);
			const double Delivered = D ? *D : 0.0;
			if (Cost.Value > Delivered)
			{
				NeedKg.FindOrAdd(Cost.Key) += Cost.Value - Delivered;
			}
			DeliveredKg.FindOrAdd(Cost.Key) += Delivered;
		}
	}
	TMap<FName, double> Short;
	for (const auto& P : NeedKg)
	{
		// On hand everywhere EXCEPT what already sits delivered at the sites
		// (GetTotalSolid counts site hoppers; the need already excludes them).
		const double* Delivered = DeliveredKg.Find(P.Key);
		const double OnHand = GetTotalSolid(P.Key) + GetStock(P.Key) - (Delivered ? *Delivered : 0.0);
		if (P.Value > OnHand)
		{
			Short.Add(P.Key, P.Value - OnHand);
		}
	}
	return Short;
}

bool URHSimWorldSubsystem::HasProducerFor(FName Resource) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (!B.bUnderConstruction && Defs && Defs->FindRecipeByOutput(B.DefName, Resource))
		{
			return true;
		}
	}
	return false;
}

void URHSimWorldSubsystem::Debug_AddSolid(FName DefName, FName Resource, double Kg)
{
	for (FRHBuildingInstance& B : Buildings)
	{
		if (B.DefName == DefName && !B.bUnderConstruction)
		{
			B.InputKg.FindOrAdd(Resource) += Kg;
			OnStockChanged.Broadcast(Resource, GetTotalSolid(Resource));
			return;
		}
	}
	UE_LOG(LogRedHopeSim, Warning, TEXT("Debug_AddSolid: no completed '%s'"), *DefName.ToString());
}

FVector URHSimWorldSubsystem::GetApproachPoint(const FRHSiteRef& Site, int32 RobotLevel) const
{
	const int32 SiteLevel = GetSiteLevel(Site);
	if (SiteLevel != RobotLevel && AreLevelsLinked(SiteLevel, RobotLevel) && ShaftDepth > 0)
	{
		FVector Head = ShaftHeadCm;
		Head.Z = RobotLevel * FloorHeightCm;
		return Head;
	}
	return GetSiteLocation(Site);
}

bool URHSimWorldSubsystem::IsFloorCirculated(int32 Level) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.Level == Level && !B.bUnderConstruction && B.bPowered)
		{
			const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(B.DefName) : nullptr;
			if (Def && Def->CirculatesAir)
			{
				return true;
			}
		}
	}
	return false;
}

void URHSimWorldSubsystem::StepHabitability(float SubDt)
{
	static const FName NOxygen(TEXT("Oxygen"));
	for (int32 Level = -1; Level >= -MaxDepth; --Level)
	{
		const int32 Cells = GetFloorCarvedCells(Level);
		if (Cells == 0 || !IsLevelConnected(Level))
		{
			continue;
		}
		const double RequiredKg = Cells * O2FillKgPerCell;
		double& FillKg = FloorO2Kg.FindOrAdd(Level);

		// Pressurized volume leaks - always. The standing tax of living in
		// what you dug; abandonment drains loudly instead of holding forever.
		FillKg = FMath::Max(0.0, FillKg - Cells * O2LeakKgPerCellPerSol * (SubDt / (double)URHSimClockSubsystem::SolLengthSimSeconds));

		// The trunk pushes O2 down; a powered circulator on the floor is what
		// actually moves air (no circulator, no fill - the chain's last link).
		if (IsFloorCirculated(Level))
		{
			const double WantKg = FMath::Min(O2FillRateKgPerHour * (SubDt / 50.0), RequiredKg - FillKg);
			if (WantKg > 0.0)
			{
				const double TakeKg = FMath::Min(WantKg, GetStock(NOxygen));
				if (TakeKg > 0.0)
				{
					AddStock(NOxygen, -TakeKg);
					FillKg += TakeKg;
				}
			}
		}

		// Rating edges announce banner-weight: rated at full fill + circulation,
		// lost below 98% (hysteresis keeps a healthy equilibrium from flapping).
		const bool bWasRated = RatedFloors.Contains(Level);
		if (!bWasRated && IsFloorCirculated(Level) && FillKg >= RequiredKg - KINDA_SMALL_NUMBER && RequiredKg > 0.0)
		{
			RatedFloors.Add(Level);
			OnAlert.Broadcast(FString::Printf(
				TEXT("FLOOR %d RATED LIVABLE — %d cell(s) pressurized and circulated, shielded under %d floor(s) of overburden."),
				Level, Cells, -Level));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== FLOOR %d RATED LIVABLE (%d cells, %.0f kg O2) ==="), Level, Cells, FillKg);
		}
		else if (bWasRated && (FillKg < RequiredKg * 0.98 || !IsFloorCirculated(Level)))
		{
			RatedFloors.Remove(Level);
			OnAlert.Broadcast(FString::Printf(
				TEXT("FLOOR %d LOST ITS HABITABILITY RATING — %s. Restore oxygen supply."),
				Level, IsFloorCirculated(Level) ? TEXT("oxygen fill below requirement") : TEXT("air circulation down")));
			UE_LOG(LogRedHopeSim, Display, TEXT("=== FLOOR %d LOST HABITABILITY (%.0f / %.0f kg O2) ==="), Level, FillKg, RequiredKg);
		}
	}
}

void URHSimWorldSubsystem::ExtendShaft(int32 ToDepth, const FVector& HeadCm)
{
	ToDepth = FMath::Clamp(ToDepth, 0, MaxDepth);
	if (ToDepth <= ShaftDepth)
	{
		return; // the trunk never retracts
	}
	if (ShaftDepth == 0)
	{
		ShaftHeadCm = HeadCm; // the column is fixed on the first bore
	}
	const int32 NewFloors = ToDepth - ShaftDepth;
	const double Spoil = NewFloors * ShaftSpoilKgPerFloor;
	SpoilPileKg += Spoil;
	ShaftDepth = ToDepth;
	UE_LOG(LogRedHopeSim, Display, TEXT("Shaft bored to floor -%d (%d new floor(s), +%.0f kg spoil; pile %.0f kg)"),
		ShaftDepth, NewFloors, Spoil, SpoilPileKg);
}

bool URHSimWorldSubsystem::ExcavateFloor(int32 Level, int32 Cells, FString& OutReason)
{
	if (Cells <= 0)
	{
		OutReason = TEXT("Nothing to excavate");
		return false;
	}
	if (Level >= 0 || !IsLevelConnected(Level))
	{
		OutReason = FString::Printf(TEXT("Floor %d not reached - bore the shaft deeper first"), Level);
		return false;
	}
	int32& Carved = FloorCarvedCells.FindOrAdd(Level);
	Carved += Cells;
	const double Spoil = Cells * SpoilKgPerCell;
	SpoilPileKg += Spoil;
	UE_LOG(LogRedHopeSim, Display, TEXT("Excavated %d cell(s) on floor %d (%d carved; +%.0f kg spoil; pile %.0f kg)"),
		Cells, Level, Carved, Spoil, SpoilPileKg);
	return true;
}

void URHSimWorldSubsystem::StepPower(float SubDt)
{
	// The storm's whole mechanical grip is one multiplier on solar generation
	// (M1-c): the bank-and-shedding stack from M0-c does the rest.
	const float Solar = Defs->EvalSolarCurve(Clock->GetSolFraction()) * (float)GetDustFactorNow();

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
		// M1-d: a Hydrogen-fuelled batch bought its energy up-front - the grid
		// sees only idle draw while it runs.
		E.WantW = (B.BatchRemaining_h > 0.0 && !B.bBatchOnH2) ? Def->PowerDraw_W : Def->PowerIdle_W;
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

	// Strip-chart ring: one sample per sol-hour, last 3 sols (72 entries).
	const int64 Hour = (int64)(Clock->GetSimSecondsTotal() / 50.0);
	if (Hour != LastPowerSampleHour)
	{
		LastPowerSampleHour = Hour;
		PowerHistory.Add(FVector3f((float)GenW, (float)LoadW, (float)Power.BatteryWh));
		if (PowerHistory.Num() > 72)
		{
			PowerHistory.RemoveAt(0, PowerHistory.Num() - 72);
		}
	}
}

void URHSimWorldSubsystem::EnqueueCommand(FRHCommand Command)
{
	Command.CommandId = NextCommandId++;
	Command.IssuedAtSimSeconds = Clock ? Clock->GetSimSecondsTotal() : 0.0;
	Command.ExecuteAtSimSeconds = Command.IssuedAtSimSeconds + OrderLagSeconds;
	UE_LOG(LogRedHopeSim, Display, TEXT("Uplink: '%s %s' transmitted, executes in %.0f sim-s"),
		*Command.Verb.ToString(), *Command.Target.ToString(), OrderLagSeconds);
	UplinkQueue.Add(MoveTemp(Command));
}

bool URHSimWorldSubsystem::CancelUplinkCommand(int32 CommandId)
{
	for (int32 i = 0; i < UplinkQueue.Num(); ++i)
	{
		if (UplinkQueue[i].CommandId == CommandId)
		{
			UE_LOG(LogRedHopeSim, Display, TEXT("Uplink: '%s %s' CANCELLED before execution"),
				*UplinkQueue[i].Verb.ToString(), *UplinkQueue[i].Target.ToString());
			UplinkQueue.RemoveAt(i);
			return true;
		}
	}
	return false; // already executed - the signal beat the cancel (that is the game)
}

bool URHSimWorldSubsystem::CanPlaceBuilding(FName DefName, const FVector& LocationCm, FString& OutReason, int32 Level) const
{
	const FRHBuildingRow* Def = Defs ? Defs->GetBuilding(DefName) : nullptr;
	if (!Def)
	{
		OutReason = FString::Printf(TEXT("Unknown building '%s'"), *DefName.ToString());
		return false;
	}
	// Z-model (M1-d Gate A): the surface is always buildable; a subsurface floor
	// opens once the shaft trunk reaches it. Out-of-range or above-surface
	// refuses; a real floor the bore hasn't reached refuses with the fix ("bore
	// deeper"), not a dead end.
	if (Level > 0 || Level < -MaxDepth)
	{
		OutReason = FString::Printf(TEXT("No such floor (%d)"), Level);
		return false;
	}
	if (Level < 0 && !IsLevelConnected(Level))
	{
		OutReason = FString::Printf(TEXT("Floor %d not reached - bore the shaft deeper"), Level);
		return false;
	}
	// Pylon-like defs (they project coverage and link by range) are the
	// territory extenders: their rule is link range to the nearest node,
	// not the coverage union. Everything else must sit inside coverage.
	if (Level < 0 && IsLevelConnected(Level))
	{
		// Subsurface: the shaft trunk carries the grid down and taps the whole
		// (small, starter) floor - coverage and the grid node both come from the
		// shaft, so neither the link-range nor the coverage-union check applies
		// (underground proposal §5; local distribution nodes are data headroom).
	}
	else if (Def->CoverageRadius_m > 0.f && Def->LinkRange_m > 0.f)
	{
		double NearestNodeCm = TNumericLimits<double>::Max();
		for (const FRHBuildingInstance& B : Buildings)
		{
			const FRHBuildingRow* NodeDef = (B.bUnderConstruction || B.Level != Level) ? nullptr : Defs->GetBuilding(B.DefName);
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
	else if (!IsInCoverage(LocationCm, Level))
	{
		OutReason = TEXT("Outside grid coverage - extend pylons first");
		return false;
	}
	// Footprint overlap: one cell, one structure (the demo-era gap - two
	// buildings could interpenetrate). Half-extents in cm match the ghost box
	// (FootprintX cells x 2 m); strict < keeps edge-adjacent placement legal.
	{
		const double HalfX = FMath::Max(1, Def->FootprintX) * 100.0;
		const double HalfY = FMath::Max(1, Def->FootprintY) * 100.0;
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.Level != Level)
			{
				continue;
			}
			const FRHBuildingRow* BDef = Defs->GetBuilding(B.DefName);
			if (!BDef)
			{
				continue;
			}
			if (FMath::Abs(B.LocationCm.X - LocationCm.X) < HalfX + FMath::Max(1, BDef->FootprintX) * 100.0
				&& FMath::Abs(B.LocationCm.Y - LocationCm.Y) < HalfY + FMath::Max(1, BDef->FootprintY) * 100.0)
			{
				OutReason = FString::Printf(TEXT("Footprint overlaps %s #%d"), *B.DefName.ToString(), B.Id);
				return false;
			}
		}
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
		// this only rejects orders the colony cannot POSSIBLY fill - short
		// stock with a completed producer online is an order that waits for
		// production (M1-d Gate B: how the first Shielding gets made - the
		// site's demand is what tells the Forge to make it).
		for (const auto& Cost : URHDefinitionsSubsystem::GetBuildCostFor(*Def, Level))
		{
			const double Available = GetTotalSolid(Cost.Key) + GetStock(Cost.Key);
			if (Available < Cost.Value && !HasProducerFor(Cost.Key))
			{
				OutReason = FString::Printf(TEXT("Insufficient %s (%.0f needed, %.0f on hand, no producer online)"),
					*Cost.Key.ToString(), Cost.Value, Available);
				return false;
			}
		}
	}
	return true;
}

const FRHDepositState* URHSimWorldSubsystem::FindDepositNear(const FVector& LocationCm, double MaxDistCm, int32 Level) const
{
	const FRHDepositState* Best = nullptr;
	double BestDist = MaxDistCm;
	for (const FRHDepositState& D : Deposits)
	{
		if (D.Level != Level || !D.bDiscovered)
		{
			continue;
		}
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
		if (!CanPlaceBuilding(Cmd.Target, Cmd.Location, Reason, Cmd.Level))
		{
			OnCommandRejected.Broadcast(Cmd, Reason);
			return;
		}
		const FRHBuildingRow* Def = Defs->GetBuilding(Cmd.Target);
		if (Def->ImportOnly)
		{
			--ImportStock.FindOrAdd(Cmd.Target); // consumption happens at execution, not preview
		}
		AddBuilding(Cmd.Target, Cmd.Location, /*bInstant*/ Def->BuildTime_s <= 0.0, Cmd.Level);
	}
	else if (Cmd.Verb == FName("Dig"))
	{
		bool bFound = false;
		for (FRHDepositState& D : Deposits)
		{
			// Undiscovered deposits reject as unknown - mission control cannot
			// designate ground truth the colony has not surveyed (and the
			// message must not leak that something is there).
			if (D.RowName == Cmd.Target && D.bDiscovered)
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
	else if (Cmd.Verb == FName("Survey"))
	{
		FRHTask T;
		T.Id = NextTaskId++;
		T.Type = ERHTaskType::Survey;
		T.TargetCm = FVector(Cmd.Location.X, Cmd.Location.Y, 0.0);
		Tasks.Add(T);
		UE_LOG(LogRedHopeSim, Display, TEXT("Survey posted: (%.0f, %.0f) m - awaiting a scout"),
			Cmd.Location.X / 100.0, Cmd.Location.Y / 100.0);
	}
	else if (Cmd.Verb == FName("Bore"))
	{
		// M1-d Gate A2: order the shaft trunk down to Value floors. The Borer
		// works it one BoreFloor batch per floor; designations are standing
		// orders, so deeper re-orders just raise the target.
		bool bHaveBorer = false;
		for (const FRHBuildingInstance& B : Buildings)
		{
			const FRHBuildingRow* D = Defs->GetBuilding(B.DefName);
			if (D && D->CanBore && !B.bUnderConstruction)
			{
				bHaveBorer = true;
				break;
			}
		}
		if (!bHaveBorer)
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("No Borer online - build one first"));
			return;
		}
		const int32 Target = FMath::Clamp((int32)Cmd.Value, 0, MaxDepth);
		if (Target <= ShaftDepth)
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Shaft already at floor -%d"), ShaftDepth));
			return;
		}
		BoreTargetDepth = FMath::Max(BoreTargetDepth, Target);
		UE_LOG(LogRedHopeSim, Display, TEXT("Bore designation: shaft to floor -%d (now -%d)"), BoreTargetDepth, ShaftDepth);
	}
	else if (Cmd.Verb == FName("Excavate"))
	{
		// M1-d Gate A2: carve Value cells on a reached floor. Queue decrements
		// at batch START (committed work); the carve applies at completion.
		const int32 Cells = (int32)Cmd.Value;
		if (Cells <= 0 || Cmd.Level >= 0 || Cmd.Level < -MaxDepth)
		{
			OnCommandRejected.Broadcast(Cmd, TEXT("Excavation needs a subsurface floor and a cell count"));
			return;
		}
		if (!IsLevelConnected(Cmd.Level))
		{
			OnCommandRejected.Broadcast(Cmd, FString::Printf(TEXT("Floor %d not reached - bore the shaft deeper first"), Cmd.Level));
			return;
		}
		CarveQueue.FindOrAdd(Cmd.Level) += Cells;
		UE_LOG(LogRedHopeSim, Display, TEXT("Excavation designation: %d cell(s) on floor %d (%d queued there)"),
			Cells, Cmd.Level, CarveQueue[Cmd.Level]);
	}

	OnCommandExecuted.Broadcast(Cmd);
}

void URHSimWorldSubsystem::AddBuilding(FName DefName, const FVector& LocationCm, bool bInstant, int32 Level)
{
	FRHBuildingInstance Instance;
	Instance.Id = NextBuildingId++;
	Instance.DefName = DefName;
	Instance.LocationCm = LocationCm;
	Instance.Level = Level;
	Instance.LocationCm.Z = Level * FloorHeightCm; // Z is derived, never authored

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
	// Undiscovered ground never attaches: you cannot drill what the colony
	// has not surveyed, even by console coordinates.
	if (Def && Def->RequiresDeposit)
	{
		for (const FRHDepositState& D : Deposits)
		{
			if (D.Level == Level && D.bDiscovered && FVector::DistXY(D.LocationCm, LocationCm) <= 1200.0)
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

void URHSimWorldSubsystem::Debug_PlaceInstant(FName DefName, const FVector& LocationCm, int32 Level)
{
	if (!Defs || !Defs->GetBuilding(DefName))
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("Debug_PlaceInstant: unknown building '%s'"), *DefName.ToString());
		return;
	}
	AddBuilding(DefName, LocationCm, /*bInstant*/ true, Level);
}

void URHSimWorldSubsystem::Debug_Showcase()
{
	if (!Defs)
	{
		return;
	}
	// Lay the whole canon set out on a grid north of the Lander so every
	// silhouette + glow is visible in one frame. Lander already sits at origin.
	TArray<FName> Names;
	Defs->ForEachBuilding([&Names](FName Name, const FRHBuildingRow&)
	{
		if (Name != NAME_Lander)
		{
			Names.Add(Name);
		}
	});
	Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

	const int32 Cols = 4;
	const float SpacingCm = 1400.f;    // 14 m centres: big silhouettes clear each other
	const float OriginXCm = 1600.f;    // start north of the Lander
	for (int32 i = 0; i < Names.Num(); ++i)
	{
		const int32 Row = i / Cols;
		const int32 Col = i % Cols;
		const FVector Loc(
			OriginXCm + Row * SpacingCm,
			(Col - (Cols - 1) * 0.5f) * SpacingCm,
			0.f);
		AddBuilding(Names[i], Loc, /*bInstant*/ true);
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("[Showcase] placed %d building types"), Names.Num());
}

bool URHSimWorldSubsystem::IsInCoverage(const FVector& LocationCm, int32 Level) const
{
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (B.bUnderConstruction || B.Level != Level)
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

int32 URHSimWorldSubsystem::GetSiteLevel(const FRHSiteRef& Site) const
{
	if (Site.BuildingId > 0)
	{
		for (const FRHBuildingInstance& B : Buildings)
		{
			if (B.Id == Site.BuildingId)
			{
				return B.Level;
			}
		}
	}
	if (Site.DepositId > 0)
	{
		for (const FRHDepositState& D : Deposits)
		{
			if (D.Id == Site.DepositId)
			{
				return D.Level;
			}
		}
	}
	return 0;
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

int32 URHSimWorldSubsystem::TryClaimDig(const FVector& RobotPosCm, int32 RobotLevel)
{
	int32 BestId = 0;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHDepositState& D : Deposits)
	{
		if (D.Level == RobotLevel && D.bDesignated && D.RemainingKg > 0.0 && D.DigClaims < 2)
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

bool URHSimWorldSubsystem::TryClaimHaul(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask, int32 RobotLevel)
{
	FRHTask* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Haul && !T.ClaimedBy.IsValid() && GetSiteLevel(T.From) == RobotLevel)
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
	// Cross-level delivery drives to the shaft head; the lift takes the cargo
	// the last leg (robots are surface-bound until a later gate).
	OutDropoffCm = GetApproachPoint(T->To, /*RobotLevel*/ 0);
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

bool URHSimWorldSubsystem::TryClaimBuild(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask, int32 RobotLevel)
{
	FRHTask* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHTask& T : Tasks)
	{
		// Cross-level claims allowed when the trunk links the floors: the
		// fabricator works the shaft head, the lift carries the work (M1-d).
		if (T.Type == ERHTaskType::Build && !T.ClaimedBy.IsValid() && AreLevelsLinked(GetSiteLevel(T.To), RobotLevel))
		{
			const double Dist = FVector::DistXY(GetApproachPoint(T.To, RobotLevel), RobotPosCm);
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
	// bringing it; multi-resource since M1-a; Level-taxed since M1-d Gate B).
	const FRHBuildingRow* Def = Defs->GetBuilding(B->DefName);
	const TMap<FName, double> Cost = Def ? URHDefinitionsSubsystem::GetBuildCostFor(*Def, B->Level) : TMap<FName, double>();
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
		// ComputeModule (M1-c latency arc): local autonomy takes the uplink
		// from tier 0 to tier 1. min() so a manifest ComputeCore's tier 2 is
		// never regressed. OrderLagSeconds serializes - survives loads.
		if (B->DefName == FName("ComputeModule"))
		{
			const double Tier1 = Defs->GetConfigScalar(FName("OrderLagTier1_s"), 20.0);
			if (Tier1 < OrderLagSeconds)
			{
				OrderLagSeconds = Tier1;
				UE_LOG(LogRedHopeSim, Display, TEXT("Compute module online: order lag now %.0f sim-s"), OrderLagSeconds);
			}
		}
		UE_LOG(LogRedHopeSim, Display, TEXT("%s #%d construction complete"), *B->DefName.ToString(), B->Id);
		OnBuildingCompleted.Broadcast(*B);
		return true;
	}
	return false;
}

bool URHSimWorldSubsystem::FindNearestChargePad(const FVector& RobotPosCm, int32& OutBuildingId, FVector& OutLocationCm, int32 RobotLevel) const
{
	const FName NAME_ChargePad(TEXT("ChargePad"));
	double BestDist = TNumericLimits<double>::Max();
	OutBuildingId = 0;
	for (const FRHBuildingInstance& B : Buildings)
	{
		if (!B.bUnderConstruction && B.DefName == NAME_ChargePad && B.Level == RobotLevel)
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

double URHSimWorldSubsystem::RequestChargeWh(int32 PadBuildingId, double SubDt, FMassEntityHandle Robot)
{
	FRHBuildingInstance* Pad = FindBuilding(PadBuildingId);
	if (!Pad || Pad->bUnderConstruction || !Pad->bPowered)
	{
		return 0.0;
	}
	// Queue etiquette (M1-b): one umbilical - only the head draws. A robot
	// that never joined (legacy brain) passes an invalid handle and bypasses.
	if (Robot.IsValid())
	{
		const TArray<FMassEntityHandle>* Queue = PadQueues.Find(PadBuildingId);
		if (Queue && Queue->Num() > 0 && (*Queue)[0] != Robot)
		{
			return 0.0; // docked, waiting for the umbilical
		}
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

void URHSimWorldSubsystem::JoinPadQueue(int32 PadBuildingId, FMassEntityHandle Robot)
{
	TArray<FMassEntityHandle>& Queue = PadQueues.FindOrAdd(PadBuildingId);
	Queue.AddUnique(Robot);
}

void URHSimWorldSubsystem::LeavePadQueue(int32 PadBuildingId, FMassEntityHandle Robot)
{
	if (TArray<FMassEntityHandle>* Queue = PadQueues.Find(PadBuildingId))
	{
		Queue->Remove(Robot);
	}
}

// --- Fleet reality (M1-b Gate B) ---

void URHSimWorldSubsystem::AccrueWear(float& Wear, float WearPerSol, float Dt) const
{
	// Weather tax (M1-c + director ruling 2026-07-07b): an active flare
	// multiplies exertion wear on exposed units (placeholder until M2's
	// electronic-fault system - flares become software faults repaired by
	// humans); a dust storm grinds working machinery at StormWearMul. Until
	// the M1-d vault / M2 warehouse exist, EVERY working unit is exposed;
	// docked at a pad is not shelter. The player is supposed to feel this gap.
	float Mul = 1.f;
	if (const FRHEventRow* Event = GetActiveEvent())
	{
		Mul = (Event->Type == FName("SolarFlare")) ? Event->Severity : StormWearMul;
	}
	Wear = FMath::Min(WearHaltThreshold, Wear + WearPerSol * Mul * (Dt / (float)URHSimClockSubsystem::SolLengthSimSeconds));
}

float URHSimWorldSubsystem::GetWearWorkMul(float Wear) const
{
	if (Wear <= WearDegradeThreshold)
	{
		return 1.f;
	}
	const float Span = FMath::Max(1.f, WearHaltThreshold - WearDegradeThreshold);
	return FMath::Max(0.f, 1.f - (Wear - WearDegradeThreshold) / Span);
}

bool URHSimWorldSubsystem::TryClaimSurvey(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask)
{
	FRHTask* Best = nullptr;
	double BestDist = TNumericLimits<double>::Max();
	for (FRHTask& T : Tasks)
	{
		if (T.Type == ERHTaskType::Survey && !T.ClaimedBy.IsValid())
		{
			const double Dist = FVector::DistXY(T.TargetCm, RobotPosCm);
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

void URHSimWorldSubsystem::CompleteSurvey(int32 TaskId, double RadiusM)
{
	const FRHTask* T = FindTask(TaskId);
	if (!T)
	{
		return;
	}
	const FVector Point = T->TargetCm;
	int32 Found = 0;
	for (FRHDepositState& D : Deposits)
	{
		if (!D.bDiscovered && FVector::DistXY(D.LocationCm, Point) <= RadiusM * 100.0)
		{
			D.bDiscovered = true;
			++Found;
			UE_LOG(LogRedHopeSim, Display, TEXT("=== SURVEY: %s discovered - %.0f t %s at (%.0f, %.0f) m ==="),
				*D.RowName.ToString(), D.RemainingKg / 1000.0, *D.Type.ToString(),
				D.LocationCm.X / 100.0, D.LocationCm.Y / 100.0);
			OnDepositDiscovered.Broadcast(D);
		}
	}
	if (Found == 0)
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("Survey complete at (%.0f, %.0f) m: nothing within %.0f m"),
			Point.X / 100.0, Point.Y / 100.0, RadiusM);
	}
	// The record is the player's map (director request): covered ground stays
	// known, including the empty circles - "nothing there" was paid for too.
	FRHSurveyRecord Record;
	Record.PointCm = Point;
	Record.RadiusM = (float)RadiusM;
	Record.Sol = Clock ? Clock->GetSol() : 0;
	Record.FoundCount = Found;
	SurveyHistory.Add(Record);
	OnSurveyCompleted.Broadcast(SurveyHistory.Last());
	CompleteTask(TaskId);
}

bool URHSimWorldSubsystem::TryClaimRepair(FMassEntityHandle Self, FMassEntityHandle& OutTarget, FVector& OutTargetCm)
{
	if (GetStock(FName("SpareParts")) < 1.0)
	{
		return false; // no parts: nothing to offer the patient
	}
	URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();
	if (!Agents)
	{
		return false;
	}
	FMassEntityHandle Best;
	FVector BestPos = FVector::ZeroVector;
	float BestWear = WearDegradeThreshold; // strictly-worse-than-threshold claims only
	Agents->ForEachRobotState([&](FMassEntityHandle Entity, const FVector& PosCm, float Wear)
	{
		if (Entity != Self && Wear >= BestWear && !RepairClaims.Contains(Entity))
		{
			Best = Entity;
			BestPos = PosCm;
			BestWear = Wear;
		}
	});
	if (!Best.IsValid())
	{
		return false;
	}
	RepairClaims.Add(Best);
	OutTarget = Best;
	OutTargetCm = BestPos;
	return true;
}

bool URHSimWorldSubsystem::GetRepairTargetPos(FMassEntityHandle Target, FVector& OutPosCm) const
{
	const URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();
	return Agents && Agents->GetRobotPosition(Target, OutPosCm);
}

void URHSimWorldSubsystem::ApplyRepairAt(FMassEntityHandle Target)
{
	URHAgentSubsystem* Agents = GetWorld()->GetSubsystem<URHAgentSubsystem>();
	if (!Agents)
	{
		return;
	}
	// Spend parts against the wear, one part = RepairWearPerPart, bounded by
	// stock. Instant at slice scale - the visit is the cost, like site work.
	const FName NAME_SpareParts(TEXT("SpareParts"));
	int32 PartsSpent = 0;
	float WearRemoved = 0.f;
	while (GetStock(NAME_SpareParts) >= 1.0)
	{
		const float Removed = Agents->RemoveRobotWear(Target, RepairWearPerPart);
		if (Removed <= 0.f)
		{
			break; // clean (or target gone)
		}
		AddStock(NAME_SpareParts, -1.0);
		++PartsSpent;
		WearRemoved += Removed;
	}
	if (PartsSpent > 0)
	{
		UE_LOG(LogRedHopeSim, Display, TEXT("REPAIR: %.0f wear removed (%d parts, %.0f left in stock)"),
			WearRemoved, PartsSpent, GetStock(NAME_SpareParts));
	}
}

void URHSimWorldSubsystem::ReleaseRepairClaim(FMassEntityHandle Target)
{
	RepairClaims.Remove(Target);
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
	ShipAlertStage = 0; // fresh countdown per launch
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
		Ar << C.Verb << C.Target << C.Location << C.Level << C.Value << C.IssuedAtSimSeconds << C.ExecuteAtSimSeconds;
	}

	Num = SavedBuildings.Num();
	Ar << Num;
	for (FRHBuildingInstance& B : SavedBuildings)
	{
		Ar << B.Id << B.DefName << B.LocationCm << B.Level << B.bUnderConstruction << B.bPowered
		   << B.BuildRemaining_s << B.BatchRemaining_h << B.ActiveRecipe << B.AttachedDepositId
		   << B.bBatchOnH2;
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
		Ar << D.Id << D.RowName << D.Type << D.RemainingKg << D.PileKg << D.LocationCm << D.Level << D.bDiscovered << D.bDesignated;
	}

	Num = SavedTasks.Num();
	Ar << Num;
	for (FRHTask& T : SavedTasks)
	{
		uint8 Type = (uint8)T.Type;
		Ar << T.Id << Type;
		SerializeSite(Ar, T.From);
		SerializeSite(Ar, T.To);
		Ar << T.Resource << T.AmountKg << T.TargetCm;
	}

	Num = Robots.Num();
	Ar << Num;
	for (FRHRobotSaveState& R : Robots)
	{
		Ar << R.DefName << R.PosCm << R.Level << R.ChargeWh << R.Wear;
	}

	Num = SurveyHistory.Num();
	Ar << Num;
	for (FRHSurveyRecord& S : SurveyHistory)
	{
		Ar << S.PointCm << S.RadiusM << S.Sol << S.FoundCount;
	}

	// Shaft & excavation + designations (save v5/v6).
	Ar << ShaftDepth << ShaftHeadCm << SpoilPileKg << FloorCarvedCells;
	Ar << BoreTargetDepth << CarveQueue << PendingBoreWork;
	// Habitability chain (save v7).
	Ar << FloorO2Kg << RatedFloors;

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
	RepairClaims.Reset();  // claim state is runtime-only; robots re-claim
	PadQueues.Reset();
	SurveyHistory.Reset();
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
		Ar << C.Verb << C.Target << C.Location << C.Level << C.Value << C.IssuedAtSimSeconds << C.ExecuteAtSimSeconds;
		UplinkQueue.Add(MoveTemp(C));
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHBuildingInstance B;
		Ar << B.Id << B.DefName << B.LocationCm << B.Level << B.bUnderConstruction << B.bPowered
		   << B.BuildRemaining_s << B.BatchRemaining_h << B.ActiveRecipe << B.AttachedDepositId
		   << B.bBatchOnH2;
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
		Ar << D.Id << D.RowName << D.Type << D.RemainingKg << D.PileKg << D.LocationCm << D.Level << D.bDiscovered << D.bDesignated;
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
		Ar << T.Resource << T.AmountKg << T.TargetCm;
		T.Type = (ERHTaskType)Type;
		Tasks.Add(MoveTemp(T));
	}

	Ar << Num;
	TArray<FMassEntityHandle> Respawned;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHRobotSaveState R;
		Ar << R.DefName << R.PosCm << R.Level << R.ChargeWh << R.Wear;
		if (const FRHRobotRow* Row = Defs->GetRobot(R.DefName))
		{
			SpawnRobotTracked(R.DefName, *Row, R.PosCm, R.ChargeWh, R.Wear, Respawned);
		}
	}

	Ar << Num;
	for (int32 i = 0; i < Num; ++i)
	{
		FRHSurveyRecord S;
		Ar << S.PointCm << S.RadiusM << S.Sol << S.FoundCount;
		SurveyHistory.Add(S);
	}

	// Shaft & excavation + designations (save v5/v6). Maps repopulate wholesale.
	FloorCarvedCells.Empty();
	Ar << ShaftDepth << ShaftHeadCm << SpoilPileKg << FloorCarvedCells;
	CarveQueue.Empty();
	PendingBoreWork.Empty();
	Ar << BoreTargetDepth << CarveQueue << PendingBoreWork;
	// Habitability chain (save v7).
	FloorO2Kg.Empty();
	RatedFloors.Empty();
	Ar << FloorO2Kg << RatedFloors;

	// Loads land at 1x regardless of the saved speed: give the player a calm
	// frame before they choose a tier again.
	Clock->Debug_SetSimSeconds(SimSeconds);
	Clock->SetSpeed(1.f);
	LastAutosaveSol = Clock->GetSol();

	// Transient edge state re-derives from the loaded clock: a mid-storm load
	// must not re-announce "onset", and the ship countdown picks up mid-run.
	const FRHEventRow* ActiveNow = GetActiveEvent();
	bEventWasActive = ActiveNow != nullptr;
	LastEventType = ActiveNow ? ActiveNow->Type : NAME_None;
	if (QuotaPhase == ERHQuotaPhase::ShipInbound)
	{
		const double SolsOut = (ShipArrivalSimSeconds - SimSeconds) / URHSimClockSubsystem::SolLengthSimSeconds;
		ShipAlertStage = SolsOut <= 1.0 ? 2 : (SolsOut <= 2.0 ? 1 : 0);
	}
	else
	{
		ShipAlertStage = 0;
	}

	OnColonyReloaded.Broadcast();
	if (Respawned.Num() > 0)
	{
		OnRobotsSpawned.Broadcast(Respawned);
	}
	UE_LOG(LogRedHopeSim, Display, TEXT("Colony loaded: slot '%s' (sol %d, %d buildings, %d robots, %d tasks)"),
		*Slot, Clock->GetSol(), Buildings.Num(), Respawned.Num(), Tasks.Num());
	return true;
}

