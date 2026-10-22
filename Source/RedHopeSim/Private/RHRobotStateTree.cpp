#include "RHRobotStateTree.h"
#include "RedHopeSim.h"
#include "RHRobotMovement.h"
#include "RHSimTypes.h"
#include "RHSimWorldSubsystem.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeLinker.h"
#include "MassStateTreeExecutionContext.h"
#include "Engine/World.h"

namespace
{
	URHSimWorldSubsystem* GetSim(const FStateTreeExecutionContext& Context)
	{
		UWorld* World = Context.GetWorld();
		return World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	}

	// Sub-step battery drain, identical to the M0-c processor epilogue.
	void Drain(FRHBatteryFragment& Battery, float DrawW, float Dt)
	{
		Battery.ChargeWh = FMath::Max(0.f, Battery.ChargeWh - DrawW * (Dt / 50.f));
	}
}

// --- FRHWorkTask ---

bool FRHWorkTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TransformHandle);
	Linker.LinkExternalData(BatteryHandle);
	Linker.LinkExternalData(RobotHandle);
	Linker.LinkExternalData(TaskHandle);
	Linker.LinkExternalData(WearHandle);
	return true;
}

EStateTreeRunStatus FRHWorkTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	URHSimWorldSubsystem* Sim = GetSim(Context);
	if (!Sim)
	{
		return EStateTreeRunStatus::Failed;
	}
	FTransform& Transform = Context.GetExternalData(TransformHandle).GetMutableTransform();
	FRHBatteryFragment& Battery = Context.GetExternalData(BatteryHandle);
	const FRHRobotFragment& Robot = Context.GetExternalData(RobotHandle);
	FRHTaskFragment& Task = Context.GetExternalData(TaskHandle);
	FRHWearFragment& Wear = Context.GetExternalData(WearHandle);
	const FMassEntityHandle Entity = static_cast<FMassStateTreeExecutionContext&>(Context).GetEntity();

	float DrawW = Robot.DrawIdleW;
	const bool bRover = Robot.RobotClass == FName("Rover"); // C6a: a cargo rover drives with its own movement profile
	// D4: the robot's floor, read from its Z (0 for every robot until a lift car is fitted). A sub-step spent holding at a column
	// for a ride draws idle power and accrues no wear (Move overrides the movement draw then).
	const int32 Level = Sim->HasRobotLifts() ? Sim->LevelOfZ(Transform.GetLocation().Z) : 0;
	const auto Move = [&](float CargoKg) -> bool
	{
		const bool bArrived = RH::MoveAlong(*Sim, Entity, Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime, bRover, CargoKg, Task.TargetLevel);
		if (Sim->DidRobotHoldThisStep(Entity))
		{
			DrawW = Robot.DrawIdleW;
		}
		return bArrived;
	};
	// Fleet reality (M1-b): worn machinery works slower - linear from the
	// degrade threshold down to nothing at halt (the processor's halt gate
	// stops ticking before the multiplier ever reaches zero).
	const float WearMul = Sim->GetWearWorkMul(Wear.Wear);

	// The M0-c switch, verbatim - the tree decides Work vs Charge; this task
	// is the whole Work activity.
	switch ((ERHTaskType)Task.TaskType)
	{
	case ERHTaskType::None:
	{
		if (Robot.RobotClass == FName("Excavator"))
		{
			const int32 DepId = Sim->TryClaimDig(Transform.GetLocation());
			if (DepId != 0)
			{
				Task.TaskType = (uint8)ERHTaskType::Dig;
				Task.DigDepositId = DepId;
				FRHSiteRef Site; Site.DepositId = DepId;
				Task.TargetCm = Sim->GetSiteLocation(Site);
				Task.Phase = 0;
			}
		}
		else if (Robot.RobotClass == FName("Hauler"))
		{
			FRHTask Board;
			if (Sim->TryClaimHaul(Entity, Transform.GetLocation(), Board, Level))
			{
				Task.TaskType = (uint8)ERHTaskType::Haul;
				Task.TaskId = Board.Id;
				Task.CargoResource = Board.Resource;
				Task.CargoKg = 0.f;
				// D4: the pickup at its own floor once a lift is fitted (TryClaimHaul gave only reachable ones).
				Task.TargetCm = Sim->HasRobotLifts() ? Sim->GetRobotTarget(Board.From, Level, Transform.GetLocation(), Task.TargetLevel) : Sim->GetSiteLocation(Board.From);
				Task.Phase = 0;
			}
		}
		else if (bRover)
		{
			// C6a: a rover takes freight only, and only what it can reach by road (R6).
			FRHTask Board;
			const float ChargeFrac = Battery.CapacityWh > 0.f ? Battery.ChargeWh / Battery.CapacityWh : 0.f;
			if (Sim->TryClaimFreight(Entity, Transform.GetLocation(), ChargeFrac, Board))
			{
				Task.TaskType = (uint8)ERHTaskType::Haul;
				Task.TaskId = Board.Id;
				Task.CargoResource = Board.Resource;
				Task.CargoKg = 0.f;
				Task.TargetCm = Sim->GetSiteLocation(Board.From);
				Task.Phase = 0;
			}
		}
		else if (Robot.RobotClass == FName("Fabricator"))
		{
			FRHTask Board;
			if (Sim->TryClaimBuild(Entity, Transform.GetLocation(), Board, Level))
			{
				Task.TaskType = (uint8)ERHTaskType::Build;
				Task.TaskId = Board.Id;
				Task.DigDepositId = Board.To.BuildingId; // reused slot: construction site id
				Task.RoadEdgeId = Board.To.RoadEdgeId;   // C4: a road edge is labour only, in its own slot
				// Cross-level sites are worked from the shaft head (M1-d); D4: or at the site's cell, when a fitted lift reaches it.
				Task.TargetCm = Sim->GetRobotTarget(Board.To, Level, Transform.GetLocation(), Task.TargetLevel);
				Task.Phase = 0;
			}
		}
		else if (Robot.RobotClass == FName("Scout"))
		{
			FRHTask Board;
			if (Sim->TryClaimSurvey(Entity, Transform.GetLocation(), Board))
			{
				Task.TaskType = (uint8)ERHTaskType::Survey;
				Task.TaskId = Board.Id;
				Task.TargetCm = Board.TargetCm;
				Task.Phase = 0;
			}
		}
		else if (Robot.RobotClass == FName("Maintenance"))
		{
			FMassEntityHandle Target;
			FVector TargetCm;
			if (Sim->TryClaimRepair(Entity, Target, TargetCm))
			{
				Task.TaskType = (uint8)ERHTaskType::Repair;
				Task.RepairTarget = Target;
				Task.TargetCm = TargetCm;
				Task.Phase = 0;
			}
		}
		break;
	}

	case ERHTaskType::Survey:
	{
		DrawW = Robot.DrawMoveW;
		if (RH::MoveAlong(*Sim, Entity, Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime))
		{
			// WorkRate = survey radius in meters (DT_Robots).
			Sim->CompleteSurvey(Task.TaskId, Robot.WorkRate);
			Task = FRHTaskFragment();
		}
		break;
	}

	case ERHTaskType::Repair:
	{
		DrawW = Robot.DrawMoveW;
		// The patient may still be limping somewhere: re-track it each step.
		FVector TargetCm;
		if (!Sim->GetRepairTargetPos(Task.RepairTarget, TargetCm))
		{
			Sim->ReleaseRepairClaim(Task.RepairTarget);
			Task = FRHTaskFragment();
			break;
		}
		if (Sim->HasRobotLifts())
		{
			// D4 (D3 REVISION item 4): across floors, or from below, only to a halted patient - it does not move, so one deep
			// plan serves - and never through the rock. A patient that is no longer halted (repaired, or it rode away) is released.
			const int32 PatientLevel = Sim->LevelOfZ(TargetCm.Z);
			if (PatientLevel != Level || Level < 0)
			{
				if (!Sim->IsRobotHalted(Task.RepairTarget))
				{
					Sim->ReleaseRepairClaim(Task.RepairTarget);
					Task = FRHTaskFragment();
					break;
				}
				Task.TargetCm = TargetCm;
				Task.TargetLevel = PatientLevel;
				if (Move(0.f))
				{
					Sim->ApplyRepairAt(Task.RepairTarget, Sim->LevelOfZ(Transform.GetLocation().Z));
					Sim->ReleaseRepairClaim(Task.RepairTarget);
					Task = FRHTaskFragment();
				}
				break;
			}
		}
		Task.TargetCm = TargetCm;
		if (RH::MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime))
		{
			Sim->ApplyRepairAt(Task.RepairTarget, Level);
			Sim->ReleaseRepairClaim(Task.RepairTarget);
			Task = FRHTaskFragment();
		}
		break;
	}

	case ERHTaskType::Dig:
	{
		if (Sim->IsDepositSpent(Task.DigDepositId))
		{
			Sim->ReleaseDigClaim(Task.DigDepositId);
			Task = FRHTaskFragment();
			break;
		}
		if (Task.Phase == 0)
		{
			DrawW = Robot.DrawMoveW;
			if (RH::MoveAlong(*Sim, Entity, Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime))
			{
				Task.Phase = 1;
			}
		}
		else if (Sim->IsDepositWorkable(Task.DigDepositId))
		{
			DrawW = Robot.DrawWorkW;
			// WorkRate = kg/sol-hour; sol-hour = 50 sim-s.
			Sim->DigDeposit(Task.DigDepositId, Robot.WorkRate * (DeltaTime / 50.f) * WearMul);
		}
		// else: pile full - stand by at idle draw until haulers clear it.
		break;
	}

	case ERHTaskType::Haul:
	{
		DrawW = Robot.DrawMoveW;
		if (Task.Phase == 0) // to pickup
		{
			if (Move(0.f))
			{
				Task.Phase = 1;
			}
		}
		else if (Task.Phase == 1) // load at the From site
		{
			float LoadedKg = 0.f;
			FVector DropoffCm = FVector::ZeroVector;
			if (Sim->HaulLoad(Task.TaskId, Robot.CargoCapKg, LoadedKg, DropoffCm, Level, &Task.TargetLevel))
			{
				Task.CargoKg = LoadedKg;
				Task.TargetCm = DropoffCm;
				Task.Phase = 2;
			}
			else
			{
				Task = FRHTaskFragment(); // task vanished or nothing to load
			}
		}
		else if (Task.Phase == 2) // to dropoff
		{
			// D4 (REVISION item 6): with transit on, a drop at the head for a site on another floor holds there until the
			// cage has taken the load down (idle draw, no wear), then credits. With no transit the hold is never entered.
			if (Sim->InHandoffHold(Entity, Task.TaskId, Transform.GetLocation()))
			{
				DrawW = Robot.DrawIdleW;
				if (Sim->StepHandoffHold(Entity))
				{
					Sim->HaulUnload(Task.TaskId, Task.CargoKg, bRover ? Task.CargoResource : NAME_None, Level);
					Task = FRHTaskFragment();
				}
			}
			else if (Move(Task.CargoKg))
			{
				if (Sim->BeginHandoffHold(Entity, Task.TaskId, Sim->HasRobotLifts() ? Sim->LevelOfZ(Transform.GetLocation().Z) : 0, Transform.GetLocation()) == 0)
				{
					Sim->HaulUnload(Task.TaskId, Task.CargoKg, bRover ? Task.CargoResource : NAME_None, Sim->HasRobotLifts() ? Sim->LevelOfZ(Transform.GetLocation().Z) : 0);
					Task = FRHTaskFragment();
				}
			}
		}
		break;
	}

	case ERHTaskType::Build:
	{
		if (Task.Phase == 0)
		{
			DrawW = Robot.DrawMoveW;
			if (Move(0.f))
			{
				Task.Phase = 1;
			}
		}
		else
		{
			DrawW = Robot.DrawWorkW;
			// C4: a road edge is labour only (its own slot, never the site id).
			const bool bDone = Task.RoadEdgeId > 0
				? Sim->ApplyRoadWork(Task.RoadEdgeId, DeltaTime * Robot.WorkRate * WearMul)
				: Sim->ApplyBuildWork(Task.DigDepositId, DeltaTime * Robot.WorkRate * WearMul, Level);
			if (bDone)
			{
				Sim->CompleteTask(Task.TaskId);
				Task = FRHTaskFragment();
			}
		}
		break;
	}

	default:
		break;
	}

	Drain(Battery, DrawW, DeltaTime);
	// Wear follows exertion: any sub-step above idle draw ages the machine.
	if (DrawW > Robot.DrawIdleW)
	{
		Sim->AccrueWear(Wear.Wear, Robot.WearPerSol, DeltaTime);
	}
	return EStateTreeRunStatus::Running;
}

// --- FRHChargeTask ---

bool FRHChargeTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TransformHandle);
	Linker.LinkExternalData(BatteryHandle);
	Linker.LinkExternalData(RobotHandle);
	Linker.LinkExternalData(TaskHandle);
	Linker.LinkExternalData(WearHandle);
	return true;
}

EStateTreeRunStatus FRHChargeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	URHSimWorldSubsystem* Sim = GetSim(Context);
	if (!Sim)
	{
		return EStateTreeRunStatus::Failed;
	}
	FTransform& Transform = Context.GetExternalData(TransformHandle).GetMutableTransform();
	FRHTaskFragment& Task = Context.GetExternalData(TaskHandle);
	const FRHRobotFragment& Robot = Context.GetExternalData(RobotHandle);
	const FMassEntityHandle Entity = static_cast<FMassStateTreeExecutionContext&>(Context).GetEntity();

	// Put the claim back before leaving (M0-c etiquette block).
	const ERHTaskType Current = (ERHTaskType)Task.TaskType;
	if (Current == ERHTaskType::Dig)
	{
		Sim->ReleaseDigClaim(Task.DigDepositId);
	}
	else if (Current == ERHTaskType::Haul || Current == ERHTaskType::Build || Current == ERHTaskType::Survey)
	{
		// C6a (R3): a rover that breaks off a delivery puts its load back before the fragment is reset - a hauler
		// still loses its (up to 200 kg), a rover's is 1 t. The load goes to the task's source, else the Lander.
		if (Current == ERHTaskType::Haul && Task.CargoKg > 0.f && Robot.RobotClass == FName("Rover"))
		{
			Sim->ReturnHaulCargo(Task.TaskId, Task.CargoResource, Task.CargoKg);
		}
		else if (Current == ERHTaskType::Haul && Task.CargoKg > 0.f)
		{
			Sim->NoteHaulCargoLost(Task.CargoKg); // D4's mass balance: the hauler's load is lost, as it always was
		}
		Sim->AbandonTask(Task.TaskId);
	}
	else if (Current == ERHTaskType::Repair)
	{
		Sim->ReleaseRepairClaim(Task.RepairTarget);
	}

	int32 PadId = 0;
	FVector PadLoc;
	// C6a: a rover docks only at a road-served pad.
	const bool bPadFound = Robot.RobotClass == FName("Rover")
		? Sim->FindNearestRoverPad(Transform.GetLocation(), PadId, PadLoc)
		: Sim->FindNearestChargePad(Transform.GetLocation(), PadId, PadLoc);
	if (!bPadFound)
	{
		return EStateTreeRunStatus::Failed; // pad vanished since the condition passed
	}
	Task = FRHTaskFragment();
	Task.TaskType = (uint8)ERHTaskType::Charge;
	Task.DigDepositId = PadId; // slot reused: pad building id
	Task.TargetCm = PadLoc;
	Task.Phase = 0;
	// Queue slot is taken at DOCKING (Tick), not here: a robot that dies on
	// the drive must never hold the umbilical from the open plain.
	return EStateTreeRunStatus::Running;
}

void FRHChargeTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Give the umbilical back, then clean slate; Work claims fresh.
	FRHTaskFragment& Task = Context.GetExternalData(TaskHandle);
	if (URHSimWorldSubsystem* Sim = GetSim(Context))
	{
		const FMassEntityHandle Entity = static_cast<FMassStateTreeExecutionContext&>(Context).GetEntity();
		Sim->LeavePadQueue(Task.DigDepositId, Entity);
	}
	Task = FRHTaskFragment();
}

EStateTreeRunStatus FRHChargeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	URHSimWorldSubsystem* Sim = GetSim(Context);
	if (!Sim)
	{
		return EStateTreeRunStatus::Failed;
	}
	FTransform& Transform = Context.GetExternalData(TransformHandle).GetMutableTransform();
	FRHBatteryFragment& Battery = Context.GetExternalData(BatteryHandle);
	const FRHRobotFragment& Robot = Context.GetExternalData(RobotHandle);
	FRHTaskFragment& Task = Context.GetExternalData(TaskHandle);
	FRHWearFragment& Wear = Context.GetExternalData(WearHandle);
	const FMassEntityHandle Entity = static_cast<FMassStateTreeExecutionContext&>(Context).GetEntity();

	// BM-3: the pad was demolished under it - back to Work, which re-seeks a
	// dock (the next pad, or the Lander) while the battery is still low.
	if (!Sim->HasBuilding(Task.DigDepositId))
	{
		return EStateTreeRunStatus::Failed;
	}
	if (Task.Phase == 0)
	{
		if (RH::MoveAlong(*Sim, Entity, Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime, Robot.RobotClass == FName("Rover"), 0.f, Task.TargetLevel))
		{
			Task.Phase = 1;
			Sim->JoinPadQueue(Task.DigDepositId, Entity); // docked: take a queue slot
		}
		// D4: a sub-step holding at a column for the ride up draws idle power and costs no wear.
		const bool bHeld = Sim->DidRobotHoldThisStep(Entity);
		Drain(Battery, bHeld ? Robot.DrawIdleW : Robot.DrawMoveW, DeltaTime);
		if (!bHeld)
		{
			Sim->AccrueWear(Wear.Wear, Robot.WearPerSol, DeltaTime); // the drive costs wear; the dock does not
		}
	}
	else
	{
		// Docked: pad supplies the robot; zero own draw. Only the queue head
		// draws - the rest wait for the umbilical.
		const float Granted = (float)Sim->RequestChargeWh(Task.DigDepositId, DeltaTime, Entity);
		Battery.ChargeWh = FMath::Min(Battery.CapacityWh, Battery.ChargeWh + Granted);
		if (Battery.ChargeWh >= Battery.CapacityWh * Sim->GetChargeResumeFraction())
		{
			return EStateTreeRunStatus::Succeeded; // topped up: back to work
		}
	}
	return EStateTreeRunStatus::Running;
}

// --- FRHNeedsChargeCondition ---

bool FRHNeedsChargeCondition::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TransformHandle);
	Linker.LinkExternalData(BatteryHandle);
	Linker.LinkExternalData(TaskHandle);
	Linker.LinkExternalData(RobotHandle);
	return true;
}

namespace
{
	// Wh a robot spends driving DistCm at its own speed and move draw (the
	// Drain idiom: W x sol-hours, 50 sim-s to the sol-hour), with a 25% margin
	// plus a little slack for the approach and the queue at the pad.
	// C6a: a rover's DistCm arrives already multiplied by RoverReserveDetourMul (an O(1) bound on its worst trip).
	float DriveWh(const FRHRobotFragment& Robot, double DistCm)
	{
		const double Secs = DistCm / FMath::Max(1.0, (double)Robot.SpeedMps * 100.0);
		return (float)(Robot.DrawMoveW * Secs / 50.0 * 1.25) + Robot.DrawMoveW * 0.5f;
	}
}

bool FRHNeedsChargeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	URHSimWorldSubsystem* Sim = GetSim(Context);
	if (!Sim)
	{
		return false;
	}
	const FRHBatteryFragment& Battery = Context.GetExternalData(BatteryHandle);
	const FRHTaskFragment& Task = Context.GetExternalData(TaskHandle);
	const FRHRobotFragment& Robot = Context.GetExternalData(RobotHandle);
	const FVector Pos = Context.GetExternalData(TransformHandle).GetTransform().GetLocation();
	const FMassEntityHandle Entity = static_cast<FMassStateTreeExecutionContext&>(Context).GetEntity();

	// D4 (1.5): below, or with a ride boarded, the deep test decides on its own - the route-true reserve to the pad, priced
	// with every ascent hold at MaxWaitSec; a boarded ascent is never broken off; the mid-delivery exemption is not taken.
	// At level 0 with no ride (every robot until a lift car is fitted) it is not applicable and today's rule runs.
	const ERHDeepCharge Deep = Sim->DeepChargeTest(Entity, Robot, Pos, Battery.ChargeWh, Battery.CapacityWh);
	if (Deep != ERHDeepCharge::NotApplicable)
	{
		return Deep == ERHDeepCharge::Seek;
	}

	int32 PadId = 0;
	FVector PadLoc;
	const bool bRover = Robot.RobotClass == FName("Rover");
	// C6a: a rover's pad is the nearest ROAD-SERVED one, and every drive it budgets is bounded by
	// RoverReserveDetourMul x the straight line: the worst trip is the crawl, straight / (0.3 v), and
	// 3.0 x 1.25 >= 1 / 0.3 (asserted at config load), so the reserve always covers it.
	const double Detour = bRover ? Sim->GetRoverReserveDetourMul() : 1.0;
	if (!(bRover ? Sim->FindNearestRoverPad(Pos, PadId, PadLoc) : Sim->FindNearestChargePad(Pos, PadId, PadLoc)))
	{
		return false; // no pad: keep working (M0-c)
	}
	const float HomeWh = DriveWh(Robot, FVector::DistXY(Pos, PadLoc) * Detour);
	const float ChargeFrac = Battery.CapacityWh > 0.f ? Battery.ChargeWh / Battery.CapacityWh : 0.f;
	if (ChargeFrac >= Sim->GetChargeSeekFraction() && Battery.ChargeWh >= HomeWh)
	{
		return false;
	}
	// Mid-delivery: finish the drop first (M0-c rule) - when the drop and the
	// drive home from it both fit in the charge left. Otherwise go now: a hauler's
	// cargo is lost either way (the robot is not); a rover's goes back (C6a, R3).
	if ((ERHTaskType)Task.TaskType == ERHTaskType::Haul && Task.Phase == 2)
	{
		int32 DropPadId = 0;
		FVector DropPadLoc;
		const bool bPadFromDrop = bRover ? Sim->FindNearestRoverPad(Task.TargetCm, DropPadId, DropPadLoc) : Sim->FindNearestChargePad(Task.TargetCm, DropPadId, DropPadLoc);
		float ViaDropWh = DriveWh(Robot, FVector::DistXY(Pos, Task.TargetCm) * Detour)
			+ DriveWh(Robot, FVector::DistXY(Task.TargetCm, bPadFromDrop ? DropPadLoc : PadLoc) * Detour);
		// D4 (REVISION item 6): a head hand-off holds before it credits - the hold left (or, before arrival, the projected
		// hold) is part of finishing the drop. 0 with no transit.
		const float HoldWh = Sim->GetHandoffHoldReserveWh(Entity, Task.TaskId, Robot, 0, Pos);
		if (HoldWh > 0.f)
		{
			ViaDropWh += HoldWh;
		}
		if (Battery.ChargeWh >= ViaDropWh)
		{
			return false;
		}
	}
	return true;
}
