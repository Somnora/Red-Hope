// Console commands for scaffold testing and the hardware stress benchmark.
// All are RH.* and development-only conveniences; shipping builds strip them.

#include "CoreMinimal.h"
#include "RedHope.h"
#include "RHAgentVisualizerSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
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
	TEXT("RH.Build <DefName> <Xm> <Ym> - transmit a Build order through the uplink (subject to signal lag + coverage)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 3)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Build <DefName> <Xm> <Ym>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Build");
			Cmd.Target = FName(*Args[0]);
			Cmd.Location = FVector(FCString::Atof(*Args[1]) * 100.f, FCString::Atof(*Args[2]) * 100.f, 0.f);
			Sim->EnqueueCommand(Cmd);
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
