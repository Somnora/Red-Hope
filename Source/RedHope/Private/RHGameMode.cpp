#include "RHGameMode.h"
#include "RHGameState.h"
#include "RHPlayerController.h"
#include "RHStrategyPawn.h"

ARHGameMode::ARHGameMode()
{
	GameStateClass = ARHGameState::StaticClass();
	PlayerControllerClass = ARHPlayerController::StaticClass();
	DefaultPawnClass = ARHStrategyPawn::StaticClass();
}
