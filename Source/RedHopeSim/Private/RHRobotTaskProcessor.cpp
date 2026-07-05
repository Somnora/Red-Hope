#include "RHRobotTaskProcessor.h"
#include "RedHopeSim.h"
#include "RHAgentFragments.h"
#include "RHSimTypes.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"

namespace
{
	constexpr float ArriveDistCm = 300.f;

	// Move toward target on the plain; returns true when arrived.
	bool MoveToward(FTransform& Transform, const FVector& TargetCm, float SpeedMps, float Dt)
	{
		FVector Pos = Transform.GetLocation();
		const FVector To = TargetCm - Pos;
		const float DistCm = To.Size2D();
		if (DistCm <= ArriveDistCm)
		{
			return true;
		}
		Pos += To.GetSafeNormal2D() * FMath::Min(SpeedMps * 100.f * Dt, DistCm);
		Transform.SetLocation(Pos);
		Transform.SetRotation(FRotationMatrix::MakeFromX(To).ToQuat());
		return false;
	}
}

URHRobotTaskProcessor::URHRobotTaskProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (uint8)EProcessorExecutionFlags::All;
	bRequiresGameThreadExecution = true; // mutates the colony economy via subsystem API
}

void URHRobotTaskProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRHBatteryFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRHRobotFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FRHTaskFragment>(EMassFragmentAccess::ReadWrite);
}

void URHRobotTaskProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	if (!World)
	{
		return;
	}
	URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>();
	URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
	if (!Clock || !Sim)
	{
		return;
	}
	const int32 Steps = Clock->GetStepsThisFrame();
	if (Steps == 0)
	{
		return;
	}
	const float SubDt = URHSimClockSubsystem::SubStepSeconds;

	EntityQuery.ForEachEntityChunk(Context,
		[Steps, SubDt, Sim](FMassExecutionContext& ChunkContext)
	{
		const TArrayView<FTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FRHBatteryFragment> Batteries = ChunkContext.GetMutableFragmentView<FRHBatteryFragment>();
		const TConstArrayView<FRHRobotFragment> Robots = ChunkContext.GetFragmentView<FRHRobotFragment>();
		const TArrayView<FRHTaskFragment> TaskFrags = ChunkContext.GetMutableFragmentView<FRHTaskFragment>();

		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			const FMassEntityHandle Entity = ChunkContext.GetEntity(i);
			FTransform& Transform = Transforms[i].GetMutableTransform();
			FRHBatteryFragment& Battery = Batteries[i];
			const FRHRobotFragment& Robot = Robots[i];
			FRHTaskFragment& Task = TaskFrags[i];

			for (int32 Step = 0; Step < Steps; ++Step)
			{
				// Charging etiquette: below the seek threshold, break off
				// (unless mid-delivery - finish the drop first) and head for
				// the nearest pad. With no pad built, keep working: a dead
				// robot is the player's lesson, not a stuck sim.
				const float ChargeFrac = Battery.CapacityWh > 0.f ? Battery.ChargeWh / Battery.CapacityWh : 0.f;
				const ERHTaskType Current = (ERHTaskType)Task.TaskType;
				const bool bMidDelivery = (Current == ERHTaskType::Haul && Task.Phase == 2);
				if (Current != ERHTaskType::Charge && !bMidDelivery && ChargeFrac < Sim->GetChargeSeekFraction())
				{
					int32 PadId = 0;
					FVector PadLoc;
					if (Sim->FindNearestChargePad(Transform.GetLocation(), PadId, PadLoc))
					{
						// Put the claim back before leaving.
						if (Current == ERHTaskType::Dig)
						{
							Sim->ReleaseDigClaim(Task.DigDepositId);
						}
						else if (Current == ERHTaskType::Haul || Current == ERHTaskType::Build)
						{
							Sim->AbandonTask(Task.TaskId);
						}
						Task = FRHTaskFragment();
						Task.TaskType = (uint8)ERHTaskType::Charge;
						Task.DigDepositId = PadId; // slot reused: pad building id
						Task.TargetCm = PadLoc;
						Task.Phase = 0;
					}
				}

				if (Battery.ChargeWh <= 0.f && Current != ERHTaskType::Charge)
				{
					break; // drained away from a pad: halted where it stands
				}

				float DrawW = Robot.DrawIdleW;

				switch ((ERHTaskType)Task.TaskType)
				{
				case ERHTaskType::Charge:
				{
					if (Task.Phase == 0)
					{
						DrawW = Robot.DrawMoveW;
						if (Battery.ChargeWh <= 0.f)
						{
							break; // died en route to the pad
						}
						if (MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, SubDt))
						{
							Task.Phase = 1;
						}
					}
					else
					{
						DrawW = 0.f; // docked; pad supplies the robot
						const float Granted = (float)Sim->RequestChargeWh(Task.DigDepositId, SubDt);
						Battery.ChargeWh = FMath::Min(Battery.CapacityWh, Battery.ChargeWh + Granted);
						if (Battery.ChargeWh >= Battery.CapacityWh * Sim->GetChargeResumeFraction())
						{
							Task = FRHTaskFragment(); // topped up: back to work
						}
					}
					break;
				}

				case ERHTaskType::None:
				{
					// Claim by class.
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
						if (MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, SubDt))
						{
							Task.Phase = 1;
						}
					}
					else if (Sim->IsDepositWorkable(Task.DigDepositId))
					{
						DrawW = Robot.DrawWorkW;
						// WorkRate = kg/sol-hour; sol-hour = 50 sim-s.
						Sim->DigDeposit(Task.DigDepositId, Robot.WorkRate * (SubDt / 50.f));
					}
					// else: pile full - stand by at idle draw until haulers clear it.
					break;
				}

				case ERHTaskType::Haul:
				{
					DrawW = Robot.DrawMoveW;
					if (Task.Phase == 0) // to pickup
					{
						if (MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, SubDt))
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
						if (MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, SubDt))
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
						if (MoveToward(Transform, Task.TargetCm, Robot.SpeedMps, SubDt))
						{
							Task.Phase = 1;
						}
					}
					else
					{
						DrawW = Robot.DrawWorkW;
						if (Sim->ApplyBuildWork(Task.DigDepositId, SubDt * Robot.WorkRate))
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

				Battery.ChargeWh = FMath::Max(0.f, Battery.ChargeWh - DrawW * (SubDt / 50.f));
			}
		}
	});
}
