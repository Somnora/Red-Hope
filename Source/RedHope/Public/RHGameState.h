#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "RHGameState.generated.h"

UCLASS()
class REDHOPE_API ARHGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	// The Planet Is the Progress Bar: 0 = raw Mars, 1 = terraformed. Sim-owned
	// in M1+ (terraforming systems); editable here so the atmosphere dial can
	// be scrubbed by hand from day one of gray-boxing.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RedHope", meta = (ClampMin = "0", ClampMax = "1"))
	float Habitability = 0.15f;
};
