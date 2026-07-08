#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHCrewVisualizerSubsystem.generated.h"

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
		EPhase Phase = EPhase::Resident;
		int32 CurLevel = 0;        // floor the figure currently stands on
		int32 HomeLevel = -1;
		FVector TargetCm = FVector::ZeroVector;
		double NextDecideRealSeconds = 0.0;
		bool bTintedUnsupported = false;
		// Alive pass: the emote plate above the head (spawned lazily) and its
		// remaining display window; cheering figures hop while CheerRemainingS
		// runs. Both windows count down ONLY while the sim runs (paused colony
		// = still colony - the celebration waits for the player).
		TWeakObjectPtr<UTextRenderComponent> Emote;
		double EmoteRemainingS = 0.0;
		double CheerRemainingS = 0.0;
	};

	void HandleColonyReloaded();
	// Alive pass: the sim's crew-life beats made visible - pairs gather for
	// pastimes, a helper walks to the worker they're joining, disputes read as
	// a close face-off under an amber plate (abstracted - words, never blows),
	// and a Celebrate sets the WHOLE crew hopping. Pure presentation.
	void HandleCrewMoment(uint8 Type, int32 IdA, int32 IdB, const FString& Text);
	void ShowEmote(FCrewVisual& Vis, const FString& Line, const FColor& Color, double Seconds);
	AActor* SpawnFigure(const FString& Name, const FVector& AtCm, UStaticMeshComponent*& OutBody);
	void SetBodyTint(FCrewVisual& Vis, bool bUnsupported);
	// A random point inside a random carved cell of the given floor - the
	// canonical spiral layout the colony visualizer lays tiles with.
	FVector WanderPointCm(int32 Level) const;
	// A point inside a cell zoned with the given job function on this floor
	// (M2 Gate B: colonists report to their posts); zero when none exists.
	FVector JobPointCm(int32 Level, FName JobFunction) const;

	TMap<int32, FCrewVisual> Tracked; // colonist Id -> figure
	// First roster sync seeds residents in place; only ids appearing AFTER it
	// get the staged pad-to-shaft arrival march.
	bool bSyncedOnce = false;
	// The body-language clock: accrues only while the sim runs, so hops and
	// work-bobs freeze mid-pose on pause instead of dancing through it.
	double AnimClockS = 0.0;
};
