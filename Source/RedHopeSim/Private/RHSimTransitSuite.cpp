#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHAgentSubsystem.h"
#include "RHAgentFragments.h"
#include "RHRobotMovement.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// D4 self-test (transit: lifts, capacity and robots below; docs/qa/2026-10-21/d4-transit-design.md, the REVISION, Q-A (a),
// Q-B (a)). Fails loudly. `-transit`. The headless world ticks no Mass robot, so robots are played by the harness through the
// very calls the brains make (RH::MoveAlong, HaulLoad, BeginHandoffHold / StepHandoffHold, HaulUnload, TryClaim*,
// DeepChargeTest) on harness transforms, or on the real fleet entities where a test needs the agent subsystem to see them
// (repair claims, the save). Holds and rides are counted in sub-steps; nothing here uses a distance timer.
//  TR0 the rows and caps; TR1 the inert twins (O6); TR2 the FitLift refusals and the per-source bill charge (O5);
//  TR3 the threshold (agent band, era, a jump, the slot tag, the era bill stamp, the robot wait guard); TR4 the wait;
//  TR5 order independence (the batched-frame driver, both orders, entity order reversed, K = 1, 7, 32; the boundary case);
//  TR6 the people load (ramp permille at 60, 61, 160; the k mod n split; O4); TR7 the commute wait and the caches;
//  TR8 a second car, a second shaft; TR9 the hand-off hold (no road and road); TR10 the planner; TR11 the ride;
//  TR12 haul and build below, the fallback, TN12b, the source-below stall; TR13 claims; TR14 charge (TN14, TN14b, TR14c);
//  TR15 repair across levels; TR16b floor freight is hauler work; TR17 era parity; TR18 the save.
namespace
{
	struct FTransitSuiteBot
	{
		FMassEntityHandle H;
		FTransform X;
		FRHRobotFragment R;
		FRHTaskFragment T;
		float ChargeWh = 0.f;
		float CapWh = 0.f;
		int32 HandoffSteps = 0;   // sub-steps spent in a head hand-off hold
		int32 RideHolds = 0;      // sub-steps spent holding at a column before a ride
		int32 Unloads = 0;
		double DeliveredKg = 0.0;
		float MinChargeWh = 1.0e9f;
	};
}

int32 RHRunTransitSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== TRANSIT TEST (D4, 2026-10-21) ==="));
	int32 Fails = 0, Checks = 0;
	const auto Expect = [&](bool bOk, const FString& What)
	{
		++Checks;
		if (bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("TRANSIT ok: %s"), *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("TRANSIT FAIL: %s"), *What); ++Fails; }
	};
	constexpr double EraDt = (double)URHSimClockSubsystem::EraStepSimSeconds;
	constexpr double SolLen = (double)URHSimClockSubsystem::SolLengthSimSeconds;
	constexpr float AgentDt = URHSimClockSubsystem::SubStepSeconds;
	const double FloorH = Sim->GetFloorHeightCm();
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	URHAgentSubsystem* Agents = World->GetSubsystem<URHAgentSubsystem>();
	const FName NStruct(TEXT("Struct")), NOx(TEXT("Oxygen"));
	FString R, Why;
	TArray<FString> Rejects;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& Text) { Rejects.Add(Text); });
	const auto LastReject = [&]() { return Rejects.Num() > 0 ? Rejects.Last() : FString(); };
	const auto Finish = [&]() -> int32
	{
		Sim->OnCommandRejected.Remove(RejectH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== TRANSIT TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const auto EraOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->EraStep((float)EraDt);
	};
	const auto AgentOnce = [&]()
	{
		Clock->Debug_AdvanceSimSeconds((double)AgentDt);
		Sim->Debug_SetBatteryWh(1.0e9);
		Sim->Debug_StepSim(AgentDt);
	};
	const auto FindB = [&](int32 Id) -> const FRHBuildingInstance*
	{
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.Id == Id) { return &Bd; } }
		return nullptr;
	};
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (Bd.DefName == FName(Def)) { Id = Bd.Id; } } return Id; };
	const auto InKg = [&](int32 Id, FName Res) { const FRHBuildingInstance* Bd = FindB(Id); const double* V = Bd ? Bd->InputKg.Find(Res) : nullptr; return V ? *V : 0.0; };
	const auto Send = [&](const FRHCommand& C)
	{
		Sim->EnqueueCommand(C);
		for (int32 K = 0; K < 10 && Sim->GetUplinkQueue().Num() > 0; ++K) { EraOnce(); }
	};
	const auto SaveFilePath = [](const TCHAR* Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / FString::Printf(TEXT("RH_%s.sav"), Slot); };
	FString SaveErr;
	const bool bBaseSaved = Sim->SaveColony(TEXT("transitbase"), SaveErr);
	int32 LanderId = 0;
	const auto Reset = [&]()
	{
		FString E;
		const bool bOk = Sim->LoadColony(TEXT("transitbase"), E);
		if (!bOk || !E.IsEmpty()) { UE_LOG(LogRedHopeSim, Error, TEXT("TRANSIT FAIL: loading the base failed: %s"), *E); ++Fails; }
		Rejects.Reset();
		Sim->Debug_ForgetRobotTransit();
		LanderId = LastIdOf(TEXT("Lander"));
		return bOk;
	};
	// The lattice: shaft 0's head is the origin, cell (x, y) on floor L is at origin + 1000 x (x, y), Z = L x FloorH.
	const FVector Head0(2000.0, 2000.0, 0.0);
	const FVector Head1(32000.0, 3000.0, 0.0); // shaft 1 at (30, 1)
	const auto Origin = [&]() { return Sim->GetShaftHeadCm(); };
	const auto Pt = [&](int32 Level, int32 Px, int32 Py) { FVector V = URHSimWorldSubsystem::LatticeToCm(Origin(), FIntPoint(Px, Py)); V.Z = Level * FloorH; return V; };
	const auto RobotZ = [&](int32 Level) { return Level * FloorH + RH::RobotZOffsetCm; };
	const auto At = [&](int32 Level, int32 Px, int32 Py) { FVector V = Pt(Level, Px, Py); V.Z = RobotZ(Level); return V; };
	const auto HeadXY = [&](int32 S) { const FVector H = Sim->GetShaft(S).HeadCm; return FVector(H.X, H.Y, 0.0); };
	const auto Place = [&](const TCHAR* Def, const FVector& Loc, int32 Level)
	{
		Sim->Debug_PlaceInstant(FName(Def), FVector(Loc.X, Loc.Y, 0.0), Level);
		return LastIdOf(Def);
	};
	const auto Ord = [&](int32 Level, int32 Px, int32 Py) { return Sim->FindFloorCell(Level, FIntPoint(Px, Py)); };
	const auto Fit = [&](int32 S)
	{
		Sim->Debug_AddSolidAt(LanderId, NStruct, Sim->GetLiftFitBillKg(), false);
		FString W;
		const bool bOk = Sim->Debug_FitLift(S, W);
		if (!bOk) { UE_LOG(LogRedHopeSim, Warning, TEXT("TRANSIT: fit on shaft %d refused: %s"), S, *W); }
		return bOk;
	};
	// One shaft bored to Depth, Cells1 / Cells2 cells carved on -1 / -2 (shaft 0's spiral: (1,0) (1,1) (0,1) (-1,1) (-1,0)
	// (-1,-1) (0,-1) (1,-1) (2,-1) (2,0) (2,1) (2,2) ...), the landing on every floor is (1,0) (ordinal 0).
	const auto OneShaft = [&](int32 Depth, int32 Cells1, int32 Cells2)
	{
		Reset();
		Sim->ExtendShaft(Depth, Head0);
		if (Cells1 > 0) { Sim->ExcavateFloor(-1, Cells1, R, 0); }
		if (Cells2 > 0) { Sim->ExcavateFloor(-2, Cells2, R, 0); }
		return Sim->GetFloorCarvedCells(-1) == Cells1 && Sim->GetFloorCarvedCells(-2) == Cells2;
	};
	// Shaft 1 at (30, 1), with its own six-cell dig on -1 (and -2 when Depth is 2).
	const auto SinkOne = [&](int32 Depth, int32 Cells)
	{
		Place(TEXT("Pylon"), FVector(32000.0, 2000.0, 0.0), 0);
		int32 Id = INDEX_NONE;
		const bool bOk = Sim->Debug_SinkShaftAt(Head1, Depth, Id, Why);
		if (Cells > 0) { Sim->ExcavateFloor(-1, Cells, R, 1); }
		if (Cells > 0 && Depth >= 2) { Sim->ExcavateFloor(-2, Cells, R, 1); }
		return bOk && Id == 1;
	};
	const auto LedgerDigest = [&]()
	{
		FString D = FString::Printf(TEXT("chk%d |"), Sim->GetTransitCheckedSol()); // never the transient counters: they run on across loads
		for (int32 S = 0; S < Sim->GetTransitLedgers().Num(); ++S)
		{
			const FRHTransitLedger& L = Sim->GetTransitLedgers()[S];
			D += FString::Printf(TEXT("[%d: %d,%lld,%d %d,%lld,%d %d,%lld,%d w%d p%d]"), S, L.Sol[0], L.Kg[0], L.Places[0], L.Sol[1], L.Kg[1], L.Places[1], L.Sol[2], L.Kg[2], L.Places[2], L.WaitSec, L.PrevWaitSec);
		}
		return D;
	};
	const auto SlotPlaces = [&](int32 S, int32 Sol) { const FRHTransitLedger* L = Sim->GetTransitLedgers().IsValidIndex(S) ? &Sim->GetTransitLedgers()[S] : nullptr; return (L && Sol >= 0 && L->Sol[Sol % 3] == Sol) ? L->Places[Sol % 3] : 0; };
	const auto SlotKg = [&](int32 S, int32 Sol) { const FRHTransitLedger* L = Sim->GetTransitLedgers().IsValidIndex(S) ? &Sim->GetTransitLedgers()[S] : nullptr; return (L && Sol >= 0 && L->Sol[Sol % 3] == Sol) ? L->Kg[Sol % 3] : (int64)0; };
	const auto SolNow = [&]() { return FMath::FloorToInt32(Clock->GetSimSecondsTotal() / SolLen); };

	// The harness robot: the brains' Haul and drive steps, call for call.
	int32 NextHandle = 990000;
	const auto MakeBot = [&](const TCHAR* Row, const FVector& Pos) -> FTransitSuiteBot
	{
		FTransitSuiteBot Bot;
		Bot.H = FMassEntityHandle(NextHandle++, 1);
		Bot.X = FTransform(FQuat::Identity, Pos);
		if (const FRHRobotRow* Def = DefsSub ? DefsSub->GetRobot(FName(Row)) : nullptr)
		{
			Bot.R.DefName = FName(Row);
			Bot.R.RobotClass = Def->RobotClass;
			Bot.R.SpeedMps = Def->Speed_mps;
			Bot.R.CargoCapKg = Def->Cargo_kg;
			Bot.R.WorkRate = Def->WorkRate;
			Bot.R.DrawMoveW = Def->DrawMove_W;
			Bot.R.DrawWorkW = Def->DrawWork_W;
			Bot.R.DrawIdleW = Def->DrawIdle_W;
			Bot.R.WearPerSol = Def->WearPerActiveSol;
			Bot.CapWh = Def->Battery_Wh;
			Bot.ChargeWh = Def->Battery_Wh;
		}
		return Bot;
	};
	const auto BotLevel = [&](const FTransitSuiteBot& Bot) { return Sim->HasRobotLifts() ? Sim->LevelOfZ(Bot.X.GetLocation().Z) : 0; };
	const auto StartHaul = [&](FTransitSuiteBot& Bot, int32 TaskId)
	{
		FRHTask Board;
		Sim->Debug_GetTask(TaskId, Board);
		Bot.T = FRHTaskFragment();
		Bot.T.TaskType = (uint8)ERHTaskType::Haul;
		Bot.T.TaskId = TaskId;
		Bot.T.CargoResource = Board.Resource;
		Bot.T.TargetCm = Sim->HasRobotLifts() ? Sim->GetRobotTarget(Board.From, BotLevel(Bot), Bot.X.GetLocation(), Bot.T.TargetLevel) : Sim->GetSiteLocation(Board.From);
	};
	const auto Drain = [&](FTransitSuiteBot& Bot, float DrawW)
	{
		Bot.ChargeWh = FMath::Max(0.f, Bot.ChargeWh - DrawW * (AgentDt / 50.f));
		Bot.MinChargeWh = FMath::Min(Bot.MinChargeWh, Bot.ChargeWh);
	};
	// One Haul sub-step (FRHWorkTask::Tick's Haul case). True on the sub-step the load is credited.
	const auto HaulTick = [&](FTransitSuiteBot& Bot) -> bool
	{
		FRHTaskFragment& Task = Bot.T;
		const int32 Level = BotLevel(Bot);
		float DrawW = Bot.R.DrawMoveW;
		bool bUnloaded = false;
		const auto Move = [&](float Cargo)
		{
			const bool bArrived = RH::MoveAlong(*Sim, Bot.H, Bot.X, Task.TargetCm, Bot.R.SpeedMps, AgentDt, false, Cargo, Task.TargetLevel);
			if (Sim->DidRobotHoldThisStep(Bot.H)) { DrawW = Bot.R.DrawIdleW; ++Bot.RideHolds; }
			return bArrived;
		};
		if (Task.Phase == 0)
		{
			if (Move(0.f)) { Task.Phase = 1; }
		}
		else if (Task.Phase == 1)
		{
			float Loaded = 0.f;
			FVector Drop = FVector::ZeroVector;
			if (Sim->HaulLoad(Task.TaskId, Bot.R.CargoCapKg, Loaded, Drop, Level, &Task.TargetLevel))
			{
				Task.CargoKg = Loaded;
				Task.TargetCm = Drop;
				Task.Phase = 2;
			}
			else { Task = FRHTaskFragment(); }
		}
		else if (Task.Phase == 2)
		{
			if (Sim->InHandoffHold(Bot.H, Task.TaskId, Bot.X.GetLocation()))
			{
				DrawW = Bot.R.DrawIdleW;
				++Bot.HandoffSteps;
				if (Sim->StepHandoffHold(Bot.H))
				{
					Sim->HaulUnload(Task.TaskId, Task.CargoKg, NAME_None, Level);
					Bot.DeliveredKg += Task.CargoKg;
					++Bot.Unloads;
					Task = FRHTaskFragment();
					bUnloaded = true;
				}
			}
			else if (Move(Task.CargoKg))
			{
				const int32 Now = BotLevel(Bot);
				if (Sim->BeginHandoffHold(Bot.H, Task.TaskId, Now, Bot.X.GetLocation()) == 0)
				{
					Sim->HaulUnload(Task.TaskId, Task.CargoKg, NAME_None, Now);
					Bot.DeliveredKg += Task.CargoKg;
					++Bot.Unloads;
					Task = FRHTaskFragment();
					bUnloaded = true;
				}
			}
		}
		Drain(Bot, DrawW);
		return bUnloaded;
	};
	// Drive a bot to a target (the Charge drive), counting sub-steps; holds draw idle. -1 when it never arrives.
	const auto Drive = [&](FTransitSuiteBot& Bot, const FVector& Target, int32 TargetLevel, int32 MaxSteps, bool bWorld = false)
	{
		for (int32 K = 1; K <= MaxSteps; ++K)
		{
			if (bWorld) { Sim->SetRobotStepEnd(Clock->GetSimSecondsTotal() + (double)AgentDt); }
			const bool bArrived = RH::MoveAlong(*Sim, Bot.H, Bot.X, Target, Bot.R.SpeedMps, AgentDt, false, 0.f, TargetLevel);
			const bool bHeld = Sim->DidRobotHoldThisStep(Bot.H);
			if (bHeld) { ++Bot.RideHolds; }
			Drain(Bot, bHeld ? Bot.R.DrawIdleW : Bot.R.DrawMoveW);
			if (bWorld) { Sim->SetRobotStepEnd(-1.0); AgentOnce(); }
			if (bArrived) { return K; }
			if (Bot.ChargeWh <= 0.f) { return -1; }
		}
		return -1;
	};
	const auto PlanCost = [&](int32 FromL, const FVector& From, int32 ToL, const FVector& To, FRHDeepRouteCost& OutCost)
	{
		FRHRobotRoadPath Path;
		return Sim->PlanDeepRoute(FromL, From, ToL, To, Clock->GetSimSecondsTotal(), Path, &OutCost);
	};

	// --- TR0. The rows, the caps, a fresh colony. ---
	{
		Expect(bBaseSaved, FString::Printf(TEXT("TR0 the fresh colony is saved as the base every case loads (%s)"), *SaveErr));
		struct FRowPin { const TCHAR* Row; double Value; double Got; };
		const FRowPin Rows[] = {
			{ TEXT("LiftFitStructKg"), 400.0, Sim->GetLiftFitBillKg() }, // the first car's bill before any fit (Q-A (a): the same 400)
			{ TEXT("FirstLiftFitStructKg"), 400.0, Sim->GetLiftFitBillKg() },
			{ TEXT("MaxLiftCars"), 4.0, (double)Sim->GetMaxLiftCars() },
			{ TEXT("HoistPlacesPerSol"), 200.0, (double)Sim->GetHoistPlacesPerSol() },
			{ TEXT("LiftPlacesPerSol"), 1000.0, (double)Sim->GetLiftPlacesPerSol() },
			{ TEXT("LiftKgPerPlace"), 100.0, (double)Sim->GetLiftKgPerPlace() },
			{ TEXT("HoistDwellSeconds"), 30.0, (double)Sim->GetHoistDwellSeconds() },
			{ TEXT("HoistSecondsPerFloor"), 15.0, (double)Sim->GetHoistSecondsPerFloor() },
			{ TEXT("LiftDwellSeconds"), 10.0, (double)Sim->GetLiftDwellSeconds() },
			{ TEXT("LiftSecondsPerFloor"), 4.0, (double)Sim->GetLiftSecondsPerFloor() },
			{ TEXT("LiftPlanSpeedMps"), 4.0, Sim->GetLiftPlanSpeedCmPerS() / 100.0 },
			{ TEXT("LiftPlanCm"), 1500.0, (double)Sim->GetLiftPlanCm() },
			{ TEXT("WaitSecAtDouble"), 120.0, (double)Sim->GetWaitSecAtDouble() },
			{ TEXT("MaxWaitSec"), 600.0, (double)Sim->GetMaxWaitSec() },
			{ TEXT("CommuteLiftCmPerFloor"), 2000.0, (double)Sim->GetCommuteLiftCmPerFloor() },
			{ TEXT("CommuteWaitCmPerSec"), 100.0, (double)Sim->GetCommuteWaitCmPerSec() },
			{ TEXT("CommuteShaftCmPerFloor"), 6000.0, (double)Sim->GetCommuteShaftCmPerFloor() },
		};
		for (const FRowPin& Pin : Rows)
		{
			const double Dt = DefsSub ? DefsSub->GetConfigTableScalar(FName(Pin.Row), -1.0) : -1.0;
			Expect(Dt == Pin.Value && Pin.Got == Pin.Value, FString::Printf(TEXT("TR0 %s is %g in the live DT_Config and %g in the sim (the CSV says %g; pure data: this fails on drift)"), Pin.Row, Dt, Pin.Got, Pin.Value));
		}
		Expect(RH::MaxLiftCarsCap == 8 && RH::MaxWaitSecCap == 1200 && Sim->GetMaxLiftCars() <= RH::MaxLiftCarsCap && Sim->GetMaxWaitSec() <= RH::MaxWaitSecCap,
			TEXT("TR0 the caps: MaxLiftCars within 1..8, MaxWaitSec within 0..1200 (one sol), the save refuses against the same caps"));
		Expect(FloorH > 100.0 && FMath::IsNearlyEqual(URHSimWorldSubsystem::GetTransitRobotLagSeconds(), 3.3, 1.0e-6),
			FString::Printf(TEXT("TR0 floors are %.0f cm (over 100: a level is read from Z); the robots' wait lag is (32 + 1) sub-steps = %.7f s"), FloorH, URHSimWorldSubsystem::GetTransitRobotLagSeconds()));
		Expect(RH::LevelOfZ(50.0, FloorH) == 0 && RH::LevelOfZ(-350.0, FloorH) == -1 && RH::LevelOfZ(-750.0, FloorH) == -2 && RH::LevelOfZ(-151.0, FloorH) == -1 && RH::LevelOfZ(-150.0, FloorH) == 0,
			TEXT("TR0 the level from Z is rounded, never truncated: Z 50 is 0, -350 is -1, -750 is -2 (past the half-way point, -151, is -1; the half-way point -150 rounds up to 0)"));
		Expect(!Sim->HasTransit() && !Sim->HasRobotLifts() && Sim->GetTransitLedgers().Num() == 0 && Sim->GetTransitCheckedSol() == INDEX_NONE && Sim->GetLiftVersion() == 0 && Sim->GetTransitWaitVersion() == 0,
			TEXT("TR0 a fresh colony has no transit: no car, no ledger, the threshold sol -1, both versions 0"));
	}

	// --- TR1 (O6). Two twins from one start: up to the fit they are bit-identical; after it, the fit acts. ---
	{
		Expect(OneShaft(1, 12, 0), TEXT("TR1 setup: shaft 0 bored to -1, twelve cells"));
		Sim->Debug_AddSolidAt(LanderId, NStruct, 4000.0, false);
		Sim->AddStock(NOx, 200.0);
		FRHCommand Build; Build.Verb = FName("Build"); Build.Target = FName("AirFilter"); Build.Level = -1; Build.Location = Pt(-1, 2, 2);
		Send(Build);
		Expect(Sim->SaveColony(TEXT("transittwin"), SaveErr), FString::Printf(TEXT("TR1 the twins' start is saved (%s)"), *SaveErr));
		const auto RunTwin = [&](bool bFit, TArray<FString>& OutDigest)
		{
			FString Le;
			Sim->LoadColony(TEXT("transittwin"), Le);
			Sim->Debug_ForgetRobotTransit();
			LanderId = LastIdOf(TEXT("Lander"));
			TArray<FTransitSuiteBot> Bots;
			Bots.Add(MakeBot(TEXT("RC_H"), FVector(-800.0, -600.0, 50.0)));
			Bots.Add(MakeBot(TEXT("RC_H"), FVector(-550.0, -600.0, 50.0)));
			for (int32 K = 0; K < 1200; ++K)
			{
				if (bFit && K == 600) { Fit(0); }
				FString D = FString::Printf(TEXT("%d st%.6f hope%.9f fr%d dj%d tr%d |"), K, Sim->GetTotalSolid(NStruct), Sim->GetColonyHope().Total, Sim->GetCommuteFlowRebuilds(), Sim->GetCommuteDijkstraRuns(), Sim->HasTransit() ? 1 : 0);
				for (FTransitSuiteBot& Bot : Bots)
				{
					Sim->SetRobotStepEnd(Clock->GetSimSecondsTotal() + (double)AgentDt);
					if (Bot.T.TaskType == 0)
					{
						FRHTask Board;
						if (Sim->TryClaimHaul(Bot.H, Bot.X.GetLocation(), Board, BotLevel(Bot))) { StartHaul(Bot, Board.Id); D += FString::Printf(TEXT("c%d;"), Board.Id); }
					}
					else { HaulTick(Bot); }
					Sim->SetRobotStepEnd(-1.0);
					const FVector P = Bot.X.GetLocation();
					D += FString::Printf(TEXT("%.3f,%.3f,%.1f,%d;"), P.X, P.Y, P.Z, (int32)Bot.T.Phase);
				}
				OutDigest.Add(D);
				AgentOnce();
			}
		};
		TArray<FString> A, B;
		RunTwin(true, A);
		RunTwin(false, B);
		int32 FirstDiff = INDEX_NONE;
		for (int32 K = 0; K < FMath::Min(A.Num(), B.Num()); ++K) { if (A[K] != B[K]) { FirstDiff = K; break; } }
		Expect(A.Num() == 1200 && B.Num() == 1200 && FirstDiff == 600,
			FString::Printf(TEXT("TR1 twin A fits a car at sub-step 600, twin B never does: every digest (stocks, Hope, the commute's rebuild and Dijkstra counts, every claim, every bot position and phase) is bit-identical before it; the first difference is sub-step %d (expect 600)"), FirstDiff));
	}

	// --- TR2. FitLift: the four refusals, each by its reason; the per-source charge of a bill taken from a floor (O5). ---
	{
		Expect(OneShaft(1, 12, 0) && SinkOne(1, 6), TEXT("TR2 setup: shaft 0 and shaft 1 at (30,1), each bored to -1 with its own dig"));
		FString W;
		Expect(!Sim->CanFitLift(7, W) && W.Contains(TEXT("Unknown shaft 7")), FString::Printf(TEXT("TR2 (1) an unknown shaft is refused (%s)"), *W));
		Place(TEXT("Pylon"), FVector(-28000.0, 2000.0, 0.0), 0);
		int32 Id2 = INDEX_NONE;
		const bool bSunk2 = Sim->Debug_SinkShaftAt(FVector(-28000.0, 2000.0, 0.0), 0, Id2, Why);
		Expect(bSunk2 && Id2 == 2 && !Sim->CanFitLift(2, W) && W.Contains(TEXT("not bored")), FString::Printf(TEXT("TR2 (2) a shaft of depth 0 is refused (%s)"), *W));
		Sim->Debug_AddSolidAt(LanderId, NStruct, 10000.0, false);
		bool bFour = true;
		for (int32 K = 0; K < 4; ++K) { bFour &= Sim->Debug_FitLift(0, W); }
		Expect(bFour && Sim->GetLiftCars(0) == 4 && !Sim->CanFitLift(0, W) && W.Contains(TEXT("4 lift cars")), FString::Printf(TEXT("TR2 (3) four cars fit on shaft 0; a fifth is refused (%s)"), *W));
		// (4) through the uplink: the Struct is gone when the order lands.
		Rejects.Reset();
		const double Held = Sim->GetRoadStructAvailableKg();
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (InKg(Bd.Id, NStruct) > 0.0) { Sim->Debug_AddSolidAt(Bd.Id, NStruct, -InKg(Bd.Id, NStruct), false); } }
		FRHCommand C; C.Verb = FName("FitLift"); C.Value = 1;
		Send(C);
		Expect(Held > 0.0 && LastReject().Contains(TEXT("Insufficient Struct")) && Sim->GetLiftCars(1) == 0, FString::Printf(TEXT("TR2 (4) short of Struct, the order is refused when it lands (%s)"), *LastReject()));
		// The per-source charge: shaft 1's second car from two floor stores in two digs - a 150 kg part in dig 0 (shaft 0's) and a
		// 250 kg part in dig 1 (shaft 1's): ceil(150 / 100) = 2 places on shaft 0 and ceil(250 / 100) = 3 on shaft 1.
		const int32 StoreA = Place(TEXT("Stockpile"), Pt(-1, 1, 1), -1);
		Sim->Debug_AddSolidAt(StoreA, NStruct, 150.0, false);
		const int32 StoreB = Place(TEXT("Stockpile"), Pt(-1, 31, 1), -1);
		Sim->Debug_AddSolidAt(StoreB, NStruct, 600.0, false);
		const FString Before = LedgerDigest();
		Rejects.Reset();
		Send(C); // shaft 1's first car: 150 from A, 250 from B
		const int32 S = SolNow(); // the era step the order landed in ends now
		Expect(Sim->GetLiftCars(1) == 1 && SlotPlaces(0, S) == 2 && SlotKg(0, S) == 150 && SlotPlaces(1, S) == 3 && SlotKg(1, S) == 250,
			FString::Printf(TEXT("TR2 a bill taken from two floor stores charges each part to its own shaft: shaft 0 %d places / %lld kg (expect 2 / 150), shaft 1 %d / %lld (expect 3 / 250) - not ceil(400 / 100) = 4 on one shaft (before %s)"),
				SlotPlaces(0, S), SlotKg(0, S), SlotPlaces(1, S), SlotKg(1, S), *Before));
		// The first fit's own bill writes nothing, even from a floor store: there is no transit system during its take.
		OneShaft(1, 12, 0);
		const int32 StoreC = Place(TEXT("Stockpile"), Pt(-1, 1, 1), -1);
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (InKg(Bd.Id, NStruct) > 0.0) { Sim->Debug_AddSolidAt(Bd.Id, NStruct, -InKg(Bd.Id, NStruct), false); } }
		Sim->Debug_AddSolidAt(StoreC, NStruct, 400.0, false);
		Expect(Sim->Debug_FitLift(0, W) && Sim->GetTransitLedgers().Num() == 1 && SlotPlaces(0, SolNow()) == 0 && InKg(StoreC, NStruct) == 0.0,
			FString::Printf(TEXT("TR2 the colony's first car takes its 400 kg from a floor store and writes no ledger entry (%s)"), *LedgerDigest()));
	}

	// --- TR3. The threshold: one roll a sol in both bands, a jump rolls once, the slot tag, the era bill stamp, the wait guard. ---
	{
		Expect(OneShaft(1, 12, 0), TEXT("TR3 setup"));
		const int32 S0 = SolNow();
		Expect(Fit(0) && Sim->GetTransitCheckedSol() == S0 && Sim->GetTransitWaitSec(0) == 0 && Sim->GetTransitLedgers().Num() == 1,
			FString::Printf(TEXT("TR3 the first fit sets the threshold sol to the current sol (%d), every wait 0, one ledger a shaft"), S0));
		// The agent band: exactly one roll a sol (12,000 sub-steps).
		const int32 T0 = Sim->GetTransitThresholds();
		while (SolNow() == S0) { AgentOnce(); }
		const int32 T1 = Sim->GetTransitThresholds();
		for (int32 K = 0; K < 12000; ++K) { AgentOnce(); }
		const int32 T2 = Sim->GetTransitThresholds();
		Expect(T1 == T0 + 1 && T2 == T1 + 1 && Sim->GetTransitCheckedSol() == SolNow(), FString::Printf(TEXT("TR3 the agent band rolls once at the boundary and once more over the next 12,000 sub-steps (%d, %d, %d)"), T0, T1, T2));
		// Era: one roll in 20 steps.
		const int32 T3 = Sim->GetTransitThresholds();
		for (int32 K = 0; K < 20; ++K) { EraOnce(); }
		Expect(Sim->GetTransitThresholds() == T3 + 1, FString::Printf(TEXT("TR3 the era band rolls once in 20 one-minute steps (%d)"), Sim->GetTransitThresholds() - T3));
		// A three-sol jump rolls once, to the latest sol.
		const int32 T4 = Sim->GetTransitThresholds();
		Clock->Debug_AdvanceSimSeconds(3.0 * SolLen);
		Sim->Debug_StepSim(AgentDt);
		Expect(Sim->GetTransitThresholds() == T4 + 1 && Sim->GetTransitCheckedSol() == SolNow(), TEXT("TR3 a jump of three sols rolls once, to the latest sol"));
		// The roll reads sol s-2's slot only when the tag matches: a slot tagged s-5 (same slot, an older tag) counts nothing.
		const int32 S = SolNow();
		Sim->Debug_SetTransitCapacity(20, 20);
		Sim->Debug_ChargeTransit(0, 40, (S - 4) * SolLen + 10.0); // tag s-4 in slot (s-4) % 3 = (s-1) % 3
		Sim->Debug_RollTransit(S + 1); // reads sol s-1's slot, which holds s-4's tag: nothing counts
		const int32 WaitStale = Sim->GetTransitWaitSec(0);
		Sim->Debug_ChargeTransit(0, 40, (S + 1) * SolLen + 10.0); // sol s+1's slot
		Sim->Debug_RollTransit(S + 3);                              // reads s+1: 40 places vs 20 -> ceil(20 x 120 / 20) = 120
		Expect(WaitStale == 0 && Sim->GetTransitWaitSec(0) == 120, FString::Printf(TEXT("TR3 a slot whose tag is not s-2 counts nothing (wait %d); one tagged s-2 does (wait %d, expect 120)"), WaitStale, Sim->GetTransitWaitSec(0)));
		Sim->Debug_SetTransitCapacity(200, 1000);
		// The era stamp: a road bill from a floor store, in the first era step of a four-step frame that crosses a boundary,
		// lands in the FIRST step's sol (REVISION item 2(a)).
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR3 era-stamp setup: a fitted shaft"));
		const int32 Store = Place(TEXT("Stockpile"), Pt(-1, 1, 1), -1);
		Place(TEXT("Pylon"), FVector(3000.0, -4000.0, 0.0), 0); // grid coverage for the road
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (InKg(Bd.Id, NStruct) > 0.0) { Sim->Debug_AddSolidAt(Bd.Id, NStruct, -InKg(Bd.Id, NStruct), false); } }
		Sim->Debug_AddSolidAt(Store, NStruct, 500.0, false);
		// The frame: four 60 s steps ending at Boundary - 30, + 30, + 90, + 150; the order (40 m of road, 80 kg) is queued
		// so it lands in the first step. Two sols ahead, so the clock only moves forward.
		const double Boundary = (SolNow() + 2) * SolLen;
		const int32 FirstSol = FMath::FloorToInt32((Boundary - 30.0) / SolLen);
		const double FrameEnd = Boundary + 150.0;
		Clock->Debug_SetSimSeconds(FrameEnd - 4.0 * EraDt);
		FRHCommand Road; Road.Verb = FName("BuildRoad"); Road.Location = FVector(1000.0, -2000.0, 0.0); Road.LocationB = FVector(5000.0, -2000.0, 0.0);
		Sim->EnqueueCommand(Road);
		Clock->Debug_SetSimSeconds(FrameEnd);
		for (int32 K = 0; K < 4; ++K) { Sim->Debug_SetBatteryWh(1.0e9); Sim->Debug_EraStepAt((float)EraDt, FrameEnd - (3 - K) * EraDt); }
		Expect(SlotPlaces(0, FirstSol) == 1 && SlotKg(0, FirstSol) == 80 && SlotPlaces(0, FirstSol + 1) == 0 && Sim->GetRoadGraph().Edges.Num() > 0 && InKg(Store, NStruct) == 420.0,
			FString::Printf(TEXT("TR3 the road bill (40 m, 80 kg from the floor store) was charged to the sol its era step ends in: %d place in sol %d (expect 1), %d in sol %d (expect 0) - the step's end, never the frame clock (%s)"),
				SlotPlaces(0, FirstSol), FirstSol, SlotPlaces(0, FirstSol + 1), FirstSol + 1, *LastReject()));
		// The robot wait guard: 1.0 s after a boundary a robot reads the previous sol's wait in either order; 3.4 s after, the new.
		Expect(OneShaft(1, 12, 0), TEXT("TR3 guard setup"));
		Clock->Debug_AdvanceSimSeconds(2.0 * SolLen);
		Expect(Fit(0), TEXT("TR3 guard setup: fitted two sols on"));
		const int32 Sg = SolNow();
		Sim->Debug_ChargeTransit(0, 3000, (Sg - 1) * SolLen + 600.0); // sol s-1's slot: the roll into s+1 reads it
		const int32 Target = Sg + 1;
		const double Bg = Target * SolLen;
		const int32 Old = Sim->GetRobotWaitSec(0, Bg + 1.0);   // before the world rolls into s+1 (robots-first)
		Sim->Debug_RollTransit(Target);
		const int32 New = Sim->GetTransitWaitSec(0);
		const int32 After1 = Sim->GetRobotWaitSec(0, Bg + 1.0);  // after it (world-first)
		const int32 After34 = Sim->GetRobotWaitSec(0, Bg + 3.4);
		Expect(Old == 0 && After1 == 0 && New > 0 && After34 == New,
			FString::Printf(TEXT("TR3 a boarding stamped 1.0 s after the boundary reads the previous sol's wait under both batch orders (%d, %d); at 3.4 s it reads the new one (%d = %d)"), Old, After1, After34, New));
	}

	// --- TR4. The wait, by hand. ---
	{
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR4 setup"));
		const int32 Wh[4] = { Sim->ComputeTransitWaitSec(200, 200), Sim->ComputeTransitWaitSec(201, 200), Sim->ComputeTransitWaitSec(400, 200), Sim->ComputeTransitWaitSec(2000, 200) };
		Expect(Wh[0] == 0 && Wh[1] == 1 && Wh[2] == 120 && Wh[3] == 600, FString::Printf(TEXT("TR4 a hoist (200 places): at Cap %d s, Cap + 1 %d s (ceil 120/200), 2 x Cap %d s, 10 x Cap %d s (1,080 clamped to MaxWaitSec 600)"), Wh[0], Wh[1], Wh[2], Wh[3]));
		const int32 W1 = Sim->ComputeTransitWaitSec(1001, 1000), W2 = Sim->ComputeTransitWaitSec(2000, 1000), W4a = Sim->ComputeTransitWaitSec(4001, 4000), W4b = Sim->ComputeTransitWaitSec(8000, 4000);
		Expect(W1 == 1 && W2 == 120 && W4a == 1 && W4b == 120, FString::Printf(TEXT("TR4 one car (1,000): Cap + 1 %d s, 2 x Cap %d s; four cars (4,000): Cap + 1 %d s, 2 x Cap %d s"), W1, W2, W4a, W4b));
		Expect(Sim->GetShaftCapacityPlaces(0) == 1000 && Fit(0) && Fit(0) && Fit(0) && Sim->GetShaftCapacityPlaces(0) == 4000, TEXT("TR4 shaft 0's capacity is 1,000 with one car and 4,000 with four"));
		const int32 S = SolNow();
		Sim->Debug_ChargeTransit(0, 6000, S * SolLen + 1.0);
		Sim->Debug_RollTransit(S + 2);
		Expect(Sim->GetTransitWaitSec(0) == 60, FString::Printf(TEXT("TR4 a real roll: 6,000 cargo places through four cars is ceil(2,000 x 120 / 4,000) = 60 s (%d)"), Sim->GetTransitWaitSec(0)));
		Expect(SinkOne(1, 6) && Sim->GetShaftCapacityPlaces(1) == 200 && Sim->GetTransitLedgers().Num() == 2,
			TEXT("TR4 a shaft sunk after the fit is a 200-place hoist with its own zero ledger"));
	}


	// --- TR5. Order independence: the batched-frame driver, robots-first and world-first, entity order forward and reversed,
	// K = 1, 7 and 32 sub-steps a frame; then the boundary case. (REVISION item 3.)
	{
		// One frame of K sub-steps, as Clock::Tick + the world's Tick + the brain processor run it: the clock advances the whole
		// frame first; each robot runs all K of its sub-steps (entity-major), each stamped with its own end; the world runs K
		// steps with StepEndOverride set as in Tick. The two batches in either order.
		const auto DriveFrame = [&](int32 K, bool bRobotsFirst, bool bReverse, TArray<FTransitSuiteBot>& Bots, const TFunctionRef<void(FTransitSuiteBot&, int32)>& RobotSub, TFunctionRef<void()> AfterWorldStep)
		{
			check(K >= 1 && K <= URHSimClockSubsystem::MaxSubStepsPerFrame);
			Clock->Debug_AdvanceSimSeconds((double)K * (double)AgentDt);
			const double FrameEnd = Clock->GetSimSecondsTotal();
			const auto RobotBatch = [&]()
			{
				for (int32 n = 0; n < Bots.Num(); ++n)
				{
					FTransitSuiteBot& Bot = Bots[bReverse ? Bots.Num() - 1 - n : n];
					for (int32 Step = 0; Step < K; ++Step)
					{
						Sim->SetRobotStepEnd(RH::RobotSubStepEnd(FrameEnd, K, Step, AgentDt));
						RobotSub(Bot, Step);
					}
				}
				Sim->SetRobotStepEnd(-1.0);
			};
			const auto WorldBatch = [&]()
			{
				for (int32 i = 0; i < K; ++i)
				{
					Sim->Debug_SetBatteryWh(1.0e9);
					Sim->Debug_StepSimAt(AgentDt, FrameEnd - (double)(K - 1 - i) * (double)AgentDt);
					AfterWorldStep();
				}
			};
			if (bRobotsFirst) { RobotBatch(); WorldBatch(); }
			else { WorldBatch(); RobotBatch(); }
		};
		// The fixture: shaft 0 (one car) and shaft 1 at (30,1) (no car: a hoist), each with a dig on -1. Six haulers, each with
		// its own surface store (1e9 kg of Struct) and its own completed store below; tasks are posted by the harness at the
		// hauler's own sub-step, never by the board, and never claimed through TryClaimHaul: nothing two haulers share.
		// Haulers 0-2 ride shaft 0 into dig 0; haulers 3-5 hand off at head 1 (dig 1 has no fitted landing). Capacity 20 places
		// a sol, so the waits are not 0 and feed back into the holds; at 60 people or fewer the people part is 0.
		Expect(OneShaft(1, 12, 0) && SinkOne(1, 6) && Fit(0), TEXT("TR5 setup: shaft 0 with a car, shaft 1 a hoist, a dig under each"));
		TArray<int32> Up, Down;
		const FIntPoint Dests[6] = { FIntPoint(1, 1), FIntPoint(2, 2), FIntPoint(-1, -1), FIntPoint(31, 1), FIntPoint(31, 2), FIntPoint(29, 0) };
		for (int32 k = 0; k < 6; ++k)
		{
			const FVector Store = k < 3 ? FVector(-1000.0, 2000.0 + 1500.0 * k, 0.0) : FVector(35000.0, 3000.0 + 1500.0 * (k - 3), 0.0);
			Up.Add(Place(TEXT("Stockpile"), Store, 0));
			Sim->Debug_AddSolidAt(Up.Last(), NStruct, 1.0e9, false);
			Down.Add(Place(TEXT("Stockpile"), Pt(-1, Dests[k].X, Dests[k].Y), -1));
		}
		Sim->Debug_SetTransitCapacity(20, 20);
		Expect(Sim->GetPopulation() <= 60 && Sim->SaveColony(TEXT("transitorder"), SaveErr), FString::Printf(TEXT("TR5 the start is saved for all twelve runs (%s)"), *SaveErr));
		struct FRun { TArray<FString> Rolls; FString End; int32 Unstamped = 0; int32 MaxWait = 0; };
		const auto RunOnce = [&](int32 K, bool bRobotsFirst, bool bReverse) -> FRun
		{
			FRun Out;
			FString Le;
			Sim->LoadColony(TEXT("transitorder"), Le);
			Sim->Debug_ForgetRobotTransit();
			Sim->Debug_SetTransitCapacity(20, 20);
			const int32 Unstamped0 = Sim->GetTransitUnstampedRobotWrites();
			TArray<FTransitSuiteBot> Bots;
			for (int32 k = 0; k < 6; ++k)
			{
				const FRHBuildingInstance* Store = FindB(Up[k]);
				Bots.Add(MakeBot(TEXT("RC_H"), Store ? FVector(Store->LocationCm.X, Store->LocationCm.Y, 50.0) : FVector::ZeroVector));
			}
			int32 Thr = Sim->GetTransitThresholds();
			const auto RobotSub = [&](FTransitSuiteBot& Bot, int32)
			{
				if (Bot.T.TaskType == 0)
				{
					const int32 k = Bots.IndexOfByPredicate([&Bot](const FTransitSuiteBot& O) { return O.H == Bot.H; });
					FRHSiteRef From, To; From.BuildingId = Up[k]; To.BuildingId = Down[k];
					StartHaul(Bot, Sim->Debug_PostHaulTask(From, To, NStruct, 200.0, Bot.H));
					return;
				}
				HaulTick(Bot);
			};
			const auto AfterWorld = [&]()
			{
				if (Sim->GetTransitThresholds() != Thr)
				{
					Thr = Sim->GetTransitThresholds();
					// What the roll read and froze: the sol, every wait, and the s-2 slot it counted (complete by then in either
					// order). The slot still being written is compared once every frame has run, at the end.
					const int32 Ck = Sim->GetTransitCheckedSol();
					FString D = FString::Printf(TEXT("sol %d:"), Ck);
					for (int32 Sh = 0; Sh < Sim->GetTransitLedgers().Num(); ++Sh)
					{
						D += FString::Printf(TEXT(" [%d w%d p%d s-2 %d places %lld kg]"), Sh, Sim->GetTransitLedgers()[Sh].WaitSec, Sim->GetTransitLedgers()[Sh].PrevWaitSec, SlotPlaces(Sh, Ck - 2), SlotKg(Sh, Ck - 2));
					}
					Out.Rolls.Add(D);
					Out.MaxWait = FMath::Max3(Out.MaxWait, Sim->GetTransitWaitSec(0), Sim->GetTransitWaitSec(1));
				}
			};
			const int32 Total = 5 * (int32)(SolLen / (double)AgentDt);
			for (int32 Done = 0; Done < Total;)
			{
				const int32 Kf = FMath::Min(K, Total - Done);
				DriveFrame(Kf, bRobotsFirst, bReverse, Bots, RobotSub, AfterWorld);
				Done += Kf;
			}
			for (const FTransitSuiteBot& Bot : Bots)
			{
				const FVector P = Bot.X.GetLocation();
				Out.End += FString::Printf(TEXT("[%.0f kg %d unloads %d hand-off %d ride-hold %.4f,%.4f,%.1f]"), Bot.DeliveredKg, Bot.Unloads, Bot.HandoffSteps, Bot.RideHolds, P.X, P.Y, P.Z);
			}
			Out.End += LedgerDigest();
			Out.Unstamped = Sim->GetTransitUnstampedRobotWrites() - Unstamped0;
			return Out;
		};
		TArray<FRun> Runs;
		TArray<FString> Labels;
		for (const int32 K : { 1, 7, 32 })
		{
			for (const bool bRobotsFirst : { true, false })
			{
				for (const bool bReverse : { false, true })
				{
					Runs.Add(RunOnce(K, bRobotsFirst, bReverse));
					Labels.Add(FString::Printf(TEXT("K%d %s %s"), K, bRobotsFirst ? TEXT("robots-first") : TEXT("world-first"), bReverse ? TEXT("reversed") : TEXT("forward")));
				}
			}
		}
		bool bEqual = true, bStamped = true;
		FString Odd;
		for (int32 i = 0; i < Runs.Num(); ++i)
		{
			bStamped &= Runs[i].Unstamped == 0;
			if (Runs[i].Rolls != Runs[0].Rolls || Runs[i].End != Runs[0].End) { bEqual = false; Odd += Labels[i] + TEXT("; "); }
		}
		const FRun& Ref = Runs[0];

		Expect(Runs.Num() == 12 && Ref.Rolls.Num() >= 5 && Ref.End.Contains(TEXT("kg")) && !Ref.End.StartsWith(TEXT("[0 kg")),
			FString::Printf(TEXT("TR5 the fixture did real work: %d rolls over 5 sols; the first run ends %s"), Ref.Rolls.Num(), *Ref.End));
		Expect(bEqual, FString::Printf(TEXT("TR5 every ledger slot (sol, kg, places), WaitSec and PrevWaitSec after every roll, the roll count, each hauler's delivered kg, holds and final position are EQUAL across the 12 runs (K 1/7/32 x robots-first/world-first x forward/reversed)%s"), Odd.IsEmpty() ? TEXT("") : *(TEXT(" - differing: ") + Odd)));
		Expect(bStamped, TEXT("TR5 every robot ledger write carried its sub-step's stamp in all 12 runs (TransitUnstampedRobotWrites did not move)"));
		Expect(Ref.MaxWait > 0, FString::Printf(TEXT("TR5 the waits were not all 0 (the 20-place capacity crowded the shafts: the longest wait was %d s), so the holds fed back"), Ref.MaxWait));

		// The boundary case. Default rows; shaft 1 is a hoist (200 places, a 450-sub-step hand-off for one floor). Slot s is
		// preloaded with exactly Cap = 200 places, slot s-1 with 400 (the roll into s+1 makes shaft 1's wait 120 s). Hauler A
		// arrives at the head at sub-step 77 and credits at 527 - the LAST sub-step stamped in sol s; hauler B arrives at 78 and
		// credits at 528, the crossing sub-step (the first of sol s+1); hauler C arrives at 530, 0.2 s into sol s+1. Sub-step
		// 528 is the 17th of its 32-frame and the 4th of its 7-frame, so the credit and the crossing share a frame.
		Sim->Debug_SetTransitCapacity(200, 1000); // the default rows again
		Expect(OneShaft(1, 12, 0) && SinkOne(1, 6) && Fit(0), TEXT("TR5 boundary setup"));
		const int32 Sm = SolNow() + 2;
		const double Bm = (double)(Sm + 1) * SolLen;
		const double TStart = Bm - 529.0 * (double)AgentDt;
		Clock->Debug_SetSimSeconds(TStart);
		Sim->Debug_RollTransit(Sm);
		Sim->Debug_ChargeTransit(1, 400, (Sm - 1) * SolLen + 600.0);
		Sim->Debug_ChargeTransit(1, 200, Sm * SolLen + 600.0);
		const int32 MicroFrom = Place(TEXT("Stockpile"), FVector(35000.0, 3000.0, 0.0), 0);
		Sim->Debug_AddSolidAt(MicroFrom, NStruct, 1.0e6, false);
		TArray<int32> MicroTasks;
		for (const FIntPoint& Dst : { FIntPoint(31, 1), FIntPoint(31, 2), FIntPoint(29, 0) })
		{
			FRHSiteRef From, To; From.BuildingId = MicroFrom; To.BuildingId = Place(TEXT("Stockpile"), Pt(-1, Dst.X, Dst.Y), -1);
			MicroTasks.Add(Sim->Debug_PostHaulTask(From, To, NStruct, 200.0));
		}
		Expect(Sim->GetTransitCheckedSol() == Sm && Sim->SaveColony(TEXT("transitmicro"), SaveErr), FString::Printf(TEXT("TR5 boundary start saved at sol %d, 529 sub-steps before its end (%s)"), Sm, *SaveErr));
		const int32 ArriveAt[3] = { 77, 78, 530 };
		const auto MicroRun = [&](int32 K, bool bRobotsFirst, bool bReverse, FString& OutDigest)
		{
			FString Le;
			Sim->LoadColony(TEXT("transitmicro"), Le);
			Sim->Debug_ForgetRobotTransit();
			TArray<FTransitSuiteBot> Bots;
			int32 HoldLen[3] = { 0, 0, 0 };
			int32 CreditAt[3] = { -1, -1, -1 };
			for (int32 k = 0; k < 3; ++k)
			{
				Bots.Add(MakeBot(TEXT("RC_H"), FVector(HeadXY(1).X, HeadXY(1).Y, 50.0)));
				float Loaded = 0.f; FVector Drop;
				Sim->HaulLoad(MicroTasks[k], 200.f, Loaded, Drop, 0, &Bots[k].T.TargetLevel);
				Bots[k].T.TaskType = (uint8)ERHTaskType::Haul; Bots[k].T.TaskId = MicroTasks[k]; Bots[k].T.Phase = 2; Bots[k].T.CargoKg = Loaded; Bots[k].T.TargetCm = Drop;
			}
			TMap<FMassEntityHandle, int32> Index;
			for (int32 k = 0; k < 3; ++k) { Index.Add(Bots[k].H, k); }
			int32 Sub[3] = { 0, 0, 0 };
			int32 W1 = -1;
			int32 Thr = Sim->GetTransitThresholds();
			const auto RobotSub = [&](FTransitSuiteBot& Bot, int32)
			{
				const int32 k = Index[Bot.H];
				const int32 i = Sub[k]++;
				if (Bot.T.TaskType == 0) { return; }
				if (i == ArriveAt[k]) { HoldLen[k] = Sim->BeginHandoffHold(Bot.H, Bot.T.TaskId, 0, Bot.X.GetLocation()); return; }
				if (i > ArriveAt[k] && Sim->InHandoffHold(Bot.H, Bot.T.TaskId, Bot.X.GetLocation()) && Sim->StepHandoffHold(Bot.H))
				{
					Sim->HaulUnload(Bot.T.TaskId, Bot.T.CargoKg, NAME_None, 0);
					CreditAt[k] = i;
					Bot.T = FRHTaskFragment();
				}
			};
			const auto AfterWorld = [&]()
			{
				if (Sim->GetTransitThresholds() != Thr) { Thr = Sim->GetTransitThresholds(); if (Sim->GetTransitCheckedSol() == Sm + 1) { W1 = Sim->GetTransitWaitSec(1); } }
			};
			const int32 Total = 12600;
			for (int32 Done = 0; Done < Total;)
			{
				const int32 Kf = FMath::Min(K, Total - Done);
				DriveFrame(Kf, bRobotsFirst, bReverse, Bots, RobotSub, AfterWorld);
				Done += Kf;
			}
			OutDigest += FString::Printf(TEXT("credits at sub-steps %d %d %d, holds %d %d %d | sol s %d places, s+1 %d | wait at s+1 %d, at s+2 %d | %s"),
				CreditAt[0], CreditAt[1], CreditAt[2], HoldLen[0], HoldLen[1], HoldLen[2], SlotPlaces(1, Sm), SlotPlaces(1, Sm + 1), W1, Sim->GetTransitCheckedSol() >= Sm + 2 ? Sim->GetTransitWaitSec(1) : -1, *LedgerDigest());
			return CreditAt[0] == 527 && CreditAt[1] == 528 && CreditAt[2] == 980 && SlotPlaces(1, Sm) == 202 && SlotPlaces(1, Sm + 1) == 4 && W1 == 120 && Sim->GetTransitCheckedSol() == Sm + 2 && Sim->GetTransitWaitSec(1) == 2 && HoldLen[0] == 450 && HoldLen[1] == 450 && HoldLen[2] == 450;
		};
		bool bAll = true, bSame = true;
		FString First, Bad;
		for (const int32 K : { 1, 7, 32 })
		{
			for (const bool bRobotsFirst : { true, false })
			{
				for (const bool bReverse : { false, true })
				{
					FString D;
					const bool bOk = MicroRun(K, bRobotsFirst, bReverse, D);
					bAll &= bOk;
					if (First.IsEmpty()) { First = D; }
					if (D != First || !bOk) { bSame &= D == First; Bad += FString::Printf(TEXT(" {K%d %s %s: %s}"), K, bRobotsFirst ? TEXT("robots-first") : TEXT("world-first"), bReverse ? TEXT("reversed") : TEXT("forward"), *D); }
				}
			}
		}
		Expect(bAll && bSame, FString::Printf(TEXT("TR5 the boundary case, all 12 runs: A's credit at the last sub-step of sol s lands in slot s (200 + 2 = 202), B's at the crossing sub-step and C's 0.2 s later land in s+1 (4); C's hold reads sol s's wait (450 sub-steps) in either batch order; the roll into s+1 gives 120 s (slot s-1's 400 places), the roll into s+2 gives ceil(2 x 120 / 200) = 2 s. %s%s"), *First, *Bad));
	}


	// --- The commute fixtures (TR6-TR8): rated floors that house whoever is added, homes and seats set by hand. ---
	const auto Housing = [&](int32 Depth)
	{
		for (int32 L = 1; L <= Depth; ++L)
		{
			Place(TEXT("AirFilter"), Pt(-L, -1, -1), -L);
			Sim->Debug_ForceRateFloor(-L);
		}
		Sim->Debug_SetColonistsPerCell(100.0);
		Sim->Debug_RefreshJobs();
		Sim->Debug_ClearColonistStations();
		return Sim->IsFloorRated(-1) && (Depth < 2 || Sim->IsFloorRated(-2));
	};
	// Add N colonists living on HomeLevel and working at Work (a surface building Id, or a floor cell when WorkLevel < 0).
	const auto People = [&](int32 N, int32 HomeLevel, int32 WorkLevel, int32 Work)
	{
		const int32 Before = Sim->GetPopulation();
		const int32 Got = Sim->Debug_AddColonists(N);
		const TArray<FRHColonist>& All = Sim->GetColonists();
		for (int32 i = Before; i < All.Num(); ++i)
		{
			Sim->Debug_SetColonistHome(All[i].Id, 0, HomeLevel);
			Sim->Debug_SetColonistStation(All[i].Id, WorkLevel < 0 ? FIntVector(WorkLevel, Work, 0) : FIntVector(0, INDEX_NONE, Work));
		}
		return Got == N;
	};
	const auto Anchor = [&](int32 HomeLevel, int32 WorkLevel) -> FIntPoint
	{
		TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
		Sim->GetCommutePairs(Pairs);
		for (const URHSimWorldSubsystem::FRHCommutePair& P : Pairs) { if (P.HomeLevel == HomeLevel && P.WorkLevel == WorkLevel) { return HomeLevel < 0 ? P.HomePosCm : P.WorkPosCm; } }
		return FIntPoint(-1, -1);
	};
	const auto PairDist = [&](int32 HomeLevel, int32 WorkLevel) -> int64
	{
		TArray<URHSimWorldSubsystem::FRHCommutePair> Pairs;
		Sim->GetCommutePairs(Pairs);
		for (const URHSimWorldSubsystem::FRHCommutePair& P : Pairs) { if (P.HomeLevel == HomeLevel && P.WorkLevel == WorkLevel) { return P.DistCm; } }
		return -1;
	};
	const auto HeadCmInt = [&](int32 S) { const FVector H = Sim->GetShaft(S).HeadCm; return FIntPoint(FMath::RoundToInt32(H.X), FMath::RoundToInt32(H.Y)); };

	// --- TR6. The people load: the pick, the ramp in permille at 60, 61 and 160, the k mod n split, a same-floor trip, O4. ---
	{
		Expect(OneShaft(2, 12, 12) && SinkOne(2, 6) && Housing(2), TEXT("TR6 setup: shafts 0 and 1 to -2, digs on -1 and -2 under both, floors -1 and -2 rated"));
		// A workplace on the line between the heads, 140 m from head 0 and about 160 m from head 1.
		const FVector H0 = HeadXY(0), H1 = HeadXY(1);
		const FVector Dir = (H1 - H0).GetSafeNormal2D();
		const int32 Work = Place(TEXT("Stockpile"), H0 + Dir * 14000.0, 0);
		const FVector WorkCm = FindB(Work)->LocationCm;
		const int64 D0 = RHRoads::RoundSqrtCm((int64)FMath::Square(FMath::RoundToInt64(WorkCm.X) - HeadCmInt(0).X) + (int64)FMath::Square(FMath::RoundToInt64(WorkCm.Y) - HeadCmInt(0).Y));
		const int64 D1 = RHRoads::RoundSqrtCm((int64)FMath::Square(FMath::RoundToInt64(WorkCm.X) - HeadCmInt(1).X) + (int64)FMath::Square(FMath::RoundToInt64(WorkCm.Y) - HeadCmInt(1).Y));
		Expect(People(60, -1, 0, Work), TEXT("TR6 sixty colonists live on -1 and work at the surface workplace"));
		const FIntPoint WorkInt(FMath::RoundToInt32(WorkCm.X), FMath::RoundToInt32(WorkCm.Y));
		const int64 Bare = Sim->GetCommuteDistanceCm(-1, HeadCmInt(0), 0, WorkInt);
		Expect(Bare == 6000 + D0, FString::Printf(TEXT("TR6 (O4) with no car the trip uses shaft 0: 6,000 + %lld beats 6,000 + %lld (%lld)"), D0, D1, Bare));
		Expect(Fit(1), TEXT("TR6 the colony's first car goes on shaft 1"));
		const int64 Lifted = Sim->GetCommuteDistanceCm(-1, HeadCmInt(0), 0, WorkInt);
		Expect(Lifted == 2000 + D1 && Anchor(-1, 0) == FIntPoint(-1, -1),
			FString::Printf(TEXT("TR6 (O4) the car on shaft 1 takes the trip over: 2,000 + %lld (lift) beats 6,000 + %lld (hoist) - %lld cm (the pairs stay empty at ramp 0)"), D1, D0, Lifted));
		const int32 S = SolNow();
		const int32 Rebuilds60 = Sim->GetCommuteFlowRebuilds();
		Sim->Debug_RollTransit(S + 1);
		Expect(Sim->GetCommuteRamp() == 0.0 && Sim->GetShaftPeopleLoad(1) == 0 && Sim->GetCommuteFlowRebuilds() == Rebuilds60,
			FString::Printf(TEXT("TR6 at 60 people the ramp is 0: the people load is 0 and the roll touches no flow (rebuilds %d -> %d)"), Rebuilds60, Sim->GetCommuteFlowRebuilds()));
		Expect(People(1, -1, 0, Work), TEXT("TR6 a 61st colonist"));
		Expect(Sim->GetCommuteShaftPeople(1) == 61 && Sim->GetCommuteShaftPeople(0) == 0 && Sim->GetShaftPeopleLoad(1) == 1 && Anchor(-1, 0) == HeadCmInt(1),
			FString::Printf(TEXT("TR6 at 61 people: 61 commuters through shaft 1 (the trip's shaft), none through shaft 0; the load is 2 x 61 x 10 / 1000 = 1 place (%lld)"), Sim->GetShaftPeopleLoad(1)));
		Expect(People(99, -1, 0, Work) && Sim->GetShaftPeopleLoad(1) == 320, FString::Printf(TEXT("TR6 at 160 people (the ramp full): 2 x 160 = 320 places (%lld)"), Sim->GetShaftPeopleLoad(1)));
		// Seven more live on -1 and work on -2: both shafts reach -2, so colonist k of the trip rides shaft (0, 1)[k mod 2].
		const int32 First7 = Sim->GetPopulation();
		Expect(People(7, -1, -2, 3), TEXT("TR6 seven colonists live on -1 and work on -2"));
		const TArray<FRHColonist>& All = Sim->GetColonists();
		bool bSplit = true;
		FString Seen;
		for (int32 k = 0; k < 7; ++k)
		{
			const int64 Want = (k % 2 == 0) ? 6000 : 2000; // shaft 0 is a hoist (6,000 a floor), shaft 1 a lift (2,000), no wait
			const int64 Got = Sim->GetColonistCommuteCm(All[First7 + k].Id);
			bSplit &= Got == Want;
			Seen += FString::Printf(TEXT("%lld "), Got);
		}
		Expect(bSplit && Sim->GetCommuteShaftPeople(0) == 4 && Sim->GetCommuteShaftPeople(1) == 160 + 3 && Sim->GetShaftPeopleLoad(0) == 8,
			FString::Printf(TEXT("TR6 the split: ordinals 0, 2, 4, 6 take shaft 0 and 1, 3, 5 shaft 1 - 4 and 3 colonists, 8 and 6 places at full ramp; each colonist pays its own shaft exactly (%s)"), *Seen));
		const int32 SameBefore0 = Sim->GetCommuteShaftPeople(0), SameBefore1 = Sim->GetCommuteShaftPeople(1);
		Expect(People(5, -2, -2, 4) && Sim->GetCommuteShaftPeople(0) == SameBefore0 && Sim->GetCommuteShaftPeople(1) == SameBefore1 && Sim->GetColonistCommuteCm(Sim->GetColonists().Last().Id) == 0,
			TEXT("TR6 a trip on one floor loads no shaft and costs 0"));
		Expect(Sim->Debug_CommuteCachesFresh(), TEXT("TR6 the commute caches are fresh (ShaftPeople is in -RHVerifyCaches' compare)"));

		// --- TR7. The commute wait, by hand; the split's display mean; one rebuild per wait bump, none on a quiet roll. ---
		const int32 HoistWork = Place(TEXT("Stockpile"), H0 + FVector(0.0, -3000.0, 0.0), 0);
		const int32 HoistFirst = Sim->GetPopulation();
		Expect(People(1, -1, 0, HoistWork), TEXT("TR7 a colonist works 30 m from head 0"));
		Expect(Sim->GetColonistCommuteCm(Sim->GetColonists()[HoistFirst].Id) == 6000 + 3000 && Sim->GetColonistCommuteCm(All[0].Id) == 2000 + D1,
			FString::Printf(TEXT("TR7 by hand: through shaft 0's hoist 6,000 + 3,000 = 9,000 cm (%lld); through shaft 1's lift 2,000 + %lld (%lld)"), Sim->GetColonistCommuteCm(Sim->GetColonists()[HoistFirst].Id), D1, Sim->GetColonistCommuteCm(All[0].Id)));
		Expect(PairDist(-1, -2) == (4 * 6000 + 3 * 2000) / 7, FString::Printf(TEXT("TR7 the split trip shows the floored mean (4 x 6,000 + 3 x 2,000) / 7 = 4,285 cm (%lld)"), PairDist(-1, -2)));
		const int32 R0 = Sim->GetCommuteFlowRebuilds();
		const uint32 V0 = Sim->GetTransitWaitVersion();
		Sim->Debug_SetTransitCapacity(200, 100); // a car carries 100 now: 326 + 6 = 332 places on shaft 1
		Sim->Debug_RollTransit(S + 2);
		const int32 CrowdWait = Sim->GetTransitWaitSec(1);
		const int64 Crowded = Sim->GetColonistCommuteCm(All[0].Id);
		const int32 R1 = Sim->GetCommuteFlowRebuilds();
		Sim->Debug_RollTransit(S + 3); // the same load: no wait moves
		Sim->GetColonistCommuteCm(All[0].Id);
		const int32 R2 = Sim->GetCommuteFlowRebuilds();
		Expect(CrowdWait == Sim->ComputeTransitWaitSec(Sim->GetShaftPeopleLoad(1), 100) && CrowdWait > 0 && Crowded == 2000 + (int64)CrowdWait * 100 + D1,
			FString::Printf(TEXT("TR7 a crowded lift: shaft 1 waits %d s, and its commuter's trip is 2,000 + %d x 100 + %lld = %lld cm"), CrowdWait, CrowdWait, D1, Crowded));
		Expect(Sim->GetTransitWaitVersion() == V0 + 1 && R1 == R0 + 1 && R2 == R1 && Sim->Debug_CommuteCachesFresh(),
			FString::Printf(TEXT("TR7 one flows rebuild for the roll that changed a wait (%d -> %d), none for the roll that changed none (%d), the caches fresh"), R0, R1, R2));
		Sim->Debug_SetTransitCapacity(200, 1000);
	}
	// TR7 at 59 and 60 people, transit on: no rebuild, and Hope bit-identical across a roll that changes a wait.
	{
		Expect(OneShaft(1, 12, 0) && Housing(1) && Fit(0), TEXT("TR7 low-population setup: transit on, floor -1 rated"));
		const int32 Work = Place(TEXT("Stockpile"), HeadXY(0) + FVector(0.0, -3000.0, 0.0), 0);
		bool bQuiet = true;
		FString Note;
		for (const int32 N : { 59, 60 })
		{
			People(N - Sim->GetPopulation(), -1, 0, Work);
			const int32 S = SolNow();
			Sim->Debug_ChargeTransit(0, 5000, S * SolLen + 1.0);
			const double Hope0 = Sim->GetColonyHope().Total;
			const int32 R0 = Sim->GetCommuteFlowRebuilds();
			Sim->Debug_RollTransit(S + 2);
			const double Hope1 = Sim->GetColonyHope().Total;
			bQuiet &= Sim->GetPopulation() == N && Sim->GetCommuteFlowRebuilds() == R0 && Hope0 == Hope1 && Sim->GetTransitWaitSec(0) > 0;
			Note += FString::Printf(TEXT("[%d people: rebuilds %d -> %d, Hope %.9f -> %.9f, wait %d] "), N, R0, Sim->GetCommuteFlowRebuilds(), Hope0, Hope1, Sim->GetTransitWaitSec(0));
		}
		Expect(bQuiet, FString::Printf(TEXT("TR7 at 59 and 60 people a wait that rises touches no flow and no Hope %s"), *Note));
	}

	// --- TR8. "A second car, a second shaft fixes it": 700 floor commuters through shaft 0's hoist. ---
	{
		Expect(OneShaft(2, 12, 12) && Housing(2), TEXT("TR8 setup: shaft 0 to -2, floors -1 and -2 rated"));
		Place(TEXT("Pylon"), FVector(-28000.0, 2000.0, 0.0), 0);
		int32 Far = INDEX_NONE;
		Sim->Debug_SinkShaftAt(FVector(-28000.0, 2000.0, 0.0), 1, Far, Why);
		Sim->ExcavateFloor(-1, 4, R, Far);
		Expect(Far == 1 && Fit(1), TEXT("TR8 transit is on through a car on a far shaft 1 that reaches only -1, so shaft 0 is a hoist"));
		Expect(People(700, -1, -2, 2), TEXT("TR8 700 colonists live on -1 and work on -2: every one through shaft 0"));
		int32 Sol = SolNow();
		struct FStep { int32 Wait = 0; int32 Long = 0; double Penalty = 0.0; };
		const auto Measure = [&]() -> FStep
		{
			Sim->Debug_RollTransit(++Sol);
			FStep St;
			St.Wait = Sim->GetTransitWaitSec(0);
			const URHSimWorldSubsystem::FRHCommuteSummary Sum = Sim->GetCommuteSummary();
			St.Long = Sum.LongCommuters;
			St.Penalty = Sim->GetColonyHope().CommutePenalty;
			return St;
		};
		const FStep Hoist = Measure();
		Expect(Hoist.Wait == 600 && Hoist.Long == 700, FString::Printf(TEXT("TR8 the hoist: 1,400 places against 200 waits %d s (ceil(1,200 x 120 / 200) = 720, clamped to 600); %d long commuters (6,000 + 60,000 cm each)"), Hoist.Wait, Hoist.Long));
		Expect(Fit(0), TEXT("TR8 a car on shaft 0"));
		const FStep One = Measure();
		Expect(Fit(0), TEXT("TR8 a second car on shaft 0"));
		const FStep Two = Measure();
		Expect(One.Wait == 48 && Two.Wait == 0 && One.Long == 0 && One.Penalty <= Hoist.Penalty && Two.Penalty <= One.Penalty && Hoist.Penalty > One.Penalty,
			FString::Printf(TEXT("TR8 one car waits %d s (ceil(400 x 120 / 1,000) = 48), two cars %d s; long commuters %d -> %d -> %d; the Hope penalty %.4f -> %.4f -> %.4f never rises"), One.Wait, Two.Wait, Hoist.Long, One.Long, Two.Long, Hoist.Penalty, One.Penalty, Two.Penalty));
		Place(TEXT("Pylon"), FVector(2000.0, 32000.0, 0.0), 0);
		int32 Second = INDEX_NONE;
		Sim->Debug_SinkShaftAt(FVector(2000.0, 32000.0, 0.0), 2, Second, Why);
		Expect(Second == 2 && Sim->GetCommuteShaftPeople(0) == 350 && Sim->GetCommuteShaftPeople(2) == 350,
			FString::Printf(TEXT("TR8 a second shaft that reaches both floors halves the floor-to-floor load: %d and %d commuters"), Sim->GetCommuteShaftPeople(0), Sim->GetCommuteShaftPeople(2)));
	}
	// TR8b. The shaft pick reads no wait (REVISION mutation 6): 700 floor-to-surface commuters whose cheapest shaft is shaft 0's
	// crowded hoist stay on it, sol after sol. A wait-aware pick would move them to shaft 1's lift on the next flows rebuild and
	// back the sol after (the crowd's wait falls to 0 once it leaves), so shaft 0's people would alternate 700, 0, 700.
	{
		Expect(OneShaft(1, 12, 0) && Housing(1), TEXT("TR8b setup: shaft 0 to -1, floor -1 rated"));
		Place(TEXT("Pylon"), FVector(-28000.0, 2000.0, 0.0), 0);
		int32 Far = INDEX_NONE;
		Sim->Debug_SinkShaftAt(FVector(-28000.0, 2000.0, 0.0), 1, Far, Why);
		Sim->ExcavateFloor(-1, 4, R, Far);
		Expect(Far == 1 && Fit(1), TEXT("TR8b transit is on through a car on the far shaft 1 (a lift, 300 m from the workplace); shaft 0 is a hoist beside it"));
		const int32 Work = Place(TEXT("Stockpile"), HeadXY(0) + FVector(0.0, -3000.0, 0.0), 0);
		Expect(People(700, -1, 0, Work), TEXT("TR8b 700 colonists live on -1 and work 30 m from head 0"));
		int32 Sol = SolNow();
		bool bSteady = true;
		FString Seen;
		for (int32 K = 0; K < 4; ++K)
		{
			Sim->Debug_RollTransit(++Sol);
			const int32 P0 = Sim->GetCommuteShaftPeople(0), P1 = Sim->GetCommuteShaftPeople(1);
			const int32 W0 = Sim->GetTransitWaitSec(0), W1 = Sim->GetTransitWaitSec(1);
			bSteady &= P0 == 700 && P1 == 0 && W0 == 600 && W1 == 0;
			Seen += FString::Printf(TEXT("[sol %d: shaft 0 %d people, wait %d s; shaft 1 %d people, wait %d s] "), Sol, P0, W0, P1, W1);
		}
		Expect(bSteady, FString::Printf(TEXT("TR8b the crowd stays on the crowded hoist over four rolls, wait 600 s each time (the pick is wait-free: no oscillation): %s"), *Seen));
	}


	// --- TR9. The head hand-off holds: 10 x (wait + dwell + floors x seconds a floor) sub-steps, idle, then one credit. ---
	{
		struct FHoldRun { int32 Arrive = -1, H = 0, Credit = -1, HoldSteps = 0; float HoldDrawWh = 0.f; int32 Places = 0; int32 Sol = -1; };
		// A hauler from a surface store to Dest (a completed store on -1), driven to the credit. Arrive = the sub-step it reached the
		// drop; Credit = the sub-step it unloaded; the ledger write is read from the shaft's slot of the credit sol.
		const auto HoldOnce = [&](const FVector& StoreCm, int32 Dest, int32 Shaft) -> FHoldRun
		{
			FHoldRun Out;
			const int32 Store = Place(TEXT("Stockpile"), StoreCm, 0);
			Sim->Debug_AddSolidAt(Store, NStruct, 1.0e6, false);
			FTransitSuiteBot Bot = MakeBot(TEXT("RC_H"), FVector(StoreCm.X, StoreCm.Y, 50.0));
			FRHSiteRef From, To; From.BuildingId = Store; To.BuildingId = Dest;
			StartHaul(Bot, Sim->Debug_PostHaulTask(From, To, NStruct, 200.0, Bot.H));
			const int32 Before = Sim->HasTransit() ? SlotPlaces(Shaft, SolNow()) : 0;
			float ChargeAtArrive = 0.f;
			for (int32 K = 0; K < 20000; ++K)
			{
				Sim->SetRobotStepEnd(Clock->GetSimSecondsTotal() + (double)AgentDt);
				const bool bUnloaded = HaulTick(Bot);
				Sim->SetRobotStepEnd(-1.0);
				if (Out.Arrive < 0 && (Sim->GetHandoffHoldStepsLeft(Bot.H) > 0 || bUnloaded) && Bot.T.Phase != 1)
				{
					Out.Arrive = K;
					Out.H = Sim->GetHandoffHoldStepsLeft(Bot.H);
					ChargeAtArrive = Bot.ChargeWh;
				}
				AgentOnce();
				if (bUnloaded)
				{
					Out.Credit = K;
					Out.HoldSteps = Bot.HandoffSteps;
					Out.HoldDrawWh = ChargeAtArrive - Bot.ChargeWh;
					Out.Sol = FMath::FloorToInt32((Clock->GetSimSecondsTotal() - (double)AgentDt) / SolLen);
					Out.Places = Sim->HasTransit() ? SlotPlaces(Shaft, Out.Sol) - (Out.Sol == SolNow() ? Before : 0) : 0;
					break;
				}
			}
			return Out;
		};
		for (const bool bRoad : { false, true })
		{
			const TCHAR* Kind = bRoad ? TEXT("road") : TEXT("no-road");
			// The hoist: shaft 1 has no car, its dig no fitted landing - the head hand-off.
			Expect(OneShaft(1, 12, 0) && SinkOne(1, 6) && Fit(0), FString::Printf(TEXT("TR9 (%s) setup: shaft 0 fitted, shaft 1 a hoist"), Kind));
			const FVector Store1(38000.0, 3000.0, 0.0);
			if (bRoad) { Sim->Debug_LayRoad(Store1, HeadXY(1) + FVector(2000.0, 0.0, 0.0), true); }
			const int32 Dest1 = Place(TEXT("Stockpile"), Pt(-1, 31, 1), -1);
			const FHoldRun Hoist = HoldOnce(Store1, Dest1, 1);
			const float IdleWh = 450.f * 5.f * (AgentDt / 50.f);
			Expect(Hoist.H == 450 && Hoist.HoldSteps == 450 && Hoist.Credit == Hoist.Arrive + 450 && Hoist.Places == 2,
				FString::Printf(TEXT("TR9 (%s) at the hoist's head the hauler holds exactly 10 x (0 + 30 + 15 x 1) = 450 sub-steps (%d), then credits once, at arrival + 450 (%d -> %d), charging shaft 1 ceil(200 / 100) = 2 places (%d)"), Kind, Hoist.H, Hoist.Arrive, Hoist.Credit, Hoist.Places));
			Expect(FMath::IsNearlyEqual(Hoist.HoldDrawWh, IdleWh, 0.01f), FString::Printf(TEXT("TR9 (%s) every hold sub-step draws idle power: %.4f Wh over the hold (450 x 5 W x 0.1 / 50 = %.4f; a drive would be 72)"), Kind, Hoist.HoldDrawWh, IdleWh));
			// A fitted shaft's head: a store in rock near shaft 0 (unreachable) is a hand-off through the lift.
			const int32 Rock = Place(TEXT("Stockpile"), Pt(-1, 5, 5), -1);
			const FHoldRun Lift = HoldOnce(FVector(-3000.0, -3000.0, 0.0), Rock, 0);
			Expect(Lift.H == 140 && Lift.HoldSteps == 140 && Lift.Places == 2, FString::Printf(TEXT("TR9 (%s) through a lift car the hold is 10 x (0 + 10 + 4 x 1) = 140 sub-steps (%d), 2 places on shaft 0 (%d)"), Kind, Lift.H, Lift.Places));
		}
		// No transit: no hold, the credit on the arrival sub-step, no ledger at all.
		Expect(OneShaft(1, 12, 0) && SinkOne(1, 6), TEXT("TR9 no-transit setup"));
		const FHoldRun None = HoldOnce(FVector(38000.0, 3000.0, 0.0), Place(TEXT("Stockpile"), Pt(-1, 31, 1), -1), 1);
		Expect(None.H == 0 && None.HoldSteps == 0 && None.Credit == None.Arrive && Sim->GetTransitLedgers().Num() == 0,
			FString::Printf(TEXT("TR9 with no car there is no hold: the credit lands on the arrival sub-step (%d = %d) and no ledger exists"), None.Credit, None.Arrive));
		// A same-level unload below writes nothing (and is a delivery below).
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR9 same-level setup"));
		const int32 LowA = Place(TEXT("Stockpile"), Pt(-1, 2, 2), -1), LowB = Place(TEXT("Stockpile"), Pt(-1, -1, -1), -1);
		Sim->Debug_AddSolidAt(LowA, NStruct, 1000.0, false);
		FTransitSuiteBot Low = MakeBot(TEXT("RC_H"), At(-1, 2, 2));
		FRHSiteRef LF, LT; LF.BuildingId = LowA; LT.BuildingId = LowB;
		StartHaul(Low, Sim->Debug_PostHaulTask(LF, LT, NStruct, 200.0, Low.H));
		const FString Ledger0 = LedgerDigest();
		const int32 Deep0 = Sim->GetDeepDeliveries();
		bool bLow = false;
		for (int32 K = 0; K < 2000 && !bLow; ++K) { Sim->SetRobotStepEnd(Clock->GetSimSecondsTotal() + 0.1); bLow = HaulTick(Low); Sim->SetRobotStepEnd(-1.0); }
		Expect(bLow && LedgerDigest() == Ledger0 && Sim->GetDeepDeliveries() == Deep0 + 1 && Low.HandoffSteps == 0 && InKg(LowB, NStruct) == 200.0,
			TEXT("TR9 a hauler below unloading at a store on its own floor holds nothing and writes no ledger entry; it counts as a delivery below"));
		// The charge rule during a hold (both brains read the same two sim calls): the hold left as idle Wh x 1.25.
		Expect(OneShaft(1, 12, 0) && SinkOne(1, 6) && Fit(0), TEXT("TR9 seek setup"));
		const int32 Dest = Place(TEXT("Stockpile"), Pt(-1, 31, 1), -1);
		const int32 Src = Place(TEXT("Stockpile"), HeadXY(1), 0);
		Sim->Debug_AddSolidAt(Src, NStruct, 1000.0, false);
		FTransitSuiteBot Seeker = MakeBot(TEXT("RC_H"), FVector(HeadXY(1).X, HeadXY(1).Y, 50.0));
		FRHSiteRef SF, ST; SF.BuildingId = Src; ST.BuildingId = Dest;
		StartHaul(Seeker, Sim->Debug_PostHaulTask(SF, ST, NStruct, 200.0, Seeker.H));
		for (int32 K = 0; K < 50 && Sim->GetHandoffHoldStepsLeft(Seeker.H) == 0; ++K) { HaulTick(Seeker); }
		const float HoldWh = Sim->GetHandoffHoldReserveWh(Seeker.H, Seeker.T.TaskId, Seeker.R, 0, Seeker.X.GetLocation());
		const float WantWh = (float)(5.0 * ((double)Sim->GetHandoffHoldStepsLeft(Seeker.H) / 10.0) / 50.0 * 1.25);
		int32 PadId = 0; FVector PadCm;
		Sim->FindNearestChargePad(Seeker.X.GetLocation(), PadId, PadCm);
		const float NeedWh = URHSimWorldSubsystem::RobotDriveWh(Seeker.R, 0.0) + URHSimWorldSubsystem::RobotDriveWh(Seeker.R, FVector::DistXY(Seeker.T.TargetCm, PadCm)) + HoldWh;
		Expect(Sim->GetHandoffHoldStepsLeft(Seeker.H) == 450 && FMath::IsNearlyEqual(HoldWh, WantWh, 1.0e-4f)
			&& Sim->HandoffHoldShort(Seeker.H, Seeker.T.TaskId, NeedWh - 1.f, Seeker.R, Seeker.X.GetLocation(), Seeker.T.TargetCm)
			&& !Sim->HandoffHoldShort(Seeker.H, Seeker.T.TaskId, NeedWh + 1.f, Seeker.R, Seeker.X.GetLocation(), Seeker.T.TargetCm),
			FString::Printf(TEXT("TR9 holding at the head the hold left costs 5 W x 45 s / 50 x 1.25 = %.4f Wh (%.4f): the StateTree adds it to finishing the drop, and the legacy brain seeks 1 Wh under drop + hold + home (%.2f Wh) and keeps the drop 1 Wh over"), WantWh, HoldWh, NeedWh));
	}

	// --- TR10. The planner: integer costs, a tunnel, a tie to the lower shaft, a crowd priced in, the unreachable, repeats. ---
	{
		Expect(OneShaft(2, 12, 12) && Fit(0), TEXT("TR10 setup: shaft 0 to -2, twelve cells on each floor, one car"));
		FRHDeepRouteCost C1;
		const bool bP1 = PlanCost(0, HeadXY(0) + FVector(3000.0, 0.0, 50.0), -1, Pt(-1, 2, 2), C1);
		// 1,600 x 3,000 (the walk to the head) + 1,600 x 400 x (0 + 10 + 4) (the ride) + 1,600 x (1,000 + 2,414) (column, landing,
		// a diagonal to (2,1) and a step to (2,2)).
		Expect(bP1 && C1.CostUnits == 19222400 && C1.SurfaceCm == 3000 && C1.LatticeCm == 3414 && C1.RideSeconds == 14 && C1.Rides == 1 && C1.AscentRides == 0,
			FString::Printf(TEXT("TR10 surface to (2,2) on -1: %lld cost units (expect 19,222,400), %lld surface cm, %lld lattice cm, %d ride s, %d ride"), C1.CostUnits, C1.SurfaceCm, C1.LatticeCm, C1.RideSeconds, C1.Rides));
		FRHDeepRouteCost C2;
		const bool bP2 = PlanCost(-1, At(-1, 2, 2), 0, HeadXY(0) + FVector(3000.0, 0.0, 0.0), C2);
		Expect(bP2 && C2.CostUnits == C1.CostUnits && C2.AscentRides == 1, FString::Printf(TEXT("TR10 the way back costs the same (%lld) and is an ascent"), C2.CostUnits));
		// A tunnel on -2 into shaft 1's dig: shaft 1 (no car) has no landing to offer, so the route rides shaft 0 and walks the tunnel.
		Expect(SinkOne(2, 6), TEXT("TR10 shaft 1 at (30,1) to -2 with its own dig, no car"));
		int32 Tid = INDEX_NONE;
		const bool bDug = Sim->Debug_DigTunnel(-2, FIntPoint(3, 0), FIntPoint(28, 1), Tid, Why);
		const int32 Laid = bDug ? Sim->Debug_CarveTunnelCells(Tid, 64) : 0;
		FRHDeepRouteCost C3;
		FRHRobotRoadPath P3;
		const bool bP3 = Sim->PlanDeepRoute(0, FVector(HeadXY(0).X, HeadXY(0).Y, 50.0), -2, Pt(-2, 31, 1), Clock->GetSimSecondsTotal(), P3, &C3);
		bool bRidesZero = true;
		for (int32 Leg : P3.LegEdge) { bRidesZero &= Leg >= 0 || Leg == -1; }
		// The lattice by hand: (1,0) -> (2,0) -> ... (28,0) is 27 x 1,000; (28,0) -> (29,1) a diagonal 1,414 ((29,0) and (28,1) are
		// carved); then (29,2), (30,2), (31,2), (31,1) - (30,1) is shaft 1's column - 4 x 1,000; plus the column-landing 1,000.
		Expect(bDug && Laid == 27 && bP3 && bRidesZero && C3.LatticeCm == 1000 + 27000 + 1414 + 4000 && C3.RideSeconds == 18,
			FString::Printf(TEXT("TR10 to (31,1) on -2 through shaft 0 (two floors, 10 + 8 s) and a 27-cell tunnel: %lld lattice cm (expect 33,414), %d ride s, only shaft 0 ridden"), C3.LatticeCm, C3.RideSeconds));
		// Unreachable: rock, and a dig no fitted landing reaches.
		FRHRobotRoadPath PNo;
		Expect(!Sim->PlanDeepRoute(0, HeadXY(0) + FVector(0.0, 0.0, 50.0), -1, Pt(-1, 9, 9), Clock->GetSimSecondsTotal(), PNo) && !Sim->PlanDeepRoute(0, HeadXY(0) + FVector(0.0, 0.0, 50.0), -1, Pt(-1, 31, 1), Clock->GetSimSecondsTotal(), PNo),
			TEXT("TR10 a target in rock, and one in a dig with no fitted landing (shaft 1's on -1), plan no route"));
		// The tie: two fitted shafts at (0,0) and (2,0), whose landing is the same cell (1,0); a robot half-way between the heads.
		Reset();
		Sim->ExtendShaft(1, Head0);
		Sim->ExcavateFloor(-1, 1, R, 0);
		int32 TieId = INDEX_NONE;
		Place(TEXT("Pylon"), Pt(0, 2, 0) + FVector(0.0, 1000.0, 0.0), 0); // coverage for the second head (the Lander's disc is 30 m)
		const bool bTie = Sim->Debug_SinkShaftAt(Pt(0, 2, 0), 1, TieId, Why) && Fit(0) && Fit(1);
		TArray<FIntVector> Ports;
		Sim->Debug_GetPortals(Ports);
		FRHRobotRoadPath PT;
		const FVector Mid = Pt(0, 1, 0) + FVector(0.0, 0.0, 50.0);
		const bool bPT = Sim->PlanDeepRoute(0, Mid, -1, Pt(-1, 1, 0), Clock->GetSimSecondsTotal(), PT);
		const int32 TieRide = PT.LegEdge.IndexOfByPredicate([](int32 L) { return L < 0; });
		Expect(bTie && Ports.Num() == 4 && bPT && TieRide != INDEX_NONE && PT.LegEdge[TieRide] == -1,
			FString::Printf(TEXT("TR10 a tie between shafts 0 and 1 (four portals: two heads, one landing (1,0) each) goes to the lower Id: the ride is on shaft %d"), TieRide != INDEX_NONE ? -PT.LegEdge[TieRide] - 1 : -1));
		// A crowd priced in: shaft 0 waits 120 s from the next sol; a robot past the lag plans shaft 1.
		const int32 S = SolNow();
		Sim->Debug_ChargeTransit(0, 2000, S * SolLen + 1.0);
		Sim->Debug_RollTransit(S + 2);
		FRHRobotRoadPath PC;
		const bool bPC = Sim->PlanDeepRoute(0, Mid, -1, Pt(-1, 1, 0), (S + 2) * SolLen + 10.0, PC);
		const int32 CrowdRide = PC.LegEdge.IndexOfByPredicate([](int32 L) { return L < 0; });
		Expect(Sim->GetTransitWaitSec(0) == 120 && bPC && CrowdRide != INDEX_NONE && PC.LegEdge[CrowdRide] == -2,
			FString::Printf(TEXT("TR10 with shaft 0 waiting %d s the planner steers to shaft 1 (the ride is on shaft %d)"), Sim->GetTransitWaitSec(0), CrowdRide != INDEX_NONE ? -PC.LegEdge[CrowdRide] - 1 : -1));
		// The same route twice, and again after a save and a load.
		FRHRobotRoadPath PA, PB, PL;
		const double Stamp = (S + 2) * SolLen + 10.0;
		Sim->PlanDeepRoute(0, Mid, -1, Pt(-1, 1, 0), Stamp, PA);
		Sim->PlanDeepRoute(0, Mid, -1, Pt(-1, 1, 0), Stamp, PB);
		Sim->SaveColony(TEXT("transitplan"), SaveErr);
		FString Le;
		Sim->LoadColony(TEXT("transitplan"), Le);
		Sim->PlanDeepRoute(0, Mid, -1, Pt(-1, 1, 0), Stamp, PL);
		Expect(PA.Waypoints == PB.Waypoints && PA.LegEdge == PB.LegEdge && PA.Waypoints == PL.Waypoints && PA.LegEdge == PL.LegEdge && PA.Levels == PL.Levels && Le.IsEmpty(),
			FString::Printf(TEXT("TR10 the same route twice, and the same after a save and a load (%d waypoints) (%s)"), PA.Waypoints.Num(), *Le));
	}

	// --- TR11. The ride: boarding ends a sub-step, the hold, 140 ride sub-steps, Z flips at the end of the last, no move in it. ---
	{
		for (const int32 Wait : { 0, 5 })
		{
			Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR11 setup"));
			if (Wait > 0)
			{
				const int32 S = SolNow();
				Sim->Debug_SetTransitCapacity(200, 120);
				Sim->Debug_ChargeTransit(0, 125, S * SolLen + 1.0); // 125 places through a 120-place car: ceil(5 x 120 / 120) = 5 s
				Sim->Debug_RollTransit(S + 2);
				Clock->Debug_SetSimSeconds((S + 2) * SolLen + 10.0);
			}
			const int32 W = Sim->GetTransitWaitSec(0);
			FTransitSuiteBot Bot = MakeBot(TEXT("RC_H"), FVector(HeadXY(0).X, HeadXY(0).Y, 50.0));
			const FVector Target = Pt(-1, 1, 0);
			int32 FlipAt = -1, Arrived = -1;
			bool bStillAtFlip = false;
			for (int32 K = 1; K <= 2000; ++K)
			{
				const FVector Before = Bot.X.GetLocation();
				Sim->SetRobotStepEnd(Clock->GetSimSecondsTotal() + 0.1);
				const bool bArr = RH::MoveAlong(*Sim, Bot.H, Bot.X, Target, Bot.R.SpeedMps, AgentDt, false, 0.f, -1);
				Sim->SetRobotStepEnd(-1.0);
				const FVector After = Bot.X.GetLocation();
				if (FlipAt < 0 && Sim->LevelOfZ(After.Z) == -1) { FlipAt = K; bStillAtFlip = Before.X == After.X && Before.Y == After.Y && Before.Z == 50.0 && After.Z == RobotZ(-1); }
				if (bArr) { Arrived = K; break; }
			}
			// Boarding is sub-step 1 (a zero-length walk onto the column), then 10 x W of hold, then 140 of ride; the walk off is 1,000 cm
			// at 40 cm a sub-step, arriving once within 300 cm: 19 more calls.
			Expect(W == Wait && FlipAt == 1 + 10 * Wait + 140 && bStillAtFlip && Arrived == FlipAt + 19,
				FString::Printf(TEXT("TR11 wait %d s: Z flips at the end of sub-step %d (expect 1 + %d + 140 = %d) and the robot does not move in it; it arrives at sub-step %d (expect %d)"), W, FlipAt, 10 * Wait, 1 + 10 * Wait + 140, Arrived, FlipAt + 19));
			Sim->Debug_SetTransitCapacity(200, 1000);
		}
	}

	// --- TR12. Haul and build below; the fallback; TN12b; the source-below stall. ---
	{
		Expect(OneShaft(2, 12, 12) && SinkOne(2, 6) && Fit(0), TEXT("TR12 setup: shaft 0 fitted to -2, shaft 1 a hoist to -2, digs on both floors"));
		int32 Tid = INDEX_NONE;
		Sim->Debug_DigTunnel(-2, FIntPoint(3, 0), FIntPoint(28, 1), Tid, Why);
		Sim->Debug_CarveTunnelCells(Tid, 64);
		// (a) A haul to a store in dig B on -2 - reached by riding shaft 0 and walking the tunnel - not dropped at a head.
		const int32 Lab = Place(TEXT("Stockpile"), Pt(-2, 31, 1), -2);
		const int32 Src = Place(TEXT("Stockpile"), FVector(-2000.0, 2000.0, 0.0), 0);
		Sim->Debug_AddSolidAt(Src, NStruct, 1000.0, false);
		FTransitSuiteBot Hauler = MakeBot(TEXT("RC_H"), FVector(-2000.0, 2000.0, 50.0));
		FRHSiteRef F1, T1; F1.BuildingId = Src; T1.BuildingId = Lab;
		StartHaul(Hauler, Sim->Debug_PostHaulTask(F1, T1, NStruct, 200.0, Hauler.H));
		const int32 Deep0 = Sim->GetDeepDeliveries(), Off0 = Sim->GetDeepOffLattice();
		const int32 S = SolNow();
		bool bDone = false;
		for (int32 K = 0; K < 30000 && !bDone; ++K) { Sim->SetRobotStepEnd(Clock->GetSimSecondsTotal() + 0.1); bDone = HaulTick(Hauler); Sim->SetRobotStepEnd(-1.0); }
		Expect(bDone && InKg(Lab, NStruct) == 200.0 && Sim->GetDeepDeliveries() == Deep0 + 1 && Hauler.HandoffSteps == 0 && Sim->LevelOfZ(Hauler.X.GetLocation().Z) == -2 && SlotPlaces(0, S) >= 2,
			FString::Printf(TEXT("TR12 (a) a hauler rode shaft 0 to -2 and walked the tunnel into dig B: the store was credited once (%.0f kg), as a delivery below, with no hand-off; the ride charged shaft 0 its cargo places (%d)"), InKg(Lab, NStruct), SlotPlaces(0, S)));
		// (b) A Fabricator reaches the site's cell and works it there.
		Sim->Debug_AddSolidAt(LanderId, NStruct, 4000.0, false);
		Sim->AddStock(NOx, 200.0);
		FRHCommand Build; Build.Verb = FName("Build"); Build.Target = FName("AirFilter"); Build.Level = -1; Build.Location = Pt(-1, 2, 2);
		Send(Build);
		const int32 Site = LastIdOf(TEXT("AirFilter"));
		const FRHBuildingInstance* SiteB = FindB(Site);
		if (SiteB) { for (const auto& Line : Sim->GetSiteBill(*SiteB)) { Sim->Debug_AddSolidAt(Site, Line.Key, Line.Value, false); } }
		FTransitSuiteBot Fab = MakeBot(TEXT("RC_F"), FVector(-800.0, -600.0, 50.0));
		FRHTask Claim;
		const bool bClaimed = Sim->TryClaimBuild(Fab.H, Fab.X.GetLocation(), Claim, 0) && Claim.To.BuildingId == Site;
		int32 TL = 0;
		const FVector BuildAt = Sim->GetRobotTarget(Claim.To, 0, Fab.X.GetLocation(), TL);
		const int32 Builds0 = Sim->GetDeepBuildsCompleted();
		const int32 Steps = Drive(Fab, BuildAt, TL, 20000);
		bool bBuilt = false;
		for (int32 K = 0; K < 200000 && !bBuilt && Steps > 0; ++K) { bBuilt = Sim->ApplyBuildWork(Site, (double)AgentDt * Fab.R.WorkRate, BotLevel(Fab)); }
		Expect(bClaimed && TL == -1 && BuildAt == SiteB->LocationCm && Steps > 0 && bBuilt && Sim->GetDeepBuildsCompleted() == Builds0 + 1,
			FString::Printf(TEXT("TR12 (b) a surface Fabricator claims the site on -1 (in shaft 0's dig), is targeted at the site itself on floor %d, rides down (%d sub-steps) and finishes it standing below"), TL, Steps));
		// (c) The fallback: a site in rock and a site in an unfitted dig get the surface head, level 0.
		const int32 RockSite = Place(TEXT("Stockpile"), Pt(-1, 8, 8), -1);
		FRHSiteRef RockRef, UnfitRef; RockRef.BuildingId = RockSite; UnfitRef.BuildingId = Place(TEXT("Stockpile"), Pt(-1, 31, 1), -1);
		int32 L1 = 7, L2 = 7;
		const FVector RockTo = Sim->GetRobotTarget(RockRef, 0, FVector(0.0, 0.0, 50.0), L1);
		const FVector UnfitTo = Sim->GetRobotTarget(UnfitRef, -1, At(-1, 2, 2), L2);
		Expect(L1 == 0 && L2 == 0 && RockTo == Sim->GetApproachPoint(RockRef, 0) && UnfitTo == Sim->GetApproachPoint(UnfitRef, 0) && UnfitTo.Z == 0.0,
			TEXT("TR12 (c) a site in rock, and one in a dig no fitted landing reaches, are targeted at the surface head (level 0) from the surface and from below alike"));
		Expect(Sim->GetDeepOffLattice() == Off0 && Sim->GetFloorCellFallbacks() == 0, TEXT("TR12 no plan started off the lattice, no ordinal read fell back"));
		// (d) TN12b: a hauler on -1 never claims a task it cannot reach; a surface hauler takes it and drops at the surface head.
		Sim->Debug_ClearTasks();
		const int32 LowSrc = Place(TEXT("Stockpile"), Pt(-1, 1, 1), -1);
		Sim->Debug_AddSolidAt(LowSrc, NStruct, 1000.0, false);
		FRHSiteRef LowRef; LowRef.BuildingId = LowSrc;
		FRHSiteRef DeepRock; DeepRock.BuildingId = Place(TEXT("Stockpile"), Pt(-2, 9, -9), -2);
		const int32 Haul = Sim->Debug_PostHaulTask(LowRef, DeepRock, NStruct, 200.0);
		FRHTask Got;
		const bool bBelowClaims = Sim->TryClaimHaul(FMassEntityHandle(980001, 1), At(-1, 2, 2), Got, -1);
		const bool bTopClaims = Sim->TryClaimHaul(FMassEntityHandle(980002, 1), FVector(0.0, 0.0, 50.0), Got, 0);
		float Loaded = 0.f; FVector Drop; int32 DropLevel = 7;
		const bool bLoaded = bTopClaims && Sim->HaulLoad(Haul, 200.f, Loaded, Drop, -1, &DropLevel);
		Expect(!bBelowClaims && bTopClaims && Got.Id == Haul && bLoaded && DropLevel == 0 && Drop == Sim->GetApproachPoint(DeepRock, 0),
			TEXT("TR12 (d) TN12b: a hauler on -1 does not claim a haul whose drop is in rock on -2; a surface hauler does, and its drop is the surface head with level 0"));
		FRHTask Rock;
		Sim->Debug_ClearTasks();
		FRHCommand RockBuild; RockBuild.Verb = FName("Build"); RockBuild.Target = FName("AirFilter"); RockBuild.Level = -1; RockBuild.Location = Pt(-1, 9, 9);
		Send(RockBuild);
		Expect(!Sim->TryClaimBuild(FMassEntityHandle(980003, 1), At(-1, 2, 2), Rock, -1), TEXT("TR12 (d) TN12b: a Fabricator on -1 does not claim an AirFilter site in rock on -1"));
		// (e) The source-below stall: a surface site whose only source is a store below. Before a car nobody can claim the
		// delivery; with one, a surface robot and a robot below both may.
		Expect(OneShaft(1, 12, 0), TEXT("TR12 (e) stall setup"));
		for (const FRHBuildingInstance& Bd : Sim->GetBuildings()) { if (InKg(Bd.Id, NStruct) > 0.0) { Sim->Debug_AddSolidAt(Bd.Id, NStruct, -InKg(Bd.Id, NStruct), false); } }
		const int32 Below = Place(TEXT("Stockpile"), Pt(-1, 1, 1), -1);
		Sim->Debug_AddSolidAt(Below, NStruct, 2000.0, false);
		FRHCommand Pad; Pad.Verb = FName("Build"); Pad.Target = FName("ChargePad"); Pad.Location = FVector(-2000.0, 1000.0, 0.0);
		Send(Pad);
		Sim->Debug_StepTaskBoard();
		const bool bPosted = Sim->GetTasks().ContainsByPredicate([&](const FRHTask& T) { return T.Type == ERHTaskType::Haul && T.From.BuildingId == Below; });
		FRHTask Stall;
		const bool bBefore = Sim->TryClaimHaul(FMassEntityHandle(980004, 1), FVector(0.0, 0.0, 50.0), Stall, 0);
		Fit(0);
		const bool bAfterTop = Sim->TryClaimHaul(FMassEntityHandle(980005, 1), FVector(0.0, 0.0, 50.0), Stall, 0) && Stall.From.BuildingId == Below;
		Sim->AbandonTask(Stall.Id);
		const bool bAfterBelow = Sim->TryClaimHaul(FMassEntityHandle(980006, 1), At(-1, 2, 2), Stall, -1) && Stall.From.BuildingId == Below;
		Expect(bPosted && !bBefore && bAfterTop && bAfterBelow, TEXT("TR12 (e) the site hopper posts a delivery from a store below; with no car no robot can claim it (the D3 stall); with a car a surface robot and a robot below both can"));
	}

	// --- TR13. Claims: reachable only from below, one ranking key (O9), ties to task order. ---
	{
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR13 setup"));
		Sim->Debug_ClearTasks();
		const auto Store = [&](const FVector& Cm, int32 Level) { const int32 Id = Place(TEXT("Stockpile"), Cm, Level); Sim->Debug_AddSolidAt(Id, NStruct, 1000.0, false); FRHSiteRef Ref; Ref.BuildingId = Id; return Ref; };
		const FRHSiteRef Dst = Store(FVector(-6000.0, 0.0, 0.0), 0);
		const FRHSiteRef Surface4000 = Store(FVector(2000.0, 0.0, 0.0), 0); // 2,000 cm due south of the robot at (2000, 2000)
		const FRHSiteRef Below = Store(Pt(-1, 1, 1), -1);
		const FRHSiteRef Rock = Store(Pt(-1, 7, 7), -1);
		// From the surface at (2000, 2000): the floor store's XY is 1,000 cm east and 1,000 north (DistXY 1,414) + 1 floor x 1,500 = 2,914;
		// the surface store is 2,000 straight south: 2,000 wins.
		const int32 TBelow = Sim->Debug_PostHaulTask(Below, Dst, NStruct, 100.0);
		const int32 TSurf = Sim->Debug_PostHaulTask(Surface4000, Dst, NStruct, 100.0);
		const int32 TRock = Sim->Debug_PostHaulTask(Rock, Dst, NStruct, 100.0);
		FRHTask Got;
		const bool bTop = Sim->TryClaimHaul(FMassEntityHandle(970001, 1), FVector(2000.0, 2000.0, 50.0), Got, 0);
		Expect(bTop && Got.Id == TSurf, FString::Printf(TEXT("TR13 from the surface the key is DistXY + floors x 1,500: the surface store at 2,000 beats the floor store at 1,414 + 1,500 = 2,914 (claimed task %d, expect %d)"), Got.Id, TSurf));
		const bool bBelow = Sim->TryClaimHaul(FMassEntityHandle(970002, 1), At(-1, 2, 2), Got, -1);
		Expect(bBelow && Got.Id == TBelow, TEXT("TR13 a robot on -1 takes the floor store (reachable), never the store in rock"));
		const bool bNone = Sim->TryClaimHaul(FMassEntityHandle(970003, 1), At(-1, 2, 2), Got, -1);
		Expect(!bNone && !Sim->GetTasks().FindByPredicate([TRock](const FRHTask& T) { return T.Id == TRock; })->ClaimedBy.IsValid(),
			TEXT("TR13 nothing else is reachable from below: the store in rock stays unclaimed"));
		// A tie: two identical floor stores' tasks at the same key go to the first in task order.
		Sim->Debug_ClearTasks();
		const FRHSiteRef TwinA = Store(Pt(-1, -1, 1), -1), TwinB = Store(Pt(-1, -1, 1), -1);
		const int32 TA = Sim->Debug_PostHaulTask(TwinA, Dst, NStruct, 100.0);
		Sim->Debug_PostHaulTask(TwinB, Dst, NStruct, 100.0);
		Expect(Sim->TryClaimHaul(FMassEntityHandle(970004, 1), At(-1, 1, 1), Got, -1) && Got.Id == TA, TEXT("TR13 equal keys go to the task posted first"));
	}


	// --- TR14. Charge below: the route-true reserve (TN14), boarding at the surface reserve (TN14b), the ascent hold priced at
	// MaxWaitSec (TR14c), and seeded random digs (O10). ---
	const auto PadAt = [&]() { const int32 Id = Place(TEXT("ChargePad"), Pt(0, 5, 0), 0); return Id; };
	{
		Expect(OneShaft(2, 12, 12) && Fit(0), TEXT("TR14 setup: shaft 0 to -2, one car"));
		PadAt();
		// TR14c (i): an RC-H on -1's landing, the pad 50 m from the head: DriveWh(1,000 + 5,000 cm) = 80 x 15 / 50 x 1.25 + 40 = 70,
		// the ride 80 x 14 / 50 x 1.25 = 28, the ascent hold at MaxWaitSec 5 x 600 / 50 x 1.25 = 75: 173 Wh.
		FTransitSuiteBot Probe = MakeBot(TEXT("RC_H"), At(-1, 1, 0));
		int32 PadId = 0; FVector PadCm;
		Sim->FindNearestChargePad(Probe.X.GetLocation(), PadId, PadCm);
		const float Reserve = Sim->DeepReserveWh(Probe.H, Probe.R, Probe.X.GetLocation(), PadId, PadCm);
		Expect(Reserve == 173.f && Sim->GetTransitWaitSec(0) == 0, FString::Printf(TEXT("TR14c (i) wait 0, yet the reserve on -1's landing prices the ascent hold at 600 s: %.3f Wh (expect 70 + 28 + 75 = 173)"), Reserve));
		// TN14: from the farthest cell of -2 at exactly its reserve, the hauler reaches the pad with charge left.
		int32 FarOrd = 0;
		float FarWh = 0.f;
		for (int32 o = 0; o < Sim->GetFloorCarvedCells(-2); ++o)
		{
			const FIntPoint P = Sim->GetFloorCell(-2, o);
			FTransitSuiteBot Each = MakeBot(TEXT("RC_H"), At(-2, P.X, P.Y));
			int32 Pid = 0; FVector Pcm;
			Sim->FindNearestChargePad(Each.X.GetLocation(), Pid, Pcm);
			const float Wh = Sim->DeepReserveWh(Each.H, Each.R, Each.X.GetLocation(), Pid, Pcm);
			if (Wh > FarWh) { FarWh = Wh; FarOrd = o; }
		}
		const FIntPoint Far = Sim->GetFloorCell(-2, FarOrd);
		FTransitSuiteBot Runner = MakeBot(TEXT("RC_H"), At(-2, Far.X, Far.Y));
		Runner.ChargeWh = FarWh + 1.f;
		const ERHDeepCharge Seek = Sim->DeepChargeTest(Runner.H, Runner.R, Runner.X.GetLocation(), Runner.ChargeWh, Runner.CapWh);
		const int32 Home = Drive(Runner, PadCm, 0, 20000);
		Expect(Seek == ERHDeepCharge::Seek && Home > 0 && Runner.MinChargeWh > 0.f && Sim->LevelOfZ(Runner.X.GetLocation().Z) == 0,
			FString::Printf(TEXT("TN14 / TN14b a hauler on the farthest cell of -2, (%d,%d), at its reserve + 1 Wh (%.1f) seeks, ascends and docks in %d sub-steps with %.2f Wh at its lowest"), Far.X, Far.Y, Runner.ChargeWh, Home, Runner.MinChargeWh));
		// TN14b: boarding at the surface for a -1 drop with the surface rule's charge + 1 Wh: the descent test seeks at boarding.
		const int32 DropStore = Place(TEXT("Stockpile"), Pt(-1, 1, 0), -1);
		FTransitSuiteBot Boarder = MakeBot(TEXT("RC_H"), FVector(HeadXY(0).X, HeadXY(0).Y, 50.0));
		Boarder.ChargeWh = URHSimWorldSubsystem::RobotDriveWh(Boarder.R, 1000.0) + URHSimWorldSubsystem::RobotDriveWh(Boarder.R, 4000.0) + 1.f; // the mid-delivery rule's drop + home, + 1
		const ERHDeepCharge Walking = Sim->DeepChargeTest(Boarder.H, Boarder.R, Boarder.X.GetLocation(), Boarder.ChargeWh, Boarder.CapWh);
		Sim->SetRobotStepEnd(Clock->GetSimSecondsTotal() + (double)AgentDt);
		RH::MoveAlong(*Sim, Boarder.H, Boarder.X, FindB(DropStore)->LocationCm, Boarder.R.SpeedMps, AgentDt, false, 200.f, -1);
		Sim->SetRobotStepEnd(-1.0);
		FRHRideState Ride;
		const bool bBoarded = Sim->GetRideState(Boarder.H, Ride);
		const ERHDeepCharge AtBoarding = Sim->DeepChargeTest(Boarder.H, Boarder.R, Boarder.X.GetLocation(), Boarder.ChargeWh, Boarder.CapWh);
		Expect(Walking == ERHDeepCharge::NotApplicable && bBoarded && !Ride.IsAscent() && AtBoarding == ERHDeepCharge::Seek && Boarder.X.GetLocation().Z == 50.0,
			FString::Printf(TEXT("TN14b at the surface (no ride boarded) the deep test does not apply and today's rule runs; once boarded for -1 with %.1f Wh the descent test seeks - the robot is still on the surface"), Boarder.ChargeWh));
		// TR14c (ii): a threshold crossed during the walk to the landing sets MaxWaitSec; the hauler at its reserve + 1 Wh holds
		// 6,000 sub-steps, ascends and docks, never at 0.
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR14c (ii) setup"));
		PadAt();
		const int32 Sc = SolNow() + 1;
		const double Bc = (double)(Sc + 1) * SolLen;
		Clock->Debug_SetSimSeconds(Bc - 2.0);
		Sim->Debug_RollTransit(Sc);
		Sim->Debug_ChargeTransit(0, 6000, (Sc - 1) * SolLen + 1.0); // 6 x Cap in the slot the roll into Sc + 1 reads
		FTransitSuiteBot Walker = MakeBot(TEXT("RC_H"), At(-1, 2, 2));
		Sim->FindNearestChargePad(Walker.X.GetLocation(), PadId, PadCm);
		Walker.ChargeWh = Sim->DeepReserveWh(Walker.H, Walker.R, Walker.X.GetLocation(), PadId, PadCm) + 1.f;
		const float StartWh = Walker.ChargeWh;
		const int32 Docked = Drive(Walker, PadCm, 0, 30000, true);
		Expect(Sim->GetTransitWaitSec(0) == 600 && Walker.RideHolds == 6000 && Docked > 0 && Walker.MinChargeWh > 0.f,
			FString::Printf(TEXT("TR14c (ii) from (2,2) on -1 with %.2f Wh: the roll during the walk set shaft 0's wait to %d s; it held %d sub-steps, rode up and docked (%d sub-steps), never empty (%.2f Wh at its lowest)"), StartWh, Sim->GetTransitWaitSec(0), Walker.RideHolds, Docked, Walker.MinChargeWh));
		// O10: fifty seeded rounds of carves and tunnels; every carved cell in a fitted dig still has a route to the pad.
		Expect(OneShaft(2, 12, 12) && Fit(0), TEXT("TR14 random setup"));
		PadAt();
		FRandomStream Rng(0xD4);
		int32 Dug = 0;
		for (int32 Round = 0; Round < 50; ++Round)
		{
			const int32 L = Rng.RandRange(0, 1) == 0 ? -1 : -2;
			if (Rng.RandRange(0, 2) == 0)
			{
				Sim->ExcavateFloor(L, Rng.RandRange(1, 3), R, 0);
			}
			else
			{
				const TArray<FIntPoint>& Have = Sim->FindFloor(L)->GetCells();
				const FIntPoint Dirs[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
				const FIntPoint A = Have[Rng.RandRange(0, Have.Num() - 1)] + Dirs[Rng.RandRange(0, 3)];
				const FIntPoint B = A + FIntPoint(Rng.RandRange(-8, 8), Rng.RandRange(-8, 8));
				int32 Id = INDEX_NONE;
				if (Sim->Debug_DigTunnel(L, A, B, Id, Why)) { ++Dug; Sim->Debug_CarveTunnelCells(Id, Rng.RandRange(1, 20)); }
			}
		}
		// The oracle (no code shared with the sim): a flood fill over 4-neighbours from each floor's landings (shaft 0's column
		// neighbours, the lowest ordinal) marks the cells of a fitted dig; each of them must plan a route up.
		int32 Fitted = 0, Routed = 0;
		for (const int32 L : { -1, -2 })
		{
			const FRHFloor* Fl = Sim->FindFloor(L);
			TSet<FIntPoint> Seen;
			TArray<FIntPoint> Stack;
			int32 Landing = INDEX_NONE;
			for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const int32 O = Fl ? Fl->FindOrdinal(D) : INDEX_NONE;
				if (O != INDEX_NONE && (Landing == INDEX_NONE || O < Landing)) { Landing = O; }
			}
			if (Landing != INDEX_NONE) { Stack.Add(Fl->GetCells()[Landing]); Seen.Add(Stack.Last()); }
			while (Stack.Num())
			{
				const FIntPoint Cur = Stack.Pop();
				for (const FIntPoint D : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
				{
					if (Fl->FindOrdinal(Cur + D) != INDEX_NONE && !Seen.Contains(Cur + D)) { Seen.Add(Cur + D); Stack.Add(Cur + D); }
				}
			}
			for (const FIntPoint& P : Seen)
			{
				FRHRobotRoadPath Path;
				++Fitted;
				Routed += Sim->PlanDeepRoute(L, At(L, P.X, P.Y), 0, PadCm, Clock->GetSimSecondsTotal(), Path) ? 1 : 0;
			}
		}
		Expect(Dug >= 2 && Fitted > 24 && Routed == Fitted, FString::Printf(TEXT("TR14 (O10) after 50 seeded rounds (%d tunnels dug): every one of the %d cells in a fitted dig plans a route up to the pad (%d)"), Dug, Fitted, Routed));
	}

	// --- TR15. Repair across floors: halted patients only; released when the floors part; never through the rock. ---
	{
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR15 setup"));
		TArray<FRHRoverState> Ms, Hs;
		Agents->CollectRobotsOfClass(FName("Maintenance"), Ms);
		Agents->CollectRobotsOfClass(FName("Hauler"), Hs);
		Expect(Ms.Num() >= 1 && Hs.Num() >= 2, TEXT("TR15 the real fleet has an RC-M and two RC-H"));
		if (Ms.Num() >= 1 && Hs.Num() >= 2)
		{
			const FMassEntityHandle M = Ms[0].Entity, P = Hs[0].Entity;
			FTransitSuiteBot Mb = MakeBot(TEXT("RC_M"), Ms[0].PosCm);
			Mb.H = M;
			const auto RepairTick = [&](FMassEntityHandle Target) -> int32
			{
				FVector Pos;
				Agents->GetRobotPosition(M, Pos);
				Mb.X.SetLocation(Pos);
				FVector TCm;
				int32 Res = 0;
				if (!Sim->GetRepairTargetPos(Target, TCm)) { Sim->ReleaseRepairClaim(Target); return -1; }
				const int32 Level = Sim->LevelOfZ(Pos.Z), PLevel = Sim->LevelOfZ(TCm.Z);
				if (PLevel != Level || Level < 0)
				{
					if (!Sim->IsRobotHalted(Target)) { Sim->ReleaseRepairClaim(Target); Res = -1; }
					else if (RH::MoveAlong(*Sim, M, Mb.X, TCm, Mb.R.SpeedMps, AgentDt, false, 0.f, PLevel)) { Sim->ApplyRepairAt(Target, Sim->LevelOfZ(Mb.X.GetLocation().Z)); Sim->ReleaseRepairClaim(Target); Res = 1; }
				}
				else if (RH::MoveToward(Mb.X, TCm, Mb.R.SpeedMps, AgentDt)) { Sim->ApplyRepairAt(Target, Level); Sim->ReleaseRepairClaim(Target); Res = 1; }
				Agents->SetRobotPosition(M, Mb.X.GetLocation());
				return Res;
			};
			// TN15: a halted hauler on -1; the RC-M on the surface claims it, rides down and repairs it there.
			Agents->SetRobotPosition(P, At(-1, 2, 2));
			Agents->Debug_SetRobotWear(P, 100.f);
			FMassEntityHandle Target; FVector TCm;
			const bool bClaim = Sim->TryClaimRepair(M, Target, TCm) && Target == P;
			int32 Res = 0, K = 0;
			for (; K < 20000 && Res == 0 && bClaim; ++K) { Res = RepairTick(P); }
			float WearAfter = 0.f; Agents->GetRobotWear(P, WearAfter);
			FVector MPos; Agents->GetRobotPosition(M, MPos);
			Expect(bClaim && Res == 1 && WearAfter < 100.f && Sim->LevelOfZ(MPos.Z) == -1,
				FString::Printf(TEXT("TN15 the RC-M claims the halted hauler on -1 from the surface, rides down (%d sub-steps) and repairs it on its floor (wear 100 -> %.0f)"), K, WearAfter));
			// A worn but working robot below is not claimed from the surface (or from below).
			Agents->SetRobotPosition(M, FVector(-800.0, -600.0, 50.0));
			Agents->Debug_SetRobotWear(P, 70.f);
			Expect(!Sim->TryClaimRepair(M, Target, TCm), TEXT("TN15 a worn (70) but working hauler on -1 is not claimed from the surface"));
			// TN15b (a): a worn, working surface patient is claimed, then rides to -1: the claim is released, no repair through the rock.
			Agents->SetRobotPosition(P, FVector(3000.0, -2000.0, 50.0));
			const bool bClaimA = Sim->TryClaimRepair(M, Target, TCm) && Target == P;
			Agents->SetRobotPosition(P, At(-1, 1, 0));
			const int32 ResA = RepairTick(P);
			float WearA = 0.f; Agents->GetRobotWear(P, WearA);
			Expect(bClaimA && ResA == -1 && WearA == 70.f, TEXT("TN15b (a) a worn working patient claimed on the surface rides to -1: the claim is released and its wear is untouched"));
			// TN15b (b): an RC-M on -1 and a worn, moving surface patient: no claim, and no deep plan over 200 sub-steps.
			Agents->SetRobotPosition(M, At(-1, 2, 2));
			Agents->SetRobotPosition(P, FVector(3000.0, -2000.0, 50.0));
			Mb.X.SetLocation(At(-1, 2, 2));
			Sim->DeepChargeTest(M, Mb.R, Mb.X.GetLocation(), 1000.f, 1200.f); // the memo is warm
			const int32 Plans0 = Sim->GetDeepPlanRuns();
			bool bAny = false;
			for (int32 Step = 0; Step < 200; ++Step)
			{
				Agents->SetRobotPosition(P, FVector(3000.0 + 10.0 * Step, -2000.0, 50.0));
				bAny |= Sim->TryClaimRepair(M, Target, TCm);
				Sim->DeepChargeTest(M, Mb.R, Mb.X.GetLocation(), 1000.f, 1200.f);
			}
			Expect(!bAny && Sim->GetDeepPlanRuns() == Plans0, FString::Printf(TEXT("TN15b (b) from -1 the RC-M never claims the moving worn patient, and runs no deep plan over 200 sub-steps (%d -> %d)"), Plans0, Sim->GetDeepPlanRuns()));
			Agents->Debug_SetRobotWear(P, 0.f);
		}
	}

	// --- TR16b. Floor freight is hauler work: with a rover on a road, a 1,000 kg haul to a site on -1 is not freight. ---
	{
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR16b setup"));
		Sim->Debug_LayRoad(FVector(0.0, -3000.0, 0.0), FVector(40000.0, -3000.0, 0.0), true);
		const int32 Rock = Place(TEXT("Stockpile"), Pt(-1, 5, 5), -1);
		const int32 FarA = Place(TEXT("Stockpile"), FVector(2000.0, -2000.0, 0.0), 0), FarB = Place(TEXT("Stockpile"), FVector(38000.0, -2000.0, 0.0), 0);
		Sim->Debug_AddSolidAt(FarA, NStruct, 5000.0, false);
		FRHSiteRef LanderRef, RockRef, ARef, BRef; LanderRef.BuildingId = FarA; RockRef.BuildingId = Rock; ARef.BuildingId = FarA; BRef.BuildingId = FarB;
		const int32 ToFloor = Sim->Debug_PostHaulTask(LanderRef, RockRef, NStruct, 1000.0);
		const int32 Surface = Sim->Debug_PostHaulTask(ARef, BRef, NStruct, 1000.0);
		Sim->Debug_DeliverCargo(FName("CargoRover"));
		Sim->Debug_StepTaskBoard(0.1f);
		FRHTask TF, TS;
		Sim->Debug_GetTask(ToFloor, TF);
		Sim->Debug_GetTask(Surface, TS);
		Expect(Sim->GetActiveRoverCount() == 1 && TS.bFreight && !TF.bFreight, TEXT("TR16b with a rover on the road the surface run is freight; the run to a site on -1 is not (the rover stays on the surface: O1)"));
		FTransitSuiteBot Hauler = MakeBot(TEXT("RC_H"), FVector(2000.0, -2000.0, 50.0));
		FRHTask Claim;
		const bool bClaim = Sim->TryClaimHaul(Hauler.H, Hauler.X.GetLocation(), Claim, 0) && Claim.Id == ToFloor;
		StartHaul(Hauler, ToFloor);
		const int32 S = SolNow();
		const int32 Before = SlotPlaces(0, S);
		bool bDone = false;
		for (int32 K = 0; K < 20000 && !bDone; ++K) { Sim->SetRobotStepEnd(Clock->GetSimSecondsTotal() + 0.1); bDone = HaulTick(Hauler); Sim->SetRobotStepEnd(-1.0); }
		Expect(bClaim && bDone && Hauler.HandoffSteps == 140 && Hauler.DeliveredKg == 200.0 && SlotPlaces(0, S) - Before == 2,
			FString::Printf(TEXT("TR16b a hauler takes it, holds at shaft 0's head %d sub-steps, credits %.0f kg and charges 2 places (%d)"), Hauler.HandoffSteps, Hauler.DeliveredKg, SlotPlaces(0, S) - Before));
	}

	// --- TR17. Era parity with transit on: the ramp open, the people load at exactly capacity, haulers below that surface. ---
	{
		Expect(OneShaft(2, 12, 30) && Fit(0) && Housing(2), TEXT("TR17 setup: shaft 0 to -2 with a car, floors -1 and -2 rated"));
		for (int32 i = 0; i < 30; ++i) { Sim->DesignateRoom(-2, i, FName("Lab"), R); }
		Sim->AddStock(FName("Food"), 1.0e6);
		Sim->AddStock(FName("Water"), 1.0e6);
		Sim->AddStock(NOx, 1.0e6);
		const int32 Housed = Sim->Debug_AddColonists(120);
		Sim->Debug_RefreshJobs();
		Sim->Debug_SetTransitCapacity(200, 1000);
		const int64 PeopleLoad = Sim->GetShaftPeopleLoad(0);
		Sim->Debug_SetTransitCapacity(200, (int32)PeopleLoad); // capacity exactly the people load: a robot body charged 1 place would tip it
		PadAt();
		const int32 ForgeId = Place(TEXT("Forge"), FVector(4000.0, -3000.0, 0.0), 0);
		FRHCommand Dig; Dig.Verb = FName("Dig"); Dig.Target = FName("Regolith_A");
		Send(Dig);
		Expect(Housed == 120 && PeopleLoad > 0 && Sim->GetCommuteRamp() > 0.0 && Sim->SaveColony(TEXT("transitera"), SaveErr),
			FString::Printf(TEXT("TR17 120 colonists, ramp %.2f, %d commuting -1 to -2 through shaft 0: the people load is %lld places and the car carries exactly that (%s)"), Sim->GetCommuteRamp(), Sim->GetCommuteShaftPeople(0), PeopleLoad, *SaveErr));
		const FRHRobotRow* ExcRow = DefsSub->GetRobot(FName("RC_E"));
		const FRHRobotRow* HaulRow = DefsSub->GetRobot(FName("RC_H"));
		const auto RegolithA = [&]() -> const FRHDepositState* { for (const FRHDepositState& Dp : Sim->GetDeposits()) { if (Dp.RowName == FName("Regolith_A")) { return &Dp; } } return nullptr; };
		struct FLeg { double Dug = 0, Struct = 0, Hope = 0; TArray<int32> Waits; int32 Rolls = 0; int32 Plans = 0; bool bSurfaced = true, bNeverEmpty = true; int32 Pop = 0; };
		const auto RunLeg = [&](bool bAgent) -> FLeg
		{
			FLeg L;
			FString Le;
			Sim->LoadColony(TEXT("transitera"), Le);
			Sim->Debug_ForgetRobotTransit();
			Sim->Debug_SetTransitCapacity(200, (int32)PeopleLoad);
			const double A0 = RegolithA() ? RegolithA()->RemainingKg : 0.0, S0 = Sim->GetTotalSolid(NStruct);
			const int32 Thr0 = Sim->GetTransitThresholds(), Plans0 = Sim->GetDeepPlanRuns();
			int32 Thr = Thr0;
			const auto Roll = [&]() { if (Sim->GetTransitThresholds() != Thr) { Thr = Sim->GetTransitThresholds(); L.Waits.Add(Sim->GetTransitWaitSec(0)); } };
			if (bAgent)
			{
				TArray<int32> Claims = { 0, 0 };
				struct FHh { FMassEntityHandle H; bool bHas = false; FRHTask Board; float CargoKg = 0.f; int32 Wait = 0; };
				TArray<FHh> Surface;
				for (int32 k = 0; k < 2; ++k) { FHh Hh; Hh.H = FMassEntityHandle(NextHandle++, 1); Surface.Add(Hh); }
				TArray<FTransitSuiteBot> Below = { MakeBot(TEXT("RC_H"), At(-1, 2, 2)), MakeBot(TEXT("RC_H"), At(-2, -1, -1)) };
				for (FTransitSuiteBot& Bot : Below) { Bot.ChargeWh = 300.f; } // under the seek fraction: both head for the pad
				TArray<bool> Seeking = { false, false }, Docked = { false, false };
				const int32 Total = 10 * (int32)(SolLen / (double)AgentDt);
				for (int32 Step = 0; Step < Total; ++Step)
				{
					for (int32& Claim : Claims)
					{
						if (Claim != 0 && Sim->IsDepositSpent(Claim)) { Sim->ReleaseDigClaim(Claim); Claim = 0; }
						if (Claim == 0) { Claim = Sim->TryClaimDig(FVector::ZeroVector); }
						if (Claim != 0 && Sim->IsDepositWorkable(Claim)) { Sim->DigDeposit(Claim, ExcRow->WorkRate * (AgentDt / 50.f)); }
					}
					for (FHh& Hh : Surface)
					{
						if (!Hh.bHas)
						{
							if (Sim->TryClaimHaul(Hh.H, FVector::ZeroVector, Hh.Board, 0))
							{
								float Loaded = 0.f; FVector Drop = FVector::ZeroVector;
								if (Sim->HaulLoad(Hh.Board.Id, HaulRow->Cargo_kg, Loaded, Drop))
								{
									const double TripCm = FVector::DistXY(Sim->GetSiteLocation(Hh.Board.From), Drop) * 2.0;
									Hh.CargoKg = Loaded;
									Hh.Wait = (int32)(TripCm / (HaulRow->Speed_mps * 100.0 * AgentDt)) + (int32)(20.0 / AgentDt);
									Hh.bHas = true;
								}
								else { Sim->AbandonTask(Hh.Board.Id); }
							}
						}
						else if (--Hh.Wait <= 0) { Sim->HaulUnload(Hh.Board.Id, Hh.CargoKg); Hh.bHas = false; }
					}
					Sim->SetRobotStepEnd(Clock->GetSimSecondsTotal() + (double)AgentDt);
					for (int32 b = 0; b < 2; ++b)
					{
						FTransitSuiteBot& Bot = Below[b];
						if (Docked[b]) { continue; }
						if (!Seeking[b]) { Seeking[b] = Sim->DeepChargeTest(Bot.H, Bot.R, Bot.X.GetLocation(), Bot.ChargeWh, Bot.CapWh) == ERHDeepCharge::Seek; continue; }
						int32 Pid = 0; FVector Pcm;
						Sim->FindNearestChargePad(Bot.X.GetLocation(), Pid, Pcm);
						const bool bArrived = RH::MoveAlong(*Sim, Bot.H, Bot.X, Pcm, Bot.R.SpeedMps, AgentDt, false, 0.f, 0);
						Drain(Bot, Sim->DidRobotHoldThisStep(Bot.H) ? Bot.R.DrawIdleW : Bot.R.DrawMoveW);
						Docked[b] = bArrived;
					}
					Sim->SetRobotStepEnd(-1.0);
					AgentOnce();
					Roll();
				}
				for (int32 b = 0; b < 2; ++b) { L.bSurfaced &= Docked[b] && Sim->LevelOfZ(Below[b].X.GetLocation().Z) == 0; L.bNeverEmpty &= Below[b].MinChargeWh > 0.f; }
			}
			else
			{
				for (int32 Step = 0; Step < 10 * (int32)(SolLen / EraDt); ++Step) { EraOnce(); Roll(); }
			}
			L.Dug = A0 - (RegolithA() ? RegolithA()->RemainingKg : 0.0);
			L.Struct = Sim->GetTotalSolid(NStruct) - S0;
			L.Hope = Sim->GetHopeSmoothed();
			L.Rolls = Sim->GetTransitThresholds() - Thr0;
			L.Plans = Sim->GetDeepPlanRuns() - Plans0;
			L.Pop = Sim->GetPopulation();
			return L;
		};
		const FLeg Agent = RunLeg(true);
		const FLeg Era = RunLeg(false);
		const auto Within = [](double A, double B, double Frac) { return FMath::Abs(A - B) <= Frac * FMath::Max3(FMath::Abs(A), FMath::Abs(B), 1.0e-9); };
		Expect(Agent.Rolls == 10 && Era.Rolls == 10 && Agent.Waits == Era.Waits && !Agent.Waits.ContainsByPredicate([](int32 W) { return W != 0; }),
			FString::Printf(TEXT("TR17 both legs rolled 10 times and the people waits are equal at every threshold, and 0 (agent %d rolls, era %d); the robots below rode cargo-free and charged nothing"), Agent.Rolls, Era.Rolls));
		Expect(Agent.bSurfaced && Agent.bNeverEmpty && Agent.Plans > 0, FString::Printf(TEXT("TR17 (O8) the agent leg's robots below surfaced and docked, none ran empty below, and the deep planner ran (%d plans)"), Agent.Plans));
		Expect(Agent.Dug > 1000.0 && Within(Agent.Dug, Era.Dug, 0.05) && Within(Agent.Struct, Era.Struct, 0.05) && Within(Agent.Hope, Era.Hope, 0.05) && Agent.Pop == Era.Pop,
			FString::Printf(TEXT("TR17 the legs agree within 5%%: dug %.0f / %.0f kg, Struct %.0f / %.0f kg, Hope %.3f / %.3f, population %d / %d (agent / era)"), Agent.Dug, Era.Dug, Agent.Struct, Era.Struct, Agent.Hope, Era.Hope, Agent.Pop, Era.Pop));
		Sim->Debug_SetTransitCapacity(200, 1000);
		(void)ForgeId;
	}


	// --- TR18. The save: the default block, a robot saved mid-ascent, the ring, the missing row, the ledger, the +1 slot, refusals. ---
	{
		// Where the transit block starts in a file: after the header (20 bytes), the floor table, the shaft block and the tunnel block.
		const auto TransitAt = [](const TArray<uint8>& Bytes) -> int32
		{
			const auto Rd = [&Bytes](int32 Off) { int32 V = 0; if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(&V, Bytes.GetData() + Off, 4); } return V; };
			int32 Off = 20;
			const int32 Floors = Rd(Off); Off += 4;
			for (int32 F = 0; F < Floors; ++F) { const int32 NC = Rd(Off + 8); Off += 12 + NC * 8; const int32 NP = Rd(Off); Off += 4 + NP * 8; }
			const int32 Shafts = Rd(Off); Off += 4;
			for (int32 K = 0; K < Shafts; ++K) { const int32 Head = K == 0 ? 24 : 8; const int32 NQ = Rd(Off + 20 + Head); Off += 20 + Head + 4 + NQ * 8; }
			const int32 Tunnels = Rd(Off + 4);
			return Off + 8 + 36 * Tunnels;
		};
		const auto ReadFile = [&](const TCHAR* Slot, TArray<uint8>& Out) { return FFileHelper::LoadFileToArray(Out, *SaveFilePath(Slot)) && Out.Num() > 100; };
		const auto Rd = [](const TArray<uint8>& Bytes, int32 Off) { int32 V = INDEX_NONE; if (Off >= 0 && Off + 4 <= Bytes.Num()) { FMemory::Memcpy(&V, Bytes.GetData() + Off, 4); } return V; };
		const auto Put = [](TArray<uint8>& Bytes, int32 Off, int32 V) { FMemory::Memcpy(Bytes.GetData() + Off, &V, 4); };
		const auto Put64 = [](TArray<uint8>& Bytes, int32 Off, int64 V) { FMemory::Memcpy(Bytes.GetData() + Off, &V, 8); };
		// The default block: 0 cars, threshold sol -1, 0 ledgers - 12 bytes - and the version 46.
		Reset();
		Expect(Sim->SaveColony(TEXT("transitdefault"), SaveErr), FString::Printf(TEXT("TR18 the base colony saves (%s)"), *SaveErr));
		TArray<uint8> Def;
		const bool bDef = ReadFile(TEXT("transitdefault"), Def);
		uint32 Ver = 0; if (bDef) { FMemory::Memcpy(&Ver, Def.GetData() + 4, 4); }
		const int32 TA = bDef ? TransitAt(Def) : -1;
		Expect(bDef && Ver == 46 && Rd(Def, TA) == 0 && Rd(Def, TA + 4) == -1 && Rd(Def, TA + 8) == 0,
			FString::Printf(TEXT("TR18 the file says version %u; straight after the tunnel block the transit block reads 0 cars, threshold sol %d, %d ledgers: 12 bytes"), Ver, Rd(Def, TA + 4), Rd(Def, TA + 8)));

		// (i) A robot saved mid-ascent from -1 loads on its column at its boarding floor - over a live colony with no floors at all -
		// re-plans, and arrives.
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR18 mid-ride setup"));
		PadAt();
		TArray<FRHRoverState> Hs;
		Agents->CollectRobotsOfClass(FName("Hauler"), Hs);
		const FMassEntityHandle Rider = Hs.Num() > 0 ? Hs[0].Entity : FMassEntityHandle();
		FTransitSuiteBot Rb = MakeBot(TEXT("RC_H"), At(-1, 2, 2));
		Rb.H = Rider;
		int32 PadId = 0; FVector PadCm;
		Sim->FindNearestChargePad(Rb.X.GetLocation(), PadId, PadCm);
		FRHRideState Ride;
		for (int32 K = 0; K < 3000; ++K)
		{
			RH::MoveAlong(*Sim, Rider, Rb.X, PadCm, Rb.R.SpeedMps, AgentDt, false, 0.f, 0);
			if (Sim->GetRideState(Rider, Ride) && Ride.HoldStepsLeft == 0 && Ride.RideStepsLeft < Ride.RideSteps / 2) { break; }
		}
		const FVector Mid = Rb.X.GetLocation();
		Agents->SetRobotPosition(Rider, Mid);
		Expect(Ride.IsAscent() && Sim->LevelOfZ(Mid.Z) == -1 && Sim->SaveColony(TEXT("transitride"), SaveErr), FString::Printf(TEXT("TR18 saved mid-ascent: on shaft 0's column at -1, %d of %d ride sub-steps left (%s)"), Ride.RideStepsLeft, Ride.RideSteps, *SaveErr));
		Reset(); // a different live colony: no shaft, no floor
		FString Le;
		const bool bRideLoaded = Sim->LoadColony(TEXT("transitride"), Le);
		Agents->CollectRobotsOfClass(FName("Hauler"), Hs);
		FVector After = FVector::ZeroVector;
		const bool bFound = Hs.Num() > 0 && Agents->GetRobotPosition(Hs[0].Entity, After);
		Expect(bRideLoaded && Le.IsEmpty() && bFound && After == Mid, FString::Printf(TEXT("TR18 (i)/(ii) loaded over a colony with no floors, the rider is kept on its column at -1, Z unchanged (%.0f, %.0f, %.0f) - judged against the loaded floors (%s)"), After.X, After.Y, After.Z, *Le));
		FTransitSuiteBot Rb2 = MakeBot(TEXT("RC_H"), After);
		Rb2.H = Hs.Num() > 0 ? Hs[0].Entity : FMassEntityHandle();
		Sim->FindNearestChargePad(After, PadId, PadCm);
		const int32 Home = Drive(Rb2, PadCm, 0, 5000);
		Expect(Home > 140 && Sim->LevelOfZ(Rb2.X.GetLocation().Z) == 0, FString::Printf(TEXT("TR18 (i) it re-plans from the column, boards again and arrives at the pad (%d sub-steps)"), Home));
		// (iii) A robot on -2 with no lift goes to the surface deploy ring.
		Expect(OneShaft(2, 12, 12), TEXT("TR18 ring setup: no car"));
		Agents->CollectRobotsOfClass(FName("Hauler"), Hs);
		const FMassEntityHandle Lost = Hs.Num() > 1 ? Hs[1].Entity : FMassEntityHandle();
		Agents->SetRobotPosition(Lost, At(-2, 1, 0));
		Sim->SaveColony(TEXT("transitring"), SaveErr);
		Sim->LoadColony(TEXT("transitring"), Le);
		Agents->CollectRobotsOfClass(FName("Hauler"), Hs);
		FVector RingPos = FVector::ZeroVector;
		if (Hs.Num() > 1) { Agents->GetRobotPosition(Hs[1].Entity, RingPos); }
		Expect(Hs.Num() > 1 && RingPos.Z == RH::RobotZOffsetCm && Sim->LevelOfZ(RingPos.Z) == 0, FString::Printf(TEXT("TR18 (iii) a robot saved on -2 with no lift reloads on the surface deploy ring (%.0f, %.0f, %.0f)"), RingPos.X, RingPos.Y, RingPos.Z));
		// The missing row (REVISION item 9(a)): the second robot of the file names an unknown row; the third keeps its own level.
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR18 missing-row setup"));
		TArray<FRHRoverState> Es;
		Agents->CollectRobotsOfClass(FName("Hauler"), Hs);
		Agents->CollectRobotsOfClass(FName("Excavator"), Es);
		Agents->SetRobotPosition(Hs[0].Entity, At(-1, 2, 2)); // the third robot of the file (RC_E, RC_E, RC_H, ...)
		Sim->SaveColony(TEXT("transitrow"), SaveErr);
		TArray<uint8> RowBytes;
		ReadFile(TEXT("transitrow"), RowBytes);
		// FString "RC_E": int32 5, then R C _ E \0. Patch its second appearance to RC_Q.
		const uint8 Pattern[9] = { 5, 0, 0, 0, 'R', 'C', '_', 'E', 0 };
		int32 Seen = 0, Patched = INDEX_NONE;
		for (int32 i = 0; i + 9 <= RowBytes.Num(); ++i)
		{
			if (FMemory::Memcmp(RowBytes.GetData() + i, Pattern, 9) == 0 && ++Seen == 2) { Patched = i; RowBytes[i + 7] = 'Q'; break; }
		}
		FFileHelper::SaveArrayToFile(RowBytes, *SaveFilePath(TEXT("transitrow2")));
		const bool bRowLoaded = Sim->LoadColony(TEXT("transitrow2"), Le);
		Agents->CollectRobotsOfClass(FName("Hauler"), Hs);
		Agents->CollectRobotsOfClass(FName("Excavator"), Es);
		FVector Third = FVector::ZeroVector;
		if (Hs.Num() > 0) { Agents->GetRobotPosition(Hs[0].Entity, Third); }
		Expect(Patched != INDEX_NONE && bRowLoaded && Es.Num() == 1 && Third == At(-1, 2, 2),
			FString::Printf(TEXT("TR18 a file whose second robot names an unknown row loads with one excavator, and the hauler after it keeps its own floor (%.0f, %.0f, %.0f)"), Third.X, Third.Y, Third.Z));

		// The ledger, the waits and the threshold sol round-trip; a load mid-sol gives the same next roll as a run that never saved.
		Expect(OneShaft(1, 12, 0) && Fit(0), TEXT("TR18 ledger setup"));
		const int32 S = SolNow();
		Sim->Debug_ChargeTransit(0, 1500, S * SolLen + 5.0);
		Sim->Debug_ChargeTransit(0, 300, (S + 1) * SolLen + 5.0); // a +1 slot (a robots-first batch before the world rolls)
		const FString Before = LedgerDigest();
		Sim->SaveColony(TEXT("transitledger"), SaveErr);
		Sim->LoadColony(TEXT("transitledger"), Le);
		const FString Loaded = LedgerDigest();
		Expect(Le.IsEmpty() && Loaded == Before && Sim->HasTransit() && Sim->GetLiftCars(0) == 1, FString::Printf(TEXT("TR18 the ledger, waits and threshold sol round-trip byte for byte, the +1 slot included: %s (%s)"), *Loaded, *Le));
		Sim->Debug_RollTransit(S + 2);
		const FString Rolled = LedgerDigest();
		Sim->LoadColony(TEXT("transitledger"), Le); // the twin that never saved has the same state; roll it the same way
		Sim->Debug_RollTransit(S + 2);
		Expect(LedgerDigest() == Rolled && Sim->GetTransitWaitSec(0) == 60, FString::Printf(TEXT("TR18 after the load the next roll is the never-saved twin's: wait %d s (1,500 places vs 1,000: ceil(500 x 120 / 1,000) = 60)"), Sim->GetTransitWaitSec(0)));
		// Hand-made refusals, one per structural rule, each leaving the live colony untouched.
		Expect(SinkOne(1, 6) && Fit(1), TEXT("TR18 refusal setup: two fitted shafts"));
		Place(TEXT("Pylon"), FVector(-28000.0, 2000.0, 0.0), 0);
		int32 Bare = INDEX_NONE;
		Sim->Debug_SinkShaftAt(FVector(-28000.0, 2000.0, 0.0), 0, Bare, Why); // shaft 2, not bored
		Sim->SaveColony(TEXT("transitgood"), SaveErr);
		TArray<uint8> Good;
		ReadFile(TEXT("transitgood"), Good);
		const int32 G = TransitAt(Good);
		const int32 Ledger0 = G + 4 + 2 * 8 + 4 + 4; // the first ledger
		Expect(Rd(Good, G) == 2 && Rd(Good, G + 4) == 0 && Rd(Good, G + 8) == 1 && Rd(Good, G + 12) == 1 && Rd(Good, G + 16) == 1 && Rd(Good, G + 20) >= 0 && Rd(Good, G + 24) == 3,
			FString::Printf(TEXT("TR18 the good file's block: 2 fitted shafts (0: 1 car, 1: 1 car), threshold sol %d, 3 ledgers"), Rd(Good, G + 20)));
		const int32 Pop0 = Sim->GetPopulation(), Build0 = Sim->GetBuildings().Num(), Shafts0 = Sim->GetShaftCount();
		const auto TryFile = [&](const TCHAR* Slot, const TArray<uint8>& Bytes, FString& OutErr)
		{
			FFileHelper::SaveArrayToFile(Bytes, *SaveFilePath(Slot));
			OutErr.Reset();
			const bool bLoaded = Sim->LoadColony(Slot, OutErr);
			return !bLoaded && Sim->GetPopulation() == Pop0 && Sim->GetBuildings().Num() == Build0 && Sim->GetShaftCount() == Shafts0 && Sim->GetLiftCars(1) == 1;
		};
		struct FBad { const TCHAR* Slot; const TCHAR* Want; TFunction<void(TArray<uint8>&)> Patch; };
		const TArray<FBad> Bads = {
			{ TEXT("transith1"), TEXT("lift shaft count"), [&](TArray<uint8>& B) { Put(B, G, -1); } },
			{ TEXT("transith2"), TEXT("does not list"), [&](TArray<uint8>& B) { Put(B, G + 12, 7); } },
			{ TEXT("transith3"), TEXT("ascend"), [&](TArray<uint8>& B) { Put(B, G + 12, 0); } },
			{ TEXT("transith4"), TEXT("not bored"), [&](TArray<uint8>& B) { Put(B, G + 12, 2); } },
			{ TEXT("transith5"), TEXT("cars"), [&](TArray<uint8>& B) { Put(B, G + 8, 0); } },
			{ TEXT("transith6"), TEXT("cars"), [&](TArray<uint8>& B) { Put(B, G + 8, 9); } },
			{ TEXT("transith7"), TEXT("with a car"), [&](TArray<uint8>& B) { Put(B, G + 20, -1); } },
			{ TEXT("transith8"), TEXT("ledger count"), [&](TArray<uint8>& B) { Put(B, G + 24, 2); } },
			{ TEXT("transith9"), TEXT("names shaft"), [&](TArray<uint8>& B) { Put(B, Ledger0, 1); } },
			{ TEXT("transith10"), TEXT("tagged -2"), [&](TArray<uint8>& B) { Put(B, Ledger0 + 4, -2); } },
			{ TEXT("transith11"), TEXT("belongs in slot"), [&](TArray<uint8>& B) { Put(B, Ledger0 + 4, 1); } },
			{ TEXT("transith12"), TEXT("after the threshold"), [&](TArray<uint8>& B) { Put(B, Ledger0 + 4, (Rd(B, G + 20) / 3 + 2) * 3); } },
			{ TEXT("transith13"), TEXT("kg and"), [&](TArray<uint8>& B) { Put64(B, Ledger0 + 8, -5); } },
			{ TEXT("transith14"), TEXT("kg and"), [&](TArray<uint8>& B) { Put(B, Ledger0 + 16, -1); } },
			{ TEXT("transith15"), TEXT("waits"), [&](TArray<uint8>& B) { Put(B, Ledger0 + 52, 1201); } },
			{ TEXT("transith16"), TEXT("waits"), [&](TArray<uint8>& B) { Put(B, Ledger0 + 56, -1); } },
			{ TEXT("transith17"), TEXT("ledger count"), [&](TArray<uint8>& B) { B.SetNum(Ledger0 + 30); } },
		};
		for (const FBad& Bad : Bads)
		{
			TArray<uint8> Copy = Good;
			Bad.Patch(Copy);
			FString E;
			Expect(TryFile(Bad.Slot, Copy, E) && E.Contains(TEXT("transit block")) && E.Contains(Bad.Want), FString::Printf(TEXT("TR18 refused, the colony untouched: %s (%s)"), *E, Bad.Want));
		}
		// The threshold sol of a file with no car must be -1.
		{
			TArray<uint8> Copy = Def;
			Put(Copy, TA + 4, 5);
			FString E;
			FFileHelper::SaveArrayToFile(Copy, *SaveFilePath(TEXT("transith18")));
			Expect(!Sim->LoadColony(TEXT("transith18"), E) && E.Contains(TEXT("no car")), FString::Printf(TEXT("TR18 refused: %s"), *E));
		}
		// The previous version.
		{
			TArray<uint8> Old = Good;
			const uint32 V45 = 45;
			FMemory::Memcpy(Old.GetData() + 4, &V45, 4);
			FString E;
			Expect(TryFile(TEXT("transith19"), Old, E) && E.Contains(TEXT("Save format mismatch")) && E.Contains(TEXT("/46")), FString::Printf(TEXT("TR18 a v45 file is refused (%s)"), *E));
		}
		FString E;
		Expect(Sim->LoadColony(TEXT("transitgood"), E) && E.IsEmpty() && Sim->GetLiftCars(0) == 1 && Sim->GetLiftCars(1) == 1 && Sim->GetLiftCars(2) == 0 && Sim->GetTransitLedgers().Num() == 3,
			FString::Printf(TEXT("TR18 and the good file still loads: three shafts, two fitted, three ledgers (%s)"), *E));
	}

	Expect(Sim->GetDeepOffLattice() == 0, FString::Printf(TEXT("TR over the whole suite no plan started off the lattice (DeepOffLattice %d)"), Sim->GetDeepOffLattice()));
	Expect(Sim->GetTransitUnstampedRobotWrites() == 0, FString::Printf(TEXT("TR over the whole suite every robot ledger write was stamped (%d unstamped)"), Sim->GetTransitUnstampedRobotWrites()));

	return Finish();
}
