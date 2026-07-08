#include "RHAtmosphereSubsystem.h"
#include "RedHope.h"
#include "RHGameState.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHSimClockSubsystem.h"
#include "RHSimWorldSubsystem.h"
#include "EngineUtils.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

static float GRHHabitabilityOverride = -1.f;
static FAutoConsoleVariableRef CVarRHHabitability(
	TEXT("RH.Habitability"),
	GRHHabitabilityOverride,
	TEXT("Scrub the atmosphere dial by hand: 0..1 overrides sim habitability; negative returns control to sim/GameState."));

// Live taste knobs for the Mars look pass (the hot-tunable surface until look
// tokens move to config). Defaults are the brief's palette: ochre/butterscotch
// sky, warm surface light, exposure LOCKED (the director's "exposure should be
// the same throughout" note - no auto-exposure swing between surface and pit).
static float GRHGradeWarmth = 8200.f;
static FAutoConsoleVariableRef CVarRHGradeWarmth(TEXT("RH.Grade.Warmth"), GRHGradeWarmth,
	TEXT("White-balance temperature for the Mars grade (higher = warmer/ochre). Default 8200."));
static float GRHGradeSaturation = 1.06f;
static FAutoConsoleVariableRef CVarRHGradeSaturation(TEXT("RH.Grade.Saturation"), GRHGradeSaturation,
	TEXT("Global saturation for the Mars grade. Default 1.06."));
static float GRHFogDensity = 0.018f;
static FAutoConsoleVariableRef CVarRHFogDensity(TEXT("RH.Grade.FogDensity"), GRHFogDensity,
	TEXT("Clear-sky dust-haze density; storms thicken it. Default 0.018."));
static float GRHExposure = 1.0f;
static FAutoConsoleVariableRef CVarRHExposure(TEXT("RH.Grade.Exposure"), GRHExposure,
	TEXT("Locked exposure brightness (min==max, no auto-swing). Default 1.0."));

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

	// Storm presentation (M1-c): Dust 0 = clear, 1 = full storm. The MPC
	// scalar is wired by the content pass; the sun dimming below works today.
	float DustFactor = 1.f;
	if (const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>())
	{
		DustFactor = (float)Sim->GetDustFactorNow();
	}
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("Habitability"), Habitability);
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("TimeOfSol"), SolFraction);
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("Dust"), 1.f - DustFactor);

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
		if (SunActor && SunActor->GetLightComponent())
		{
			SunBaseIntensity = SunActor->GetLightComponent()->Intensity;
		}
	}
	if (SunActor)
	{
		// Sunrise 06:00 (frac 0.25) = horizon; noon (0.5) = max elevation.
		const float SunAngleDeg = (SolFraction - 0.25f) * 360.f;
		SunActor->SetActorRotation(FRotator(-SunAngleDeg, -45.f, 0.f));
		if (ULightComponent* Light = SunActor->GetLightComponent())
		{
			// A storm mutes the disc itself - light drops with generation, so
			// the world darkens the way the readout does. Warm the disc toward
			// the brief's butterscotch sun (subtle - the grade does most of it).
			Light->SetIntensity(SunBaseIntensity * (0.25f + 0.75f * DustFactor));
			Light->SetLightColor(FLinearColor(1.0f, 0.86f, 0.66f));
		}
	}

	// The Mars look pass: found-or-spawned once, driven every frame.
	EnsureLookRig();
	bool bUnderground = false;
	if (const URHColonyVisualizerSubsystem* Colony = World->GetSubsystem<URHColonyVisualizerSubsystem>())
	{
		bUnderground = Colony->GetViewLevel() < 0;
	}
	DriveLook(DustFactor, SolFraction, bUnderground);
}

void URHAtmosphereSubsystem::EnsureLookRig()
{
	UWorld* World = GetWorld();
	if (bLookInit || !World)
	{
		return;
	}
	bLookInit = true;

	// Grade: an unbound, high-priority post-process volume covering the whole
	// scene. Prefer an existing one tagged RH.Grade; else spawn.
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->ActorHasTag(FName("RH.Grade"))) { GradeVolume = *It; break; }
	}
	if (!GradeVolume)
	{
		GradeVolume = World->SpawnActor<APostProcessVolume>();
		if (GradeVolume)
		{
			GradeVolume->bUnbound = true;
			GradeVolume->Priority = 100.f;
			GradeVolume->Tags.Add(FName("RH.Grade"));
#if WITH_EDITOR
			GradeVolume->SetActorLabel(TEXT("RH_MarsGrade"));
#endif
		}
	}

	// Dust haze: an exponential height fog tinted butterscotch. Prefer existing.
	if (TActorIterator<AExponentialHeightFog> It(World); It)
	{
		DustFog = *It; // any height fog is ours to drive
	}
	if (!DustFog)
	{
		DustFog = World->SpawnActor<AExponentialHeightFog>();
#if WITH_EDITOR
		if (DustFog) { DustFog->SetActorLabel(TEXT("RH_DustHaze")); }
#endif
	}

	// Cool ambient fill for the shadow side (and the pit). Tune an existing
	// SkyLight if the level has one; a missing one is fine (the grade carries).
	if (TActorIterator<ASkyLight> It(World); It)
	{
		FillSky = *It;
	}
}

void URHAtmosphereSubsystem::DriveLook(float DustFactor, float SolFraction, bool bUnderground)
{
	const float Dust = 1.f - FMath::Clamp(DustFactor, 0.f, 1.f); // 0 clear .. 1 full storm
	// Daylight strength for ambient (night = no sky contribution).
	const float Day = FMath::Clamp(FMath::Sin(SolFraction * PI), 0.f, 1.f);

	// The crisis screen cue (M4 Gate D): an active crisis reads on the LENS, not
	// just the deck - a Malfunction desaturates and grains the frame (systems
	// failing); Environmental strain thickens the haze. Absolute values are set
	// every frame, so recovery reverts the look automatically.
	bool bMalfunction = false, bEnvironmental = false;
	if (const URHSimWorldSubsystem* Sim = GetWorld() ? GetWorld()->GetSubsystem<URHSimWorldSubsystem>() : nullptr)
	{
		bMalfunction = Sim->GetActiveCrisis() == URHSimWorldSubsystem::ERHCrisis::Malfunction;
		bEnvironmental = Sim->GetActiveCrisis() == URHSimWorldSubsystem::ERHCrisis::Environmental;
	}

	if (GradeVolume)
	{
		FPostProcessSettings& PP = GradeVolume->Settings;
		// Exposure LOCKED (director's note): min==max pins auto-exposure so the
		// image never re-brightens descending into the pit or at dusk.
		PP.bOverride_AutoExposureMinBrightness = true;
		PP.bOverride_AutoExposureMaxBrightness = true;
		PP.AutoExposureMinBrightness = GRHExposure;
		PP.AutoExposureMaxBrightness = GRHExposure;
		// White balance toward warm ochre; underground pulls slightly cooler so
		// the pit reads as sheltered interior without CHANGING exposure.
		PP.bOverride_WhiteTemp = true;
		PP.WhiteTemp = bUnderground ? GRHGradeWarmth - 900.f : GRHGradeWarmth;
		PP.bOverride_ColorSaturation = true;
		// A Malfunction crisis drains the color from the frame - the world reads
		// as "systems failing" without a single graphic image.
		const float Sat = bMalfunction ? GRHGradeSaturation * 0.72f : GRHGradeSaturation;
		PP.ColorSaturation = FVector4(Sat, Sat, Sat, 1.f);
		PP.bOverride_ColorContrast = true;
		PP.ColorContrast = FVector4(1.05f, 1.05f, 1.05f, 1.f);
		// A warm ochre gain on the surface; a cooler slate gain underground -
		// "color = life earned" reads warmest where the crew actually lives.
		PP.bOverride_ColorGain = true;
		PP.ColorGain = bUnderground
			? FVector4(0.92f, 0.95f, 1.02f, 1.f)
			: FVector4(1.06f, 0.99f, 0.86f, 1.f);
		// Bloom + vignette for a lensed, cinematic frame (subtle).
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 0.35f;
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = 0.42f;
		// A dust storm crushes the sky into a hazy sepia - lift the shadows and
		// desaturate slightly so the world reads as "socked in", not just dark.
		PP.bOverride_FilmGrainIntensity = true;
		PP.FilmGrainIntensity = 0.08f + 0.25f * Dust + (bMalfunction ? 0.3f : 0.f);
	}

	if (DustFog)
	{
		if (UExponentialHeightFogComponent* Fog = DustFog->GetComponent())
		{
			// Haze thickens with the storm; butterscotch inscatter by day, dimmer
			// at night. Base density is the ever-present thin Mars dust.
			// Environmental strain thickens the haze - the crisis you can SEE roll in.
			Fog->SetFogDensity(GRHFogDensity + 0.16f * Dust + (bEnvironmental ? 0.08f : 0.f));
			Fog->SetFogHeightFalloff(0.2f);
			const FLinearColor Haze = FLinearColor(0.62f, 0.42f, 0.26f) * (0.25f + 0.75f * Day);
			Fog->SetFogInscatteringColor(Haze);
		}
	}

	if (FillSky)
	{
		if (USkyLightComponent* Sky = FillSky->GetLightComponent())
		{
			// Cool, low ambient fill so shadows aren't black - lifts at night a
			// touch (starlight/earthlight fiction) and dims under a storm.
			const float Fill = 0.6f * (0.35f + 0.65f * Day) * (0.4f + 0.6f * FMath::Clamp(DustFactor, 0.f, 1.f));
			Sky->SetIntensity(Fill);
			Sky->SetLightColor(FLinearColor(0.55f, 0.62f, 0.8f));
		}
	}
}
