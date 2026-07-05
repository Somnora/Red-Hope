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
	const FMassEntityHandle Entity = static_cast<FMassStateTreeExecutionContext&>(Context).GetEntity();

	float DrawW = Robot.DrawIdleW;

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
			if (Sim->TryClaimHaul(Entity, Transform.GetLocation(), Board))
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
			if (Sim->TryClaimBuild(Entity, Transform.GetLocation(), Board))
			{
				Task.TaskType = (uint8)ERHTaskType::Build;
				Task.TaskId = Board.Id;
				Task.DigDepositId = Board.To.BuildingId; // reused slot: construction site id
				Task.TargetCm = Sim->GetSiteLocation(Board.To);
				Task.Phase = 0;
			}
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
			if (RH::MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime))
			{
				Task.Phase = 1;
			}
		}
		else if (Sim->IsDepositWorkable(Task.DigDepositId))
		{
			DrawW = Robot.DrawWorkW;
			// WorkRate = kg/sol-hour; sol-hour = 50 sim-s.
			Sim->DigDeposit(Task.DigDepositId, Robot.WorkRate * (DeltaTime / 50.f));
		}
		// else: pile full - stand by at idle draw until haulers clear it.
		break;
	}

	case ERHTaskType::Haul:
	{
		DrawW = Robot.DrawMoveW;
		if (Task.Phase == 0) // to pickup
		{
			if (RH::MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime))
			{
				Task.Phase = 1;
			}
		}
		else if (Task.Phase == 1) // load at the From site
		{
			float LoadedKg = 0.f;
			FVector DropoffCm = FVector::ZeroVector;
			if (Sim->HaulLoad(Task.TaskId, Robot.CargoCapKg, LoadedKg, DropoffCm))
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
			if (RH::MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime))
			{
				Sim->HaulUnload(Task.TaskId, Task.CargoKg);
				Task = FRHTaskFragment();
			}
		}
		break;
	}

	case ERHTaskType::Build:
	{
		if (Task.Phase == 0)
		{
			DrawW = Robot.DrawMoveW;
			if (RH::MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime))
			{
				Task.Phase = 1;
			}
		}
		else
		{
			DrawW = Robot.DrawWorkW;
			if (Sim->ApplyBuildWork(Task.DigDepositId, DeltaTime * Robot.WorkRate))
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
	return EStateTreeRunStatus::Running;
}

// --- FRHChargeTask ---

bool FRHChargeTask::Link(FStateTreeLinker& Linker)
{
	Linker.LinkExternalData(TransformHandle);
	Linker.LinkExternalData(BatteryHandle);
	Linker.LinkExternalData(RobotHandle);
	Linker.LinkExternalData(TaskHandle);
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

	// Put the claim back before leaving (M0-c etiquette block).
	const ERHTaskType Current = (ERHTaskType)Task.TaskType;
	if (Current == ERHTaskType::Dig)
	{
		Sim->ReleaseDigClaim(Task.DigDepositId);
	}
	else if (Current == ERHTaskType::Haul || Current == ERHTaskType::Build)
	{
		Sim->AbandonTask(Task.TaskId);
	}

	int32 PadId = 0;
	FVector PadLoc;
	if (!Sim->FindNearestChargePad(Transform.GetLocation(), PadId, PadLoc))
	{
		return EStateTreeRunStatus::Failed; // pad vanished since the condition passed
	}
	Task = FRHTaskFragment();
	Task.TaskType = (uint8)ERHTaskType::Charge;
	Task.DigDepositId = PadId; // slot reused: pad building id
	Task.TargetCm = PadLoc;
	Task.Phase = 0;
	return EStateTreeRunStatus::Running;
}

void FRHChargeTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Topped up (or bounced): clean slate; Work claims fresh.
	Context.GetExternalData(TaskHandle) = FRHTaskFragment();
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

	if (Task.Phase == 0)
	{
		if (RH::MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, DeltaTime))
		{
			Task.Phase = 1;
		}
		Drain(Battery, Robot.DrawMoveW, DeltaTime);
	}
	else
	{
		// Docked: pad supplies the robot; zero own draw.
		const float Granted = (float)Sim->RequestChargeWh(Task.DigDepositId, DeltaTime);
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
	return true;
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

	const float ChargeFrac = Battery.CapacityWh > 0.f ? Battery.ChargeWh / Battery.CapacityWh : 0.f;
	if (ChargeFrac >= Sim->GetChargeSeekFraction())
	{
		return false;
	}
	// Mid-delivery: finish the drop first (M0-c rule).
	if ((ERHTaskType)Task.TaskType == ERHTaskType::Haul && Task.Phase == 2)
	{
		return false;
	}
	int32 PadId = 0;
	FVector PadLoc;
	return Sim->FindNearestChargePad(Context.GetExternalData(TransformHandle).GetTransform().GetLocation(), PadId, PadLoc);
}
