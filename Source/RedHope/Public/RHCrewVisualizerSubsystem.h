#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHCrewVisualizerSubsystem.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

// Presentation mirror of the crew (M2 Gate A2): one small suited figure per
// colonist. Pure listener - the sim keeps colonists abstract (roster + home
// floor); everything spatial here (arrival march, home-floor wander, surface
// strolls) is presentation flavor and never feeds back. Deleting this class
// leaves the sim intact.
//
// Where a colonist IS, visually:
//   Arriving  - walked in from the supply-ship pad to the shaft head (staged
//               only for colonists who land mid-session; a reload's existing
//               crew spawns straight into residence).
//   Resident  - wandering the carved cells of their home floor.
//   Strolling - a brief suited walk on the surface near the shaft head, then
//               back down ("the crew takes air", the A2 spec's surface beat).
// Visibility obeys the pit view's hard cut: a figure shows iff the floor it
// currently stands on IS the viewed floor.
UCLASS()
class REDHOPE_API URHCrewVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHCrewVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

private:
	enum class EPhase : uint8 { Arriving, Descending, Resident, StrollOut, StrollBack };

	struct FCrewVisual
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStaticMeshComponent> Body; // tint flips with support state
		// The rigged walker (animation phase): when a RH_Walker_<name> skeletal
		// asset exists the figure animates for real - Walk while moving, Idle at
		// rest - and Body stays null. Static mesh / primitive remain fallbacks.
		TWeakObjectPtr<USkeletalMeshComponent> SkelBody;
		FName CurrentClip;           // Walk / Idle / WorkBench / WorkLab
		// The name plate (spawned with the figure) - hover-gated: hundreds of
		// always-on names read as noise (director 2026-07-10), so plates show
		// only when the cursor is near the figure.
		TWeakObjectPtr<UTextRenderComponent> NameLabel;
		EPhase Phase = EPhase::Resident;
		int32 CurLevel = 0;        // floor the figure currently stands on
		int32 HomeLevel = -1;
		FVector TargetCm = FVector::ZeroVector;
		double NextDecideRealSeconds = 0.0;
		bool bTintedUnsupported = false;
		// Base height the body sits at when idle: 0 for a real grounded colonist
		// mesh (feet on the floor), 62 for the primitive cylinder fallback. The
		// tick's work-bob / cheer-hop add on top of this.
		float BaseZCm = 62.f;
		// No-rig behavior pass (director 2026-07-09, "I want to know what
		// they're doing"): the current beat's activity line, shown persistently
		// on the emote plate whenever no crew-moment is playing; and the yaw the
		// figure settles into on arrival (facing the bench/table it works at).
		FString Activity;
		float PostFaceYawDeg = 0.f;
		bool bFaceOnArrive = false;
		// Alive pass: the emote plate above the head (spawned lazily) and its
		// remaining display window; cheering figures hop while CheerRemainingS
		// runs. Both windows count down ONLY while the sim runs (paused colony
		// = still colony - the celebration waits for the player).
		TWeakObjectPtr<UTextRenderComponent> Emote;
		double EmoteRemainingS = 0.0;
		double CheerRemainingS = 0.0;
	};

public:
	// True if any crew figure stands within RadiusCm (2D) of WorldPos - the
	// colony visualizer's elevator doors open for approaching colonists.
	bool IsAnyCrewWithinCm(const FVector& WorldPos, float RadiusCm) const;

	// Scripted-capture helper: world location of the Nth tracked crew figure,
	// in colonist-Id order so the same index means the same person between
	// runs. Lets a QA driver frame walkers that are moving instead of guessing
	// a fixed point and hoping someone walks through it. False when the index
	// is past the roster or that figure no longer exists.
	bool Debug_GetCrewLocation(int32 Index, FVector& OutLocationCm) const;

private:
	void HandleColonyReloaded();
	// Alive pass: the sim's crew-life beats made visible - pairs gather for
	// pastimes, a helper walks to the worker they're joining, disputes read as
	// a close face-off under an amber plate (abstracted - words, never blows),
	// and a Celebrate sets the WHOLE crew hopping. Pure presentation.
	void HandleCrewMoment(uint8 Type, int32 IdA, int32 IdB, const FString& Text);
	void ShowEmote(FCrewVisual& Vis, const FString& Line, const FColor& Color, double Seconds);
	AActor* SpawnFigure(const FString& Name, int32 Id, const FVector& AtCm, UStaticMeshComponent*& OutBody, USkeletalMeshComponent*& OutSkel, float& OutBaseZCm);
	void SetBodyTint(FCrewVisual& Vis, bool bUnsupported);
	// Content path of the real colonist mesh for this Id (deterministic pick from
	// the imported roster so each crew member looks distinct and stable across
	// reloads); empty when the meshes are absent so SpawnFigure falls back to the
	// primitive figure.
	FString ColonistMeshPath(int32 Id) const;
	// A random point inside a random carved cell of the given floor - the
	// canonical spiral layout the colony visualizer lays tiles with.
	FVector WanderPointCm(int32 Level) const;
	// Furniture positions per level, rebuilt each Tick from the colony
	// visualizer's furnished props. Presentation-side only: the SIM has no
	// furniture concept and stays untouched - this is about where figures
	// APPEAR to walk. Wander targets are resampled away from these, and the
	// walk steers around them; work-post approaches are deliberately exempt
	// so a colonist can still reach the bench they are meant to stand at.
	TMap<int32, TArray<FVector>> FurnitureByLevel;
	bool NearFurnitureCm(int32 Level, const FVector& PointCm, float RadiusCm) const;
	// A work post BESIDE the job room's furnishing prop on this floor - a
	// deterministic seat around the cell centre keyed by SeatSeed (colonist Id)
	// so workers stand at the bench, not inside it; zero when none exists.
	// OutFaceYawDeg (optional) = the yaw that faces the prop from that seat.
	FVector JobPointCm(int32 Level, FName JobFunction, int32 SeatSeed, float* OutFaceYawDeg = nullptr) const;
	// Same seat ring, but keyed by ROOM ROW NAME instead of job function - for
	// off-shift beats at rooms that aren't jobs (a meal at the Dining table).
	FVector RoomSeatCm(int32 Level, FName RoomRowName, int32 SeatSeed, float* OutFaceYawDeg = nullptr) const;

	TMap<int32, FCrewVisual> Tracked; // colonist Id -> figure
	// First roster sync seeds residents in place; only ids appearing AFTER it
	// get the staged pad-to-shaft arrival march.
	bool bSyncedOnce = false;
	// The body-language clock: accrues only while the sim runs, so hops and
	// work-bobs freeze mid-pose on pause instead of dancing through it.
	double AnimClockS = 0.0;
};
