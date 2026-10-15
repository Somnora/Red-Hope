// The colony's sound (2026-10-14 - Red Hope shipped silent until then).
//
// Presentation only: it READS the sim and the visualizers and never writes to
// either. The design brief's direction is "thin-atmosphere sound design
// (muffled exterior, rich interior)" and a tone of "awe, isolation, then
// community". So:
//   - ambience beds: a soft surface wind, a warmer habitat interior when the
//     view is underground, and a dust-storm layer while a storm runs;
//   - machine loops placed on their buildings, audible as you zoom in (the
//     brief's "the zoom IS the emotional register shift"), quiet when the
//     building is idle and silent when it is off, shed or on another floor;
//   - the elevator's doors and chime;
//   - one-shot cues for UI and events (Cue());
//   - music by phase (Automation / Habitation / Sovereignty) with silence
//     between tracks - colony-sim pacing, not wall-to-wall score.
// Sounds live under /Game/RedHope/Audio (scripts/audio/rh_sfx_build.py,
// scripts/unreal/rh_import_audio.py). Every one is optional: anything missing
// is simply skipped. Volume dials: rh.AudioMaster / Music / Ambience / SFX / UI.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHAudioSubsystem.generated.h"

class UAudioComponent;
class USoundBase;

UCLASS()
class REDHOPE_API URHAudioSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHAudioSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// A one-shot UI/event cue by name: Click, Toggle, Confirm, Error, CardOpen,
	// CardClose, Notify, Milestone, Alert, BuildDone, Arrival. Safe to call from
	// Slate with any world (no-op without the subsystem); rate-limited per cue
	// so a burst of identical events plays once.
	static void Cue(const UWorld* World, FName CueName);
	void PlayCue(FName CueName);

	// For Slate buttons, which play their click without a world.
	static USoundBase* UIClickSound();

private:
	USoundBase* LoadSound(const TCHAR* Category, const TCHAR* Name) const;
	UAudioComponent* MakeBed(USoundBase* Sound);
	void TickAmbience(float DeltaTime, bool bUnderground, bool bStorm);
	void TickMachines(float DeltaTime, int32 ViewLevel, double FloorH);
	void TickElevator(const FVector& DoorCm);
	void TickEvents();
	void TickMusic(int32 Phase);
	void PlayAt(USoundBase* Sound, const FVector& WorldCm, float Volume, float InnerCm, float FalloffCm);

	UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> Cues;
	UPROPERTY() TMap<FName, TObjectPtr<USoundBase>> MachineSounds;   // by building DefName
	UPROPERTY() TObjectPtr<USoundBase> DoorOpen;
	UPROPERTY() TObjectPtr<USoundBase> DoorClose;
	UPROPERTY() TObjectPtr<USoundBase> Chime;
	// Music by phase (UHT allows no static arrays of containers).
	UPROPERTY() TArray<TObjectPtr<USoundBase>> MusicAutomation;
	UPROPERTY() TArray<TObjectPtr<USoundBase>> MusicHabitation;
	UPROPERTY() TArray<TObjectPtr<USoundBase>> MusicSovereignty;

	UPROPERTY() TObjectPtr<UAudioComponent> Wind;
	UPROPERTY() TObjectPtr<UAudioComponent> Habitat;
	UPROPERTY() TObjectPtr<UAudioComponent> Storm;
	UPROPERTY() TObjectPtr<UAudioComponent> Music;
	UPROPERTY() TMap<int32, TObjectPtr<UAudioComponent>> MachineLoops; // by building Id
	TMap<int32, float> MachineVolume;

	float WindVol = 0.f;
	float HabitatVol = 0.f;
	float StormVol = 0.f;
	double NextMachineSync = 0.0;
	float PrevDoorAlpha = 0.f;
	bool bDoorsOpenState = false;
	bool bDoorsClosing = false;
	double LastChimeAt = -100.0;
	bool bEventsPrimed = false;
	int32 PrevColonists = 0;
	TSet<int32> PrevUnderConstruction;
	bool bPrevAlert = false;
	TMap<FName, double> LastCueAt;
	int32 MusicPhase = -1;
	int32 MusicTrack = 0;
	bool bMusicPlaying = false;
	double NextMusicAt = 0.0;
	double NextStatusLog = 0.0;
};
