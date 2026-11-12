#include "RHSimWorldSubsystem.h"
#include "RedHopeSim.h"
#include "RHSimClockSubsystem.h"
#include "RHDefinitionsSubsystem.h"
#include "RHAgentFragments.h"
#include "Data/RHRows.h"
#include "Engine/World.h"
#include "Engine/Engine.h"

// C0a "Building that finishes" self-test (docs/design-decisions.md, "C0a, building that finishes"; the gate's brief, section 4.2). Fails
// loudly. `-building`. The battery never runs the robot brains, so the rules a brain follows live in sim functions both brains call
// (StepBuildAtSite, StepDigAtPile) and this suite calls the same functions with a harness robot (a made-up entity handle and a local
// task fragment, as -demolish and -transit do). Every check builds its own fixture on a fresh colony (the base saved at the start and
// loaded before each), so no check reads another's state; one line per check, `BUILDING ok: <id> ...` or `BUILDING FAIL: <id> ...`.
//  building.readyfirst  C0-1 a ready site outranks a nearer site that is not ready
//  building.return      C0-1 a part-built site outranks a nearer ready one (a Fabricator back from a charge)
//  building.swap        C0-1 a Fabricator standing by at a starved site moves to a ready one, at the BuildSwapCheckS look and not before
//  building.keep        C0-1 a Fabricator on a ready site keeps it to the end
//  building.idle        C0-1 standing by is not work: BuildStepDrawsWork
//  building.digskip     C0-2 a full pile is skipped by a fresh claim
//  building.pileswap    C0-2 an excavator at a full pile moves to another designated deposit, at the DigFullHoldS look and not before
//  building.digrank     C0-2 a deposit whose material the open sites are short of ranks first
//  building.stopdig     C0-2 a dig designation can be cancelled
//  building.shield      C0-3 regolith pays the surface Shielding line at ShieldingRegolithPerKg; forge-made Shielding goes first
//  building.chain       C0-4 a pylon chain ordered in one go is accepted and builds outward
//  building.drill       C0-5 an extractor must stand on a surveyed deposit of its kind
//  building.prefab      C0-10 a network made only of prefabs is livable at its 2 bays
//  building.shell       C0-11 a machine ordered into a shell still in flight waits for the shell; with no shell it is refused
//  building.stall       C0-12 a site nothing can supply does not hold 60x; the read is bounded by EraStallReadS
//  building.manifest    C0-7 the ExtraFabricator row
//  building.words       C0-6 the site line state and the 60x reason
//  building.growwhy     C0-6 the growth note names the rated habitat's reason
//  building.zone        C0-6 zoning a surface spot with no bay says so in surface words
// The suite prints no file size, no save version and no wall-clock figure.
int32 RHRunBuildingSuite(UWorld* World, URHSimClockSubsystem* Clock, URHSimWorldSubsystem* Sim)
{
	UE_LOG(LogRedHopeSim, Display, TEXT("=== BUILDING TEST (C0a, 2026-11-10) ==="));
	using EBuild = URHSimWorldSubsystem::ERHBuildStep;
	using EDig = URHSimWorldSubsystem::ERHDigStep;
	int32 Fails = 0, Checks = 0;
	// One check: every clause it names, and one line at the end. A failed line lists the clauses that failed.
	struct FCheck
	{
		bool bOk = true;
		FString Bad;
		void Need(bool bClause, const TCHAR* Name) { if (!bClause) { bOk = false; Bad += Name; Bad += TEXT("; "); } }
	};
	const auto Report = [&](const TCHAR* Id, const FCheck& C, const FString& What)
	{
		++Checks;
		if (C.bOk) { UE_LOG(LogRedHopeSim, Display, TEXT("BUILDING ok: %s %s"), Id, *What); }
		else { UE_LOG(LogRedHopeSim, Error, TEXT("BUILDING FAIL: %s %s [failed: %s]"), Id, *What, *C.Bad); ++Fails; }
	};
	URHDefinitionsSubsystem* DefsSub = World->GetSubsystem<URHDefinitionsSubsystem>();
	TArray<FString> Rejects, Alerts;
	const FDelegateHandle RejectH = Sim->OnCommandRejected.AddLambda([&Rejects](const FRHCommand&, const FString& S) { Rejects.Add(S); });
	const FDelegateHandle AlertH = Sim->OnAlert.AddLambda([&Alerts](const FString& S) { Alerts.Add(S); });
	const auto Finish = [&]() -> int32
	{
		Sim->OnCommandRejected.Remove(RejectH);
		Sim->OnAlert.Remove(AlertH);
		UE_LOG(LogRedHopeSim, Display, TEXT("=== BUILDING TEST END: %d check(s), %d failure(s) ==="), Checks, Fails);
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
		return Fails > 0 ? 1 : 0;
	};
	const FName NStruct(TEXT("Struct")), NShield(TEXT("Shielding")), NRegolith(TEXT("Regolith")), NSeg(TEXT("HabSegment")), NOxygen(TEXT("Oxygen"));
	const double SubDt = URHSimClockSubsystem::SubStepSeconds;
	const double EraDt = URHSimClockSubsystem::EraStepSimSeconds;

	// The base every fixture loads: the fresh colony with the commandlet's own Dig order taken back out of the uplink (each check
	// designates what it needs) and no order lag (an order lands on the next sub-step).
	while (Sim->GetUplinkQueue().Num() > 0)
	{
		Sim->CancelUplinkCommand(Sim->GetUplinkQueue()[0].CommandId);
	}
	Sim->SetOrderLagSeconds(0.0);
	FString SaveErr;
	if (!Sim->SaveColony(TEXT("buildingbase"), SaveErr))
	{
		UE_LOG(LogRedHopeSim, Error, TEXT("BUILDING FAIL: the fresh colony could not be saved as the base (%s)"), *SaveErr);
		++Fails;
		return Finish();
	}
	const auto Reset = [&]()
	{
		FString E;
		if (!Sim->LoadColony(TEXT("buildingbase"), E) || !E.IsEmpty())
		{
			UE_LOG(LogRedHopeSim, Error, TEXT("BUILDING FAIL: loading the base failed: %s"), *E);
			++Fails;
		}
		Sim->SetOrderLagSeconds(0.0);
		Rejects.Reset();
		Alerts.Reset();
	};
	// One agent sub-step (the body of Tick's loop), and one era step as the suites take it (water and the bank topped up).
	const auto SubSteps = [&](int32 N)
	{
		for (int32 K = 0; K < N; ++K)
		{
			Clock->Debug_AdvanceSimSeconds(SubDt);
			Sim->Debug_StepSim((float)SubDt);
		}
	};
	const auto EraStepOnce = [&]()
	{
		Sim->Debug_AddFreshWater(500.0 * EraDt / (double)URHSimClockSubsystem::SolLengthSimSeconds);
		Sim->Debug_SetBatteryWh(1.0e9);
		Clock->Debug_AdvanceSimSeconds(EraDt);
		Sim->EraStep((float)EraDt);
	};
	const auto RunSols = [&](double Sols) { for (int32 S = 0; S < (int32)(Sols * URHSimClockSubsystem::SolLengthSimSeconds / EraDt); ++S) { EraStepOnce(); } };
	const auto Find = [&](int32 Id) -> const FRHBuildingInstance*
	{
		for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.Id == Id) { return &B; } }
		return nullptr;
	};
	// Buildings stay Id-ascending: the last match is the newest.
	const auto LastIdOf = [&](const TCHAR* Def) { int32 Id = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { if (B.DefName == FName(Def)) { Id = B.Id; } } return Id; };
	const auto CountOf = [&](const TCHAR* Def) { int32 N = 0; for (const FRHBuildingInstance& B : Sim->GetBuildings()) { N += B.DefName == FName(Def) ? 1 : 0; } return N; };
	const auto Place = [&](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1)
	{
		Sim->Debug_PlaceInstant(FName(Def), FVector(X, Y, 0.0), 0, SX, SY);
		return LastIdOf(Def);
	};
	const auto Dep = [&](const TCHAR* Row) -> const FRHDepositState*
	{
		for (const FRHDepositState& D : Sim->GetDeposits()) { if (D.RowName == FName(Row)) { return &D; } }
		return nullptr;
	};
	const auto BuildCmd = [](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1)
	{
		FRHCommand C;
		C.Verb = FName("Build");
		C.Target = FName(Def);
		C.Location = FVector(X, Y, 0.0);
		C.Level = 0;
		C.Value = (SX > 1 || SY > 1) ? URHSimWorldSubsystem::EncodeBuildSize(SX, SY) : 0.0;
		return C;
	};
	// Queue a Build order and land it (one sub-step). Returns the new site's Id, 0 when it was rejected.
	const auto Order = [&](const TCHAR* Def, double X, double Y, int32 SX = 1, int32 SY = 1)
	{
		const int32 Before = LastIdOf(Def);
		Sim->EnqueueCommand(BuildCmd(Def, X, Y, SX, SY));
		SubSteps(1);
		const int32 Id = LastIdOf(Def);
		return Id != Before ? Id : 0;
	};
	const auto Give = [&](int32 BuildingId, FName Res, double Kg) { Sim->Debug_AddSolidAt(BuildingId, Res, Kg, false); };
	// Put a site's whole bill on it, in the bill's own resources.
	const auto FillBill = [&](int32 SiteId)
	{
		if (const FRHBuildingInstance* Site = Find(SiteId))
		{
			const TMap<FName, double> Bill = Sim->GetSiteBill(*Site);
			for (const auto& Line : Bill)
			{
				const double HeldKg = Find(SiteId)->InputKg.FindRef(Line.Key);
				if (HeldKg < Line.Value) { Give(SiteId, Line.Key, Line.Value - HeldKg); }
			}
		}
	};
	const auto TaskTo = [&](int32 SiteId, ERHTaskType Type, FName Res = NAME_None) -> const FRHTask*
	{
		for (const FRHTask& T : Sim->GetTasks()) { if (T.Type == Type && T.To.BuildingId == SiteId && (Res.IsNone() || T.Resource == Res)) { return &T; } }
		return nullptr;
	};
	const auto CountTasksTo = [&](int32 SiteId, ERHTaskType Type, FName Res)
	{
		int32 N = 0;
		for (const FRHTask& T : Sim->GetTasks()) { N += (T.Type == Type && T.To.BuildingId == SiteId && T.Resource == Res) ? 1 : 0; }
		return N;
	};
	// A harness Fabricator's claim, as both brains make it (the fragment set from the board's task); it stands at the site at once.
	const auto ClaimBuild = [&](const FMassEntityHandle Robot, const FVector& Pos, FRHTaskFragment& Frag) -> int32
	{
		FRHTask Board;
		if (!Sim->TryClaimBuild(Robot, Pos, Board))
		{
			return 0;
		}
		Frag = FRHTaskFragment();
		Frag.TaskType = (uint8)ERHTaskType::Build;
		Frag.TaskId = Board.Id;
		Frag.DigDepositId = Board.To.BuildingId;
		Frag.RoadEdgeId = Board.To.RoadEdgeId;
		Frag.TargetCm = Sim->GetRobotTarget(Board.To, 0, Pos, Frag.TargetLevel);
		Frag.Phase = 1;
		return Board.To.BuildingId;
	};
	// The charge path's own calls (RHRobotStateTree.cpp FRHChargeTask::EnterState, RHRobotTaskProcessor.cpp's break-off): the claim put
	// back, the fragment reset.
	const auto BreakOff = [&](FRHTaskFragment& Frag)
	{
		Sim->AbandonTask(Frag.TaskId);
		Frag = FRHTaskFragment();
	};
	// Fabricate until the step says Done (never asserts the step's name: that is building.keep's and building.idle's). Returns the
	// sub-steps taken, -1 when it did not finish, -2 when the Fabricator swapped away.
	const auto BuildOut = [&](const FMassEntityHandle Robot, const FVector& Pos, FRHTaskFragment& Frag, int32 MaxSteps) -> int32
	{
		for (int32 K = 1; K <= MaxSteps; ++K)
		{
			const EBuild S = Sim->StepBuildAtSite(Robot, Pos, 0, Frag, (float)SubDt, (float)SubDt);
			if (S == EBuild::Done) { return K; }
			if (S == EBuild::Swapped) { return -2; }
		}
		return -1;
	};
	const auto EraReason = [&]() { FString Why; return Sim->CanEnterEraMode(Why) ? FString(TEXT("(60x allowed)")) : Why; };
	const int32 LanderId = LastIdOf(TEXT("Lander"));
	const FMassEntityHandle Fab(960001, 1), Hauler(960004, 1);

	// ---------------- building.readyfirst ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		const int32 Pad = Order(TEXT("Stockpile"), 1200.0, 1200.0);   // near the Fabricator, never given its bill
		const int32 Pylon = Order(TEXT("Pylon"), -2000.0, 0.0);       // far from it
		const FVector FabPos(1200.0, 2200.0, 0.0);
		FRHTaskFragment Frag;
		const int32 Both = ClaimBuild(Fab, FabPos, Frag);
		BreakOff(Frag);
		FillBill(Pylon);
		const bool bPadReady = Find(Pad) && Sim->IsSiteReady(*Find(Pad)), bPylonReady = Find(Pylon) && Sim->IsSiteReady(*Find(Pylon));
		const int32 One = ClaimBuild(Fab, FabPos, Frag);
		C.Need(Pad != 0 && Pylon != 0, TEXT("both orders land as sites"));
		C.Need(Both == Pad, TEXT("with both not ready the nearer is claimed"));
		C.Need(!bPadReady && bPylonReady, TEXT("the Stockpile site is not ready, the Pylon site is"));
		C.Need(One == Pylon, TEXT("the ready site is claimed, though it is farther"));
		Report(TEXT("building.readyfirst"), C, FString::Printf(TEXT("a Fabricator 10 m from a Stockpile site with nothing delivered and 38 m from a Pylon site: with neither ready TryClaimBuild returns the nearer (#%d, the Stockpile #%d); with the Pylon's bill on site it returns the Pylon (#%d, the Pylon #%d)"),
			Both, Pad, One, Pylon));
	}

	// ---------------- building.return ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		const int32 B = Order(TEXT("Stockpile"), 1200.0, 1200.0);
		FillBill(B);
		FRHTaskFragment Frag;
		const int32 First = ClaimBuild(Fab, FVector(1200.0, 2200.0, 0.0), Frag);
		for (int32 K = 0; K < 300; ++K) { Sim->StepBuildAtSite(Fab, FVector(1200.0, 2200.0, 0.0), 0, Frag, (float)SubDt, (float)SubDt); }
		const double Left = Find(B) ? Find(B)->BuildRemaining_s : -1.0;
		const bool bPart = Find(B) && Sim->IsSitePartBuilt(*Find(B));
		// A second site appears, ready and untouched, nearer to where the Fabricator comes back from.
		const int32 D = Order(TEXT("Pylon"), -2000.0, 0.0);
		FillBill(D);
		const bool bDReady = Find(D) && Sim->IsSiteReady(*Find(D)) && !Sim->IsSitePartBuilt(*Find(D));
		BreakOff(Frag);
		const int32 Back = ClaimBuild(Fab, FVector(-2000.0, 1000.0, 0.0), Frag);
		const double LeftAfter = Find(B) ? Find(B)->BuildRemaining_s : -1.0;
		C.Need(First == B && B != 0 && D != 0, TEXT("the fixture: B claimed first, D lands"));
		C.Need(bPart && Left > 25.0 && Left < 35.0, TEXT("B is worked to about half"));
		C.Need(bDReady, TEXT("D is ready and untouched"));
		C.Need(Back == B, TEXT("the part-built site is claimed first"));
		C.Need(LeftAfter == Left, TEXT("B's remaining seconds are unchanged"));
		Report(TEXT("building.return"), C, FString::Printf(TEXT("a Fabricator works Stockpile #%d from 60 s to %.1f s left, breaks off as the charge path does (AbandonTask, the fragment reset), and claims again from 10 m beside a ready untouched Pylon #%d: TryClaimBuild returns #%d (part-built first), its remaining %.1f s"),
			B, Left, D, Back, LeftAfter));
	}

	// ---------------- building.swap ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		const int32 A = Order(TEXT("Stockpile"), 1200.0, 1200.0); // starved for the whole check
		const int32 B = Order(TEXT("Pylon"), -2000.0, 0.0);       // not ready until its bill is put on it below
		const FVector FabPos(1200.0, 2200.0, 0.0);
		FRHTaskFragment Frag;
		const int32 Held = ClaimBuild(Fab, FabPos, Frag);
		const int32 ATask = Frag.TaskId;
		int32 NotStandby = 0;
		for (int32 K = 1; K <= 150; ++K) { NotStandby += Sim->StepBuildAtSite(Fab, FabPos, 0, Frag, (float)SubDt, 0.f) == EBuild::StandingBy ? 0 : 1; }
		const bool bStillA = Frag.DigDepositId == A && Frag.TaskId == ATask;
		for (int32 K = 151; K <= 163; ++K) { NotStandby += Sim->StepBuildAtSite(Fab, FabPos, 0, Frag, (float)SubDt, 0.f) == EBuild::StandingBy ? 0 : 1; }
		FillBill(B); // 1.3 sim-s after the third look
		int32 SwapAt = 0, Early = 0;
		for (int32 K = 164; K <= 210 && SwapAt == 0; ++K)
		{
			const EBuild S = Sim->StepBuildAtSite(Fab, FabPos, 0, Frag, (float)SubDt, 0.f);
			if (S == EBuild::Swapped) { SwapAt = K; }
			else if (S != EBuild::StandingBy) { ++Early; }
		}
		FRHTask OldTask, NewTask;
		const bool bOld = Sim->Debug_GetTask(ATask, OldTask), bNew = Sim->Debug_GetTask(Frag.TaskId, NewTask);
		C.Need(Held == A && A != 0 && B != 0, TEXT("the fixture: the Fabricator holds the starved site"));
		C.Need(NotStandby == 0 && bStillA, TEXT("no swap in the first 15.0 sim-s: every step StandingBy"));
		C.Need(SwapAt == 200 && Early == 0, TEXT("the swap comes on the sub-step that completes the next 5.0 sim-s, not before"));
		C.Need(bOld && !OldTask.ClaimedBy.IsValid(), TEXT("the old task is unclaimed"));
		C.Need(bNew && NewTask.To.BuildingId == B && NewTask.ClaimedBy == Fab && Frag.DigDepositId == B && Frag.Phase == 0 && Frag.TaskType == (uint8)ERHTaskType::Build,
			TEXT("the fragment holds the new task at phase 0"));
		Report(TEXT("building.swap"), C, FString::Printf(TEXT("a Fabricator stands by at the starved Stockpile #%d for 150 sub-steps (3 looks, %d step(s) not StandingBy); Pylon #%d is made ready 1.3 sim-s after the third look and the swap comes at sub-step %d (expect 200, 37 after it became ready; BuildSwapCheckS %.1f); #%d's task is unclaimed and the fragment holds #%d at phase %d"),
			A, NotStandby, B, SwapAt, URHSimWorldSubsystem::BuildSwapCheckS, A, Frag.DigDepositId, (int32)Frag.Phase));
	}

	// ---------------- building.keep ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		const int32 B = Order(TEXT("Pylon"), -2000.0, 0.0);
		FillBill(B);
		const FVector FabPos(1200.0, 2200.0, 0.0);
		FRHTaskFragment Frag;
		const int32 Held = ClaimBuild(Fab, FabPos, Frag);
		for (int32 K = 0; K < 10; ++K) { Sim->StepBuildAtSite(Fab, FabPos, 0, Frag, (float)SubDt, (float)SubDt); }
		const int32 Near = Order(TEXT("Stockpile"), 1200.0, 1200.0); // ready, and nearer to the Fabricator than the site it is on
		FillBill(Near);
		const bool bNearReady = Find(Near) && Sim->IsSiteReady(*Find(Near));
		int32 NotWorking = 0, NotFalling = 0, Moved = 0;
		double Last = Find(B) ? Find(B)->BuildRemaining_s : 0.0;
		for (int32 K = 1; K <= 150; ++K)
		{
			NotWorking += Sim->StepBuildAtSite(Fab, FabPos, 0, Frag, (float)SubDt, (float)SubDt) == EBuild::Working ? 0 : 1;
			const double Now = Find(B) ? Find(B)->BuildRemaining_s : 0.0;
			NotFalling += Now < Last ? 0 : 1;
			Last = Now;
			Moved += Frag.DigDepositId == B ? 0 : 1;
		}
		int32 DoneAt = 0;
		for (int32 K = 151; K <= 1200 && DoneAt == 0; ++K)
		{
			const EBuild S = Sim->StepBuildAtSite(Fab, FabPos, 0, Frag, (float)SubDt, (float)SubDt);
			if (S == EBuild::Done) { DoneAt = K; }
			else if (S != EBuild::Working) { ++NotWorking; }
		}
		const bool bFinished = Find(B) && !Find(B)->bUnderConstruction && Frag.TaskType == (uint8)ERHTaskType::None;
		const int32 Next = ClaimBuild(Fab, FabPos, Frag);
		C.Need(Held == B && B != 0 && Near != 0 && bNearReady, TEXT("the fixture: on ready B, a nearer ready site appears"));
		C.Need(NotWorking == 0, TEXT("every step Working"));
		C.Need(NotFalling == 0, TEXT("B's remaining falls on every sub-step"));
		C.Need(Moved == 0, TEXT("the fragment never changes site"));
		C.Need(DoneAt > 0 && bFinished, TEXT("B finishes (Done)"));
		C.Need(Next == Near, TEXT("the next claim is the nearer site"));
		Report(TEXT("building.keep"), C, FString::Printf(TEXT("a Fabricator fabricating ready Pylon #%d when ready Stockpile #%d appears 10 m from it: over 3 intervals (150 sub-steps) %d step(s) not Working, %d without progress, %d off the site; the Pylon finishes at sub-step %d of its work and the next claim is #%d"),
			B, Near, NotWorking, NotFalling, Moved, DoneAt + 10, Next));
	}

	// ---------------- building.idle ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		const int32 A = Order(TEXT("Stockpile"), 1200.0, 1200.0);
		const FVector FabPos(1200.0, 2200.0, 0.0);
		FRHTaskFragment Frag;
		const int32 Held = ClaimBuild(Fab, FabPos, Frag);
		const double Left0 = Find(A) ? Find(A)->BuildRemaining_s : -1.0;
		int32 NotStandby = 0, DrewWork = 0;
		for (int32 K = 1; K <= 100; ++K)
		{
			const EBuild S = Sim->StepBuildAtSite(Fab, FabPos, 0, Frag, (float)SubDt, (float)SubDt);
			NotStandby += S == EBuild::StandingBy ? 0 : 1;
			DrewWork += URHSimWorldSubsystem::BuildStepDrawsWork(S) ? 1 : 0;
		}
		const double Left1 = Find(A) ? Find(A)->BuildRemaining_s : -2.0;
		C.Need(Held == A && A != 0, TEXT("the fixture: the Fabricator holds the starved site"));
		C.Need(NotStandby == 0, TEXT("every return is StandingBy"));
		C.Need(DrewWork == 0 && !URHSimWorldSubsystem::BuildStepDrawsWork(EBuild::StandingBy), TEXT("BuildStepDrawsWork is false for StandingBy"));
		C.Need(URHSimWorldSubsystem::BuildStepDrawsWork(EBuild::Working) && URHSimWorldSubsystem::BuildStepDrawsWork(EBuild::Done), TEXT("BuildStepDrawsWork is true for Working and Done"));
		C.Need(Left1 == Left0 && Left0 > 0.0, TEXT("BuildRemaining_s did not move"));
		Report(TEXT("building.idle"), C, FString::Printf(TEXT("100 sub-steps at the starved Stockpile #%d: %d return(s) not StandingBy, %d that draw work (BuildStepDrawsWork: StandingBy %d, Swapped %d, Working %d, Done %d), remaining %.1f -> %.1f s; the brains' two call sites (idle draw, no wear while standing by) are proved by the replays' wear and charge lines, not here"),
			A, NotStandby, DrewWork, (int32)URHSimWorldSubsystem::BuildStepDrawsWork(EBuild::StandingBy), (int32)URHSimWorldSubsystem::BuildStepDrawsWork(EBuild::Swapped),
			(int32)URHSimWorldSubsystem::BuildStepDrawsWork(EBuild::Working), (int32)URHSimWorldSubsystem::BuildStepDrawsWork(EBuild::Done), Left0, Left1));
	}

	// ---------------- building.digskip ----------------
	{
		Reset();
		FCheck C;
		const FRHDepositState* RegA = Dep(TEXT("Regolith_A"));
		const FRHDepositState* OreA = Dep(TEXT("Ore_A"));
		const int32 RegId = RegA ? RegA->Id : 0, OreId = OreA ? OreA->Id : 0;
		const FVector Beside = RegA ? RegA->LocationCm + FVector(300.0, 0.0, 0.0) : FVector::ZeroVector;
		Sim->Debug_SetDepositDesignated(RegId, true);
		Sim->Debug_SetDepositDesignated(OreId, true);
		Sim->DigDeposit(RegId, 1.0e6); // to the cap
		const double Cap = Dep(TEXT("Regolith_A")) ? Dep(TEXT("Regolith_A"))->PileKg : 0.0;
		const int32 First = Sim->TryClaimDig(Beside);
		if (First != 0) { Sim->ReleaseDigClaim(First); }
		Sim->Debug_SetDepositDesignated(OreId, false);
		const int32 OnlyFull = Sim->TryClaimDig(Beside);
		if (OnlyFull != 0) { Sim->ReleaseDigClaim(OnlyFull); }
		Sim->Debug_SetDepositPileKg(RegId, Cap - 1.0);
		const int32 Room = Sim->TryClaimDig(Beside);
		if (Room != 0) { Sim->ReleaseDigClaim(Room); }
		C.Need(RegId != 0 && OreId != 0 && Cap > 0.0 && Cap == DefsSub->GetConfigScalar(FName("PilesCapacity_kg"), -1.0), TEXT("the fixture: Regolith_A's pile at the cap"));
		C.Need(First == OreId, TEXT("the full pile is skipped: Ore_A is claimed"));
		C.Need(OnlyFull == 0, TEXT("with only the full deposit designated nothing is claimed"));
		C.Need(Room == RegId, TEXT("with 1 kg of room Regolith_A is claimed"));
		Report(TEXT("building.digskip"), C, FString::Printf(TEXT("a fresh excavator 3 m from Regolith_A (#%d) with its pile at the %.0f kg cap and Ore_A (#%d) designated with room: TryClaimDig returns #%d; with only Regolith_A designated it returns %d; with the pile at %.0f kg it returns #%d"),
			RegId, Cap, OreId, First, OnlyFull, Cap - 1.0, Room));
	}

	// ---------------- building.pileswap ----------------
	{
		Reset();
		FCheck C;
		const FRHDepositState* RegA = Dep(TEXT("Regolith_A"));
		const FRHDepositState* OreA = Dep(TEXT("Ore_A"));
		const int32 RegId = RegA ? RegA->Id : 0, OreId = OreA ? OreA->Id : 0;
		const FVector At = RegA ? RegA->LocationCm : FVector::ZeroVector;
		Sim->Debug_SetDepositDesignated(RegId, true);
		FRHTaskFragment Frag;
		Frag.TaskType = (uint8)ERHTaskType::Dig;
		Frag.DigDepositId = Sim->TryClaimDig(At);
		Frag.TargetCm = At;
		Frag.Phase = 1;
		Sim->DigDeposit(RegId, 1.0e6); // the pile fills
		const double DigKg = 0.1;      // RC_E: 50 kg a sol-hour over a 0.1 s sub-step
		int32 NotStandby = 0;
		for (int32 K = 1; K <= 300; ++K) { NotStandby += Sim->StepDigAtPile(At, 0, Frag, (float)SubDt, DigKg) == EDig::StandingBy ? 0 : 1; }
		const int32 ClaimsHeld = Dep(TEXT("Regolith_A")) ? Dep(TEXT("Regolith_A"))->DigClaims : -1;
		const bool bKept = Frag.DigDepositId == RegId && Frag.Phase == 1;
		for (int32 K = 301; K <= 313; ++K) { NotStandby += Sim->StepDigAtPile(At, 0, Frag, (float)SubDt, DigKg) == EDig::StandingBy ? 0 : 1; }
		Sim->Debug_SetDepositDesignated(OreId, true); // 1.3 sim-s after the third look
		int32 SwapAt = 0, Early = 0;
		for (int32 K = 314; K <= 420 && SwapAt == 0; ++K)
		{
			const EDig S = Sim->StepDigAtPile(At, 0, Frag, (float)SubDt, DigKg);
			if (S == EDig::Swapped) { SwapAt = K; }
			else if (S != EDig::StandingBy) { ++Early; }
		}
		const int32 RegClaims = Dep(TEXT("Regolith_A")) ? Dep(TEXT("Regolith_A"))->DigClaims : -1, OreClaims = Dep(TEXT("Ore_A")) ? Dep(TEXT("Ore_A"))->DigClaims : -1;
		C.Need(RegId != 0 && OreId != 0, TEXT("the fixture"));
		C.Need(NotStandby == 0 && ClaimsHeld == 1 && bKept, TEXT("the claim is kept for the first 30.0 sim-s (3 looks, nowhere else to dig)"));
		C.Need(SwapAt == 400 && Early == 0, TEXT("the swap comes at the next 10.0 s look and not before"));
		C.Need(RegClaims == 0 && OreClaims == 1, TEXT("the claims move: Regolith_A 1 -> 0, Ore_A 0 -> 1"));
		C.Need(Frag.DigDepositId == OreId && Frag.Phase == 0 && OreA && Frag.TargetCm.Equals(OreA->LocationCm, 1.0), TEXT("the fragment targets Ore_A at phase 0"));
		Report(TEXT("building.pileswap"), C, FString::Printf(TEXT("an excavator claimed on Regolith_A at its full pile with no other deposit designated: 300 sub-steps StandingBy (%d not), claims %d; Ore_A designated 1.3 sim-s after the third look and the swap comes at sub-step %d (expect 400; DigFullHoldS %.1f); claims Regolith_A %d, Ore_A %d; the fragment targets deposit #%d at phase %d"),
			NotStandby, ClaimsHeld, SwapAt, URHSimWorldSubsystem::DigFullHoldS, RegClaims, OreClaims, Frag.DigDepositId, (int32)Frag.Phase));
	}

	// ---------------- building.digrank ----------------
	{
		Reset();
		FCheck C;
		const FRHDepositState* RegA = Dep(TEXT("Regolith_A"));
		const FRHDepositState* OreA = Dep(TEXT("Ore_A"));
		const int32 RegId = RegA ? RegA->Id : 0, OreId = OreA ? OreA->Id : 0;
		const FVector Beside = RegA ? RegA->LocationCm + FVector(300.0, 0.0, 0.0) : FVector::ZeroVector;
		Sim->Debug_SetDepositDesignated(RegId, true);
		Sim->Debug_SetDepositDesignated(OreId, true);
		Place(TEXT("Forge"), -2000.0, -2000.0);
		Sim->Debug_StepTaskBoard();
		const int32 Before = Sim->TryClaimDig(Beside);
		if (Before != 0) { Sim->ReleaseDigClaim(Before); }
		const int32 Hab = Order(TEXT("Habitat"), -1000.0, 1500.0); // the prefab: 2 Habitat Segments, pressed from Struct and Shielding, Shielding from Struct and Ore
		Sim->Debug_StepTaskBoard();
		const double OreHeld = Sim->GetTotalSolid(FName("Ore")) + Sim->GetStock(FName("Ore")), ShieldHeld = Sim->GetTotalSolid(NShield) + Sim->GetStock(NShield);
		const int32 After = Sim->TryClaimDig(Beside);
		if (After != 0) { Sim->ReleaseDigClaim(After); }
		C.Need(RegId != 0 && OreId != 0 && Hab != 0 && OreHeld == 0.0 && ShieldHeld == 0.0, TEXT("the fixture: a Forge, a prefab Habitat site, no Shielding and no Ore held"));
		C.Need(Before == RegId, TEXT("before the site existed the nearer Regolith_A was claimed"));
		C.Need(After == OreId, TEXT("with the site short of segments Ore_A ranks first"));
		Report(TEXT("building.digrank"), C, FString::Printf(TEXT("an excavator 3 m from Regolith_A (#%d), Ore_A (#%d) 216 m away, both designated with room: with nothing short TryClaimDig returns #%d; with a prefab Habitat site #%d waiting for segments (no Shielding, no Ore: Ore is in the shortage map) it returns #%d"),
			RegId, OreId, Before, Hab, After));
	}

	// ---------------- building.stopdig ----------------
	{
		Reset();
		FCheck C;
		const FRHDepositState* RegA = Dep(TEXT("Regolith_A"));
		const int32 RegId = RegA ? RegA->Id : 0;
		const FVector At = RegA ? RegA->LocationCm : FVector::ZeroVector;
		Sim->Debug_SetDepositDesignated(RegId, true);
		FRHTaskFragment Frag;
		Frag.TaskType = (uint8)ERHTaskType::Dig;
		Frag.DigDepositId = Sim->TryClaimDig(At);
		Frag.TargetCm = At;
		Frag.Phase = 1;
		Sim->Debug_SetDepositPileKg(RegId, 120.0);
		FRHCommand Stop;
		Stop.Verb = FName("StopDig");
		Stop.Target = FName("Regolith_A");
		Sim->EnqueueCommand(Stop);
		SubSteps(1);
		const bool bDesignated = Dep(TEXT("Regolith_A")) && Dep(TEXT("Regolith_A"))->bDesignated;
		const int32 ClaimsBefore = Dep(TEXT("Regolith_A")) ? Dep(TEXT("Regolith_A"))->DigClaims : -1;
		const EDig S = Sim->StepDigAtPile(At, 0, Frag, (float)SubDt, 0.1);
		const int32 ClaimsAfter = Dep(TEXT("Regolith_A")) ? Dep(TEXT("Regolith_A"))->DigClaims : -1;
		Sim->Debug_StepTaskBoard();
		const double Pile = Dep(TEXT("Regolith_A")) ? Dep(TEXT("Regolith_A"))->PileKg : -1.0;
		const int32 HaulDest = Sim->Debug_GetOpenHaulDestFromDeposit(RegId);
		const int32 R0 = Rejects.Num();
		Sim->EnqueueCommand(Stop);
		SubSteps(1);
		const FString Second = Rejects.Num() > R0 ? Rejects.Last() : FString();
		FRHCommand Unknown = Stop;
		Unknown.Target = FName("Nowhere_Z");
		Sim->EnqueueCommand(Unknown);
		SubSteps(1);
		const FString Third = Rejects.Num() > R0 + 1 ? Rejects.Last() : FString();
		C.Need(RegId != 0 && Frag.TaskType == (uint8)ERHTaskType::None && R0 == 0, TEXT("the fixture: one claim, the first StopDig accepted"));
		C.Need(!bDesignated, TEXT("the deposit is no longer designated"));
		C.Need(ClaimsBefore == 1 && S == EDig::Released && ClaimsAfter == 0, TEXT("the claimant's next step returns Released and the claims read 0"));
		C.Need(Pile == 120.0 && HaulDest != 0, TEXT("the pile still holds 120 kg and the board still posts its haul"));
		C.Need(Second == TEXT("That deposit is not being dug"), TEXT("a second StopDig is rejected: That deposit is not being dug"));
		C.Need(Third.StartsWith(TEXT("Unknown deposit")), TEXT("an unknown name is rejected: Unknown deposit"));
		Report(TEXT("building.stopdig"), C, FString::Printf(TEXT("Regolith_A designated with one claim and 120 kg on its pile; a StopDig order lands: designated %d, the claimant's next StepDigAtPile %s (claims %d -> %d), the pile %.0f kg with its haul posted to building #%d; a second StopDig is rejected '%s', an unknown name '%s'"),
			(int32)bDesignated, S == EDig::Released ? TEXT("Released") : TEXT("not Released"), ClaimsBefore, ClaimsAfter, Pile, HaulDest, *Second, *Third));
	}

	// ---------------- building.shield ----------------
	{
		Reset();
		FCheck C;
		const FRHDepositState* RegA = Dep(TEXT("Regolith_A"));
		const int32 RegId = RegA ? RegA->Id : 0;
		Give(LanderId, NStruct, 5000.0);
		const int32 Forge = Place(TEXT("Forge"), -2000.0, -2500.0);
		Give(Forge, NRegolith, 60.0); // under one batch (100): the Forge cannot start, so only this suite's rule could move it
		const double Row = DefsSub->GetConfigTableScalar(FName("ShieldingRegolithPerKg"), -1.0);
		const double HeaderDefault = GetDefault<URHSimWorldSubsystem>()->GetShieldingRegolithPerKg();
		const int32 S1 = Order(TEXT("HabModule"), 2000.0, 1000.0); // 1 x 1: Struct 200, Shielding 50 (the Forge stands, so the order is accepted)
		Give(S1, NStruct, 200.0);
		Sim->Debug_StepTaskBoard();
		const int32 HopperOnly = CountTasksTo(S1, ERHTaskType::Haul, NRegolith); // the only Regolith is in the Forge's hopper: nothing to pack
		Sim->Debug_SetDepositPileKg(RegId, 500.0);
		Sim->Debug_StepTaskBoard();
		Sim->Debug_StepTaskBoard();
		const int32 Posted = CountTasksTo(S1, ERHTaskType::Haul, NRegolith);
		const FRHTask* Leg = TaskTo(S1, ERHTaskType::Haul, NRegolith);
		const double LegKg = Leg ? Leg->AmountKg : -1.0;
		const int32 LegFrom = Leg ? Leg->From.DepositId : 0, LegId = Leg ? Leg->Id : 0;
		float Loaded = 0.f; FVector Drop;
		const bool bLoad = LegId != 0 && Sim->HaulLoad(LegId, 200.f, Loaded, Drop);
		Sim->HaulUnload(LegId, Loaded);
		const double Paid1 = Find(S1) ? Sim->GetSiteLinePaidKg(*Find(S1), NShield) : -1.0;
		const bool bReady1 = Find(S1) && Sim->IsSiteReady(*Find(S1));
		const double Reg1 = Find(S1) ? Find(S1)->InputKg.FindRef(NRegolith) : -1.0, Sh1 = Find(S1) ? Find(S1)->InputKg.FindRef(NShield) : -1.0;
		FRHTaskFragment Frag;
		const int32 Claim1 = ClaimBuild(Fab, FVector(2000.0, 2000.0, 0.0), Frag);
		const int32 Took1 = BuildOut(Fab, FVector(2000.0, 2000.0, 0.0), Frag, 4000);
		const bool bDone1 = Find(S1) && !Find(S1)->bUnderConstruction;
		const double RegUsed1 = Reg1 - (Find(S1) ? Find(S1)->InputKg.FindRef(NRegolith) : 0.0), ShUsed1 = Sh1 - (Find(S1) ? Find(S1)->InputKg.FindRef(NShield) : 0.0);
		// The second site: 20 kg of forge-made Shielding is in the Lander.
		Give(LanderId, NShield, 20.0);
		const int32 S2 = Order(TEXT("HabModule"), 2000.0, -1000.0);
		Give(S2, NStruct, 200.0);
		Sim->Debug_StepTaskBoard();
		const int32 ShieldLegs = CountTasksTo(S2, ERHTaskType::Haul, NShield), RegLegsEarly = CountTasksTo(S2, ERHTaskType::Haul, NRegolith);
		const FRHTask* ShLeg = TaskTo(S2, ERHTaskType::Haul, NShield);
		const int32 ShLegId = ShLeg ? ShLeg->Id : 0;
		Loaded = 0.f;
		const bool bLoadSh = ShLegId != 0 && Sim->HaulLoad(ShLegId, 200.f, Loaded, Drop);
		const double ShLoaded = Loaded;
		Sim->HaulUnload(ShLegId, Loaded);
		Sim->Debug_StepTaskBoard();
		const FRHTask* Leg2 = TaskTo(S2, ERHTaskType::Haul, NRegolith);
		const double Leg2Kg = Leg2 ? Leg2->AmountKg : -1.0;
		const int32 Leg2Id = Leg2 ? Leg2->Id : 0;
		Loaded = 0.f;
		const bool bLoad2 = Leg2Id != 0 && Sim->HaulLoad(Leg2Id, 200.f, Loaded, Drop);
		Sim->HaulUnload(Leg2Id, Loaded);
		const double Reg2 = Find(S2) ? Find(S2)->InputKg.FindRef(NRegolith) : -1.0, Sh2 = Find(S2) ? Find(S2)->InputKg.FindRef(NShield) : -1.0;
		const bool bReady2 = Find(S2) && Sim->IsSiteReady(*Find(S2));
		const int32 Claim2 = ClaimBuild(Fab, FVector(2000.0, -2000.0, 0.0), Frag);
		const int32 Took2 = BuildOut(Fab, FVector(2000.0, -2000.0, 0.0), Frag, 4000);
		const bool bDone2 = Find(S2) && !Find(S2)->bUnderConstruction;
		const double RegUsed2 = Reg2 - (Find(S2) ? Find(S2)->InputKg.FindRef(NRegolith) : 0.0), ShUsed2 = Sh2 - (Find(S2) ? Find(S2)->InputKg.FindRef(NShield) : 0.0);
		const double Hopper = Find(Forge) ? Find(Forge)->InputKg.FindRef(NRegolith) : -1.0;
		C.Need(Row == 2.0 && HeaderDefault == Row && Sim->GetShieldingRegolithPerKg() == Row, TEXT("the config row reads 2.0 and equals the header default"));
		C.Need(S1 != 0 && S2 != 0 && RegId != 0, TEXT("the fixture: both Habitat Bay orders land"));
		C.Need(HopperOnly == 0, TEXT("no haul while the only Regolith is in the Forge's hopper"));
		C.Need(Posted == 1 && LegKg == 100.0 && LegFrom == RegId, TEXT("one haul of 100 kg Regolith is posted from the pile, and no second one while it is open"));
		C.Need(bLoad && Paid1 == 50.0 && bReady1 && Reg1 == 100.0 && Sh1 == 0.0, TEXT("delivered, the line reads 50.0 paid and the site is ready"));
		C.Need(Claim1 == S1 && Took1 > 0 && bDone1 && RegUsed1 == 100.0 && ShUsed1 == 0.0, TEXT("the site finishes using 100.0 Regolith and 0 Shielding"));
		C.Need(ShieldLegs == 1 && RegLegsEarly == 0, TEXT("the second site gets a Shielding haul first and no Regolith haul while Shielding is in store"));
		C.Need(bLoadSh && ShLoaded == 20.0 && Leg2Kg == 60.0 && bLoad2, TEXT("then 60 kg Regolith for the 30 kg left"));
		C.Need(bReady2 && Claim2 == S2 && Took2 > 0 && bDone2 && ShUsed2 == 20.0 && RegUsed2 == 60.0, TEXT("at completion the second site uses 20 Shielding and 60 Regolith"));
		C.Need(Hopper == 60.0, TEXT("the Forge's hopper regolith did not move"));
		Report(TEXT("building.shield"), C, FString::Printf(TEXT("ShieldingRegolithPerKg %.1f (header default %.1f). Habitat Bay #%d with its Struct on site and no Shielding anywhere: %d Regolith haul(s) while only the Forge's hopper holds any; with 500 kg on Regolith_A's pile %d haul of %.0f kg from deposit #%d; delivered, the Shielding line reads %.1f of 50 paid and the site finishes using %.1f kg Regolith and %.1f kg Shielding. Habitat Bay #%d with 20 kg Shielding in the Lander: %d Shielding haul and %d Regolith haul(s) while it is in store, then a %.0f kg Regolith haul; it finishes using %.1f kg Shielding and %.1f kg Regolith; the Forge's hopper holds %.0f kg Regolith as at the start"),
			Row, HeaderDefault, S1, HopperOnly, Posted, LegKg, LegFrom, Paid1, RegUsed1, ShUsed1, S2, ShieldLegs, RegLegsEarly, Leg2Kg, ShUsed2, RegUsed2, Hopper));
	}

	// ---------------- building.chain ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		const FVector NearAt(7000.0, 0.0, 0.0), FarAt(14000.0, 0.0, 0.0);
		FString WhyNear, WhyFar, WhyPad;
		const bool bPreNear = Sim->CanPlaceBuilding(FName("Pylon"), NearAt, WhyNear);
		Sim->EnqueueCommand(BuildCmd(TEXT("Pylon"), NearAt.X, NearAt.Y));
		const bool bPreFar = Sim->CanPlaceBuilding(FName("Pylon"), FarAt, WhyFar); // the first is still in the queue
		Sim->EnqueueCommand(BuildCmd(TEXT("Pylon"), FarAt.X, FarAt.Y));
		SubSteps(1);
		int32 NearId = 0, FarId = 0;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			if (B.DefName == FName("Pylon") && B.bUnderConstruction) { (B.LocationCm.X < 10000.0 ? NearId : FarId) = B.Id; }
		}
		FillBill(NearId);
		FillBill(FarId);
		const bool bFarWaits = Find(FarId) && Sim->IsSiteWaitingForLink(*Find(FarId)) && !Sim->IsSiteReady(*Find(FarId));
		const bool bNearReady = Find(NearId) && !Sim->IsSiteWaitingForLink(*Find(NearId)) && Sim->IsSiteReady(*Find(NearId));
		// A site waiting for its link takes no fabrication, whoever asks.
		const double FarLeft0 = Find(FarId) ? Find(FarId)->BuildRemaining_s : -1.0;
		const bool bFarRefusesWork = !Sim->ApplyBuildWork(FarId, 1.0) && Find(FarId) && Find(FarId)->BuildRemaining_s == FarLeft0;
		const FVector FabPos(7000.0, 500.0, 0.0); // beside the near site
		FRHTaskFragment Frag;
		const int32 First = ClaimBuild(Fab, FabPos, Frag);
		const bool bPadRefused = !Sim->CanPlaceBuilding(FName("Stockpile"), FVector(9500.0, 0.0, 0.0), WhyPad); // inside the near SITE's ring, outside finished coverage
		const int32 TookNear = BuildOut(Fab, FabPos, Frag, 1200);
		const bool bNearDone = Find(NearId) && !Find(NearId)->bUnderConstruction, bFarStillSite = Find(FarId) && Find(FarId)->bUnderConstruction;
		const bool bFarReadyNow = Find(FarId) && Sim->IsSiteReady(*Find(FarId));
		const int32 Second = ClaimBuild(Fab, FabPos, Frag);
		const int32 TookFar = BuildOut(Fab, FabPos, Frag, 1200);
		const bool bFarDone = Find(FarId) && !Find(FarId)->bUnderConstruction;
		const int32 ChainRejects = Rejects.Num();
		// The control: only the far one.
		Reset();
		Give(LanderId, NStruct, 5000.0);
		FString WhyAlone;
		const bool bPreAlone = Sim->CanPlaceBuilding(FName("Pylon"), FarAt, WhyAlone);
		const int32 Alone = Order(TEXT("Pylon"), FarAt.X, FarAt.Y);
		const FString AloneReject = Rejects.Num() > 0 ? Rejects.Last() : FString();
		C.Need(bPreNear && bPreFar, TEXT("both preflights pass with the first still in the queue"));
		C.Need(NearId != 0 && FarId != 0 && ChainRejects == 0, TEXT("both land as sites, no rejection"));
		C.Need(bFarWaits && bFarRefusesWork && bNearReady && First == NearId, TEXT("the far site waits for its link (no fabrication) and the near one is claimed first"));
		C.Need(bPadRefused && WhyPad.Contains(TEXT("Outside grid coverage")), TEXT("a Stockpile in the near site's ring is refused: Outside grid coverage"));
		C.Need(TookNear > 0 && bNearDone && bFarStillSite && bFarReadyNow && Second == FarId && TookFar > 0 && bFarDone, TEXT("both finish, near first"));
		C.Need(!bPreAlone && Alone == 0 && AloneReject == TEXT("No grid node within 80 m link range") && WhyAlone == AloneReject, TEXT("the control is refused: No grid node within 80 m link range"));
		Report(TEXT("building.chain"), C, FString::Printf(TEXT("two Pylon orders queued in one step, 70 m and 140 m out on one bearing: preflights %d and %d, sites #%d and #%d, %d rejection(s); with both bills on site the far one waits for its link (%d) and a Fabricator is given #%d; a Stockpile 25 m past the near site is refused '%s'; the near one finishes in %d sub-steps, then the far one is ready and finishes in %d; with only the far one ordered it is refused '%s'"),
			(int32)bPreNear, (int32)bPreFar, NearId, FarId, ChainRejects, (int32)bFarWaits, First, *WhyPad, TookNear, TookFar, *AloneReject));
	}

	// ---------------- building.drill ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		const FRHDepositState* RegA = Dep(TEXT("Regolith_A"));
		const FRHDepositState* IceA = Dep(TEXT("Ice_A"));
		const FRHDepositState* IceB = Dep(TEXT("Ice_B"));
		const FVector Bare(1500.0, 1500.0, 0.0);
		const FVector BesideRegolith = RegA ? RegA->LocationCm + FVector(-200.0, 700.0, 0.0) : FVector::ZeroVector; // 7.3 m from Regolith_A, inside the Lander's ring
		const FVector OnIceA = IceA ? IceA->LocationCm : FVector::ZeroVector, OnIceB = IceB ? IceB->LocationCm : FVector::ZeroVector;
		Sim->Debug_RevealDeposit(FName("Ice_A"));
		Place(TEXT("Pylon"), OnIceA.X - 1000.0, OnIceA.Y - 2000.0); // covers Ice_A
		Place(TEXT("Pylon"), OnIceB.X, OnIceB.Y - 2000.0);          // covers Ice_B, which stays unrevealed
		const bool bIceBHidden = IceB && !Dep(TEXT("Ice_B"))->bDiscovered;
		FString WhyBare, WhyReg, WhyA, WhyB;
		const bool bBare = Sim->CanPlaceBuilding(FName("IceDrill"), Bare, WhyBare);
		const bool bReg = Sim->CanPlaceBuilding(FName("IceDrill"), BesideRegolith, WhyReg);
		const bool bA = Sim->CanPlaceBuilding(FName("IceDrill"), OnIceA, WhyA);
		const bool bB = Sim->CanPlaceBuilding(FName("IceDrill"), OnIceB, WhyB);
		const int32 OnIce = Order(TEXT("IceDrill"), OnIceA.X, OnIceA.Y);
		const int32 Attached = Find(OnIce) ? Find(OnIce)->AttachedDepositId : 0;
		const int32 OffIce = Place(TEXT("IceDrill"), Bare.X, Bare.Y);
		const int32 OffAttached = Find(OffIce) ? Find(OffIce)->AttachedDepositId : -1;
		const TCHAR* Words = TEXT("An Ice Drill must stand on a surveyed ice deposit");
		C.Need(RegA && IceA && IceB && bIceBHidden && FVector::DistXY(BesideRegolith, RegA->LocationCm) <= 1200.0, TEXT("the fixture: four covered spots, Ice_B unrevealed"));
		C.Need(!bBare && WhyBare == Words, TEXT("refused on covered bare ground"));
		C.Need(!bReg && WhyReg == Words, TEXT("refused beside regolith (the kind is tested)"));
		C.Need(!bB && WhyB == Words, TEXT("refused over the unrevealed ice, in the same words"));
		C.Need(bA && OnIce != 0 && IceA && Attached == IceA->Id, TEXT("accepted on Ice_A and attached to it"));
		C.Need(OffIce != 0 && OffIce != OnIce && OffAttached == 0 && Sim->IsExtractorOffDeposit(OffIce) && !Sim->IsExtractorOffDeposit(OnIce) && !Sim->IsExtractorOffDeposit(LanderId),
			TEXT("a debug-placed drill off the ice is unattached and IsExtractorOffDeposit is true for it only"));
		Report(TEXT("building.drill"), C, FString::Printf(TEXT("an Ice Drill on covered bare ground: %d '%s'; 7.3 m from Regolith_A: %d '%s'; over unrevealed Ice_B: %d '%s'; on revealed Ice_A: %d, site #%d attached to deposit #%d; a drill placed by the debug path off the ice is #%d, attached to %d, off its deposit %d (the one on Ice_A %d)"),
			(int32)bBare, *WhyBare, (int32)bReg, *WhyReg, (int32)bB, *WhyB, (int32)bA, OnIce, Attached, OffIce, OffAttached, (int32)Sim->IsExtractorOffDeposit(OffIce), (int32)Sim->IsExtractorOffDeposit(OnIce)));
	}

	// ---------------- building.prefab ----------------
	{
		FCheck C;
		// (a) a lone prefab with a filter and oxygen.
		Reset();
		Sim->AddStock(NOxygen, 3000.0);
		const int32 Hab = Place(TEXT("Habitat"), -1000.0, 1500.0);
		Place(TEXT("AirFilter"), -1000.0, 1000.0);
		RunSols(1.0);
		const bool bRated = Sim->IsHabRated(Hab);
		const int32 Bays = Sim->GetHabNetworkBays(Hab), MinBays = Sim->GetHabNetMinBays(Hab);
		const int32 CapBefore = Sim->GetHousingCapacity();
		Sim->Debug_SetVaultRated(true);
		const int32 CapAfter = Sim->GetHousingCapacity(), Surface = Sim->GetSurfaceBeds(), Vault = Sim->GetVaultBeds();
		const int32 Housed = Sim->Debug_AddColonists(2), Residents = Sim->GetHabResidents(Hab);
		// (b) a lone 1 x 2 Habitat Bay with the same.
		Reset();
		Sim->AddStock(NOxygen, 3000.0);
		const int32 Mod = Place(TEXT("HabModule"), -2000.0, 1500.0, 1, 2);
		Place(TEXT("AirFilter"), -2000.0, 1000.0);
		RunSols(1.0);
		const bool bModRated = Sim->IsHabRated(Mod), bModSmall = Sim->IsHabNetworkSealedButSmall(Mod);
		const int32 ModMin = Sim->GetHabNetMinBays(Mod);
		// (c) a prefab joined to a 1 x 1 Habitat Bay: a mixed network of 3 bays.
		Reset();
		Sim->AddStock(NOxygen, 3000.0);
		const int32 Hab2 = Place(TEXT("Habitat"), -1000.0, 1500.0);
		const int32 Mod2 = Place(TEXT("HabModule"), -2000.0, 1000.0, 1, 1);
		Place(TEXT("AirFilter"), -2000.0, 1000.0);
		RunSols(1.0);
		const bool bMixedRated = Sim->IsHabRated(Hab2);
		const int32 MixedBays = Sim->GetHabNetworkBays(Hab2), MixedMin = Sim->GetHabNetMinBays(Hab2);
		const bool bJoined = Sim->GetHabNetworkRoot(Hab2) == Sim->GetHabNetworkRoot(Mod2);
		C.Need(Hab != 0 && bRated && Bays == 2 && MinBays == 2, TEXT("the lone prefab is rated at 2 bays"));
		C.Need(CapBefore == 0, TEXT("before the vault flag the housing capacity is 0"));
		C.Need(CapAfter == Surface + Vault && Surface == 2 && Vault == 0, TEXT("after it the capacity equals GetSurfaceBeds() (2) plus GetVaultBeds() (0)"));
		C.Need(Housed == 2 && Residents == 2, TEXT("two colonists move in"));
		C.Need(Mod != 0 && !bModRated && bModSmall && ModMin == 4, TEXT("the lone Habitat Bay pair is sealed but small"));
		C.Need(Hab2 != 0 && Mod2 != 0 && bJoined && MixedBays == 3 && MixedMin == 4 && !bMixedRated, TEXT("the mixed 3-bay network is not rated"));
		Report(TEXT("building.prefab"), C, FString::Printf(TEXT("a lone prefab Habitat with an Air Filtration Station and oxygen, one sol: rated %d at %d bays (minimum %d); housing capacity %d before the vault flag, %d after it (surface %d + vault %d), %d colonists housed, %d living in it. A lone 1 x 2 Habitat Bay with the same: rated %d, sealed but small %d (minimum %d). A prefab joined to a 1 x 1 Habitat Bay: %d bays, minimum %d, rated %d"),
			(int32)bRated, Bays, MinBays, CapBefore, CapAfter, Surface, Vault, Housed, Residents, (int32)bModRated, (int32)bModSmall, ModMin, MixedBays, MixedMin, (int32)bMixedRated));
	}

	// ---------------- building.shell ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		Give(LanderId, NShield, 500.0);
		// The first pair: a 1 x 1 Habitat Bay and an Air Filtration Station inside its bay, queued in one step.
		const FVector ShellAt(2000.0, 1000.0, 0.0);
		FString WhyShell, WhyFilter;
		const bool bPreShell = Sim->CanPlaceBuildingSized(FName("HabModule"), ShellAt, WhyShell, 0, 1, 1);
		Sim->EnqueueCommand(BuildCmd(TEXT("HabModule"), ShellAt.X, ShellAt.Y));
		const bool bPreFilter = Sim->CanPlaceBuilding(FName("AirFilter"), ShellAt, WhyFilter); // the shell is still in the queue
		Sim->EnqueueCommand(BuildCmd(TEXT("AirFilter"), ShellAt.X, ShellAt.Y));
		const bool bMarked = Sim->GetUplinkQueue().Num() == 2 && Sim->GetUplinkQueue()[1].Text == TEXT("IntoShell") && Sim->GetUplinkQueue()[0].Text.IsEmpty();
		SubSteps(1);
		const int32 Shell = LastIdOf(TEXT("HabModule")), Filter = LastIdOf(TEXT("AirFilter"));
		const int32 PairRejects = Rejects.Num();
		FillBill(Shell);
		FillBill(Filter);
		const bool bWaits = Find(Filter) && Sim->IsSiteWaitingForShell(*Find(Filter)) && !Sim->IsSiteReady(*Find(Filter));
		FName Blocker;
		const bool bBillOn = Find(Filter) && Sim->GetSiteBlockerLine(*Find(Filter), Blocker) == INDEX_NONE;
		FRHTaskFragment Frag;
		const int32 First = ClaimBuild(Fab, ShellAt, Frag); // standing on the filter's spot
		const int32 TookShell = BuildOut(Fab, ShellAt, Frag, 4000);
		const bool bShellDone = Find(Shell) && !Find(Shell)->bUnderConstruction;
		const bool bFilterReady = Find(Filter) && Sim->IsSiteReady(*Find(Filter));
		const int32 Second = ClaimBuild(Fab, ShellAt, Frag);
		const int32 TookFilter = BuildOut(Fab, ShellAt, Frag, 4000);
		int32 InHab = 0, InBay = INDEX_NONE;
		const bool bInside = Find(Filter) && !Find(Filter)->bUnderConstruction && Sim->FindBayAt(Find(Filter)->LocationCm, InHab, InBay) && InHab == Shell;
		// The second pair: its shell meets a Stockpile placed on its spot before it lands.
		const FVector Shell2At(-2000.0, 1000.0, 0.0), Filter2At(-2200.0, 1000.0, 0.0);
		FString WhyShell2, WhyFilter2;
		const bool bPreShell2 = Sim->CanPlaceBuildingSized(FName("HabModule"), Shell2At, WhyShell2, 0, 1, 1);
		Sim->EnqueueCommand(BuildCmd(TEXT("HabModule"), Shell2At.X, Shell2At.Y));
		const bool bPreFilter2 = Sim->CanPlaceBuilding(FName("AirFilter"), Filter2At, WhyFilter2);
		Sim->EnqueueCommand(BuildCmd(TEXT("AirFilter"), Filter2At.X, Filter2At.Y));
		const int32 Pad = Place(TEXT("Stockpile"), -1300.0, 1000.0); // overlaps the shell's footprint, clear of the filter's
		const int32 Shells0 = CountOf(TEXT("HabModule")), Filters0 = CountOf(TEXT("AirFilter"));
		SubSteps(1);
		const FString ShellReject = Rejects.Num() >= PairRejects + 2 ? Rejects[PairRejects] : FString();
		const FString FilterReject = Rejects.Num() >= PairRejects + 2 ? Rejects[PairRejects + 1] : FString();
		const FString WantFilterReject = FString::Printf(TEXT("No habitat shell here: its Habitat Bay order was refused (%s)"), *ShellReject);
		const bool bNothingBuilt = CountOf(TEXT("HabModule")) == Shells0 && CountOf(TEXT("AirFilter")) == Filters0;
		// On open covered ground, with no shell ordered: legal as before (-surfacehab S3f).
		FString WhyOpen;
		const FVector OpenAt(0.0, -2000.0, 0.0);
		const bool bPreOpen = Sim->CanPlaceBuilding(FName("AirFilter"), OpenAt, WhyOpen);
		const int32 Open = Order(TEXT("AirFilter"), OpenAt.X, OpenAt.Y);
		FillBill(Open);
		const bool bOpenReady = Find(Open) && Sim->IsSiteReady(*Find(Open)) && !Sim->IsSiteWaitingForShell(*Find(Open));
		const int32 Third = ClaimBuild(Fab, OpenAt, Frag);
		const int32 TookOpen = BuildOut(Fab, OpenAt, Frag, 4000);
		const bool bOpenDone = Find(Open) && !Find(Open)->bUnderConstruction;
		C.Need(bPreShell && bPreFilter && bMarked, TEXT("both preflights pass, and the machine's order carries the into-shell marker"));
		C.Need(Shell != 0 && Filter != 0 && PairRejects == 0, TEXT("both land, no rejection"));
		C.Need(bWaits && bBillOn && First == Shell, TEXT("the filter's site is not ready while the shell is a site, with its bill on it"));
		C.Need(TookShell > 0 && bShellDone && bFilterReady && Second == Filter && TookFilter > 0 && bInside, TEXT("the shell finished, the filter becomes ready and is built inside the bay"));
		C.Need(bPreShell2 && bPreFilter2 && Pad != 0, TEXT("the second pair's preflights pass"));
		C.Need(ShellReject.StartsWith(TEXT("Footprint overlaps Stockpile")), TEXT("the second shell is refused for the overlap"));
		C.Need(FilterReject == WantFilterReject && bNothingBuilt, TEXT("the filter is refused 'No habitat shell here: its Habitat Bay order was refused (...)' and no Air Filter exists there"));
		C.Need(bPreOpen && Open != 0 && bOpenReady && Third == Open && TookOpen > 0 && bOpenDone, TEXT("the open-ground filter is accepted and built"));
		Report(TEXT("building.shell"), C, FString::Printf(TEXT("a 1 x 1 Habitat Bay and an Air Filtration Station inside its bay queued in one step: preflights %d and %d, the second order marked %d, sites #%d and #%d, %d rejection(s); the filter waits for its shell (%d) with its bill on site (%d) and a Fabricator standing on it is given #%d; the shell finishes in %d sub-steps, then the filter is ready and is built in %d, inside habitat #%d bay %d. A second pair whose shell meets a Stockpile: the shell '%s', the filter '%s', nothing built %d. An Air Filtration Station ordered on open covered ground: accepted %d, built in %d sub-steps"),
			(int32)bPreShell, (int32)bPreFilter, (int32)bMarked, Shell, Filter, PairRejects, (int32)bWaits, (int32)bBillOn, First, TookShell, TookFilter, InHab, InBay, *ShellReject, *FilterReject, (int32)bNothingBuilt, (int32)bPreOpen, TookOpen));
	}

	// ---------------- building.stall ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		const int32 Forge = Place(TEXT("Forge"), -2000.0, -2500.0);
		const int32 Hab = Order(TEXT("Habitat"), -1000.0, 1500.0);
		SubSteps(1);
		const FString WhyForge = EraReason();
		const bool bStalledEarly = Sim->IsSiteStalled(Hab);
		FRHCommand Demo;
		Demo.Verb = FName("Demolish");
		Demo.Target = FName("Building");
		Demo.Value = (double)Forge;
		Sim->EnqueueCommand(Demo);
		SubSteps(1);
		const bool bForgeGone = Find(Forge) == nullptr;
		const int32 Reads0 = Sim->Debug_GetStallReads();
		SubSteps(300);
		const int32 Reads = Sim->Debug_GetStallReads() - Reads0;
		FString Why;
		const bool bAllowed = Sim->CanEnterEraMode(Why);
		const bool bStalled = Sim->IsSiteStalled(Hab);
		// The era band, asked before each step as Tick asks.
		int32 EraAllowed = 0;
		for (int32 K = 0; K < 3; ++K)
		{
			FString W;
			EraAllowed += Sim->CanEnterEraMode(W) ? 1 : 0;
			EraStepOnce();
		}
		Give(LanderId, NSeg, 2.0); // the segments appear (a ship, a trade)
		EraStepOnce();
		const FString WhyBack = EraReason();
		const bool bStalledBack = Sim->IsSiteStalled(Hab);
		// A surface site short only of Shielding, with regolith on a pile, is never in the set; with no regolith, no designated
		// regolith deposit and no Forge it is.
		Reset();
		Give(LanderId, NStruct, 5000.0);
		const FRHDepositState* RegA = Dep(TEXT("Regolith_A"));
		const int32 RegId = RegA ? RegA->Id : 0;
		Sim->Debug_SetDepositPileKg(RegId, 500.0);
		const int32 Bay = Order(TEXT("HabModule"), 2000.0, 1000.0);
		Give(Bay, NStruct, 200.0);
		int32 EverStalled = 0;
		for (int32 K = 0; K < 3; ++K) { SubSteps(100); EverStalled += Sim->IsSiteStalled(Bay) ? 1 : 0; }
		const FString WhyShield = EraReason();
		Sim->Debug_SetDepositPileKg(RegId, 0.0);
		// c0a_b: the Regolith haul the board posted while the pile held regolith is C0-3's rule (building.shield's), not this check's.
		// It is removed as a hauler would find it (the pile empty: the stale task goes) WHEN there is one; what this check needs is
		// that no Regolith haul to the site is left. (The first dry run of the mutation 'shieldleg' failed this check as well, through
		// this fixture: with the haul never posted there was no stale task to remove, though the site read stalled.)
		const FRHTask* Leg = TaskTo(Bay, ERHTaskType::Haul, NRegolith);
		const bool bHadLeg = Leg != nullptr;
		float Loaded = 0.f; FVector Drop;
		const bool bLegRemoved = !Leg || !Sim->HaulLoad(Leg->Id, 200.f, Loaded, Drop);
		const bool bNoLeg = bLegRemoved && TaskTo(Bay, ERHTaskType::Haul, NRegolith) == nullptr;
		SubSteps(100);
		const bool bBayStalled = Sim->IsSiteStalled(Bay);
		const FString WhyNothing = EraReason();
		C.Need(Hab != 0 && Forge != 0 && bForgeGone, TEXT("the fixture: a prefab Habitat site, the Forge demolished"));
		C.Need(WhyForge.StartsWith(TEXT("the Habitat is ")) && !bStalledEarly, TEXT("while the Forge stood the site held 60x"));
		C.Need(bAllowed && bStalled, TEXT("after the demolish and the next read CanEnterEraMode is true"));
		C.Need(Reads == 3, TEXT("over the 300 sub-steps exactly 3 reads were made"));
		C.Need(EraAllowed == 3, TEXT("the era band is allowed before each of three era steps"));
		C.Need(WhyBack.StartsWith(TEXT("the Habitat is ")) && !bStalledBack, TEXT("the era band refuses on the first era step after the segments appear"));
		C.Need(Bay != 0 && EverStalled == 0 && WhyShield.StartsWith(TEXT("the Habitat Bay is ")), TEXT("a site short only of Shielding with regolith on a pile is never in the set"));
		C.Need(bNoLeg && bBayStalled && WhyNothing == TEXT("(60x allowed)"), TEXT("with no regolith, no designated regolith deposit and no Forge it is"));
		Report(TEXT("building.stall"), C, FString::Printf(TEXT("a prefab Habitat site #%d ordered with a Forge standing: 60x refused '%s'; the Forge demolished and 30.0 sim-s of sub-steps: %d read(s) of the stalled set (expect 3; EraStallReadS %.1f), the site stalled %d, 60x allowed %d; allowed before each of 3 era steps (%d); 2 Habitat Segments put in the Lander and one era step: refused '%s'. A Habitat Bay site #%d short only of Shielding with 500 kg on Regolith_A's pile: stalled at %d of 3 reads, 60x refused '%s'; the pile emptied, %s, no designated regolith and no Forge: stalled %d, '%s'"),
			Hab, *WhyForge, Reads, URHSimWorldSubsystem::EraStallReadS, (int32)bStalled, (int32)bAllowed, EraAllowed, *WhyBack, Bay, EverStalled, *WhyShield,
			bHadLeg ? TEXT("the stale haul gone") : TEXT("no Regolith haul was ever posted"), (int32)bBayStalled, *WhyNothing));
	}

	// ---------------- building.manifest ----------------
	{
		Reset();
		FCheck C;
		const FRHManifestItemRow* Row = DefsSub->GetManifestItem(FName("ExtraFabricator"));
		const FRHManifestItemRow* Hauler_ = DefsSub->GetManifestItem(FName("ExtraHauler"));
		const FRHManifestItemRow* Excavator = DefsSub->GetManifestItem(FName("AdvExcavator"));
		const FRHManifestEffect* Effect = Sim->FindManifestEffect(FName("ExtraFabricator"));
		const int32 N0 = Sim->GetFleetCount(FName("RC_F"));
		int32 Spawned = 0;
		const FDelegateHandle SpawnH = Sim->OnRobotsSpawned.AddLambda([&Spawned](const TArray<FMassEntityHandle>& Units) { Spawned += Units.Num(); });
		Sim->Debug_DeliverCargo(FName("ExtraFabricator"));
		Sim->OnRobotsSpawned.Remove(SpawnH);
		const int32 N1 = Sim->GetFleetCount(FName("RC_F"));
		C.Need(Row && Row->SliceActive && Row->Category == TEXT("Fleet") && Row->Mass_kg == 2200.f && Row->Price_mcr == 2200000, TEXT("the row is active, Fleet, 2200 kg, 2,200,000 mcr"));
		C.Need(Row && Hauler_ && Excavator && Hauler_->Mass_kg < Row->Mass_kg && Row->Mass_kg < Excavator->Mass_kg && Hauler_->Price_mcr < Row->Price_mcr && Row->Price_mcr < Excavator->Price_mcr,
			TEXT("strictly between ExtraHauler and AdvExcavator on both"));
		C.Need(Effect && Effect->Verb == ERHManifestVerb::Robot && Effect->Target == FName("RC_F") && Effect->Amount == 1.0, TEXT("its effect is Robot, RC_F, 1"));
		C.Need(N0 == 1 && N1 == 2 && Spawned == 1, TEXT("delivered, the RC_F count is 2 and one robot spawned"));
		Report(TEXT("building.manifest"), C, FString::Printf(TEXT("ExtraFabricator in the live table: active %d, %s, %.0f kg, %lld mcr (ExtraHauler %.0f kg, AdvExcavator %.0f kg); its effect target %s amount %.0f; delivered on a fresh colony the RC_F count goes %d -> %d with %d robot spawned"),
			Row ? (int32)Row->SliceActive : 0, Row ? *Row->Category : TEXT("?"), Row ? Row->Mass_kg : 0.f, Row ? (int64)Row->Price_mcr : 0, Hauler_ ? Hauler_->Mass_kg : 0.f, Excavator ? Excavator->Mass_kg : 0.f,
			Effect ? *Effect->Target.ToString() : TEXT("?"), Effect ? Effect->Amount : 0.0, N0, N1, Spawned));
	}

	// ---------------- building.words ----------------
	{
		Reset();
		FCheck C;
		Give(LanderId, NStruct, 5000.0);
		Sim->SetFleetHold(true);
		const int32 Pad = Order(TEXT("Stockpile"), 1200.0, 1200.0);
		SubSteps(1);
		URHSimWorldSubsystem::FRHSiteLineState Held, Claimed, Delivered;
		const bool bHeld = Sim->GetSiteLineState(Pad, NStruct, Held);
		const FString WhyWaiting = EraReason();
		Sim->SetFleetHold(false);
		FRHTask Haul;
		const bool bClaim = Sim->TryClaimHaul(Hauler, FVector::ZeroVector, Haul) && Haul.To.BuildingId == Pad && Haul.Resource == NStruct;
		const bool bClaimed = Sim->GetSiteLineState(Pad, NStruct, Claimed);
		float Loaded = 0.f; FVector Drop;
		const bool bLoad = bClaim && Sim->HaulLoad(Haul.Id, 200.f, Loaded, Drop);
		Sim->HaulUnload(Haul.Id, Loaded);
		const bool bDelivered = Sim->GetSiteLineState(Pad, NStruct, Delivered);
		const FString WhyBuilding = EraReason();
		C.Need(Pad != 0 && bHeld && bClaimed && bDelivered && bLoad, TEXT("the fixture: a Stockpile site, Struct in the Lander, one haul"));
		C.Need(Held.bShort && Held.InStoreKg > 0.0 && !Held.bHaulClaimed && Held.bHaulPosted && Held.NeedKg == 20.0 && Held.PaidKg == 0.0, TEXT("first the line reads in store and not claimed"));
		C.Need(Claimed.bShort && Claimed.bHaulClaimed, TEXT("then claimed"));
		C.Need(!Delivered.bShort && Delivered.PaidKg == 20.0 && !Delivered.bHaulClaimed, TEXT("then not short"));
		C.Need(WhyWaiting == TEXT("the Stockpile Pad is waiting for Structural Materials"), TEXT("the era reason reads: the Stockpile Pad is waiting for Structural Materials"));
		C.Need(WhyBuilding == TEXT("the Stockpile Pad is still being built"), TEXT("then: the Stockpile Pad is still being built"));
		Report(TEXT("building.words"), C, FString::Printf(TEXT("a Stockpile site #%d with the fleet held: its Struct line reads %.0f of %.0f kg paid, short %d, %.0f kg in store, haul posted %d claimed %d, and 60x is refused '%s'; a hauler claims: claimed %d; it delivers: short %d, %.0f kg paid, and 60x is refused '%s'"),
			Pad, Held.PaidKg, Held.NeedKg, (int32)Held.bShort, Held.InStoreKg, (int32)Held.bHaulPosted, (int32)Held.bHaulClaimed, *WhyWaiting, (int32)Claimed.bHaulClaimed, (int32)Delivered.bShort, Delivered.PaidKg, *WhyBuilding));
	}

	// ---------------- building.growwhy ----------------
	{
		Reset();
		FCheck C;
		const FName NHomes(TEXT("Homes")), NLiving(TEXT("LivingQuarters"));
		Sim->Debug_SetGrowthEnabled(false);
		Sim->Debug_SetBigArrivalDone(true);
		Place(TEXT("Pylon"), 4500.0, 1500.0);
		const int32 HabA = Place(TEXT("HabModule"), 3500.0, 0.0, 2, 1);      // bays (3,0) (4,0)
		const int32 HabB = Place(TEXT("HabModule"), 3500.0, -1000.0, 2, 1);  // bays (3,-1) (4,-1): one rated 2 x 2 network with HabA
		Place(TEXT("AirFilter"), 3000.0, 0.0);
		Place(TEXT("SolarArray"), 5000.0, 0.0);                              // the district's two free cells are taken: no room to grow
		Place(TEXT("SolarArray"), 5000.0, -1000.0);
		const int32 HabU = Place(TEXT("HabModule"), 6000.0, -500.0, 1, 2);   // bays (6,-1) (6,0): its own network, no filter, never rated
		Sim->AddStock(NStruct, 5000.0);
		Sim->AddStock(NShield, 3000.0);
		Sim->AddStock(NOxygen, 6000.0);
		Sim->AddStock(FName("Food"), 6000.0);
		RunSols(1.0);
		Sim->Debug_SetVaultRated(true);
		FString R;
		for (const int32 H : { HabA, HabB }) { for (int32 BayIndex = 0; BayIndex < 2; ++BayIndex) { Sim->DesignateBay(H, BayIndex, NLiving, R); } }
		const auto Paint = [&](int32 X1, int32 Y1, int32 X2, int32 Y2)
		{
			FRHCommand P;
			P.Verb = FName("PaintDistrict");
			P.Target = NHomes;
			P.Level = 0;
			P.Location = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(X1, Y1));
			P.LocationB = URHSimWorldSubsystem::LatticeCentreCm(FIntPoint(X2, Y2));
			Sim->EnqueueCommand(P);
			EraStepOnce();
		};
		Paint(3, -1, 6, 0);
		const int32 Both = Sim->GetDistrictOfBuilding(HabA);
		const bool bBothIn = Both != 0 && Sim->GetDistrictOfBuilding(HabB) == Both && Sim->GetDistrictOfBuilding(HabU) == Both;
		const int32 Beds = Sim->GetHousingCapacity();
		const int32 Housed = Sim->Debug_AddColonists(Beds);
		Sim->Debug_StepPopulation((float)SubDt);
		Sim->Debug_SetHopeSmoothed(100.0);
		Sim->Debug_SetGrowthEnabled(true);
		const int32 Started = Sim->Debug_RunGrowthCheck();
		const FString NoteBoth = Sim->GetGrowthNote(Both);
		const bool bRatedA = Sim->IsHabRated(HabA), bRatedU = Sim->IsHabRated(HabU);
		// Only the unrated one in the district.
		FRHCommand Erase;
		Erase.Verb = FName("EraseDistrict");
		Erase.Value = (double)Both;
		Sim->EnqueueCommand(Erase);
		EraStepOnce();
		Paint(6, -1, 6, 0);
		const int32 Only = Sim->GetDistrictOfBuilding(HabU);
		Sim->Debug_SetHopeSmoothed(100.0);
		const int32 Started2 = Sim->Debug_RunGrowthCheck();
		const FString NoteOnly = Sim->GetGrowthNote(Only);
		C.Need(HabA != 0 && HabB != 0 && HabU != 0 && HabU > HabA && bRatedA && !bRatedU && bBothIn && Beds > 0 && Housed == Beds && Rejects.Num() == 0,
			TEXT("the fixture: a rated 2 x 2 habitat and an unrated 1 x 2 one with the higher Id in one Homes district, every bed taken"));
		C.Need(Started == 0 && NoteBoth.StartsWith(FString::Printf(TEXT("habitat #%d has no room to grow"), Sim->GetHabNetworkRoot(HabA))), TEXT("the note names the rated habitat's reason"));
		C.Need(Only != 0 && Only != Both && Sim->GetDistrictOfBuilding(HabA) != Only && Started2 == 0 && NoteOnly == FString::Printf(TEXT("habitat #%d is not rated yet"), HabU),
			TEXT("with only the unrated one in the district it reads: habitat #N is not rated yet"));
		Report(TEXT("building.growwhy"), C, FString::Printf(TEXT("a Homes district #%d over a rated 2 x 2 habitat (root #%d, no free cell beside it) and an unrated 1 x 2 habitat #%d, %d of %d beds taken: the growth note reads '%s'; a district #%d over the unrated one alone reads '%s'"),
			Both, Sim->GetHabNetworkRoot(HabA), HabU, Housed, Beds, *NoteBoth, Only, *NoteOnly));
	}

	// ---------------- building.zone ----------------
	{
		Reset();
		FCheck C;
		FRHCommand Zone;
		Zone.Verb = FName("Designate");
		Zone.Target = FName("LivingQuarters");
		Zone.Level = 0;
		Zone.Location = FVector(0.0, -2000.0, 0.0); // open ground, no habitat bay
		Sim->EnqueueCommand(Zone);
		SubSteps(1);
		const FString Surface = Rejects.Num() == 1 ? Rejects.Last() : FString();
		Zone.Level = -1;
		Zone.Value = 7.0; // past the carved cells of floor -1 (none on a fresh colony)
		Zone.Location = FVector::ZeroVector;
		Sim->EnqueueCommand(Zone);
		SubSteps(1);
		const FString Below = Rejects.Num() == 2 ? Rejects.Last() : FString();
		C.Need(Surface == TEXT("No habitat bay there: zone a bay inside a surface habitat"), TEXT("the surface order: No habitat bay there: zone a bay inside a surface habitat"));
		C.Need(Below == TEXT("Cell 7 on floor -1 is not carved yet"), TEXT("the floor -1 order: Cell 7 on floor -1 is not carved yet"));
		Report(TEXT("building.zone"), C, FString::Printf(TEXT("a Designate order at level 0 over open ground is rejected '%s'; one at floor -1 for cell 7, past the carved cells, is rejected '%s'"), *Surface, *Below));
	}

	return Finish();
}
