// Console commands for scaffold testing and the hardware stress benchmark.
// All are RH.* and development-only conveniences; shipping builds strip them.

#include "CoreMinimal.h"
#include "RedHope.h"
#include "RHAgentVisualizerSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHPlayerController.h"
#include "RHStrategyPawn.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHCrewVisualizerSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHDistrictVisualizerSubsystem.h"
#include "RHFrameStats.h"
#include "RHMarsTerrain.h"
#include "RHGrowthVisualizerSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "RHTrafficVisualizerSubsystem.h"
#include "Data/RHRows.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformMisc.h"
#include "Containers/Ticker.h"
#include "Misc/CoreDelegates.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "TimerManager.h"
#include "UnrealClient.h"

static FAutoConsoleCommandWithWorldAndArgs GRHSpawn(
	TEXT("RH.Spawn"),
	TEXT("RH.Spawn <DefName> [Xm] [Ym] [Level=0] [SizeX SizeY] - DEBUG: place a finished building instantly (bypasses cost/fabrication). Size = beds per side for a drag-sized def (the greenhouse dome). For look-dev and demo dressing."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1) { UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Spawn <DefName> [Xm] [Ym] [Level]")); return; }
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			const double Xm = Args.Num() > 1 ? FCString::Atod(*Args[1]) : 20.0;
			const double Ym = Args.Num() > 2 ? FCString::Atod(*Args[2]) : 0.0;
			const int32 Level = Args.Num() > 3 ? FCString::Atoi(*Args[3]) : 0;
			const int32 SX = Args.Num() > 5 ? FCString::Atoi(*Args[4]) : 1;
			const int32 SY = Args.Num() > 5 ? FCString::Atoi(*Args[5]) : 1;
			Sim->Debug_PlaceInstant(FName(*Args[0]), FVector(Xm * 100.0, Ym * 100.0, 0), Level, SX, SY);
			UE_LOG(LogRedHope, Display, TEXT("Spawned %s at (%.0f, %.0f) m L%d"), *Args[0], Xm, Ym, Level);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSnapshot(
	TEXT("RH.Snapshot"),
	TEXT("RH.Snapshot [DelaySeconds=5] [ui] - DEBUG: request a screenshot after a delay (written to Saved/Screenshots). Lets a headless driver verify the look; 'ui' keeps the HUD/deck in the frame."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const float Delay = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 5.f;
		const bool bUI = Args.Num() > 1 && Args[1].Equals(TEXT("ui"), ESearchCase::IgnoreCase);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, [bUI]()
		{
			FScreenshotRequest::RequestScreenshot(TEXT("RHSnap"), /*bShowUI*/ bUI, /*bAddFilenameSuffix*/ true);
			UE_LOG(LogRedHope, Display, TEXT("Snapshot requested"));
		}, Delay, false);
	}));

// ---------------------------------------------------------------------------
// Scripted visual capture (QA contact sheets). The strategy camera is otherwise
// mouse-only, so a headless driver could never frame anything: every shot came
// out at the default 215 m opening register, which is far too distant to judge
// a mesh or a paint job. These four make a boot reproducible end to end.
// ---------------------------------------------------------------------------

namespace
{
	ARHStrategyPawn* FindStrategyPawn(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC ? Cast<ARHStrategyPawn>(PC->GetPawn()) : nullptr;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GRHCam(
	TEXT("RH.Cam"),
	TEXT("RH.Cam <Xm> <Ym> <ZoomT 0-1> [PitchOffsetDeg] [YawDeg] - place the strategy camera exactly. ZoomT 0 = 25 m, 0.45 = 215 m (default opening), 1 = 3000 m. Snaps with no easing so a timed screenshot is repeatable."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 3)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Cam <Xm> <Ym> <ZoomT 0-1> [PitchOffsetDeg] [YawDeg]"));
			return;
		}
		ARHStrategyPawn* Pawn = FindStrategyPawn(World);
		if (!Pawn)
		{
			UE_LOG(LogRedHope, Error, TEXT("RH.Cam: no strategy pawn possessed"));
			return;
		}
		const float Xm = FCString::Atof(*Args[0]);
		const float Ym = FCString::Atof(*Args[1]);
		const float ZoomT = FCString::Atof(*Args[2]);
		const float Pitch = Args.Num() > 3 ? FCString::Atof(*Args[3]) : 0.f;
		const float Yaw = Args.Num() > 4 ? FCString::Atof(*Args[4]) : 0.f;
		Pawn->SetCameraPose(FVector2D(Xm * 100.f, Ym * 100.f), ZoomT, Pitch, Yaw);
		UE_LOG(LogRedHope, Display, TEXT("[RH.Cam] focus (%.0f, %.0f) m | zoomT %.2f = %.0f m out | pitch offset %.0f | yaw %.0f"),
			Xm, Ym, ZoomT, Pawn->GetViewDistanceCm() / 100.f, Pitch, Yaw);
	}));

// W1: where may an outpost deposit stand? The height field is readable (the scenery's peaks and mesas are
// meshes and are NOT in it - only a capture shows those), so this reports what the field says over the
// 60 m scout disc: the ground height at the centre and the largest rise over it.
static FAutoConsoleCommandWithWorldAndArgs GRHTerrainProbe(
	TEXT("RH.TerrainProbe"),
	TEXT("RH.TerrainProbe <Xm> <Ym> [RadiusM=60] - READ-ONLY: the scenery height field at a point and its largest rise and fall over the disc (sampled on 3 rings). Peaks and mesas are meshes, not height: capture the site too."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.TerrainProbe <Xm> <Ym> [RadiusM=60]"));
			return;
		}
		const double Xcm = FCString::Atod(*Args[0]) * 100.0, Ycm = FCString::Atod(*Args[1]) * 100.0;
		const double Rcm = (Args.Num() > 2 ? FCString::Atod(*Args[2]) : 60.0) * 100.0;
		const float Centre = RHMarsTerrain::GroundZCm(Xcm, Ycm);
		float Lo = Centre, Hi = Centre, DrawnLo = RHMarsTerrain::SceneryGroundZCm(Xcm, Ycm), DrawnHi = DrawnLo;
		for (int32 Ring = 1; Ring <= 3; ++Ring)
		{
			for (int32 K = 0; K < 16; ++K)
			{
				const double A = K * 2.0 * PI / 16.0, R = Rcm * Ring / 3.0;
				const float G = RHMarsTerrain::GroundZCm(Xcm + FMath::Cos(A) * R, Ycm + FMath::Sin(A) * R);
				const float Dr = RHMarsTerrain::SceneryGroundZCm(Xcm + FMath::Cos(A) * R, Ycm + FMath::Sin(A) * R);
				Lo = FMath::Min(Lo, G); Hi = FMath::Max(Hi, G);
				DrawnLo = FMath::Min(DrawnLo, Dr); DrawnHi = FMath::Max(DrawnHi, Dr);
			}
		}
		UE_LOG(LogRedHope, Display, TEXT("[RH.TerrainProbe] (%.0f, %.0f) m r %.0f m: field z %.2f m at the centre, %.2f..%.2f m over the disc (rise %.2f m, spread %.2f m); drawn lattice %.2f..%.2f m (spread %.2f m); nearest peak or mesa base %.0f m away (negative = inside)"),
			Xcm / 100.0, Ycm / 100.0, Rcm / 100.0, Centre / 100.f, Lo / 100.f, Hi / 100.f, (Hi - Centre) / 100.f, (Hi - Lo) / 100.f, DrawnLo / 100.f, DrawnHi / 100.f, (DrawnHi - DrawnLo) / 100.f,
			RHMarsTerrain::SceneryClearanceCm(Xcm, Ycm) / 100.f);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHCamRobot(
	TEXT("RH.CamRobot"),
	TEXT("RH.CamRobot [ZoomT=0.04] [YawDeg=0] [PitchOffsetDeg=0] - frame the robot on a road trip (else the first robot) where it stands now. Robots move: chain it with RH.After to follow one."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARHStrategyPawn* Pawn = FindStrategyPawn(World);
		const URHAgentSubsystem* Agents = World ? World->GetSubsystem<URHAgentSubsystem>() : nullptr;
		const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Pawn || !Agents || !Sim)
		{
			return;
		}
		const float ZoomT = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.04f;
		const float Yaw = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.f;
		const float Pitch = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.f;
		FVector Best = FVector::ZeroVector;
		bool bFound = false, bOnRoad = false;
		Agents->ForEachRobotState([&](FMassEntityHandle Entity, const FVector& PosCm, float)
		{
			const FRHRobotRoadPath* Road = Sim->GetRobotRoadPath(Entity);
			const bool bRoad = Road && Road->IsRoadTrip();
			if (!bFound || (bRoad && !bOnRoad))
			{
				Best = PosCm;
				bFound = true;
				bOnRoad = bRoad;
			}
		});
		if (bFound)
		{
			Pawn->SetCameraPose(FVector2D(Best.X, Best.Y), ZoomT, Pitch, Yaw);
			UE_LOG(LogRedHope, Display, TEXT("[RH.CamRobot] framed a robot at (%.0f, %.0f) m%s"), Best.X / 100.f, Best.Y / 100.f, bOnRoad ? TEXT(" on a road trip") : TEXT(""));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHCamCrew(
	TEXT("RH.CamCrew"),
	TEXT("RH.CamCrew [Index=0] [ZoomT=0.05] [YawDeg=0] - frame the Nth crew figure where it currently stands. Walkers move, so a fixed RH.Cam pose can miss them entirely."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		ARHStrategyPawn* Pawn = FindStrategyPawn(World);
		const URHCrewVisualizerSubsystem* Crew = World->GetSubsystem<URHCrewVisualizerSubsystem>();
		if (!Pawn || !Crew)
		{
			UE_LOG(LogRedHope, Error, TEXT("RH.CamCrew: no strategy pawn or crew visualizer"));
			return;
		}
		const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		const float ZoomT = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.05f;
		const float Yaw = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 0.f;
		FVector At = FVector::ZeroVector;
		if (!Crew->Debug_GetCrewLocation(Index, At))
		{
			UE_LOG(LogRedHope, Warning, TEXT("RH.CamCrew: no crew figure at index %d"), Index);
			return;
		}
		Pawn->SetCameraPose(FVector2D(At.X, At.Y), ZoomT, 0.f, Yaw);
		UE_LOG(LogRedHope, Display, TEXT("[RH.CamCrew] crew %d at (%.0f, %.0f) m | zoomT %.2f = %.0f m out"),
			Index, At.X / 100.f, At.Y / 100.f, ZoomT, Pawn->GetViewDistanceCm() / 100.f);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHPulse(
	TEXT("RH.Pulse"),
	TEXT("RH.Pulse <Scale> - scale every machine's ambient emissive throb against its authored depth (0 = fully still, 1 = as authored, 2 = double). Absolute, so repeated calls do not compound."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Pulse <Scale>"));
			return;
		}
		if (URHColonyVisualizerSubsystem* Viz = World->GetSubsystem<URHColonyVisualizerSubsystem>())
		{
			Viz->Debug_SetPulseScale(FCString::Atof(*Args[0]));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHQuit(
	TEXT("RH.Quit"),
	TEXT("RH.Quit [DelaySeconds=0] - request a clean shutdown after a delay, so a capture driver can end a run without SIGKILL truncating the log or a screenshot mid-write."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const float Delay = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f;
		if (Delay <= 0.f)
		{
			UE_LOG(LogRedHope, Display, TEXT("[RH.Quit] exiting"));
			FPlatformMisc::RequestExit(false, TEXT("RH.Quit"));
			return;
		}
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, []()
		{
			UE_LOG(LogRedHope, Display, TEXT("[RH.Quit] exiting"));
			FPlatformMisc::RequestExit(false, TEXT("RH.Quit"));
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
	TEXT("RH.Build <DefName> <Xm> <Ym> [Level=0] [SizeX SizeY] - transmit a Build order through the uplink (Level<0 needs the shaft bored to that depth; size = beds per side for the greenhouse dome)."),
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
			if (Args.Num() > 5)
			{
				Cmd.Value = URHSimWorldSubsystem::EncodeBuildSize(FCString::Atoi(*Args[4]), FCString::Atoi(*Args[5]));
			}
			Sim->EnqueueCommand(Cmd);
		}
	}));

// D2: the optional trailing [shaft] argument of RH.Bore / RH.Excavate / RH.ExcavateNow. 0 (or none) is shaft 0 (the order's Target stays
// empty, as every order before D2); k > 0 is Target "Shaft<k>".
static FName RHShaftTarget(const TArray<FString>& Args, int32 At)
{
	const int32 K = Args.Num() > At ? FCString::Atoi(*Args[At]) : 0;
	return K > 0 ? FName(*FString::Printf(TEXT("Shaft%d"), K)) : FName(NAME_None);
}

static FAutoConsoleCommandWithWorldAndArgs GRHBore(
	TEXT("RH.Bore"),
	TEXT("RH.Bore <ToDepth> [shaft=0] - order the shaft trunk bored down to the given floor (uplink; needs a Borer online, works batch by batch). shaft k > 0 is a shaft sunk with RH.SinkShaft."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Bore <ToDepth> [shaft]"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Bore");
			Cmd.Value = FCString::Atod(*Args[0]);
			Cmd.Target = RHShaftTarget(Args, 1);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSinkShaft(
	TEXT("RH.SinkShaft"),
	TEXT("RH.SinkShaft <BorerId> - order a new shaft sunk where that Borer stands (uplink). Needs shaft 0 bored, a completed Borer inside coverage that anchors no shaft and has no batch in flight, and a free column (RH.Status lists the shafts)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.SinkShaft <BorerId>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("SinkShaft");
			Cmd.Value = FCString::Atod(*Args[0]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSinkNow(
	TEXT("RH.SinkNow"),
	TEXT("RH.SinkNow <Xm> <Ym> [Depth=1] - DEBUG: sink a new shaft instantly at that spot (snapped to the lattice; bypasses Borer/uplink, runs the column checks) and bore it to Depth (spoil banks at its head, unhauled)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.SinkNow <Xm> <Ym> [Depth]"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			int32 NewId = INDEX_NONE;
			FString Why;
			const FVector Head(FCString::Atof(*Args[0]) * 100.f, FCString::Atof(*Args[1]) * 100.f, 0.f);
			const int32 Depth = Args.Num() > 2 ? FCString::Atoi(*Args[2]) : 1;
			if (Sim->Debug_SinkShaftAt(Head, Depth, NewId, Why))
			{
				UE_LOG(LogRedHope, Display, TEXT("[RH.SinkNow] shaft %d sunk and bored to -%d"), NewId, Sim->GetShaftDepth(NewId));
			}
			else
			{
				UE_LOG(LogRedHope, Warning, TEXT("[RH.SinkNow] refused: %s"), *Why);
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDigTunnel(
	TEXT("RH.DigTunnel"),
	TEXT("RH.DigTunnel <Level> <Ax> <Ay> <Bx> <By> - order a tunnel on a floor between two LATTICE points (10 m cells, the shaft head is (0,0)): an L along X, then along Y, carved by the Borers cell by cell (uplink). A must be rock at the edge of a dig. RH.Status lists the tunnels."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 5)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.DigTunnel <Level> <Ax> <Ay> <Bx> <By>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			const FVector Origin = Sim->GetShaftHeadCm();
			FRHCommand Cmd;
			Cmd.Verb = FName("DigTunnel");
			Cmd.Level = FCString::Atoi(*Args[0]);
			Cmd.Location = URHSimWorldSubsystem::LatticeToCm(Origin, FIntPoint(FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2])));
			Cmd.LocationB = URHSimWorldSubsystem::LatticeToCm(Origin, FIntPoint(FCString::Atoi(*Args[3]), FCString::Atoi(*Args[4])));
			FString Why;
			if (!Sim->CanDigTunnel(Cmd.Level, Cmd.Location, Cmd.LocationB, Why))
			{
				UE_LOG(LogRedHope, Warning, TEXT("[RH.DigTunnel] would be refused now: %s (the order is sent anyway; the uplink checks again on arrival)"), *Why);
			}
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHCancelTunnel(
	TEXT("RH.CancelTunnel"),
	TEXT("RH.CancelTunnel <Id> - cancel a tunnel (uplink): no new cell starts, the cells carved stay, a batch under way lands its cell."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.CancelTunnel <Id>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("CancelTunnel");
			Cmd.Value = FCString::Atod(*Args[0]);
			Sim->EnqueueCommand(Cmd);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHTunnelNow(
	TEXT("RH.TunnelNow"),
	TEXT("RH.TunnelNow <Id> [Cells=all] - DEBUG: carve up to N cells of a tunnel instantly (bypasses the Borer; spoil banks at the head, unhauled)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.TunnelNow <Id> [Cells]"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			const int32 Laid = Sim->Debug_CarveTunnelCells(FCString::Atoi(*Args[0]), Args.Num() > 1 ? FCString::Atoi(*Args[1]) : Sim->GetTunnelMaxCells());
			UE_LOG(LogRedHope, Display, TEXT("[RH.TunnelNow] %d cell(s) carved"), Laid);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHTunnelGhost(
	TEXT("RH.TunnelGhost"),
	TEXT("RH.TunnelGhost <Level> <Ax> <Ay> <Bx> <By> - DEBUG: hold the Tunnel brush's drag ghost on a floor between two lattice points (as if the mouse were down there) for a screenshot; it never transmits. With no arguments it clears the brush."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARHPlayerController* PC = World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!PC)
		{
			return;
		}
		if (Args.Num() < 5)
		{
			PC->CancelModes();
			return;
		}
		PC->Debug_TunnelGhost(FCString::Atoi(*Args[0]), FIntPoint(FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2])), FIntPoint(FCString::Atoi(*Args[3]), FCString::Atoi(*Args[4])));
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHExcavate(
	TEXT("RH.Excavate"),
	TEXT("RH.Excavate <Level> <Cells> [shaft=0] - order N 10x10 cells carved on a floor that shaft reaches (uplink; its Borer works the queue)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Excavate <Level> <Cells> [shaft]"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("Excavate");
			Cmd.Level = FCString::Atoi(*Args[0]);
			Cmd.Value = FCString::Atod(*Args[1]);
			Cmd.Target = RHShaftTarget(Args, 2);
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

static FAutoConsoleCommandWithWorldAndArgs GRHZoneBay(
	TEXT("RH.ZoneBay"),
	TEXT("RH.ZoneBay <Room|None> <Xm> <Ym> - zone the surface habitat bay under a point (uplink; None clears)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 3)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.ZoneBay <Room|None> <Xm> <Ym>"));
			return;
		}
		FRHCommand Cmd;
		Cmd.Verb = FName("Designate");
		Cmd.Target = (Args[0] == TEXT("None")) ? NAME_None : FName(*Args[0]);
		Cmd.Level = 0;
		Cmd.Location = FVector(FCString::Atof(*Args[1]) * 100.f, FCString::Atof(*Args[2]) * 100.f, 0.f);
		Sim->EnqueueCommand(Cmd);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHHabDemo(
	TEXT("RH.HabDemo"),
	TEXT("RH.HabDemo - DEBUG (BM-2 look-dev): lay a surface habitat network NE of the Lander - a 2x2 module, a corridor and a 1x2 module, an Air Filter inside - and zone its bays. Run time to fill it with O2; RH.Demo first gives the crew-ready vault."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim)
		{
			return;
		}
		Sim->Debug_PlaceInstant(FName("HabModule"), FVector(3500.f, 3500.f, 0.f), 0, 2, 2);
		Sim->Debug_PlaceInstant(FName("HabCorridor"), FVector(5000.f, 3000.f, 0.f), 0, 1, 1);
		Sim->Debug_PlaceInstant(FName("HabModule"), FVector(6000.f, 3500.f, 0.f), 0, 1, 2);
		Sim->Debug_PlaceInstant(FName("AirFilter"), FVector(3000.f, 3000.f, 0.f));
		Sim->AddStock(FName("Oxygen"), 3000.0);
		// Look-dev: RH.Demo certifies its floor without the vault milestone,
		// and surface beds open only after the vault (director 2026-10-16).
		Sim->Debug_SetVaultRated(true);
		const TPair<FVector, const TCHAR*> Zones[] = {
			{ FVector(3000.f, 4000.f, 0.f), TEXT("LivingQuarters") }, { FVector(4000.f, 4000.f, 0.f), TEXT("LivingQuarters") },
			{ FVector(4000.f, 3000.f, 0.f), TEXT("Dining") }, { FVector(3000.f, 3000.f, 0.f), TEXT("Lab") },
			{ FVector(6000.f, 3000.f, 0.f), TEXT("Workstation") }, { FVector(6000.f, 4000.f, 0.f), TEXT("LivingQuarters") },
		};
		for (const TPair<FVector, const TCHAR*>& Z : Zones)
		{
			int32 Hab = 0, Bay = INDEX_NONE;
			FString Why;
			if (Sim->FindBayAt(Z.Key, Hab, Bay))
			{
				Sim->DesignateBay(Hab, Bay, FName(Z.Value), Why);
			}
		}
		UE_LOG(LogRedHope, Display, TEXT("[RH.HabDemo] surface habitat laid out (7 bays); fill it with time (RH.SetSpeed 60), then RH.AddColonists to move people in"));
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
				const FIntPoint P = Sim->GetFloorCell(L, i);
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
			TEXT("[RH.Hope] instant %.1f | smoothed %.1f  %s  tempo %.0f%% | base %.0f housing +%.1f rooms +%.1f jobs +%.1f (%d seats for %d) milestones +%.1f comforts +%.1f furnishings +%.1f views +%.1f | adjacency -%.1f (%d of %d refuser cells offended, %d pairs) unsupported -%.1f (%.1f of %d on short rations) water -%.1f (potability %.0f%%) food -%.1f (runway %.1f sols)"),
			H.Total, Sim->GetHopeSmoothed(), Sim->GetHopeBandName(), Sim->GetHumanWorkTempo() * 100.0,
			H.Base, H.Housing, H.Rooms, H.Jobs, H.FilledSeats, H.Population, H.Milestones, H.Comforts, H.Furnishings, H.Views,
			H.AdjacencyPenalty, H.OffendedRefusers, H.RefuserCells, H.OffendedPairs, H.UnsupportedPenalty, H.DeficitEquivalent, H.Population,
			H.WaterPenalty, Sim->GetWaterPotability() * 100.0, H.FoodPenalty, Sim->GetFoodRunwaySols());
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
	TEXT("RH.AddSolid <DefName> <Resource> <Kg> [out] - DEBUG: drop solid stock into the first completed building of a def (its hopper; with 'out', its output shelf - what a hauler or rover collects)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 3)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.AddSolid <DefName> <Resource> <Kg>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			if (Args.Num() > 3 && Args[3].Equals(TEXT("out"), ESearchCase::IgnoreCase))
			{
				for (const FRHBuildingInstance& B : Sim->GetBuildings())
				{
					if (B.DefName == FName(*Args[0]) && !B.bUnderConstruction)
					{
						Sim->Debug_AddSolidAt(B.Id, FName(*Args[1]), FCString::Atod(*Args[2]), /*bOutput*/ true);
						return;
					}
				}
				UE_LOG(LogRedHope, Warning, TEXT("RH.AddSolid: no completed '%s'"), *Args[0]);
				return;
			}
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

static FAutoConsoleCommandWithWorldAndArgs GRHDayShift(
	TEXT("RH.DayShift"),
	TEXT("RH.DayShift <BuildingId> <0|1> - day shift for a heavy machine (1 = works while the sun is up, idles at night)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.DayShift <BuildingId> <0|1>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			if (!Sim->SetDayShift(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]) != 0))
			{
				UE_LOG(LogRedHope, Warning, TEXT("RH.DayShift: #%s is not a completed heavy machine"), *Args[0]);
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

static FAutoConsoleCommandWithWorldAndArgs GRHDeckExpand(
	TEXT("RH.DeckExpand"),
	TEXT("RH.DeckExpand [roster] - DEBUG: unfold every collapsible command-deck panel (captures/QA); 'roster' unfolds the crew list too."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (ARHPlayerController* PC = World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr)
		{
			PC->Debug_ExpandDeck(Args.Num() > 0 && Args[0].Equals(TEXT("roster"), ESearchCase::IgnoreCase));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDeckBuild(
	TEXT("RH.DeckBuild"),
	TEXT("RH.DeckBuild <Category> - DEBUG: open the command deck's build drawer on a category (Power, Logistics, Production...)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARHPlayerController* PC = World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (PC && Args.Num() > 0)
		{
			PC->Debug_OpenBuildDrawer(FName(*Args[0]));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHCrewMoment(
	TEXT("RH.CrewMoment"),
	TEXT("RH.CrewMoment <0 pastime|1 join|2 dispute|3 celebrate> <IdA> <IdB> - DEBUG (captures): replay a crew-moment beat to the presentation listeners. The sim's state is untouched."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 3)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.CrewMoment <Type> <IdA> <IdB>"));
			return;
		}
		Sim->OnCrewMoment.Broadcast((uint8)FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2]), TEXT("(debug) a crew moment"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHAfter(
	TEXT("RH.After"),
	TEXT("RH.After <Seconds> <Command...> - DEBUG: run a console command later. Capture scripts' -ExecCmds all fire at boot; this stages a later beat (e.g. RH.After 25 RH.Launch)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.After <Seconds> <Command...>"));
			return;
		}
		const float Delay = FCString::Atof(*Args[0]);
		const FString Cmd = FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" "));
		TWeakObjectPtr<UWorld> WeakWorld = World;
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Cmd]()
		{
			if (UWorld* W = WeakWorld.Get())
			{
				UE_LOG(LogRedHope, Display, TEXT("[RH.After] %s"), *Cmd);
				GEngine->Exec(W, *Cmd);
			}
		}), FMath::Max(0.01f, Delay), false);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHAssign(
	TEXT("RH.Assign"),
	TEXT("RH.Assign <Level> <Cell> | RH.Assign Building <Id> - put a work station at the front of the seat order (uplink)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Assign <Level> <Cell> | RH.Assign Building <Id>"));
			return;
		}
		FRHCommand Cmd;
		Cmd.Verb = FName("Assign");
		if (Args[0].Equals(TEXT("Building"), ESearchCase::IgnoreCase))
		{
			Cmd.Target = FName("Building");
			Cmd.Value = FCString::Atoi(*Args[1]);
		}
		else
		{
			Cmd.Level = FCString::Atoi(*Args[0]);
			Cmd.Value = FCString::Atoi(*Args[1]);
		}
		Sim->EnqueueCommand(Cmd);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHFurnish(
	TEXT("RH.Furnish"),
	TEXT("RH.Furnish <Row> <Level> <Cell|Xm Ym> <Slot> [Rot] - BM-4: order a furnishing (uplink). Underground: the cell index; surface: a point in the habitat bay."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 4)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Furnish <Row> <Level> <Cell|Xm Ym> <Slot> [Rot]"));
			return;
		}
		FRHCommand Cmd;
		Cmd.Verb = FName("Furnish");
		Cmd.Target = FName(*Args[0]);
		Cmd.Level = FCString::Atoi(*Args[1]);
		if (Cmd.Level == 0)
		{
			if (Args.Num() < 5)
			{
				UE_LOG(LogRedHope, Error, TEXT("Usage (surface): RH.Furnish <Row> 0 <Xm> <Ym> <Slot> [Rot]"));
				return;
			}
			Cmd.Location = FVector(FCString::Atof(*Args[2]) * 100.f, FCString::Atof(*Args[3]) * 100.f, 0.f);
			Cmd.Value = URHSimWorldSubsystem::EncodeFurnish(0, FCString::Atoi(*Args[4]), Args.Num() > 5 ? FCString::Atoi(*Args[5]) : 0);
		}
		else
		{
			Cmd.Value = URHSimWorldSubsystem::EncodeFurnish(FCString::Atoi(*Args[2]), FCString::Atoi(*Args[3]), Args.Num() > 4 ? FCString::Atoi(*Args[4]) : 0);
		}
		Sim->EnqueueCommand(Cmd);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSell(
	TEXT("RH.Sell"),
	TEXT("RH.Sell <FurnitureId> - BM-4: sell a furnishing back (uplink)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Sell <FurnitureId>"));
			return;
		}
		FRHCommand Cmd;
		Cmd.Verb = FName("Sell");
		Cmd.Target = FName("Furniture");
		Cmd.Value = FCString::Atoi(*Args[0]);
		Sim->EnqueueCommand(Cmd);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDeckFurnish(
	TEXT("RH.DeckFurnish"),
	TEXT("RH.DeckFurnish [Row] - DEBUG: show the deck's FURNISH tab (captures); with a row, arm Furnish mode on it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARHPlayerController* PC = World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (PC)
		{
			PC->Debug_ShowFurnishTab();
			if (Args.Num() > 0)
			{
				PC->BeginFurnish(FName(*Args[0]));
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHFit(
	TEXT("RH.Fit"),
	TEXT("RH.Fit <Row> <Xm> <Ym> <Side 0..3 = +X,-X,+Y,-Y> - BM-6: order a window, door or airlock into the wall of the habitat bay at that point (uplink)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 4)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Fit <Row> <Xm> <Ym> <Side 0..3>"));
			return;
		}
		FRHCommand Cmd;
		Cmd.Verb = FName("Fit");
		Cmd.Target = FName(*Args[0]);
		Cmd.Level = 0;
		Cmd.Location = FVector(FCString::Atof(*Args[1]) * 100.f, FCString::Atof(*Args[2]) * 100.f, 0.f);
		Cmd.Value = FCString::Atoi(*Args[3]);
		Sim->EnqueueCommand(Cmd);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHSellFitting(
	TEXT("RH.SellFitting"),
	TEXT("RH.SellFitting <Id> - BM-6: sell a window, door or airlock back (uplink)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.SellFitting <Id>"));
			return;
		}
		FRHCommand Cmd;
		Cmd.Verb = FName("Sell");
		Cmd.Target = FName("WallFeature");
		Cmd.Value = FCString::Atoi(*Args[0]);
		Sim->EnqueueCommand(Cmd);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDeckWalls(
	TEXT("RH.DeckWalls"),
	TEXT("RH.DeckWalls [Row] - DEBUG: show the deck's WALLS tab (captures); with a row, arm Fit mode on it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARHPlayerController* PC = World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (PC)
		{
			PC->Debug_ShowWallsTab();
			if (Args.Num() > 0)
			{
				PC->BeginFit(FName(*Args[0]));
			}
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDemolish(
	TEXT("RH.Demolish"),
	TEXT("RH.Demolish <Id> - BM-3: order a structure demolished or a site cancelled (uplink; salvage to the Lander)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Demolish <Id>"));
			return;
		}
		FRHCommand Cmd;
		Cmd.Verb = FName("Demolish");
		Cmd.Target = FName("Building");
		Cmd.Value = FCString::Atoi(*Args[0]);
		Sim->EnqueueCommand(Cmd);
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHMenu(
	TEXT("RH.Menu"),
	TEXT("RH.Menu <title|pause|settings|credits|none> - DEBUG: open a menu screen (captures/QA)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARHPlayerController* PC = World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (PC && Args.Num() > 0)
		{
			PC->Debug_OpenMenu(Args[0]);
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
	TEXT("RH.Crew - log the colonist roster: name, home floor, support state, ration; and each home's ration and departure clock."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		UE_LOG(LogRedHope, Display, TEXT("[RH.Crew] population %d | housing %d beds (%d free) | Food %.0f kg | short-ration deficit %.1f"),
			Sim->GetPopulation(), Sim->GetHousingCapacity(), Sim->GetFreeHousing(), Sim->GetStock(FName("Food")), Sim->GetDeficitEquivalent());
		for (const URHSimWorldSubsystem::FRHHomeRation& Home : Sim->GetHomeRations())
		{
			UE_LOG(LogRedHope, Display, TEXT("  home %-20s %d resident(s)  ration %.0f%%%s  departure clock %.2f of 1 (counted on %d residents at onset)"),
				*Sim->GetHomeLabel(Home.HomeLevel, Home.HomeHab), Home.Residents, Home.Fraction * 100.0,
				Home.Limiting.IsNone() ? TEXT("") : *FString::Printf(TEXT(" (%s short)"), *Home.Limiting.ToString()),
				Sim->GetHomeDepartureAcc(Home.Key), Sim->GetHomeOnsetResidents(Home.Key));
		}
		for (const FRHColonist& C : Sim->GetColonists())
		{
			const FName Job = Sim->GetColonistJob(C.Id);
			UE_LOG(LogRedHope, Display, TEXT("  %-12s floor %d  %-12s %s%s%s"),
				*C.Name, C.HomeLevel,
				Job.IsNone() ? TEXT("(no post)") : *Job.ToString(),
				C.bSupported ? TEXT("supported") : TEXT("SHORT RATIONS"),
				C.bSupported ? TEXT("") : *FString::Printf(TEXT("  (ration %.0f%%)"), Sim->GetColonistRation(C) * 100.0),
				C.bMarsBorn ? TEXT("  [Mars-born]") : TEXT(""));
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

// C3 (living-city gate 3): the -scale suite's big colony in a running game, and
// the instrument that measures what it costs on screen.
static FAutoConsoleCommandWithWorldAndArgs GRHCityBench(
	TEXT("RH.CityBench"),
	TEXT("RH.CityBench <N> - DEBUG: stand up the -scale suite's city (shaft to -5, 1,050 furnished cells, five floors) and move N colonists in. Needs a fresh colony (no shaft yet). For measuring crowds: pair with RH.Floor -1 and RH.FrameStats."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.CityBench <N>"));
			return;
		}
		URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
		if (!Sim) { return; }
		const int32 N = FMath::Clamp(FCString::Atoi(*Args[0]), 0, 5000);
		const double T0 = FPlatformTime::Seconds();
		const URHSimWorldSubsystem::FRHScaleColonyReport Rep = Sim->Debug_BuildScaleColony(N);
		for (const FString& E : Rep.Errors) { UE_LOG(LogRedHope, Error, TEXT("RH.CityBench: %s"), *E); }
		// Everyone starts in residence, as after a load: not 3,000 people marching in
		// from the supply pad one frame later.
		if (URHCrewVisualizerSubsystem* Crew = World->GetSubsystem<URHCrewVisualizerSubsystem>())
		{
			Crew->Debug_SettleCrew();
		}
		UE_LOG(LogRedHope, Display, TEXT("[CityBench] %d colonists housed (target %d, population %d) | %d floors x %d cells | %d beds, %d work seats, %d refused orders | built in %.0f ms"),
			Rep.Housed, N, Sim->GetPopulation(), Rep.Floors, Rep.CellsPerFloor, Rep.Capacity, Rep.SeatsTotal, Rep.Rejected, (FPlatformTime::Seconds() - T0) * 1000.0);
	}));

namespace
{
	// One RH.FrameStats window in flight. A core-ticker callback runs once a
	// frame: the gap between calls is the real frame time (render, RHI and sleep
	// included), and the slot totals the two visualizers accumulated since the
	// last call are that frame's game-thread cost.
	struct FFrameStatsRun
	{
		TWeakObjectPtr<UWorld> World;
		double Seconds = 10.0;
		double StartS = 0.0;
		double LastS = 0.0;
		TArray<float> FrameMs;
		double SlotMs[RHFrameStats::SlotCount] = { 0.0, 0.0 };
		double GameMs = 0.0, RenderMs = 0.0, GpuMs = 0.0; // engine's own per-frame thread timers, summed
		int32 Frames = 0;
		FString Tag;
		int32 StartPromotions = 0, StartDemotions = 0;
		TArray<FString> Spikes; // frames over 150 ms: when, how long, what the two visualizers cost in it
	};
	TSharedPtr<FFrameStatsRun> GFrameStatsRun;

	void ReportFrameStats(const FFrameStatsRun& Run)
	{
		TArray<float> Sorted = Run.FrameMs;
		Sorted.Sort();
		const int32 N = Sorted.Num();
		if (N == 0) { UE_LOG(LogRedHope, Warning, TEXT("[FrameStats] no frames sampled")); return; }
		double Sum = 0.0;
		for (const float F : Sorted) { Sum += F; }
		const float Med = Sorted[N / 2];
		const float P95 = Sorted[FMath::Min(N - 1, (int32)FMath::CeilToInt(0.95f * N) - 1)];
		int32 Actors = 0, Skel = 0, Pooled = 0, Crowd = 0, CrowdDet = 0, Promos = 0, Demos = 0, CrowdAll = 0, TrackedN = 0;
		bool bTier = false;
		int32 BuildActors = 0, TileActors = 0, Pop = 0, ViewLevel = 0;
		if (UWorld* W = Run.World.Get())
		{
			if (const URHCrewVisualizerSubsystem* Crew = W->GetSubsystem<URHCrewVisualizerSubsystem>())
			{
				const URHCrewVisualizerSubsystem::FFigureCounts C = Crew->GetFigureCounts();
				Actors = C.Actors; Skel = C.Skeletal; Pooled = C.Pooled; Crowd = C.CrowdInstances; CrowdDet = C.CrowdDetail; Promos = C.Promotions - Run.StartPromotions; Demos = C.Demotions - Run.StartDemotions; CrowdAll = C.CrowdRecords; TrackedN = C.Tracked; bTier = C.bTiered;
			}
			if (const URHColonyVisualizerSubsystem* Colony = W->GetSubsystem<URHColonyVisualizerSubsystem>())
			{
				BuildActors = Colony->GetBuildingActorCount();
				TileActors = Colony->GetTileActorCount();
				ViewLevel = Colony->GetViewLevel();
			}
			if (const URHSimWorldSubsystem* Sim = W->GetSubsystem<URHSimWorldSubsystem>()) { Pop = Sim->GetPopulation(); }
		}
		UE_LOG(LogRedHope, Display, TEXT("[FrameStats] %s window %.1f s, %d frames | frame ms: avg %.2f, median %.2f, p95 %.2f, max %.2f (median %.1f fps) | population %d, view floor %d | crew: %s, %d full figures tracked, %d figure actors (%d skeletal, %d pooled), %d crowd records (%d drawn on the viewed floor, %d of them in full mesh detail), tier swaps in the window: %d promotions / %d demotions | colony: %d building actors, %d tile actors | game-thread ms/frame: crew tick %.3f, colony tick %.3f | engine threads ms/frame: game %.1f, render %.1f, GPU %.1f"),
			*Run.Tag, Run.Seconds, N, Sum / N, Med, P95, Sorted.Last(), Med > 0.f ? 1000.f / Med : 0.f, Pop, ViewLevel,
			bTier ? TEXT("crowd mode") : TEXT("all full"), TrackedN, Actors, Skel, Pooled, CrowdAll, Crowd, CrowdDet, Promos, Demos, BuildActors, TileActors,
			Run.SlotMs[RHFrameStats::Crew] / N, Run.SlotMs[RHFrameStats::Colony] / N, Run.GameMs / N, Run.RenderMs / N, Run.GpuMs / N);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GRHFrameStats(
	TEXT("RH.FrameStats"),
	TEXT("RH.FrameStats <Seconds=10> [Tag] - DEBUG: sample real frame times for a window, then log [FrameStats]: avg, median and p95 ms, crew figure actors and skeletal components, building actors, and the game-thread ms the crew and colony visualizer ticks cost per frame."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World) { return; }
		if (GFrameStatsRun.IsValid())
		{
			UE_LOG(LogRedHope, Warning, TEXT("RH.FrameStats: a window is already running"));
			return;
		}
		TSharedPtr<FFrameStatsRun> Run = MakeShared<FFrameStatsRun>();
		Run->World = World;
		Run->Seconds = Args.Num() > 0 ? FMath::Max(0.5, (double)FCString::Atof(*Args[0])) : 10.0;
		Run->Tag = Args.Num() > 1 ? Args[1] : TEXT("");
		Run->StartS = FPlatformTime::Seconds();
		Run->LastS = Run->StartS;
		if (const URHCrewVisualizerSubsystem* Crew = World->GetSubsystem<URHCrewVisualizerSubsystem>())
		{
			const URHCrewVisualizerSubsystem::FFigureCounts C0 = Crew->GetFigureCounts();
			Run->StartPromotions = C0.Promotions;
			Run->StartDemotions = C0.Demotions;
		}
		for (double& S : RHFrameStats::GSlotSeconds) { S = 0.0; }
		GFrameStatsRun = Run;
		UE_LOG(LogRedHope, Display, TEXT("[FrameStats] sampling %.1f s"), Run->Seconds);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Run](float)
		{
			const double Now = FPlatformTime::Seconds();
			Run->FrameMs.Add((float)((Now - Run->LastS) * 1000.0));
			Run->LastS = Now;
			if (Run->FrameMs.Last() > 150.f && Run->Spikes.Num() < 8)
			{
				Run->Spikes.Add(FString::Printf(TEXT("+%.1fs %.0f ms (crew %.1f, colony %.1f)"), Now - Run->StartS, Run->FrameMs.Last(),
					RHFrameStats::GSlotSeconds[RHFrameStats::Crew] * 1000.0, RHFrameStats::GSlotSeconds[RHFrameStats::Colony] * 1000.0));
			}
			for (int32 S = 0; S < RHFrameStats::SlotCount; ++S)
			{
				Run->SlotMs[S] += RHFrameStats::GSlotSeconds[S] * 1000.0;
				RHFrameStats::GSlotSeconds[S] = 0.0;
			}
			Run->GameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
			Run->RenderMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
			Run->GpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
			if (Now - Run->StartS < Run->Seconds)
			{
				return true;
			}
			// Drop the first sample: it spans the command itself, not a frame.
			if (Run->FrameMs.Num() > 1) { Run->FrameMs.RemoveAt(0); }
			ReportFrameStats(*Run);
			if (Run->Spikes.Num() > 0)
			{
				UE_LOG(LogRedHope, Display, TEXT("[FrameStats] %s frames over 150 ms: %s"), *Run->Tag, *FString::Join(Run->Spikes, TEXT("; ")));
			}
			GFrameStatsRun.Reset();
			return false;
		}));
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
	TEXT("RH.Select <BuildingId> | RH.Select Cell <Level> <Index> - raise the in-world action card for a building by id (0 = deselect), or for a zoned work-station cell; handy when the gray-box is fiddly to click."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Select <BuildingId>"));
			return;
		}
		if (ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController()))
		{
			int32 Hab = 0, Bay = INDEX_NONE;
			if (Args[0].Equals(TEXT("Bay"), ESearchCase::IgnoreCase) && Args.Num() >= 3)
			{
				const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
				if (Sim && Sim->FindBayAt(FVector(FCString::Atof(*Args[1]) * 100.f, FCString::Atof(*Args[2]) * 100.f, 0.f), Hab, Bay))
				{
					PC->Debug_SelectBay(Hab, Bay);
					UE_LOG(LogRedHope, Display, TEXT("Selected habitat #%d bay %d (action card raised)"), Hab, Bay);
				}
				return;
			}
			if (Args[0].Equals(TEXT("Cell"), ESearchCase::IgnoreCase) && Args.Num() >= 3)
			{
				PC->Debug_SelectCell(FCString::Atoi(*Args[1]), FCString::Atoi(*Args[2]));
				UE_LOG(LogRedHope, Display, TEXT("Selected cell %s on floor %s (action card raised)"), *Args[2], *Args[1]);
				return;
			}
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
	TEXT("RH.ExcavateNow <Level> <Cells> [shaft] - DEBUG: carve instantly (bypasses Borer/uplink; spoil banks at the head, unhauled). No shaft = the lowest-Id shaft that reaches the floor."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.ExcavateNow <Level> <Cells> [shaft]"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			FString Reason;
			if (!Sim->ExcavateFloor(FCString::Atoi(*Args[0]), FCString::Atoi(*Args[1]), Reason, Args.Num() > 2 ? FCString::Atoi(*Args[2]) : (int32)INDEX_NONE))
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
		// Garden materials so zoned beds actually PLANT (director hand-play
		// 2026-07-17: the demo colony had no Soil/Seeds, so RH.ActivateCrop
		// had nothing to grow). Glass covers a greenhouse fork too.
		Sim->AddStock(FName("Soil"), 2000.0);
		Sim->AddStock(FName("Seeds"), 400.0);
		Sim->AddStock(FName("Glass"), 200.0);
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
			UE_LOG(LogRedHope, Display, TEXT("  LADDER ENDED (QuotaRepeatBillMul 0) - the Program continues."));
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
				const int32 Queued = Sim->GetCarveQueued(L, 0);
				const int32 Carved = Sim->GetFloorCarvedCells(L);
				if (Queued > 0 || Carved > 0)
				{
					Carve += FString::Printf(TEXT("  %d: %d carved%s"), L, Carved,
						Queued > 0 ? *FString::Printf(TEXT(" (+%d queued)"), Queued) : TEXT(""));
				}
			}
			// D2: shaft 0's line keeps today's text (per-shaft getters: GetShaftDepth() is the DEEPEST shaft).
			UE_LOG(LogRedHope, Display, TEXT("  shaft: floor -%d of ordered -%d%s"),
				Sim->GetShaftDepth(0), Sim->GetBoreTargetDepth(0), *Carve);
		}
		// One more line per shaft sunk after shaft 0.
		for (int32 K = 1; K < Sim->GetShaftCount(); ++K)
		{
			const FRHShaft& Sh = Sim->GetShaft(K);
			FString Queue;
			for (int32 L = -1; L >= -Sim->GetMaxDepth(); --L)
			{
				if (Sim->GetCarveQueued(L, K) > 0) { Queue += FString::Printf(TEXT("  %d: +%d queued"), L, Sim->GetCarveQueued(L, K)); }
			}
			UE_LOG(LogRedHope, Display, TEXT("  shaft %d: floor -%d of ordered -%d | column (%d, %d) | Borer #%d%s"),
				K, Sh.Depth, Sh.BoreTargetDepth, Sh.Column.X, Sh.Column.Y, Sh.BorerId, *Queue);
		}
		// D3: one line per tunnel.
		for (const FRHTunnel& T : Sim->GetTunnels())
		{
			UE_LOG(LogRedHope, Display, TEXT("  tunnel %d: floor %d | (%d, %d) to (%d, %d) | %d of %d cells left%s%s%s"),
				T.Id, T.Level, T.A.X, T.A.Y, T.B.X, T.B.Y, Sim->GetTunnelUncarved(T), URHSimWorldSubsystem::GetTunnelChainLength(T.A, T.B),
				T.bTwoEnded ? TEXT(" | two-ended") : TEXT(""), T.bCancelled ? TEXT(" | cancelled") : TEXT(""), Sim->IsTunnelStalled(T) ? TEXT(" | stalled") : TEXT(""));
		}
		if (const FRHEventRow* Event = Sim->GetActiveEvent())
		{
			UE_LOG(LogRedHope, Display, TEXT("  EVENT: %s until sol %.1f (severity %.2f; dust factor now %.2f)"),
				*Event->Type.ToString(), Event->StartSol + Event->DurationSols, Event->Severity, Sim->GetDustFactorNow());
		}

		if (Sim->GetQuotaRepeatRound() > 0)
		{
			UE_LOG(LogRedHope, Display, TEXT("  ladder: %s renewed, round %d (bills x%.2f)"),
				*Sim->GetActiveQuotaName().ToString(), Sim->GetQuotaRepeatRound(), Sim->GetQuotaBillScale());
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

static FAutoConsoleCommandWithWorldAndArgs GRHRoad(
	TEXT("RH.Road"),
	TEXT("RH.Road <x1m> <y1m> <x2m> <y2m> - transmit a BuildRoad order (uplink) from P to Q, in metres; the Struct leaves the stores when it lands and Fabricators build it. No arguments: log the road network."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim)
		{
			return;
		}
		if (Args.Num() == 0)
		{
			const FRHRoadGraph& G = Sim->GetRoadGraph();
			int32 Open = 0;
			for (const FRHRoadEdge& E : G.Edges) { Open += E.bBuilt ? 0 : 1; }
			UE_LOG(LogRedHope, Display, TEXT("[RH.Road] %d nodes, %d edges (%d built, %d open), %.0f m built, %.0f kg Struct in the stores"),
				G.Nodes.Num(), G.Edges.Num(), G.BuiltEdgeCount, Open, (double)G.BuiltLengthCm() / 100.0, Sim->GetRoadStructAvailableKg());
			return;
		}
		if (Args.Num() < 4)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Road <x1m> <y1m> <x2m> <y2m>"));
			return;
		}
		FRHCommand Cmd;
		Cmd.Verb = FName("BuildRoad");
		Cmd.Location = FVector(FCString::Atof(*Args[0]) * 100.f, FCString::Atof(*Args[1]) * 100.f, 0.f);
		Cmd.LocationB = FVector(FCString::Atof(*Args[2]) * 100.f, FCString::Atof(*Args[3]) * 100.f, 0.f);
		FString Reason;
		URHSimWorldSubsystem::FRHRoadPreview Pre;
		const bool bOk = Sim->CanPlaceRoad(Cmd.Location, Cmd.LocationB, Reason, &Pre);
		UE_LOG(LogRedHope, Display, TEXT("[RH.Road] (%.0f, %.0f) -> (%.0f, %.0f) m: %d stretch(es), %d split(s), %.0f m, %.0f kg Struct of %.0f held - %s"),
			Cmd.Location.X / 100.f, Cmd.Location.Y / 100.f, Cmd.LocationB.X / 100.f, Cmd.LocationB.Y / 100.f,
			Pre.Pieces, Pre.Splits, (double)Pre.LengthCm / 100.0, Pre.BillKg, Pre.HaveKg, bOk ? TEXT("accepted for transmission") : *Reason);
		Sim->EnqueueCommand(Cmd); // transmitted either way: the seam re-checks when it lands (and says why it refuses)
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHRoadInstant(
	TEXT("RH.RoadInstant"),
	TEXT("RH.RoadInstant <x1m> <y1m> <x2m> <y2m> - DEBUG: lay a FINISHED road from P to Q at once (no cost, no lag, no coverage rule, no labour). For look-dev and the live rover smoke."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 4)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.RoadInstant <x1m> <y1m> <x2m> <y2m>"));
			return;
		}
		const TArray<int32> Ids = Sim->Debug_LayRoad(FVector(FCString::Atof(*Args[0]) * 100.f, FCString::Atof(*Args[1]) * 100.f, 0.f), FVector(FCString::Atof(*Args[2]) * 100.f, FCString::Atof(*Args[3]) * 100.f, 0.f), true);
		UE_LOG(LogRedHope, Display, TEXT("[RH.RoadInstant] %d finished edge(s) laid"), Ids.Num());
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHRoadDemolish(
	TEXT("RH.RoadDemolish"),
	TEXT("RH.RoadDemolish <edgeId> - C6a (Q3a): transmit a DemolishRoad order (uplink) for one road edge. A standing edge refunds 50% of its Struct to the Lander, an unbuilt one the whole bill; a robot or rover already on it finishes its trip (a rover crawls the rest of that leg). Ids: RH.Road with no arguments logs the network; the road overlay prints them."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim || Args.Num() < 1)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.RoadDemolish <edgeId>"));
			return;
		}
		FRHCommand Cmd;
		Cmd.Verb = FName("DemolishRoad");
		Cmd.Value = (double)FCString::Atoi(*Args[0]);
		FString Reason;
		double RefundKg = 0.0;
		const bool bOk = Sim->CanDemolishRoad(FCString::Atoi(*Args[0]), Reason, &RefundKg);
		UE_LOG(LogRedHope, Display, TEXT("[RH.RoadDemolish] edge #%d - %s"), FCString::Atoi(*Args[0]), bOk ? *FString::Printf(TEXT("accepted for transmission, %.1f kg of Struct back at the Lander"), RefundKg) : *Reason);
		Sim->EnqueueCommand(Cmd); // transmitted either way: the seam re-checks when it lands (and says why it refuses)
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHRover(
	TEXT("RH.Rover"),
	TEXT("RH.Rover <spawn | list | drain <fraction>> - C6a cargo rovers. spawn delivers a CargoRover as if the ship had landed (it deploys at the built road node nearest the Lander, or waits aboard until a road exists); list logs every rover (position, charge, wear, task, load), the freight tasks open and the rovers waiting aboard; drain sets every rover's battery to a fraction of its capacity (DEBUG: forces the charge break-off, to watch a loaded rover put its cargo back)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim)
		{
			return;
		}
		const FString Sub = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("list"));
		if (Sub == TEXT("drain"))
		{
			if (URHAgentSubsystem* Agents = World->GetSubsystem<URHAgentSubsystem>())
			{
				const float Frac = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.1f;
				Agents->Debug_SetChargeFrac(FName("Rover"), Frac);
				UE_LOG(LogRedHope, Display, TEXT("[RH.Rover] every rover's battery set to %.0f%%"), Frac * 100.f);
			}
			return;
		}
		if (Sub == TEXT("spawn"))
		{
			Sim->Debug_DeliverCargo(FName("CargoRover"));
			UE_LOG(LogRedHope, Display, TEXT("[RH.Rover] a cargo rover delivered: %d waiting aboard"), Sim->GetRoversWaitingAboard());
			return;
		}
		TArray<FRHRoverState> Rovers;
		Sim->GetRoverStates(Rovers);
		UE_LOG(LogRedHope, Display, TEXT("[RH.Rover] %d rover(s), %d active, %d waiting aboard, %d freight task(s) open; a road %s; %.0f kg of Struct on the stores' shelves (a load in flight is not)"), Rovers.Num(), Sim->GetActiveRoverCount(), Sim->GetRoversWaitingAboard(), Sim->GetFreightTaskCount(), Sim->HasBuiltRoads() ? TEXT("stands") : TEXT("does not exist yet"), Sim->GetTotalSolid(FName("Struct")));
		for (const FRHRoverState& R : Rovers)
		{
			const FRHRobotRoadPath* Path = Sim->GetRoverRoadPath(R.Entity);
			UE_LOG(LogRedHope, Display, TEXT("[RH.Rover]   %s at (%.0f, %.0f) m, charge %.0f%%, wear %.0f, task %d phase %d #%d, carrying %.0f kg of %s%s"), *R.DefName.ToString(), R.PosCm.X / 100.0, R.PosCm.Y / 100.0, R.ChargeFrac * 100.f, R.Wear,
				(int32)R.TaskType, (int32)R.Phase, R.TaskId, R.CargoKg, *R.CargoResource.ToString(), Path && Path->IsRoadTrip() ? TEXT(", on a road trip") : TEXT(""));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHCommute(
	TEXT("RH.Commute"),
	TEXT("RH.Commute [flows | edges] - C6a commuting: the ramp, the people measured, the long commuters, the Hope term and the work multiplier, the longest trip and the cache counters. flows logs each district-to-district flow (Unzoned = 0); edges logs the commuters on each road edge. Everything below the population ramp reads zero by design."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim)
		{
			return;
		}
		const FString Sub = Args.Num() > 0 ? Args[0].ToLower() : FString();
		const URHSimWorldSubsystem::FRHCommuteSummary S = Sim->GetCommuteSummary();
		if (Sub.IsEmpty())
		{
			UE_LOG(LogRedHope, Display, TEXT("[RH.Commute] population %d, ramp %.3f (%s), %d employed measured, %d commute, %d LONG (over %.0f m), Hope term -%.3f, a long commuter's work x%.3f, the longest trip %.0f m; flows rebuilt %d times, %d Dijkstras run"),
				Sim->GetPopulation(), Sim->GetCommuteRamp(), Sim->GetCommuteRamp() <= 0.0 ? TEXT("closed: nothing is measured") : TEXT("open"), S.Employed, S.Commuters, S.LongCommuters, (double)Sim->GetCommuteLongCm() / 100.0, S.Penalty, S.WorkMul, (double)S.MaxDistCm / 100.0,
				Sim->GetCommuteFlowRebuilds(), Sim->GetCommuteDijkstraRuns());
			return;
		}
		if (Sub == TEXT("flows"))
		{
			TArray<URHSimWorldSubsystem::FRHCommuteFlow> Flows;
			Sim->GetCommuteFlows(Flows);
			UE_LOG(LogRedHope, Display, TEXT("[RH.Commute] %d flow(s) between districts (0 = Unzoned)"), Flows.Num());
			for (const URHSimWorldSubsystem::FRHCommuteFlow& F : Flows)
			{
				UE_LOG(LogRedHope, Display, TEXT("[RH.Commute]   #%d -> #%d: %d people, %d long"), F.HomeDistrictId, F.WorkDistrictId, F.Count, F.LongCount);
			}
			return;
		}
		TArray<TPair<int32, int32>> Loads;
		Sim->GetCommuteEdgeLoads(Loads);
		UE_LOG(LogRedHope, Display, TEXT("[RH.Commute] %d road edge(s) carry commuters"), Loads.Num());
		for (const TPair<int32, int32>& L : Loads)
		{
			UE_LOG(LogRedHope, Display, TEXT("[RH.Commute]   edge #%d: %d commuters"), L.Key, L.Value);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHRoadGhost(
	TEXT("RH.RoadGhost"),
	TEXT("RH.RoadGhost <x1m> <y1m> <x2m> <y2m> - DEBUG: hold the road card's drag ghost between two points (as if the mouse were down there) for a screenshot; it never transmits. RH.RoadGhost with no arguments clears it."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		ARHPlayerController* PC = World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!PC)
		{
			return;
		}
		if (Args.Num() < 4)
		{
			PC->CancelModes();
			return;
		}
		PC->Debug_RoadGhost(FVector(FCString::Atof(*Args[0]) * 100.f, FCString::Atof(*Args[1]) * 100.f, 0.f),
			FVector(FCString::Atof(*Args[2]) * 100.f, FCString::Atof(*Args[3]) * 100.f, 0.f));
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDeckScroll(
	TEXT("RH.DeckScroll"),
	TEXT("RH.DeckScroll <OffsetPx=0> - DEBUG/QA: scroll the command deck's right-hand column to a pixel offset, for a screenshot of a panel below the fold."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (ARHPlayerController* PC = World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr)
		{
			PC->Debug_ScrollDeck(Args.Num() > 0 ? FCString::Atof(*Args[0]) : 0.f);
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHTraffic(
	TEXT("RH.Traffic"),
	TEXT("RH.Traffic <overlay [0|1] | status | noon | hover <xm> <ym> | home [DefName=Habitat] | demolish <xm> <ym>> - C6b traffic. overlay switches the TRAFFIC heat overlay on or off (no argument toggles); noon sets the clock to mid-morning so a capture is lit; status logs the drawn shuttles and cars, the busiest road and the cursor edge; home DEBUG-re-homes every colonist round-robin into the finished surface buildings of a def (Id order), to stage a commuting city: with floor work seats it makes every trip surface-to-shaft; demolish holds the Demolish Road tool's hover on a ground point for a screenshot (RH.RoadGhost-style, never transmits; no arguments clears it)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		URHTrafficVisualizerSubsystem* Traffic = World ? World->GetSubsystem<URHTrafficVisualizerSubsystem>() : nullptr;
		ARHPlayerController* PC = World ? Cast<ARHPlayerController>(World->GetFirstPlayerController()) : nullptr;
		if (!Sim || !Traffic)
		{
			return;
		}
		const FString Sub = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("status"));
		if (Sub == TEXT("overlay"))
		{
			Traffic->SetOverlayOn(Args.Num() > 1 ? FCString::Atoi(*Args[1]) != 0 : !Traffic->IsOverlayOn());
			UE_LOG(LogRedHope, Display, TEXT("[RH.Traffic] overlay %s"), Traffic->IsOverlayOn() ? TEXT("on") : TEXT("off"));
			return;
		}
		if (Sub == TEXT("home"))
		{
			const FName Def(*(Args.Num() > 1 ? Args[1] : FString(TEXT("Habitat"))));
			TArray<int32> Homes;
			for (const FRHBuildingInstance& B : Sim->GetBuildings())
			{
				if (B.DefName == Def && B.Level == 0 && !B.bUnderConstruction)
				{
					Homes.Add(B.Id);
				}
			}
			if (Homes.Num() == 0)
			{
				UE_LOG(LogRedHope, Error, TEXT("[RH.Traffic] no finished surface '%s' to move people into (RH.Spawn one first)"), *Def.ToString());
				return;
			}
			int32 Index = 0;
			for (const FRHColonist& C : TArray<FRHColonist>(Sim->GetColonists()))
			{
				Sim->Debug_SetColonistHome(C.Id, Homes[Index % Homes.Num()], 0);
				++Index;
			}
			UE_LOG(LogRedHope, Display, TEXT("[RH.Traffic] %d colonists re-homed round-robin into %d '%s' building(s)"), Index, Homes.Num(), *Def.ToString());
			return;
		}
		if (Sub == TEXT("hover"))
		{
			// DEBUG: hold the overlay's hover on a ground point (metres) for a screenshot; no arguments releases it.
			Traffic->SetForcedHover(Args.Num() >= 3 ? FVector(FCString::Atof(*Args[1]) * 100.f, FCString::Atof(*Args[2]) * 100.f, 0.f) : FVector::ZeroVector, Args.Num() >= 3);
			return;
		}
		if (Sub == TEXT("noon"))
		{
			// DEBUG: the sim clock to mid-morning of the current sol, so a capture is lit (a city built by RH.CityBench can start at night).
			if (URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>())
			{
				Clock->Debug_SetSimSeconds((double)Clock->GetSol() * (double)URHSimClockSubsystem::SolLengthSimSeconds + 0.3 * (double)URHSimClockSubsystem::SolLengthSimSeconds);
				UE_LOG(LogRedHope, Display, TEXT("[RH.Traffic] the clock is at sol %d, fraction %.2f"), Clock->GetSol(), Clock->GetSolFraction());
			}
			return;
		}
		if (Sub == TEXT("demolish"))
		{
			if (!PC) { return; }
			if (Args.Num() < 3)
			{
				PC->CancelModes();
				return;
			}
			PC->Debug_RoadDemolishHover(FVector(FCString::Atof(*Args[1]) * 100.f, FCString::Atof(*Args[2]) * 100.f, 0.f));
			return;
		}
		const URHSimWorldSubsystem::FRHCommuteSummary Commute = Sim->GetCommuteSummary();
		UE_LOG(LogRedHope, Display, TEXT("[RH.Traffic] %d vehicle(s) drawn (%d shuttles, %d cars) over %d district flow(s); commute ramp %.2f, %d commute, %d long; overlay %s, busiest road %d, %d loaded; hover: %s"),
			Traffic->GetVehicleCount(), Traffic->GetShuttleCount(), Traffic->GetCarCount(), Traffic->GetFlowGroupCount(), Commute.Ramp, Commute.Commuters, Commute.LongCommuters,
			Traffic->IsOverlayOn() ? TEXT("on") : TEXT("off"), Traffic->GetPeakLoad(), Traffic->GetLoadedEdges(), Traffic->GetHoverText().IsEmpty() ? TEXT("-") : *Traffic->GetHoverText());
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDistrict(
	TEXT("RH.District"),
	TEXT("RH.District <list | demand | paint <Kind> <x1> <y1> <x2> <y2> | floor <Kind> <Level> | erase <Id> | rename <Id> <Name...> | growth <Id> <0|1> | labels <0|1> | mode <Kind> | ghost <Kind> <x1> <y1> <x2> <y2> | tab | city <0|1>> - C5a districts. Cells are 10 m lattice cells (cell 0,0 is centred on the Lander, x1..x2 and y1..y2 corners in either order). paint, floor, erase and rename transmit the real uplink orders; growth switches a district's own growth on or off (C5b; instant, like the machine switches); mode arms the paint tool, ghost holds its drag ghost for a screenshot (no args to clear), labels shows every name, tab and city fold the deck."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim)
		{
			return;
		}
		ARHPlayerController* PC = Cast<ARHPlayerController>(World->GetFirstPlayerController());
		const FString Sub = Args.Num() > 0 ? Args[0].ToLower() : FString(TEXT("list"));
		const auto Cell = [&Args](int32 First) { return FIntPoint(FCString::Atoi(*Args[First]), FCString::Atoi(*Args[First + 1])); };
		const auto LogPreflight = [&](const TCHAR* What, FName Kind, int32 Level, const FVector& From, const FVector& To)
		{
			FString Reason;
			FRHDistrictPaintPreview Pre;
			const bool bOk = Sim->CanPaintDistrict(Kind, Level, From, To, Reason, &Pre);
			UE_LOG(LogRedHope, Display, TEXT("[RH.District] %s %s: %d cell%s in the rectangle, %d new, %d outside coverage%s - %s"), What, *Kind.ToString(),
				Pre.CellsInRect, Pre.CellsInRect == 1 ? TEXT("") : TEXT("s"), Pre.CellsNew, Pre.CellsOutside,
				Pre.JoinId != 0 ? *FString::Printf(TEXT(", joins #%d"), Pre.JoinId) : TEXT(""), bOk ? TEXT("accepted for transmission") : *Reason);
		};
		if (Sub == TEXT("list"))
		{
			TArray<FRHDistrictCounts> Counts;
			Sim->GetDistrictCounts(Counts);
			UE_LOG(LogRedHope, Display, TEXT("[RH.District] %d district%s (v%u, next Id %d); the shaft head stands at (%.0f, %.0f) m, bored to floor -%d"), Sim->GetDistricts().Num(), Sim->GetDistricts().Num() == 1 ? TEXT("") : TEXT("s"),
				Sim->GetDistrictsVersion(), Sim->GetNextDistrictId(), Sim->GetShaftHeadCm().X / 100.0, Sim->GetShaftHeadCm().Y / 100.0, Sim->GetShaftDepth());
			for (int32 i = 0; i < Sim->GetDistricts().Num(); ++i)
			{
				const FRHDistrict& D = Sim->GetDistricts()[i];
				const FRHDistrictCounts& C = Counts[i];
				UE_LOG(LogRedHope, Display, TEXT("[RH.District]   #%d '%s' %s %s: %d cell%s, %d building%s, %d bay%s, %d resident%s, %d bed%s, %d seat%s"), D.Id, *D.Name, *D.Kind.ToString(),
					D.IsFloor() ? *FString::Printf(TEXT("floor %d"), D.Level) : TEXT("surface"), C.Cells, C.Cells == 1 ? TEXT("") : TEXT("s"), C.Buildings, C.Buildings == 1 ? TEXT("") : TEXT("s"),
					C.Bays, C.Bays == 1 ? TEXT("") : TEXT("s"), C.Residents, C.Residents == 1 ? TEXT("") : TEXT("s"), C.Beds, C.Beds == 1 ? TEXT("") : TEXT("s"), C.Seats, C.Seats == 1 ? TEXT("") : TEXT("s"));
			}
			return;
		}
		if (Sub == TEXT("demand"))
		{
			const FRHCityDemand Dm = Sim->GetCityDemand();
			UE_LOG(LogRedHope, Display, TEXT("[RH.District] demand at %d: Housing %.2f (%d bed(s) short of %d, limit %s; free %d, comfortable %d, vault %s) | Jobs %.2f (%d of %d seats filled, %d short) | Services %.2f (worst %s) | Rations %.2f (%d home(s) short, margin %.1f sols)"),
				Dm.Population, Dm.Housing, Dm.HousingBedsShort, Dm.HousingBedsWanted, *Dm.HousingLimit.ToString(), Dm.FreeBeds, Dm.ComfortCapacity, Dm.bVaultCertified ? TEXT("certified") : TEXT("not yet"),
				Dm.Jobs, Dm.FilledSeats, Dm.TotalSeats, Dm.SeatsShort, Dm.Services, Dm.ServiceGaps.Num() > 0 ? *Dm.ServiceGaps[0].Function.ToString() : TEXT("-"), Dm.Air, Dm.HomesShort, Dm.MinOxygenMarginSols);
			return;
		}
		if (Sub == TEXT("paint") && Args.Num() >= 6)
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("PaintDistrict");
			Cmd.Target = FName(*Args[1]);
			Cmd.Location = URHSimWorldSubsystem::LatticeCentreCm(Cell(2));
			Cmd.LocationB = URHSimWorldSubsystem::LatticeCentreCm(Cell(4));
			LogPreflight(TEXT("paint"), Cmd.Target, 0, Cmd.Location, Cmd.LocationB);
			Sim->EnqueueCommand(Cmd); // transmitted either way: the seam re-checks when it lands (and says why it refuses)
			return;
		}
		if (Sub == TEXT("floor") && Args.Num() >= 3)
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("PaintDistrict");
			Cmd.Target = FName(*Args[1]);
			Cmd.Level = FCString::Atoi(*Args[2]);
			LogPreflight(TEXT("floor"), Cmd.Target, Cmd.Level, FVector::ZeroVector, FVector::ZeroVector);
			Sim->EnqueueCommand(Cmd);
			return;
		}
		if (Sub == TEXT("erase") && Args.Num() >= 2)
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("EraseDistrict");
			Cmd.Value = FCString::Atoi(*Args[1]);
			Sim->EnqueueCommand(Cmd);
			return;
		}
		if (Sub == TEXT("rename") && Args.Num() >= 3)
		{
			FRHCommand Cmd;
			Cmd.Verb = FName("RenameDistrict");
			Cmd.Value = FCString::Atoi(*Args[1]);
			TArray<FString> Words;
			for (int32 i = 2; i < Args.Num(); ++i) { Words.Add(Args[i]); }
			Cmd.Text = FString::Join(Words, TEXT(" "));
			Sim->EnqueueCommand(Cmd);
			return;
		}
		if (Sub == TEXT("growth") && Args.Num() >= 3)
		{
			const int32 Id = FCString::Atoi(*Args[1]);
			const bool bOn = FCString::Atoi(*Args[2]) != 0;
			if (Sim->SetDistrictGrowth(Id, bOn))
			{
				UE_LOG(LogRedHope, Display, TEXT("[RH.District] #%d growth %s"), Id, bOn ? TEXT("ON") : TEXT("OFF"));
			}
			else
			{
				UE_LOG(LogRedHope, Error, TEXT("[RH.District] growth: no district #%d"), Id);
			}
			return;
		}
		if (Sub == TEXT("labels") && Args.Num() >= 2)
		{
			if (URHDistrictVisualizerSubsystem* Viz = World->GetSubsystem<URHDistrictVisualizerSubsystem>())
			{
				Viz->SetShowAllLabels(FCString::Atoi(*Args[1]) != 0);
			}
			return;
		}
		if (PC && Sub == TEXT("mode") && Args.Num() >= 2)
		{
			PC->BeginDistrictPaint(FName(*Args[1]));
			return;
		}
		if (PC && Sub == TEXT("ghost"))
		{
			if (Args.Num() >= 6) { PC->Debug_DistrictGhost(FName(*Args[1]), Cell(2), Cell(4)); }
			else { PC->CancelModes(); }
			return;
		}
		if (PC && Sub == TEXT("tab"))
		{
			PC->Debug_ShowDistrictTab();
			return;
		}
		if (PC && Sub == TEXT("city"))
		{
			PC->Debug_SetCityExpanded(Args.Num() < 2 || FCString::Atoi(*Args[1]) != 0);
			return;
		}
		UE_LOG(LogRedHope, Error, TEXT("Usage: RH.District <list | demand | paint <Kind> <x1> <y1> <x2> <y2> | floor <Kind> <Level> | erase <Id> | rename <Id> <Name...> | growth <Id> <0|1> | labels <0|1> | mode <Kind> | ghost <Kind> <x1> <y1> <x2> <y2> | tab | city <0|1>>"));
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHGrowth(
	TEXT("RH.Growth"),
	TEXT("RH.Growth [now | share <0..1>] - C5b: list the city's growth. The open projects (what, how far, what is left, the bill), each Homes and Civic district's switch and why it is not growing, the pause, the City / Earth share, the free material per resource (held - quota bill - reserve) and the life-support books (production made against the draw). 'now' runs the sol-boundary start check at once (DEBUG: it runs by itself at every sol, one project at most). 'share <0..1>' sets the City / Earth share (C5d: how much of the spare material may grow the city; 1 = all of it, 0 = Earth's next quota is held whole; instant, saved)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
		if (!Sim)
		{
			return;
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("share"), ESearchCase::IgnoreCase))
		{
			if (Args.Num() < 2)
			{
				UE_LOG(LogRedHope, Error, TEXT("Usage: RH.Growth share <0..1>"));
				return;
			}
			Sim->SetCityShare(FCString::Atod(*Args[1]));
		}
		if (Args.Num() > 0 && Args[0].Equals(TEXT("now"), ESearchCase::IgnoreCase))
		{
			const int32 Started = Sim->Debug_RunGrowthCheck();
			UE_LOG(LogRedHope, Display, TEXT("[RH.Growth] the start check ran: %d project%s started"), Started, Started == 1 ? TEXT("") : TEXT("s"));
		}
		UE_LOG(LogRedHope, Display, TEXT("[RH.Growth] %d project%s under way (growth v%u; last sol checked %d; the cursor rests on district #%d)%s%s"), Sim->GetGrowthProjects().Num(), Sim->GetGrowthProjects().Num() == 1 ? TEXT("") : TEXT("s"),
			Sim->GetGrowthVersion(), Sim->GetGrowthCheckedSol(), Sim->GetGrowthCursor(), Sim->GetGrowthPause().IsEmpty() ? TEXT("") : TEXT(" - "), *Sim->GetGrowthPause());
		for (const FRHGrowthProject& P : Sim->GetGrowthProjects())
		{
			const FRHDistrict* D = Sim->FindDistrict(P.DistrictId);
			FString Bill;
			TArray<FName> Keys;
			P.Bill.GetKeys(Keys);
			Keys.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
			for (const FName& K : Keys) { Bill += FString::Printf(TEXT("%s%s %.0f"), Bill.IsEmpty() ? TEXT("") : TEXT(", "), *K.ToString(), P.Bill.FindChecked(K)); }
			UE_LOG(LogRedHope, Display, TEXT("[RH.Growth]   #%d '%s' (%s): %s - %.0f%% done, %.2f of %.2f sol left; bill %s"), P.DistrictId, D ? *D->Name : TEXT("?"), D ? *D->Kind.ToString() : TEXT("?"),
				*Sim->DescribeGrowthProject(P), P.Progress() * 100.0, P.RemainingSols, P.TotalSols, *Bill);
		}
		for (const FRHDistrict& D : Sim->GetDistricts())
		{
			if (D.Kind != FName("Homes") && D.Kind != FName("Civic"))
			{
				continue;
			}
			UE_LOG(LogRedHope, Display, TEXT("[RH.Growth]   district #%d '%s' (%s%s): growth %s%s%s"), D.Id, *D.Name, *D.Kind.ToString(), D.IsFloor() ? *FString::Printf(TEXT(", floor %d"), D.Level) : TEXT(""),
				D.bGrowth ? TEXT("ON") : TEXT("OFF"), Sim->FindGrowthProject(D.Id) ? TEXT(", a project is under way") : TEXT(""),
				Sim->GetGrowthNote(D.Id).IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" - %s"), *Sim->GetGrowthNote(D.Id)));
		}
		UE_LOG(LogRedHope, Display, TEXT("[RH.Growth]   City / Earth share: %.0f%% city, %.0f%% Earth (default %.0f%%)"), Sim->GetCityShare() * 100.0, (1.0 - Sim->GetCityShare()) * 100.0, Sim->GetCityShareDefault() * 100.0);
		for (const TCHAR* Res : { TEXT("Struct"), TEXT("Shielding"), TEXT("Oxygen") })
		{
			const URHSimWorldSubsystem::FRHGrowthAvailability A = Sim->GetGrowthAvailability(FName(Res));
			UE_LOG(LogRedHope, Display, TEXT("[RH.Growth]   %s: %.0f held - %.0f for the %s (of %.0f) - %.0f reserve = %.0f free"), Res, A.Held, A.QuotaBill, A.bNextRound ? TEXT("next quota round") : TEXT("open quota"), A.FullBill, A.Reserve, A.Free);
		}
		// C5d: the life-support books at the present head count, and at the head count the next settler ship or the Big Arrival would make it.
		for (const TCHAR* Res : { TEXT("Oxygen"), TEXT("Water"), TEXT("Food") })
		{
			const URHSimWorldSubsystem::FRHSupportLeg L = Sim->GetSupportLeg(FName(Res), Sim->GetPopulation());
			UE_LOG(LogRedHope, Display, TEXT("[RH.Growth]   %s: makes %.1f kg/sol, draws %.1f at %d people (net %.1f), stock %.0f = %.1f sols of the deficit"), Res, L.ProductionPerSol, L.DrawPerSol, Sim->GetPopulation(), L.NetPerSol(), L.StockKg, L.CoverSols);
		}
		UE_LOG(LogRedHope, Display, TEXT("[RH.Growth]   life support carries %d more settler%s (settlers need net production >= 0 on every leg and the stores behind it; the Big Arrival may also lean on %.0f sols of stock)"), Sim->GetSettlerSupportRoom(), Sim->GetSettlerSupportRoom() == 1 ? TEXT("") : TEXT("s"), Sim->GetLifeSupportDeficitCoverSols());
		if (const URHGrowthVisualizerSubsystem* Viz = World->GetSubsystem<URHGrowthVisualizerSubsystem>())
		{
			UE_LOG(LogRedHope, Display, TEXT("[RH.Growth]   %d scaffold%s drawn in the viewed level"), Viz->GetDrawnProjectCount(), Viz->GetDrawnProjectCount() == 1 ? TEXT("") : TEXT("s"));
		}
	}));

static FAutoConsoleCommandWithWorldAndArgs GRHDepositLeft(
	TEXT("RH.DepositLeft"),
	TEXT("RH.DepositLeft <Row> <kg> - DEBUG: set a deposit's remaining mass (ice-low alert and guide smoke; e.g. Ice_A 7600)."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (!World || Args.Num() < 2)
		{
			UE_LOG(LogRedHope, Error, TEXT("Usage: RH.DepositLeft <Row> <kg>"));
			return;
		}
		if (URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
		{
			for (const FRHDepositState& D : Sim->GetDeposits())
			{
				if (D.RowName == FName(*Args[0]))
				{
					Sim->Debug_SetDepositRemainingKg(D.Id, FCString::Atod(*Args[1]));
					UE_LOG(LogRedHope, Display, TEXT("[RH.DepositLeft] %s now %.0f kg of %.0f"), *D.RowName.ToString(), D.RemainingKg, D.InitialKg);
					return;
				}
			}
			UE_LOG(LogRedHope, Error, TEXT("RH.DepositLeft: no deposit named '%s'"), *Args[0]);
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
	TEXT("RH.Fleet - log every robot: position, wear (degrade/halt flags), and the leg of any road trip it is on."),
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
			const FRHRobotRoadPath* Road = Sim->GetRobotRoadPath(Entity);
			UE_LOG(LogRedHope, Display, TEXT("  robot %d at (%.0f, %.0f) m: wear %.1f (work rate x%.2f)%s%s"),
				Entity.Index, PosCm.X / 100.f, PosCm.Y / 100.f, Wear, Sim->GetWearWorkMul(Wear), Flag,
				(Road && Road->IsRoadTrip()) ? *FString::Printf(TEXT("  ON A ROAD TRIP (leg %d of %d)"), Road->Next + 1, Road->Waypoints.Num()) : TEXT(""));
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
