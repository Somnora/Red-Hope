#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RHWanderProcessor.generated.h"

// Scaffold/stress movement: integrates wander motion in fixed sim sub-steps
// consumed from the sim clock, so 8x speed really does execute 8x the work
// per real second - the honest cost model the stress test must measure.
// Acts as the root sim driver in the scaffold (sole ConsumeSubSteps caller;
// also pumps the uplink queue). M0 moves that role to a dedicated driver.
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
