#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHAtmosphereSubsystem.generated.h"

class UMaterialParameterCollection;

// Presentation-side driver of the atmosphere dial. Reads sim state
// (habitability, sol clock) and writes MPC_Atmosphere + rotates the sun.
// The sim never knows this exists; delete it and the colony still runs.
// M0 adds CT_AtmosphereDial curve evaluation between read and write.
UCLASS()
class REDHOPE_API URHAtmosphereSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHAtmosphereSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

private:
	UPROPERTY() TObjectPtr<UMaterialParameterCollection> Collection;
	// Sun = first directional light tagged RH.Sun in the level.
	UPROPERTY() TObjectPtr<AActor> SunActor;

	static constexpr const TCHAR* CollectionPath = TEXT("/Game/RedHope/Sky/MPC_Atmosphere.MPC_Atmosphere");
};
