#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RHWanderProcessor.generated.h"

// Scaffold/stress movement: integrates wander motion in fixed sim sub-steps
// published by the sim clock, so 8x speed really does execute 8x the work
// per real second - the honest cost model the stress test must measure.
// Replaced by the task-driven movement processor in M0-b.
UCLASS()
class REDHOPESIM_API URHWanderProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URHWanderProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
