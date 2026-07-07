// Console commands for scaffold testing and the hardware stress benchmark.
// All are RH.* and development-only conveniences; shipping builds strip them.

#include "CoreMinimal.h"
#include "RedHope.h"
#include "RHAgentVisualizerSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHPlayerController.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "Misc/CoreDelegates.h"

static FAutoConsoleCommandWithWorldAndArgs GRHSpawnDummies(
	TEXT("RH.SpawnDummies"),
	TEXT("RH.SpawnDummies <Count> [ExtentM=300] - spawn wandering dummy agents for the hardware stress test."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const int32 Count = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 100;
		const float ExtentCm = (Args.Num() > 1 ? FCString::Atof(*Args[1]) : 300.f) * 100.f;

		URHAgentSubsystem* Agents = World->GetSubsystem<URHAgentSubsystem>();
		URHAgentVisualizerSubsystem* Viz = World->GetSubsystem<URHAgentVisualizerSubsystem>();
		if (!Agents || !Viz)
		{
			UE_LOG(LogRedHope, Error, TEXT("RH.SpawnDummies: subsystems unavailable (PIE running?)"));
			return;
		}
		const TArray<FMassEntityHandle> Handles = Agents->SpawnDummyAgents(Count, FVector::ZeroVector, ExtentCm);
		Viz->TrackEntities(Handles);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSetSpeed(
	TEXT("RH.SetSpeed"),
	TEXT("RH.SetSpeed <0|1|3|8|60> - set sim speed tier (60 = era mode; auto-drops on agent-fidelity events)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { return; }
		if (URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>())
		{
			Clock->SetSpeed(FCString::Atof(*Args[0]));
		}
	}));

// Frame-time + memory sampler. Samples every rendered frame for the given
// duration, then logs avg/p95/worst ms and process memory. Output lands in
// the log so the MCP log reader can lift it into the build log verbatim.
struct FRHBenchmarkSampler
{
	TArray<float> FrameMs;
	double EndTime = 0.0;
	double LastTime = 0.0;
	FDelegateHandle Handle;
	FString Label;

	void Start(const FString& InLabel, float Seconds)
	{
		Label = InLabel;
		FrameMs.Reset();
		FrameMs.Reserve(20000);
		LastTime = FPlatformTime::Seconds();
		EndTime = LastTime + Seconds;
		Handle = FCoreDelegates::OnEndFrame.AddRaw(this, &FRHBenchmarkSampler::OnFrame);
		UE_LOG(LogRedHope, Display, TEXT("[RH.Benchmark] '%s' sampling %.0f s..."), *Label, Seconds);
	}

	void OnFrame()
	{
		const double Now = FPlatformTime::Seconds();
		FrameMs.Add(static_cast<float>((Now - LastTime) * 1000.0));
		LastTime = Now;

		if (Now >= EndTime)
		{
			FCoreDelegates::OnEndFrame.Remove(Handle);
			Report();
		}
	}

	void Report()
	{
		if (FrameMs.Num() < 2)
		{
			return;
		}
		FrameMs.RemoveAt(0); // first sample spans the command frame itself
		TArray<float> Sorted = FrameMs;
		Sorted.Sort();

		float Sum = 0.f;
		for (float Ms : Sorted) { Sum += Ms; }
		const float Avg = Sum / Sorted.Num();
		const float P95 = Sorted[FMath::Min(Sorted.Num() - 1, (int32)(Sorted.Num() * 0.95f))];
		const float Worst = Sorted.Last();

		const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
		UE_LOG(LogRedHope, Display,
			TEXT("[RH.Benchmark] '%s' RESULT: frames=%d avg=%.2f ms (%.0f fps) p95=%.2f ms worst=%.2f ms | mem used=%.0f MB peak=%.0f MB"),
			*Label, Sorted.Num(), Avg, Avg > 0.f ? 1000.f / Avg : 0.f, P95, Worst,
			MemStats.UsedPhysical / (1024.0 * 1024.0), MemStats.PeakUsedPhysical / (1024.0 * 1024.0));
	}
};

static FRHBenchmarkSampler GRHBenchmarkSampler;

static FAutoConsoleCommandWithWorldAndArgs GRHBenchmark(
	TEXT("RH.Benchmark"),
	TEXT("RH.Benchmark <LabelNoSpaces> [Seconds=10] - sample frame time + memory and log the result."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		const FString Label = Args.Num() > 0 ? Args[0] : TEXT("unlabeled");
		const float Seconds = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 10.f;
		GRHBenchmarkSampler.Start(Label, Seconds);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHBuild(
	TEXT("RH.Build"),
	TEXT("RH.Build <DefName> <Xm> <Ym> [Level=0] - transmit a Build order through the uplink (Level<0 needs the shaft bored to that depth)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 3)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Build <DefName> <Xm> <Ym> [Level=0]"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Build");
			Cmd.Target = FName(*Args[0]);
			Cmd.Level = Args.Num() > 3 ? FCString::Atoi(*Args[3]) : 0;
			Cmd.Location = FVector(FCString::Atof(*Args[1]) * 100.f, FCString::Atof(*Args[2]) * 100.f,
				Cmd.Level * (float)Sim->GetFloorHeightCm());
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHBore(
	TEXT("RH.Bore"),
	TEXT("RH.Bore <ToDepth> - order the shaft trunk bored down to the given floor (uplink; needs a Borer online, works batch by batch)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Bore <ToDepth>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Bore");
			Cmd.Value = FCString::Atod(*Args[0]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHExcavate(
	TEXT("RH.Excavate"),
	TEXT("RH.Excavate <Level> <Cells> - order N 10x10 cells carved on a reached floor (uplink; the Borer works the queue)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Excavate <Level> <Cells>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Excavate");
			Cmd.Level = FCString::Atoi(*Args[0]);
			Cmd.Value = FCString::Atod(*Args[1]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHFloor(
	TEXT("RH.Floor"),
	TEXT("RH.Floor <Level> - ride the elevator: 0 = surface, -N = subsurface floor (camera, slice view, and order Level follow)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Floor <Level>"));
			return;
		}
		if (ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController()))
		{
			PC->SetActiveLevel(FCString::Atoi(*Args[0]));
			UE_LOG(LogRedHope, Display, TEXT("Elevator: floor %d"), PC->GetActiveLevel());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHBoreNow(
	TEXT("RH.BoreNow"),
	TEXT("RH.BoreNow <ToDepth> [Xm=0] [Ym=0] - DEBUG: extend the shaft instantly (bypasses Borer/uplink; spoil banks at the head, unhauled)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.BoreNow <ToDepth> [Xm] [Ym]"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			const float X = (Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f) * 100.f;
			const float Y = (Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.f) * 100.f;
			Sim->ExtendShaft(FCString::Atoi(*Args[0]), FVector(X, Y, 0.f));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHExcavateNow(
	TEXT("RH.ExcavateNow"),
	TEXT("RH.ExcavateNow <Level> <Cells> - DEBUG: carve instantly (bypasses Borer/uplink; spoil banks at the head, unhauled)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.ExcavateNow <Level> <Cells>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FString Reason;
			if (!Sim->ExcavateFloor(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]), Reason))
			{
				UE_LOG(LogRedHope, Warning, TEXT("Excavate: %s"), *Reason);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHShowcase(
	TEXT("RH.Showcase"),
	TEXT("RH.Showcase - instantly place one completed instance of every building type on a grid (visual QA)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr)
		{
			Sim->Debug_Showcase();
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHStatus(
	TEXT("RH.Status"),
	TEXT("RH.Status - log colony power, economy, tasks, quota, and uplink."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		const URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>();
		if (!Sim || !Clock) { return; }

		const FRHPowerState& P = Sim->GetPower();
		UE_LOG(LogRedHope, Display, TEXT("[RH.Status] Sol %d %.0f%% | gen %.0f W load %.0f W battery %.0f/%.0f Wh%s | buildings %d | tasks %d | uplink %d"),
			Clock->GetSol(), Clock->GetSolFraction() * 100.f,
			P.GenW, P.LoadW, P.BatteryWh, P.BatteryCapWh,
			P.ShedCount > 0 ? *FString::Printf(TEXT(" SHED:%d"), P.ShedCount) : TEXT(""),
			Sim->GetBuildings().Num(), Sim->GetOpenTaskCount(), Sim->GetUplinkQueue().Num());

		switch (Sim->GetQuotaPhase())
		{
		case ERHQuotaPhase::AwaitingManifest:
			UE_LOG(LogRedHope, Display, TEXT("  QUOTA MET. Manifest: %.0f / %.0f kg [%d items] - RH.Manifest <Item>, RH.Launch"),
				Sim->GetManifestMassKg(), Sim->GetAwardMassKg(), Sim->GetManifestItems().Num());
			break;
		case ERHQuotaPhase::ShipInbound:
			UE_LOG(LogRedHope, Display, TEXT("  SHIP INBOUND: arrival at t=%.0f (now %.0f)"),
				Sim->GetShipEtaSimSeconds(), Clock->GetSimSecondsTotal());
			break;
		case ERHQuotaPhase::Completed:
			UE_LOG(LogRedHope, Display, TEXT("  SLICE COMPLETE - the Program continues."));
			break;
		default:
			break;
		}
		UE_LOG(LogRedHope, Display, TEXT("  import stock: solar %d, battery %d"),
			Sim->GetImportStock(FName("SolarArray")), Sim->GetImportStock(FName("BatteryBank")));
		if (Sim->GetShaftDepth() > 0 || Sim->GetBoreTargetDepth() > 0)
		{
			FString Carve;
			for (int32 L = -1; L >= -Sim->GetMaxDepth(); --L)
			{
				const int32 Queued = Sim->GetCarveQueued(L);
				const int32 Carved = Sim->GetFloorCarvedCells(L);
				if (Queued > 0 || Carved > 0)
				{
					Carve += FString::Printf(TEXT("  %d: %d carved%s"), L, Carved,
						Queued > 0 ? *FString::Printf(TEXT(" (+%d queued)"), Queued) : TEXT(""));
				}
			}
			UE_LOG(LogRedHope, Display, TEXT("  shaft: floor -%d of ordered -%d%s"),
				Sim->GetShaftDepth(), Sim->GetBoreTargetDepth(), *Carve);
		}
		if (const FRHEventRow* Event = Sim->GetActiveEvent())
		{
			UE_LOG(LogRedHope, Display, TEXT("  EVENT: %s until sol %.1f (severity %.2f; dust factor now %.2f)"),
				*Event->Type.ToString(), Event->StartSol + Event->DurationSols, Event->Severity, Sim->GetDustFactorNow());
		}

		for (const auto& Q : Sim->GetQuotaProgress())
		{
			UE_LOG(LogRedHope, Display, TEXT("  quota %s: %.0f / %.0f kg"),
				*Q.Key.ToString(), Q.Value.Key, Q.Value.Value);
		}
		for (const FRHDepositState& D : Sim->GetDeposits())
		{
			if (D.bDesignated)
			{
				UE_LOG(LogRedHope, Display, TEXT("  dig %s: %.0f kg underground, %.0f kg on pile, %d claims"),
					*D.RowName.ToString(), D.RemainingKg, D.PileKg, D.DigClaims);
			}
		}
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.bUnderConstruction)
			{
				UE_LOG(LogRedHope, Display, TEXT("  site %s #%d: %.0f s remaining"),
					*B.DefName.ToString(), B.Id, B.BuildRemaining_s);
			}
			else if (B.BatchRemaining_h > 0.0)
			{
				UE_LOG(LogRedHope, Display, TEXT("  batch %s #%d: %.2f h remaining"),
					*B.DefName.ToString(), B.Id, B.BatchRemaining_h);
			}
		}
		for (const FRHCommand& C : Sim->GetUplinkQueue())
		{
			UE_LOG(LogRedHope, Display, TEXT("  queued: %s %s, executes at t=%.0f (now %.0f)"),
				*C.Verb.ToString(), *C.Target.ToString(), C.ExecuteAtSimSeconds, Clock->GetSimSecondsTotal());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDig(
	TEXT("RH.Dig"),
	TEXT("RH.Dig <DepositRowName> - transmit a dig designation through the uplink (e.g. RH.Dig Regolith_A)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Dig <DepositRowName>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Dig");
			Cmd.Target = FName(*Args[0]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHLedger(
	TEXT("RH.Ledger"),
	TEXT("RH.Ledger - one parseable line per stock (solids summed over buildings, fluids from the pool, deposits) for paired-run diffs."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		const URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>();
		if (!Sim || !Clock) { return; }

		// Solids: every resource name seen in any hopper/output, summed.
		TSet<FName> SolidNames;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			for (const auto& In : B.InputKg) { SolidNames.Add(In.Key); }
			for (const auto& Out : B.OutputKg) { SolidNames.Add(Out.Key); }
		}
		TArray<FName> Sorted = SolidNames.Array();
		Sorted.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

		UE_LOG(LogRedHope, Display, TEXT("[RH.Ledger] sol=%d t=%.0f battery=%.1f"),
			Clock->GetSol(), Clock->GetSimSecondsTotal(), Sim->GetPower().BatteryWh);
		for (const FName& Name : Sorted)
		{
			UE_LOG(LogRedHope, Display, TEXT("LEDGER solid %s %.2f"), *Name.ToString(), Sim->GetTotalSolid(Name));
		}
		for (const FName& Fluid : { FName("Water"), FName("Oxygen"), FName("Hydrogen"), FName("SpareParts") })
		{
			UE_LOG(LogRedHope, Display, TEXT("LEDGER stock %s %.2f"), *Fluid.ToString(), Sim->GetStock(Fluid));
		}
		for (const FRHDepositState& D : Sim->GetDeposits())
		{
			UE_LOG(LogRedHope, Display, TEXT("LEDGER deposit %s %.2f %.2f"), *D.RowName.ToString(), D.RemainingKg, D.PileKg);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSurvey(
	TEXT("RH.Survey"),
	TEXT("RH.Survey <Xm> <Ym> - transmit a survey order; a scout drives out and reveals hidden deposits in its radius."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Survey <Xm> <Ym>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Survey");
			Cmd.Location = FVector(FCString::Atof(*Args[0]) * 100.f, FCString::Atof(*Args[1]) * 100.f, 0.f);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHWear(
	TEXT("RH.Wear"),
	TEXT("RH.Wear <0-100> - set every robot's wear directly (fleet-crisis test knob, bypasses accrual)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Wear <0-100>"));
			return;
		}
		if (URHAgentSubsystem* Agents = World->GetSubsystem<URHAgentSubsystem>())
		{
			Agents->Debug_SetAllWear(FCString::Atof(*Args[0]));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHFleet(
	TEXT("RH.Fleet"),
	TEXT("RH.Fleet - log every robot: class, position, battery, wear (degrade/halt flags)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHAgentSubsystem* Agents = World->GetSubsystem<URHAgentSubsystem>();
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Agents || !Sim) { return; }
		UE_LOG(LogRedHope, Display, TEXT("[RH.Fleet] wear thresholds: degrade %.0f, halt %.0f | SpareParts %.0f"),
			Sim->GetWearDegradeThreshold(), Sim->GetWearHaltThreshold(), Sim->GetStock(FName("SpareParts")));
		Agents->ForEachRobotState([Sim](FMassEntityHandle Entity, const FVector& PosCm, float Wear)
		{
			const TCHAR* Flag = TEXT("");
			if (Wear >= Sim->GetWearHaltThreshold())
			{
				Flag = TEXT("  HALTED");
			}
			else if (Wear >= Sim->GetWearDegradeThreshold())
			{
				Flag = TEXT("  degraded");
			}
			UE_LOG(LogRedHope, Display, TEXT("  robot %d at (%.0f, %.0f) m: wear %.1f (work rate x%.2f)%s"),
				Entity.Index, PosCm.X / 100.f, PosCm.Y / 100.f, Wear, Sim->GetWearWorkMul(Wear), Flag);
		});
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHRadiation(
	TEXT("RH.Radiation"),
	TEXT("RH.Radiation - log the radiation index per floor (surface to MaxDepth) plus any live solar-flare surface spike."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		const float SurfaceNow = Sim->GetRadiationNow(0);
		const float SurfaceBase = Sim->GetRadiationAtLevel(0);
		UE_LOG(LogRedHope, Display, TEXT("[RH.Radiation] surface index %.3f%s"),
			SurfaceNow,
			SurfaceNow > SurfaceBase + KINDA_SMALL_NUMBER
				? *FString::Printf(TEXT("  (SOLAR FLARE x%.1f)"), SurfaceNow / FMath::Max(SurfaceBase, KINDA_SMALL_NUMBER))
				: TEXT(""));
		for (int32 Level = 0; Level >= -Sim->GetMaxDepth(); --Level)
		{
			UE_LOG(LogRedHope, Display, TEXT("  level %d: radiation %.4f (steady-state, overburden-shielded)"),
				Level, Sim->GetRadiationAtLevel(Level));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHManifest(
	TEXT("RH.Manifest"),
	TEXT("RH.Manifest <ItemRowName> - add an item to the open manifest (e.g. RH.Manifest SolarPack)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Manifest <ItemRowName>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FString Error;
			if (!Sim->AddManifestItem(FName(*Args[0]), Error))
			{
				UE_LOG(LogRedHope, Warning, TEXT("Manifest: %s"), *Error);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHLaunch(
	TEXT("RH.Launch"),
	TEXT("RH.Launch - confirm the manifest and launch the supply ship."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FString Error;
			if (!Sim->LaunchShip(Error))
			{
				UE_LOG(LogRedHope, Warning, TEXT("Launch: %s"), *Error);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHLag(
	TEXT("RH.Lag"),
	TEXT("RH.Lag <SimSeconds> - set uplink order lag (latency-tier test knob)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			Sim->SetOrderLagSeconds(FCString::Atod(*Args[0]));
			UE_LOG(LogRedHope, Display, TEXT("Order lag set to %s sim-s"), *Args[0]);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSave(
	TEXT("RH.Save"),
	TEXT("RH.Save [Slot=quick] - snapshot the colony to Saved/SaveGames/RH_<Slot>.sav."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			const FString Slot = Args.Num() > 0 ? Args[0] : TEXT("quick");
			FString Error;
			if (!Sim->SaveColony(Slot, Error))
			{
				UE_LOG(LogRedHope, Warning, TEXT("Save: %s"), *Error);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHLoad(
	TEXT("RH.Load"),
	TEXT("RH.Load [Slot=quick] - replace the running colony with the snapshot."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			const FString Slot = Args.Num() > 0 ? Args[0] : TEXT("quick");
			FString Error;
			if (!Sim->LoadColony(Slot, Error))
			{
				UE_LOG(LogRedHope, Warning, TEXT("Load: %s"), *Error);
			}
		}
	}));
