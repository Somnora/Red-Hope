// Console commands for scaffold testing and the hardware stress benchmark.
// All are RH.* and development-only conveniences; shipping builds strip them.

#include "CoreMinimal.h"
#include "RedHope.h"
#include "RHAgentVisualizerSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHPlayerController.h"
#include "RHDefinitionsSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "Misc/CoreDelegates.h"
#include "TimerManager.h"
#include "UnrealClient.h"

static FAutoConsoleCommandWithWorldAndArgs GRHSpawn(
	TEXT("RH.Spawn"),
	TEXT("RH.Spawn <DefName> [Xm] [Ym] [Level=0] - DEBUG: place a finished building instantly (bypasses cost/fabrication). For look-dev and demo dressing."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Spawn <DefName> [Xm] [Ym] [Level]")); return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			const double Xm = Args.Num() > 1 ? FCString::Atod(*Args[1]) : 20.0;
			const double Ym = Args.Num() > 2 ? FCString::Atod(*Args[2]) : 0.0;
			const int32 Level = Args.Num() > 3 ? FCString::Atoi(*Args[3]) : 0;
			Sim->Debug_PlaceInstant(FName(*Args[0]), FVector(Xm * 100.0, Ym * 100.0, 0), Level);
			UE_LOG(LogRedHope, Display, TEXT("Spawned %s at (%.0f, %.0f) m L%d"), *Args[0], Xm, Ym, Level);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSnapshot(
	TEXT("RH.Snapshot"),
	TEXT("RH.Snapshot [DelaySeconds=5] - DEBUG: request a screenshot after a delay (written to Saved/Screenshots). Lets a headless driver verify the look."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const float Delay = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 5.f;
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, []()
		{
			FScreenshotRequest::RequestScreenshot(TEXT("RHSnap"), /*bShowUI*/ false, /*bAddFilenameSuffix*/ true);
			UE_LOG(LogRedHope, Display, TEXT("Snapshot requested"));
		}, Delay, false);
	}));

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

static FAutoConsoleCommandWithWorldAndArgs GRHDesignate(
	TEXT("RH.Designate"),
	TEXT("RH.Designate <Room|None> <Level> <CellIndex> [Count=1] - zone carved cells with a room function (uplink; None clears)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 3)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Designate <Room|None> <Level> <CellIndex> [Count=1]"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			const int32 Count = Args.Num() > 3 ? FMath::Max(1, FCString::Atoi(*Args[3])) : 1;
			for (int32 i = 0; i < Count; ++i)
			{
				FRHCommand Cmd;
				Cmd.Verb = FName("Designate");
				Cmd.Target = (Args[0] == TEXT("None")) ? NAME_None : FName(*Args[0]);
				Cmd.Level = FCString::Atoi(*Args[1]);
				Cmd.Value = FCString::Atoi(*Args[2]) + i;
				Sim->EnqueueCommand(Cmd);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHRooms(
	TEXT("RH.Rooms"),
	TEXT("RH.Rooms - log every floor's room designations by cell index."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		for (int32 L = -1; L >= -Sim->GetMaxDepth(); --L)
		{
			const int32 Carved = Sim->GetFloorCarvedCells(L);
			if (Carved == 0)
			{
				continue;
			}
			FString Line = FString::Printf(TEXT("[RH.Rooms] floor %d (%d cells%s):"), L, Carved,
				Sim->IsFloorRated(L) ? TEXT(", LIVABLE") : TEXT(""));
			for (int32 i = 0; i < Carved; ++i)
			{
				const FName Room = Sim->GetRoomAt(L, i);
				const FIntPoint P = URHSimWorldSubsystem::SpiralCell(i);
				Line += FString::Printf(TEXT("\n  cell %d (%+d,%+d): %s"), i, P.X, P.Y,
					Room.IsNone() ? TEXT("-") : *Room.ToString());
			}
			UE_LOG(LogRedHope, Display, TEXT("%s"), *Line);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHHope(
	TEXT("RH.Hope"),
	TEXT("RH.Hope - log the colony Hope index with its full component breakdown."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		const URHSimWorldSubsystem::FRHHopeBreakdown H = Sim->GetColonyHope();
		UE_LOG(LogRedHope, Display,
			TEXT("[RH.Hope] instant %.1f | smoothed %.1f  %s  tempo %.0f%% | base %.0f housing +%.1f rooms +%.1f jobs +%.1f (%d seats) milestones +%.1f comforts +%.1f | adjacency -%.1f (%d pairs) unsupported -%.1f water -%.1f (potability %.0f%%)"),
			H.Total, Sim->GetHopeSmoothed(), Sim->GetHopeBandName(), Sim->GetHumanWorkTempo() * 100.0,
			H.Base, H.Housing, H.Rooms, H.Jobs, H.FilledSeats, H.Milestones, H.Comforts,
			H.AdjacencyPenalty, H.OffendedPairs, H.UnsupportedPenalty, H.WaterPenalty, Sim->GetWaterPotability() * 100.0);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHEarth(
	TEXT("RH.Earth"),
	TEXT("RH.Earth [tension <delta>] [identity <delta>] - log or nudge the Earth-tension / identity-axis state (identity + = Martian, - = Earth-aligned)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		for (int32 i = 0; i + 1 < Args.Num(); i += 2)
		{
			const double Delta = FCString::Atod(*Args[i + 1]);
			if (Args[i] == TEXT("tension")) { Sim->Debug_AddTension(Delta); }
			else if (Args[i] == TEXT("identity")) { Sim->Debug_ShiftIdentity(Delta); }
		}
		UE_LOG(LogRedHope, Display, TEXT("[RH.Earth] tension %.0f | identity %+.0f | demand %s | requisition x%.2f"),
			Sim->GetEarthTension(), Sim->GetIdentityAxis(),
			Sim->IsEarthDemandPending() ? TEXT("PENDING") : TEXT("none"), Sim->GetRequisitionMultiplier());
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSolidarity(
	TEXT("RH.Solidarity"),
	TEXT("RH.Solidarity <comply|defy> - answer Earth's pending demand (comply = cut trade / defy = keep faith with the settlements). Rides the uplink."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Solidarity <comply|defy>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Solidarity");
			Cmd.Target = Args[0].Equals(TEXT("comply"), ESearchCase::IgnoreCase) ? FName("Comply") : FName("Defy");
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHConvoy(
	TEXT("RH.Convoy"),
	TEXT("RH.Convoy <RivalName> - dispatch the rover trade convoy to a rival (uplink; commits Hydrogen + SpareParts + your export lot at departure)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Convoy <RivalName>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Convoy");
			Cmd.Target = FName(*Args[0]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHCovert(
	TEXT("RH.Covert"),
	TEXT("RH.Covert <RivalName> - run a covert requisition against a rival (steal export goods; a night op is less likely to be caught). Rides the uplink."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Covert <RivalName>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Covert");
			Cmd.Target = FName(*Args[0]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHInjectRivals(
	TEXT("RH.InjectRivals"),
	TEXT("RH.InjectRivals - DEBUG: load the canonical rival settlements (Zarya active, Meridian dormant) in-memory, mirroring RH_Rivals.csv - the demo bridge until DT_Rivals is imported."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (!Defs) { return; }
		FRHRivalRow Z;
		Z.DisplayName = TEXT("Zarya Station"); Z.Nation = TEXT("Zarya Consortium");
		Z.DistanceKm = 120.f; Z.ExportLot = TEXT("Ice:150"); Z.ImportLot = TEXT("Struct:100");
		Z.RelationStart = 40.f; Z.SliceActive = true;
		Defs->Debug_InjectRival(FName("Zarya"), Z);
		FRHRivalRow M;
		M.DisplayName = TEXT("Meridian Base"); M.Nation = TEXT("Meridian Compact");
		M.DistanceKm = 200.f; M.ExportLot = TEXT("SpareParts:4"); M.ImportLot = TEXT("Food:60");
		M.RelationStart = 30.f; M.SliceActive = false; // dormant until scouted
		Defs->Debug_InjectRival(FName("Meridian"), M);
		UE_LOG(LogRedHope, Display, TEXT("Rivals injected (in-memory): Zarya Station active, Meridian Base dormant. The neighbors exist; RH.Trade to see them."));
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHCrisis(
	TEXT("RH.Crisis"),
	TEXT("RH.Crisis [trigger] - log the crisis + endings state, or force the alignment-gated selector to fire a crisis now."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		if (Args.Num() > 0 && Args[0] == TEXT("trigger"))
		{
			Sim->Debug_TriggerCrisis();
		}
		const auto E = Sim->GetProjectedEnding();
		UE_LOG(LogRedHope, Display, TEXT("[RH.Crisis] active: %s%s | identity %+.0f martian/earth | humanNature %+.0f evolved/destructive | projected ending: %s"),
			Sim->GetActiveCrisisName(),
			Sim->GetActiveCrisis() != URHSimWorldSubsystem::ERHCrisis::None ? *FString::Printf(TEXT(" (%.1f sols left)"), Sim->GetCrisisRemaining()) : TEXT(""),
			Sim->GetIdentityAxis(), Sim->GetHumanNatureAxis(), Sim->GetEndingName(E));
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHPacify(
	TEXT("RH.Pacify"),
	TEXT("RH.Pacify <RivalName> - spend Influence to lift an Earth-ordered embargo (the colony sees reason). Rides the uplink."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Pacify <RivalName>")); return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd; Cmd.Verb = FName("Pacify"); Cmd.Target = FName(*Args[0]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHLaunder(
	TEXT("RH.Launder"),
	TEXT("RH.Launder <RivalName> - make amends to a rival (return goods to defuse hidden tension). Rides the uplink."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Launder <RivalName>")); return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd; Cmd.Verb = FName("Launder"); Cmd.Target = FName(*Args[0]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSabotage(
	TEXT("RH.Sabotage"),
	TEXT("RH.Sabotage <RivalName> - covert blackout: disrupt a rival's production/trade for a period (detection-gated; a hostile act). Rides the uplink."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Sabotage <RivalName>")); return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd; Cmd.Verb = FName("Sabotage"); Cmd.Target = FName(*Args[0]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHTrade(
	TEXT("RH.Trade"),
	TEXT("RH.Trade - log the trade state: rivals, relations, and the convoy's status."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (!Sim || !Defs) { return; }
		const FName Out = Sim->GetConvoyRival();
		UE_LOG(LogRedHope, Display, TEXT("[RH.Trade] convoy: %s%s"),
			Out.IsNone() ? TEXT("idle") : *Out.ToString(),
			Out.IsNone() ? TEXT("") : *FString::Printf(TEXT(" (%s, %.0f%%)"),
				Sim->IsConvoyReturning() ? TEXT("returning") : TEXT("outbound"), Sim->GetConvoyProgress() * 100.0));
		UE_LOG(LogRedHope, Display, TEXT("  human nature %+.0f (Evolved/Diplomatic .. Destructive/Predatory) | %s"),
			Sim->GetHumanNatureAxis(), Sim->IsNight() ? TEXT("NIGHT (covert ops safer)") : TEXT("day"));
		Defs->ForEachRivalRow([&](FName Name, const FRHRivalRow& Row)
		{
			if (!Sim->IsRivalAvailable(Name)) { return; } // hide undiscovered dormant settlements
			const double Sab = Sim->GetSabotageRemaining(Name);
			FString Flags;
			if (Sim->IsRivalDiscovered(Name)) { Flags += TEXT(" [scouted]"); }
			if (Sab > 0.0) { Flags += FString::Printf(TEXT(" [SABOTAGED %.1f]"), Sab); }
			if (Sim->HasDefected(Name)) { Flags += TEXT(" [DEFECTED]"); }
			else if (Sim->IsEmbargoed(Name)) { Flags += FString::Printf(TEXT(" [EMBARGO %.1f]"), Sim->GetEmbargoGrace(Name)); }
			UE_LOG(LogRedHope, Display, TEXT("  %s (%s): public relation %.0f | HIDDEN tension %.0f | give %s -> get %s | %.0f km%s"),
				*Row.DisplayName, *Row.Nation, Sim->GetRivalRelation(Name), Sim->GetHiddenTension(Name),
				*Row.ImportLot, *Row.ExportLot, Row.DistanceKm, *Flags);
		});
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDiscoveries(
	TEXT("RH.Discoveries"),
	TEXT("RH.Discoveries - log the research state: what the colony has uncovered, what's next, and the live progress."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (!Sim || !Defs) { return; }
		UE_LOG(LogRedHope, Display, TEXT("[RH.Discoveries] found %d | accruing: %s | next: %s (%.0f%%)"),
			Sim->GetDiscoveryLog().Num(),
			Sim->IsResearchAccruing() ? TEXT("yes") : TEXT("no (needs flourishing Hope + a staffed Lab)"),
			Sim->GetNextDiscovery().IsNone() ? TEXT("-") : *Sim->GetNextDiscovery().ToString(),
			Sim->GetDiscoveryProgress() * 100.0);
		for (const FName& Found : Sim->GetDiscoveryLog())
		{
			const FRHDiscoveryRow* Row = Defs->GetDiscovery(Found);
			UE_LOG(LogRedHope, Display, TEXT("  %s%s"),
				Row ? *Row->DisplayName : *Found.ToString(),
				(Row && Row->HopeBonus > 0.f) ? *FString::Printf(TEXT("  (+%.0f Hope)"), Row->HopeBonus) : TEXT(""));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHGarden(
	TEXT("RH.Garden"),
	TEXT("RH.Garden - log the garden: planted/producing cells, soil/seed/water stocks, net food per sol."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		const double YieldPerSol = Sim->GetProducingCellCount() * Sim->GetGardenFoodKgPerSolPerCell();
		const double EatPerSol = Sim->GetPopulation() * Sim->GetColonistFoodKgPerSol();
		UE_LOG(LogRedHope, Display,
			TEXT("[RH.Garden] planted %d cell(s), producing %d | yield %.1f kg/sol vs crew draw %.1f kg/sol | Soil %.0f Seeds %.0f Water %.0f Food %.0f"),
			Sim->GetPlantedCellCount(), Sim->GetProducingCellCount(), YieldPerSol, EatPerSol,
			Sim->GetStock(FName("Soil")), Sim->GetStock(FName("Seeds")),
			Sim->GetStock(FName("Water")), Sim->GetStock(FName("Food")));
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHActivateRoom(
	TEXT("RH.ActivateRoom"),
	TEXT("RH.ActivateRoom <RowName> - DEBUG: flip a room row slice-active in the loaded table (test knob until the DT_Rooms sync lands)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { return; }
		const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (FRHRoomRow* Row = const_cast<FRHRoomRow*>(Defs ? Defs->GetRoom(FName(*Args[0])) : nullptr))
		{
			Row->SliceActive = true;
			UE_LOG(LogRedHope, Display, TEXT("Room row '%s' slice-active (in-memory)"), *Args[0]);
		}
		else
		{
			UE_LOG(LogRedHope, Warning, TEXT("RH.ActivateRoom: no room row '%s'"), *Args[0]);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHActivateQuota(
	TEXT("RH.ActivateQuota"),
	TEXT("RH.ActivateQuota <RowName> - DEBUG: flip a quota row slice-active in the loaded table (test knob until the DT_Quotas sync lands)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { return; }
		URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (Defs && Defs->Debug_ActivateQuota(FName(*Args[0])))
		{
			UE_LOG(LogRedHope, Display, TEXT("Quota row '%s' slice-active (in-memory)"), *Args[0]);
		}
		else
		{
			UE_LOG(LogRedHope, Warning, TEXT("RH.ActivateQuota: no quota row '%s'"), *Args[0]);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHActivateCrop(
	TEXT("RH.ActivateCrop"),
	TEXT("RH.ActivateCrop <RowName|all> - flip a dormant crop row live (the per-gate director flip; in-memory)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { return; }
		URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (!Defs) { return; }
		if (Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			int32 Flipped = 0;
			TArray<FName> Names;
			Defs->ForEachCropRow([&Names](FName Name, const FRHCropRow&) { Names.Add(Name); });
			for (const FName& Name : Names) { Flipped += Defs->ActivateCropRow(Name) ? 1 : 0; }
			UE_LOG(LogRedHope, Display, TEXT("Crops live: %d rows slice-active (in-memory)"), Flipped);
		}
		else if (Defs->ActivateCropRow(FName(*Args[0])))
		{
			UE_LOG(LogRedHope, Display, TEXT("Crop row '%s' slice-active (in-memory)"), *Args[0]);
		}
		else
		{
			UE_LOG(LogRedHope, Warning, TEXT("RH.ActivateCrop: no crop row '%s'"), *Args[0]);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHClimate(
	TEXT("RH.Climate"),
	TEXT("RH.Climate <Level> <Temperate|Humid|Arid> - set a floor's greenhouse climate (needs a HumidityRegulator online for non-Temperate to take effect)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Climate <Level> <Temperate|Humid|Arid>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FString Err;
			if (!Sim->SetFloorClimate(FCString::Atoi(*Args[0]), FName(*Args[1]), Err))
			{
				UE_LOG(LogRedHope, Warning, TEXT("RH.Climate: %s"), *Err);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDuct(
	TEXT("RH.Duct"),
	TEXT("RH.Duct <Level> - duct a garden floor into the colony air loop (crops draw crew CO2, emit O2)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Duct <Level>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FString Err;
			if (!Sim->DesignateDuct(FCString::Atoi(*Args[0]), Err))
			{
				UE_LOG(LogRedHope, Warning, TEXT("RH.Duct: %s"), *Err);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHAddSolid(
	TEXT("RH.AddSolid"),
	TEXT("RH.AddSolid <DefName> <Resource> <Kg> - DEBUG: drop solid stock into the first completed building of a def."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 3)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.AddSolid <DefName> <Resource> <Kg>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			Sim->Debug_AddSolid(FName(*Args[0]), FName(*Args[1]), FCString::Atod(*Args[2]));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHPower(
	TEXT("RH.Power"),
	TEXT("RH.Power <BuildingId> <0|1> - switch a structure off/on (storm discipline: 0 = breaker off, zero draw, batches frozen)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Power <BuildingId> <0|1>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			if (!Sim->SetManualPower(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]) != 0))
			{
				UE_LOG(LogRedHope, Warning, TEXT("RH.Power: no completed building #%s"), *Args[0]);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHHoldFleet(
	TEXT("RH.HoldFleet"),
	TEXT("RH.HoldFleet <0|1> - 1: robots finish current tasks then claim nothing new (ride out a storm on stored charge); 0: release."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			Sim->SetFleetHold(FCString::Atoi(*Args[0]) != 0);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHAddStock(
	TEXT("RH.AddStock"),
	TEXT("RH.AddStock <Resource> <Kg> - DEBUG: add to the colony-wide pool stock (fluids/gases: Oxygen, Water, Hydrogen...)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2) { return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			Sim->AddStock(FName(*Args[0]), FCString::Atod(*Args[1]));
			UE_LOG(LogRedHope, Display, TEXT("Pool stock %s: %.0f kg"), *Args[0], Sim->GetStock(FName(*Args[0])));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSetCirculates(
	TEXT("RH.SetCirculates"),
	TEXT("RH.SetCirculates <DefName> <0|1> - DEBUG: set a building def's CirculatesAir in the loaded table (test knob until the AirFilter DT row lands)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2) { return; }
		const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (FRHBuildingRow* Row = const_cast<FRHBuildingRow*>(Defs ? Defs->GetBuilding(FName(*Args[0])) : nullptr))
		{
			Row->CirculatesAir = FCString::Atoi(*Args[1]) != 0;
			UE_LOG(LogRedHope, Display, TEXT("'%s' CirculatesAir = %s (in-memory)"), *Args[0], *Args[1]);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHActivateRecipe(
	TEXT("RH.ActivateRecipe"),
	TEXT("RH.ActivateRecipe <RowName> - DEBUG: flip a recipe row slice-active in the loaded table (test knob; the DT asset is untouched)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { return; }
		const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		// Dev-only knob: mutating the in-memory row is the point (the asset on
		// disk is untouched); const_cast is confined to cheats.
		if (FRHRecipeRow* Row = const_cast<FRHRecipeRow*>(Defs ? Defs->GetRecipe(FName(*Args[0])) : nullptr))
		{
			Row->SliceActive = true;
			UE_LOG(LogRedHope, Display, TEXT("Recipe '%s' activated (in-memory)"), *Args[0]);
		}
		else
		{
			UE_LOG(LogRedHope, Warning, TEXT("RH.ActivateRecipe: no recipe row '%s'"), *Args[0]);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSetShieldTax(
	TEXT("RH.SetShieldTax"),
	TEXT("RH.SetShieldTax <DefName> <Kg> - DEBUG: set a building def's SurfaceShielding_kg in the loaded table (test knob)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2) { return; }
		const URHDefinitionsSubsystem* Defs = World->GetSubsystem<URHDefinitionsSubsystem>();
		if (FRHBuildingRow* Row = const_cast<FRHBuildingRow*>(Defs ? Defs->GetBuilding(FName(*Args[0])) : nullptr))
		{
			Row->SurfaceShielding_kg = FCString::Atof(*Args[1]);
			UE_LOG(LogRedHope, Display, TEXT("'%s' SurfaceShielding_kg = %s (in-memory)"), *Args[0], *Args[1]);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHCrew(
	TEXT("RH.Crew"),
	TEXT("RH.Crew - log the colonist roster: name, home floor, support state, evacuation timer."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		UE_LOG(LogRedHope, Display, TEXT("[RH.Crew] population %d | housing %d beds (%d free) | Food %.0f kg"),
			Sim->GetPopulation(), Sim->GetHousingCapacity(), Sim->GetFreeHousing(), Sim->GetStock(FName("Food")));
		for (const FRHColonist& C : Sim->GetColonists())
		{
			const double EvacSols = C.UnsupportedSimSeconds / URHSimClockSubsystem::SolLengthSimSeconds;
			const FName Job = Sim->GetColonistJob(C.Id);
			UE_LOG(LogRedHope, Display, TEXT("  %-12s floor %d  %-12s %s%s"),
				*C.Name, C.HomeLevel,
				Job.IsNone() ? TEXT("(no post)") : *Job.ToString(),
				C.bSupported ? TEXT("supported") : TEXT("UNSUPPORTED"),
				C.bSupported ? TEXT("") : *FString::Printf(TEXT("  (evac in %.1f sols)"), Sim->GetColonistEvacSols() - EvacSols));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHAddColonists(
	TEXT("RH.AddColonists"),
	TEXT("RH.AddColonists <N> - DEBUG: house N colonists into certified housing (respects the capacity gate)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			const int32 Housed = Sim->Debug_AddColonists(FCString::Atoi(*Args[0]));
			UE_LOG(LogRedHope, Display, TEXT("Housed %d colonist(s); population %d"), Housed, Sim->GetPopulation());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDeliver(
	TEXT("RH.Deliver"),
	TEXT("RH.Deliver <ItemName> - DEBUG: land a manifest item's cargo effect immediately (e.g. CrewPod; skips the ship transit)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			Sim->Debug_DeliverCargo(FName(*Args[0]));
			UE_LOG(LogRedHope, Display, TEXT("Delivered '%s'; population %d, free beds %d"),
				*Args[0], Sim->GetPopulation(), Sim->GetFreeHousing());
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHHabitat(
	TEXT("RH.Habitat"),
	TEXT("RH.Habitat - log every subsurface floor's habitability chain: carved, O2 fill/required, circulation, rating."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		UE_LOG(LogRedHope, Display, TEXT("[RH.Habitat] shaft -%d | colony O2 %.0f kg | habitat minimum %d cells"),
			Sim->GetShaftDepth(), Sim->GetStock(FName("Oxygen")), Sim->GetMinLivableCells());
		for (int32 L = -1; L >= -Sim->GetMaxDepth(); --L)
		{
			const int32 Cells = Sim->GetFloorCarvedCells(L);
			if (Cells == 0 && !Sim->IsLevelConnected(L)) { continue; }
			const TCHAR* Status = Sim->IsFloorRated(L) ? TEXT("LIVABLE")
				: (Sim->IsFloorSealedButSmall(L) ? TEXT("sealed - too small") : TEXT("suit-only"));
			UE_LOG(LogRedHope, Display, TEXT("  floor %d: %d/%d cell(s) | O2 %.0f / %.0f kg | circulation %s | %s"),
				L, Cells, Sim->GetMinLivableCells(), Sim->GetFloorO2Kg(L), Sim->GetFloorO2RequiredKg(L),
				Sim->IsFloorCirculated(L) ? TEXT("ON") : TEXT("off"), Status);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSelect(
	TEXT("RH.Select"),
	TEXT("RH.Select <BuildingId> - raise the in-world action card for a building by id (0 = deselect); handy when the gray-box is fiddly to click."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Select <BuildingId>"));
			return;
		}
		if (ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController()))
		{
			const int32 Id = FCString::Atoi(*Args[0]);
			PC->Debug_SelectBuilding(Id);
			UE_LOG(LogRedHope, Display, TEXT("Selected building #%d (action card raised)"), Id);
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

static FAutoConsoleCommandWithWorldAndArgs GRHDemo(
	TEXT("RH.Demo"),
	TEXT("RH.Demo - the everything button: bore a floor, carve it, place power + life-support, "
	     "zone one of every room, house a crew, and ride down. Then speed up (RH.SetSpeed 3) to watch it live."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim)
		{
			return;
		}
		FString R;
		// Dig and carve floor -1 (instant, bypasses the Borer for the demo).
		Sim->ExtendShaft(1, FVector(1000.f, 1000.f, 0.f));
		Sim->ExcavateFloor(-1, 8, R);
		// Power at the surface, life-support on the floor, and stocked so the
		// crew can breathe/eat/drink the moment the floor rates.
		Sim->Debug_PlaceInstant(FName("SolarArray"), FVector(3500.f, 1000.f, 0.f));
		Sim->Debug_PlaceInstant(FName("BatteryBank"), FVector(1000.f, 3500.f, 0.f));
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(1000.f, 1500.f, 0.f), -1);
		Sim->AddStock(FName("Oxygen"), 1200.0);
		Sim->AddStock(FName("Food"), 400.0);
		Sim->AddStock(FName("Water"), 400.0);
		// One of every active room, spread across the 8 carved cells.
		const TCHAR* Rooms[8] = {
			TEXT("LivingQuarters"), TEXT("LivingQuarters"), TEXT("Workstation"), TEXT("Lab"),
			TEXT("Dining"), TEXT("Cooking"), TEXT("Hallway"), TEXT("Garden")
		};
		for (int32 i = 0; i < 8; ++i)
		{
			Sim->DesignateRoom(-1, i, FName(Rooms[i]), R);
		}
		// Certify the floor NOW (fills O2 + rates it) so the crew move in this
		// frame - otherwise Debug_AddColonists finds no rated bed and adds none.
		Sim->Debug_ForceRateFloor(-1);
		// The crew (now the floor is rated, they house immediately).
		Sim->Debug_AddColonists(4);
		// Ride the elevator down to look at it.
		if (ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController()))
		{
			PC->SetActiveLevel(-1);
		}
		UE_LOG(LogRedHope, Display, TEXT("[RH.Demo] floor -1: 8 cells carved + zoned, power + AirFilter placed, 4 crew inbound. "
			"Run RH.SetSpeed 3 to let it certify and the crew move in."));
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
