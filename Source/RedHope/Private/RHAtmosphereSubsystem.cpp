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
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
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
// The night-glow dial the director asked for: build your own lighting, then
// turn the assist off and take the credit. Rides MPC_Atmosphere.GlowScale,
// which M_RH_Master multiplies into its MASKED emissive only.
static float GRHGlowScale = 1.0f;
static FAutoConsoleVariableRef CVarRHGlowScale(TEXT("rh.Glow"), GRHGlowScale,
	TEXT("Machine-glow assist: 1 = as authored, 0 = off (player-built lights only). Default 1."));
static float GRHExposure = 1.0f;
static FAutoConsoleVariableRef CVarRHExposure(TEXT("RH.Grade.Exposure"), GRHExposure,
	TEXT("Locked exposure brightness (min==max, no auto-swing). Default 1.0."));
// The finishing trio the grade lacked (2026-08-18). The session's measured
// doctrine: at 29-216 m the image is carried by silhouette, value contrast,
// emissive points and lighting - these three act on exactly those. Bloom makes
// the night lamps and readouts GLOW instead of merely existing; AO seats
// furniture and hulls against their floors; the film curve gives the flat
// linear-ish frame real blacks and a gentle shoulder. All live-tunable, all
// zeroable for an honest A/B.
static float GRHGradeBloom = 0.55f;
static FAutoConsoleVariableRef CVarRHGradeBloom(TEXT("RH.Grade.Bloom"), GRHGradeBloom,
	TEXT("Bloom intensity for emissives and lamps. 0 disables the override. Default 0.55."));
static float GRHGradeAO = 0.7f;
static FAutoConsoleVariableRef CVarRHGradeAO(TEXT("RH.Grade.AO"), GRHGradeAO,
	TEXT("Ambient-occlusion intensity (0..1); radius rides it. 0 disables the override. Default 0.7."));
static float GRHGradeFilm = 1.0f;
static FAutoConsoleVariableRef CVarRHGradeFilm(TEXT("RH.Grade.Film"), GRHGradeFilm,
	TEXT("Filmic contrast blend: 0 = engine default curve, 1 = the Mars curve (slope 0.94 toe 0.60 shoulder 0.30). Default 1."));

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
	// The collection declares DustAmount, not Dust: this push had been writing
	// to a parameter that does not exist, so it went nowhere.
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("DustAmount"), 1.f - DustFactor);
	// The machine-glow assist. 1 = as authored; 0 hands the night entirely to
	// lights the player built (hab ceiling lamps and Floodmasts are real point
	// lights and are NOT scaled by this).
	UKismetMaterialLibrary::SetScalarParameterValue(World, Collection, TEXT("GlowScale"),
		FMath::Max(0.f, GRHGlowScale));

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

	// The Martian sky dome: prefer an existing SkyAtmosphere; else spawn one.
	// Tinted so the horizon reads dusty butterscotch (the reference panorama's
	// airborne-dust look, NOT Earth blue) fading darker overhead; the sun disc
	// tracks the sol clock via the RH.Sun directional light.
	if (TActorIterator<ASkyAtmosphere> It(World); It)
	{
		SkyDome = *It;
	}
	if (!SkyDome)
	{
		SkyDome = World->SpawnActor<ASkyAtmosphere>();
#if WITH_EDITOR
		if (SkyDome) { SkyDome->SetActorLabel(TEXT("RH_MarsSky")); }
#endif
	}
	if (SkyDome)
	{
		if (USkyAtmosphereComponent* Sky = SkyDome->GetComponent())
		{
			// Warm-dominant scattering = ochre sky; low Rayleigh + high Mie =
			// the dusty, low-contrast Martian haze rather than a crisp blue dome.
			Sky->SetRayleighScattering(FLinearColor(0.65f, 0.32f, 0.12f));
			Sky->SetRayleighScatteringScale(0.06f);
			Sky->SetMieScatteringScale(0.02f);
			Sky->SetMieAbsorptionScale(0.006f);
			Sky->SetMieAnisotropy(0.65f);
			Sky->SetGroundAlbedo(FColor(120, 74, 46)); // rust bounce
		}
	}
	if (SunActor)
	{
		if (UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(SunActor->GetLightComponent()))
		{
			Sun->SetAtmosphereSunLight(true); // the disc + sky tint ride the sol clock
		}
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
		// Finishing trio (see the cvar block for why these three).
		if (GRHGradeBloom > 0.f)
		{
			PP.bOverride_BloomIntensity = true;
			PP.BloomIntensity = GRHGradeBloom;
			PP.bOverride_BloomThreshold = true;
			// Above 1.0 only genuinely bright pixels bloom - lamps, glow,
			// readouts - and the sunlit regolith stays crisp.
			PP.BloomThreshold = 1.1f;
		}
		if (GRHGradeAO > 0.f)
		{
			PP.bOverride_AmbientOcclusionIntensity = true;
			PP.AmbientOcclusionIntensity = FMath::Clamp(GRHGradeAO, 0.f, 1.f);
			PP.bOverride_AmbientOcclusionRadius = true;
			PP.AmbientOcclusionRadius = 120.f;
			PP.bOverride_AmbientOcclusionPower = true;
			PP.AmbientOcclusionPower = 1.6f;
		}
		if (GRHGradeFilm > 0.f)
		{
			const float T = FMath::Clamp(GRHGradeFilm, 0.f, 1.f);
			PP.bOverride_FilmSlope = true;
			PP.FilmSlope = FMath::Lerp(0.88f, 0.94f, T);   // steeper mids = presence
			PP.bOverride_FilmToe = true;
			PP.FilmToe = FMath::Lerp(0.55f, 0.60f, T);     // deeper blacks
			PP.bOverride_FilmShoulder = true;
			PP.FilmShoulder = FMath::Lerp(0.26f, 0.30f, T); // gentler highlight rolloff
		}
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
		// Vignette for a lensed frame (bloom is owned by RH.Grade.Bloom above -
		// this block used to hardcode 0.35 AFTER the cvar block wrote 0.55,
		// silently winning; one owner now).
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
