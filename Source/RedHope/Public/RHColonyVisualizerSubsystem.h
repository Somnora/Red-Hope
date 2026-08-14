#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHColonyVisualizerSubsystem.generated.h"

class AStaticMeshActor;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UStaticMeshComponent;
struct FRHBuildingInstance;
struct FRHCommand;
struct FRHDepositState;
struct FRHSurveyRecord;

// Presentation mirror of the colony: spawns a gray-box mesh per sim building
// event and debug-draws grid coverage discs (Power-as-Territory made
// visible). Pure listener - deleting this class leaves the sim intact.
UCLASS()
class REDHOPE_API URHColonyVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHColonyVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// Last order-rejection notice, empty once stale. The visualizer already
	// listens on the sim's rejection seam; the deck polls this rather than
	// wiring its own delegate (debug messages hide under the Slate overlay).
	FText GetNoticeText() const;
	// Banner-weight alert (era refusal, event onset, ship countdown) - the
	// must-not-miss channel the director asked for. Empty once stale.
	FText GetAlertText() const;

	// The pit view (M1-d, v2 after the director's watch-through): once the
	// shaft exists the world reads as a PERFECT SQUARE HOLE DUG IN SAND - the
	// surrounding surface stays (an instanced sand skirt replaces the ground
	// plane), rock walls line the pit from the surface down to the open
	// floor, and the carved floor tiles sit flush at the bottom. Visible from
	// the surface AND underground; the elevator changes which floor is open
	// and rides the camera down. Sunlight and exposure never change - it is
	// one continuous lit scene, exactly like looking into a real excavation.
	// Presentation-only; the sim never knows what the camera looks at.
	void SetViewLevel(int32 Level);
	int32 GetViewLevel() const { return ViewLevel; }

	// Live tuning of the ambient emissive throb (QA verdict 4). Scale is applied
	// against each building's AUTHORED PulseDepth, so the art-bible mix between
	// machines is preserved and repeated calls set an absolute multiplier
	// instead of compounding. 0 = fully still, 1 = as authored.
	void Debug_SetPulseScale(float Scale);

private:
	void HandleBuildingAdded(const FRHBuildingInstance& Instance);
	void HandleBuildingCompleted(const FRHBuildingInstance& Instance);
	void HandleCommandRejected(const FRHCommand& Cmd, const FString& Reason);
	void HandleQuotaMet(int32 Sol, double AwardKg);
	void HandleShipArrived(const TArray<FName>& Items);
	// Discovery (M1-b): marker + notice the moment a scout's survey reports.
	void HandleDepositDiscovered(const FRHDepositState& Deposit);
	// Empty surveys report too - paid-for knowledge, even when it's "nothing".
	void HandleSurveyCompleted(const FRHSurveyRecord& Record);
	void HandleAlert(const FString& Alert);
	// Load path: drop every mirror actor and rebuild from a full state walk.
	// The visualizer owning zero authoritative state is what makes this safe.
	void HandleColonyReloaded();
	void SpawnDepositMarkers();
	void SpawnDepositMarker(const FRHDepositState& Deposit);
	FVector ScaleFor(const FRHBuildingInstance& Instance) const;
	// Canon visual language (director's reference set, ~/Martians/assets):
	// bone-white or dark-slate industrial bodies, hazard-yellow details, and
	// ONE saturated glowing accent per function - furnace orange, battery
	// teal, ice blue. TintFor = the function accent (labels + glows);
	// BodyFor = the box albedo. Presentation-only, like ScaleFor.
	FLinearColor TintFor(FName DefName) const;
	FLinearColor BodyFor(FName DefName) const;
	void ApplyTint(AStaticMeshActor* Actor, const FLinearColor& Color, const FLinearColor& Emissive = FLinearColor::Black) const;
	void ApplyTint(UStaticMeshComponent* Mesh, const FLinearColor& Color, const FLinearColor& Emissive = FLinearColor::Black) const;
	// Real tiling texture via the triplanar M_MarsSurface (RHArt commandlet
	// asset). Returns false when the material/texture assets are absent so the
	// caller can fall back to the flat ApplyTint look.
	bool ApplySurface(UStaticMeshComponent* Mesh, const TCHAR* TexPath, float TileCm, const FLinearColor& Tint, float Rough = 0.95f) const;
	void AddLabel(AStaticMeshActor* Actor, const FString& Text, const FLinearColor& Color, float SouthOffsetCm) const;
	// Silhouette pass (director: "no boring rectangles"): each family gets a
	// distinct primitive-composed form per the reference sheet. World-space
	// accents (buildings never move), added only once construction ends.
	void AddAccent(AStaticMeshActor* Actor, const TCHAR* ShapePath, const FVector& WorldCm, const FRotator& Rot, const FVector& Scale, const FLinearColor& Color, const FLinearColor& Emissive = FLinearColor::Black) const;
	void BuildSilhouette(AStaticMeshActor* Actor, FName DefName, const FVector& BaseCm, const FVector& ScaleM) const;

	// Shaft & excavation mirror (M1-d Gate A2): a column actor for the trunk,
	// one floor tile per carved cell. The sim keeps carved cells as per-floor
	// COUNTS (A1 model); tiles lay out in a deterministic square spiral around
	// the shaft head - honest about how much is carved, canonical about where.
	// Flagged in the build log: spatial (painted-position) carving is a sim
	// upgrade for a later gate if the director wants placement-in-pocket rules.
	void UpdateShaftVisuals();
	void ApplyViewLevel();
	AStaticMeshActor* SpawnBox(const FVector& CenterCm, const FVector& ScaleM, const FLinearColor& Body, const FLinearColor& Emissive) const;

public:
	// Canonical carved-cell layout (shared with the crew visualizer): cell
	// index -> grid offset from the shaft head. Presentation-only convention.
	static FIntPoint SpiralCell(int32 Index);

private:
	// The pit rig (v2): two instanced-mesh layers rebuilt whenever the pit's
	// shape changes - the sand skirt (surface around the hole) and the rock
	// walls (surface down to the open floor). One actor, thousands-cheap.
	void EnsureSliceRig();
	void RebuildSliceRig();
	// The scenery rig (Session 44+, director request): a mountain ring + mesas
	// hazing into the fog (the reference panorama), a hero massif near enough
	// to build a Habitat at its base, a far ground apron so the horizon never
	// shows void, and deterministic rock/pebble/drift/crater scatter that gives
	// the regolith readable detail. Built once (seeded FRandomStream - identical
	// every run), pure presentation, hidden while underground like the surface.
	void EnsureEnvironmentRig();
	// Two distinct views (director recording verdict): the elevator is a hard
	// cut between strata. At SURF you see only the intact sunlit surface - no
	// underground bleed. Descending to -N opens THAT floor as a pit you look
	// down into (sand walls up to the rim, the dug floor at the bottom). So
	// "underground" is the whole condition; the open floor is just ViewLevel.
	bool IsUnderground() const { return ViewLevel < 0; }

	// Room zoning made visible (M2 Gate B): tiles retint by designated function
	// and carry a small flat label. State-diffed per tick like the shaft mirror.
	void RefreshRoomVisuals();
	FLinearColor RoomTint(FName RoomRowName) const;
	// Content path of the furnishing prop for a room function (empty = none).
	FString RoomPropPath(FName Room) const;
	// Sovereignty made visible (M3/M4): a distant settlement marker per
	// AVAILABLE rival (tinted by its diplomatic state - normal / embargoed /
	// defected / sabotaged) and a rover that physically drives the trade route,
	// following the sim's convoy progress. Pure presentation, state-diffed per
	// tick; markers sit at a deterministic bearing (a content hash of the rival
	// name) at presentation distance - "they're over there", not literal km.
	void RefreshSovereigntyVisuals();
	FVector RivalMarkerPos(FName Rival, float DistanceKm) const;

	UPROPERTY() TMap<int32, TObjectPtr<AStaticMeshActor>> BuildingVisuals;
	// Last PoweredState pushed into a building's material, so the per-frame pass
	// only touches a MID when the sim actually changed the answer.
	// 0 = dark (shed or switched off), 1 = running, 2 = under construction.
	TMap<int32, uint8> AppliedPowerState;
	// Each building's PulseDepth as the art pass authored it, captured the first
	// time RH.Pulse touches it - the MID's own value stops being the authored
	// one after the first scale, so scaling off it would compound.
	TMap<int32, float> AuthoredPulseDepth;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> DepositMarkers;
	// (level, spiral index, 0) -> the tile actor + what function it last showed.
	TMap<FIntVector, TWeakObjectPtr<AStaticMeshActor>> TileByCell;
	TMap<FIntVector, FName> AppliedRoomTint;
	// Per-cell furnishing prop component (owned by its tile actor, so it is
	// GC-safe via CarveTileVisuals and inherits the tile's slice-view hiding).
	TMap<FIntVector, TWeakObjectPtr<UStaticMeshComponent>> RoomPropByCell;
	// Automatic hab lighting (director 2026-07-09): one soft ceiling light per
	// designated cell, owned by its tile actor like the prop; intensity is a
	// READOUT of the sim - full while the floor's circulator runs on a healthy
	// grid, dimmed in a brownout, dark when circulation is down. Plus one cool
	// dim fill that rides the viewed floor (owned by the shaft column) so an
	// unpowered hab reads dim-and-cold instead of void-black at night.
	TMap<FIntVector, TWeakObjectPtr<UPointLightComponent>> LightByCell;
	TWeakObjectPtr<UPointLightComponent> FloorFill;
	// Insulated-wall dressing (director 2026-07-09: "walls look like bare
	// martian rock... every few blocks an air vent"): vent units mounted on the
	// pit's wall faces, rebuilt with the rig. Owned by SliceRigActor.
	TArray<TWeakObjectPtr<UStaticMeshComponent>> WallVents;
	// Lived-in accumulation: crates/drums/lockers appear over time on populated
	// floors (count grows with residents + sols; components ride tile actors so
	// lifecycle and floor-cut hiding come free). Per-floor spawned count.
	TMap<int32, int32> ClutterSpawnedPerLevel;
	// The elevator at the shaft head: real cage mesh + two sliding door panels,
	// doors easing open whenever a colonist is near the shaft on the open floor.
	TWeakObjectPtr<UStaticMeshComponent> ElevatorCage;
	TWeakObjectPtr<UStaticMeshComponent> ElevatorDoorL;
	TWeakObjectPtr<UStaticMeshComponent> ElevatorDoorR;
	float ElevatorDoorAlpha = 0.f;
	// Sovereignty visuals (M3/M4): marker per rival + its last-applied state
	// (0 normal / 1 embargoed / 2 defected / 3 sabotaged), and the trade rover.
	UPROPERTY() TMap<FName, TObjectPtr<AStaticMeshActor>> RivalMarkers;
	TMap<FName, uint8> RivalMarkerState;
	UPROPERTY() TObjectPtr<AStaticMeshActor> ConvoyVisual;
	UPROPERTY() TObjectPtr<AStaticMeshActor> ShipVisual;
	UPROPERTY() TObjectPtr<AStaticMeshActor> ShaftVisual;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> CarveTileVisuals;
	UPROPERTY() TObjectPtr<AActor> SliceRigActor;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SkirtISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WallISM;
	UPROPERTY() TObjectPtr<AActor> EnvironmentRigActor;
	bool bEnvironmentHidden = false;
	FIntVector LastRigKey = FIntVector(-999, -999, -999); // (depth, pit level, carved count)
	TMap<int32, int32> TilesSpawnedPerLevel;
	TWeakObjectPtr<AActor> GroundActor;
	bool bGroundSearched = false;
	int32 ViewLevel = 0;
	int32 LastShaftDepthSeen = 0;
	FDelegateHandle AddedHandle;
	FDelegateHandle CompletedHandle;
	FDelegateHandle RejectedHandle;
	FString LastNotice;
	double LastNoticeRealSeconds = -1.0e9;
	FString LastAlert;
	double LastAlertRealSeconds = -1.0e9;
};
