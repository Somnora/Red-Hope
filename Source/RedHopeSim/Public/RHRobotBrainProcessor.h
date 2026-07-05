#pragma once

#include "CoreMinimal.h"
#include "MassProcessor.h"
#include "RHRobotBrainProcessor.generated.h"

// Deterministic StateTree ticker (M1-a Gate B). MassAI's stock scheduling is
// signal/frame-driven - decisions would land on render-frame boundaries and
// acceleration could change outcomes. This processor instead ticks each
// robot's tree once per fixed sim sub-step, so the decision layer obeys the
// same determinism invariant as everything else. Runs only on entities
// carrying FMassStateTreeInstanceFragment; the legacy switch processor
// excludes them - the two brains are A/B-testable on the same driver.
UCLASS()
class REDHOPESIM_API URHRobotBrainProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	URHRobotBrainProcessor();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& EntityManager) override;
	virtual void Execute(FMassEntityManager& EntityManager, FMassExecutionContext& Context) override;

private:
	FMassEntityQuery EntityQuery;
};
