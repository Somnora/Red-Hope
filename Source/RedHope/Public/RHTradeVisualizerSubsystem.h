#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHTradeVisualizerSubsystem.generated.h"

class AActor;
class UProceduralMeshComponent;
class URHSimWorldSubsystem;
class URHDefinitionsSubsystem;
class UMaterialInstanceDynamic;

// E8: a trade route's status, first applicable wins - the rival card's chain (sided with Earth, route closed, embargo, disrupted), then a
// dust storm halting convoys, then open. The MARKET panel's chips, the Routes overlay's colours and the markers' status word all read it.
enum class ERHRouteStatus : uint8 { Open, Storm, Disrupted, Embargo, Closed, Defected };

// The Routes overlay (E8, docs/qa/2026-10-25/e8-market-panel-design.md section 4 and R2-9). A pure listener: it reads the sim through const
// getters and writes nothing back, so deleting it leaves the sim intact. While on, it drapes one 3 m ribbon per AVAILABLE rival's route from
// the colony depot (E7's GetColonyDepotLocationCm) to the rival's marker (the colony visualizer's RivalMarkerPos), sampled every 5 m on the
// VISIBLE surface (RHMarsTerrain::SceneryTopZCm: the director's Zarya (a), 2026-10-31), 40 cm up, in one procedural mesh with a section per
// status colour, each opaque and unlit in its legend colour (M_RH_RouteUnlit): teal open, amber embargo or disrupted, red closed or sided with Earth, sky
// blue halted by a storm. It is polled twice a second and rebuilt only when its key moves (each available rival's name, status and marker
// position, the depot, and whether the markers show), never per frame; a route's draped edges are kept until its ends or the scenery move,
// so a status change only re-bins them. It is drawn exactly when the markers are (the colony visualizer's ShowsSurfaceLevel, R3-6). An
// undiscovered rival gets no ribbon.
UCLASS()
class REDHOPE_API URHTradeVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHTradeVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// The switch (the command bar's Routes button, RH.Trade overlay <0|1>).
	void SetOverlayOn(bool bOn);
	bool IsOverlayOn() const { return bOverlay; }
	// What the click test reads: the ribbons in the last build (0 while off or hidden), and the status each was drawn with.
	int32 GetDrawnRouteCount() const { return DrawnRoutes; }
	ERHRouteStatus GetRouteStatus(FName Rival) const;
	// The worst rebuild in milliseconds since the subsystem started (nothing resets it today; [FrameStats] reads the window's worst from
	// RHFrameStats::GMarketWorstRebuildMs, which RH.FrameStats resets at a window's start).
	float GetWorstRebuildMs() const { return WorstRebuildMs; }
	void ResetWorstRebuildMs() { WorstRebuildMs = 0.f; }

	// The status chain, the status's colour and its word (the marker's "Zarya Station · embargo" and the legend). Static: the deck and the
	// colony visualizer read them too.
	static ERHRouteStatus RouteStatusOf(const URHSimWorldSubsystem& Sim, FName Rival);
	static FLinearColor RouteStatusColor(ERHRouteStatus Status);
	static const TCHAR* RouteStatusWord(ERHRouteStatus Status);

	// QA and the report: the highest the visible surface stood above the height field (GroundZCm) under a drawn ribbon's edge (cm).
	float GetRibbonMaxRiseCm(FName Rival) const { const FRouteStrip* S = Strips.Find(Rival); return S ? S->MaxRiseCm : 0.f; }

private:
	// A route's draped edges (left and right, one point every 5 m), kept for its two ends and the scenery's build.
	struct FRouteStrip
	{
		FVector Depot = FVector::ZeroVector, Marker = FVector::ZeroVector;
		int32 Generation = -1;
		TArray<FVector> L, R;
		float MaxRiseCm = 0.f;
	};
	void EnsureMesh();
	void ApplyRibbonGlow();
	const FRouteStrip& StripFor(FName Rival, const FVector& Depot, const FVector& Marker);
	void Rebuild(const URHSimWorldSubsystem& Sim, const URHDefinitionsSubsystem& Defs, bool bShown);
	TMap<FName, FRouteStrip> Strips;
	TWeakObjectPtr<UMaterialInstanceDynamic> BinMids[4];             // the mesh owns them (its materials); weak here
	float AppliedGlow = -1.f;

	UPROPERTY() TObjectPtr<AActor> Holder;
	UPROPERTY() TObjectPtr<UProceduralMeshComponent> Mesh;

	bool bOverlay = false;
	double NextPollS = 0.0;
	uint32 DrawnKey = 0;
	bool bDrawnValid = false;
	int32 DrawnRoutes = 0;
	TMap<FName, ERHRouteStatus> DrawnStatus;
	float WorstRebuildMs = 0.f;
};
