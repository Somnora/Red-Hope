#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RHRobotTaskProcessor.generated.h"

// Executes robot work in fixed sim sub-steps: claim -> travel -> work ->
// deliver, per robot class. All economy mutations go through the
// URHSimWorldSubsystem API; this processor owns only motion and phases.
// Charging behavior is M0-c (StateTree); a drained robot simply halts.
UCLASS()
class REDHOPESIM_API URHRobotTaskProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URHRobotTaskProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
