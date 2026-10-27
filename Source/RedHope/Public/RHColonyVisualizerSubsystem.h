#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHColonyVisualizerSubsystem.generated.h"

class AStaticMeshActor;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USpotLightComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class URHSimWorldSubsystem;
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
	// A building's main visual actor (the build menu's thumbnail capture,
	// RH.Thumbs); null if it has none yet.
	AStaticMeshActor* GetBuildingVisual(int32 BuildingId) const
	{
		const TObjectPtr<AStaticMeshActor>* A = BuildingVisuals.Find(BuildingId);
		return A ? A->Get() : nullptr;
	}

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
	// D8 (the deep view): the player controller owns the section state and pushes this mirror on entry, on every focus change and on exit.
	// While it is on, every actor this mirror keeps is hidden (the section draws its own content), ViewLevel stays frozen at the entry
	// floor, and crew, atmosphere and audio read IsSectionMode() / GetSectionFocus() from here. Leaving re-applies the view itself, since
	// SetViewLevel returns early when the level did not change.
	void SetSectionMode(bool bOn, int32 Focus);
	bool IsSectionMode() const { return bSectionMode; }
	int32 GetSectionFocus() const { return SectionFocus; }
	// Alive pass 2: where a placed furnishing actually stands - its logical
	// centre (with a seat's pull-up applied) on the floor, its yaw (90 x Rot:
	// the catalogue's front faces -Y at yaw 0) and the drawn mesh's half
	// extents along its own X / Y. False until the piece is drawn.
	bool GetFurnitureFrame(int32 Id, FVector& OutCentreCm, float& OutYawDeg, FVector2D& OutHalfCm) const;
	// 0 closed .. 1 open: the cab's sliding doors (0 when only the legacy
	// lattice cage exists). The audio subsystem plays the slide and chime off it.
	float GetElevatorDoorAlpha() const { return bElevatorIsCab ? ElevatorWakeAlpha : 0.f; }
	// RH.FrameStats: the actors the colony mirror keeps (a building actor each,
	// plus one tile actor per carved cell).
	int32 GetBuildingActorCount() const { return BuildingVisuals.Num(); }
	// Bumped whenever a prop or furnishing component is added, removed, or shown
	// or hidden: the crew's obstacle list rebuilds only when this moves.
	uint32 GetPropsVersion() const { return PropsVersion; }
	int32 GetTileActorCount() const { return TileByCell.Num(); }
	// Visual pass item 2 (construction sites, 2026-10-26): the site kit instances a building's site holds right now in
	// the four kit HISMs - Part 0 pad tiles, 1 scaffold bays, 2 its mast, 3 crates, -1 all four. 0 for a finished
	// building, a site on a floor out of view, or no such building. The click test's build.site reads it.
	int32 GetSiteInstanceCount(int32 BuildingId, int32 Part = -1) const;
	// What the kit's HISMs hold right now, all sites together (Part -1 = all four) - read off the components, not the
	// per-site bookkeeping, so the click test can check the two agree.
	int32 GetSiteKitHismInstanceCount(int32 Part = -1) const;
	// A fabricating site's cut, read back from its model's own MID (BuildCutZ, world cm), the model's bottom on the pad
	// and whether the model is drawn wearing that MID. False when the site has no model to reveal (or is no site).
	bool GetSiteRevealCut(int32 BuildingId, float& OutCutZ, float& OutBottomZ, bool& bOutModelVisible) const;
	// The fabrication fraction a site's own model was last revealed to (0..1, in 5 % steps), or -1 while the site waits
	// for materials, has no model to reveal (a pressurized shell, a greenhouse deck, the Floodmast) or is not a site.
	float GetSiteRevealFraction(int32 BuildingId) const;
	// RH.Sites: one log line per site (stage, step, crates, courses, instances) - captures and QA pick their frames by it.
	void LogSites() const;

	// Live tuning of the ambient emissive throb (QA verdict 4). Scale is applied
	// against each building's AUTHORED PulseDepth, so the art-bible mix between
	// machines is preserved and repeated calls set an absolute multiplier
	// instead of compounding. 0 = fully still, 1 = as authored.
	void Debug_SetPulseScale(float Scale);

	// Sims-style interior viewing (rh.Cutaway). The pit is open-topped by
	// construction, so there is no roof to lift: what occludes an interior is
	// the near WALL faces. Mode 1 drops the one or two facing the camera and
	// swaps as you orbit; mode 2 drops them all for a floorplan read.
	// State-diffed on a (mode, hidden-sides) key, with hysteresis, so orbiting
	// past a boundary cannot strobe the walls.
	void ApplyCutaway();

	// D8: the floor view's colour language, shared with the section (one helper each, never a copy). RoomTint is a room function's
	// accent; DepthWarmTint is a floor tile's base after D5's depth darkening and Warmth lerp (the base itself above the cool line).
	FLinearColor RoomTint(FName RoomRowName) const;
	static FLinearColor DepthWarmTint(const class URHSimWorldSubsystem& Sim, const FLinearColor& Base, int32 Level);

private:
	// C3: the per-frame passes (dome crops, habitat shells, furnishings, room
	// tiles, clutter) re-run when the sim says something changed - an order, a
	// building, a furnishing, a wall feature, a reload, a new tile - and on a slow
	// safety poll for the state that moves with time and fires no event (a crop's
	// growth stage, a habitat's rating). They used to walk every cell, bay and
	// furnishing and build their text keys every frame.
	uint32 PropsVersion = 1;
	bool bPassesDirty = true;
	double NextPassPollS = 0.0;
	bool bRoomsDirty = true;
	double NextRoomPassS = 0.0;
	void MarkPassesDirty() { bPassesDirty = true; bHoverDirty = true; bRoomsDirty = true; }
	// Cell lights follow the grid every frame, but only a change of the inputs
	// (grid state, which floors circulate, which lights exist) touches them.
	uint32 AppliedLightKey = 0;
	bool bLightKeyValid = false;
	uint32 LightMulVersion = 0;
	// Hover labels (director 2026-07-10): a registry of every tile and building
	// label with a position, bucketed in a grid; the cursor asks the grid for
	// what is near it and only labels that change state are touched.
	struct FHoverLabel { TWeakObjectPtr<UTextRenderComponent> Label; FVector2D Pos = FVector2D::ZeroVector; float RadiusCm = 0.f; };
	TArray<FHoverLabel> HoverLabels;
	TMap<int64, TArray<int32>> HoverGrid;
	TArray<int32> HoverLit;
	bool bHoverDirty = true;
	void RebuildHoverRegistry();
	void UpdateHoverLabels(const FVector& CursorCm);

	void HandleBuildingAdded(const FRHBuildingInstance& Instance);
	void HandleBuildingCompleted(const FRHBuildingInstance& Instance);
	// BM-3: a structure demolished (or a site cancelled) - its actor goes, and
	// with it the shell parts, crop props and lamps it owns.
	void HandleBuildingRemoved(const FRHBuildingInstance& Instance);
	void HandleCommandRejected(const FRHCommand& Cmd, const FString& Reason);
	void HandleQuotaMet(int32 Sol, double AwardKg);
	void HandleShipArrived(const TArray<FName>& Items);
	// The supply ship (2026-10-17, "Ship C"): a landing pad east of the colony,
	// a descent timed onto the sim's arrival, a sol on the pad, a lift-off.
	// Presentation only - the sim's ship is the quota phase and the ETA.
	void EnsureLandingPad();
	void SpawnSupplyShip(bool bLanded);
	void SetShipBurn(float Amount);
	void UpdateSupplyShip(const URHSimWorldSubsystem* Sim);
	void RestoreSupplyShip(const URHSimWorldSubsystem* Sim);
	float ShipPadTopCm() const;
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
	// The dig site's generated ground (2026-10-17): mound, cut, heap and rocks
	// from the deposit's state; the claim stakes the first time it is worked.
	void BuildDepositGround(AStaticMeshActor* Actor, const FRHDepositState& Deposit);
	void RefreshDepositMarkers(const URHSimWorldSubsystem* Sim);
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
	// Agri Gate C: the greenhouse dome, built in code on its deck - strut frame
	// always, glazing and soil beds once complete. Crop props per bed are
	// refreshed by RefreshDomeCrops (state-diffed on the beds' growth stages).
	void BuildGreenhouseDome(AStaticMeshActor* Actor, const FRHBuildingInstance& B, const FVector& Seated) const;
	void RefreshDomeCrops();
	// BM-2: a pressurized habitat shell's interior - one floor tile, label,
	// prop and lamp per bay, and walls DERIVED from the bays (exterior on an
	// open edge; a partition with a doorway between different rooms; a joint
	// with a doorway where modules of one network meet; nothing between bays
	// of the same room). No roof by default (rh.HabRoof 1 adds one);
	// rh.Cutaway 1 drops exterior walls to waist height, 2 = floorplan.
	// State-diffed per shell on a key of rooms + neighbours + rating.
	void RefreshHabShells();
	struct FRHHabShellVis { FString Key; TArray<TWeakObjectPtr<UActorComponent>> Parts; };
	TMap<int32, FRHHabShellVis> HabShellVis;
	// Per dome: the bed-stage key last drawn, and the crop props it drew.
	TMap<int32, FString> AppliedDomeCrops;
	TMap<int32, TArray<TWeakObjectPtr<UStaticMeshComponent>>> DomeCropProps;

	// Shaft & excavation mirror (M1-d Gate A2): a column actor for the trunk,
	// one floor tile per carved cell. The sim keeps carved cells as per-floor
	// COUNTS (A1 model); tiles lay out in a deterministic square spiral around
	// the shaft head - honest about how much is carved, canonical about where.
	// Flagged in the build log: spatial (painted-position) carving is a sim
	// upgrade for a later gate if the director wants placement-in-pocket rules.
	void UpdateShaftVisuals();
	void ApplyViewLevel();
	AStaticMeshActor* SpawnBox(const FVector& CenterCm, const FVector& ScaleM, const FLinearColor& Body, const FLinearColor& Emissive) const;

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
	// D8: the visibility rules go through these three. With the section off they are today's expressions exactly.
	bool ShowsLevel(int32 L) const { return !bSectionMode && L == ViewLevel; }
	bool ShowsSurface() const { return !bSectionMode && !IsUnderground(); }
	bool ShowsUnderground() const { return !bSectionMode && IsUnderground(); }

	// Room zoning made visible (M2 Gate B): tiles retint by designated function
	// and carry a small flat label. State-diffed per tick like the shaft mirror.
	void RefreshRoomVisuals();
	// D5 depth: a floor tile past the cool line darkens a step per three floors (a depth cue) and lerps toward a soft amber by its
	// floor's Warmth. The tile's own tint (the room pass sets it, and sets it again when the room changes) is the base, remembered per
	// material, so the multiplier never compounds. Re-applied only when a floor's Warmth crosses a 10% step (the sim's WarmthVersion).
	void ApplyDepthTint(AStaticMeshActor* Tile, int32 Level) const;
	void RefreshDepthTint(const class URHSimWorldSubsystem* Sim);
	mutable TMap<TWeakObjectPtr<class UMaterialInstanceDynamic>, FLinearColor> DepthTintBase;
	uint32 DepthTintKey = MAX_uint32;
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

	// Visual pass item 2: construction sites (docs/qa/2026-10-25/visual-pass-design.md section 2, DIRECTOR ANSWER Q-A
	// (a)). Every site on every floor draws from ONE holder actor with one HISM per kit part (scripts/blender/
	// rh_build_sitekit.py: RH_SitePad, RH_SiteScaffold, RH_SiteMast, RH_SiteCrate), so all sites cost 4 draws (and each
	// site one shadowless work light on its own actor, lit from dusk to dawn and always underground). Bays and the mast
	// stand only on ground nothing else takes (GatherSiteGroundTaken: other buildings' footprints and a floor's props and
	// furniture; RebuildSiteKit adds the bays and masts already placed for lower-Id sites, and underground every corner
	// of a slot must lie on a carved cell, so no bay stands in the rock or the pocket wall). A site's
	// own model, when it has one, rises inside the scaffold on its own actor through MIDs of M_RH_BuildReveal; completion
	// rebuilds that actor with the shipping MI. RefreshSites runs on the pass poll (a second, or sooner on any building
	// event) and writes a MID only when the 5 % step moves; the HISMs are rebuilt whole, and only when a site's instance
	// set changes (a site added or gone, a crate step, the scaffold growing at fabrication, the viewed floor) or the
	// ground round it may have (PropsVersion or the tile count moved since the last rebuild: a room prop, furniture, a
	// carved tile, a reload). A whole
	// rebuild is what keeps the per-site counts honest: removing one instance re-indexes the rest.
	static constexpr int32 SiteKitParts = 4; // pad tile, scaffold bay, mast, crate (the HISM order)
	struct FRHSiteVis
	{
		FName DefName;
		FVector SeatCm = FVector::ZeroVector;  // the building's seat: footprint centre on its ground or floor plane
		int32 Level = 0;
		int32 CellsX = 1;                      // the footprint in 2 m cells
		int32 CellsY = 1;
		float FinishedCm = 0.f;                // the height being built to (the model as drawn, or the stand-in's)
		int32 FullCourses = 1;                 // scaffold courses at the top of fabrication (one while it waits)
		int32 Courses = 0;                     // courses drawn: one while it waits, then a course ahead of the rising cut
		int32 CrateSlots = 2;                  // crates on the pad when the whole bill has arrived
		int32 Crates = -1;                     // crates drawn
		bool bFabricating = false;
		int32 Step = -1;                       // the progress step drawn, 0..20 (5 % each): delivery while waiting, then fabrication
		float RevealBottomCm = 0.f;            // the model's world Z span; RevealHeightCm 0 = no model to reveal
		float RevealHeightCm = 0.f;
		TArray<TWeakObjectPtr<UMaterialInstanceDynamic>> RevealMids;
		TWeakObjectPtr<UStaticMeshComponent> RevealMesh;
		int32 Count[SiteKitParts] = { 0, 0, 0, 0 }; // instances it holds now in each kit HISM (set by RebuildSiteKit)
		int32 BaysLeftOut = 0;                 // bay slots skipped because their ground is taken (set by RebuildSiteKit)
		int32 BaysInRock = 0;                  // of those, underground slots past the carved floor's edge
		int32 MastCorner = -1;                 // which open corner the mast took, -1 none (underground, or none free)
		TWeakObjectPtr<USpotLightComponent> WorkLight; // the site's work light (on its building's actor)
	};
	TMap<int32, FRHSiteVis> SiteVis;
	UPROPERTY() TObjectPtr<AActor> SiteKitActor;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> SiteKitISM;
	bool bSiteKitLooked = false;   // the kit meshes were looked up once (a missing kit warns once, then sites draw the legacy dressing)
	bool bSiteKitDirty = false;
	uint32 SiteKitPropsVersion = 0; // PropsVersion and TileByCell.Num() the kit was last built against: when either
	int32 SiteKitTileCount = -1;    // moves, the ground round a site may have changed and RefreshSites rebuilds
	int8 SiteLightsDay = -1;        // the work lights' last day / night answer (-1 = not yet pushed)
	bool EnsureSiteKit();
	void AddSite(const FRHBuildingInstance& Instance, AStaticMeshActor* Actor, const FVector& Seated, float ModelHeightCm);
	void RefreshSites(const URHSimWorldSubsystem* Sim);
	void RebuildSiteKit();
	void GatherSiteGroundTaken(TArray<FBox>& OutTaken) const;
	void ForgetSite(int32 BuildingId);

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
	TMap<int32, TWeakObjectPtr<AStaticMeshActor>> DepositMarkerById;
	TMap<int32, uint32> DepositShapeKey;  // what each dig site was last built from
	TSet<int32> DepositClaimDressed;       // stakes and beacon already placed
	double NextDepositRefreshRealSeconds = 0.0;
	FBox GroundPadBox = FBox(ForceInit);  // the map's MarsGround pad, once found
	bool bGroundPadMeasured = false;
	// (level, spiral index, 0) -> the tile actor + what function it last showed.
	TMap<FIntVector, TWeakObjectPtr<AStaticMeshActor>> TileByCell;
	TMap<FIntVector, FName> AppliedRoomTint;
	// Per-cell furnishing prop component (owned by its tile actor, so it is
	// GC-safe via CarveTileVisuals and inherits the tile's slice-view hiding).
	TMap<FIntVector, TWeakObjectPtr<UStaticMeshComponent>> RoomPropByCell;
	// Per-cell hab-light brightness multiplier by room type (professor-gated
	// 2026-08-18): folded into the tick loop's Want so brownout/circulation
	// behaviour stays untouched. Keyed like LightByCell.
	TMap<FIntVector, float> LightMulByCell;

public:
	// Read-only view of the furnished props, for the crew visualizer's
	// furniture avoidance (director 2026-08-17: "there's no collision so they
	// walk right through objects like desks and tables"). Key.X is the level.
	const TMap<FIntVector, TWeakObjectPtr<UStaticMeshComponent>>& GetRoomProps() const { return RoomPropByCell; }
	// BM-5: placed furniture components by cell (keys like the room props:
	// Key.X is the level, 0 for a habitat bay), for crew avoidance.
	const TMap<FIntVector, TArray<TWeakObjectPtr<UStaticMeshComponent>>>& GetFurnitureComps() const { return FurnitureCompsByCell; }

private:
	// BM-5: one component per placed piece, owned by its tile (underground)
	// or habitat actor (a bay) - floor-cut hiding and lifecycle come free -
	// reconciled against the sim every tick by signature (row, cell, slot,
	// rotation); a furnished cell's automatic room-prop hides.
	struct FRHFurnVis { TWeakObjectPtr<UStaticMeshComponent> Comp; uint32 Sig = 0; FVector FrameCm = FVector::ZeroVector; float YawDeg = 0.f; FVector2D HalfCm = FVector2D::ZeroVector; };
	TMap<int32, FRHFurnVis> FurnitureVis;
	// BM-6: door leaves in doored doorways (drawn by RefreshHabShells, owned
	// by the shell), keyed by the wall edge; they part for passing crew.
	struct FRHDoorLeaves { TWeakObjectPtr<UStaticMeshComponent> L; TWeakObjectPtr<UStaticMeshComponent> R; FVector Mid = FVector::ZeroVector; FVector Along = FVector::ZeroVector; float Open = 0.f; };
	TMap<FIntVector, FRHDoorLeaves> DoorLeaves;
	void AnimateDoors(float DeltaTime);
	TMap<FIntVector, TArray<TWeakObjectPtr<UStaticMeshComponent>>> FurnitureCompsByCell;
	TSet<FIntVector> PropsHiddenForFurniture;
	void RefreshFurniture(const URHSimWorldSubsystem* Sim);
	// Mesh per catalogue row; OutFitCm > 0 fits a room-scale prop's larger
	// horizontal extent to that size, 0 = the code-built pieces' true scale.
	static FString FurniturePropPath(FName Row, float& OutFitCm);

public:

private:
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
	// Cutaway bookkeeping. WallISM is a FILTERED VIEW of WallFaceXf: the rig
	// authors every face here, and ApplyCutaway re-adds only the visible ones.
	// WallVentDir parallels WallVents so a vent hides with the wall it is
	// mounted on. Key = (mode << 4) | hidden-side bitmask; 0xFF forces re-apply.
	TArray<FTransform> WallFaceXf;
	TArray<FIntPoint> WallFaceDir;
	TArray<FIntPoint> WallVentDir;
	FVector PitCenterCm = FVector::ZeroVector;
	uint8 AppliedCutawayKey = 0xFF;
	// Buildings that carry a real light (Floodmast). Intensity rides bPowered,
	// so a brownout darkens the city the player lit.
	TMap<int32, TWeakObjectPtr<UPointLightComponent>> BuildingLights;
	// Lived-in accumulation: crates/drums/lockers appear over time on populated
	// floors (count grows with residents + sols; components ride tile actors so
	// lifecycle and floor-cut hiding come free). Per-floor spawned count.
	TMap<int32, int32> ClutterSpawnedPerLevel;
	// The elevator at the shaft head: real cage mesh + two sliding door panels,
	// doors easing open whenever a colonist is near the shaft on the open floor.
	TWeakObjectPtr<UStaticMeshComponent> ElevatorCage;
	// The sliding door panels are GONE (director 2026-08-17: "the doors are
	// floating as they open and close... nothing encasing the doors").
	// Two free panels can never read as attached to an open lattice cage - the
	// mesh has no casing to pocket into, and two rounds of offset math
	// (2026-07-17, 2026-08-14) already failed to fake it. The approach cue the
	// doors carried (2026-07-09: the elevator reacting to a nearing colonist
	// "is what sells the ride") now lives in this lamp: the cage interior
	// warms up as crew approach. A light inside a lattice cannot detach.
	TWeakObjectPtr<UPointLightComponent> ElevatorLamp;
	float ElevatorWakeAlpha = 0.f;
	// 2026-10-14: the doors are BACK, on a cab that can hold them. RH_ElevatorCab
	// (scripts/blender/rh_build_elevator.py) is a solid car whose front wall
	// has pocket cavities; the two RH_ElevatorDoor leaves slide into those
	// pockets, so they can never float. Geometry contract: RHElevatorLayout.h.
	// Null when only the legacy lattice cage is present (then no doors).
	TWeakObjectPtr<UStaticMeshComponent> ElevatorDoorA;
	TWeakObjectPtr<UStaticMeshComponent> ElevatorDoorB;
	bool bElevatorIsCab = false;
	double ElevatorHoldUntil = 0.0;
	// Sovereignty visuals (M3/M4): marker per rival + its last-applied state
	// (0 normal / 1 embargoed / 2 defected / 3 sabotaged), and the trade rover.
	UPROPERTY() TMap<FName, TObjectPtr<AStaticMeshActor>> RivalMarkers;
	TMap<FName, uint8> RivalMarkerState;
	// E4: one trade rover per convoy out, by convoy Id (up to MaxConvoysOut), each along its own road.
	UPROPERTY() TMap<int32, TObjectPtr<AStaticMeshActor>> ConvoyVisuals;
	UPROPERTY() TObjectPtr<AStaticMeshActor> ShipVisual;
	UPROPERTY() TObjectPtr<AStaticMeshActor> LandingPadVisual;
	uint8 ShipBeat = 0;                // 0 away, 1 descending, 2 on the pad, 3 lifting off
	double ShipBeatStartReal = 0.0;    // real seconds: the burns run in real time
	double ShipLandedSimSeconds = -1.0;
	double ShipEtaLanded = -1.0;       // the sim ETA the current ship came down on
	TWeakObjectPtr<UPointLightComponent> ShipBurnLight;
	TWeakObjectPtr<class UMaterialInstanceDynamic> ShipMid;
	UPROPERTY() TObjectPtr<AStaticMeshActor> ShaftVisual;
	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> CarveTileVisuals;
	UPROPERTY() TObjectPtr<AActor> SliceRigActor;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> SkirtISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> WallISM;
	UPROPERTY() TObjectPtr<AActor> EnvironmentRigActor;
	bool bEnvironmentHidden = false;
	FIntVector LastRigKey = FIntVector(-999, -999, -999); // (depth, pit level, carved count)
	uint32 LastRigShaftKey = 0;                            // D2: the shaft list's version while there is more than one shaft (0 with one: the one-shaft key is unchanged)
	// D2: the elevator head of every shaft sunk after shaft 0 (index k - 1): an invisible anchor actor that owns a
	// static cab and a fill light, shown only on the floors that shaft reaches. Shaft 0's cage, doors and lamp stay the
	// members above. The cab does not animate doors, and crew still board at shaft 0 (D2 presentation is minimal).
	struct FExtraShaftVisual
	{
		TWeakObjectPtr<AStaticMeshActor> Anchor;
		TWeakObjectPtr<UStaticMeshComponent> Cab;
		TWeakObjectPtr<UPointLightComponent> Fill;
		int32 DepthSeen = 0;
	};
	TArray<FExtraShaftVisual> ExtraShafts;
	void UpdateExtraShaftVisuals(const class URHSimWorldSubsystem* Sim);   // spawn / respawn on a depth change
	void PlaceExtraShaftVisuals(const class URHSimWorldSubsystem* Sim);    // every tick and view change: position and visibility
	void ClearExtraShaftVisuals();
	TMap<int32, int32> TilesSpawnedPerLevel;
	TWeakObjectPtr<AActor> GroundActor;
	bool bGroundSearched = false;
	int32 ViewLevel = 0;
	bool bSectionMode = false;   // D8: the controller's mirror (see SetSectionMode)
	int32 SectionFocus = 0;
	int32 LastShaftDepthSeen = 0;
	FDelegateHandle AddedHandle;
	FDelegateHandle CompletedHandle;
	FDelegateHandle RejectedHandle;
	FDelegateHandle RemovedHandle;
	FString LastNotice;
	double LastNoticeRealSeconds = -1.0e9;
	FString LastAlert;
	double LastAlertRealSeconds = -1.0e9;
};
