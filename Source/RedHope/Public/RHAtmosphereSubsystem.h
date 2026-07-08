#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHAtmosphereSubsystem.generated.h"

class UMaterialParameterCollection;
class ADirectionalLight;
class APostProcessVolume;
class AExponentialHeightFog;
class ASkyLight;
class ASkyAtmosphere;

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
	UPROPERTY() TObjectPtr<ADirectionalLight> SunActor;
	// Authored sun intensity, captured at discovery; storms scale from this.
	float SunBaseIntensity = 10.f;

	// The Mars LOOK pass (M2 Gate D+, director-scheduled). Presentation-only,
	// spawned in code so it works with the editor closed - a cinematic grade
	// over the gray-box world (respects the gray-box GEOMETRY rule; this is
	// lighting/atmosphere/colour, not asset modelling). All three are found-or-
	// spawned once, then driven each Tick from the sol clock + dust + camera
	// depth. Deleting this subsystem restores the raw look; the sim never knows.
	UPROPERTY() TObjectPtr<APostProcessVolume> GradeVolume;
	UPROPERTY() TObjectPtr<AExponentialHeightFog> DustFog;
	UPROPERTY() TObjectPtr<ASkyLight> FillSky;
	// The Martian sky dome (Session 44+ scenery pass): a physically-driven sky
	// so tilting the camera up shows a butterscotch horizon fading over the
	// mountain ring, with the animated sun disc + a naturally dark night.
	UPROPERTY() TObjectPtr<ASkyAtmosphere> SkyDome;
	bool bLookInit = false;
	void EnsureLookRig();          // find-or-spawn the grade / fog / fill
	void DriveLook(float DustFactor, float SolFraction, bool bUnderground);

	static constexpr const TCHAR* CollectionPath = TEXT("/Game/RedHope/Sky/MPC_Atmosphere.MPC_Atmosphere");
};
