#include "RHRobotBrainProcessor.h"
#include "RedHopeSim.h"
#include "RHAgentFragments.h"
#include "RHSimTypes.h"
#include "RHSimClockSubsystem.h"
#include "Mass/EntityFragments.h"
#include "MassExecutionContext.h"
#include "MassStateTreeFragments.h"
#include "MassStateTreeSubsystem.h"
#include "MassStateTreeExecutionContext.h"
#include "StateTree.h"

URHRobotBrainProcessor::URHRobotBrainProcessor()
	: EntityQuery(*this)
{
	ExecutionFlags = (uint8)EProcessorExecutionFlags::All;
	bRequiresGameThreadExecution = true; // tasks mutate the colony economy via subsystem API
}

void URHRobotBrainProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager)
{
	EntityQuery.AddRequirement<FTransformFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRHBatteryFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRHWearFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FRHRobotFragment>(EMassFragmentAccess::ReadOnly);
	EntityQuery.AddRequirement<FRHTaskFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FMassStateTreeInstanceFragment>(EMassFragmentAccess::ReadWrite);
	// Only matches archetypes whose COMPOSITION carries the const-shared bit -
	// the ST spawn path builds its archetype explicitly for this reason.
	// (Mass prunes processors with zero matching archetypes: a composition
	// mismatch here fails silently, with Execute never even called.)
	EntityQuery.AddConstSharedRequirement<FMassStateTreeSharedFragment>();
}

void URHRobotBrainProcessor::Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context)
{
	UWorld* World = EntityManager.GetWorld();
	URHSimClockSubsystem* Clock = World ? World->GetSubsystem<URHSimClockSubsystem>() : nullptr;
	UMassStateTreeSubsystem* StateTreeSubsystemPtr = World ? World->GetSubsystem<UMassStateTreeSubsystem>() : nullptr;
	if (!Clock || !StateTreeSubsystemPtr)
	{
		return;
	}
	const int32 Steps = Clock->GetStepsThisFrame();
	if (Steps == 0)
	{
		return; // paused or era band: decisions freeze with the agents
	}
	const float SubDt = URHSimClockSubsystem::SubStepSeconds;

	int32 Matched = 0;
	EntityQuery.ForEachEntityChunk(Context,
		[Steps, SubDt, &Matched, StateTreeSubsystemPtr](FMassExecutionContext& ChunkContext)
	{
		Matched += ChunkContext.GetNumEntities();
		UMassStateTreeSubsystem& StateTreeSubsystem = *StateTreeSubsystemPtr;
		const FMassStateTreeSharedFragment& Shared = ChunkContext.GetConstSharedFragment<FMassStateTreeSharedFragment>();
		const UStateTree* StateTree = Shared.StateTree;
		if (!StateTree)
		{
			return;
		}
		const TArrayView<FMassStateTreeInstanceFragment> Instances = ChunkContext.GetMutableFragmentView<FMassStateTreeInstanceFragment>();
		const TArrayView<FRHBatteryFragment> Batteries = ChunkContext.GetMutableFragmentView<FRHBatteryFragment>();
		const TArrayView<FRHTaskFragment> TaskFrags = ChunkContext.GetMutableFragmentView<FRHTaskFragment>();

		for (int32 i = 0; i < ChunkContext.GetNumEntities(); ++i)
		{
			FStateTreeInstanceData* InstanceData = StateTreeSubsystem.GetInstanceData(Instances[i].InstanceHandle);
			if (!InstanceData)
			{
				continue;
			}
			FMassStateTreeExecutionContext StateTreeContext(StateTreeSubsystem, *StateTree, *InstanceData, ChunkContext);
			StateTreeContext.SetEntity(ChunkContext.GetEntity(i));
			if (!ensureMsgf(StateTreeContext.AreContextDataViewsValid(), TEXT("Robot StateTree missing external data")))
			{
				continue;
			}
			if (StateTreeContext.GetStateTreeRunStatus() != EStateTreeRunStatus::Running)
			{
				if (StateTreeContext.Start() == EStateTreeRunStatus::Failed)
				{
					UE_LOG(LogRedHopeSim, Warning, TEXT("Robot entity %d: StateTree Start failed"),
						ChunkContext.GetEntity(i).Index);
					continue;
				}
			}

			for (int32 Step = 0; Step < Steps; ++Step)
			{
				// Halt gate (M0-c rule): a drained robot stops where it stands
				// unless it is already docked at a pad (Charge phase 1) - the
				// pad can refill a dead battery; the open plain cannot.
				const FRHBatteryFragment& Battery = Batteries[i];
				const FRHTaskFragment& Task = TaskFrags[i];
				const bool bDockedAtPad = (ERHTaskType)Task.TaskType == ERHTaskType::Charge && Task.Phase == 1;
				if (Battery.ChargeWh <= 0.f && !bDockedAtPad)
				{
					break;
				}
				StateTreeContext.Tick(SubDt);
			}
		}
	});

	// Ops signal: which brain population is live (logs only on change).
	static int32 LastMatched = -1;
	if (Matched != LastMatched)
	{
		LastMatched = Matched;
		UE_LOG(LogRedHopeSim, Display, TEXT("Brain processor: %d StateTree robots matched"), Matched);
	}
}
