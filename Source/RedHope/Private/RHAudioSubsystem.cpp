#include "RHAudioSubsystem.h"
#include "RedHope.h"
#include "RHColonyVisualizerSubsystem.h"
#include "RHElevatorLayout.h"
#include "RHSimWorldSubsystem.h"
#include "Data/RHRows.h"
#include "Components/AudioComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

static TAutoConsoleVariable<float> CVarAudioMaster(TEXT("rh.AudioMaster"), 1.0f,
	TEXT("Master volume for the colony's sound (0-1)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarAudioMusic(TEXT("rh.AudioMusic"), 0.55f,
	TEXT("Music volume (0-1)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarAudioAmbience(TEXT("rh.AudioAmbience"), 0.8f,
	TEXT("Ambience beds volume: wind, habitat, storm (0-1)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarAudioSFX(TEXT("rh.AudioSFX"), 0.9f,
	TEXT("World sound volume: machines, elevator (0-1)."), ECVF_Default);
static TAutoConsoleVariable<float> CVarAudioUI(TEXT("rh.AudioUI"), 0.7f,
	TEXT("UI and event cue volume (0-1)."), ECVF_Default);
static TAutoConsoleVariable<int32> CVarAudioLog(TEXT("rh.AudioLog"), 0,
	TEXT("1 = log every cue, door slide and music start, plus a status line every 10 s (verification with rh.AudioMaster 0: the logic runs, the speakers stay silent)."),
	ECVF_Default);

namespace
{
	float Vol(const TAutoConsoleVariable<float>& Cat)
	{
		return FMath::Clamp(CVarAudioMaster.GetValueOnGameThread(), 0.f, 1.f)
			* FMath::Clamp(Cat.GetValueOnGameThread(), 0.f, 1.f);
	}

	// Machine loop per building type (the rest stay quiet).
	const TMap<FName, const TCHAR*>& MachineMap()
	{
		static const TMap<FName, const TCHAR*> Map = {
			{ FName("IceDrill"),          TEXT("mach_drill") },
			{ FName("Borer"),             TEXT("mach_borer") },
			{ FName("Forge"),             TEXT("mach_forge") },
			{ FName("Electrolyzer"),      TEXT("mach_electrolyzer") },
			{ FName("AirFilter"),         TEXT("mach_fan") },
			{ FName("HumidityRegulator"), TEXT("mach_fan") },
			{ FName("WaterPlant"),        TEXT("mach_pump") },
			{ FName("BatteryBank"),       TEXT("mach_hum") },
			{ FName("ComputeModule"),     TEXT("mach_computer") },
			{ FName("Lander"),            TEXT("mach_lander") },
		};
		return Map;
	}

	// Cue -> (category, asset, minimum seconds between plays).
	struct FCueDef { const TCHAR* Category; const TCHAR* Name; double MinGap; };
	const TMap<FName, FCueDef>& CueMap()
	{
		static const TMap<FName, FCueDef> Map = {
			{ FName("Click"),     { TEXT("UI"),     TEXT("ui_click"),       0.05 } },
			{ FName("Toggle"),    { TEXT("UI"),     TEXT("ui_toggle"),      0.05 } },
			{ FName("Confirm"),   { TEXT("UI"),     TEXT("ui_confirm"),     0.15 } },
			{ FName("Error"),     { TEXT("UI"),     TEXT("ui_error"),       0.25 } },
			{ FName("CardOpen"),  { TEXT("UI"),     TEXT("ui_card_open"),   0.8 } },
			{ FName("CardClose"), { TEXT("UI"),     TEXT("ui_card_close"),  0.2 } },
			{ FName("Notify"),    { TEXT("Events"), TEXT("evt_notify"),     1.0 } },
			{ FName("Milestone"), { TEXT("Events"), TEXT("evt_milestone"),  3.0 } },
			{ FName("Alert"),     { TEXT("Events"), TEXT("evt_alert"),      4.0 } },
			{ FName("BuildDone"), { TEXT("Events"), TEXT("evt_build_done"), 1.0 } },
			{ FName("Arrival"),   { TEXT("Events"), TEXT("evt_arrival"),    6.0 } },
		};
		return Map;
	}
}

USoundBase* URHAudioSubsystem::LoadSound(const TCHAR* Category, const TCHAR* Name) const
{
	const FString Path = FString::Printf(TEXT("/Game/RedHope/Audio/%s/A_%s.A_%s"), Category, Name, Name);
	return LoadObject<USoundBase>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

USoundBase* URHAudioSubsystem::UIClickSound()
{
	static TWeakObjectPtr<USoundBase> Cached;
	if (!Cached.IsValid())
	{
		if (USoundBase* S = LoadObject<USoundBase>(nullptr,
			TEXT("/Game/RedHope/Audio/UI/A_ui_click.A_ui_click"), nullptr, LOAD_NoWarn | LOAD_Quiet))
		{
			S->AddToRoot(); // Slate holds it by raw pointer for the session
			Cached = S;
		}
	}
	return Cached.Get();
}

UAudioComponent* URHAudioSubsystem::MakeBed(USoundBase* Sound)
{
	if (!Sound)
	{
		return nullptr;
	}
	UAudioComponent* C = UGameplayStatics::CreateSound2D(GetWorld(), Sound, 0.001f, 1.f, 0.f, nullptr, false, false);
	if (C)
	{
		C->bIsUISound = false;
		C->Play(FMath::FRandRange(0.f, 20.f)); // beds start mid-loop: no two boots sound identical
	}
	return C;
}

void URHAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	for (const auto& Pair : CueMap())
	{
		if (USoundBase* S = LoadSound(Pair.Value.Category, Pair.Value.Name))
		{
			Cues.Add(Pair.Key, S);
		}
	}
	for (const auto& Pair : MachineMap())
	{
		if (USoundBase* S = LoadSound(TEXT("Machines"), Pair.Value))
		{
			MachineSounds.Add(Pair.Key, S);
		}
	}
	DoorOpen = LoadSound(TEXT("Elevator"), TEXT("elev_door_open"));
	DoorClose = LoadSound(TEXT("Elevator"), TEXT("elev_door_close"));
	Chime = LoadSound(TEXT("Elevator"), TEXT("elev_chime"));
	// Music: A_mus_p<phase>_<a..f>, whatever exists.
	TArray<TObjectPtr<USoundBase>>* Lists[3] = { &MusicAutomation, &MusicHabitation, &MusicSovereignty };
	for (int32 P = 0; P < 3; ++P)
	{
		for (TCHAR Letter = TEXT('a'); Letter <= TEXT('f'); ++Letter)
		{
			const FString Name = FString::Printf(TEXT("mus_p%d_%c"), P + 1, Letter);
			if (USoundBase* S = LoadSound(TEXT("Music"), *Name))
			{
				Lists[P]->Add(S);
			}
		}
	}
	Wind = MakeBed(LoadSound(TEXT("Ambience"), TEXT("amb_surface_wind")));
	Habitat = MakeBed(LoadSound(TEXT("Ambience"), TEXT("amb_habitat")));
	Storm = MakeBed(LoadSound(TEXT("Ambience"), TEXT("amb_storm")));
	NextMusicAt = InWorld.GetTimeSeconds() + 8.0;
	UE_LOG(LogRedHope, Display, TEXT("Audio: %d cues, %d machine loops, beds %s/%s/%s, music %d/%d/%d tracks"),
		Cues.Num(), MachineSounds.Num(), Wind ? TEXT("wind") : TEXT("-"), Habitat ? TEXT("habitat") : TEXT("-"),
		Storm ? TEXT("storm") : TEXT("-"), MusicAutomation.Num(), MusicHabitation.Num(), MusicSovereignty.Num());
}

void URHAudioSubsystem::Deinitialize()
{
	for (auto& Pair : MachineLoops)
	{
		if (UAudioComponent* C = Pair.Value.Get())
		{
			C->Stop();
		}
	}
	MachineLoops.Empty();
	Super::Deinitialize();
}

void URHAudioSubsystem::Cue(const UWorld* World, FName CueName)
{
	if (URHAudioSubsystem* Self = World ? World->GetSubsystem<URHAudioSubsystem>() : nullptr)
	{
		Self->PlayCue(CueName);
	}
}

void URHAudioSubsystem::PlayCue(FName CueName)
{
	const FCueDef* Def = CueMap().Find(CueName);
	USoundBase* S = Cues.FindRef(CueName);
	UWorld* World = GetWorld();
	if (!Def || !S || !World)
	{
		return;
	}
	const double Now = World->GetRealTimeSeconds();
	if (const double* Last = LastCueAt.Find(CueName))
	{
		if (Now - *Last < Def->MinGap)
		{
			return;
		}
	}
	LastCueAt.Add(CueName, Now);
	UGameplayStatics::PlaySound2D(World, S, FMath::Max(Vol(CVarAudioUI), 0.001f));
	if (CVarAudioLog.GetValueOnGameThread())
	{
		UE_LOG(LogRedHope, Display, TEXT("Audio: cue %s (%s)"), *CueName.ToString(), *S->GetName());
	}
}

void URHAudioSubsystem::PlayAt(USoundBase* Sound, const FVector& WorldCm, float Volume, float InnerCm, float FalloffCm)
{
	if (!Sound)
	{
		return;
	}
	if (UAudioComponent* C = UGameplayStatics::SpawnSoundAtLocation(GetWorld(), Sound, WorldCm, FRotator::ZeroRotator,
		Volume, 1.f, 0.f, nullptr, nullptr, true))
	{
		C->bOverrideAttenuation = true;
		C->AttenuationOverrides.bAttenuate = true;
		C->AttenuationOverrides.bSpatialize = true;
		C->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
		C->AttenuationOverrides.AttenuationShapeExtents = FVector(InnerCm);
		C->AttenuationOverrides.FalloffDistance = FalloffCm;
	}
}

void URHAudioSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World ? World->GetSubsystem<URHSimWorldSubsystem>() : nullptr;
	const URHColonyVisualizerSubsystem* Colony = World ? World->GetSubsystem<URHColonyVisualizerSubsystem>() : nullptr;
	if (!Sim || !Colony)
	{
		return;
	}
	const int32 ViewLevel = Colony->IsSectionMode() ? Colony->GetSectionFocus() : Colony->GetViewLevel(); // D8: the focus floor's ambience
	const FRHEventRow* Event = Sim->GetActiveEvent();
	const bool bStorm = Event && Event->Type == FName("DustStorm");
	TickAmbience(DeltaTime, ViewLevel < 0, bStorm);
	TickMachines(DeltaTime, ViewLevel, Sim->GetFloorHeightCm());
	FVector Head = Sim->GetShaftHeadCm();
	// D2: with more than one shaft the elevator you hear is the head nearest the camera (among the shafts that reach the viewed floor).
	if (Sim->GetShaftCount() > 1)
	{
		const APlayerController* PC = World->GetFirstPlayerController();
		const FVector Cam = (PC && PC->PlayerCameraManager) ? PC->PlayerCameraManager->GetCameraLocation() : Head;
		double BestCm = TNumericLimits<double>::Max();
		for (const FRHShaft& Sh : Sim->GetShafts())
		{
			if (ViewLevel < 0 && !Sim->ShaftReaches(Sh.Id, ViewLevel))
			{
				continue;
			}
			const double D = FVector::DistXY(Cam, Sh.HeadCm);
			if (D < BestCm) { BestCm = D; Head = Sh.HeadCm; }
		}
	}
	TickElevator(FVector(Head.X + RHElevator::DoorPlaneX, Head.Y, ViewLevel * Sim->GetFloorHeightCm() + 120.0));
	TickEvents();
	// Phase for the score: Automation until the crew land, Habitation with
	// people, Sovereignty once the Earth/rival layer wakes (it stays inert,
	// tension 0 and identity 0, for a lonely colony).
	const int32 Phase = (Sim->GetEarthTension() > 0.0 || FMath::Abs(Sim->GetIdentityAxis()) > 0.0) ? 2
		: (Sim->GetColonists().Num() > 0 ? 1 : 0);
	TickMusic(Phase);
	if (CVarAudioLog.GetValueOnGameThread() && World->GetTimeSeconds() >= NextStatusLog)
	{
		NextStatusLog = World->GetTimeSeconds() + 10.0;
		int32 Loud = 0;
		FString Which;
		for (const auto& Pair : MachineVolume)
		{
			if (Pair.Value > 0.f)
			{
				++Loud;
				for (const FRHBuildingInstance& B : Sim->GetBuildings())
				{
					if (B.Id == Pair.Key) { Which += FString::Printf(TEXT(" %s=%.2f"), *B.DefName.ToString(), Pair.Value); break; }
				}
			}
		}
		UE_LOG(LogRedHope, Display, TEXT("Audio status: view %d storm %d | beds wind %.2f habitat %.2f storm %.2f (playing %d/%d/%d) | machines %d placed, %d audible:%s | music phase %d %s"),
			ViewLevel, bStorm ? 1 : 0, WindVol, HabitatVol, StormVol,
			Wind && Wind->IsPlaying(), Habitat && Habitat->IsPlaying(), Storm && Storm->IsPlaying(),
			MachineLoops.Num(), Loud, *Which, Phase, Music && Music->IsPlaying() ? TEXT("playing") : TEXT("silent"));
	}
}

void URHAudioSubsystem::TickAmbience(float DeltaTime, bool bUnderground, bool bStorm)
{
	// Thin air outside, rich inside: underground the wind is a far-off hush
	// through the rock and the habitat bed carries the room.
	const float Amb = Vol(CVarAudioAmbience);
	WindVol = FMath::FInterpTo(WindVol, bUnderground ? 0.12f : 1.f, DeltaTime, 1.5f);
	HabitatVol = FMath::FInterpTo(HabitatVol, bUnderground ? 1.f : 0.f, DeltaTime, 1.5f);
	StormVol = FMath::FInterpTo(StormVol, bStorm ? (bUnderground ? 0.12f : 1.f) : 0.f, DeltaTime, 0.6f);
	if (Wind) { Wind->SetVolumeMultiplier(FMath::Max(WindVol * Amb, 0.001f)); }
	if (Habitat) { Habitat->SetVolumeMultiplier(FMath::Max(HabitatVol * Amb, 0.001f)); }
	if (Storm) { Storm->SetVolumeMultiplier(FMath::Max(StormVol * Amb, 0.001f)); }
}

void URHAudioSubsystem::TickMachines(float DeltaTime, int32 ViewLevel, double FloorH)
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
	const double Now = World->GetTimeSeconds();
	const float Sfx = Vol(CVarAudioSFX);
	if (Now >= NextMachineSync)
	{
		NextMachineSync = Now + 0.25;
		TSet<int32> Alive;
		for (const FRHBuildingInstance& B : Sim->GetBuildings())
		{
			USoundBase* Sound = MachineSounds.FindRef(B.DefName);
			if (!Sound)
			{
				continue;
			}
			Alive.Add(B.Id);
			TObjectPtr<UAudioComponent>& C = MachineLoops.FindOrAdd(B.Id);
			if (!C)
			{
				// Placed on the building, audible as the camera comes down:
				// full within 15 m of the listener, gone by ~75 m (the default
				// strategy height is ~215 m, so the overview stays quiet).
				C = UGameplayStatics::SpawnSoundAtLocation(World, Sound, FVector(B.LocationCm.X, B.LocationCm.Y, B.Level * FloorH + 150.0),
					FRotator::ZeroRotator, 0.001f, FMath::FRandRange(0.96f, 1.04f), FMath::FRandRange(0.f, 4.f), nullptr, nullptr, false);
				if (C)
				{
					C->bOverrideAttenuation = true;
					C->AttenuationOverrides.bAttenuate = true;
					C->AttenuationOverrides.bSpatialize = true;
					C->AttenuationOverrides.AttenuationShape = EAttenuationShape::Sphere;
					C->AttenuationOverrides.AttenuationShapeExtents = FVector(1500.f);
					C->AttenuationOverrides.FalloffDistance = 6000.f;
				}
			}
			// Working = full, idle = a murmur, off / shed / building / another
			// floor = silent.
			const bool bRunning = B.bPowered && !B.bManualOff && !B.bUnderConstruction;
			// D5a: a day-shift machine resting at night murmurs - its batch is paused.
			const bool bBatch = (B.BatchRemaining_h > 0.0 || !B.ActiveRecipe.IsNone()) && !(B.bDayShift && !Sim->IsDaylightNow());
			float Target = !bRunning || B.Level != ViewLevel ? 0.f : (bBatch || B.DefName == FName("AirFilter")
				|| B.DefName == FName("BatteryBank") || B.DefName == FName("ComputeModule") || B.DefName == FName("Lander")
				|| B.DefName == FName("HumidityRegulator") ? 1.f : 0.35f);
			MachineVolume.FindOrAdd(B.Id) = Target;
		}
		for (auto It = MachineLoops.CreateIterator(); It; ++It)
		{
			if (!Alive.Contains(It.Key()))
			{
				if (UAudioComponent* C = It.Value().Get())
				{
					C->Stop();
					C->DestroyComponent();
				}
				MachineVolume.Remove(It.Key());
				It.RemoveCurrent();
			}
		}
	}
	for (auto& Pair : MachineLoops)
	{
		if (UAudioComponent* C = Pair.Value.Get())
		{
			const float Target = MachineVolume.FindRef(Pair.Key) * Sfx;
			const float Cur = C->VolumeMultiplier;
			C->SetVolumeMultiplier(FMath::Max(FMath::FInterpTo(Cur, Target, DeltaTime, 2.f), 0.001f));
		}
	}
}

void URHAudioSubsystem::TickElevator(const FVector& DoorCm)
{
	const URHColonyVisualizerSubsystem* Colony = GetWorld()->GetSubsystem<URHColonyVisualizerSubsystem>();
	const float Alpha = Colony ? Colony->GetElevatorDoorAlpha() : 0.f;
	const float Sfx = Vol(CVarAudioSFX);
	// The doors' own wake: a slide as they start to part (with the arrival
	// chime) and a slide as they start to close.
	const bool bLog = CVarAudioLog.GetValueOnGameThread() != 0;
	// Open / closing states, so every parting is one "open" and every
	// retreat is one "close" - including doors that only half-open for a
	// quick rider, or reverse before they shut.
	const double Now = GetWorld()->GetTimeSeconds();
	if (!bDoorsOpenState && Alpha > 0.03f)
	{
		bDoorsOpenState = true;
		bDoorsClosing = false;
		PlayAt(DoorOpen, DoorCm, FMath::Max(Sfx, 0.001f), 1800.f, 7000.f);
		const bool bChime = Now - LastChimeAt >= 6.0;
		if (bChime)
		{
			LastChimeAt = Now;
			PlayAt(Chime, DoorCm, FMath::Max(0.8f * Sfx, 0.001f), 1800.f, 7000.f);
		}
		if (bLog) { UE_LOG(LogRedHope, Display, TEXT("Audio: elevator doors opening (slide%s)"), bChime ? TEXT(" + chime") : TEXT("")); }
	}
	else if (bDoorsOpenState && !bDoorsClosing && Alpha < PrevDoorAlpha - 1.0e-4f)
	{
		bDoorsClosing = true;
		PlayAt(DoorClose, DoorCm, FMath::Max(Sfx, 0.001f), 1800.f, 7000.f);
		if (bLog) { UE_LOG(LogRedHope, Display, TEXT("Audio: elevator doors closing (slide)")); }
	}
	else if (bDoorsClosing && Alpha > PrevDoorAlpha + 1.0e-4f)
	{
		bDoorsClosing = false; // reversed before shutting: no new "open"
	}
	if (bDoorsOpenState && Alpha < 0.03f)
	{
		bDoorsOpenState = false; // fully shut: the next parting is a new opening
		bDoorsClosing = false;
	}
	PrevDoorAlpha = Alpha;
}

void URHAudioSubsystem::TickEvents()
{
	UWorld* World = GetWorld();
	const URHSimWorldSubsystem* Sim = World->GetSubsystem<URHSimWorldSubsystem>();
	const URHColonyVisualizerSubsystem* Colony = World->GetSubsystem<URHColonyVisualizerSubsystem>();
	TSet<int32> Building;
	for (const FRHBuildingInstance& B : Sim->GetBuildings())
	{
		if (B.bUnderConstruction)
		{
			Building.Add(B.Id);
		}
	}
	const int32 Colonists = Sim->GetColonists().Num();
	const bool bAlert = Colony && !Colony->GetAlertText().IsEmpty();
	if (bEventsPrimed)
	{
		for (int32 Id : PrevUnderConstruction)
		{
			if (!Building.Contains(Id) && Sim->GetBuildings().ContainsByPredicate([Id](const FRHBuildingInstance& B) { return B.Id == Id; }))
			{
				PlayCue(FName("BuildDone"));
				break;
			}
		}
		if (Colonists > PrevColonists)
		{
			PlayCue(FName("Arrival"));
		}
		if (bAlert && !bPrevAlert)
		{
			PlayCue(FName("Alert"));
		}
	}
	bEventsPrimed = true; // the first tick only learns the state (a loaded save is not news)
	PrevUnderConstruction = MoveTemp(Building);
	PrevColonists = Colonists;
	bPrevAlert = bAlert;
}

void URHAudioSubsystem::TickMusic(int32 Phase)
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	const TArray<TObjectPtr<USoundBase>>& List = Phase == 2 ? MusicSovereignty : (Phase == 1 ? MusicHabitation : MusicAutomation);
	if (Music)
	{
		Music->SetVolumeMultiplier(FMath::Max(Vol(CVarAudioMusic), 0.001f));
	}
	if (Phase != MusicPhase)
	{
		// A new era of the colony: let the old score breathe out, then the
		// new one comes in after a short silence.
		if (Music && Music->IsPlaying() && MusicPhase >= 0)
		{
			Music->FadeOut(5.f, 0.f);
			NextMusicAt = FMath::Max(NextMusicAt, Now + 9.0);
		}
		MusicPhase = Phase;
		MusicTrack = 0;
	}
	const bool bPlaying = Music && Music->IsPlaying();
	if (bMusicPlaying && !bPlaying)
	{
		// A track ended: silence before the next (pacing, not wallpaper).
		NextMusicAt = Now + FMath::FRandRange(25.0, 70.0);
	}
	bMusicPlaying = bPlaying;
	if (!bPlaying && Now >= NextMusicAt && List.Num() > 0)
	{
		USoundBase* Track = List[MusicTrack % List.Num()];
		++MusicTrack;
		if (!Music)
		{
			Music = UGameplayStatics::CreateSound2D(World, Track, Vol(CVarAudioMusic), 1.f, 0.f, nullptr, false, false);
		}
		if (Music)
		{
			Music->SetSound(Track);
			Music->FadeIn(4.f, 1.f);
			bMusicPlaying = true;
			UE_LOG(LogRedHope, Display, TEXT("Audio: music phase %d track %s"), Phase, *Track->GetName());
		}
	}
}
