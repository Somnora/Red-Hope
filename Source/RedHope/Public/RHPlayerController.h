#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RHPlayerController.generated.h"

// Strategic controller. Scaffold scope: show cursor, own the speed hotkeys
// path later. Enhanced Input wiring (IMC_Strategy) lands with the M0 UI pass;
// until then the RH.* console commands drive speed and spawning.
UCLASS()
class REDHOPE_API ARHPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARHPlayerController();
};
