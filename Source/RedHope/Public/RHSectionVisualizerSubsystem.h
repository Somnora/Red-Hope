#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHSectionVisualizerSubsystem.generated.h"

class AActor;
class UInstancedStaticMeshComponent;
class UTextRenderComponent;
class UStaticMesh;
class URHSimWorldSubsystem;

// D8 (the deep view, docs/qa/2026-10-23/d8-deep-view-design.md): the section. One look at the whole deep city, cut open along a vertical
// plane: every floor stacked at its real depth, the cut face and a few rows behind it (the thin ant farm, Q-D (a)), the shafts, their lift
// cars, the tunnels, the caverns, the districts, people as pips (Q-B (a)), and under TRAFFIC the lift load and the floor flows.
//
// A pure listener: it reads the sim's const accessors and draws; RedHopeSim never knows it exists, and nothing here is saved. The
// player controller owns every piece of view state (on, focus floor, axis, plane row) and pushes it here with SetView, exactly as it
// pushes the view level to the colony mirror. Everything is plain instanced static meshes, unlit (no light, no shadow): one pooled
// component per quantised colour. The rig actor is spawned on the first entry, and Tick returns before any sim read while the section
// is off, so the playtest world (which ticks this module) is untouched.
UCLASS()
class REDHOPE_API URHSectionVisualizerSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHSectionVisualizerSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// The controller's state. Axis 0..3 = the camera looks along +X, +Y, -X, -Y; PlaneRow is the depth coordinate (DepthOf) of the cut row.
	void SetView(bool bInOn, int32 InFocus, int32 InAxis, int32 InPlaneRow);
	bool IsOn() const { return bOn; }
	int32 GetFocus() const { return Focus; }
	int32 GetAxis() const { return Axis; }
	int32 GetPlaneRow() const { return PlaneRow; }

	// --- Pure helpers (static: the click test's unit checks run them with no world) ---
	// A lattice point of region 0's frame, seen along Axis: its depth away from the camera and its coordinate along the plane (rising
	// to the screen's right). DepthOf is (x, y, -x, -y)[Axis] and AlongOf (y, -x, -y, x)[Axis]; PointOf is their inverse.
	static int32 DepthOf(int32 InAxis, const FIntPoint& P);
	static int32 AlongOf(int32 InAxis, const FIntPoint& P);
	static FIntPoint PointOf(int32 InAxis, int32 Depth, int32 Along);
	// World directions (X, Y) of rising depth and rising along-coordinate.
	static FVector2D DirDepth(int32 InAxis);
	static FVector2D DirAlong(int32 InAxis);
	// R3: a point's row against the plane: -1 in front (not drawn), 0 on the cut (drawn with a cut face), +1 behind it.
	static int32 ClassifyRow(int32 Row) { return Row < 0 ? -1 : (Row == 0 ? 0 : 1); }
	// R3 option (a): the rows drawn behind and on the cut, the same on every floor (rh.SectionDepth, default 3).
	static int32 SlabRows();
	static bool InSlabRow(int32 Row, int32 Rows) { return Row >= 0 && Row < Rows; }
	// The draft's detail bands, measured from the focus floor: 0 = within one floor, 1 = within five, 2 = further.
	static int32 LodBand(int32 Level, int32 InFocus);
	// The carved extent over every floor and shaft column: depth and along ranges. False when nothing is carved.
	static bool ComputeExtent(const URHSimWorldSubsystem& Sim, int32 InAxis, int32& OutDMin, int32& OutDMax, int32& OutAMin, int32& OutAMax);

	// --- Geometry the controller and robots ask ---
	// R6: true iff a world point lies in the slab: a floor below the surface, a row in [0, rows), along the carved extent.
	bool IsInSlab(const FVector& PosCm) const;
	// R4: a ray against the vertical cut face (d = plane row - 0.5 cells). Gives the floor and the first carved lattice point behind
	// the cut on that column. False when nothing is hit.
	bool Pick(const FVector& RayOrigin, const FVector& RayDir, int32& OutLevel, FIntPoint& OutCell) const;
	// The cut face's centre on a floor: the middle of the first carved row-0 cells (the click test's plate to press). False when the floor
	// has none.
	bool LocatePlate(int32 Level, FVector& OutWorldCm) const;
	// The world point on the cut face for an along coordinate (cells) and a height.
	FVector CutFacePoint(double AlongCells, double ZCm) const;

	// RH.FrameStats: "plates 1,912, walls ...".
	FString DescribeCounts() const;
	int32 GetPlateCount() const { return CountPlates; }
	int32 GetRebuilds() const { return Rebuilds; }
	double GetLastRebuildMs() const { return LastRebuildMs; }
	double GetWorstRebuildMs() const { return WorstRebuildMs; }
	int32 GetOverlayRefreshes() const { return OverlayRefreshes; }
	int32 GetOverlayArrowCount() const { return OverlayArrows; }
	// D6d: the tram band as TRAFFIC last drew it - the chain cells drawn in the slab, and the lines that have any (a line outside the slab shows
	// only in the legend).
	int32 GetTramBandCells() const { return TramBandCells; }
	int32 GetTramBandLines() const { return TramBandLines; }
	bool IsSimplified() const { return bSimplified; }
	// What TRAFFIC's legend adds below ground (empty while the section is off).
	const FString& GetLegendText() const { return LegendText; }
	// Per-floor flows as the overlay last summed them: commuters leaving a floor (their home) and arriving (their work).
	void GetFlowSums(TMap<int32, FIntPoint>& Out) const { Out = FlowSums; }
	// The overlay's last people sums per (home floor, work floor) as an ordered list, for the click test's and the bench's checks.
	static void SumFlows(const URHSimWorldSubsystem& Sim, TMap<FIntPoint, int32>& OutPairs);

private:
	struct FFloorCache
	{
		int32 Band = -99;
		TMap<uint32, TArray<FTransform>> Boxes;   // colour key -> world transforms (unit cube scaled)
		int32 Plates = 0, Walls = 0, Blocks = 0, Pips = 0;
	};
	struct FCarSlot
	{
		FVector HeadCm = FVector::ZeroVector;
		int32 Shaft = 0, Index = 0, Count = 1, Depth = 0;
		FVector LaneOffsetCm = FVector::ZeroVector;
		FVector SizeCm = FVector::ZeroVector;
		float PhaseFloors = 0.f;
	};

	void EnsureRig();
	UInstancedStaticMeshComponent* PoolComponent(TMap<uint32, TWeakObjectPtr<UInstancedStaticMeshComponent>>& Pool, uint32 ColourKey);
	// Re-uploads only the colours whose instances changed (Sigs holds each colour's last list signature): a rebuild that moved a few floors
	// touches a few components, not every one.
	void ApplyPool(TMap<uint32, TWeakObjectPtr<UInstancedStaticMeshComponent>>& Pool, TMap<uint32, uint32>& Sigs, const TMap<uint32, TArray<FTransform>>& Lists);
	void ApplyAll();
	UTextRenderComponent* TextAt(TArray<TWeakObjectPtr<UTextRenderComponent>>& Pool, int32 Index, const FString& Text, const FLinearColor& Colour, const FVector& AtCm, float Size, bool bRightAligned);
	void HideTexts(TArray<TWeakObjectPtr<UTextRenderComponent>>& Pool, int32 FromIndex);
	void Hide();

	// bPeopleOnly: only who lives and works where moved (the pips and furniture of the full-detail floors), so only those floors redraw.
	void Rebuild(const URHSimWorldSubsystem& Sim, bool bPeopleOnly = false);
	void BuildFloor(const URHSimWorldSubsystem& Sim, FFloorCache& Out, int32 Level, int32 Band, bool bSimple);
	void BuildShared(const URHSimWorldSubsystem& Sim, TMap<uint32, TArray<FTransform>>& Out);
	void PlaceLabels(const URHSimWorldSubsystem& Sim);
	void UpdateCars(const URHSimWorldSubsystem& Sim, float DeltaTime);
	void RefreshOverlay(const URHSimWorldSubsystem& Sim);
	void ClearOverlay();
	void RefreshSurface(const URHSimWorldSubsystem& Sim);

	// One quantised colour -> one component. The key is 3 bits a channel (the 512-colour palette).
	static uint32 ColourKey(const FLinearColor& C);
	static FLinearColor ColourOfKey(uint32 Key);
	// A box given in (depth, along, z) around a centre, in cm relative to the origin, emitted as a world transform.
	void AddBox(TMap<uint32, TArray<FTransform>>& Lists, const FLinearColor& C, double DepthCm, double AlongCm, double ZCm, double SizeDepthCm, double SizeAlongCm, double SizeZCm) const;
	FVector WorldOf(double DepthCm, double AlongCm) const;

	bool bOn = false;
	bool bEverOn = false;
	int32 Focus = 0;
	int32 Axis = 0;
	int32 PlaneRow = 0;

	UPROPERTY() TObjectPtr<AActor> Rig;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> CarISM;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> PipISM;
	TMap<uint32, TWeakObjectPtr<UInstancedStaticMeshComponent>> StaticPool;
	TMap<uint32, uint32> StaticSigs;
	// What each source last drew, by colour; ApplyAll merges them into the one palette.
	TMap<uint32, TArray<FTransform>> StaticLists, OverlayLists, SurfaceLists;
	static constexpr uint32 PaletteSize = 512;
	TArray<TWeakObjectPtr<UTextRenderComponent>> FloorLabels;
	TArray<TWeakObjectPtr<UTextRenderComponent>> OverlayLabels;
	TWeakObjectPtr<UStaticMesh> Cube;
	FVector Origin = FVector::ZeroVector;     // region 0's origin (shaft 0's head), refreshed each rebuild
	double FloorH = 400.0;

	// What a rebuild last drew, and the composite key that says when to draw again.
	TMap<int32, FFloorCache> Floors;
	TArray<FCarSlot> CarSlots;
	uint32 DrawnKey = 0;
	bool bKeyValid = false;
	int32 DrawnFocus = MIN_int32, DrawnAxis = -1, DrawnPlane = MIN_int32, DrawnRows = -1;
	bool bSimplified = false;
	int32 AlongMin = 0, AlongMax = 0;         // the carved extent along the plane (cells), +- one, for IsInSlab and the framing of the labels
	int32 DepthMin = 0, DepthMax = 0;
	int32 CountPlates = 0, CountWalls = 0, CountBlocks = 0, CountPips = 0, CountCars = 0;
	int32 Rebuilds = 0;
	double LastRebuildMs = 0.0, WorstRebuildMs = 0.0;
	int32 Rows = 3;                            // rows drawn at the last rebuild
	int32 DeepestSeen = 0;

	// The cars' presentation clock, in sim seconds: advanced by the sim's speed at a rate clamped to two floors a real second (O5).
	double CarClockSec = 0.0;
	// What one rebuild gathers once and every floor reads: residents, buildings and furniture by floor.
	TMap<int32, TArray<int32>> ScratchResidents;   // level -> colonist ids housed on that floor
	TMap<int32, TArray<int32>> ScratchBuildings;   // level -> indices into GetBuildings()
	TMap<int32, TArray<int32>> ScratchFurniture;   // level -> indices into GetFurniture()
	uint32 StructKey = 0;
	bool bStructKeyValid = false;
	uint32 PeopleKey = 0;                      // jobs, beds and the roster: a live city moves these every few seconds
	double NextPeopleS = 0.0;                  // so a people-only redraw runs at most once a second
	int32 ComponentsUploaded = 0;
	double SlowestUploadMs = 0.0;              // diagnosis: the costliest component of the last rebuild
	FString SlowestUpload;              // the last rebuild's re-uploaded components (the spike line says how many)
	bool bFloorsSimple = false;

	// TRAFFIC below ground (2 Hz) and the surface strip.
	double NextOverlayS = 0.0;
	bool bOverlayShown = false;
	int32 OverlayRefreshes = 0, OverlayArrows = 0;
	int32 TramBandCells = 0, TramBandLines = 0;
	uint32 OverlayHash = 0;
	TMap<int32, FIntPoint> FlowSums;           // floor -> (out, in)
	FString LegendText;
	double NextSurfaceS = 0.0;
	uint32 SurfaceHash = 0;
	bool bLabelsOverlay = false;
};
