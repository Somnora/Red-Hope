#include "RHAtmosphereSubsystem.h"
#include "RedHope.h"
#include "RHGameState.h"
#include "RHSimClockSubsystem.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

static float GRHHabitabilityOverride = -1.f;
static FAutoConsoleVariableRef CVarRHHabitability(
	TEXT("RH.Habitability"),
	GRHHabitabilityOverride,
	TEXT("Scrub the atmosphere dial by hand: 0..1 overrides sim habitability; negative returns control to sim/GameState."));

void URHAtmosphereSubsystem::Initialize(FSubsystemCollectionBase& Collection_)
{
	Super::Initialize(Collection_);
	Collection = LoadObject<UMaterialParameterCollection>(nullptr, CollectionPath);
	if (!Collection)
	{
		UE_LOG(LogRedHope, Warning, TEXT("MPC_Atmosphere not found at %s (expected until the content pass creates it)"), CollectionPath);
	}
}

void URHAtmosphereSubsystem::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!World || !Collection)
	{
		return;
	}

	float Habitability = 0.15f;
	if (GRHHabitabilityOverride >= 0.f)
	{
		Habitability = FMath::Clamp(GRHHabitabilityOverride, 0.f, 1.f);
	}
	else if (const ARHGameState* GameState = World->GetGameState<ARHGameState>())
	{
		Habitability = GameState->Habitability;
	}

	float SolFraction = 0.35f;
	if (const URHSimClockSubsystem* Clock = World->GetSubsystem<URHSimClockSubsystem>())
	{
		SolFraction = Clock->GetSolFraction();
	}

	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("Habitability"), Habitability);
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("TimeOfSol"), SolFraction);

	if (!SunActor)
	{
		for (TActorIterator<ADirectionalLight> It(World); It; ++It)
		{
			if (It->ActorHasTag(FName("RH.Sun")))
			{
				SunActor = *It;
				break;
			}
		}
	}
	if (SunActor)
	{
		// Sunrise 06:00 (frac 0.25) = horizon; noon (0.5) = max elevation.
		const float SunAngleDeg = (SolFraction - 0.25f) * 360.f;
		SunActor->SetActorRotation(FRotator(-SunAngleDeg, -45.f, 0.f));
	}
}
