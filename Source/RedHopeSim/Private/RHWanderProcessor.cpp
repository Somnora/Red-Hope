#include "RHWanderProcessor.h"
#include "RedHopeSim.h"
#include "RHAgentFragments.h"
#include "RHSimClockSubsystem.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"

URHWanderProcessor::URHWanderProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (uint8)EProcessorExecutionFlags::All;
	bRequiresGameThreadExecution = true; // reads world subsystems (clock, sim world)
}

void URHWanderProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRHWanderFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRHBatteryFragment>(EMassFragmentAccess::ReadWrite);
}

void URHWanderProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	if (!World)
	{
		return;
	}
	URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>();
	if (!Clock)
	{
		return;
	}

	const int32 Steps = Clock->GetStepsThisFrame();
	if (Steps == 0)
	{
		return; // paused, or no whole sub-step accumulated this frame
	}

	const float SubDt = URHSimClockSubsystem::SubStepSeconds;
	FRandomStream Rand(static_cast<int32>(Clock->GetSimSecondsTotal() * 10.0));

	EntityQuery.ForEachEntityChunk(Context,
		[Steps, SubDt, &Rand](FMassExecutionContext& ChunkContext)
	{
		const TArrayView<FTransformFragment> Transforms = ChunkContext.GetMutableFragmentView<FTransformFragment>();
		const TArrayView<FRHWanderFragment> Wanders = ChunkContext.GetMutableFragmentView<FRHWanderFragment>();
		const TArrayView<FRHBatteryFragment> Batteries = ChunkContext.GetMutableFragmentView<FRHBatteryFragment>();

		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			FTransform& Transform = Transforms[i].GetMutableTransform();
			FRHWanderFragment& Wander = Wanders[i];
			FRHBatteryFragment& Battery = Batteries[i];

			FVector Pos = Transform.GetLocation();
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const FVector ToTarget = Wander.Target - Pos;
				const float DistCm = ToTarget.Size2D();
				const float MoveCm = Wander.SpeedMps * 100.f * SubDt;

				if (DistCm <= MoveCm)
				{
					Pos = Wander.Target;
					Wander.Target = Pos + FVector(Rand.FRandRange(-20000.f, 20000.f), Rand.FRandRange(-20000.f, 20000.f), 0.f);
				}
				else
				{
					Pos += ToTarget.GetSafeNormal2D() * MoveCm;
				}
				// Wh = W x sol-hours; sol-hour = 50 sim-s (units doctrine)
				Battery.ChargeWh = FMath::Max(0.f, Battery.ChargeWh - Battery.DrawMoveW * (SubDt / 50.f));
			}
			Transform.SetLocation(Pos);
			Transform.SetRotation(FRotationMatrix::MakeFromX(Wander.Target - Pos).ToQuat());
		}
	});
}
