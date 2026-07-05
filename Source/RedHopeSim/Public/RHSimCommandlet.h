#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RHSimCommandlet.generated.h"

// Headless balance runner: -run=RHSim -sols=N -scenario=X -> CSV metrics.
// Stub in the scaffold; the M1 era-integrator work makes it real. Its
// existence keeps the sim module honest about presentation independence.
UCLASS()
class REDHOPESIM_API URHSimCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
