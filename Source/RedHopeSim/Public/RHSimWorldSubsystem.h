#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RHSimTypes.h"
#include "RHRoads.h"
#include "RHDistricts.h"
#include "RHSimWorldSubsystem.generated.h"

class URHDefinitionsSubsystem;
class URHSimClockSubsystem;
class URHAgentSubsystem;
struct FRHRobotRow;
struct FRHRoomRow;
struct FRHFurnitureRow;
struct FRHBuildingRow;

// A strategic order. Presentation can only reach the sim through these:
// commands enter the uplink queue, wait out the signal lag, then apply at a
// sim-tick boundary. The Earth-latency mechanic is a property of this seam.
USTRUCT()
struct REDHOPESIM_API FRHCommand
{
	GENERATED_BODY()

	UPROPERTY() FName Verb;                     // Build, Dig, ...
	UPROPERTY() FName Target;                   // definition/deposit row name
	// The queue handle the uplink panel cancels by. Serialized with the queue (save
	// v37) so a loaded order keeps its own Id: every loaded order used to come back
	// as Id 0, and the deck's cancel then removed the FIRST queued order whichever
	// one it was meant for.
	UPROPERTY() int32 CommandId = 0;
	UPROPERTY() FVector Location = FVector::ZeroVector;
	// C4: the second end point of a BuildRoad order (Location is the first).
	// Serialized with the uplink queue (save v36), so a queued road order keeps
	// both ends across a save and a load.
	UPROPERTY() FVector LocationB = FVector::ZeroVector;
	// Z-model (M1-b): the floor an order targets. 0 = surface; subsurface
	// floors become orderable when the shaft exists (M1-d).
	UPROPERTY() int32 Level = 0;
	UPROPERTY() double Value = 0.0;
	UPROPERTY() double IssuedAtSimSeconds = 0.0;
	UPROPERTY() double ExecuteAtSimSeconds = 0.0;
	// C5a: free text an order carries (RenameDistrict's new name). Serialized with the
	// queue (save v37). An FString, never an FName: names are case-sensitive.
	UPROPERTY() FString Text;
};

// A colonist as the sim knows them (M2 Gate A1). Colonists are population
// entries first - presentation agents arrive in Gate A2. Serialized (v10).
USTRUCT()
struct REDHOPESIM_API FRHColonist
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FString Name;               // deterministic callsign
	UPROPERTY() int32 HomeLevel = -1;       // the rated floor (<0), or 0 for a surface-habitat resident
	// BM-1: the pressurized hab (building Id) whose Living bay houses this
	// colonist; 0 = floor-housed. Surface residents carry HomeLevel 0.
	// Serialized inline in the colonist record (save v29).
	UPROPERTY() int32 HomeHab = 0;
	UPROPERTY() bool bSupported = true;     // life support met this step
	// Sustained time on short rations (C2a: a readout for the panels; recall is
	// driven by the home's departure accumulator, not by this clock). The
	// consequence stays abstract and prevention-framed - the mental-health
	// directive's discipline; wording is Gate-D review fodder.
	UPROPERTY() double UnsupportedSimSeconds = 0.0;
	// C2a: born on Mars (StepGrowth). Mars-born colonists leave last in a shortage.
	// Serialized in the colonist record (save v34).
	UPROPERTY() bool bMarsBorn = false;
	// Alive pass: sols worked per job function. Skill grows with practice -
	// output multiplier ramps 1x -> 2x at SkillMasterySols (linear = identical
	// across both time bands). Serialized sorted (save v24).
	TMap<FName, double> SkillSols;
};

// C4: one robot's planned trip along the roads. Runtime only (never saved): it is
// planned lazily the first time a robot drives to a target while any road is
// built, and re-planned only when that target changes. Presentation reads it to
// draw the robot exactly along the sim's waypoints.
struct FRHRobotRoadPath
{
	FVector TargetCm = FVector::ZeroVector; // the target the trip was planned for, compared exactly
	FVector StartCm = FVector::ZeroVector;  // where the robot stood when it was planned
	// Empty = drive straight (no road beats the straight line). Otherwise the
	// entry node .. exit node, then the target as the last waypoint.
	TArray<FVector> Waypoints;
	TArray<int32> LegEdge;                  // the edge each leg runs along to reach its waypoint; 0 = off road
	int32 Next = 0;                         // the leg being driven
	int32 Serial = 0;                       // new for every plan: presentation re-syncs on it
	bool IsRoadTrip() const { return Waypoints.Num() > 1; }
};

// A planted cell's crop state (greenhouse-agriculture Gate A, save v26).
// Parallel to PlantedCells: a planted cell WITHOUT an entry here is a legacy
// flat-yield cell (pre-crop saves, or crops dormant at plant time). Growth
// stage is derived from PlantedSol - never stored (era-parity by construction).
struct FRHPlantedCropState
{
	FName Crop;
	double PlantedSol = 0.0;
	double SoilKg = 0.0; // remaining bed soil; fertilizer refills, zero reverts the cell
};

// A placed structure as the sim knows it. Solids live here (approved hybrid
// logistics): InputKg feeds recipes/construction, OutputKg awaits hauling.
USTRUCT()
struct REDHOPESIM_API FRHBuildingInstance
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FName DefName;
	UPROPERTY() FVector LocationCm = FVector::ZeroVector;
	// Z-model (M1-b): the floor this structure occupies. Sim reasons in
	// floors; LocationCm.Z = Level x FloorHeightCm, presentation-only.
	UPROPERTY() int32 Level = 0;
	UPROPERTY() bool bUnderConstruction = false;
	UPROPERTY() bool bPowered = true;           // false when shed by priority
	UPROPERTY() double BuildRemaining_s = 0.0;
	UPROPERTY() double BatchRemaining_h = 0.0;  // > 0: recipe in progress
	UPROPERTY() FName ActiveRecipe;
	UPROPERTY() int32 AttachedDepositId = 0;    // extraction buildings (RequiresDeposit)
	// M1-d Gate A2: this batch was fuelled by Hydrogen at start (whole-batch
	// stock deducted up-front) - it draws idle grid power only and runs
	// straight through shedding. Cleared at batch completion.
	UPROPERTY() bool bBatchOnH2 = false;
	// Director ruling (M1-d hand-play): the player can switch a structure OFF
	// to stretch the battery through a storm - zero draw, zero gen, batches
	// frozen (even H2), until switched back on. Serialized (save v9).
	UPROPERTY() bool bManualOff = false;
	// D5a (director ruling 2026-10-17): a day-shift machine spends only the
	// daylight share of each step on its batch and draws idle power for the
	// rest (no new night starts; the batch pauses and nothing is lost). Manual
	// OFF still trumps it. A per-machine player choice, so it is saved inline in
	// the building record (save v33).
	UPROPERTY() bool bDayShift = false;
	// Agri Gate C (save v28): beds per side of a drag-sized structure (the
	// greenhouse dome). 1 x 1 for every fixed-size building.
	UPROPERTY() int32 SizeX = 1;
	UPROPERTY() int32 SizeY = 1;
	UPROPERTY() TMap<FName, double> InputKg;
	UPROPERTY() TMap<FName, double> OutputKg;
};

// A placed furnishing (BM-4, 2026-10-16). Key = the cell it stands in, in
// the station-key convention: (Level, CellIndex, 0) underground, (0, Bay,
// HabId) in a surface habitat bay. Slot = sy * 4 + sx of its min-corner 2.5 m
// sub-cell; Rot 0..3 quarter turns. Serialized (save v30).
USTRUCT()
struct REDHOPESIM_API FRHFurnitureInstance
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FName Row;
	UPROPERTY() FIntVector Key = FIntVector::ZeroValue;
	UPROPERTY() int32 Slot = 0;
	UPROPERTY() int32 Rot = 0;
};

// A wall fitting (BM-6, 2026-10-16) on a surface habitat's bay edge. Edge =
// (X, Y, Axis) on the 10 m bay lattice: the edge between bay point (X, Y)
// and (X+1, Y) (Axis 0) or (X, Y+1) (Axis 1) - one key whichever side it is
// seen from. Serialized (save v31).
USTRUCT()
struct REDHOPESIM_API FRHWallFeature
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FName Row;
	UPROPERTY() FIntVector Edge = FIntVector::ZeroValue;
};

USTRUCT()
struct REDHOPESIM_API FRHPowerState
{
	GENERATED_BODY()

	UPROPERTY() double GenW = 0.0;
	UPROPERTY() double LoadW = 0.0;
	UPROPERTY() double BatteryWh = 0.0;
	UPROPERTY() double BatteryCapWh = 0.0;
	UPROPERTY() bool bDeficit = false;
	UPROPERTY() int32 ShedCount = 0;           // buildings currently unpowered by shedding
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FRHOnStockChanged, FName, double);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnCommandExecuted, const FRHCommand&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnBuildingAdded, const FRHBuildingInstance&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnBuildingCompleted, const FRHBuildingInstance&);
// BM-3: a structure demolished or a site cancelled - the instance as it stood.
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnBuildingRemoved, const FRHBuildingInstance&);
// BM-4: a cell's furnishings changed (placed, sold, or its habitat demolished).
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnFurnitureChanged, const FIntVector& /*CellKey*/);
// BM-6: a wall fitting was placed or sold (Edge = the lattice edge key).
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnWallFeatureChanged, const FIntVector& /*Edge*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRHOnCommandRejected, const FRHCommand&, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnRobotsSpawned, const TArray<FMassEntityHandle>&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FRHOnQuotaMet, int32 /*Sol*/, double /*AwardKg*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnShipArrived, const TArray<FName>& /*Items*/);
// Alive pass: a beat of crew life. Type is one of the ERHCrewMoment values;
// A/B are colonist ids (B may be 0 for solo/whole-crew moments). Text is a
// presentation-ready line (Gate-D framing-review placeholder, like all copy).
DECLARE_MULTICAST_DELEGATE_FourParams(FRHOnCrewMoment, uint8 /*Type*/, int32 /*A*/, int32 /*B*/, const FString& /*Text*/);
// Alive pass: a celebratory milestone (first harvest, discovery, the vault...)
// - the golden-banner channel, distinct from OnAlert's warnings.
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnMilestone, const FString&);
DECLARE_MULTICAST_DELEGATE(FRHOnColonyReloaded);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnDepositDiscovered, const FRHDepositState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnSurveyCompleted, const FRHSurveyRecord&);
// Loud, transient, must-not-miss moments: event onsets (with the 1x snap),
// era refusals, ship arrival countdowns. Banner-weight in the deck -
// distinct from the notice line (director finding: the refusal read as
// "nothing happened").
DECLARE_MULTICAST_DELEGATE_OneParam(FRHOnAlert, const FString&);

// Single owner of colony truth. Its Tick is the sim driver: uplink ->
// task board -> production -> power, in that fixed order, per sub-step.
// Robot movement/work executes in Mass processors against the same
// published step count, mutating the economy only through the API below.
UCLASS()
class REDHOPESIM_API URHSimWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URHSimWorldSubsystem, STATGROUP_Tickables);
	}
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override
	{
		return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
	}

	// --- Command seam (in) ---
	void EnqueueCommand(FRHCommand Command);
	double GetOrderLagSeconds() const { return OrderLagSeconds; }
	void SetOrderLagSeconds(double Lag) { OrderLagSeconds = Lag; }
	const TArray<FRHCommand>& GetUplinkQueue() const { return UplinkQueue; }
	// Abort a queued order before its lag elapses (M0 spec §8: cancellable
	// until execution). Stopping your own transmission is instantaneous
	// mission-control action - no lag on the cancel itself.
	bool CancelUplinkCommand(int32 CommandId);

	// --- Territory ---
	// Every spatial query is per-level 2D (Z-model, M1-b): the shaft is the
	// only cross-level connector and it does not exist until M1-d. Level
	// defaults to the surface so agent-band callers stay unchanged until
	// robots can descend.
	bool IsInCoverage(const FVector& LocationCm, int32 Level = 0) const;
	// True if a Build order for Def at LocationCm would be accepted right now
	// (link/coverage rule, footprint overlap, import stock, material
	// sufficiency). The placement ghost polls this every frame; ExecuteCommand
	// runs the SAME check at uplink execution - the authoritative gate stays
	// at the seam, and the world may have changed during the signal delay
	// (that is the game).
	bool CanPlaceBuilding(FName DefName, const FVector& LocationCm, FString& OutReason, int32 Level = 0) const;
	// The same, for a drag-sized structure of SizeX x SizeY beds (Agri Gate C).
	bool CanPlaceBuildingSized(FName DefName, const FVector& LocationCm, FString& OutReason, int32 Level, int32 SizeX, int32 SizeY) const;
	// Drag-sized structures (the greenhouse dome). A Build order carries its
	// size in Value as SizeX * 1000 + SizeY (0 = 1 x 1).
	static double EncodeBuildSize(int32 SizeX, int32 SizeY) { return (double)(SizeX * 1000 + SizeY); }
	static FIntPoint DecodeBuildSize(double Value) { const int32 V = (int32)Value; return V <= 0 ? FIntPoint(1, 1) : FIntPoint(FMath::Max(1, V / 1000), FMath::Max(1, V % 1000)); }
	// Footprint half-extents in cm for a def at a size (2 m footprint cells).
	static FVector2D GetFootprintHalfCm(const FRHBuildingRow* Def, int32 SizeX = 1, int32 SizeY = 1);
	int32 GetDomeMaxBedsPerSide() const { return DomeMaxBedsPerSide; }
	// BM-1: a drag-sized def's cap per side (row MaxBedsPerSide, else the dome cap).
	int32 GetMaxBedsPerSide(const FRHBuildingRow* Def) const;
	// --- Surface habitats (BM-1, 2026-10-16) ---
	// A Pressurized shell is a grid of 10 m bays (footprint 5x5 per bay), its
	// bay centres on the world 10 m grid. Bay index = by * NX + bx (the dome-bed
	// convention). (0,0) when B is not a pressurized shell.
	FIntPoint GetBayGrid(const FRHBuildingInstance& B) const;
	static FIntPoint GetBayGridFor(const FRHBuildingRow* Def, int32 SizeX, int32 SizeY);
	static bool IsOnBayGrid(const FRHBuildingRow* Def, const FVector& LocationCm, int32 SizeX, int32 SizeY);
	static FVector SnapToBayGrid(const FRHBuildingRow* Def, const FVector& LocationCm, int32 SizeX, int32 SizeY);
	FVector GetBayCentreCm(const FRHBuildingInstance& B, int32 Bay) const;
	// The pressurized shell and bay under a surface point (any build state).
	bool FindBayAt(const FVector& LocationCm, int32& OutHabId, int32& OutBay) const;
	static int32 GetBedCount(const FRHBuildingInstance& B) { return FMath::Max(1, B.SizeX) * FMath::Max(1, B.SizeY); }
	// The site's whole bill: the def's (Level-taxed) bill x its bed count.
	TMap<FName, double> GetSiteBill(const FRHBuildingInstance& B) const;
	// --- Demolish (BM-3, 2026-10-16) ---
	// A Demolish order (Target "Building", Value = Id) takes a structure down
	// or cancels a site. True if it would be accepted right now; OutSalvage
	// (optional) = what comes back to the Lander. The card polls this and
	// ExecuteCommand runs the same check when the order lands. Salvage: a
	// site returns everything delivered (and an imported kit's crate); a
	// standing structure returns its held stock plus SalvageFraction of its
	// bill. Refused: the Lander, standing territory, a Borer once boring has
	// begun, a home, a hab with machines inside, and anything whose loss
	// would leave residents without a rated habitat or air.
	bool CanDemolishBuilding(int32 Id, FString& OutReason, TMap<FName, double>* OutSalvage = nullptr) const;
	bool HasBuilding(int32 Id) const { return FindBuildingById(Id) != nullptr; }
	double GetSalvageFraction() const { return SalvageFraction; }
	// --- Furniture (BM-4, 2026-10-16) ---
	// A 10 m cell or bay is a 4x4 grid of 2.5 m sub-cells. A Furnish order is
	// Target = row, Level, Location (a bay is found by location, like
	// Designate) and Value = EncodeFurnish(Cell, Slot, Rot). A Sell order is
	// Target "Furniture" + Value = piece Id, or Target "Cell" + Level and
	// Value = cell index (underground) or Location (bay): everything in it.
	static constexpr int32 FurnishGrid = 4;
	static double EncodeFurnish(int32 Cell, int32 Slot, int32 Rot) { return (double)(FMath::Max(0, Cell) * 64 + FMath::Clamp(Slot, 0, 15) * 4 + (Rot & 3)); }
	static void DecodeFurnish(double Value, int32& OutCell, int32& OutSlot, int32& OutRot)
	{
		const int32 V = FMath::Max(0, (int32)Value);
		OutCell = V / 64;
		OutSlot = (V / 4) % 16;
		OutRot = V % 4;
	}
	// Footprint in sub-cells at a rotation (odd turns swap the sides).
	static FIntPoint GetFurnitureSize(const FRHFurnitureRow& Row, int32 Rot);
	const TArray<FRHFurnitureInstance>& GetFurniture() const { return Furniture; }
	const FRHFurnitureInstance* FindFurniture(int32 Id) const;
	int32 GetCellFurniture(const FIntVector& Key, TArray<int32>* OutIds = nullptr) const;
	// The furnishable cell (or bay) under a point on a floor.
	bool ResolveFurnitureKey(int32 Level, const FVector& LocationCm, FIntVector& OutKey) const;
	FVector GetCellCentreCm(const FIntVector& Key) const;
	FName GetCellRoom(const FIntVector& Key) const;
	bool GetFurnitureFootprintCm(FName Row, const FIntVector& Key, int32 Slot, int32 Rot, FVector& OutCentre, FVector2D& OutHalf) const;
	// The slot that centres a W x H footprint on a point measured from the
	// cell's min corner (cm), clamped into the grid.
	static int32 AnchorSlotFor(const FVector2D& LocalFromCellMinCm, int32 W, int32 H);
	// Preflight for a Furnish order. The ghost polls it; ExecuteCommand runs it
	// again without the in-transit check (the order itself is no longer queued).
	bool CanPlaceFurniture(FName Row, const FIntVector& Key, int32 Slot, int32 Rot, FString& OutReason, bool bCheckQueued = true) const;
	bool CanSellFurniture(int32 Id, FString& OutReason, TMap<FName, double>* OutRefund = nullptr) const;
	bool CanSellCell(const FIntVector& Key, FString& OutReason, TMap<FName, double>* OutRefund = nullptr) const;
	// A cell's effective beds and work seats (furnished, or the legacy number).
	int32 GetCellBeds(const FIntVector& Key) const;
	int32 GetCellSeats(const FIntVector& Key) const;
	// What placing a row would do to the cell's beds and seats and to the
	// colony's Furnishings line - the sim's own replacement rule, for the hint.
	struct FRHFurnishPreview { int32 BedsBefore = 0; int32 BedsAfter = 0; int32 SeatsBefore = 0; int32 SeatsAfter = 0; double ComfortBefore = 0.0; double ComfortAfter = 0.0; };
	bool PreviewFurnishEffect(FName Row, const FIntVector& Key, FRHFurnishPreview& Out) const;
	// Derived each RefreshJobs, never saved: the bed / work-seat piece each
	// colonist uses (0 = a legacy bed or seat) - what animation will target.
	int32 GetColonistBedPiece(int32 ColonistId) const { return ColonistBedPiece.FindRef(ColonistId); }
	int32 GetColonistSeatPiece(int32 ColonistId) const { return ColonistSeatPiece.FindRef(ColonistId); }
	double GetFurnitureRefundFraction() const { return FurnitureRefundFraction; }
	double GetHopeFurnishingsMax() const { return HopeFurnishingsMax; }
	// The Hope weights (D3a): the suites derive their expectations from these
	// instead of hard-coding the tuning.
	double GetHopeHousingMax() const { return HopeHousingMax; }
	double GetHopePerMoralePoint() const { return HopePerMoralePoint; }
	double GetHopeJobsMax() const { return HopeJobsMax; }
	double GetHopeUnsupportedMax() const { return HopeUnsupportedMax; }
	double GetHopeUnsupportedSteepness() const { return HopeUnsupportedSteepness; }
	double GetHopeAdjacencyMax() const { return HopeAdjacencyMax; }
	double GetRoomServesResidents() const { return RoomServesResidents; }
	double GetComfortRefPop() const { return ComfortRefPop; }
	int32 GetFloorBeds(int32 Level) const { return CountFloorBeds(Level, nullptr); }
	// --- Wall fittings (BM-6, 2026-10-16) ---
	// Windows, doors and airlocks on the derived walls of BUILT surface
	// habitats. A Fit order is Target = row, Level 0, Location = a point in a
	// habitat bay and Value = the side (0 +X, 1 -X, 2 +Y, 3 -Y); Sell with
	// Target "WallFeature" + Value = Id refunds FurnitureRefundFraction.
	// Effects: a window adds Hope (Views, capped) and air leak; a habitat
	// network with a door in EVERY doorway is sealed into compartments and
	// leaks at the SealedBay rate; an airlock is the network's entrance
	// (cosmetic in the sim, ruling 6a). No fittings = the legacy habitat.
	enum class ERHWallEdge : uint8 { None = 0, Exterior, Open, Doorway };
	static FIntVector WallEdgeKey(const FIntPoint& BayPoint, int32 Dir);
	bool ResolveWallEdge(const FVector& LocationCm, int32 Dir, FIntVector& OutEdge) const;
	// What the edge is now; OutInsideHab / OutOutward describe an exterior
	// edge (the member it belongs to, the step from inside to outside).
	ERHWallEdge ClassifyWallEdge(const FIntVector& Edge, int32* OutInsideHab = nullptr, FIntPoint* OutInsidePoint = nullptr, FIntPoint* OutOutward = nullptr) const;
	FVector GetWallEdgeMidCm(const FIntVector& Edge) const;
	// The 3 x 3 m chamber outside an airlock edge (centre, half extents).
	bool GetAirlockChamberRect(const FIntVector& Edge, FVector& OutCentre, FVector2D& OutHalf) const;
	const TArray<FRHWallFeature>& GetWallFeatures() const { return WallFeatures; }
	const FRHWallFeature* FindWallFeature(int32 Id) const;
	const FRHWallFeature* FindWallFeatureOnEdge(const FIntVector& Edge) const;
	bool CanPlaceWallFeature(FName Row, const FIntVector& Edge, FString& OutReason, bool bCheckQueued = true) const;
	bool CanSellWallFeature(int32 Id, FString& OutReason, TMap<FName, double>* OutRefund = nullptr) const;
	// A habitat network's doorways and how many carry a door; sealed = all,
	// and at least one.
	void GetHabNetworkDoorways(int32 HabId, int32& OutDoorways, int32& OutDoored) const;
	bool IsHabNetworkSealed(int32 HabId) const;
	// The network's air leak per sol as the step integrates it (bays x the
	// leak rate x the sealed multiplier, plus its windows).
	double GetHabNetworkLeakKgPerSol(int32 HabId) const;
	double GetViewsHope(double* OutRaw = nullptr) const;
	double GetHopeViewsMax() const { return HopeViewsMax; }
	double GetSealedLeakMul() const { return SealedLeakMul; }
	// The greenhouse dome's garden row if B is a BUILT surface dome whose room
	// is live, else null. Its beds plant and grow like greenhouse cells.
	const FRHRoomRow* GetDomeRoom(const FRHBuildingInstance& B) const;
	// Bed state for presentation (Key (0, Bed, DomeId) in the planted maps).
	bool IsDomeBedPlanted(int32 DomeId, int32 Bed) const { return PlantedCells.Contains(FIntVector(0, Bed, DomeId)); }
	FName GetDomeBedCrop(int32 DomeId, int32 Bed) const;
	int32 GetDomeBedCropStage(int32 DomeId, int32 Bed) const;
	// Nearest slice deposit within MaxDistCm of a point, or nullptr (click-to-dig).
	const FRHDepositState* FindDepositNear(const FVector& LocationCm, double MaxDistCm, int32 Level = 0) const;
	int32 GetMaxDepth() const { return MaxDepth; }
	double GetFloorHeightCm() const { return FloorHeightCm; }

	// --- Shaft & excavation (M1-d Gate A) ---
	// The shaft is a vertical trunk bored down from ShaftHeadCm; a subsurface
	// floor joins the grid (power/O2/coverage carried by the trunk) once the
	// bore reaches its depth. Excavation on a reached floor carves 10x10 cells,
	// emitting regolith spoil to the shaft-head pile (hauled to the Forge in A2).
	int32 GetShaftDepth() const { return ShaftDepth; }
	bool IsLevelConnected(int32 Level) const { return Level == 0 || (Level < 0 && -Level <= ShaftDepth); }
	// Two floors exchange materials iff the trunk reaches both (the lift is
	// the only cross-level connector - underground proposal §5).
	bool AreLevelsLinked(int32 A, int32 B) const { return A == B || (IsLevelConnected(A) && IsLevelConnected(B)); }
	// Where a robot physically drives to serve a site (M1-d): its own floor's
	// point directly, or the SHAFT HEAD when the site is on another linked
	// floor - the lift carries the last leg (cargo down the trunk; the
	// fabricator works the head). Robots themselves stay surface-bound until
	// a later gate sends them below.
	FVector GetApproachPoint(const FRHSiteRef& Site, int32 RobotLevel) const;
	int32 GetFloorCarvedCells(int32 Level) const { const int32* C = FloorCarvedCells.Find(Level); return C ? *C : 0; }
	double GetSpoilPileKg() const { return SpoilPileKg; }
	FVector GetShaftHeadCm() const { return ShaftHeadCm; }
	// Designation reads (M1-d Gate A2): the player's standing orders the Borer
	// works through, batch by batch.
	int32 GetBoreTargetDepth() const { return BoreTargetDepth; }
	int32 GetCarveQueued(int32 Level) const { const int32* C = CarveQueue.Find(Level); return C ? *C : 0; }

	// --- Habitability chain (M1-d Gate B) ---
	// A carved floor is bored -> shielded (free from overburden; the M1-c
	// radiation curve) -> oxygenated (O2 fill mass scaling with carved volume,
	// pushed down the trunk by a circulator) -> circulated (a completed,
	// powered CirculatesAir station on the floor) before it rates Livable.
	// Dig wide without the ISRU to match and it stays a spacesuit-only void.
	double GetFloorO2Kg(int32 Level) const { const double* V = FloorO2Kg.Find(Level); return V ? *V : 0.0; }
	double GetFloorO2RequiredKg(int32 Level) const { return GetFloorCarvedCells(Level) * O2FillKgPerCell; }
	bool IsFloorCirculated(int32 Level) const;
	bool IsFloorRated(int32 Level) const { return RatedFloors.Contains(Level); }
	// Director ruling (2026-07-07f): a habitat must be at least this many
	// carved cells to certify Livable - a single 10x10 room is a sealed pocket,
	// not a vault. The atmosphere chain still runs below the minimum (the floor
	// pressurizes as you build toward it); only the RATING and the Phase-1 exit
	// gate on size. DT_Config row MinLivableCells.
	int32 GetMinLivableCells() const { return MinLivableCells; }
	// A floor that is circulated + fully pressurized for its current size but
	// still under the cell minimum: sealed, air-holding, not yet a habitat.
	// The reason the exit banner is withheld - surfaced so it never reads as a
	// silent failure.
	bool IsFloorSealedButSmall(int32 Level) const;
	// The Phase 1 exit (M1-d Gate C): true once ANY floor has ever rated
	// Livable - the colony's first vault. Fires the exit card; never unset
	// (losing the rating later is a crisis, not an un-achievement).
	bool HasVaultRating() const { return bVaultRated; }
	// BM-1: surface habitat networks. Shells sharing a bay edge form one
	// network (derived, never saved); a network rates Livable when every member
	// is full of O2, it is circulated (a powered Interior air circulator in any
	// bay, or a shaft link to a RATED floor -1), and it spans >= MinLivableCells
	// bays. A member of a rated network is "rated".
	bool IsHabRated(int32 HabId) const;
	int32 GetHabNetworkRoot(int32 HabId) const;      // min member Id; 0 = not a member
	int32 GetHabNetworkBays(int32 HabId) const;
	double GetHabO2Kg(int32 HabId) const;
	double GetHabO2RequiredKg(int32 HabId) const;    // bays x O2FillKgPerCell
	double GetHabNetworkO2Kg(int32 HabId) const;
	double GetHabNetworkO2RequiredKg(int32 HabId) const;
	bool IsHabNetworkCirculated(int32 HabId) const;
	bool IsHabNetworkSealedButSmall(int32 HabId) const;

	// --- Population (M2 Gate A1) ---
	// Colonists arrive as CrewPod cargo and can disembark only into certified
	// housing: rated floors x ColonistsPerCell. Each draws O2 from their home
	// floor's fill and Food from the pool - the habitability chain's first
	// consumers. Sustained loss of support evacuates them back to orbit.
	const TArray<FRHColonist>& GetColonists() const { return Colonists; }
	// A colonist by Id in O(1) (C1: the Id -> index map is derived, never saved,
	// and kept in step with arrivals, evacuations and loads). Null if no such Id.
	const FRHColonist* FindColonist(int32 Id) const;
	int32 GetPopulation() const { return Colonists.Num(); }
	// Total certified beds and the free remainder (capacity minus population).
	int32 GetHousingCapacity() const;
	int32 GetFreeHousing() const { return GetHousingCapacity() - Colonists.Num(); }
	double GetColonistEvacSols() const { return ColonistEvacSols; }
	double GetColonistFoodKgPerSol() const { return ColonistFoodKgPerSol; }
	double GetColonistO2KgPerSol() const { return ColonistO2KgPerSol; }
	// C2a: shared rationing. A HOME is a rated floor or a surface-habitat network;
	// food and water are colony-wide pools, air is per home. When a pool runs short
	// every resident of a home gets the same fraction of the ration. One entry per
	// home from the last population step, ascending Key; derived each step, never
	// saved (the departure accumulators and the deficit are).
	struct FRHHomeRation
	{
		int32 Key = 0;        // the floor level (<= 0), or the network's root hab Id (> 0)
		int32 HomeLevel = 0;
		int32 HomeHab = 0;    // a resident's hab Id; 0 = a floor
		int32 Residents = 0;
		double Fraction = 1.0; // the ration each resident got: min over food, water, air
		FName Limiting;        // Oxygen, Water or Food (the scarcest); None at a full ration
	};
	const TArray<FRHHomeRation>& GetHomeRations() const { return HomeRations; }
	// Sum over homes of residents x (1 - ration): the deficit Hope reads.
	double GetDeficitEquivalent() const { return DeficitEquivalent; }
	// A home's departure accumulator (one resident leaves each time it reaches 1)
	// and the residents it was counted on: the home's size when the shortage began
	// (raised if the home grew since). 0 for a home on a full ration.
	double GetHomeDepartureAcc(int32 HomeKey) const { const FRHHomeClock* C = HomeClocks.Find(HomeKey); return C ? C->Acc : 0.0; }
	int32 GetHomeOnsetResidents(int32 HomeKey) const { const FRHHomeClock* C = HomeClocks.Find(HomeKey); return C ? C->OnsetResidents : 0; }
	// "Floor -2" / "Surface habitat #6" (the network's root) - the name the rationing alerts use.
	FString GetHomeLabel(int32 HomeLevel, int32 HomeHab) const;
	// The ration this colonist's home got in the last step (1 = full). A home with
	// no entry yet (an arrival since the last step) reads as full.
	double GetColonistRation(const FRHColonist& C) const;
	// Harness/cheat: house N colonists into certified housing (respects the
	// capacity gate; returns how many actually found beds).
	int32 Debug_AddColonists(int32 Count);
	// C3 (living-city gate 3): the -scale suite's big colony as ONE debug call, so
	// the same city can be stood up in a running game (RH.CityBench) and measured
	// on screen. A shaft to -5 with 210 carved cells a floor (1,050), every floor
	// rated and zoned (living, work, lab, dining, gardens), furnished with bunks
	// and benches (about 3,400 pieces), air and power held in surplus, six work
	// stations assigned first; then colonists are moved in up to ColonistTarget (0 = the layout only).
	// DEBUG ONLY: it drives the same public orders a player would (excavate,
	// designate, furnish, assign) and the debug stock setters, advances the sim
	// clock, and adds no saved field. Needs a fresh colony (no shaft yet).
	struct FRHScaleColonyReport
	{
		bool bFresh = true;          // false: a shaft already existed, nothing was built
		bool bSetupOk = false;       // shaft to -Floors, every floor rated
		int32 Floors = 0;
		int32 CellsPerFloor = 0;
		int32 ZonedLiving = 0, ZonedSeat = 0, ZonedGarden = 0, ZonedOther = 0;
		int32 StationOrders = 0;
		int32 Rejected = 0;          // orders the sim refused while building
		int32 SeatsTotal = 0;        // work seats across the floors
		int32 Capacity = 0;          // certified beds
		int32 Housed = 0;            // colonists moved in by this call
		TArray<FIntPoint> WorkCells; // (level, cell), in zoning order
		TArray<FString> Errors;
	};
	FRHScaleColonyReport Debug_BuildScaleColony(int32 ColonistTarget);
	// --- Arrivals at scale (C2b, living-city gate 2b; every number a placeholder in RH_Config) ---
	// Births, immigration, the Big Arrival and moving home. Births scale per head
	// (the streak runs at max(1, N / BirthRefPop) x real time); each supply ship that
	// lands on a thriving colony brings settlers; a colony that holds its Hope draws
	// one big wave, once; and residents whose home has lost their bed move to a free
	// one. The wording of every alert is Gate-D placeholder copy.
	// How many settlers a ship brings: floor((Hope - Floor) / (100 - Floor) x
	// max(Base, PerCapita x N)), capped at the per-ship cap, at the free beds less
	// ImmigrationReserveBeds (births come first), and at the comfortable room left
	// (ComfortCapacity - Pop: settlers never push the Housing term below its maximum);
	// 0 below the Hope minimum. Pure arithmetic - the landing and the suites share it.
	int32 GetImmigrationCount(double Hope, int32 Pop, int32 FreeBeds, int32 ComfortCapacity) const;
	// C5a: GetImmigrationCount in its two halves, so the city's Housing demand measures
	// headroom with the SAME limits arrivals use. The raw part is what a ship would bring
	// if room allowed (the Hope draw, capped per ship: 0 below the Hope minimum or with
	// nobody here); the room part is min(FreeBeds - ReserveBeds, ComfortCapacity - Pop),
	// floored at 0. GetImmigrationCount = (FreeBeds <= 0) ? 0 : min(raw, room).
	int32 GetImmigrationRaw(double Hope, int32 Pop) const;
	int32 GetImmigrationRoom(int32 Pop, int32 FreeBeds, int32 ComfortCapacity) const;
	// The population the Housing term can serve at full value: the bed-weighted Living
	// cells and bays on rated floors and rated surface networks, exactly the number
	// GetColonyHope divides by (the helper both use). Settlers never push past it.
	int32 GetHousingComfortCapacity() const;
	int32 GetImmigrantsTotal() const { return ImmigrantsTotal; }
	double GetBigArrivalStreakSols() const { return BigArrivalStreakSols; }
	bool IsBigArrivalDone() const { return bBigArrivalDone; }
	double GetMoveHomeAccSols() const { return MoveHomeAccSols; }
	double GetHopeGrowthStreakSols() const { return HopeGrowthStreakSols; }
	double GetBirthRefPop() const { return BirthRefPop; }
	int32 GetColonistTransportColonists() const { return ColonistTransportColonists; }
	double GetColonistTransportFoodKgPerColonist() const { return ColonistTransportFoodKgPerColonist; }
	double GetImmigrationHopeMin() const { return ImmigrationHopeMin; }
	double GetImmigrationHopeFloor() const { return ImmigrationHopeFloor; }
	double GetImmigrationBase() const { return ImmigrationBase; }
	double GetImmigrationPerCapita() const { return ImmigrationPerCapita; }
	int32 GetImmigrationCapPerShip() const { return ImmigrationCapPerShip; }
	int32 GetImmigrationReserveBeds() const { return ImmigrationReserveBeds; }
	double GetBigArrivalHopeMin() const { return BigArrivalHopeMin; }
	double GetBigArrivalStreakSolsNeeded() const { return BigArrivalStreakSolsNeeded; }
	int32 GetBigArrivalFreeBeds() const { return BigArrivalFreeBeds; }
	int32 GetBigArrivalColonists() const { return BigArrivalColonists; }
	double GetMoveHomeIntervalSols() const { return MoveHomeIntervalSols; }
	// Harness: the settlers a supply ship lands, without the ship (returns how many
	// found beds); the smoothed Hope and the growth interval a suite pins; the Big
	// Arrival's flag; one move-home pass on its own (returns how many moved).
	int32 Debug_Immigrate() { return ApplyImmigration(); }
	void Debug_SetHopeSmoothed(double V) { HopeSmoothed = V; UpdateHopeBand(); }
	void Debug_SetHopeBase(double V) { HopeBase = V; }
	void Debug_SetHopeGrowthIntervalSols(double V) { HopeGrowthIntervalSols = V; }
	void Debug_SetBigArrivalDone(bool bDone) { bBigArrivalDone = bDone; }
	int32 Debug_RehomeResidents() { return RehomeResidents(); }
	void Debug_UnrateFloor(int32 Level) { RatedFloors.Remove(Level); } // a floor that lost its rating (the next StepHabitability may re-rate it)
	// C2a harness: a floor's O2 fill, and how many colonists a carved cell houses
	// without furniture (the legacy bed rule), so a suite can hold thousands
	// without furnishing thousands of beds.
	void Debug_SetFloorO2Kg(int32 Level, double Kg) { FloorO2Kg.FindOrAdd(Level) = FMath::Max(0.0, Kg); }
	void Debug_SetColonistsPerCell(double V) { ColonistsPerCell = FMath::Max(0.0, V); }
	// C2a harness: the smoothed-Hope gates for research and for births. Per head, a
	// city of thousands does not reach 75 or 85 (that is the design), and the
	// -scale suite wants the research and growth steps in what it times.
	void Debug_SetHopeGates(double DiscoveryThreshold, double GrowthThreshold) { HopeDiscoveryThreshold = DiscoveryThreshold; HopeGrowthThreshold = GrowthThreshold; }
	// C2a harness: one population step on its own (rationing, departures, alerts),
	// and the Mars-born flag a birth sets, so a suite can set up who leaves last.
	void Debug_StepPopulation(float SubDt) { StepPopulation(SubDt); }
	void Debug_SetMarsBorn(int32 ColonistId, bool bMarsBorn)
	{
		if (FRHColonist* C = const_cast<FRHColonist*>(FindColonist(ColonistId))) { C->bMarsBorn = bMarsBorn; }
	}
	// BM-1: beds in a hab's Living bays (x ColonistsPerCell) and who sleeps there.
	int32 GetHabBeds(int32 HabId) const;
	int32 GetHabResidents(int32 HabId) const;
	// Cheat (RH.Demo): certify a floor instantly - fill its O2 to required and
	// mark it Livable - so a demo crew can move in this frame instead of after
	// sols of fill. Only certifies if it's circulated and big enough (real gate).
	void Debug_ForceRateFloor(int32 Level);
	// Harness: set the Phase-1 vault flag without the milestone (BM-1 tests:
	// surface beds open to crew only once the vault is certified).
	void Debug_SetVaultRated(bool bRated) { bVaultRated = bRated; }
	// C1 harness: one agent-band sub-step (StepSim is private). The -scale suite
	// times it next to EraStep; the caller advances the clock first, as the
	// era-driving suites do.
	void Debug_StepSim(float SubDt) { StepSim(SubDt); }
	// BM-3 harness: post tasks, read one, and stock a given building's store.
	void Debug_StepTaskBoard() { StepTaskBoard(); }
	bool Debug_GetTask(int32 TaskId, FRHTask& Out) const;
	void Debug_AddSolidAt(int32 BuildingId, FName Resource, double Kg, bool bOutput);
	// Harness: run a cargo item's landing effect directly (the commandlet's
	// arrival tests skip the quota/manifest ceremony).
	void Debug_DeliverCargo(FName ItemName) { ApplyManifestItemEffect(ItemName); }

	// --- Rooms & the Hope index (M2 Gate B) ---
	// Carved cells take room FUNCTIONS (zoning, free - construction costs are a
	// later gate). Cells are addressed by carve index; the canonical square
	// spiral (below) maps index -> grid offset from the shaft head, so geometry
	// and presentation agree. Rooms FUNCTION only on rated floors; designation
	// is allowed anytime (planning ahead is free).
	// The deterministic carve layout, now sim-owned: adjacency is gameplay
	// (habitat vision §4), so the sim is the authority on where cell i IS.
	static FIntPoint SpiralCell(int32 Index);
	// Designate (or clear, RoomName=None) a room function on a carved cell.
	bool DesignateRoom(int32 Level, int32 CellIndex, FName RoomName, FString& OutReason);
	FName GetRoomAt(int32 Level, int32 CellIndex) const;
	const TMap<int32, TArray<FName>>& GetFloorRoomCells() const { return FloorRoomCells; }
	// BM-1: zone a habitat bay (None clears). Refuses fixed-room shells
	// (corridors), gardens (domes and underground grow food), a bay holding a
	// station module, and taking the last bed from a sleeping resident.
	bool DesignateBay(int32 HabId, int32 Bay, FName RoomName, FString& OutReason);
	// The zoned room, else the shell's fixed ProvidesRoom, else None.
	FName GetBayRoom(int32 HabId, int32 Bay) const;
	const TMap<int32, TArray<FName>>& GetHabBayRooms() const { return HabBayRooms; }
	const FRHBuildingInstance* FindModuleInBay(int32 HabId, int32 Bay) const;
	// The colonist's job: a seat in a Lab/Workstation cell on a rated floor,
	// assigned deterministically (shallowest floor, lowest cell, colonists by
	// Id). Presentation flavor + Hope input; production effects come later.
	FName GetColonistJob(int32 ColonistId) const { const FName* J = ColonistJobs.Find(ColonistId); return J ? *J : NAME_None; }
	// The Hope index (brief §5), Gate-B slice: a pure derived read - base +
	// housing quality + functioning rooms + jobs + milestones - adjacency
	// offenses - unsupported colonists - water/food scarcity, clamped 0..100.
	// No sim state; effects (work speed, growth) wire up in later gates after the framing review.
	struct FRHHopeBreakdown
	{
		double Base = 0, Housing = 0, Rooms = 0, Jobs = 0, Milestones = 0, Comforts = 0;
		double Furnishings = 0; // BM-4: bonus-only, capped (HopeFurnishingsMax)
		double Views = 0;       // BM-6: windows in rated habitats, bonus-only, capped (HopeViewsMax)
		double AdjacencyPenalty = 0, UnsupportedPenalty = 0, WaterPenalty = 0; // stored positive
		double FoodPenalty = 0; // D4a: stored positive - a food runway under FoodRunwayFloorSols at today's draw (soft cap HopeFoodPenalty)
		double CrisisPenalty = 0; // stored positive (M4 Gate D active crisis)
		double Solidarity = 0; // signed: + after Defy, - after Comply (M3 Gate C)
		int32 OffendedPairs = 0, FilledSeats = 0;
		// C2a (per head): the shares behind the terms. AdjacencyPenalty is
		// HopeAdjacencyMax x OffendedRefusers / RefuserCells (OffendedPairs stays the
		// raw pair count, for the layout hint); UnsupportedPenalty is
		// HopeUnsupportedMax x min(1, steepness x DeficitEquivalent / Population);
		// Jobs is HopeJobsMax x FilledSeats / Population.
		int32 OffendedRefusers = 0, RefuserCells = 0, Population = 0;
		double DeficitEquivalent = 0;
		double Total = 0;
	};
	FRHHopeBreakdown GetColonyHope() const;
	// Comforts (M2 Gate D, abstract slice): colonists who drew their luxury
	// ration this step. Pure Hope bonus - absence never penalizes (prevention
	// framing; all wording pending the Gate-D review).
	int32 GetComfortsSuppliedCount() const { return ComfortsSupplied; }

	// --- Hope drives the colony (M2 Gate D+, "what does Hope DO") ---
	// GetColonyHope() is the INSTANTANEOUS read; HopeSmoothed is its low-pass
	// integral (Tau ~ 3 sols) and is what actually drives effects, so a rec room
	// or a lost circulator moves the colony's MOOD over sols, not per-frame. The
	// smoother uses the exp(-dt/Tau) form so the agent band and the 60x era band
	// converge to the identical value (the load-bearing parity property).
	// Serialized (save v13). Named bands carry hysteresis so nothing flickers.
	enum class ERHHopeBand : uint8 { Failing = 0, Strained, Steady, Thriving, Flourishing };
	// M4 Gate D dynamic crises: a Malfunction (systemic blackout / robot-downtime
	// - the Destructive colony's self-inflicted crisis) or an Environmental test
	// (the Evolved colony's external strain). Which one fires is gated by the
	// HumanNature axis - the spec's "human evolution vs destruction" made mechanical.
	enum class ERHCrisis : uint8 { None = 0, Malfunction, Environmental };
	// M4 Gate D endings: where the two axes (Identity + HumanNature) point.
	enum class ERHEnding : uint8 { Undetermined = 0, CorporateJewel, IndependentFederation, MartianColdWar, Collapse };
	double GetHopeSmoothed() const { return HopeSmoothed; }
	ERHHopeBand GetHopeBand() const { return HopeBand; }
	const TCHAR* GetHopeBandName() const;
	// The human work-tempo multiplier this mood produces (0.60..1.25, 1.00 at
	// Hope 50). Applies ONLY to colonist labor (garden yield today; lab/bench
	// output as those gain mechanical outputs). Robots are morale-immune, so
	// every Phase-1 baseline is untouched. Exactly 1.0 at zero population - no
	// people, no tempo - which keeps the pre-crew regressions bit-identical.
	double GetHumanWorkTempo() const;
	// The Generational Carrot reads (deck + M3 identity axis).
	int32 GetBirthsOnMars() const { return BirthsOnMars; }
	bool HasFirstBorn() const { return bFirstBornAnnounced; }
	// Progress toward the next birth, 0..1 (0 when the colony isn't currently
	// eligible to grow - the deck shows a live "next arrival" bar only then).
	double GetGrowthProgress() const;
	bool IsGrowthEligible() const;

	// --- Discoveries (M2 Gate D+, the Flourishing layer) ---
	// Staffed Labs accrue research seat-hours while the colony's smoothed Hope
	// holds above HopeDiscoveryThreshold; each authored discovery pops in Order
	// (deterministic sequence, no RNG), granting a permanent Hope milestone and
	// an optional stock reward. The sequence ends in microbial life - the
	// landmark. Wording is Gate-D framing-review placeholder.
	const TArray<FName>& GetDiscoveryLog() const { return DiscoveryLog; }
	bool HasDiscovered(FName Name) const { return DiscoveryLog.Contains(Name); }
	// The next undiscovered row (None when the sequence is exhausted or the
	// table is absent) and the live progress toward it, 0..1.
	FName GetNextDiscovery() const;
	double GetDiscoveryProgress() const;
	// Whether Labs are currently accruing (flourishing + at least one staffed seat).
	bool IsResearchAccruing() const;
	// The smoothed-Hope gate for research (85) and the Thriving|Flourishing
	// enter edge (90): pure reads for the Crew panel and the suites.
	double GetHopeDiscoveryThreshold() const { return HopeDiscoveryThreshold; }
	double GetHopeFlourishingEnter() const { return HopeBandUp[3]; }

	// --- Rivals & trade (M3 Gate A, "The Neighbors") ---
	// One rover convoy runs barter with a rival settlement: dispatch is an
	// uplink verb (Convoy <Rival>); Hydrogen + SpareParts + your export lot are
	// ALL committed at departure (the Borer batch pattern); transit takes real
	// sols and FREEZES under an active dust storm; the return credits their
	// export lot (solids drop at the Lander for hauling, fluids join the pool)
	// and warms the per-rival relation - the dependency the Solidarity Dilemma
	// (Gate C) will later demand the player cut.
	FName GetConvoyRival() const { return ConvoyRival; }
	bool IsConvoyReturning() const { return bConvoyReturning; }
	// Round-trip progress 0..1 (0 when idle).
	double GetConvoyProgress() const;
	double GetRivalRelation(FName Rival) const;

	// --- Earth's Shadow (M3 Gate B) ---
	// Earth tension (0..100) rises while you have neighbors and gates a demand
	// from your sponsor nation. The identity axis (-100 Earth-aligned .. +100
	// Martian) is the through-line the Solidarity Dilemma (Gate C) moves and the
	// endings read. Both persist (save v18). Requisition awards scale with the
	// axis + tension - loyalty is rewarded, defiance and crisis shrink supply.
	// The whole layer is INERT until a rival is slice-active (a lonely colony's
	// awards and baseline are untouched). Gate C wires Comply/Defy to move these.
	double GetEarthTension() const { return EarthTension; }
	double GetIdentityAxis() const { return IdentityAxis; }
	bool IsEarthDemandPending() const { return bEarthDemandPending; }
	// The multiplier applied to a supply-ship award mass (1.0 when the Earth
	// layer is inert, so every pre-M3 award is unchanged).
	double GetRequisitionMultiplier() const;
	// Gate-C / cheat hooks (Comply/Defy will call these):
	void Debug_ShiftIdentity(double Delta) { IdentityAxis = FMath::Clamp(IdentityAxis + Delta, -100.0, 100.0); }
	void Debug_AddTension(double Delta) { EarthTension = FMath::Clamp(EarthTension + Delta, 0.0, 100.0); }
	void Debug_ShiftHumanNature(double Delta) { HumanNatureAxis = FMath::Clamp(HumanNatureAxis + Delta, -100.0, 100.0); }

	// --- The Solidarity Dilemma (M3 Gate C, the signature mechanic) ---
	// When a demand is pending, the player resolves it: COMPLY (cut trade with
	// the Martian settlements - the route CLOSES permanently, relations crater,
	// identity swings Earth-ward, tension is relieved, colonists grieve the
	// shrunken community) or DEFY (keep trading - identity swings Martian,
	// relations + colonist morale rise, but Earth's requisitions stay slashed by
	// your Martian axis and tension barely drops, so the next demand comes
	// sooner). ALL player-facing wording is Gate-D framing-review placeholder.
	// Returns false (with reason) if no demand is pending.
	bool ResolveDilemma(bool bComply, FString& OutReason);
	bool IsRouteClosed(FName Rival) const { return ClosedRoutes.Contains(Rival); }
	int32 GetClosedRouteCount() const { return ClosedRoutes.Num(); }
	// The lasting morale swing from recent choices (fades toward 0); folds into
	// the Hope index. Signed: negative after Comply, positive after Defy.
	double GetSolidarityHope() const { return SolidarityHope; }

	// --- The covert layer (M4 Gate A) ---
	// Two-layer diplomacy: Public_Standing is the per-rival relation (M3); this
	// adds HIDDEN tension - the secret hostility beneath the official smile. And
	// a second late-game axis, HumanNature (-100 Destructive/Predatory .. +100
	// Evolved/Diplomatic): HOW you treat others. Fair trade nudges it +; covert
	// theft/sabotage nudges it -. It gates which late-game crisis fires (Gate D).
	// A covert requisition steals a fraction of a rival's export goods; a
	// DETERMINISTIC (seeded, day/night-modulated) detection check decides whether
	// you get away clean (HiddenTension rises quietly) or are CAUGHT (relation
	// craters, an incident). No live RNG - the seed is (rival, attempt#).
	double GetHumanNatureAxis() const { return HumanNatureAxis; }
	double GetHiddenTension(FName Rival) const;
	// Resolve a covert requisition against a rival (the uplink verb's worker).
	// Returns false (with reason) if the rival isn't a valid target.
	bool ResolveCovert(FName Rival, FString& OutReason);
	// True while the sun is down (folds Module 2's night visibility into covert
	// detection). Deterministic from the sol clock.
	bool IsNight() const;

	// --- The espionage economy (M4 Gate B) ---
	// Launder: trade goods back to a rival to DEFUSE the HiddenTension you built
	// (a peace offering - costs their ImportLot, drops hidden tension + warms
	// public standing + nudges HumanNature toward Evolved). Sabotage: a covert
	// blackout that disrupts a rival's trade for SabotageDurationSols (detection-
	// gated like a covert op; a hostile act - HumanNature toward Destructive).
	// Discovery: a survey may uncover a DORMANT settlement, unlocking it for
	// trade/diplomacy. All deterministic, no combat.
	bool ResolveLaunder(FName Rival, FString& OutReason);
	bool ResolveSabotage(FName Rival, FString& OutReason);
	// A rival is available to interact with if it ships active OR was discovered.
	bool IsRivalAvailable(FName Rival) const;
	// Sols of sabotage disruption remaining on a rival (0 = operating normally).
	double GetSabotageRemaining(FName Rival) const;
	bool IsRivalDiscovered(FName Rival) const { return DiscoveredRivals.Contains(Rival); }

	// --- Earth's pre-emptive pressure (M4 Gate C) ---
	// Under high Earth tension, Earth turns a NEIGHBOR against you: that colony
	// EMBARGOES you (trade refused) and, if you don't act within EmbargoGraceSols,
	// DEFECTS (relation locks hostile, route closed). Your counter is Pacify -
	// spending INFLUENCE, a soft currency earned by fair dealing + Evolved
	// conduct (so the peaceful path earns the tools of peace). Distinct from the
	// Solidarity Dilemma (Earth demanding YOU cut trade) and pitched higher =
	// an escalation. All player-facing wording is Gate-D framing-review placeholder.
	double GetInfluence() const { return Influence; }
	bool IsEmbargoed(FName Rival) const { return EmbargoGrace.Contains(Rival); }
	bool HasDefected(FName Rival) const { return DefectedRivals.Contains(Rival); }
	double GetEmbargoGrace(FName Rival) const;
	// The rival currently embargoing you (None if none) - the deck's "who to pacify".
	FName GetEmbargoingRival() const;
	// Pacify an embargoing rival (the uplink verb's worker): spend Influence, the
	// colony ignores Earth. Returns false (with reason) on cost/target failure.
	bool ResolvePacify(FName Rival, FString& OutReason);

	// --- Dynamic crises + the endings framework (M4 Gate D, the finale) ---
	// Once the late game is live, crises fire on a cadence; WHICH one is gated by
	// your HumanNature axis (a Destructive colony draws self-inflicted Malfunction
	// blackouts; an Evolved colony draws survivable Environmental tests) - the
	// spec's thematic payoff. A Malfunction slows human work (CrisisWorkPenalty)
	// + weighs on Hope; an Environmental strains Water + a lighter Hope hit. Both
	// are deterministic, recoverable downtimes - never feral, per the on-brief
	// ruling. The endings read maps (IdentityAxis, HumanNatureAxis) to one of the
	// brief's four outcomes. All player-facing wording is Gate-D review placeholder.
	ERHCrisis GetActiveCrisis() const { return ActiveCrisis; }
	const TCHAR* GetActiveCrisisName() const;
	double GetCrisisRemaining() const { return CrisisRemainingSols; }
	// Which ending the colony is trending toward (a pure read of the two axes +
	// a collapse check); Undetermined while the colony is still contested.
	ERHEnding GetProjectedEnding() const;
	const TCHAR* GetEndingName(ERHEnding Ending) const;
	// --- Ending resolution (save v25): the game structure closes its loop.
	// Once a path is DECLARED (bEndingDeclared), holding the same projected
	// ending for EpilogueSustainSols fires the one-time EPILOGUE card - the
	// story of this Mars is written; the sandbox keeps running. Collapse
	// (had crew, lost it all) declares its own one-time card in
	// StepPopulation - abstract and prevention-framed with the recovery path
	// named (Gate-D wording review owns the final copy).
	ERHEnding GetDeclaredEnding() const { return (ERHEnding)DeclaredEnding; }
	bool IsEndingDeclared() const { return bEndingDeclared; }
	bool IsEpilogueDeclared() const { return bEpilogueDeclared; }
	bool IsCollapseDeclared() const { return bCollapseDeclared; }
	double GetPathSustainSols() const { return PathSustainSols; }
	// Harness/cheat: force the crisis selector to fire now (natural cadence is in
	// StepCrisis). Returns the crisis that fired.
	ERHCrisis Debug_TriggerCrisis() { TriggerCrisis(); return ActiveCrisis; }
	// Harness knobs (v25 suites): shrink the epilogue hold, bank funding
	// directly, spin the weather cycle, arm the workstation fab bonus.
	void Debug_SetEpilogueSustainSols(double Sols) { EpilogueSustainSols = Sols; }
	void Debug_AddResearchFunding(double Kg) { PendingResearchFundingKg += Kg; }
	void Debug_SetWeatherCycleSols(double Sols) { WeatherCycleSols = Sols; }
	void Debug_SetWorkstationFabBonus(double PerSeat) { WorkstationFabBonusPerSeat = PerSeat; }

	// --- Station tiers (tiered-production spec, Gate A) ---
	// Sum of YieldMul over FILLED seats per family (derived with ColonistJobs,
	// never saved). Plain T1 content: equals the filled-seat count exactly.
	double GetLabYieldSeatSum() const { return LabYieldSeatSum; }
	double GetWorkstationYieldSeatSum() const { return WorkstationYieldSeatSum; }
	// Construction-speed multiplier from staffed workstation-line seats
	// (1.0 + PerSeatBonus x yield-seat-sum). Rides the same integrator slot as
	// FabricatorSpeedMul; zero pop or zero bonus = exactly 1.0 (baselines safe).
	double GetWorkstationFabMul() const { return 1.0 + WorkstationFabBonusPerSeat * WorkstationYieldSeatSum; }
	// Infirmary (passive, prevention-framed): any slice-active Infirmary-
	// function cell on a RATED floor stretches the evacuation countdown -
	// more time to fix the missing leg, never a penalty.
	bool HasInfirmaryOnline() const;
	double GetEffectiveEvacSols() const { return ColonistEvacSols + (HasInfirmaryOnline() ? InfirmaryEvacBonusSols : 0.0); }
	// Prefab stations (tiered production T1, 2026-10-15): the room a building
	// provides - null if none or the room row is dormant. bRequireOnline adds:
	// built, switched on, powered, on a rated floor.
	const FRHRoomRow* GetProvidedRoom(const FRHBuildingInstance& B, bool bRequireOnline) const;
	// The carved cell (spiral index) under a point on a subsurface floor, or
	// INDEX_NONE - the inverse of SpiralCell around the shaft head.
	int32 FindCarvedCellAt(int32 Level, const FVector& LocationCm) const;

	// --- Staffing (tiered production T2/T3, save v27) ---
	// A station is a seat-bearing room cell, key (Level, Cell, 0), or a prefab
	// module, key (Level, INDEX_NONE, BuildingId). The seat-priority list holds
	// the stations the player ASSIGNED staff to (uplink verb "Assign"), newest
	// first; they fill before the default walk (floors shallow->deep, cells in
	// carve order, then modules by Id). An empty list is the default exactly.
	const TArray<FIntVector>& GetSeatPriority() const { return SeatPriority; }
	int32 GetSeatPriorityRank(const FIntVector& Key) const { return SeatPriority.IndexOfByKey(Key); }
	// The station a colonist works at (derived each refresh; false = idle).
	bool GetColonistStation(int32 ColonistId, FIntVector& OutKey) const
	{
		const FIntVector* K = ColonistStations.Find(ColonistId);
		if (K) { OutKey = *K; }
		return K != nullptr;
	}
	// Seats filled and seat capacity at a station (0/0 if it is not one).
	void GetStationSeats(const FIntVector& Key, int32& OutFilled, int32& OutCapacity) const;
	static constexpr int32 MaxSeatPriority = 32;
	// A work station's function: its seats take colonists.
	static bool IsSeatFunction(FName Fn)
	{
		static const FName NLab(TEXT("Lab")), NWorkstation(TEXT("Workstation")), NGarden(TEXT("Garden"));
		return Fn == NLab || Fn == NWorkstation || Fn == NGarden;
	}
	// Lazy seat fill (T3): with LazySeatFillSols > 0, unassigned seats are
	// not filled at once - an idle colonist takes charge of an open seat each
	// LazySeatFillSols. 0 (the shipped default) = instant fill, as before.
	int32 GetStaffedSeats() const { return StaffedSeats; }
	void Debug_SetLazySeatFillSols(double Sols) { LazySeatFillSols = FMath::Max(0.0, Sols); }
	// The station module (any build state) standing on a carved cell, or null.
	// One station per cell: modules refuse zoned cells and zoning refuses
	// module cells.
	const FRHBuildingInstance* FindModuleOnCell(int32 Level, int32 Cell) const;

	// --- The garden (M2 Gate C) ---
	// A Garden-zoned cell on a RATED floor auto-plants when the colony holds
	// the soil and seeds (the SoilPallet/SeedVault gamble paying off), then
	// yields Food per sol while Water holds out. Rating loss makes the crop
	// dormant, never dead (prevention framing); re-zoning a planted cell
	// forfeits its soil, loudly. Grow-light power draw is a flagged later gate.
	bool IsGardenPlanted(int32 Level, int32 CellIndex) const { return PlantedCells.Contains(FIntVector(Level, CellIndex, 0)); }
	int32 GetPlantedCellCount() const { return PlantedCells.Num(); }
	// Producing = planted + floor rated + fed by Water this step (the deck's
	// honest "your farm is actually running" read).
	int32 GetProducingCellCount() const { return ProducingCells; }
	double GetGardenFoodKgPerSolPerCell() const { return GardenFoodKgPerSolPerCell; }
	double GetGardenWaterKgPerSolPerCell() const { return GardenWaterKgPerSolPerCell; }
	// Garden power fork reads (deck): the grow-light load this step, and whether
	// the bank went dark on it (a legibility cue - "your gardens lost power").
	double GetGardenPowerDrawW() const { return GardenPowerDrawW; }

	// --- Crops (greenhouse-agriculture spec 2026-07-10, Gates A/B/D) ---
	// With ANY DT_Crops row slice-active, newly planted cells carry a named
	// crop: real grow time (no yield until mature), per-crop water draw,
	// climate preference, bed-soil depletion + fertilizer refill, and (ducted)
	// CO2/O2 exchange with the crew. With every crop dormant, the garden
	// reproduces the legacy flat-yield math exactly - baseline untouched.
	bool AreCropsLive() const;
	// Cumulative garden yield (pure production, unaffected by consumption -
	// what the self-tests measure against).
	double GetGardenFoodCumKg() const { return GardenFoodCumKg; }
	// Test knob: soil-depletion rate (the -agri suite spends a bed in one sol
	// instead of ten; same linear shape, just faster).
	void Debug_SetCropSoilDepleteKgPerSol(double V) { CropSoilDepleteKgPerSol = V; }
	FName GetCellCrop(int32 Level, int32 CellIndex) const;
	// Presentation read: 0 sprout / 1 young / 2 mature; -1 legacy or unplanted.
	int32 GetCellCropStage(int32 Level, int32 CellIndex) const;
	double GetCellSoilKg(int32 Level, int32 CellIndex) const;
	// Per-floor climate (Gate B): Temperate default. Humid/Arid only take
	// EFFECT with a powered HumidityRegulator on the floor (else Temperate).
	bool SetFloorClimate(int32 Level, FName Band, FString& OutError);
	FName GetFloorClimateSetting(int32 Level) const;
	FName GetFloorClimateEffective(int32 Level) const;
	bool HasRegulatorOnline(int32 Level) const;
	// Ducts (Gate D): a ducted garden floor breathes with the colony - crops
	// draw the crew's exhaled CO2 (yield bonus while the pool holds) and emit
	// O2 back into the colony supply, offsetting leakage.
	bool DesignateDuct(int32 Level, FString& OutError);
	bool RemoveDuct(int32 Level) { return DuctedFloors.Remove(Level) > 0; }
	bool IsFloorDucted(int32 Level) const { return DuctedFloors.Contains(Level); }
	double GetColonyCO2Kg() const { return ColonyCO2Kg; }
	bool AreGrowLightsDark() const { return bGrowLightsDark; }
	// Water loop reads (deck): pool potability 0..1 and its penalty threshold.
	double GetWaterPotability() const { return WaterPotability; }
	double GetWaterPotabilityFloor() const { return WaterPotabilityFloor; }
	double GetColonistWaterKgPerSol() const { return ColonistWaterKgPerSol; }
	// D4a food runway: pooled Food stock / (Pop x ColonistFoodKgPerSol), the
	// gross "at today's draw" figure the FOOD tile, guide and playtest use.
	// 1.0e9 at zero population (the sentinel they already use).
	double GetFoodRunwaySols() const;
	double GetFoodRunwayFloorSols() const { return FoodRunwayFloorSols; }
	double GetFoodRunwayRearmSols() const { return FoodRunwayRearmSols; }
	double GetHopeFoodPenaltyMax() const { return HopeFoodPenalty; }
	// Harness: inject fresh ice-melt water (restores potability like the real
	// IceDrill->WaterPlant chain, without scripting the whole ISRU economy).
	void Debug_AddFreshWater(double Kg) { AddStock(FName("Water"), Kg); FreshWaterThisStepKg += Kg; }
	// Ice running low (D2a): the one low-deposit predicate (alert, guide,
	// inspect card, playtest). Fraction is RH_Config IceLowAlertFraction.
	bool IsIceDepositLow(const FRHDepositState& D) const;
	double GetIceLowAlertFraction() const { return IceLowAlertFraction; }
	// Any ice body no scout has surveyed yet (ground truth - the deck copy
	// leaks no more than the guide already does).
	bool HasUndiscoveredIce() const;
	// What the player can still do about a low ice deposit: -1 nothing to
	// resolve, 0 find more ice, 1 carry the grid to healthy ice, 2 drill it,
	// 3 melt it where it is drilled (no finished Water Plant near the drill).
	int32 GetIceExpansionStage() const;

	// --- Storm power discipline (director ruling, M1-d hand-play) ---
	// "Shut off some of the robots, tools, areas so you're not wasting battery
	// when you don't know when you'll get valuable sun again." Manual per-
	// structure off switch + a colony-wide fleet hold; both survive save/load.
	bool SetManualPower(int32 BuildingId, bool bOn);
	bool IsManualOff(int32 BuildingId) const;
	// --- Day shift (D5a, director ruling 2026-10-17) ---
	// A heavy machine on the day shift works only while the sun is up (the window
	// is RH_Config DayShiftStartFrac..DayShiftEndFrac of the sol) and rests on idle
	// power at night. Both the production budget and the above-idle draw of a step
	// scale by that step's DAYLIGHT SHARE, which is linear in dt - so total work
	// and energy per sol are the same at any step size or alignment (era parity).
	// Offered only where a machine has something to pause: PowerDraw > PowerIdle
	// and a recipe (or the Borer's bore/carve work) - never life support, rooms or
	// pads. Manual OFF still overrides, shedding and load priority are unchanged.
	bool SetDayShift(int32 BuildingId, bool bOn);
	bool IsDayShift(int32 BuildingId) const;
	bool CanDayShift(FName DefName) const;
	// RH_Config DayShiftDefault_<Def> (and the DayShiftDefaultsOn A/B master
	// switch): a NEW instance of this definition starts on the day shift.
	bool IsDayShiftDefault(FName DefName) const;
	// On the day shift, finished, not switched off, and the sun is down.
	bool IsDayShiftResting(int32 BuildingId) const;
	int32 GetDayShiftRestingCount() const;
	// Sun above the day-shift window's edge right now (point test on the clock).
	bool IsDaylightNow() const;
	// A stable key for the night the clock is in (changes at sunset; it also holds
	// through the daylight that follows it, so pair it with IsDaylightNow()).
	int32 GetNightIndexNow() const;
	// The night key of the last night the grid shed something (the bank ran dry
	// after sunset), -1 if none since boot or load. A runtime latch, never saved
	// and read by nothing in the sim: the guide shows its day-shift step for the
	// rest of THAT night instead of reading the shed flag, which flips every sim
	// step while the bank scrapes empty (photosensitivity: nothing may flash).
	int32 GetLastBlackoutNight() const { return LastBlackoutNight; }
	// The share of [T0, T1] (sim seconds) inside the day-shift window, 0..1.
	// Pure and wrap-safe; public so the -dayshift suite can pin it.
	double GetDaylightFraction(double T0, double T1) const;
	double GetDayShiftStartFrac() const { return DayShiftStartFrac; }
	double GetDayShiftEndFrac() const { return DayShiftEndFrac; }
	bool GetDayShiftDefaultsOn() const { return bDayShiftDefaultsOn; }
	// Held robots finish their current task, then claim nothing new (they
	// idle/dock instead of burning charge on work).
	void SetFleetHold(bool bHold);
	bool IsFleetHeld() const { return bFleetHold; }
	// Bore the trunk down to ToDepth floors below surface (clamped to MaxDepth);
	// emits shaft spoil per newly bored floor. The shaft head (surface column)
	// is fixed on the first bore. Never retracts.
	void ExtendShaft(int32 ToDepth, const FVector& HeadCm);
	// Carve Cells (10x10 units) on a reached subsurface floor; emits per-cell
	// spoil. Fails (with reason) if the floor is not reached.
	bool ExcavateFloor(int32 Level, int32 Cells, FString& OutReason);

	// --- Reads (out) ---
	const TArray<FRHBuildingInstance>& GetBuildings() const { return Buildings; }
	const TArray<FRHDepositState>& GetDeposits() const { return Deposits; }
	const FRHPowerState& GetPower() const { return Power; }
	int32 GetOpenTaskCount() const { return Tasks.Num(); }
	const TArray<FRHTask>& GetTasks() const { return Tasks; }
	// Colony-held solids: sum of building stores (deposit piles are not
	// collected yet, so they do not count toward quota).
	double GetTotalSolid(FName Resource) const;
	// Network-instant pool (fluids/gases/abstract per hybrid logistics).
	double GetStock(FName Resource) const;
	void AddStock(FName Resource, double Delta);
	// Quota progress vs the active quota (bills scaled by the repeat round, D1a): resource -> (have, need).
	TMap<FName, TPair<double, double>> GetQuotaProgress() const;

	// --- Roads (C4, 2026-10-18) ---
	// You lay roads; robots build them (labour only), then drive them 1.6x faster.
	// A BuildRoad uplink order carries both end points (Location and LocationB),
	// snapped to the road grid. The bill (Struct per metre of NEW road) is taken
	// from the stores the moment the order lands, whole or not at all; there is no
	// haul. Each new edge is a labour-only Build task for a Fabricator.
	struct FRHRoadPreview
	{
		int32 Pieces = 0;          // new edges this order lays
		int32 Splits = 0;          // existing edges it splits (junctions, crossings)
		int32 LengthCm = 0;        // the new road's length
		double BillKg = 0.0;       // Struct the order costs
		double HaveKg = 0.0;       // Struct the stores hold
		TArray<TPair<FVector, FVector>> NewSegmentsCm; // each new edge's end points, for the ghost
		TArray<FVector> NodesCm;                       // every junction and bend the order touches
	};
	const FRHRoadGraph& GetRoadGraph() const { return RoadGraph; }
	const FRHRoadParams& GetRoadParams() const { return RoadParams; }
	bool HasBuiltRoads() const { return RoadGraph.BuiltEdgeCount > 0; }
	double GetRoadStructKgPerMeter() const { return RoadStructKgPerMeter; }
	double GetRoadBuildMetersPerHour() const { return RoadBuildMetersPerHour; }
	double GetRoadSpeedMul() const { return RoadSpeedMul; }
	// A world point snapped to the road grid (Z = 0).
	FVector SnapToRoadGrid(const FVector& LocationCm) const;
	// True if a BuildRoad order from FromCm to ToCm would be accepted right now
	// (both ends and every junction in coverage, the strip clear of every building,
	// site and in-flight order, the bill affordable). The ghost polls it every
	// frame; ExecuteCommand runs the SAME check when the order lands.
	bool CanPlaceRoad(const FVector& FromCm, const FVector& ToCm, FString& OutReason, FRHRoadPreview* OutPreview = nullptr) const;
	// Struct the stores hold for a road (completed buildings on linked floors).
	double GetRoadStructAvailableKg() const;
	// Does a footprint (centre, half-extents) overlap a road strip - built or
	// planned - or the strip of a BuildRoad order still in flight? Roads and
	// buildings keep clear of each other from both sides.
	bool FindRoadOverlap(const FVector& CentreCm, const FVector2D& HalfCm, FString& OutWhat) const;
	// The speed a robot gets on an edge right now: RoadSpeedMul on a built one,
	// 1 on a demolished or missing one (looked up live, never cached).
	double GetRoadEdgeSpeedMul(int32 EdgeId) const;
	// The shared movement call (RH::MoveAlong): one trip step toward TargetCm,
	// along the roads where they beat the straight line. Planned lazily, re-planned
	// only when TargetCm changes. True on arrival, exactly as MoveToward.
	bool MoveAlongRoads(FMassEntityHandle Entity, FTransform& Transform, const FVector& TargetCm, float SpeedMps, float Dt);
	// Presentation: the robot's current road trip (nullptr when it has none).
	const FRHRobotRoadPath* GetRobotRoadPath(FMassEntityHandle Entity) const { return RobotPaths.Find(Entity); }
	// The route a trip between two points would take now, for tests and readouts.
	bool PlanRoadRoute(const FVector& FromCm, const FVector& ToCm, FRHRoadRoute& OutRoute) const;
	// Fabricator labour on a road edge: true when the edge is built (or gone).
	bool ApplyRoadWork(int32 EdgeId, double Seconds);
	// Harness: lay a road with no cost, no lag, no coverage rule. Built = instant;
	// else the edges are planned with their labour tasks posted. Returns the new
	// edge Ids (the splits are in the graph, not in this list).
	TArray<int32> Debug_LayRoad(const FVector& FromCm, const FVector& ToCm, bool bBuilt);
	// Harness: a road edge vanishes (road demolish is a follow-up gate; this is
	// the hook the tests use to prove robots cope).
	bool Debug_RemoveRoadEdge(int32 EdgeId);
	// Harness: the labour left, as if every Fabricator had finished.
	void Debug_FinishRoads();

	// --- Districts (C5a, living-city gate 5a, 2026-10-19) ---
	// The player ZONES districts: a Kind (Homes | Industry | Farming | Research | Civic)
	// painted on surface ground as 10 m lattice cells, or on a whole underground floor.
	// A district has a generated Mars place-name (renamable), a region id, and a Level.
	// The gate is INERT until one is painted: no existing suite paints, so no existing
	// output moves. Three uplink verbs, the same lag as every order, every one of them
	// rejected with a reason when it cannot apply:
	//   PaintDistrict   Target = Kind. Level 0: a rectangle, Location and LocationB two
	//                   points whose lattice cells are its corners (either order). Level
	//                   < 0: that whole floor. Same Kind joins the one district it
	//                   overlaps (it keeps its Id); a rectangle over another Kind, or over
	//                   two districts, is refused. At most 32 x 32 cells an order, every
	//                   cell inside grid coverage; a floor must be reached by the shaft.
	//   EraseDistrict   Value = Id: the whole district goes (its Id is never reused).
	//   RenameDistrict  Value = Id, Text = the name (trimmed, capped at 32 characters,
	//                   never empty; case-sensitive).
	// Membership is DERIVED, never saved: a building (or a surface habitat's every bay)
	// belongs to the district holding its anchor lattice cell - the bay-0 / GridMin cell
	// of a shell, the footprint centre of anything else - so a habitat is never split by
	// a district edge; an underground building or cell to its floor's district; a
	// colonist to their home's. The membership caches rebuild lazily, when read, keyed on
	// (districts, zoning, colonists, furniture) and the building set, and -RHVerifyCaches
	// re-derives and compares them every time one is reused.
	const TArray<FRHDistrict>& GetDistricts() const { return Districts; }
	const FRHDistrict* FindDistrict(int32 Id) const;
	uint32 GetDistrictsVersion() const { return DistrictsVersion; }
	int32 GetNextDistrictId() const { return NextDistrictId; }
	// The lattice cell holding a world point (cell centres are multiples of 10 m), and the
	// world point at a cell's centre.
	static FIntPoint LatticeOf(const FVector& Cm);
	static FVector LatticeCentreCm(const FIntPoint& Cell) { return FVector((double)Cell.X * 1000.0, (double)Cell.Y * 1000.0, 0.0); }
	// True if the order would be accepted right now (the ghost polls it every frame;
	// ExecuteCommand runs the same check when the order lands). A floor order ignores
	// FromCm and ToCm.
	bool CanPaintDistrict(FName Kind, int32 Level, const FVector& FromCm, const FVector& ToCm, FString& OutReason, FRHDistrictPaintPreview* OutPreview = nullptr) const;
	bool CanEraseDistrict(int32 Id, FString& OutReason) const;
	bool CanRenameDistrict(int32 Id, const FString& Typed, FString& OutReason, FString* OutName = nullptr) const;
	// Membership reads. 0 = in no district.
	int32 GetDistrictAtCell(const FIntPoint& Cell) const;
	int32 GetDistrictOfFloor(int32 Level) const;
	int32 GetDistrictAtPoint(int32 Level, const FVector& Cm) const;       // the district painted on the ground at a point
	int32 GetDistrictOfBuilding(int32 BuildingId) const;
	int32 GetDistrictOfBay(int32 HabId, int32 Bay) const { return GetDistrictOfBuilding(HabId); } // a bay is its building's
	int32 GetDistrictOfCell(int32 Level, int32 CellIndex) const { return CellIndex >= 0 ? GetDistrictOfFloor(Level) : 0; } // an underground cell is its floor's
	int32 GetDistrictOfColonist(int32 ColonistId) const;                  // their home's
	// One entry per district, in the order of GetDistricts().
	void GetDistrictCounts(TArray<FRHDistrictCounts>& Out) const;
	// The city's demand: colony-wide Housing, Jobs, Services and Air (C5a: a readout;
	// the sim does not act on it). A pure function of state.
	FRHCityDemand GetCityDemand() const;
	// Harness: true unless a derived district cache is being REUSED while stale (its key
	// still matches but a fresh derivation differs). A cache whose key has moved reads as
	// fresh: it rebuilds on its next read.
	bool Debug_DistrictCachesFresh() const;

	// --- Robot work API (game-thread, called by Mass processors) ---
	FVector GetSiteLocation(const FRHSiteRef& Site) const;
	// Excavator: ground -> pile, respecting pile cap and remaining mass.
	double DigDeposit(int32 DepositId, double Kg);
	bool IsDepositWorkable(int32 DepositId) const; // designated, mass left, pile below cap
	bool IsDepositSpent(int32 DepositId) const;    // no mass left underground or undesignated
	void ReleaseDigClaim(int32 DepositId);
	int32 TryClaimDig(const FVector& RobotPosCm, int32 RobotLevel = 0);
	// Hauler: claim nearest open haul task; then load at From / unload at To.
	bool TryClaimHaul(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask, int32 RobotLevel = 0);
	// Loads up to CargoCapKg from the task's From site. Returns false if the
	// task is gone or nothing could be loaded; otherwise sets the loaded mass
	// and the dropoff location.
	bool HaulLoad(int32 TaskId, float CargoCapKg, float& OutLoadedKg, FVector& OutDropoffCm);
	// Delivers cargo to the task's To building and completes the task.
	void HaulUnload(int32 TaskId, float CargoKg);
	// Fabricator: claim nearest construction; apply work seconds. Work only
	// progresses once the site's materials are delivered.
	bool TryClaimBuild(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask, int32 RobotLevel = 0);
	bool ApplyBuildWork(int32 BuildingId, double Seconds); // true when completed
	void CompleteTask(int32 TaskId);
	void AbandonTask(int32 TaskId);
	// Charging: the nearest completed, switched-on Charging Pad on the
	// robot's floor. With none, the Lander is the emergency dock (2026-10-15
	// playtest fix: robots recharge only by docking, builders take the nearest
	// site, so a fabricator that built the Forge before the first pad ran flat
	// and nothing could ever build one - a permanent softlock). The Lander
	// charges slowly (LanderDockRate_W) and serves every docked robot at once;
	// the pad stays the real charger. False only when neither exists (or the
	// player has switched the Lander off).
	bool FindNearestChargePad(const FVector& RobotPosCm, int32& OutBuildingId, FVector& OutLocationCm, int32 RobotLevel = 0) const;
	// Grants up to the pad's transfer rate x dt from grid energy. Zero when
	// the pad is shed/unbuilt or the grid has nothing to give (night, empty
	// bank) - the robot waits docked. With a valid Robot handle, only the
	// head of the pad's queue is served (M1-b etiquette: one umbilical per
	// pad; the rest wait docked at zero draw). An invalid handle bypasses
	// the queue - the legacy brain's M0-c behavior, frozen. The Lander dock
	// has no queue and grants LanderDockRate_W.
	double RequestChargeWh(int32 PadBuildingId, double SubDt, FMassEntityHandle Robot = FMassEntityHandle());
	void JoinPadQueue(int32 PadBuildingId, FMassEntityHandle Robot);
	void LeavePadQueue(int32 PadBuildingId, FMassEntityHandle Robot);
	// Presentation readout: robots docked at (queued on) a pad right now. The
	// Charging Pad's rings brighten and breathe while this is non-zero.
	int32 GetPadDockedCount(int32 PadBuildingId) const { const TArray<FMassEntityHandle>* Q = PadQueues.Find(PadBuildingId); return Q ? Q->Num() : 0; }
	float GetChargeSeekFraction() const { return ChargeSeekFraction; }
	float GetChargeResumeFraction() const { return ChargeResumeFraction; }

	// --- Fleet reality (M1-b Gate B) ---
	// Wear follows exertion: any sub-step spent above idle draw accrues
	// WearPerSol / sol. Clamped at the halt threshold.
	void AccrueWear(float& Wear, float WearPerSol, float Dt) const;
	// Work-rate multiplier for a wear value: 1 below the degrade threshold,
	// linear to 0 at the halt threshold.
	float GetWearWorkMul(float Wear) const;
	float GetWearDegradeThreshold() const { return WearDegradeThreshold; }
	float GetWearHaltThreshold() const { return WearHaltThreshold; }
	// Scout: claim the nearest open survey task.
	bool TryClaimSurvey(FMassEntityHandle Robot, const FVector& RobotPosCm, FRHTask& OutTask);
	// Arrival at the survey point: reveal hidden deposits within RadiusM,
	// broadcast each discovery, complete the task.
	void CompleteSurvey(int32 TaskId, double RadiusM);
	// Maintenance: claim the most-worn robot at or past the degrade
	// threshold (parts in stock required; self excluded; one claim per target).
	bool TryClaimRepair(FMassEntityHandle Self, FMassEntityHandle& OutTarget, FVector& OutTargetCm);
	// Re-track a moving repair target; false once it despawned.
	bool GetRepairTargetPos(FMassEntityHandle Target, FVector& OutPosCm) const;
	// On arrival: spend SpareParts (RepairWearPerPart wear each) against the
	// target's wear until it is clean or stock runs out. Instant at slice scale.
	void ApplyRepairAt(FMassEntityHandle Target);
	void ReleaseRepairClaim(FMassEntityHandle Target);
	// Everywhere the colony has surveyed (director request: surveyed-land map).
	const TArray<FRHSurveyRecord>& GetSurveyHistory() const { return SurveyHistory; }

	// --- World pressure (M1-c) ---
	// The event active at the current sol, or nullptr for clear skies. Rows
	// come from DT_Events; overlapping rows resolve first-found (author
	// schedules should not overlap - flagged at load if they do).
	const struct FRHEventRow* GetActiveEvent() const;
	// Long-game weather (v25): fold a sol back into the authored event window
	// when the WeatherCycleSols knob is on (0 = off, sol passes through).
	double FoldEventSol(double SolNow) const;
	double EventCycleSpan() const;
	// Solar multiplier right now: the active dust storm's Severity, else the
	// clear-sky DustFactor config row (1.0).
	double GetDustFactorNow() const;
	// Radiation exposure index at a floor. Overburden shields: each level down
	// multiplies the surface index by RadiationPerLevelMul (M1-c plumbing;
	// the M1-d vault trades boring cost for near-zero radiation, and M2's human
	// layer reads this for health). Steady-state - no active-flare spike.
	float GetRadiationAtLevel(int32 Level) const;
	// As above, but a live solar flare multiplies the *surface* index by its
	// Severity (subsurface stays shielded). This is the number the player sees
	// spike on an exposed station's card mid-flare.
	float GetRadiationNow(int32 Level) const;
	// Last 3 sols of (GenW, LoadW, BatteryWh), one sample per sol-hour, oldest
	// first. Transient - rebuilds after load (the chart is a gauge, not truth).
	const TArray<FVector3f>& GetPowerHistory() const { return PowerHistory; }

	// --- Quota / manifest / ship (the slice finale) ---
	ERHQuotaPhase GetQuotaPhase() const { return QuotaPhase; }
	double GetAwardMassKg() const { return AwardMassKg; }
	// The goal ladder (save v25): which CEO quota is live now, and the sol it
	// opened (deadlines are relative to opening). When a quota's ship lands,
	// the next slice-active row - ascending (DeadlineSol, name) - opens, so
	// the program's demands keep coming; once the rows run out the last rung
	// repeats with its bills scaled up each round (D1a, save v32).
	FName GetActiveQuotaName() const { return ActiveQuota; }
	int32 GetQuotaOpenedSol() const { return QuotaOpenedSol; }
	// The renewal count: 0 while authored rows remain, +1 each time the last
	// rung reopens. GetQuotaBillScale() is exactly 1.0 at round 0 or with the
	// knob off, else QuotaRepeatBillMul^round (every bill, not award/deadline).
	int32 GetQuotaRepeatRound() const { return QuotaRepeatRound; }
	double GetQuotaBillScale() const;
	// Manifest budget banked by completed discoveries (FundingKg), paid out on
	// top of the next quota award - research funds the program.
	double GetPendingResearchFundingKg() const { return PendingResearchFundingKg; }
	double GetManifestMassKg() const;
	const TArray<FName>& GetManifestItems() const { return ManifestItems; }
	double GetShipEtaSimSeconds() const { return ShipArrivalSimSeconds; }
	int32 GetImportStock(FName DefName) const;
	// Add an item if the award budget covers it. Returns false + reason otherwise.
	bool AddManifestItem(FName ItemName, FString& OutError);
	// Confirm the manifest and launch the ship (AwaitingManifest only).
	bool LaunchShip(FString& OutError);

	// --- Save / load (sim-owned, versioned binary; M1-a) ---
	// Snapshot the whole colony to Saved/SaveGames/RH_<Slot>.sav. Task claims
	// are cleared and in-flight hauler cargo returned to its source in the
	// saved copy, so a load never depends on robot intent.
	bool SaveColony(const FString& Slot, FString& OutError);
	// Despawn everything, apply the snapshot, respawn robots, then broadcast
	// OnColonyReloaded followed by OnRobotsSpawned - presentation rebuilds
	// entirely from those two events.
	bool LoadColony(const FString& Slot, FString& OutError);

	// --- Era mode (tier 60, M1-a): ledger integration at 1 sim-min steps ---
	// One coarse step: uplink -> abstracted logistics -> production -> quota
	// -> power, reusing the sub-step functions at dt = 60 s. Agents are parked
	// (the clock publishes zero sub-steps in the era band).
	void EraStep(float DtSimSeconds);
	// False (with reason) while any agent-fidelity event is live; the sim
	// auto-drops the clock to 1x when era mode is engaged against this.
	bool CanEnterEraMode(FString& OutReason) const;
	// Headless driver (commandlet): deploy the fleet without waiting for a
	// first agent sub-step.
	void Debug_DeployFleet() { DeployFleetOnce(); }
	// QA/visual driver: place one completed instance of every building type on a
	// grid so the full canon silhouette set is on screen at once (RH.Showcase).
	// Bypasses the economy - completed, no cost, no uplink lag.
	void Debug_Showcase();
	// Harness driver (M1-d): place one completed building instantly - no cost,
	// no lag - so headless tests can stand up a Borer without scripting the
	// whole construction economy.
	void Debug_PlaceInstant(FName DefName, const FVector& LocationCm, int32 Level = 0, int32 SizeX = 1, int32 SizeY = 1);
	// Harness driver (M1-d Gate B): drop solid stock into the first completed
	// building of a def (seeding a Stockpile with Struct/Ore for tax tests).
	void Debug_AddSolid(FName DefName, FName Resource, double Kg);
	// Harness: force the banked energy (grow-light blackout tests). Clamped to
	// the current capacity by the next StepPower.
	void Debug_SetBatteryWh(double Wh) { Power.BatteryWh = FMath::Max(0.0, Wh); }
	// Harness (D1a): flip the ladder-repeat knob mid-run (0 = off, the legacy
	// Completed rest state after the last ship).
	void Debug_SetQuotaRepeatBillMul(double Mul) { QuotaRepeatBillMul = Mul; }
	// Harness (D2a): surveys complete in the agent band, which the era-mode
	// loop doesn't drive - reveal a deposit directly (returns its id, or 0).
	int32 Debug_RevealDeposit(FName RowName)
	{
		for (FRHDepositState& D : Deposits)
		{
			if (D.RowName == RowName) { D.bDiscovered = true; return D.Id; }
		}
		return 0;
	}
	// Harness / RH.DepositLeft: the one path that can RAISE a deposit's mass.
	void Debug_SetDepositRemainingKg(int32 Id, double Kg)
	{
		if (FRHDepositState* D = FindDeposit(Id)) { D->RemainingKg = FMath::Max(0.0, Kg); }
	}
	// Harness: drop every open task (so a routing test sees fresh postings).
	void Debug_ClearTasks() { Tasks.Reset(); }
	// Harness: set a deposit's dug pile (pile-haul routing tests).
	void Debug_SetDepositPileKg(int32 Id, double Kg)
	{
		if (FRHDepositState* D = FindDeposit(Id)) { D->PileKg = FMath::Max(0.0, Kg); }
	}
	// Harness: where the open haul task from a deposit's pile is bound - its To
	// building id, or 0 if none is posted.
	int32 Debug_GetOpenHaulDestFromDeposit(int32 DepositId) const
	{
		for (const FRHTask& T : Tasks)
		{
			if (T.Type == ERHTaskType::Haul && T.From.DepositId == DepositId) { return T.To.BuildingId; }
		}
		return 0;
	}
	// Harness (D2a): empty the low-ice latch so a test can prove LoadColony
	// rebuilds it (and resets a stale one).
	void Debug_ClearIceLowLatch() { IceLowAnnounced.Reset(); }
	// Harness (D2a): where the open haul task from a building (for a resource)
	// is bound - its To building id, or 0 if none is posted.
	int32 Debug_GetOpenHaulDestFrom(int32 FromBuildingId, FName Resource) const
	{
		for (const FRHTask& T : Tasks)
		{
			if (T.Type == ERHTaskType::Haul && T.From.BuildingId == FromBuildingId && T.Resource == Resource) { return T.To.BuildingId; }
		}
		return 0;
	}
	// Harness: run the discovery roll for the Nth survey directly (surveys
	// complete in the agent band, which the era-mode test loop doesn't drive).
	bool Debug_MaybeDiscoverSettlement(int32 SurveyCount) { return MaybeDiscoverSettlement(SurveyCount); }
	// Harness: grant diplomatic influence (pacification-cost tests).
	void Debug_AddInfluence(double Points) { Influence = FMath::Max(0.0, Influence + Points); }

	FRHOnStockChanged OnStockChanged;
	FRHOnDepositDiscovered OnDepositDiscovered;
	FRHOnSurveyCompleted OnSurveyCompleted;
	FRHOnAlert OnAlert;
	FRHOnColonyReloaded OnColonyReloaded;
	FRHOnCommandExecuted OnCommandExecuted;
	FRHOnBuildingAdded OnBuildingAdded;
	FRHOnBuildingCompleted OnBuildingCompleted;
	FRHOnBuildingRemoved OnBuildingRemoved;
	FRHOnFurnitureChanged OnFurnitureChanged;
	FRHOnWallFeatureChanged OnWallFeatureChanged;
	FRHOnCommandRejected OnCommandRejected;
	FRHOnRobotsSpawned OnRobotsSpawned;
	FRHOnQuotaMet OnQuotaMet;
	FRHOnShipArrived OnShipArrived;
	FRHOnCrewMoment OnCrewMoment;
	FRHOnMilestone OnMilestone;

	// --- The alive pass: skills, traits, and crew-life moments ---
	// Crew moments: a deterministic sol-cadence generator of visible life -
	// pastimes in pairs, a lounger joining a worker, and (only when needs are
	// unmet or Hope runs low) abstracted DISPUTES - raised voices, never
	// violence, and no Hope penalty: the friction is a symptom the player can
	// see, not a second punishment (mental-health directive).
	enum class ERHCrewMoment : uint8 { Pastime = 0, JoinWork, Dispute, Celebrate };
	// A colonist's disposition, derived (not stored) from their id: biases the
	// pastime they reach for. The pool is presentation-facing flavor.
	static const TCHAR* TraitFor(int32 ColonistId);
	// Crew skill: 1x fresh -> 2x at mastery, per job function - the average
	// across everyone currently holding that job (1.0 with nobody employed).
	double GetCrewSkillMul(FName Function) const;
	double GetSkillMasterySols() const { return SkillMasterySols; }
	bool HasHadFirstHarvest() const { return bFirstHarvestAnnounced; }

private:
	void StepSim(float SubDt);
	void StepUplink();
	void StepTaskBoard();
	void StepProduction(float SubDt);
	void StepDepositAlerts(); // D2a: one-shot "ice running low" per deposit
	bool HasHealthyDiscoveredIce() const; // known, mass left, above the low line
	void StepQuota();
	void StepPower(float SubDt);
	// Habitability integrator (M1-d Gate B): leak drains every pressurized
	// floor; a circulator tops it back up from the colony O2 pool. Runs in
	// BOTH bands (dimensionally honest like StepProduction).
	void StepHabitability(float SubDt);
	// Population integrator (M2 Gate A1): colonists breathe their floor's O2
	// and eat pooled Food; support edges alert; sustained loss evacuates.
	// Runs in BOTH bands; no-ops at zero population (regression-safe).
	void StepPopulation(float SubDt);
	// C2b: the arrival fill order, shared by new arrivals and by moving home. A
	// colonist goes to the shallowest rated floor with a free bed, then to the rated
	// surface networks in root order (their members by Id). The state holds the
	// residents and beds per home so a whole batch costs one pass over the roster.
	struct FRHFillState
	{
		TMap<int32, int32> FloorResidents, HabResidents;
		TMap<int32, int32> FloorBeds, HabBeds;
		TArray<int8> NetRatedNow; // per HabNets index: -1 not asked yet, else 0/1
	};
	void InitFillState(FRHFillState& State, const TSet<int32>* SkipIds = nullptr) const;
	bool PickFillHome(FRHFillState& State, int32& OutLevel, int32& OutHab) const;
	// C2b: settlers with a landing supply ship (called as the ship lands).
	int32 ApplyImmigration();
	int32 LivingCellWeight(const FIntVector& Key) const; // a Living cell with beds counts its beds, else 1
	// C2b: the Big Arrival's streak; one move-home pass per MoveHomeIntervalSols.
	void StepBigArrival(float SubDt);
	void StepMoveHome(float SubDt);
	int32 RehomeResidents();
	// Construction shortage (M1-d Gate B): resource -> kg the open sites need
	// beyond everything the colony holds. Drives demand-preferred recipe
	// selection and the store->producer feed leg. Empty when nothing builds -
	// the guard that keeps steady-state logistics (and era parity) untouched.
	TMap<FName, double> ComputeConstructionShortage() const;
	// Can some completed building run a slice-active recipe outputting this?
	bool HasProducerFor(FName Resource) const;
	void ApplyManifestItemEffect(FName ItemName);
	void ExecuteCommand(const FRHCommand& Cmd);
	void AddBuilding(FName DefName, const FVector& LocationCm, bool bInstant, int32 Level = 0, int32 SizeX = 1, int32 SizeY = 1);
	int32 GetCropStageForKey(const FIntVector& Key) const;
	// The floor a task site sits on (building/deposit lookup; 0 if gone).
	int32 GetSiteLevel(const FRHSiteRef& Site) const;
	void SpawnStartingFleet();
	void DeployFleetOnce();
	// All sim-initiated robot spawns go through here so FleetCounts (the era
	// integrator's aggregate-rate source) stays true.
	FMassEntityHandle SpawnRobotTracked(FName RowName, const FRHRobotRow& Row, const FVector& PosCm, float ChargeWh, float Wear, TArray<FMassEntityHandle>& OutSpawned);
	void HandleSolElapsed(int32 NewSol);
	// Event onset/end edge detection (director ruling: onset snaps any speed
	// to 1x so the player can batten down; both edges alert loudly).
	void StepEventEdges();
	// Era-band stand-in for dig + haul: deposits feed demanding hoppers at the
	// parked excavator fleet's aggregate rate; finished outputs transfer to
	// demanders/stores instantly (haulers are not modeled above the agent band).
	void EraLogistics(float DtSimSeconds);
	FRHBuildingInstance* FindBuilding(int32 Id);
	FRHDepositState* FindDeposit(int32 Id);
	FRHTask* FindTask(int32 Id);
	bool HasOpenTask(ERHTaskType Type, const FRHSiteRef& From, const FRHSiteRef& To, FName Resource = NAME_None) const;

	// Roads (C4). The graph and its tuning (DT_Config; header defaults equal the
	// CSV). Saved (v36); robot trips are runtime only and rebuilt on load.
	FRHRoadGraph RoadGraph;
	FRHRoadParams RoadParams;
	double RoadStructKgPerMeter = 2.0;
	double RoadBuildMetersPerHour = 30.0;
	double RoadSpeedMul = 1.6;
	TMap<FMassEntityHandle, FRHRobotRoadPath> RobotPaths; // lookups only, never iterated
	int32 NextPathSerial = 1;
	void ExecuteBuildRoad(const FRHCommand& Cmd);
	// Takes Kg of Struct out of the stores, whole or not at all.
	bool TakeRoadStruct(double Kg);
	void PostRoadTasks(const FRHRoadAddResult& Added);
	bool PlanRobotPath(FRHRobotRoadPath& Path, const FVector& FromCm, const FVector& TargetCm) const;
	// Strip (segment + width) against a footprint; the order's strip against everything.
	bool RoadStripHitsBox(const FVector2D& A, const FVector2D& B, const FVector2D& Centre, const FVector2D& Half) const;

	// Districts (C5a). SAVED (v37): the list (ascending Id, each with its sorted lattice) and
	// the next Id. Everything below it is DERIVED and rebuilt lazily; nothing derived is saved.
	TArray<FRHDistrict> Districts;
	int32 NextDistrictId = 1;               // SAVED; Ids are never reused
	uint32 DistrictsVersion = 1;            // bumped by a paint, an erase, a rename and a load
	// Ground ownership: lattice cell -> district Id, floor -> district Id, Id -> index into
	// Districts. Lookups only, never iterated. Keyed on DistrictsVersion.
	struct FRHDistrictGeo
	{
		bool bValid = false;
		uint32 Version = 0;
		TMap<FIntPoint, int32> CellOwner;
		TMap<int32, int32> FloorOwner;
		TMap<int32, int32> IndexById;
	};
	// Who belongs to which district: building Id -> district Id, and per-district (Districts
	// order) counts of buildings, shell bays and residents. Keyed on
	// (DistrictsVersion, ZoningVersion, ColonistSetVersion, FurnitureVersion) plus the building
	// set (its count and the next building Id, which move on every add and every demolish).
	struct FRHDistrictMembershipKey
	{
		uint32 Districts = 0, Zoning = 0, Colonists = 0, Furniture = 0;
		int32 BuildingCount = 0, NextBuilding = 0;
		bool operator==(const FRHDistrictMembershipKey& O) const
		{
			return Districts == O.Districts && Zoning == O.Zoning && Colonists == O.Colonists && Furniture == O.Furniture
				&& BuildingCount == O.BuildingCount && NextBuilding == O.NextBuilding;
		}
	};
	struct FRHDistrictMembership
	{
		bool bValid = false;
		FRHDistrictMembershipKey Key;
		TMap<int32, int32> OfBuilding;
		TArray<int32> Buildings, Bays, Residents;
	};
	mutable FRHDistrictGeo DistrictGeo;
	mutable FRHDistrictMembership DistrictMembers;
	const FRHDistrictGeo& EnsureDistrictGeo() const;
	void DeriveDistrictGeo(FRHDistrictGeo& Out) const;
	const FRHDistrictMembership& EnsureDistrictMembership() const;
	void DeriveDistrictMembership(FRHDistrictMembership& Out) const;
	FRHDistrictMembershipKey MakeDistrictMembershipKey() const;
	// The lattice cell a surface building is anchored on: bay 0 (GridMin) for a shell of any
	// state, the footprint centre for anything else.
	FIntPoint GetBuildingAnchorLattice(const FRHBuildingInstance& B) const;
	// The three verbs' workers: false with a reason (the order is rejected, never "executed").
	bool ExecutePaintDistrict(const FRHCommand& Cmd, FString& OutReason);
	bool ExecuteEraseDistrict(const FRHCommand& Cmd, FString& OutReason);
	bool ExecuteRenameDistrict(const FRHCommand& Cmd, FString& OutReason);

	TMap<FName, double> Stocks;             // fluids/gases/abstract (network-instant)
	TMap<int32, TMap<FName, double>> PendingOutputs; // building id -> outputs owed when its batch completes
	TArray<FRHCommand> UplinkQueue;
	TArray<FRHBuildingInstance> Buildings;
	TArray<FRHDepositState> Deposits;
	TArray<FRHTask> Tasks;
	TMap<FName, int32> ImportStock;         // starter flat-packs (SolarArray, BatteryBank)
	FRHPowerState Power;
	double OrderLagSeconds = 45.0;
	double PileCapKg = 500.0;
	double HaulLoadMinKg = 100.0;
	float ChargeSeekFraction = 0.25f;
	float ChargeResumeFraction = 0.9f;
	// D5a day shift, RH_Config (header defaults equal the CSV values).
	double DayShiftStartFrac = 0.25;
	double DayShiftEndFrac = 0.75;
	bool bDayShiftDefaultsOn = true;
	// Daylight seconds accumulated from sim time 0 up to T (sol-periodic).
	double DaylightSecondsUpTo(double T) const;
	// In-game burst parity: when several steps run in one frame against the
	// same advanced clock, Tick gives each its own end time here (>= 0) so the
	// daylight share is computed for the step's own window. The commandlet
	// leaves it at -1 and reads the clock, which it advances per step.
	double StepEndOverride = -1.0;
	double GetStepEndSeconds() const;
	bool IsDaylightAt(double T) const;
	int32 NightIndexAt(double T) const;
	int32 LastBlackoutNight = -1; // D5a runtime latch, see GetLastBlackoutNight
	double LanderDockRateW = 100.0; // RH_Config LanderDockRate_W
	// Fleet reality (M1-b): DT_Config rows.
	float WearDegradeThreshold = 50.f;
	float WearHaltThreshold = 100.f;
	float RepairWearPerPart = 25.f;
	// Director ruling 2026-07-07b: robots working through a dust storm wear
	// at this multiple (the storm grinds machinery; shelter arrives with the
	// M2 warehouse). DT_Config row StormWearMul.
	float StormWearMul = 2.f;
	// Radiation model (M1-c): surface baseline index and per-level attenuation
	// (each floor down multiplies by this). DT_Config rows RadiationSurface,
	// RadiationPerLevelMul. Overburden is free shielding - the M1-d vault's
	// whole point. No M1-c consumer taxes it yet (shielding build tax lands in
	// M1-d with the underground contrast); M1-c only surfaces the flare spike.
	float RadiationSurface = 1.f;
	float RadiationPerLevelMul = 0.05f;
	// Transient event-edge + ship-countdown state (never serialized; a load
	// re-derives both from the clock + quota phase on the next step).
	bool bEventWasActive = false;
	FName LastEventType;
	int32 ShipAlertStage = 0; // 0 none, 1 = T-2 fired, 2 = T-1 fired
	// Runtime claim state - never serialized; load resets it and robots
	// re-claim from fragments/board.
	TSet<FMassEntityHandle> RepairClaims;
	TMap<int32, TArray<FMassEntityHandle>> PadQueues;
	TArray<FRHSurveyRecord> SurveyHistory; // serialized (save v4)
	int32 NextCommandId = 1;               // uplink handles (each order's own; saved with the queue, v37; a load raises this past the loaded ones)
	TArray<FVector3f> PowerHistory;        // transient strip-chart ring
	int64 LastPowerSampleHour = -1;
	double FabricatorSpeedMul = 1.0; // Toolkit manifest item raises this
	// Z-model config (DT_Config: FloorHeightMeters, MaxDepth).
	double FloorHeightCm = 400.0;
	int32 MaxDepth = 5;
	// Shaft & excavation state (M1-d Gate A; serialized, save v6).
	int32 ShaftDepth = 0;                  // floors the trunk reaches below surface (0 = none)
	FVector ShaftHeadCm = FVector::ZeroVector; // surface column the shaft descends from
	TMap<int32, int32> FloorCarvedCells;   // Level -> carved 10x10 cell count
	double SpoilPileKg = 0.0;              // debug-cheat spoil only; Borer batches drop spoil at the building
	// Designation queues (A2; serialized): the Borer works these through the
	// batch integrator. Bore first, then carve shallowest-first (deterministic).
	int32 BoreTargetDepth = 0;             // ordered trunk depth; work proceeds while ShaftDepth < this
	TMap<int32, int32> CarveQueue;         // Level -> cells still to carve (decremented at batch START)
	// Batch-in-flight work records (BuildingId -> X: 0=bore floor/1=carve cell,
	// Y: level). Applied at batch completion; serialized so a mid-batch save
	// resumes and still advances the shaft.
	TMap<int32, FIntPoint> PendingBoreWork;
	// Habitability chain state (M1-d Gate B; serialized, save v7). O2 fill per
	// floor in kg; RatedFloors = floors currently holding a Livable rating
	// (hysteresis: rated at 100% fill, lost below 98% - a dry O2 pool drains
	// the floor through leakage and the loss is announced, not silent).
	TMap<int32, double> FloorO2Kg;
	TSet<int32> RatedFloors;
	bool bVaultRated = false; // Phase 1 exit fired (save v8); never unset
	bool bFleetHold = false;  // storm discipline: no new task claims (save v9)
	// DT_Config rows (CSV-staged defaults): fill mass per carved 10x10 cell,
	// leak per cell per sol (the standing tax of pressurized volume), and the
	// trunk's push rate per floor per sol-hour.
	double O2FillKgPerCell = 100.0;
	double O2LeakKgPerCellPerSol = 2.0;
	double O2FillRateKgPerHour = 20.0;
	int32 MinLivableCells = 4; // director ruling: min carved cells to certify a habitat
	// Transient UX debounce (not serialized): floors we've already told the
	// player are "sealed but too small", so the note fires once per episode
	// instead of every step. Cleared when the floor rates up or loses seal.
	TSet<int32> FloorsNotedSmall;
	// BM-1 surface habitats. SAVED (v29): per-hab O2 fill (kg) and the hab Ids
	// holding the rating (hysteresis needs history, like RatedFloors).
	TMap<int32, double> HabO2Kg;
	TSet<int32> RatedHabs;
	TSet<int32> HabNetsNotedSmall;        // transient debounce by network root; reset on load
	double ShaftLinkRangeCm = 1000.0;     // config ShaftLinkRange_m
	// DERIVED topology (never saved): a pure function of Buildings, rebuilt
	// EAGERLY by RebuildHabTopology() at every Buildings mutation site.
	struct FRHHabMemberCache { int32 Id = 0; int32 NX = 0; int32 NY = 0; FIntPoint GridMin = FIntPoint::ZeroValue; int32 NetIndex = INDEX_NONE; FName FixedRoom; };
	// Per network: Members ascending; Circulators / Modules are Buildings
	// INDICES (Buildings stays Id-ascending; a demolish RemoveAt is followed
	// at once by RebuildHabTopology) of the built Interior air
	// circulators and the Interior station modules (any build state) standing
	// in a member bay, ascending - the per-step reads never scan Buildings.
	struct FRHHabNetCache { int32 Root = 0; TArray<int32> Members; int32 Bays = 0; TArray<int32> Circulators; TArray<int32> Modules; };
	TMap<FIntPoint, int32> HabBayOwner;         // member bay lattice point -> hab Id (lookup only)
	// The member shell and bay under a surface point (built members only; O(1)).
	bool FindMemberBayAt(const FVector& LocationCm, int32& OutHabId, int32& OutBay) const;
	TArray<FRHHabNetCache> HabNets;             // ascending Root; ITERATE THIS
	TMap<int32, FRHHabMemberCache> HabMembers;  // lookup only, NEVER iterated
	void RebuildHabTopology();
	const FRHHabNetCache* FindHabNet(int32 HabId) const;
	bool IsNetRated(const FRHHabNetCache& N) const;
	bool IsNetCirculated(const FRHHabNetCache& N) const;
	bool IsNetShaftLinked(const FRHHabNetCache& N) const;
	// BM-3 internals.
	double SalvageFraction = 0.5;         // config SalvageFraction
	void ComputeSalvage(const FRHBuildingInstance& B, TMap<FName, double>& OutIn, TMap<FName, double>& OutOut, int32& OutPacks) const;
	void RemoveBuilding(int32 Id);
	// Credits stock to the Lander: solids onto a shelf, the rest to the pool.
	void CreditLander(const TMap<FName, double>& Lines, bool bOutputShelf);
	// --- Furniture (BM-4) ---
	TArray<FRHFurnitureInstance> Furniture;   // SAVED (v30), Id-ascending
	int32 NextFurnitureId = 1;               // SAVED; Ids are never reused
	double FurnitureRefundFraction = 0.75;   // config FurnitureRefundFraction
	double HopeFurnishingsMax = 6.0;         // config HopeFurnishingsMax
	// DERIVED per cell, lookup only: raw capacities by kind, the sub-cell
	// occupancy mask and the piece Ids (ascending). Rebuilt eagerly by
	// RebuildFurnitureCache at every change, like the hab topology.
	struct FRHCellFurnish { int32 Beds = 0; int32 BedPieces = 0; int32 JobSeats = 0; int32 SeatPieces = 0; int32 DiningSeats = 0; uint32 Occupied = 0; TArray<int32> Ids; };
	TMap<FIntVector, FRHCellFurnish> FurnishedCells;
	TMap<int32, int32> ColonistBedPiece;     // derived (RefreshJobs)
	TMap<int32, int32> ColonistSeatPiece;    // derived (RefreshJobs)
	// C1 (living-city foundations): derived lookups and change counters. Nothing
	// here is saved; every one is rebuilt on load or by the call that changes
	// its input, so a load can never leave a stale copy.
	TMap<int32, int32> FurnitureIndexById;   // piece Id -> Furniture index (RebuildFurnitureCache)
	TArray<int32> ComfortPieces;             // Furniture indices of pieces that lift Hope, ascending (RebuildFurnitureCache)
	uint32 FurnitureVersion = 1;             // bumped by RebuildFurnitureCache
	uint32 ZoningVersion = 1;                // bumped when a cell/bay is re-zoned, a hab goes, or a colony loads
	uint32 ColonistSetVersion = 1;           // bumped when Colonists gains, loses or reloads an entry
	// The bed assignment is a pure function of (colonist roster, furniture,
	// zoning): RefreshJobs re-derives it only when one of the three has moved.
	bool bBedKeyValid = false;
	uint32 BedKeyColonists = 0, BedKeyFurniture = 0, BedKeyZoning = 0;
	// -RHVerifyCaches (command line): every keyed cache that is about to be
	// REUSED is re-derived from scratch and compared, and a difference is a
	// LogRedHopeSim error. The battery's safety net for "recompute only on
	// change" - a stale cache is a bug the line-diff might not see.
	bool bVerifyCaches = false;
	mutable TMap<int32, int32> ColonistIndexById; // colonist Id -> Colonists index
	mutable bool bColonistIndexDirty = true;
	void RebuildColonistIndex() const;
	void RebuildFurnitureCache();
	bool IsFurnishKeyValid(const FIntVector& Key, FString* OutReason = nullptr) const;
	bool IsLivingKey(const FIntVector& Key) const;
	bool IsFurnitureCellWorking(const FIntVector& Key) const;
	static uint32 FurnitureMask(int32 Slot, const FIntPoint& Size);
	// Sub-cells of a cell covered by machine footprints; OutFirst = the first
	// building (by Id) covering any sub-cell of Probe.
	uint32 BuildingMaskInCell(const FIntVector& Key, uint32 Probe = 0, const FRHBuildingInstance** OutFirst = nullptr) const;
	int32 CountFloorBeds(int32 Level, const TSet<int32>* Exclude) const;
	int32 CountHabBeds(int32 HabId, int32 ExcludeBay = INDEX_NONE, const TSet<int32>* Exclude = nullptr) const;
	bool BedGuardAllowsRemoval(const FIntVector& Key, const TSet<int32>& Ids, FString& OutReason) const;
	bool ResolveFurnishOrderKey(const FRHCommand& Cmd, FIntVector& OutKey) const;
	void AddFurniture(FName Row, const FIntVector& Key, int32 Slot, int32 Rot);
	void RemoveFurniture(const TSet<int32>& Ids, TMap<FName, double>* OutRefund);
	TMap<FName, double> FurnitureRefundFor(const TSet<int32>& Ids) const;
	double GetFurnishingsHope(double* OutRaw = nullptr) const;
	bool FurnitureAllowsRezone(const FIntVector& Key, FName NewRoom, FString& OutReason) const;
	void BuildBedAssignments(const TArray<int32>& ByIdOrder);
	// --- Wall fittings (BM-6) ---
	TArray<FRHWallFeature> WallFeatures;    // SAVED (v31), Id-ascending
	int32 NextWallFeatureId = 1;            // SAVED; never reused
	double HopeViewsMax = 3.0;              // config HopeViewsMax
	double SealedLeakMul = 1.0;             // DT_Compartments SealedBay LeakMul (active row), else 1
	TMap<FIntVector, int32> FeatureByEdge;  // derived lookup (edge -> Id)
	void RebuildWallFeatureIndex();
	int32 WallBayIndex(int32 HabId, const FIntPoint& Point) const;
	bool IsNetSealed(const FRHHabNetCache& N) const;
	double GetMemberWindowLeakPerSol(int32 HabId) const;
	void AddWallFeature(FName Row, const FIntVector& Edge);
	void RemoveWallFeatures(const TSet<int32>& Ids, TMap<FName, double>* OutRefund);
	TMap<FName, double> WallFeatureRefundFor(const TSet<int32>& Ids) const;
	// The fittings on any edge of a habitat's bays (both sides).
	void GetFeaturesTouchingHab(int32 HabId, TSet<int32>& OutIds) const;
	bool AnyHabNetRated() const;
	int32 GetHabLivingBays(int32 HabId) const;
	bool IsLocationInRatedHab(const FVector& LocationCm) const;
	bool IsWhollyInBay(const FRHBuildingInstance& Hab, const FVector& LocationCm, const FVector2D& HalfCm, int32& OutBay) const;
	FIntPoint CellPos(const FIntVector& Key) const;
	void StepHabNetworks(float SubDt);
	const FRHBuildingInstance* FindBuildingById(int32 Id) const;
	// Population (M2 Gate A1; serialized, save v10). Config rows drive every
	// rate - no hardcoded balance.
	TArray<FRHColonist> Colonists;
	int32 NextColonistId = 1;
	double ColonistsPerCell = 1.0;      // certified beds per carved cell on a rated floor
	double ColonistO2KgPerSol = 0.75;   // breathing draw against the home floor's fill
	double ColonistFoodKgPerSol = 0.62; // pooled Food draw
	int32 CrewPodColonists = 4;         // colonists per CrewPod cargo item
	double CrewPodFoodKg = 200.0;       // provisions landed with each pod
	double ColonistEvacSols = 2.0;      // C2a: sols a home takes to empty at ration 0 (a ration of r takes EvacSols / (1 - r))
	// C2a: per short home, OnsetResidents x (1 - ration) x dt / (EffectiveEvacSols x
	// sol) accumulates in Acc; each whole 1 recalls one resident. OnsetResidents is
	// the home's size when the shortage began, raised if the home grew since, so a
	// total failure empties the home in EvacSols: the recall RATE is set by the
	// shortage, it does not slow as the home shrinks. SAVED (v34), sorted by key.
	// A home back on a full ration, or with no one left, drops its entry.
	struct FRHHomeClock { double Acc = 0.0; int32 OnsetResidents = 0; };
	TMap<int32, FRHHomeClock> HomeClocks;
	double DeficitEquivalent = 0.0;     // SAVED (v34): the last step's sum of residents x (1 - ration)
	TArray<FRHHomeRation> HomeRations;  // derived each step
	// Rooms & Hope (M2 Gate B). FloorRoomCells: per floor, the room function of
	// each carved cell by spiral index (NAME_None = undesignated). Serialized
	// (save v11). ColonistJobs is derived (rebuilt each population step + on
	// load) - never saved, so it can't diverge.
	TMap<int32, TArray<FName>> FloorRoomCells;
	// BM-1: per hab, the zoned room of each bay (index by*NX+bx); saved (v29).
	// Never stored in FloorRoomCells: key 0 would open SetFloorClimate /
	// DesignateDuct for the dome-bed level and defeat StepAgriculture's early-out.
	TMap<int32, TArray<FName>> HabBayRooms;
	TMap<int32, FName> ColonistJobs;
	// Staffing (T2/T3). SeatPriority, StaffedSeats and SeatFillSols are saved
	// (v27); the station maps re-derive with the jobs.
	TArray<FIntVector> SeatPriority;
	int32 StaffedSeats = 0;
	double SeatFillSols = 0.0;
	double LazySeatFillSols = 0.0;        // config LazySeatFillSols (0 = instant)
	TMap<int32, FIntVector> ColonistStations;
	TMap<FIntVector, int32> StationFilled;
	TMap<FIntVector, int32> StationCapacity;
	int32 LastIdleColonists = 0;          // derived by RefreshJobs, read by T3
	int32 LastOpenSeats = 0;
	void RefreshJobs();
	double HopeBase = 50.0;
	// The three weights below must equal RH_Config.csv (D3a): GetConfigScalar
	// falls back here when the DT row is missing.
	double HopeHousingMax = 7.5;        // full when LivingQuarters cells >= population
	double HopePerMoralePoint = 2.5;    // per room type, colony-wide, x its coverage (Living excluded)
	// C2a (per head): every term is a share or a rate. The same fallbacks as
	// RH_Config.csv; HopePerJob / HopeAdjacencyPenalty / HopeUnsupportedPenalty
	// are retired (rows kept in the CSV, unused).
	double HopeJobsMax = 7.5;             // x FilledSeats / Population
	double HopeUnsupportedMax = 30.0;     // x min(1, steepness x deficit share)
	double HopeUnsupportedSteepness = 3.0;
	double HopeAdjacencyMax = 16.0;       // x offended refuser cells / refuser cells (a fully offended small layout = the old two pairs, 2 x 8)
	double RoomServesResidents = 25.0;    // one room of a type covers this many residents
	double ComfortRefPop = 8.0;           // Furnishings and Views sums are per resident, at this crew size
	double HopeVaultMilestone = 5.0;
	// Hope-drives-the-colony state (save v13). HopeSmoothed low-passes the
	// instantaneous index; HopeBand is the hysteresis band it currently sits in
	// (saved so save/load is exact-equivalent even mid-gap). Both run in BOTH
	// time bands via StepHope, using the step-size-invariant exp smoother.
	double HopeSmoothed = 50.0;
	ERHHopeBand HopeBand = ERHHopeBand::Steady;
	void StepHope(float SubDt);
	void UpdateHopeBand(); // rise/fall the band from HopeSmoothed with hysteresis
	double HopeSmoothTauSols = 3.0;
	double HopeTempoSlope = 0.006;      // tempo = 1 + slope*(smoothed-50)
	double HopeTempoMin = 0.60;         // a strained colony is slow, never dead
	double HopeTempoMax = 1.25;
	// The Generational Carrot (M2 Gate D+): what a FLOURISHING colony EARNS. A
	// deterministic sol-accumulator, zero RNG - streak the sols where smoothed
	// Hope >= threshold AND there is housing headroom AND a real food buffer;
	// when the streak crosses the interval, the colony GROWS by one (a birth,
	// via the existing housing path). The FIRST such birth is the brief's
	// "first Martian-born child" - a one-time Hope surge and the seed of the M3
	// identity axis (native-born citizens weight the colony toward Mars).
	// Threshold-on-monotone-accumulator = the most era-parity-safe construct
	// (no sub-step ordering sensitivity), evaluated at the sol boundary.
	double HopeGrowthThreshold = 75.0;    // smoothed Hope needed to grow
	double HopeGrowthIntervalSols = 20.0; // sustained sols per new colonist
	double HopeGrowthFoodBufferSols = 6.0;// required Food buffer (sols of colony draw)
	double HopeFirstBornMilestone = 6.0;  // one-time Hope bonus once a child is born
	double HopeGrowthStreakSols = 0.0;    // serialized (save v15): the running streak
	bool bFirstBornAnnounced = false;     // serialized: the milestone fires once, never retracts
	int32 BirthsOnMars = 0;               // serialized: native-born count (M3 identity seed)
	void StepGrowth(float SubDt);
	// C2b: arrivals at scale (config rows, placeholders). Births: the streak runs at
	// max(1, N / BirthRefPop) x dt, so N <= BirthRefPop is exactly as it always was.
	double BirthRefPop = 25.0;
	int32 ColonistTransportColonists = 20;           // colonists per Colonist Transport cargo item
	double ColonistTransportFoodKgPerColonist = 50.0; // provisions landed with each colonist
	double ImmigrationHopeMin = 75.0;                // smoothed Hope a landing needs to draw settlers
	double ImmigrationHopeFloor = 70.0;              // the Hope at which the draw is zero
	double ImmigrationBase = 4.0;                    // settlers at full draw for a small colony
	double ImmigrationPerCapita = 0.2;               // ... or this share of the population, if larger
	int32 ImmigrationCapPerShip = 50;
	int32 ImmigrationReserveBeds = 1;                // free beds settlers leave open for Mars-born children (births come first)
	double BigArrivalHopeMin = 75.0;                 // smoothed Hope the streak counts at
	double BigArrivalStreakSolsNeeded = 20.0;        // sols of it
	int32 BigArrivalFreeBeds = 100;                  // free beds the wave needs
	int32 BigArrivalColonists = 100;
	double MoveHomeIntervalSols = 1.0;               // 0 = nobody moves home
	// SAVED (v35): the Big Arrival's running streak and its once-only flag, the
	// move-home accumulator, and the settlers who came with ships (a count).
	double BigArrivalStreakSols = 0.0;
	bool bBigArrivalDone = false;
	double MoveHomeAccSols = 0.0;
	int32 ImmigrantsTotal = 0;
	// Discoveries (save v16): the ordered log of what the colony has uncovered
	// (each name's HopeBonus is a permanent milestone) + the seat-hours accrued
	// toward the next. Linear accumulation on a threshold = era-parity-safe.
	TArray<FName> DiscoveryLog;
	double DiscoverySeatHours = 0.0;
	double HopeDiscoveryThreshold = 85.0;
	void StepDiscovery(float SubDt);
	// Rivals & trade (M3 Gate A; save v17). One active convoy at a time (fleet
	// expansion is data once proven). ConvoyRival None = idle. Transit is a sol
	// accumulator advanced only under clear sky (storm freezes it); at the
	// one-way distance it flips to the return leg; at the round trip it delivers.
	// Relations persist per rival (RivalRelations), seeded from RelationStart.
	FName ConvoyRival;
	bool bConvoyReturning = false;
	double ConvoyLegSols = 0.0;         // sols travelled on the CURRENT leg
	TMap<FName, double> RivalRelations; // rival row name -> 0..100
	double ConvoySpeedKmPerSol = 60.0;
	double ConvoyH2PerRun = 8.0;
	double ConvoyWearParts = 1.0;
	double RelationPerRun = 2.0;
	void StepTrade(float SubDt);
	// Earth's Shadow (M3 Gate B; save v18). Inert until a rival is slice-active.
	double EarthTension = 0.0;
	double IdentityAxis = 0.0;
	bool bEarthDemandPending = false;
	double EarthTensionDriftPerSol = 0.6;
	double EarthTensionDemandThreshold = 60.0;
	double RequisitionEarthBonus = 0.4;
	double RequisitionMartianPenalty = 0.6;
	double RequisitionTensionPenalty = 0.3;
	// The Solidarity Dilemma (M3 Gate C; save v19). Closed routes refuse new
	// convoys; SolidarityHope is a fading morale shock folded into the index.
	TSet<FName> ClosedRoutes;
	double SolidarityHope = 0.0;
	double DilemmaComplyAxisShift = 20.0;
	double DilemmaDefyAxisShift = 20.0;
	double DilemmaComplyTensionDrop = 45.0;
	double DilemmaDefyTensionDrop = 12.0;
	double DilemmaRelationHit = 40.0;
	double DilemmaDefyRelationBonus = 5.0;
	double SolidarityHopeShock = 12.0;
	double SolidarityHopeTauSols = 5.0;
	// The covert layer (M4 Gate A; save v20). HiddenTensions is per-rival secret
	// hostility; HumanNatureAxis is the Evolved/Destructive throughline;
	// CovertAttempts seeds the deterministic detection roll (saved so it never
	// re-rolls the same outcome on reload).
	TMap<FName, double> HiddenTensions;
	double HumanNatureAxis = 0.0;
	int32 CovertAttempts = 0;
	double HumanNatureFairTradeShift = 2.0;
	double HumanNatureCovertShift = 6.0;
	double CovertLotFraction = 0.5;
	double CovertDetectionBase = 0.5;
	double CovertDetectionTrustMul = 0.5;
	double CovertDetectionNightMul = 0.4;
	double CovertHiddenTensionClean = 8.0;
	double CovertHiddenTensionCaught = 25.0;
	double CovertRelationHitCaught = 30.0;
	double& HiddenTensionRef(FName Rival) { return HiddenTensions.FindOrAdd(Rival); }
	// The espionage economy (M4 Gate B; save v21). Sabotage timers per rival;
	// the set of dormant rivals a survey has uncovered.
	TMap<FName, double> SabotageSols;   // rival -> disruption sols remaining
	TSet<FName> DiscoveredRivals;
	double LaunderHiddenDrop = 20.0;
	double LaunderRelationBonus = 4.0;
	double HumanNatureLaunderShift = 3.0;
	double HumanNatureSabotageShift = 8.0;
	double SabotageDurationSols = 6.0;
	double SabotageHiddenTension = 20.0;
	double DiscoveryChancePerSurvey = 0.5;
	// Earth's pre-emptive pressure (M4 Gate C; save v22). EmbargoGrace: rival ->
	// sols left to pacify before defection; DefectedRivals: locked-hostile.
	// Influence: the diplomatic soft currency.
	TMap<FName, double> EmbargoGrace;
	TSet<FName> DefectedRivals;
	double Influence = 0.0;
	double InfluencePerTrade = 3.0;
	double InfluencePerLaunder = 2.0;
	double InfluenceDriftPerSol = 1.0;
	double EarthPreemptiveThreshold = 75.0;
	double EmbargoGraceSols = 8.0;
	double PacifyInfluenceCost = 20.0;
	double PacifyTensionRelief = 20.0;
	double HumanNaturePacifyShift = 4.0;
	double DefectRelationFloor = 5.0;
	void StepPreemptive(float SubDt); // embargo trigger + grace countdown + influence drift
	// Dynamic crises + endings (M4 Gate D; save v23). ActiveCrisis + its timer;
	// CrisisCheckSols accumulates toward the next selector roll; CrisisCount seeds
	// the deterministic roll (saved); bEverHadCrew + bEndingDeclared are milestones.
	ERHCrisis ActiveCrisis = ERHCrisis::None;
	double CrisisRemainingSols = 0.0;
	double CrisisCheckSols = 0.0;
	int32 CrisisCount = 0;
	bool bEverHadCrew = false;
	bool bEndingDeclared = false;
	double CrisisIntervalSols = 15.0;
	double CrisisChance = 0.6;
	double CrisisDurationSols = 5.0;
	double CrisisAlignmentThreshold = 0.0;
	double CrisisWorkPenalty = 0.6;
	double CrisisMalfunctionHope = 12.0;
	double CrisisEnvironmentHope = 6.0;
	double CrisisWaterDrainPerSol = 8.0;
	double EndingIdentityThreshold = 60.0;
	double EndingHumanNatureThreshold = 30.0;
	void StepCrisis(float SubDt);
	void TriggerCrisis(); // run the alignment-gated selector, start a crisis
	// The alive pass (save v24). MomentSols accumulates toward the next crew
	// moment; MomentCounter seeds the deterministic picks. First-harvest
	// tracking: cumulative garden-grown food + the once-only milestone flag.
	void StepMoments(float SubDt);
	double MomentSols = 0.0;
	int32 MomentCounter = 0;
	bool bFirstHarvestAnnounced = false;
	double GardenFoodCumKg = 0.0;
	double SkillMasterySols = 30.0;
	double CrewMomentIntervalSols = 0.5;
	double DisputeHopeBelow = 45.0;
	double FirstHarvestKg = 1.0;
	double HopeFirstHarvestMilestone = 4.0;
	// A deterministic detection roll for a covert op against a rival (shared by
	// requisition + sabotage): seeded by content hash + the saved attempt count.
	bool RollCovertDetected(FName Rival);
	// Discovery-on-scout worker (called from CompleteSurvey, seeded by survey #).
	bool MaybeDiscoverSettlement(int32 SurveyCount);
	void StepEarth(float SubDt);
	bool HasActiveRival() const; // any available rival => the layer is live
	// Ensure a rival's relation entry exists (lazy seed from RelationStart).
	double& RelationRef(FName Rival);
	// One-way transit time for a rival, in sols.
	double ConvoyLegSolsFor(FName Rival) const;
	// Trade-lot accounting: does the colony hold this "Res:Kg;..." lot (solids
	// across building stores + pool; fluids from the pool)? And spend it,
	// draining solids from building stores in ascending-Id order (deterministic).
	bool HasTradeLot(const TMap<FName, double>& Lot) const;
	void SpendTradeLot(const TMap<FName, double>& Lot);
	// Band boundary thresholds (up>down = the hysteresis gap). Index 0 is the
	// Failing|Strained edge, 3 is the Thriving|Flourishing edge.
	double HopeBandUp[4]   = { 28.0, 45.0, 75.0, 90.0 };
	double HopeBandDown[4] = { 20.0, 35.0, 70.0, 85.0 };
	// The garden (M2 Gate C; PlantedCells serialized, save v12). Keys are
	// (Level, CellIndex, 0). ProducingCells is per-step derived, never saved.
	TSet<FIntVector> PlantedCells;
	// Crops (save v26): per-cell crop state, per-floor climate settings,
	// ducted floors, the colony CO2 pool, and the deterministic rotation
	// counter. A planted cell absent from PlantedCrops is a legacy cell.
	TMap<FIntVector, FRHPlantedCropState> PlantedCrops;
	TMap<int32, FName> FloorClimate; // absent = Temperate
	TSet<int32> DuctedFloors;
	double ColonyCO2Kg = 0.0;
	int32 PlantRotation = 0;
	bool bSoilSpentAnnounced = false; // runtime edge, never saved
	// Crop economy scalars (Gate B/D; all linear or threshold-on-monotone -
	// era-parity-safe by shape; numbers legible-math placeholders).
	double CropSoilDepleteKgPerSol = 25.0;
	double FertilizerSoilPerKg = 2.0;
	double CompostKgPerColonistSol = 1.0;
	double SpoilChemFraction = 0.02;
	double ClimateMismatchYieldMul = 0.6;
	double DuctYieldBonusMul = 1.1;
	double GardenCO2DrawKgPerSolPerCell = 0.5;
	double GardenO2EmitKgPerSolPerCell = 0.2;
	double CO2KgPerColonistSol = 1.0;
	int32 ProducingCells = 0;
	bool bFirstCropAnnounced = false;   // serialized: the milestone fires once
	bool bGardenThirstAnnounced = false; // runtime edge: water-starve alert
	void StepAgriculture(float SubDt);
	double GardenSoilKgPerCell = 250.0;
	double GardenSeedsKgPerCell = 50.0;
	double GardenFoodKgPerSolPerCell = 1.0;
	double GardenWaterKgPerSolPerCell = 4.0;
	// Garden power fork (M2 Gate D+): a planted GARDEN cell runs grow-lights
	// (steady yield, costs battery energy - dark => dormant when the bank can't
	// pay); a GREENHOUSE cell trades that for glass-glazed near-free power whose
	// yield rides the solar curve and needs a shallow floor. Both are "garden
	// functions" for planting/forfeit; growth branches on which. All per-step
	// derived - the room designation (saved) carries the type, so no new save state.
	double GardenGrowLightWPerCell = 60.0;
	double GreenhouseGlassKgPerCell = 200.0;
	int32 DomeMaxBedsPerSide = 3;          // config DomeMaxBedsPerSide (Agri Gate C)
	int32 GreenhouseMinLevel = -1;
	double GardenPowerDrawW = 0.0;      // grow-light load this step (deck read)
	bool bGrowLightsDark = false;       // last step the bank couldn't power the lights
	bool bGardenDarkAnnounced = false;  // runtime edge: one alert per blackout episode
	// The last sim-second the grow-lights were dark. A bank hovering near empty
	// at night flips the lights every step, and each flip used to be a new
	// "episode": the first-hour playtest logged 12,524 GROW-LIGHTS DARK alerts
	// in 40 sols. The alert now re-arms only after a full sol fully lit.
	// Runtime only (the alert channel; garden yield is untouched).
	double GardenLastDarkSimSeconds = -1.0e12;
	// A room function that grows crops (Garden = grow-lit, Greenhouse = glazed).
	bool IsGardenFunction(FName Fn) const
	{
		static const FName NGarden(TEXT("Garden")), NGreenhouse(TEXT("Greenhouse"));
		return Fn == NGarden || Fn == NGreenhouse;
	}
	// The water loop (M2 Gate D+). Colonists draw potable water (part of the
	// life-support contract, like O2/Food); recycling reclaims most but degrades
	// POTABILITY - a single 0..1 pool-quality scalar that decays as the crew
	// recycles and is restored only by FRESH ice-melt makeup (linear in kg, so
	// the agent band and era band agree). Low potability saps Hope (the first
	// scarcity-driven Hope input) - the mechanical reason to drill the ice caps.
	double WaterPotability = 1.0;       // serialized (save v14)
	double FreshWaterThisStepKg = 0.0;  // ice-melt inflow this step (per-step derived)
	bool bWaterQualityAnnounced = false; // runtime edge: one grim-water alert per episode
	double ColonistWaterKgPerSol = 2.0;
	double GreywaterReturnFraction = 0.85;
	double WaterPotabilityDecayPerSol = 0.08;
	double WaterPotabilityRestorePerKg = 0.0008;
	double WaterPotabilityFloor = 0.6;
	double HopeWaterPenalty = 12.0;
	// Food runway (D4a). Derived from the pooled Food stock - nothing here is saved.
	double HopeFoodPenalty = 10.0;
	double FoodRunwayFloorSols = 10.0;
	double FoodRunwayRearmSols = 15.0;
	bool bFoodLowAnnounced = false; // runtime edge, re-derived on load (never saved)
	double IceLowAlertFraction = 0.25;  // D2a, RH_Config (0 disables)
	double IceWaterPlantNear_m = 100.0; // D2a, RH_Config: a Water Plant this close counts as "beside" the drill
	// D2a: resources construction draws from stores (Struct plus every
	// building / furnishing / fitting bill line). Only these overflow to a
	// store when their consumer's hopper is full; anything else (drill ice)
	// waits at its producer for the consumer. Built from data at begin-play.
	TSet<FName> ConstructionMaterials;
	// D2a runtime latch: deposit ids already announced. Never saved;
	// re-derived on load from the loaded masses.
	TSet<int32> IceLowAnnounced;
	// Comforts (M2 Gate D, abstract). Supplied count is per-step derived,
	// never saved (the pool stock is the state).
	int32 ComfortsSupplied = 0;
	double LuxuryKgPerColonistPerSol = 0.2;
	double HopeLuxuryBonus = 8.0;
	// DT_Config: ShaftSpoilKgPerFloor (bore-column regolith per floor descended),
	// SpoilKgPerCell (~1200 kg per 10x10 cell carved, underground proposal §2).
	double ShaftSpoilKgPerFloor = 1200.0;
	double SpoilKgPerCell = 1200.0;
	int32 NextBuildingId = 1;
	int32 NextTaskId = 1;
	bool bFleetDeployed = false;
	// Fleet composition ledger (row name -> live count), maintained by
	// SpawnRobotTracked; feeds era-mode aggregate rates and the save payload.
	TMap<FName, int32> FleetCounts;
	double AutosaveEverySols = 0.0;
	int32 LastAutosaveSol = -1;

	// Quota arc state
	ERHQuotaPhase QuotaPhase = ERHQuotaPhase::Open;
	double AwardMassKg = 0.0;
	TArray<FName> ManifestItems;
	double ShipArrivalSimSeconds = 0.0;
	int32 QuotaMetSol = 0;
	// The goal ladder (save v25). ActiveQuota derives to the first
	// slice-active row when none is set (fresh colony or legacy path).
	FName ActiveQuota;
	int32 QuotaOpenedSol = 0;
	// The goal ladder's renewal count (save v32): 0 while authored rows remain;
	// +1 each time the last rung reopens. Bills scale by QuotaRepeatBillMul^round;
	// deadline and award do not.
	int32 QuotaRepeatRound = 0;
	double QuotaRepeatBillMul = 1.25;   // config QuotaRepeatBillMul; <= 0 = no repeat
	FString DescribeQuotaBills() const;
	double PendingResearchFundingKg = 0.0;
	FName FirstQuotaName() const;
	void AdvanceQuotaLadder();
	// Ending resolution (save v25).
	uint8 DeclaredEnding = 0;      // ERHEnding recorded at PATH IS SET
	double PathSustainSols = 0.0;  // sols the projected ending has held since
	bool bEpilogueDeclared = false;
	bool bCollapseDeclared = false;
	double EpilogueSustainSols = 15.0;        // config EpilogueSustainSols
	// Long-game weather (config WeatherCycleSols, 0 = off): past the authored
	// schedule, events repeat on this period - deterministic, data-driven.
	double WeatherCycleSols = 0.0;
	// Station tiers (Gate A). Derived with ColonistJobs (never saved): the sum
	// of YieldMul over FILLED seats per family. All-default content makes
	// these equal the plain seat counts.
	double LabYieldSeatSum = 0.0;
	double WorkstationYieldSeatSum = 0.0;
	double WorkstationFabBonusPerSeat = 0.0;  // config, 0 until the director arms it
	double InfirmaryEvacBonusSols = 1.0;      // config InfirmaryEvacBonusSols

	UPROPERTY() TObjectPtr<URHDefinitionsSubsystem> Defs;
	UPROPERTY() TObjectPtr<URHSimClockSubsystem> Clock;
};
